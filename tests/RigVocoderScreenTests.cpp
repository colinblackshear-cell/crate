#include "TestHelpers.h"

#include "RigEngine.h"
#include "VocoderScreen.h"
#include "VocoderSettings.h"
#include "Parameters.h"

namespace
{
float param (RigEngine& engine, const char* id) { return engine.getVocoder().params.getRawParameterValue (id)->load(); }

void setParam (RigEngine& engine, const char* id, float value)
{
    auto* p = engine.getVocoder().params.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
}

struct RigVocoderScreenTests : juce::UnitTest
{
    RigVocoderScreenTests() : juce::UnitTest ("Rig Vocoder pedalboard", "Rig") {}

    void runTest() override
    {
        beginTest ("each pedal's footswitch and knob set the vocoder");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.setSize (1400, 600);

            for (int i = 0; i < VocoderScreen::numPedals; ++i)
                expect (! screen.isOn (i), "a new vocoder is just your voice: " + VocoderScreen::pedalName (i));

            screen.stomp (VocoderScreen::robot);
            expect (param (engine, ParamIDs::robotOn) > 0.5f, "Robot");
            screen.turnKnob (VocoderScreen::robot, 3.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::sharpness), 3.0f, 0.01f, "Beep");

            screen.stomp (VocoderScreen::character);
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), -7.0f, 0.01f, "Character comes on as a monster");
            screen.turnKnob (VocoderScreen::character, 8.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), 8.0f, 0.01f, "or a chipmunk");
            screen.stomp (VocoderScreen::character);
            expect (! screen.isOn (VocoderScreen::character));
            screen.stomp (VocoderScreen::character);
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), 8.0f, 0.01f, "comes back as it was");

            screen.stomp (VocoderScreen::vintageMic);
            expect (param (engine, ParamIDs::vintageOn) > 0.5f);

            screen.stomp (VocoderScreen::arp);
            expect (screen.isOn (VocoderScreen::arp));
            expectEquals (juce::roundToInt (param (engine, ParamIDs::arpMode)), 1, "with the robot on, a note arp");
            screen.turnKnob (VocoderScreen::arp, 1.0);
            expectEquals (juce::roundToInt (param (engine, ParamIDs::arpRate)), 1, "Speed");

            screen.stomp (VocoderScreen::harmony);
            expect (param (engine, ParamIDs::tuneOn) > 0.5f, "Harmony sits on Tune");
            expectEquals (juce::roundToInt (param (engine, ParamIDs::singToPlay)), 3, "one note, a whole chord");

            screen.stomp (VocoderScreen::filter);
            expect (screen.isOn (VocoderScreen::filter));
            expect (param (engine, ParamIDs::cutoff) < 19000.0f, "the filter comes on closed enough to hear");

            for (int pedal : { (int) VocoderScreen::dirt, (int) VocoderScreen::chorus, (int) VocoderScreen::echo, (int) VocoderScreen::space })
            {
                screen.stomp (pedal);
                expect (screen.isOn (pedal), VocoderScreen::pedalName (pedal));
            }
            expect (param (engine, ParamIDs::delayMix) > 0.1f && param (engine, ParamIDs::reverbMix) > 0.1f);

            screen.stomp (VocoderScreen::synth);
            expect (param (engine, ParamIDs::drone) > 0.5f, "Synth's footswitch is Drone");

            // Bypass all, and back.
            screen.pressBypassAll();
            for (int i = 0; i < VocoderScreen::numPedals; ++i)
                expect (! screen.isOn (i), "bypassed: " + VocoderScreen::pedalName (i));
            screen.pressBypassAll();
            for (int i : { (int) VocoderScreen::tune, (int) VocoderScreen::harmony, (int) VocoderScreen::robot, (int) VocoderScreen::character,
                           (int) VocoderScreen::synth, (int) VocoderScreen::arp, (int) VocoderScreen::filter, (int) VocoderScreen::vintageMic,
                           (int) VocoderScreen::dirt, (int) VocoderScreen::chorus, (int) VocoderScreen::echo, (int) VocoderScreen::space })
                expect (screen.isOn (i), "back on: " + VocoderScreen::pedalName (i));
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), 8.0f, 0.01f, "Character comes back as it was");

            // Off again.
            screen.stomp (VocoderScreen::harmony);
            expectEquals (juce::roundToInt (param (engine, ParamIDs::singToPlay)), 0);
        }

        beginTest ("the rack shows every setting of the picked pedal");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.setSize (1400, 600);

            int total = 0;
            for (int i = 0; i < VocoderScreen::numPedals; ++i)
            {
                expect (screen.numRackKnobs (i) >= 2, VocoderScreen::pedalName (i));
                total += screen.numRackKnobs (i);
            }
            expect (total >= 70, "every setting of the vocoder has a knob somewhere");
            expectEquals (screen.numRackKnobs (VocoderScreen::synth), 18);

            screen.selectPedal (VocoderScreen::synth);
            expectEquals (screen.getSelectedPedal(), (int) VocoderScreen::synth);
            int shown = 0;
            for (auto* c : screen.getChildren())
                if (auto* k = dynamic_cast<vintage::Knob*> (c); k != nullptr && k->isVisible() && k->name != "Mic gain")
                {
                    ++shown;
                    expect (k->getWidth() >= 60 && k->getHeight() >= 60, "big enough to turn: " + k->name);
                }
            expectEquals (shown, 18, "only the picked pedal's knobs");

            // Turn something deep in the rack, then Reset.
            engine.getVocoder().params.getParameter (ParamIDs::osc2Level)->setValueNotifyingHost (0.8f);
            screen.resetPedal (VocoderScreen::synth);
            expectWithinAbsoluteError (param (engine, ParamIDs::osc2Level), 0.0f, 0.01f, "Reset");

            // Chords: no keys needed.
            screen.selectPedal (VocoderScreen::chords);
            screen.pressChord (4);
            expectEquals (screen.getActiveChord(), 4);
            expect (screen.isOn (VocoderScreen::chords));
            expect (param (engine, ParamIDs::robotOn) > 0.5f, "the robot comes on to sing it");
            screen.pressChord (4);
            expectEquals (screen.getActiveChord(), -1, "again to stop");

            juce::Image image (juce::Image::ARGB, 1400, 600, true);
            juce::Graphics g (image);
            screen.paintEntireComponent (g, false);
        }

        beginTest ("sounds: step, pick, save your own and load it back");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.setSize (1400, 600);

            screen.stepSound (1);
            expectEquals (engine.getVocoder().getCurrentProgram(), 1);
            expectEquals (screen.soundName(), engine.getVocoder().getProgramName (1));
            expect (screen.isOn (VocoderScreen::robot), "a robot sound lights the Robot pedal");
            screen.stepSound (-2);
            expectEquals (engine.getVocoder().getCurrentProgram(), engine.getVocoder().getNumPrograms() - 1, "wraps around");

            const juce::String soundName = "Crate test sound " + juce::String (juce::Random::getSystemRandom().nextInt (1000000));
            screen.turnKnob (VocoderScreen::space, 0.77);
            expect (screen.saveSound (soundName));
            auto file = VocoderScreen::userSoundsFolder().getChildFile (soundName + ".xml");
            expect (file.existsAsFile());
            screen.pickSound (0);
            expect (! screen.isOn (VocoderScreen::space));
            expect (screen.loadSound (file));
            expectWithinAbsoluteError (param (engine, ParamIDs::reverbMix), 0.77f, 0.01f, "your sound comes back");
            expectEquals (screen.soundName(), soundName);
            file.deleteFile();
        }

        beginTest ("Raw voice, Out and the screen's switches");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.setSize (1400, 600);

            // The mic starts off (no feedback through speakers), so you can see your voice but not hear it.
            expect (! engine.micOn.load());
            expect (! screen.isMonitoring(), "the wave shows, but the mic is off: not monitoring");
            engine.getStrip (RigEngine::vocoderStrip).mute = true;
            engine.vocoderOn = false;
            screen.pressMonitor();
            expect (engine.micOn.load(), "Monitor switches the mic on");
            expect (param (engine, ParamIDs::voiceOn) > 0.5f);
            expect (engine.vocoderOn.load() && ! engine.getStrip (RigEngine::vocoderStrip).mute.load(), "and the vocoder back on, unmuted");
            expect (screen.isMonitoring());
            screen.pressMonitor();
            expect (param (engine, ParamIDs::voiceOn) < 0.5f, "Monitor off");
            expect (engine.micOn.load(), "keeps the mic on, so the robot still hears you");
            screen.pressMonitor();
            expect (param (engine, ParamIDs::voiceOn) > 0.5f);

            // With the mic on, the voice really comes out of the vocoder strip.
            {
                juce::AudioBuffer<float> in (1, 480), out (2, 480);
                float sum = 0.0f;
                for (int block = 0; block < 40; ++block)
                {
                    for (int i = 0; i < 480; ++i)
                        in.setSample (0, i, 0.3f * std::sin ((float) (block * 480 + i) * 0.05f));
                    out.clear();
                    engine.process (in.getArrayOfReadPointers(), 1, out.getArrayOfWritePointers(), 2, 480);
                    if (block > 20)
                        sum += out.getMagnitude (0, 0, 480);
                }
                expect (sum > 0.1f, "you hear your voice");
            }

            screen.setDryWet (0.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::robotLevel), 0.0f, 0.01f, "dry");
            expectWithinAbsoluteError (param (engine, ParamIDs::voiceLevel), 0.9f, 0.01f);
            screen.setDryWet (1.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::robotLevel), 1.0f, 0.01f, "wet");
            expectWithinAbsoluteError (param (engine, ParamIDs::voiceLevel), 0.0f, 0.01f);
            screen.setDryWet (0.5);
            expectWithinAbsoluteError (screen.getDryWet(), 0.5, 0.01, "both at full");

            engine.getRouting().keysTo = (int) RigRouting::Dest::bass;
            screen.pressNotesFrom();
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::vocoder, "the keys go to the vocoder");

            screen.pressToLooper();
            expectEquals (screen.getLooperLayer(), 0, "the first free layer");
            expectEquals (engine.getLooper().getLayer (0).source.load(), (int) RigLooper::vocoder, "records the vocoder");

            screen.setStudio (true);
            expect (screen.isStudio());
            screen.setStudio (false);
            expect (! screen.isStudio());
        }

        beginTest ("Mix: your voice against the robot, the out level and the effects, right on the pedalboard");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.setSize (1440, 600);

            // The faders are the vocoder's own levels (the ones Full window's bottom bar used to have).
            auto* voice = dynamic_cast<juce::Slider*> (findByTooltip (screen, "Your voice: how much"));
            auto* robot = dynamic_cast<juce::Slider*> (findByTooltip (screen, "Robot: how loud"));
            auto* level = dynamic_cast<juce::Slider*> (findByTooltip (screen, "Out: the vocoder's level"));
            expect (voice != nullptr && robot != nullptr && level != nullptr, "three faders on the pedalboard");
            if (voice == nullptr || robot == nullptr || level == nullptr)
                return;
            expect (voice->isVisible() && robot->isVisible() && level->isVisible());
            voice->setValue (0.2, juce::sendNotificationSync);
            expectWithinAbsoluteError (param (engine, ParamIDs::voiceLevel), 0.2f, 0.01f, "the voice fader turns your voice down");
            robot->setValue (0.7, juce::sendNotificationSync);
            expectWithinAbsoluteError (param (engine, ParamIDs::robotLevel), 0.7f, 0.01f);
            level->setValue (-6.0, juce::sendNotificationSync);
            expectWithinAbsoluteError (param (engine, ParamIDs::outputGain), -6.0f, 0.05f);
            screen.setDryWet (1.0);
            expectWithinAbsoluteError (voice->getValue(), 0.0, 0.01, "Blend moves the faders");
            expectWithinAbsoluteError (robot->getValue(), 1.0, 0.01);

            // FX: every effect at once, keeping their settings; on is always heard.
            expect (screen.isEffectsOn(), "a new vocoder has its effects switch on");
            screen.pressEffects();
            expect (! screen.isEffectsOn());
            expect (param (engine, ParamIDs::fxOn) < 0.5f);
            screen.pressEffects();
            expect (screen.isEffectsOn());
            expect (screen.isOn (VocoderScreen::space), "with every effect down, Space comes up so it's heard");
            screen.stomp (VocoderScreen::space);
            setParam (engine, ParamIDs::delayMix, 0.4f);
            screen.pressEffects();
            screen.pressEffects();
            expectWithinAbsoluteError (param (engine, ParamIDs::delayMix), 0.4f, 0.01f, "the effects keep their settings");
            expect (! screen.isOn (VocoderScreen::space), "and Space isn't forced up when Echo is already there");
        }

        beginTest ("the robot always has something to sing (as the vocoder's own window did)");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.watchRobot();
            expectEquals (screen.getActiveChord(), -1);
            setParam (engine, ParamIDs::robotOn, 1.0f);
            screen.watchRobot();
            expectEquals (screen.getActiveChord(), 0, "the robot comes on singing the key's own chord");
            expect (screen.isOn (VocoderScreen::chords), "and the Chords pedal shows it");
            screen.pressChord (0);
            setParam (engine, ParamIDs::robotOn, 0.0f);
            screen.watchRobot();
            setParam (engine, ParamIDs::singToPlay, 1.0f);
            setParam (engine, ParamIDs::robotOn, 1.0f);
            screen.watchRobot();
            expectEquals (screen.getActiveChord(), -1, "not with Sing to play: your voice gives the notes");
        }

        beginTest ("Full window: every setting on one page, no Easy / Studio / Advanced");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, true);
            screen.setSize (1440, 640);
            screen.setStudio (true);
            auto& page = screen.getSettings();
            expect (page.isVisible() && page.getWidth() > 1000, "the page fills the screen");

            // Every one of the vocoder's settings is here, apart from the vocoder app's own clock
            // (Kaleidobox keeps the time).
            const auto shown = page.shownIds();
            for (auto* p : engine.getVocoder().getParameters())
                if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto id = ranged->getParameterID();
                    if (id != ParamIDs::tempo && id != ParamIDs::metronome)
                        expect (shown.contains (id), "Full window shows " + id);
                }
            int knobs = 0;
            for (int i = 0; i < VocoderSettings::numSections; ++i)
                knobs += page.numKnobs (i);
            expectEquals (knobs, shown.size() - 7, "a knob for each setting, a switch for each part");
            expectEquals (shown.size(), (int) juce::StringArray (shown).size());
            {
                juce::StringArray unique (shown);
                unique.removeDuplicates (false);
                expectEquals (unique.size(), shown.size(), "each setting once");
            }

            // The parts' switches are the pedals'.
            page.pressSwitch (VocoderSettings::robot);
            expect (screen.isOn (VocoderScreen::robot));
            page.pressSwitch (VocoderSettings::rhythm);
            expect (screen.isOn (VocoderScreen::arp));
            page.pressSwitch (VocoderSettings::vintageMic);
            expect (param (engine, ParamIDs::vintageOn) > 0.5f);
            page.pressSwitch (VocoderSettings::voice);
            expect (screen.isMonitoring(), "Your voice's switch is Monitor");
            page.pressSwitch (VocoderSettings::chords);
            expect (screen.getActiveChord() >= 0, "Chords plays");
            page.pressSwitch (VocoderSettings::chords);
            expectEquals (screen.getActiveChord(), -1, "and stops");

            // Reset: the part's settings back, its switch as it was.
            setParam (engine, ParamIDs::bands, 33.0f);
            page.resetSection (VocoderSettings::robot);
            expectWithinAbsoluteError (param (engine, ParamIDs::bands), 20.0f, 0.5f);
            expect (screen.isOn (VocoderScreen::robot), "reset keeps it on");

            // Raw voice: back to just your voice.
            page.pressRawVoice();
            expect (! screen.isOn (VocoderScreen::robot) && ! screen.isOn (VocoderScreen::arp) && param (engine, ParamIDs::vintageOn) < 0.5f);
            expectEquals (screen.getActiveChord(), -1);

            // The keys, with Hold.
            page.setHold (true);
            expect (page.isHolding());
            page.getKeyState().noteOn (1, 60, 0.8f);
            page.pressStopAll();
            expect (! page.getKeyState().isNoteOn (1, 60), "Stop all lets go of held keys");

            // The pedalboard's controls hide; the sound row stays.
            if (auto* fader = findByTooltip (screen, "Your voice: how much"))
                expect (! fader->isVisible());
            if (auto* sounds = findByTooltip (screen, "Every sound, by kind"))
                expect (sounds->isVisible(), "the sounds stay along the top");

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_VOCODER_SNAPSHOT", {}); path.isNotEmpty())
            {
                setParam (engine, ParamIDs::robotOn, 1.0f);
                setParam (engine, ParamIDs::reverbMix, 0.3f);
                setParam (engine, ParamIDs::voiceLevel, 0.35f);
                screen.pressChord (2);
                vintage::LookAndFeel look;
                screen.setLookAndFeel (&look);
                for (auto [studio, w, h, suffix] : { std::tuple<bool, int, int, const char*> { false, 1440, 600, "-board" }, { true, 1440, 600, "-full" },
                                                     { true, 1920, 900, "-full-big" } })
                {
                    screen.setSize (w, h);
                    screen.setStudio (studio);
                    screen.refresh();
                    page.refresh();
                    const auto image = screen.createComponentSnapshot (screen.getLocalBounds(), true, 1.0f);
                    juce::File file (path + suffix + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
                screen.setLookAndFeel (nullptr);
            }
        }
    }

    static juce::Component* findByTooltip (juce::Component& root, const juce::String& start)
    {
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (c); t != nullptr && t->getTooltip().startsWith (start))
                return c;
            if (auto* found = findByTooltip (*c, start))
                return found;
        }
        return nullptr;
    }
};

static RigVocoderScreenTests rigVocoderScreenTests;
