#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

// Knobs, switches and menus tied to a plugin parameter, with a name under them. Shared by the
// Crate synths. Turning one on screen is also what the Rig's knob Learn listens for.
namespace crate
{
class ParamKnob : public juce::Component
{
public:
    ParamKnob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name,
               juce::Colour colour = Theme::accent)
        : attachment (state, id, slider)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
        slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
        slider.setColour (juce::Slider::thumbColourId, colour);
        slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxTextColourId, Theme::textDim);
        if (auto* p = state.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        slider.setTooltip (name + ". Drag up or down; double-click for the default. In the Rig, Learn knob puts it on a knob of your keyboard.");
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (16));
        slider.setBounds (area);
    }

    juce::Slider slider;
    juce::Label label;

private:
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class ParamChoice : public juce::Component
{
public:
    ParamChoice (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name)
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (id)))
            box.addItemList (p->choices, 1);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, id, box);
        label.setText (name, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (box);
        addAndMakeVisible (label);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (16));
        box.setBounds (area.withSizeKeepingCentre (area.getWidth(), juce::jmin (28, area.getHeight())));
    }

    juce::ComboBox box;
    juce::Label label;

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

class ParamSwitch : public juce::TextButton
{
public:
    ParamSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& name,
                 juce::Colour colour = Theme::accent)
        : juce::TextButton (name)
    {
        setClickingTogglesState (true);
        setColour (juce::TextButton::buttonOnColourId, colour);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, id, *this);
    }

private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};
}
