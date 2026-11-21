#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

// Automation: knob moves recorded over time, played back on the beat.
//
// Press Rec moves and, from the next bar (now, starting the clock, if it's stopped), every
// setting you move on any tool or its effects (the bass's Cutoff, the synth's filter, a delay's
// feedback, from the screen or a learned knob) is recorded, one lane per setting. Press again
// and it closes at the end of that bar: each lane is then that many bars long and plays its
// moves back from the bar it started on, round and round like a loop (Loop), or once along the
// song from where you recorded it (Once, the default when you record over the song).
//
// Lanes follow the clock's beat, so they line up with loops and the song (in Logic, with
// Logic's bars), and they're saved with the show. While a lane plays, moving its setting takes
// over for as long as you're moving it, then the lane carries on (like a DAW's Touch mode).
// Recording again on a setting that has a lane replaces that lane. Switches (patterns, on/off,
// wave choices) are never recorded: only settings that turn smoothly.
//
// Settings are named the way knob Learn names them: a tool (RigEngine::ToolId) and the index in
// its parameter list.
//
// Threads: process() runs on the audio thread; everything else on the message thread. The audio
// thread only try-locks, so editing never makes it wait (a block it can't get in, nothing moves).
class RigAutomation
{
public:
    struct Point
    {
        double beat = 0;   // from the lane's start
        float value = 0;   // the setting's 0..1 value
    };

    struct Lane
    {
        int tool = 0, parameter = 0;
        juce::String paramId;   // the setting's ID, so a saved show finds it again
        double startBeat = 0, lengthBeats = 4;
        bool loop = true, on = true;
        std::vector<Point> points;   // in order of beat, within 0..lengthBeats
        // The value at `beat` from the lane's start (straight lines between points).
        float valueAt (double beat) const;
        // Where the lane is at the clock's `clockBeat`, from its start: -1 when it isn't playing
        // there (Once, before its start or after its end).
        double positionAt (double clockBeat) const;
    };

    // How it reaches the tools' settings (the engine sets this up). Null for a tool or setting
    // that doesn't exist.
    std::function<juce::AudioProcessorParameter* (int tool, int parameter)> getParameter;
    std::function<int (int tool)> numParameters;
    std::function<juce::String (int tool)> toolName;
    int numTools = 0;

    static constexpr int kMaxLanes = 64;
    static constexpr int kMaxRecording = 32;   // settings recorded at once

    RigAutomation() = default;

    //==========================================================================
    // Message thread.
    enum class RecState { off, armed, recording, closing };
    // Rec moves: arms (from the next bar), or closes at the end of the bar if it's recording.
    // `overSong`: lanes recorded now play Once (along the song) instead of looping.
    void pressRecord (bool overSong);
    RecState getRecState() const { return (RecState) recState.load(); }
    // Calls off an armed recording (nothing was recorded yet).
    void cancelRecord();
    // The settings moved so far in this recording.
    int getNumRecording() const { return numRecording.load(); }
    // Call often (the engine's timer): finishes a recording into lanes (true when it did), and
    // gives a long one more room.
    bool service();

    // Read: lanes move their settings (on by default). Off, nothing plays back.
    std::atomic<bool> reading { true };
    // Changes whenever the lanes do (for the window to redraw).
    int getVersion() const { return version.load(); }
    // How the last recording ended, for the window: how many lanes it made (or replaced).
    int takeFinishedLanes() { return finishedLanes.exchange (-1); }

    std::vector<Lane> getLanes() const;
    int getNumLanes() const;
    juce::String laneName (int index) const;
    juce::String laneName (const Lane&) const;
    // Which lane plays a setting, or -1.
    int findLane (int tool, int parameter) const;
    // Edits (each one can be undone).
    void setLane (int index, const Lane&);
    void setLaneOn (int index, bool);
    void setLaneLoop (int index, bool);
    void setLaneLength (int index, double beats);   // the moves after the new end are cut
    // Draws a straight line of values from (`fromBeat`, `fromValue`) to (`toBeat`, `toValue`),
    // replacing the moves in between (beats from the lane's start). `snapshot`: whether this
    // starts a new undo step (the first drag of a stroke).
    void drawLine (int index, double fromBeat, float fromValue, double toBeat, float toValue, bool snapshot);
    void clearLane (int index);
    void clearAll();
    bool canUndo() const { return ! undoStack.empty(); }
    void undo();
    // Adds a lane (or replaces the one for that setting). Returns its index.
    int addLane (Lane);
    // Move: a lane made to move a setting by itself, round and round in time like an LFO (a
    // filter that breathes every 2 bars, a delay mix that pulses, random steps on a pitch).
    // It's an ordinary Loop lane, so it's drawn, edited, switched off and saved like the rest.
    enum MoveShape { moveSine, moveTriangle, moveRampUp, moveRampDown, moveSquare, moveRandom, numMoveShapes };
    static juce::String moveShapeName (int);
    // `beats` for one time round, swinging `depth` (0..1) either side of `centre` (kept inside 0..1).
    static Lane makeMove (int tool, int parameter, int shape, double beats, float centre, float depth, int seed = 1);

    // Every lane of a tool (and its effects, if `fxTool` >= 0), e.g. to clear a track's moves.
    void clearTool (int tool, int fxTool = -1);
    // Where a lane is now (0..1 through it), -1 when it isn't playing: for drawing.
    float getLanePosition (int index) const;
    double getClockBeat() const { return lastBeat.load(); }
    // Whether a lane's setting is being moved by hand right now.
    bool isTouched (int index) const;

    // Shows.
    static constexpr const char* kTreeType = "Automation";
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);

    //==========================================================================
    // Audio thread, before the tools play this block (after learned knobs moved their settings).
    struct Clock
    {
        double beat = 0;
        double bpm = 120;
        int beatsPerBar = 4;
        bool playing = false;
        double sampleRate = 44100;
        int samples = 0;
    };
    void process (const Clock&);

private:
    std::atomic<int> version { 0 }, finishedLanes { -1 };
    struct Live
    {
        juce::AudioProcessorParameter* param = nullptr;
        int watch = -1;         // its entry in the recording's watch list
        float written = -1;     // what this lane last set (-1: nothing yet)
        float seen = -1;        // the setting's value after the last block
        int heldSamples = 0;    // being moved by hand: the lane waits
    };
    struct Watch
    {
        juce::AudioProcessorParameter* param = nullptr;
        int tool = 0, parameter = 0;
        float seen = 0;
        int slot = -1;          // its recording slot once it moves
        int lane = -1;          // the lane that plays it (it notices its own moves), or -1
    };
    struct Slot
    {
        int watch = -1;
        float before = 0;       // its value when recording started
        std::vector<Point> points;   // reserved; the audio thread only appends within capacity
        std::atomic<int> used { 0 };
        double lastBeat = -1;   // of the last point
        float lastValue = 0;    // the value seen last block
        double lastPos = 0;     // that block's place
    };
    void startSlot (int watchIndex, float before, double prevPos, double pos, float value);
    void recordInto (Slot&, double pos, float value);

    void snapshotForUndo();
    // Puts `newLanes` in (resolving their settings), keeping the recording's links to them.
    void install (std::vector<Lane> newLanes);
    void linkWatch();   // under the lock
    void finishRecording();

    mutable juce::SpinLock lock;
    std::vector<Lane> lanes;
    std::vector<Live> live;   // one per lane
    std::vector<std::vector<Lane>> undoStack;

    // Recording.
    std::atomic<int> recState { (int) RecState::off };
    std::atomic<bool> recOverSong { false };
    std::atomic<bool> closeRequested { false }, cancelRequested { false };
    std::atomic<int> numRecording { 0 };
    std::vector<Watch> watch;
    std::array<Slot, kMaxRecording> slots;
    int slotsUsed = 0;
    double recStart = 0, recStop = -1;
    std::atomic<bool> recFinished { false };
    std::atomic<double> lastBeat { 0 };
    double lastPlayingBeat = 0;   // the last block's beat while the clock ran
};
