#include "TestHelpers.h"

#include "RigEngine.h"
#include "GrooveView.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

void runBlocks (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        engine.tickScenes();
    }
}

float param (juce::AudioProcessorValueTreeState& p, const char* id) { return p.getRawParameterValue (id)->load(); }

void turnKnob (RigEngine& engine, int cc, int value, int channel = 1)
{
    engine.addMidi (juce::MidiMessage::controllerEvent (channel, cc, value));
    runBlocks (engine, 1);
}
}

struct RigGrooveTests : juce::UnitTest
{
    RigGrooveTests() : juce::UnitTest ("Rig main screen", "Rig") {}

    void runTest() override
    {
        beginTest ("picking a track: the keys play it and the keyboard's knobs turn it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            GrooveView view (engine);
            view.setSize (1400, 740);
            expectEquals (view.getTrack(), (int) GrooveView::vocoder, "starts where the keys already go");

            view.selectTrack (GrooveView::bass);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::bass);
            turnKnob (engine, 21, 127);
            expectWithinAbsoluteError (param (engine.getBass().params, "cutoff"), 8000.0f, 1.0f, "knob 1 is the bass's cutoff");

            view.selectTrack (GrooveView::synth);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::synth);
            turnKnob (engine, 21, 0);
            expectWithinAbsoluteError (param (engine.getSynth().params, "cutoff"), 60.0f, 1.0f, "now it's the synth's");
            expectWithinAbsoluteError (param (engine.getBass().params, "cutoff"), 8000.0f, 1.0f, "the bass stays put");
            turnKnob (engine, 28, 0, 16);
            expectWithinAbsoluteError (param (engine.getSynth().params, "volume"), 0.0f, 0.001f, "knob 8, on any channel");

            // A knob learned onto something else does that instead.
            const auto& list = engine.getBass().getParameters();
            int resonance = -1;
            for (int p = 0; p < list.size(); ++p)
                if (dynamic_cast<juce::AudioProcessorParameterWithID*> (list[p])->paramID == "resonance")
                    resonance = p;
            engine.getRouting().addParameterKnob (1, 22, RigEngine::bassTool, resonance);
            const float synthResonance = param (engine.getSynth().params, "resonance");
            turnKnob (engine, 22, 0);
            expectWithinAbsoluteError (param (engine.getBass().params, "resonance"), 0.0f, 0.001f);
            expectWithinAbsoluteError (param (engine.getSynth().params, "resonance"), synthResonance, 0.001f);

            // The loops have no notes: the keys stay where they were, the knobs turn its effects.
            view.selectTrack (GrooveView::loops);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::synth);
            expectEquals (view.knobFor (0).first, RigEngine::fxTool + RigEngine::loopsStrip);
        }

        beginTest ("each track says why it's quiet");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            engine.micOn = false;
            expect (view.stateOf (GrooveView::vocoder).contains ("Mic is off"));
            engine.micOn = true;
            expect (! view.stateOf (GrooveView::vocoder).contains ("Mic"));
            engine.vocoderOn = false;
            expect (view.stateOf (GrooveView::vocoder).contains ("off"));
            engine.getStrip (RigEngine::bassStrip).mute = true;
            expectEquals (view.stateOf (GrooveView::bass), juce::String ("Muted"));
            engine.getStrip (RigEngine::bassStrip).mute = false;
            expect (view.stateOf (GrooveView::bass).contains ("press Play"));
            expect (view.stateOf (GrooveView::drums).contains ("No sounds"));
            expect (view.stateOf (GrooveView::sampler).contains ("Empty"));
            expect (view.stateOf (GrooveView::loops).contains ("Empty"));
        }

        beginTest ("bass: lighting a step turns the pattern on");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::bass);
            auto& bass = engine.getBass();
            bass.params.getParameter ("sequencer")->setValueNotifyingHost (0.0f);
            expect (view.stateOf (GrooveView::bass).contains ("Pattern off"));
            expect (! bass.getPattern (0).steps[1].on);
            view.pressStep (1);
            expect (bass.getPattern (0).steps[1].on);
            expect (param (bass.params, "sequencer") > 0.5f);
            view.pressStep (1);
            expect (! bass.getPattern (0).steps[1].on, "and off again");

            view.pressPattern (3);
            expectEquals (bass.getSelectedPattern(), 3);
            expect (view.stateOf (GrooveView::bass).contains ("no notes"));
        }

        beginTest ("drums: steps write the MPC's beat, patterns pick it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::drums);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::mpc);
            auto& project = engine.getMpc().getProject();
            view.pressStep (4);
            int notes = 0;
            for (const auto& t : project.currentSequence().tracks)
                for (const auto& n : t.notes)
                {
                    ++notes;
                    expectEquals (n.tick, 4 * seq::kTicksPerBeat / 4);
                    expectEquals (n.pad, 0);
                }
            expectEquals (notes, 1);
            view.pressStep (4);
            expect (project.currentSequence().isEmpty());

            view.pressPattern (2);
            expect (project.getSequences().size() >= 3, "the beat is made when you pick it");
            expectEquals (project.getCurrentSequenceIndex(), 2);
        }

        beginTest ("synth: the row picks how the arp plays");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::synth);
            auto& params = engine.getSynth().params;
            view.pressPattern (2);
            expect (param (params, "arpOn") > 0.5f);
            expectEquals ((int) param (params, "arpMode"), 1);
            view.pressPattern (0);
            expect (param (params, "arpOn") < 0.5f);
            view.pressStep (3);   // on by default: off
            expect (! engine.getSynth().getRhythm().on[3]);
            view.pressStep (3);
            expect (engine.getSynth().getRhythm().on[3]);
            expect (param (params, "arpOn") > 0.5f, "lighting a step turns the arp on");
        }

        beginTest ("song parts: an empty one saves what's playing, a full one plays it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            GrooveView view (engine);
            auto& scenes = engine.getScenes();
            view.pressPart (0);
            expectEquals ((int) scenes.getScenes().size(), 1);
            expectEquals (scenes.getScenes()[0].name, juce::String ("Part A"));
            engine.getStrip (RigEngine::bassStrip).mute = true;
            view.pressPart (5);   // any empty one: the next part
            expectEquals ((int) scenes.getScenes().size(), 2);
            expectEquals (scenes.getScenes()[1].name, juce::String ("Part B"));

            engine.tickScenes();
            view.pressPart (0);
            runBlocks (engine, 2);
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load(), "Part A has the bass playing");
            expectEquals (scenes.getCurrent(), 0);
        }

        beginTest ("loops and vocoder");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::loops);
            view.pressStep (0);
            runBlocks (engine, 1);
            expect (engine.getLooper().getState (0) != RigLooper::State::empty);
            view.pressPattern (1);
            expect (engine.getLooper().getLayer (1).mute.load());

            view.selectTrack (GrooveView::vocoder);
            engine.vocoderOn = false;
            auto& params = engine.getVocoder().params;
            const bool robot = param (params, "robotOn") > 0.5f;
            view.pressStep (0);
            expect ((param (params, "robotOn") > 0.5f) != robot);
            view.pressPattern (1);
            expectEquals (engine.getVocoder().getProgramName (engine.getVocoder().getCurrentProgram()), juce::String ("Classic Robot"));
            expect (engine.vocoderOn.load(), "picking a sound turns the vocoder on");
        }
    }
};

static RigGrooveTests rigGrooveTests;
