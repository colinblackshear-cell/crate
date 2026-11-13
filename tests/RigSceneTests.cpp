#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigSong.h"
#include "StageView.h"
#include "RigView.h"
#include "SongGrid.h"
#include "RigExport.h"
#include "HelpPanel.h"
#include "MoreView.h"

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
            rigsong::addSection (engine.getTimeline(), "Verse", 4);
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

            // The song's sections: Verse plays, an empty one saves what's playing as the next.
            stage.parts[0]->onClick();
            runBeats (engine, 1.0);
            expect (engine.getTimeline().on.load() && engine.getTransport().isPlaying());
            expectEquals (rigsong::sectionAt (engine.getTimeline(), engine.getTransport().getPositionInBeats() / 4), 0);
            stage.parts[3]->onClick();
            expectEquals ((int) rigsong::sections (engine.getTimeline()).size(), 2, "an empty part saves what's playing");

            // Mutes, loop layers, the vocoder's parts.
            stage.pressMute (2);   // (the drum machine is channel 1)
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

        beginTest ("the how-to guide opens on the screen you're on");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Groove");   // the Rig opens on the Guide; its ? opens Getting started
            view.showHelp (true);
            expect (view.getHelp() != nullptr && view.getHelp()->isVisible());
            expectEquals (HelpPanel::guides()[(size_t) HelpPanel ("Looper").getShown()].title, juce::String ("Looper (F3): record loops"));
            if (view.getHelp() != nullptr)
            {
                expectEquals (HelpPanel::guides()[(size_t) view.getHelp()->getShown()].title, juce::String ("Groove (F1): patterns"), "it opens on the Groove screen's guide");
                for (const auto& g : HelpPanel::guides())
                    expect (g.steps.size() >= 3, g.title);
                if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_HELP_SNAPSHOT", {}); path.isNotEmpty())
                {
                    view.getHelp()->show (4);
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File file (path);
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
            view.showHelp (false);
            expect (view.getHelp() == nullptr);
        }

        beginTest ("export for Logic: each loop, and each loop laid out over the song, starting on bar 1");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.getTransport().setBpm (120.0);
            const int perBeat = (int) (kSceneRate / 2);
            const auto makeLoop = [perBeat] (int beats)
            {
                juce::AudioBuffer<float> a (2, beats * perBeat);
                for (int i = 0; i < a.getNumSamples(); ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        a.setSample (ch, i, (float) (i / perBeat + 1) / 20.0f);
                return a;
            };
            engine.getLooper().setLayerAudio (0, makeLoop (8), 8.0);
            engine.getLooper().setLayerAudio (1, makeLoop (4), 4.0);
            engine.getLooper().getLayer (0).level = 1.0f;
            engine.getLooper().getLayer (1).level = 0.5f;

            const auto dry = RigExport::renderLoop (engine, 1, 120.0, kSceneRate, false);
            expectEquals (dry.getNumSamples(), 4 * perBeat, "one pass of the loop");
            expectWithinAbsoluteError (dry.getSample (0, perBeat * 2 + 10), 0.15f, 1.0e-4f);
            const auto heard = RigExport::renderLoop (engine, 1, 120.0, kSceneRate, true);
            expectWithinAbsoluteError (heard.getSample (0, perBeat * 2 + 10), 0.075f, 1.0e-4f, "as heard: at its level");

            // Part A (1 bar): loop 2 muted. Part B (2 bars): it comes in from its top.
            RigScene a, b;
            a.name = "Intro";
            a.bars = 1;
            a.state.loopMutes[0] = 0;
            a.state.loopMutes[1] = 1;
            b.name = "Verse";
            b.bars = 2;
            b.state.loopMutes[0] = 0;
            b.state.loopMutes[1] = 0;
            engine.getScenes().getScenes() = { a, b };
            expectEquals (RigExport::songBars (engine), 3);
            const auto stem = RigExport::renderSongStem (engine, 1, 120.0, kSceneRate, false);
            expectEquals (stem.getNumSamples(), 12 * perBeat, "the whole song long");
            expectLessThan (std::abs (stem.getSample (0, perBeat * 2)), 1.0e-6f, "muted in the intro");
            expectWithinAbsoluteError (stem.getSample (0, perBeat * 4 + 10), 0.05f, 1.0e-4f, "starts from its top in the verse");
            expectWithinAbsoluteError (stem.getSample (0, perBeat * 9 + 10), 0.10f, 1.0e-4f, "and goes round");
            const auto long0 = RigExport::renderSongStem (engine, 0, 120.0, kSceneRate, false);
            expectWithinAbsoluteError (long0.getSample (0, perBeat * 9 + 10), 0.10f, 1.0e-4f, "loop 1 plays straight through: beat 10 is its 2nd beat");

            // With an effect, the "as heard" file is different from the dry one.
            engine.getLoopFx (0).params.getParameter ("crush")->setValueNotifyingHost (0.9f);
            const auto crushed = RigExport::renderLoop (engine, 0, 120.0, kSceneRate, true);
            const auto plain = RigExport::renderLoop (engine, 0, 120.0, kSceneRate, false);
            float diff = 0;
            for (int i = 0; i < plain.getNumSamples(); i += 97)
                diff = juce::jmax (diff, std::abs (crushed.getSample (0, i) - plain.getSample (0, i)));
            expectGreaterThan (diff, 0.001f, "the loop's effects are printed");

            const auto parent = juce::File::createTempFile ("rigexport");
            parent.createDirectory();
            const auto result = RigExport::exportForLogic (engine, parent);
            expect (result.error.isEmpty(), result.error);
            expectEquals (result.files, 2 * 2 + 2 + 1);
            expectEquals (result.folder.getChildFile ("Loops").findChildFiles (juce::File::findFiles, false, "*.wav").size(), 2);
            expectEquals (result.folder.getChildFile ("Loops").getChildFile ("Dry").findChildFiles (juce::File::findFiles, false, "*.wav").size(), 2);
            expectEquals (result.folder.getChildFile ("Song").findChildFiles (juce::File::findFiles, false, "*.wav").size(), 2);
            expect (result.folder.getChildFile ("Read me.txt").loadFileAsString().contains ("bar 2: Verse"));
            juce::MidiFile midi;
            juce::FileInputStream in (result.folder.getChildFile ("Song").getChildFile ("Song markers.mid"));
            expect (midi.readFrom (in));
            int markers = 0;
            if (midi.getNumTracks() > 0)
                for (auto* e : *midi.getTrack (0))
                    if (e->message.isTextMetaEvent() && e->message.getMetaEventType() == 6)
                        ++markers;
            expectEquals (markers, 2, "a marker for each part");
            parent.deleteRecursively();

            RigEngine empty;
            expect (RigExport::exportForLogic (empty, parent).error.isNotEmpty(), "nothing to export");
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

        beginTest ("moving, copying and deleting parts keeps the one that's playing");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.getTransport().setBpm (120.0);
            auto& scenes = engine.getScenes();
            for (auto name : { "A", "B", "C", "D" })
                scenes.getScenes().push_back (sceneWith (engine, name));
            engine.tickScenes();
            scenes.launch (1);
            runBeats (engine, 0.1);
            expectEquals (scenes.getCurrent(), 1);

            scenes.move (1, 3);   // B goes last: A C D B
            expectEquals (scenes.getScenes()[3].name, juce::String ("B"));
            expectEquals (scenes.getCurrent(), 3, "B still plays, at its new place");
            scenes.move (0, 2);   // C D A B
            expectEquals (scenes.getScenes()[0].name, juce::String ("C"));
            expectEquals (scenes.getCurrent(), 3);

            expectEquals (scenes.duplicate (0), 1);   // C "C 2" D A B
            expectEquals (scenes.getScenes()[1].name, juce::String ("C 2"));
            expectEquals (scenes.getCurrent(), 4);

            scenes.remove (0);   // "C 2" D A B
            expectEquals ((int) scenes.getScenes().size(), 4);
            expectEquals (scenes.getCurrent(), 3);
            scenes.remove (3);   // the one playing
            expectEquals (scenes.getCurrent(), -1);

            // A part that's coming up next, deleted, doesn't come.
            engine.getTransport().play();
            scenes.launch (0);
            runBeats (engine, 0.1);
            const int before = scenes.getCurrent();
            scenes.remove (scenes.getQueued() >= 0 ? scenes.getQueued() : 0);
            runBeats (engine, 8.0);
            expect (scenes.getCurrent() == before || scenes.getCurrent() == -1, "the deleted part never started");
        }

        beginTest ("the More page: a button for every tool, and they open");
        {
            MoreView more (true);
            more.setSize (1400, 560);
            juce::StringArray opened;
            more.onOpen = [&opened] (const juce::String& page) { opened.add (page); };
            expectEquals (more.tiles.size(), (int) MoreView::toolList().size());
            for (auto* t : more.tiles)
            {
                expect (! t->getBounds().isEmpty(), "every tool shows");
                t->onClick();
            }
            for (auto page : { "Song", "MPC", "Drum machine", "Sampler editor", "Bass", "Synth", "Sounds", "Mixer" })
                expect (opened.contains (page), page);

            // In the Rig, More is a page, and its buttons go to the pages.
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 900);
            view.goTo ("More");
            expectEquals (view.getPage(), juce::String ("More"));
            view.goTo ("Song");
            expectEquals (view.getPage(), juce::String ("Song"));

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
