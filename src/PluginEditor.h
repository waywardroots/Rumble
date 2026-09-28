#pragma once

#include "PluginProcessor.h"

class RumbleAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit RumbleAudioProcessorEditor(RumbleAudioProcessor&);
    ~RumbleAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    Knob& addKnob(const char* paramID, const juce::String& name);

    RumbleAudioProcessor& processor;
    juce::OwnedArray<Knob> knobs;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessorEditor)
};
