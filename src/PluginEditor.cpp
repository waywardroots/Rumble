#include "PluginEditor.h"

#include <BinaryData.h>

namespace {
// Layout constants. Sections stack vertically; cells flow across each section.
constexpr int kCols       = 4;    // cells per row inside a section
constexpr int kCellH      = 96;
constexpr int kCaptionH   = 14;
constexpr int kHeaderH    = 18;
constexpr int kTitleH     = 64;   // tall enough for the square logo
constexpr int kLogoSize   = 44;
constexpr int kMargin     = 10;
constexpr int kSectionGap = 8;
constexpr int kSectionPad = 6;
constexpr int kWindowW    = 860;
constexpr int kPresetW    = 170;  // preset selector in the title bar
constexpr int kPresetBoxH = 26;
constexpr int kNumColumns = 2;    // sections are laid out in two columns

using namespace rumble_ui;

int sectionHeight(int numCells) {
    const int rows = juce::jmax(1, (numCells + kCols - 1) / kCols);
    return kHeaderH + rows * kCellH + kSectionPad * 2;
}
} // namespace

RumbleAudioProcessorEditor::RumbleAudioProcessorEditor(RumbleAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    setLookAndFeel(&lookAndFeel);
    logo = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

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
    addKnob(reverb, "diffusion", "Diffusion");
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

    auto& eq = addSection("EQ", 1);
    addKnob(eq, "eqlow", "Weight");
    addKnob(eq, "eqmid", "Body");
    addKnob(eq, "eqmidhz", "Body Freq");
    addKnob(eq, "eqhigh", "Edge");

    auto& duck = addSection("DUCK", 1);
    addKnob(duck, "duck", "Amount");
    addKnob(duck, "duckatk", "Attack");
    addKnob(duck, "duckrel", "Release");

    auto& output = addSection("OUTPUT", 1);
    addKnob(output, "width", "Width");
    addKnob(output, "monobelow", "Mono Below");
    addKnob(output, "output", "Output");

    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, kHeaderText);
    presetLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(presetLabel);

    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem(processor.getProgramName(i), i + 1);
    presetBox.setSelectedId(processor.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getCurrentProgram())
            processor.setCurrentProgram(index);
    };
    addAndMakeVisible(presetBox);

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

RumbleAudioProcessorEditor::~RumbleAudioProcessorEditor() {
    setLookAndFeel(nullptr);
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
    c->label.setMinimumHorizontalScale(0.85f);
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
    // Background wash, darker towards the bottom.
    g.setGradientFill(juce::ColourGradient(kBgTop, 0.0f, 0.0f,
                                           kBgBottom, 0.0f, (float) getHeight(), false));
    g.fillAll();

    auto title = getLocalBounds().removeFromTop(kTitleH).reduced(kMargin, 0);

    // Logo badge, top left.
    if (logo.isValid()) {
        auto badge = title.removeFromLeft(kLogoSize)
                          .withSizeKeepingCentre(kLogoSize, kLogoSize);
        g.drawImageWithin(logo, badge.getX(), badge.getY(),
                          badge.getWidth(), badge.getHeight(),
                          juce::RectanglePlacement::centred, false);
        title.removeFromLeft(12);
    }

    g.setColour(kText);
    g.setFont(juce::FontOptions(23.0f, juce::Font::bold));
    g.drawText("RUMBLE", title, juce::Justification::centredLeft);

    // Accent rule under the title, fading out to the right.
    {
        const float yLine = (float) kTitleH - 3.0f;
        juce::ColourGradient rule(kAccent.withAlpha(0.75f), (float) kMargin, yLine,
                                  kAccent.withAlpha(0.0f), (float) getWidth() * 0.66f, yLine, false);
        g.setGradientFill(rule);
        g.fillRect((float) kMargin, yLine, (float) getWidth() - kMargin * 2.0f, 1.0f);
    }

    g.setColour(kTextDim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("v" JucePlugin_VersionString,
               title.withTrimmedRight(kPresetW + kMargin + 52),
               juce::Justification::centredRight);

    for (auto* s : sections) {
        drawPanel(g, s->bounds);

        auto header = s->bounds.withTrimmedTop(2).withHeight(kHeaderH).reduced(kSectionPad + 2, 0);
        g.setColour(kHeaderText);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(s->name.toUpperCase(), header, juce::Justification::centredLeft);

        // Engraved separator beneath the header: one dark line, one light.
        const float sepY = (float) header.getBottom() + 1.0f;
        g.setColour(juce::Colour(0x30000000));
        g.drawLine((float) header.getX(), sepY, (float) header.getRight(), sepY, 1.0f);
        g.setColour(kPanelLift);
        g.drawLine((float) header.getX(), sepY + 1.0f, (float) header.getRight(), sepY + 1.0f, 1.0f);
    }
}

void RumbleAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(kMargin, 0);

    auto title = area.removeFromTop(kTitleH);
    // Fixed control height: the title bar is sized for the logo, not the combo.
    presetBox.setBounds(title.removeFromRight(kPresetW)
                             .withSizeKeepingCentre(kPresetW, kPresetBoxH));
    presetLabel.setBounds(title.removeFromRight(52));

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
