#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigPanels.h"
#include "RigSong.h"
#include "RigView.h"
#include "GrooveView.h"
#include "SongView.h"
#include "HelpPanel.h"

// Capture and compose: a track's pattern (or a take played over the beat) goes into the song from
// any screen, the rail does something on every view, a song section can be deleted and brought
// back, and the Guide walks through the whole tool.
class RigSongCaptureTests : public juce::UnitTest
{
public:
    RigSongCaptureTests() : juce::UnitTest ("Rig: capture into the song, the rail, delete, the Guide", "Rig") {}

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

    static int sections (RigEngine& e) { return (int) rigsong::sections (e.getTimeline()).size(); }

    static void run (RigEngine& engine, int blocks)
    {
        juce::AudioBuffer<float> out (2, 480);
        for (int i = 0; i < blocks; ++i)
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
    }

    static void snapshot (juce::Component& view, const juce::String& name)
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_CAPTURE_SNAPSHOT", {});
        if (path.isEmpty())
            return;
        const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
        juce::File file (path + name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    void runTest() override
    {
        beginTest ("To Song puts what a track plays into a section, a new one, or every one");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::machine);
            const int drumsLane = RigTimeline::laneForStrip (RigEngine::drumsStrip);

            expectEquals (view.toSong (-1), 0, "the first section");
            expectEquals (sections (engine), 1);
            const int bi = rigsong::boxBlock (engine.getTimeline(), 0, drumsLane);
            expect (bi >= 0, "Drums plays in A");
            expectEquals (engine.getTimeline().getBlocks()[(size_t) bi].clip, rigsong::currentClip (engine, drumsLane), "its pattern as it is now");

            view.pickTrack (GrooveView::bass);
            const int bassLane = RigTimeline::laneForStrip (RigEngine::bassStrip);
            expectEquals (view.toSong (-1), 1, "a new section B with just the bass");
            expect (rigsong::boxBlock (engine.getTimeline(), 1, bassLane) >= 0);
            expect (rigsong::boxBlock (engine.getTimeline(), 1, drumsLane) < 0, "the drums aren't in B");
            expectEquals (view.toSong (-2), 0, "into every section");
            expect (rigsong::boxBlock (engine.getTimeline(), 0, bassLane) >= 0, "the bass is in A now too");
            expectEquals (sections (engine), 2, "no new sections for 'every'");
        }

        beginTest ("Rec take records the picked track onto the next empty loop, and To Song puts the take in");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::vocoder);
            auto& looper = engine.getLooper();

            const int layer = view.pressRecTake();
            expectEquals (layer, 0, "loop 1 is the first empty one");
            expectEquals (looper.getLayer (0).source.load(), (int) RigLooper::vocoder, "it records the vocoder");
            run (engine, 1);
            const auto st = looper.getState (0);
            expect (st == RigLooper::State::armed || st == RigLooper::State::recording, "armed for the next bar");
            snapshot (view, "-rec-take");

            // Run into the recording, then stop it: it closes at the end of the bar.
            for (int i = 0; i < 400 && looper.getState (0) != RigLooper::State::recording; ++i)
                run (engine, 1);
            expect (looper.getState (0) == RigLooper::State::recording);
            view.pressRecTake();
            expectEquals (view.getTakeLayer(), -1);
            for (int i = 0; i < 800 && looper.getState (0) != RigLooper::State::playing; ++i)
                run (engine, 1);
            expect (looper.getState (0) == RigLooper::State::playing, "the take plays round: state " + juce::String ((int) looper.getState (0))
                    + " beat " + juce::String (engine.getTransport().getPositionInBeats()) + " playing " + juce::String ((int) engine.getTransport().isPlaying()));
            expectEquals (view.lastTakeOf (GrooveView::vocoder), 0);

            expectEquals (view.toSong (-1, true), 0, "the take as the first section");
            expect (rigsong::boxBlock (engine.getTimeline(), 0, RigTimeline::laneForLoop (0)) >= 0, "loop 1 plays in A");

            // A second take goes onto the next empty loop.
            expectEquals (view.pressRecTake(), 1);
        }

        beginTest ("the rail: a click shows the track on every view, a double-click opens the whole tool");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.pickTrack (GrooveView::machine);
            view.toSong (-1);
            view.pickTrack (GrooveView::bass);
            view.toSong (-1);

            view.showView ("Song");
            auto* song = find<SongView> (view);
            expect (song != nullptr);
            song->select (-1, -1);
            view.railClicked (GrooveView::bass);
            expectEquals (view.getView(), juce::String ("Song"), "stays on Song");
            expectEquals (song->getSelectedLane(), RigTimeline::laneForStrip (RigEngine::bassStrip), "the bass's row lights up");
            expectEquals (song->getSelectedSection(), 0);
            snapshot (view, "-song-rail");

            view.railClicked (GrooveView::machine);
            expectEquals (song->getSelectedLane(), RigTimeline::laneForStrip (RigEngine::drumsStrip));

            view.railDoubleClicked (GrooveView::vocoder);
            expectEquals (view.getView(), juce::String ("Edit"), "the whole tool");
            expectEquals (view.getPage(), juce::String ("Vocoder"));

            view.goTo ("Setup");
            view.railClicked (GrooveView::bass);
            expectEquals (view.getView(), juce::String ("Play"), "from Settings, a track goes to Play");
        }

        beginTest ("a section can be deleted (button, key) and Undo brings it back");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.pickTrack (GrooveView::machine);
            view.toSong (-1);
            view.toSong (-1);
            view.toSong (-1);
            expectEquals (sections (engine), 3);
            view.showView ("Song");
            auto* song = find<SongView> (view);
            // (The song's timer sees each change: here, by hand.)
            song->forgetUndo();

            song->select (1, -1);
            expect (song->deleteSection (1));
            expectEquals (sections (engine), 2);
            expect (song->undo(), "Undo");
            expectEquals (sections (engine), 3, "it's back");

            song->select (2, -1);
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
            expectEquals (sections (engine), 2, "the Delete key deletes the picked section");
            expect (view.keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)));
            expectEquals (sections (engine), 3, "Cmd+Z on Song brings it back");
            expect (! song->undo(), "nothing more to take back");
        }

        beginTest ("one-click deletes: a box's x empties it, a track's x takes it out of the song, Delete empties a picked box");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.pickTrack (GrooveView::machine);
            view.toSong (-1);
            view.toSong (-1);
            expectEquals (sections (engine), 2);
            view.showView ("Song");
            auto* song = find<SongView> (view);
            song->forgetUndo();
            auto& tl = engine.getTimeline();
            const int lineIn = RigTimeline::laneForStrip (RigEngine::inputStrip);
            const int drums = RigTimeline::laneForStrip (RigEngine::drumsStrip);
            rigsong::setBox (tl, 0, lineIn, -1);
            rigsong::setBox (tl, 1, lineIn, -1);
            rigsong::setBox (tl, 0, drums, -1);
            rigsong::setBox (tl, 1, drums, -1);
            song->refresh();
            song->forgetUndo();
            const auto shows = [song] (int lane)
            {
                const auto lanes = song->shownLanes();
                return std::find (lanes.begin(), lanes.end(), lane) != lanes.end();
            };
            expect (shows (lineIn), "Line in is on the board");

            // The box's x: that box only.
            expect (song->emptyBox (0, drums));
            expectEquals (rigsong::boxBlock (tl, 0, drums), -1);
            expect (rigsong::boxBlock (tl, 1, drums) >= 0, "the other section keeps its box");
            expect (! song->emptyBox (0, drums), "already empty");
            expectEquals (sections (engine), 2, "no section went");

            // The track's x: out of the whole song, and its row goes.
            expect (song->removeLaneFromSong (lineIn));
            expect (! tl.laneUsed (lineIn));
            expect (! shows (lineIn), "Line in's row is gone");
            expectEquals (sections (engine), 2);
            expect (song->undo(), "Undo");
            expect (rigsong::boxBlock (tl, 0, lineIn) >= 0 && rigsong::boxBlock (tl, 1, lineIn) >= 0, "Undo brings Line in back");
            expect (shows (lineIn));

            // Delete (button or key) with a box picked: the box goes, not its section.
            song->select (1, drums);
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::deleteKey)));
            expectEquals (rigsong::boxBlock (tl, 1, drums), -1, "the picked box is empty");
            expectEquals (sections (engine), 2, "its section stays");
            song->select (1, drums);
            song->pressDelete();
            expectEquals (sections (engine), 2, "an empty picked box: nothing more goes");
        }

        beginTest ("the Guide is always at the top and walks through the whole tool on its real screens");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            auto* bar = find<rigpanels::ScreenBar> (view);
            expect (bar != nullptr && bar->help.isVisible());
            expectEquals (bar->help.text, juce::String ("Guide"));

            view.showHelp (true);   // (as its button does)
            auto* help = view.getHelp();
            expect (help != nullptr && help->isTour(), "the walkthrough opens");
            expect (HelpPanel::tour().size() >= 15, "it covers the whole tool");
            expect (help->getBounds().getWidth() < view.getWidth() / 2, "a card beside the screen, not over it");

            int vocoderStop = -1, songStop = -1;
            for (int i = 0; i < (int) HelpPanel::tour().size(); ++i)
            {
                if (HelpPanel::tour()[(size_t) i].place == "Edit:Vocoder" && vocoderStop < 0)
                    vocoderStop = i;
                if (HelpPanel::tour()[(size_t) i].place == "Song" && songStop < 0)
                    songStop = i;
            }
            help->showStop (vocoderStop);
            expectEquals (view.getPage(), juce::String ("Vocoder"), "the stop shows its screen");
            expectEquals (view.getTrack(), (int) GrooveView::vocoder);
            snapshot (view, "-guide-vocoder");
            help->showStop (songStop);
            expectEquals (view.getView(), juce::String ("Song"));

            // Closed and opened again: the same stop.
            view.showHelp (false);
            view.showHelp (true);
            expectEquals (view.getHelp()->getStop(), songStop);
        }
    }
};

static RigSongCaptureTests rigSongCaptureTests;
