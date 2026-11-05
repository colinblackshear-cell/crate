#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigRouting.h"
#include "RigTransport.h"

using namespace crate;

namespace
{
// Runs the engine for `blocks` blocks of `size` and returns the output's peak.
float runEngine (RigEngine& engine, int blocks, int size = 512, const juce::AudioBuffer<float>* input = nullptr)
{
    juce::AudioBuffer<float> out (2, size);
    float peak = 0;
    for (int b = 0; b < blocks; ++b)
    {
        const float* const* in = input != nullptr ? input->getArrayOfReadPointers() : nullptr;
        engine.process (in, input != nullptr ? input->getNumChannels() : 0, out.getArrayOfWritePointers(), 2, size);
        peak = juce::jmax (peak, out.getMagnitude (0, 0, size), out.getMagnitude (1, 0, size));
    }
    return peak;
}

void loadPadTone (RigEngine& engine, int pad)
{
    auto& project = engine.getMpc().getProject();
    project.currentBank().pads[(size_t) pad].audio = share (sine (44100, 1, 0.5f));
    project.currentBank().pads[(size_t) pad].name = "tone";
    project.changed();
    project.dispatchPendingMessages();
}
}

struct RigTransportTests : juce::UnitTest
{
    RigTransportTests() : juce::UnitTest ("Rig transport", "Rig") {}

    void runTest() override
    {
        beginTest ("plays at the tempo and stops back at the start");
        RigTransport t;
        t.prepare (48000.0);
        t.setBpm (120.0);
        t.beginBlock();
        t.endBlock (48000);
        expect (t.getPositionInBeats() == 0.0, "doesn't move while stopped");

        t.play();
        t.beginBlock();
        expect (t.isPlaying());
        expect (t.getPosition()->getIsPlaying());
        t.endBlock (48000);   // one second at 120 BPM
        expectWithinAbsoluteError (t.getPositionInBeats(), 2.0, 1.0e-9);
        expectWithinAbsoluteError (*t.getPosition()->getPpqPosition(), 2.0, 1.0e-9);
        expectWithinAbsoluteError (*t.getPosition()->getBpm(), 120.0, 1.0e-9);

        t.setBeatsPerBar (4);
        for (int i = 0; i < 3; ++i)
        {
            t.beginBlock();
            t.endBlock (48000);
        }
        expectWithinAbsoluteError (*t.getPosition()->getPpqPositionOfLastBarStart(), 8.0, 1.0e-9);

        t.stop();
        t.beginBlock();
        expect (! t.isPlaying());
        expect (*t.getPosition()->getPpqPosition() == 0.0);

        beginTest ("tempo stays in range");
        t.setBpm (1000.0);
        expect (t.getBpm() == RigTransport::kMaxBpm);
        t.setBpm (1.0);
        expect (t.getBpm() == RigTransport::kMinBpm);

        beginTest ("the metronome clicks on each beat while playing");
        RigTransport m;
        m.prepare (44100.0);
        m.setBpm (60.0);              // a beat a second
        m.metronome = true;
        juce::AudioBuffer<float> out (2, 441);
        std::vector<int> clickStarts;
        m.play();
        for (int b = 0; b < 300; ++b)  // three seconds
        {
            out.clear();
            m.beginBlock();
            m.endBlock (441, &out);
            if (out.getMagnitude (0, 0, 441) > 0.01f)
                if (clickStarts.empty() || b - clickStarts.back() > 10)
                    clickStarts.push_back (b);
        }
        expectEquals ((int) clickStarts.size(), 3);
        if (clickStarts.size() == 3)
        {
            expectEquals (clickStarts[0], 0);
            expectEquals (clickStarts[1], 100);
            expectEquals (clickStarts[2], 200);
        }

        m.metronome = false;
        float peak = 0;
        for (int b = 0; b < 200; ++b)
        {
            out.clear();
            m.beginBlock();
            m.endBlock (441, &out);
            peak = juce::jmax (peak, out.getMagnitude (0, 0, 441));
        }
        expect (peak < 1.0e-6f, "quiet with the metronome off");
    }
};

struct RigRoutingTests : juce::UnitTest
{
    RigRoutingTests() : juce::UnitTest ("Rig routing", "Rig") {}

    void runTest() override
    {
        beginTest ("drum pads play the MPC's pads, keys play the vocoder or the sampler");
        RigRouting r;
        expect (r.keysTo.load() == (int) RigRouting::Dest::vocoder, "the keys play the vocoder to start with");
        {
            juce::MidiBuffer keysIn, m, smp, voc, b, sy;
            std::array<juce::MidiBuffer*, RigRouting::kNumDests> dests { nullptr, &m, &smp, &voc, &b, &sy };
            keysIn.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            RigRouting::Result res;
            r.route (keysIn, dests, res);
            expect (voc.getNumEvents() == 1 && smp.getNumEvents() == 0 && m.getNumEvents() == 0);
        }
        r.keysTo = (int) RigRouting::Dest::sampler;
        juce::MidiBuffer in, mpc, sampler, vocoder;
        juce::MidiBuffer unusedBass, unusedSynth;
        std::array<juce::MidiBuffer*, RigRouting::kNumDests> to { nullptr, &mpc, &sampler, &vocoder, &unusedBass, &unusedSynth };
        in.addEvent (juce::MidiMessage::noteOn (10, 36, 1.0f), 0);   // bottom-left pad
        in.addEvent (juce::MidiMessage::noteOn (10, 51, 1.0f), 1);   // top-right pad
        in.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 2);    // middle C
        RigRouting::Result result;
        r.route (in, to, result);

        std::vector<int> mpcNotes, samplerNotes;
        for (const auto m : mpc)
            mpcNotes.push_back (m.getMessage().getNoteNumber());
        for (const auto m : sampler)
            samplerNotes.push_back (m.getMessage().getNoteNumber());
        expect (mpcNotes == std::vector<int> { noteForPad (0), noteForPad (15) });
        expect (samplerNotes == std::vector<int> { 60 });

        beginTest ("the pad bank picks which 16 pads");
        r.padBank = 2;   // C
        expectEquals (r.mpcNoteForPad (36), noteForPad (32));
        expectEquals (r.mpcNoteForPad (35), -1);

        beginTest ("keys can play the MPC, or nothing");
        r.keysTo = (int) RigRouting::Dest::mpc;
        in.clear(); mpc.clear(); sampler.clear();
        in.addEvent (juce::MidiMessage::noteOn (1, noteForPad (3), 1.0f), 0);
        r.route (in, to, result);
        expectEquals (mpc.getNumEvents(), 1);
        expectEquals (sampler.getNumEvents(), 0);
        r.keysTo = (int) RigRouting::Dest::off;
        mpc.clear();
        r.route (in, to, result);
        expectEquals (mpc.getNumEvents() + sampler.getNumEvents(), 0);

        beginTest ("learn a fader, then it moves the level");
        r.learn (RigRouting::mpcLevel);
        in.clear(); mpc.clear(); sampler.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 53, 40), 0);
        r.route (in, to, result);
        expectEquals (r.getLearning(), -1);
        expect (r.getAssignment (RigRouting::mpcLevel) == std::pair<int, int> { 1, 53 });
        expect (result.values[RigRouting::mpcLevel] < 0.0f, "the learning move itself changes nothing");
        expectEquals (mpc.getNumEvents() + sampler.getNumEvents(), 0, "knobs aren't passed on as notes");

        in.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 53, 127), 0);
        r.route (in, to, result);
        expectWithinAbsoluteError (result.values[RigRouting::mpcLevel], 1.0f, 1.0e-6f);

        beginTest ("learn a button: presses count, releases don't");
        r.learn (RigRouting::play);
        in.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 115, 0), 0);    // a release first: ignored
        in.addEvent (juce::MidiMessage::controllerEvent (1, 115, 127), 1);  // learned on the press
        in.addEvent (juce::MidiMessage::controllerEvent (1, 115, 0), 2);
        r.route (in, to, result);
        expect (r.getAssignment (RigRouting::play) == std::pair<int, int> { 1, 115 });
        expectEquals (result.presses[RigRouting::play], 0);
        in.clear();
        in.addEvent (juce::MidiMessage::controllerEvent (1, 115, 127), 0);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 115, 0), 10);
        in.addEvent (juce::MidiMessage::controllerEvent (1, 115, 127), 20);
        r.route (in, to, result);
        expectEquals (result.presses[RigRouting::play], 2);

        beginTest ("one knob controls one thing");
        r.setAssignment (RigRouting::samplerLevel, 1, 53);
        expect (r.getAssignment (RigRouting::mpcLevel).second == -1, "the MPC fader let go of CC 53");
        r.forget (RigRouting::samplerLevel);
        expect (r.getAssignment (RigRouting::samplerLevel).second == -1);
    }
};

struct RigEngineTests : juce::UnitTest
{
    RigEngineTests() : juce::UnitTest ("Rig engine", "Rig") {}

    void runTest() override
    {
        beginTest ("a drum pad plays the MPC's pad through the mixer");
        {
            RigEngine engine;
            engine.prepare (44100.0, 512);
            loadPadTone (engine, 0);
            expect (runEngine (engine, 4) < 1.0e-6f, "silent before anything plays");

            engine.addMidiNow (juce::MidiMessage::noteOn (10, 36, 1.0f));
            expect (runEngine (engine, 4) > 0.2f, "the pad plays");
            expect ((engine.getMpc().takePlayedPads() & 1u) != 0, "pad A01 lit");
            expect (engine.getStrip (RigEngine::mpcStrip).peak.load() > 0.2f, "the MPC strip meters it");

            beginTest ("mute and the master fader");
            engine.getMpc().stopAll();
            runEngine (engine, 10);
            engine.getStrip (RigEngine::mpcStrip).mute = true;
            engine.addMidiNow (juce::MidiMessage::noteOn (10, 36, 1.0f));
            runEngine (engine, 2);    // the mute fades in over 20 ms
            expect (runEngine (engine, 4) < 1.0e-4f, "a muted strip is silent");

            engine.getStrip (RigEngine::mpcStrip).mute = false;
            engine.getStrip (RigEngine::masterStrip).level = 0.0f;
            engine.addMidiNow (juce::MidiMessage::noteOn (10, 36, 1.0f));
            runEngine (engine, 2);
            expect (runEngine (engine, 4) < 1.0e-4f, "master down is silent");
        }

        beginTest ("fader positions");
        expectWithinAbsoluteError (RigEngine::faderGain (0.8f), 1.0f, 1.0e-5f);
        expect (RigEngine::faderGain (1.0f) > 1.9f && RigEngine::faderGain (1.0f) < 2.1f);
        expect (RigEngine::faderGain (0.0f) == 0.0f);
        expect (RigEngine::faderGain (0.5f) < RigEngine::faderGain (0.6f));

        beginTest ("the MPC follows the Rig's clock");
        {
            RigEngine engine;
            engine.prepare (44100.0, 512);
            auto& transport = engine.getTransport();
            transport.setBpm (100.0);
            transport.play();
            runEngine (engine, 4);
            expect (engine.getMpc().getSequencer().isFollowingHost(), "the sequencer follows the Rig");
            expect (engine.getMpc().getSequencer().isPlaying());

            transport.stop();
            runEngine (engine, 4);
            expect (! engine.getMpc().getSequencer().isPlaying(), "and stops with it");
        }

        beginTest ("a learned fader moves the strip");
        {
            RigEngine engine;
            engine.prepare (44100.0, 256);
            engine.getRouting().setAssignment (RigRouting::samplerLevel, 1, 54);
            engine.getRouting().setAssignment (RigRouting::play, 1, 115);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 54, 0));
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 115, 127));
            runEngine (engine, 1, 256);
            expect (engine.getStrip (RigEngine::samplerStrip).level.load() == 0.0f);
            expect (engine.controlsMoved.exchange (false));
            runEngine (engine, 1, 256);
            expect (engine.getTransport().isPlaying(), "the learned button starts the Rig");
        }

        beginTest ("the keys play the sampler, and blocks bigger than prepared are split");
        {
            RigEngine engine;
            engine.getRouting().keysTo = (int) RigRouting::Dest::sampler;
            engine.prepare (44100.0, 256);
            engine.getSampler().getDocument().load (share (sine (44100)), "tone", "Open");
            engine.getSampler().getDocument().dispatchPendingMessages();
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 60, 1.0f));
            expect (runEngine (engine, 2, 1024) > 0.3f);
            expect (engine.getStrip (RigEngine::samplerStrip).peak.load() > 0.3f);
        }

        beginTest ("the mic reaches the sampler, not the speakers");
        {
            RigEngine engine;
            engine.vocoderOn = false;   // (the vocoder's dry mix would pass some voice)
            engine.prepare (44100.0, 512);
            auto mic = sine (512, 1, 0.5f);
            expect (runEngine (engine, 4, 512, &mic) < 1.0e-6f, "no feedback through the speakers");
            expect (engine.getSampler().takeInputLevel() > 0.4f, "the sampler hears it");
        }

        beginTest ("with the mic off, the vocoder doesn't hear it (no feedback through the speakers)");
        {
            RigEngine engine;
            engine.prepare (44100.0, 512);
            auto voice = sine (512, 1, 0.5f, 220.0);
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 48, 1.0f));
            runEngine (engine, 20, 512, &voice);
            expect (engine.getStrip (RigEngine::vocoderStrip).peak.load() < 1.0e-4f, "silent until the mic is on");
        }

        beginTest ("the keys play the vocoder with the mic as its voice");
        {
            RigEngine engine;
            engine.prepare (44100.0, 512);
            engine.micOn = true;
            auto voice = sine (512, 1, 0.5f, 220.0);
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 48, 1.0f));
            runEngine (engine, 20, 512, &voice);
            expect (runEngine (engine, 20, 512, &voice) > 0.01f, "the vocoder sings");
            expect (engine.getStrip (RigEngine::vocoderStrip).peak.load() > 0.01f, "on its own strip");

            beginTest ("off, the vocoder is silent");
            engine.vocoderOn = false;
            runEngine (engine, 4, 512, &voice);
            engine.getStrip (RigEngine::vocoderStrip).peak = 0.0f;
            runEngine (engine, 10, 512, &voice);
            expect (engine.getStrip (RigEngine::vocoderStrip).peak.load() < 1.0e-6f);
        }

        beginTest ("sample the vocoder straight into the sampler");
        {
            RigEngine engine;
            engine.prepare (44100.0, 512);
            engine.micOn = true;
            engine.sampleSource = (int) RigEngine::SampleSource::vocoder;
            engine.getStrip (RigEngine::vocoderStrip).mute = true;   // recorded before its fader
            auto voice = sine (512, 1, 0.5f, 220.0);
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 48, 1.0f));
            runEngine (engine, 10, 512, &voice);

            auto& sampler = engine.getSampler();
            sampler.startRecording();
            for (int b = 0; b < 40; ++b)
            {
                runEngine (engine, 1, 512, &voice);
                sampler.collectRecording();
            }
            auto take = sampler.stopRecording();
            expect (take.getNumSamples() > 15000, "about half a second recorded");
            expect (take.getMagnitude (0, take.getNumSamples()) > 0.01f, "with the vocoder in it");

            beginTest ("or the Rig mix: the MPC as you hear it");
            RigEngine mixEngine;
            mixEngine.prepare (44100.0, 512);
            mixEngine.vocoderOn = false;
            mixEngine.sampleSource = (int) RigEngine::SampleSource::mix;
            loadPadTone (mixEngine, 0);
            auto& mixSampler = mixEngine.getSampler();
            mixSampler.startRecording();
            mixEngine.addMidiNow (juce::MidiMessage::noteOn (10, 36, 1.0f));
            for (int b = 0; b < 20; ++b)
            {
                runEngine (mixEngine, 1);
                mixSampler.collectRecording();
            }
            auto mixTake = mixSampler.stopRecording();
            expect (mixTake.getMagnitude (0, mixTake.getNumSamples()) > 0.2f, "the pad is in the recording");
        }

        beginTest ("a show saves and opens again");
        {
            RigEngine engine;
            engine.prepare (44100.0, 512);
            loadPadTone (engine, 4);
            engine.getTransport().setBpm (128.0);
            engine.getTransport().metronome = true;
            engine.getRouting().padBank = 1;
            engine.getRouting().keysTo = (int) RigRouting::Dest::mpc;
            engine.getRouting().setAssignment (RigRouting::masterLevel, 1, 61);
            engine.getStrip (RigEngine::samplerStrip).level = 0.3f;
            engine.getStrip (RigEngine::mpcStrip).mute = true;
            engine.vocoderOn = false;
            engine.sampleSource = (int) RigEngine::SampleSource::mix;
            engine.getRouting().setAssignment (RigRouting::vocoderLevel, 1, 55);
            const auto show = engine.saveShow();

            // Through XML, as it's saved on disk.
            const auto xml = show.createXml();
            expect (xml != nullptr);
            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            expectWithinAbsoluteError (other.getTransport().getBpm(), 128.0, 1.0e-9);
            expect (other.getTransport().metronome.load());
            expectEquals (other.getRouting().padBank.load(), 1);
            expectEquals (other.getRouting().keysTo.load(), (int) RigRouting::Dest::mpc);
            expect (other.getRouting().getAssignment (RigRouting::masterLevel) == std::pair<int, int> { 1, 61 });
            expectWithinAbsoluteError (other.getStrip (RigEngine::samplerStrip).level.load(), 0.3f, 1.0e-6f);
            expect (other.getStrip (RigEngine::mpcStrip).mute.load());
            expect (! other.vocoderOn.load());
            expectEquals (other.sampleSource.load(), (int) RigEngine::SampleSource::mix);
            expect (other.getRouting().getAssignment (RigRouting::vocoderLevel) == std::pair<int, int> { 1, 55 });
            const auto& pad = other.getMpc().getProject().currentBank().pads[4];
            expect (pad.audio != nullptr && pad.audio->getNumSamples() == 44100, "the MPC's sounds come back");
        }
    }
};

static RigTransportTests rigTransportTests;
static RigRoutingTests rigRoutingTests;
static RigEngineTests rigEngineTests;
