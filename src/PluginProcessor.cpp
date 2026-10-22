#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <utility>

using APVTS = juce::AudioProcessorValueTreeState;

namespace {
juce::String pctText(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }
juce::String hzText(float v, int) {
    return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
}
juce::String msText(float v, int) { return juce::String(v, 1) + " ms"; }
juce::String secText(float v, int) { return juce::String(v, 2) + " s"; }
juce::String dbText(float v, int)  { return juce::String(v, 1) + " dB"; }

// Tempo-synced pre-delay divisions, ordered shortest to longest.
// Each entry is a length in quarter notes ("beats" in JUCE's terms).
struct Division { const char* name; double beats; };
const Division kDivisions[] = {
    { "1/32",  0.125 },
    { "1/16T", 1.0 / 6.0 },
    { "1/16",  0.25 },
    { "1/8T",  1.0 / 3.0 },
    { "1/16D", 0.375 },
    { "1/8",   0.5 },
    { "1/4T",  2.0 / 3.0 },
    { "1/8D",  0.75 },
    { "1/4",   1.0 },
    { "1/4D",  1.5 },
    { "1/2",   2.0 },
    { "1/1",   4.0 },
};
constexpr int kNumDivisions = (int) (sizeof(kDivisions) / sizeof(kDivisions[0]));
constexpr int kDefaultDivision = 2; // 1/16
constexpr int kDefaultDelayDivision = 7; // 1/8D, the classic dub echo

// Every parameter the processor reads, by a fixed internal id. Resolved to
// getParameters() indices once in the constructor, so the audio thread
// never looks anything up by name. LFO parameters must stay grouped four
// per LFO in this order: shape, rate, sync, division.
enum Pid {
    pDrive, pTailDrive, pPredelay, pSync, pDiv, pSwing,
    pDelayMix, pDelayTime, pDelayDiv, pDelayFb, pDelayPP,
    pFilterType, pFilterHz, pFilterQ, pEnhance,
    pEqLow, pEqMid, pEqMidHz, pEqHigh,
    pSize, pDecay, pDamp, pLowCut, pTone, pDiffusion, pMod, pModRate,
    pDuck, pDuckThresh, pDuckAtk, pDuckRel, pTrigger,
    pWidth, pMonoBelow, pOutput,
    pDriveMix, pReverbMix,
    pLfo1Shape, pLfo1Rate, pLfo1Sync, pLfo1Div,
    pLfo2Shape, pLfo2Rate, pLfo2Sync, pLfo2Div,
    pDriveOn, pReverbOn, pTimingOn, pDelayOn, pFilterOn, pEqOn, pDuckOn, pOutputOn,
    pLfo1On, pLfo2On,
    kNumPids
};
const char* const kPidNames[kNumPids] = {
    "drive", "taildrive", "predelay", "sync", "div", "swing",
    "delaymix", "delaytime", "delaydiv", "delayfb", "delaypp",
    "filtertype", "filterhz", "filterq", "enhance",
    "eqlow", "eqmid", "eqmidhz", "eqhigh",
    "size", "decay", "damp", "lowcut", "tone", "diffusion", "mod", "modrate",
    "duck", "duckthresh", "duckatk", "duckrel", "trigger",
    "width", "monobelow", "output",
    "drivemix", "reverbmix",
    "lfo1shape", "lfo1rate", "lfo1sync", "lfo1div",
    "lfo2shape", "lfo2rate", "lfo2sync", "lfo2div",
    "driveon", "reverbon", "timingon", "delayon", "filteron", "eqon", "duckon", "outputon",
    "lfo1on", "lfo2on",
};
constexpr int kLfoParamStride = pLfo2Shape - pLfo1Shape;

// How many sub-blocks the audio is cut into while anything is modulated:
// the LFOs are re-evaluated every this many samples.
constexpr int kModBlock = 32;

// Presets. Each lists only the parameters it changes; everything else is reset
// to its default first, so a preset always lands somewhere predictable. Any
// LFO assignments are listed separately.
struct Setting { const char* id; float value; };
struct ModSetting { int lfo; const char* id; float depth; };
struct Preset  { const char* name; std::vector<Setting> settings; std::vector<ModSetting> mods {}; };

const std::vector<Preset>& presets() {
    static const std::vector<Preset> p = {
        { "Init", {} },

        { "Warehouse", {
            { "drive", 3.0f }, { "taildrive", 4.0f },
            { "size", 0.30f }, { "decay", 5.0f }, { "damp", 800.0f },
            { "lowcut", 35.0f }, { "tone", 320.0f }, { "mod", 0.30f },
            { "duck", 0.90f }, { "duckrel", 220.0f }, { "predelay", 14.0f },
            { "width", 1.30f }, { "enhance", 0.25f } } },

        { "Tight Room", {
            { "drive", 2.0f }, { "taildrive", 2.5f },
            { "size", 0.15f }, { "decay", 2.2f }, { "damp", 1100.0f },
            { "lowcut", 40.0f }, { "tone", 420.0f }, { "mod", 0.20f },
            { "duck", 0.85f }, { "duckrel", 140.0f }, { "predelay", 8.0f },
            { "width", 1.0f } } },

        { "Sub Roller", {
            { "drive", 2.5f }, { "taildrive", 3.0f },
            { "size", 0.25f }, { "decay", 6.0f }, { "damp", 500.0f },
            { "lowcut", 28.0f }, { "tone", 220.0f }, { "mod", 0.25f },
            { "duck", 0.95f }, { "duckrel", 260.0f }, { "enhance", 0.55f },
            { "eqlow", 3.0f }, { "monobelow", 180.0f } } },

        { "Basement Distortion", {
            { "drive", 7.0f }, { "taildrive", 12.0f },
            { "size", 0.35f }, { "decay", 4.5f }, { "damp", 900.0f },
            { "lowcut", 38.0f }, { "tone", 500.0f }, { "mod", 0.35f },
            { "duck", 0.90f }, { "duckrel", 200.0f }, { "enhance", 0.40f },
            { "eqmid", -3.0f }, { "eqmidhz", 700.0f }, { "output", -4.0f } } },

        { "Offbeat Shuffle", {
            { "drive", 3.0f }, { "taildrive", 4.0f },
            { "size", 0.28f }, { "decay", 3.5f }, { "damp", 950.0f },
            { "lowcut", 34.0f }, { "tone", 360.0f }, { "duck", 0.90f },
            { "duckrel", 170.0f }, { "sync", 1.0f },
            { "div", (float) kDefaultDivision }, { "swing", 0.58f } } },

        { "Cavern", {
            { "drive", 2.0f }, { "taildrive", 2.0f },
            { "size", 0.70f }, { "decay", 12.0f }, { "damp", 700.0f },
            { "lowcut", 45.0f }, { "tone", 300.0f }, { "mod", 0.55f },
            { "duck", 0.95f }, { "duckrel", 350.0f }, { "predelay", 30.0f },
            { "width", 1.6f } } },

        { "Filter Sweep", {
            { "drive", 3.0f }, { "taildrive", 5.0f },
            { "size", 0.30f }, { "decay", 5.0f }, { "damp", 1400.0f },
            { "lowcut", 32.0f }, { "tone", 900.0f }, { "duck", 0.88f },
            { "filterhz", 500.0f }, { "filterq", 4.5f }, { "filtertype", 0.0f } } },

        { "Clean Tail", {
            { "drive", 1.2f }, { "taildrive", 1.0f },
            { "size", 0.25f }, { "decay", 3.0f }, { "damp", 1600.0f },
            { "lowcut", 30.0f }, { "tone", 600.0f }, { "mod", 0.15f },
            { "duck", 0.75f }, { "duckrel", 180.0f },
            // This preset used to sit at 70% Mix; with the output always wet
            // the restraint has to come from the tail itself and the trim.
            { "output", -3.0f } } },

        { "Dotted Echo", {
            { "drive", 3.0f }, { "taildrive", 4.0f },
            { "size", 0.25f }, { "decay", 3.0f }, { "damp", 900.0f },
            { "lowcut", 38.0f }, { "tone", 380.0f }, { "duck", 0.90f },
            { "duckrel", 190.0f }, { "sync", 1.0f },
            { "delaymix", 0.45f }, { "delaydiv", (float) kDefaultDelayDivision },
            { "delayfb", 0.45f }, { "delaypp", 1.0f } } },

        // Shows off the LFOs: a slow synced sweep of the filter, and a
        // smooth random drift of the stereo width.
        { "Breathing Sweep", {
            { "drive", 3.0f }, { "taildrive", 5.0f },
            { "size", 0.30f }, { "decay", 5.0f }, { "damp", 1300.0f },
            { "lowcut", 34.0f }, { "tone", 900.0f }, { "duck", 0.88f },
            { "filtertype", 1.0f }, { "filterhz", 650.0f }, { "filterq", 3.0f },
            { "lfo1shape", 0.0f }, { "lfo1sync", 1.0f }, { "lfo1div", 2.0f },     // sine, 2 bars
            { "lfo2shape", 6.0f }, { "lfo2sync", 1.0f }, { "lfo2div", 3.0f } },   // smooth random, 1 bar
          { { 0, "filterhz", 0.22f }, { 1, "width", 0.25f } } },
    };
    return p;
}
} // namespace

APVTS::ParameterLayout RumbleAudioProcessor::createLayout() {
    using P = juce::AudioParameterFloat;
    using ID = juce::ParameterID;
    APVTS::ParameterLayout layout;

    auto add = [&](const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                   std::function<juce::String(float, int)> fmt) {
        layout.add(std::make_unique<P>(ID { id, 1 }, name, r, def,
                                       juce::AudioParameterFloatAttributes().withStringFromValueFunction(std::move(fmt))));
    };

    add("drive",    "Drive",      { 1.0f, 10.0f, 0.0f, 0.5f },    3.0f,  [](float v, int) { return juce::String(v, 2); });
    add("taildrive","Tail Drive", { 1.0f, 20.0f, 0.0f, 0.4f },    2.0f,  [](float v, int) { return juce::String(v, 2); });
    add("predelay", "Pre-Delay",  { 0.0f, 200.0f, 0.0f, 0.5f },   12.0f, msText);
    add("size",     "Size",       { 0.0f, 1.0f },                 0.30f, pctText);
    add("decay",    "Decay",      { 0.2f, 30.0f, 0.0f, 0.35f },   4.0f,  secText);
    add("damp",     "Damping",    { 100.0f, 8000.0f, 0.0f, 0.3f },900.0f, hzText);
    add("lowcut",   "Low Cut",    { 20.0f, 400.0f, 0.0f, 0.4f },  32.0f, hzText);
    add("tone",     "Tone",       { 60.0f, 5000.0f, 0.0f, 0.3f }, 350.0f, hzText);
    add("diffusion","Diffusion",  { 0.0f, 1.0f },                 0.70f, pctText);
    add("mod",      "Modulation", { 0.0f, 1.0f },                 0.30f, pctText);
    add("modrate",  "Mod Rate",   { 0.05f, 8.0f, 0.0f, 0.35f },   1.0f,
        [](float v, int) { return juce::String(v, 2) + " x"; });
    add("duck",     "Duck",       { 0.0f, 1.0f },                 0.90f, pctText);
    // Trigger level that reaches full duck depth. -6 dB matches how the
    // ducker behaved before this control existed, so old sessions (which
    // restore missing parameters to their default) sound the same.
    add("duckthresh","Duck Threshold",{ -48.0f, 0.0f },           -6.0f, dbText);
    add("duckatk",  "Duck Attack",{ 0.1f, 50.0f, 0.0f, 0.4f },    1.5f,  msText);
    add("duckrel",  "Duck Release",{ 10.0f, 1000.0f, 0.0f, 0.4f },200.0f, msText);
    add("width",    "Width",      { 0.0f, 2.0f },                 1.30f, pctText);
    add("monobelow","Mono Below", { 20.0f, 1000.0f, 0.0f, 0.4f }, 150.0f, hzText);
    add("output",   "Output",     { -24.0f, 12.0f },              0.0f,  dbText);

    // When Sync is on, Pre-Delay is driven by the host tempo instead of the
    // millisecond knob, so the rumble keeps its place on the grid.
    layout.add(std::make_unique<juce::AudioParameterBool>(ID { "sync", 1 }, "Sync", false));

    // Echo delay on the tail. Off at 0%, so it changes nothing until used.
    // Like Pre-Delay it follows Sync: Delay Division when on, Delay Time
    // when off.
    add("delaymix",  "Delay",          { 0.0f, 1.0f },                0.0f,   pctText);
    add("delaytime", "Delay Time",     { 1.0f, 2000.0f, 0.0f, 0.4f }, 375.0f, msText);
    add("delayfb",   "Delay Feedback", { 0.0f, rumble::kMaxEchoFeedback }, 0.35f, pctText);
    layout.add(std::make_unique<juce::AudioParameterBool>(ID { "delaypp", 1 }, "Delay Ping-Pong", false));

    // What drives the ducker. Auto used to be the only behaviour and it could
    // switch sources mid-performance, which sounded like the plugin changing
    // its own settings, so the default is now Input. Auto stays available
    // for anyone who wants the old behaviour.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ID { "trigger", 1 }, "Trigger",
        juce::StringArray { "Auto", "Input", "Sidechain" }, 1));

    juce::StringArray divNames;
    for (const auto& d : kDivisions) divNames.add(d.name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "div", 1 }, "Division",
                                                            divNames, kDefaultDivision));
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "delaydiv", 1 }, "Delay Division",
                                                            divNames, kDefaultDelayDivision));

    add("filterhz", "Filter",     { 20.0f, 20000.0f, 0.0f, 0.25f }, 20000.0f, hzText);
    add("filterq",  "Resonance",  { 0.5f, 12.0f, 0.0f, 0.4f },     0.7f,
        [](float v, int) { return juce::String(v, 2); });
    add("enhance",  "Enhance",    { 0.0f, 1.0f },                  0.0f,  pctText);
    // Named for what they do here rather than Low/Mid/High: this plugin only
    // outputs roughly 20-800 Hz, so all three bands live in the bass.
    add("eqlow",    "Weight",     { -18.0f, 18.0f },               0.0f,  dbText);
    add("eqmid",    "Body",       { -18.0f, 18.0f },               0.0f,  dbText);
    add("eqmidhz",  "Body Freq",  { 60.0f, 1200.0f, 0.0f, 0.5f },  220.0f, hzText);
    add("eqhigh",   "Edge",       { -18.0f, 18.0f },               0.0f,  dbText);

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ID { "filtertype", 1 }, "Filter Type",
        juce::StringArray { "Low Pass 12", "Low Pass 24", "Low Pass 48",
                            "High Pass 12", "High Pass 24", "High Pass 48",
                            "Band Pass 12", "Band Pass 24",
                            "Notch", "Deep Notch", "All Pass" }, 0));

    // 0.5 = straight, 0.667 = the late slot on the triplet, 0.75 = maximum.
    add("swing", "Swing", { 0.5f, 0.75f }, 0.5f,
        [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; });

    // Wet/dry for DRIVE and REVERB. 100% is the plugin as it always was.
    add("drivemix",  "Drive Mix",  { 0.0f, 1.0f }, 1.0f, pctText);
    add("reverbmix", "Reverb Mix", { 0.0f, 1.0f }, 1.0f, pctText);

    // Section switches -- the lamps beside each section's name. Off passes
    // that section's audio through untouched.
    for (auto [id, name] : { std::pair<const char*, const char*> { "driveon", "Drive On" },
                             { "reverbon", "Reverb On" }, { "timingon", "Timing On" },
                             { "delayon", "Delay On" },   { "filteron", "Filter On" },
                             { "eqon", "EQ On" },         { "duckon", "Duck On" },
                             { "outputon", "Output On" }, { "lfo1on", "LFO 1 On" },
                             { "lfo2on", "LFO 2 On" } })
        layout.add(std::make_unique<juce::AudioParameterBool>(ID { id, 1 }, name, true));

    // Two LFOs. What they move, and by how much, is the modulation matrix,
    // which lives in the plugin state rather than in parameters.
    juce::StringArray shapes, lfoDivs;
    for (auto* n : rumble::kLfoShapeNames) shapes.add(n);
    for (const auto& d : rumble::kLfoDivisions) lfoDivs.add(d.name);
    for (int k = 1; k <= kNumLfos; ++k) {
        const juce::String n(k);
        layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "lfo" + n + "shape", 1 }, "LFO " + n + " Shape", shapes, 0));
        add(("lfo" + n + "rate").toRawUTF8(), ("LFO " + n + " Rate").toRawUTF8(),
            { 0.01f, 20.0f, 0.0f, 0.3f }, 0.5f,
            [](float v, int) { return v < 1.0f ? juce::String(v, 2) + " Hz" : juce::String(v, 1) + " Hz"; });
        layout.add(std::make_unique<juce::AudioParameterBool>(ID { "lfo" + n + "sync", 1 }, "LFO " + n + " Sync", true));
        layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "lfo" + n + "div", 1 }, "LFO " + n + " Division",
                                                                lfoDivs, rumble::kDefaultLfoDivision));
    }

    return layout;
}

RumbleAudioProcessor::RumbleAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
    for (auto* param : getParameters()) {
        auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
        jassert(ranged != nullptr);
        paramsByIndex.push_back(ranged);
        rawByIndex.push_back(apvts.getRawParameterValue(ranged->getParameterID()));
        // Continuous knobs only; switches and menus are not modulated, and
        // an LFO is never allowed to move its own (or the other's) settings.
        modulatable.push_back(dynamic_cast<juce::AudioParameterFloat*>(ranged) != nullptr
                              && ! ranged->getParameterID().startsWith("lfo"));
    }
    jassert((int) paramsByIndex.size() <= kMaxParams);

    paramIndexById.resize(kNumPids);
    for (int i = 0; i < kNumPids; ++i) {
        auto* param = apvts.getParameter(kPidNames[i]);
        jassert(param != nullptr);
        paramIndexById[(size_t) i] = param->getParameterIndex();
    }

    for (auto& lfoDepths : modDepth)
        for (auto& d : lfoDepths) d.store(0.0f);
    for (auto& v : lfoValueUi) v.store(0.0f);
    for (auto& v : lfoPhaseUi) v.store(0.0);

    markPresetLoaded("Init");
}

bool RumbleAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto main = layouts.getMainOutputChannelSet();
    if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono())
        return false;
    if (layouts.getMainInputChannelSet() != main)
        return false;

    const auto sc = layouts.getChannelSet(true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void RumbleAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate, samplesPerBlock);
    scBuffer.setSize(1, samplesPerBlock, false, true, true);
    monoScratch.setSize(1, samplesPerBlock, false, true, true);
    inputScratch.setSize(1, samplesPerBlock, false, true, true);
    gainScratch.setSize(1, samplesPerBlock, false, true, true);
    scopeFrameLength = juce::jmax(1, juce::roundToInt(sampleRate / kScopeFramesPerSecond));
    scopeAcc = {};
    scopeCount = 0;
    currentSampleRate = sampleRate;
    for (auto& lfo : lfos) lfo.prepare(sampleRate);
    const float still[kNumLfos] = {};
    pullParams(still);
}

// A parameter's value, moved by the LFOs if any are assigned to it.
float RumbleAudioProcessor::paramValue(int which, const float* lfoNow) {
    const int i = paramIndexById[(size_t) which];
    const float raw = rawByIndex[(size_t) i]->load(std::memory_order_relaxed);
    if (! anyModulation.load(std::memory_order_relaxed) || ! modulatable[(size_t) i])
        return raw;

    float depth[kNumLfos];
    bool moved = false;
    for (int k = 0; k < kNumLfos; ++k) {
        depth[k] = modDepth[(size_t) k][(size_t) i].load(std::memory_order_relaxed);
        moved = moved || std::abs(depth[k]) > 0.0f;
    }
    if (! moved)
        return raw;

    auto* param = paramsByIndex[(size_t) i];
    const float norm = rumble::modulateNormalised(param->convertTo0to1(raw), depth, lfoNow, kNumLfos);
    return param->convertFrom0to1(norm);
}

void RumbleAudioProcessor::pullParams(const float* lfoNow) {
    auto get = [&](int which) { return paramValue(which, lfoNow); };

    rumble::Params p;
    p.drive       = get(pDrive);
    p.tailDrive   = get(pTailDrive);
    p.predelayMs  = get(pPredelay);

    // Tempo sync: the engine works out the delay -- and swing -- per sample
    // from the timeline passed to process(), so it stays on the grid at any
    // buffer size.
    p.sync     = get(pSync) >= 0.5f;
    p.divBeats = (float) kDivisions[juce::jlimit(0, kNumDivisions - 1, (int) get(pDiv))].beats;
    p.swing    = get(pSwing);

    p.delayMix      = get(pDelayMix);
    p.delayMs       = get(pDelayTime);
    p.delayBeats    = (float) kDivisions[juce::jlimit(0, kNumDivisions - 1, (int) get(pDelayDiv))].beats;
    p.delayFeedback = get(pDelayFb);
    p.delayPingPong = get(pDelayPP) >= 0.5f;

    p.filterType  = (int) get(pFilterType);
    p.filterHz    = get(pFilterHz);
    p.filterQ     = get(pFilterQ);
    p.enhance     = get(pEnhance);
    p.eqLowDb     = get(pEqLow);
    p.eqMidDb     = get(pEqMid);
    p.eqMidHz     = get(pEqMidHz);
    p.eqHighDb    = get(pEqHigh);
    p.size        = get(pSize);
    p.decaySec    = get(pDecay);
    p.dampHz      = get(pDamp);
    p.lowCutHz    = get(pLowCut);
    p.toneHz      = get(pTone);
    p.diffusion   = get(pDiffusion);
    p.modDepth    = get(pMod);
    p.modRate     = get(pModRate);
    p.duckAmount  = get(pDuck);
    p.duckThreshDb= get(pDuckThresh);
    p.duckAtkMs   = get(pDuckAtk);
    p.duckRelMs   = get(pDuckRel);
    p.width       = get(pWidth);
    p.monoBelowHz = get(pMonoBelow);
    p.outGain     = juce::Decibels::decibelsToGain(get(pOutput));
    p.driveMix    = get(pDriveMix);
    p.driveOn     = get(pDriveOn)  >= 0.5f;
    p.reverbOn    = get(pReverbOn) >= 0.5f;
    p.timingOn    = get(pTimingOn) >= 0.5f;
    p.delayOn     = get(pDelayOn)  >= 0.5f;
    p.filterOn    = get(pFilterOn) >= 0.5f;
    p.eqOn        = get(pEqOn)     >= 0.5f;
    p.duckOn      = get(pDuckOn)   >= 0.5f;
    p.outputOn    = get(pOutputOn) >= 0.5f;
    p.reverbMix   = get(pReverbMix);
    engine.setParams(p);
}

void RumbleAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    rumble::Timeline timeline;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            // A host can report a nonsense tempo; the engine also checks, but
            // never hand it a NaN to begin with.
            if (const auto hostBpm = pos->getBpm())
                if (std::isfinite(*hostBpm) && *hostBpm > 0.0)
                    timeline.bpm = *hostBpm;
            // Swing needs to know where in the bar we are, which only means
            // anything while the transport is running.
            if (const auto hostPpq = pos->getPpqPosition()) {
                timeline.ppq = *hostPpq;
                timeline.valid = pos->getIsPlaying() && std::isfinite(timeline.ppq);
            }
        }
    }

    auto mainIO = getBusBuffer(buffer, false, 0);
    const int numSamples = mainIO.getNumSamples();
    const bool mono = mainIO.getNumChannels() < 2;

    // Mono hosts: duplicate into a scratch right channel so the FDN still runs
    // stereo, then fold back down on the way out. The scratch buffer is
    // preallocated -- resizing it here would allocate on the audio thread.
    float* l = mainIO.getWritePointer(0);
    float* r = nullptr;
    if (mono) {
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize(1, numSamples, false, false, true);
        monoScratch.copyFrom(0, 0, l, numSamples);
        r = monoScratch.getWritePointer(0);
    } else {
        r = mainIO.getWritePointer(1);
    }

    // What drives the ducker.
    //
    // Hosts frequently enable the sidechain bus with nothing routed to it.
    // Trusting an enabled-but-silent bus pegs the duck envelope at zero and
    // silently disables ducking, so Auto follows the sidechain only while it
    // actually carries signal. That switching is audible, so Input and
    // Sidechain let you pin the source and stop it changing on its own.
    const int trigger = (int) rawByIndex[(size_t) paramIndexById[pTrigger]]->load();
    const float* sc = nullptr;
    auto* scBus = getBus(true, 1);
    const bool scAvailable = scBus != nullptr && scBus->isEnabled();

    if (scAvailable && trigger != 1) {           // 1 = Input only
        auto scIn = getBusBuffer(buffer, true, 1);
        if (scIn.getNumChannels() > 0) {
            if (scBuffer.getNumSamples() < numSamples)
                scBuffer.setSize(1, numSamples, false, false, true);
            scBuffer.copyFrom(0, 0, scIn, 0, 0, numSamples);
            if (scIn.getNumChannels() > 1) {
                scBuffer.addFrom(0, 0, scIn, 1, 0, numSamples);
                scBuffer.applyGain(0.5f);
            }

            if (trigger == 2) {                  // 2 = Sidechain, no second guessing
                sidechainIsLive = true;
            } else {                             // 0 = Auto
                if (scBuffer.getMagnitude(0, 0, numSamples) > 1.0e-6f)
                    sidechainIsLive = true;
                else if (mainIO.getMagnitude(0, numSamples) > 1.0e-6f)
                    sidechainIsLive = false;
            }

            if (sidechainIsLive)
                sc = scBuffer.getReadPointer(0);
        }
    } else {
        sidechainIsLive = false;
    }

    // The scope wants the input as it was before processing overwrote it.
    // Skipped (never reallocated) if a host sends a bigger block than it
    // announced.
    const bool scope = numSamples <= inputScratch.getNumSamples();
    if (scope) {
        auto* in = inputScratch.getWritePointer(0);
        for (int i = 0; i < numSamples; ++i)
            in[i] = 0.5f * (l[i] + r[i]);
    }

    // While anything is modulated, run in short slices and move the LFOs
    // between them; otherwise one pass, exactly as without the LFOs.
    float* gain = scope ? gainScratch.getWritePointer(0) : nullptr;
    const int step = anyModulation.load(std::memory_order_relaxed) ? kModBlock : juce::jmax(1, numSamples);
    const double ppqPerSample = timeline.bpm / 60.0 / currentSampleRate;

    rumble::Lfo::Settings lfoSettings[kNumLfos];
    bool lfoOn[kNumLfos];
    for (int k = 0; k < kNumLfos; ++k)
        lfoOn[k] = rawByIndex[(size_t) paramIndexById[(size_t) (pLfo1On + k)]]->load() >= 0.5f;
    for (int k = 0; k < kNumLfos; ++k) {
        auto raw = [&](int which) { return rawByIndex[(size_t) paramIndexById[(size_t) (which + k * kLfoParamStride)]]->load(); };
        lfoSettings[k].shape = (rumble::LfoShape) juce::jlimit(0, rumble::kNumLfoShapes - 1, (int) raw(pLfo1Shape));
        lfoSettings[k].rateHz = raw(pLfo1Rate);
        lfoSettings[k].sync = raw(pLfo1Sync) >= 0.5f;
        lfoSettings[k].cycleBeats = rumble::kLfoDivisions[juce::jlimit(0, rumble::kNumLfoDivisions - 1, (int) raw(pLfo1Div))].beats;
    }

    for (int start = 0; start < numSamples; start += step) {
        const int n = juce::jmin(step, numSamples - start);
        rumble::Timeline slice = timeline;
        slice.ppq = timeline.ppq + (double) start * ppqPerSample;

        float lfoNow[kNumLfos];
        for (int k = 0; k < kNumLfos; ++k) {
            // A switched-off LFO keeps running (so it is in phase when it
            // comes back) but moves nothing.
            const float v = lfos[(size_t) k].next(lfoSettings[k], n, slice.valid, slice.ppq, slice.bpm);
            lfoNow[k] = lfoOn[k] ? v : 0.0f;
        }
        pullParams(lfoNow);

        engine.process(l + start, r + start, n, sc != nullptr ? sc + start : nullptr, &slice,
                       gain != nullptr ? gain + start : nullptr);
    }
    for (int k = 0; k < kNumLfos; ++k) {
        lfoValueUi[(size_t) k].store(lfos[(size_t) k].getLastValue(), std::memory_order_relaxed);
        lfoPhaseUi[(size_t) k].store(lfos[(size_t) k].getPhase(), std::memory_order_relaxed);
    }

    if (scope)
        pushScope(inputScratch.getReadPointer(0), l, r, gainScratch.getReadPointer(0), numSamples);

    if (mono) {
        juce::FloatVectorOperations::add(l, r, numSamples);
        mainIO.applyGain(0, 0, numSamples, 0.5f);
    }
}

void RumbleAudioProcessor::pushScope(const float* in, const float* outL, const float* outR,
                                     const float* gain, int n) {
    for (int i = 0; i < n; ++i) {
        scopeAcc.in   = juce::jmax(scopeAcc.in, std::abs(in[i]));
        scopeAcc.out  = juce::jmax(scopeAcc.out, std::abs(outL[i]), std::abs(outR[i]));
        scopeAcc.gain = juce::jmin(scopeAcc.gain, gain[i]);
        if (++scopeCount >= scopeFrameLength) {
            const auto w = scopeFifo.write(1);
            if (w.blockSize1 > 0) scopeFrames[(size_t) w.startIndex1] = scopeAcc;
            scopeAcc = {};
            scopeCount = 0;
        }
    }
}

int RumbleAudioProcessor::readScope(ScopeFrame* dest, int maxFrames) {
    const auto r = scopeFifo.read(juce::jmin(maxFrames, scopeFifo.getNumReady()));
    for (int i = 0; i < r.blockSize1; ++i) dest[i] = scopeFrames[(size_t) (r.startIndex1 + i)];
    for (int i = 0; i < r.blockSize2; ++i) dest[r.blockSize1 + i] = scopeFrames[(size_t) (r.startIndex2 + i)];
    return r.blockSize1 + r.blockSize2;
}

// The window size lives in the plugin state so the session reopens at the
// size you left it. Message thread only.
float RumbleAudioProcessor::getUiScale() const {
    return (float) apvts.state.getProperty("uiScale", 1.0f);
}

void RumbleAudioProcessor::setUiScale(float scale) {
    apvts.state.setProperty("uiScale", scale, nullptr);
}

int RumbleAudioProcessor::getNumPrograms() { return (int) presets().size(); }

const juce::String RumbleAudioProcessor::getProgramName(int index) {
    if (juce::isPositiveAndBelow(index, (int) presets().size()))
        return presets()[(size_t) index].name;
    return {};
}

void RumbleAudioProcessor::setCurrentProgram(int index) {
    if (! juce::isPositiveAndBelow(index, (int) presets().size()))
        return;

    currentProgram = index;
    const auto& preset = presets()[(size_t) index];

    // Start from the defaults so a preset never inherits stray values from
    // whatever was loaded before it.
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            ranged->setValueNotifyingHost(ranged->getDefaultValue());

    for (const auto& setting : preset.settings)
        if (auto* ranged = apvts.getParameter(setting.id))
            ranged->setValueNotifyingHost(ranged->convertTo0to1(setting.value));

    clearModulation();
    for (const auto& mod : preset.mods)
        if (auto* ranged = apvts.getParameter(mod.id))
            setModDepth(mod.lfo, ranged->getParameterIndex(), mod.depth);

    markPresetLoaded(preset.name);
    updateHostDisplay();
}

// ---------------------------------------------------------- user presets

juce::File RumbleAudioProcessor::userPresetDirectory() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("Fuzzy Audio")
               .getChildFile("Rumble")
               .getChildFile("Presets");
}

juce::Array<juce::File> RumbleAudioProcessor::userPresets() const {
    juce::Array<juce::File> files;
    const auto dir = userPresetDirectory();
    if (dir.isDirectory())
        files = dir.findChildFiles(juce::File::findFiles, false, "*.xml");

    // findChildFiles gives no guaranteed order; sort so the menu is stable.
    files.sort();
    return files;
}

juce::Result RumbleAudioProcessor::saveUserPreset(const juce::String& name) {
    const auto trimmed = name.trim();
    if (trimmed.isEmpty())
        return juce::Result::fail("Please enter a name.");

    // Strip anything the filesystem would reject rather than failing on it.
    const auto safe = juce::File::createLegalFileName(trimmed);
    if (safe.isEmpty())
        return juce::Result::fail("That name cannot be used for a file.");

    const auto dir = userPresetDirectory();
    const auto created = dir.createDirectory();
    if (created.failed())
        return created;

    auto state = apvts.copyState();
    writeModulation(state);
    auto xml = state.createXml();
    if (xml == nullptr)
        return juce::Result::fail("Could not read the current settings.");

    // Remember the display name: the filename may have been sanitised.
    // The window size belongs to the session, not to a preset.
    xml->setAttribute("presetName", trimmed);
    xml->removeAttribute("uiScale");

    const auto file = dir.getChildFile(safe + ".xml");
    if (! file.replaceWithText(xml->toString()))
        return juce::Result::fail("Could not write to " + file.getFullPathName());

    markPresetLoaded(trimmed);
    return juce::Result::ok();
}

bool RumbleAudioProcessor::loadUserPreset(const juce::File& file) {
    if (! file.existsAsFile())
        return false;

    auto xml = juce::parseXML(file);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return false;

    const auto name = xml->getStringAttribute("presetName", file.getFileNameWithoutExtension());

    // replaceState updates every parameter synchronously, so the snapshot
    // taken straight after reflects the preset.
    auto tree = juce::ValueTree::fromXml(*xml);
    tree.removeProperty("presetName", nullptr);
    tree.removeProperty("presetModified", nullptr);
    tree.setProperty("uiScale", getUiScale(), nullptr);   // keep the window as it is
    readModulation(tree);
    apvts.replaceState(tree);
    resyncParameters();
    markPresetLoaded(name);
    return true;
}

// replaceState skips any parameter whose stored value already matches -- and
// for a toggle "matches" means the same after rounding. A Sync parameter left
// at 0.49 by automation therefore stayed at 0.49 instead of returning to 0.
// Push every parameter to exactly the value in the state.
void RumbleAudioProcessor::resyncParameters() {
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param)) {
            const float target = ranged->convertTo0to1(
                apvts.getRawParameterValue(ranged->getParameterID())->load());
            if (std::abs(ranged->getValue() - target) > 1.0e-6f)
                ranged->setValueNotifyingHost(target);
        }
}

// --------------------------------------------------- preset "modified" state

// Everything "modified" is judged on: every parameter, then every depth.
std::vector<float> RumbleAudioProcessor::currentParameterValues() const {
    std::vector<float> v;
    for (auto* param : getParameters())
        v.push_back(param->getValue());
    for (const auto& lfoDepths : modDepth)
        for (size_t i = 0; i < paramsByIndex.size(); ++i)
            v.push_back(lfoDepths[i].load(std::memory_order_relaxed));
    return v;
}

// ------------------------------------------------------------ modulation

bool RumbleAudioProcessor::isModulatable(int paramIndex) const {
    return juce::isPositiveAndBelow(paramIndex, (int) modulatable.size()) && modulatable[(size_t) paramIndex];
}

float RumbleAudioProcessor::getModDepth(int lfo, int paramIndex) const {
    if (! juce::isPositiveAndBelow(lfo, kNumLfos) || ! isModulatable(paramIndex))
        return 0.0f;
    return modDepth[(size_t) lfo][(size_t) paramIndex].load(std::memory_order_relaxed);
}

void RumbleAudioProcessor::setModDepth(int lfo, int paramIndex, float depth) {
    if (! juce::isPositiveAndBelow(lfo, kNumLfos) || ! isModulatable(paramIndex))
        return;
    depth = juce::jlimit(-1.0f, 1.0f, std::isfinite(depth) ? depth : 0.0f);
    if (std::abs(depth) < 1.0e-4f) depth = 0.0f;
    modDepth[(size_t) lfo][(size_t) paramIndex].store(depth, std::memory_order_relaxed);

    bool any = false;
    for (const auto& lfoDepths : modDepth)
        for (size_t i = 0; i < paramsByIndex.size(); ++i)
            any = any || std::abs(lfoDepths[i].load(std::memory_order_relaxed)) > 0.0f;
    anyModulation.store(any);
}

void RumbleAudioProcessor::clearModulation() {
    for (auto& lfoDepths : modDepth)
        for (auto& d : lfoDepths) d.store(0.0f, std::memory_order_relaxed);
    anyModulation.store(false);
}

// Assignments are stored by parameter id, so they survive parameters being
// added or reordered in later versions.
void RumbleAudioProcessor::writeModulation(juce::ValueTree& state) const {
    state.removeChild(state.getChildWithName("MODULATION"), nullptr);
    juce::ValueTree mods("MODULATION");
    for (int k = 0; k < kNumLfos; ++k)
        for (size_t i = 0; i < paramsByIndex.size(); ++i) {
            const float d = modDepth[(size_t) k][i].load(std::memory_order_relaxed);
            if (std::abs(d) > 0.0f)
                mods.appendChild(juce::ValueTree("MOD")
                                     .setProperty("lfo", k, nullptr)
                                     .setProperty("param", paramsByIndex[i]->getParameterID(), nullptr)
                                     .setProperty("depth", d, nullptr), nullptr);
        }
    state.appendChild(mods, nullptr);
}

// Replace the matrix with what `state` holds (nothing, for older sessions),
// and take it out of the tree so it never ends up inside the APVTS state.
void RumbleAudioProcessor::readModulation(juce::ValueTree& state) {
    clearModulation();
    auto mods = state.getChildWithName("MODULATION");
    for (const auto& mod : mods)
        if (auto* param = apvts.getParameter(mod.getProperty("param").toString()))
            setModDepth((int) mod.getProperty("lfo", 0), param->getParameterIndex(),
                        (float) mod.getProperty("depth", 0.0f));
    if (mods.isValid())
        state.removeChild(mods, nullptr);
}

void RumbleAudioProcessor::markPresetLoaded(const juce::String& name) {
    const juce::ScopedLock sl(presetLock);
    presetName = name;
    presetSnapshot = currentParameterValues();
    presetRestoredModified = false;
}

juce::String RumbleAudioProcessor::getPresetName() const {
    const juce::ScopedLock sl(presetLock);
    return presetName;
}

bool RumbleAudioProcessor::isPresetModified() const {
    const juce::ScopedLock sl(presetLock);
    if (presetRestoredModified)
        return true;
    const auto now = currentParameterValues();
    if (now.size() != presetSnapshot.size())
        return true;
    for (size_t i = 0; i < now.size(); ++i)
        if (std::abs(now[i] - presetSnapshot[i]) > 1.0e-5f)
            return true;
    return false;
}

juce::AudioProcessorEditor* RumbleAudioProcessor::createEditor() { return new RumbleAudioProcessorEditor(*this); }

void RumbleAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    // The session remembers which preset it came from, and whether it had
    // been edited, so the editor can show the same thing after reloading.
    auto state = apvts.copyState();
    state.setProperty("presetName", getPresetName(), nullptr);
    state.setProperty("presetModified", isPresetModified(), nullptr);
    state.setProperty("program", currentProgram, nullptr);
    writeModulation(state);
    if (auto xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void RumbleAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName(apvts.state.getType()))
        return;

    auto tree = juce::ValueTree::fromXml(*xml);
    const auto name = tree.getProperty("presetName", "Init").toString();
    const bool modified = (bool) tree.getProperty("presetModified", false);
    currentProgram = juce::jlimit(0, getNumPrograms() - 1, (int) tree.getProperty("program", 0));
    tree.removeProperty("presetName", nullptr);
    tree.removeProperty("presetModified", nullptr);
    tree.removeProperty("program", nullptr);
    readModulation(tree);

    apvts.replaceState(tree);
    resyncParameters();
    markPresetLoaded(name);

    const juce::ScopedLock sl(presetLock);
    presetRestoredModified = modified;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RumbleAudioProcessor(); }
