#include "TestHelpers.h"

#include "RigEngine.h"
#include "VocoderScreen.h"
#include "Parameters.h"

namespace
{
float param (RigEngine& engine, const char* id) { return engine.getVocoder().params.getRawParameterValue (id)->load(); }
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
            VocoderScreen screen (engine, nullptr, true);
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
            VocoderScreen screen (engine, nullptr, true);
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
            VocoderScreen screen (engine, nullptr, true);
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
            VocoderScreen screen (engine, new juce::Component(), true);
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
    }
};

static RigVocoderScreenTests rigVocoderScreenTests;
