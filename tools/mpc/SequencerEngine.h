#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Sequence.h"

#include <array>
#include <atomic>
#include <memory>

// The sequencer's clock, run on the audio thread. Each block it turns the sequence into pad
// notes (MIDI, on the MPC's pad notes) at the right samples, ticks the metronome, repeats held
// pads for note repeat, and catches what you play while recording.
//
// It runs on its own tempo in the app, and follows Logic's tempo and play position when Logic is
// playing. Everything here is safe to call from the audio thread: no locks held while playing,
// no allocation, and snapshots are never freed here.
class SequencerEngine
{
public:
    // What the audio thread plays: the current sequence with muted tracks left out.
    struct Snapshot
    {
        std::vector<seq::Note> notes;   // sorted by tick
        int length = seq::kTicksPerBeat * 8;
        int beatsPerBar = 4;
        double bpm = 90.0;
        bool loop = true;
        int id = 0;                     // which sequence it is
        int ticksPerBar() const { return beatsPerBar * seq::kTicksPerBeat; }
    };

    // `id` is the sequence's index; a song plays every sequence as a loop.
    static std::shared_ptr<const Snapshot> makeSnapshot (const seq::Sequence&, int id = 0, bool forceLoop = false);

    // A note you played while recording, already timing-corrected, for the message thread to
    // add to the track.
    struct Recorded
    {
        int tick = 0, pad = 0, velocity = 100, length = 24;
    };

    struct Host
    {
        bool playing = false;
        double ppq = 0;
        double bpm = 120;
    };

    // A metronome click starting at a sample in this block.
    struct Click
    {
        int offset = 0;
        bool accent = false;
    };

    enum class ClickMode { off, recording, always };

    SequencerEngine();

    //==========================================================================
    // Message thread.
    void setSnapshot (std::shared_ptr<const Snapshot>);
    void freeUnusedSnapshots();

    // Plays this sequence next: when the one playing reaches its end, or at the next bar line.
    // Null clears the queue. Song mode queues each step's sequence this way.
    void queue (std::shared_ptr<const Snapshot>, bool atNextBar);
    // Stop when the sequence playing reaches its end (the end of a song).
    std::atomic<bool> stopAtEnd { false };

    // On in the Rig: a queued sequence (and so a song) still takes over while following the
    // host's clock, starting from its top where it switches, so the MPC changes on the Rig's
    // bar lines. Off (in Logic) a sequence just follows Logic's position, as before.
    std::atomic<bool> switchWhileFollowing { false };

    int getPlayingId() const { return playingIdNow.load(); }   // the sequence playing (or last played)
    int getPasses() const { return passesNow.load(); }         // counts each time a sequence ends or switches
    bool hasQueued() const { return queuedNow.load(); }

    void play (bool fromStart);
    void record();               // from stopped: count in and record from the start; playing: punch in or out
    void stop();
    void locate (double tick);   // moves the stopped playhead

    std::atomic<int> grid { (int) seq::Grid::sixteenth };
    std::atomic<int> swing { 50 };
    std::atomic<bool> noteRepeat { false };
    std::atomic<int> clickMode { (int) ClickMode::recording };
    std::atomic<bool> countIn { true };
    std::atomic<bool> followHost { true };   // in Logic: play along when Logic plays

    bool isPlaying() const { return playingNow.load(); }
    bool isRecording() const { return recordingNow.load(); }
    bool isFollowingHost() const { return followingNow.load(); }
    double getHostBpm() const { return hostBpmNow.load(); }   // the host's tempo while following it
    // Where the playhead is in the sequence, in ticks; below 0 while counting in.
    double getPosition() const { return positionNow.load(); }

    // Takes the notes recorded since the last call.
    int takeRecorded (Recorded* dest, int max);

    //==========================================================================
    // Audio thread. `played` holds the pad notes you played this block (clicks and MIDI in);
    // the sequence's notes are added to `out`, and metronome clicks to `clicks`.
    void prepare (double sampleRate);
    void process (int numSamples, const Host*, const juce::MidiBuffer& played, juce::MidiBuffer& out,
                  std::array<Click, 16>& clicks, int& numClicks);

    static constexpr int kSkipSlots = 64;
    static constexpr int kNoteOffSlots = 128;

private:
    struct Segment
    {
        double from = 0, to = 0;    // ticks in the sequence; from < to
        double sampleStart = 0;     // where `from` is in the block
    };

    void emitSegment (const Snapshot&, const Segment&, double samplesPerTick, juce::MidiBuffer& out,
                      std::array<Click, 16>& clicks, int& numClicks, bool clicking, int numSamples);
    void recordHit (const Snapshot&, int pad, int velocity, double tick, double ticksNow);
    void finishHeld (int pad, double tick, int length);
    void push (const Recorded&);
    void noteOn (juce::MidiBuffer&, int pad, int velocity, int offset, int lengthSamples);

    double sampleRate = 44100.0;

    juce::SpinLock snapshotLock;
    std::shared_ptr<const Snapshot> latest;                       // message thread writes, under the lock
    std::vector<std::shared_ptr<const Snapshot>> retired;         // message thread only
    std::shared_ptr<const Snapshot> playingSnapshot;              // audio thread only
    int latestVersion = 0, seenVersion = -1;                      // so a queued switch isn't undone
    std::shared_ptr<const Snapshot> queuedLatest;                 // message thread writes, under the lock
    bool queuedAtBarLatest = false;
    int queuedVersion = 0, seenQueuedVersion = 0;
    std::vector<std::shared_ptr<const Snapshot>> retiredQueued;   // message thread only
    std::shared_ptr<const Snapshot> queuedSnapshot;               // audio thread only
    bool queuedAtBar = false;
    int passes = 0;
    void switchToQueued();

    // Transport requests from the message thread.
    std::atomic<int> request { 0 };        // 0 none, 1 play, 2 play from start, 3 record, 4 stop, 5 locate
    std::atomic<double> locateTo { 0 };

    // Audio-thread state.
    bool playing = false, recording = false, following = false;
    double tick = 0;                 // sequence position; negative while counting in
    double anchor = 0;               // following: the host tick where the playing pass began
    long long sampleClock = 0;       // samples processed, to time note repeat after a press

    struct Held { bool down = false; int velocity = 100; long long pressedAt = 0; double startTick = 0; bool recording = false; int recordedTick = 0; };
    std::array<Held, 64> held {};

    struct Skip { int pad = -1; int tick = 0; };
    std::array<Skip, kSkipSlots> skips {};

    struct NoteOff { int pad = -1; double samplesLeft = 0; };
    std::array<NoteOff, kNoteOffSlots> noteOffs {};

    // Recorded notes waiting for the message thread.
    juce::AbstractFifo fifo { 1024 };
    std::array<Recorded, 1024> fifoData {};

    std::atomic<bool> playingNow { false }, recordingNow { false }, followingNow { false }, queuedNow { false };
    std::atomic<double> hostBpmNow { 0.0 };
    std::atomic<int> playingIdNow { 0 }, passesNow { 0 };
    std::atomic<double> positionNow { 0 };
};
