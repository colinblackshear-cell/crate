#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

// The phone's song: the loop layers on a bar timeline, the same idea as the Mac's Song screen. A block says
// a layer plays over those bars (with fades in and out, a level, and a slip: how far into its loop it is on
// the block's first bar). While the song plays, a layer plays only inside its blocks; a layer with none plays
// as you set it (its M).
//
// REC LIVE plays the song into it: after a count-in, bring layers in and out (their tiles, or their M) and each
// change lands on the next bar, written down as blocks while you play. STOP + KEEP keeps the run as a new take;
// nothing is recorded over. PUNCH plays bars again (from a 2-bar run-up) and replaces only those.
//
// The phone's clock can't jump, so the song keeps `startBar`: the song's bar on the clock's beat 0 (playing from
// bar 9 starts the clock with startBar 8). Four beats to the bar.
//
// Threads: everything is the message thread's except update(), gain() and originBeat(), which the audio thread
// calls (on a copy of the blocks handed over with a try-lock).
namespace pocket
{
struct SongBlock
{
    int layer = 0;
    double start = 0, length = 4;     // bars from the song's start
    double fadeIn = 0, fadeOut = 0;   // bars
    float level = 1.0f;
    double offset = 0;                // bars into the loop on the block's first bar
    double end() const { return start + length; }
};

struct SongMarker
{
    double bar = 0;
    juce::String name;
};

class PocketSong
{
public:
    static constexpr int kMaxBlocks = 128;
    static constexpr int kLayers = 8;
    static constexpr double kBeatsPerBar = 4.0;

    // What the song needs from the looper (the message thread's).
    struct Host
    {
        std::function<bool (int layer)> hasLoop, isMuted;
        std::function<void (int layer, bool)> setMute;
        std::function<double (int layer)> loopBeats, loopStart;   // its length, and the clock beat its top is on
        std::function<double()> clockBeat;
        std::function<bool()> clockRunning;
        std::function<void (bool)> setClock;            // starts (from beat 0) or stops the clock
        std::function<void (double untilBeat)> countIn; // the click counts until this beat (-1: none)
    };
    explicit PocketSong (Host h) : host (std::move (h)) {}

    //==========================================================================
    // Editing (message thread). Change `blocks` / `markers`, then changed().
    std::vector<SongBlock> blocks;
    std::vector<SongMarker> markers;
    void changed();
    int edits() const { return editCount; }
    double lengthBars() const;               // where the last block (or the last section) ends
    bool layerUsed (int layer) const;
    int blockAt (int layer, double bar) const;
    // Sections from the markers: [start, end) and names. None: one section, the whole song.
    struct Section { juce::String name; double start = 0, end = 0; };
    std::vector<Section> sections() const;
    juce::String suggestName() const;
    static double wrap (double x, double length);

    // Edits the song screen makes (each keeps the loop where it was in time).
    bool split (int block, double bar);
    void trimStart (int block, double newStart);
    void deleteBlock (int block);
    int copyBlock (int block);               // right after it; returns the copy
    double loopBars (int layer) const;

    //==========================================================================
    // Playing it.
    void play (double fromBar = 0);          // the song in charge, from that bar
    void stopPlaying();
    bool isOn() const { return on.load(); }
    double barNow() const { return startBar.load() + host.clockBeat() / kBeatsPerBar; }
    // The timer's part (~30 Hz): Rec live, and the song stopping a bar after its end.
    void tick();

    //==========================================================================
    // Rec live.
    enum class State { idle, waiting, recording };
    State getState() const { return state; }
    bool isLive() const { return state != State::idle; }
    bool isPunch() const { return punch; }
    int countInBars = 1;
    bool startLive (juce::String* why = nullptr);
    bool startPunch (double from, double to, juce::String* why = nullptr);   // to < 0: on until stopped
    int stopAndKeep();
    void cancel();
    void mark();
    void toggle (int layer);                 // in <-> out at the next bar
    bool isIn (int layer) const;
    double changeAt (int layer) const;       // the bar a pending change lands on, -1 for none
    double liveStart() const { return startBarLive; }
    double liveEnd() const { return endBar; }
    int changes() const { return changeCount; }
    juce::String message() const { return lastMessage; }

    //==========================================================================
    // Takes.
    struct Take
    {
        juce::String name;
        std::vector<SongBlock> blocks;
        std::vector<SongMarker> markers;
    };
    const std::vector<Take>& takes() const { return takeList; }
    int currentTake() const { return current; }
    bool useTake (int);
    bool deleteTake (int);
    void clear();   // a new song

    juce::var toVar() const;
    void fromVar (const juce::var&);

    //==========================================================================
    // Audio thread.
    void update();
    bool running() const { return liveOn; }   // after update(): the song decides what plays
    double barAt (double clockBeat) const { return liveStartBar + clockBeat / kBeatsPerBar; }
    float gain (int layer, double bar) const;
    // The clock beat a layer's loop has its top on, inside the block at `bar` (or -1e9: none).
    double originBeat (int layer, double bar) const;

private:
    struct LaneState
    {
        bool in = false;
        double openStart = -1, outAt = -1;
        bool lastMute = true;
    };
    bool begin (double first, double runFrom, bool fresh, juce::String* why);
    void finish (bool keep);
    double nextBar() const;
    int openBlock (int layer) const;
    void bringIn (int layer, double bar);
    void takeOut (int layer, double bar);
    void extendOpen();
    void storeCurrent();

    Host host;
    std::atomic<bool> on { false };
    std::atomic<double> startBar { 0.0 };
    std::atomic<juce::uint32> gated { 0 };
    int editCount = 0;
    double pendingFrom = -1;   // play() waits for the clock to stop before starting it from here
    int pendingTicks = 0;
    bool seenRunning = false;

    // Rec live.
    State state = State::idle;
    bool punch = false, fresh = false, arrived = true;
    double startBarLive = 0, endBar = -1, origin = 0, lastNow = 0;
    std::array<LaneState, kLayers> lanes {};
    std::vector<int> liveLayers;
    int changeCount = 0, marks = 0, waitTicks = 0;
    juce::String lastMessage;
    std::vector<SongBlock> beforeBlocks;
    std::vector<SongMarker> beforeMarkers;
    int beforeTake = -1;
    std::vector<Take> takeList;
    int current = -1;

    // The audio thread's copy.
    struct Snapshot
    {
        std::array<SongBlock, kMaxBlocks> blocks {};
        int count = 0;
        std::array<bool, kLayers> used {};
        bool on = false;
        juce::uint32 gated = 0;
        double startBar = 0;
    };
    juce::SpinLock lock;
    Snapshot pending, live;
    int pendingVersion = 0, liveVersion = 0;
    bool liveOn = false;
    double liveStartBar = 0;
    void publish();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PocketSong)
};
}
