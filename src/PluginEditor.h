#pragma once

#include "PluginProcessor.h"
#include "RumbleLookAndFeel.h"

class RumbleAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit RumbleAudioProcessorEditor(RumbleAudioProcessor&);
    // Must detach the LookAndFeel before it is destroyed.
    ~RumbleAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // One labelled control: a rotary, a combo box or a toggle, plus its caption.
    struct Cell {
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    // A titled group of cells, drawn as its own panel.
    struct Section {
        juce::String name;
        int column = 0;
        juce::OwnedArray<Cell> cells;
        juce::Rectangle<int> bounds;
    };

    Section& addSection(const juce::String& name, int column);
    void rebuildPresetMenu();
    void promptForPresetName();
    void deleteSelectedPreset();
    Cell& addCell(Section&, const juce::String& caption);
    void addKnob(Section&, const char* paramID, const juce::String& caption);
    void addCombo(Section&, const char* paramID, const juce::String& caption);
    void addToggle(Section&, const char* paramID, const juce::String& caption);

    RumbleAudioProcessor& processor;
    rumble_ui::RumbleLookAndFeel lookAndFeel;
    juce::OwnedArray<Section> sections;
    juce::Image logo;
    juce::ComboBox presetBox;
    juce::Label presetLabel;
    juce::TextButton saveButton { "Save" };
    juce::TextButton deleteButton { "Del" };
    juce::Array<juce::File> userPresetFiles;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessorEditor)
};
