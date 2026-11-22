#include "RigLesson.h"

#include "Vintage.h"

namespace
{
juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }
}

RigLesson::RigLesson()
{
    primary.fontSize = 17.0f;
    secondary.fontSize = 15.0f;
    back.fontSize = 15.0f;
    addAndMakeVisible (primary);
    addAndMakeVisible (secondary);
    addAndMakeVisible (back);
    back.onClick = [this] { if (current > kick && current <= rec) goTo (current - 1); };
    primary.onClick = [this]
    {
        switch (current)
        {
            case kick:  goTo (snare); break;
            case snare: goTo (hats); break;
            case hats:  goTo (rec); break;
            case rec:   if (! busy && onPressRec) onPressRec(); break;
            default:    if (onAddTool) onAddTool(); break;
        }
    };
    secondary.onClick = [this] { if (onEnd) onEnd(); };
}

void RigLesson::goTo (int phase)
{
    current = juce::jlimit ((int) kick, (int) landed, phase);
    if (current != rec)
        busy = false;
    resized();
    repaint();
    if (onPhase)
        onPhase (current);
}

void RigLesson::showLanded (int layerNumber)
{
    landedOn = layerNumber;
    busy = false;
    current = landed;
    resized();
    repaint();
    if (onPhase)
        onPhase (current);
}

void RigLesson::setRecBusy (bool b)
{
    if (busy == b)
        return;
    busy = b;
    resized();
    repaint();
}

RigLesson::Words RigLesson::words() const
{
    switch (current)
    {
        case kick:
            return { "Start with the kick",
                     u8 ("The kick is the low thump. Click the glowing steps to put it on 1 and 11, or anywhere you like. It loops as you click, with the click."),
                     "Next: snare" };
        case snare:
            return { "Now the snare",
                     u8 ("Your kick is looping. The snare is the crack: click the two glowing steps, or put it where you like. It plays as you go."),
                     "Next: hats" };
        case hats:
            return { "Add hats",
                     u8 ("Hats keep time. Click every other glowing step for steady ticks, or go busier."),
                     "Next: Rec" };
        case rec:
            if (busy)
                return { "Recording your beat",
                         u8 ("It counts you in, records a few bars of what you made, then it keeps looping. Press Rec again to call it off."),
                         "Recording" };
            return { "Press Rec",
                     u8 ("Press the red Rec at the bottom (or here). Your beat goes on layer ")
                         + juce::String (nextLayerNumber ? juce::jmax (1, nextLayerNumber()) : 1)
                         + u8 (" and keeps looping while you add the next tool."),
                     "Press Rec" };
        default:
            return { u8 ("It\xe2\x80\x99s looping"),
                     u8 ("Your beat is looping on layer ") + juce::String (landedOn)
                         + u8 (". Pick another tool on the left and play on top of it, or you\xe2\x80\x99re done."),
                     "Add another tool" };
    }
}

void RigLesson::paint (juce::Graphics& g)
{
    // A cream card with an amber edge, as in the design.
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    vintage::glow (g, r, 16.0f, vintage::colours::lcdAmber, 12.0f, 0.4f);
    g.setColour (juce::Colour (0xfff6eedd));
    g.fillRoundedRectangle (r, 16.0f);
    g.setColour (vintage::colours::lcdAmber);
    g.drawRoundedRectangle (r.reduced (1.5f), 16.0f, 3.0f);

    const auto ink = juce::Colour (0xff271f18);
    auto text = r.reduced (24.0f, 18.0f);
    const auto step = current == landed ? juce::String ("done") : juce::String (current + 1) + " of 4";
    vintage::engraved (g, u8 ("Make a beat \xc2\xb7 ") + step, text.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft, 12.0f,
                       false, ink.withAlpha (0.7f));
    const auto w = words();
    g.setFont (vintage::wordmark (30.0f));
    g.setColour (ink);
    g.drawFittedText (w.title, text.removeFromTop (42.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    g.setFont (vintage::barlow (16.0f));
    g.setColour (ink.withAlpha (0.85f));
    auto body = text.removeFromTop (text.getHeight() - 74.0f);
    g.drawFittedText (w.body, body.toNearestInt(), juce::Justification::topLeft, 4, 0.9f);
    // Where you are: four bars.
    auto bars = text.removeFromTop (14.0f).withSizeKeepingCentre (text.getWidth(), 5.0f);
    const float each = (bars.getWidth() - 3 * 8.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (i <= current ? vintage::colours::lcdAmber : ink.withAlpha (0.15f));
        g.fillRoundedRectangle (bars.removeFromLeft (each), 2.5f);
        bars.removeFromLeft (8.0f);
    }
}

void RigLesson::resized()
{
    auto footer = getLocalBounds().reduced (30, 0).withTop (getHeight() - 24 - 42).withHeight (42);
    const bool last = current == landed;
    const auto w = words();
    primary.text = w.next + (last || current == rec ? juce::String() : u8 (" \xe2\x80\xba"));
    primary.lit = current == rec ? vintage::Lit::arm : vintage::Lit::next;
    primary.icon = rigpanels::PanelButton::Icon::none;
    primary.setEnabled (! (current == rec && busy));
    secondary.text = last ? "Done" : "Skip lesson";
    primary.setBounds (footer.removeFromRight (last ? 190 : 150));
    back.setVisible (current > kick && current <= rec);
    if (back.isVisible())
    {
        back.setBounds (footer.removeFromLeft (80));
        footer.removeFromLeft (8);
    }
    secondary.setBounds (footer.removeFromLeft (last ? 100 : 140));
    primary.repaint();
    secondary.repaint();
}
