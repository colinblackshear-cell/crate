#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigPlugin.h"
#include "RigPanels.h"
#include "RigView.h"
#include "GrooveView.h"

// Kaleidobox Vocoder, MPC, Drums, Bass and Synth: the box with one tool in it, each on its own
// Logic track, getting its sound from a whole Kaleidobox and sending it back.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 512;

float play (RigPlugin& plugin, const juce::MidiBuffer& notes, int blocks = 8)
{
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> buffer (plugin.getTotalNumOutputChannels(), kBlock);
        buffer.clear();
        juce::MidiBuffer midi;
        if (b == 0)
            midi.addEvents (notes, 0, -1, 0);
        plugin.processBlock (buffer, midi);
        peak = juce::jmax (peak, buffer.getMagnitude (0, 0, kBlock));
    }
    return peak;
}

void setParam (juce::AudioProcessor& tool, const juce::String& id, float plain)
{
    for (auto* p : tool.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p); withId != nullptr && withId->paramID == id)
            withId->setValueNotifyingHost (withId->convertTo0to1 (plain));
}

float getParam (juce::AudioProcessor& tool, const juce::String& id)
{
    for (auto* p : tool.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p); withId != nullptr && withId->paramID == id)
            return withId->convertFrom0to1 (withId->getValue());
    return -1.0f;
}

template <typename T>
void collect (juce::Component& c, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&c))
        out.push_back (t);
    for (auto* child : c.getChildren())
        collect (*child, out);
}
}

struct RigToolPluginTests : juce::UnitTest
{
    RigToolPluginTests() : juce::UnitTest ("Each tool on its own Logic track", "Rig") {}

    void runTest() override
    {
        beginTest ("each one-tool plugin is named for its tool and has one output");
        {
            const std::pair<int, const char*> tools[] = { { RigEngine::mpcStrip, "Kaleidobox MPC" }, { RigEngine::bassStrip, "Kaleidobox Bass" },
                                                          { RigEngine::synthStrip, "Kaleidobox Synth" }, { RigEngine::drumsStrip, "Kaleidobox Drums" },
                                                          { RigEngine::vocoderStrip, "Kaleidobox Vocoder" } };
            for (auto [strip, name] : tools)
            {
                RigPlugin plugin (strip);
                expectEquals (plugin.getName(), juce::String (name));
                expectEquals (plugin.getBusCount (false), 1);
                expectEquals (plugin.getEngine().getOnlyTool(), strip);
            }
            RigPlugin box;
            expectEquals (box.getName(), juce::String ("Kaleidobox"));
            expectEquals (box.getBusCount (false), 1 + (int) RigEngine::numStems);
            expectEquals (box.getEngine().getOnlyTool(), -1);
        }

        beginTest ("every note from Logic plays the plugin's tool, and only that tool runs");
        {
            RigPlugin bass (RigEngine::bassStrip);
            bass.prepareToPlay (kRate, kBlock);
            juce::MidiBuffer notes;
            notes.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 0);
            expectGreaterThan (play (bass, notes), 0.001f, "the bass plays from the keys");
            // Even channel 10 (the Launchkey's pads) plays the bass here, not the MPC.
            juce::MidiBuffer pads;
            pads.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            play (bass, pads, 40);
            juce::MidiBuffer drumPad;
            drumPad.addEvent (juce::MidiMessage::noteOn (10, 40, 1.0f), 0);
            expectGreaterThan (play (bass, drumPad), 0.001f, "pads play the bass too");
            expect (! bass.getEngine().runsStrip (RigEngine::vocoderStrip));
            expect (! bass.getEngine().runsStrip (RigEngine::mpcStrip));
            expect (bass.getEngine().runsStrip (RigEngine::masterStrip));

            RigPlugin synth (RigEngine::synthStrip);
            synth.prepareToPlay (kRate, kBlock);
            expectGreaterThan (play (synth, notes), 0.001f, "the synth plays from the keys");

            RigPlugin mpc (RigEngine::mpcStrip);
            expect (mpc.getEngine().runsStrip (RigEngine::samplerStrip), "the MPC comes with its Sampler");
            expectEquals (mpc.getEngine().getRouting().keysTo.load(), (int) RigRouting::Dest::mpc);
            expectEquals (mpc.getEngine().getRouting().padChannel.load(), 10);

            RigPlugin drums (RigEngine::drumsStrip);
            expectEquals (drums.getEngine().getRouting().padsTo.load(), (int) RigRouting::Dest::drums);
            expectEquals (drums.getEngine().getRouting().keysTo.load(), (int) RigRouting::Dest::drums);
        }

        beginTest ("a show loaded into a one-tool plugin can't send its keys elsewhere");
        {
            RigEngine box;
            box.getRouting().keysTo = (int) RigRouting::Dest::vocoder;
            box.getRouting().splitNote = 48;
            RigPlugin bass (RigEngine::bassStrip);
            bass.getEngine().loadShow (box.saveShow());
            expectEquals (bass.getEngine().getRouting().keysTo.load(), (int) RigRouting::Dest::bass);
            expectEquals (bass.getEngine().getRouting().splitNote.load(), 0);
            bass.getEngine().newShow();
            expectEquals (bass.getEngine().getRouting().keysTo.load(), (int) RigRouting::Dest::bass);
        }

        beginTest ("Get from box and Send to box carry one tool's sound, and nothing else");
        {
            RigPlugin box;
            RigPlugin bass (RigEngine::bassStrip);
            expect (RigEngine::openBoxes().contains (&box.getEngine()));
            expect (! RigEngine::openBoxes().contains (&bass.getEngine()), "a one-tool plugin isn't a box");

            setParam (box.getEngine().getBass(), "cutoff", 2000.0f);
            setParam (box.getEngine().getSynth(), "arpOn", 1.0f);
            box.getEngine().getStrip (RigEngine::bassStrip).level = 0.5f;
            box.getEngine().getStrip (RigEngine::bassStrip).pan = -0.4f;
            expect (bass.getEngine().loadToolSound (box.getEngine().saveToolSound (RigEngine::bassStrip)));
            expectWithinAbsoluteError (getParam (bass.getEngine().getBass(), "cutoff"), 2000.0f, 1.0f);
            expectWithinAbsoluteError (bass.getEngine().getStrip (RigEngine::bassStrip).level.load(), 0.5f, 1.0e-6f);
            expectWithinAbsoluteError (bass.getEngine().getStrip (RigEngine::bassStrip).pan.load(), -0.4f, 1.0e-6f);
            expectLessThan (getParam (bass.getEngine().getSynth(), "arpOn"), 0.5f, "the synth isn't part of the bass's sound");

            // And back: a change in the Bass plugin goes to the box's Bass.
            setParam (bass.getEngine().getBass(), "cutoff", 120.0f);
            expect (box.getEngine().loadToolSound (bass.getEngine().saveToolSound (RigEngine::bassStrip)));
            expectWithinAbsoluteError (getParam (box.getEngine().getBass(), "cutoff"), 120.0f, 1.0f);
            expectGreaterThan (getParam (box.getEngine().getSynth(), "arpOn"), 0.5f, "the box's synth is left as it was");

            // From a saved show too; a show without the tool (or not a show) gives nothing.
            const auto fromShow = RigEngine::toolSoundFromShow (box.getEngine().saveShow(), RigEngine::mpcStrip);
            expect (fromShow.hasType (RigEngine::kToolSoundType));
            expect (fromShow.getChildWithProperty ("name", "Sampler").isValid(), "the MPC's sound has its Sampler");
            expect (! RigEngine::toolSoundFromShow (juce::ValueTree ("Nope"), RigEngine::mpcStrip).isValid());
            expect (! bass.getEngine().loadToolSound (juce::ValueTree ("Nope")));
        }

        beginTest ("songs made with the first Kaleidobox Bass plugin open on the new one");
        {
            BassProcessor old;
            setParam (old, "cutoff", 3000.0f);
            juce::MemoryBlock state;
            old.getStateInformation (state);
            RigPlugin bass (RigEngine::bassStrip);
            bass.setStateInformation (state.getData(), (int) state.getSize());
            expectWithinAbsoluteError (getParam (bass.getEngine().getBass(), "cutoff"), 3000.0f, 1.0f);

            // And its own songs come back as they were.
            juce::MemoryBlock saved;
            bass.getStateInformation (saved);
            RigPlugin again (RigEngine::bassStrip);
            again.setStateInformation (saved.getData(), (int) saved.getSize());
            expectWithinAbsoluteError (getParam (again.getEngine().getBass(), "cutoff"), 3000.0f, 1.0f);
        }

        beginTest ("the one-tool window: its track, Edit and Mix, and the tool menu");
        {
            const std::pair<int, const char*> tools[] = { { RigEngine::vocoderStrip, "Vocoder" }, { RigEngine::mpcStrip, "Sampler" },
                                                          { RigEngine::bassStrip, "Bass" }, { RigEngine::drumsStrip, "Drum machine" } };
            for (auto [strip, page] : tools)
            {
                RigPlugin plugin (strip);
                plugin.prepareToPlay (kRate, kBlock);
                std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
                editor->setSize (1440, 960);
                std::vector<RigView*> views;
                collect (*editor, views);
                expectEquals ((int) views.size(), 1);
                auto& view = *views.front();
                expect (view.toolMode());
                expectEquals (view.getView(), juce::String ("Edit"));
                expectEquals (view.getPage(), juce::String (page));
                expectEquals (GrooveView::stripFor (view.getTrack()), strip);
                view.showView ("Play");
                expectEquals (view.getView(), juce::String ("Edit"), "Play is the whole box's");
                view.showView ("Mix");
                expectEquals (view.getView(), juce::String ("Mix"));

                std::vector<rigpanels::ScreenBar*> bars;
                collect (*editor, bars);
                int shownViews = 0;
                for (auto* b : bars.front()->tabs)
                    shownViews += b->isVisible() ? 1 : 0;
                expectEquals (shownViews, 2);

                // The rail: just the tool's own track (the MPC and its Sampler).
                std::vector<rigpanels::TrackRail*> rails;
                collect (*editor, rails);
                int cards = 0;
                for (auto* c : rails.front()->getChildren())
                    cards += c->isVisible() && dynamic_cast<rigpanels::PanelButton*> (c) == nullptr ? 1 : 0;
                expectEquals (cards, strip == RigEngine::mpcStrip ? 2 : 1);

                // CRATE_PLUGIN_SNAPSHOT=/path/prefix saves each one-tool window (Edit, then its menu's look).
                if (const auto snapshot = juce::SystemStats::getEnvironmentVariable ("CRATE_PLUGIN_SNAPSHOT", {}); snapshot.isNotEmpty())
                {
                    view.showView ("Edit");
                    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                    juce::File file (snapshot + "-tool-" + RigEngine::onlyToolName (strip) + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
        }

        beginTest ("tool sound files: kept from the box, opened in the tool plugin");
        {
            juce::TemporaryFile file (RigEngine::kToolSoundExtension);
            RigEngine box;
            setParam (box.getBass(), "cutoff", 900.0f);
            auto xml = box.saveToolSound (RigEngine::bassStrip).createXml();
            expect (xml != nullptr && xml->writeTo (file.getFile()));

            RigPlugin bass (RigEngine::bassStrip);
            std::unique_ptr<juce::AudioProcessorEditor> editor (bass.createEditor());
            std::vector<RigView*> views;
            collect (*editor, views);
            views.front()->loadToolSoundFile (file.getFile());
            expectWithinAbsoluteError (getParam (bass.getEngine().getBass(), "cutoff"), 900.0f, 1.0f);

            // A Bass sound doesn't go on the Synth plugin.
            RigPlugin synth (RigEngine::synthStrip);
            std::unique_ptr<juce::AudioProcessorEditor> synthEditor (synth.createEditor());
            views.clear();
            collect (*synthEditor, views);
            expect (views.front()->loadToolSoundFile (file.getFile()).contains ("Bass sound"));
        }
    }
};

static RigToolPluginTests rigToolPluginTests;
