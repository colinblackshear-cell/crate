#pragma once

#include <juce_graphics/juce_graphics.h>

#include <cmath>
#include <vector>

// Kaleidobox's kaleidoscope, as the opening title draws it on the phone and the Mac: a dark disc, a petal for each tool
// in its colour (an outer ring and a smaller inner one turning the other way), blooming in one by one, and a cream
// centre with the red record lamp. One drawing for both, so they match.
namespace crate::kaleidoscope
{
// The tools' colours, in the petals' order: DRUMS, PADS, SAMPLER, BASS, SYNTH, CHORDS, VOCODER, LINE IN, DRONE, EFFECTS, LOOPS.
inline const std::vector<juce::Colour>& petalColours()
{
    static const std::vector<juce::Colour> c { juce::Colour (0xfff0674f), juce::Colour (0xfff2b632), juce::Colour (0xffe08fb0), juce::Colour (0xfff0a24a),
                                               juce::Colour (0xff5fb3c4), juce::Colour (0xffb9a3dc), juce::Colour (0xff82cf6a), juce::Colour (0xffc9b48a),
                                               juce::Colour (0xffe0608e), juce::Colour (0xffe8c27a), juce::Colour (0xfff1e7d1) };
    return c;
}

inline float easeOutBack (float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    return 1.0f + c3 * std::pow (t - 1.0f, 3.0f) + c1 * std::pow (t - 1.0f, 2.0f);
}

// How far a line that starts rising at `at` seconds has come (0..1, over 0.7 s).
inline float rise (float opened, float at) { return juce::jlimit (0.0f, 1.0f, (opened - at) / 0.7f); }

// `angle`: the turn (radians); `bloom`: 0..1 (the mic); `opened`: seconds since the title showed (the petals bloom in
// over the first two). `lamp`: the record lamp is lit.
inline void draw (juce::Graphics& g, juce::Rectangle<float> m, float angle, float bloom, float opened, bool lamp)
{
    const auto c = m.getCentre();
    const float R = m.getWidth() * 0.5f;
    const float glow = 0.5f + 0.5f * std::sin (opened * 1.6f);
    g.setColour (juce::Colour (0xfff5b13d).withAlpha (0.10f * glow * rise (opened, 1.6f)));
    g.fillEllipse (m.expanded (R * 0.12f));
    g.setColour (juce::Colour (0xff2c241d));
    g.fillEllipse (m);
    g.setColour (juce::Colour (0xff1c1814));
    g.fillEllipse (m.reduced (R * 0.05f));

    const auto& colours = petalColours();
    const int n = (int) colours.size();
    for (int layer = 0; layer < 2; ++layer)
    {
        const float len = R * (layer == 0 ? 0.76f : 0.52f) * (1.0f + bloom * 0.12f);
        const float wid = R * (layer == 0 ? 0.24f : 0.17f);
        const float turn = layer == 0 ? angle : -angle * 1.5f + juce::MathConstants<float>::pi / (float) n;
        for (int k = 0; k < n; ++k)
        {
            const float grow = easeOutBack ((opened - 0.1f - 0.12f * (float) k - (layer == 0 ? 0.0f : 0.5f)) / 0.9f);
            if (grow <= 0.0f)
                continue;
            juce::Path petal;
            petal.addRoundedRectangle (-wid * 0.5f, -len, wid, len, wid * 0.5f);
            petal.applyTransform (juce::AffineTransform::scale (grow).rotated (turn + juce::MathConstants<float>::twoPi * (float) k / (float) n).translated (c));
            g.setColour (colours[(size_t) ((k + layer * 4) % n)].withAlpha (layer == 0 ? 0.80f : 0.5f + bloom * 0.3f));
            g.fillPath (petal);
        }
    }
    const float core = R * (0.24f + bloom * 0.05f), rim = R * 0.04f;
    g.setColour (juce::Colour (0xff1c1814));
    g.fillEllipse (c.x - core - rim, c.y - core - rim, (core + rim) * 2, (core + rim) * 2);
    g.setColour (juce::Colour (0xffece3d0));
    g.fillEllipse (c.x - core, c.y - core, core * 2, core * 2);
    if (lamp)
    {
        const float l = juce::jmax (6.0f, R * 0.05f);
        g.setColour (juce::Colour (0xffe8432f));
        g.fillEllipse (c.x - l, c.y - l, l * 2, l * 2);
    }
}
}
