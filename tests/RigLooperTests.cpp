#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigLooper.h"

namespace
{
constexpr double kLoopRate = 48000.0;
constexpr int kBlock = 480;

// Drives a looper like the Rig does: a clock that moves while playing, and one source.
struct LooperRig
{
    RigLooper looper;
    RigLooper::Clock clock;
    juce::AudioBuffer<float> source { 2, kBlock }, out { 2, kBlock };
    std::function<float (double beat)> signal = [] (double) { return 0.0f; };
    std::vector<float> heard;   // left output, every sample

    LooperRig()
    {
        looper.prepare (kLoopRate);
        clock.bpm = 120.0;
        clock.beatsPerBar = 4;
    }

    void run (double beats)
    {
        const int blocks = (int) std::round (beats * 60.0 / clock.bpm * kLoopRate / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const double beat = clock.beat + (clock.playing ? i * clock.bpm / (60.0 * kLoopRate) : 0.0);
                source.setSample (0, i, signal (beat));
                source.setSample (1, i, signal (beat));
            }
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            for (auto& s : sources)
                s = &source;
            if (looper.process (clock, sources, out))
                clock.playing = true;
            for (int i = 0; i < kBlock; ++i)
                heard.push_back (out.getSample (0, i));
            if (clock.playing)
                clock.beat += kBlock * clock.bpm / (60.0 * kLoopRate);
        }
    }

    float heardAtBeat (double beat) const
    {
        // `heard` starts at beat 0 when the clock started at 0 and ran from the first block.
        const auto i = (size_t) std::round (beat * 60.0 / clock.bpm * kLoopRate);
        return i < heard.size() ? heard[i] : 0.0f;
    }
};
}

struct RigLooperTests : juce::UnitTest
{
    RigLooperTests() : juce::UnitTest ("Rig looper", "Rig") {}

    void runTest() override
    {
        beginTest ("record while stopped starts the clock, and a set length stops by itself");
        {
            LooperRig rig;
            rig.looper.getLayer (0).bars = 1;
            rig.looper.getLayer (0).level = 1.0f;
            rig.signal = [] (double beat) { return (float) (0.1 + 0.2 * beat); };   // a ramp, so position shows
            rig.looper.pressRecord (0);
            rig.run (0.05);
            expect (rig.clock.playing, "the clock starts");
            expect (rig.looper.getState (0) == RigLooper::State::recording);
            rig.run (4.0);
            expect (rig.looper.getState (0) == RigLooper::State::playing, "stops after one bar");
            expectWithinAbsoluteError (rig.looper.getLayer (0).lengthBeats.load(), 4.0, 1.0e-9);

            beginTest ("the loop plays back in time");
            rig.signal = [] (double) { return 0.0f; };
            rig.heard.clear();
            const double start = rig.clock.beat;
            rig.run (4.0);
            // At loop beat 2 the ramp was 0.1 + 0.2 * 2 = 0.5.
            const double offset = std::fmod (start, 4.0);
            const double beatInHeard = std::fmod (2.0 - offset + 4.0, 4.0);
            expectWithinAbsoluteError (rig.heardAtBeat (beatInHeard), 0.5f, 0.01f);

            beginTest ("loops follow the tempo");
            rig.clock.bpm = 60.0;
            rig.heard.clear();
            const double start2 = rig.clock.beat;
            rig.run (4.0);
            const double beatInHeard2 = std::fmod (3.0 - std::fmod (start2, 4.0) + 4.0, 4.0);
            expectWithinAbsoluteError (rig.heardAtBeat (beatInHeard2), 0.7f, 0.01f);
        }

        beginTest ("free recording starts at the next bar and finishes the bar you stop in");
        {
            LooperRig rig;
            rig.clock.playing = true;
            rig.signal = [] (double) { return 0.25f; };
            rig.run (1.5);
            rig.looper.pressRecord (2);
            rig.run (1.0);
            expect (rig.looper.getState (2) == RigLooper::State::armed, "waits for the bar");
            rig.run (2.0);   // beat 4.5
            expect (rig.looper.getState (2) == RigLooper::State::recording);
            rig.run (1.5);   // beat 6
            rig.looper.pressRecord (2);
            rig.run (1.0);
            expect (rig.looper.getState (2) == RigLooper::State::recording, "keeps going to the end of the bar");
            rig.run (1.5);   // past beat 8
            expect (rig.looper.getState (2) == RigLooper::State::playing);
            expectWithinAbsoluteError (rig.looper.getLayer (2).lengthBeats.load(), 4.0, 1.0e-9);
        }

        beginTest ("overdub adds to the loop, and undo takes it back");
        {
            LooperRig rig;
            auto& layer = rig.looper.getLayer (1);
            layer.bars = 1;
            layer.level = 1.0f;
            rig.signal = [] (double) { return 0.2f; };
            rig.looper.pressRecord (1);
            rig.run (4.2);
            expect (rig.looper.getState (1) == RigLooper::State::playing);

            rig.looper.pressRecord (1);   // overdub one full pass
            rig.run (4.0);
            rig.looper.pressRecord (1);
            rig.signal = [] (double) { return 0.0f; };
            rig.run (0.1);
            expect (rig.looper.getState (1) == RigLooper::State::playing);
            rig.heard.clear();
            rig.run (2.0);
            expectWithinAbsoluteError (rig.heard[rig.heard.size() / 2], 0.4f, 0.02f);

            rig.looper.undo (1);
            rig.heard.clear();
            rig.run (2.0);
            expectWithinAbsoluteError (rig.heard[rig.heard.size() / 2], 0.2f, 0.02f);

            beginTest ("mute silences a layer, and clear empties it");
            layer.mute = true;
            rig.heard.clear();
            rig.run (1.0);
            expectLessThan (std::abs (rig.heard.back()), 1.0e-4f);
            rig.looper.clear (1);
            rig.run (0.1);
            expect (rig.looper.getState (1) == RigLooper::State::empty);
        }

        beginTest ("a sound from a file plays as a loop of its beats");
        {
            LooperRig rig;
            juce::AudioBuffer<float> audio (1, 1000);
            for (int i = 0; i < 1000; ++i)
                audio.setSample (0, i, i / 1000.0f);
            rig.looper.setLayerAudio (5, audio, 8.0);
            rig.looper.getLayer (5).level = 1.0f;
            expect (rig.looper.getState (5) == RigLooper::State::playing);
            rig.clock.playing = true;
            rig.run (8.0);
            expectWithinAbsoluteError (rig.heardAtBeat (4.0), 0.5f, 0.01f);
        }

        beginTest ("in the Rig: the looper records the bass, plays it through the Loops strip, and saves with the show");
        {
            RigEngine engine;
            engine.prepare (kLoopRate, kBlock);
            engine.vocoderOn = false;
            engine.getRouting().keysTo = (int) RigRouting::Dest::bass;
            engine.getTransport().setBpm (120.0);
            auto& layer = engine.getLooper().getLayer (0);
            layer.source = RigLooper::bass;
            layer.bars = 1;
            engine.getLooper().pressRecord (0);
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 40, (juce::uint8) 110));
            juce::AudioBuffer<float> out (2, kBlock);
            const int bar = (int) (2.0 * kLoopRate / kBlock);
            for (int b = 0; b < bar + 10; ++b)
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expect (engine.getTransport().isPlaying(), "recording started the clock");
            expect (engine.getLooper().getState (0) == RigLooper::State::playing);

            // The bass stops; the loop keeps playing it.
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 40));
            engine.getStrip (RigEngine::bassStrip).mute = true;
            float peak = 0;
            for (int b = 0; b < bar / 2; ++b)
            {
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
                peak = juce::jmax (peak, out.getMagnitude (0, 0, kBlock));
            }
            expectGreaterThan (peak, 0.01f, "the loop is heard");
            engine.getStrip (RigEngine::loopsStrip).mute = true;
            for (int b = 0; b < 20; ++b)
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            expectLessThan (out.getMagnitude (0, 0, kBlock), 1.0e-4f, "the Loops strip mutes it");

            const auto folder = juce::File::createTempFile ("rigloops");
            folder.createDirectory();
            const auto showFile = folder.getChildFile ("Test show.crateshow");
            const auto xml = engine.saveShow().createXml();
            expect (xml->writeTo (showFile));
            expect (engine.writeLoopAudio (showFile));
            expect (RigEngine::loopsFolder (showFile).getChildFile ("Loop 1.wav").existsAsFile());

            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            other.readLoopAudio (showFile);
            expect (other.getLooper().getState (0) == RigLooper::State::playing, "the loop comes back");
            expectWithinAbsoluteError (other.getLooper().getLayer (0).lengthBeats.load(), 4.0, 1.0e-9);
            expectEquals (other.getLooper().getLayer (0).source.load(), (int) RigLooper::bass);
            expect (other.getLooper().getState (1) == RigLooper::State::empty);
            folder.deleteRecursively();
        }
    }
};

static RigLooperTests rigLooperTests;
