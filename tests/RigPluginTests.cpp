#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigPlugin.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

juce::AudioPlayHead::PositionInfo hostAt (bool playing, double ppq, double bpm, int beatsPerBar = 4)
{
    juce::AudioPlayHead::PositionInfo info;
    info.setIsPlaying (playing);
    info.setPpqPosition (ppq);
    info.setBpm (bpm);
    info.setTimeSignature (juce::AudioPlayHead::TimeSignature { beatsPerBar, 4 });
    info.setTimeInSamples ((juce::int64) (ppq * 60.0 / bpm * kRate));
    return info;
}

struct Host : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override { return info; }
    PositionInfo info;
};

float peakOf (const juce::AudioBuffer<float>& b, int channel = 0)
{
    return b.getMagnitude (channel, 0, b.getNumSamples());
}
}

struct RigPluginTests : juce::UnitTest
{
    RigPluginTests() : juce::UnitTest ("The Rig in Logic", "Rig") {}

    void runTest() override
    {
        beginTest ("follows Logic's tempo, time signature, position and Play/Stop");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& transport = engine.getTransport();
            juce::AudioBuffer<float> out (2, kBlock);

            transport.setHostPosition (hostAt (true, 8.0, 120.0, 3));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expect (transport.isPlaying());
            expectWithinAbsoluteError (transport.getBpm(), 120.0, 1.0e-9);
            expectEquals (transport.getBeatsPerBar(), 3);
            expectWithinAbsoluteError (transport.getPositionInBeats(), 8.0 + kBlock * 2.0 / kRate, 1.0e-6);

            // A jump (Logic's cycle) is followed.
            transport.setHostPosition (hostAt (true, 2.0, 120.0, 3));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expectWithinAbsoluteError (transport.getPositionInBeats(), 2.0 + kBlock * 2.0 / kRate, 1.0e-6);

            // One long Logic block, played in pieces, moves on all the way through.
            transport.setHostPosition (hostAt (true, 4.0, 120.0, 3));
            juce::AudioBuffer<float> big (2, kBlock * 3);
            engine.process (nullptr, 0, big.getArrayOfWritePointers(), 2, kBlock * 3);
            expectWithinAbsoluteError (transport.getPositionInBeats(), 4.0 + kBlock * 3 * 2.0 / kRate, 1.0e-6);

            transport.setHostPosition (hostAt (false, 9.0, 100.0));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expect (! transport.isPlaying());
            expectWithinAbsoluteError (transport.getPositionInBeats(), 0.0, 1.0e-9);
            expectWithinAbsoluteError (transport.getBpm(), 100.0, 1.0e-9);

            // Logic stopped: the Rig's own Play still works.
            transport.play();
            transport.setHostPosition (hostAt (false, 9.0, 100.0));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expect (transport.isPlaying());

            // Logic's count-in, before bar 1, stays quiet... then Logic takes over.
            transport.stop();
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            transport.setHostPosition (hostAt (true, -2.0, 100.0));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expect (! transport.isPlaying());
            transport.setHostPosition (hostAt (true, 0.0, 100.0));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expect (transport.isPlaying());
        }

        beginTest ("a tool on its own output leaves the main mix");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.inputOn = true;
            engine.micOn = true;
            // Only the Input strip hears the input (the vocoder's raw voice and the Sampler would too).
            engine.vocoderOn = false;
            engine.sampleSource = (int) RigEngine::SampleSource::vocoder;
            juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock), stem (2, kBlock);
            for (int i = 0; i < kBlock; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    in.setSample (ch, i, 0.3f * std::sin (i * 0.05f));

            for (int b = 0; b < 4; ++b)
                engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            expectGreaterThan (peakOf (out), 0.1f);

            stem.clear();
            engine.setStemOutput (RigEngine::inputStem, stem.getArrayOfWritePointers());
            for (int b = 0; b < 4; ++b)
                engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            engine.setStemOutput (RigEngine::inputStem, nullptr);
            expectGreaterThan (peakOf (stem), 0.1f);
            expectLessThan (peakOf (out), 1.0e-4f);
        }

        beginTest ("Logic's MIDI comes in");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            juce::AudioBuffer<float> out (2, kBlock);
            engine.lastMidiTime = 0;
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 100);
            engine.setHostMidi (&midi);
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            engine.setHostMidi (nullptr);
            expect (engine.lastMidiTime.load() != 0);
        }

        beginTest ("the plugin: outputs for each tool, the mic in, and the show saved with the song");
        {
            RigPlugin plugin;
            expectEquals (plugin.getBusCount (true), 2);
            expectEquals (plugin.getBusCount (false), 1 + RigEngine::numStems);
            expect (plugin.getBus (false, 0)->isEnabled());
            expect (! plugin.getBus (false, 1)->isEnabled());

            // Logic turns on the side chain and the Input strip's own output.
            expect (plugin.getBus (true, 1)->enable());
            expect (plugin.getBus (false, 1 + RigEngine::inputStem)->enable());
            const int outs = plugin.getTotalNumOutputChannels();
            expectEquals (outs, 4);
            Host host;
            host.info = hostAt (false, 0.0, 120.0);
            plugin.setPlayHead (&host);
            plugin.prepareToPlay (kRate, kBlock);
            plugin.getEngine().inputOn = true;
            plugin.getEngine().vocoderOn = false;
            plugin.getEngine().sampleSource = (int) RigEngine::SampleSource::vocoder;

            juce::AudioBuffer<float> buffer (juce::jmax (outs, plugin.getTotalNumInputChannels()), kBlock);
            expectEquals (plugin.getTotalNumInputChannels(), 2);
            juce::MidiBuffer midi;
            float stemPeak = 0, mainPeak = 0;
            for (int b = 0; b < 4; ++b)
            {
                buffer.clear();
                for (int i = 0; i < kBlock; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, 0.3f * std::sin (i * 0.05f));   // the mic, on the side chain
                plugin.processBlock (buffer, midi);
                mainPeak = juce::jmax (mainPeak, buffer.getMagnitude (0, 0, kBlock));
                stemPeak = juce::jmax (stemPeak, buffer.getMagnitude (2, 0, kBlock));
            }
            expectGreaterThan (stemPeak, 0.1f);
            expectLessThan (mainPeak, 1.0e-4f);
            expectWithinAbsoluteError (plugin.getEngine().getTransport().getBpm(), 120.0, 1.0e-9);

            plugin.getEngine().getStrip (RigEngine::bassStrip).level = 0.33f;
            juce::MemoryBlock state;
            plugin.getStateInformation (state);
            RigPlugin again;
            again.setStateInformation (state.getData(), (int) state.getSize());
            expectWithinAbsoluteError (again.getEngine().getStrip (RigEngine::bassStrip).level.load(), 0.33f, 1.0e-6f);
            plugin.setPlayHead (nullptr);
        }
    }
};

static RigPluginTests rigPluginTests;
