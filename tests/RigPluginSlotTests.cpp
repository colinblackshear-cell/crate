#include "TestHelpers.h"

#include "RigEngine.h"
#include "PluginSlot.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// A stand-in for someone else's plugin: an instrument that plays a square at full level while
// any note is held (and counts the notes it got), or an effect that halves the sound.
class FakePlugin : public juce::AudioPluginInstance
{
public:
    explicit FakePlugin (bool instrument)
        : AudioPluginInstance (instrument ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                          : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())),
          isInstrument (instrument)
    {
    }

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = isInstrument ? "Fake Synth" : "Fake FX";
        d.manufacturerName = "Test";
        d.pluginFormatName = "Fake";
        d.isInstrument = isInstrument;
        d.fileOrIdentifier = d.name;
        d.uniqueId = d.deprecatedUid = isInstrument ? 1 : 2;
        d.numInputChannels = isInstrument ? 0 : 2;
        d.numOutputChannels = 2;
    }

    void prepareToPlay (double, int) override { prepared = true; }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
    {
        for (const auto m : midi)
        {
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
            {
                ++notesOn;
                held = msg.getNoteNumber();
            }
            else if (msg.isNoteOff() && msg.getNoteNumber() == held)
                held = -1;
        }
        if (isInstrument)
        {
            buffer.clear();
            if (held >= 0)
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    for (int i = 0; i < buffer.getNumSamples(); ++i)
                        buffer.setSample (ch, i, (i / 20) % 2 ? 0.5f : -0.5f);
        }
        else
            buffer.applyGain (0.5f);
    }
    using AudioPluginInstance::processBlock;

    const juce::String getName() const override { return isInstrument ? "Fake Synth" : "Fake FX"; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return isInstrument; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 3; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int i) override { program = i; }
    const juce::String getProgramName (int i) override { return "Patch " + juce::String (i + 1); }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& m) override { m.append ("hello", 5); }
    void setStateInformation (const void*, int) override {}

    const bool isInstrument;
    bool prepared = false;
    int notesOn = 0, held = -1, program = 0;
};

float runBlocks (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    float peak = 0;
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        peak = juce::jmax (peak, out.getMagnitude (0, kBlock));
    }
    return peak;
}
}

struct RigPluginSlotTests : juce::UnitTest
{
    RigPluginSlotTests() : juce::UnitTest ("Rig plugin slots", "Rig") {}

    void runTest() override
    {
        beginTest ("an instrument in the Synth's slot plays the synth's keys and arp");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            engine.getSynth().params.getParameter ("volume")->setValueNotifyingHost (0.0f);   // the synth's own sound: silent

            auto plugin = std::make_unique<FakePlugin> (true);
            auto* fake = plugin.get();
            engine.getSynthPlugin().setInstance (std::move (plugin));
            expect (engine.getSynthPlugin().isLoaded());
            expect (fake->prepared);
            expectEquals (engine.getSynthPlugin().getName(), juce::String ("Fake Synth"));

            runBlocks (engine, 2);
            engine.addMidi (juce::MidiMessage::noteOn (1, 60, 0.8f));
            const float peak = runBlocks (engine, 6);
            expect (fake->notesOn >= 1, "the plugin got the key");
            expectEquals (fake->held, 60);
            expect (peak > 0.05f, "and you hear it");
            engine.addMidi (juce::MidiMessage::noteOff (1, 60));
            runBlocks (engine, 4);
            expectEquals (fake->held, -1, "and the key's release");

            // Its sounds.
            expectEquals (engine.getSynthPlugin().numPrograms(), 3);
            engine.getSynthPlugin().setProgram (2);
            expectEquals (engine.getSynthPlugin().currentProgram(), 2);

            // Saved with the show.
            const auto tree = engine.getSynthPlugin().toTree();
            expect (tree.hasProperty ("description") && tree.hasProperty ("state"));
            expect (engine.saveShow().getChildWithProperty ("tool", "Synth").isValid(), "the show keeps it");

            engine.getSynthPlugin().clear();
            expect (! engine.getSynthPlugin().isLoaded());
        }

        beginTest ("the Bass's pattern plays the plugin in its slot");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& bass = engine.getBass();
            auto p = bass.getPattern (0);
            for (int k = 0; k < 16; k += 2)
                p.steps[(size_t) k].on = true;
            bass.setPattern (0, p);
            bass.params.getParameter ("sequencer")->setValueNotifyingHost (1.0f);
            auto plugin = std::make_unique<FakePlugin> (true);
            auto* fake = plugin.get();
            engine.getBassPlugin().setInstance (std::move (plugin));
            engine.getTransport().play();
            runBlocks (engine, 100);   // a second: a bar of steps at 120
            expect (fake->notesOn >= 4, "each lit step is a note for the plugin: " + juce::String (fake->notesOn));

            engine.getBassPlugin().bypassed = true;
            const int before = fake->notesOn;
            runBlocks (engine, 50);
            expectEquals (fake->notesOn, before, "bypassed: the bass's own sound plays");
        }

        beginTest ("an effect in a strip's or a loop layer's effects");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& fx = engine.getFx (RigEngine::synthStrip);
            expect (! fx.isActive());
            fx.plugin.setInstance (std::make_unique<FakePlugin> (false));
            expect (fx.isActive(), "a plugin lights the strip's FX");

            juce::AudioBuffer<float> b (2, kBlock);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (b.getWritePointer (ch), 0.8f, kBlock);
            juce::MidiBuffer none;
            fx.processBlock (b, none);
            expectWithinAbsoluteError (b.getSample (0, 100), 0.4f, 0.001f, "the plugin works on the sound");

            // Saved and loaded with the strip (the plugin is made again from its description;
            // the fake one can't be, so loading it leaves the slot empty but doesn't fail).
            juce::MemoryBlock state;
            fx.getStateInformation (state);
            auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
            expect (xml != nullptr && xml->getChildByName ("Plugin") != nullptr, "the strip's state keeps the plugin");
            if (xml != nullptr && xml->getChildByName ("Plugin") != nullptr)
                expectEquals (xml->getChildByName ("Plugin")->getStringAttribute ("name"), juce::String ("Fake FX"));

            engine.getLoopFx (0).plugin.setInstance (std::make_unique<FakePlugin> (false));
            expect (engine.getLoopFx (0).isActive());
            engine.getLoopFx (0).plugin.clear();
            expect (! engine.getLoopFx (0).isActive());
        }

        beginTest ("the host lists what's installed by kind");
        {
            juce::SharedResourcePointer<PluginHost> host;
            for (const auto& d : host->instruments())
                expect (d.isInstrument);
            for (const auto& d : host->effects())
                expect (! d.isInstrument);
        }
    }
};

static RigPluginSlotTests rigPluginSlotTests;
