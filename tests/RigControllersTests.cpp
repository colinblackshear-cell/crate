#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "ControllersPanel.h"
#include "core/control/Controllers.h"

// MIDI controllers: whatever a friend plugs in (a Launchkey, an MPK mini, a plain keyboard, a
// Launchpad, an MPD, an APC mini), it plays the Rig the Launchkey's way, and what isn't known
// from its name is learned.
class RigControllersTests : public juce::UnitTest
{
public:
    RigControllersTests() : juce::UnitTest ("Rig: MIDI controllers", "Rig") {}

    using Kind = crate::controllers::Kind;
    using Profile = crate::controllers::Profile;
    using Translator = crate::controllers::Translator;

    static juce::MidiMessage pass (Translator& t, juce::MidiMessage m, bool* kept = nullptr)
    {
        const bool k = t.translate (m, 10);
        if (kept != nullptr)
            *kept = k;
        return m;
    }

    void runTest() override
    {
        beginTest ("names guess the controller");
        {
            using crate::controllers::guessFor;
            expect (guessFor ("Launchkey MK3 61 LKMK3 MIDI Out").kind == Kind::launchkey);
            expect (guessFor ("Launchkey MK3 61 LKMK3 DAW Out").kind == Kind::launchkey);
            expect (guessFor ("Launchpad X LPX MIDI Out").kind == Kind::pads);
            expect (guessFor ("MPD218 Port A").kind == Kind::pads);
            expect (guessFor ("APC mini mk2 Control").kind == Kind::pads);
            expect (guessFor ("APC mini mk2 Control").layout == crate::controllers::Layout::rows);
            expectEquals (guessFor ("APC mini mk2 Control").firstPadNote, 0);
            expect (guessFor ("MPK mini 3").kind == Kind::keysAndPads);
            expectEquals (guessFor ("MPK mini 3").knobs[0], 70);
            expect (guessFor ("KeyStep 37").kind == Kind::keys);
            expect (guessFor ("Some USB Keyboard").kind == Kind::keysAndPads, "unknown: as Kaleidobox always read keyboards");
        }

        beginTest ("a Launchkey comes through exactly as before");
        {
            Translator t;
            t.setProfile (crate::controllers::guessFor ("Launchkey MK3 61"));
            for (auto m : { juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100), juce::MidiMessage::noteOn (10, 51, (juce::uint8) 100),
                            juce::MidiMessage::noteOn (1, 60, (juce::uint8) 90), juce::MidiMessage::noteOff (1, 60),
                            juce::MidiMessage::controllerEvent (1, 21, 64), juce::MidiMessage::controllerEvent (16, 104, 127),
                            juce::MidiMessage::pitchWheel (1, 9000), juce::MidiMessage::aftertouchChange (10, 40, 50) })
            {
                bool kept = false;
                const auto out = pass (t, m, &kept);
                expect (kept);
                expect (out.getRawDataSize() == m.getRawDataSize()
                        && std::memcmp (out.getRawData(), m.getRawData(), (size_t) m.getRawDataSize()) == 0,
                        "unchanged: " + m.getDescription());
            }
        }

        beginTest ("a Launchpad's or MPD's pads play the MPC's pads on any channel");
        {
            Translator t;
            t.setProfile (crate::controllers::guessFor ("MPD218"));
            auto out = pass (t, juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100));
            expectEquals (out.getChannel(), 10);
            expectEquals (out.getNoteNumber(), 36);
            out = pass (t, juce::MidiMessage::noteOn (3, 99, (juce::uint8) 100));
            expectEquals (out.getChannel(), 10);
            expectEquals (out.getNoteNumber(), 99, "pad 64: the fourth bank");
            out = pass (t, juce::MidiMessage::noteOn (10, 20, (juce::uint8) 100));
            expectEquals (out.getChannel(), 1, "a note below the pads is a key, off the pads' channel");
        }

        beginTest ("an APC mini's rows of eight become the MPC's 4x4 banks");
        {
            Translator t;
            t.setProfile (crate::controllers::guessFor ("APC mini"));
            auto pad = [&t] (int note) { return pass (t, juce::MidiMessage::noteOn (1, note, (juce::uint8) 127)).getNoteNumber() - 36; };
            expectEquals (pad (0), 0, "bottom-left corner: pad 1 of the first bank");
            expectEquals (pad (3), 3);
            expectEquals (pad (8), 4, "second row up: pad 5");
            expectEquals (pad (4), 16, "bottom row, fifth across: the second bank's pad 1");
            expectEquals (pad (32), 32, "fifth row: the third bank");
            expectEquals (pad (63), 63, "top right: the fourth bank's pad 16");
        }

        beginTest ("a plain keyboard's notes are keys, even on channel 10, and the octave moves them");
        {
            Translator t;
            auto p = Profile::defaultsFor (Kind::keys);
            p.octave = -2;
            t.setProfile (p);
            auto out = pass (t, juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100));
            expectEquals (out.getChannel(), 1);
            expectEquals (out.getNoteNumber(), 36);
            // The octave changes while the key is down: letting go still stops the note it started.
            p.octave = 0;
            t.setProfile (p);
            out = pass (t, juce::MidiMessage::noteOff (10, 60));
            expect (out.isNoteOff());
            expectEquals (out.getNoteNumber(), 36);
            out = pass (t, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100));
            expectEquals (out.getNoteNumber(), 60);
            expectEquals (out.getChannel(), 2, "other channels stay");
        }

        beginTest ("knobs: the controller's CCs turn knobs 1-8 (CC 21-28)");
        {
            Translator t;
            t.setProfile (crate::controllers::guessFor ("MPK mini 3"));
            auto out = pass (t, juce::MidiMessage::controllerEvent (1, 72, 99));
            expectEquals (out.getControllerNumber(), 23);
            expectEquals (out.getControllerValue(), 99);
            out = pass (t, juce::MidiMessage::controllerEvent (1, 7, 99));
            expectEquals (out.getControllerNumber(), 7, "not a knob: as it is (Learn can take it)");
        }

        beginTest ("Learn pad 1 and Learn knobs");
        {
            Translator t;
            t.setProfile (Profile::defaultsFor (Kind::keysAndPads));
            t.learn (Translator::Learn::pad);
            bool kept = true;
            pass (t, juce::MidiMessage::noteOn (2, 48, (juce::uint8) 100), &kept);
            expect (! kept, "the learning hit doesn't play");
            expect (t.takeLearned());
            expectEquals (t.getProfile().firstPadNote, 48);
            expectEquals (t.getProfile().padChannel, 2);
            auto out = pass (t, juce::MidiMessage::noteOn (2, 50, (juce::uint8) 100));
            expectEquals (out.getChannel(), 10);
            expectEquals (out.getNoteNumber(), 38, "pad 3");

            t.learn (Translator::Learn::knobs);
            for (int cc : { 14, 14, 15, 16 })
                pass (t, juce::MidiMessage::controllerEvent (1, cc, 10), &kept);
            expect (! kept);
            expectEquals (t.knobsLearned(), 3, "the same knob twice is one knob");
            t.stopLearning();
            expect (t.takeLearned());
            expectEquals (t.getProfile().knobs[1], 15);
            expectEquals (t.getProfile().knobs[3], -1, "the rest have none");
            out = pass (t, juce::MidiMessage::controllerEvent (1, 16, 5));
            expectEquals (out.getControllerNumber(), 23);
        }

        beginTest ("Don't listen drops everything; profiles are saved on the Mac");
        {
            auto file = juce::File::createTempFile (".xml");
            {
                crate::controllers::Setup setup (file);
                expect (! setup.isKnown ("Launchpad Mini"));
                auto p = setup.profileFor ("Launchpad Mini");
                expect (p.kind == Kind::pads);
                p.firstPadNote = 11;
                setup.setProfile ("Launchpad Mini", p);
                setup.setProfile ("Clock box", Profile::defaultsFor (Kind::off));
                auto m = juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100);
                expect (! setup.translate ("Clock box", m, 10));
            }
            crate::controllers::Setup again (file);
            expect (again.isKnown ("Launchpad Mini"));
            expectEquals (again.profileFor ("Launchpad Mini").firstPadNote, 11);
            expect (! again.isOn ("Clock box"));
            file.deleteFile();
        }

        beginTest ("the engine reads each controller as what it is");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& setup = engine.getControllers();
            // (A test's own setup, so nothing is written to the real one.)
            setup.translatorFor ("Test pads").setProfile (Profile::defaultsFor (Kind::pads));
            juce::MidiMessage m = juce::MidiMessage::noteOn (4, 37, (juce::uint8) 100);
            expect (setup.translate ("Test pads", m, engine.getRouting().padChannel.load()));
            expectEquals (m.getChannel(), engine.getRouting().padChannel.load());
            expectEquals (engine.getRouting().mpcNoteForPad (m.getNoteNumber()), engine.getRouting().mpcNoteForPad (37),
                          "the MPC's pad 2, as from a Launchkey");
        }

        beginTest ("the picker: a row per controller, every button does something");
        {
            crate::controllers::Setup setup;
            juce::StringArray plugged { "Launchkey MK3 61", "MPD218" };
            int changed = 0;
            ControllersPanel panel (setup, [&plugged] { return plugged; }, [&changed] { ++changed; });
            panel.setSize (820, 640);
            expectEquals (panel.getNumRows(), 2);
            expect (setup.isKnown ("MPD218"), "seen in the picker: it won't pop up again");

            auto* mpd = panel.getRow ("MPD218");
            expect (mpd != nullptr);
            expect (mpd->layout.isVisible() && mpd->learnPad.isVisible() && ! mpd->octave.isVisible());

            mpd->learnPad.onClick();
            expect (setup.translatorFor ("MPD218").learning() == Translator::Learn::pad);
            mpd->learnPad.onClick();
            expect (setup.translatorFor ("MPD218").learning() == Translator::Learn::none, "Cancel stops it");

            mpd->learnKnobs.onClick();
            expect (setup.translatorFor ("MPD218").learning() == Translator::Learn::knobs);
            mpd->learnKnobs.onClick();
            expect (setup.translatorFor ("MPD218").learning() == Translator::Learn::none);

            mpd->layout.setSelectedId (2, juce::sendNotificationSync);
            expect (setup.profileFor ("MPD218").layout == crate::controllers::Layout::rows);
            mpd->reset.onClick();
            expect (setup.profileFor ("MPD218").layout == crate::controllers::Layout::blocks, "Defaults");

            mpd->kind.setSelectedId ((int) Kind::keys + 1, juce::sendNotificationSync);
            expect (setup.profileFor ("MPD218").kind == Kind::keys);
            expect (mpd->octave.isVisible() && ! mpd->learnPad.isVisible());
            mpd->octave.setSelectedId (5, juce::sendNotificationSync);
            expectEquals (setup.profileFor ("MPD218").octave, 1);

            mpd->kind.setSelectedId ((int) Kind::off + 1, juce::sendNotificationSync);
            expectEquals (changed, 1, "switching it off closes it");
            expect (! mpd->learnKnobs.isVisible());

            // CRATE_CONTROLLERS_SNAPSHOT=/path/to.png saves a picture of the picker (a friend's
            // MPK mini just plugged in, beside a Launchkey and an APC mini).
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_CONTROLLERS_SNAPSHOT", {}); path.isNotEmpty())
            {
                crate::controllers::Setup shot;
                ControllersPanel picture (shot, [] { return juce::StringArray { "Launchkey MK3 61 LKMK3 MIDI Out", "APC mini mk2 Control", "MPK mini 3" }; });
                picture.setNewDevice ("MPK mini 3");
                picture.setSize (820, 860);
                shot.translatorFor ("APC mini mk2 Control").learn (Translator::Learn::knobs);
                for (auto* r : { picture.getRow ("APC mini mk2 Control"), picture.getRow ("MPK mini 3"), picture.getRow ("Launchkey MK3 61 LKMK3 MIDI Out") })
                    r->tick();
                auto image = picture.createComponentSnapshot (picture.getLocalBounds(), true, 2.0f);
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (image, out);
            }

            bool closed = false;
            panel.onDone = [&closed] { closed = true; };
            panel.getDoneButton().onClick();
            expect (closed);
        }

        beginTest ("drum triggers: named, played by note until learned");
        {
            using crate::controllers::guessFor;
            expect (guessFor ("Kaleidobox Trigger").kind == Kind::triggers);
            expect (guessFor ("Arduino Leonardo").kind == Kind::triggers);
            expect (guessFor ("TD-17").kind == Kind::triggers);
            expect (guessFor ("Alesis Strike MultiPad").kind == Kind::triggers);
            expect (guessFor ("Trigger Finger Pro").kind == Kind::pads, "the M-Audio pad controller is still pads");

            Translator t;
            t.setProfile (Profile::defaultsFor (Kind::triggers));
            bool kept = false;
            auto out = pass (t, juce::MidiMessage::noteOn (10, 38, (juce::uint8) 90), &kept);
            expect (kept);
            expectEquals (out.getChannel(), 10);
            expectEquals (out.getNoteNumber(), 38, "note 38 is pad 3 until learned");
            expectEquals ((int) out.getVelocity(), 90, "as played");
            out = pass (t, juce::MidiMessage::noteOn (1, 37, (juce::uint8) 90));
            expectEquals (out.getChannel(), 10, "any channel: onto the pads' channel");
            pass (t, juce::MidiMessage::noteOn (10, 20, (juce::uint8) 90), &kept);
            expect (! kept, "a note that isn't a pad plays nothing (not a key)");
            pass (t, juce::MidiMessage::aftertouchChange (10, 38, 40), &kept);
            expect (! kept);
            pass (t, juce::MidiMessage::controllerEvent (10, 4, 100), &kept);
            expect (kept, "a hi-hat pedal's CC comes through");
        }

        beginTest ("drum triggers: hit each drum to learn it onto the next pad");
        {
            Translator t;
            t.setProfile (Profile::defaultsFor (Kind::triggers));
            t.learn (Translator::Learn::triggers, 4);   // starting at pad 5
            bool kept = false;
            auto out = pass (t, juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100), &kept);
            expect (kept, "the hit still plays while learning");
            expectEquals (out.getNoteNumber(), 36 + 4);
            pass (t, juce::MidiMessage::noteOff (10, 60));
            out = pass (t, juce::MidiMessage::noteOn (10, 60, (juce::uint8) 100));
            expectEquals (out.getNoteNumber(), 36 + 4, "hitting the same drum again doesn't take another pad");
            out = pass (t, juce::MidiMessage::noteOn (10, 61, (juce::uint8) 100));
            expectEquals (out.getNoteNumber(), 36 + 5, "the next drum gets the next pad");
            expectEquals (t.nextTriggerTarget(), 6);
            expectEquals (t.triggersLearned(), 2);
            expect (t.takeLearned());
            t.stopLearning();
            const auto p = t.getProfile();
            expectEquals (p.triggerFor (60), 4);
            expectEquals (p.triggerFor (61), 5);
            expect (p.learnedTriggers() == std::vector<int> { 60, 61 });
            out = pass (t, juce::MidiMessage::noteOff (10, 61));
            expectEquals (out.getNoteNumber(), 36 + 5, "its let go stops the same pad");

            // Saved and read back, with the feel.
            auto q = p;
            q.feel = crate::controllers::Feel::soft;
            q.triggers[40] = (juce::int8) crate::controllers::kTriggerNothing;
            const auto back = Profile::fromTree (q.toTree());
            expect (back == q);
            expectEquals (back.triggerFor (40), (int) crate::controllers::kTriggerNothing);
        }

        beginTest ("drum triggers: a drum on a loop layer presses its Rec");
        {
            Translator t;
            auto p = Profile::defaultsFor (Kind::triggers);
            p.triggers[36] = (juce::int8) (crate::controllers::kTriggerFirstLoop + 2);   // loop 3
            t.setProfile (p);
            int loop = -1;
            auto m = juce::MidiMessage::noteOn (10, 36, (juce::uint8) 100);
            expect (! t.translate (m, 10, &loop), "it doesn't play a pad");
            expectEquals (loop, 2);
            loop = -1;
            m = juce::MidiMessage::noteOff (10, 36);
            expect (! t.translate (m, 10, &loop));
            expectEquals (loop, -1, "the let go presses nothing");

            t.learn (Translator::Learn::triggers, crate::controllers::kTriggerFirstLoop + 15);
            m = juce::MidiMessage::noteOn (10, 50, (juce::uint8) 100);
            t.translate (m, 10, &loop);
            expectEquals (loop, 15, "learned onto loop 16");
            expectEquals (t.nextTriggerTarget(), crate::controllers::kTriggerFirstLoop + 15, "the last loop stays the last");
        }

        beginTest ("drum triggers: the feel bends velocity");
        {
            using crate::controllers::applyFeel;
            using crate::controllers::Feel;
            expectEquals (applyFeel (Feel::even, 40), 40);
            expect (applyFeel (Feel::soft, 40) > 40, "light hands: soft hits louder");
            expect (applyFeel (Feel::hard, 40) < 40, "heavy hands: soft hits softer");
            expectEquals (applyFeel (Feel::soft, 127), 127);
            expectEquals (applyFeel (Feel::hard, 1), 1, "never silent");
            Translator t;
            auto p = Profile::defaultsFor (Kind::triggers);
            p.feel = Feel::soft;
            t.setProfile (p);
            const auto out = pass (t, juce::MidiMessage::noteOn (10, 36, (juce::uint8) 40));
            expectEquals ((int) out.getVelocity(), applyFeel (Feel::soft, 40));
        }

        beginTest ("the picker: drum triggers learned hit by hit, a line per drum");
        {
            crate::controllers::Setup setup;
            ControllersPanel panel (setup, [] { return juce::StringArray { "Kaleidobox Trigger" }; });
            panel.setSize (820, 640);
            auto* row = panel.getRow ("Kaleidobox Trigger");
            expect (row != nullptr);
            expect (row->learnTriggers.isVisible() && row->learnAs.isVisible() && row->feel.isVisible());
            expect (! row->learnPad.isVisible() && ! row->learnKnobs.isVisible() && ! row->octave.isVisible());
            expectEquals (row->getNumTriggerLines(), 0);
            const int h0 = row->getHeight();

            row->learnAs.setSelectedId (3, juce::sendNotificationSync);   // pad 2
            row->learnTriggers.onClick();
            auto& t = setup.translatorFor ("Kaleidobox Trigger");
            expect (t.learning() == Translator::Learn::triggers);
            for (int note : { 36, 38, 42 })
            {
                auto m = juce::MidiMessage::noteOn (10, note, (juce::uint8) 100);
                setup.translate ("Kaleidobox Trigger", m, 10);
            }
            row->learnTriggers.onClick();
            expect (t.learning() == Translator::Learn::none, "Stop");
            expect (setup.saveLearned());
            row->refresh();
            panel.resized();
            expectEquals (row->getNumTriggerLines(), 3);
            expect (row->getHeight() > h0, "a line per drum");
            expectEquals (setup.profileFor ("Kaleidobox Trigger").triggerFor (38), 2, "the second drum: pad 3");

            auto* line = row->getTriggerLine (42);
            expect (line != nullptr);
            line->target.setSelectedId (crate::controllers::kTriggerFirstLoop + 2, juce::sendNotificationSync);
            expectEquals (setup.profileFor ("Kaleidobox Trigger").triggerFor (42), (int) crate::controllers::kTriggerFirstLoop, "onto loop 1");

            row->feel.setSelectedId ((int) crate::controllers::Feel::hard + 1, juce::sendNotificationSync);
            expect (setup.profileFor ("Kaleidobox Trigger").feel == crate::controllers::Feel::hard);

            row->forgetTrigger (36);   // (what Forget does, just after the click)
            expectEquals (row->getNumTriggerLines(), 2, "Forget");

            // CRATE_TRIGGERS_SNAPSHOT=/path/to.png saves a picture of a trigger box being learned.
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TRIGGERS_SNAPSHOT", {}); path.isNotEmpty())
            {
                crate::controllers::Setup shot;
                ControllersPanel picture (shot, [] { return juce::StringArray { "Kaleidobox Trigger", "Launchkey MK3 61 LKMK3 MIDI Out" }; });
                picture.setNewDevice ("Kaleidobox Trigger");
                auto& tr = shot.translatorFor ("Kaleidobox Trigger");
                tr.learn (Translator::Learn::triggers, 0);
                for (int note : { 36, 37, 38, 39, 40 })
                {
                    auto m = juce::MidiMessage::noteOn (10, note, (juce::uint8) 100);
                    shot.translate ("Kaleidobox Trigger", m, 10);
                }
                auto p = tr.getProfile();
                p.triggers[40] = (juce::int8) crate::controllers::kTriggerFirstLoop;
                shot.setProfile ("Kaleidobox Trigger", p);
                picture.setSize (820, 900);
                picture.rebuild();
                picture.resized();
                picture.getRow ("Kaleidobox Trigger")->tick();
                auto image = picture.createComponentSnapshot (picture.getLocalBounds(), true, 2.0f);
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }

        beginTest ("Settings has MIDI controllers in the app, not in Logic");
        {
            RigEngine engine;
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showControllers();
            expect (view.getControllersPanel() == nullptr, "in Logic, Logic picks the MIDI");
        }
    }
};

static RigControllersTests rigControllersTests;
