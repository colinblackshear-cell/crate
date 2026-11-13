#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "SamplerScreen.h"
#include "MpcView.h"

// The MPC as one unit: the Sampler · MPC screen with the MPC's sequencer side, the MPC's beat on
// the Rig's one clock with its own on/off, and the pads played into the looper.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

void runBlocks (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
}

template <typename T>
T* find (juce::Component& c, std::function<bool (T&)> wanted = {})
{
    if (auto* t = dynamic_cast<T*> (&c); t != nullptr && (! wanted || wanted (*t)))
        return t;
    for (auto* child : c.getChildren())
        if (auto* t = find<T> (*child, wanted))
            return t;
    return nullptr;
}

// Visible all the way up (the window isn't on a screen in the tests, so isShowing() is false).
bool shown (juce::Component& c)
{
    for (auto* p = &c; p->getParentComponent() != nullptr; p = p->getParentComponent())
        if (! p->isVisible())
            return false;
    return true;
}

juce::TextButton* button (juce::Component& c, const juce::String& text)
{
    return find<juce::TextButton> (c, [&text] (juce::TextButton& b) { return b.getButtonText() == text && shown (b); });
}
}

struct RigMpcUnitTests : juce::UnitTest
{
    RigMpcUnitTests() : juce::UnitTest ("Rig: the MPC as one unit", "Rig") {}

    void runTest() override
    {
        beginTest ("the MPC's beat has its own on/off under the Rig's Play");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& seq = engine.getMpc().getSequencer();
            auto& transport = engine.getTransport();
            expect (engine.isMpcBeatOn(), "on in a new Rig, as before");

            transport.play();
            runBlocks (engine, 4);
            expect (seq.isPlaying() && seq.isFollowingHost(), "on: it plays along with the Rig");

            engine.setMpcBeatOn (false);
            runBlocks (engine, 2);
            expect (transport.isPlaying(), "the Rig plays on");
            expect (! seq.isPlaying(), "off: the beat stops at once");

            transport.stop();
            runBlocks (engine, 2);
            transport.play();
            runBlocks (engine, 4);
            expect (! seq.isPlaying(), "off: the Rig's Play leaves it quiet");

            engine.setMpcBeatOn (true);
            runBlocks (engine, 2);
            expect (seq.isPlaying(), "on again: it comes in");

            // Saved with the show, and in song parts.
            engine.setMpcBeatOn (false);
            auto show = engine.saveShow();
            expect (! (bool) show.getProperty ("mpcBeat", true));
            auto scene = engine.captureScene();
            expectEquals (scene.mpcBeat, 0);
            engine.setMpcBeatOn (true);
            engine.loadShow (show);
            expect (! engine.isMpcBeatOn(), "a show keeps it off");
            show.removeProperty ("mpcBeat", nullptr);
            engine.loadShow (show);
            expect (engine.isMpcBeatOn(), "older shows play the MPC with the Rig, as they did");
        }

        beginTest ("the MPC's Play, Stop and Rec are the Rig's, so its Stop always stops it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& mpc = engine.getMpc();
            auto& transport = engine.getTransport();
            engine.setMpcBeatOn (false);

            mpc.pressPlay (false);
            runBlocks (engine, 4);
            expect (transport.isPlaying(), "the MPC's Play starts the Rig");
            expect (engine.isMpcBeatOn(), "and turns its beat on");
            expect (mpc.getSequencer().isPlaying() && mpc.getSequencer().isFollowingHost(), "on the Rig's clock, not its own");

            mpc.pressStop();
            runBlocks (engine, 2);
            expect (! transport.isPlaying(), "the MPC's Stop stops the Rig");
            expect (! mpc.getSequencer().isPlaying(), "and the beat with it");

            mpc.pressRecord();
            runBlocks (engine, 4);
            expect (transport.isPlaying() && mpc.getSequencer().isRecording(), "Rec records on the Rig's clock");
            mpc.pressStop();
            runBlocks (engine, 2);
            expect (! mpc.getSequencer().isRecording());

            // The Follow switch is the beat's switch in the Rig.
            mpc.setFollowing (false);
            expect (! engine.isMpcBeatOn());
            mpc.setFollowing (true);
            expect (engine.isMpcBeatOn());
        }

        beginTest ("Loop pads plays the MPC into the next empty loop layer");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            expectEquals (engine.getPadLoopLayer(), -1);
            auto text = engine.loopMpcPads (2);
            expect (text.contains ("Loop 1"), text);
            expectEquals (engine.getPadLoopLayer(), 0);
            expectEquals (looper.getLayer (0).source.load(), (int) RigLooper::mpc);
            expectEquals (looper.getLayer (0).bars.load(), 2);
            runBlocks (engine, 2);
            expect (engine.getTransport().isPlaying(), "the clock starts with it, as the looper's record does");
            const auto state = looper.getState (0);
            expect (state == RigLooper::State::armed || state == RigLooper::State::recording);

            // Pressed again, it ends that loop; the next press uses the next empty layer.
            engine.loopMpcPads (0);
            expectEquals (engine.getPadLoopLayer(), -1);
            runBlocks (engine, (int) (kRate * 6 / kBlock));
            expect (looper.getState (0) != RigLooper::State::empty);
            text = engine.loopMpcPads (0);
            expect (text.contains ("Loop 2"), text);
            expectEquals (engine.getPadLoopLayer(), 1);
            expectEquals (looper.getLayer (1).bars.load(), 0, "Free: until pressed again");
        }

        beginTest ("one MPC: the Sampler · MPC screen's Sequencer side, beside the same pads");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Sampler");
            auto* screen = find<SamplerScreen> (view);
            expect (screen != nullptr);
            if (screen == nullptr)
                return;
            auto* mpc = screen->getMpcView();
            expect (mpc != nullptr && mpc->isHosted(), "the MPC's window lives on the screen");
            if (mpc == nullptr)
                return;
            expect (find<MpcView> (view, [mpc] (MpcView& v) { return &v != mpc; }) == nullptr, "and there's no second MPC page");

            view.goTo ("MPC");
            expectEquals (view.getPage(), juce::String ("Sampler"));
            expectEquals (view.getPlace(), juce::String ("MPC"));
            expect (screen->getSide() == SamplerScreen::mpcSide);
            expect (shown (*mpc), "the sequencer side shows");
            expect (button (*screen, "A") != nullptr, "the pad banks stay");
            expect (button (*screen, "Beat on") != nullptr && button (*screen, "Loop pads") != nullptr);
            expect (button (*mpc, "Steps") != nullptr && button (*mpc, "Pad") != nullptr);

            // Every MPC tool is still there: kits, the transport, songs, the pad's settings.
            for (auto* text : { "New kit", "Remove", "Export to card...", "Open kit...", "Play", "Stop", "Rec", "Play song", "Steps..." })
                expect (button (*mpc, text) != nullptr, text);

            // Tapping a pad picks it (and plays it): the MPC's pad settings follow.
            screen->pressPad (5);
            expectEquals (mpc->getSelectedPad(), 5);

            // Beat off from the screen, and back on.
            screen->pressBeat();
            expect (! engine.isMpcBeatOn());
            expect (button (*screen, "Beat off") != nullptr);
            screen->pressBeat();
            expect (engine.isMpcBeatOn());

            // Edit opens the picked pad's sound editor (pads only play).
            auto& kit = engine.getMpc().getProject().currentBank();
            kit.pads[5].audio = crate::share (sine (4000, 1, 0.5f, 220.0));
            kit.pads[5].name = "Tone";
            engine.getMpc().getProject().changed();
            view.goTo ("Sampler");
            screen->setSide (SamplerScreen::chopSide);
            screen->pressEditPad();
            expect (screen->getSide() == SamplerScreen::mpcSide);
            expect (mpc->isEditingSound());

            // The Sampler's own button shows the Chop side again.
            screen->setSide (SamplerScreen::chopSide);
            expectEquals (view.getPlace(), juce::String ("Sampler"));

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_MPC_UNIT_SNAPSHOT", {}); path.isNotEmpty())
            {
                for (int i = 0; i < 16; ++i)
                {
                    kit.pads[(size_t) i].audio = crate::share (sine (20000, 1, 0.4f, 110.0 * (1 + i % 5)));
                    kit.pads[(size_t) i].name = juce::StringArray { "Kick", "Snare", "Hat", "Clap", "Bass" }[i % 5] + " " + juce::String (i + 1);
                }
                auto& s = engine.getMpc().getProject().currentSequence();
                for (int step = 0; step < 16; ++step)
                    if (step % 4 == 0 || step % 8 == 6 || step % 2 == 1)
                        s.tracks[0].notes.push_back ({ step * seq::kTicksPerBeat / 4, step % 4 == 0 ? 0 : step % 8 == 6 ? 1 : 2, 100, 24 });
                engine.getMpc().getProject().sequenceChanged();
                engine.getMpc().getProject().changed();
                engine.getMpc().getProject().dispatchPendingMessages();
                screen->selectSlice (0);
                auto snap = [&view] (const juce::String& file)
                {
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File f (file);
                    f.deleteFile();
                    juce::FileOutputStream stream (f);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                };
                view.goTo ("MPC");
                mpc->showSequencer (true);
                snap (path + "-steps.png");
                mpc->showSequencer (false);
                snap (path + "-pad.png");
                engine.loopMpcPads (4);
                runBlocks (engine, 4);
                view.goTo ("Sampler");
                screen->setSide (SamplerScreen::chopSide);
                snap (path + "-chop.png");
            }
        }
    }
};

static RigMpcUnitTests rigMpcUnitTests;
