#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

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

    // Editor size, as a scale of the design size. Saved with the session.
    float getUiScale() const;
    void setUiScale(float);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParams();
    void markPresetLoaded(const juce::String& name);
    void resyncParameters();
    std::vector<float> currentParameterValues() const;

    rumble::RumbleEngine engine;
    juce::AudioBuffer<float> scBuffer;
    juce::AudioBuffer<float> monoScratch;
    bool sidechainIsLive = false;
    int currentProgram = 0;

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
