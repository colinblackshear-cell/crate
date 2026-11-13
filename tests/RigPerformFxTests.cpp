#include "TestHelpers.h"

#include "PerformPad.h"
#include "MixerView.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
constexpr double kBpm = 120.0;   // a beat is 24000 samples

// Runs `blocks` blocks through the pad's effects, the input made by `in` (sample index -> value),
// and gives back the left channel out.
struct Runner
{
    PerformFx fx;
    double ppq = 0;
    long long t = 0;
    Runner() { fx.prepare (kRate); }

    std::vector<float> run (int blocks, std::function<float (long long)> in, bool playing = true)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> b (2, kBlock);
        for (int k = 0; k < blocks; ++k)
        {
            for (int i = 0; i < kBlock; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    b.setSample (ch, i, in (t + i));
            fx.process (b, ppq, kBpm, playing);
            for (int i = 0; i < kBlock; ++i)
                out.push_back (b.getSample (0, i));
            t += kBlock;
            ppq += kBlock * kBpm / 60.0 / kRate;
        }
        return out;
    }
};

float rms (const std::vector<float>& v, size_t from, size_t to)
{
    double sum = 0;
    for (size_t i = from; i < to; ++i)
        sum += v[i] * v[i];
    return (float) std::sqrt (sum / (double) juce::jmax ((size_t) 1, to - from));
}

float tone (long long t, double hz) { return 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) t / kRate); }
}

struct RigPerformFxTests : juce::UnitTest
{
    RigPerformFxTests() : juce::UnitTest ("Rig FX pad", "Rig") {}

    void runTest() override
    {
        beginTest ("untouched, the mix goes through as it is");
        {
            Runner r;
            auto out = r.run (20, [] (long long t) { return tone (t, 440); });
            for (size_t i = 0; i < out.size(); ++i)
                if (std::abs (out[i] - tone ((long long) i, 440)) > 1.0e-6f)
                    return expect (false, "sample " + juce::String ((int) i) + " changed");
        }

        beginTest ("filter: left is a low-pass that takes the top off, the middle is open");
        {
            Runner r;
            r.fx.mode = PerformFx::filter;
            r.fx.x = 0.0f;
            r.fx.y = 0.2f;
            r.fx.engaged = true;
            auto out = r.run (40, [] (long long t) { return tone (t, 6000); });
            expect (rms (out, 9600, 19200) < 0.02f, "6 kHz is gone through the low-pass: " + juce::String (rms (out, 9600, 19200)));

            Runner open;
            open.fx.mode = PerformFx::filter;
            open.fx.x = 0.5f;
            open.fx.engaged = true;
            out = open.run (40, [] (long long t) { return tone (t, 6000); });
            expectWithinAbsoluteError (rms (out, 9600, 19200), 0.3535f, 0.01f, "the middle is open");

            Runner high;
            high.fx.mode = PerformFx::filter;
            high.fx.x = 1.0f;
            high.fx.y = 0.2f;
            high.fx.engaged = true;
            out = high.run (40, [] (long long t) { return tone (t, 100); });
            expect (rms (out, 9600, 19200) < 0.02f, "the right is a high-pass: 100 Hz is gone");
        }

        beginTest ("repeat: catches the beat you touch and plays it again");
        {
            Runner r;
            juce::Random rnd (7);
            std::vector<float> noise (200000);
            for (auto& s : noise)
                s = rnd.nextFloat() - 0.5f;
            r.run (10, [&] (long long t) { return noise[(size_t) t]; });
            r.fx.mode = PerformFx::repeat;
            r.fx.x = 0.0f;   // a quarter: 24000 samples
            r.fx.y = 1.0f;
            r.fx.engaged = true;
            const auto start = (size_t) r.t;
            auto out = r.run (150, [&] (long long t) { return noise[(size_t) t]; });
            // Past the fade in, a quarter later it's the same as a quarter before.
            double diff = 0;
            for (size_t i = 2000; i < 20000; ++i)
                diff += std::abs (out[i + 24000] - out[i]);
            expect (diff / 18000.0 < 0.01, "the slice repeats: " + juce::String (diff / 18000.0));
            // ... and not what's coming in now.
            double fresh = 0;
            for (size_t i = 2000; i < 20000; ++i)
                fresh += std::abs (out[i + 24000] - noise[start + i + 24000]);
            expect (fresh / 18000.0 > 0.1, "it's not the live input");

            r.fx.engaged = false;
            out = r.run (10, [&] (long long t) { return noise[(size_t) t]; });
            expectWithinAbsoluteError (out.back(), noise[(size_t) r.t - 1], 1.0e-6f, "let go and it's the live mix again");
        }

        beginTest ("tape stop: slows the mix to silence");
        {
            Runner r;
            r.run (10, [] (long long t) { return tone (t, 440); });
            r.fx.mode = PerformFx::tapeStop;
            r.fx.y = 1.0f;   // the quickest: a quarter of a beat
            r.fx.engaged = true;
            auto out = r.run (40, [] (long long t) { return tone (t, 440); });
            expect (rms (out, 0, 1500) > 0.2f, "it starts at speed");
            expect (rms (out, 9000, 19200) < 0.001f, "and stops");
        }

        beginTest ("gate: chops the mix in time");
        {
            Runner r;
            r.fx.mode = PerformFx::gate;
            r.fx.x = 0.5f;   // 1/16: a quarter beat, 6000 samples
            r.fx.y = 1.0f;
            r.fx.engaged = true;
            auto out = r.run (100, [] (long long) { return 0.5f; });
            // Bars after the start, in the first half it's open, in the second shut.
            expectWithinAbsoluteError (out[24000 + 1500], 0.5f, 0.01f, "open");
            expectWithinAbsoluteError (out[24000 + 4500], 0.0f, 0.01f, "shut");
            expectWithinAbsoluteError (out[30000 + 1500], 0.5f, 0.01f, "open again on the next 16th");
        }

        beginTest ("riser: a noise build that rises while held");
        {
            Runner r;
            r.fx.mode = PerformFx::riser;
            r.fx.x = 0.0f;   // a bar: 4 beats
            r.fx.y = 1.0f;
            r.fx.engaged = true;
            auto out = r.run (100, [] (long long) { return 0.0f; });   // 1 s, half a bar
            expect (rms (out, 24000, 48000) > 0.005f, "you hear it over silence");
            expectWithinAbsoluteError (r.fx.getRise(), 0.5f, 0.05f, "half way after half a bar");
            r.run (150, [] (long long) { return 0.0f; });
            expectWithinAbsoluteError (r.fx.getRise(), 1.0f, 0.001f, "all the way after a bar");
            r.fx.engaged = false;
            out = r.run (10, [] (long long) { return 0.0f; });
            expect (rms (out, 2400, 4800) == 0.0f, "let go and it's gone");
            expectEquals (r.fx.getRise(), 0.0f);
        }

        beginTest ("the pad: touch plays, let go stops, Hold keeps it; on the Mixer");
        {
            PerformFx fx;
            PerformPad pad (fx);
            pad.setBounds (0, 0, 600, 400);
            pad.setMode (PerformFx::gate);
            expectEquals (fx.mode.load(), (int) PerformFx::gate);
            pad.touch (0.25f, 0.75f);
            expect (fx.engaged.load());
            expectEquals (fx.x.load(), 0.25f);
            pad.release();
            expect (! fx.engaged.load());
            pad.setHold (true);
            pad.touch (0.5f, 0.5f);
            pad.release();
            expect (fx.engaged.load(), "held");
            pad.setHold (false);
            expect (! fx.engaged.load(), "Hold off lets it go");

            RigEngine engine;
            engine.prepare (kRate, kBlock);
            MixerView mixer (engine);
            mixer.setBounds (0, 0, 1600, 1000);
            expect (! mixer.getPad().isVisible());
            mixer.padBank.onClick();
            expect (mixer.isShowingPad() && mixer.getPad().isVisible());
            expect (! mixer.getPad().getBounds().isEmpty());
            expect (! mixer.channelFor (RigEngine::bassStrip)->isVisible(), "the strips make way");
            expect (mixer.channelFor (RigEngine::masterStrip)->isVisible(), "the master stays");
            mixer.getPad().touch (0.1f, 0.5f);
            expect (engine.getPerformFx().engaged.load(), "the Mixer's pad plays the Rig's mix");
            mixer.showPad (false);
            expect (! engine.getPerformFx().engaged.load(), "and switching back lets go");
            expect (mixer.channelFor (RigEngine::bassStrip)->isVisible());
        }
    }
};

static RigPerformFxTests rigPerformFxTests;
