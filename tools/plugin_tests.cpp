// Tests that need the real plugin -- parameters, the modulation matrix, state
// -- rather than just the DSP core. Linked against the plugin's shared code,
// built as `rumble_plugin_tests` and run by CTest alongside rumble_tests.

#include "../src/PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>

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

float rawValue(RumbleAudioProcessor& p, const char* id) { return p.apvts.getRawParameterValue(id)->load(); }

// Sessions saved before the Decay fix keep the tail length they really had.
void testOldSessionsKeepTheirDecay() {
    RumbleAudioProcessor a;
    set(a, "decay", 5.0f); set(a, "damp", 800.0f); set(a, "lowcut", 35.0f); set(a, "size", 0.3f);
    juce::MemoryBlock state;
    a.getStateInformation(state);

    // The same session as an older build would have saved it: no decayModel.
    auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), (int) state.getSize());
    xml->removeAttribute("decayModel");
    juce::MemoryBlock oldState;
    juce::AudioProcessor::copyXmlToBinary(*xml, oldState);

    RumbleAudioProcessor fresh, old;
    fresh.setStateInformation(state.getData(), (int) state.getSize());
    old.setStateInformation(oldState.getData(), (int) oldState.getSize());
    check(std::abs(rawValue(fresh, "decay") - 5.0f) < 0.01f, "a current session keeps its Decay as saved (s)", rawValue(fresh, "decay"));
    check(std::abs(rawValue(old, "decay") - 2.48f) < 0.1f,
          "an old session's Decay 5 s @ Damping 800 becomes the 2.5 s it really rang (s)", rawValue(old, "decay"));
}

void testOldUserPresetsKeepTheirDecay() {
    RumbleAudioProcessor p;
    set(p, "decay", 12.0f); set(p, "damp", 700.0f); set(p, "lowcut", 45.0f); set(p, "size", 0.7f);
    auto xml = p.apvts.copyState().createXml();   // no decayModel: an old preset file
    xml->setAttribute("presetName", "Old Cavern");
    const auto file = juce::File::createTempFile(".xml");
    file.replaceWithText(xml->toString());
    RumbleAudioProcessor q;
    q.loadUserPreset(file);
    file.deleteFile();
    check(std::abs(rawValue(q, "decay") - 4.12f) < 0.15f, "an old user preset is converted on load (s)", rawValue(q, "decay"));
}

// ------------------------------------------------------- factory presets

// A host transport running at a fixed tempo, so synced presets behave as
// they would in a DAW.
struct TestPlayHead : juce::AudioPlayHead {
    double bpm = 128.0, sampleRate = 48000.0;
    juce::int64 sample = 0;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo info;
        info.setBpm(bpm);
        info.setTimeInSamples(sample);
        info.setPpqPosition((double) sample / sampleRate * bpm / 60.0);
        info.setIsPlaying(true);
        return info;
    }
};

// BS.1770 K-weighting at 48 kHz: what loudness meters (LUFS) measure. Plain
// RMS would let a sub-heavy preset read louder than it sounds.
struct KWeight {
    double z[2][2] = {};
    double process(double x) {
        static const double b[2][3] = { { 1.53512485958697, -2.69169618940638, 1.19839281085285 },
                                        { 1.0, -2.0, 1.0 } };
        static const double a[2][2] = { { -1.69065929318241, 0.73248077421585 },
                                        { -1.99004745483398, 0.99007225036621 } };
        for (int st = 0; st < 2; ++st) {
            const double y = b[st][0] * x + z[st][0];
            z[st][0] = b[st][1] * x - a[st][0] * y + z[st][1];
            z[st][1] = b[st][2] * x - a[st][1] * y;
            x = y;
        }
        return x;
    }
};

struct PresetAudit { double peakDb, lufs; };

PresetAudit auditProgram(int program, float kickPeakDb = -6.0f) {
    constexpr double sr = 48000.0, bpm = 128.0;
    constexpr int block = 512;
    RumbleAudioProcessor p;
    TestPlayHead head;
    p.setPlayHead(&head);
    p.prepareToPlay(sr, block);
    p.setCurrentProgram(program);

    // An analogue-style kick on every beat, peaking at kickPeakDb (-6 dBFS by
    // default: a typical level for a drum bus feeding a send).
    const int beat = (int) std::lround(60.0 / bpm * sr);
    const int total = beat * 4 * 10;                     // 10 bars
    std::vector<float> kick((size_t) beat);
    double phase = 0.0, kickPeak = 0.0;
    for (int n = 0; n < beat; ++n) {
        const double t = n / sr;
        phase += 2.0 * juce::MathConstants<double>::pi * (45.0 + 130.0 * std::exp(-t * 55.0)) / sr;
        kick[(size_t) n] = (float) (std::tanh(std::sin(phase) * 1.6) * std::exp(-t * 11.0) + std::exp(-t * 900.0) * 0.5);
        kickPeak = std::max(kickPeak, (double) std::abs(kick[(size_t) n]));
    }
    const double gain = juce::Decibels::decibelsToGain((double) kickPeakDb) / kickPeak;
    for (auto& v : kick) v = (float) (v * gain);

    juce::AudioBuffer<float> buffer(2, block);
    juce::MidiBuffer midi;
    KWeight kw[2];
    double peak = 0.0, sum = 0.0;
    juce::int64 counted = 0;
    for (int start = 0; start < total; start += block) {
        for (int i = 0; i < block; ++i) {
            const float v = kick[(size_t) ((start + i) % beat)];
            buffer.setSample(0, i, v);
            buffer.setSample(1, i, v);
        }
        head.sample = start;
        p.processBlock(buffer, midi);
        for (int i = 0; i < block; ++i)
            for (int ch = 0; ch < 2; ++ch) {
                const double y = buffer.getSample(ch, i);
                peak = std::max(peak, std::abs(y));
                const double k = kw[ch].process(y);
                if (start >= beat * 8) { sum += k * k; ++counted; }   // skip the first two bars
            }
    }
    p.setPlayHead(nullptr);
    // Ungated integrated loudness, both channels summed as BS.1770 does.
    const double lufs = -0.691 + 10.0 * std::log10(2.0 * sum / (double) juce::jmax<juce::int64>(1, counted) + 1.0e-20);
    return { juce::Decibels::gainToDecibels(peak, -120.0), lufs };
}

// Every factory preset: no clipping, and close in loudness to Warehouse so
// stepping through them doesn't jump in level.
void testFactoryPresetsAreSafeAndLevelMatched() {
    RumbleAudioProcessor names;
    int warehouse = 0;
    for (int i = 0; i < names.getNumPrograms(); ++i)
        if (names.getProgramName(i) == "Warehouse") warehouse = i;
    const double reference = auditProgram(warehouse).lufs;

    double worstPeak = -120.0, worstSpread = 0.0;
    juce::String worstPeakName, worstSpreadName;
    for (int i = 0; i < names.getNumPrograms(); ++i) {
        const auto a = auditProgram(i);
        std::printf("      %-20s %-18s peak %6.1f dBFS  loudness %6.1f LUFS (%+4.1f)\n",
                    names.getProgramName(i).toRawUTF8(), names.getProgramCategory(i).toRawUTF8(),
                    a.peakDb, a.lufs, a.lufs - reference);
        // Clipping is checked with a hot, full-scale kick as well.
        const double hotPeak = auditProgram(i, 0.0f).peakDb;
        if (hotPeak > worstPeak) { worstPeak = hotPeak; worstPeakName = names.getProgramName(i); }
        if (std::abs(a.lufs - reference) > worstSpread) { worstSpread = std::abs(a.lufs - reference); worstSpreadName = names.getProgramName(i); }
    }
    check(worstPeak < -1.0, ("0 dBFS kick: every preset peaks below -1 dBFS (worst: " + worstPeakName + ", dBFS)").toStdString(), worstPeak);
    check(worstSpread <= 2.0, ("every factory preset within 2 LU of Warehouse (worst: " + worstSpreadName + ", LU)").toStdString(), worstSpread);
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
    testOldSessionsKeepTheirDecay();
    testOldUserPresetsKeepTheirDecay();
    testFactoryPresetsAreSafeAndLevelMatched();
    std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
