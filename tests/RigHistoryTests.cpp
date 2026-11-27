#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "RigHistory.h"
#include "GrooveView.h"
#include "SongView.h"
#include "RigSong.h"
#include "SamplerScreen.h"

// Undo and Redo on the Mac, as on the phone: one history (RigHistory) of steps that each take
// themselves back and do themselves again, up to 64. A take, an overdub, a Clear, Clear all,
// To looper, the Sing free fit, song edits, the Play screen's Clear, quantize, automation...
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

template <typename T>
T* findIn (juce::Component& c)
{
    if (auto* t = dynamic_cast<T*> (&c))
        return t;
    for (auto* child : c.getChildren())
        if (auto* t = findIn<T> (*child))
            return t;
    return nullptr;
}

juce::AudioBuffer<float> constant (float value, int samples)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, samples);
    return b;
}

bool same (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (a.getSample (ch, i) != b.getSample (ch, i))
                return false;
    return true;
}

void busyBass (RigEngine& engine)
{
    auto& bass = engine.getBass();
    crate::bass::Pattern p;
    p.length = 16;
    for (int s = 0; s < 16; ++s)
        p.steps[(size_t) s].on = true;
    bass.setPattern (bass.getSelectedPattern(), p);
    if (auto* rate = bass.params.getParameter ("stepRate"))
        rate->setValueNotifyingHost (rate->convertTo0to1 (6.0f));
    if (auto* on = bass.params.getParameter ("sequencer"))
        on->setValueNotifyingHost (1.0f);
}

bool bassOn (RigEngine& engine)
{
    auto* v = engine.getBass().params.getRawParameterValue ("sequencer");
    return v != nullptr && v->load() > 0.5f;
}

// Blocks of audio, with the window's timer part after each.
void run (RigEngine& engine, RigView& view, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int i = 0; i < blocks; ++i)
    {
        out.clear();
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        view.tickToLooper();
    }
}

struct Rig
{
    RigEngine engine;
    std::unique_ptr<RigView> view;
    Rig()
    {
        engine.prepare (kRate, kBlock);
        engine.getTransport().metronome = false;
        view = std::make_unique<RigView> (engine, nullptr);
        view->setSize (1440, 960);
    }
};
}

class RigHistoryTests : public juce::UnitTest
{
public:
    RigHistoryTests() : juce::UnitTest ("Rig: Undo and Redo (the history)", "Rig") {}

    // A take of the bass on `layer`, `bars` long, recorded the way any Rec records: the layer
    // records, then plays.
    bool recordTake (Rig& rig, int layer, int bars)
    {
        auto& looper = rig.engine.getLooper();
        busyBass (rig.engine);
        looper.getLayer (layer).source = RigLooper::bass;
        looper.getLayer (layer).bars = bars;
        looper.getLayer (layer).countIn = 0;
        if (! rig.engine.getTransport().isPlaying())
            rig.engine.getTransport().play();
        looper.pressRecord (layer);
        for (int i = 0; i < 3000 && looper.getState (layer) != RigLooper::State::playing; ++i)
            run (rig.engine, *rig.view, 1);
        run (rig.engine, *rig.view, 1);
        return looper.getState (layer) == RigLooper::State::playing;
    }

    void runTest() override
    {
        using State = RigLooper::State;

        beginTest ("the history on its own: newest first, stale steps skipped, a new change clears Redo, 64 steps");
        {
            RigHistory h;
            std::vector<int> log;
            bool secondGood = true;
            auto stepOf = [&] (int n)
            {
                RigHistory::Step s;
                s.what = "step " + juce::String (n);
                s.undo = [&log, n] { log.push_back (-n); };
                s.redo = [&log, n] { log.push_back (n); };
                if (n == 2)
                    s.stillValid = [&secondGood] { return secondGood; };
                return s;
            };
            for (int n = 1; n <= 3; ++n)
                h.push (stepOf (n));
            expectEquals (h.nextUndo(), juce::String ("step 3"));
            expectEquals (h.undo(), juce::String ("step 3"));
            expect (h.canRedo());
            secondGood = false;
            expectEquals (h.undo(), juce::String ("step 1"), "step 2 can't any more: skipped");
            expect (! h.canUndo());
            expectEquals (h.undo(), juce::String(), "nothing more");
            expectEquals (h.redo(), juce::String ("step 1"));
            expectEquals (h.redo(), juce::String ("step 3"));
            expectEquals (h.redo(), juce::String(), "nothing more to redo");
            expect (log == std::vector<int> { -3, -1, 1, 3 });
            h.undo();
            expect (h.canRedo());
            h.push (stepOf (4));
            expect (! h.canRedo(), "a new change clears Redo");

            // Work done inside a step is never a step.
            RigHistory g;
            RigHistory::Step pushy;
            pushy.what = "pushy";
            pushy.undo = [&g] { g.push ({ "inner", {}, {}, {} }); };
            g.push (pushy);
            g.undo();
            expectEquals (g.undoSteps(), 0, "nothing was pushed while undoing");

            RigHistory capped;
            for (int n = 0; n < 70; ++n)
                capped.push ({ "n" + juce::String (n), {}, {}, {} });
            expectEquals (capped.undoSteps(), RigHistory::kMaxSteps, "64 steps at most");
            int back = 0;
            while (capped.undo().isNotEmpty())
                ++back;
            expectEquals (back, 64);
            expectEquals (capped.nextRedo(), juce::String ("n6"), "the oldest kept is the 7th");

            expect (RigHistory::undidText ("take on loop 1", true) == "Undid: take on loop 1. Undo again goes back another step.");
            expect (RigHistory::undidText ("take on loop 1", false) == "Undid: take on loop 1. That's as far back as it goes.");
            expect (RigHistory::redidText ("take on loop 1") == "Redid: take on loop 1.");
        }

        beginTest ("a take: Undo clears its loop, Redo puts the same loop back (sound, length, name, level)");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            expect (recordTake (rig, 0, 1), "the take plays round");
            looper.getLayer (0).level = 0.6f;
            rig.engine.setLoopName (0, "Bassline");
            const auto sound = looper.copyLayerAudio (0);
            const double beats = looper.getLayer (0).lengthBeats.load();
            expect (sound.getNumSamples() > 0 && sound.getMagnitude (0, sound.getNumSamples()) > 0.001f, "it recorded the bass");
            expectEquals (rig.view->getHistory().nextUndo(), juce::String ("take on loop 1"));

            expect (rig.view->undoLastTake(), "the dock's Undo: a step back");
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::empty, "the loop is cleared");
            expect (rig.view->getHistory().canRedo());

            expect (rig.view->historyRedo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::playing, "it plays again");
            expect (same (looper.copyLayerAudio (0), sound), "the same samples");
            expectWithinAbsoluteError (looper.getLayer (0).lengthBeats.load(), beats, 1.0e-9);
            expectEquals (rig.engine.getLoopName (0), juce::String ("Bassline"));
            expectWithinAbsoluteError (looper.getLayer (0).level.load(), 0.6f, 1.0e-6f);
            expect (! rig.view->getHistory().canRedo());

            // And back again: Undo, then a new change clears Redo.
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::empty);
            expect (rig.view->getHistory().canRedo());
            rig.engine.getLooper().setLayerAudio (5, constant (0.1f, 96000), 4.0);
            expect (recordTake (rig, 1, 1));
            expect (! rig.view->getHistory().canRedo(), "a new take clears Redo");
        }

        beginTest ("a loop that comes in by a loop bank or a song part is no take");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            looper.queueLayerAudio (2, constant (0.2f, 96000), 4.0, -1.0, 0.8f, false);
            run (rig.engine, *rig.view, 3);
            expect (looper.getState (2) == State::playing);
            expect (! rig.view->getHistory().canUndo(), "nothing to undo");
            expect (! rig.view->historyUndo());
        }

        beginTest ("Clear one loop by hand, and Clear all: Undo brings them back, Redo clears them again");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            looper.setLayerAudio (3, constant (0.2f, 48000), 2.0);
            run (rig.engine, *rig.view, 2);
            const auto sound = looper.copyLayerAudio (0);
            rig.engine.clearLoop (0);
            expect (looper.getState (0) == State::empty, "cleared at once");
            expectEquals (rig.view->getHistory().nextUndo(), juce::String ("clear loop 1"));
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::playing && same (looper.copyLayerAudio (0), sound), "loop 1 is back");
            expect (rig.view->historyRedo());
            expect (looper.getState (0) == State::empty, "cleared again");
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);

            rig.engine.clearAllLoops();
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::empty && looper.getState (3) == State::empty);
            expectEquals (rig.view->getHistory().nextUndo(), juce::String ("clear all loops"));
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::playing && looper.getState (3) == State::playing, "both back");
            expect (same (looper.copyLayerAudio (0), sound));
            expect (rig.view->historyRedo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::empty && looper.getState (3) == State::empty, "cleared again");
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (3) == State::playing);
            // (Loop 1's clear had been undone, and Clear all was a new change: nothing more.)
            expect (! rig.view->getHistory().canUndo());
        }

        beginTest ("a step that would put a loop back over newer work is skipped");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            rig.view->goTo ("Groove");
            rig.view->pickTrack (GrooveView::bass);
            auto* groove = findIn<GrooveView> (*rig.view);
            expect (groove != nullptr);
            if (groove == nullptr)
                return;
            auto& bass = rig.engine.getBass();
            auto p = bass.getPattern (bass.getSelectedPattern());
            p.steps[3].on = true;
            bass.setPattern (bass.getSelectedPattern(), p);
            groove->selectTrack (GrooveView::bass);
            groove->clearPatternNow();   // step 1: the Bass's Clear

            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            run (rig.engine, *rig.view, 2);
            rig.engine.clearLoop (0);    // step 2: loop 1 cleared
            // A loop bank puts another loop there: that's newer than the Clear.
            looper.queueLayerAudio (0, constant (0.5f, 48000), 2.0, -1.0, 0.8f, false);
            run (rig.engine, *rig.view, 2);
            expect (rig.view->historyUndo());
            expect (looper.getState (0) == State::playing);
            expectWithinAbsoluteError (looper.getLayer (0).lengthBeats.load(), 2.0, 1.0e-9, "the bank's loop is still there");
            expect (bass.getPattern (bass.getSelectedPattern()).steps[3].on, "Undo took back the Bass's Clear instead");
        }

        beginTest ("an overdub: Undo takes it back (the looper's own undo), Redo puts it back");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            looper.setLayerAudio (0, constant (0.25f, 96000), 4.0);
            busyBass (rig.engine);
            looper.getLayer (0).source = RigLooper::bass;
            rig.engine.getTransport().play();
            run (rig.engine, *rig.view, 4);
            const auto plain = looper.copyLayerAudio (0);
            looper.pressRecord (0);
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::overdubbing, "it overdubs");
            run (rig.engine, *rig.view, 250);
            looper.pressRecord (0);
            run (rig.engine, *rig.view, 3);
            expect (looper.getState (0) == State::playing);
            const auto dubbed = looper.copyLayerAudio (0);
            expect (! same (dubbed, plain), "the overdub changed it");
            expectEquals (rig.view->getHistory().nextUndo(), juce::String ("overdub on loop 1"));

            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 400);
            expect (same (looper.copyLayerAudio (0), plain), "as it was before the overdub");
            expect (rig.view->historyRedo());
            run (rig.engine, *rig.view, 2);
            expect (same (looper.copyLayerAudio (0), dubbed), "the overdub is back");
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (same (looper.copyLayerAudio (0), plain), "and gone again");
        }

        beginTest ("To looper: Undo clears the loop and switches the instrument back on, Redo the other way");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            rig.view->showView ("Play");
            rig.view->pickTrack (GrooveView::bass);
            busyBass (rig.engine);
            expectEquals (rig.view->pressToLooper(), 0);
            for (int i = 0; i < 2000 && looper.getState (0) != State::playing; ++i)
                run (rig.engine, *rig.view, 1);
            run (rig.engine, *rig.view, 1);
            expect (looper.getState (0) == State::playing);
            expect (! bassOn (rig.engine), "the bass is off once its part loops");
            expect (rig.view->getHistory().nextUndo().startsWith ("To looper"), "its own step: " + rig.view->getHistory().nextUndo());
            expectEquals (rig.view->getHistory().undoSteps(), 1, "not a take as well");
            const auto sound = looper.copyLayerAudio (0);

            expect (rig.view->undoLastTake());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::empty, "loop 1 cleared");
            expect (bassOn (rig.engine), "the bass is back on");
            expectEquals (rig.engine.handOffLayer.load(), -1);

            expect (rig.view->historyRedo());
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::playing && same (looper.copyLayerAudio (0), sound), "the loop is back");
            expect (! bassOn (rig.engine), "the bass is off again");
        }

        beginTest ("the Play screen's Clear: Undo puts it back, Redo clears it again; a change since makes it stale");
        {
            Rig rig;
            rig.view->goTo ("Groove");
            rig.view->pickTrack (GrooveView::bass);
            auto* groove = findIn<GrooveView> (*rig.view);
            expect (groove != nullptr);
            if (groove == nullptr)
                return;
            auto& bass = rig.engine.getBass();
            auto p = bass.getPattern (bass.getSelectedPattern());
            p.steps[5].on = true;
            p.steps[5].note = 7;
            bass.setPattern (bass.getSelectedPattern(), p);
            groove->clearPatternNow();
            expect (! bass.getPattern (bass.getSelectedPattern()).steps[5].on);
            expect (rig.view->historyUndo());
            expect (bass.getPattern (bass.getSelectedPattern()).steps[5].on);
            expectEquals (bass.getPattern (bass.getSelectedPattern()).steps[5].note, 7);
            expect (rig.view->historyRedo());
            expect (! bass.getPattern (bass.getSelectedPattern()).steps[5].on, "cleared again");
            groove->pressCell (0, 2);   // a new step: that's the pattern now
            expect (! rig.view->historyUndo(), "the Clear can't come back over it");
        }

        beginTest ("a song edit: Undo takes it back, Redo does it again (and the Song screen's own Undo agrees)");
        {
            Rig rig;
            rig.view->pickTrack (GrooveView::machine);
            rig.view->toSong (-1);
            rig.view->toSong (-1);
            rig.view->toSong (-1);
            auto sections = [&rig] { return (int) rigsong::sections (rig.engine.getTimeline()).size(); };
            expectEquals (sections(), 3);
            rig.view->showView ("Song");
            auto* song = findIn<SongView> (*rig.view);
            expect (song != nullptr);
            if (song == nullptr)
                return;
            song->forgetUndo();
            expect (song->deleteSection (1));
            expectEquals (sections(), 2);
            expect (rig.view->historyUndo());
            expectEquals (sections(), 3, "it's back");
            expect (rig.view->historyRedo());
            expectEquals (sections(), 2, "deleted again");
            expect (song->undo(), "the Song screen's Undo goes through the history");
            expectEquals (sections(), 3);
            expect (rig.view->getHistory().canRedo(), "so Redo knows");
            expect (! song->undo(), "nothing more to take back");
        }

        beginTest ("quantize and automation are steps too");
        {
            Rig rig;
            auto& a = rig.engine.getAutomation();
            RigAutomation::Lane lane;
            lane.tool = 0;
            lane.parameter = 0;
            lane.lengthBeats = 4.0;
            lane.points.push_back ({ 0.0, 0.5f });
            a.addLane (lane);
            expectEquals ((int) a.getLanes().size(), 1);
            expectEquals (rig.view->getHistory().nextUndo(), juce::String ("a change to the automation"));
            expect (rig.view->historyUndo());
            expectEquals ((int) a.getLanes().size(), 0);
            expect (rig.view->historyRedo());
            expectEquals ((int) a.getLanes().size(), 1);

            auto& bass = rig.engine.getBass();
            auto p = bass.getPattern (bass.getSelectedPattern());
            for (auto& s : p.steps)
                s = {};
            p.steps[1].on = true;   // off the 1/8 grid
            bass.setPattern (bass.getSelectedPattern(), p);
            auto& q = rig.engine.getQuantizer();
            q.settings.grid = quantize::quarter;
            q.settings.strength = 1.0f;
            const auto said = q.apply (RigQuantizer::bass);
            if (q.canUndo())
            {
                const auto after = bass.getPattern (bass.getSelectedPattern());
                expect (rig.view->getHistory().nextUndo().startsWith ("quantize"), rig.view->getHistory().nextUndo());
                expect (rig.view->historyUndo());
                expect (bass.getPattern (bass.getSelectedPattern()).steps[1].on, "back off the grid");
                expect (rig.view->historyRedo());
                const auto again = bass.getPattern (bass.getSelectedPattern());
                bool equal = true;
                for (size_t i = 0; i < again.steps.size(); ++i)
                    equal = equal && again.steps[i].on == after.steps[i].on;
                expect (equal, "quantized again the same way");
            }
            else
                logMessage ("(the bass quantize moved nothing: " + said + ")");
        }

        beginTest ("Sing free's fit to the beat is its own step, after the take");
        {
            Rig rig;
            auto& looper = rig.engine.getLooper();
            auto& lab = rig.engine.getLoopLab();
            lab.renderInline = true;
            rig.view->showView ("Play");
            rig.view->pickTrack (GrooveView::vocoder);
            rig.view->setSingFree (true);
            rig.engine.getTransport().play();
            expectEquals (rig.view->pressRecTake(), 0);
            for (int i = 0; i < 400 && looper.getState (0) != State::recording; ++i)
                run (rig.engine, *rig.view, 1);
            run (rig.engine, *rig.view, 300);
            looper.pressRecord (0);   // as Rec again does: it closes at the end of the bar
            for (int i = 0; i < 1000 && looper.getState (0) != State::playing; ++i)
                run (rig.engine, *rig.view, 1);
            run (rig.engine, *rig.view, 3);
            expect (lab.isOpen (0), "fitted in the Loop Lab");
            auto& h = rig.view->getHistory();
            expectEquals (h.nextUndo(), juce::String ("fit to the beat on loop 1"));
            const auto fitted = lab.get (0).plan.targetBeats;
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (lab.isOpen (0) && ! lab.canUndo (0), "the fit is taken back");
            expectEquals (h.nextUndo(), juce::String ("take on loop 1"), "the take is the step before");
            expect (rig.view->historyRedo());
            run (rig.engine, *rig.view, 2);
            expectWithinAbsoluteError (lab.get (0).plan.targetBeats, fitted, 1.0e-9, "fitted again");
            expect (rig.view->historyUndo());
            run (rig.engine, *rig.view, 2);
            expect (rig.view->historyUndo(), "then the take");
            run (rig.engine, *rig.view, 2);
            expect (looper.getState (0) == State::empty);
        }

        beginTest ("the Sampler's Remove: a step, and the screen's own Undo goes through the history");
        {
            Rig rig;
            rig.view->goTo ("Sampler");
            auto* screen = findIn<SamplerScreen> (*rig.view);
            expect (screen != nullptr);
            if (screen != nullptr)
            {
                auto& project = rig.engine.getMpc().getProject();
                project.currentBank().pads[2].audio = crate::share (sine (4000, 1, 0.5f, 220.0));
                project.currentBank().pads[2].name = "Snare";
                project.changed();
                screen->pressPad (2);
                screen->pressRemovePad();
                expect (project.currentBank().pads[2].isEmpty());
                expectEquals (rig.view->getHistory().nextUndo(), juce::String ("remove pad A03"));
                expect (rig.view->historyUndo());
                expectEquals (project.currentBank().pads[2].name, juce::String ("Snare"), "back");
                expect (rig.view->historyRedo());
                expect (project.currentBank().pads[2].isEmpty(), "removed again");
                expect (screen->canUndoRemovePad());
                screen->pressRemovePad();   // (already empty: says so, no step)
                screen->pressUndoRemovePad();
                expectEquals (project.currentBank().pads[2].name, juce::String ("Snare"), "the screen's Undo");
                expect (rig.view->getHistory().canRedo(), "went through the history, so Redo can do it again");
            }
        }

        beginTest ("a chord edit (a pad, the key): Undo puts it back, Redo does it again");
        {
            Rig rig;
            auto& chords = rig.engine.getChords();
            const auto padBefore = chords.getPad (0).notes;
            chords.setPad (0, { 62, 65, 69 }, "Dm");
            expect (rig.view->historyUndo());
            expect (chords.getPad (0).notes == padBefore, "the pad's chord is back");
            expect (rig.view->historyRedo());
            expectEquals (chords.getPad (0).name, juce::String ("Dm"));
            expect (chords.getPad (0).notes == juce::Array<int> ({ 62, 65, 69 }));
            // Changed again since Undo: Redo can't put the old one over it.
            expect (rig.view->historyUndo());
            chords.setPad (0, { 60, 64, 67, 71 });
            expect (rig.view->historyUndo(), "the new edit is a step of its own");
            expect (chords.getPad (0).notes == padBefore);
        }

        beginTest ("a new show starts the history afresh");
        {
            Rig rig;
            rig.engine.getLooper().setLayerAudio (0, constant (0.3f, 96000), 4.0);
            run (rig.engine, *rig.view, 2);
            rig.engine.clearLoop (0);
            expect (rig.view->getHistory().canUndo());
            rig.view->newSong (false, false);
            expect (! rig.view->getHistory().canUndo() && ! rig.view->getHistory().canRedo(), "nothing to take back into the new song");
            expect (! rig.view->historyUndo());
        }
    }
};

static RigHistoryTests rigHistoryTests;
