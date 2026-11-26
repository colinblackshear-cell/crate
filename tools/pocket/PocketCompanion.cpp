#include "PocketCompanion.h"

namespace pocket
{
//==============================================================================
// A big coloured tile: a name and one plain line about what it does.
class AddLayerSheet::Tile : public juce::Component
{
public:
    Tile (juce::String n, juce::String l, juce::Colour c) : name (std::move (n)), line (std::move (l)), colour (c) {}
    std::function<void()> onClick;
    std::function<void (juce::Graphics&, juce::Point<float>, juce::Colour)> icon;   // drawn in a badge, top right

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (4.0f);
        const float sink = down ? 3.0f : 0.0f;
        if (! down)
            for (int i = 1; i <= 4; ++i)   // a soft shadow under it
            {
                g.setColour (juce::Colours::black.withAlpha (0.09f));
                g.fillRoundedRectangle (r.translated (0, 2.0f + (float) i * 1.3f).expanded ((float) i * 0.7f, 0), 18.0f + (float) i * 0.7f);
            }
        g.setColour (colour.darker (0.55f));
        g.fillRoundedRectangle (r.withTrimmedTop (4.0f), 18.0f);
        auto face = r.withTrimmedBottom (4.0f).translated (0.0f, sink);
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.12f), face.getX(), face.getY(), colour.darker (0.08f), face.getX(), face.getBottom(), false));
        g.fillRoundedRectangle (face, 18.0f);
        // light from above: a sheen over the top half, and a bright hairline round the edge
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (down ? 0.10f : 0.26f), 0, face.getY(), juce::Colours::white.withAlpha (0.0f), 0,
                                                 face.getCentreY(), false));
        g.fillRoundedRectangle (face.reduced (1.5f), 16.5f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (face.reduced (0.7f), 18.0f, 1.0f);
        auto in = face.reduced (12.0f, 8.0f);
        if (icon)
        {
            // the tool's icon in a dark round badge before the name (the line reads full width under them)
            const float d = 36.0f;
            auto badge = juce::Rectangle<float> (in.getX(), in.getY(), d, d);
            g.setColour (col::onBright.withAlpha (0.82f));
            g.fillEllipse (badge);
            juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (juce::AffineTransform::scale (1.05f, 1.05f, badge.getCentreX(), badge.getCentreY()));
            icon (g, badge.getCentre(), colour.brighter (0.15f));
        }
        // the badge and the name share the top row; the line has its own rows under them, so nothing overlaps
        auto head = in.removeFromTop (36.0f);
        g.setColour (col::onBright);
        g.setFont (condensedBold (23.0f));
        g.drawFittedText (name, head.withTrimmedLeft (icon ? 44.0f : 0.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        in.removeFromTop (5.0f);
        g.setFont (small (13.5f));
        g.drawFittedText (line, in.toNearestInt(), juce::Justification::topLeft, 2, 1.0f);   // (1.0: wrap, never squeeze)
    }
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        down = false;
        repaint();
        if (getLocalBounds().contains (e.getPosition()) && onClick)
            onClick();
    }
    void mouseExit (const juce::MouseEvent&) override { down = false; repaint(); }

private:
    juce::String name, line;
    juce::Colour colour;
    bool down = false;
};

static void giveIcon (PButton&, juce::String, std::function<void (juce::Graphics&, juce::Point<float>, juce::Colour)>, float);

// The Add a layer icons: the tool's own, with MY VOICE a mic and its sound, BASS a long low wave, VOCODER a mic in waves,
// RESAMPLE a loop arrow.
static void drawChoiceIcon (juce::Graphics& g, int choice, Tool tool, juce::Point<float> c, juce::Colour ink)
{
    const juce::PathStrokeType stroke (2.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (ink);
    switch (choice)
    {
        case AddLayerSheet::voice:
        {
            drawToolIcon (g, Tool::vocoder, c.translated (-3.0f, 0.0f), ink);   // the mic
            for (int i = 1; i <= 2; ++i)   // its sound going out
            {
                juce::Path a;
                const float r = 4.0f + 4.0f * (float) i;
                a.addCentredArc (c.x + 3.0f, c.y - 2.0f, r, r, 0.0f, 0.55f, 2.55f, true);
                g.strokePath (a, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            return;
        }
        case AddLayerSheet::bass:
        {
            juce::Path p;
            for (int i = 0; i <= 24; ++i)
            {
                const float x = c.x - 11.0f + (float) i * 22.0f / 24.0f;
                const float y = c.y + 6.0f * std::sin ((float) i / 24.0f * juce::MathConstants<float>::twoPi);
                i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
            }
            g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            return;
        }
        case AddLayerSheet::vocoder:
        {
            drawToolIcon (g, Tool::vocoder, c, ink);
            for (float side : { -1.0f, 1.0f })
                for (int i = 0; i < 2; ++i)
                {
                    const float x = c.x + side * (9.0f + 4.0f * (float) i), h = i == 0 ? 6.0f : 3.5f;
                    g.drawLine (x, c.y - 2.0f - h, x, c.y - 2.0f + h, 2.0f);
                }
            return;
        }
        case AddLayerSheet::resample:
        {
            juce::Path a;
            a.addCentredArc (c.x, c.y, 9.0f, 9.0f, 0.0f, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath (a, stroke);
            juce::Path head;   // the arrow's head, at the arc's start
            const float ax = c.x + 9.0f * std::sin (0.5f), ay = c.y - 9.0f * std::cos (0.5f);
            head.addTriangle (ax - 1.0f, ay - 5.5f, ax + 5.5f, ay + 1.0f, ax - 3.0f, ay + 3.5f);
            g.fillPath (head);
            g.fillEllipse (c.x - 2.5f, c.y - 2.5f, 5.0f, 5.0f);
            return;
        }
        default:
            drawToolIcon (g, tool, c, ink);
            return;
    }
}

AddLayerSheet::AddLayerSheet()
{
    struct Def { const char* name; const char* line; Tool colourOf; };
    const Def defs[kChoices] = {
        { "My voice", "Sing, rap, hum, or catch any sound around you", Tool::loops },
        { "Sampler", "Grab a sound, chop it, pitch it, add effects", Tool::sampler },
        { "Pads", "Tap sounds: your own recordings or the kits", Tool::pads },
        { "Drums", "Tap boxes to make a beat, or play it in live", Tool::drums },
        { "Bass", "A low line under it all. Every key fits", Tool::drone },
        { "Synth", "Pick a sound and play. Every key fits", Tool::synth },
        { "Vocoder", "Your voice turned into a robot choir", Tool::vocoder },
        { "Resample", "Grab any layer, or the whole mix, as a new sound", Tool::song },
    };
    for (int i = 0; i < kChoices; ++i)
    {
        auto* t = tiles.add (new Tile (defs[i].name, defs[i].line, toolColour (defs[i].colourOf)));
        t->icon = [i, tool = defs[i].colourOf] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawChoiceIcon (g, i, tool, c, ink); };
        t->onClick = [this, i] { pick (i); };
        addAndMakeVisible (t);
    }
    change.setFontHeight (15.0f);
    change.onClick = [this] { if (onRecSetup) onRecSetup(); };
    addAndMakeVisible (change);
    const std::pair<PButton*, Tool> chips[] = { { &chords, Tool::chords }, { &drone, Tool::drone }, { &song, Tool::song }, { &effects, Tool::effects } };
    for (auto [b, t] : chips)
    {
        b->setFontHeight (15.0f);
        b->onClick = [this, t = t] { if (onMoreTool) onMoreTool (t); };
        giveIcon (*b, toolName (t), [t = t] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink)
        {
            juce::Graphics::ScopedSaveState keep (g);
            g.addTransform (juce::AffineTransform::scale (0.8f, 0.8f, c.x, c.y));
            drawToolIcon (g, t, c, toolColour (t));
            juce::ignoreUnused (ink);
        }, 14.0f);
        addAndMakeVisible (b);
    }
    setDragZone (80);
}

AddLayerSheet::~AddLayerSheet() = default;

void AddLayerSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 24.0f);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (fraunces (28.0f));
    g.drawText ("Add a layer", 20, 16, getWidth() - 40, 36, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (14.5f));
    g.drawFittedText ("What do you want to record with? It plays along with your other layers.",
                      juce::Rectangle<int> (20, 52, getWidth() - 40, 36), juce::Justification::topLeft, 2);

    drawWell (g, setupCard, 14.0f);
    auto in = setupCard.reduced (14.0f, 8.0f).withTrimmedRight ((float) change.getWidth() + 8.0f);
    g.setColour (col::lcdAmber);
    g.setFont (spaced (condensedBold (16.0f), 0.06f));
    g.drawFittedText (setupText ? setupText().toUpperCase() : juce::String(), in.removeFromTop (in.getHeight() * 0.55f).toNearestInt(),
                      juce::Justification::bottomLeft, 1, 0.8f);
    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    g.drawFittedText ("Each take loops to fit the idea.", in.toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.1f));
    g.drawText ("MORE TOOLS", 20, chords.getY() - 24, getWidth() - 40, 20, juce::Justification::centredLeft);
}

void AddLayerSheet::resized()
{
    const int w = getWidth(), x0 = 16, iw = w - 32, gap = 10;
    const int tileW = (iw - gap) / 2, tileH = 98;
    for (int i = 0; i < tiles.size(); ++i)   // all the same size: no one choice is the most important
        tiles[i]->setBounds (x0 + (i % 2) * (tileW + gap), 94 + (i / 2) * (tileH + gap), tileW, tileH);
    const int setupY = 94 + 4 * tileH + 3 * gap + 12;
    setupCard = juce::Rectangle<float> ((float) x0, (float) setupY, (float) iw, 58.0f);
    change.setBounds (x0 + iw - 8 - 96, setupY + 7, 96, 44);
    const int chipY = setupY + 58 + 34;
    const int chipW = (iw - 3 * 8) / 4;
    PButton* chips[] = { &chords, &drone, &song, &effects };
    for (int i = 0; i < 4; ++i)
        chips[i]->setBounds (x0 + i * (chipW + 8), chipY, chipW, 44);
}

// Small line icons for the after-take and vibe sheets (the same pen as the tool row's).
static void drawVibeIcon (juce::Graphics& g, int v, juce::Point<float> c, juce::Colour ink)
{
    const juce::PathStrokeType pen (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (ink);
    switch (v)
    {
        case TakeSheet::none:   // a circle with a line through it
            g.drawEllipse (c.x - 7, c.y - 7, 14, 14, 2.0f);
            g.drawLine (c.x - 5, c.y + 5, c.x + 5, c.y - 5, 2.0f);
            break;
        case TakeSheet::space:   // a big star and two small
        {
            auto star = [&g] (float x, float y, float r)
            {
                juce::Path p;
                p.addStar ({ x, y }, 4, r * 0.35f, r);
                g.fillPath (p);
            };
            star (c.x - 1, c.y + 1, 8.0f);
            star (c.x + 7, c.y - 6, 3.5f);
            star (c.x - 8, c.y - 6, 2.5f);
            break;
        }
        case TakeSheet::echo:   // three dots, each fainter
            for (int i = 0; i < 3; ++i)
            {
                g.setColour (ink.withAlpha (1.0f - 0.3f * (float) i));
                const float r = 4.5f - (float) i;
                g.fillEllipse (c.x - 8 + (float) i * 8 - r, c.y - r, r * 2, r * 2);
            }
            break;
        case TakeSheet::radio:   // a little radio: body, dial and aerial
            g.drawRoundedRectangle (c.x - 9, c.y - 3, 18, 11, 2.5f, 2.0f);
            g.drawLine (c.x - 6, c.y - 3, c.x + 4, c.y - 10, 2.0f);
            g.fillEllipse (c.x + 2, c.y - 0.5f, 5, 5);
            g.drawLine (c.x - 6, c.y + 1, c.x - 1, c.y + 1, 1.6f);
            g.drawLine (c.x - 6, c.y + 4, c.x - 1, c.y + 4, 1.6f);
            break;
        case TakeSheet::underwater:   // two waves and a bubble
            for (int row = 0; row < 2; ++row)
            {
                juce::Path p;
                const float y = c.y + 1 + (float) row * 6;
                p.startNewSubPath (c.x - 9, y);
                p.cubicTo (c.x - 6, y - 4, c.x - 3, y + 4, c.x, y);
                p.cubicTo (c.x + 3, y - 4, c.x + 6, y + 4, c.x + 9, y);
                g.strokePath (p, pen);
            }
            g.drawEllipse (c.x + 2, c.y - 10, 6, 6, 1.6f);
            break;
        case TakeSheet::crunch:   // a jagged line
        {
            juce::Path p;
            p.startNewSubPath (c.x - 10, c.y + 2);
            const float pts[][2] = { { -6, -6 }, { -2, 5 }, { 2, -7 }, { 6, 4 }, { 10, -3 } };
            for (auto& q : pts)
                p.lineTo (c.x + q[0], c.y + q[1]);
            g.strokePath (p, juce::PathStrokeType (2.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            break;
        }
        default: break;
    }
}

enum class ActionIcon { again, pad, keep, rec, knobs, board };
static void drawActionIcon (juce::Graphics& g, ActionIcon a, juce::Point<float> c, juce::Colour ink)
{
    const juce::PathStrokeType pen (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (ink);
    switch (a)
    {
        case ActionIcon::again:   // a circular arrow
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, 7.5f, 7.5f, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.3f, true);
            g.strokePath (p, pen);
            const float ax = c.x + 7.5f * std::sin (0.6f), ay = c.y - 7.5f * std::cos (0.6f);
            juce::Path h;
            h.addTriangle (ax - 1, ay - 5, ax + 5, ay + 1, ax - 3, ay + 3);
            g.fillPath (h);
            break;
        }
        case ActionIcon::pad:   // four pads
            for (int i = 0; i < 4; ++i)
                g.fillRoundedRectangle (c.x - 8 + (float) (i % 2) * 9, c.y - 8 + (float) (i / 2) * 9, 7, 7, 1.8f);
            break;
        case ActionIcon::keep:   // a tick
        {
            juce::Path p;
            p.startNewSubPath (c.x - 8, c.y);
            p.lineTo (c.x - 2, c.y + 6);
            p.lineTo (c.x + 9, c.y - 6);
            g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case ActionIcon::rec:
            g.setColour (col::red);
            g.fillEllipse (c.x - 6, c.y - 6, 12, 12);
            break;
        case ActionIcon::knobs:   // two knobs
            for (int i = 0; i < 2; ++i)
            {
                const float x = c.x - 5 + (float) i * 10;
                g.drawEllipse (x - 4.5f, c.y - 4.5f, 9, 9, 2.0f);
                g.drawLine (x, c.y, x + (i == 0 ? -2.5f : 2.5f), c.y - 3.5f, 2.0f);
            }
            break;
        case ActionIcon::board:   // a pedal
            g.drawRoundedRectangle (c.x - 7, c.y - 9, 14, 18, 3.0f, 2.0f);
            g.fillEllipse (c.x - 2.5f, c.y - 6, 5, 5);
            g.drawLine (c.x - 4, c.y + 4, c.x + 4, c.y + 4, 2.0f);
            break;
    }
}

// A button that shows an icon before its words (centred together, or left with the words after it).
static void giveIcon (PButton& b, juce::String text, std::function<void (juce::Graphics&, juce::Point<float>, juce::Colour)> icon, float fontH)
{
    b.glyph = [text, icon, fontH] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        const auto font = spaced (condensed (fontH));
        const auto label = text.toUpperCase();
        const float tw = juce::GlyphArrangement::getStringWidth (font, label), iconW = 26.0f, gap = 6.0f;
        const float total = juce::jmin (r.getWidth() - 8.0f, iconW + gap + tw);
        const float x = r.getCentreX() - total * 0.5f;
        icon (g, { x + iconW * 0.5f, r.getCentreY() }, ink);
        g.setColour (ink);
        g.setFont (font);
        g.drawFittedText (label, juce::Rectangle<float> (x + iconW + gap, r.getY(), total - iconW - gap + 2.0f, r.getHeight()).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.75f);
    };
    b.repaint();
}

//==============================================================================
const char* TakeSheet::vibeName (int v)
{
    static const char* const names[kVibes] = { "None", "Space", "Echo", "Radio", "Underwater", "Crunch" };
    return names[juce::jlimit (0, kVibes - 1, v)];
}

LayerFxState TakeSheet::withVibe (LayerFxState fx, int v)
{
    const float volume = fx.knob[(size_t) fxVolume];
    fx = {};
    fx.knob[(size_t) fxVolume] = volume;
    auto set = [&fx] (int k, float x) { fx.knob[(size_t) k] = x; };
    switch (v)
    {
        case space:      set (fxReverb, 0.5f); break;
        case echo:       set (fxDelay, 0.42f); set (fxReverb, 0.12f); break;
        case radio:      set (fxFilter, 0.78f); set (fxDrive, 0.35f); break;   // thin and a little dirty
        case underwater: set (fxFilter, 0.18f); set (fxReverb, 0.35f); break;   // muffled and far away
        case crunch:     set (fxDrive, 0.62f); break;
        default: break;
    }
    return fx;
}

TakeSheet::TakeSheet()
{
    for (int i = 0; i < kVibes; ++i)
    {
        vibes[i].setText (vibeName (i));
        vibes[i].setFontHeight (17.0f);
        giveIcon (vibes[i], vibeName (i), [i] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawVibeIcon (g, i, c, ink); }, 16.0f);
        vibes[i].onClick = [this, i]
        {
            vibe = i;
            refresh();
            if (onVibe)
                onVibe (i);
        };
        addAndMakeVisible (vibes[i]);
    }
    more.setFontHeight (15.0f);
    more.setLeftAligned (true);
    again.setFontHeight (17.0f);
    makePad.setFontHeight (17.0f);
    keep.setFontHeight (20.0f);
    more.onClick = [this] { if (onMoreEffects) onMoreEffects(); };
    again.onClick = [this] { if (onTryAgain) onTryAgain(); };
    makePad.onClick = [this] { if (onMakePad) onMakePad(); };
    keep.onClick = [this] { if (onKeep) onKeep(); };
    giveIcon (again, "Try again", [] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawActionIcon (g, ActionIcon::again, c, ink); }, 17.0f);
    giveIcon (makePad, "Make it a pad", [] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawActionIcon (g, ActionIcon::pad, c, ink); }, 17.0f);
    for (auto* b : { &more, &again, &makePad, &keep })
        addAndMakeVisible (b);
    setDragZone (80);
}

TakeSheet::~TakeSheet() = default;

void TakeSheet::show (int l, const juce::String& what)
{
    shownLayer = l;
    takeName = what;
    vibe = none;
    keep.setText ("Keep it as layer " + juce::String (l + 1));
    giveIcon (keep, "Keep it as layer " + juce::String (l + 1), [] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawActionIcon (g, ActionIcon::keep, c, ink); }, 20.0f);
    refresh();
}

void TakeSheet::refresh()
{
    for (int i = 0; i < kVibes; ++i)
        vibes[i].setLook (i == vibe ? Look::amber : Look::cream);
    repaint();
}

void TakeSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 24.0f);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (fraunces (28.0f));
    g.drawText ("Nice take", 20, 16, getWidth() - 40, 36, juce::Justification::centredLeft);
    g.setColour (col::lcdAmber);
    g.setFont (spaced (condensedBold (14.0f), 0.1f));
    g.drawText (takeName.toUpperCase() + U8(" \xc2\xb7 LOOPING"), 20, 16, getWidth() - 40, 36, juce::Justification::centredRight);
    g.setColour (col::ink);
    g.setFont (condensedBold (17.0f));
    g.drawText ("Add a vibe", 20, 62, 120, 24, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (14.0f));
    g.drawText ("tap one to hear it", 112, 62, getWidth() - 132, 24, juce::Justification::centredLeft);
}

void TakeSheet::resized()
{
    const int w = getWidth(), x0 = 16, iw = w - 32, gap = 8;
    const int vw = (iw - 2 * gap) / 3;
    for (int i = 0; i < kVibes; ++i)
        vibes[i].setBounds (x0 + (i % 3) * (vw + gap), 94 + (i / 3) * (52 + gap), vw, 52);
    more.setBounds (x0, 94 + 2 * 52 + gap + 6, iw, 40);
    const int half = (iw - 10) / 2;
    again.setBounds (x0, 262, half, 52);
    makePad.setBounds (x0 + half + 10, 262, half, 52);
    keep.setBounds (x0, 326, iw, 66);
}

//==============================================================================
class VibeSheet::Pad : public juce::Component
{
public:
    explicit Pad (VibeSheet& o) : owner (o) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        drawWell (g, r, 18.0f);
        g.setColour (col::wellCard);
        g.drawLine (r.getCentreX(), r.getY() + 8, r.getCentreX(), r.getBottom() - 8, 1.0f);
        g.drawLine (r.getX() + 8, r.getCentreY(), r.getRight() - 8, r.getCentreY(), 1.0f);
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (12.0f), 0.1f));
        auto in = r.reduced (12.0f, 8.0f);
        g.drawText (U8("BRIGHT \xe2\x86\x91"), in, juce::Justification::topLeft);
        g.drawText (U8("DARK \xe2\x86\x93"), in.withTrimmedBottom (18.0f), juce::Justification::bottomLeft);
        g.drawText (U8("LESS \xe2\x86\x90 HOW MUCH \xe2\x86\x92 MORE"), in, juce::Justification::bottomLeft);
        g.setColour (col::lcdAmber);
        g.drawText (juce::String (TakeSheet::vibeName (owner.vibe)).toUpperCase(), in, juce::Justification::topRight);
        const auto c = juce::Point<float> (r.getX() + owner.dot.x * r.getWidth(), r.getBottom() - owner.dot.y * r.getHeight());
        g.setColour ((owner.rec ? col::red : col::amber).withAlpha (0.25f));
        g.fillEllipse (juce::Rectangle<float> (64.0f, 64.0f).withCentre (c));
        g.setColour (owner.rec ? col::red : col::amber);
        g.fillEllipse (juce::Rectangle<float> (34.0f, 34.0f).withCentre (c));
    }
    void mouseDown (const juce::MouseEvent& e) override { if (owner.onTouch) owner.onTouch (true); place (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { place (e.position); }
    void mouseUp (const juce::MouseEvent&) override { if (owner.onTouch) owner.onTouch (false); }
    void place (juce::Point<float> p)
    {
        auto r = getLocalBounds().toFloat();
        owner.moveDot ({ juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (0.0f, 1.0f, (r.getBottom() - p.y) / r.getHeight()) });
    }
    VibeSheet& owner;
};

LayerFxState VibeSheet::fxFor (LayerFxState fx, int v, juce::Point<float> xy)
{
    auto out = TakeSheet::withVibe (fx, v);
    const float amount = juce::jlimit (0.0f, 2.0f, xy.x * 2.0f);   // the middle = the vibe as made
    for (int k : { (int) fxDelay, (int) fxReverb, (int) fxDrive })
        out.knob[(size_t) k] = juce::jlimit (0.0f, 1.0f, out.knob[(size_t) k] * amount);
    if (v == TakeSheet::none)
    {
        // no vibe picked: the pad is still a space + tone pad (a little room, more to the right)
        out.knob[(size_t) fxReverb] = juce::jlimit (0.0f, 1.0f, juce::jmax (0.0f, xy.x - 0.5f) * 1.2f);
    }
    out.knob[(size_t) fxFilter] = juce::jlimit (0.04f, 0.96f, out.knob[(size_t) fxFilter] + (xy.y - 0.5f) * 0.7f);
    return out;
}

VibeSheet::VibeSheet() : pad (std::make_unique<Pad> (*this))
{
    for (int i = 0; i < TakeSheet::kVibes; ++i)
    {
        vibes[i].setText (TakeSheet::vibeName (i));
        vibes[i].setFontHeight (16.0f);
        giveIcon (vibes[i], TakeSheet::vibeName (i), [i] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawVibeIcon (g, i, c, ink); }, 15.0f);
        vibes[i].onClick = [this, i]
        {
            vibe = i;
            refresh();
            if (onChange)
                onChange (vibe, dot);
        };
        addAndMakeVisible (vibes[i]);
    }
    addAndMakeVisible (*pad);
    for (auto* b : { &recButton, &knobs, &board })
    {
        b->setFontHeight (15.0f);
        addAndMakeVisible (b);
    }
    done.setFontHeight (16.0f);
    addAndMakeVisible (done);
    recButton.onClick = [this] { setRecording (! rec); if (onRecordMoves) onRecordMoves (rec); };
    knobs.onClick = [this] { if (onKnobs) onKnobs(); };
    board.onClick = [this] { if (onBoard) onBoard(); };
    giveIcon (knobs, U8("Knobs \xe2\x80\xba"), [] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawActionIcon (g, ActionIcon::knobs, c, ink); }, 15.0f);
    giveIcon (board, U8("Full pedalboard \xe2\x80\xba"), [] (juce::Graphics& g, juce::Point<float> c, juce::Colour ink) { drawActionIcon (g, ActionIcon::board, c, ink); }, 15.0f);
    done.onClick = [this] { if (onDone) onDone(); };
    setDragZone (70);
}

VibeSheet::~VibeSheet() = default;

void VibeSheet::show (int l, const juce::String& w, int v, juce::Point<float> xy, bool recording)
{
    shownLayer = l;
    what = w;
    vibe = juce::jlimit (0, TakeSheet::kVibes - 1, v);
    dot = xy;
    rec = recording;
    refresh();
}

void VibeSheet::setRecording (bool on)
{
    rec = on;
    refresh();
}

void VibeSheet::moveDot (juce::Point<float> xy01)
{
    dot = xy01;
    pad->repaint();
    if (onChange)
        onChange (vibe, dot);
}

void VibeSheet::refresh()
{
    for (int i = 0; i < TakeSheet::kVibes; ++i)
        vibes[i].setLook (i == vibe ? Look::amber : Look::cream);
    recButton.setLook (rec ? Look::red : Look::cream);
    recButton.setText (rec ? U8("\xe2\x97\x8f Recording moves") : U8("\xe2\x97\x8f Record my moves"));
    pad->repaint();
    repaint();
}

void VibeSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 24.0f);
    drawGrabber (g, getWidth());
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (12.0f), 0.12f));
    g.drawText ("EFFECTS ON", 20, 14, getWidth() - 140, 18, juce::Justification::centredLeft);
    g.setColour (col::ink);
    g.setFont (fraunces (26.0f));
    g.drawFittedText (what, juce::Rectangle<int> (20, 30, getWidth() - 140, 32), juce::Justification::centredLeft, 1, 0.8f);
    g.setColour (col::inkDim);
    g.setFont (small (13.5f));
    g.drawText ("Drag the dot. One finger runs the whole sound.", juce::Rectangle<int> (16, pad->getBottom() + 4, getWidth() - 32, 20),
                juce::Justification::centred);
}

void VibeSheet::resized()
{
    const int w = getWidth(), x0 = 16, iw = w - 32, gap = 8;
    done.setBounds (w - 16 - 88, 18, 88, 44);
    const int vw = (iw - 2 * gap) / 3;
    for (int i = 0; i < TakeSheet::kVibes; ++i)
        vibes[i].setBounds (x0 + (i % 3) * (vw + gap), 74 + (i / 3) * (48 + gap), vw, 48);
    pad->setBounds (x0, 74 + 2 * 48 + gap + 12, iw, 230);
    const int half = (iw - 10) / 2;
    const int y = pad->getBottom() + 30;
    recButton.setBounds (x0, y, iw, 50);
    knobs.setBounds (x0, y + 58, half, 46);
    board.setBounds (x0 + half + 10, y + 58, half, 46);
}
}
