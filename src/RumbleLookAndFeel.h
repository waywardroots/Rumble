#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace rumble_ui {

// ---------------------------------------------------------------- palette

const juce::Colour kBgTop       { 0xff23242a };
const juce::Colour kBgBottom    { 0xff121318 };
const juce::Colour kPanelTop    { 0xff2a2c34 };
const juce::Colour kPanelBottom { 0xff1e2026 };
const juce::Colour kPanelEdge   { 0xff3a3d47 };
const juce::Colour kPanelLift   { 0x18ffffff };  // 1px highlight along the top
const juce::Colour kShadow      { 0x90000000 };

const juce::Colour kKnobTop     { 0xff44474f };
const juce::Colour kKnobBottom  { 0xff23252b };
const juce::Colour kKnobEdge    { 0xff15161a };
const juce::Colour kTrack       { 0xff15161a };

const juce::Colour kAccent      { 0xffe0a44a };  // warm amber
const juce::Colour kAccentGlow  { 0x60e0a44a };
const juce::Colour kPointer     { 0xfff2ece0 };

const juce::Colour kText        { 0xffe8e2d6 };
const juce::Colour kTextDim     { 0xff9a9aa8 };
const juce::Colour kHeaderText  { 0xffb9a184 };

// ----------------------------------------------------------- look and feel

class RumbleLookAndFeel : public juce::LookAndFeel_V4 {
public:
    RumbleLookAndFeel() {
        setColour(juce::Slider::textBoxTextColourId,      kText);
        setColour(juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, kAccent.withAlpha(0.3f));

        setColour(juce::Label::textColourId,              kText);

        setColour(juce::ComboBox::backgroundColourId,     kKnobBottom);
        setColour(juce::ComboBox::textColourId,           kText);
        setColour(juce::ComboBox::outlineColourId,        kPanelEdge);
        setColour(juce::ComboBox::arrowColourId,          kAccent);

        setColour(juce::PopupMenu::backgroundColourId,    kPanelBottom);
        setColour(juce::PopupMenu::textColourId,          kText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha(0.28f));
        setColour(juce::PopupMenu::highlightedTextColourId,       kText);
    }

    // --- rotary ----------------------------------------------------------

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle,
                          juce::Slider& slider) override {
        const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
        const float dia = juce::jmin(area.getWidth(), area.getHeight());
        const auto box = juce::Rectangle<float>(dia, dia).withCentre(area.getCentre());

        const float ringThickness = juce::jmax(3.0f, dia * 0.075f);
        // The glow pass is 3px wider than the track, so the radius has to
        // leave room for half of that or the arc clips the component edge.
        const float ringRadius    = dia * 0.5f - ringThickness * 0.5f - 2.5f;
        const auto  centre        = box.getCentre();
        const float angle         = startAngle + pos * (endAngle - startAngle);

        // Track behind the value arc.
        juce::Path track;
        track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                            startAngle, endAngle, true);
        g.setColour(kTrack);
        g.strokePath(track, juce::PathStrokeType(ringThickness, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // Value arc. Bipolar controls (EQ gain, output trim) fill from the
        // centre outwards, which reads far better than always filling from
        // the left.
        const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const float originAngle = bipolar ? startAngle + 0.5f * (endAngle - startAngle)
                                          : startAngle;
        if (std::abs(angle - originAngle) > 0.001f) {
            juce::Path value;
            value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.0f,
                                juce::jmin(originAngle, angle),
                                juce::jmax(originAngle, angle), true);
            g.setColour(kAccentGlow);
            g.strokePath(value, juce::PathStrokeType(ringThickness + 3.0f,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
            g.setColour(kAccent);
            g.strokePath(value, juce::PathStrokeType(ringThickness,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        }

        // Knob body: drop shadow, gradient fill, dark rim, top bevel.
        const auto body = box.reduced(ringThickness + 3.0f);

        juce::Path bodyPath;
        bodyPath.addEllipse(body);
        juce::DropShadow(kShadow, (int) juce::jmax(4.0f, dia * 0.12f),
                         { 0, (int) juce::jmax(1.0f, dia * 0.03f) }).drawForPath(g, bodyPath);

        g.setGradientFill(juce::ColourGradient(kKnobTop, body.getCentreX(), body.getY(),
                                               kKnobBottom, body.getCentreX(), body.getBottom(),
                                               false));
        g.fillEllipse(body);

        g.setColour(kKnobEdge);
        g.drawEllipse(body, 1.0f);

        g.setColour(juce::Colour(0x22ffffff));
        g.drawEllipse(body.reduced(1.2f).translated(0.0f, 0.6f), 1.0f);

        // Pointer.
        const float pointerLen = body.getWidth() * 0.40f;
        const float pointerW   = juce::jmax(2.0f, dia * 0.045f);
        juce::Path pointer;
        pointer.addRoundedRectangle(-pointerW * 0.5f, -body.getHeight() * 0.5f + 3.0f,
                                    pointerW, pointerLen, pointerW * 0.5f);
        pointer.applyTransform(juce::AffineTransform::rotation(angle)
                                   .translated(body.getCentreX(), body.getCentreY()));
        g.setColour(kPointer);
        g.fillPath(pointer);
    }

    // --- combo box -------------------------------------------------------

    void drawComboBox(juce::Graphics& g, int width, int height, bool,
                      int, int, int, int, juce::ComboBox& box) override {
        const auto r = juce::Rectangle<float>(0, 0, (float) width, (float) height).reduced(0.5f);
        const float corner = 3.0f;

        g.setGradientFill(juce::ColourGradient(kKnobTop.withMultipliedBrightness(0.85f), 0, 0,
                                               kKnobBottom, 0, (float) height, false));
        g.fillRoundedRectangle(r, corner);

        g.setColour(box.hasKeyboardFocus(true) ? kAccent : kPanelEdge);
        g.drawRoundedRectangle(r, corner, 1.0f);

        // Arrow.
        const float cx = (float) width - 14.0f;
        const float cy = (float) height * 0.5f;
        juce::Path arrow;
        arrow.addTriangle(cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour(kAccent);
        g.fillPath(arrow);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override {
        return juce::Font(juce::FontOptions(12.5f));
    }

    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override {
        label.setBounds(8, 0, box.getWidth() - 26, box.getHeight());
        label.setFont(getComboBoxFont(box));
        label.setJustificationType(juce::Justification::centredLeft);
    }

    // --- toggle ----------------------------------------------------------

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool isHighlighted, bool) override {
        auto r = button.getLocalBounds().toFloat().reduced(1.0f);
        const float dia = juce::jmin(r.getWidth(), r.getHeight());
        const auto box = juce::Rectangle<float>(dia, dia).withCentre(r.getCentre());
        const bool on = button.getToggleState();

        juce::Path p;
        p.addEllipse(box);
        juce::DropShadow(kShadow, 6, { 0, 1 }).drawForPath(g, p);

        g.setGradientFill(juce::ColourGradient(kKnobTop, box.getCentreX(), box.getY(),
                                               kKnobBottom, box.getCentreX(), box.getBottom(),
                                               false));
        g.fillEllipse(box);
        g.setColour(kKnobEdge);
        g.drawEllipse(box, 1.0f);

        // Lit core when engaged.
        const auto lamp = box.reduced(dia * 0.28f);
        if (on) {
            g.setColour(kAccentGlow);
            g.fillEllipse(lamp.expanded(dia * 0.14f));
            g.setColour(kAccent);
        } else {
            g.setColour(kTrack);
        }
        g.fillEllipse(lamp);

        if (isHighlighted) {
            g.setColour(juce::Colour(0x18ffffff));
            g.fillEllipse(box);
        }
    }

    // --- labels ----------------------------------------------------------

    juce::Label* createSliderTextBox(juce::Slider& slider) override {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox(slider);
        l->setFont(juce::Font(juce::FontOptions(11.0f)));
        l->setColour(juce::Label::textColourId, kTextDim);
        return l;
    }
};

// ------------------------------------------------------------- shared bits

// Panel with a gradient face, a lifted top edge and a cast shadow.
inline void drawPanel(juce::Graphics& g, juce::Rectangle<int> bounds) {
    const auto r = bounds.toFloat();
    const float corner = 5.0f;

    juce::Path p;
    p.addRoundedRectangle(r, corner);
    juce::DropShadow(kShadow, 10, { 0, 3 }).drawForPath(g, p);

    g.setGradientFill(juce::ColourGradient(kPanelTop, r.getCentreX(), r.getY(),
                                           kPanelBottom, r.getCentreX(), r.getBottom(), false));
    g.fillRoundedRectangle(r, corner);

    g.setColour(kPanelEdge);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);

    g.setColour(kPanelLift);
    g.drawLine(r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
}

} // namespace rumble_ui
