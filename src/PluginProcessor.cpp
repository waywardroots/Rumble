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
constexpr int kDefaultDelayDivision = 7; // 1/8D, the classic dub echo

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

        { "Dotted Echo", {
            { "drive", 3.0f }, { "taildrive", 4.0f },
            { "size", 0.25f }, { "decay", 3.0f }, { "damp", 900.0f },
            { "lowcut", 38.0f }, { "tone", 380.0f }, { "duck", 0.90f },
            { "duckrel", 190.0f }, { "sync", 1.0f },
            { "delaymix", 0.45f }, { "delaydiv", (float) kDefaultDelayDivision },
            { "delayfb", 0.45f }, { "delaypp", 1.0f } } },
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

    return layout;
}

RumbleAudioProcessor::RumbleAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts(*this, nullptr, "PARAMS", createLayout()) {
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
    pullParams();
}

void RumbleAudioProcessor::pullParams() {
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    rumble::Params p;
    p.drive       = get("drive");
    p.tailDrive   = get("taildrive");
    p.predelayMs  = get("predelay");

    // Tempo sync: the engine works out the delay -- and swing -- per sample
    // from the timeline passed to process(), so it stays on the grid at any
    // buffer size.
    p.sync     = get("sync") >= 0.5f;
    p.divBeats = (float) kDivisions[juce::jlimit(0, kNumDivisions - 1, (int) get("div"))].beats;
    p.swing    = get("swing");

    p.delayMix      = get("delaymix");
    p.delayMs       = get("delaytime");
    p.delayBeats    = (float) kDivisions[juce::jlimit(0, kNumDivisions - 1, (int) get("delaydiv"))].beats;
    p.delayFeedback = get("delayfb");
    p.delayPingPong = get("delaypp") >= 0.5f;

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
    p.diffusion   = get("diffusion");
    p.modDepth    = get("mod");
    p.modRate     = get("modrate");
    p.duckAmount  = get("duck");
    p.duckThreshDb= get("duckthresh");
    p.duckAtkMs   = get("duckatk");
    p.duckRelMs   = get("duckrel");
    p.width       = get("width");
    p.monoBelowHz = get("monobelow");
    p.outGain     = juce::Decibels::decibelsToGain(get("output"));
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

    pullParams();

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
    const int trigger = (int) apvts.getRawParameterValue("trigger")->load();
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

    engine.process(l, r, numSamples, sc, &timeline);

    // Keep the lowest gain until the editor collects it.
    const float g = engine.lastMinDuckGain();
    float prev = duckMeter.load(std::memory_order_relaxed);
    while (g < prev && ! duckMeter.compare_exchange_weak(prev, g)) {}

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

    auto xml = apvts.copyState().createXml();
    if (xml == nullptr)
        return juce::Result::fail("Could not read the current settings.");

    // Remember the display name: the filename may have been sanitised.
    xml->setAttribute("presetName", trimmed);

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

std::vector<float> RumbleAudioProcessor::currentParameterValues() const {
    std::vector<float> v;
    for (auto* param : getParameters())
        v.push_back(param->getValue());
    return v;
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

    apvts.replaceState(tree);
    resyncParameters();
    markPresetLoaded(name);

    const juce::ScopedLock sl(presetLock);
    presetRestoredModified = modified;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RumbleAudioProcessor(); }
