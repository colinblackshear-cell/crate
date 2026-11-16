#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The look every Crate tool shares: dark grey panels with one warm accent.
//
// Kaleidobox shows these tools inside its vintage panel, so it switches them to warm dark wells
// (useWarmWells) before it makes any of them: the same layout, in the panel's colours.
namespace crate::Theme
{
    inline juce::Colour background { 0xff1c1d20 };
    inline juce::Colour panel { 0xff26272b };
    inline juce::Colour raised { 0xff33353a };
    inline juce::Colour outline { 0xff44464d };
    inline juce::Colour waveform { 0xff8fd3c8 };
    inline juce::Colour accent { 0xffff8a3d };
    inline juce::Colour record { 0xffff4d4d };
    inline juce::Colour text { 0xffeeeeee };
    inline juce::Colour textDim { 0xff9a9ca3 };

    // The vintage panel's wells: warm near-black, cream ink, amber for what's picked.
    inline void useWarmWells()
    {
        background = juce::Colour (0xff15110e);
        panel = juce::Colour (0xff1e1a16);
        raised = juce::Colour (0xff2c2620);
        outline = juce::Colour (0xff4a4036);
        waveform = juce::Colour (0xffffc35a);
        accent = juce::Colour (0xffffb23e);
        record = juce::Colour (0xffe8432f);
        text = juce::Colour (0xfff1e7d1);
        textDim = juce::Colour (0xffbdaf97);
    }

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
