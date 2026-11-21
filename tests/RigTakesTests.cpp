#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigPanels.h"
#include "RigSong.h"
#include "RigTakes.h"
#include "RigView.h"
#include "GrooveView.h"
#include "SongView.h"

// Song world: Rec into records a track into a section of the song, from its top, and with Cycle
// every time round is a take of its own that sits in time with the song; the takes go onto a loop
// and into that section; the song strip shows where the next take goes; and the Song screen's
// Add a layer with opens an instrument ready to record over its section.
class RigTakesTests : public juce::UnitTest
{
public:
    RigTakesTests() : juce::UnitTest ("Rig: Rec into the song, takes, the song strip", "Rig") {}

    template <typename T>
    static T* find (juce::Component& c)
    {
        if (auto* t = dynamic_cast<T*> (&c))
            return t;
        for (auto* child : c.getChildren())
            if (auto* t = find<T> (*child))
                return t;
        return nullptr;
    }

    static void run (RigEngine& engine, int blocks)
    {
        juce::AudioBuffer<float> out (2, 480);
        for (int i = 0; i < blocks; ++i)
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
    }

    static void snapshot (juce::Component& view, const juce::String& name)
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TAKES_SNAPSHOT", {});
        if (path.isEmpty())
            return;
        const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
        juce::File file (path + name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    // Feeds RigTakes a song that plays from `from` to `to` (beats) in blocks of `block` samples;
    // each sample of the source is the beat it was played on, so a take shows where it came from.
    static double play (RigTakes& takes, double from, double to, int block, double rate, double bpm)
    {
        const double bps = bpm / (60.0 * rate);
        juce::AudioBuffer<float> source (2, block);
        double beat = from;
        while (beat < to - 1.0e-9)
        {
            const int n = juce::jmin (block, (int) std::ceil ((to - beat) / bps - 1.0e-6));
            for (int i = 0; i < n; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    source.setSample (ch, i, (float) (beat + i * bps));
            takes.process (&source, n, beat, bpm, true, rate);
            beat += n * bps;
        }
        return beat;
    }

    void runTest() override
    {
        beginTest ("every time round the section is a take, starting at its top wherever you started");
        {
            RigTakes takes;
            const double rate = 1000.0, bpm = 60.0;   // a beat is 1000 samples
            // Bars 3-4 (beats 8-16) of a 4/4 song.
            expect (takes.start (2, 1, 2.0, 2.0, true, rate, bpm, 4));
            expect (takes.getState() == RigTakes::State::waiting);
            expectEquals (takes.nextPassNumber(), 1);

            // Played in from bar 1, in blocks that don't land on the section's top.
            play (takes, 4.0, 16.0, 128, rate, bpm);
            expect (takes.getState() != RigTakes::State::idle, "still going at the very end of the section");
            // The song cycles: back to the top for a second time round, then stop half way.
            play (takes, 8.0, 12.0, 128, rate, bpm);
            expectEquals (takes.passNumber(), 2);
            takes.stop();
            juce::AudioBuffer<float> silence (2, 64);
            silence.clear();
            takes.process (&silence, 64, 12.0, bpm, true, rate);
            expect (takes.getState() == RigTakes::State::idle);
            expectEquals (takes.collect(), 2, "two takes");
            expect (! takes.isActive(), "done");

            const auto* set = takes.setFor (2);
            expect (set != nullptr);
            if (set == nullptr)
                return;
            expectEquals ((int) set->takes.size(), 2);
            expectEquals (set->playing, 1, "the newest plays");
            const auto& one = set->takes[0];
            expectEquals (one.audio.getNumSamples(), 8000, "a take is the section's length");
            expectEquals (one.beats, 8.0);
            expect (! one.partial);
            expectWithinAbsoluteError (one.audio.getSample (0, 100), 8.1f, 0.002f, "it starts at the section's top");
            expectWithinAbsoluteError (one.audio.getSample (1, 4000), 12.0f, 0.002f);
            expectWithinAbsoluteError (one.audio.getSample (0, 7900), 15.9f, 0.002f);
            const auto& two = set->takes[1];
            expect (two.partial, "cut short");
            expectEquals (two.number, 2);
            expectWithinAbsoluteError (two.audio.getSample (0, 2000), 10.0f, 0.002f);
            expectEquals (two.audio.getSample (0, 6000), 0.0f, "the rest is silence");

            // Trying again over the same bars adds to the same layer's takes.
            expectEquals (takes.layerFor (1, 2.0, 2.0), 2);
            expect (takes.start (2, 1, 2.0, 2.0, false, rate, bpm, 4));
            expectEquals (takes.nextPassNumber(), 3);
            play (takes, 8.0, 20.0, 100, rate, bpm);
            expect (takes.getState() == RigTakes::State::idle, "without Cycle it's one time through, then the song plays on");
            expectEquals (takes.collect(), 1);
            expectEquals ((int) takes.setFor (2)->takes.size(), 3);
            expectEquals (takes.setFor (2)->takes.back().number, 3);

            // Picking and deleting.
            expect (! takes.deleteTake (2, 2), "not the one playing");
            expect (takes.deleteTake (2, 0));
            expectEquals (takes.setFor (2)->playing, 1);
            takes.keepOnlyPlaying (2);
            expectEquals ((int) takes.setFor (2)->takes.size(), 1);
            expectEquals (takes.setFor (2)->takes[0].number, 3);
        }

        beginTest ("a stop before a bar keeps nothing; waiting is called off at once; too long a section says why");
        {
            RigTakes takes;
            expect (takes.start (0, 1, 0.0, 4.0, true, 1000.0, 60.0, 4));
            play (takes, 0.0, 2.0, 100, 1000.0, 60.0);
            takes.stop();
            juce::AudioBuffer<float> silence (2, 64);
            takes.process (&silence, 64, 2.0, 60.0, true, 1000.0);
            expectEquals (takes.collect(), 0, "half a bar isn't a take");

            expect (takes.start (0, 1, 4.0, 4.0, true, 1000.0, 60.0, 4));
            takes.stop();
            expect (takes.getState() == RigTakes::State::idle, "nothing started yet: called off straight away");
            takes.collect();
            expect (! takes.isActive());

            juce::String why;
            expect (! takes.start (0, 1, 0.0, 400.0, true, 48000.0, 60.0, 4, &why));
            expect (why.contains ("minutes"), why);
        }

        beginTest ("Rec into: the vocoder into section B, round and round, then the takes are on a loop in B");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            auto& tl = engine.getTimeline();
            rigsong::addSection (tl, "Intro", 2);
            rigsong::addSection (tl, "Drop", 2);
            tl.changed();
            // Something in the song already (the drum machine through both sections).
            rigsong::setBox (tl, 0, RigTimeline::laneForStrip (RigEngine::drumsStrip), 0);
            rigsong::setBox (tl, 1, RigTimeline::laneForStrip (RigEngine::drumsStrip), 0);
            view.showView ("Edit");
            view.pickTrack (GrooveView::vocoder);

            auto* footer = find<rigpanels::Footer> (view);
            expect (footer != nullptr);
            if (footer == nullptr)
                return;
            footer->tick();
            expect (! footer->isSlim(), "the song strip");
            expect (footer->recButton.isVisible() && footer->cycleButton.isVisible() && footer->songButton.isVisible());

            view.setRecTarget (1, -1);
            expectEquals (view.recTargetSection(), 1);
            expectEquals (view.recTargetLayer(), 0, "the first free loop");
            const int layer = view.pressRecInto();
            expectEquals (layer, 0);
            auto& takes = engine.getTakes();
            expect (takes.isActive());
            run (engine, 1);
            expect (engine.getTransport().isPlaying(), "the song starts at B's top");
            expect (tl.cycle.load(), "B plays round");
            expectWithinAbsoluteError (tl.cycleStart.load(), 2.0, 1.0e-9);
            expectEquals (footer->getRecInto().section, 1);
            expect (footer->recButton.text == "Stop take");

            // Two bars at 120 BPM is 4 seconds: 400 blocks. Twice round and a bit.
            run (engine, 20);
            expect (takes.getState() == RigTakes::State::recording, "recording from B's top");
            snapshot (view, "-recording");
            // Twice round and a bit (however long two bars are at this tempo).
            for (int i = 0; i < 4000 && takes.getTakesMade() < 2; ++i)
            {
                run (engine, 1);
                engine.serviceTakes();
            }
            run (engine, 30);
            engine.serviceTakes();
            const auto* set = takes.setFor (0);
            expect (set != nullptr && set->takes.size() >= 2, "a take every time round");
            expectEquals (view.pressRecTake(), -1, "Rec take on the rail stops it too");
            run (engine, 2);
            engine.serviceTakes();
            expect (! takes.isActive());
            set = takes.setFor (0);
            expect (set != nullptr && set->takes.size() >= 2);
            expect (engine.getLooper().getState (0) == RigLooper::State::playing, "the newest take plays on loop 1");
            expectWithinAbsoluteError (engine.getLooper().getLayer (0).lengthBeats.load(), 8.0, 1.0e-6, "B's length");
            expect (engine.getLoopName (0).startsWith ("Vocoder take"), engine.getLoopName (0));
            expect (rigsong::boxBlock (tl, 1, RigTimeline::laneForLoop (0)) >= 0, "in B");
            expect (rigsong::boxBlock (tl, 0, RigTimeline::laneForLoop (0)) < 0, "and only in B");

            // Pick an older one.
            expect (engine.useTake (0, 0));
            expectEquals (takes.setFor (0)->playing, 0);
            expect (engine.getLoopName (0) == "Vocoder take 1");

            // The next Rec into for the vocoder over B adds takes to the same loop; another track
            // gets a new layer.
            expectEquals (view.recTargetLayer(), 0);
            view.pickTrack (GrooveView::synth);
            expectEquals (view.recTargetLayer(), 1, "a new layer for the synth");
            snapshot (view, "-takes");
        }

        beginTest ("Rec take with a song records into it; Change > Free take is the old way");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::bass);
            auto& tl = engine.getTimeline();
            rigsong::addSection (tl, "Verse", 4);
            tl.changed();
            expectEquals (view.pressRecTake(), 0);
            expect (engine.getTakes().isActive(), "into the song");
            expectEquals (engine.getTakes().getSource(), (int) RigLooper::bass);
            view.pressRecTake();
            run (engine, 2);
            engine.serviceTakes();
            expect (! engine.getTakes().isActive(), "stopped before anything: called off");
        }

        beginTest ("the Song screen: Add a layer with opens the instrument ready over the picked section");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            auto& tl = engine.getTimeline();
            rigsong::addSection (tl, "Intro", 4);
            rigsong::addSection (tl, "Build", 4);
            rigsong::addSection (tl, "Drop", 8);
            rigsong::setBox (tl, 0, RigTimeline::laneForStrip (RigEngine::drumsStrip), 0);
            tl.changed();
            view.showView ("Song");
            auto* song = find<SongView> (view);
            expect (song != nullptr);
            if (song == nullptr)
                return;
            song->refresh();
            song->resized();
            expectEquals (song->layerButtons.size(), 7);
            expect (song->layerButtons[4]->isVisible());
            song->select (2, -1);
            snapshot (view, "-song");
            song->layerButtons[4]->onClick();   // Vocoder
            expect (view.getView() == "Edit", view.getView());
            expectEquals (view.getTrack(), (int) GrooveView::vocoder);
            expectEquals (view.recTargetSection(), 2);
            expect (tl.cycle.load(), "the Drop plays round");
            expectWithinAbsoluteError (tl.cycleStart.load(), 8.0, 1.0e-9);
            run (engine, 10);
            expect (engine.getTransport().isPlaying());
            auto* footer = find<rigpanels::Footer> (view);
            if (footer != nullptr)
                footer->tick();
            snapshot (view, "-ready");

            // Cycle off: one time through, and the song plays on.
            view.toggleCycle();
            expect (! tl.cycle.load());
            expect (! view.isCycleOn());
        }
    }
};

static RigTakesTests rigTakesTests;
