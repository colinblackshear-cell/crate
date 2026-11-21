#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <vector>

// Recording into the song ("Rec into", along the bottom of every screen): one track, played over
// the song, into one section of it, on a loop layer of its own.
//
// It starts from the top of the section (the song jumps there, or starts there when stopped) and
// records to its end. With Cycle on, the section plays over and over and every pass is a take of
// its own: nothing is recorded over, so you can keep trying until it's right and then pick the
// best one. Without Cycle it's one pass and the song plays on.
//
// Each pass is exactly as long as the section and starts at its top, so a take always sits in
// time with the song wherever you started. A pass cut short (you pressed stop, or the clock
// stopped) is kept as a take too, filled out with silence to the section's length, as long as it
// has at least a bar in it.
//
// The takes of one layer are its Set: the newest goes on the layer as it comes in (the engine
// puts it there and into the song's section), and any of them can be put back on later.
//
// Threads: start, stop, collect and the sets are the message thread's; process is the audio
// thread's. The audio thread only writes into buffers made ready for it, and hands each finished
// pass back through a flag.
class RigTakes
{
public:
    static constexpr int kPool = 3;               // passes in flight (recording, or waiting to be collected)
    static constexpr int kMaxTakes = 12;          // per layer; the oldest one not playing goes first
    static constexpr double kMaxSeconds = 600.0;  // the longest section that can be recorded into

    RigTakes() = default;

    enum class State { idle, waiting, recording };

    struct Take
    {
        juce::AudioBuffer<float> audio;   // stereo, the section's length
        double beats = 0;
        bool partial = false;             // cut short (the rest is silence)
        int number = 1;                   // Take 1, Take 2... (kept when older ones are deleted)
    };
    // The takes of one loop layer, over one stretch of the song.
    struct Set
    {
        int layer = -1, source = 0;
        double startBar = 0, bars = 0;
        std::vector<Take> takes;
        int playing = -1;                 // the take on the layer (an index into takes)
        int nextNumber = 1;
        bool fresh = false;               // a new take came in that isn't on the layer yet
    };

    //==========================================================================
    // Message thread.
    // Records `source` (a RigLooper::Source) for loop `layer` over the song's bars [startBar,
    // startBar + bars). The caller gets the song there. False (with why) when it can't: already
    // recording, or the section is too long or empty.
    bool start (int layer, int source, double startBar, double bars, bool cycle,
                double sampleRate, double bpm, int beatsPerBar, juce::String* why = nullptr);
    // Stops: a pass in the middle is kept as a take (cut short) if it has a bar in it. While still
    // waiting for the section's top, it's just called off.
    void stop();
    State getState() const { return (State) state.load(); }
    bool isActive() const { return getState() != State::idle || activeLayer.load() >= 0; }
    // The layer and source being recorded (-1 when idle).
    int getLayer() const { return activeLayer.load(); }
    int getSource() const { return activeSource.load(); }
    bool isCycling() const { return cycle.load(); }
    void setCycle (bool c) { cycle = c; }
    double getStartBar() const { return activeStart; }
    double getBars() const { return activeBars; }
    // The take being recorded (its number), and how far through the section it is (0..1).
    int passNumber() const;
    // The number the next take will have, while waiting for the section's top.
    int nextPassNumber() const { return firstNumber + passesStarted.load(); }
    double passProgress() const;

    // Picks up finished passes as takes (call often, ~10 times a second). Returns how many came in.
    // Also notices when the recording ended by itself (one pass done, the clock stopped).
    int collect();
    // Counts every take that ever came in, so a window can tell there's a new one.
    int getTakesMade() const { return takesMade; }

    Set* setFor (int layer);
    const Set* setFor (int layer) const;
    // A set for this source over exactly these bars (-1: none), so trying again adds to it.
    int layerFor (int source, double startBar, double bars) const;
    // Deletes a take (the one playing can't go while it's the only one). False if it can't.
    bool deleteTake (int layer, int take);
    // Keeps only the take playing.
    void keepOnlyPlaying (int layer);
    // Forgets a layer's takes (it was cleared, or used for something else).
    void forget (int layer);
    // A new song: forgets everything (stops a recording first).
    void clear();

    //==========================================================================
    // Audio thread: once per block, with the recorded source's block (stereo; null for silence)
    // and the clock at the block's start (beats from the song's start). The engine splits its
    // blocks at the song's jumps, so a jump only ever falls between two calls.
    void process (const juce::AudioBuffer<float>* source, int numSamples, double beat, double bpm,
                  bool playing, double sampleRate);

private:
    void finishPass();

    // What the message thread set up (read by the audio thread after it sees `state`).
    std::atomic<int> state { (int) State::idle };
    std::atomic<bool> stopRequested { false }, cycle { true };
    double startBeat = 0, endBeat = 0;
    int expected = 0;                     // samples in a whole pass
    std::atomic<int> activeLayer { -1 }, activeSource { 0 };   // (the engine reads them on the audio thread)
    double activeStart = 0, activeBars = 0;
    double rate = 44100.0;
    int beatsPerBar = 4;

    // The passes. status: 0 free, 1 being written, 2 finished (waiting for collect).
    std::array<juce::AudioBuffer<float>, kPool> pool;
    std::array<std::atomic<int>, kPool> status {};
    std::array<int, kPool> written {};
    std::array<int, kPool> order {};      // which pass it was, so they're collected in order
    int current = -1, writeIndex = 0, passCount = 0, firstNumber = 1;
    double lastEnd = -1.0;
    std::atomic<int> passesStarted { 0 }, progressSamples { 0 };

    std::vector<Set> sets;
    int takesMade = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigTakes)
};
