#include <juce_core/juce_core.h>

#include "core/Warp.h"

namespace
{
constexpr double kRate = 44100.0;
constexpr int kBeat = 22050;   // 120 bpm

// A short burst of a tone at each source beat in `at` (beats), in a loop `beats` long.
juce::AudioBuffer<float> bursts (const std::vector<double>& at, double beats, float freq = 330.0f)
{
    juce::AudioBuffer<float> b (2, (int) (beats * kBeat));
    b.clear();
    for (double a : at)
    {
        const int start = (int) (a * kBeat);
        for (int i = 0; i < 2200 && start + i < b.getNumSamples(); ++i)
        {
            const float env = std::exp (-(float) i / 700.0f);
            const float v = 0.6f * env * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / (float) kRate);
            b.setSample (0, start + i, v);
            b.setSample (1, start + i, v);
        }
    }
    return b;
}

// Where the sound first gets loud after `from` (samples), or -1.
int onsetAfter (const juce::AudioBuffer<float>& b, int from, int to, float level = 0.08f)
{
    for (int i = juce::jmax (0, from); i < juce::jmin (to, b.getNumSamples()); ++i)
        if (std::abs (b.getSample (0, i)) > level)
            return i;
    return -1;
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i)))
                return false;
    return true;
}

float rms (const juce::AudioBuffer<float>& b)
{
    return b.getRMSLevel (0, 0, b.getNumSamples());
}
}

struct WarpTests : juce::UnitTest
{
    WarpTests() : juce::UnitTest ("Warp", "Core") {}

    void runTest() override
    {
        using namespace crate::Warp;

        beginTest ("markers map the warped loop onto the recording and back");
        {
            Plan p;
            p.sourceBeats = p.targetBeats = 4;
            p.markers = { { 1.2, 1.0, true } };
            expectWithinAbsoluteError (p.sourceAt (1.0), 1.2, 1.0e-9);
            expectWithinAbsoluteError (p.sourceAt (0.5), 0.6, 1.0e-9);
            expectWithinAbsoluteError (p.targetAt (2.6), 2.5, 1.0e-9);
            expectWithinAbsoluteError (p.sourceAt (p.targetAt (3.3)), 3.3, 1.0e-9);
            expect (! p.isStraight());
            p.markers = { { 2.0, 2.0, false } };
            expect (p.isStraight());
        }

        beginTest ("tidy keeps time running forwards and hand-placed markers win");
        {
            Plan p;
            p.sourceBeats = p.targetBeats = 4;
            p.markers = { { 2.0, 2.5, false }, { 1.0, 3.0, true }, { 3.0, 3.5, false }, { 0.0, 0.0, false }, { 3.99999, 3.0, false } };
            tidy (p);
            expectEquals ((int) p.markers.size(), 2);
            expect (p.markers[0].manual && p.markers[0].source == 1.0);
            expectEquals (p.markers[1].source, 3.0);
        }

        beginTest ("snap pulls hits onto the grid by the strength, and hand-placed markers stay");
        {
            Plan p;
            p.sourceBeats = p.targetBeats = 4;
            const std::vector<double> hits { 0.9, 2.1, 3.05 };
            snap (p, hits, 1.0, 1.0f);
            expectEquals ((int) p.markers.size(), 3);
            expectWithinAbsoluteError (p.markers[0].target, 1.0, 1.0e-9);
            expectWithinAbsoluteError (p.markers[1].target, 2.0, 1.0e-9);
            expectWithinAbsoluteError (p.markers[2].target, 3.0, 1.0e-9);

            snap (p, hits, 1.0, 0.5f);
            expectWithinAbsoluteError (p.markers[0].target, 0.95, 1.0e-9);
            expectWithinAbsoluteError (p.markers[1].target, 2.05, 1.0e-9);

            moveMarker (p, 1, 2.25);
            expect (p.markers[1].manual);
            snap (p, hits, 0.5, 1.0f);
            expectWithinAbsoluteError (p.markers[1].target, 2.25, 1.0e-9);   // the hand-placed one stays
            expectWithinAbsoluteError (p.markers[0].target, 1.0, 1.0e-9);

            snap (p, hits, 1.0, 0.0f);   // strength 0: only hand-placed markers left
            expectEquals ((int) p.markers.size(), 1);

            expectWithinAbsoluteError (moveMarker (p, 0, 9.0), 4.0 - 1.0 / 64.0, 1.0e-9);   // can't pass the end
        }

        beginTest ("FIT TO n BARS keeps the markers in their place in the loop");
        {
            Plan p;
            p.sourceBeats = p.targetBeats = 4;
            p.markers = { { 1.1, 1.0, true } };
            setLength (p, 8);
            expectEquals (p.targetBeats, 8.0);
            expectWithinAbsoluteError (p.markers[0].target, 2.0, 1.0e-9);
        }

        beginTest ("the plan saves and loads");
        {
            Plan p;
            p.sourceBeats = 4;
            p.targetBeats = 8;
            p.settings = { Style::texture, -2.5f, 1.0f };
            p.markers = { { 1.1, 2.0, true }, { 2.9, 6.0, false } };
            const auto q = Plan::fromVar (juce::JSON::parse (juce::JSON::toString (p.toVar())));
            expectEquals (q.targetBeats, 8.0);
            expect (q.settings == p.settings);
            expectEquals ((int) q.markers.size(), 2);
            expect (q.markers[0].manual && ! q.markers[1].manual);
            expectWithinAbsoluteError (q.markers[1].source, 2.9, 1.0e-9);
        }

        beginTest ("hits are found where they are played");
        {
            const auto src = bursts ({ 0.0, 1.13, 2.07, 2.94 }, 4.0);
            const auto hits = findHits (src, 4.0, 0.6f);
            expectEquals ((int) hits.size(), 3);   // the one at the very top is the loop's start
            if (hits.size() == 3)
            {
                expectWithinAbsoluteError (hits[0], 1.13, 0.03);
                expectWithinAbsoluteError (hits[1], 2.07, 0.03);
                expectWithinAbsoluteError (hits[2], 2.94, 0.03);
            }
        }

        beginTest ("every style lands the hits on the beat and keeps the length");
        {
            const std::vector<double> played { 0.0, 1.13, 2.07, 2.94 };
            const auto src = bursts (played, 4.0);
            Plan p;
            p.sourceBeats = p.targetBeats = 4;
            snap (p, { 1.13, 2.07, 2.94 }, 1.0, 1.0f);
            for (auto style : { Style::voice, Style::beats, Style::texture, Style::tape })
            {
                p.settings.style = style;
                const auto out = render (src, p, 4 * kBeat, kRate);
                expectEquals (out.getNumSamples(), 4 * kBeat);
                expect (allFinite (out), styleName (style));
                expect (rms (out) > 0.01f, styleName (style) + " has sound");
                // Texture smears on purpose (grains wander ~12 ms and fade in over 45 ms).
                const int slack = style == Style::beats ? 8 : style == Style::texture ? (int) (0.07 * kRate) : (int) (0.012 * kRate);
                for (int k = 1; k <= 3; ++k)
                {
                    const int at = onsetAfter (out, k * kBeat - kBeat / 3, k * kBeat + kBeat / 3, style == Style::texture ? 0.03f : 0.08f);
                    expect (at >= 0 && std::abs (at - k * kBeat) <= slack,
                            styleName (style) + " hit " + juce::String (k) + " at " + juce::String (at - k * kBeat) + " samples from the beat");
                }
            }
        }

        beginTest ("stretching to twice the length keeps the pitch (voice) and halves it (tape)");
        {
            juce::AudioBuffer<float> tone (1, 2 * kBeat);
            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) kRate));
            Plan p;
            p.sourceBeats = p.targetBeats = 2;
            setLength (p, 4);
            const auto crossingsHz = [] (const juce::AudioBuffer<float>& b)
            {
                int n = 0;
                const int from = b.getNumSamples() / 4, to = 3 * b.getNumSamples() / 4;
                for (int i = from + 1; i < to; ++i)
                    if ((b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0))
                        ++n;
                return n / 2.0 / ((to - from) / kRate);
            };
            p.settings.style = Style::voice;
            expectWithinAbsoluteError (crossingsHz (render (tone, p, 4 * kBeat, kRate)), 440.0, 8.0);
            p.settings.style = Style::tape;
            expectWithinAbsoluteError (crossingsHz (render (tone, p, 4 * kBeat, kRate)), 220.0, 4.0);
            p.settings.style = Style::voice;
            p.settings.transpose = 12.0f;
            expectWithinAbsoluteError (crossingsHz (render (tone, p, 4 * kBeat, kRate)), 880.0, 16.0);
        }

        beginTest ("a straight plan gives the recording back");
        {
            const auto src = bursts ({ 0.5, 1.5 }, 2.0);
            Plan p;
            p.sourceBeats = p.targetBeats = 2;
            const auto out = render (src, p, src.getNumSamples(), kRate);
            expectEquals (out.getNumSamples(), src.getNumSamples());
            expectEquals (out.getSample (0, kBeat / 2 + 100), src.getSample (0, kBeat / 2 + 100));
        }

        beginTest ("the tempo it sounds like comes from the spacing of its hits");
        {
            std::vector<double> t;
            for (int k = 0; k < 12; ++k)
                t.push_back (k * 60.0 / 93.6 + (k % 3 == 0 ? 0.004 : -0.003));
            expectWithinAbsoluteError (soundsLikeBpm (t, 92.0), 93.6, 0.3);
            expectWithinAbsoluteError (soundsLikeBpm (t, 180.0), 187.2, 0.6);
            expectEquals (soundsLikeBpm ({ 0.1, 0.37, 0.41 }, 120.0), 0.0);
        }
    }
};

static WarpTests warpTests;
