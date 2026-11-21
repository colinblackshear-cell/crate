#include "TestHelpers.h"

#include "AutomationPanel.h"
#include "Pedalboard.h"
#include "RigEngine.h"
#include "RigLooper.h"

// Step 6 of the deep show: the Grain pedal (a cloud, Freeze), bending a playing loop
// (Reverse, Half, Double, Stutter) and Move (a knob that moves by itself).
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
constexpr int kBeat = 24000;   // 120 bpm

float rms (const juce::AudioBuffer<float>& b, int from, int to)
{
    double sum = 0;
    for (int i = from; i < to; ++i)
        sum += (double) b.getSample (0, i) * b.getSample (0, i);
    return (float) std::sqrt (sum / juce::jmax (1, to - from));
}

juce::AudioBuffer<float> runFx (StripFx& fx, const juce::AudioBuffer<float>& in)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer none;
    for (int at = 0; at < out.getNumSamples(); at += kBlock)
    {
        const int n = juce::jmin (kBlock, out.getNumSamples() - at);
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, at, n);
        fx.processBlock (block, none);
    }
    return out;
}

// Plays layer 0 for `beats` from the clock's beat, returning the left output.
std::vector<float> play (RigLooper& looper, RigLooper::Clock& clock, double beats, std::function<void (double)> atBeat = {})
{
    std::vector<float> heard;
    juce::AudioBuffer<float> out (2, kBlock);
    std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
    const int blocks = (int) std::round (beats * kBeat / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        if (atBeat)
            atBeat (clock.beat);
        looper.process (clock, sources, out);
        for (int i = 0; i < kBlock; ++i)
            heard.push_back (out.getSample (0, i));
        clock.beat += kBlock / (double) kBeat;
    }
    return heard;
}
}

struct SoundBendingTests : juce::UnitTest
{
    SoundBendingTests() : juce::UnitTest ("Sound bending", "Rig") {}

    void runTest() override
    {
        beginTest ("the Grain pedal: a cloud that changes the sound, and Freeze keeps a moment ringing");
        {
            const int n = (int) kRate * 4;
            juce::AudioBuffer<float> in (2, n);
            in.clear();
            for (int i = 0; i < n / 2; ++i)   // two seconds of a note, then silence
            {
                const float v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / kRate);
                in.setSample (0, i, v);
                in.setSample (1, i, v);
            }

            StripFx listening;
            listening.prepareToPlay (kRate, kBlock);
            pedalboard::setOn (listening, pedalboard::grain, true);
            pedalboard::set (listening, "grainMix", 1.0f);
            pedalboard::set (listening, "grainSpray", 0.0f);
            expect (listening.isActive());
            const auto heard = runFx (listening, in);
            bool finite = true;
            for (int i = 0; i < n; ++i)
                finite = finite && std::isfinite (heard.getSample (0, i)) && std::abs (heard.getSample (0, i)) < 4.0f;
            expect (finite, "finite");
            const float cloud = rms (heard, (int) kRate / 2, (int) kRate * 2);
            expectGreaterThan (cloud, 0.1f, "the cloud is about as loud as the note");
            expectLessThan (cloud, 0.8f, "and not much louder");
            expectLessThan (rms (heard, (int) kRate * 3, n), 0.02f, "listening: once the note is gone, so is the cloud");

            StripFx frozen;
            frozen.prepareToPlay (kRate, kBlock);
            pedalboard::setOn (frozen, pedalboard::grain, true);
            pedalboard::set (frozen, "grainMix", 1.0f);
            pedalboard::set (frozen, "grainSpray", 0.0f);
            juce::AudioBuffer<float> first (2, n / 2), rest (2, n / 2);
            for (int ch = 0; ch < 2; ++ch)
            {
                first.copyFrom (ch, 0, in, ch, 0, n / 2);
                rest.copyFrom (ch, 0, in, ch, n / 2, n / 2);
            }
            runFx (frozen, first);
            pedalboard::set (frozen, "grainFreeze", 1.0f);   // Freeze, just as the note stops
            const auto after = runFx (frozen, rest);
            expectGreaterThan (rms (after, (int) kRate, n / 2), 0.1f, "frozen: the note rings on in silence");

            pedalboard::setOn (frozen, pedalboard::grain, false);
            const auto off = runFx (frozen, rest);
            expectLessThan (rms (off, kBlock * 4, n / 2), 1.0e-4f, "off: the pedal leaves silence silent");
        }

        beginTest ("bending a loop: reverse, half and double speed, a stutter, and back on the beat");
        {
            RigLooper looper;
            looper.prepare (kRate, kBlock);
            // A ramp, so where it plays from can be read off what comes out.
            const int len = 4 * kBeat;
            juce::AudioBuffer<float> ramp (2, len);
            for (int i = 0; i < len; ++i)
            {
                ramp.setSample (0, i, (float) i / (float) len);
                ramp.setSample (1, i, (float) i / (float) len);
            }
            looper.setLayerAudio (0, ramp, 4.0);
            looper.getLayer (0).level = 1.0f;
            RigLooper::Clock clock;
            clock.bpm = 120;
            clock.playing = true;
            play (looper, clock, 1.0);   // the level settles

            // Straight: a beat later, a quarter further on.
            auto heard = play (looper, clock, 1.0);
            const float straightSlope = heard[(size_t) kBeat / 2 + 100] - heard[(size_t) kBeat / 2 - 100];
            expectGreaterThan (straightSlope, 0.0f, "straight goes forwards");

            looper.setBend (0, RigLooper::reverse);
            heard = play (looper, clock, 1.0);
            const float reverseSlope = heard[(size_t) kBeat / 2 + 100] - heard[(size_t) kBeat / 2 - 100];
            expectWithinAbsoluteError (reverseSlope, -straightSlope, 1.0e-4f, "reverse goes backwards as fast");
            expectEquals (looper.getBend (0), (int) RigLooper::reverse);

            looper.setBend (0, RigLooper::half);
            heard = play (looper, clock, 1.0);
            expectWithinAbsoluteError (heard[(size_t) kBeat / 2 + 100] - heard[(size_t) kBeat / 2 - 100], straightSlope * 0.5f, 1.0e-4f, "half speed");

            looper.setBend (0, RigLooper::twice);
            heard = play (looper, clock, 0.5);
            expectWithinAbsoluteError (heard[(size_t) kBeat / 4 + 100] - heard[(size_t) kBeat / 4 - 100], straightSlope * 2.0f, 1.0e-4f, "double speed");

            looper.setBend (0, RigLooper::stutter8);
            heard = play (looper, clock, 2.0);
            // An 8th is half a beat: what plays repeats every half beat.
            const size_t eighth = (size_t) kBeat / 2;
            for (size_t at : { (size_t) kBeat / 8, (size_t) kBeat / 3 })
                expectWithinAbsoluteError (heard[at + eighth * 2], heard[at + eighth], 1.0e-4f, "stutter repeats its slice");

            // Straight again: on the beat, as if it had never been bent.
            looper.setBend (0, RigLooper::straight);
            const double beatNow = clock.beat;
            heard = play (looper, clock, 1.0);
            const double p = std::fmod (beatNow + 0.5, 4.0) / 4.0;
            expectWithinAbsoluteError (heard[(size_t) kBeat / 2], (float) p, 2.0e-3f, "back on the beat");

            // The loop itself was never changed.
            const auto audio = looper.copyLayerAudio (0);
            expectWithinAbsoluteError (audio.getSample (0, len / 2), 0.5f, 1.0e-6f);
            // Clearing the layer puts it back to straight for the next loop.
            looper.setBend (0, RigLooper::stutter16);
            looper.clear (0);
            play (looper, clock, 0.1);
            expectEquals (looper.getBend (0), (int) RigLooper::straight);
            expectEquals (RigLooper::bendName (RigLooper::half), juce::String ("Half speed"));
        }

        beginTest ("Move: a setting moves by itself, in time, as a looping lane");
        {
            for (int shape = 0; shape < RigAutomation::numMoveShapes; ++shape)
            {
                const auto lane = RigAutomation::makeMove (0, 0, shape, 8.0, 0.9f, 0.25f);
                expect (lane.loop && lane.on && lane.lengthBeats == 8.0, RigAutomation::moveShapeName (shape));
                float lo = 1, hi = 0;
                for (double b = 0; b < 8.0; b += 0.05)
                {
                    lo = juce::jmin (lo, lane.valueAt (b));
                    hi = juce::jmax (hi, lane.valueAt (b));
                }
                expectLessOrEqual (hi, 1.0f, "kept inside");
                expectGreaterThan (hi - lo, 0.4f, "swings both ways (moved down from the top)");
            }
            const auto wave = RigAutomation::makeMove (0, 0, RigAutomation::moveSine, 4.0, 0.5f, 0.25f);
            expectWithinAbsoluteError (wave.valueAt (0.0), 0.5f, 1.0e-3f, "starts where the setting is");
            expectWithinAbsoluteError (wave.valueAt (1.0), 0.75f, 1.0e-3f, "up first");
            expectWithinAbsoluteError (wave.valueAt (3.0), 0.25f, 1.0e-3f);

            RigEngine engine;
            engine.prepare (kRate, kBlock);
            AutomationPanel panel (engine);
            panel.pressMove();
            expect (panel.getStatus().contains ("Touch a knob"), "nothing touched yet");
            expectEquals (engine.getAutomation().getNumLanes(), 0);

            // A switch can't Move.
            const auto& bassParams = engine.getTool (RigEngine::bassTool).getParameters();
            int knob = -1, choice = -1;
            for (int p = 0; p < bassParams.size(); ++p)
            {
                const bool discrete = bassParams[p]->isDiscrete() || bassParams[p]->isBoolean();
                if (discrete && choice < 0)
                    choice = p;
                if (! discrete && knob < 0)
                    knob = p;
            }
            expect (knob >= 0 && choice >= 0);
            engine.lastTouchedTool = RigEngine::bassTool;
            engine.lastTouchedParameter = choice;
            panel.pressMove();
            expectEquals (engine.getAutomation().getNumLanes(), 0, "a switch");
            expect (panel.getStatus().contains ("switch"));

            // The bass's first knob, as if just touched.
            engine.lastTouchedParameter = knob;
            panel.setMoveChoices (RigAutomation::moveTriangle, 2, 3);   // every bar, all the way
            panel.pressMove();
            expectEquals (engine.getAutomation().getNumLanes(), 1);
            const auto lane = engine.getAutomation().getLanes()[0];
            expectEquals (lane.tool, (int) RigEngine::bassTool);
            expectWithinAbsoluteError (lane.lengthBeats, 4.0, 1.0e-9, "a bar round");
            expect (lane.loop);
            expect (panel.getStatus().contains ("moves by itself"));
            expectEquals (panel.getNumRows(), 1);

            // It really moves the setting as the clock runs.
            auto* param = bassParams[knob];
            RigAutomation::Clock clock;
            clock.playing = true;
            clock.sampleRate = kRate;
            clock.samples = kBlock;
            float lo = 1, hi = 0;
            for (int b = 0; b < 200; ++b)
            {
                clock.beat = b * kBlock / (double) kBeat;
                engine.getAutomation().process (clock);
                lo = juce::jmin (lo, param->getValue());
                hi = juce::jmax (hi, param->getValue());
            }
            expectGreaterThan (hi - lo, 0.5f, "the setting went up and down");

            if (const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TAKES_SNAPSHOT", {}); path.isNotEmpty())
            {
                panel.resized();
                const auto image = panel.createComponentSnapshot (panel.getLocalBounds(), true, 1.0f);
                juce::File file (path + "-move.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            // Moving it again replaces that lane, and Undo brings the first back.
            panel.setMoveChoices (RigAutomation::moveRandom, 4, 1);
            panel.pressMove();
            expectEquals (engine.getAutomation().getNumLanes(), 1);
            expectWithinAbsoluteError (engine.getAutomation().getLanes()[0].lengthBeats, 16.0, 1.0e-9);
            panel.pressUndo();
            expectWithinAbsoluteError (engine.getAutomation().getLanes()[0].lengthBeats, 4.0, 1.0e-9);
        }
    }
};

static SoundBendingTests soundBendingTests;
