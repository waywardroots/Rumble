#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS = juce::AudioProcessorValueTreeState;

namespace {
juce::String pctText(float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; }
juce::String hzText(float v, int) {
    return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
}
juce::String msText(float v, int) { return juce::String(v, 1) + " ms"; }
juce::String secText(float v, int) { return juce::String(v, 2) + " s"; }
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

    add("mix",      "Mix",        { 0.0f, 1.0f },                 0.85f, pctText);
    add("drive",    "Drive",      { 1.0f, 10.0f, 0.0f, 0.5f },    3.0f,  [](float v, int) { return juce::String(v, 2); });
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
    add("output",   "Output",     { -24.0f, 12.0f },              0.0f,
        [](float v, int) { return juce::String(v, 1) + " dB"; });

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
    pullParams();
}

void RumbleAudioProcessor::pullParams() {
    auto get = [this](const char* id) { return apvts.getRawParameterValue(id)->load(); };

    rumble::Params p;
    p.mix         = get("mix");
    p.drive       = get("drive");
    p.predelayMs  = get("predelay");
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
    pullParams();

    auto mainIO = getBusBuffer(buffer, false, 0);
    const int numSamples = mainIO.getNumSamples();
    const bool mono = mainIO.getNumChannels() < 2;

    // Mono hosts: duplicate into a scratch right channel so the FDN still runs
    // stereo, then fold back down on the way out.
    juce::AudioBuffer<float> scratch;
    float* l = mainIO.getWritePointer(0);
    float* r = nullptr;
    if (mono) {
        scratch.setSize(1, numSamples, false, false, true);
        scratch.copyFrom(0, 0, l, numSamples);
        r = scratch.getWritePointer(0);
    } else {
        r = mainIO.getWritePointer(1);
    }

    // Optional external sidechain trigger.
    const float* sc = nullptr;
    if (auto* scBus = getBus(true, 1); scBus != nullptr && scBus->isEnabled()) {
        auto scIn = getBusBuffer(buffer, true, 1);
        if (scIn.getNumChannels() > 0) {
            scBuffer.setSize(1, numSamples, false, false, true);
            scBuffer.copyFrom(0, 0, scIn, 0, 0, numSamples);
            if (scIn.getNumChannels() > 1) {
                scBuffer.addFrom(0, 0, scIn, 1, 0, numSamples);
                scBuffer.applyGain(0.5f);
            }
            sc = scBuffer.getReadPointer(0);
        }
    }

    engine.process(l, r, numSamples, sc);

    if (mono) {
        juce::FloatVectorOperations::add(l, r, numSamples);
        mainIO.applyGain(0, 0, numSamples, 0.5f);
    }
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
