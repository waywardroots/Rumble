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

// A knob that LFOs can be dropped onto. Shows each assigned LFO's range as
// a coloured ring and the knob's live position as a dot; drag on a ring to
// change that LFO's depth, right-click to invert or remove it.
class ModKnob : public juce::Slider, public juce::DragAndDropTarget {
public:
    ModKnob(RumbleAudioProcessor&, int paramIndex);

    bool isModulated() const;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;

private:
    struct Geometry { juce::Point<float> centre; float dia = 0.0f; };
    Geometry geometry() const;
    float ringRadius(int lfo) const;
    int ringUnder(juce::Point<float>) const;   // LFO whose ring is under the mouse, or -1
    void showMenu();

    RumbleAudioProcessor& processor;
    const int paramIndex;
    int dropHoverLfo = -1;       // LFO being dragged over this knob
    int editingLfo = -1;         // LFO whose depth is being dragged
    int ringHoverLfo = -1;
    float depthAtDragStart = 0.0f;
};

// The drag handle and mini display for one LFO: its waveform, a playhead at
// the current phase, and how many knobs it is driving.
class LfoSource : public juce::Component {
public:
    LfoSource(RumbleAudioProcessor&, int lfo);
    void paint(juce::Graphics&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
private:
    RumbleAudioProcessor& processor;
    const int lfo;
    std::atomic<float>* shapeParam = nullptr;
};

class RumbleAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   public juce::DragAndDropContainer,
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
        std::unique_ptr<juce::Component> custom;   // e.g. an LFO source
        bool spacer = false;
        int span = 1;                              // how many cell widths it takes
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
    void addLfoSource(Section&, int lfo);
    Cell* findCell(const juce::String& paramID);
    int designHeight() const;
    static int slotsIn(const Section&);
    static int sectionHeight(const Section&);

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
    juce::Array<LfoSource*> lfoSources;      // owned by their cells
    juce::Array<ModKnob*> modKnobs;          // owned by their cells
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
