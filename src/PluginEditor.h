#pragma once

#include "PluginProcessor.h"
#include "RumbleLookAndFeel.h"

// Horizontal duck gain-reduction meter. Fed the lowest gain the processor
// saw since the last frame; falls back with a gentle release so short pumps
// stay readable.
class DuckMeter : public juce::Component {
public:
    void push(float minGain);
    void paint(juce::Graphics&) override;
private:
    float shownDb = 0.0f;   // gain reduction, <= 0
};

class RumbleAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer {
public:
    explicit RumbleAudioProcessorEditor(RumbleAudioProcessor&);
    // Must detach the LookAndFeel before it is destroyed.
    ~RumbleAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshPresetDisplay();
    void refreshEnablement();

    // One labelled control: a rotary, a combo box or a toggle, plus its caption.
    struct Cell {
        juce::String paramID;
        juce::Label label;
        std::unique_ptr<juce::Component> custom;     // e.g. the duck meter
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
    Cell* findCell(const juce::String& paramID);

    RumbleAudioProcessor& processor;
    rumble_ui::RumbleLookAndFeel lookAndFeel;
    juce::OwnedArray<Section> sections;
    juce::Image logo;
    juce::ComboBox presetBox;
    juce::Label presetLabel;
    juce::TextButton saveButton { "Save" };
    juce::TextButton deleteButton { "Del" };
    juce::Array<juce::File> userPresetFiles;
    DuckMeter* duckMeter = nullptr;      // owned by its cell
    juce::String shownPresetText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessorEditor)
};
