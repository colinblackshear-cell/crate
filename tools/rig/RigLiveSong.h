#pragma once

#include "RigTimeline.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <vector>

class RigEngine;

// Rec live: the song, played. Press Rec live on the Song screen and, after a count-in, bring
// loops and tools in and out (the Rec live pads, a layer's M, the mixer, the Launchkey: any way
// you mute or unmute them). Each change lands on the next bar, and the song writes it down as
// blocks on the timeline while you play, so what you hear is what it keeps.
//
//   Mark           a new section from the next bar (with none, a section every 8 bars)
//   Stop + Keep    ends it: the run is a new take (Take 1, Take 2...) and the clock stops
//   Cancel         throws the run away: the song is back as it was
//   Punch          plays the song from 2 bars before `from` and replaces only bars from..to
//   From the end   carries the song on from where it ends
//
// Takes: each run is kept as a take of its own, so nothing ever records over the song. The
// song on the timeline is the take picked (edits to it are that take's); pick another to hear
// it. Saved with the show.
//
// A loop that comes in keeps its groove: its block is slipped so the loop carries on where it
// would be (lined up with the take's first bar), not from its top. The live inputs (Vocoder,
// Line in) aren't written: sing over it.
//
// Message thread only. tick() (~30 Hz, from the engine's tickScenes) moves it on.
class RigLiveSong
{
public:
    explicit RigLiveSong (RigEngine&);

    enum class State { idle, waiting, recording };
    State getState() const { return state; }
    bool isActive() const { return state != State::idle; }
    bool isPunch() const { return punch; }

    // Bars of count-in from a stopped clock (0-2).
    int countInBars = 1;

    //==========================================================================
    // A new take from the top. With the clock stopped it counts in and starts at bar 1; while
    // it's playing (you're jamming) it starts on the next bar where your loops are at their top.
    // What's playing when it starts is in from its first bar.
    bool start (juce::String* why = nullptr);
    // Replaces bars [from, to) of the song with a new run (to < 0: on from `from` until you stop,
    // so from the song's end it carries the song on). Plays from 2 bars before.
    bool startPunch (double from, double to, juce::String* why = nullptr);
    bool startFromEnd (juce::String* why = nullptr);
    // Ends it. Keep: the run is a new take (and the clock stops). Returns the take, or -1.
    int stopAndKeep();
    void cancel();
    // A new section at the next bar.
    void mark();
    // In <-> out at the next bar (what the pads do). Same as pressing the lane's M.
    void toggle (int lane);

    //==========================================================================
    // For the screens.
    double getStartBar() const { return startBar; }      // the first bar written (timeline bars)
    double getEndBar() const { return endBar; }          // punch's end, -1 for none
    double nowBar() const;
    int getChanges() const { return changes; }
    // The lanes being written (loops with something on them, and the tools that play notes).
    const std::vector<int>& getLanes() const { return lanes; }
    // Every lane Rec live could write (a loop with audio, a tool with something to play).
    std::vector<int> playableLanes() const;
    bool isIn (int lane) const;
    // The bar where a lane's next change lands (in or out), -1 for none.
    double changeAt (int lane) const;
    // Says how the run went, or why it ended ("Take 2 kept: 48 bars, 31 changes").
    juce::String takeMessage() const { return lastMessage; }

    //==========================================================================
    // Takes.
    struct Take
    {
        juce::String name;
        std::vector<RigTimeline::Block> blocks;
        std::vector<RigTimeline::Marker> markers;
    };
    const std::vector<Take>& getTakes() const { return takes; }
    int getCurrentTake() const { return current; }
    // Puts take `i` on the timeline (keeping the edits made to the one there). False: no such take.
    bool useTake (int i);
    // Deletes a take (not while recording). The one on the timeline stays there, as no take.
    bool deleteTake (int i);
    // Copies the timeline into the take it is (call before saving).
    void storeCurrent();
    void clear();   // a new song

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);
    static constexpr const char* kTreeType = "LiveSong";

    void tick();

private:
    struct LaneState
    {
        bool in = false;            // has an open block (it may start on a bar still to come)
        double openStart = -1;      // that block's start
        double outAt = -1;          // going out at this bar
        bool lastMute = true;       // the mute we last saw (or set)
    };

    bool begin (double first, double runFrom, bool fresh, juce::String* why);
    void finish (bool keep);
    bool getMute (int lane) const;
    void setMute (int lane, bool);
    double nextBar() const;
    double loopBars (int lane) const;
    double slipFor (int lane, double bar) const;
    int openBlock (int lane) const;
    void bringIn (int lane, double bar);
    void takeOut (int lane, double bar);
    void extendOpen();
    double phraseStart() const;

    RigEngine& engine;
    State state = State::idle;
    bool punch = false, fresh = false;
    double startBar = 0, endBar = -1, origin = 0, lastNow = 0, runFromBar = 0;
    bool arrived = true;
    int waitTicks = 0;   // the clock has got to where the run plays from
    std::vector<int> lanes;
    std::array<LaneState, RigTimeline::kLanes> laneStates {};
    int changes = 0, marks = 0;
    juce::String lastMessage;

    // The song as it was when the run began (Cancel puts it back).
    std::vector<RigTimeline::Block> beforeBlocks;
    std::vector<RigTimeline::Marker> beforeMarkers;
    int beforeTake = -1;

    std::vector<Take> takes;
    int current = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigLiveSong)
};
