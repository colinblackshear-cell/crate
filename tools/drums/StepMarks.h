#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "DrumEngine.h"

// How a hit looks on a step key, a grid cell or a phone step: a roll as that many small ticks (with
// its count, "x3"), a roll that builds as ticks that rise, a flam as a short grace tick before the
// main one. One drawing for the Mac panel, the Rig's Groove screen and the phone.
namespace crate::drums::steptools
{
inline void drawHitMarks (juce::Graphics& g, juce::Rectangle<float> r, int hits, int shape, juce::Colour ink, bool label, juce::Font labelFont)
{
    if (hits < 1 || (hits == 1 && shape != flam))
        return;
    g.setColour (ink);
    if (hits == 1)
    {
        const float w = juce::jlimit (2.0f, 5.0f, r.getWidth() * 0.08f), cx = r.getCentreX();
        g.fillRect (cx - w * 0.5f + w * 0.9f, r.getCentreY() - r.getHeight() * 0.22f, w, r.getHeight() * 0.44f);
        g.fillRect (cx - w * 2.4f, r.getCentreY() - r.getHeight() * 0.1f, w * 0.8f, r.getHeight() * 0.2f);
        return;
    }
    auto ticks = r;
    juce::Rectangle<float> text;
    if (label && r.getHeight() < 24.0f && r.getWidth() >= 40.0f)
        text = ticks.removeFromRight (r.getWidth() * 0.34f);
    else if (label && r.getHeight() >= 24.0f)
        text = ticks.removeFromBottom (juce::jmin (14.0f, r.getHeight() * 0.36f));
    const float slot = ticks.getWidth() / (float) hits;
    const float tw = juce::jlimit (1.5f, 6.0f, slot * 0.45f);
    for (int i = 0; i < hits; ++i)
    {
        const float f = shape == builds ? 0.3f + 0.7f * (float) i / (float) juce::jmax (1, hits - 1) : 0.6f;
        const float h = ticks.getHeight() * f * 0.8f;
        g.fillRect (ticks.getX() + slot * ((float) i + 0.5f) - tw * 0.5f, ticks.getBottom() - 0.1f * ticks.getHeight() - h, tw, h);
    }
    if (! text.isEmpty())
    {
        g.setFont (labelFont.withHeight (juce::jmin (12.0f, text.getHeight() + 1.0f)));
        g.drawText (juce::String::fromUTF8 ("\xc3\x97") + juce::String (hits), text, juce::Justification::centred);
    }
}
}
