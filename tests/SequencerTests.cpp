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

//==============================================================================
struct PerformTests : juce::UnitTest
{
    PerformTests() : juce::UnitTest ("Playing live", "Sequencer") {}

    static seq::Sequence hitsOn (int pad, int bars = 1)
    {
        auto s = oneBar();
        s.bars = bars;
        for (int b = 0; b < bars; ++b)
            s.tracks[0].add ({ b * s.ticksPerBar(), pad + b, 100, 24 });
        return s;
    }

    static int padAt (const Run& run, long long sample)
    {
        for (const auto& [at, pad] : run.hits)
            if (std::abs (at - sample) <= 1)
                return pad;
        return -1;
    }

    void runTest() override
    {
        // 120 BPM at 48 kHz: a bar is 96000 samples.
        beginTest ("a queued sequence waits for the end of the one playing");
        {
            Engine engine;
            engine.prepare (48000.0);
            engine.clickMode = (int) Engine::ClickMode::off;
            engine.setSnapshot (Engine::makeSnapshot (hitsOn (0, 2), 0));
            engine.play (true);
            Run run { engine };
            run.blocks (100);
            engine.queue (Engine::makeSnapshot (hitsOn (10), 1), false);
            run.blocks (100);
            expect (engine.hasQueued());
            expectEquals (engine.getPlayingId(), 0);
            run.blocks (250);   // past the end of the two bars
            expectEquals (padAt (run, 96000), 1, "the rest of the first sequence still plays");
            expectEquals (padAt (run, 192000), 10, "then the queued one, right on time");
            expectEquals (engine.getPlayingId(), 1);
            expect (! engine.hasQueued());
            expectEquals (engine.getPasses(), 1);
            run.blocks (200);
            expectEquals (padAt (run, 288000), 10, "and it loops");
        }

        beginTest ("or switches at the next bar line");
        {
            Engine engine;
            engine.prepare (48000.0);
            engine.clickMode = (int) Engine::ClickMode::off;
            engine.setSnapshot (Engine::makeSnapshot (hitsOn (0, 4), 0));
            engine.play (true);
            Run run { engine };
            run.blocks (100);   // half way through bar 1
            engine.queue (Engine::makeSnapshot (hitsOn (20, 2), 1), true);
            run.blocks (200);
            expectEquals (padAt (run, 96000), 20, "the new one starts at bar 2, from its own start");
            expectEquals (engine.getPlayingId(), 1);
            expectEquals (padAt (run, 0), 0);
        }

        beginTest ("stops at the end when asked");
        {
            Engine engine;
            engine.prepare (48000.0);
            engine.clickMode = (int) Engine::ClickMode::off;
            engine.setSnapshot (Engine::makeSnapshot (hitsOn (0), 0, true));
            engine.play (true);
            Run run { engine };
            run.blocks (50);
            engine.stopAtEnd = true;
            run.blocks (300);
            expect (! engine.isPlaying());
            expectEquals ((int) run.hits.size(), 1);
        }

        beginTest ("solo plays only the soloed tracks");
        {
            auto s = oneBar();
            s.tracks[0].add ({ 0, 0, 100, 24 });
            s.tracks[1].add ({ 0, 1, 100, 24 });
            s.tracks[2].add ({ 0, 2, 100, 24 });
            s.tracks[2].mute = true;
            expectEquals ((int) Engine::makeSnapshot (s)->notes.size(), 2);
            s.tracks[1].solo = true;
            const auto solo = Engine::makeSnapshot (s);
            expectEquals ((int) solo->notes.size(), 1);
            expectEquals (solo->notes[0].pad, 1);
            s.tracks[2].solo = true;
            expectEquals ((int) Engine::makeSnapshot (s)->notes.size(), 2, "solo wins over mute");
            const auto back = seq::Sequence::fromValueTree (s.toValueTree());
            expect (back.tracks[1].solo && ! back.tracks[0].solo);
        }

        beginTest ("songs play their steps in order, then stop");
        {
            MpcProcessor processor;
            processor.setPlayConfigDetails (0, 2, 48000.0, 512);
            processor.prepareToPlay (48000.0, 512);
            auto& project = processor.getProject();
            auto& engine = processor.getSequencer();
            engine.clickMode = (int) SequencerEngine::ClickMode::off;

            project.getSequences()[0] = hitsOn (0);
            project.getSequences()[0].name = "Verse";
            project.addSequence ("Chorus");
            project.getSequences()[1] = hitsOn (5);
            project.getSequences()[1].name = "Chorus";
            auto& song = project.currentSong();
            song.name = "Tune";
            song.steps = { { 0, 2 }, { 1, 1 } };
            expectEquals (song.totalBars (project.getSequences()), 3);
            project.sequenceEdits.dispatchPendingMessages();

            expect (processor.playSong (0));
            expectEquals (project.getCurrentSequenceIndex(), 0);

            std::vector<int> ids;
            int stoppedAt = -1;
            juce::AudioBuffer<float> out (2, 512);
            for (int b = 0; b < 1200; ++b)
            {
                juce::MidiBuffer midi;
                processor.processBlock (out, midi);
                processor.followTransport();
                if (ids.empty() || ids.back() != engine.getPlayingId())
                    ids.push_back (engine.getPlayingId());
                if (b == 300)
                {
                    expectEquals (processor.getSongStep(), 0);
                    expectEquals (processor.getSongRepeat(), 1, "second time round the verse");
                }
                if (! engine.isPlaying())
                {
                    stoppedAt = b;
                    break;
                }
            }
            expect (ids == std::vector<int> ({ 0, 1 }), "verse, then chorus");
            expect (stoppedAt >= 3 * 96000 / 512 - 1 && stoppedAt <= 3 * 96000 / 512 + 1, "stops after three bars: " + juce::String (stoppedAt));
            expectEquals (project.getCurrentSequenceIndex(), 1);
            expect (! processor.isSongPlaying());

            beginTest ("songs are saved with the project");
            song.loop = true;
            juce::MemoryBlock state;
            processor.getStateInformation (state);
            MpcProcessor other;
            other.setStateInformation (state.getData(), (int) state.getSize());
            const auto& s2 = other.getProject().currentSong();
            expectEquals (s2.name, juce::String ("Tune"));
            expectEquals ((int) s2.steps.size(), 2);
            expect (s2.loop && s2.steps[0].repeats == 2 && s2.steps[1].sequence == 1);

            beginTest ("deleting a sequence takes it out of songs");
            project.removeSequence (0);
            expectEquals ((int) project.currentSong().steps.size(), 1);
            expectEquals (project.currentSong().steps[0].sequence, 0);
        }

        beginTest ("picking a sequence while playing queues it");
        {
            MpcProcessor processor;
            processor.setPlayConfigDetails (0, 2, 48000.0, 512);
            processor.prepareToPlay (48000.0, 512);
            auto& project = processor.getProject();
            auto& engine = processor.getSequencer();
            engine.clickMode = (int) SequencerEngine::ClickMode::off;
            project.getSequences()[0] = hitsOn (0);
            project.addSequence ("B");
            project.getSequences()[1] = hitsOn (7);
            project.setCurrentSequence (0);
            project.sequenceEdits.dispatchPendingMessages();

            juce::AudioBuffer<float> out (2, 512);
            juce::MidiBuffer midi;
            engine.play (true);
            processor.processBlock (out, midi);
            processor.selectSequence (1);
            expectEquals (processor.getQueuedSequence(), 1);
            expectEquals (project.getCurrentSequenceIndex(), 0, "the one playing stays on screen");
            for (int b = 0; b < 200; ++b)
            {
                processor.processBlock (out, midi);
                processor.followTransport();
            }
            expectEquals (project.getCurrentSequenceIndex(), 1, "it switched at the end");
            expectEquals (processor.getQueuedSequence(), -1);

            processor.stopTransport();
            processor.processBlock (out, midi);
            processor.selectSequence (0);
            expectEquals (project.getCurrentSequenceIndex(), 0, "stopped, it switches straight away");
        }

        beginTest ("pads in a mute group cut each other off");
        {
            MpcProcessor processor;
            processor.setPlayConfigDetails (0, 2, 44100.0, 512);
            processor.prepareToPlay (44100.0, 512);
            auto& project = processor.getProject();
            auto& bank = project.currentBank();
            bank.pads[0].audio = crate::share (sine (44100, 1, 0.5f));
            bank.pads[0].mode = PadMode::oneShot;
            bank.pads[1].audio = crate::share (sine (44100, 1, 0.0f));   // silent, so only pad 1 is heard
            bank.pads[1].mode = PadMode::oneShot;
            project.changed();
            project.dispatchPendingMessages();

            juce::AudioBuffer<float> out (2, 512);
            auto hit = [&] (int pad)
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, noteForPad (pad), 1.0f), 0);
                processor.processBlock (out, midi);
            };
            auto ringsOn = [&]
            {
                juce::MidiBuffer midi;
                for (int i = 0; i < 20; ++i)
                    processor.processBlock (out, midi);
                return out.getMagnitude (0, 512) > 0.1f;
            };

            hit (0);
            hit (1);
            expect (ringsOn(), "with no group, pads play over each other");

            bank.pads[0].muteGroup = bank.pads[1].muteGroup = 1;
            project.changed();
            project.dispatchPendingMessages();
            hit (0);
            hit (1);
            expect (! ringsOn(), "in the same group, the second cuts the first");
        }
    }
};

SequenceTests sequenceTests;
EngineTests engineTests;
SequencerProcessorTests sequencerProcessorTests;
PerformTests performTests;
}
