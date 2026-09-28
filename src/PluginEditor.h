#pragma once

#include "PluginProcessor.h"

class RumbleAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit RumbleAudioProcessorEditor(RumbleAudioProcessor&);
    ~RumbleAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // Every control is laid out as one labelled cell in the grid, whether it
    // holds a rotary, a combo box or a button.
    struct Cell {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    Cell& addCell(const juce::String& name);
    void addKnob(const char* paramID, const juce::String& name);
    void addCombo(const char* paramID, const juce::String& name);
    void addToggle(const char* paramID, const juce::String& name);

    RumbleAudioProcessor& processor;
    juce::OwnedArray<Cell> cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessorEditor)
};
