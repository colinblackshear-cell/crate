#include "TestHelpers.h"

#include "MacroStrip.h"
#include "MixerView.h"
#include "RigEngine.h"
#include "RigTransport.h"

// Step 7 of the deep show: a tempo ramp, and the show's macro knobs. (The FX pad's new
// Drop, Freeze, Grain and Reverse are in RigPerformFxTests.)
struct ShowMovesTests : juce::UnitTest
{
    ShowMovesTests() : juce::UnitTest ("Show moves", "Rig") {}

    void runTest() override
    {
        beginTest ("a tempo ramp glides along the beat, and setting the tempo by hand calls it off");
        {
            RigTransport t;
            t.prepare (48000.0);
            t.setBpm (120.0);
            t.rampTo (140.0, 8.0);
            expectEquals (t.getRampTarget(), 140.0);
            t.beginBlock();
            t.endBlock (480);
            expectEquals (t.getBpm(), 120.0, "stopped: it waits for Play");
            t.play();
            double beats = 0;
            while (t.getPositionInBeats() < 4.0)
            {
                t.beginBlock();
                t.endBlock (480);
                beats = t.getPositionInBeats();
            }
            expectWithinAbsoluteError (t.getBpm(), 130.0, 0.3, "half way, half way there");
            while (t.getPositionInBeats() < 8.5)
            {
                t.beginBlock();
                t.endBlock (480);
            }
            expectEquals (t.getBpm(), 140.0, "there after 8 beats");
            expectEquals (t.getRampTarget(), 0.0, "and done");
            juce::ignoreUnused (beats);

            t.rampTo (100.0, 16.0);
            t.beginBlock();
            t.endBlock (480);
            t.setBpm (126.0);
            expectEquals (t.getRampTarget(), 0.0, "a hand on the tempo stops it");
            t.beginBlock();
            t.endBlock (480);
            expectEquals (t.getBpm(), 126.0);
        }

        beginTest ("macros: Assign, move settings from start to end, then one knob turns them all");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& macros = engine.getMacros();
            const auto& bass = engine.getTool (RigEngine::bassTool).getParameters();
            const auto& synth = engine.getTool (RigEngine::synthTool_).getParameters();
            auto knobOf = [] (const juce::Array<juce::AudioProcessorParameter*>& list, int skip)
            {
                for (int p = 0; p < list.size(); ++p)
                    if (! list[p]->isDiscrete() && ! list[p]->isBoolean() && list[p]->getNumSteps() > 1000 && skip-- == 0)
                        return p;
                return -1;
            };
            const int a = knobOf (bass, 0), b = knobOf (synth, 1);
            expect (a >= 0 && b >= 0);

            MacroStrip strip (engine);
            strip.setBounds (0, 0, 1100, 150);
            strip.pressAssign (2);
            expectEquals (macros.getAssigning(), 2);

            // Move the bass's setting from 0.2 to 0.8, the synth's from 0.9 to 0.3.
            bass[a]->setValueNotifyingHost (0.2f);
            macros.touchBegan (RigEngine::bassTool, a, bass[a]->getValue());
            bass[a]->setValueNotifyingHost (0.8f);
            expect (macros.touchEnded (RigEngine::bassTool, a, bass[a]->getValue()));
            synth[b]->setValueNotifyingHost (0.9f);
            macros.touchBegan (RigEngine::synthTool_, b, synth[b]->getValue());
            synth[b]->setValueNotifyingHost (0.3f);
            expect (macros.touchEnded (RigEngine::synthTool_, b, synth[b]->getValue()));
            // Touched, not moved: not added.
            macros.touchBegan (RigEngine::bassTool, knobOf (bass, 1), 0.5f);
            expect (! macros.touchEnded (RigEngine::bassTool, knobOf (bass, 1), 0.5f));
            expectEquals (macros.getNumTargets (2), 2);
            expectEquals (macros.value[2].load(), 1.0f, "it starts all the way up, where the settings are");
            strip.pressAssign (2);
            expectEquals (macros.getAssigning(), -1, "Done");

            auto approx = [] (juce::AudioProcessorParameter* p) { return p->getValue(); };
            strip.turn (2, 0.0f);
            macros.process();
            expectWithinAbsoluteError (approx (bass[a]), 0.2f, 0.01f);
            expectWithinAbsoluteError (approx (synth[b]), 0.9f, 0.01f);
            strip.turn (2, 0.5f);
            macros.process();
            expectWithinAbsoluteError (approx (bass[a]), 0.5f, 0.01f);
            expectWithinAbsoluteError (approx (synth[b]), 0.6f, 0.01f);

            // Saved with the show.
            macros.setName (2, "Build");
            const auto show = engine.saveShow();
            RigEngine other;
            other.prepare (48000.0, 480);
            other.loadShow (show);
            expectEquals (other.getMacros().getName (2), juce::String ("Build"));
            expectEquals (other.getMacros().getNumTargets (2), 2);
            expectWithinAbsoluteError (other.getMacros().value[2].load(), 0.5f, 1.0e-4f);
            expectEquals (other.getMacros().getName (0), juce::String ("Macro 1"));

            // Taking one off, clearing.
            macros.removeTarget (2, 0);
            expectEquals (macros.getNumTargets (2), 1);
            expect (macros.getTargets (2)[0].tool == RigEngine::synthTool_);
            macros.clear (2);
            expectEquals (macros.getNumTargets (2), 0);

            // A New song starts with none.
            engine.newShow();
            expectEquals (macros.getNumTargets (2), 0);
        }

        beginTest ("the Mixer's FX pad page has the macros under the pad");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            MixerView mixer (engine);
            mixer.setBounds (0, 0, 1600, 1000);
            expect (! mixer.getMacroStrip().isVisible());
            mixer.showPad (true);
            expect (mixer.getMacroStrip().isVisible());
            expect (mixer.getMacroStrip().getBottom() <= 1000 && mixer.getMacroStrip().getY() > mixer.getPad().getBottom());
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TAKES_SNAPSHOT", {}); path.isNotEmpty())
            {
                engine.getMacros().setName (0, "Build");
                RigMacros::Target t;
                t.tool = RigEngine::bassTool;
                engine.getMacros().setTarget (0, t);
                engine.getMacros().value[0] = 0.6f;
                mixer.getPad().setMode (PerformFx::drop);
                mixer.getMacroStrip().update();
                const auto image = mixer.createComponentSnapshot (mixer.getLocalBounds(), true, 1.0f);
                juce::File file (path + "-fxpad.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
            mixer.showPad (false);
            expect (! mixer.getMacroStrip().isVisible());
        }
    }
};

static ShowMovesTests showMovesTests;
