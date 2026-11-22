#include "TestHelpers.h"

#include "DroneView.h"
#include "GrooveView.h"
#include "RigEngine.h"
#include "RigView.h"

// The Drone box as a track of the Rig: its tool number (after every saved one), its strip,
// effects and looper source, the sound it makes off the clock, its strings ringing to the Synth,
// the show, and its track on the rail with its own screen.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

void setParam (juce::AudioProcessorValueTreeState& params, const char* id, float value)
{
    if (auto* p = params.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float param (const juce::AudioProcessorValueTreeState& params, const char* id)
{
    return params.getRawParameterValue (id)->load();
}

// Plays the Rig for `seconds` with nothing coming in; the loudest it was at the output.
float render (RigEngine& engine, double seconds)
{
    juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
    in.clear();
    float peak = 0.0f;
    for (int b = 0; b < juce::jmax (1, (int) (seconds * kRate / kBlock)); ++b)
    {
        engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
        peak = juce::jmax (peak, out.getMagnitude (0, 0, kBlock), out.getMagnitude (1, 0, kBlock));
    }
    return peak;
}

// The loudest a strip played over `seconds`.
float stripPeak (RigEngine& engine, int strip, double seconds)
{
    engine.getStrip (strip).peak = 0.0f;
    render (engine, seconds);
    return engine.getStrip (strip).peak.exchange (0.0f);
}
// Plays the Rig for `seconds` with a voice (a held note, rich in harmonics, at `hz`) coming in on
// the mic; the loudest the Drone's strip was.
float singInto (RigEngine& engine, double seconds, double hz, float loudness = 0.2f)
{
    juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
    engine.getStrip (RigEngine::droneStrip).peak = 0.0f;
    static double phase = 0.0;
    for (int b = 0; b < juce::jmax (1, (int) (seconds * kRate / kBlock)); ++b)
    {
        for (int i = 0; i < kBlock; ++i)
        {
            float v = 0.0f;
            for (int h = 1; h <= 8; ++h)
                v += (float) std::sin (phase * h) / (float) h;
            phase += juce::MathConstants<double>::twoPi * hz / kRate;
            in.setSample (0, i, v * loudness * 0.6f);
            in.setSample (1, i, v * loudness * 0.6f);
        }
        engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
    }
    return engine.getStrip (RigEngine::droneStrip).peak.exchange (0.0f);
}
}

struct DroneRigTests : juce::UnitTest
{
    DroneRigTests() : juce::UnitTest ("Drone in the Rig", "Rig") {}

    void runTest() override
    {
        beginTest ("tool numbers: every saved one stays put, the drone comes after the racks");
        {
            expectEquals ((int) RigEngine::mpcTool, 0);
            expectEquals ((int) RigEngine::samplerTool, 1);
            expectEquals ((int) RigEngine::vocoderTool, 2);
            expectEquals ((int) RigEngine::bassTool, 3);
            expectEquals ((int) RigEngine::synthTool_, 4);
            expectEquals ((int) RigEngine::drumsTool, 5);
            expectEquals ((int) RigEngine::fxTool, 6);
            const int racks[] = { RigEngine::mpcStrip, RigEngine::samplerStrip, RigEngine::masterStrip, RigEngine::vocoderStrip, RigEngine::bassStrip,
                                  RigEngine::synthStrip, RigEngine::inputStrip, RigEngine::loopsStrip, RigEngine::drumsStrip };
            for (int i = 0; i < 9; ++i)
                expectEquals (RigEngine::fxTool + racks[i], 6 + i, "rack " + juce::String (i) + " keeps its number");
            expectEquals ((int) RigEngine::droneStrip, 9, "the drone's strip comes last");
            expectEquals (RigEngine::fxTool + RigEngine::droneStrip, 15, "its rack after the others");
            expectEquals ((int) RigEngine::droneTool, 16);
            expectEquals ((int) RigEngine::numTools, 17);
            expect (RigEngine::isFxTool (RigEngine::fxTool + RigEngine::droneStrip) && ! RigEngine::isFxTool (RigEngine::droneTool));
            expectEquals ((int) RigLooper::drone, 9, "the looper's sources only grew at the end");
            expectEquals ((int) GrooveView::drone, 8);

            RigEngine engine;
            expect (&engine.getTool (RigEngine::droneTool) == &engine.getDrone(), "the drone's tool is the drone");
            expect (&engine.getTool (RigEngine::fxTool + RigEngine::droneStrip) == &engine.getFx (RigEngine::droneStrip));
            expect (&engine.getTool (RigEngine::fxTool + RigEngine::drumsStrip) == &engine.getFx (RigEngine::drumsStrip));
            expectEquals (RigEngine::toolName (RigEngine::droneTool), juce::String ("Drone"));
            expectEquals (RigEngine::toolName (RigEngine::fxTool + RigEngine::droneStrip), juce::String ("FX Drone"));
            expectEquals (RigEngine::toolName (RigEngine::fxTool + RigEngine::drumsStrip), juce::String ("FX Drum machine"));
            expectEquals (RigEngine::stripName (RigEngine::droneStrip), juce::String ("Drone"));
            expectEquals (RigEngine::stripForSource (RigLooper::drone), (int) RigEngine::droneStrip);
            expectEquals (RigLooper::sourceName (RigLooper::drone), juce::String ("Drone"));
            // Knob learn and automation reach its settings by its number.
            expect (engine.getAutomation().getParameter (RigEngine::droneTool, 0) != nullptr);
            expect (engine.getAutomation().getParameter (RigEngine::numTools, 0) == nullptr);
        }

        beginTest ("off it's silent; on it sounds without Play; its fader and mute work");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& d = engine.getDrone().params;
            expect (! engine.getDrone().isOn(), "a new Rig's drone is off");
            expectLessThan (render (engine, 2.0), 1.0e-6f, "off, nothing comes out");

            setParam (d, "droneOn", 1.0f);
            const float on = render (engine, 3.0);
            expect (! engine.getTransport().isPlaying(), "the clock never started");
            expectGreaterThan (on, 0.02f, "on, the drone sounds at the master");
            expectGreaterThan (stripPeak (engine, RigEngine::droneStrip, 1.0), 0.02f, "on its own strip");

            engine.getStrip (RigEngine::droneStrip).level = 0.0f;
            render (engine, 0.2);
            expectLessThan (render (engine, 1.0), 1.0e-5f, "the fader down silences it");
            engine.getStrip (RigEngine::droneStrip).level = 0.8f;
            render (engine, 0.2);
            expectGreaterThan (render (engine, 1.5), 0.02f, "and back up it's back");
            engine.getStrip (RigEngine::droneStrip).mute = true;
            render (engine, 0.2);
            expectLessThan (render (engine, 1.0), 1.0e-5f, "mute silences it");
            engine.getStrip (RigEngine::droneStrip).mute = false;
            engine.getStrip (RigEngine::droneStrip).level = 0.6f;
            render (engine, 0.5);
            const float lower = stripPeak (engine, RigEngine::droneStrip, 2.0);
            engine.getStrip (RigEngine::droneStrip).level = 0.8f;
            render (engine, 0.5);
            const float unity = stripPeak (engine, RigEngine::droneStrip, 2.0);
            expectGreaterThan (unity, lower * 1.5f, "the fader sets its level");

            // Its effects rack is its own.
            expect (! engine.getFx (RigEngine::droneStrip).isActive());
        }

        beginTest ("its strings ring to the Synth after the Synth has stopped");
        {
            auto ringAfter = [this] (float resonate)
            {
                RigEngine engine;
                engine.prepare (kRate, kBlock);
                auto& d = engine.getDrone().params;
                setParam (d, "droneOn", 0.0f);
                setParam (d, "droneStrings", 0.0f);
                setParam (d, "droneResonate", resonate);
                setParam (d, "droneFeed", (float) DroneProcessor::feedSynth);
                engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
                engine.addMidiNow (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100));
                render (engine, 1.0);
                engine.addMidiNow (juce::MidiMessage::noteOff (1, 60));
                // Until the Synth's own release has died away.
                double waited = 0;
                while (waited < 6.0 && stripPeak (engine, RigEngine::synthStrip, 0.05) > 1.0e-4f)
                    waited += 0.05;
                expectLessThan (waited, 6.0, "the Synth's note dies away");
                return std::pair<float, float> { stripPeak (engine, RigEngine::droneStrip, 0.2), stripPeak (engine, RigEngine::synthStrip, 0.2) };
            };
            const auto [ringing, synthAfter] = ringAfter (0.8f);
            const auto [dry, synthDry] = ringAfter (0.0f);
            logMessage ("drone after the synth: " + juce::String (juce::Decibels::gainToDecibels (ringing), 1) + " dB, synth "
                        + juce::String (juce::Decibels::gainToDecibels (synthAfter), 1) + " dB; Resonate 0: "
                        + juce::String (juce::Decibels::gainToDecibels (dry), 1) + " dB");
            expectLessThan (synthAfter, 1.0e-4f, "the Synth is quiet");
            expectGreaterThan (ringing, 1.0e-3f, "the drone's strings still ring");
            expectGreaterThan (ringing, dry * 10.0f + 1.0e-6f, "and not with Resonate at 0");
            juce::ignoreUnused (synthDry);
        }

        beginTest ("singing into the mic rings the strings, with the Input strip off; not while the mic is off");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& d = engine.getDrone().params;
            setParam (d, "droneResonate", 0.6f);
            setParam (d, "droneFeed", (float) DroneProcessor::feedInput);
            expect (! engine.inputOn.load(), "the Input strip is off, as it is in a new Rig");
            expect (! engine.micOn.load(), "and the mic is off, as it is when the app starts");
            expect (engine.whyDroneDeaf().isNotEmpty(), "the screen can say why it's silent");
            expectLessThan (singInto (engine, 1.5, 130.81), 1.0e-5f, "mic off: the strings don't hear you");

            engine.micOn = true;
            expect (engine.whyDroneDeaf().isEmpty());
            singInto (engine, 0.5, 130.81);
            const float sung = singInto (engine, 1.5, 130.81);
            logMessage ("singing Sa (-14 dBFS in): the strings ring at " + juce::String (juce::Decibels::gainToDecibels (sung), 1) + " dB");
            expectGreaterThan (sung, 0.01f, "mic on: singing Sa rings the strings");

            // The Input strip's own fader and mute don't stop it (the strings hear the mic itself).
            engine.getStrip (RigEngine::inputStrip).mute = true;
            expectGreaterThan (singInto (engine, 1.0, 130.81), 0.01f, "with the Input strip muted too");
            expect (! engine.inputOn.load(), "and the mic isn't put through the speakers by it");

            setParam (d, "droneFeed", (float) DroneProcessor::feedSynth);
            render (engine, 3.0);
            expectLessThan (singInto (engine, 1.0, 130.81), 1.0e-3f, "ringing to the Synth instead, the voice doesn't ring them");
            expect (engine.whyDroneDeaf().isEmpty(), "and there's nothing to warn about");
        }

        beginTest ("its default loudness sits with the other tools");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            setParam (engine.getDrone().params, "droneOn", 1.0f);
            render (engine, 2.0);
            juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
            in.clear();
            double sum = 0;
            long count = 0;
            float peak = 0;
            for (int b = 0; b < (int) (6.0 * kRate / kBlock); ++b)
            {
                engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
                for (int i = 0; i < kBlock; ++i)
                    sum += out.getSample (0, i) * out.getSample (0, i);
                count += kBlock;
                peak = juce::jmax (peak, out.getMagnitude (0, 0, kBlock));
            }
            const float rms = (float) std::sqrt (sum / (double) count);
            logMessage ("the drone alone at 0 dB: peak " + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dB, RMS "
                        + juce::String (juce::Decibels::gainToDecibels (rms), 1) + " dB");
            expectLessThan (juce::Decibels::gainToDecibels (rms), -24.0f, "a bed under the song, not on top of it");
            expectGreaterThan (juce::Decibels::gainToDecibels (rms), -32.0f, "but there");
        }

        beginTest ("the looper records the drone");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            setParam (engine.getDrone().params, "droneOn", 1.0f);
            auto& looper = engine.getLooper();
            looper.getLayer (0).source = (int) RigLooper::drone;
            looper.getLayer (0).bars = 1;
            looper.pressRecord (0);
            engine.getTransport().play();
            render (engine, 5.0);
            expect (looper.getState (0) == RigLooper::State::playing, "a bar of the drone, looping");
            // The drone off and quiet: what's heard is the loop.
            setParam (engine.getDrone().params, "droneOn", 0.0f);
            engine.getStrip (RigEngine::droneStrip).mute = true;
            render (engine, 0.3);
            expectGreaterThan (stripPeak (engine, RigEngine::loopsStrip, 2.0), 0.01f, "the loop has the drone in it");

            // Rec take on the Drone track takes it from the drone too.
            expectEquals (RigEngine::stripForSource (RigLooper::drone), (int) RigEngine::droneStrip);
        }

        beginTest ("saved with the show; New song and an older show bring a new drone");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& d = engine.getDrone().params;
            setParam (d, "droneOn", 1.0f);
            setParam (d, "droneRoot", 4.0f);
            setParam (d, "droneResonate", 0.5f);
            setParam (d, "droneFeed", (float) DroneProcessor::feedDrums);
            engine.getStrip (RigEngine::droneStrip).level = 0.6f;
            engine.getStrip (RigEngine::droneStrip).pan = -0.4f;
            engine.getStrip (RigEngine::bassStrip).level = 0.5f;
            const auto show = engine.saveShow();
            expect (! engine.isBlank(), "a drone that's been set isn't a blank Rig");

            engine.newShow();
            expect (! engine.getDrone().isOn(), "New song: off");
            expectEquals (param (d, "droneRoot"), 0.0f);
            expectEquals (param (d, "droneResonate"), 0.0f);
            expectEquals (engine.getStrip (RigEngine::droneStrip).level.load(), 0.8f);
            expect (engine.isBlank());

            engine.loadShow (show);
            expect (engine.getDrone().isOn(), "the show's drone is on");
            expectEquals (param (d, "droneRoot"), 4.0f);
            expectWithinAbsoluteError (param (d, "droneResonate"), 0.5f, 1.0e-4f);
            expectEquals (engine.getDrone().getFeed(), (int) DroneProcessor::feedDrums);
            expectEquals (engine.getStrip (RigEngine::droneStrip).level.load(), 0.6f);
            expectEquals (engine.getStrip (RigEngine::droneStrip).pan.load(), -0.4f);

            // A show from before the drone: no Drone tool, nine strips.
            auto older = show.createCopy();
            for (int i = older.getNumChildren(); --i >= 0;)
            {
                const auto c = older.getChild (i);
                if ((c.hasType ("Tool") && (c.getProperty ("name") == "Drone" || c.getProperty ("name") == "FX Drone"))
                    || (c.hasType ("Strip") && c.getProperty ("name") == "Drone"))
                    older.removeChild (i, nullptr);
            }
            int strips = 0;
            for (const auto c : older)
                strips += c.hasType ("Strip") ? 1 : 0;
            expectEquals (strips, 9);
            engine.loadShow (older);
            expect (! engine.getDrone().isOn(), "an older show: the drone as a new Rig has it");
            expectEquals (param (d, "droneRoot"), 0.0f);
            expectEquals (engine.getStrip (RigEngine::droneStrip).level.load(), 0.8f);
            expectEquals (engine.getStrip (RigEngine::bassStrip).level.load(), 0.5f, "and the rest of it loads as it was");
            render (engine, 0.5);
        }

        beginTest ("the rail has the Drone track: its card switches it, picking it shows its screen, a click plucks a string");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.screens.setProperty ("skipGuide", true, nullptr);
            RigView view (engine, nullptr);
            view.setSize (1600, 1000);
            view.goTo ("Groove");
            const auto& rail = view.getRail();
            expect (rail.isVisible());
            juce::Array<juce::Component*> cards;
            for (auto* c : rail.getChildren())
                if (dynamic_cast<rigpanels::PanelButton*> (c) == nullptr && c->isVisible())
                    cards.add (c);
            expectEquals (cards.size(), (int) GrooveView::numTracks, "a card for every track");
            checkRail (rail, cards, "1600x1000");
            auto* card = cards.getLast();
            auto* go = dynamic_cast<juce::Button*> (card->getChildComponent (0));
            expect (go != nullptr);
            if (go != nullptr)
            {
                go->onClick();
                expect (engine.getDrone().isOn(), "the card's On switches the drone on");
                go->onClick();
                expect (! engine.getDrone().isOn(), "and off");
            }

            view.railDoubleClicked (GrooveView::drone);
            expectEquals (view.getPage(), juce::String ("Drone"));
            expectEquals (view.getView(), juce::String ("Edit"));
            auto* screen = dynamic_cast<DroneView*> (view.getCurrentContent());
            expect (screen != nullptr && screen->isVisible(), "Edit shows the Drone's screen");
            if (screen == nullptr)
                return;
            for (int s = 0; s < 4; ++s)
                expect (! screen->stringArea (s).isEmpty() && screen->getLocalBounds().contains (screen->stringArea (s)));
            for (auto* c : screen->getChildren())
                if (c->isVisible())
                    expect (screen->getLocalBounds().contains (c->getBounds()) && c->getWidth() > 20 && c->getHeight() > 20,
                            "everything on the Drone's screen fits: " + c->getName() + " " + c->getBounds().toString());

            const int before = engine.getDrone().getPluckCount (1);
            auto mouse = juce::Desktop::getInstance().getMainMouseSource();
            const auto at = screen->stringArea (1).getCentre().toFloat();
            const juce::MouseEvent click (mouse, at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, screen, screen, juce::Time::getCurrentTime(), at,
                                          juce::Time::getCurrentTime(), 1, false);
            screen->mouseDown (click);
            render (engine, 0.1);
            expectEquals (engine.getDrone().getPluckCount (1), before + 1, "clicking a string plucks it (the drone's off, so it wakes for it)");
            expectGreaterThan (engine.getDrone().getRinging (1), 0.0f, "and it rings");

            // The screen's buttons are the drone's settings.
            screen->noteKeys[7]->onClick();
            expectEquals (param (engine.getDrone().params, "droneRoot"), 7.0f, "G");
            screen->pluckButtons[1]->onClick();
            expectEquals (param (engine.getDrone().params, "droneSync"), 1.0f);
            screen->tick();
            if (auto* pace = screen->knobFor ("dronePace"))
                expect (pace->getAlpha() < 0.9f && pace->slider.getTooltip().contains ("Own time"), "Pace is dimmed, and says why");
            screen->pluckButtons[0]->onClick();
            engine.getChords().fillKey = 2;
            screen->chordsKeyButton.onClick();
            expectEquals (param (engine.getDrone().params, "droneRoot"), 2.0f, "the chord builder's key");
            screen->onButton.onClick();
            expect (engine.getDrone().isOn(), "its On");

            // Its volume is its Mixer fader, both ways.
            screen->levelKnob.slider.setValue (0.5, juce::sendNotificationSync);
            expectWithinAbsoluteError (engine.getStrip (RigEngine::droneStrip).level.load(), 0.5f, 1.0e-4f, "Volume turns the drone's fader down");
            engine.getStrip (RigEngine::droneStrip).level = 0.7f;
            screen->tick();
            expectWithinAbsoluteError ((float) screen->levelKnob.slider.getValue(), 0.7f, 1.0e-4f, "and follows the Mixer's fader");
            expect (screen->levelKnob.isVisible() && screen->getLocalBounds().contains (screen->levelKnob.getBounds()));

            // Singing into the strings with the mic off: the screen says so, and its button turns the mic on.
            setParam (engine.getDrone().params, "droneFeed", (float) DroneProcessor::feedInput);
            engine.micOn = false;
            screen->tick();
            expect (screen->micButton.isVisible(), "the mic-off button shows");
            screen->micButton.onClick();
            expect (engine.micOn.load(), "and turns the mic on");
            screen->tick();
            expect (! screen->micButton.isVisible(), "then goes away");
            engine.micOn = false;
            screen->tick();
            expect (screen->micButton.isVisible() && screen->getLocalBounds().contains (screen->micButton.getBounds()), "it fits");

            // The Play view's middle: its strings and ready-made drones.
            view.showView ("Play");
            expectEquals (view.getPage(), juce::String ("Groove"));
            expectEquals (view.getTrack(), (int) GrooveView::drone);

            if (const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TAKES_SNAPSHOT", {}); path.isNotEmpty())
            {
                setParam (engine.getDrone().params, "droneResonate", 0.4f);
                render (engine, 2.5);
                view.showView ("Edit");
                for (int i = 0; i < 6; ++i)
                {
                    render (engine, 0.03);
                    screen->tick();
                }
                view.refresh();
                snapshot (view, path + "-drone.png");
                view.showView ("Play");
                snapshot (view, path + "-drone-play.png");
                view.goTo ("Mixer");
                snapshot (view, path + "-drone-mixer.png");
                view.setSize (1000, 700);
                view.showView ("Edit");
                screen->tick();
                snapshot (view, path + "-drone-small.png");
            }

            // At the smallest window, the rail's cards still don't overlap.
            view.setSize (1000, 700);
            view.goTo ("Groove");
            checkRail (rail, cards, "1000x700");
        }
    }

    void checkRail (const rigpanels::TrackRail& rail, const juce::Array<juce::Component*>& cards, const juce::String& size)
    {
        for (int i = 0; i < cards.size(); ++i)
        {
            const auto b = cards[i]->getBounds();
            // (A card reaches 4 pixels past the rail on the left, for its glow.)
            expect (b.getHeight() >= 24 && b.getY() >= 0 && b.getBottom() <= rail.getHeight(), size + ": card " + juce::String (i) + " fits: " + b.toString());
            if (i > 0)
                expect (cards[i - 1]->getBottom() <= b.getY(), size + ": cards don't overlap");
            // Its buttons stay inside it.
            for (auto* c : cards[i]->getChildren())
                if (c->isVisible())
                    expect (cards[i]->getLocalBounds().contains (c->getBounds()), size + ": card " + juce::String (i) + "'s buttons fit inside it: "
                                                                                        + c->getBounds().toString() + " in " + b.toString());
        }
        expect (cards.getLast()->getBottom() <= rail.recTake.getY(), size + ": the cards sit above Rec take");
    }

    static void snapshot (juce::Component& c, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
};

static DroneRigTests droneRigTests;
