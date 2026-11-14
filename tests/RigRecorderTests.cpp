#include "TestHelpers.h"

#include "RigEngine.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;   // 50 blocks a beat at 120 BPM

void runBeats (RigEngine& engine, double beats)
{
    juce::AudioBuffer<float> out (2, kBlock);
    const int blocks = (int) std::round (beats * 60.0 / engine.getTransport().getBpm() * kRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        engine.serviceRecorder();
    }
}

int wavLength (const juce::File& f)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
    return reader != nullptr ? (int) reader->lengthInSamples : -1;
}
}

struct RigRecorderTests : juce::UnitTest
{
    RigRecorderTests() : juce::UnitTest ("Recording the set", "Rig") {}

    void runTest() override
    {
        beginTest ("each tool as a WAV, the mix, and what was played as MIDI, from the first bar");
        {
            juce::TemporaryFile temp;
            const auto folder = temp.getFile();
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            engine.getTransport().metronome = true;   // never recorded
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;

            expect (engine.startRecording (folder));
            runBeats (engine, 0.5);
            expect (engine.getRecorder().getState() == RigRecorder::State::armed, "waits for the clock");

            engine.getTransport().play();
            runBeats (engine, 1.3);
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 60, 0.8f));
            runBeats (engine, 1.0);
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 60));
            runBeats (engine, 5.7);   // to beat 8
            expect (engine.getRecorder().getState() == RigRecorder::State::recording);
            engine.stopRecording();
            for (int i = 0; i < 10 && engine.getRecorder().getState() != RigRecorder::State::idle; ++i)
                runBeats (engine, 0.02);
            expect (engine.getRecorder().getState() == RigRecorder::State::idle, "saved");

            const auto bassFile = folder.getChildFile ("03 Bass.wav");
            const auto mixFile = folder.getChildFile ("08 Mix.wav");
            expect (bassFile.existsAsFile(), "the bass (its sequencer plays from the start)");
            expect (folder.getChildFile ("04 Synth.wav").existsAsFile(), "the synth, played from the keys");
            expect (mixFile.existsAsFile());
            expect (! folder.getChildFile ("05 Input.wav").exists(), "a silent strip isn't saved");
            expect (folder.getChildFile ("Open in Logic.txt").existsAsFile());
            const int length = wavLength (mixFile);
            expect (std::abs (length - 8 * 24000) <= kBlock, "starts on beat 1: " + juce::String (length));
            expectEquals (wavLength (bassFile), length, "every stem lines up");

            juce::MidiFile midi;
            juce::FileInputStream in (folder.getChildFile ("Played.mid"));
            expect (in.openedOk() && midi.readFrom (in));
            expectEquals (midi.getNumTracks(), 2, "the tempo and the synth");
            if (midi.getNumTracks() == 2)
            {
                const auto* track = midi.getTrack (1);
                double noteAt = -1;
                for (const auto* e : *track)
                    if (e->message.isNoteOn())
                        noteAt = e->message.getTimeStamp() / 960.0;
                expectWithinAbsoluteError (noteAt, 1.3, 0.03, "the note lands where it was played");
            }
            folder.deleteRecursively();
        }

        beginTest ("started while playing, it begins at the next bar");
        {
            juce::TemporaryFile temp;
            const auto folder = temp.getFile();
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            engine.getTransport().play();
            runBeats (engine, 1.5);
            expect (engine.startRecording (folder));
            runBeats (engine, 2.0);   // beat 3.5: not yet
            expectEquals (engine.getRecorder().getRecordedSeconds(), 0.0);
            runBeats (engine, 2.5);   // beat 6: two beats in
            expectWithinAbsoluteError (engine.getRecorder().getRecordedSeconds(), 1.0, 0.02);
            engine.getTransport().stop();   // stopping the Rig ends the take
            runBeats (engine, 0.1);
            expect (engine.getRecorder().getState() == RigRecorder::State::idle);
            expect (folder.getChildFile ("08 Mix.wav").existsAsFile());
            folder.deleteRecursively();
        }
    }
};

struct RigCaptureTests : juce::UnitTest
{
    RigCaptureTests() : juce::UnitTest ("Recording one tool into another", "Rig") {}

    void runTest() override
    {
        beginTest ("the bass, one bar, onto the next empty MPC pad, from the next bar");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            auto& kit = engine.getMpc().getProject().currentBank();
            juce::AudioBuffer<float> taken (1, 100);
            taken.clear();
            taken.setSample (0, 0, 0.5f);
            kit.pads[0].audio = crate::share (std::move (taken));   // A1 is taken
            engine.captureDest = RigCapture::toMpcPad;
            engine.getTransport().play();
            runBeats (engine, 1.0);
            expect (engine.startCapture (RigCapture::bass, 1));
            runBeats (engine, 2.0);   // beat 3
            expect (engine.getCapture().getState() == RigCapture::State::armed, "waits for the bar");
            runBeats (engine, 1.5);   // beat 4.5
            expect (engine.getCapture().getState() == RigCapture::State::recording, "started at the bar (beat 4)");
            expectWithinAbsoluteError (engine.getCapture().getRecordedSeconds(), 0.25, 0.02);
            runBeats (engine, 4.0);   // past beat 8: one bar recorded
            const auto said = engine.serviceCapture();
            expect (said.contains ("pad A2"), said);
            const auto& pad = kit.pads[1];
            expect (! pad.isEmpty());
            if (! pad.isEmpty())
                expectEquals (pad.audio->getNumSamples(), (int) crate::samples (2.0), "one bar at 120 BPM, at the MPC's rate");
            expectEquals (pad.name, juce::String ("Bass 1 bar"));
            expectWithinAbsoluteError (pad.beats, 4.0, 0.001);
            expect (engine.getCapture().getState() == RigCapture::State::idle);
        }

        beginTest ("the synth, free, into the Sampler while stopped");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            engine.captureDest = RigCapture::toSampler;
            expect (engine.startCapture (RigCapture::synth, 0));
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 64, 0.9f));
            runBeats (engine, 1.0);
            engine.stopCapture();
            runBeats (engine, 0.02);
            const auto said = engine.serviceCapture();
            expect (said.contains ("Sampler"), said);
            expect (engine.getSampler().getDocument().getName().startsWith ("Synth"));
        }

        beginTest ("the bass, one bar, into a WAV for Logic");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            engine.captureDest = RigCapture::toLogic;
            engine.getTransport().play();
            expect (engine.startCapture (RigCapture::bass, 1));
            runBeats (engine, 9.0);
            const auto said = engine.serviceCapture();
            expect (said.contains ("for Logic"), said);
            const auto file = engine.lastLogicFile;
            expect (file.existsAsFile());
            expect (file.isAChildOf (RigEngine::logicFolder()));
            juce::WavAudioFormat wav;
            if (std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (file.createInputStream().release(), true)); reader != nullptr)
            {
                expectWithinAbsoluteError ((double) reader->lengthInSamples / reader->sampleRate, 2.0, 0.01, "one bar at 120 BPM");
                juce::AudioBuffer<float> read ((int) reader->numChannels, (int) reader->lengthInSamples);
                reader->read (&read, 0, read.getNumSamples(), 0, true, true);
                expectGreaterThan (read.getMagnitude (0, read.getNumSamples()), 0.01f, "the bass is in it");
            }
            else
                expect (false, "a WAV file");
            file.deleteFile();
        }

        beginTest ("nothing playing: nothing is recorded");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            expect (engine.startCapture (RigCapture::input, 1));
            runBeats (engine, 5.0);
            expect (engine.serviceCapture().startsWith ("Nothing"));
        }
    }
};

static RigRecorderTests rigRecorderTests;
static RigCaptureTests rigCaptureTests;
