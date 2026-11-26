#include "PocketIntro.h"

#include "PocketIdeas.h"

#include "core/ui/Kaleidoscope.h"

using namespace pocket;

//==============================================================================
namespace
{
const juce::Colour kStage { 0xff0f0c15 }, kStageGlow { 0xff221b2d };
const juce::Colour kPaper { 0xfff3eadb }, kSoft { 0xffe4dbee }, kDim { 0xffb7a9c2 };

float rise (float opened, float at) { return crate::kaleidoscope::rise (opened, at); }
}

const std::vector<juce::Colour>& TitleScreen::petalColours() { return crate::kaleidoscope::petalColours(); }

TitleScreen::TitleScreen()
{
    for (auto* b : { &tour, &start })
        addAndMakeVisible (*b);
    start.setFontHeight (21.0f);
    tour.setFontHeight (17.0f);
    tour.onClick = [this] { if (onTour) onTour(); };
    start.onClick = [this] { if (onStart) onStart(); };
}

void TitleScreen::setContinue (const juce::String& name, const juce::String& meta, std::vector<juce::Colour> layers)
{
    continueName = name;
    continueMeta = meta;
    continueLayers = std::move (layers);
    resized();
    repaint();
}

void TitleScreen::visibilityChanged()
{
    if (isVisible())
    {
        if (opened < 9.0f)
            opened = 0.0f;   // (the screenshots keep it open)
        startTimerHz (30);
    }
    else
        stopTimer();
}

void TitleScreen::timerCallback()
{
    const float level = micLevel ? micLevel() : 0.0f;
    bloom = juce::jmax (bloom * 0.9f, juce::jlimit (0.0f, 1.0f, std::sqrt (level) * 1.4f));
    angle += 0.004f + bloom * 0.05f;   // turns slowly, faster when you make a noise
    const bool opening = opened < 3.6f;
    opened += 1.0f / 30.0f;
    if (opening)
        repaint();
    else
        repaint (mandalaRect().expanded (30).toNearestInt());
}

juce::Rectangle<float> TitleScreen::mandalaRect() const
{
    const float below = (float) (continueName.isNotEmpty() ? getHeight() - 320 : getHeight() - 236);   // the card, or START MAKING
    const float d = juce::jmax (120.0f, juce::jmin ((float) getWidth() - 100.0f, 270.0f, below - 88.0f - 190.0f));
    return { (float) getWidth() * 0.5f - d * 0.5f, 54.0f + 34.0f, d, d };
}

juce::Rectangle<int> TitleScreen::continueCard() const
{
    if (continueName.isEmpty())
        return {};
    return { 20, getHeight() - 236 - 84, getWidth() - 40, 76 };
}

void TitleScreen::resized()
{
    const int w = getWidth(), h = getHeight();
    start.setBounds (20, h - 226, w - 40, 60);
    tour.setBounds (20, h - 156, w - 40, 52);
}

void TitleScreen::mouseUp (const juce::MouseEvent& e)
{
    if (continueCard().contains (e.getPosition()) && onContinue)
        return onContinue();
    if (onStart)
        onStart();   // a tap anywhere else: straight in
}

void TitleScreen::paint (juce::Graphics& g)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    g.setGradientFill (juce::ColourGradient (kStageGlow, w * 0.5f, h * 0.3f, kStage, w * 0.5f, h * 0.3f + h * 0.62f, true));
    g.fillAll();

    g.setColour (kDim);
    g.setFont (spaced (lcdFont (17.0f), 0.12f));
    g.drawText (U8("KB-1 \xc2\xb7 POCKET"), juce::Rectangle<float> (0, 54, w, 22), juce::Justification::centred);

    // the kaleidoscope: a petal for each tool, blooming in one by one (core/ui/Kaleidoscope.h: the Mac draws the same)
    auto m = mandalaRect();
    crate::kaleidoscope::draw (g, m, angle, bloom, opened, std::fmod (opened, 1.1f) < 0.55f);

    // the name, rising in under it, and what it does
    auto text = juce::Rectangle<float> (0, m.getBottom() + 22, w, 60);
    const float a = rise (opened, 1.5f), b = rise (opened, 1.8f), d = rise (opened, 2.1f);
    g.setColour (kPaper.withAlpha (a));
    g.setFont (fraunces (54.0f).italicised());
    g.drawText ("Kaleidobox", text.translated (0, 18.0f * (1.0f - a)), juce::Justification::centred);
    g.setColour (kSoft.withAlpha (b));
    g.setFont (small (17.0f));
    g.drawFittedText ("Every sound is the start of a song.\nHum it, loop it, build it out.",
                      juce::Rectangle<int> (20, (int) (m.getBottom() + 88 + 18.0f * (1.0f - b)), getWidth() - 40, 48), juce::Justification::centred, 2);
    g.setColour (col::lcdAmber.withAlpha (d));
    g.setFont (spaced (lcdFont (18.0f), 0.05f));
    g.drawText ("MAKE A NOISE. THE PETALS LISTEN.", juce::Rectangle<float> (0, m.getBottom() + 146 + 18.0f * (1.0f - d), w, 22), juce::Justification::centred);

    // PICK UP WHERE YOU LEFT OFF
    if (auto card = continueCard().toFloat(); ! card.isEmpty())
    {
        const float e = rise (opened, 2.4f);
        juce::Graphics::ScopedSaveState save (g);
        g.setOpacity (e);
        card = card.translated (0, 14.0f * (1.0f - e));
        g.setColour (juce::Colour (0xff221b2d));
        g.fillRoundedRectangle (card, 14.0f);
        g.setColour (juce::Colour (0xff3a2f4f));
        g.drawRoundedRectangle (card.reduced (0.75f), 14.0f, 1.5f);
        auto play = card.removeFromRight (70).withSizeKeepingCentre (46, 46);
        g.setColour (col::amber);
        g.fillEllipse (play);
        juce::Path tri;
        const auto pc = play.getCentre();
        tri.addTriangle (pc.x - 5, pc.y - 8, pc.x - 5, pc.y + 8, pc.x + 9, pc.y);
        g.setColour (col::onBright);
        g.fillPath (tri);
        auto in = card.reduced (16, 10);
        g.setColour (juce::Colour (0xffb7a9c2));
        g.setFont (spaced (condensedBold (12.0f), 0.14f));
        g.drawText ("PICK UP WHERE YOU LEFT OFF", in.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xfff3eadb));
        g.setFont (small (18.0f).boldened());
        g.drawFittedText (continueName, in.removeFromTop (24).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        auto row = in.removeFromTop (18);
        float x = row.getX();
        for (size_t l = 0; l < juce::jmin<size_t> (continueLayers.size(), 8); ++l)
        {
            g.setColour (continueLayers[l]);
            g.fillRoundedRectangle (x, row.getCentreY() - 3.0f, 18.0f, 6.0f, 3.0f);
            x += 22.0f;
        }
        g.setColour (juce::Colour (0xffb7a9c2));
        g.setFont (small (13.0f));
        g.drawFittedText (continueMeta, row.withLeft (x + (continueLayers.empty() ? 0.0f : 4.0f)).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    g.setColour (kDim);
    g.setFont (small (13.0f));
    g.drawText ("Tap anywhere to skip the opening", juce::Rectangle<int> (0, getHeight() - 92, getWidth(), 20), juce::Justification::centred);
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
        { "TOOLS", col::ink, "Pick a tool", "LOOPS, DRUMS, PADS, SAMPLER, SYNTH, CHORDS and VOCODER live down here, and EFFECTS puts a pedalboard on any of them. Tap one, or slide a finger along the row. The song keeps playing.", "Next" },
        { "REC", juce::Colour (0xffec4e8a), "One button records", U8("REC records the tool on screen onto the next empty loop layer, in time. Tap it again to stop. Tap the display up top for count-in and length; BPM sets the tempo, CLICK the metronome."), "Got it" },
        { "CHORDS", juce::Colour (0xff9a5cf0), "Chords in the song's key", "Eight chords of the song's key: tap one, or fill the PROGRESSION for one chord a bar. They set the vocoder's notes, and SYNTH plays in the same key.", "Next" },
        { "PADS", col::amber, "Hit it!", "Every pad holds a sound, and a factory kit is already loaded. Tap one. Press REC to play a pattern in, then it locks onto a layer. Hold a pad for its sound on the SAMPLER.", "I hit it" },
        { "LOOPS", col::ink, "Build it up", "Every take lands on its own layer, marked with the tool that made it. Tap a layer for IN / OUT, FIT or QUANTIZE, EFFECTS and TO MAC, or hold one for the same in a quick menu.", "Next" },
        { "VOCODER", juce::Colour (0xfff27bb0), "Headphones first", "The mic stays off until you say what you're listening on. With headphones on, you hear yourself; without, it still records.", "Next" },
        { "MENU", juce::Colour (0xffa877f5), "Your songs, and the rest", "Tap the menu icon, or swipe in from the left edge: your songs, Sounds, the sound recorder, Mac link, Remote, Settings, and this tour. Hold a song to rename it or delete it.", "Let's make one" } };
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
    g.setColour (juce::Colour (0x8c221b2d));
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
