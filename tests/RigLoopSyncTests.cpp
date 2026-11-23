#include "TestHelpers.h"

#include "RigLooper.h"

#include <cmath>

// Every loop on one shared timeline: Play starts them all from their top, a loop recorded while
// others play lines up its bar 1 with theirs, and it still sounds exactly where it was played.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

struct SyncRig
{
    RigLooper looper;
    RigLooper::Clock clock;
    juce::AudioBuffer<float> source { 2, kBlock }, out { 2, kBlock };
    std::function<float (double beat)> signal = [] (double) { return 0.0f; };
    // (clock beat, what was heard) for every sample while the clock played.
    std::vector<std::pair<double, float>> heard;
    bool maintain = true;   // the engine's timer, every 10 blocks

    SyncRig()
    {
        looper.prepare (kRate);
        clock.bpm = 120.0;
        clock.beatsPerBar = 4;
    }

    void run (double beats)
    {
        const int blocks = (int) std::round (beats * 60.0 / clock.bpm * kRate / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            const double bps = clock.bpm / (60.0 * kRate);
            for (int i = 0; i < kBlock; ++i)
            {
                const double beat = clock.beat + (clock.playing ? i * bps : 0.0);
                source.setSample (0, i, signal (beat));
                source.setSample (1, i, signal (beat));
            }
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            for (auto& s : sources)
                s = &source;
            const bool wasPlaying = clock.playing;
            if (looper.process (clock, sources, out))
                clock.playing = true;
            if (wasPlaying)
                for (int i = 0; i < kBlock; ++i)
                    heard.emplace_back (clock.beat + i * bps, out.getSample (0, i));
            if (clock.playing)
                clock.beat += kBlock * bps;
            if (maintain && b % 10 == 0)
                looper.maintain();
        }
    }

    void stopAndRewind()
    {
        clock.playing = false;
        run (0.05);
        clock.beat = 0;   // the Rig's Stop goes back to the start
    }

    // The loudest difference between what was heard and `expected` of the beat, from `fromBeat`
    // (a few samples round each bar line left out: the loop's short fades are there).
    float worstError (double fromBeat, std::function<float (double)> expected) const
    {
        float worst = 0;
        for (auto& [beat, v] : heard)
        {
            if (beat < fromBeat)
                continue;
            const double inBar = std::fmod (beat, 4.0);
            if (inBar < 0.01 || inBar > 3.99)
                continue;
            worst = juce::jmax (worst, std::abs (v - expected (beat)));
        }
        return worst;
    }
};

// A sound that tells where in an 8-beat (two-bar) phrase it was played.
float phrase (double beat) { return (float) (0.1 + 0.05 * std::fmod (beat, 8.0)); }
}

struct RigLoopSyncTests : juce::UnitTest
{
    RigLoopSyncTests() : juce::UnitTest ("Rig loop sync", "Rig") {}

    void runTest() override
    {
        beginTest ("a loop recorded partway through another lines up its bar 1 with it");
        {
            SyncRig rig;
            auto& a = rig.looper.getLayer (0);
            auto& b = rig.looper.getLayer (1);
            a.bars = 2;
            a.level = 0.0f;   // heard: only B
            b.bars = 2;
            b.level = 1.0f;
            b.source = RigLooper::mix;
            rig.signal = [] (double beat) { return phrase (beat); };
            rig.looper.pressRecord (0);
            rig.run (9.0);    // A: beats 0-8, playing
            expect (rig.looper.getState (0) == RigLooper::State::playing);
            rig.looper.pressRecord (1);   // B: from the next bar, beat 12, to 20
            rig.run (11.5);
            expect (rig.looper.getState (1) == RigLooper::State::playing, "B recorded");
            rig.signal = [] (double) { return 0.0f; };
            rig.run (1.0);    // the timer has put B on the shared bars by now
            expectWithinAbsoluteError (b.position.load(), a.position.load(), 1.0e-6, "the playheads move together");
            rig.heard.clear();
            rig.run (16.0);
            expectWithinAbsoluteError (b.position.load(), a.position.load(), 1.0e-6, "still together");
            expectLessThan (rig.worstError (0.0, phrase), 0.01f, "B sounds exactly where it was played");

            beginTest ("Stop then Play starts every loop from its top, together, and keeps how they fit");
            rig.stopAndRewind();
            rig.clock.playing = true;
            rig.heard.clear();
            rig.run (0.05);
            expectLessThan (a.position.load(), 0.01, "A from its top");
            expectLessThan (b.position.load(), 0.01, "B from its top");
            rig.run (12.0);
            expectWithinAbsoluteError (b.position.load(), a.position.load(), 1.0e-6);
            expectLessThan (rig.worstError (0.0, phrase), 0.01f, "B still sounds where it was played against A");
        }

        beginTest ("the playheads match right away, before the loop is turned round");
        {
            SyncRig rig;
            rig.maintain = false;
            auto& a = rig.looper.getLayer (0);
            auto& b = rig.looper.getLayer (1);
            a.bars = 4;
            b.bars = 2;
            rig.looper.pressRecord (0);
            rig.run (2.0);
            rig.looper.pressRecord (1);   // beats 4-12, against A's 0-16
            rig.run (11.0);               // beat 13: B done, A still recording (to 16)
            expect (rig.looper.getState (1) == RigLooper::State::playing);
            // B is 2 bars: it's at the same place in its two bars as A is in A's first two.
            const double bBeats = b.position.load() * 8.0;
            expectWithinAbsoluteError (std::fmod (rig.clock.beat - 0.02, 8.0), bBeats, 0.03, "B lines up with A, which is still recording");
            rig.run (4.0);                // A done at 16, lining up with B (which lined up with A)
            expect (rig.looper.getState (0) == RigLooper::State::playing);
            const double aNow = a.position.load() * 16.0, bNow = b.position.load() * 8.0;
            expectWithinAbsoluteError (std::fmod (aNow, 8.0), bNow, 1.0e-6);
        }

        beginTest ("a loop with nothing else playing keeps its own top");
        {
            SyncRig rig;
            auto& a = rig.looper.getLayer (0);
            a.bars = 1;
            a.countIn = 1;
            rig.looper.pressRecord (0);
            rig.run (8.5);   // counts in bar 1, records bar 2
            expect (rig.looper.getState (0) == RigLooper::State::playing);
            rig.run (0.05);
            expectLessThan (std::abs (a.position.load() - (std::fmod (rig.clock.beat - 4.0, 4.0) / 4.0)), 0.01);
        }

        beginTest ("Rec from a clock left partway into a bar starts on the next bar line");
        {
            SyncRig rig;
            rig.clock.beat = 1.5;
            auto& a = rig.looper.getLayer (0);
            a.bars = 1;
            rig.looper.pressRecord (0);
            rig.run (0.01);
            expectWithinAbsoluteError (a.startsAt.load(), 4.0, 1.0e-9);
        }

        beginTest ("a loop put on a layer at a beat (a song part) plays from its top there");
        {
            SyncRig rig;
            auto& a = rig.looper.getLayer (0);
            a.bars = 2;
            rig.looper.pressRecord (0);
            rig.run (9.0);
            rig.looper.restartAt (0, 12.0);
            rig.run (3.5);   // beat 12.5
            expectWithinAbsoluteError (a.position.load(), 0.5 / 8.0, 0.01);
        }
    }
};

static RigLoopSyncTests rigLoopSyncTests;
