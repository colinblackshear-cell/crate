#include "PocketIntro.h"

#include "PocketIdeas.h"

using namespace pocket;

//==============================================================================
TitleScreen::TitleScreen()
{
    for (auto* b : { &tour, &start })
    {
        addAndMakeVisible (*b);
        b->setFontHeight (18.0f);
    }
    tour.onClick = [this] { if (onTour) onTour(); };
    start.onClick = [this] { if (onStart) onStart(); };
}

void TitleScreen::visibilityChanged()
{
    if (isVisible())
        startTimerHz (30);
    else
        stopTimer();
}

void TitleScreen::timerCallback()
{
    const float level = micLevel ? micLevel() : 0.0f;
    bloom = juce::jmax (bloom * 0.9f, juce::jlimit (0.0f, 1.0f, std::sqrt (level) * 1.4f));
    angle += 0.006f + bloom * 0.05f;   // turns slowly, faster when you make a noise
    repaint (mandalaRect().expanded (30).toNearestInt());
}

juce::Rectangle<float> TitleScreen::mandalaRect() const
{
    const float d = juce::jmin ((float) getWidth() - 140.0f, 250.0f);
    return { (float) getWidth() * 0.5f - d * 0.5f, (float) getHeight() * 0.31f - d * 0.5f, d, d };
}

void TitleScreen::resized()
{
    const int w = getWidth(), h = getHeight();
    tour.setBounds (22, h - 210, w - 44, 58);
    start.setBounds (22, h - 142, w - 44, 50);
}

void TitleScreen::mouseUp (const juce::MouseEvent&)
{
    if (onStart)
        onStart();   // a tap anywhere else: straight in
}

void TitleScreen::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);

    // the kaleidoscope: twelve petals round a cream centre, on a dark disc
    auto m = mandalaRect();
    const auto c = m.getCentre();
    const float R = m.getWidth() * 0.5f;
    g.setColour (juce::Colour (0x26000000));
    g.fillEllipse (m.expanded (6).translated (0, 8));
    g.setColour (juce::Colour (0xff2a241f));
    g.fillEllipse (m);
    g.setColour (col::well);
    g.fillEllipse (m.reduced (9));

    static const juce::uint32 colours[6] = { 0xffd9a6a0, 0xffe8c27a, 0xffb7c98a, 0xff9fc0c0, 0xffd7a3c0, 0xffc9b3e0 };
    for (int layer = 0; layer < 2; ++layer)
    {
        const float len = R * (layer == 0 ? 0.78f : 0.6f) * (1.0f + bloom * 0.12f);
        const float wid = R * (layer == 0 ? 0.22f : 0.17f);
        const float turn = layer == 0 ? angle : -angle * 1.6f + juce::MathConstants<float>::pi / 12.0f;
        for (int k = 0; k < 12; ++k)
        {
            juce::Path petal;
            petal.addRoundedRectangle (-wid * 0.5f, -len, wid, len, wid * 0.5f);
            petal.applyTransform (juce::AffineTransform::rotation (turn + juce::MathConstants<float>::twoPi * (float) k / 12.0f).translated (c));
            g.setColour (juce::Colour (colours[(k + layer * 3) % 6]).withAlpha (layer == 0 ? 0.72f : 0.6f + bloom * 0.3f));
            g.fillPath (petal);
        }
    }
    const float core = R * (0.2f + bloom * 0.05f);
    g.setColour (col::cream);
    g.fillEllipse (c.x - core, c.y - core, core * 2, core * 2);

    // the name and what it does
    g.setColour (col::ink);
    g.setFont (fraunces (52.0f).italicised());
    g.drawText ("Kaleidobox", juce::Rectangle<float> (0, m.getBottom() + 18, (float) getWidth(), 64), juce::Justification::centred);
    g.setColour (col::inkDim);
    g.setFont (small (17.0f));
    g.drawFittedText ("Turn any sound into a little song.\nRecord it, chop it, beat it, sing over it.",
                      juce::Rectangle<int> (20, (int) m.getBottom() + 84, getWidth() - 40, 50), juce::Justification::centred, 2);
    g.setColour (col::inkFaint);
    g.setFont (lcdFont (17.0f));
    g.drawText ("The mandala turns with the mic: make a noise.", juce::Rectangle<float> (0, m.getBottom() + 140, (float) getWidth(), 22), juce::Justification::centred);
    g.setFont (small (13.0f));
    g.drawText ("Shown when the app opens. Take the tour again any time from More.", juce::Rectangle<int> (0, getHeight() - 84, getWidth(), 20), juce::Justification::centred);
}

//==============================================================================
TourOverlay::TourOverlay()
{
    addAndMakeVisible (nextButton);
    addAndMakeVisible (skip);
    nextButton.setFontHeight (16.0f);
    skip.setFontHeight (15.0f);
    nextButton.onClick = [this]
    {
        if (index + 1 < (int) stops().size())
            goTo (index + 1);
        else if (onFinish)
            onFinish();
    };
    skip.onClick = [this] { if (onFinish) onFinish(); };
}

const std::vector<TourOverlay::Stop>& TourOverlay::stops()
{
    static const std::vector<Stop> all {
        { "TOOLS", col::ink, "Pick a tool", "LOOPS, DRUMS, PADS, SYNTH, CHORDS and VOCODER live down here. Switch any time: the song keeps playing.", "Next" },
        { "REC", juce::Colour (0xffd8452f), "One button records", U8("REC records the tool on screen onto the next empty loop layer, in time. Tap it again to stop. Tap the display up top for count-in and length; BPM sets the tempo, CLICK the metronome."), "Got it" },
        { "CHORDS", juce::Colour (0xffb9a3dc), "Chords in the song's key", "Eight chords of the song's key: tap one, or fill the PROGRESSION for one chord a bar. They set the vocoder's notes, and SYNTH plays in the same key.", "Next" },
        { "PADS", col::amber, "Hit it!", "Every pad holds a sound, and a factory kit is already loaded. Tap one. Press REC to play a pattern in, then it locks onto a layer.", "I hit it" },
        { "LOOPS", col::ink, "Build it up", "Every take lands on its own layer, marked with the tool that made it. Tap a layer: IN / OUT trims it, FIT or QUANTIZE puts it in time, EFFECTS adds knob moves.", "Next" },
        { "VOCODER", juce::Colour (0xff82cf6a), "Headphones first", "The mic stays off until you say what you're listening on. With headphones on, you hear yourself; without, it still records.", "Next" },
        { "TO MAC", juce::Colour (0xff56b3d6), "Send it home", "TO MAC puts the picked layer (or every layer) on your Mac's Kaleidobox, in time.", "Let's make one" } };
    return all;
}

void TourOverlay::begin()
{
    goTo (0);
}

void TourOverlay::goTo (int stop)
{
    index = juce::jlimit (0, (int) stops().size() - 1, stop);
    spot = show ? show (index) : juce::Rectangle<int>();
    nextButton.setText (stops()[(size_t) index].next + U8(" \xe2\x80\xba"));
    resized();
    toFront (false);
    repaint();
}

bool TourOverlay::hitTest (int x, int y)
{
    return ! spot.contains (x, y);
}

juce::Rectangle<float> TourOverlay::cardRect() const
{
    const float h = 232.0f, w = (float) getWidth() - 44.0f;
    const bool spotLow = spot.getCentreY() > getHeight() / 2;
    const float y = spotLow ? juce::jmax (70.0f, (float) spot.getY() - h - 18.0f)
                            : juce::jmin ((float) getHeight() - h - 100.0f, (float) spot.getBottom() + 18.0f);
    return { 22.0f, y, w, h };
}

void TourOverlay::resized()
{
    auto card = cardRect();
    nextButton.setBounds (juce::Rectangle<float> (card.getRight() - 172, card.getBottom() - 62, 152, 46).toNearestInt());
    skip.setBounds (juce::Rectangle<float> (card.getX() + 8, card.getBottom() - 60, 110, 42).toNearestInt());
}

void TourOverlay::paint (juce::Graphics& g)
{
    // dim all but the spotlight
    juce::Path dimPath;
    dimPath.addRectangle (getLocalBounds().toFloat());
    if (! spot.isEmpty())
    {
        dimPath.addRoundedRectangle (spot.toFloat().expanded (4), 14.0f);
        dimPath.setUsingNonZeroWinding (false);
    }
    g.setColour (juce::Colour (0x8c231d17));
    g.fillPath (dimPath);
    if (! spot.isEmpty())
    {
        g.setColour (col::amber);
        g.drawRoundedRectangle (spot.toFloat().expanded (4), 14.0f, 3.0f);
    }

    const auto& stop = stops()[(size_t) index];
    auto card = cardRect();
    g.setColour (juce::Colour (0x40000000));
    g.fillRoundedRectangle (card.translated (0, 6), 22.0f);
    g.setColour (col::creamLight);
    g.fillRoundedRectangle (card, 22.0f);

    auto in = card.reduced (18, 18);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.12f));
    g.drawText ("STOP " + juce::String (index + 1) + " OF " + juce::String ((int) stops().size()) + U8(" \xc2\xb7 ") + stop.tag,
                in.removeFromTop (18), juce::Justification::centredLeft);
    in.removeFromTop (4);
    g.setColour (col::ink);
    g.setFont (condensedBold (28.0f));
    g.drawText (stop.title, in.removeFromTop (34), juce::Justification::centredLeft);
    g.setFont (small (15.5f));
    g.drawFittedText (stop.body, in.removeFromTop (66).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
    in.removeFromTop (8);
    auto dots = in.removeFromTop (6);
    const int n = (int) stops().size();
    const float w = (dots.getWidth() - (float) (n - 1) * 5.0f) / (float) n;
    for (int s = 0; s < n; ++s)
    {
        g.setColour (s <= index ? stops()[(size_t) s].colour : col::creamDeep);
        g.fillRoundedRectangle (dots.getX() + (float) s * (w + 5.0f), dots.getY(), w, 6.0f, 3.0f);
    }
}
