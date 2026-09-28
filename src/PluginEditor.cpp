#include "PluginEditor.h"

namespace {
// Layout constants. Sections stack vertically; cells flow across each section.
constexpr int kCols       = 3;    // cells per row inside a section
constexpr int kCellH      = 88;
constexpr int kCaptionH   = 14;
constexpr int kHeaderH    = 18;
constexpr int kTitleH     = 42;
constexpr int kMargin     = 10;
constexpr int kSectionGap = 8;
constexpr int kSectionPad = 6;
constexpr int kWindowW    = 760;
constexpr int kNumColumns = 2;    // sections are laid out in two columns

const juce::Colour kBackground { 0xff121215 };
const juce::Colour kPanel      { 0xff1c1c21 };
const juce::Colour kPanelEdge  { 0xff2e2e36 };
const juce::Colour kText       { 0xffe8e0d0 };
const juce::Colour kHeaderText { 0xff8f8fa0 };
const juce::Colour kDim        { 0xff6a6a72 };

int sectionHeight(int numCells) {
    const int rows = juce::jmax(1, (numCells + kCols - 1) / kCols);
    return kHeaderH + rows * kCellH + kSectionPad * 2;
}
} // namespace

RumbleAudioProcessorEditor::RumbleAudioProcessorEditor(RumbleAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    // Grouped by what each control actually does, in signal-flow order.
    auto& drive = addSection("DRIVE", 0);
    addKnob(drive, "drive", "Input Drive");
    addKnob(drive, "taildrive", "Tail Drive");
    addKnob(drive, "enhance", "Enhance");

    auto& reverb = addSection("REVERB", 0);
    addKnob(reverb, "size", "Size");
    addKnob(reverb, "decay", "Decay");
    addKnob(reverb, "damp", "Damping");
    addKnob(reverb, "lowcut", "Low Cut");
    addKnob(reverb, "mod", "Mod");

    auto& timing = addSection("TIMING", 0);
    addKnob(timing, "predelay", "Pre-Delay");
    addToggle(timing, "sync", "Sync");
    addCombo(timing, "div", "Division");
    addKnob(timing, "swing", "Swing");

    auto& filter = addSection("FILTER", 1);
    addCombo(filter, "filtertype", "Type");
    addKnob(filter, "filterhz", "Cutoff");
    addKnob(filter, "filterq", "Resonance");
    addKnob(filter, "tone", "Tone");

    auto& duck = addSection("DUCK", 1);
    addKnob(duck, "duck", "Amount");
    addKnob(duck, "duckatk", "Attack");
    addKnob(duck, "duckrel", "Release");

    auto& output = addSection("OUTPUT", 1);
    addKnob(output, "mix", "Mix");
    addKnob(output, "width", "Width");
    addKnob(output, "monobelow", "Mono Below");
    addKnob(output, "output", "Output");

    // Height is whichever column of sections is tallest.
    int colH[kNumColumns] = {};
    for (auto* s : sections) {
        auto& h = colH[juce::jlimit(0, kNumColumns - 1, s->column)];
        h += sectionHeight(s->cells.size()) + kSectionGap;
    }
    int tallest = 0;
    for (int h : colH) tallest = juce::jmax(tallest, h - kSectionGap);
    setSize(kWindowW, kTitleH + tallest + kMargin);
}

RumbleAudioProcessorEditor::Section& RumbleAudioProcessorEditor::addSection(const juce::String& name, int column) {
    auto* s = sections.add(new Section());
    s->name = name;
    s->column = column;
    return *s;
}

RumbleAudioProcessorEditor::Cell& RumbleAudioProcessorEditor::addCell(Section& s, const juce::String& caption) {
    auto* c = s.cells.add(new Cell());
    c->label.setText(caption, juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.setFont(juce::FontOptions(11.5f));
    c->label.setColour(juce::Label::textColourId, kText);
    addAndMakeVisible(c->label);
    return *c;
}

void RumbleAudioProcessorEditor::addKnob(Section& s, const char* paramID, const juce::String& caption) {
    auto& c = addCell(s, caption);
    c.slider = std::make_unique<juce::Slider>();
    c.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 72, 15);
    addAndMakeVisible(*c.slider);
    c.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, *c.slider);
}

void RumbleAudioProcessorEditor::addCombo(Section& s, const char* paramID, const juce::String& caption) {
    auto& c = addCell(s, caption);
    c.combo = std::make_unique<juce::ComboBox>();

    // ComboBoxAttachment does not populate the box; it only maps the selected
    // index to the parameter. The items must exist before it is attached.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter(paramID)))
        c.combo->addItemList(choice->choices, 1);

    addAndMakeVisible(*c.combo);
    c.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, paramID, *c.combo);
}

void RumbleAudioProcessorEditor::addToggle(Section& s, const char* paramID, const juce::String& caption) {
    auto& c = addCell(s, caption);
    c.button = std::make_unique<juce::ToggleButton>("");
    addAndMakeVisible(*c.button);
    c.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, paramID, *c.button);
}

void RumbleAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kBackground);

    auto title = getLocalBounds().removeFromTop(kTitleH);
    g.setColour(kText);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("RUMBLE", title.reduced(kMargin, 0), juce::Justification::centredLeft);

    g.setColour(kDim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("v" JucePlugin_VersionString, title.reduced(kMargin, 0), juce::Justification::centredRight);

    for (auto* s : sections) {
        g.setColour(kPanel);
        g.fillRoundedRectangle(s->bounds.toFloat(), 4.0f);
        g.setColour(kPanelEdge);
        g.drawRoundedRectangle(s->bounds.toFloat().reduced(0.5f), 4.0f, 1.0f);

        g.setColour(kHeaderText);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(s->name,
                   s->bounds.withHeight(kHeaderH).reduced(kSectionPad + 2, 0),
                   juce::Justification::centredLeft);
    }
}

void RumbleAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin, 0);
    area.removeFromTop(kTitleH);

    const int colW = (area.getWidth() - kSectionGap * (kNumColumns - 1)) / kNumColumns;
    int colY[kNumColumns];
    for (int i = 0; i < kNumColumns; ++i) colY[i] = area.getY();

    for (auto* s : sections) {
        const int col = juce::jlimit(0, kNumColumns - 1, s->column);
        const int h = sectionHeight(s->cells.size());
        s->bounds = { area.getX() + col * (colW + kSectionGap), colY[col], colW, h };
        colY[col] += h + kSectionGap;

        auto inner = s->bounds.reduced(kSectionPad);
        inner.removeFromTop(kHeaderH);

        const int cellW = inner.getWidth() / kCols;
        for (int i = 0; i < s->cells.size(); ++i) {
            juce::Rectangle<int> cell(inner.getX() + (i % kCols) * cellW,
                                      inner.getY() + (i / kCols) * kCellH,
                                      cellW, kCellH);
            cell.reduce(4, 3);

            auto* c = s->cells[i];
            c->label.setBounds(cell.removeFromTop(kCaptionH));

            if (c->slider != nullptr) {
                c->slider->setBounds(cell);
            } else if (c->combo != nullptr) {
                c->combo->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
            } else if (c->button != nullptr) {
                // A bare toggle centred under its caption, like the knobs.
                c->button->setBounds(cell.withSizeKeepingCentre(26, 26));
            }
        }
    }
}
