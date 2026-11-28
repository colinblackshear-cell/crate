#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "GrooveView.h"

// TO LOOPER on the Mac, as on the phone: the picked instrument's part goes onto the next empty
// loop layer for the part's length, and once it loops the instrument switches off (part kept) so
// it isn't heard twice; Undo clears the loop and switches it back on.
class RigToLooperTests : public juce::UnitTest
{
public:
    RigToLooperTests() : juce::UnitTest ("Rig: To looper sends a part and switches the instrument off", "Rig") {}

    static float run (RigEngine& engine, int blocks)
    {
        juce::AudioBuffer<float> out (2, 480);
        float peak = 0.0f;
        for (int i = 0; i < blocks; ++i)
        {
            out.clear();
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            peak = juce::jmax (peak, out.getMagnitude (0, 480));
        }
        return peak;
    }

    static void busyBass (RigEngine& engine, int length)
    {
        auto& bass = engine.getBass();
        crate::bass::Pattern p;
        p.length = length;
        for (int s = 0; s < length; ++s)
            p.steps[(size_t) s].on = true;
        bass.setPattern (bass.getSelectedPattern(), p);
        if (auto* rate = bass.params.getParameter ("stepRate"))
            rate->setValueNotifyingHost (rate->convertTo0to1 (6.0f));   // 1/16
    }

    static bool bassOn (RigEngine& engine)
    {
        auto* v = engine.getBass().params.getRawParameterValue ("sequencer");
        return v != nullptr && v->load() > 0.5f;
    }

    void runTest() override
    {
        beginTest ("the bass's part goes onto loop 1 for its length, then the bass switches off");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            engine.getTransport().metronome = false;
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::bass);
            busyBass (engine, 16);
            auto& looper = engine.getLooper();

            expectEquals (view.partBars (GrooveView::bass), 1, "16 sixteenths: one bar");
            expectEquals (view.pressToLooper(), 0, "loop 1");
            expectEquals (looper.getLayer (0).source.load(), (int) RigLooper::bass);
            expectEquals (looper.getLayer (0).bars.load(), 1);
            expect (bassOn (engine), "a stopped bass starts so its part can be recorded");
            expectEquals (engine.handOffLayer.load(), 0);

            for (int i = 0; i < 2000 && looper.getState (0) != RigLooper::State::playing; ++i)
            {
                run (engine, 1);
                view.tickToLooper();
                if (looper.getState (0) == RigLooper::State::playing)
                    break;
            }
            // (The tick above may have seen it land already; land it here if not.)
            view.tickToLooper();
            expect (looper.getState (0) == RigLooper::State::playing, "the loop plays round");
            expect (! bassOn (engine), "the bass is off once its part loops");
            expectEquals (view.getPrintLayer(), -1);
            expectEquals (view.lastTakeOf (GrooveView::bass), 0);
            expectEquals (engine.getBass().getPattern (engine.getBass().getSelectedPattern()).length, 16, "its part is kept");

            // Only the loop sounds now: muted, there's nothing (the bass was cut at the loop's first beat).
            expect (run (engine, 20) > 0.001f, "the loop plays the part");
            looper.getLayer (0).mute = true;
            run (engine, 20);
            expect (run (engine, 20) < 1.0e-4f, "with the loop muted it's quiet: the bass doesn't play it twice");
            looper.getLayer (0).mute = false;

            // Undo: the loop goes and the bass plays its part again.
            expect (view.undoLastTake());
            run (engine, 2);
            expect (looper.getState (0) == RigLooper::State::empty, "loop 1 cleared");
            expect (bassOn (engine), "the bass is back on");
            expectEquals (engine.handOffLayer.load(), -1, "the hand-off has let go");
        }

        beginTest ("pressed again before it closes, To looper is called off");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::bass);
            busyBass (engine, 16);
            expectEquals (view.pressToLooper(), 0);
            run (engine, 2);
            view.tickToLooper();
            expectEquals (view.pressToLooper(), -1, "called off");
            run (engine, 2);
            expect (engine.getLooper().getState (0) == RigLooper::State::empty, "nothing on loop 1");
            expectEquals (engine.handOffLayer.load(), -1);
        }

        beginTest ("the part's length: whole bars that hold the pattern, the MPC's bars, the Length box");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.pickTrack (GrooveView::bass);
            busyBass (engine, 12);
            expectEquals (view.partBars (GrooveView::bass), 3, "12 sixteenths fit 3 bars exactly");
            busyBass (engine, 8);
            expectEquals (view.partBars (GrooveView::bass), 1, "8 sixteenths: one bar holds it twice");
            engine.getMpc().getProject().currentSequence().bars = 4;
            expectEquals (view.partBars (GrooveView::drums), 4, "the MPC sequence's bars");
            view.setRecSetup (1, 2);
            expectEquals (view.partBars (GrooveView::synth), 2, "the synth: the bottom bar's Length");
            view.setRecSetup (1, 0);
            expectEquals (view.partBars (GrooveView::drone), 4, "Length free: 4 bars");
            expect (! RigView::hasPart (GrooveView::vocoder) && ! RigView::hasPart (GrooveView::loops), "no part to send");
        }

        beginTest ("hot keys: Cmd+K the click, Cmd+Up the tempo, Cmd+3 the bass, Cmd+L To looper, Cmd+Z and Cmd+Shift+Z");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            engine.getTransport().metronome = false;
            engine.getTransport().setBpm (100.0);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            const auto cmd = juce::ModifierKeys::commandModifier;
            expect (view.keyPressed (juce::KeyPress ('k', cmd, 0)));
            expect (engine.getTransport().metronome.load(), "Cmd+K: click on");
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::upKey, cmd, 0)));
            expectWithinAbsoluteError (engine.getTransport().getBpm(), 101.0, 1.0e-6, "Cmd+Up: a BPM faster");
            expect (view.keyPressed (juce::KeyPress ('3', cmd, 0)));
            expectEquals (view.getTrack(), (int) GrooveView::bass, "Cmd+3: the third instrument on the rail");
            busyBass (engine, 16);
            expect (view.keyPressed (juce::KeyPress ('l', cmd, 0)));
            expectEquals (view.getPrintLayer(), 0, "Cmd+L: To looper");
            expect (view.keyPressed (juce::KeyPress ('z', cmd, 0)), "Cmd+Z");
            expectEquals (view.getPrintLayer(), -1, "called off");
            expect (view.keyPressed (juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)), "Cmd+Shift+Z is taken");
        }

        beginTest ("Sing free: a voice take with no click or count-in, until Rec again");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            engine.getTransport().metronome = true;
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::vocoder);
            view.setSingFree (true);
            expectEquals (view.pressRecTake(), 0);
            expectEquals (engine.getLooper().getLayer (0).bars.load(), 0, "free length");
            expectEquals (engine.getLooper().getLayer (0).countIn.load(), 0, "no count-in");
            expect (! engine.getTransport().metronome.load(), "no click while you sing");
            run (engine, 2);
            expect (view.undoLastTake(), "thrown away");
            run (engine, 2);
            view.tickToLooper();
            expect (engine.getTransport().metronome.load(), "the click is back");

            view.pickTrack (GrooveView::bass);
            expectEquals (view.pressRecTake(), 0);
            expectEquals (engine.getLooper().getLayer (0).bars.load(), view.getRecLength(), "not a voice: Length as set");
        }

        beginTest ("Sing free: the take is fitted to the beat it was sung at");
        {
            // The phone's own check (PocketLoopTools): sung free at 90 BPM, a sound on every beat, 8
            // beats and a little late, over a song at 100. It hears ~90 and becomes 8 beats (2 bars).
            // (putOnLoopLayer takes 44.1 kHz sound, as a sound from anywhere is.)
            RigEngine engine;
            const double sr = 44100.0, songBpm = 100.0, sungBpm = 90.0, lead = 0.07;
            engine.prepare (48000.0, 480);
            engine.getTransport().setBpm (songBpm);
            engine.getLoopLab().renderInline = true;
            const int length = (int) ((8.0 * 60.0 / sungBpm + lead + 0.05) * sr);
            juce::AudioBuffer<float> audio (2, length);
            audio.clear();
            juce::Random rng (5);
            for (int b = 0; b < 8; ++b)
            {
                const int at = (int) ((lead + b * 60.0 / sungBpm) * sr);
                for (int i = 0; i < (int) (0.18 * sr) && at + i < length; ++i)
                {
                    const float v = (rng.nextFloat() * 2 - 1) * 0.7f * (float) std::exp (-i / (0.04 * sr));
                    audio.setSample (0, at + i, v);
                    audio.setSample (1, at + i, v);
                }
            }
            const double beats = length / sr * songBpm / 60.0;
            const int layer = engine.putOnLoopLayer (std::make_shared<juce::AudioBuffer<float>> (audio), "free take", beats);
            expectEquals (layer, 0);
            run (engine, 2);
            const double heard = engine.getLoopLab().fitToBeat (layer);
            expect (std::abs (heard - sungBpm) <= 4.0, "heard about 90 BPM: " + juce::String (heard));
            expectWithinAbsoluteError (engine.getLoopLab().get (layer).plan.targetBeats, 8.0, 1.0e-6, "8 beats: 2 bars at the song's tempo");
            expect (engine.getLoopLab().canUndo (layer), "Undo takes the fit back");
        }
    }
};

static RigToLooperTests rigToLooperTests;
