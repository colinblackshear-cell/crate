#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <vector>

// The song on a timeline: a lane for each loop and each tool, and blocks on the lanes that say
// when it plays. A bar ruler runs across the top, over the click.
//
// While the timeline plays (it's "on" and the clock runs), a lane plays only inside its
// blocks. A block can fade in at its start and out at its end, so one layer can fade into
// another. A loop starts from its top on its block's first beat and repeats for as long as the
// block is. A lane with no blocks at all isn't run by the timeline: it plays as you set it in
// the mixer, so you can sing or play your guitar live over the song.
//
// The tools (MPC, Bass, Synth...) play their patterns as usual; their lane's blocks bring them
// in and out. Markers name the song's sections (Verse, Chorus...); click one to jump there at
// the next bar. Cycle plays a few bars over and over.
//
// Times are in bars from the song's start (bar 1 is 0.0). The blocks belong to the message
// thread: edit them and call changed(), and the audio thread picks up a copy at its next block.
class RigTimeline
{
public:
    static constexpr int kMaxBlocks = 256;

    // The lanes: loops 1-8, then the tools.
    struct Lane
    {
        bool isLoop;
        int index;   // looper layer, or RigEngine::StripId
    };
    static constexpr int kLanes = 15;   // (the drum machine came last)
    static const std::array<Lane, kLanes>& lanes();
    static int laneForLoop (int layer) { return layer; }
    static int laneForStrip (int strip);   // -1 if the strip has no lane

    struct Block
    {
        int lane = 0;
        double start = 0, length = 4;      // bars
        double fadeIn = 0, fadeOut = 0;    // bars (a beat is 1 / beats per bar)
        double end() const { return start + length; }
    };
    struct Marker
    {
        double bar = 0;
        juce::String name;
    };

    //==========================================================================
    // Message thread.
    std::vector<Block>& getBlocks() { return blocks; }
    const std::vector<Block>& getBlocks() const { return blocks; }
    std::vector<Marker>& getMarkers() { return markers; }
    const std::vector<Marker>& getMarkers() const { return markers; }
    // After changing the blocks: tidies them (no zero lengths, fades that fit) and hands the
    // audio thread a copy.
    void changed();
    void clear();
    bool isEmpty() const { return blocks.empty(); }
    // Where the last block ends (0 for none).
    double lengthBars() const;
    bool laneUsed (int lane) const;
    // The block on `lane` under `bar` (the last one that starts there, if they overlap), or -1.
    int blockAt (int lane, double bar) const;
    // How loud a lane is at `bar` (0..1) in `list`: 1 if the lane has no blocks.
    static float gainAt (const Block* list, int count, int lane, double bar, bool laneUsed);
    float gainAt (int lane, double bar) const { return gainAt (blocks.data(), (int) blocks.size(), lane, bar, laneUsed (lane)); }
    // The start of the block that plays `lane` at `bar` (-1 for none).
    static double blockStartAt (const Block* list, int count, int lane, double bar);

    // The timeline runs the mix while the clock plays.
    std::atomic<bool> on { false };
    // Cycle: plays from cycleStart to cycleEnd (bars) over and over.
    std::atomic<bool> cycle { false };
    std::atomic<double> cycleStart { 0 }, cycleEnd { 4 };
    // Stops the clock a bar after the last block ends (unless cycling).
    std::atomic<bool> stopAtEnd { true };
    // Jumps to `bar` at the next bar line (while playing).
    void jumpTo (double bar) { jumpRequest = juce::jmax (0.0, bar); }
    double getPendingJump() const { return jumpTarget.load(); }

    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);   // an invalid tree empties it
    static constexpr const char* kTreeType = "Timeline";

    //==========================================================================
    // Audio thread.
    // Picks up the newest copy of the blocks. Call at the start of each block.
    void update();
    float liveGain (int lane, double bar) const { return gainAt (live.blocks.data(), live.count, lane, bar, live.used[(size_t) lane]); }
    double liveBlockStart (int lane, double bar) const { return blockStartAt (live.blocks.data(), live.count, lane, bar); }
    double liveEnd() const { return live.end; }
    bool liveEmpty() const { return live.count == 0; }

    // Where the clock should go now, if anywhere (a jump whose bar has come, or the end of the
    // cycle): returns the beat to go to, or -1. `beat` is where the block starts.
    double takeJump (double beat, int beatsPerBar);
    // Beats from `beat` to the next jump or cycle end (a large number if none), so the engine
    // can play up to it exactly.
    double beatsToNextEvent (double beat, int beatsPerBar) const;

private:
    struct Snapshot
    {
        std::array<Block, kMaxBlocks> blocks {};
        int count = 0;
        std::array<bool, kLanes> used {};
        double end = 0;
    };
    static void fill (Snapshot&, const std::vector<Block>&);

    std::vector<Block> blocks;
    std::vector<Marker> markers;

    juce::SpinLock lock;
    Snapshot pending;    // under lock
    int pendingVersion = 0;
    Snapshot live;       // audio thread
    int liveVersion = 0;

    std::atomic<double> jumpRequest { -1.0 };   // bars
    std::atomic<double> jumpTarget { -1.0 };    // bars, waiting for its bar line
    double jumpAtBeat = -1.0;                    // audio thread
};
