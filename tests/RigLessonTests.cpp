#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "RigLesson.h"
#include "GrooveView.h"
#include "GuideView.h"
#include "HelpPanel.h"
#include "RigPanels.h"

// The phone's way in, on the Mac: the Guide's three ways in (Take the 2-minute tour, Show me: make a
// beat, Start making), and the Make a beat lesson over the drum machine: kick, snare, hats glow on
// the real grid, Rec puts the beat on the next loop layer, and the last card says which.
class RigLessonTests : public juce::UnitTest
{
public:
    RigLessonTests() : juce::UnitTest ("Rig: the phone's welcome and Make a beat lesson", "Rig") {}

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
        const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_LESSON_SNAPSHOT", {});
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
        beginTest ("the Guide's three ways in, as on the phone's title");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Guide");
            auto* guide = view.getGuide();
            expect (guide != nullptr);
            const char* words[] = { "Take the 2-minute tour", "Show me: make a beat", "Start making" };
            for (int i = 0; i < 3; ++i)
            {
                auto* b = guide->wayIn (i);
                expect (b != nullptr && b->isVisible() && b->getWidth() > 120, words[i]);
                if (b != nullptr)
                    expectEquals (b->getButtonText(), juce::String (words[i]));
            }
            snapshot (view, "-guide");

            guide->wayIn (0)->onClick();
            expect (view.getLesson() == nullptr);
            bool touring = false;
            for (auto* c : view.getChildren())
                if (auto* h = dynamic_cast<HelpPanel*> (c))
                    touring = h->isTour() && h->getStop() == 0;
            expect (touring, "the tour starts at its first stop");

            guide->wayIn (2)->onClick();
            expectEquals (view.getView(), juce::String ("Play"), "Start making goes to Play");
        }

        beginTest ("the Rec arrow opens the Rec setup: count-in, length, click, and where Rec lands");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            view.showRecMenu();
            auto* setup = view.getRecSetup();
            expect (setup != nullptr && setup->isVisible());
            if (setup == nullptr)
                return;
            expect (setup->getBottom() <= view.getDock().getY() + 8 && setup->getY() >= 0, "over the bottom bar, on screen");
            snapshot (view, "-rec-setup");
            setup->countIns[2]->onClick();
            expectEquals (view.getRecCountIn(), 2);
            setup->lengths[5]->onClick();
            expectEquals (view.getRecLength(), 0, "Free");
            setup->lands[2]->onClick();
            expect (view.getRecLands() == RigView::RecLands::sample);
            expect (setup->lands[2]->on && ! setup->lands[0]->on);
            setup->lands[4]->onClick();
            expect (view.getRecLands() == RigView::RecLands::pattern, "the fifth row: the pads into the MPC pattern");
            view.showRecMenu();   // the arrow again closes it
            expect (view.getRecSetup() == nullptr);

            // Rec with the pads into the MPC pattern: the MPC records, Rec again stops it
            view.pressRec();
            juce::AudioBuffer<float> out (2, 480);
            for (int b = 0; b < 4; ++b)
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expect (engine.getMpc().getSequencer().isRecording(), "Rec records the pads into the MPC pattern");
            view.pressRec();
            for (int b = 0; b < 4; ++b)
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expect (! engine.getMpc().getSequencer().isRecording(), "Rec again stops it");
            expect (engine.getLooper().getState (0) == RigLooper::State::empty, "nothing went on a loop layer");
        }

        beginTest ("Make a beat: kick, snare, hats glow on the drum machine, Rec lands it on layer 1");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Guide");
            view.getGuide()->wayIn (1)->onClick();   // a blank song: no need to ask

            auto* lesson = view.getLesson();
            expect (lesson != nullptr && lesson->isVisible(), "the coach card shows");
            if (lesson == nullptr)
                return;
            auto* groove = find<GrooveView> (view);
            expect (groove != nullptr);
            expectEquals (view.getView(), juce::String ("Play"));
            expectEquals (groove->getTrack(), (int) GrooveView::machine, "the drum machine is picked");
            expectEquals (groove->getGuideRow(), 1, "the kick row glows");
            for (int step = 0; step < 16; ++step)
                expect (engine.getDrums().getEditedPattern().hits[0][(size_t) step] == 0, "an empty grid: nothing is filled in for you");
            expect (groove->getGuideMask() == ((1u << 0) | (1u << 10)), "steps 1 and 11");
            run (engine, 2);
            expect (groove->isGoing (GrooveView::machine), "the pattern loops as you click");
            expect (engine.getTransport().metronome.load(), "with the click");
            snapshot (view, "-lesson-kick");

            // Nothing is filled in: the glow is only a glow. Click a glowing step yourself.
            groove->pressCell (1, 0);
            lesson->primary.onClick();
            expectEquals (lesson->phase(), (int) RigLesson::snare);
            expectEquals (groove->getGuideRow(), 2);
            lesson->primary.onClick();
            expectEquals (lesson->phase(), (int) RigLesson::hats);
            expectEquals (groove->getGuideRow(), 10, "the closed hat row");
            lesson->primary.onClick();
            expectEquals (lesson->phase(), (int) RigLesson::rec);
            expectEquals (groove->getGuideRow(), -1);
            expect (lesson->words().body.contains ("layer 1"), lesson->words().body);
            snapshot (view, "-lesson-rec");

            lesson->primary.onClick();   // the card's Rec: the bottom bar's
            auto& looper = engine.getLooper();
            expectEquals (looper.getLayer (0).source.load(), (int) RigLooper::drums, "it records the drum machine");
            run (engine, 1);
            view.tickLesson();
            expect (lesson->isRecBusy(), "counting in");
            for (int i = 0; i < 3000 && looper.getState (0) != RigLooper::State::playing; ++i)
                run (engine, 10);
            expect (looper.getState (0) == RigLooper::State::playing, "the take plays round");
            view.tickLesson();
            expectEquals (lesson->phase(), (int) RigLesson::landed);
            expectEquals (lesson->landedLayer(), 1);
            snapshot (view, "-lesson-landed");

            expectEquals (lesson->secondary.text, juce::String ("Done"));
            view.endLesson();   // (what Done does, once the click is over)
            expect (view.getLesson() == nullptr, "Done ends it");
            expectEquals (groove->getGuideRow(), -1);
            expect (! engine.getTransport().metronome.load(), "the click as it was");
        }
    }
};

static RigLessonTests rigLessonTests;
