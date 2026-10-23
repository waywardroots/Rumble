#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

#include "Lfo.h"
#include "RumbleEngine.h"

class RumbleAudioProcessor : public juce::AudioProcessor {
public:
    RumbleAudioProcessor();
    ~RumbleAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;   // the double-precision overload

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Rumble"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // --- user presets, stored as XML alongside the factory ones ------------
    static juce::File userPresetDirectory();
    juce::Array<juce::File> userPresets() const;
    juce::Result saveUserPreset(const juce::String& name);
    bool loadUserPreset(const juce::File&);

    // Name of the preset last loaded or saved, and whether any parameter has
    // moved since. Message thread only.
    juce::String getPresetName() const;
    bool isPresetModified() const;

    // --- live display -----------------------------------------------------
    // The audio thread condenses its signal into short frames -- peak input
    // (the kick), peak output (the tail) and lowest duck gain -- and hands
    // them to the editor through a lock-free FIFO. Frames that the editor
    // is not around to collect are simply dropped.
    struct ScopeFrame { float in = 0.0f, out = 0.0f, gain = 1.0f; };
    static constexpr int kScopeCapacity = 4096;
    static constexpr double kScopeFramesPerSecond = 200.0;
    int readScope(ScopeFrame* dest, int maxFrames);

    // --- modulation -------------------------------------------------------
    // Two LFOs, each able to move any number of knobs by its own depth per
    // knob (-1..1 of the knob's travel). Depths are written on the message
    // thread and read on the audio thread through atomics, so neither ever
    // waits for the other. Parameters are addressed by their index in
    // getParameters().
    static constexpr int kNumLfos = 2;
    static constexpr int kMaxParams = 96;
    bool isModulatable(int paramIndex) const;
    float getModDepth(int lfo, int paramIndex) const;
    void setModDepth(int lfo, int paramIndex, float depth);    // message thread
    void clearModulation();                                     // message thread
    // The LFOs' current output and phase, for drawing.
    float getLfoValue(int lfo) const { return lfoValueUi[(size_t) lfo].load(std::memory_order_relaxed); }
    double getLfoPhase(int lfo) const { return lfoPhaseUi[(size_t) lfo].load(std::memory_order_relaxed); }

    // Editor size, as a scale of the design size. Saved with the session.
    float getUiScale() const;
    void setUiScale(float);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParams(const float* lfoNow);
    void writeModulation(juce::ValueTree& state) const;
    void readModulation(juce::ValueTree& state);
    float paramValue(int which, const float* lfoNow);
    void markPresetLoaded(const juce::String& name);
    void resyncParameters();
    void convertLegacyDecay();
    std::vector<float> currentParameterValues() const;

    rumble::RumbleEngine engine;
    juce::AudioBuffer<float> scBuffer;
    juce::AudioBuffer<float> monoScratch;
    bool sidechainIsLive = false;
    int currentProgram = 0;

    // Parameters by a fixed internal id, resolved to getParameters() indices
    // once, so the audio thread never looks a parameter up by name.
    std::vector<int> paramIndexById;
    std::vector<juce::RangedAudioParameter*> paramsByIndex;
    std::vector<std::atomic<float>*> rawByIndex;
    std::vector<char> modulatable;

    std::array<std::array<std::atomic<float>, kMaxParams>, kNumLfos> modDepth;
    std::atomic<bool> anyModulation { false };
    std::array<rumble::Lfo, kNumLfos> lfos { rumble::Lfo(0x51ab), rumble::Lfo(0x7c3e) };
    std::array<std::atomic<float>, kNumLfos> lfoValueUi;
    std::array<std::atomic<double>, kNumLfos> lfoPhaseUi;
    double currentSampleRate = 48000.0;

    juce::AbstractFifo scopeFifo { kScopeCapacity };
    std::array<ScopeFrame, kScopeCapacity> scopeFrames {};
    ScopeFrame scopeAcc;
    int scopeCount = 0;
    int scopeFrameLength = 240;
    juce::AudioBuffer<float> inputScratch, gainScratch;
    void pushScope(const float* in, const float* outL, const float* outR, const float* gain, int n);

    // What "modified" is measured against: the parameter values right after
    // the current preset was loaded or saved.
    mutable juce::CriticalSection presetLock;
    juce::String presetName { "Init" };
    std::vector<float> presetSnapshot;
    bool presetRestoredModified = false;   // session was saved with edits

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessor)
};
