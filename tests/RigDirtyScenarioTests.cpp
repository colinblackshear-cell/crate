// The dirty model against the 60 situations the first checkpoint measured, with the answers they
// should have had: playing, stopping and looking around keep a show clean; every kind of edit makes
// it dirty; putting a setting back makes it clean again. Plus saving while it plays, a hand edit
// that playback overwrites, a hand edit to the value playback left, and a bank's mute that playback
// moved into storage.

#include "RigDirtyTestHelpers.h"
#include "MixerView.h"

using namespace dirtytest;

struct RigDirtyScenarioTests : juce::UnitTest
{
    RigDirtyScenarioTests() : juce::UnitTest ("Rig dirty scenarios", "Rig") {}

    juce::File show, withTakes;

    void expectState (RigEngine& e, RigDirty::State want, const juce::String& what)
    {
        const auto s = settle (e);
        expect (s.state == want, what + ": " + stateName (s.state) + " (want " + stateName (want) + ")");
        if (s.state != want)
            for (auto& d : e.getDirty().differences (8))
                logMessage ("    " + d);
    }

    void run (const juce::String& what, bool wantDirty, std::function<void (RigEngine&, Runner&)> f, bool view = false, bool takes = false)
    {
        beginTest (what);
        auto e = openSaved (takes ? withTakes : show);
        std::unique_ptr<RigView> v;
        if (view)
        {
            v = std::make_unique<RigView> (*e, nullptr);
            v->setSize (1440, 960);
        }
        Runner r { *e, v.get() };
        r.seconds (0.05);
        expectState (*e, RigDirty::State::clean, "just opened");
        f (*e, r);
        if (e->getTransport().isPlaying())
            e->getTransport().stop();
        r.seconds (0.05);
        expectState (*e, wantDirty ? RigDirty::State::dirty : RigDirty::State::clean, what);
        v.reset();
    }

    void runTest() override
    {
        const auto folder = tempFolder ("kb-dirty-scenarios");
        show = folder.getChildFile ("Cp1.crateshow");
        withTakes = folder.getChildFile ("Cp1 takes.crateshow");
        {
            beginTest ("The two shows");
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, kBlock);
            buildRich (*e, 2);
            addNoteClip (*e);
            Runner r { *e };
            r.seconds (0.1);
            expect (e->saveShowFile (show).ok);
            addTakes (*e);
            const auto* set = e->getTakes().setFor (4);
            expect (set != nullptr && set->takes.size() > 1, "two takes on loop 5");
            expect (e->saveShowFile (withTakes).ok);
        }

        // A. Open and idle: clean.
        run ("Open, fingerprint twice", false, [] (RigEngine& e, Runner&) { e.getDirty().checkNow(); });
        run ("Idle 10 s, stopped", false, [] (RigEngine&, Runner& r) { r.seconds (10); });
        run ("Idle 10 s, window open (its timer runs)", false, [] (RigEngine&, Runner& r) { r.seconds (10); }, true);
        run ("Opened and saved again, no change (the saved file vs the engine)", false, [&] (RigEngine& e, Runner&) { e.saveShowFile (folder.getChildFile ("Again.crateshow")); });

        // B. Playback, stopping, screens: clean.
        run ("Jam play 16.5 bars (automation on bass cutoff, macro on synth cutoff)", false, [] (RigEngine& e, Runner& r) { e.getTransport().play(); r.beats (66); });
        run ("Stop/start 5 times with locate", false, [] (RigEngine& e, Runner& r)
             { for (int i = 0; i < 5; ++i) { e.getTransport().play(); r.beats (3 + i); e.getTransport().stop(); r.seconds (0.2); e.getTransport().setPosition (2.0 * i); } });
        run ("Song playback, both sections (patterns, arp, unmute, bank B)", false, [] (RigEngine& e, Runner& r) { e.launchSection (0); r.beats (17); e.playOrStop(); });
        run ("Section key 2, play, stop", false, [] (RigEngine& e, Runner& r) { e.launchSection (1); r.beats (6); });
        run ("Song to its end (stops itself)", false, [] (RigEngine& e, Runner& r) { e.launchSection (0); r.beats (24); });
        run ("Song played twice", false, [] (RigEngine& e, Runner& r) { for (int k = 0; k < 2; ++k) { e.launchSection (0); r.beats (17); e.playOrStop(); r.seconds (0.2); } });
        run ("Arm a loop and cancel before the bar", false, [] (RigEngine& e, Runner& r)
             { e.getTransport().play(); r.beats (1); e.getLooper().pressRecord (3); r.beats (0.5); e.getLooper().pressRecord (3); r.beats (4); });
        run ("Window open: play 8 bars", false, [] (RigEngine& e, Runner& r) { e.getTransport().play(); r.beats (32); }, true);
        run ("Window open: visit every screen, no edits", false, [] (RigEngine&, Runner& r)
             { for (auto page : { "Groove", "MPC", "Sampler editor", "Song", "Looper", "Mixer", "Effects", "Guide", "Setup" }) r.view->goTo (page), r.seconds (0.2); }, true);
        run ("Window open: play, visit every screen, stop", false, [] (RigEngine& e, Runner& r)
             { e.getTransport().play(); r.beats (8); for (auto page : { "Groove", "MPC", "Sampler editor", "Song", "Looper", "Mixer", "Effects", "Guide", "Setup" }) r.view->goTo (page), r.seconds (0.2); }, true);

        // C. Your edits: dirty.
        run ("Bass cutoff by hand", true, [] (RigEngine& e, Runner&) { hand (e.getBass(), "cutoff", 1000.0f); });
        run ("After a song: bass cutoff by hand", true, [] (RigEngine& e, Runner& r) { e.launchSection (0); r.beats (17); e.playOrStop(); hand (e.getBass(), "cutoff", 1000.0f); });
        run ("Cutoff moved by hand while its lane plays, then stop", true, [] (RigEngine& e, Runner& r) { e.getTransport().play(); r.beats (2); hand (e.getBass(), "cutoff", 1500.0f); r.beats (0.25); });
        run ("After a song: pick bass pattern 3, the one the song left on", true, [] (RigEngine& e, Runner& r) { e.launchSection (0); r.beats (17); e.playOrStop(); hand (e.getBass(), "pattern", 2.0f); });
        // (the Mixer's mute button: the write, then it says the mute is yours)
        run ("After a song: unmute drums by hand, the value the song left", true, [] (RigEngine& e, Runner& r) { e.launchSection (0); r.beats (17); e.playOrStop(); handStripMute (e, RigEngine::drumsStrip, false); });
        run ("After a song: pick bank B by hand, the one the song left", true, [] (RigEngine& e, Runner& r) { e.launchSection (0); r.beats (17); e.playOrStop(); e.switchLoopBank (1); e.serviceLoopBanks(); });
        run ("Synth cutoff by hand (a macro target)", true, [] (RigEngine& e, Runner&) { hand (e.getSynth(), "cutoff", 3000.0f); });
        run ("Strip fx setting", true, [] (RigEngine& e, Runner&) { auto& fx = e.getFx (RigEngine::bassStrip); if (! fx.getParameters().isEmpty()) fx.getParameters()[0]->setValueNotifyingHost (0.77f); });
        run ("Vocoder, drone, MPC, sampler: one setting each", true, [] (RigEngine& e, Runner&)
             {
                 for (auto* t : { (juce::AudioProcessor*) &e.getVocoder(), (juce::AudioProcessor*) &e.getDrone(), (juce::AudioProcessor*) &e.getMpc(), (juce::AudioProcessor*) &e.getSampler() })
                     if (! t->getParameters().isEmpty())
                         t->getParameters()[0]->setValueNotifyingHost (1.0f - t->getParameters()[0]->getValue());
             });
        run ("Automation: a point moved", true, [] (RigEngine& e, Runner&) { auto lane = e.getAutomation().getLanes()[0]; lane.points[1].value = 0.5f; e.getAutomation().clearAll(); e.getAutomation().addLane (lane); });
        run ("Macro moved", true, [] (RigEngine& e, Runner& r) { e.getMacros().value[0] = 0.2f; r.seconds (0.1); });
        run ("Drum pattern: one hit added", true, [] (RigEngine& e, Runner&) { auto p = e.getDrums().getEditedPattern(); p.hits[2][3] = 1; e.getDrums().setEditedPattern (p); });
        run ("Bass pattern: one note changed", true, [] (RigEngine& e, Runner&) { auto p = e.getBass().getPattern (0); p.steps[0].note = 7; e.getBass().setPattern (0, p); });
        run ("Mixer: bass strip muted", true, [] (RigEngine& e, Runner&) { e.getStrip (RigEngine::bassStrip).mute = true; });
        run ("Mixer: bass level", true, [] (RigEngine& e, Runner&) { e.getStrip (RigEngine::bassStrip).level = 0.5f; });
        run ("Loop mute (looper)", true, [] (RigEngine& e, Runner&) { e.getLooper().getLayer (1).mute = true; });
        run ("Loop banks: a new bank", true, [] (RigEngine& e, Runner&) { e.newLoopBank ("C"); });
        run ("Loop banks: renamed", true, [] (RigEngine& e, Runner&) { e.renameLoopBank (1, "Bridge"); });
        run ("Loop banks: bank B's loop changed (bank not current)", true, [] (RigEngine& e, Runner&)
             { e.switchLoopBank (1); e.serviceLoopBanks(); e.getLooper().setLayerAudio (0, noise ((int) kRate * 2, 11), 4.0); e.switchLoopBank (0); e.serviceLoopBanks(); });
        run ("Song: a box moved", true, [] (RigEngine& e, Runner&) { e.getTimeline().getBlocks()[0].start += 1; e.getTimeline().changed(); });
        run ("Song: timeline switched on", true, [] (RigEngine& e, Runner&) { e.getTimeline().on = true; });
        run ("Song: a section added", true, [] (RigEngine& e, Runner&) { rigsong::addSection (e.getTimeline(), "Outro", 2); e.getTimeline().changed(); });
        run ("MIDI notes: one note moved in a clip", true, [] (RigEngine& e, Runner&) { auto& c = e.getNoteClips().at (0); c.notes.at (0).beat += 0.25; c.sort(); e.notesChanged(); });
        run ("MIDI notes: a clip added", true, [] (RigEngine& e, Runner&) { addNoteClip (e, 4, RigEngine::bassStrip, 1); });
        run ("Takes: use the other take", true, [this] (RigEngine& e, Runner& r)
             {
                 const auto* s = e.getTakes().setFor (4);
                 expect (s != nullptr && s->takes.size() > 1, "the show has two takes");
                 if (s != nullptr && s->takes.size() > 1)
                     e.useTake (4, s->playing == 0 ? 1 : 0);
                 r.seconds (0.1);
             }, false, true);
        run ("Takes: record a song take", true, [] (RigEngine& e, Runner& r) { r.input = true; e.recordInto (1, 6, RigLooper::input, false); r.beats (12); r.input = false; });
        run ("Takes: delete a take that isn't playing", true, [this] (RigEngine& e, Runner&)
             {
                 const auto* s = e.getTakes().setFor (4);
                 expect (s != nullptr && s->takes.size() > 1, "the show has two takes");
                 if (s != nullptr && s->takes.size() > 1)
                     e.getTakes().deleteTake (4, s->playing == 0 ? 1 : 0);
             }, false, true);
        run ("Loop audio: loop 1 re-recorded, same length", true, [] (RigEngine& e, Runner&) { e.getLooper().setLayerAudio (0, noise ((int) kRate * 2, 9), 4.0); });
        run ("Loop audio: overdub loop 2", true, [] (RigEngine& e, Runner& r)
             { e.getTransport().play(); r.beats (3.9); r.input = true; e.getLooper().pressRecord (1); r.beats (4.2); e.getLooper().pressRecord (1); r.beats (4); r.input = false; });
        run ("Loop audio: new loop recorded on layer 4", true, [] (RigEngine& e, Runner& r)
             { e.getLooper().getLayer (3).source = RigLooper::input; e.getLooper().getLayer (3).bars = 1; e.getTransport().play(); r.input = true; e.getLooper().pressRecord (3); r.beats (10); r.input = false; });
        run ("Loop cleared", true, [] (RigEngine& e, Runner& r) { e.getLooper().clear (1); r.seconds (0.05); });
        run ("Tempo", true, [] (RigEngine& e, Runner&) { e.getTransport().setBpm (124.0); });
        run ("Quantize strength", true, [] (RigEngine& e, Runner&) { e.getQuantizer().settings.strength = 0.3f; });

        // D. Back to the saved value: clean.
        run ("Cutoff 1000 and back to 400", false, [] (RigEngine& e, Runner&) { hand (e.getBass(), "cutoff", 1000.0f); hand (e.getBass(), "cutoff", 400.0f); });
        run ("Bass level 0.5 and back", false, [] (RigEngine& e, Runner&) { e.getStrip (RigEngine::bassStrip).level = 0.5f; e.getStrip (RigEngine::bassStrip).level = 0.7f; });
        run ("Bass mute on and off", false, [] (RigEngine& e, Runner&) { e.getStrip (RigEngine::bassStrip).mute = true; e.getStrip (RigEngine::bassStrip).mute = false; });
        run ("Macro 0.2 and back to 0.6", false, [] (RigEngine& e, Runner& r) { e.getMacros().value[0] = 0.2f; r.seconds (0.1); e.getMacros().value[0] = 0.6f; r.seconds (0.1); });
        run ("Automation point moved and back", false, [] (RigEngine& e, Runner&)
             {
                 auto lane = e.getAutomation().getLanes()[0];
                 const auto was = lane.points[1].value;
                 lane.points[1].value = 0.5f; e.getAutomation().clearAll(); e.getAutomation().addLane (lane);
                 lane.points[1].value = was; e.getAutomation().clearAll(); e.getAutomation().addLane (lane);
             });
        run ("Tempo 124 and back", false, [] (RigEngine& e, Runner&) { e.getTransport().setBpm (124.0); e.getTransport().setBpm (120.0); });
        run ("Drum hit added and removed", false, [] (RigEngine& e, Runner&)
             { auto p = e.getDrums().getEditedPattern(); p.hits[2][3] = 1; e.getDrums().setEditedPattern (p); p.hits[2][3] = 0; e.getDrums().setEditedPattern (p); });
        run ("Note moved and back", false, [] (RigEngine& e, Runner&)
             { auto& c = e.getNoteClips().at (0); c.notes.at (0).beat += 0.25; e.notesChanged(); c.notes.at (0).beat -= 0.25; e.notesChanged(); });
        run ("Other take, then the first again", false, [this] (RigEngine& e, Runner& r)
             {
                 const auto* s = e.getTakes().setFor (4);
                 expect (s != nullptr && s->takes.size() > 1, "the show has two takes");
                 const int was = s != nullptr ? s->playing : -1;
                 if (s != nullptr && s->takes.size() > 1) { e.useTake (4, was == 0 ? 1 : 0); r.seconds (0.1); e.useTake (4, was); r.seconds (0.1); }
             }, false, true);
        run ("Loop 1 replaced by the same sound", false, [] (RigEngine& e, Runner&) { auto a = e.getLooper().copyLayerAudio (0); e.getLooper().setLayerAudio (0, a, e.getLooper().getLayer (0).lengthBeats.load()); });
        run ("Overdub loop 2, then Undo", false, [] (RigEngine& e, Runner& r)
             { e.getTransport().play(); r.beats (3.9); r.input = true; e.getLooper().pressRecord (1); r.beats (4.2); e.getLooper().pressRecord (1); r.beats (4); r.input = false; e.getLooper().undo (1); r.beats (1); });
        run ("Bank B by hand, then back to A by hand", false, [] (RigEngine& e, Runner&) { e.switchLoopBank (1); e.serviceLoopBanks(); e.switchLoopBank (0); e.serviceLoopBanks(); });
        run ("Song played, then every touched setting put back by hand", false, [] (RigEngine& e, Runner& r)
             { e.launchSection (0); r.beats (17); e.playOrStop(); hand (e.getBass(), "cutoff", 400.0f); hand (e.getBass(), "pattern", 0.0f); });

        // E. Beyond the first checkpoint.
        {
            beginTest ("Saved while the song plays: clean, and edits after it still count");
            auto e = openSaved (show);
            Runner r { *e };
            auto* cutoff = paramOf (e->getBass(), "cutoff");
            const float savedCutoff = cutoff->getValue();
            e->launchSection (0);
            r.beats (5);
            expect (e->getTransport().isPlaying());
            const auto again = folder.getChildFile ("Saved playing.crateshow");
            expect (e->saveShowFile (again, rigshow::SaveKind::document).ok);
            expectState (*e, RigDirty::State::clean, "right after the save, still playing");
            r.beats (4);
            expectState (*e, RigDirty::State::clean, "playing on");
            e->getTransport().stop();
            r.seconds (0.1);
            expectState (*e, RigDirty::State::clean, "stopped");
            e->launchSection (0);
            r.beats (17);
            e->playOrStop();
            r.seconds (0.1);
            expectState (*e, RigDirty::State::clean, "played again, stopped");
            cutoff->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, savedCutoff + 0.2f));
            expectState (*e, RigDirty::State::dirty, "the automated cutoff by hand");
            cutoff->setValueNotifyingHost (savedCutoff);
            expectState (*e, RigDirty::State::clean, "and back");
            expect (! e->getStrip (RigEngine::drumsStrip).mute.load(), "the song left the drums on");
            handStripMute (*e, RigEngine::drumsStrip, false);
            expectState (*e, RigDirty::State::dirty, "the drums unmuted by hand (what the song left)");
            handStripMute (*e, RigEngine::drumsStrip, true);
            expectState (*e, RigDirty::State::clean, "and muted again (as saved)");
        }
        {
            beginTest ("A hand edit that the lane then overwrites stays an edit");
            auto e = openSaved (show);
            Runner r { *e };
            auto* cutoff = dynamic_cast<juce::RangedAudioParameter*> (paramOf (e->getBass(), "cutoff"));
            e->getTransport().play();
            r.beats (2);
            hand (e->getBass(), "cutoff", 1500.0f);
            r.beats (2);
            e->getTransport().stop();
            r.seconds (0.05);
            expect (std::abs (cutoff->convertFrom0to1 (cutoff->getValue()) - 1500.0f) > 1.0f, "the lane wrote over it");
            expectState (*e, RigDirty::State::dirty, "after stop");
        }
        {
            beginTest ("A hand edit to the value playback left counts");
            auto e = openSaved (show);
            Runner r { *e };
            e->launchSection (0);
            r.beats (17);
            e->playOrStop();
            r.seconds (0.05);
            expectState (*e, RigDirty::State::clean, "after the song");
            const bool left = e->getStrip (RigEngine::drumsStrip).mute.load();
            expect (! left, "the song unmuted the drums");
            handStripMute (*e, RigEngine::drumsStrip, left);
            expectState (*e, RigDirty::State::dirty, "same value, by hand");
        }
        {
            beginTest ("The Mixer's own mute button after a song");
            auto e = openSaved (show);
            RigView view (*e, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Mixer");
            Runner r { *e, &view };
            e->launchSection (0);
            r.beats (17);
            e->playOrStop();
            r.seconds (0.05);
            expectState (*e, RigDirty::State::clean, "after the song");
            auto* mixer = view.getMixer();
            auto* drums = mixer != nullptr ? mixer->channelFor (RigEngine::drumsStrip) : nullptr;
            expect (drums != nullptr, "the drums' channel is on the Mixer");
            if (drums != nullptr)
            {
                expect (! e->getStrip (RigEngine::drumsStrip).mute.load());
                drums->mute.onClick();
                expect (e->getStrip (RigEngine::drumsStrip).mute.load());
                expectState (*e, RigDirty::State::clean, "muted by the button, as saved");
                drums->mute.onClick();
                expectState (*e, RigDirty::State::dirty, "unmuted by the button (what the song had left)");
            }
        }
        {
            beginTest ("A loop the song unmutes, kept in its bank when the song moves on");
            const auto banked = folder.getChildFile ("Banked mute.crateshow");
            {
                auto e = std::make_unique<RigEngine>();
                e->prepare (kRate, kBlock);
                buildRich (*e, 2);
                e->getLooper().getLayer (0).mute = true;   // bank A, loop 1: muted, and the song plays it
                auto& t = e->getTimeline();
                rigsong::setBox (t, 0, RigTimeline::laneForLoop (0), 0);
                rigsong::setBox (t, 1, RigTimeline::laneForLoop (0), 0);
                t.getMarkers()[0].loopBank = 0;
                t.getMarkers()[1].loopBank = 1;
                t.changed();
                Runner r { *e };
                r.seconds (0.1);
                expect (e->saveShowFile (banked).ok);
            }
            auto e = openSaved (banked);
            Runner r { *e };
            r.seconds (0.05);
            expectState (*e, RigDirty::State::clean, "opened");
            expect (e->getLooper().getLayer (0).mute.load(), "loop 1 starts muted");
            e->launchSection (0);
            r.beats (17);
            e->playOrStop();
            r.seconds (0.1);
            expectEquals (e->getCurrentLoopBank(), 1, "the song left bank B on");
            expectState (*e, RigDirty::State::clean, "both sections played, stopped");
            handLoopMute (*e, 0, true);
            expectState (*e, RigDirty::State::dirty, "bank B's loop 1 muted by hand");
            handLoopMute (*e, 0, false);
            expectState (*e, RigDirty::State::clean, "and unmuted");
            e->switchLoopBank (0);
            e->serviceLoopBanks();
            r.seconds (0.05);
            expectState (*e, RigDirty::State::clean, "bank A again by hand");
            expect (! e->getLooper().getLayer (0).mute.load(), "loop 1 as the song left it (unmuted)");
            handLoopMute (*e, 0, true);
            expectState (*e, RigDirty::State::clean, "muted by hand, as saved");
        }
        folder.deleteRecursively();
    }
};

static RigDirtyScenarioTests rigDirtyScenarioTests;
