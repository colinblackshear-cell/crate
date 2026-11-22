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
    addAndMakeVisible (primary);
    addAndMakeVisible (secondary);
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
    auto r = getLocalBounds().toFloat().reduced (6.0f);
    vintage::glow (g, r, 14.0f, vintage::colours::lcdAmber, 12.0f, 0.35f);
    g.setColour (juce::Colour (0xf2191410));
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (vintage::colours::lcdAmber.withAlpha (0.8f));
    g.drawRoundedRectangle (r.reduced (0.75f), 14.0f, 1.5f);

    auto text = r.reduced (18.0f, 14.0f);
    auto top = text.removeFromTop (20.0f);
    g.setFont (vintage::cond (13.0f, vintage::Weight::extra, 0.22f));
    g.setColour (vintage::colours::lcdAmber);
    const auto step = current == landed ? juce::String ("DONE") : "STEP " + juce::String (current + 1) + " OF 4";
    g.drawText (u8 ("MAKE A BEAT  \xc2\xb7  ") + step, top, juce::Justification::centredLeft, false);
    // Four little dots: where you are.
    for (int i = 0; i < 4; ++i)
    {
        const auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ top.getRight() - 8.0f - (float) (3 - i) * 16.0f, top.getCentreY() });
        g.setColour (i <= current ? vintage::colours::lcdAmber : juce::Colours::white.withAlpha (0.18f));
        g.fillEllipse (dot);
    }
    const auto w = words();
    g.setFont (vintage::cond (26.0f, vintage::Weight::extra, 0.02f));
    g.setColour (juce::Colour (0xfff6efe2));
    g.drawText (w.title, text.removeFromTop (34.0f), juce::Justification::centredLeft, false);
    g.setFont (vintage::barlow (15.0f));
    g.setColour (juce::Colour (0xffd9cfbf));
    g.drawFittedText (w.body, text.removeFromTop (text.getHeight() - 50.0f).toNearestInt(), juce::Justification::topLeft, 4, 0.9f);
}

void RigLesson::resized()
{
    auto footer = getLocalBounds().reduced (24, 0).withTop (getHeight() - 20 - 44).withHeight (44);
    const bool last = current == landed;
    const auto w = words();
    primary.text = w.next + (last || current == rec ? juce::String() : u8 (" \xe2\x80\xba"));
    primary.lit = current == rec ? vintage::Lit::arm : vintage::Lit::next;
    primary.icon = current == rec ? rigpanels::PanelButton::Icon::rec : rigpanels::PanelButton::Icon::none;
    primary.setEnabled (! (current == rec && busy));
    secondary.text = last ? "Done" : "Skip the lesson";
    primary.setBounds (footer.removeFromRight (last ? 200 : 170));
    secondary.setBounds (footer.removeFromLeft (last ? 110 : 150));
    primary.repaint();
    secondary.repaint();
}
