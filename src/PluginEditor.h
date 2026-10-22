#pragma once

#include "PluginProcessor.h"
#include "RumbleLookAndFeel.h"

#include <vector>

// Live display across the top of the plugin: the kick coming in, the rumble
// going out, and the duck gain pumping it, scrolling right to left.
class ScopeDisplay : public juce::Component {
public:
    ScopeDisplay();

    // Append frames from the processor; the oldest scroll off the left.
    void push(const RumbleAudioProcessor::ScopeFrame* frames, int count);
    void paint(juce::Graphics&) override;

private:
    static constexpr int kHistory = 900;   // 4.5 s at 200 frames per second
    std::vector<RumbleAudioProcessor::ScopeFrame> history;
    int head = 0;                          // index of the oldest frame

    // Smoothed numeric readouts.
    float shownGrDb = 0.0f;
    float shownOutDb = -100.0f;
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
    // Everything is laid out once, at the design size, on this canvas. When
    // the window is resized the whole canvas is scaled, so the faceplate
    // keeps its proportions at any size.
    struct Canvas : public juce::Component {
        explicit Canvas(RumbleAudioProcessorEditor& e) : owner(e) {}
        void paint(juce::Graphics& g) override { owner.paintCanvas(g); }
        RumbleAudioProcessorEditor& owner;
    };

    // One labelled control: a rotary, a combo box or a toggle, plus its caption.
    struct Cell {
        juce::String paramID;
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::ToggleButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
        bool spacer = false;
    };

    // A titled faceplate of cells, laid out `perRow` across.
    struct Section {
        juce::String name;
        int column = 0;
        int perRow = 4;
        juce::OwnedArray<Cell> cells;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void paintCanvas(juce::Graphics&);
    void layoutCanvas();
    void refreshPresetDisplay();
    void refreshEnablement();
    void stepPreset(int delta);

    Section& addSection(const juce::String& name, int column, int perRow);
    Cell& addCell(Section&, const juce::String& caption);
    void addKnob(Section&, const char* paramID, const juce::String& caption);
    void addCombo(Section&, const char* paramID, const juce::String& caption);
    void addToggle(Section&, const char* paramID, const juce::String& caption);
    void addSpacer(Section&);
    Cell* findCell(const juce::String& paramID);
    int designHeight() const;

    void rebuildPresetMenu();
    void promptForPresetName();
    void deleteSelectedPreset();

    RumbleAudioProcessor& processor;
    rumble_ui::RumbleLookAndFeel lookAndFeel;
    Canvas canvas { *this };
    juce::OwnedArray<Section> sections;
    juce::Image logo;
    juce::Image texture;                   // chassis grain, generated once

    ScopeDisplay scope;
    std::vector<RumbleAudioProcessor::ScopeFrame> scopeScratch;

    juce::ComboBox presetBox;
    juce::ArrowButton prevButton { "Previous preset", 0.5f, rumble_ui::kAccent };
    juce::ArrowButton nextButton { "Next preset", 0.0f, rumble_ui::kAccent };
    juce::TextButton saveButton { "SAVE" };
    juce::TextButton deleteButton { "DELETE" };
    juce::Array<juce::File> userPresetFiles;
    juce::String shownPresetText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessorEditor)
};
