#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "RigNavigation.h"
#include "RigSongStart.h"
#include "GrooveView.h"
#include "LooperView.h"
#include "SamplerScreen.h"

// Back and forward through the screens, and starting a song from nothing (New song, New
// pattern) from the top bar.
class RigNavigationTests : public juce::UnitTest
{
public:
    RigNavigationTests() : juce::UnitTest ("Rig navigation and new song", "Rig") {}

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

    void runTest() override
    {
        beginTest ("history: back, forward, and somewhere new drops what was ahead");
        {
            RigNavigation n;
            expect (! n.canGoBack() && ! n.canGoForward());
            expect (n.back().isEmpty() && n.forward().isEmpty());
            n.visit ("Groove");
            n.visit ("Groove");   // the same place again is nothing new
            n.visit ("MPC");
            n.visit ("Looper");
            expectEquals (n.previous(), juce::String ("MPC"));
            expectEquals (n.back(), juce::String ("MPC"));
            expectEquals (n.back(), juce::String ("Groove"));
            expect (! n.canGoBack() && n.canGoForward());
            expectEquals (n.next(), juce::String ("MPC"));
            expectEquals (n.forward(), juce::String ("MPC"));
            n.visit ("Bass");
            expect (! n.canGoForward(), "Looper was ahead and goes");
            expectEquals (n.back(), juce::String ("MPC"));
            expectEquals (n.forward(), juce::String ("Bass"));

            RigNavigation many;
            for (int i = 0; i < 200; ++i)
                many.visit ("Page " + juce::String (i));
            int steps = 0;
            while (many.canGoBack())
            {
                many.back();
                ++steps;
            }
            expectEquals (steps, RigNavigation::kMaxPlaces - 1);
            expectEquals (many.current(), juce::String ("Page 136"));
        }

        beginTest ("Back and Forward go through the screens, the Sampler's editor and the Looper's timeline");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            expectEquals (view.getPage(), juce::String ("Guide"));
            expect (! view.getNavigation().canGoBack());

            view.goTo ("Groove");
            view.goTo ("MPC");
            view.goTo ("Sampler editor");
            expectEquals (view.getPlace(), juce::String ("Sampler editor"));
            view.goTo ("Song");

            view.goBack();
            expectEquals (view.getPlace(), juce::String ("Sampler editor"));
            view.goBack();
            expectEquals (view.getPage(), juce::String ("MPC"));
            view.goBack();
            expectEquals (view.getPage(), juce::String ("Groove"));
            view.goBack();
            expectEquals (view.getPage(), juce::String ("Guide"));
            expect (! view.getNavigation().canGoBack());
            view.goBack();   // nowhere further: stays put
            expectEquals (view.getPage(), juce::String ("Guide"));

            view.goForward();
            view.goForward();
            view.goForward();
            expectEquals (view.getPlace(), juce::String ("Sampler editor"));
            view.goForward();
            expectEquals (view.getPage(), juce::String ("Song"));
            expect (! view.getNavigation().canGoForward());

            // Back to the Sampler from somewhere else shows the pads side if that's where you were.
            view.goTo ("Sampler");
            if (auto* sampler = find<SamplerScreen> (view))
                sampler->setEditMode (false);
            view.goTo ("Sampler");   // (the window notices on its timer; showing a page notices too)
            view.goTo ("Groove");
            view.goBack();
            expectEquals (view.getPlace(), juce::String ("Sampler"));

            // The Looper's timeline is a place of its own.
            view.goTo ("Looper");
            auto* looper = find<LooperView> (view);
            expect (looper != nullptr);
            if (looper != nullptr)
            {
                looper->showTimeline (true);
                view.goTo ("Looper");
                view.goTo ("Groove");
                view.goBack();
                expectEquals (view.getPlace(), juce::String ("Looper / Timeline"));
                expect (looper->getTimeline() != nullptr);
                view.goBack();
                expectEquals (view.getPlace(), juce::String ("Looper"));
                expect (looper->getTimeline() == nullptr);
            }

            // Cmd+[ and Cmd+].
            view.goTo ("Bass");
            view.goTo ("Synth");
            expect (view.keyPressed (juce::KeyPress ('[', juce::ModifierKeys::commandModifier, 0)));
            expectEquals (view.getPage(), juce::String ("Bass"));
            expect (view.keyPressed (juce::KeyPress (']', juce::ModifierKeys::commandModifier, 0)));
            expectEquals (view.getPage(), juce::String ("Synth"));
        }

        beginTest ("New pattern picks an empty pattern on its track, so nothing you made is written over");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);

            // Bass: fill the first empty one; the next start goes past it.
            const int bassEmpty = songstart::firstEmptyBassPattern (engine);
            expect (bassEmpty >= 0);
            {
                auto p = engine.getBass().getPattern (bassEmpty);
                p.steps[0].on = true;
                engine.getBass().setPattern (bassEmpty, p);
            }
            const int bassNext = songstart::firstEmptyBassPattern (engine);
            expect (bassNext != bassEmpty);
            engine.getStrip (RigEngine::bassStrip).mute = true;
            auto r = songstart::begin (engine, songstart::bassLine);
            expectEquals (r.pattern, bassNext);
            expectEquals (r.track, (int) GrooveView::bass);
            expectEquals (engine.getBass().getSelectedPattern(), bassNext);
            expect (engine.getBass().params.getRawParameterValue ("sequencer")->load() > 0.5f, "the bass's pattern plays");
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load());
            expect (r.message.contains ("song part"));

            // The drum machine.
            const int machineEmpty = songstart::firstEmptyMachinePattern (engine);
            expect (machineEmpty >= 0);
            r = songstart::begin (engine, songstart::machineBeat);
            expectEquals (engine.getDrums().getSelectedPattern(), machineEmpty);
            expect (engine.getDrums().params.getRawParameterValue ("sequencer")->load() > 0.5f);

            // The MPC: an empty beat (a sequence it adds if it hasn't one).
            const int beat = songstart::firstEmptyMpcBeat (engine);
            expect (beat >= 0);
            r = songstart::begin (engine, songstart::mpcBeat);
            expectEquals (r.pattern, beat);
            expect ((int) engine.getMpc().getProject().getSequences().size() > beat);
            expect (engine.getMpc().getProject().getSequences()[(size_t) beat].isEmpty());

            // A loop and a sample go to their screens.
            expectEquals (songstart::begin (engine, songstart::loop).page, juce::String ("Looper"));
            expectEquals (songstart::begin (engine, songstart::sample).page, juce::String ("Sampler"));
            for (int s = 0; s < songstart::numStarts; ++s)
                expect (songstart::name (s).isNotEmpty());
        }

        beginTest ("New pattern from the top bar picks the track, and the keys play it");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Groove");
            auto* groove = find<GrooveView> (view);
            expect (groove != nullptr);
            view.startPattern (songstart::bassLine);
            expectEquals (view.getPage(), juce::String ("Groove"));
            if (groove != nullptr)
                expectEquals (groove->getTrack(), (int) GrooveView::bass);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::bass);
            view.startPattern (songstart::keys);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::synth);
            view.startPattern (songstart::loop);
            expectEquals (view.getPage(), juce::String ("Looper"));

            // Keep as the next song part.
            expectEquals ((int) engine.getScenes().getScenes().size(), 0);
            view.keepAsSongPart();
            expectEquals ((int) engine.getScenes().getScenes().size(), 1);
        }

        beginTest ("New song clears the Rig to a blank one, and keeps the windows as they were");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            expect (engine.isBlank(), "a new Rig is blank");
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            const auto blankBass = engine.getBass().getPattern (0);
            const double blankBpm = engine.getTransport().getBpm();

            engine.screens.setProperty ("dark", true, nullptr);
            engine.getTransport().setBpm (97.0);
            auto p = engine.getBass().getPattern (0);
            p.steps[3].on = ! p.steps[3].on;
            engine.getBass().setPattern (0, p);
            view.startPattern (songstart::bassLine);
            view.keepAsSongPart();
            expect (! engine.isBlank());

            view.newSong (false, false);
            expect (engine.isBlank());
            expectEquals ((int) engine.getScenes().getScenes().size(), 0);
            expectWithinAbsoluteError (engine.getTransport().getBpm(), blankBpm, 0.001);
            expect (engine.getBass().getPattern (0).steps[3].on == blankBass.steps[3].on);
            expect ((bool) engine.screens.getProperty ("dark", false), "the window's set-up stays");
            expect (view.getCurrentShow() == juce::File());

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_NAV_SNAPSHOT", {}); path.isNotEmpty())
            {
                for (auto [w, h, page, suffix] : { std::tuple<int, int, const char*, const char*> { 1440, 960, "Groove", "" },
                                                   { 1200, 860, "Groove", "-1200" }, { 1440, 960, "Mixer", "-mixer" } })
                {
                    view.setSize (w, h);
                    view.goTo (page);
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File file (path + suffix + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
        }
    }
};

static RigNavigationTests rigNavigationTests;
