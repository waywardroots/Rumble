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

int rowsFor(int slots, int perRow) { return juce::jmax(1, (slots + perRow - 1) / perRow); }

int sectionHeightForSlots(int slots, int perRow) {
    return kSectionHead + rowsFor(slots, perRow) * kCellH + kSectionPad;
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

// =============================================================== mod knob

namespace {
constexpr float kDefaultDropDepth = 0.25f;
constexpr float kDepthDragPixels  = 140.0f;   // drag this far for the full -1..1 swing / 2

juce::String depthText(float d) {
    return (d >= 0.0f ? "+" : "-") + juce::String(juce::roundToInt(std::abs(d) * 100.0f)) + "%";
}
int lfoFromDescription(const juce::var& description) {
    const auto s = description.toString();
    return s.startsWith("lfo:") ? s.substring(4).getIntValue() : -1;
}
} // namespace

ModKnob::ModKnob(RumbleAudioProcessor& p, int index) : processor(p), paramIndex(index) {}

bool ModKnob::isModulated() const {
    for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k)
        if (std::abs(processor.getModDepth(k, paramIndex)) > 0.0f) return true;
    return false;
}

ModKnob::Geometry ModKnob::geometry() const {
    // Must match RumbleLookAndFeel::drawRotarySlider, which draws into the
    // same slider bounds.
    auto* self = const_cast<ModKnob*>(this);
    const auto b = getLookAndFeel().getSliderLayout(*self).sliderBounds.toFloat();
    return { b.getCentre(), juce::jmin(b.getWidth(), b.getHeight()) };
}

// LFO 1's ring sits on the printed scale, LFO 2's just inside it.
float ModKnob::ringRadius(int lfo) const {
    return geometry().dia * 0.5f - (lfo == 0 ? 2.0f : 5.2f);
}

int ModKnob::ringUnder(juce::Point<float> p) const {
    const auto g = geometry();
    const float r = p.getDistanceFrom(g.centre);
    // Anywhere outside the value arc counts, so the rings are easy to grab.
    if (r < g.dia * 0.5f - 7.0f || r > g.dia * 0.5f + 3.0f) return -1;
    int best = -1;
    float bestDist = 1.0e9f;
    for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k) {
        if (! (std::abs(processor.getModDepth(k, paramIndex)) > 0.0f)) continue;
        const float d = std::abs(r - ringRadius(k));
        if (d < bestDist) { bestDist = d; best = k; }
    }
    return best;
}

void ModKnob::paint(juce::Graphics& g) {
    juce::Slider::paint(g);   // the knob itself, via the look and feel

    const auto geo = geometry();
    const auto rot = getRotaryParameters();
    auto angleAt = [&](float norm) {
        return rot.startAngleRadians + juce::jlimit(0.0f, 1.0f, norm) * (rot.endAngleRadians - rot.startAngleRadians);
    };
    const float base = (float) valueToProportionOfLength(getValue());

    // A knob something is being dragged over glows in that LFO's colour.
    if (dropHoverLfo >= 0) {
        const auto c = kLfoColours[dropHoverLfo];
        const float r = geo.dia * 0.5f;
        g.setColour(c.withAlpha(0.18f));
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(geo.centre));
        g.setColour(c);
        g.drawEllipse(juce::Rectangle<float>(r * 2.0f - 2.0f, r * 2.0f - 2.0f).withCentre(geo.centre), 1.5f);
    }

    bool any = false;
    for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k) {
        const float d = processor.getModDepth(k, paramIndex);
        if (! (std::abs(d) > 0.0f)) continue;
        any = true;
        const auto colour = kLfoColours[k];
        const bool hot = (k == editingLfo || k == ringHoverLfo);
        const float r = ringRadius(k);

        // Background track for the ring, so it reads over the printed scale.
        juce::Path track;
        track.addCentredArc(geo.centre.x, geo.centre.y, r, r, 0.0f, rot.startAngleRadians, rot.endAngleRadians, true);
        g.setColour(kTrack.withAlpha(0.85f));
        g.strokePath(track, juce::PathStrokeType(3.0f));

        // The range this LFO sweeps the knob through.
        juce::Path range;
        range.addCentredArc(geo.centre.x, geo.centre.y, r, r, 0.0f,
                            angleAt(base - std::abs(d)), angleAt(base + std::abs(d)), true);
        if (hot) {
            g.setColour(colour.withAlpha(0.35f));
            g.strokePath(range, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour(colour.withAlpha(hot ? 1.0f : 0.85f));
        g.strokePath(range, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Where the knob actually is right now: its setting moved by each LFO's
    // current output -- the same sum the audio thread makes.
    if (any) {
        float depths[RumbleAudioProcessor::kNumLfos], values[RumbleAudioProcessor::kNumLfos];
        for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k) {
            depths[k] = processor.getModDepth(k, paramIndex);
            values[k] = processor.getLfoValue(k);
        }
        const float a = angleAt(rumble::modulateNormalised(base, depths, values, RumbleAudioProcessor::kNumLfos));
        const float r = geo.dia * 0.5f - 7.5f;
        const juce::Point<float> p(geo.centre.x + r * std::sin(a), geo.centre.y - r * std::cos(a));
        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.fillEllipse(juce::Rectangle<float>(9.0f, 9.0f).withCentre(p));
        g.setColour(juce::Colours::white);
        g.fillEllipse(juce::Rectangle<float>(4.6f, 4.6f).withCentre(p));
    }
}

// While a depth is being dragged, the readout shows the depth instead.
void ModKnob::paintOverChildren(juce::Graphics& g) {
    if (editingLfo < 0) return;
    auto* self = const_cast<ModKnob*>(this);
    const auto box = getLookAndFeel().getSliderLayout(*self).textBoxBounds.toFloat().reduced(4.0f, 0.0f);
    drawScreen(g, box, 3.0f);
    g.setColour(kLfoColours[editingLfo]);
    g.setFont(legendFont(10.0f));
    g.drawText("LFO " + juce::String(editingLfo + 1) + " " + depthText(processor.getModDepth(editingLfo, paramIndex)),
               box, juce::Justification::centred);
}

void ModKnob::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) { showMenu(); return; }
    const int k = ringUnder(e.position);
    if (k >= 0 && isEnabled()) {
        editingLfo = k;
        depthAtDragStart = processor.getModDepth(k, paramIndex);
        repaint();
        return;
    }
    juce::Slider::mouseDown(e);
}

void ModKnob::mouseDrag(const juce::MouseEvent& e) {
    if (editingLfo >= 0) {
        // Up for more, down for less; through zero inverts. The drag works
        // from where it started, so passing zero and coming back is fine.
        const float d = depthAtDragStart - (float) e.getDistanceFromDragStartY() / kDepthDragPixels;
        processor.setModDepth(editingLfo, paramIndex, juce::jlimit(-1.0f, 1.0f, d));
        repaint();
        return;
    }
    juce::Slider::mouseDrag(e);
}

void ModKnob::mouseUp(const juce::MouseEvent& e) {
    if (editingLfo >= 0) { editingLfo = -1; repaint(); return; }
    juce::Slider::mouseUp(e);
}

void ModKnob::mouseMove(const juce::MouseEvent& e) {
    const int k = isEnabled() ? ringUnder(e.position) : -1;
    if (k != ringHoverLfo) {
        ringHoverLfo = k;
        setMouseCursor(k >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    juce::Slider::mouseMove(e);
}

void ModKnob::mouseExit(const juce::MouseEvent& e) {
    if (ringHoverLfo >= 0) { ringHoverLfo = -1; setMouseCursor(juce::MouseCursor::NormalCursor); repaint(); }
    juce::Slider::mouseExit(e);
}

bool ModKnob::isInterestedInDragSource(const SourceDetails& d) {
    return lfoFromDescription(d.description) >= 0 && processor.isModulatable(paramIndex);
}

void ModKnob::itemDragEnter(const SourceDetails& d) { dropHoverLfo = lfoFromDescription(d.description); repaint(); }
void ModKnob::itemDragExit(const SourceDetails&)    { dropHoverLfo = -1; repaint(); }

void ModKnob::itemDropped(const SourceDetails& d) {
    const int k = lfoFromDescription(d.description);
    dropHoverLfo = -1;
    // Dropping onto a knob the LFO already drives keeps its depth.
    if (k >= 0 && ! (std::abs(processor.getModDepth(k, paramIndex)) > 0.0f))
        processor.setModDepth(k, paramIndex, kDefaultDropDepth);
    repaint();
}

void ModKnob::showMenu() {
    juce::PopupMenu m;
    bool any = false;
    for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k) {
        const float d = processor.getModDepth(k, paramIndex);
        if (! (std::abs(d) > 0.0f)) continue;
        any = true;
        m.addSectionHeader("LFO " + juce::String(k + 1) + "   depth " + depthText(d));
        m.addItem(10 + k, "Invert");
        m.addItem(20 + k, "Remove");
    }
    if (any) {
        m.addSeparator();
        m.addItem(1, "Remove all modulation");
    } else {
        m.addItem(-1, processor.isModulatable(paramIndex) ? "Drag an LFO onto this knob to modulate it"
                                                          : "This knob can't be modulated", false, false);
    }
    m.setLookAndFeel(&getLookAndFeel());
    juce::Component::SafePointer<ModKnob> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int result) {
        if (safe == nullptr || result <= 0) return;
        auto& p = safe->processor;
        if (result == 1)
            for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k) p.setModDepth(k, safe->paramIndex, 0.0f);
        else if (result >= 20) p.setModDepth(result - 20, safe->paramIndex, 0.0f);
        else if (result >= 10) p.setModDepth(result - 10, safe->paramIndex, -p.getModDepth(result - 10, safe->paramIndex));
        safe->repaint();
    });
}

// ============================================================= LFO source

LfoSource::LfoSource(RumbleAudioProcessor& p, int index) : processor(p), lfo(index) {
    shapeParam = processor.apvts.getRawParameterValue("lfo" + juce::String(index + 1) + "shape");
}

void LfoSource::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    const auto colour = kLfoColours[lfo];
    drawScreen(g, bounds, 4.0f);

    // Header: the name. Footer: how many knobs it drives, or how to start.
    auto head = bounds.reduced(8.0f, 4.0f).withHeight(13.0f);
    auto foot = bounds.reduced(8.0f, 3.0f).removeFromBottom(11.0f);
    int targets = 0;
    for (int i = 0; i < processor.getParameters().size(); ++i)
        targets += std::abs(processor.getModDepth(lfo, i)) > 0.0f ? 1 : 0;
    g.setFont(legendFont(9.5f));
    g.setColour(colour);
    g.fillEllipse(head.getX(), head.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.drawText("LFO " + juce::String(lfo + 1), head.withTrimmedLeft(10.0f), juce::Justification::centredLeft);
    g.setFont(legendFont(8.0f, false));
    g.setColour(targets == 0 ? kTextDim : colour.withAlpha(0.8f));
    g.drawFittedText(targets == 0 ? juce::String("DRAG ONTO A KNOB")
                                  : juce::String(targets) + (targets == 1 ? " TARGET" : " TARGETS"),
                     foot.toNearestInt(), juce::Justification::centred, 1, 0.75f);

    // One cycle of the shape (four for the random ones, so they read as
    // random), with the playhead at the current phase.
    const auto plot = bounds.reduced(10.0f, 6.0f).withTrimmedTop(14.0f).withTrimmedBottom(10.0f);
    const auto shape = (rumble::LfoShape) juce::jlimit(0, rumble::kNumLfoShapes - 1,
                                                       shapeParam != nullptr ? (int) shapeParam->load() : 0);
    const bool random = shape == rumble::LfoShape::SampleHold || shape == rumble::LfoShape::SmoothRandom;
    const int cycles = random ? 4 : 1;
    auto yOf = [&](float v) { return plot.getCentreY() - v * plot.getHeight() * 0.42f; };

    g.setColour(kGrid);
    g.drawHorizontalLine((int) plot.getCentreY(), plot.getX(), plot.getRight());

    juce::Path wave;
    const int points = 120;
    for (int i = 0; i <= points; ++i) {
        const double x = (double) i / points * cycles;
        const auto n = (std::int64_t) std::floor(juce::jmin(x, (double) cycles - 1.0e-9));
        const float v = rumble::lfoShapeValue(shape, x - (double) n,
                                              rumble::lfoRandom(n, 99), rumble::lfoRandom(n - 1, 99));
        const juce::Point<float> pt(plot.getX() + plot.getWidth() * (float) i / points, yOf(v));
        if (i == 0) wave.startNewSubPath(pt); else wave.lineTo(pt);
    }
    g.setColour(colour.withAlpha(0.25f));
    g.strokePath(wave, juce::PathStrokeType(4.0f));
    g.setColour(colour);
    g.strokePath(wave, juce::PathStrokeType(1.5f));

    const float phase = (float) processor.getLfoPhase(lfo);
    const float px = plot.getX() + plot.getWidth() * (random ? 1.0f : phase);
    const float py = yOf(processor.getLfoValue(lfo));
    if (! random) {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        g.drawVerticalLine((int) px, plot.getY(), plot.getBottom());
    }
    g.setColour(juce::Colours::white);
    g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({ px, py }));
}

void LfoSource::mouseDrag(const juce::MouseEvent& e) {
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr || container->isDragAndDropActive() || e.getDistanceFromDragStart() < 4)
        return;

    // A small puck in the LFO's colour follows the mouse.
    juce::Image puck(juce::Image::ARGB, 30, 30, true);
    {
        juce::Graphics g(puck);
        g.setColour(kLfoColours[lfo].withAlpha(0.35f));
        g.fillEllipse(0.0f, 0.0f, 30.0f, 30.0f);
        g.setColour(kLfoColours[lfo]);
        g.fillEllipse(5.0f, 5.0f, 20.0f, 20.0f);
        g.setColour(kChassis);
        g.setFont(legendFont(11.0f));
        g.drawText(juce::String(lfo + 1), juce::Rectangle<int>(0, 0, 30, 30), juce::Justification::centred);
    }
    container->startDragging("lfo:" + juce::String(lfo), this, juce::ScaledImage(puck));
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
    addKnob(drive, "drivemix", "Dry/Wet");

    auto& reverb = addSection("REVERB", 0, 4);
    addKnob(reverb, "size", "Size");
    addKnob(reverb, "decay", "Decay");
    addKnob(reverb, "damp", "Damping");
    addKnob(reverb, "lowcut", "Low Cut");
    addKnob(reverb, "diffusion", "Diffusion");
    addKnob(reverb, "mod", "Mod");
    addKnob(reverb, "modrate", "Mod Rate");
    addKnob(reverb, "reverbmix", "Dry/Wet");

    auto& timing = addSection("TIMING", 0, 4);
    addKnob(timing, "predelay", "Pre-Delay");
    addToggle(timing, "sync", "Sync");
    addCombo(timing, "div", "Division");
    addKnob(timing, "swing", "Swing");

    // The echo has its own plate. It still follows TIMING's Sync switch.
    auto& delay = addSection("DELAY", 0, 5);
    addKnob(delay, "delaymix", "Level");
    addKnob(delay, "delaytime", "Time");
    addCombo(delay, "delaydiv", "Division");
    addKnob(delay, "delayfb", "Feedback");
    addToggle(delay, "delaypp", "Ping-Pong");

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

    auto& duck = addSection("DUCK", 1, 4);
    addKnob(duck, "duck", "Amount");
    addKnob(duck, "duckthresh", "Threshold");
    addKnob(duck, "duckatk", "Attack");
    addKnob(duck, "duckrel", "Release");
    addCombo(duck, "trigger", "Trigger");

    auto& output = addSection("OUTPUT", 1, 4);
    addKnob(output, "width", "Width");
    addKnob(output, "monobelow", "Mono Below");
    addKnob(output, "output", "Output");

    // The LFOs, one plate each across the bottom. Drag from the display
    // onto any knob.
    for (int k = 0; k < RumbleAudioProcessor::kNumLfos; ++k) {
        const juce::String n(k + 1);
        auto& lfo = addSection("LFO " + n, k, 7);
        addLfoSource(lfo, k);
        addCombo(lfo, ("lfo" + n + "shape").toRawUTF8(), "Shape");
        lfo.cells.getLast()->span = 2;
        addKnob(lfo, ("lfo" + n + "rate").toRawUTF8(), "Rate");
        addCombo(lfo, ("lfo" + n + "div").toRawUTF8(), "Division");
        addToggle(lfo, ("lfo" + n + "sync").toRawUTF8(), "Sync");
    }

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
    auto knob = std::make_unique<ModKnob>(processor, processor.apvts.getParameter(paramID)->getParameterIndex());
    modKnobs.add(knob.get());
    c.slider = std::move(knob);
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

void RumbleAudioProcessorEditor::addLfoSource(Section& s, int lfo) {
    auto* c = s.cells.add(new Cell());
    c->span = 2;
    auto source = std::make_unique<LfoSource>(processor, lfo);
    lfoSources.add(source.get());
    c->custom = std::move(source);
    canvas.addAndMakeVisible(*c->custom);
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

int RumbleAudioProcessorEditor::slotsIn(const Section& s) {
    int slots = 0;
    for (auto* c : s.cells) slots += c->span;
    return slots;
}

int RumbleAudioProcessorEditor::sectionHeight(const Section& s) {
    return sectionHeightForSlots(slotsIn(s), s.perRow);
}

int RumbleAudioProcessorEditor::designHeight() const {
    int colH[kNumColumns] = {};
    for (auto* s : sections)
        colH[juce::jlimit(0, kNumColumns - 1, s->column)] += sectionHeight(*s) + kSectionGap;
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
        const int h = sectionHeight(*s);
        s->bounds = { area.getX() + col * (colW + kColumnGap), colY[col], colW, h };
        colY[col] += h + kSectionGap;
        last[col] = s;
    }
    for (auto* s : last)
        if (s != nullptr) s->bounds.setBottom(area.getBottom());

    for (auto* s : sections) {
        auto inner = s->bounds.reduced(kSectionPad, 0).withTrimmedTop(kSectionHead);
        const int slots = slotsIn(*s);
        const int rows = rowsFor(slots, s->perRow);
        // Centre the rows in whatever height the plate ended up with.
        inner = inner.withSizeKeepingCentre(inner.getWidth(), rows * kCellH)
                     .translated(0, -kSectionPad / 2);

        const int cellW = inner.getWidth() / s->perRow;
        int slot = 0;
        for (int i = 0; i < s->cells.size(); ++i) {
            auto* c = s->cells[i];
            // A cell never straddles two rows.
            if (slot % s->perRow + c->span > s->perRow) slot += s->perRow - slot % s->perRow;
            const int row = slot / s->perRow;
            const int inRow = juce::jmin(s->perRow, slots - row * s->perRow);
            // A short final row is centred rather than left-aligned.
            const int rowX = inner.getX() + (s->perRow - inRow) * cellW / 2;
            juce::Rectangle<int> cell(rowX + (slot % s->perRow) * cellW,
                                      inner.getY() + row * kCellH, cellW * c->span, kCellH);
            slot += c->span;
            cell.reduce(3, 2);

            if (c->spacer) continue;
            if (c->custom != nullptr) {          // fills the whole cell, no caption
                c->custom->setBounds(cell.reduced(2, 8));
                continue;
            }
            c->label.setBounds(cell.removeFromTop(kCaptionH));

            if (c->slider != nullptr) {
                c->slider->setBounds(cell);
            } else if (c->combo != nullptr) {
                c->combo->setBounds(cell.withSizeKeepingCentre(juce::jmin(cell.getWidth(), 96 * c->span), 26)
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

    for (auto* source : lfoSources) source->repaint();
    for (auto* knob : modKnobs)
        if (knob->isModulated()) knob->repaint();

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

    for (int k = 1; k <= RumbleAudioProcessor::kNumLfos; ++k) {
        const juce::String n(k);
        const bool lfoSync = processor.apvts.getRawParameterValue("lfo" + n + "sync")->load() >= 0.5f;
        setLive(("lfo" + n + "rate").toRawUTF8(), ! lfoSync);
        setLive(("lfo" + n + "div").toRawUTF8(), lfoSync);
    }
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
