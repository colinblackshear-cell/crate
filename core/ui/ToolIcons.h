#pragma once

#include <juce_graphics/juce_graphics.h>

// The tools' little pictures, the same on the iPhone (Kaleidobox Pocket's tool row) and on the
// Mac (the track rail): tape reels for the loops, a drum, four pads, a sound between its handles
// for the sampler, a wave for the synth (a fat one for the bass), three bars for chords, a mic for
// the vocoder, a jack for line in, two waves for the drone, and two linked knobs for effects.
// Drawn on a 26 x 24 design, scaled to fit `box`.
namespace crate::toolicons
{
enum class Tool { loops, drums, pads, sampler, bass, synth, chords, vocoder, lineIn, drone, effects };

inline void draw (juce::Graphics& g, Tool t, juce::Rectangle<float> box, juce::Colour ink)
{
    const float s = juce::jmin (box.getWidth() / 26.0f, box.getHeight() / 24.0f);
    if (s <= 0.0f)
        return;
    juce::Graphics::ScopedSaveState state (g);
    g.addTransform (juce::AffineTransform::scale (s).translated (box.getCentreX() - 13.0f * s, box.getCentreY() - 12.0f * s));
    g.setColour (ink);
    const float w = 2.3f;
    const juce::PathStrokeType stroke (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::Point<float> c (13.0f, 12.0f);
    juce::Path p;
    switch (t)
    {
        case Tool::loops:
            g.drawEllipse (c.x - 11, c.y - 5, 10, 10, w);
            g.drawEllipse (c.x + 1, c.y - 5, 10, 10, w);
            g.drawLine (c.x - 6, c.y + 5, c.x + 6, c.y + 5, w);
            break;
        case Tool::drums:
            g.drawEllipse (c.x - 11, c.y - 9, 22, 8, w);
            p.startNewSubPath (c.x - 11, c.y - 5);
            p.lineTo (c.x - 11, c.y + 6);
            p.quadraticTo (c.x, c.y + 12, c.x + 11, c.y + 6);
            p.lineTo (c.x + 11, c.y - 5);
            g.strokePath (p, stroke);
            break;
        case Tool::pads:
            for (int k = 0; k < 4; ++k)
                g.fillRoundedRectangle (c.x - 10 + (float) (k % 2) * 11.0f, c.y - 10 + (float) (k / 2) * 11.0f, 9, 9, 2.0f);
            break;
        case Tool::sampler:
        {
            const float bars[5] = { 5.0f, 11.0f, 7.0f, 13.0f, 6.0f };
            for (int k = 0; k < 5; ++k)
                g.fillRoundedRectangle (c.x - 7.0f + (float) k * 3.5f - 1.1f, c.y - bars[k] * 0.5f, 2.2f, bars[k], 1.0f);
            p.startNewSubPath (c.x - 9, c.y - 10);
            p.lineTo (c.x - 12, c.y - 10);
            p.lineTo (c.x - 12, c.y + 10);
            p.lineTo (c.x - 9, c.y + 10);
            p.startNewSubPath (c.x + 9, c.y - 10);
            p.lineTo (c.x + 12, c.y - 10);
            p.lineTo (c.x + 12, c.y + 10);
            p.lineTo (c.x + 9, c.y + 10);
            g.strokePath (p, stroke);
            break;
        }
        case Tool::bass:
            p.startNewSubPath (c.x - 11, c.y);
            p.cubicTo (c.x - 7, c.y + 12, c.x - 3, c.y + 12, c.x, c.y);
            p.cubicTo (c.x + 3, c.y - 12, c.x + 7, c.y - 12, c.x + 11, c.y);
            g.strokePath (p, juce::PathStrokeType (3.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        case Tool::synth:
            p.startNewSubPath (c.x - 12, c.y);
            p.cubicTo (c.x - 8, c.y - 14, c.x - 3, c.y - 14, c.x, c.y);
            p.cubicTo (c.x + 3, c.y + 14, c.x + 8, c.y + 14, c.x + 12, c.y);
            g.strokePath (p, stroke);
            break;
        case Tool::chords:
            for (int k = 0; k < 3; ++k)
                g.fillRoundedRectangle (c.x - 10 + (float) k * 7.5f, c.y - 10, 5, 20, 1.5f);
            break;
        case Tool::vocoder:
            g.fillRoundedRectangle (c.x - 4.5f, c.y - 12, 9, 15, 4.5f);
            p.addCentredArc (c.x, c.y - 1, 8, 8, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f, true);
            p.startNewSubPath (c.x, c.y + 7);
            p.lineTo (c.x, c.y + 12);
            g.strokePath (p, stroke);
            break;
        case Tool::lineIn:
            p.startNewSubPath (c.x - 11, c.y);
            p.lineTo (c.x - 2, c.y);
            p.addRoundedRectangle (c.x - 2, c.y - 5, 7, 10, 1.5f);
            p.startNewSubPath (c.x + 5, c.y - 2.5f);
            p.lineTo (c.x + 11, c.y - 2.5f);
            p.startNewSubPath (c.x + 5, c.y + 2.5f);
            p.lineTo (c.x + 11, c.y + 2.5f);
            g.strokePath (p, stroke);
            break;
        case Tool::drone:
        {
            for (int k = 0; k < 2; ++k)
            {
                juce::Path wave;
                const float y = c.y - 3.0f + (float) k * 6.0f;
                wave.startNewSubPath (c.x - 12, y);
                wave.cubicTo (c.x - 8, y - 6, c.x - 4, y - 6, c.x, y);
                wave.cubicTo (c.x + 4, y + 6, c.x + 8, y + 6, c.x + 12, y);
                g.setColour (ink.withMultipliedAlpha (k == 0 ? 1.0f : 0.6f));
                g.strokePath (wave, juce::PathStrokeType (k == 0 ? w : 1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            break;
        }
        case Tool::effects:
            g.drawEllipse (c.x - 11, c.y - 11, 11, 11, w);
            g.drawEllipse (c.x + 0, c.y + 0, 11, 11, w);
            g.drawLine (c.x - 5.5f, c.y - 5.5f, c.x - 5.5f, c.y - 9.5f, w);
            g.drawLine (c.x + 5.5f, c.y + 5.5f, c.x + 8.5f, c.y + 2.5f, w);
            g.drawLine (c.x - 1.5f, c.y + 1.5f, c.x + 1.5f, c.y - 1.5f, w);
            break;
    }
}
}
