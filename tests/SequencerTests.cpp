#include "TestHelpers.h"

#include "MpcProcessor.h"
#include "MpcView.h"
#include "SequencerEngine.h"

namespace
{
using Engine = SequencerEngine;

// Runs the engine for `blocks` blocks and returns every note-on as (sample from the start, pad).
struct Run
{
    Engine& engine;
    int blockSize = 512;
    long long now = 0;
    std::vector<std::pair<long long, int>> hits, offs;
    int clicks = 0, accents = 0;

    void blocks (int count, const Engine::Host* host = nullptr, juce::MidiBuffer played = {})
    {
        for (int b = 0; b < count; ++b)
        {
            juce::MidiBuffer out;
            std::array<Engine::Click, 16> c;
            int n = 0;
            engine.process (blockSize, host, b == 0 ? played : juce::MidiBuffer(), out, c, n);
            for (const auto m : out)
            {
                if (m.getMessage().isNoteOn())
                    hits.push_back ({ now + m.samplePosition, padForNote (m.getMessage().getNoteNumber()) });
                else if (m.getMessage().isNoteOff())
                    offs.push_back ({ now + m.samplePosition, padForNote (m.getMessage().getNoteNumber()) });
            }
            clicks += n;
            for (int i = 0; i < n; ++i)
                accents += c[(size_t) i].accent ? 1 : 0;
            now += blockSize;
        }
    }
};

seq::Sequence oneBar (double bpm = 120.0)
{
    seq::Sequence s;
    s.bpm = bpm;
    s.bars = 1;
    return s;
}

struct SequenceTests : juce::UnitTest
{
    SequenceTests() : juce::UnitTest ("Sequences", "Sequencer") {}

    void runTest() override
    {
        beginTest ("tracks keep notes sorted, one per pad and tick");
        seq::Track t;
        t.add ({ 96, 1, 100, 24 });
        t.add ({ 0, 2, 100, 24 });
        t.add ({ 96, 1, 50, 24 });
        expectEquals ((int) t.notes.size(), 2);
        expectEquals (t.notes[0].tick, 0);
        expectEquals (t.notes[1].velocity, 50, "a new hit replaces the old one");
        expectEquals (t.remove (1, 0, 200), 1);

        beginTest ("timing correct and swing");
        using seq::Grid;
        expectEquals (seq::gridTicks (Grid::sixteenth), 24);
        expectEquals (seq::gridTicks (Grid::eighthTriplet), 32);
        expectEquals (seq::correct (13, Grid::sixteenth, 50, 384), 24);
        expectEquals (seq::correct (11, Grid::sixteenth, 50, 384), 0);
        expectEquals (seq::correct (380, Grid::sixteenth, 50, 384), 0, "rounds up past the end onto the downbeat");
        expectEquals (seq::stepTick (Grid::sixteenth, 66, 1), 24 + 8, "66% swing is a triplet feel");
        expectEquals (seq::stepTick (Grid::sixteenth, 75, 1), 36);
        expectEquals (seq::stepTick (Grid::sixteenth, 66, 2), 48, "on-beats don't move");
        expectEquals (seq::correct (30, Grid::sixteenth, 66, 384), 32);
        expectEquals (seq::stepTick (Grid::sixteenthTriplet, 75, 1), 16, "triplets don't swing");
        expectEquals (seq::correct (7, Grid::off, 50, 384), 7);

        beginTest ("positions read like the MPC");
        expectEquals (seq::formatPosition (0, 4), juce::String ("001.1.00"));
        expectEquals (seq::formatPosition (384 + 96 * 2 + 5, 4), juce::String ("002.3.05"));

        beginTest ("shortening drops notes past the end");
        seq::Sequence s;
        s.bars = 2;
        s.tracks[0].add ({ 10, 0, 100, 24 });
        s.tracks[0].add ({ 400, 0, 100, 24 });
        s.setBars (1);
        expectEquals ((int) s.tracks[0].notes.size(), 1);

        beginTest ("saved and opened again");
        s.name = "Beat";
        s.bpm = 93.5;
        s.loop = false;
        s.tracks[1].mute = true;
        s.tracks[1].name = "Hats";
        s.tracks[1].add ({ 48, 5, 70, 12 });
        const auto back = seq::Sequence::fromValueTree (s.toValueTree());
        expectEquals (back.name, juce::String ("Beat"));
        expect (back.bpm == 93.5 && ! back.loop && back.bars == 1);
        expectEquals ((int) back.tracks.size(), 8);
        expect (back.tracks[1].mute && back.tracks[1].name == "Hats");
        expect (back.tracks[1].notes == s.tracks[1].notes && back.tracks[0].notes == s.tracks[0].notes);

        beginTest ("MIDI file there and back");
        seq::Sequence m;
        m.bpm = 101.0;
        m.bars = 2;
        m.tracks[0].add ({ 0, 0, 127, 24 });
        m.tracks[0].add ({ 96, 3, 64, 48 });
        m.tracks[2].add ({ 500, 63, 100, 10 });
        const auto data = seq::toMidiFile (m);
        seq::Sequence read;
        juce::String error;
        expect (seq::fromMidiFile (data.getData(), data.getSize(), read, error), error);
        expect (std::abs (read.bpm - 101.0) < 0.01);
        expectEquals (read.bars, 2);
        expectEquals (read.usedTracks(), 2);
        expect (read.tracks[0].notes == m.tracks[0].notes);
        expect (read.tracks[1].notes == m.tracks[2].notes);

        // The pads' notes on channel 10, which the MPC reads.
        juce::MemoryInputStream in (data, false);
        juce::MidiFile file;
        expect (file.readFrom (in));
        expectEquals (file.getTimeFormat(), (short) 96);
        const auto& first = file.getTrack (1)->getEventPointer (1)->message;
        expect (first.isNoteOn() && first.getChannel() == 10 && first.getNoteNumber() == noteForPad (0));

        beginTest ("MIDI notes that aren't on pads are left out");
        juce::MidiFile other;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::noteOn (1, 20, (juce::uint8) 100), 0);
        track.addEvent (juce::MidiMessage::noteOn (1, 37, (juce::uint8) 100), 480);
        other.setTicksPerQuarterNote (480);
        other.addTrack (track);
        juce::MemoryOutputStream out;
        other.writeTo (out);
        seq::Sequence fromOther;
        expect (seq::fromMidiFile (out.getData(), out.getDataSize(), fromOther, error));
        expectEquals ((int) fromOther.tracks[0].notes.size(), 1);
        expectEquals (fromOther.tracks[0].notes[0].tick, 96, "480 ticks a beat becomes 96");
        expect (error.isNotEmpty());
    }
};

struct EngineTests : juce::UnitTest
{
    EngineTests() : juce::UnitTest ("Sequencer engine", "Sequencer") {}

    void runTest() override
    {
        // At 120 BPM and 48 kHz a beat is 24000 samples.
        beginTest ("plays notes on time and loops");
        {
            Engine engine;
            engine.prepare (48000.0);
            auto s = oneBar();
            s.tracks[0].add ({ 0, 0, 100, 24 });
            s.tracks[0].add ({ 96, 4, 100, 24 });
            s.tracks[1].add ({ 288, 9, 100, 24 });
            s.tracks[1].mute = true;
            engine.setSnapshot (Engine::makeSnapshot (s));
            engine.clickMode = (int) Engine::ClickMode::off;

            Run run { engine };
            run.blocks (2);
            expect (run.hits.empty(), "nothing before Play");

            engine.play (true);
            run.now = 0;
            run.blocks (400);   // just over two bars
            expectEquals ((int) run.hits.size(), 5);
            expect (run.hits[0] == std::pair<long long, int> (0, 0));
            expect (std::abs (run.hits[1].first - 24000) <= 1 && run.hits[1].second == 4);
            expect (std::abs (run.hits[2].first - 96000) <= 1, "loops after a bar");
            expect (std::abs (run.hits[4].first - 192000) <= 1);
            expect (! run.offs.empty(), "notes let go when they end");
            expect (engine.isPlaying());

            engine.stop();
            run.blocks (1);
            expect (! engine.isPlaying());
            expect (engine.getPosition() > 0, "Stop keeps the position");
            engine.play (true);
            run.blocks (1);
            expect (engine.getPosition() < 600, "Play start goes back to the start");
        }

        beginTest ("a sequence that doesn't loop stops at the end");
        {
            Engine engine;
            engine.prepare (48000.0);
            auto s = oneBar();
            s.loop = false;
            s.tracks[0].add ({ 0, 0, 100, 24 });
            engine.setSnapshot (Engine::makeSnapshot (s));
            engine.play (true);
            Run run { engine };
            run.blocks (400);
            expectEquals ((int) run.hits.size(), 1);
            expect (! engine.isPlaying());
        }

        beginTest ("count-in and the metronome");
        {
            Engine engine;
            engine.prepare (48000.0);
            auto s = oneBar();
            s.tracks[0].add ({ 0, 0, 100, 24 });
            engine.setSnapshot (Engine::makeSnapshot (s));
            engine.record();
            Run run { engine };
            run.blocks (1);
            expect (engine.isRecording());
            expect (engine.getPosition() < 0, "counting in");
            run.blocks (186);   // the count-in bar is 96000 samples
            expect (run.hits.empty(), "the sequence waits for the count-in");
            run.blocks (2);
            expectEquals ((int) run.hits.size(), 1);
            expect (std::abs (run.hits[0].first - 96000) <= 1);
            expectEquals (run.clicks, 5, "four beats counting in, then the downbeat");
            expectEquals (run.accents, 2);
        }

        beginTest ("recording: timing correct, held length, no double hit");
        {
            Engine engine;
            engine.prepare (48000.0);
            engine.countIn = false;
            engine.clickMode = (int) Engine::ClickMode::off;
            auto s = oneBar();
            engine.setSnapshot (Engine::makeSnapshot (s));
            engine.record();
            Run run { engine };
            run.blocks (1);

            // A hit 2.5 ticks before the 1/16 at tick 24 (6000 samples), at sample 5375.
            juce::MidiBuffer played;
            played.addEvent (juce::MidiMessage::noteOn (1, noteForPad (2), (juce::uint8) 90), 5375 - 512 * 10);
            run.blocks (10);
            run.blocks (1, nullptr, played);
            juce::MidiBuffer up;
            up.addEvent (juce::MidiMessage::noteOff (1, noteForPad (2)), 0);
            run.blocks (5);
            run.blocks (1, nullptr, up);

            std::array<Engine::Recorded, 8> got;
            const int n = engine.takeRecorded (got.data(), 8);
            expectEquals (n, 1);
            expectEquals (got[0].tick, 24);
            expectEquals (got[0].pad, 2);
            expectEquals (got[0].velocity, 90);
            expect (got[0].length > 10 && got[0].length < 16, "length is how long it was held");

            // The sequence gets the note (as the processor would add it); the playhead is just
            // short of it, so it mustn't play again this time round.
            s.tracks[0].add ({ got[0].tick, got[0].pad, got[0].velocity, got[0].length });
            engine.setSnapshot (Engine::makeSnapshot (s));
            run.hits.clear();
            run.blocks (190);
            expectEquals ((int) run.hits.size(), 1, "heard next time round only");
            expect (! run.hits.empty() && std::abs (run.hits[0].first - (96000 + 6000)) <= 1);
        }

        beginTest ("note repeat");
        {
            Engine engine;
            engine.prepare (48000.0);
            engine.noteRepeat = true;
            engine.grid = (int) seq::Grid::eighth;
            engine.clickMode = (int) Engine::ClickMode::off;
            engine.setSnapshot (Engine::makeSnapshot (oneBar()));
            engine.play (true);
            Run run { engine };
            juce::MidiBuffer down;
            down.addEvent (juce::MidiMessage::noteOn (1, noteForPad (7), (juce::uint8) 100), 0);
            run.blocks (1, nullptr, down);
            run.blocks (93);   // half a bar: 4 eighths
            expectEquals ((int) run.hits.size(), 4, "the press, then a repeat every 1/8");
            expect (std::abs (run.hits[0].first - 12000) <= 1);
            juce::MidiBuffer up;
            up.addEvent (juce::MidiMessage::noteOff (1, noteForPad (7)), 0);
            run.blocks (1, nullptr, up);
            run.blocks (100);
            expectEquals ((int) run.hits.size(), 4, "stops when you let go");
        }

        beginTest ("follows Logic");
        {
            Engine engine;
            engine.prepare (48000.0);
            auto s = oneBar (90.0);
            s.tracks[0].add ({ 96 * 2, 1, 100, 24 });
            engine.setSnapshot (Engine::makeSnapshot (s));
            Run run { engine };
            Engine::Host host;
            host.playing = true;
            host.bpm = 120.0;
            host.ppq = 4.0 + 2.0;    // bar 2, beat 3 in Logic: beat 3 of this one-bar loop
            run.blocks (1, &host);
            expect (engine.isPlaying() && engine.isFollowingHost());
            expectEquals ((int) run.hits.size(), 1);
            expect (! run.hits.empty() && run.hits[0].first == 0);
            host.playing = false;
            run.blocks (1, &host);
            expect (! engine.isPlaying(), "stops with Logic");
        }
    }
};

struct SequencerProcessorTests : juce::UnitTest
{
    SequencerProcessorTests() : juce::UnitTest ("Sequencer in the MPC tool", "Sequencer") {}

    void runTest() override
    {
        beginTest ("records pads onto the selected track, with undo");
        MpcProcessor processor;
        processor.setPlayConfigDetails (0, 2, 44100.0, 512);
        processor.prepareToPlay (44100.0, 512);
        auto& project = processor.getProject();
        project.currentBank().pads[0].audio = crate::share (sine (44100, 1, 0.5f));
        project.changed();
        project.dispatchPendingMessages();

        auto& engine = processor.getSequencer();
        engine.countIn = false;
        engine.clickMode = (int) SequencerEngine::ClickMode::off;
        processor.recordTrack = 2;
        processor.toggleRecord();

        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer midi;
        processor.processBlock (out, midi);
        expect (engine.isRecording());
        midi.addEvent (juce::MidiMessage::noteOn (1, noteForPad (0), 1.0f), 10);
        processor.processBlock (out, midi);
        expect (out.getMagnitude (0, 512) > 0.3f, "you hear the pad as you record it");
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, noteForPad (0)), 0);
        processor.processBlock (out, midi);

        expectEquals (processor.collectRecorded(), 1);
        const auto& track = project.currentSequence().tracks[2];
        expectEquals ((int) track.notes.size(), 1);
        expect (! track.notes.empty() && track.notes[0].tick == 0 && track.notes[0].pad == 0);

        expect (processor.canUndo());
        expect (processor.undoEdit());
        expect (project.currentSequence().tracks[2].notes.empty(), "undo takes the recording back");
        expect (processor.redoEdit());
        expectEquals ((int) project.currentSequence().tracks[2].notes.size(), 1);

        beginTest ("the sequence plays the kit");
        engine.stop();
        midi.clear();
        processor.processBlock (out, midi);
        for (int i = 0; i < 100; ++i)
            processor.processBlock (out, midi);   // let the pad ring out
        expect (out.getMagnitude (0, 512) < 1.0e-6f);
        engine.play (true);
        processor.processBlock (out, midi);
        expect (out.getMagnitude (0, 512) > 0.3f);

        beginTest ("sequences are saved with the project");
        project.currentSequence().name = "Groove";
        project.addSequence ("Second");
        juce::MemoryBlock state;
        processor.getStateInformation (state);
        MpcProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        const auto& seqs = other.getProject().getSequences();
        expectEquals ((int) seqs.size(), 2);
        expectEquals (seqs[0].name, juce::String ("Groove"));
        expectEquals ((int) seqs[0].tracks[2].notes.size(), 1);
        expectEquals (other.getProject().getCurrentSequenceIndex(), 1);

        beginTest ("the sequencer screen opens");
        project.setCurrentSequence (0);
        std::unique_ptr<juce::AudioProcessorEditor> view (processor.createEditor());
        auto* mpcView = dynamic_cast<MpcView*> (view.get());
        expect (mpcView != nullptr);
        mpcView->showSequencer (true);
        if (auto* path = std::getenv ("CRATE_SEQ_SNAPSHOT"))
        {
            auto& s = project.currentSequence();
            for (int i = 0; i < 16; i += 4)
                s.tracks[0].add ({ i * 24, 0, 110, 24 });
            for (int i = 2; i < 16; i += 4)
                s.tracks[1].add ({ i * 24, 4, 90, 24 });
            for (int i = 0; i < 16; ++i)
                s.tracks[0].add ({ i * 24, 8, 40 + i * 5, 12 });
            project.sequenceChanged();
            project.sequenceEdits.dispatchPendingMessages();
            auto image = view->createComponentSnapshot (view->getLocalBounds());
            juce::File file { juce::String { path } };
            file.deleteFile();
            juce::FileOutputStream stream { file };
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
    }
};

SequenceTests sequenceTests;
EngineTests engineTests;
SequencerProcessorTests sequencerProcessorTests;
}
