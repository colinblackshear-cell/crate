#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The look every Crate tool shares: dark grey panels with one warm accent.
namespace crate::Theme
{
    inline const juce::Colour background { 0xff1c1d20 };
    inline const juce::Colour panel { 0xff26272b };
    inline const juce::Colour raised { 0xff33353a };
    inline const juce::Colour outline { 0xff44464d };
    inline const juce::Colour waveform { 0xff8fd3c8 };
    inline const juce::Colour accent { 0xffff8a3d };
    inline const juce::Colour record { 0xffff4d4d };
    inline const juce::Colour text { 0xffeeeeee };
    inline const juce::Colour textDim { 0xff9a9ca3 };

    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            auto scheme = getDarkColourScheme();
            scheme.setUIColour (ColourScheme::windowBackground, background);
            scheme.setUIColour (ColourScheme::widgetBackground, raised);
            scheme.setUIColour (ColourScheme::menuBackground, panel);
            scheme.setUIColour (ColourScheme::outline, outline);
            scheme.setUIColour (ColourScheme::defaultText, text);
            scheme.setUIColour (ColourScheme::defaultFill, accent);
            scheme.setUIColour (ColourScheme::highlightedText, text);
            scheme.setUIColour (ColourScheme::highlightedFill, accent.withAlpha (0.5f));
            scheme.setUIColour (ColourScheme::menuText, text);
            setColourScheme (scheme);

            setColour (juce::TextButton::buttonColourId, raised);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, background);
            setColour (juce::ComboBox::backgroundColourId, raised);
            setColour (juce::TextEditor::backgroundColourId, raised);
            setColour (juce::Slider::thumbColourId, accent);
            setColour (juce::Slider::trackColourId, accent.withAlpha (0.6f));
            setColour (juce::Slider::backgroundColourId, raised);
            setColour (juce::ListBox::backgroundColourId, panel);
        }
    };
}
