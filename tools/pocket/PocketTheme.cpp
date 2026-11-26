#include "PocketTheme.h"

#include "PocketFonts.h"

namespace pocket
{
static juce::Typeface::Ptr face (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}

static juce::Font make (juce::Typeface::Ptr t, float h)
{
    return juce::Font (juce::FontOptions (t).withHeight (h));
}

juce::Font fraunces (float h)
{
    static auto t = face (PocketFonts::FrauncesExtraBoldItalic_ttf, PocketFonts::FrauncesExtraBoldItalic_ttfSize);
    return make (t, h);
}

juce::Font condensed (float h)
{
    static auto t = face (PocketFonts::BarlowCondensedExtraBold_ttf, PocketFonts::BarlowCondensedExtraBold_ttfSize);
    return make (t, h);
}

juce::Font condensedBold (float h)
{
    static auto t = face (PocketFonts::BarlowCondensedSemiBold_ttf, PocketFonts::BarlowCondensedSemiBold_ttfSize);
    return make (t, h);
}

juce::Font lcdFont (float h)
{
    static auto t = face (PocketFonts::VT323Regular_ttf, PocketFonts::VT323Regular_ttfSize);
    return make (t, h);
}

juce::Font small (float h)
{
    static auto t = face (PocketFonts::BarlowSemiBold_ttf, PocketFonts::BarlowSemiBold_ttfSize);
    return make (t, h);
}

juce::Font spaced (juce::Font f, float k) { return f.withExtraKerningFactor (k); }

struct Palette { juce::Colour top, bottom, edge, ink; };

static Palette paletteFor (Look l)
{
    switch (l)
    {
        case Look::amber: return { juce::Colour (0xfffbd36f), juce::Colour (0xffeeb236), juce::Colour (0xff9a7418), col::onBright };
        case Look::green: return { juce::Colour (0xff55dfc3), juce::Colour (0xff27b89b), juce::Colour (0xff167a66), col::onBright };
        case Look::red:   return { juce::Colour (0xffd0427c), juce::Colour (0xffb22c5e), juce::Colour (0xff7a1a42), juce::Colours::white };
        case Look::blue:  return { juce::Colour (0xffc19cf8), juce::Colour (0xff9f6aee), juce::Colour (0xff5e3aa6), col::onBright };
        case Look::dark:  return { juce::Colour (0xff17121f), juce::Colour (0xff0f0c15), juce::Colour (0xff050408), col::ink };
        case Look::pad:   return { juce::Colour (0xff4b3e66), juce::Colour (0xff3a2f4f), juce::Colour (0xff08060c), col::ink };
        case Look::ghost: return { juce::Colours::transparentBlack, juce::Colours::transparentBlack, juce::Colours::transparentBlack, col::ink };
        case Look::cream: break;
    }
    return { juce::Colour (0xff382e4d), juce::Colour (0xff2e2540), juce::Colour (0xff08060c), col::ink };
}

static bool isBright (Look l) { return l == Look::amber || l == Look::green || l == Look::red || l == Look::blue; }

void drawRaised (juce::Graphics& g, juce::Rectangle<float> r, Look look, bool pressed, float radius)
{
    if (look == Look::ghost)
        return;
    const auto p = paletteFor (look);
    const float lift = pressed ? 1.0f : 3.0f;
    auto body = r.withTrimmedBottom (3.0f).translated (0, pressed ? 2.0f : 0.0f);
    g.setColour (p.edge);
    g.fillRoundedRectangle (body.translated (0, lift), radius);
    g.setGradientFill (juce::ColourGradient (p.top, 0, body.getY(), p.bottom, 0, body.getBottom(), false));
    g.fillRoundedRectangle (body, radius);
    g.setColour (juce::Colours::white.withAlpha (isBright (look) ? 0.40f : look == Look::dark ? 0.05f : 0.10f));
    g.drawRoundedRectangle (body.reduced (0.7f), radius, 1.0f);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (col::wellEdge);                     // the brass rim
    g.fillRoundedRectangle (r, radius);
    g.setColour (col::well);
    g.fillRoundedRectangle (r.reduced (1.75f), radius - 1.75f);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> b, float v, juce::Colour arc, bool active, bool onDark)
{
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);

    // the scale arc around the knob
    const float arcR = R - 2.5f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0, a0, a1, true);
    g.setColour (onDark ? juce::Colour (0xff2a2038) : col::creamDeep);
    g.strokePath (track, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (active)
    {
        juce::Path lit;
        lit.addCentredArc (c.x, c.y, arcR, arcR, 0, a0, a0 + (a1 - a0) * juce::jlimit (0.02f, 1.0f, v), true);
        g.setColour (arc);
        g.strokePath (lit, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // the dark ridged ring
    const float ringR = R - 8.0f;
    g.setColour (juce::Colour (0xff0f0c15));
    g.fillEllipse (c.x - ringR, c.y - ringR, ringR * 2, ringR * 2);
    g.setColour (juce::Colour (0xff4a3d63));
    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
        g.drawLine (c.x + std::sin (a) * (ringR - 5.0f), c.y - std::cos (a) * (ringR - 5.0f),
                    c.x + std::sin (a) * (ringR - 0.8f), c.y - std::cos (a) * (ringR - 0.8f), 1.3f);
    }

    // the brass cap
    const float capR = ringR * 0.66f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffeed99a), c.x - capR * 0.4f, c.y - capR * 0.6f,
                                              juce::Colour (0xff9a7d36), c.x + capR * 0.5f, c.y + capR, true));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (juce::Colour (0x33000000));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);

    // a small pointer dot
    const float a = a0 + (a1 - a0) * v;
    g.setColour (col::onBright.withAlpha (0.75f));
    g.fillEllipse (c.x + std::sin (a) * capR * 0.62f - 2.2f, c.y - std::cos (a) * capR * 0.62f - 2.2f, 4.4f, 4.4f);
}

//==============================================================================
void PButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f, 0.5f + inset);
    auto look = toggled && style == Look::cream ? Look::amber : style;
    drawRaised (g, r, look, down);
    const auto p = paletteFor (look);
    auto inner = r.withTrimmedBottom (3.0f).translated (0, down ? 2.0f : 0.0f);

    if (lamp)
    {
        auto lampBox = inner.withSizeKeepingCentre (9, 9).withX (inner.getX() + 12);
        g.setColour (lampLit ? juce::Colour (0xff2fc6a8) : juce::Colour (0xff5a4d70));
        g.fillEllipse (lampBox);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawEllipse (lampBox, 1.0f);
        inner.removeFromLeft (22);
    }

    if (glyph)
    {
        glyph (g, inner, p.ink);
        return;
    }

    if (detail.isNotEmpty())
    {
        g.setColour (isBright (look) ? col::onBright.withAlpha (0.75f) : col::inkDim);
        g.setFont (small (13.0f));
        g.drawText (detail, inner.withTrimmedRight (14), juce::Justification::centredRight);
    }
    g.setColour (p.ink);
    g.setFont (spaced (condensed (fontHeight)));
    g.drawText (label.toUpperCase(), leftAligned ? inner.withTrimmedLeft (14) : inner, leftAligned ? juce::Justification::centredLeft : juce::Justification::centred);
}

void PButton::mouseUp (const juce::MouseEvent& e)
{
    const bool hit = down && getLocalBounds().contains (e.getPosition());
    down = false;
    repaint();
    if (hit && onClick)
        onClick();
}

//==============================================================================
KnobView::KnobView (juce::String n, juce::Colour a, double lo, double hi, double s, double centre,
                    std::function<juce::String (double)> f, std::function<bool (double)> act)
    : name (std::move (n)), arc (a), min (lo), max (hi), start (s), skewCentre (centre), value (s),
      format (std::move (f)), isActive (std::move (act))
{
}

float KnobView::normalised() const
{
    if (skewCentre > min && skewCentre < max)
        return (float) juce::NormalisableRange<double> (min, max, 0.0, std::log (0.5) / std::log ((skewCentre - min) / (max - min))).convertTo0to1 (value);
    return (float) ((value - min) / (max - min));
}

void KnobView::setNormalised (float n)
{
    n = juce::jlimit (0.0f, 1.0f, n);
    if (skewCentre > min && skewCentre < max)
        value = juce::NormalisableRange<double> (min, max, 0.0, std::log (0.5) / std::log ((skewCentre - min) / (max - min))).convertFrom0to1 (n);
    else
        value = min + (max - min) * n;
}

void KnobView::setValue (double v, bool notify)
{
    value = juce::jlimit (min, max, v);
    repaint();
    if (notify && onChange)
        onChange();
}

void KnobView::mouseDown (const juce::MouseEvent&) { dragStart = normalised(); }

void KnobView::mouseDrag (const juce::MouseEvent& e)
{
    setNormalised ((float) dragStart - (float) e.getDistanceFromDragStartY() / 160.0f);
    repaint();
    if (onChange)
        onChange();
}

void KnobView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto nameArea = r.removeFromTop (20);
    auto valueArea = r.removeFromBottom (22);
    g.setColour (col::ink);
    g.setFont (spaced (condensed (14.0f), 0.1f));
    g.drawText (name, nameArea, juce::Justification::centred);

    auto k = r.withSizeKeepingCentre (66, 66);
    drawKnob (g, k, normalised(), arc, active());

    g.setColour (active() ? col::ink : col::inkDim);
    g.setFont (lcdFont (21.0f));
    g.drawText (format (value), valueArea, juce::Justification::centred);
}

//==============================================================================
juce::String PitchStrip::describe (int st)
{
    if (st == 0)
        return "original";
    return (st > 0 ? "+" : U8("\xe2\x88\x92")) + juce::String (std::abs (st)) + (st > 0 ? U8(" \xc2\xb7 higher") : U8(" \xc2\xb7 lower"));
}

void PitchStrip::setValue (int st, bool notify)
{
    st = juce::jlimit (-12, 12, st);
    if (st == value)
        return;
    value = st;
    repaint();
    if (notify && onChange)
        onChange (value);
}

juce::Rectangle<float> PitchStrip::track() const
{
    auto r = getLocalBounds().toFloat();
    return r.withTrimmedTop (22.0f).reduced (14.0f, 0.0f);
}

void PitchStrip::setFromX (float x)
{
    const auto t = track();
    const float n = juce::jlimit (0.0f, 1.0f, (x - t.getX()) / juce::jmax (1.0f, t.getWidth()));
    setValue (juce::roundToInt (n * 24.0f) - 12, true);
}

void PitchStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (16.0f), 0.08f));
    g.drawText ("PITCH", r.withHeight (20.0f), juce::Justification::centredLeft);
    g.setColour (value == 0 ? col::inkDim : col::amberDark);
    g.setFont (spaced (condensedBold (16.0f), 0.04f));
    g.drawText (describe (value).toUpperCase(), r.withHeight (20.0f), juce::Justification::centredRight);

    const auto t = track();
    const float cy = t.getCentreY(), x0 = t.getX(), w = t.getWidth();
    auto xOf = [&] (int st) { return x0 + w * (float) (st + 12) / 24.0f; };
    auto bar = juce::Rectangle<float> (x0, cy - 5.0f, w, 10.0f);
    drawWell (g, bar.expanded (0.0f, 1.0f), 5.0f);
    const float cx = xOf (0), vx = xOf (value);
    g.setColour (col::amber);
    g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (cx, vx), cy - 4.0f, std::abs (vx - cx), 8.0f), 4.0f);
    for (int st = -12; st <= 12; st += 12)
    {
        g.setColour (col::inkFaint);
        g.fillRect (juce::Rectangle<float> (xOf (st) - 1.0f, cy + 9.0f, 2.0f, 6.0f));
    }
    const float knob = 30.0f;
    drawRaised (g, juce::Rectangle<float> (vx - knob * 0.5f, cy - knob * 0.5f, knob, knob), Look::dark, false, knob * 0.5f);
}
}
