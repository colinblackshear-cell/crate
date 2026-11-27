#include <juce_core/juce_core.h>

#include "core/IdeaTempo.h"
#include "IdeaTempoSounds.h"

// The first idea sets the song's tempo (core/IdeaTempo.h): claps and a sung jingle (with a pickup and
// 1/8s off the beat) at many tempos, a hum with no beat in it, silence, and the cut loop going round.
class IdeaTempoTests : public juce::UnitTest
{
public:
    IdeaTempoTests() : juce::UnitTest ("Idea tempo", "Core") {}

    void runTest() override
    {
        using namespace ideatempo;
        beginTest ("claps: the tempo they're at, bar 1 on the first");
        for (double bpm : { 72.0, 93.0, 128.0, 140.0 })
        {
            const auto take = claps (bpm, 0.37, 16, 20.0);
            const auto f = crate::IdeaTempo::fit (take, kRate, (int) ((0.37 + 16 * 60.0 / bpm + 0.3) * kRate));
            expect (f.isValid() && f.heard, "heard a beat at " + juce::String (bpm));
            expectWithinAbsoluteError (f.bpm, bpm, 0.5);
            expectWithinAbsoluteError (f.beats, 16.0, 1.0e-9);
            expectWithinAbsoluteError (f.start / kRate, 0.37, 0.03);
            expectEquals (f.length, (int) std::lround (f.beats * 60.0 / f.bpm * kRate), "exactly its bars at its tempo");
        }

        beginTest ("a sung jingle: syncopated, a pickup into bar 1, sung loosely");
        for (double bpm = 74.5; bpm <= 160.0; bpm += 6.5)
        {
            const auto take = jingle (bpm, 0.8, 20.0);
            const auto f = crate::IdeaTempo::fit (take, kRate, (int) ((0.8 + 16 * 60.0 / bpm + 0.6) * kRate));
            expect (f.heard, "heard a beat at " + juce::String (bpm));
            expectWithinAbsoluteError (f.bpm, bpm, 1.0);
            expectWithinAbsoluteError (f.beats, 16.0, 1.0e-9, "the 4 bars it was sung over");
            expectWithinAbsoluteError (f.start / kRate, 0.8, 0.04, "bar 1 after the pickup");
        }
        // Very slow (68): it may be heard in double time (136), the same loop in twice the bars.
        {
            const auto take = jingle (68.0, 0.8, 20.0);
            const auto f = crate::IdeaTempo::fit (take, kRate, (int) ((0.8 + 16 * 60.0 / 68.0 + 0.6) * kRate));
            const bool twice = std::abs (f.bpm - 136.0) <= 2.0;
            expect (std::abs (f.bpm - 68.0) <= 1.0 || twice, juce::String (f.bpm));
            expectWithinAbsoluteError (f.length / kRate, 16 * 60.0 / 68.0, 0.2, "as long as it was sung, either way");
        }

        beginTest ("a hum with no edges: the tempo comes from its length");
        {
            juce::AudioBuffer<float> take (2, (int) (8 * kRate));
            take.clear();
            addTone (take, 0.5, 5.3, 220.0, 0.3f, 0.2);
            const auto f = crate::IdeaTempo::fit (take, kRate, (int) (6.2 * kRate));
            expect (f.isValid() && ! f.heard);
            expect (f.bpm >= 70.0 && f.bpm < 140.0, juce::String (f.bpm));
            expectWithinAbsoluteError (f.length / kRate, 5.45, 0.1, "the loop is as long as the hum (and its ring)");
        }

        beginTest ("silence, or too little: nothing changes");
        {
            juce::AudioBuffer<float> quiet (2, (int) (8 * kRate));
            quiet.clear();
            expect (! crate::IdeaTempo::fit (quiet, kRate).isValid());
            juce::AudioBuffer<float> blip (2, (int) (8 * kRate));
            blip.clear();
            addTone (blip, 1.0, 0.3, 440.0, 0.5f, 0.01);
            expect (! crate::IdeaTempo::fit (blip, kRate).isValid());
        }

        beginTest ("the cut loop: its bars long, the pickup folded round to its end");
        {
            const double bpm = 96.0;
            const auto take = jingle (bpm, 0.8, 20.0);
            const auto f = crate::IdeaTempo::fit (take, kRate, (int) ((0.8 + 16 * 60.0 / bpm + 0.6) * kRate));
            const auto loop = crate::IdeaTempo::cut (take, f);
            expectEquals (loop.getNumSamples(), f.length);
            expectEquals (loop.getNumChannels(), 2);
            // the pickup (half a beat before bar 1) is in the loop's last half beat
            const int half = (int) (0.5 * 60.0 / f.bpm * kRate);
            expect (loop.getRMSLevel (0, f.length - half, half) > 0.02f, "the pickup plays going round");
            // and the first beat is the recording's from bar 1
            const int beat = (int) (60.0 / f.bpm * kRate);
            expect (loop.getRMSLevel (0, 0, beat) > 0.05f);
        }
    }
};

static IdeaTempoTests ideaTempoTests;
