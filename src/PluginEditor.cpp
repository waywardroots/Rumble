#include "PluginEditor.h"

#include <BinaryData.h>

#include <random>

namespace {
using namespace rumble_ui;

// Design-size layout. The canvas is laid out at these sizes and then scaled
// as a whole to the window, so nothing here needs to know the window size.
constexpr int kDesignW     = 960;
constexpr int kMargin      = 12;
constexpr int kHeaderH     = 64;
constexpr int kLogoSize    = 40;
constexpr int kScopeH      = 150;
constexpr int kScopePad    = 9;     // plate around the scope screen
constexpr int kNumColumns  = 2;
constexpr int kColumnGap   = 10;
constexpr int kSectionGap  = 10;
constexpr int kSectionHead = 28;    // header strip inside each plate
constexpr int kSectionPad  = 12;
constexpr int kCellH       = 94;
constexpr int kCaptionH    = 15;
constexpr int kReadoutH    = 18;
constexpr int kPresetW     = 230;
constexpr int kPresetH     = 30;
constexpr int kButtonW     = 62;
constexpr int kUserPresetIdBase = 1000;   // keeps user ids clear of factory ones

constexpr float kMinScale = 0.7f;
constexpr float kMaxScale = 2.0f;

int rowsFor(int cells, int perRow) { return juce::jmax(1, (cells + perRow - 1) / perRow); }

int sectionHeight(int cells, int perRow) {
    return kSectionHead + rowsFor(cells, perRow) * kCellH + kSectionPad;
}

// Fine grain for the chassis, so large dark areas do not look like flat
// digital fills. Generated once, deterministically.
juce::Image makeTexture() {
    juce::Image img(juce::Image::ARGB, 128, 128, true);
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> d(0, 255);
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x) {
            const int v = d(rng);
            img.setPixelAt(x, y, v > 128 ? juce::Colour::fromRGBA(255, 255, 255, (juce::uint8) ((v - 128) / 22))
                                         : juce::Colour::fromRGBA(0, 0, 0, (juce::uint8) ((128 - v) / 14)));
        }
    return img;
}
} // namespace

// ================================================================== scope

ScopeDisplay::ScopeDisplay() : history((size_t) kHistory) {
    setInterceptsMouseClicks(false, false);
}

void ScopeDisplay::push(const RumbleAudioProcessor::ScopeFrame* frames, int count) {
    for (int i = 0; i < count; ++i) {
        history[(size_t) head] = frames[i];
        head = (head + 1) % kHistory;
    }

    // Readouts over the most recent ~150 ms: deepest duck, loudest output.
    float minGain = 1.0f, maxOut = 0.0f;
    for (int i = 1; i <= 30; ++i) {
        const auto& f = history[(size_t) ((head - i + kHistory) % kHistory)];
        minGain = juce::jmin(minGain, f.gain);
        maxOut  = juce::jmax(maxOut, f.out);
    }
    const float grDb  = juce::Decibels::gainToDecibels(minGain, -60.0f);
    const float outDb = juce::Decibels::gainToDecibels(maxOut, -100.0f);
    // Fast to show a peak, slower to let go, so the numbers are readable.
    shownGrDb  = grDb  < shownGrDb  ? grDb  : shownGrDb  + (grDb  - shownGrDb)  * 0.15f;
    shownOutDb = outDb > shownOutDb ? outDb : shownOutDb + (outDb - shownOutDb) * 0.15f;
    repaint();
}

void ScopeDisplay::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    drawScreen(g, bounds, 5.0f);

    const auto plot = bounds.reduced(10.0f, 22.0f).withTrimmedBottom(-12.0f);
    constexpr float kFloorDb = -48.0f;
    auto levelY = [&](float level) {
        const float db = juce::Decibels::gainToDecibels(level, kFloorDb);
        return plot.getBottom() - plot.getHeight() * (1.0f - db / kFloorDb);
    };
    // Gain reduction hangs from the top, 0 to -30 dB.
    auto duckY = [&](float gain) {
        const float db = juce::Decibels::gainToDecibels(gain, -30.0f);
        return plot.getY() + plot.getHeight() * (-db / 30.0f);
    };

    // Grid: dB lines, and a vertical line every half second.
    g.setColour(kGrid);
    for (float db : { -12.0f, -24.0f, -36.0f }) {
        const float y = levelY(juce::Decibels::decibelsToGain(db));
        g.drawHorizontalLine((int) y, plot.getX(), plot.getRight());
    }
    const float pxPerFrame = plot.getWidth() / (float) (kHistory - 1);
    const float halfSecond = (float) RumbleAudioProcessor::kScopeFramesPerSecond * 0.5f * pxPerFrame;
    for (float x = plot.getRight(); x > plot.getX(); x -= halfSecond)
        g.drawVerticalLine((int) x, plot.getY(), plot.getBottom());

    auto frameAt = [&](int i) -> const RumbleAudioProcessor::ScopeFrame& {
        return history[(size_t) ((head + i) % kHistory)];
    };
    auto xAt = [&](int i) { return plot.getX() + (float) i * pxPerFrame; };

    // The kick: faint bars from the floor.
    g.setColour(kText.withAlpha(0.16f));
    for (int i = 0; i < kHistory; i += 2) {
        const float y = levelY(frameAt(i).in);
        if (y < plot.getBottom() - 0.5f)
            g.fillRect(xAt(i), y, juce::jmax(1.0f, pxPerFrame * 2.0f - 0.5f), plot.getBottom() - y);
    }

    // The rumble: a filled amber envelope with a bright edge.
    juce::Path tail;
    tail.startNewSubPath(plot.getX(), plot.getBottom());
    for (int i = 0; i < kHistory; ++i)
        tail.lineTo(xAt(i), levelY(frameAt(i).out));
    tail.lineTo(plot.getRight(), plot.getBottom());
    tail.closeSubPath();
    g.setGradientFill(juce::ColourGradient(kAccent.withAlpha(0.55f), 0, plot.getY(),
                                           kAccent.withAlpha(0.04f), 0, plot.getBottom(), false));
    g.fillPath(tail);

    juce::Path edge;
    for (int i = 0; i < kHistory; ++i) {
        const auto p = juce::Point<float>(xAt(i), levelY(frameAt(i).out));
        if (i == 0) edge.startNewSubPath(p); else edge.lineTo(p);
    }
    g.setColour(kAccentHot.withAlpha(0.9f));
    g.strokePath(edge, juce::PathStrokeType(1.3f));

    // The duck: gain reduction hanging from the top.
    juce::Path duck;
    for (int i = 0; i < kHistory; ++i) {
        const auto p = juce::Point<float>(xAt(i), duckY(frameAt(i).gain));
        if (i == 0) duck.startNewSubPath(p); else duck.lineTo(p);
    }
    juce::Path duckFill(duck);
    duckFill.lineTo(plot.getRight(), plot.getY());
    duckFill.lineTo(plot.getX(), plot.getY());
    duckFill.closeSubPath();
    g.setColour(kDuckLine.withAlpha(0.07f));
    g.fillPath(duckFill);
    g.setColour(kDuckLine.withAlpha(0.85f));
    g.strokePath(duck, juce::PathStrokeType(1.2f));

    // Legend, top left.
    auto legend = bounds.reduced(12.0f, 6.0f).withHeight(14.0f);
    g.setFont(legendFont(9.5f));
    auto key = [&](const juce::String& text, juce::Colour c) {
        g.setColour(c);
        g.fillEllipse(legend.getX(), legend.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour(kTextDim);
        const float w = (float) juce::GlyphArrangement::getStringWidthInt(g.getCurrentFont(), text) + 22.0f;
        g.drawText(text, legend.withX(legend.getX() + 10.0f).withWidth(w), juce::Justification::centredLeft);
        legend.removeFromLeft(w + 10.0f);
    };
    key("KICK", kText.withAlpha(0.5f));
    key("RUMBLE", kAccent);
    key("DUCK", kDuckLine);

    // Readouts, top right.
    auto readout = bounds.reduced(12.0f, 6.0f).withHeight(14.0f);
    g.setFont(legendFont(9.5f));
    const auto gr = shownGrDb > -0.05f ? juce::String("0.0") : juce::String(shownGrDb, 1);
    const auto out = shownOutDb <= -60.0f ? juce::String("-inf") : juce::String(shownOutDb, 1);
    g.setColour(kDuckLine);
    g.drawText("GR " + gr + " dB", readout.removeFromRight(96.0f), juce::Justification::centredRight);
    g.setColour(kAccent);
    g.drawText("OUT " + out + " dB", readout.removeFromRight(104.0f), juce::Justification::centredRight);
}

// ================================================================= editor

RumbleAudioProcessorEditor::RumbleAudioProcessorEditor(RumbleAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    setLookAndFeel(&lookAndFeel);
    logo = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);
    texture = makeTexture();
    scopeScratch.resize((size_t) RumbleAudioProcessor::kScopeCapacity);

    addAndMakeVisible(canvas);
    canvas.addAndMakeVisible(scope);

    // Grouped by what each control does, roughly in signal-flow order.
    auto& drive = addSection("DRIVE", 0, 4);
    addKnob(drive, "drive", "Input Drive");
    addKnob(drive, "taildrive", "Tail Drive");
    addKnob(drive, "enhance", "Enhance");

    auto& reverb = addSection("REVERB", 0, 4);
    addKnob(reverb, "size", "Size");
    addKnob(reverb, "decay", "Decay");
    addKnob(reverb, "damp", "Damping");
    addKnob(reverb, "lowcut", "Low Cut");
    addKnob(reverb, "diffusion", "Diffusion");
    addKnob(reverb, "mod", "Mod");
    addKnob(reverb, "modrate", "Mod Rate");

    // Pre-delay on the first row, the echo delay on the second.
    auto& timing = addSection("TIMING", 0, 5);
    addKnob(timing, "predelay", "Pre-Delay");
    addToggle(timing, "sync", "Sync");
    addCombo(timing, "div", "Division");
    addKnob(timing, "swing", "Swing");
    addSpacer(timing);
    addKnob(timing, "delaymix", "Delay");
    addKnob(timing, "delaytime", "Delay Time");
    addCombo(timing, "delaydiv", "Delay Div");
    addKnob(timing, "delayfb", "Feedback");
    addToggle(timing, "delaypp", "Ping-Pong");

    auto& filter = addSection("FILTER", 1, 4);
    addCombo(filter, "filtertype", "Type");
    addKnob(filter, "filterhz", "Cutoff");
    addKnob(filter, "filterq", "Resonance");
    addKnob(filter, "tone", "Tone");

    auto& eq = addSection("EQ", 1, 4);
    addKnob(eq, "eqlow", "Weight");
    addKnob(eq, "eqmid", "Body");
    addKnob(eq, "eqmidhz", "Body Freq");
    addKnob(eq, "eqhigh", "Edge");

    auto& duck = addSection("DUCK", 1, 5);
    addKnob(duck, "duck", "Amount");
    addKnob(duck, "duckthresh", "Threshold");
    addKnob(duck, "duckatk", "Attack");
    addKnob(duck, "duckrel", "Release");
    addCombo(duck, "trigger", "Trigger");

    auto& output = addSection("OUTPUT", 1, 4);
    addKnob(output, "width", "Width");
    addKnob(output, "monobelow", "Mono Below");
    addKnob(output, "output", "Output");

    // --- preset bar
    presetBox.setJustificationType(juce::Justification::centredLeft);
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        if (id >= kUserPresetIdBase) {
            const int index = id - kUserPresetIdBase;
            if (juce::isPositiveAndBelow(index, userPresetFiles.size()))
                processor.loadUserPreset(userPresetFiles.getReference(index));
        } else if (id > 0) {
            // No "already loaded" check: picking the current preset again is
            // how you throw away your edits.
            processor.setCurrentProgram(id - 1);
        }
        shownPresetText = {};
        refreshPresetDisplay();
    };
    canvas.addAndMakeVisible(presetBox);

    prevButton.setTooltip("Previous preset");
    nextButton.setTooltip("Next preset");
    prevButton.onClick = [this] { stepPreset(-1); };
    nextButton.onClick = [this] { stepPreset(+1); };
    saveButton.onClick = [this] { promptForPresetName(); };
    deleteButton.onClick = [this] { deleteSelectedPreset(); };
    for (juce::Component* c : { (juce::Component*) &prevButton, (juce::Component*) &nextButton,
                                (juce::Component*) &saveButton, (juce::Component*) &deleteButton })
        canvas.addAndMakeVisible(c);

    rebuildPresetMenu();

    // --- size: design size, scaled; aspect locked; remembered per session
    const int designH = designHeight();
    canvas.setBounds(0, 0, kDesignW, designH);
    layoutCanvas();

    setResizable(true, true);
    setResizeLimits(juce::roundToInt(kDesignW * kMinScale), juce::roundToInt(designH * kMinScale),
                    juce::roundToInt(kDesignW * kMaxScale), juce::roundToInt(designH * kMaxScale));
    getConstrainer()->setFixedAspectRatio((double) kDesignW / (double) designH);
    const float scale = juce::jlimit(kMinScale, kMaxScale, processor.getUiScale());
    setSize(juce::roundToInt(kDesignW * scale), juce::roundToInt(designH * scale));

    // Whatever piled up in the scope FIFO while the editor was closed is
    // stale; start the display from now.
    while (processor.readScope(scopeScratch.data(), (int) scopeScratch.size()) > 0) {}

    refreshPresetDisplay();
    refreshEnablement();
    startTimerHz(60);
}

RumbleAudioProcessorEditor::~RumbleAudioProcessorEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

// -------------------------------------------------------------- building

RumbleAudioProcessorEditor::Section& RumbleAudioProcessorEditor::addSection(const juce::String& name,
                                                                            int column, int perRow) {
    auto* s = sections.add(new Section());
    s->name = name;
    s->column = column;
    s->perRow = perRow;
    return *s;
}

RumbleAudioProcessorEditor::Cell& RumbleAudioProcessorEditor::addCell(Section& s, const juce::String& caption) {
    auto* c = s.cells.add(new Cell());
    c->label.setText(caption.toUpperCase(), juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.setFont(legendFont(9.5f));
    c->label.setColour(juce::Label::textColourId, kLegend);
    c->label.setMinimumHorizontalScale(0.8f);
    canvas.addAndMakeVisible(c->label);
    return *c;
}

void RumbleAudioProcessorEditor::addKnob(Section& s, const char* paramID, const juce::String& caption) {
    auto& c = addCell(s, caption);
    c.paramID = paramID;
    c.slider = std::make_unique<juce::Slider>();
    c.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 84, kReadoutH);
    c.slider->setMouseDragSensitivity(220);
    c.slider->setVelocityBasedMode(false);
    canvas.addAndMakeVisible(*c.slider);
    c.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, *c.slider);
}

void RumbleAudioProcessorEditor::addCombo(Section& s, const char* paramID, const juce::String& caption) {
    auto& c = addCell(s, caption);
    c.paramID = paramID;
    c.combo = std::make_unique<juce::ComboBox>();

    // ComboBoxAttachment does not populate the box; it only maps the selected
    // index to the parameter. The items must exist before it is attached.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter(paramID)))
        c.combo->addItemList(choice->choices, 1);

    canvas.addAndMakeVisible(*c.combo);
    c.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, paramID, *c.combo);
}

void RumbleAudioProcessorEditor::addToggle(Section& s, const char* paramID, const juce::String& caption) {
    auto& c = addCell(s, caption);
    c.paramID = paramID;
    c.button = std::make_unique<juce::ToggleButton>("");
    canvas.addAndMakeVisible(*c.button);
    c.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, paramID, *c.button);
}

void RumbleAudioProcessorEditor::addSpacer(Section& s) {
    auto* c = s.cells.add(new Cell());
    c->spacer = true;
}

RumbleAudioProcessorEditor::Cell* RumbleAudioProcessorEditor::findCell(const juce::String& paramID) {
    for (auto* s : sections)
        for (auto* c : s->cells)
            if (c->paramID == paramID)
                return c;
    return nullptr;
}

int RumbleAudioProcessorEditor::designHeight() const {
    int colH[kNumColumns] = {};
    for (auto* s : sections)
        colH[juce::jlimit(0, kNumColumns - 1, s->column)] += sectionHeight(s->cells.size(), s->perRow) + kSectionGap;
    int tallest = 0;
    for (int h : colH) tallest = juce::jmax(tallest, h - kSectionGap);
    return kHeaderH + kScopeH + kSectionGap + tallest + kMargin;
}

// -------------------------------------------------------------- layout

void RumbleAudioProcessorEditor::resized() {
    const float scale = (float) getWidth() / (float) kDesignW;
    canvas.setTransform(juce::AffineTransform::scale(scale));
    processor.setUiScale(scale);
}

void RumbleAudioProcessorEditor::layoutCanvas() {
    auto area = canvas.getLocalBounds().reduced(kMargin, 0);

    // Header: logo and name on the left, preset bar on the right.
    auto header = area.removeFromTop(kHeaderH);
    auto bar = header.withSizeKeepingCentre(header.getWidth(), kPresetH);
    deleteButton.setBounds(bar.removeFromRight(kButtonW));
    bar.removeFromRight(6);
    saveButton.setBounds(bar.removeFromRight(kButtonW));
    bar.removeFromRight(14);
    nextButton.setBounds(bar.removeFromRight(22).withSizeKeepingCentre(14, 14));
    presetBox.setBounds(bar.removeFromRight(kPresetW));
    prevButton.setBounds(bar.removeFromRight(22).withSizeKeepingCentre(14, 14));

    // Scope, on its own plate.
    auto scopePlate = area.removeFromTop(kScopeH);
    scope.setBounds(scopePlate.reduced(kScopePad + 8, kScopePad));
    area.removeFromTop(kSectionGap);

    // Two columns of plates. The shorter column's last plate stretches so
    // both columns finish level.
    const int colW = (area.getWidth() - kColumnGap * (kNumColumns - 1)) / kNumColumns;
    int colY[kNumColumns];
    Section* last[kNumColumns] = {};
    for (int i = 0; i < kNumColumns; ++i) colY[i] = area.getY();

    for (auto* s : sections) {
        const int col = juce::jlimit(0, kNumColumns - 1, s->column);
        const int h = sectionHeight(s->cells.size(), s->perRow);
        s->bounds = { area.getX() + col * (colW + kColumnGap), colY[col], colW, h };
        colY[col] += h + kSectionGap;
        last[col] = s;
    }
    for (auto* s : last)
        if (s != nullptr) s->bounds.setBottom(area.getBottom());

    for (auto* s : sections) {
        auto inner = s->bounds.reduced(kSectionPad, 0).withTrimmedTop(kSectionHead);
        const int rows = rowsFor(s->cells.size(), s->perRow);
        // Centre the rows in whatever height the plate ended up with.
        inner = inner.withSizeKeepingCentre(inner.getWidth(), rows * kCellH)
                     .translated(0, -kSectionPad / 2);

        const int cellW = inner.getWidth() / s->perRow;
        for (int i = 0; i < s->cells.size(); ++i) {
            const int row = i / s->perRow;
            const int inRow = juce::jmin(s->perRow, s->cells.size() - row * s->perRow);
            // A short final row is centred rather than left-aligned.
            const int rowX = inner.getX() + (s->perRow - inRow) * cellW / 2;
            juce::Rectangle<int> cell(rowX + (i % s->perRow) * cellW,
                                      inner.getY() + row * kCellH, cellW, kCellH);
            cell.reduce(3, 2);

            auto* c = s->cells[i];
            if (c->spacer) continue;
            c->label.setBounds(cell.removeFromTop(kCaptionH));

            if (c->slider != nullptr) {
                c->slider->setBounds(cell);
            } else if (c->combo != nullptr) {
                c->combo->setBounds(cell.withSizeKeepingCentre(juce::jmin(cell.getWidth(), 96), 26)
                                        .translated(0, -6));
            } else if (c->button != nullptr) {
                c->button->setBounds(cell.withSizeKeepingCentre(34, 34).translated(0, -6));
            }
        }
    }
}

// ------------------------------------------------------------- painting

void RumbleAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(kChassis);   // only visible for a frame while the canvas rescales
}

void RumbleAudioProcessorEditor::paintCanvas(juce::Graphics& g) {
    const auto bounds = canvas.getLocalBounds();

    // Chassis: dark metal with grain and a soft vignette.
    g.fillAll(kChassis);
    g.setTiledImageFill(texture, 0, 0, 1.0f);
    g.fillRect(bounds);
    g.setGradientFill(juce::ColourGradient(juce::Colours::transparentBlack,
                                           bounds.getCentreX(), bounds.getCentreY() * 0.8f,
                                           juce::Colour(0x70000000), 0.0f, (float) bounds.getBottom(), true));
    g.fillRect(bounds);

    // Header strip.
    auto header = bounds.withHeight(kHeaderH);
    g.setGradientFill(juce::ColourGradient(kChassisLift, 0, 0, kChassis, 0, (float) kHeaderH, false));
    g.fillRect(header);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawHorizontalLine(kHeaderH - 1, 0.0f, (float) bounds.getWidth());
    g.setColour(kPlateLift);
    g.drawHorizontalLine(kHeaderH, 0.0f, (float) bounds.getWidth());

    auto brand = header.reduced(kMargin, 0);
    if (logo.isValid()) {
        auto badge = brand.removeFromLeft(kLogoSize).withSizeKeepingCentre(kLogoSize, kLogoSize);
        g.setOpacity(1.0f);   // images inherit the alpha of the last colour set
        g.drawImageWithin(logo, badge.getX(), badge.getY(), badge.getWidth(), badge.getHeight(),
                          juce::RectanglePlacement::centred, false);
        brand.removeFromLeft(12);
    }
    g.setColour(kText);
    g.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)).withExtraKerningFactor(0.22f));
    g.drawText("RUMBLE", brand.withY(9).withHeight(30), juce::Justification::centredLeft);
    g.setColour(kTextDim);
    g.setFont(legendFont(9.0f, false));
    g.drawText("KICK REVERB  /  DUCKER  /  ECHO   v" JucePlugin_VersionString,
               brand.withY(41).withHeight(13), juce::Justification::centredLeft);

    // Preset label, left of the arrows.
    g.setColour(kLegend);
    g.setFont(legendFont(9.5f));
    g.drawText("PRESET", prevButton.getBounds().withWidth(60).translated(-66, 0).expanded(0, 6),
               juce::Justification::centredRight);

    // Scope plate.
    drawPlate(g, scope.getBounds().expanded(kScopePad + 8, kScopePad), true);

    // Section plates with an indicator lamp and silkscreened name.
    for (auto* s : sections) {
        drawPlate(g, s->bounds);
        auto head = s->bounds.withHeight(kSectionHead).reduced(kSectionPad + 10, 0);
        drawLed(g, { (float) head.getX() + 3.0f, (float) head.getCentreY() + 1.0f }, 2.6f, true);
        g.setColour(kLegend);
        g.setFont(legendFont(10.5f));
        g.drawText(s->name, head.withTrimmedLeft(12).translated(0, 1), juce::Justification::centredLeft);

        // Engraved rule under the name.
        const float y = (float) s->bounds.getY() + kSectionHead - 2.0f;
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.drawHorizontalLine((int) y, (float) s->bounds.getX() + kSectionPad, (float) s->bounds.getRight() - kSectionPad);
        g.setColour(kPlateLift);
        g.drawHorizontalLine((int) y + 1, (float) s->bounds.getX() + kSectionPad, (float) s->bounds.getRight() - kSectionPad);
    }
}

// --------------------------------------------------------------- timer

void RumbleAudioProcessorEditor::timerCallback() {
    const int n = processor.readScope(scopeScratch.data(), (int) scopeScratch.size());
    if (n > 0) scope.push(scopeScratch.data(), n);

    refreshPresetDisplay();
    refreshEnablement();
}

// Show the loaded preset -- including after the editor was closed and
// reopened, or the session reloaded -- and mark it once anything has moved.
void RumbleAudioProcessorEditor::refreshPresetDisplay() {
    const auto name = processor.getPresetName();
    const bool modified = processor.isPresetModified();
    const auto text = modified ? name + " *" : name;
    if (text == shownPresetText)
        return;
    shownPresetText = text;

    if (modified) {
        // No item is selected while edited, so choosing the preset again
        // fires onChange and reloads it.
        presetBox.setText(text, juce::dontSendNotification);
    } else {
        int match = 0;
        for (int i = 0; i < presetBox.getNumItems(); ++i)
            if (presetBox.getItemText(i) == name) { match = presetBox.getItemId(i); break; }
        if (match != 0) presetBox.setSelectedId(match, juce::dontSendNotification);
        else            presetBox.setText(text, juce::dontSendNotification);
    }
    deleteButton.setEnabled(presetBox.getSelectedId() >= kUserPresetIdBase);
}

// Grey out controls that currently have no effect.
void RumbleAudioProcessorEditor::refreshEnablement() {
    const bool sync = processor.apvts.getRawParameterValue("sync")->load() >= 0.5f;
    auto setLive = [this](const char* id, bool live) {
        if (auto* c = findCell(id)) {
            for (juce::Component* comp : { (juce::Component*) &c->label,
                                           (juce::Component*) c->slider.get(),
                                           (juce::Component*) c->combo.get() }) {
                if (comp == nullptr || comp->isEnabled() == live) continue;
                comp->setEnabled(live);
                comp->setAlpha(live ? 1.0f : 0.4f);
            }
        }
    };
    setLive("predelay", ! sync);   // overridden by the tempo grid
    setLive("div", sync);
    setLive("swing", sync);
    setLive("delaytime", ! sync);  // Sync swaps the ms knob for a division
    setLive("delaydiv", sync);
}

// -------------------------------------------------------------- presets

// Step through the whole list, factory then user, wrapping at the ends.
// While edited, steps from the preset the edits started from.
void RumbleAudioProcessorEditor::stepPreset(int delta) {
    const int count = presetBox.getNumItems();
    if (count == 0) return;

    int index = presetBox.getSelectedItemIndex();
    if (index < 0) {
        const auto name = processor.getPresetName();
        for (int i = 0; i < count; ++i)
            if (presetBox.getItemText(i) == name) { index = i; break; }
    }
    index = index < 0 ? 0 : (index + delta + count) % count;
    // setSelectedItemIndex does not notify if the item is already selected,
    // so go through the id, which reloads -- the same as picking it.
    presetBox.setSelectedId(0, juce::dontSendNotification);
    presetBox.setSelectedItemIndex(index, juce::sendNotificationSync);
}

void RumbleAudioProcessorEditor::rebuildPresetMenu() {
    const int previous = presetBox.getSelectedId();

    presetBox.clear(juce::dontSendNotification);
    userPresetFiles = processor.userPresets();

    presetBox.addSectionHeading("Factory");
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presetBox.addItem(processor.getProgramName(i), i + 1);

    if (! userPresetFiles.isEmpty()) {
        presetBox.addSeparator();
        presetBox.addSectionHeading("User");
        for (int i = 0; i < userPresetFiles.size(); ++i) {
            // Prefer the name stored inside the file; the filename may have
            // been sanitised when it was saved.
            juce::String name = userPresetFiles.getReference(i).getFileNameWithoutExtension();
            if (auto xml = juce::parseXML(userPresetFiles.getReference(i)))
                name = xml->getStringAttribute("presetName", name);
            presetBox.addItem(name, kUserPresetIdBase + i);
        }
    }

    presetBox.setSelectedId(previous, juce::dontSendNotification);
    shownPresetText = {};
    refreshPresetDisplay();
}

void RumbleAudioProcessorEditor::promptForPresetName() {
    auto* window = new juce::AlertWindow("Save Preset",
                                         "Name this preset:",
                                         juce::MessageBoxIconType::NoIcon);
    window->setLookAndFeel(&lookAndFeel);
    window->addTextEditor("name", "My Rumble");
    window->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    // Modal state with a callback, so no modal loop is needed.
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, window](int result) {
            if (result != 1)
                return;

            const auto name = window->getTextEditorContents("name");
            const auto outcome = processor.saveUserPreset(name);

            if (outcome.wasOk()) {
                rebuildPresetMenu();
            } else {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                       "Could not save preset",
                                                       outcome.getErrorMessage());
            }
        }), true);
}

void RumbleAudioProcessorEditor::deleteSelectedPreset() {
    const int id = presetBox.getSelectedId();
    if (id < kUserPresetIdBase)
        return;   // factory presets are not removable

    const int index = id - kUserPresetIdBase;
    if (! juce::isPositiveAndBelow(index, userPresetFiles.size()))
        return;

    const auto file = userPresetFiles.getReference(index);
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
        "Delete preset",
        "Delete \"" + file.getFileNameWithoutExtension() + "\"? This cannot be undone.",
        "Delete", "Cancel", nullptr,
        juce::ModalCallbackFunction::create([this, file](int result) {
            if (result == 1) {
                file.deleteFile();
                presetBox.setSelectedId(0, juce::dontSendNotification);
                rebuildPresetMenu();
            }
        }));
}
