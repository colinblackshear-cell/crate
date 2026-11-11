#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <vector>

// Scenes: the parts of a song (intro, verse, drop...). A scene remembers what's playing: the
// MPC's sequence, the bass pattern, whether the bass sequencer and the synth's arp run, which
// mixer strips and loop layers are muted, and whether the vocoder and the input are on.
//
// Launch a scene and it takes over at the next bar line, so the song changes on the beat.
// In song mode, a scene with a length plays that many bars and then the next one starts by
// itself, so a song is a list of scenes you can let run or step through live.
//
// The list belongs to the message thread. The audio thread gets the scene to play next as a
// plain copy, and changes the mutes exactly at the bar.
struct RigScene
{
    static constexpr int kMaxStrips = 16, kLayers = 8;

    // What it changes. -1 (or "leave") keeps what's playing.
    struct State
    {
        int mpcSequence = -1;
        int bassPattern = -1;
        int bassSequencer = -1;   // 0 off, 1 on
        int arp = -1;             // 0 off, 1 on
        int vocoderOn = -1, inputOn = -1;
        int drumPattern = -1, drumSequencer = -1;   // the drum machine
        std::array<int, kMaxStrips> stripMutes {};   // 0 playing, 1 muted, -1 leave
        std::array<int, kLayers> loopMutes {};
        State() { stripMutes.fill (-1); loopMutes.fill (-1); }
    };

    juce::String name;
    int bars = 0;   // in song mode: bars to play before the next scene (0 = stay)
    State state;

    juce::ValueTree toTree() const;
    static RigScene fromTree (const juce::ValueTree&);
};

class RigScenes
{
public:
    static constexpr int kMaxScenes = 32;

    //==========================================================================
    // Message thread.
    std::vector<RigScene>& getScenes() { return scenes; }
    const std::vector<RigScene>& getScenes() const { return scenes; }

    // Launches a scene at the next bar (now, if the clock is stopped).
    void launch (int index);
    void next();
    void back();
    void clear();   // no scenes (loading a show)
    // Editing the list while it plays: the part that's playing (and the one coming up) stay
    // the same parts, wherever they move to.
    void move (int from, int to);
    // Deletes a part. If it was coming up next, it no longer is.
    void remove (int index);
    // A copy of a part, put right after it. Returns its index (-1 when the song is full).
    int duplicate (int index);
    int getCurrent() const { return current; }
    int getQueued() const { return queued; }
    double getQueuedBeat() const { return queuedBeat; }
    // The beat the current scene started on (its bar line).
    double getCurrentStartBeat() const { return currentStartBeat; }

    // Song mode: scenes with a length move on by themselves.
    bool songMode = false;

    // Called often by the engine's message-thread tick with the clock's position. Returns a
    // scene whose MPC sequence and bass pattern should change now, a moment before its bar, so
    // those tools are ready on the beat (-1 for none).
    int tick (double beat, bool playing, int beatsPerBar);

    //==========================================================================
    // Audio thread: the scene to play at `beat` (or right away if not playing). Returns true and
    // fills `out` when its bar has come.
    bool takeDue (double beat, bool playing, RigScene::State& out, double& firedBeat);

    // Learned Next and Back buttons, pressed on the audio thread.
    std::atomic<int> nextPresses { 0 }, backPresses { 0 };

private:
    void queue (int index, double beat);

    std::vector<RigScene> scenes;
    int current = -1, queued = -1;
    double queuedBeat = 0, lastBeat = 0;
    bool lastPlaying = false;
    int lastBeatsPerBar = 4;
    bool earlyDone = false;
    double currentStartBeat = 0;
    std::atomic<double> firedAt { -1.0 };
    std::atomic<int> firedScene { -1 };

    juce::SpinLock pendingLock;
    bool pending = false;          // under pendingLock
    double pendingBeat = 0;
    int pendingIndex = -1;
    RigScene::State pendingState;
};
