// Tests that need the real plugin -- parameters, the modulation matrix, state
// -- rather than just the DSP core. Linked against the plugin's shared code,
// built as `rumble_plugin_tests` and run by CTest alongside rumble_tests.

#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int failures = 0;

void check(bool ok, const std::string& what, double measured) {
    std::printf("%s  %-62s (%.4g)\n", ok ? "PASS" : "FAIL", what.c_str(), measured);
    if (! ok) ++failures;
}

void set(RumbleAudioProcessor& p, const char* id, float value) {
    auto* param = p.apvts.getParameter(id);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

int indexOf(RumbleAudioProcessor& p, const char* id) {
    return p.apvts.getParameter(id)->getParameterIndex();
}

// An LFO on Output, with the reverb mixed fully dry so the output is just
// the input times Output: the level must swing between the two extremes.
float outputSwingDb(bool lfoOn) {
    RumbleAudioProcessor p;
    p.prepareToPlay(48000.0, 512);
    set(p, "reverbmix", 0.0f);
    set(p, "output", -12.0f);
    set(p, "lfo1shape", 4.0f);
    set(p, "lfo1sync", 0.0f);
    set(p, "lfo1rate", 2.0f);
    set(p, "lfo1on", lfoOn ? 1.0f : 0.0f);
    p.setModDepth(0, indexOf(p, "output"), 0.25f);
    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    float lo = 1.0e9f, hi = 0.0f;
    for (int b = 0; b < 200; ++b) {
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 0.5f, 512);
        p.processBlock(buffer, midi);
        if (b > 20) {
            const float g = buffer.getSample(0, 256) / 0.5f;
            lo = juce::jmin(lo, g);
            hi = juce::jmax(hi, g);
        }
    }
    return juce::Decibels::gainToDecibels(hi) - juce::Decibels::gainToDecibels(lo);
}

void testLfoSwitchStopsModulation() {
    const float off = outputSwingDb(false);
    check(off < 0.01f && outputSwingDb(true) > 17.0f,
          "switching LFO 1 off stops it moving its knobs (swing dB when off)", off);
}

void testLfoMovesAKnob() {
    RumbleAudioProcessor p;
    p.prepareToPlay(48000.0, 512);
    set(p, "reverbmix", 0.0f);
    set(p, "output", -12.0f);
    set(p, "lfo1shape", 4.0f);       // square
    set(p, "lfo1sync", 0.0f);
    set(p, "lfo1rate", 2.0f);        // 2 Hz: high for 0.25 s, low for 0.25 s
    p.setModDepth(0, indexOf(p, "output"), 0.25f);

    juce::AudioBuffer<float> buffer(2, 512);
    juce::MidiBuffer midi;
    float lo = 1.0e9f, hi = 0.0f;
    for (int b = 0; b < 200; ++b) {                    // ~2.1 s
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill(buffer.getWritePointer(ch), 0.5f, 512);
        p.processBlock(buffer, midi);
        if (b > 20) {                                   // past the first glide
            const float g = buffer.getSample(0, 256) / 0.5f;
            lo = juce::jmin(lo, g);
            hi = juce::jmax(hi, g);
        }
    }
    // Output spans -24..+12 dB; 25% of that is 9 dB either side of -12 dB.
    const float loDb = juce::Decibels::gainToDecibels(lo), hiDb = juce::Decibels::gainToDecibels(hi);
    check(std::abs(loDb + 21.0f) < 0.3f && std::abs(hiDb + 3.0f) < 0.3f,
          "square LFO swings Output -21 / -3 dB at 25% depth (low dB)", loDb);
}

void testModulationRoundTrips() {
    RumbleAudioProcessor a;
    a.setModDepth(0, indexOf(a, "filterhz"), 0.4f);
    a.setModDepth(1, indexOf(a, "decay"), -0.3f);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    RumbleAudioProcessor b;
    b.setStateInformation(state.getData(), (int) state.getSize());
    const bool ok = std::abs(b.getModDepth(0, indexOf(b, "filterhz")) - 0.4f) < 1.0e-6f
                 && std::abs(b.getModDepth(1, indexOf(b, "decay")) + 0.3f) < 1.0e-6f
                 && std::abs(b.getModDepth(0, indexOf(b, "decay"))) < 1.0e-9f;
    check(ok, "LFO assignments survive saving and reloading the session", ok ? 1.0 : 0.0);
    check(! b.isPresetModified() == ! a.isPresetModified(), "the edited marker survives too", 1.0);
}

void testPresetsSetModulation() {
    RumbleAudioProcessor p;
    p.setModDepth(0, indexOf(p, "decay"), 0.5f);
    check(p.isPresetModified(), "adding an assignment marks the preset edited", 1.0);

    p.setCurrentProgram(0);                                 // Init
    check(std::abs(p.getModDepth(0, indexOf(p, "decay"))) < 1.0e-9f && ! p.isPresetModified(),
          "loading a factory preset clears assignments", 1.0);

    for (int i = 0; i < p.getNumPrograms(); ++i)
        if (p.getProgramName(i) == "Breathing Sweep") p.setCurrentProgram(i);
    check(p.getModDepth(0, indexOf(p, "filterhz")) > 0.0f && p.getModDepth(1, indexOf(p, "width")) > 0.0f,
          "Breathing Sweep brings its own assignments", p.getModDepth(0, indexOf(p, "filterhz")));
}

void testSwitchesAreNotModulatable() {
    RumbleAudioProcessor p;
    p.setModDepth(0, indexOf(p, "sync"), 0.5f);
    p.setModDepth(0, indexOf(p, "lfo2rate"), 0.5f);
    const bool ok = std::abs(p.getModDepth(0, indexOf(p, "sync"))) < 1.0e-9f
                 && std::abs(p.getModDepth(0, indexOf(p, "lfo2rate"))) < 1.0e-9f;
    check(ok, "switches, menus and LFO settings cannot be modulated", ok ? 1.0 : 0.0);
}

} // namespace

int main() {
    juce::ScopedJuceInitialiser_GUI juce;   // parameters and value trees want a message manager
    testLfoMovesAKnob();
    testLfoSwitchStopsModulation();
    testModulationRoundTrips();
    testPresetsSetModulation();
    testSwitchesAreNotModulatable();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
