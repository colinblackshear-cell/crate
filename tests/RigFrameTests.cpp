#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "RigPanels.h"
#include "GrooveView.h"
#include "SamplerScreen.h"

// The window's frame: the track rail down the left, the four views along the top (Play, Edit,
// Song, Mix), the Sounds drawer and Settings. Every tool is still there, one way in.
class RigFrameTests : public juce::UnitTest
{
public:
    RigFrameTests() : juce::UnitTest ("Rig frame: track rail and views", "Rig") {}

    void runTest() override
    {
        beginTest ("the views: Play is the Groove, Song, Mix is the Mixer, F1-F4 and the view buttons");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            const juce::StringArray pages { "Groove", "", "Song", "Mixer" };
            for (int i = 0; i < 4; ++i)
            {
                expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F1Key + i)));
                expectEquals (view.getView(), rigpanels::ScreenBar::views()[i]);
                if (pages[i].isNotEmpty())
                    expectEquals (view.getPage(), pages[i]);
            }
            view.goTo ("Guide");
            expectEquals (view.getView(), juce::String(), "nothing lit on the Guide");
            expect (! view.getRail().isVisible(), "the Guide fills the window");
            view.showView ("Play");
            expect (view.getRail().isVisible() && view.getRail().getWidth() > 150, "the rail is back");
        }

        beginTest ("Edit shows the picked track in full, and picking another track on Edit shows that one");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            const std::pair<int, const char*> edits[] = { { GrooveView::drums, "MPC" }, { GrooveView::machine, "Drum machine" },
                                                          { GrooveView::bass, "Bass" }, { GrooveView::synth, "Synth" },
                                                          { GrooveView::vocoder, "Vocoder" }, { GrooveView::sampler, "Sampler" },
                                                          { GrooveView::loops, "Looper" }, { GrooveView::lineIn, "Effects" } };
            view.showView ("Play");
            for (auto [track, place] : edits)
            {
                view.pickTrack (track);
                expectEquals (view.getPage(), juce::String ("Groove"), "picking a track on Play stays on Play");
            }
            view.showView ("Edit");
            for (auto [track, place] : edits)
            {
                view.pickTrack (track);
                expectEquals (view.getPlace(), juce::String (place), GrooveView::trackName (track));
                expectEquals (view.getView(), juce::String ("Edit"), GrooveView::trackName (track));
                expectEquals (view.getTrack(), track);
            }

            // A tool's screen reached another way picks its track, so the keys and Edit follow it.
            view.goTo ("Bass");
            expectEquals (view.getTrack(), (int) GrooveView::bass);
            view.goTo ("Sampler editor");
            expectEquals (view.getTrack(), (int) GrooveView::sampler);
            expectEquals (view.getView(), juce::String ("Edit"));
            view.goTo ("Mixer");
            expectEquals (view.getTrack(), (int) GrooveView::sampler, "the Mixer leaves the track alone");
        }

        beginTest ("the Sounds drawer opens over any view and closes again; Settings is the set-up");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Song");
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F5Key)));
            expect (view.isSoundsOpen());
            expectEquals (view.getPage(), juce::String ("Song"), "the view stays underneath");
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (! view.isSoundsOpen());
            view.goTo ("Sounds");
            expect (view.isSoundsOpen(), "an old link to Sounds opens the drawer");
            view.showSounds (false);
            // (Windows need an app with a screen; the test runner is a console program.)
            if (juce::JUCEApplicationBase::getInstance() != nullptr && ! juce::Desktop::getInstance().getDisplays().displays.isEmpty())
            {
                view.showSounds (true);
                view.popOutTab ("Sounds");
                expect (view.isInOwnWindow ("Sounds") && ! view.isSoundsOpen(), "Sounds can still go on a second screen");
                view.bringBack ("Sounds");
            }
            view.showSounds (true);
            expect (view.isSoundsOpen());
            view.showSounds (false);

            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F6Key)));
            expectEquals (view.getPage(), juce::String ("Setup"));
        }
    }
};

static RigFrameTests rigFrameTests;
