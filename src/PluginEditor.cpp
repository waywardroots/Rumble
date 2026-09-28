#include "PluginEditor.h"

RumbleAudioProcessorEditor::RumbleAudioProcessorEditor(RumbleAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p) {
    addKnob("mix", "Mix");
    addKnob("drive", "Drive");
    addKnob("taildrive", "Tail Drive");
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

RumbleAudioProcessorEditor::Knob& RumbleAudioProcessorEditor::addKnob(const char* paramID, const juce::String& name) {
    auto* k = knobs.add(new Knob());

    k->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 78, 16);
    addAndMakeVisible(k->slider);

    k->label.setText(name, juce::dontSendNotification);
    k->label.setJustificationType(juce::Justification::centred);
    k->label.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(k->label);

    k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramID, k->slider);
    return *k;
}

void RumbleAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff121215));
    g.setColour(juce::Colour(0xffe8e0d0));
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("RUMBLE", getLocalBounds().removeFromTop(38), juce::Justification::centred);
}

void RumbleAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(10);
    area.removeFromTop(32); // title

    const int cols = 5;
    const int cellW = area.getWidth() / cols;
    const int rows = (knobs.size() + cols - 1) / cols;
    const int cellH = area.getHeight() / juce::jmax(1, rows);

    for (int i = 0; i < knobs.size(); ++i) {
        juce::Rectangle<int> cell(area.getX() + (i % cols) * cellW,
                                  area.getY() + (i / cols) * cellH,
                                  cellW, cellH);
        cell.reduce(4, 4);
        knobs[i]->label.setBounds(cell.removeFromTop(14));
        knobs[i]->slider.setBounds(cell);
    }
}
