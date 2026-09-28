#include "PluginEditor.h"

RumbleAudioProcessorEditor::RumbleAudioProcessorEditor(RumbleAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    addKnob("mix", "Mix");
    addKnob("drive", "Drive");
    addKnob("taildrive", "Tail Drive");
    addToggle("sync", "Sync");
    addCombo("div", "Division");
    addKnob("predelay", "Pre-Delay");
    addKnob("size", "Size");
    addKnob("decay", "Decay");
    addKnob("damp", "Damping");
    addKnob("lowcut", "Low Cut");
    addKnob("tone", "Tone");
    addKnob("mod", "Mod");
    addKnob("duck", "Duck");
    addKnob("duckatk", "Duck Atk");
    addKnob("duckrel", "Duck Rel");
    addKnob("width", "Width");
    addKnob("monobelow", "Mono Below");
    addKnob("output", "Output");

    setSize(640, 470);
}

RumbleAudioProcessorEditor::Cell& RumbleAudioProcessorEditor::addCell(const juce::String& name) {
    auto* c = cells.add(new Cell());
    c->label.setText(name, juce::dontSendNotification);
    c->label.setJustificationType(juce::Justification::centred);
    c->label.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(c->label);
    return *c;
}

void RumbleAudioProcessorEditor::addKnob(const char* paramID, const juce::String& name) {
    auto& c = addCell(name);
    c.slider = std::make_unique<juce::Slider>();
    c.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 16);
    addAndMakeVisible(*c.slider);
    c.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, *c.slider);
}

void RumbleAudioProcessorEditor::addCombo(const char* paramID, const juce::String& name) {
    auto& c = addCell(name);
    c.combo = std::make_unique<juce::ComboBox>();

    // ComboBoxAttachment does not populate the box; it only maps the selected
    // index to the parameter. The items must exist before it is attached.
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(processor.apvts.getParameter(paramID)))
        c.combo->addItemList(choice->choices, 1);

    addAndMakeVisible(*c.combo);
    c.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        processor.apvts, paramID, *c.combo);
}

void RumbleAudioProcessorEditor::addToggle(const char* paramID, const juce::String& name) {
    auto& c = addCell(name);
    c.button = std::make_unique<juce::ToggleButton>(name);
    addAndMakeVisible(*c.button);
    c.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.apvts, paramID, *c.button);
}

void RumbleAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121215));
    g.setColour(juce::Colour(0xffe8e0d0));
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("RUMBLE", getLocalBounds().removeFromTop(38), juce::Justification::centred);

    // Version in the corner, so a downloaded build can be identified on sight.
    g.setColour(juce::Colour(0xff6a6a72));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("v" JucePlugin_VersionString,
               getLocalBounds().removeFromTop(38).reduced(8, 0),
               juce::Justification::centredRight);
}

void RumbleAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);
    area.removeFromTop(32); // title

    const int cols = 5;
    const int cellW = area.getWidth() / cols;
    const int rows = (cells.size() + cols - 1) / cols;
    const int cellH = area.getHeight() / juce::jmax(1, rows);

    for (int i = 0; i < cells.size(); ++i) {
        juce::Rectangle<int> cell(area.getX() + (i % cols) * cellW,
                                  area.getY() + (i / cols) * cellH,
                                  cellW, cellH);
        cell.reduce(4, 4);
        cells[i]->label.setBounds(cell.removeFromTop(14));

        auto* c = cells[i];
        if (c->slider != nullptr) {
            c->slider->setBounds(cell);
        } else if (c->combo != nullptr) {
            // Combo boxes and buttons look wrong stretched to a knob's height.
            c->combo->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
        } else if (c->button != nullptr) {
            c->button->setBounds(cell.withSizeKeepingCentre(cell.getWidth(), 24));
        }
    }
}
