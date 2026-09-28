#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

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

// Presets. Each lists only the parameters it changes; everything else is reset
// to its default first, so a preset always lands somewhere predictable.
struct Setting { const char* id; float value; };
struct Preset  { const char* name; std::vector<Setting> settings; };

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
    add("mod",      "Modulation", { 0.0f, 1.0f },                 0.30f, pctText);
    add("duck",     "Duck",       { 0.0f, 1.0f },                 0.90f, pctText);
    add("duckatk",  "Duck Attack",{ 0.1f, 50.0f, 0.0f, 0.4f },    1.5f,  msText);
    add("duckrel",  "Duck Release",{ 10.0f, 1000.0f, 0.0f, 0.4f },200.0f, msText);
    add("width",    "Width",      { 0.0f, 2.0f },                 1.30f, pctText);
    add("monobelow","Mono Below", { 20.0f, 1000.0f, 0.0f, 0.4f }, 150.0f, hzText);
    add("output",   "Output",     { -24.0f, 12.0f },              0.0f,  dbText);

    // When Sync is on, Pre-Delay is driven by the host tempo instead of the
    // millisecond knob, so the rumble keeps its place on the grid.
    layout.add(std::make_unique<juce::AudioParameterBool>(ID { "sync", 1 }, "Sync", false));

    juce::StringArray divNames;
    for (const auto& d : kDivisions) divNames.add(d.name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(ID { "div", 1 }, "Division",
                                                            divNames, kDefaultDivision));

    // 0.5 = straight, 0.75 = fully swung (the late slot lands on the triplet).
    add("filterhz", "Filter",     { 20.0f, 20000.0f, 0.0f, 0.25f }, 20000.0f, hzText);
    add("filterq",  "Resonance",  { 0.5f, 12.0f, 0.0f, 0.4f },     0.7f,
        [](float v, int) { return juce::String(v, 2); });
    add("enhance",  "Enhance",    { 0.0f, 1.0f },                  0.0f,  pctText);
    add("eqlow",    "Low",        { -18.0f, 18.0f },               0.0f,  dbText);
    add("eqmid",    "Mid",        { -18.0f, 18.0f },               0.0f,  dbText);
    add("eqmidhz",  "Mid Freq",   { 80.0f, 4000.0f, 0.0f, 0.35f }, 400.0f, hzText);
    add("eqhigh",   "High",       { -18.0f, 18.0f },               0.0f,  dbText);

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        ID { "filtertype", 1 }, "Filter Type",
        juce::StringArray { "Low Pass", "Band Pass", "High Pass" }, 0));

    add("swing", "Swing", { 0.5f, 0.75f }, 0.5f,
        [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; });

    return layout;
}

RumbleAudioProcessor::RumbleAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {}

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
    pullParams(120.0, 0.0, false);
}

void RumbleAudioProcessor::pullParams(double bpm, double ppq, bool ppqValid) {
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    rumble::Params p;
    p.drive       = get("drive");
    p.tailDrive   = get("taildrive");
    p.predelayMs  = get("predelay");

    // Tempo sync overrides the millisecond knob.
    if (get("sync") >= 0.5f) {
        const int idx = juce::jlimit(0, kNumDivisions - 1, (int) get("div"));
        const double divBeats = kDivisions[idx].beats;
        const double quarterMs = 60000.0 / juce::jlimit(20.0, 999.0, bpm);
        double ms = divBeats * quarterMs;

        // Swing delays every second slot of the grid, the way an MPC does:
        // at 66% the late slot lands two thirds of the way through the pair.
        // It needs the timeline position to know which slot we are in, so it
        // only applies while the host is reporting a playhead.
        const double swing = get("swing");
        if (ppqValid && swing > 0.5) {
            const double slot = std::floor(ppq / divBeats);
            const bool lateSlot = ((long long) slot % 2) != 0;
            if (lateSlot)
                ms += (2.0 * swing - 1.0) * divBeats * quarterMs;
        }

        p.predelayMs = (float) juce::jlimit(0.0, (double) rumble::kMaxPredelayMs, ms);
    }
    p.filterType  = (int) get("filtertype");
    p.filterHz    = get("filterhz");
    p.filterQ     = get("filterq");
    p.enhance     = get("enhance");
    p.eqLowDb     = get("eqlow");
    p.eqMidDb     = get("eqmid");
    p.eqMidHz     = get("eqmidhz");
    p.eqHighDb    = get("eqhigh");
    p.size        = get("size");
    p.decaySec    = get("decay");
    p.dampHz      = get("damp");
    p.lowCutHz    = get("lowcut");
    p.toneHz      = get("tone");
    p.modDepth    = get("mod");
    p.duckAmount  = get("duck");
    p.duckAtkMs   = get("duckatk");
    p.duckRelMs   = get("duckrel");
    p.width       = get("width");
    p.monoBelowHz = get("monobelow");
    p.outGain     = juce::Decibels::decibelsToGain(get("output"));
    engine.setParams(p);
}

void RumbleAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    double bpm = 120.0;
    double ppq = 0.0;
    bool ppqValid = false;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            // A host can report a nonsense tempo; jlimit would pass a NaN
            // straight through into the pre-delay time.
            if (const auto hostBpm = pos->getBpm())
                if (std::isfinite(*hostBpm) && *hostBpm > 0.0)
                    bpm = *hostBpm;
            if (const auto hostPpq = pos->getPpqPosition()) {
                ppq = *hostPpq;
                ppqValid = std::isfinite(ppq);
            }
        }
    }

    pullParams(bpm, ppq, ppqValid);

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

    // Optional external sidechain trigger.
    //
    // Hosts frequently enable the sidechain bus even when the user has routed
    // nothing to it. Trusting an enabled-but-silent bus would peg the duck
    // envelope at zero, silently disabling the ducker and letting the tail
    // pile up across kicks -- so fall back to the main input unless the
    // sidechain actually carries signal.
    const float* sc = nullptr;
    if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled()) {
        auto scIn = getBusBuffer(buffer, true, 1);
        if (scIn.getNumChannels() > 0) {
            if (scBuffer.getNumSamples() < numSamples)
                scBuffer.setSize(1, numSamples, false, false, true);
            scBuffer.copyFrom(0, 0, scIn, 0, 0, numSamples);
            if (scIn.getNumChannels() > 1) {
                scBuffer.addFrom(0, 0, scIn, 1, 0, numSamples);
                scBuffer.applyGain(0.5f);
            }
            if (scBuffer.getMagnitude(0, 0, numSamples) > 1.0e-6f)
                sidechainIsLive = true;
            else if (mainIO.getMagnitude(0, numSamples) > 1.0e-6f)
                sidechainIsLive = false; // main input is playing, sidechain is not

            if (sidechainIsLive)
                sc = scBuffer.getReadPointer(0);
        }
    } else {
        sidechainIsLive = false;
    }

    engine.process(l, r, numSamples, sc);

    if (mono) {
        juce::FloatVectorOperations::add(l, r, numSamples);
        mainIO.applyGain(0, 0, numSamples, 0.5f);
    }
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

    updateHostDisplay();
}

juce::AudioProcessorEditor* RumbleAudioProcessor::createEditor() { return new RumbleAudioProcessorEditor(*this); }

void RumbleAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void RumbleAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RumbleAudioProcessor(); }
