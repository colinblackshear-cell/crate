#include "StripFxPanel.h"
#include "Vintage.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

StripFxPanel::StripFxPanel (StripFx& f, const juce::String& t, juce::Colour c)
    : fx (f), title (t), colour (c)
{
    auto& state = fx.params;
    const std::pair<const char*, const char*> list[] = { { "filter", "Filter" }, { "resonance", "Resonance" }, { "drive", "Drive" },
                                                         { "crush", "Crush" }, { "delayMix", "Delay" }, { "feedback", "Feedback" },
                                                         { "reverbMix", "Reverb" }, { "reverbSize", "Room size" } };
    for (auto [id, name] : list)
    {
        auto knob = std::make_unique<crate::ParamKnob> (state, id, name, colour);
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }
    knobs[0]->slider.setTooltip ("Turn left to close a low-pass filter, right to open a high-pass. The middle is off. "
                                 "Double-click to put it back in the middle.");
    knobs[4]->slider.setTooltip ("How much goes into the delay. Pull it down and the echoes ring out.");
    delayTime = std::make_unique<crate::ParamChoice> (state, "delayTime", "Delay time");
    addAndMakeVisible (*delayTime);

    const auto& names = StripFx::repeatNames();
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = repeatButtons.add (new juce::TextButton (names[i]));
        b->setClickingTogglesState (false);
        b->setColour (juce::TextButton::buttonOnColourId, colour);
        b->setTooltip ("Beat repeat: loops the next slice of this length over and over. Click it again (or Off) to let go.");
        b->onClick = [this, i] { setRepeat (i); };
        addAndMakeVisible (b);
    }
    addAndMakeVisible (resetButton);
    addAndMakeVisible (closeButton);
    resetButton.setTooltip ("Turns every effect here off.");
    resetButton.onClick = [this] { fx.reset(); };
    tick();
}

void StripFxPanel::setRepeat (int choice)
{
    auto* p = fx.params.getParameter ("repeat");
    const int current = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
    if (choice == current)
        choice = 0;
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) choice));
    p->endChangeGesture();
}

void StripFxPanel::tick()
{
    const int current = (int) fx.params.getRawParameterValue ("repeat")->load();
    for (int i = 0; i < repeatButtons.size(); ++i)
        repeatButtons[i]->setToggleState (i == current && i > 0, juce::dontSendNotification);
}

void StripFxPanel::paint (juce::Graphics& g)
{
    // A dark well on the Rig's panel, with a stripe in the strip's (or loop's) colour.
    vintage::drawFace (g, getLocalBounds());
    const auto well = getLocalBounds().toFloat().reduced (1.0f);
    vintage::drawWell (g, well, 10.0f);
    g.setColour (colour);
    g.fillRoundedRectangle (well.withWidth (6.0f).reduced (0, 10.0f), 3.0f);
    g.setColour (vintage::pal().wellInk);
    g.setFont (vintage::cond (19.0f, vintage::Weight::extra, 0.06f));
    g.drawText (title.toUpperCase(), getLocalBounds().reduced (16, 8).removeFromTop (20), juce::Justification::centredLeft);
    auto area = getLocalBounds().reduced (16, 8);
    area.removeFromTop (24);
    vintage::engraved (g, "Repeat", area.removeFromTop (16).removeFromLeft (150), juce::Justification::centredLeft, 12.0f, true);
}

void StripFxPanel::resized()
{
    auto area = getLocalBounds().reduced (14, 8);
    auto top = area.removeFromTop (20);
    closeButton.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    resetButton.setBounds (top.removeFromRight (70));

    area.removeFromTop (4);
    auto repeatArea = area.removeFromLeft (200);
    repeatArea.removeFromTop (18);
    auto row = repeatArea.removeFromTop (30);
    const int w = row.getWidth() / juce::jmax (1, repeatButtons.size());
    for (auto* b : repeatButtons)
        b->setBounds (row.removeFromLeft (w).reduced (1, 0));

    area.removeFromLeft (16);
    const int count = (int) knobs.size() + 1;
    const int kw = juce::jmin (100, area.getWidth() / count);
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->setBounds (area.removeFromLeft (kw));
        if (i == 5)
        {
            delayTime->setBounds (area.removeFromLeft (kw + 10).reduced (4, 10));
        }
    }
}
