#include "Vintage.h"
#include "RigFonts.h"

namespace vintage
{
namespace
{
bool stageTheme = false;

juce::Typeface::Ptr load (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}

struct Typefaces
{
    juce::Typeface::Ptr semi = load (RigFonts::BarlowCondensedSemiBold_ttf, RigFonts::BarlowCondensedSemiBold_ttfSize);
    juce::Typeface::Ptr bold = load (RigFonts::BarlowCondensedBold_ttf, RigFonts::BarlowCondensedBold_ttfSize);
    juce::Typeface::Ptr extra = load (RigFonts::BarlowCondensedExtraBold_ttf, RigFonts::BarlowCondensedExtraBold_ttfSize);
    juce::Typeface::Ptr text = load (RigFonts::BarlowSemiBold_ttf, RigFonts::BarlowSemiBold_ttfSize);
    juce::Typeface::Ptr screen = load (RigFonts::VT323Regular_ttf, RigFonts::VT323Regular_ttfSize);
    juce::Typeface::Ptr mark = load (RigFonts::FrauncesExtraBoldItalic_ttf, RigFonts::FrauncesExtraBoldItalic_ttfSize);
};

const Typefaces& faces()
{
    static Typefaces t;
    return t;
}

juce::Font make (const juce::Typeface::Ptr& face, float size, float tracking = 0.0f)
{
    auto options = face != nullptr ? juce::FontOptions (face) : juce::FontOptions();
    return juce::Font (options.withPointHeight (size).withKerningFactor (tracking));
}

juce::Colour ink (Lit lit)
{
    switch (lit)
    {
        case Lit::play:    return juce::Colour (0xff062611);
        case Lit::arm:     return juce::Colour (0xff2a0602);
        case Lit::next:
        case Lit::current: return juce::Colour (0xff2a1a05);
        case Lit::mute:    return juce::Colour (0xff06233a);
        default:           return {};
    }
}

std::pair<juce::Colour, juce::Colour> litGradient (Lit lit)
{
    switch (lit)
    {
        case Lit::play:    return { juce::Colour (0xff86f2a9), juce::Colour (0xff37c26a) };
        case Lit::arm:     return { juce::Colour (0xffff7a62), juce::Colour (0xffd8341f) };
        case Lit::next:
        case Lit::current: return { juce::Colour (0xffffd884), juce::Colour (0xfff2a93b) };
        case Lit::mute:    return { juce::Colour (0xff5fb3e8), juce::Colour (0xff2f86c4) };
        default:           return {};
    }
}
}

//==============================================================================
const Palette& studio()
{
    static const Palette p { juce::Colour (0xffe6dcc5), juce::Colour (0xffcfc3a8), juce::Colour (0xff271f18), juce::Colour (0xff54463a),
                             juce::Colours::white.withAlpha (0.6f), juce::Colour (0xff1e1a16), juce::Colour (0xfff1e7d1),
                             juce::Colour (0xffbdaf97), juce::Colour (0xfffaf4e6), juce::Colour (0xffd5c9af), juce::Colour (0xff271f18),
                             juce::Colour (0xff6e3d22), juce::Colour (0xffa2643b), juce::Colour (0xff271f18).withAlpha (0.22f),
                             juce::Colour (0xff271f18).withAlpha (0.22f) };
    return p;
}

const Palette& stage()
{
    static const Palette p { juce::Colour (0xff191614), juce::Colour (0xff0e0c0b), juce::Colour (0xfff1e7d1), juce::Colour (0xffbdaf97),
                             juce::Colours::black.withAlpha (0.85f), juce::Colour (0xff090807), juce::Colour (0xfff1e7d1),
                             juce::Colour (0xffbdaf97), juce::Colour (0xff3b3531), juce::Colour (0xff25211e), juce::Colour (0xfff1e7d1),
                             juce::Colour (0xff24150d), juce::Colour (0xff3a2416), juce::Colour (0xfff1e7d1).withAlpha (0.16f),
                             juce::Colour (0xfff1e7d1).withAlpha (0.16f) };
    return p;
}

const Palette& pal() { return stageTheme ? stage() : studio(); }
bool isStage() { return stageTheme; }
void setStage (bool s) { stageTheme = s; }

//==============================================================================
juce::Font cond (float size, Weight w, float tracking)
{
    const auto& f = faces();
    return make (w == Weight::semi ? f.semi : w == Weight::bold ? f.bold : f.extra, size, tracking);
}

juce::Font barlow (float size) { return make (faces().text, size); }
juce::Font lcd (float size) { return make (faces().screen, size); }
juce::Font wordmark (float size) { return make (faces().mark, size, -0.01f); }

void engraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> r, juce::Justification j, float size, bool inWell,
               juce::Colour colour)
{
    g.setFont (cond (size, Weight::bold, 0.14f));
    const auto& p = pal();
    if (! inWell)
    {
        g.setColour (p.engrave);
        g.drawText (text.toUpperCase(), r.translated (0, 1), j, true);
    }
    g.setColour (! colour.isTransparent() ? colour : inWell ? p.wellInk2 : p.ink2);
    g.drawText (text.toUpperCase(), r, j, true);
}

juce::Colour litColour (Lit lit)
{
    switch (lit)
    {
        case Lit::play:    return colours::play;
        case Lit::arm:     return colours::arm;
        case Lit::next:    return colours::next;
        case Lit::current: return colours::selected;
        case Lit::mute:    return colours::mute;
        default:           return {};
    }
}

void glow (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c, float size, float strength)
{
    constexpr int rings = 6;
    for (int i = rings; i >= 1; --i)
    {
        const float grow = size * (float) i / rings;
        g.setColour (c.withAlpha (strength * 0.22f * (1.0f - (float) (i - 1) / rings)));
        g.fillRoundedRectangle (r.expanded (grow), radius + grow);
    }
}

//==============================================================================
void drawFace (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (pal().face);
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (int y = r.getY(); y < r.getBottom(); y += 3)
        g.fillRect (r.getX(), y, r.getWidth(), 1);
}

void drawCheek (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto& p = pal();
    const auto f = r.toFloat();
    juce::ColourGradient wood (p.wood1, f.getX(), 0, p.wood1, f.getRight(), 0, false);
    wood.addColour (0.45, p.wood2);
    g.setGradientFill (wood);
    g.fillRect (r);
    // The grain.
    for (int x = r.getX(); x < r.getRight(); x += 15)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRect (x, r.getY(), 1, r.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (x + 4, r.getY(), 1, r.getHeight());
        g.setColour (juce::Colours::black.withAlpha (0.08f));
        g.fillRect (x + 9, r.getY(), 2, r.getHeight());
    }
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRect (r.getRight() - 2, r.getY(), 2, r.getHeight());
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (r.getX(), r.getY(), 2, r.getHeight());
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    const auto& p = pal();
    g.setColour (p.engrave);
    g.fillRoundedRectangle (r.translated (0, 1), radius);
    g.setColour (p.well);
    g.fillRoundedRectangle (r, radius);
    // Inner shadow along the top.
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.7f), 0, r.getY(), juce::Colours::transparentBlack, 0, r.getY() + 9, false);
    g.setGradientFill (shade);
    g.fillRect (r.withHeight (9));
}

void drawButton (juce::Graphics& g, juce::Rectangle<float> r, Lit lit, bool down, bool over, bool inWell, float radius)
{
    const auto& p = pal();
    const float drop = down ? 1.0f : 3.0f;
    const auto face = r.withTrimmedBottom (3.0f).translated (0, down ? 2.0f : 0.0f);

    if (lit != Lit::none)
        glow (g, face, radius, litColour (lit), 12.0f, 0.55f);
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillRoundedRectangle (face.translated (0, drop + 2.0f), radius + 1.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillRoundedRectangle (face.translated (0, drop), radius);

    juce::Colour top, bottom;
    if (lit != Lit::none)
        std::tie (top, bottom) = litGradient (lit);
    else if (inWell)
        top = juce::Colour (0xff4a433d), bottom = juce::Colour (0xff302b27);
    else
        top = p.btn1, bottom = p.btn2;
    if (over && ! down)
        top = top.brighter (0.06f), bottom = bottom.brighter (0.06f);
    g.setGradientFill (juce::ColourGradient (top, 0, face.getY(), bottom, 0, face.getBottom(), false));
    g.fillRoundedRectangle (face, radius);
    if (lit == Lit::next || lit == Lit::current)
    {
        // Pressed in: a little inner shade.
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.drawRoundedRectangle (face.reduced (1.0f), radius, 2.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (inWell && lit == Lit::none ? 0.12f : 0.35f));
    g.drawLine (face.getX() + radius, face.getY() + 1.0f, face.getRight() - radius, face.getY() + 1.0f, 1.0f);
}

void drawLamp (juce::Graphics& g, juce::Point<float> c, Lit lit, float d)
{
    const auto r = juce::Rectangle<float> (d, d).withCentre (c);
    if (lit == Lit::none)
    {
        g.setColour (juce::Colour (0xff4a4038));
        g.fillEllipse (r);
        g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0, r.getY(), juce::Colours::transparentBlack, 0,
                                                 r.getY() + d * 0.45f, false));
        g.fillEllipse (r);
        return;
    }
    juce::Colour light, mid, edge;
    switch (lit)
    {
        case Lit::play: light = juce::Colour (0xffe9ffef), mid = juce::Colour (0xff57e389), edge = juce::Colour (0xff2baa5c); break;
        case Lit::arm:  light = juce::Colour (0xffffe1da), mid = juce::Colour (0xfff0503a), edge = juce::Colour (0xffb42612); break;
        case Lit::mute: light = juce::Colour (0xffe3f3ff), mid = juce::Colour (0xff5fb3e8), edge = juce::Colour (0xff2a77ae); break;
        default:        light = juce::Colour (0xfffff2d6), mid = juce::Colour (0xffffb23e), edge = juce::Colour (0xffc97a0e); break;
    }
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (mid.withAlpha (0.12f));
        g.fillEllipse (r.expanded (d * 0.16f * i));
    }
    const auto hot = r.getRelativePoint (0.4f, 0.35f);
    juce::ColourGradient grad (light, hot.x, hot.y, edge, hot.x + d * 0.62f, hot.y, true);
    grad.addColour (0.45, mid);
    g.setGradientFill (grad);
    g.fillEllipse (r);
}

void drawLcd (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (pal().engrave);
    g.fillRoundedRectangle (r.translated (0, 1), radius);
    g.setColour (colours::lcdBack);
    g.fillRoundedRectangle (r, radius);
    juce::Graphics::ScopedSaveState s (g);
    juce::Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.85f), 0, r.getY(), juce::Colours::transparentBlack, 0,
                                             r.getY() + 8, false));
    g.fillRect (r.withHeight (8));
}

void lcdText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, float size, juce::Justification j, juce::Colour c)
{
    g.setFont (lcd (size));
    g.setColour (c.withAlpha (0.22f));
    for (auto off : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
        g.drawText (text, r.translated (off.x, off.y), j, false);
    g.setColour (c);
    g.drawText (text, r, j, false);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float v, juce::Colour ringColour)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto r = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
    const auto c = r.getCentre();
    const float outer = size * 0.5f, inner = size * 0.305f;
    const float start = juce::MathConstants<float>::pi * 1.25f, sweep = juce::MathConstants<float>::pi * 1.5f;

    auto ring = [&] (float from, float to, juce::Colour colour)
    {
        if (to <= from)
            return;
        juce::Path p;
        p.addCentredArc (c.x, c.y, outer, outer, 0.0f, from, to, true);
        p.addCentredArc (c.x, c.y, inner, inner, 0.0f, to, from, false);
        p.closeSubPath();
        g.setColour (colour);
        g.fillPath (p);
    };
    ring (start, start + sweep, pal().ringOff);
    ring (start, start + sweep * v, ringColour);

    // The skirt, with its grip.
    const auto skirt = r.reduced (size * 0.1f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (skirt.translated (0, size * 0.06f));
    const auto lightAt = skirt.getRelativePoint (0.5f, 0.38f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff45403a), lightAt.x, lightAt.y, juce::Colour (0xff141211), lightAt.x,
                                             lightAt.y + skirt.getHeight() * 0.62f, true));
    g.fillEllipse (skirt);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    const float sr = skirt.getWidth() * 0.5f;
    for (int a = 0; a < 360; a += 12)
    {
        const float rad = juce::degreesToRadians ((float) a);
        g.drawLine (c.x + std::sin (rad) * sr * 0.72f, c.y - std::cos (rad) * sr * 0.72f, c.x + std::sin (rad) * sr * 0.98f,
                    c.y - std::cos (rad) * sr * 0.98f, juce::jmax (1.0f, size * 0.025f));
    }

    // The cap.
    const auto cap = r.reduced (size * 0.23f);
    const auto hot = cap.getRelativePoint (0.38f, 0.3f);
    juce::ColourGradient capFill (juce::Colour (0xfffffbf2), hot.x, hot.y, juce::Colour (0xff8c816c), hot.x + cap.getWidth() * 0.75f, hot.y, true);
    capFill.addColour (0.42, juce::Colour (0xffdcd1ba));
    g.setGradientFill (capFill);
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (cap, 1.0f);

    // The pointer.
    const auto rot = r.reduced (size * 0.1f);
    const float w = juce::jmax (3.0f, size * 0.068f), h = rot.getHeight() * 0.36f;
    juce::Path ptr;
    ptr.addRoundedRectangle (c.x - w * 0.5f, rot.getY() + rot.getHeight() * 0.03f, w, h, w * 0.5f);
    ptr.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (-135.0f + 270.0f * v), c.x, c.y));
    g.setColour (juce::Colour (0xff16120f));
    g.strokePath (ptr, juce::PathStrokeType (3.0f));
    g.setColour (juce::Colour (0xfffff8e8));
    g.fillPath (ptr);
}

void drawVu (juce::Graphics& g, juce::Rectangle<float> r, float level, const juce::String& label)
{
    // Drawn on the design's 132 x 78 face, scaled to fit.
    const float sx = r.getWidth() / 132.0f, sy = r.getHeight() / 78.0f;
    auto at = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * sx, r.getY() + y * sy); };
    const juce::Colour ink (0xff2a221b), red (0xffc8321f);

    g.setColour (juce::Colour (0xfff6dfa0));
    g.fillRoundedRectangle (r.reduced (1.0f), 7.0f * sx);
    g.setColour (ink);
    g.drawRoundedRectangle (r.reduced (1.0f), 7.0f * sx, 2.0f);

    const auto pivot = at (66, 74);
    const float radius = 50.0f * sy;
    auto arc = [&] (float fromDeg, float toDeg, juce::Colour c, float thickness)
    {
        juce::Path p;
        p.addCentredArc (pivot.x, pivot.y, 50.0f * sx, radius, 0.0f, juce::degreesToRadians (fromDeg), juce::degreesToRadians (toDeg), true);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thickness));
    };
    arc (-50.0f, 50.0f, ink, 2.0f);
    arc (28.0f, 50.0f, red, 6.0f * sy);

    g.setFont (cond (11.0f * sy, Weight::bold));
    g.setColour (ink);
    g.drawText ("-20", juce::Rectangle<float> (at (12, 22), at (36, 36)), juce::Justification::centredLeft);
    g.drawText ("0", juce::Rectangle<float> (at (76, 9), at (90, 22)), juce::Justification::centredLeft);
    g.setColour (red);
    g.drawText ("+3", juce::Rectangle<float> (at (104, 22), at (126, 36)), juce::Justification::centredLeft);
    g.setColour (ink);
    g.setFont (cond (13.0f * sy, Weight::extra, 0.08f));
    g.drawText (label, juce::Rectangle<float> (at (20, 50), at (112, 66)), juce::Justification::centred);

    const float angle = juce::degreesToRadians (-50.0f + 100.0f * juce::jlimit (0.0f, 1.04f, level));
    const float len = 52.0f * sy;
    g.setColour (juce::Colour (0xff16110d));
    g.drawLine (pivot.x, pivot.y, pivot.x + std::sin (angle) * len, pivot.y - std::cos (angle) * len, 2.4f);
    g.fillEllipse (juce::Rectangle<float> (12.0f * sy, 12.0f * sy).withCentre (pivot));
}

void drawStep (juce::Graphics& g, juce::Rectangle<float> r, bool on, bool playhead, bool alt, juce::Colour colour, float alpha)
{
    const float radius = 8.0f;
    const auto face = r.withTrimmedBottom (3.0f);
    if (on && alpha > 0.5f)
        glow (g, face, radius, colour, 10.0f, 0.5f * alpha);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (face.translated (0, 3.0f), radius);
    if (on)
    {
        g.setColour (colour.withMultipliedAlpha (alpha).overlaidWith (juce::Colour (0xff36312c).withAlpha (1.0f - alpha)));
        g.fillRoundedRectangle (face, radius);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.38f * alpha), 0, face.getY(), juce::Colours::transparentWhite, 0,
                                                 face.getY() + face.getHeight() * 0.55f, false));
        g.fillRoundedRectangle (face, radius);
    }
    else
    {
        juce::Colour top (0xff4e4740), bottom (0xff36312c);
        if (playhead)
            top = juce::Colour (0xff6e665d), bottom = juce::Colour (0xff514941);
        else if (alt)
            top = juce::Colour (0xff3f3934), bottom = juce::Colour (0xff2b2723);
        g.setGradientFill (juce::ColourGradient (top, 0, face.getY(), bottom, 0, face.getBottom(), false));
        g.fillRoundedRectangle (face, radius);
        g.setColour (juce::Colours::white.withAlpha (0.11f));
        g.drawLine (face.getX() + radius, face.getY() + 1.0f, face.getRight() - radius, face.getY() + 1.0f, 1.0f);
    }
    if (playhead)
    {
        g.setColour (juce::Colour (0xfffff3d6));
        g.drawRoundedRectangle (face.expanded (3.5f), radius + 3.0f, 3.0f);
    }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff1e1a16));
    setColour (juce::TooltipWindow::textColourId, juce::Colour (0xfff1e7d1));
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1e1a16));
    setColour (juce::PopupMenu::textColourId, juce::Colour (0xfff1e7d1));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::selected);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff2a1a05));
    setColour (juce::PopupMenu::headerTextColourId, juce::Colour (0xffbdaf97));
}

void setLit (juce::Button& b, Lit lit) { b.getProperties().set ("lit", (int) lit); }
void setInWell (juce::Button& b, bool inWell) { b.getProperties().set ("well", inWell); }

static Lit buttonLit (juce::Button& b)
{
    if (! b.getToggleState())
        return Lit::none;
    const auto v = b.getProperties()["lit"];
    return v.isVoid() ? Lit::current : (Lit) (int) v;
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    drawButton (g, b.getLocalBounds().toFloat(), buttonLit (b), down, over && b.isEnabled(), (bool) b.getProperties()["well"]);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down)
{
    const auto lit = buttonLit (b);
    const bool inWell = (bool) b.getProperties()["well"];
    auto r = b.getLocalBounds().withTrimmedBottom (3).translated (0, down ? 2 : 0);
    juce::Colour c = lit != Lit::none ? ink (lit) : inWell ? juce::Colour (0xfff1e7d1) : pal().btnInk;
    if (! b.isEnabled())
        c = c.withMultipliedAlpha (0.45f);
    g.setColour (c);
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.drawFittedText (b.getButtonText().toUpperCase(), r.reduced (6, 2), juce::Justification::centred, 2, 0.8f);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int height)
{
    const auto size = b.getProperties()["fontSize"];
    return cond (size.isVoid() ? juce::jlimit (12.0f, 21.0f, height * 0.4f) : (float) size, Weight::extra, 0.08f);
}

// A fader (as on the Mixer): a slot with a scale and a cream cap with the setting's colour across it
// (the slider's trackColourId).
void LookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                    juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearVertical)
        return juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, minPos, maxPos, style, s);
    const auto& p = pal();
    const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
    const float capH = juce::jlimit (16.0f, 30.0f, area.getHeight() * 0.11f);
    const float top = area.getY() + capH * 0.5f, bottom = area.getBottom() - capH * 0.5f;

    // The scale: 0 dB (where it starts), and every 6 dB down.
    g.setFont (cond (10.0f, Weight::bold));
    for (float level : { 1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f })
    {
        const float ty = bottom - (bottom - top) * level;
        g.setColour (p.wellInk2.withAlpha (level == 0.8f ? 0.9f : 0.45f));
        g.fillRect (area.getX(), ty - 0.5f, area.getWidth(), level == 0.8f ? 1.5f : 1.0f);
    }

    // The slot.
    const auto slot = juce::Rectangle<float> (area.getCentreX() - 3.0f, top, 6.0f, bottom - top);
    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillRoundedRectangle (slot, 3.0f);
    const auto ring = s.findColour (juce::Slider::trackColourId);
    g.setColour (ring.withAlpha (0.55f));
    g.fillRoundedRectangle (slot.withTop (pos), 3.0f);

    // The cap.
    const float capW = juce::jmin (area.getWidth() - 2.0f, 44.0f);
    auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ area.getCentreX(), pos });
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (cap.translated (0, 2.0f), 4.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4ead6), 0, cap.getY(), juce::Colour (0xffc9bda6), 0, cap.getBottom(), false));
    g.fillRoundedRectangle (cap, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (float dy : { -0.3f, 0.3f })
        g.fillRect (cap.getX() + 4.0f, cap.getCentreY() + cap.getHeight() * dy, cap.getWidth() - 8.0f, 1.0f);
    g.setColour (ring);
    g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 1.5f, cap.getWidth() - 6.0f, 3.0f);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s)
{
    drawKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, s.findColour (juce::Slider::rotarySliderFillColourId));
}

juce::Font LookAndFeel::getLabelFont (juce::Label& l) { return cond (juce::jmax (12.0f, l.getFont().getHeight()), Weight::bold); }
juce::Font LookAndFeel::getPopupMenuFont() { return cond (17.0f, Weight::bold, 0.04f); }

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (juce::Colour (0xff1e1a16));
    g.setColour (juce::Colour (0xfff1e7d1).withAlpha (0.16f));
    g.drawRect (0, 0, width, height);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& n, juce::Colour ring)
    : name (n)
{
    setUp (ring);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

Knob::Knob (const juce::String& n, juce::Colour ring)
    : name (n)
{
    setUp (ring);
}

void Knob::setUp (juce::Colour ring)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, ring);
    slider.setMouseDragSensitivity (220);
    slider.onValueChange = [this] { repaint (valueArea); };
    addAndMakeVisible (slider);
}

void Knob::paint (juce::Graphics& g)
{
    engraved (g, name, nameArea, sideways ? juce::Justification::bottomLeft : juce::Justification::centred, nameSize, inWell,
              inWell ? pal().wellInk : pal().ink);
    auto box = valueArea.toFloat();
    drawLcd (g, box);
    auto text = slider.getTextFromValue (slider.getValue());
    lcdText (g, text, box, juce::jmin (24.0f, box.getHeight() * 0.9f));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    if (sideways)
    {
        const int side = juce::jmin (r.getHeight(), r.getWidth() / 2);
        slider.setBounds (r.removeFromLeft (side).withSizeKeepingCentre (side, side));
        r.removeFromLeft (6);
        auto text = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 44));
        nameArea = text.removeFromTop (text.getHeight() / 2 - 1);
        text.removeFromTop (2);
        valueArea = text.withWidth (juce::jmin (text.getWidth(), 104));
        return;
    }
    // A short knob squeezes its name and value a little, so the knob itself stays big enough to grab.
    const bool small = r.getHeight() < 100;
    nameArea = r.removeFromTop (small ? 15 : 20);
    const int valueHeight = small ? 18 : juce::jlimit (20, 28, r.getHeight() / 4);
    valueArea = r.removeFromBottom (valueHeight).withSizeKeepingCentre (juce::jmin (r.getWidth(), 104), valueHeight);
    r.removeFromBottom (small ? 3 : 6);
    r.removeFromTop (small ? 2 : 4);
    slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
}

// A value as a person reads it: "35%", "1.2 kHz", "40 ms", "+7 st", a choice's name.
juce::String readable (juce::RangedAudioParameter* p, double v)
{
    if (p == nullptr)
        return juce::String (v, 2);
    if (dynamic_cast<juce::AudioParameterFloat*> (p) == nullptr)
        return p->getText (p->convertTo0to1 ((float) v), 32);
    const auto range = p->getNormalisableRange();
    juce::String unit = p->getLabel();
    if (unit.isEmpty() && range.start >= 0.0f && range.end <= 1.0f)
        return juce::String (juce::roundToInt (v * 100.0)) + "%";
    if (unit == "cents")
        unit = "ct";
    if (unit == "Hz" && std::abs (v) >= 1000.0)
        return juce::String (v / 1000.0, std::abs (v) >= 10000.0 ? 0 : 1) + " kHz";
    if (unit == "ms" && v >= 1000.0)
        return juce::String (v / 1000.0, 2) + " s";
    const int decimals = std::abs (v) >= 100.0 ? 0 : std::abs (v) >= 10.0 ? (unit == "ms" ? 0 : 1) : (unit == "st" || unit == "oct" ? 1 : 2);
    const bool signedValue = range.start < 0.0f;
    return (signedValue && v > 0.004 ? "+" : "") + juce::String (v, decimals) + (unit.isNotEmpty() ? " " + unit : juce::String());
}
}
