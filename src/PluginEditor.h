#pragma once

#include "PluginProcessor.h"
#include "RumbleLookAndFeel.h"

#include <juce_animation/juce_animation.h>

#include <map>
#include <optional>
#include <vector>

// A float that eases to a new value instead of jumping, driven by the
// display's refresh (via a VBlankAnimatorUpdater) so it moves in step with
// the screen. `onChange` runs on every step.
struct Fader {
    float value = 0.0f;
    std::function<void()> onChange;

    void animateTo(float target, double ms, juce::VBlankAnimatorUpdater& updater) {
        if (anim.has_value() && juce::approximatelyEqual(target, to)) return;
        if (! anim.has_value() && juce::approximatelyEqual(target, value)) return;
        from = value;
        to = target;
        anim = juce::ValueAnimatorBuilder {}
                   .withDurationMs(ms)
                   .withEasing(juce::Easings::createEaseOut())
                   .withValueChangedCallback([this](float p) {
                       value = from + (to - from) * p;
                       if (onChange) onChange();
                   })
                   .withOnCompleteCallback([this] { anim.reset(); })
                   .build();
        updater.addAnimator(*anim);
        anim->start();
    }
    void snapTo(float v) { anim.reset(); value = from = to = v; if (onChange) onChange(); }

private:
    float from = 0.0f, to = 0.0f;
    std::optional<juce::Animator> anim;
};

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
    juce::Image bloom;                     // reused glow buffer for the trace
};

// A knob that LFOs can be dropped onto. Shows each assigned LFO's range as
// a coloured ring and the knob's live position as a dot; drag on a ring to
// change that LFO's depth, right-click to invert or remove it.
class ModKnob : public juce::Slider, public juce::DragAndDropTarget {
public:
    ModKnob(RumbleAudioProcessor&, int paramIndex, juce::VBlankAnimatorUpdater&);

    bool isModulated() const;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
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
    juce::VBlankAnimatorUpdater& animator;
    Fader hover;                 // 0..1, brightens the value arc's glow
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

// A section's power switch: its indicator lamp and name, clickable as one.
// Lit means on.
class SectionSwitch : public juce::Button {
public:
    explicit SectionSwitch(const juce::String& sectionName);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
    float lit = 1.0f;            // lamp brightness, animated by the editor
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
        juce::String powerParam;                    // empty: no switch
        std::unique_ptr<SectionSwitch> power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
        Fader powerFade;                            // 1 on, 0 off, eased in between
        bool powerKnown = false;
        juce::Rectangle<int> bounds;
    };

    void timerCallback() override;
    void paintCanvas(juce::Graphics&);
    void paintBackground(juce::Graphics&);
    void layoutCanvas();
    void refreshPresetDisplay();
    void refreshEnablement();
    void applySectionAlpha(Section&);
    void onVBlank();
    void stepPreset(int delta);

    Section& addSection(const juce::String& name, int column, int perRow,
                        const juce::String& powerParam = {});
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
    // Declared before the sections and knobs that use it, so it outlives them.
    juce::VBlankAnimatorUpdater animatorUpdater { this };
    juce::OwnedArray<Section> sections;
    juce::Image logo;
    juce::Image texture;                   // chassis grain, generated once
    juce::Image backgroundCache;           // the static faceplate, see paintCanvas
    float backgroundScale = 0.0f;

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
    std::map<juce::String, bool> liveCache;     // which controls apply in the current mode
    // Live visuals (scope, LFOs, modulated knobs) repaint in step with the
    // display rather than on a fixed timer.
    juce::VBlankAttachment vblank { this, [this] { onVBlank(); } };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RumbleAudioProcessorEditor)
};
