#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "RumbleEngine.h"

class RumbleAudioProcessor : public juce::AudioProcessor {
public:
    RumbleAudioProcessor();
    ~RumbleAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void pullParams(double bpm, double ppq, bool ppqValid);

    rumble::RumbleEngine engine;
    juce::AudioBuffer<float> scBuffer;
    juce::AudioBuffer<float> monoScratch;
    bool sidechainIsLive = false;
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessor)
};
