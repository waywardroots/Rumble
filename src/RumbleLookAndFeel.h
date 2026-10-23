#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <BinaryData.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <unordered_map>

// Rumble's "hardware" look: a dark machined faceplate with amber accents.
// Knobs are drawn as knurled metal caps over a printed scale, panels as
// screwed-on plates, readouts and the scope as recessed screens.

namespace rumble_ui {

// ---------------------------------------------------------------- palette

const juce::Colour kChassis     { 0xff17181c };   // behind everything
const juce::Colour kChassisLift { 0xff1f2126 };
const juce::Colour kPlateTop    { 0xff2a2c32 };
const juce::Colour kPlateBottom { 0xff202227 };
const juce::Colour kPlateEdge   { 0xff0d0e10 };
const juce::Colour kPlateLift   { 0x1effffff };   // bevel highlight
const juce::Colour kShadow      { 0xa0000000 };

const juce::Colour kScreen      { 0xff0b0c0e };   // recessed displays
const juce::Colour kScreenEdge  { 0xff050506 };
const juce::Colour kGrid        { 0xff1d1f24 };

const juce::Colour kCapLight    { 0xff6a6d75 };
const juce::Colour kCapMid      { 0xff3b3e45 };
const juce::Colour kCapDark     { 0xff1c1d21 };
const juce::Colour kKnurl       { 0xff141518 };
const juce::Colour kTrack       { 0xff0e0f12 };

const juce::Colour kAccent      { 0xffe8a33d };   // amber
const juce::Colour kAccentHot   { 0xffffc670 };
const juce::Colour kAccentGlow  { 0x55e8a33d };
const juce::Colour kPointer     { 0xfff4eee2 };
const juce::Colour kScale       { 0xff4a4d55 };   // printed tick marks

const juce::Colour kText        { 0xffe6e1d7 };
const juce::Colour kTextDim     { 0xff8d8f99 };
const juce::Colour kLegend      { 0xffb7aa96 };   // silkscreen captions
const juce::Colour kDuckLine    { 0xffcfe3f0 };   // gain reduction on the scope

// Modulation colours, one per LFO. Kept clear of the amber accent so a
// modulated knob reads differently from the knob's own setting.
const juce::Colour kLfoColours[2] = { juce::Colour(0xff4fd1c5), juce::Colour(0xffb48cff) };

// ------------------------------------------------------------ typography

// Inter (SIL OFL 1.1), embedded so the plugin looks the same on every
// machine. It is a variable font: one file, any weight from 100 to 900.
inline juce::Typeface::Ptr interTypeface() {
    static const juce::Typeface::Ptr typeface =
        juce::Typeface::createSystemTypefaceFor(BinaryData::InterVariable_ttf,
                                                (size_t) BinaryData::InterVariable_ttfSize);
    return typeface;
}

// Inter at a given weight. JUCE only applies variable settings to fonts it
// looks up by name -- a font given a typeface object directly ignores them --
// so each weight is cloned from the embedded typeface once and cached.
inline juce::Typeface::Ptr interAtWeight(float weight) {
    static std::unordered_map<int, juce::Typeface::Ptr> clones;
    const int key = juce::roundToInt(juce::jlimit(100.0f, 900.0f, weight));
    auto it = clones.find(key);
    if (it == clones.end()) {
        const std::array<juce::FontVariableSetting, 1> axes { juce::FontVariableSetting { "wght", (float) key } };
        auto clone = interTypeface()->cloneWithVariableSettings(axes);
        it = clones.emplace(key, clone != nullptr ? clone : interTypeface()).first;
    }
    return it->second;
}

// A UI font at any weight (400 regular, 600 semibold, 800 extra bold).
// `tabular` gives every digit the same width, so changing numbers don't
// shuffle sideways.
inline juce::Font uiFont(float height, float weight = 400.0f, bool tabular = false) {
    auto options = juce::FontOptions().withTypeface(interAtWeight(weight)).withHeight(height);
    if (tabular)
        options = options.withFeatureEnabled("tnum");
    return juce::Font(options);
}

// Letter-spaced small caps for panel headers and captions.
inline juce::Font legendFont(float size, bool bold = true) {
    return uiFont(size, bold ? 640.0f : 460.0f).withExtraKerningFactor(0.12f);
}

// ----------------------------------------------------------------- glow

// Real, Gaussian-blurred glow. `draw` renders the shape that should glow
// into a transparent image the size of `area` (in the caller's
// coordinates); the image is blurred and drawn behind the crisp shape.
// Built images are cached, keyed by everything that affects how they look,
// so a glow costs one blur when it changes and nothing while it doesn't.
class GlowCache {
public:
    template <typename DrawFn>
    void draw(juce::Graphics& g, std::uint64_t key, juce::Rectangle<float> area,
              float blurRadius, float opacity, DrawFn&& drawShape) {
        const auto padded = area.expanded(blurRadius * 2.5f).getSmallestIntegerContainer();
        key = mix(key, (std::uint64_t) padded.getWidth() << 20 | (std::uint64_t) padded.getHeight());
        key = mix(key, (std::uint64_t) std::lround(blurRadius * 16.0f));

        auto it = images.find(key);
        if (it == images.end()) {
            juce::Image img(juce::Image::ARGB, juce::jmax(1, padded.getWidth()), juce::jmax(1, padded.getHeight()), true);
            {
                juce::Graphics gi(img);
                gi.addTransform(juce::AffineTransform::translation((float) -padded.getX(), (float) -padded.getY()));
                drawShape(gi);
            }
            img.getPixelData()->applyGaussianBlurEffect(blurRadius);
            it = images.emplace(key, img).first;
            order.push_back(key);
            if (order.size() > kMaxEntries) { images.erase(order.front()); order.pop_front(); }
        }
        g.setOpacity(opacity);
        g.drawImageAt(it->second, padded.getX(), padded.getY());
        g.setOpacity(1.0f);
    }

    static std::uint64_t mix(std::uint64_t h, std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return h;
    }

private:
    static constexpr size_t kMaxEntries = 256;
    std::unordered_map<std::uint64_t, juce::Image> images;
    std::deque<std::uint64_t> order;
};

inline GlowCache& glowCache() { static GlowCache cache; return cache; }

// ------------------------------------------------------------ shared bits

// A small slotted screw head, as on a rack panel.
inline void drawScrew(juce::Graphics& g, juce::Point<float> c, float r, float slotAngle) {
    g.setGradientFill(juce::ColourGradient(kCapLight, c.x - r, c.y - r,
                                           kCapDark, c.x + r, c.y + r, false));
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour(kPlateEdge);
    g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);
    juce::Path slot;
    slot.addRectangle(-r * 0.75f, -r * 0.14f, r * 1.5f, r * 0.28f);
    slot.applyTransform(juce::AffineTransform::rotation(slotAngle).translated(c.x, c.y));
    g.setColour(kPlateEdge.withAlpha(0.9f));
    g.fillPath(slot);
}

// A screwed-on faceplate: cast shadow, brushed gradient, bevel, screws.
inline void drawPlate(juce::Graphics& g, juce::Rectangle<int> bounds, bool screws = true) {
    const auto r = bounds.toFloat();
    const float corner = 4.0f;

    juce::Path p;
    p.addRoundedRectangle(r, corner);
    juce::DropShadow(kShadow, 12, { 0, 4 }).drawForPath(g, p);

    g.setGradientFill(juce::ColourGradient(kPlateTop, r.getCentreX(), r.getY(),
                                           kPlateBottom, r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, corner);

    // Faint horizontal brushing.
    g.setColour(juce::Colour(0x05ffffff));
    for (float y = r.getY() + 3.0f; y < r.getBottom() - 2.0f; y += 3.0f)
        g.drawHorizontalLine((int) y, r.getX() + 2.0f, r.getRight() - 2.0f);

    g.setColour(kPlateEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
    g.setColour(kPlateLift);
    g.drawLine(r.getX() + corner, r.getY() + 1.5f, r.getRight() - corner, r.getY() + 1.5f, 1.0f);

    if (screws) {
        const float s = 3.2f, inset = 7.5f;
        drawScrew(g, { r.getX() + inset,     r.getY() + inset },      s, 0.6f);
        drawScrew(g, { r.getRight() - inset, r.getY() + inset },      s, 2.1f);
        drawScrew(g, { r.getX() + inset,     r.getBottom() - inset }, s, 1.3f);
        drawScrew(g, { r.getRight() - inset, r.getBottom() - inset }, s, 0.2f);
    }
}

// A recessed screen: dark well with an inner shadow and a lit lower lip.
inline void drawScreen(juce::Graphics& g, juce::Rectangle<float> r, float corner = 4.0f) {
    g.setColour(kScreen);
    g.fillRoundedRectangle(r, corner);

    // Inner shadow along the top and left edges.
    juce::ColourGradient top(juce::Colour(0x90000000), 0, r.getY(),
                             juce::Colour(0x00000000), 0, r.getY() + 7.0f, false);
    g.setGradientFill(top);
    g.fillRoundedRectangle(r.withHeight(8.0f), corner);

    g.setColour(kScreenEdge);
    g.drawRoundedRectangle(r, corner, 1.2f);
    g.setColour(kPlateLift);
    g.drawLine(r.getX() + corner, r.getBottom() + 0.8f, r.getRight() - corner, r.getBottom() + 0.8f, 1.0f);
}

// A small round indicator lamp.
// `lit` runs 0 (dark) to 1 (fully on); in between it fades, for animation.
inline void drawLed(juce::Graphics& g, juce::Point<float> c, float r, float lit) {
    lit = juce::jlimit(0.0f, 1.0f, lit);
    if (lit > 0.001f) {
        // A soft blurred halo, cached once per size.
        const auto key = GlowCache::mix(0x1ed, (std::uint64_t) std::lround(r * 64.0f));
        const auto area = juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre({ 0.0f, 0.0f });
        juce::Graphics::ScopedSaveState state(g);
        g.addTransform(juce::AffineTransform::translation(c.x, c.y));
        glowCache().draw(g, key, area, r * 1.6f, 0.9f * lit, [r](juce::Graphics& gi) {
            gi.setColour(kAccentHot);
            gi.fillEllipse(-r * 1.3f, -r * 1.3f, r * 2.6f, r * 2.6f);
        });
    }
    const auto off = juce::Colour(0xff2a2420);
    g.setGradientFill(juce::ColourGradient(off.interpolatedWith(kAccentHot, lit), c.x, c.y - r * 0.4f,
                                           off.interpolatedWith(kAccent.darker(0.4f), lit), c.x, c.y + r, true));
    g.fillEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawEllipse(c.x - r, c.y - r, r * 2.0f, r * 2.0f, 0.8f);
}

// ----------------------------------------------------------- look and feel

class RumbleLookAndFeel : public juce::LookAndFeel_V4 {
public:
    RumbleLookAndFeel() {
        // Everything not given a font explicitly (menus, dialogs) uses Inter too.
        setDefaultSansSerifTypeface(interTypeface());
        setColour(juce::Slider::textBoxTextColourId,       kText);
        setColour(juce::Slider::textBoxOutlineColourId,    juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId,  kAccent.withAlpha(0.3f));

        setColour(juce::Label::textColourId,               kLegend);

        setColour(juce::ComboBox::backgroundColourId,      kScreen);
        setColour(juce::ComboBox::textColourId,            kAccent);
        setColour(juce::ComboBox::outlineColourId,         kScreenEdge);
        setColour(juce::ComboBox::arrowColourId,           kAccent);

        setColour(juce::TextButton::buttonColourId,        kCapMid);
        setColour(juce::TextButton::buttonOnColourId,      kAccent.withAlpha(0.35f));
        setColour(juce::TextButton::textColourOffId,       kText);
        setColour(juce::TextButton::textColourOnId,        kText);

        setColour(juce::AlertWindow::backgroundColourId,   kPlateBottom);
        setColour(juce::AlertWindow::textColourId,         kText);
        setColour(juce::AlertWindow::outlineColourId,      kPlateEdge);

        setColour(juce::TextEditor::backgroundColourId,    kScreen);
        setColour(juce::TextEditor::textColourId,          kText);
        setColour(juce::TextEditor::outlineColourId,       kScreenEdge);
        setColour(juce::TextEditor::focusedOutlineColourId, kAccent);
        setColour(juce::TextEditor::highlightColourId,     kAccent.withAlpha(0.3f));

        setColour(juce::PopupMenu::backgroundColourId,     juce::Colour(0xff1b1c20));
        setColour(juce::PopupMenu::textColourId,           kText);
        setColour(juce::PopupMenu::headerTextColourId,     kLegend);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.25f));
        setColour(juce::PopupMenu::highlightedTextColourId,       kAccentHot);
    }

    // --- rotary ----------------------------------------------------------

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider& slider) override {
        const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
        const float dia = juce::jmin(area.getWidth(), area.getHeight());
        const auto  box = juce::Rectangle<float>(dia, dia).withCentre(area.getCentre());
        const auto  c   = box.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool  enabled = slider.isEnabled();

        // Printed scale: eleven ticks, longer at the ends and the centre.
        const float scaleR = dia * 0.5f - 1.0f;
        g.setColour(kScale);
        for (int i = 0; i <= 10; ++i) {
            const float a = startAngle + (float) i / 10.0f * (endAngle - startAngle);
            const bool major = (i == 0 || i == 5 || i == 10);
            const float r0 = scaleR - (major ? 4.0f : 2.5f);
            g.drawLine(c.x + r0 * std::sin(a),     c.y - r0 * std::cos(a),
                       c.x + scaleR * std::sin(a), c.y - scaleR * std::cos(a), major ? 1.4f : 1.0f);
        }

        // Value arc in a recessed track.
        const float arcR = dia * 0.5f - 7.5f;
        const float arcW = juce::jmax(2.5f, dia * 0.05f);
        juce::Path track;
        track.addCentredArc(c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(kTrack);
        g.strokePath(track, juce::PathStrokeType(arcW + 1.5f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Bipolar controls (EQ gain, Output) fill from the centre outwards.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float origin = bipolar ? startAngle + 0.5f * (endAngle - startAngle) : startAngle;
        if (std::abs(angle - origin) > 0.001f) {
            juce::Path value;
            value.addCentredArc(c.x, c.y, arcR, arcR, 0.0f,
                                juce::jmin(origin, angle), juce::jmax(origin, angle), true);
            if (enabled) {
                // Blurred glow under the arc, brighter while hovered. Keyed
                // on the arc's ends (to a quarter degree) so it is only
                // re-blurred when the knob actually moves.
                const float hover = (float) (double) slider.getProperties().getWithDefault("rumbleHover", 0.0);
                std::uint64_t key = 0xa7c;
                for (float v : { juce::jmin(origin, angle), juce::jmax(origin, angle), arcR, arcW, c.x - area.getX(), c.y - area.getY() })
                    key = GlowCache::mix(key, (std::uint64_t) std::llround(v * 230.0f) + 0x8000);
                const auto local = juce::AffineTransform::translation(-area.getX(), -area.getY());
                juce::Graphics::ScopedSaveState state(g);
                g.addTransform(juce::AffineTransform::translation(area.getX(), area.getY()));
                glowCache().draw(g, key, area.transformedBy(local), 3.5f, 0.55f + 0.45f * hover,
                                 [&value, arcW, local](juce::Graphics& gi) {
                    gi.setColour(kAccentHot);
                    auto p = value;
                    p.applyTransform(local);
                    gi.strokePath(p, juce::PathStrokeType(arcW + 1.5f, juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
                });
            }
            g.setColour(enabled ? kAccent : kScale);
            g.strokePath(value, juce::PathStrokeType(arcW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Cap: knurled outer skirt, then a domed metal top.
        const auto skirt = box.reduced(dia * 0.5f - arcR + arcW + 2.5f);
        juce::Path skirtPath;
        skirtPath.addEllipse(skirt);
        juce::DropShadow(kShadow, (int) juce::jmax(5.0f, dia * 0.14f),
                         { 0, (int) juce::jmax(2.0f, dia * 0.05f) }).drawForPath(g, skirtPath);

        g.setGradientFill(juce::ColourGradient(kCapMid, skirt.getCentreX(), skirt.getY(),
                                               kCapDark, skirt.getCentreX(), skirt.getBottom(), false));
        g.fillEllipse(skirt);

        // Knurling: fine grooves that turn with the knob.
        {
            const int grooves = 36;
            const float r0 = skirt.getWidth() * 0.5f * 0.80f;
            const float r1 = skirt.getWidth() * 0.5f - 0.8f;
            g.setColour(kKnurl.withAlpha(0.85f));
            for (int i = 0; i < grooves; ++i) {
                const float a = angle + juce::MathConstants<float>::twoPi * (float) i / (float) grooves;
                g.drawLine(c.x + r0 * std::sin(a), c.y - r0 * std::cos(a),
                           c.x + r1 * std::sin(a), c.y - r1 * std::cos(a), 1.0f);
            }
        }
        g.setColour(kPlateEdge);
        g.drawEllipse(skirt, 1.0f);

        // Top: brushed-metal dome with a soft highlight upper left.
        const auto top = skirt.reduced(skirt.getWidth() * 0.13f);
        g.setGradientFill(juce::ColourGradient(kCapLight, top.getX() + top.getWidth() * 0.3f, top.getY(),
                                               kCapDark,  top.getRight(), top.getBottom(), true));
        g.fillEllipse(top);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0x30ffffff), top.getCentreX(), top.getY(),
                                               juce::Colour(0x00ffffff), top.getCentreX(), top.getCentreY(), false));
        g.fillEllipse(top.reduced(1.0f));
        g.setColour(juce::Colour(0xff121316));
        g.drawEllipse(top, 0.8f);

        // Pointer line engraved into the cap, running onto the skirt.
        const float pr0 = top.getWidth() * 0.12f;
        const float pr1 = skirt.getWidth() * 0.5f - 2.0f;
        const auto p0 = juce::Point<float>(c.x + pr0 * std::sin(angle), c.y - pr0 * std::cos(angle));
        const auto p1 = juce::Point<float>(c.x + pr1 * std::sin(angle), c.y - pr1 * std::cos(angle));
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.drawLine({ p0.translated(0.0f, 1.0f), p1.translated(0.0f, 1.0f) }, juce::jmax(2.0f, dia * 0.04f));
        g.setColour(enabled ? kPointer : kTextDim);
        g.drawLine({ p0, p1 }, juce::jmax(1.8f, dia * 0.035f));
    }

    // --- value readouts --------------------------------------------------

    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox(slider);
        l->setFont(uiFont(12.0f, 520.0f, true));   // tabular: numbers don't jiggle
        l->setColour(juce::Label::textColourId, kText);
        l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        return l;
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override {
        // Slider readouts sit in a small recessed window.
        if (dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr) {
            const auto r = label.getLocalBounds().toFloat().reduced(6.0f, 0.5f);
            drawScreen(g, r, 3.0f);
            if (! label.isBeingEdited()) {
                g.setColour(label.findColour(juce::Label::textColourId)
                                .withMultipliedAlpha(label.isEnabled() ? 1.0f : 0.4f));
                g.setFont(getLabelFont(label));
                g.drawFittedText(label.getText(), r.toNearestInt(), juce::Justification::centred, 1, 0.8f);
            }
            return;
        }
        juce::LookAndFeel_V4::drawLabel(g, label);
    }

    // --- combo box -------------------------------------------------------

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override {
        const auto r = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(0.5f, 1.0f);
        drawScreen(g, r, 3.0f);
        if (box.hasKeyboardFocus(true)) {
            g.setColour(kAccent.withAlpha(0.6f));
            g.drawRoundedRectangle(r, 3.0f, 1.0f);
        }
        const float cx = (float) width - 12.0f;
        const float cy = (float) height * 0.5f;
        juce::Path chevron;
        chevron.startNewSubPath(cx - 3.5f, cy - 1.5f);
        chevron.lineTo(cx, cy + 2.0f);
        chevron.lineTo(cx + 3.5f, cy - 1.5f);
        g.setColour(box.isEnabled() ? kAccent : kScale);
        g.strokePath(chevron, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override {
        return uiFont(12.5f, 600.0f);
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 1, box.getWidth() - 22, box.getHeight() - 2);
        label.setFont(getComboBoxFont(box));
        label.setMinimumHorizontalScale(0.65f);  // squeeze "Low Pass 12" rather than cut it
        label.setJustificationType(juce::Justification::centredLeft);
    }

    // --- toggle: a square push button with an indicator lamp -------------

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool isHighlighted, bool isDown) override {
        const auto bounds = button.getLocalBounds().toFloat();
        const float side = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 2.0f;
        const auto cap = juce::Rectangle<float>(side, side).withCentre(bounds.getCentre());
        const bool on = button.getToggleState();

        juce::Path p;
        p.addRoundedRectangle(cap, 4.0f);
        juce::DropShadow(kShadow, isDown ? 3 : 7, { 0, isDown ? 1 : 3 }).drawForPath(g, p);

        g.setGradientFill(juce::ColourGradient(isHighlighted ? kCapLight : kCapMid, 0, cap.getY(),
                                               kCapDark, 0, cap.getBottom(), false));
        g.fillRoundedRectangle(cap, 4.0f);
        g.setColour(kPlateEdge);
        g.drawRoundedRectangle(cap, 4.0f, 1.0f);
        g.setColour(kPlateLift);
        g.drawLine(cap.getX() + 3.0f, cap.getY() + 1.2f, cap.getRight() - 3.0f, cap.getY() + 1.2f, 1.0f);

        drawLed(g, cap.getCentre(), side * 0.14f, on);
    }

    // --- push buttons ----------------------------------------------------

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                              const juce::Colour&, bool isHighlighted, bool isDown) override {
        const auto r = button.getLocalBounds().toFloat().reduced(0.5f, 1.0f);
        const float corner = 3.0f;
        const float alpha = button.isEnabled() ? 1.0f : 0.35f;

        g.setGradientFill(juce::ColourGradient(
            (isHighlighted ? kCapLight : kCapMid).withMultipliedAlpha(alpha), 0, r.getY(),
            kCapDark.withMultipliedAlpha(alpha), 0, r.getBottom(), false));
        g.fillRoundedRectangle(isDown ? r.translated(0.0f, 0.5f) : r, corner);
        g.setColour(kPlateEdge.withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(r, corner, 1.0f);
        if (! isDown) {
            g.setColour(kPlateLift.withMultipliedAlpha(alpha));
            g.drawLine(r.getX() + corner, r.getY() + 1.2f, r.getRight() - corner, r.getY() + 1.2f, 1.0f);
        }
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override {
        return legendFont(10.5f);
    }

    // --- popup menus -----------------------------------------------------

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override {
        g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
        g.setColour(kPlateEdge);
        g.drawRect(0, 0, width, height, 1);
    }

    // --- alert windows ---------------------------------------------------

    void drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                      const juce::Rectangle<int>& textArea, juce::TextLayout& layout) override {
        drawPlate(g, alert.getLocalBounds(), false);
        g.setColour(kText);
        layout.draw(g, textArea.toFloat());
    }
};

} // namespace rumble_ui
