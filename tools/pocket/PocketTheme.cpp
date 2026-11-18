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
        case Look::amber: return { juce::Colour (0xfffcca64), juce::Colour (0xffeea02a), juce::Colour (0xffb9741a), col::ink };
        case Look::green: return { juce::Colour (0xff78e6a2), juce::Colour (0xff35bc6b), juce::Colour (0xff1f8348), col::ink };
        case Look::red:   return { juce::Colour (0xfff58b78), juce::Colour (0xffd8452f), juce::Colour (0xff922a1c), juce::Colours::white };
        case Look::dark:  return { juce::Colour (0xff3d362e), juce::Colour (0xff27221c), juce::Colour (0xff100d0a), juce::Colour (0xfff1e7d1) };
        case Look::ghost: return { juce::Colours::transparentBlack, juce::Colours::transparentBlack, juce::Colours::transparentBlack, col::ink };
        case Look::cream: break;
    }
    return { juce::Colour (0xfff8f2e4), juce::Colour (0xffe6dcc4), juce::Colour (0xffb7ab91), col::ink };
}

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
    g.setColour (juce::Colours::white.withAlpha (look == Look::dark ? 0.08f : 0.45f));
    g.drawRoundedRectangle (body.reduced (0.7f), radius, 1.0f);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (col::wellEdge);
    g.fillRoundedRectangle (r, radius);
    g.setColour (col::well);
    g.fillRoundedRectangle (r.reduced (1.5f), radius - 1.0f);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> b, float v, juce::Colour arc, bool active)
{
    const auto c = b.getCentre();
    const float R = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f);

    // the scale arc around the knob
    const float arcR = R - 2.5f;
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0, a0, a1, true);
    g.setColour (col::creamDeep.darker (0.12f));
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
    g.setColour (juce::Colour (0xff201c18));
    g.fillEllipse (c.x - ringR, c.y - ringR, ringR * 2, ringR * 2);
    g.setColour (juce::Colour (0xff4a423a));
    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 48.0f;
        g.drawLine (c.x + std::sin (a) * (ringR - 5.0f), c.y - std::cos (a) * (ringR - 5.0f),
                    c.x + std::sin (a) * (ringR - 0.8f), c.y - std::cos (a) * (ringR - 0.8f), 1.3f);
    }

    // the cream cap
    const float capR = ringR * 0.66f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffaf5e8), c.x - capR * 0.4f, c.y - capR * 0.6f,
                                              juce::Colour (0xffb9ad95), c.x + capR * 0.5f, c.y + capR, true));
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (juce::Colour (0x33000000));
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.0f);

    // a small pointer dot
    const float a = a0 + (a1 - a0) * v;
    g.setColour (col::ink.withAlpha (0.55f));
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
        g.setColour (lampLit ? juce::Colour (0xff3fd16a) : juce::Colour (0xff6c7a63));
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
        g.setColour (col::inkDim);
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
}
