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
            screen.setSize (1400, 560);

            for (int i = 0; i < VocoderScreen::numPedals; ++i)
                expect (! screen.isOn (i), "a new vocoder is just your voice: " + VocoderScreen::pedalName (i));

            screen.stomp (VocoderScreen::robot);
            expect (param (engine, ParamIDs::robotOn) > 0.5f, "Robot");
            screen.turnKnob (VocoderScreen::robot, 3.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::sharpness), 3.0f, 0.01f, "Beep-ness");

            screen.turnKnob (VocoderScreen::monster, 10.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), 0.0f, 0.01f, "Growl waits for the switch");
            screen.stomp (VocoderScreen::monster);
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), -10.0f, 0.01f, "Monster on");
            screen.turnKnob (VocoderScreen::monster, 5.0);
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), -5.0f, 0.01f, "Growl while on");

            screen.stomp (VocoderScreen::vintageMic);
            expect (param (engine, ParamIDs::vintageOn) > 0.5f);
            screen.turnKnob (VocoderScreen::vintageMic, 8.0);
            expectEquals (juce::roundToInt (param (engine, ParamIDs::vintageEra)), 0, "turned all the way up: the 1900s");

            screen.stomp (VocoderScreen::arp);
            expect (screen.isOn (VocoderScreen::arp));
            expectEquals (juce::roundToInt (param (engine, ParamIDs::arpMode)), 1, "with the robot on, a note arp");
            screen.turnKnob (VocoderScreen::arp, 1.0);
            expectEquals (juce::roundToInt (param (engine, ParamIDs::arpRate)), 1, "Speed");

            screen.turnKnob (VocoderScreen::chords, 4.0);
            screen.stomp (VocoderScreen::chords);
            expect (param (engine, ParamIDs::tuneOn) > 0.5f);
            expectEquals (juce::roundToInt (param (engine, ParamIDs::singToPlay)), 3, "one note, a whole chord");
            expectEquals (juce::roundToInt (param (engine, ParamIDs::tuneScale)), 2, "Sad is minor");

            // Bypass all, and back.
            screen.pressBypassAll();
            for (int i = 0; i < VocoderScreen::numPedals; ++i)
                expect (! screen.isOn (i), "bypassed: " + VocoderScreen::pedalName (i));
            expectWithinAbsoluteError (param (engine, ParamIDs::tuneOn), 0.0f, 0.01f);
            screen.pressBypassAll();
            for (int i = 0; i < VocoderScreen::numPedals; ++i)
                expect (screen.isOn (i), "back on: " + VocoderScreen::pedalName (i));
            expectWithinAbsoluteError (param (engine, ParamIDs::formant), -5.0f, 0.01f, "Monster comes back as deep");

            // Off again.
            screen.stomp (VocoderScreen::chords);
            expectEquals (juce::roundToInt (param (engine, ParamIDs::singToPlay)), 0);
            expectWithinAbsoluteError (param (engine, ParamIDs::tuneOn), 0.0f, 0.01f);
        }

        beginTest ("Raw voice, Out and the screen's switches");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            VocoderScreen screen (engine, new juce::Component(), true);
            screen.setSize (1400, 560);

            screen.pressMonitor();
            expect (param (engine, ParamIDs::voiceOn) < 0.5f, "Monitor off");
            screen.pressMonitor();
            expect (param (engine, ParamIDs::voiceOn) > 0.5f);

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
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::vocoder, "Notes from: the keys go to the vocoder");

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
