#include "TestHelpers.h"

#include "RigEngine.h"
#include "StageView.h"
#include "RigView.h"
#include "SongGrid.h"

namespace
{
constexpr double kSceneRate = 48000.0;
constexpr int kSceneBlock = 480;

// Runs the Rig for `beats` (at its tempo), ticking the scenes like the window does.
void runBeats (RigEngine& engine, double beats)
{
    juce::AudioBuffer<float> out (2, kSceneBlock);
    const int blocks = (int) std::round (beats * 60.0 / engine.getTransport().getBpm() * kSceneRate / kSceneBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kSceneBlock);
        engine.tickScenes();
    }
}

RigScene sceneWith (RigEngine& engine, const juce::String& name)
{
    RigScene s;
    s.name = name;
    s.state = engine.captureScene();
    return s;
}
}

struct RigSceneTests : juce::UnitTest
{
    RigSceneTests() : juce::UnitTest ("Rig scenes", "Rig") {}

    void runTest() override
    {
        beginTest ("a scene keeps what's playing, and comes back at the next bar");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            auto& scenes = engine.getScenes().getScenes();

            scenes.push_back (sceneWith (engine, "Verse"));            // nothing muted
            engine.getStrip (RigEngine::bassStrip).mute = true;
            engine.getLooper().getLayer (2).mute = true;
            scenes.push_back (sceneWith (engine, "Break"));            // bass and loop 3 muted
            expectEquals (scenes[1].state.stripMutes[(size_t) RigEngine::bassStrip], 1);
            expectEquals (scenes[1].state.stripMutes[(size_t) RigEngine::masterStrip], -1, "the master isn't part of a scene");

            // Stopped: a scene starts straight away.
            engine.tickScenes();
            engine.getScenes().launch (0);
            runBeats (engine, 0.1);
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load());
            expect (! engine.getLooper().getLayer (2).mute.load());
            expectEquals (engine.getScenes().getCurrent(), 0);

            // Playing: it waits for the bar.
            engine.getTransport().play();
            runBeats (engine, 1.5);
            engine.getScenes().launch (1);
            expectEquals (engine.getScenes().getQueued(), 1);
            runBeats (engine, 2.0);   // beat 3.5
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load(), "not before the bar");
            runBeats (engine, 1.0);   // past beat 4
            expect (engine.getStrip (RigEngine::bassStrip).mute.load(), "at the bar");
            expect (engine.getLooper().getLayer (2).mute.load());
            expectEquals (engine.getScenes().getCurrent(), 1);
            expectEquals (engine.getScenes().getQueued(), -1);
        }

        beginTest ("a scene changes the MPC's sequence and the bass pattern");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            auto& project = engine.getMpc().getProject();
            project.addSequence ("B section");
            project.setCurrentSequence (0);
            auto& scenes = engine.getScenes().getScenes();
            RigScene s;
            s.name = "B";
            s.state.mpcSequence = 1;
            s.state.bassPattern = 3;
            s.state.arp = 1;
            scenes.push_back (s);
            engine.tickScenes();
            engine.getScenes().launch (0);
            runBeats (engine, 0.1);
            expectEquals (project.getCurrentSequenceIndex(), 1);
            expectEquals (engine.getBass().getSelectedPattern(), 3);
            expect (engine.getSynth().params.getRawParameterValue ("arpOn")->load() > 0.5f);
        }

        beginTest ("the MPC's song plays on the Rig's clock, and a sequence picked live waits for the bar");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            auto& mpc = engine.getMpc();
            auto& project = mpc.getProject();
            project.getSequences()[0].setBars (1);
            project.getSequences()[0].loop = true;
            project.addSequence ("Chorus");
            project.getSequences()[1].setBars (1);
            project.getSequences()[1].loop = true;
            project.setCurrentSequence (0);
            auto& song = project.currentSong();
            song.steps = { { 0, 2 }, { 1, 1 } };   // verse twice, then the chorus
            song.loop = true;

            expect (engine.playMpcSong (project.getCurrentSongIndex()));
            auto runFollowing = [&] (double beats)
            {
                const int steps = (int) std::round (beats * 10);
                for (int i = 0; i < steps; ++i)
                {
                    runBeats (engine, 0.1);
                    mpc.followTransport();
                }
            };
            runFollowing (2.0);
            expect (engine.getTransport().isPlaying(), "starts the clock");
            expectEquals (mpc.getSequencer().getPlayingId(), 0);
            expectEquals (mpc.getSongStep(), 0);
            runFollowing (4.0);   // beat 6, bar 2: still the verse, second time
            expectEquals (mpc.getSequencer().getPlayingId(), 0);
            runFollowing (4.0);   // beat 10, bar 3: the chorus
            expectEquals (mpc.getSequencer().getPlayingId(), 1);
            expectEquals (mpc.getSongStep(), 1);
            runFollowing (4.0);   // beat 14, bar 4: round again to the verse
            expectEquals (mpc.getSequencer().getPlayingId(), 0);

            // Picking a sequence mid-bar waits for the bar line (beat 16).
            runFollowing (0.5);
            engine.playMpcSequence (1);
            expect (! mpc.isSongPlaying(), "picking a sequence leaves the song");
            runFollowing (1.0);   // beat 15.5
            expectEquals (mpc.getSequencer().getPlayingId(), 0, "not yet");
            runFollowing (1.0);   // beat 16.5
            expectEquals (mpc.getSequencer().getPlayingId(), 1, "on the bar");

            // A song started while playing comes in at the next bar (beat 20).
            runFollowing (1.0);
            expect (engine.playMpcSong (project.getCurrentSongIndex()));
            runFollowing (0.5);   // beat 18
            expectEquals (mpc.getSequencer().getPlayingId(), 1, "waits for the bar");
            runFollowing (3.0);   // beat 21
            expectEquals (mpc.getSequencer().getPlayingId(), 0, "the song's first step");
            expect (mpc.isSongPlaying());
            runFollowing (8.0);   // beat 29: verse twice (20-28), then the chorus
            expectEquals (mpc.getSequencer().getPlayingId(), 1, "and it moves on after two bars");
        }

        beginTest ("the Stage: song parts, mutes, loop layers, vocoder parts, and into a tool");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            RigScene verse;
            verse.name = "Verse";
            engine.getScenes().getScenes().push_back (verse);
            StageView stage (engine);
            juce::StringArray opened;
            stage.onOpen = [&] (const juce::String& tab) { opened.add (tab); };
            stage.meterOf = [] (int) { return 0.5f; };
            stage.setSize (1400, 700);
            stage.refresh();
            runBeats (engine, 1.0);
            stage.refresh();
            for (auto* child : stage.getChildren())
                expect (! child->getBounds().isEmpty());

            // The song parts: Verse plays, the next empty one saves.
            stage.parts[0]->onClick();
            runBeats (engine, 1.0);
            expectEquals (engine.getScenes().getCurrent(), 0);
            stage.parts[3]->onClick();
            expectEquals ((int) engine.getScenes().getScenes().size(), 2, "an empty part saves what's playing");

            // Mutes, loop layers, the vocoder's parts.
            stage.pressMute (1);
            expect (engine.getStrip (RigEngine::bassStrip).mute.load());
            stage.pressLayer (2, true);
            expect (engine.getLooper().getLayer (2).mute.load());
            stage.pressLayer (0, false);
            runBeats (engine, 0.1);
            expect (engine.getLooper().getState (0) != RigLooper::State::empty);
            stage.pressVocoderPart (2);   // Robot
            expect (engine.vocoderOn.load(), "switching a vocoder part on turns the vocoder on");

            // A click on a channel's name goes into its tool.
            const auto at = stage.channelArea (0).getCentre().withY (stage.channelArea (0).getY() + 20).toFloat();
            const juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), at, {}, juce::MouseInputSource::defaultPressure,
                                          0.0f, 0.0f, 0.0f, 0.0f, &stage, &stage, juce::Time::getCurrentTime(), at,
                                          juce::Time::getCurrentTime(), 1, false);
            stage.mouseUp (click);
            expect (opened.contains ("MPC"));
        }

        beginTest ("a tab pops out into its own window, is saved with the show, and comes back");
        // Windows need an app with a screen; the test runner is a console program.
        if (juce::JUCEApplicationBase::getInstance() == nullptr || juce::Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            logMessage ("(no screen here: skipped)");
            expect (true);
        }
        else
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            juce::AudioDeviceManager devices;
            juce::ValueTree saved;
            {
                RigView view (engine, &devices);
                view.setSize (1400, 900);
                view.popOutTab ("MPC");
                expect (view.isInOwnWindow ("MPC"));
                expect (engine.getMpc().getActiveEditor() != nullptr && engine.getMpc().getActiveEditor()->isShowing(),
                        "the MPC shows in its window");
                expectEquals (engine.screens.getNumChildren(), 1);
                saved = engine.saveShow();
                view.bringBack ("MPC");
                expect (! view.isInOwnWindow ("MPC"));
                expectEquals (engine.screens.getNumChildren(), 0);
                view.popOutTab ("Stage");
            }   // closing the Rig with a window open is fine

            engine.loadShow (saved);
            expectEquals (engine.screens.getNumChildren(), 1, "loaded with the show");
            RigView view (engine, &devices);
            view.restoreWindows();
            expect (view.isInOwnWindow ("MPC") && ! view.isInOwnWindow ("Stage"));
        }

        beginTest ("the song grid: parts switch loops on and off, and a loop that comes in starts from its top");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            // An 8-beat loop whose sound tells which beat of it is playing (1 to 8).
            const int perBeat = (int) (kSceneRate / 2);
            juce::AudioBuffer<float> audio (2, 8 * perBeat);
            for (int i = 0; i < audio.getNumSamples(); ++i)
                for (int ch = 0; ch < 2; ++ch)
                    audio.setSample (ch, i, (float) (i / perBeat + 1) / 20.0f);
            engine.getLooper().setLayerAudio (0, audio, 8.0);
            engine.getLooper().getLayer (0).level = 1.0f;
            // Only the loops are heard (the tools' own idle sounds stay out of the measurement).
            for (int st : { RigEngine::mpcStrip, RigEngine::samplerStrip, RigEngine::vocoderStrip, RigEngine::bassStrip,
                            RigEngine::synthStrip, RigEngine::inputStrip })
                engine.getStrip (st).mute = true;

            SongGrid grid (engine);
            grid.setBounds (0, 0, 1100, 560);
            grid.addPart();
            grid.addPart();
            auto& parts = engine.getScenes().getScenes();
            expectEquals ((int) parts.size(), 2);
            expectEquals (SongGrid::cellValue (parts[0], { true, 0 }), 1, "a new part keeps the playing loop on");
            grid.clickCell (0, 0);
            expectEquals (SongGrid::cellValue (parts[0], { true, 0 }), 0, "a click mutes it in part A");
            expectEquals (SongGrid::cellValue (parts[1], { true, 0 }), 1);
            parts[0].bars = 1;
            parts[1].bars = 2;

            engine.tickScenes();
            grid.playSong();
            expect (engine.getScenes().songMode);
            juce::AudioBuffer<float> out (2, kSceneBlock);
            std::vector<float> heard;
            const int blocks = (int) std::round (8.0 * 60.0 / 120.0 * kSceneRate / kSceneBlock);
            for (int b = 0; b < blocks; ++b)
            {
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kSceneBlock);
                engine.tickScenes();
                for (int i = 0; i < kSceneBlock; ++i)
                    heard.push_back (out.getSample (0, i));
            }
            const auto at = [&] (double beat) { return std::abs (heard[(size_t) (beat * perBeat)]); };
            expectLessThan (at (2.5), 1.0e-4f, "part A: the loop is muted");
            expectEquals (engine.getScenes().getCurrent(), 1, "part B after part A's bar");
            expectGreaterThan (at (4.5), 1.0e-3f, "part B: the loop plays");
            expectWithinAbsoluteError (at (5.5) / at (4.5), 2.0f, 0.05f, "from its first beat, not the middle");
            expectWithinAbsoluteError (at (7.5) / at (4.5), 4.0f, 0.1f);

            // Clicking a cell in the part that's playing changes it now.
            grid.clickCell (1, 0);
            expect (engine.getLooper().getLayer (0).mute.load());

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_SONG_GRID_SNAPSHOT", {}); path.isNotEmpty())
            {
                grid.clickCell (1, 0);
                grid.addPart();
                grid.clickCell (2, 8);
                const auto image = grid.createComponentSnapshot (grid.getLocalBounds(), true, 2.0f);
                juce::File file (path);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
        }

        beginTest ("song mode: a part with a length moves on by itself");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            auto& rig = engine.getScenes();
            for (auto name : { "Intro", "Verse", "Outro" })
            {
                auto s = sceneWith (engine, name);
                s.bars = 1;
                rig.getScenes().push_back (s);
            }
            rig.songMode = true;
            engine.tickScenes();
            rig.launch (0);
            engine.getTransport().play();
            runBeats (engine, 0.5);
            expectEquals (rig.getCurrent(), 0);
            runBeats (engine, 4.0);
            expectEquals (rig.getCurrent(), 1, "one bar later");
            runBeats (engine, 4.0);
            expectEquals (rig.getCurrent(), 2);
            runBeats (engine, 8.0);
            expectEquals (rig.getCurrent(), 2, "the last part stays");

            beginTest ("Next and Back from keyboard buttons");
            rig.backPresses += 1;
            runBeats (engine, 4.5);
            expectEquals (rig.getCurrent(), 1);
            rig.nextPresses += 1;
            runBeats (engine, 0.1);
            expectEquals (rig.getQueued(), 2);
        }

        beginTest ("scenes are saved with the show");
        {
            RigEngine engine;
            auto& scenes = engine.getScenes();
            auto s = sceneWith (engine, "Drop");
            s.bars = 8;
            s.state.loopMutes[3] = 1;
            scenes.getScenes().push_back (s);
            scenes.songMode = true;
            const auto xml = engine.saveShow().createXml();

            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            expectEquals ((int) other.getScenes().getScenes().size(), 1);
            expect (other.getScenes().songMode);
            if (! other.getScenes().getScenes().empty())
            {
                const auto& back = other.getScenes().getScenes()[0];
                expectEquals (back.name, juce::String ("Drop"));
                expectEquals (back.bars, 8);
                expectEquals (back.state.loopMutes[3], 1);
                expectEquals (back.state.stripMutes[(size_t) RigEngine::bassStrip], 0);
            }
        }
    }
};

static RigSceneTests rigSceneTests;
