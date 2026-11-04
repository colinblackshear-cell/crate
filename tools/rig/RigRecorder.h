#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Records the set for Logic: every mixer strip as its own WAV (as you hear it, after its
// effects and fader), the whole mix, and what you played on the keys and pads as a MIDI file
// with a track for each tool. Everything starts on the same bar line, so in Logic you set the
// tempo, drop all the files at bar 1 and they line up.
//
// Press record and it starts at the next bar (or when the clock starts, if it's stopped). It
// stops when you press again or the clock stops. A strip that stayed silent isn't saved.
//
// The audio thread hands the sound to background writers and the notes to a queue; it never
// waits for the disk. The message thread starts and finishes a take and calls service() often.
class RigRecorder
{
public:
    RigRecorder();
    ~RigRecorder();

    enum class State { idle, armed, recording, finishing };
    State getState() const { return (State) state.load(); }

    //==========================================================================
    // Message thread.

    // Gets ready to record `stemNames` (one stereo WAV each) and `midiTracks` (one MIDI track
    // each) into `folder`, which is made. The take starts at the next bar.
    bool start (const juce::File& folder, const juce::StringArray& stemNames, const juce::StringArray& midiTracks,
                double sampleRate);
    // Stops the take (the audio thread stops writing at its next block).
    void stop();
    // Collects played notes, and once a take has stopped closes its files and writes the MIDI
    // file and a note on how to open it in Logic. Returns true when a take has just been saved.
    bool service();

    juce::File getFolder() const { return folder; }
    double getRecordedSeconds() const;
    // The files the last take saved.
    const juce::Array<juce::File>& getSavedFiles() const { return saved; }

    //==========================================================================
    // Audio thread, once a block, after the mix. `beat` is the clock at the start of the block.
    struct Clock
    {
        double beat = 0, bpm = 120, sampleRate = 44100;
        int beatsPerBar = 4;
        bool playing = false;
    };
    void process (const Clock&, const std::vector<const juce::AudioBuffer<float>*>& stems, int numSamples);
    // A note or controller played to MIDI track `track`, at `sample` in this block (call
    // before process(), with the same block's clock).
    void addMidi (int track, const juce::MidiMessage&, int sample, const Clock&);

private:
    void finish();

    struct Event
    {
        int track = 0;
        double beat = 0;   // from the start of the take
        juce::uint8 data[3] {};
        int size = 0;
    };

    std::atomic<int> state { (int) State::idle };
    std::atomic<bool> stopRequested { false };

    juce::File folder;
    juce::StringArray stemNames, trackNames;
    juce::TimeSliceThread writerThread { "Rig recorder" };
    std::vector<std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter>> writers;
    std::vector<juce::File> stemFiles;
    std::unique_ptr<std::atomic<float>[]> stemPeaks;
    int numStems = 0;

    // The take's clock (written by the audio thread when it starts).
    std::atomic<double> startBeat { 0 }, startBpm { 120 };
    std::atomic<int> startBeatsPerBar { 4 };
    std::atomic<long long> samplesWritten { 0 };
    double sampleRate = 44100;

    juce::AbstractFifo eventFifo { 8192 };
    std::array<Event, 8192> events;
    std::vector<juce::MidiMessageSequence> sequences;
    juce::Array<juce::File> saved;

    JUCE_DECLARE_NON_COPYABLE (RigRecorder)
};
