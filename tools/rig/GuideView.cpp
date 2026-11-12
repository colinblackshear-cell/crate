#include "GuideView.h"
#include "RigColours.h"
#include "Vintage.h"

namespace
{
using vintage::Weight;

// The Guide is always night: deep blue-black, whatever the panel's theme.
const juce::Colour kSky { 0xff07080f }, kSky2 { 0xff121528 }, kCard { 0xff10121d }, kCardOver { 0xff171a2a },
                   kInk { 0xfff1e7d1 }, kInk2 { 0xffa9a4b8 }, kInk3 { 0xff6d6a80 }, kAmber { 0xffffb54a };

juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }

void wrapped (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& font, juce::Colour colour,
              juce::Justification j = juce::Justification::topLeft)
{
    juce::AttributedString s;
    s.setJustification (j);
    s.setWordWrap (juce::AttributedString::byWord);
    s.setLineSpacing (2.0f);
    s.append (text, font, colour);
    s.draw (g, r);
}

// A rounded pad or step, lit in a colour or dark.
void pad (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool lit, float corner = 3.0f)
{
    if (lit)
    {
        g.setColour (c.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        g.setColour (c);
    }
    else
        g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRoundedRectangle (r, corner);
}

void keys (juce::Graphics& g, juce::Rectangle<float> r, int whites, juce::Colour c, std::initializer_list<int> litWhites)
{
    const float w = r.getWidth() / (float) whites;
    for (int i = 0; i < whites; ++i)
    {
        const bool lit = std::find (litWhites.begin(), litWhites.end(), i) != litWhites.end();
        g.setColour (lit ? c : juce::Colour (0xffe9e1cf));
        g.fillRoundedRectangle (r.getX() + w * (float) i + 0.5f, r.getY(), w - 1.0f, r.getHeight(), 2.0f);
    }
    for (int i = 0; i < whites - 1; ++i)
    {
        const int n = i % 7;
        if (n == 2 || n == 6)
            continue;
        g.setColour (juce::Colour (0xff15131a));
        g.fillRoundedRectangle (r.getX() + w * ((float) i + 0.68f), r.getY(), w * 0.64f, r.getHeight() * 0.6f, 1.5f);
    }
}

void knob (juce::Graphics& g, juce::Point<float> c, float radius, float value, juce::Colour ring)
{
    const float start = juce::MathConstants<float>::pi * 1.25f, end = juce::MathConstants<float>::pi * 2.75f;
    juce::Path arc;
    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, end, true);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.strokePath (arc, juce::PathStrokeType (2.5f));
    juce::Path on;
    on.addCentredArc (c.x, c.y, radius, radius, 0.0f, start, start + (end - start) * value, true);
    g.setColour (ring);
    g.strokePath (on, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colour (0xff2a2733));
    g.fillEllipse (juce::Rectangle<float> (radius * 1.4f, radius * 1.4f).withCentre (c));
    const float a = start + (end - start) * value;
    g.setColour (kInk);
    g.drawLine ({ c, c.getPointOnCircumference (radius * 0.62f, a) }, 2.0f);
}

juce::Path wave (juce::Rectangle<float> r, std::function<float (float)> f, int points = 120)
{
    juce::Path p;
    for (int i = 0; i <= points; ++i)
    {
        const float t = (float) i / (float) points;
        const auto pt = juce::Point<float> (r.getX() + t * r.getWidth(), r.getCentreY() - f (t) * r.getHeight() * 0.5f);
        if (i == 0)
            p.startNewSubPath (pt);
        else
            p.lineTo (pt);
    }
    return p;
}

// A sampled sound: a decaying, wobbling waveform, drawn as a filled band.
void waveform (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, int seed)
{
    juce::Random random (seed);
    const int bars = juce::jmax (8, (int) (r.getWidth() / 3.0f));
    const float w = r.getWidth() / (float) bars;
    for (int i = 0; i < bars; ++i)
    {
        const float t = (float) i / (float) bars;
        const float env = std::pow (1.0f - std::fmod (t * 2.0f, 1.0f), 1.6f) * 0.85f + 0.08f;
        const float h = r.getHeight() * env * (0.55f + 0.45f * random.nextFloat());
        g.setColour (c.withAlpha (0.55f + 0.45f * env));
        g.fillRect (r.getX() + w * (float) i, r.getCentreY() - h * 0.5f, juce::jmax (1.0f, w - 1.0f), h);
    }
}
}

//==============================================================================
const std::vector<GuideView::Entry>& GuideView::entries()
{
    static const std::vector<Entry> e {
        { "Groove", "The Groove", "Where everyone agrees what time it is.",
          "Drums, bass, synth and friends on sixteen steps each. Light up steps, pick a pattern, turn the eight knobs, "
          "and all of Kaleidobox falls in behind you. New to this planet? Land here first.",
          RigColours::song, Art::groove, true, "F1" },
        { "Vocoder", "The Vocoder", "Teaches your voice to speak synthesizer.",
          "Sing into the mic and play chords: your words come out as a choir of robots with excellent pitch. "
          "Thirteen pedals change the weather. Side effects may include spontaneous funk.",
          RigColours::vocoder, Art::vocoder, true, "F2" },
        { "Looper", "The Looper", "Time, folded neatly.",
          "Record a bar, record another on top, repeat until the room is full. Eight layers that never drift off the beat, "
          "up to ten minutes each, with their own effects and a timeline for building whole songs.",
          RigColours::loops, Art::looper, true, "F3" },
        { "Sampler", utf8 ("The Sampler \xc2\xb7 MPC"), "Any sound, caught and put to work.",
          "Catch a noise from the mic, a tool or your crate, then trim it, chop it and spread it across the keys or the pads. "
          "Most of the universe can be sampled, given patience and a decent cable.",
          RigColours::sampler, Art::sampler, true, "F4" },
        { "Mixer", "The Mixer", "Every sound, on a fader. Every tool, one click away.",
          "A real mixing desk: a strip for each tool and each loop layer with its fader, meter, pan, mute, solo, effects "
          "and two sends, to a shared reverb and an echo in time. Along the top, a button into every tool.",
          RigColours::master, Art::mixer, true, "F5" },

        // Under More.
        { "Drum machine", "The Drum Machine", "Sixteen steps of pure analog swagger.",
          "Classic hand-built drum sounds, kick to cowbell, each with its own tune and decay. For when the beat needs to boom.",
          RigColours::mpc, Art::drumMachine, false, {} },
        { "MPC", "The MPC", "Sixty-four pads and a long memory.",
          "Programs, sequences and songs, laid out the way your MPC2000XL thinks, and ready to save to its card.",
          RigColours::mpc, Art::pads, false, {} },
        { "Sampler editor", "The Sample Editor", "A microscope for waveforms.",
          "Every tool for one sound: record, trim, fade, effects, smart chop, and keys to play it on.",
          RigColours::sampler, Art::editor, false, {} },
        { "Bass", "The Bass", "Low frequencies, high morale.",
          "A bass synth with named sounds, its own patterns and a slot for your own plugins. Felt more than heard, which is the point.",
          RigColours::bass, Art::bass, false, {} },
        { "Synth", "The Synth", "Chords, leads and the arpeggiator.",
          "A synth with named sounds, an arpeggiator that plays your chords for you, and a slot for your Arturia collection.",
          RigColours::synth, Art::synth, false, {} },
        { "Song", "The Song", "The running order of the evening.",
          "Song parts A to H in order, with their names and lengths. It's the setlist the Stage and the footer play from.",
          RigColours::song, Art::song, false, {} },
        { "Sounds", "The Sounds", "Your crate, catalogued.",
          "Your samples and your Splice folder, sorted by kind, tempo and key. The perfect snare is in here somewhere. "
          "Statistically, near the bottom.",
          juce::Colour (0xffa1887f), Art::sounds, false, {} },
        { "Stage", "The Big Stage", "Big buttons for small, dark rooms.",
          "The view for playing to actual humans: giant Play and Rec, a tempo you can read from the back, mutes, loop lamps "
          "and the FX pad. Esc brings you back down to earth (and to the Mixer).",
          RigColours::master, Art::stage, false, {} },
        { "Setup", "The Setup", "What your keyboard plays, and where.",
          "Which tool the keys and pads play, the split, sampling one tool into another, knob Learn, the click and the mic. "
          "Set it once, then forget about it until the next gig.",
          RigColours::master, Art::generic, false, {} },
    };
    return e;
}

//==============================================================================
class GuideView::Card : public juce::Button
{
public:
    Card (GuideView& g, int i, int number) : juce::Button (g.shown[(size_t) i].title), guide (g), index (i), entryNumber (number)
    {
        const auto& e = guide.shown[(size_t) index];
        setTooltip ("Opens " + e.title + (e.key.isNotEmpty() ? " (" + e.key + ")." : "."));
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        onClick = [this] { guide.open (index); };
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& e = guide.shown[(size_t) index];
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        if (down)
            r = r.reduced (1.5f);
        if (over)
            vintage::glow (g, r, 14.0f, e.colour, 14.0f, 0.35f);

        juce::ColourGradient fill (over ? kCardOver : kCard, r.getX(), r.getY(), kSky, r.getX(), r.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (e.colour.withAlpha (over ? 0.85f : 0.35f));
        g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, over ? 2.0f : 1.2f);

        auto inner = r.reduced (16.0f, 14.0f);
        const bool big = e.main;

        // The picture: on top for the big entries, at the left for the small ones.
        auto art = big ? inner.removeFromTop (inner.getHeight() * 0.38f) : inner.removeFromLeft (juce::jmin (130.0f, inner.getWidth() * 0.34f));
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path clip;
            clip.addRoundedRectangle (art, 10.0f);
            g.reduceClipRegion (clip);
            juce::ColourGradient halo (e.colour.withAlpha (0.22f), art.getCentreX(), art.getCentreY(),
                                       e.colour.withAlpha (0.0f), art.getCentreX() + art.getWidth() * 0.6f, art.getCentreY(), true);
            g.setGradientFill (halo);
            g.fillRect (art);
            drawArt (g, e.art, art.reduced (art.getWidth() * 0.08f, art.getHeight() * 0.12f), e.colour, over ? guide.phase : 0.0f);
        }
        if (big)
            inner.removeFromTop (12.0f);
        else
            inner.removeFromLeft (16.0f);

        // "ENTRY 03 · F3"
        auto top = inner.removeFromTop (20.0f);
        g.setFont (vintage::lcd (big ? 20.0f : 18.0f));
        g.setColour (kAmber.withAlpha (0.9f));
        g.drawText ("ENTRY " + juce::String (entryNumber).paddedLeft ('0', 2), top, juce::Justification::centredLeft, false);
        // Its key, or (under the mouse) where a click goes.
        g.setFont (over ? vintage::cond (13.0f, Weight::extra, 0.16f) : vintage::lcd (big ? 20.0f : 18.0f));
        g.setColour (over ? e.colour : kInk3);
        g.drawText (over ? utf8 ("OPEN \xe2\x80\xba") : e.key, top, juce::Justification::centredRight, false);

        g.setColour (kInk);
        g.setFont (vintage::wordmark (big ? 30.0f : 24.0f));
        g.drawFittedText (e.title, inner.removeFromTop (big ? 38.0f : 30.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour (e.colour);
        g.setFont (vintage::cond (big ? 17.0f : 15.0f, Weight::bold, 0.02f));
        {
            // One line if it fits, else two.
            const bool fits = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), e.tagline) <= inner.getWidth() / 0.9f;
            g.drawFittedText (e.tagline, inner.removeFromTop (fits ? (big ? 24.0f : 20.0f) : (big ? 42.0f : 36.0f)).toNearestInt(),
                              juce::Justification::topLeft, 2, 0.9f);
        }
        inner.removeFromTop (6.0f);
        wrapped (g, e.blurb, inner, vintage::barlow (big ? 14.5f : 13.5f), kInk2);
    }

private:
    GuideView& guide;
    const int index, entryNumber;
};

//==============================================================================
// The page itself, taller than the window when it must be (it scrolls): the sky, the title,
// then the entries.
class GuideView::Content : public juce::Component
{
public:
    explicit Content (GuideView& g) : guide (g)
    {
        juce::Random random (42);
        for (int i = 0; i < 260; ++i)
            stars.push_back ({ random.nextFloat(), random.nextFloat(), 0.4f + random.nextFloat() * random.nextFloat() * 1.8f,
                               random.nextFloat() * juce::MathConstants<float>::twoPi });

        howTo.setButtonText ("How-to guides  ?");
        howTo.setTooltip ("Step-by-step guides: recording loops, building a song, saving and exporting, singing live, plugging in a guitar.");
        howTo.onClick = [this] { if (guide.onHelp) guide.onHelp(); };
        addAndMakeVisible (howTo);

        startHere.setTooltip ("When lit, Kaleidobox opens on this page. F1-F5 and More still go straight to the tools.");
        startHere.onClick = [this]
        {
            if (guide.setStartsHere)
                guide.setStartsHere (! (guide.startsHere && guide.startsHere()));
            startHere.repaint();
        };
        startHere.isOn = [this] { return guide.startsHere && guide.startsHere(); };
        addAndMakeVisible (startHere);
    }

    void rebuild()
    {
        cards.clear();
        for (size_t i = 0; i < guide.shown.size(); ++i)
            addAndMakeVisible (cards.add (new Card (guide, (int) i, (int) i + 1)));
    }

    // Lays the page out for this width and says how tall it is.
    int layout (int width)
    {
        const int margin = juce::jlimit (16, 48, width / 28);
        auto r = juce::Rectangle<int> (0, 0, width, 100000).reduced (margin, 0);
        const bool narrow = width < 900;

        hero = r.removeFromTop (narrow ? 330 : 300);
        planet = narrow ? juce::Rectangle<int>() : hero.withTrimmedLeft (hero.getWidth() * 3 / 5).reduced (10, 20);
        auto heroText = narrow ? hero : hero.withWidth (hero.getWidth() * 3 / 5);
        auto buttons = heroText.removeFromBottom (54).withSizeKeepingCentre (heroText.getWidth(), 36);
        howTo.setBounds (buttons.removeFromLeft (190));
        buttons.removeFromLeft (14);
        startHere.setBounds (buttons.removeFromLeft (260));
        heroTextArea = heroText;

        // The five screens.
        std::vector<Card*> big, small;
        for (auto* c : cards)
            (guide.shown[(size_t) cards.indexOf (c)].main ? big : small).push_back (c);

        auto place = [&r] (std::vector<Card*>& list, int minWidth, int height, int maxColumns, juce::Rectangle<int>& label)
        {
            if (list.empty())
            {
                label = {};
                return;
            }
            label = r.removeFromTop (44);
            const int columns = juce::jlimit (1, maxColumns, juce::jmin ((int) list.size(), r.getWidth() / minWidth));
            const int w = r.getWidth() / columns;
            for (size_t i = 0; i < list.size(); i += (size_t) columns)
            {
                auto row = r.removeFromTop (height);
                for (size_t c = 0; c < (size_t) columns && i + c < list.size(); ++c)
                    list[i + c]->setBounds (row.removeFromLeft (w));
            }
            r.removeFromTop (18);
        };
        place (big, 250, 450, 5, mainLabel);
        place (small, 380, 196, 3, moreLabel);
        footer = r.removeFromTop (50);
        return footer.getBottom() + 20;
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat();
        juce::ColourGradient sky (kSky2, b.getCentreX(), 0.0f, kSky, b.getCentreX(), juce::jmin (b.getHeight(), 900.0f), false);
        g.setGradientFill (sky);
        g.fillRect (b);
        // A faint nebula behind the title.
        {
            juce::ColourGradient neb (juce::Colour (0x3a8b5cf6), b.getWidth() * 0.78f, 140.0f, juce::Colour (0x008b5cf6), b.getWidth() * 0.78f + 420.0f, 140.0f, true);
            g.setGradientFill (neb);
            g.fillRect (b.withHeight (560.0f));
            juce::ColourGradient neb2 (juce::Colour (0x26ffb54a), b.getWidth() * 0.15f, 60.0f, juce::Colour (0x00ffb54a), b.getWidth() * 0.15f + 360.0f, 60.0f, true);
            g.setGradientFill (neb2);
            g.fillRect (b.withHeight (420.0f));
        }

        // The stars, twinkling.
        for (const auto& s : stars)
        {
            const float tw = 0.55f + 0.45f * std::sin (guide.phase * 1.7f + s.twinkle);
            g.setColour (kInk.withAlpha (juce::jlimit (0.05f, 1.0f, (0.25f + 0.35f * s.size) * tw)));
            const float x = s.x * b.getWidth(), y = s.y * b.getHeight();
            g.fillEllipse (x, y, s.size, s.size);
            if (s.size > 1.9f)
            {
                g.setColour (kInk.withAlpha (0.12f * tw));
                g.drawLine (x - 4.0f, y + s.size * 0.5f, x + 4.0f + s.size, y + s.size * 0.5f, 0.8f);
                g.drawLine (x + s.size * 0.5f, y - 4.0f, x + s.size * 0.5f, y + 4.0f + s.size, 0.8f);
            }
        }

        paintTitle (g);
        if (! planet.isEmpty())
            paintPlanet (g, planet.toFloat());

        auto section = [&g] (juce::Rectangle<int> r, const juce::String& title, const juce::String& note)
        {
            if (r.isEmpty())
                return;
            r = r.reduced (4, 0).withTrimmedTop (10);
            g.setFont (vintage::cond (15.0f, Weight::extra, 0.2f));
            g.setColour (kAmber);
            const int w = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title)) + 4;
            g.drawText (title, r.removeFromLeft (w), juce::Justification::centredLeft, false);
            g.setFont (vintage::barlow (13.0f));
            g.setColour (kInk3);
            const int nw = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), note)) + 4;
            r.removeFromLeft (14);
            g.drawText (note, r.removeFromLeft (nw), juce::Justification::centredLeft, false);
            r.removeFromLeft (14);
            g.setColour (kInk.withAlpha (0.12f));
            g.fillRect (r.withSizeKeepingCentre (r.getWidth(), 1));
        };
        section (mainLabel, "THE FIVE SCREENS", utf8 ("F1\xe2\x80\x93" "F5, along the top. Start with any of them."));
        section (moreLabel, "DEEPER IN", "Each tool in full. Also under More.");

        g.setFont (vintage::barlow (13.0f));
        g.setColour (kInk3);
        g.drawFittedText (utf8 ("Everything here runs on one clock, so whatever you start, the rest keeps up. "
                                "This guide is incomplete, and will stay that way: there's always another tool coming."),
                          footer.reduced (4, 0), juce::Justification::centred, 2);
    }

private:
    void paintTitle (juce::Graphics& g)
    {
        auto r = heroTextArea.toFloat().withTrimmedTop (34.0f);
        g.setFont (vintage::cond (14.0f, Weight::extra, 0.32f));
        g.setColour (kAmber);
        g.drawText ("THE KALEIDOBOX FIELD GUIDE", r.removeFromTop (22.0f), juce::Justification::centredLeft, false);
        g.setFont (vintage::barlow (13.0f));
        g.setColour (kInk3);
        g.drawText (utf8 ("Edition one \xc2\xb7 For use on this planet and nearby ones"), r.removeFromTop (18.0f),
                    juce::Justification::centredLeft, false);
        r.removeFromTop (4.0f);

        // The title, with a little LCD-amber shadow.
        float size = juce::jlimit (54.0f, 92.0f, r.getWidth() / 7.0f);
        {
            // Kaleidobox is a long word: shrink it until it fits the column.
            const auto width = juce::GlyphArrangement::getStringWidth (vintage::wordmark (size), "Kaleidobox");
            if (width > r.getWidth() - 6.0f)
                size *= (r.getWidth() - 6.0f) / width;
        }
        auto titleArea = r.removeFromTop (size * 1.05f);
        g.setFont (vintage::wordmark (size));
        g.setColour (kAmber.withAlpha (0.28f));
        g.drawText ("Kaleidobox", titleArea.translated (3.0f, 4.0f), juce::Justification::centredLeft, false);
        g.setColour (kInk);
        g.drawText ("Kaleidobox", titleArea, juce::Justification::centredLeft, false);

        g.setFont (vintage::cond (24.0f, Weight::semi, 0.01f));
        g.setColour (kInk2);
        g.drawFittedText ("A guide to making noise in the known universe", r.removeFromTop (30.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
        r.removeFromTop (14.0f);

        // In large, friendly letters.
        g.setFont (vintage::cond (21.0f, Weight::extra, 0.12f));
        const juce::String sticker ("TOUCH ANYTHING. NOTHING BREAKS.");
        const float w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), sticker) + 34.0f;
        auto badge = r.removeFromTop (40.0f).withWidth (juce::jmin (w, r.getWidth()));
        vintage::glow (g, badge, 8.0f, kAmber, 10.0f, 0.3f);
        g.setColour (kAmber);
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (juce::Colour (0xff1a1206));
        g.drawFittedText (sticker, badge.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

    // A record that is also a planet, with a ring and a small moon going round.
    void paintPlanet (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float radius = juce::jmin (area.getHeight() * 0.36f, area.getWidth() * 0.24f);
        const auto c = area.getCentre();
        const float tilt = -0.32f;
        const float ringW = radius * 2.15f, ringH = radius * 0.5f;

        auto ring = [&] (bool back)
        {
            juce::Graphics::ScopedSaveState save (g);
            const auto t = juce::AffineTransform::rotation (tilt, c.x, c.y);
            juce::Path half;
            half.addCentredArc (c.x, c.y, ringW, ringH, 0.0f, back ? -juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi,
                                back ? juce::MathConstants<float>::halfPi : juce::MathConstants<float>::pi * 1.5f, true);
            for (int i = 0; i < 3; ++i)
            {
                juce::Path p (half);
                const float s = 1.0f - (float) i * 0.08f;
                p.applyTransform (juce::AffineTransform::scale (s, s, c.x, c.y).followedBy (t));
                g.setColour ((i == 1 ? kAmber : RigColours::vocoder).withAlpha (back ? 0.25f : 0.7f));
                g.strokePath (p, juce::PathStrokeType (i == 1 ? 3.0f : 1.5f));
            }
        };

        // The moon, behind or in front, as it goes round.
        const float a = phase() * 0.35f;
        const auto moon = juce::Point<float> (c.x + std::cos (a) * ringW * 1.08f, c.y + std::sin (a) * ringH * 1.25f)
                              .transformedBy (juce::AffineTransform::rotation (tilt, c.x, c.y));
        const bool moonBehind = std::sin (a) < 0.0f;
        auto drawMoon = [&]
        {
            g.setColour (RigColours::loops.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (moon));
            g.setColour (RigColours::loops);
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (moon));
        };

        if (moonBehind)
            drawMoon();
        ring (true);

        // The record.
        const auto disc = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c);
        vintage::glow (g, disc, radius, juce::Colour (0xff8b5cf6), 26.0f, 0.4f);
        g.setColour (juce::Colour (0xff0b0a10));
        g.fillEllipse (disc);
        for (float rr = radius * 0.42f; rr < radius * 0.97f; rr += 3.2f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.035f + 0.02f * std::sin (rr * 0.7f)));
            g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 1.0f);
        }
        {
            // The shine on the grooves, turning slowly.
            juce::Graphics::ScopedSaveState save (g);
            const float spin = phase() * 0.25f;
            juce::Path shine;
            shine.addPieSegment (disc, spin, spin + 0.55f, 0.4f);
            shine.addPieSegment (disc, spin + juce::MathConstants<float>::pi, spin + juce::MathConstants<float>::pi + 0.55f, 0.4f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillPath (shine);
        }
        const auto label = disc.reduced (radius * 0.6f);
        juce::ColourGradient lab (kAmber, label.getX(), label.getY(), RigColours::mpc, label.getRight(), label.getBottom(), false);
        g.setGradientFill (lab);
        g.fillEllipse (label);
        g.setColour (juce::Colour (0xff1a1206));
        g.setFont (vintage::cond (radius * 0.13f, Weight::extra, 0.1f));
        g.drawText ("SIDE A", label.withTrimmedBottom (label.getHeight() * 0.6f), juce::Justification::centredBottom, false);
        g.setFont (vintage::lcd (radius * 0.16f));
        g.drawText ("33 1/3", label.withTrimmedTop (label.getHeight() * 0.62f), juce::Justification::centredTop, false);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (c));

        ring (false);
        if (! moonBehind)
            drawMoon();
    }

    float phase() const { return guide.phase; }

    class LampToggle : public juce::Button
    {
    public:
        LampToggle() : juce::Button ("Open Kaleidobox on this page") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            auto r = getLocalBounds().toFloat();
            const bool on = isOn && isOn();
            vintage::drawLamp (g, { r.getX() + 10.0f, r.getCentreY() }, on ? vintage::Lit::play : vintage::Lit::none, 14.0f);
            g.setFont (vintage::cond (16.0f, Weight::bold, 0.04f));
            g.setColour (over ? kInk : kInk2);
            g.drawText (getName(), r.withTrimmedLeft (26.0f), juce::Justification::centredLeft, false);
        }
        std::function<bool()> isOn;
    };

    class GhostButton : public juce::Button
    {
    public:
        GhostButton() : juce::Button ("How-to") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (down ? 1.5f : 0.5f);
            g.setColour (kInk.withAlpha (over ? 0.12f : 0.05f));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (kInk.withAlpha (over ? 0.6f : 0.3f));
            g.drawRoundedRectangle (r, 8.0f, 1.2f);
            g.setFont (vintage::cond (17.0f, Weight::bold, 0.06f));
            g.setColour (kInk);
            g.drawText (getButtonText(), r, juce::Justification::centred, false);
        }
    };

    struct Star { float x, y, size, twinkle; };
    GuideView& guide;
    std::vector<Star> stars;
    juce::Rectangle<int> hero, heroTextArea, planet, mainLabel, moreLabel, footer;
    GhostButton howTo;
    LampToggle startHere;
public:
    juce::OwnedArray<Card> cards;
    juce::Rectangle<int> getHero() const { return hero; }
};

//==============================================================================
GuideView::GuideView (std::function<bool (const juce::String&)> hasPage)
{
    for (const auto& e : entries())
        if (hasPage == nullptr || hasPage (e.page))
            shown.push_back (e);
    content = std::make_unique<Content> (*this);
    content->rebuild();
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    viewport.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, kInk.withAlpha (0.25f));
    addAndMakeVisible (viewport);
    setOpaque (true);
}

GuideView::~GuideView() = default;

void GuideView::paint (juce::Graphics& g) { g.fillAll (kSky); }

void GuideView::resized()
{
    viewport.setBounds (getLocalBounds());
    const int w = getWidth() - (getHeight() > 0 ? viewport.getScrollBarThickness() : 0);
    content->setSize (w, juce::jmax (getHeight(), content->layout (w)));
}

void GuideView::visibilityChanged()
{
    if (isShowing())
        startTimerHz (20);
    else
        stopTimer();
}

void GuideView::timerCallback()
{
    if (! isShowing())
        return stopTimer();
    phase += 0.05f;
    // Only the sky over the title and the card under the mouse move.
    content->repaint (content->getHero());
    for (auto* c : content->cards)
        if (c->isOver())
            c->repaint();
}

void GuideView::refresh() { content->repaint(); }

void GuideView::open (int i)
{
    if (juce::isPositiveAndBelow (i, (int) shown.size()) && onOpen)
        onOpen (shown[(size_t) i].page);
}

juce::Component* GuideView::getCard (int i) const
{
    return juce::isPositiveAndBelow (i, content->cards.size()) ? content->cards[i] : nullptr;
}

//==============================================================================
void GuideView::drawArt (juce::Graphics& g, Art art, juce::Rectangle<float> r, juce::Colour c, float phase)
{
    const auto centre = r.getCentre();
    switch (art)
    {
        case Art::groove:
        {
            // Four tracks of sixteen steps, the play head going along.
            const juce::Colour rows[] = { RigColours::mpc, RigColours::bass, RigColours::synth, RigColours::vocoder };
            const char* pattern[] = { "x...x...x...x.x.", "x..x..x...x..x..", "..x...x...x.x...", "x.......x......." };
            const int head = (int) (phase * 4.0f) % 16;
            const float cw = r.getWidth() / 16.0f, ch = r.getHeight() / 4.0f;
            for (int row = 0; row < 4; ++row)
                for (int s = 0; s < 16; ++s)
                {
                    auto cell = juce::Rectangle<float> (r.getX() + cw * (float) s, r.getY() + ch * (float) row, cw, ch).reduced (juce::jmax (1.5f, cw * 0.14f));
                    pad (g, cell, rows[row], pattern[row][s] == 'x', 2.5f);
                    if (s == head)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.18f));
                        g.fillRoundedRectangle (cell, 2.5f);
                    }
                }
            break;
        }
        case Art::vocoder:
        {
            // A voice goes in at the left, through a bank of filters, and comes out as bands.
            auto left = r.removeFromLeft (r.getWidth() * 0.22f);
            auto mic = left.withSizeKeepingCentre (left.getWidth() * 0.5f, left.getHeight() * 0.5f).translated (0, -left.getHeight() * 0.1f);
            g.setColour (kInk.withAlpha (0.85f));
            g.fillRoundedRectangle (mic, mic.getWidth() * 0.5f);
            g.setColour (kSky);
            for (float y = mic.getY() + 5.0f; y < mic.getCentreY(); y += 4.0f)
                g.drawLine (mic.getX() + 3.0f, y, mic.getRight() - 3.0f, y, 1.0f);
            g.setColour (kInk.withAlpha (0.6f));
            g.drawLine (mic.getCentreX(), mic.getBottom(), mic.getCentreX(), left.getBottom() - 4.0f, 2.0f);
            g.drawLine (mic.getCentreX() - 10.0f, left.getBottom() - 4.0f, mic.getCentreX() + 10.0f, left.getBottom() - 4.0f, 2.0f);

            auto waveArea = r.removeFromLeft (r.getWidth() * 0.3f);
            g.setColour (c.withAlpha (0.7f));
            g.strokePath (wave (waveArea, [phase] (float t) { return std::sin (t * 18.0f + phase * 3.0f) * (0.3f + 0.5f * std::sin (t * 3.1f)); }),
                          juce::PathStrokeType (2.0f));
            const int bands = 12;
            const float bw = r.getWidth() / (float) bands;
            for (int i = 0; i < bands; ++i)
            {
                const float level = 0.25f + 0.7f * std::abs (std::sin ((float) i * 0.9f + phase * 2.0f + std::sin ((float) i * 0.3f)));
                const float h = r.getHeight() * level;
                auto bar = juce::Rectangle<float> (r.getX() + bw * (float) i, r.getBottom() - h, bw, h).reduced (bw * 0.18f, 0);
                juce::ColourGradient grad (c, bar.getX(), bar.getY(), c.withAlpha (0.25f), bar.getX(), bar.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRoundedRectangle (bar, 2.0f);
            }
            break;
        }
        case Art::looper:
        {
            // Layers of loops, round and round, with the play head.
            const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.48f;
            const float lengths[] = { 1.0f, 0.75f, 0.5f, 0.88f, 0.62f };
            for (int i = 0; i < 5; ++i)
            {
                const float rr = rad * (1.0f - (float) i * 0.17f);
                juce::Path back, on;
                back.addCentredArc (centre.x, centre.y, rr, rr, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
                g.setColour (juce::Colours::white.withAlpha (0.06f));
                g.strokePath (back, juce::PathStrokeType (rad * 0.1f));
                on.addCentredArc (centre.x, centre.y, rr, rr, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * lengths[i], true);
                g.setColour (c.withAlpha (1.0f - (float) i * 0.15f).withRotatedHue ((float) i * 0.03f));
                g.strokePath (on, juce::PathStrokeType (rad * 0.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            const float a = phase * 1.2f;
            g.setColour (kInk);
            g.drawLine ({ centre, centre.getPointOnCircumference (rad * 1.06f, a) }, 2.0f);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (centre));
            break;
        }
        case Art::sampler:
        case Art::editor:
        {
            auto pads = art == Art::sampler ? r.removeFromRight (r.getHeight() * 0.9f) : juce::Rectangle<float>();
            auto w = r.reduced (0, r.getHeight() * 0.1f);
            if (art == Art::editor)
            {
                auto sel = w.withTrimmedLeft (w.getWidth() * 0.3f).withWidth (w.getWidth() * 0.35f);
                g.setColour (c.withAlpha (0.16f));
                g.fillRect (sel);
                g.setColour (c);
                g.fillRect (sel.withWidth (2.0f));
                g.fillRect (sel.withTrimmedLeft (sel.getWidth() - 2.0f));
            }
            waveform (g, w, art == Art::editor ? kInk.withAlpha (0.85f) : c, art == Art::editor ? 7 : 3);
            // The chops.
            g.setColour (kAmber.withAlpha (0.8f));
            for (float t : { 0.25f, 0.5f, 0.75f })
                if (art == Art::sampler)
                    g.drawLine (w.getX() + w.getWidth() * t, w.getY(), w.getX() + w.getWidth() * t, w.getBottom(), 1.2f);
            if (! pads.isEmpty())
            {
                pads = pads.reduced (pads.getWidth() * 0.08f);
                const float s = pads.getWidth() / 4.0f;
                const int lit = (int) (phase * 3.0f) % 16;
                for (int i = 0; i < 16; ++i)
                    pad (g, juce::Rectangle<float> (pads.getX() + s * (float) (i % 4), pads.getY() + s * (float) (i / 4), s, s).reduced (s * 0.12f),
                         c, i == lit || i == 5 || i == 10, 3.0f);
            }
            break;
        }
        case Art::stage:
        {
            // Two spotlights on a stage, and a mic stand.
            auto floor = r.removeFromBottom (r.getHeight() * 0.18f);
            for (float x : { 0.3f, 0.7f })
            {
                juce::Path beam;
                const float top = r.getX() + r.getWidth() * x;
                beam.startNewSubPath (top - 4.0f, r.getY());
                beam.lineTo (top + 4.0f, r.getY());
                beam.lineTo (centre.x + (x - 0.5f) * r.getWidth() * 0.2f + r.getWidth() * 0.2f, floor.getY());
                beam.lineTo (centre.x + (x - 0.5f) * r.getWidth() * 0.2f - r.getWidth() * 0.2f, floor.getY());
                beam.closeSubPath();
                juce::ColourGradient grad (kAmber.withAlpha (0.45f), top, r.getY(), kAmber.withAlpha (0.04f), top, floor.getY(), false);
                g.setGradientFill (grad);
                g.fillPath (beam);
            }
            g.setColour (kAmber.withAlpha (0.25f));
            g.fillEllipse (floor.withSizeKeepingCentre (r.getWidth() * 0.55f, floor.getHeight() * 0.8f));
            g.setColour (kInk.withAlpha (0.25f));
            g.fillRect (floor.removeFromTop (2.0f));
            g.setColour (kInk);
            const float standTop = r.getY() + r.getHeight() * 0.38f;
            g.drawLine (centre.x, standTop, centre.x, floor.getY() - 2.0f, 2.5f);
            g.drawLine (centre.x - 14.0f, floor.getY() - 2.0f, centre.x + 14.0f, floor.getY() - 2.0f, 2.5f);
            g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 20.0f).withCentre ({ centre.x, standTop - 6.0f }), 6.0f);
            break;
        }
        case Art::pads:
        case Art::drumMachine:
        {
            if (art == Art::drumMachine)
            {
                // A row of sixteen step buttons in four colours, knobs above.
                const juce::Colour groups[] = { juce::Colour (0xffe8432f), juce::Colour (0xfff39b33), juce::Colour (0xffefcb45), juce::Colour (0xfff1e7d1) };
                auto knobs = r.removeFromTop (r.getHeight() * 0.5f);
                for (int i = 0; i < 6; ++i)
                    knob (g, { knobs.getX() + knobs.getWidth() * ((float) i + 0.5f) / 6.0f, knobs.getCentreY() },
                          juce::jmin (knobs.getHeight() * 0.32f, knobs.getWidth() / 15.0f), 0.2f + 0.13f * (float) i, c);
                const float s = r.getWidth() / 16.0f;
                const char* steps = "x...x..xx...x.x.";
                for (int i = 0; i < 16; ++i)
                {
                    auto b = juce::Rectangle<float> (r.getX() + s * (float) i, r.getCentreY() - s * 0.7f, s, s * 1.4f).reduced (s * 0.12f, 0);
                    g.setColour (groups[i / 4].withAlpha (0.85f));
                    g.fillRoundedRectangle (b, 2.0f);
                    if (steps[i] == 'x')
                    {
                        g.setColour (juce::Colour (0xffff3b2a));
                        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ b.getCentreX(), b.getY() + 6.0f }));
                    }
                }
                break;
            }
            const float s = juce::jmin (r.getWidth(), r.getHeight()) / 4.0f;
            auto grid = juce::Rectangle<float> (s * 4.0f, s * 4.0f).withCentre (centre);
            for (int i = 0; i < 16; ++i)
                pad (g, juce::Rectangle<float> (grid.getX() + s * (float) (i % 4), grid.getY() + s * (float) (i / 4), s, s).reduced (s * 0.1f),
                     c, i == 1 || i == 6 || i == 11 || i == 12, 4.0f);
            break;
        }
        case Art::bass:
        case Art::synth:
        {
            auto top = r.removeFromTop (r.getHeight() * 0.5f);
            if (art == Art::bass)
            {
                g.setColour (c);
                g.strokePath (wave (top, [] (float t) { const float x = std::fmod (t * 2.0f, 1.0f); return 0.8f - 1.6f * x; }, 200),
                              juce::PathStrokeType (2.5f));
            }
            else
            {
                for (int i = 0; i < 5; ++i)
                    knob (g, { top.getX() + top.getWidth() * ((float) i + 0.5f) / 5.0f, top.getCentreY() },
                          juce::jmin (top.getHeight() * 0.32f, top.getWidth() / 12.0f), 0.15f + 0.17f * (float) i, c);
            }
            r.removeFromTop (r.getHeight() * 0.12f);
            if (art == Art::bass)
                keys (g, r, 10, c, { 0, 4 });
            else
                keys (g, r, 14, c, { 2, 4, 6, 9 });
            break;
        }
        case Art::song:
        {
            // Lanes of blocks along a timeline: A, B, A, C.
            const juce::Colour lanes[] = { RigColours::mpc, RigColours::bass, RigColours::synth, RigColours::loops };
            const float spans[][4][2] = { { { 0.0f, 0.5f }, { 0.5f, 1.0f } }, { { 0.0f, 0.25f }, { 0.25f, 0.75f }, { 0.75f, 1.0f } },
                                          { { 0.25f, 0.5f }, { 0.75f, 1.0f } }, { { 0.5f, 1.0f } } };
            const int counts[] = { 2, 3, 2, 1 };
            auto ruler = r.removeFromTop (r.getHeight() * 0.18f);
            g.setFont (vintage::cond (ruler.getHeight() * 0.9f, Weight::extra));
            const char* parts[] = { "A", "B", "A", "C" };
            for (int i = 0; i < 4; ++i)
            {
                g.setColour (kAmber);
                g.drawText (parts[i], ruler.withX (ruler.getX() + ruler.getWidth() * (float) i / 4.0f).withWidth (ruler.getWidth() / 4.0f).translated (3, 0),
                            juce::Justification::centredLeft, false);
            }
            const float lh = r.getHeight() / 4.0f;
            for (int l = 0; l < 4; ++l)
                for (int b = 0; b < counts[l]; ++b)
                {
                    auto block = juce::Rectangle<float> (r.getX() + r.getWidth() * spans[l][b][0], r.getY() + lh * (float) l,
                                                         r.getWidth() * (spans[l][b][1] - spans[l][b][0]), lh).reduced (1.5f, lh * 0.15f);
                    g.setColour (lanes[l].withAlpha (0.8f));
                    g.fillRoundedRectangle (block, 3.0f);
                }
            const float x = r.getX() + r.getWidth() * std::fmod (phase * 0.15f, 1.0f);
            g.setColour (kInk);
            g.drawLine (x, ruler.getY(), x, r.getBottom(), 1.5f);
            break;
        }
        case Art::sounds:
        {
            // A crate of records, flipping.
            auto crate = r.removeFromBottom (r.getHeight() * 0.42f);
            const int n = 7;
            const float w = r.getWidth() / (float) (n + 1);
            const juce::Colour sleeves[] = { RigColours::mpc, RigColours::vocoder, RigColours::synth, RigColours::sampler,
                                             RigColours::loops, RigColours::bass, kInk };
            for (int i = 0; i < n; ++i)
            {
                const float lean = ((float) i - 3.0f) * 0.07f;
                auto sleeve = juce::Rectangle<float> (w * 1.5f, r.getHeight() * 0.95f + crate.getHeight() * 0.5f)
                                  .withBottomY (crate.getY() + crate.getHeight() * 0.55f)
                                  .withCentre ({ r.getX() + w * ((float) i + 1.0f), 0 })
                                  .withBottomY (crate.getY() + crate.getHeight() * 0.55f);
                juce::Path p;
                p.addRoundedRectangle (sleeve, 2.0f);
                p.applyTransform (juce::AffineTransform::rotation (lean, sleeve.getCentreX(), sleeve.getBottom()));
                g.setColour (sleeves[i].withAlpha (0.85f).darker (0.15f * (float) (i % 2)));
                g.fillPath (p);
                g.setColour (kSky.withAlpha (0.5f));
                g.strokePath (p, juce::PathStrokeType (1.0f));
            }
            g.setColour (juce::Colour (0xff6e3d22));
            g.fillRoundedRectangle (crate, 3.0f);
            g.setColour (juce::Colour (0xffa2643b));
            g.fillRect (crate.reduced (6.0f, crate.getHeight() * 0.38f));
            break;
        }
        case Art::mixer:
        {
            const int n = 7;
            const float w = r.getWidth() / (float) n;
            const float levels[] = { 0.7f, 0.45f, 0.8f, 0.55f, 0.3f, 0.65f, 0.75f };
            for (int i = 0; i < n; ++i)
            {
                const float x = r.getX() + w * ((float) i + 0.5f);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillRoundedRectangle (x - 2.0f, r.getY(), 4.0f, r.getHeight(), 2.0f);
                const float level = juce::jlimit (0.1f, 0.95f, levels[i] + 0.05f * std::sin (phase * 2.0f + (float) i));
                const float y = r.getBottom() - r.getHeight() * level;
                g.setColour (i == n - 1 ? kInk : RigColours::forStrip (i));
                g.fillRoundedRectangle (x - w * 0.32f, y - 5.0f, w * 0.64f, 10.0f, 2.0f);
            }
            break;
        }
        case Art::generic:
        default:
        {
            // A knob, glowing: a tool still to come.
            const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.38f;
            knob (g, centre, rad, 0.5f + 0.3f * std::sin (phase), c);
            break;
        }
    }
}
