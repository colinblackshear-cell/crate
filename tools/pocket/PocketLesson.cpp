#include "PocketLesson.h"

using namespace pocket;

BeatLesson::BeatLesson()
{
    addAndMakeVisible (primary);
    addAndMakeVisible (secondary);
    primary.setFontHeight (17.0f);
    secondary.setFontHeight (15.0f);
    primary.onClick = [this]
    {
        if (current == landed)
        {
            if (onAddTool)
                onAddTool();
        }
        else if (current == rec)
        {
            if (onPressRec)
                onPressRec();
        }
        else
            goTo (current + 1);
    };
    secondary.onClick = [this] { if (onEnd) onEnd(); };
}

void BeatLesson::goTo (int phase)
{
    current = juce::jlimit ((int) kick, (int) landed, phase);
    if (current != rec)
        busy = false;
    if (onPhase)
        onPhase (current);
    visibilityChanged();
    resized();
    repaint();
}

void BeatLesson::showLanded (int layerNumber)
{
    landedOn = layerNumber;
    goTo (landed);
}

void BeatLesson::setRecBusy (bool b)
{
    if (current != rec || b == busy)
        return;
    busy = b;
    resized();
    repaint();
}

void BeatLesson::visibilityChanged()
{
    if (isVisible() && current == rec && animated)
        startTimerHz (20);
    else
        stopTimer();
}

void BeatLesson::timerCallback()
{
    if (current != rec)
        return stopTimer();
    if (recSpot)
        repaint (recSpot().expanded (14));
}

BeatLesson::Words BeatLesson::words() const
{
    switch (current)
    {
        case kick:
            return { "Start with the kick",
                     U8("The kick is the low thump. Tap the glowing boxes to put it on 1 and 11, or anywhere you like. It loops as you tap, with the click."),
                     "NEXT: SNARE" };
        case snare:
            return { "Now the snare",
                     U8("Your kick is looping. The snare is the crack: tap the two glowing boxes, or put it where you like. It plays as you go."),
                     "NEXT: HATS" };
        case hats:
            return { "Add hats",
                     U8("Hats keep time. Tap every other glowing box for steady ticks, or go busier. Hold a box to accent it."),
                     "NEXT: REC" };
        case rec:
            if (busy)
                return { "Recording your beat",
                         U8("It counts you in, records a few bars of what you made, then it keeps looping. Press STOP to call it off."),
                         "RECORDING" };
            return { "Press REC",
                     U8("Press the red REC button. Your beat goes on layer ") + juce::String (nextLayerNumber ? juce::jmax (1, nextLayerNumber()) : 1)
                         + U8(" and keeps looping while you add the next tool."),
                     "PRESS REC" };
        default:
            return { "It's looping",
                     U8("Your beat is looping on layer ") + juce::String (landedOn) + U8(". Add another tool on top of it, or you're done."),
                     "ADD ANOTHER TOOL" };
    }
}

void BeatLesson::resized()
{
    auto footer = card.reduced (16, 0).withTop (card.getBottom() - 16 - 46).withHeight (46);
    const bool last = current == landed;
    const int pw = last ? 196 : current == rec ? 150 : 158;
    primary.setBounds (footer.removeFromRight (pw));
    secondary.setBounds (footer.removeFromLeft (last ? 100 : 140));
    primary.setText (words().next + (last || current == rec ? juce::String() : U8(" \xe2\x80\xba")));
    primary.setLook (current == rec ? Look::red : Look::amber);
    primary.setVisible (! (current == rec && busy));
    secondary.setText (last ? "DONE" : "Skip the lesson");
    secondary.setLook (last ? Look::cream : Look::ghost);
    secondary.setFontHeight (last ? 16.0f : 15.0f);
}

void BeatLesson::paint (juce::Graphics& g)
{
    // the spotlight on REC (step 4)
    if (current == rec && recSpot)
    {
        const auto spot = recSpot().toFloat();
        const float pulse = animated ? 0.5f + 0.5f * (float) std::sin ((double) juce::Time::getMillisecondCounter() * 0.008) : 1.0f;
        g.setColour (col::amber.withAlpha (0.25f + 0.2f * pulse));
        g.drawRoundedRectangle (spot.expanded (9), spot.getHeight() * 0.5f + 9.0f, 5.0f);
        g.setColour (col::amber);
        g.drawRoundedRectangle (spot.expanded (4), spot.getHeight() * 0.5f + 4.0f, 3.2f);
    }

    auto r = card.toFloat();
    g.setColour (juce::Colour (0x33000000));
    g.fillRoundedRectangle (r.translated (0, 4), 22.0f);
    g.setColour (col::creamLight);
    g.fillRoundedRectangle (r, 22.0f);
    g.setColour (col::amber);
    g.drawRoundedRectangle (r.reduced (1.0f), 22.0f, 2.4f);

    const auto w = words();
    auto in = r.reduced (18, 0).withTrimmedTop (14);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (12.5f), 0.12f));
    g.drawText (current == landed ? juce::String (U8("MAKE A BEAT \xc2\xb7 DONE")) : "MAKE A BEAT" + juce::String (U8(" \xc2\xb7 ")) + juce::String (current + 1) + " OF 4",
                in.removeFromTop (18), juce::Justification::centredLeft);
    in.removeFromTop (3);
    g.setColour (col::ink);
    g.setFont (condensedBold (27.0f));
    g.drawText (w.title, in.removeFromTop (32), juce::Justification::centredLeft);
    g.setFont (small (15.0f));
    g.drawFittedText (w.body, in.removeFromTop (64).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);

    // progress: four segments (the finished ones salmon, this one amber)
    auto dots = juce::Rectangle<float> (r.getX() + 18, (float) card.getBottom() - 16 - 46 - 14, r.getWidth() - 36, 6.0f);
    const float sw = (dots.getWidth() - 3.0f * 6.0f) / 4.0f;
    for (int s = 0; s < 4; ++s)
    {
        g.setColour (current == landed || s < current ? juce::Colour (0xfff59a2a) : s == current ? col::amber : col::creamDeep);
        g.fillRoundedRectangle (dots.getX() + (float) s * (sw + 6.0f), dots.getY(), sw, 6.0f, 3.0f);
    }
}
