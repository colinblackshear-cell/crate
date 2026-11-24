#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "StripFx.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>

// The Rig's looper: sixteen layers of sound recorded live, in time with the Rig's clock.
//
// Press record on a layer and it starts at the next bar (or right away if the clock was
// stopped: the clock starts with it). Press again and it stops at the end of that bar, so a
// loop is always a whole number of bars and stays locked to the beat. Pick a length first
// (1, 2, 4, 8, 16, 32 or 64 bars) and it stops by itself. Press record on a playing layer to overdub
// over it; Undo takes back the last overdub. Each layer records from one source: your audio
// input, one of the tools, or the whole Rig mix (never the loops themselves).
//
// All the loops share one timeline: when the clock starts, every loop starts from its top
// (bar 1) together, and a loop recorded while others play is turned round as it closes so its
// bar 1 lines up with theirs (it sounds exactly where it was played). So their playheads always
// move together, and Stop then Play never drops a loop in partway.
//
// Loops play by the beat, so they follow the tempo when it changes. A loop or a whole song
// from a file can be dropped on a layer, either stretched to the Rig's tempo or setting it.
//
// Each layer can have its own effects (a StripFx rack the engine owns): the loop plays
// through them, and what it records is never affected by them.
//
// The window and MIDI ask for things through atomics; the audio thread does them.
class RigLooper
{
public:
    static constexpr int kLayers = 16;
    static constexpr int kMaxBars = 64;   // the longest set length (Free goes on to kMaxRecordSeconds)
    // Each layer starts with room for kStartSeconds; while it records, maintain() gives it more
    // in the background, up to kMaxRecordSeconds (so a long loop only takes memory when you make one):
    // a take as long as a whole long set. All the layers together stop growing at
    // kTotalRecordSeconds, so memory stays bounded (about 1 GB per 45 minutes at 48 kHz). Undo of
    // an overdub keeps a copy of the loop, so only loops up to kMaxUndoSeconds have one.
    static constexpr double kStartSeconds = 32.0, kMaxRecordSeconds = 3600.0, kTotalRecordSeconds = 2.5 * 3600.0;
    static constexpr double kMaxUndoSeconds = 600.0;
    static constexpr double kGrowAheadSeconds = 12.0;   // grows when less than this is left
    static constexpr double kGrowStepSeconds = 300.0;   // grows by at most this at a time
    // Copying a playing loop (copyLayerAudio) is done by the audio thread, a slice each block:
    // kCopySpeed samples for each sample of the block (between kCopyMinSlice and kCopyMaxSlice),
    // in pages of kCopyPageSamples. A copy the loop's replacement interrupts is tried again, at
    // most kCopyRounds times. With no block for kStallFloorMs (or four of the longest recent gaps
    // between blocks, up to kStallCapMs) the audio is taken to have stopped.
    static constexpr int kCopyPageShift = 10, kCopyPageSamples = 1 << kCopyPageShift;
    static constexpr int kCopySpeed = 1024, kCopyMinSlice = 1 << 15, kCopyMaxSlice = 1 << 20, kCopyRounds = 3;
    static constexpr juce::uint32 kStallFloorMs = 1000, kStallCapMs = 5000;
    // The waveform each layer keeps for the window (see readPeaks): kPeakBins loudness bins, each
    // the loudest sample of 2^shift samples of the loop. A loop takes from half to all of them, so
    // at least 8192 for any loop: about one a pixel for the Timeline's widest (64 bars, zoomed in).
    static constexpr int kPeakBins = 16384;

    enum class State { empty, armed, recording, playing, overdubbing };
    // line: the Input strip (a guitar, keytar... on your interface, through its effects). New
    // sources go at the end, so saved shows keep theirs.
    enum Source { input, vocoder, bass, synth, mpc, sampler, mix, line, drums, drone, numSources };
    static juce::String sourceName (int);
    static const std::array<int, 8>& barChoices();   // 0 (until you press again), 1, 2, 4, 8, 16, 32, 64

    struct Layer
    {
        std::atomic<int> source { mix };
        std::atomic<int> bars { 0 };          // 0: record until pressed again
        // Bars to count in before it records (the bottom bar's Count-in): from stop it waits this
        // many bars with the click; while playing, 2 waits a bar more than the next bar line.
        std::atomic<int> countIn { 0 };
        std::atomic<float> level { 0.8f };
        std::atomic<bool> mute { false };

        // Read by the window.
        std::atomic<int> state { (int) State::empty };
        std::atomic<double> lengthBeats { 0 };
        std::atomic<double> position { 0 };   // 0..1 through the loop
        std::atomic<float> peak { 0 };
        std::atomic<int> version { 0 };       // changes when the sound does (for the waveform)
        // How the last recording on this layer ended, for the window to say (see takeEnding()).
        std::atomic<int> ending { 0 };
        // How it plays right now (a Bend: reverse, half or double speed, a stutter), any thread.
        // Only the playing changes: the loop itself, and what an overdub writes, stay as they are.
        std::atomic<int> bend { 0 };
        // The clock beat it starts recording on, while armed (-1 otherwise), for the window's
        // count-in and the click.
        std::atomic<double> startsAt { -1.0 };

    private:
        friend class RigLooper;
        std::atomic<int> presses { 0 }, clears { 0 }, undos { 0 };
        std::atomic<double> restartBeat { -1.0 };
        // Every loop counts its bars on one shared timeline, so their playheads move together.
        // A loop recorded while others played is turned round by this many beats (see
        // finishRecording) so its bar 1 falls on theirs: until maintain() has done it, the sound
        // plays unturned from startBeat, and its place on the shared timeline is
        // startBeat - syncShift. 0: none waiting.
        std::atomic<double> syncShift { 0.0 };
        bool restartPending = false;          // audio thread: the clock just started
        juce::SpinLock lock;                  // the audio, while the window swaps it
        juce::AudioBuffer<float> audio;       // capacity; the loop is the first `length` samples
        int length = 0;
        double startBeat = 0, stopBeat = -1;
        int writeIndex = 0;
        std::atomic<int> recorded { 0 };      // writeIndex, for the message thread
        float gain = 0.8f;
        // A loop waiting to take this one's place at swapBeat (a loop bank coming in). Under
        // `lock`; after the swap it holds the old sound until maintain() frees it.
        juce::AudioBuffer<float> swapAudio;
        double swapBeats = 0, swapBeat = -1;
        float swapLevel = 0.8f;
        bool swapMute = false;
        std::atomic<bool> swapPending { false };
        int swapSerial = 0;                   // under `lock`: the queued swap's serial
        // The last swap done: (serial << 32) | the version it left (-1 before any).
        std::atomic<juce::int64> swapApplied { (juce::int64) (juce::uint32) -1 };
        // The bend now and the one it's fading from: where in the loop (in beats) and on which
        // clock beat it took over, so it carries on from there without a jump.
        struct BendAt
        {
            int mode = 0;
            double from = 0, at = 0;
        };
        BendAt bendNow, bendBefore;
        int bendFade = 0;   // samples left of the fade from bendBefore

        // `length`, for a copier on another thread (set with it, under `lock`).
        std::atomic<int> publishedLength { 0 };
        // A copy of the loop being made by the audio thread (see copyLayerAudio). The copier
        // fills in where it goes, then posts it (`state`); the audio thread takes it at the start
        // of a block, and from then on only touches it under `lock`, until it says it's finished.
        struct Snapshot
        {
            std::atomic<int> state { 0 };
            // The copier's, until posted: its buffer (2 channels of `length`) and a flag a page.
            float* dest[2] {};
            juce::uint8* copied = nullptr;
            int length = 0, pages = 0;
            int version = -1;   // the layer's version when the copy started (the audio thread's)
            // Under `lock`, while the audio thread copies: the next page in order.
            bool active = false;
            int next = 0, credit = 0;   // credit: samples of copying owed, short of a page
        };
        Snapshot snap;

        // The loop's waveform, for the window: made once, never moved, so readPeaks can read it
        // at any time without the lock (only atomics). One writer at a time, always holding `lock`
        // (the audio thread as it records and overdubs, or whoever puts a new sound in). `seq` is
        // odd while the whole table is being rewritten, so a reader can tell it changed under it.
        struct PeakTable
        {
            PeakTable();
            std::unique_ptr<std::atomic<float>[]> bins;   // kPeakBins
            std::atomic<int> shift { 0 };                 // each bin is 2^shift samples
            std::atomic<int> covered { 0 };               // samples of the loop it holds
            std::atomic<juce::uint32> seq { 0 };
        };
        PeakTable peaks;
        // The waveform of swapAudio, made by queueLayerAudio, put in with it (under `lock`).
        std::unique_ptr<float[]> swapPeaks { new float[(size_t) kPeakBins]() };
        int swapPeakShift = 0, swapPeakCovered = 0;
    };

    // Ways a playing loop can be bent, live: backwards, at half speed (an octave down, like a
    // slowed tape), at double speed (an octave up), or a stutter that keeps playing the slice
    // it was in (a beat, an 8th or a 16th). Each takes over from where the loop is, and going
    // back to Straight drops it back on the beat.
    enum Bend { straight, reverse, half, twice, stutter4, stutter8, stutter16, numBends };
    static juce::String bendName (int);
    // Any thread.
    void setBend (int layer, int bend) { layers[(size_t) layer].bend = juce::jlimit (0, numBends - 1, bend); }
    int getBend (int layer) const { return layers[(size_t) layer].bend.load(); }

    RigLooper();

    Layer& getLayer (int i) { return layers[(size_t) i]; }
    const Layer& getLayer (int i) const { return layers[(size_t) i]; }
    State getState (int i) const { return (State) layers[(size_t) i].state.load(); }

    // How a layer's last recording ended: as asked (pressed again, or its bars were up), it ran
    // out of room (kMaxRecordSeconds, or all the layers together at kTotalRecordSeconds), the
    // clock stopped (it keeps what was played, to the end of that bar), or a loop bank or song
    // part tried to replace it while it recorded (it was kept and the switch skipped on this
    // layer). takeEnding() reads it and resets it to `asked`.
    enum class Ending { asked, full, clockStopped, kept };
    Ending takeEnding (int i) { return (Ending) layers[(size_t) i].ending.exchange (0); }
    Ending peekEnding (int i) const { return (Ending) layers[(size_t) i].ending.load(); }
    // Seconds a layer has recorded so far (while it records), for the window.
    double recordedSeconds (int i) const { return layers[(size_t) i].recorded.load() / sampleRate; }
    // The longest a recording can still go on this layer now, in seconds (the per-layer limit, or
    // what's left of the total), for the window.
    double roomLeftSeconds (int i) const;

    // Any thread (the window, MIDI): record / stop / overdub, undo the last overdub, clear.
    void pressRecord (int layer) { ++layers[(size_t) layer].presses; }
    // Any thread: the loop starts again from its top at `beat` (a song part coming in).
    void restartAt (int layer, double beat) { layers[(size_t) layer].restartBeat = beat; }
    void undo (int layer) { ++layers[(size_t) layer].undos; }
    void clear (int layer) { ++layers[(size_t) layer].clears; }
    // Message thread: record an empty layer from the next bar, or close one that's recording;
    // a layer that has its loop is left alone (recording on top is overdub only, on purpose).
    // Returns false when it did nothing for that reason.
    bool recordOrClose (int layer)
    {
        const auto st = getState (layer);
        if (st == State::playing || st == State::overdubbing)
            return false;
        pressRecord (layer);
        return true;
    }
    // Whether undo would take back an overdub on this layer (the last one only).
    bool canUndo (int layer) const { return undoLayer.load() == layer; }

    // Message thread: puts a sound on a layer, `beats` long, playing from beat 0. Returns the
    // layer's version that names this sound (-1 if it did nothing).
    int setLayerAudio (int layer, juce::AudioBuffer<float> audio, double beats);
    // Message thread: swaps a layer's loop for another (a loop bank coming in) at `beat`, from
    // its top, or right away if `beat` < 0. `audio` `beats` long, or empty (or `beats` 0) to leave
    // the layer empty. Its level and mute change with it. A newer swap replaces one not yet done.
    // Returns the swap's serial (never reused), which getAppliedSwap names once it's done.
    int queueLayerAudio (int layer, juce::AudioBuffer<float> audio, double beats, double beat, float level, bool mute);
    bool isSwapPending (int layer) const { return layers[(size_t) layer].swapPending.load(); }
    // Any thread: the last swap done on a layer (its serial, 0 for none) and the version it left,
    // read together: while the version is still that, the layer holds exactly the swapped-in sound.
    int getSwapVersion (int layer) const { return (int) (juce::uint32) layers[(size_t) layer].swapApplied.load (std::memory_order_acquire); }
    int getAppliedSwap (int layer, int& version) const
    {
        const auto v = layers[(size_t) layer].swapApplied.load (std::memory_order_acquire);
        version = (int) (juce::uint32) v;
        return (int) (v >> 32);
    }
    // Message thread: forgets a swap that hasn't happened yet.
    void cancelSwap (int layer);
    // Message thread: new sound for a playing loop, the same length (quantizing it): it keeps its
    // place in the bar, level and effects. False (and nothing changes) while it records or
    // overdubs, or if the length differs.
    bool replaceLayerAudio (int layer, juce::AudioBuffer<float> audio);
    // Message thread, or a plugin host's thread: a copy of a layer's loop exactly as it was at
    // one moment (empty if it has none, or while it records), made while it keeps playing.
    //
    // The copy is the loop as it is at the start of the audio thread's next block for that
    // layer (after that block's requests: a clear or an undo asked for before then is in it).
    // The copier sets aside the room here; the audio thread fills it, a slice each block, and
    // before an overdub writes over a page not yet copied, copies that page first. So nothing
    // reads the loop's samples while the audio thread may write them, and the audio thread never
    // allocates, frees or waits. If the loop is replaced (a new loop, a bank swap, a clear, an
    // undo) before the copy is done, it starts again, up to kCopyRounds times, then copies under
    // the lock. With no audio running (or the copier being the thread that runs it), it copies
    // under the lock at once; if the audio stops partway, it finishes under the lock.
    juce::AudioBuffer<float> copyLayerAudio (int layer) const;
    // The same, and the layer's version at the moment the copy is of (-1 when it's empty), so
    // (layer, version) names exactly the sound copied.
    juce::AudioBuffer<float> copyLayerAudio (int layer, int* versionOfCopy) const;
    // How copies were made, for tests and benchmarks.
    struct CopyStats
    {
        int byAudioThread = 0, pagesCopiedBeforeOverdub = 0, retries = 0, underLock = 0, finishedUnderLock = 0;
    };
    CopyStats getCopyStats() const;

    // What the audio thread copies, for tests and benchmarks: pages (kCopyPageSamples of both
    // channels) and bytes, by kind, in total and per block. Mandatory copies are made whatever
    // the budget (before an overdub writes a page, before a stale page is read); discretionary
    // ones are the copies made in order, a slice each block. wholeLoop is a copy of a whole loop
    // in one block (the old overdub start and undo), which nothing should ever make.
    enum CopyKind { undoBeforeWrite, undoBeforeRead, saveBeforeWrite, undoInOrder, saveInOrder, wholeLoop, numCopyKinds };
    static bool isMandatoryCopy (int kind) { return kind != undoInOrder && kind != saveInOrder; }
    struct CopyCounters
    {
        std::array<juce::int64, numCopyKinds> pages {}, bytes {};   // in total
        std::array<int, numCopyKinds> maxPages {};                   // the most in one block
        std::array<juce::int64, numCopyKinds> maxBytes {};
        int maxMandatory = 0, maxDiscretionary = 0, maxTotal = 0;    // pages in one block
        int maxLooks = 0;                                            // page marks looked at in one block
        juce::int64 blocks = 0;
        juce::int64 skippedLayerBlocks = 0;   // a layer's block not played (its lock was busy)
        // Blocks whose copying went over the budget's bounds: the most a block may copy, and
        // the most it may copy that it must (see the design of the copy budget).
        juce::int64 overBudgetBlocks = 0, overMandatoryBlocks = 0, overLookBlocks = 0;
        double mostCredit = 0, deepestDebt = 0;   // pages: the most ever available, the most owed
        int adoptions = 0, swaps = 0, restorations = 0;
        int roomBuffersMost = 0;   // undo-room-sized buffers alive at once, at the most
    };
    CopyCounters getCopyCounters() const;
    void resetCopyCounters();
    // Undo-room-sized buffers alive now (the room, a spare waiting, one being made or freed).
    int roomBuffersNow() const { return roomBuffers.load(); }
    // The undo room's usable length in samples (as the audio thread last published it), and
    // whether a bigger one is waiting to be taken. Any thread.
    int undoRoomSamples() const { return roomUsableSamples.load(); }
    bool isSpareRoomReady() const { return spareReady.load(); }
    // Message thread (or one loading a show): makes the undo room ready for a loop `samples`
    // long ahead of need, as a spare the audio thread takes over. maintain() does this for
    // what's playing and recording; putting in a sound does it for that sound first.
    void prepareUndoRoom (int samples) { prepareRoom (samples); }
    // Tests only: the loop as it sounds now (exactly what a copy would be), read under the lock
    // without changing anything (a copy may finish work first).
    juce::AudioBuffer<float> soundingLoopForTests (int layer) const;
    // Whether undo is made a slice at a time (false: a whole copy in the block of the press).
    static constexpr bool kIncrementalUndo = false;
    // Tests only: called once while the audio thread copies (`underLock` false), and while a
    // copy under the lock holds it (true); always copy under the lock (as before); and a slice
    // size (samples a block) instead of kCopySpeed, to make a copy slow.
    static inline std::function<void (int layer, bool underLock)> copyPauseForTests;
    static inline std::atomic<bool> copyUnderLockForTests { false };
    static inline std::atomic<int> copySliceForTests { 0 };
    // Message thread: the loop's loudness in `count` slices, for drawing it (also while it
    // records): each the loudest sample in that slice (up to 1), to the nearest bin of the
    // layer's waveform. Reads only that waveform, never the loop, so it needs no lock.
    std::vector<float> readPeaks (int layer, int count) const;
    // Tests only: the most bins of the waveforms any one layer's block has touched since the
    // last call (it starts again from 0). Never more than kMaxPeakOpsPerBlock.
    int takePeakOpsForTests() { return statPeakOps.exchange (0); }
    // A block's waveform work on a layer, however long the loop: a bin per sample it writes, and
    // at most a few whole-table steps (a clear, an undo, an overdub starting, a swap, a fold).
    static constexpr int kMaxPeakOpsPerBlock = 4096 + 8 * kPeakBins;
    // Message thread: empties a layer now, dropping any presses not yet handled (loading a show).
    void reset (int layer);
    // Message thread: gives an empty layer room to record again after holding a long file.
    void makeRoom (int layer);
    // Message thread, often (the engine's timer does it ~10 times a second): gives a layer that's
    // recording more room before it fills up, and the undo room for the longest loop.
    void maintain();
    int getCapacity (int layer) const { return layers[(size_t) layer].audio.getNumSamples(); }

    // Trimming, on the message thread. The editor always works on the loop as it was before
    // you trimmed it, so you can change your mind: openTrim keeps a copy of the whole loop (or
    // keeps the copy it has, if the loop hasn't changed since the last trim), and setTrim plays
    // the part between two of its beats, stretched to `playBeats` so it stays on the beat.
    // Recording, overdubbing, clearing or loading a new sound on the layer starts afresh.
    // The shape is applied to the whole loop each time (reverse, then slip and nudge round the
    // loop, then the crop, then level and fades), so every step can be changed or undone.
    struct Shape
    {
        double slipBeats = 0;                 // moves the audio later inside the loop (wraps)
        double nudgeMs = 0;                   // the same, in milliseconds, for timing fixes
        float gainDb = 0;
        double fadeInBeats = 0, fadeOutBeats = 0;   // in beats of the part that plays
        bool reversed = false;

        bool isDefault() const { return slipBeats == 0 && nudgeMs == 0 && gainDb == 0 && fadeInBeats == 0 && fadeOutBeats == 0 && ! reversed; }
    };
    struct Trim
    {
        juce::AudioBuffer<float> whole;   // the loop before trimming
        double wholeBeats = 0;
        double start = 0, end = 0;        // the part that plays, in beats of `whole`
        double playBeats = 0;             // how many beats that part plays for
        Shape shape;
    };
    bool openTrim (int layer);            // false if the layer has no loop
    const Trim& getTrim (int layer) const { return trims[(size_t) layer]; }
    void setTrim (int layer, double startBeat, double endBeat, double playBeats);
    // Changes the shape and plays it with the current start, end and length.
    void setShape (int layer, const Shape& shape);
    // The part of `audio` (which is `beats` long) between two beats, with short fades at the cuts.
    static juce::AudioBuffer<float> cropBeats (const juce::AudioBuffer<float>& audio, double beats, double from, double to);

    //==========================================================================
    // Audio thread.
    void prepare (double sampleRate, int maxBlock = 4096);
    // Before prepare: the effects each layer plays through (null for none). Not owned.
    void setEffects (int layer, StripFx* fx) { effects[(size_t) layer] = fx; }
    // The song timeline's level for a layer over the next block, from `from` to `to` (1 when
    // the timeline isn't playing it). Before its effects, so an echo rings out after a fade.
    // How late a live source (the mic, the vocoder, the line in) comes in, in samples: the
    // interface's round trip, so what you sing in time with what you hear lands on the beat.
    // A loop recorded from one is moved this much earlier as it closes, and an overdub is
    // written this much earlier.
    void setLiveDelay (int samples) { liveDelay = juce::jmax (0, samples); }
    int getLiveDelay() const { return liveDelay.load(); }
    static bool isLiveSource (int source) { return source == input || source == vocoder || source == line; }
    void setArrangeGain (int layer, float from, float to) { arrangeFrom[(size_t) layer] = from; arrangeTo[(size_t) layer] = to; }

    struct Clock
    {
        double beat = 0;          // at the start of the block
        double bpm = 120;
        int beatsPerBar = 4;
        bool playing = false;
    };
    // Records from `sources` (each stereo, post-fader; nullptr for none) and plays every layer
    // into `out` (which is cleared first). Returns true if the clock should start (a layer was
    // armed while it was stopped).
    bool process (const Clock&, const std::array<const juce::AudioBuffer<float>*, numSources>& sources,
                  juce::AudioBuffer<float>& out);

private:
    void handleRequests (Layer&, int index, const Clock&, bool& wantsPlay, int& peakOps);

    // The copy (copyLayerAudio). Snapshot states:
    enum SnapState { snapIdle, snapPosted, snapActive, snapDone, snapAborted, snapMismatch, snapNothing };
    enum class Waited { done, finishedUnderLock, aborted, nothing, audioStopped };
    // Audio thread, under the layer's lock: take a posted copy; copy the next slice; copy a page
    // before an overdub writes over it.
    void startSnapshot (Layer&);
    void continueSnapshot (Layer&, int blockSize);
    void copyBeforeWrite (Layer& layer, int sample)
    {
        if (layer.snap.active && layer.snap.copied[sample >> kCopyPageShift] == 0)
        {
            const int page = sample >> kCopyPageShift;
            copyPage (layer, page, 2);
            noteCopy (saveBeforeWrite, juce::jmin (kCopyPageSamples, layer.snap.length - (page << kCopyPageShift)));
        }
    }
    // Copies a page of the loop into the copy, and marks it (1 in order, 2 before an overdub).
    static void copyPage (Layer&, int page, juce::uint8 mark);
    // Any thread, under the layer's lock: the loop is about to change, so a copy in progress is
    // called off (the copier tries again).
    static void abortSnapshot (Layer&);
    Waited waitForSnapshot (Layer&, int index) const;
    juce::AudioBuffer<float> copyUnderLock (Layer&, int index, int* versionOfCopy = nullptr) const;
    // Whether an audio thread (not this one) is running blocks, and how long a pause in them
    // means it has stopped.
    bool audioRunningElsewhere() const;
    juce::uint32 stallLimitMs() const;

    // The waveform (Layer::peaks); each returns the bins it touched. scanPeaks (any thread, on a
    // buffer it owns) works out the table for a whole sound, and returns its shift.
    static int scanPeaks (const juce::AudioBuffer<float>&, int length, float* bins);
    static int installPeaks (Layer&, const float* bins, int shift, int covered);   // under the lock
    static int clearPeaks (Layer&);                                                 // under the lock
    static int coverPeaks (Layer&, int length);   // audio thread: makes the table hold `length` samples

    int finishRecording (Layer&);   // returns the waveform bins it touched
    // Where a layer's bar 1 is on the shared timeline.
    static double syncAnchor (const Layer& l) { return l.startBeat - l.syncShift.load (std::memory_order_relaxed); }
    // Message thread (maintain): turns round a loop that's waiting to be put on the shared bars.
    void applySyncShift (int index);
    int applySwap (Layer&, int index);   // under the layer's lock; returns the waveform bins it touched
    // Where a bend plays from (beats into the loop), given where straight would be.
    static double bentPosition (const Layer::BendAt&, double straightPos, double beat, double lengthBeats, int beatsPerBar);

    double sampleRate = 44100.0;
    bool wasPlaying = false;              // audio thread: the clock, last block
    std::array<Layer, kLayers> layers;
    // The undo room: one buffer for all the layers, as long as the longest loop (up to
    // kMaxUndoSeconds), and a mark for each of its pages. Only the audio thread changes which
    // buffer it is, or touches it, while audio runs (a copier holding the undo layer's lock may
    // too, when the audio has stopped). A bigger one is made, and written through so its memory
    // is really there, on the message thread, and handed over in the spare slot: the audio
    // thread takes it at a block with no undo held (or at a press that needs it) by exchanging
    // the two, and leaves the old one there for the message thread to free. `undoLock` is held
    // only to move the spare slot's buffers.
    struct Room
    {
        juce::AudioBuffer<float> audio;
        std::unique_ptr<juce::uint32[]> stamps;   // a mark a page
        int stampPages = 0;
        int usable() const { return audio.getNumChannels() < 2 ? 0 : juce::jmin (audio.getNumSamples(), stampPages << kCopyPageShift); }
    };
    static void swapRooms (Room& a, Room& b)
    {
        std::swap (a.audio, b.audio);
        std::swap (a.stamps, b.stamps);
        std::swap (a.stampPages, b.stampPages);
    }
    Room room, spare;                          // spare: under undoLock
    std::atomic<bool> spareReady { false };    // the spare is a new room to take (else one to free)
    std::atomic<int> roomUsableSamples { 0 }; // the room's usable length, published by the audio thread
    juce::SpinLock undoLock;
    juce::CriticalSection roomMaking;          // the threads that make and free rooms, one at a time
    // Off the audio thread: frees a room the audio thread has let go of, and makes one at least
    // `samples` long (up to kMaxUndoSeconds) if the room isn't, unless the slot is taken.
    void prepareRoom (int samples);
    static void fillRoom (Room&, int samples);   // off the audio thread: a new room, written through
    // Audio thread: takes the spare room if there is one (one try of undoLock a block).
    bool adoptSpare();
    bool adoptTried = false;                   // audio thread: this block
    std::atomic<int> undoLayer { -1 };
    std::atomic<int> swapSerials { 0 };   // queueLayerAudio's serials
    int undoLength = 0;
    // The undo loop's waveform (audio thread only): kept as an overdub starts, put back by undo.
    std::unique_ptr<float[]> undoPeaks { new float[kPeakBins]() };
    int undoPeakShift = 0, undoPeakCovered = 0;
    std::atomic<int> statPeakOps { 0 };
    std::array<StripFx*, kLayers> effects {};
    std::atomic<int> liveDelay { 0 };
    std::array<float, kLayers> arrangeFrom = filled (1.0f), arrangeTo = filled (1.0f);
    static std::array<float, kLayers> filled (float v) { std::array<float, kLayers> a; a.fill (v); return a; }
    std::array<juce::AudioBuffer<float>, kLayers> layerOut;   // a layer's sound, before its effects
    std::array<int, kLayers> fxTail {};                        // samples left of an effect's tail
    std::array<Trim, kLayers> trims;
    std::array<int, kLayers> trimVersions {};   // the layer's version when its trim was set

    // Copies are made one at a time (copyLayerAudio).
    mutable juce::CriticalSection copyMutex;
    // The audio thread's beat: blocks done, when the last ended, the longest recent gap between
    // two (x256, fading), and which thread runs them.
    std::atomic<juce::uint32> blocksDone { 0 }, lastBlockMs { 0 }, longestGap256 { 0 };
    std::atomic<juce::Thread::ThreadID> audioThread { nullptr };
    mutable std::atomic<int> statByAudio { 0 }, statBeforeOverdub { 0 }, statRetries { 0 }, statUnderLock { 0 }, statFinishedUnderLock { 0 };

    // The copy counters (getCopyCounters). The block's own tallies are the audio thread's; it
    // adds them to the atomics as the block ends.
    struct BlockTally
    {
        std::array<int, numCopyKinds> pages {};
        std::array<juce::int64, numCopyKinds> bytes {};
        int looks = 0;
    };
    BlockTally tally;
    void noteCopy (int kind, int samples)
    {
        ++tally.pages[(size_t) kind];
        tally.bytes[(size_t) kind] += (juce::int64) samples * 2 * (juce::int64) sizeof (float);
    }
    void noteWholeCopy (int samples)
    {
        tally.pages[(size_t) wholeLoop] += (samples + kCopyPageSamples - 1) / kCopyPageSamples;
        tally.bytes[(size_t) wholeLoop] += (juce::int64) samples * 2 * (juce::int64) sizeof (float);
    }
    void publishTally (int blockSize, double creditNow);
    struct Counters
    {
        std::array<std::atomic<juce::int64>, numCopyKinds> pages {}, bytes {}, maxBytes {};
        std::array<std::atomic<int>, numCopyKinds> maxPages {};
        std::atomic<int> maxMandatory { 0 }, maxDiscretionary { 0 }, maxTotal { 0 }, maxLooks { 0 };
        std::atomic<juce::int64> blocks { 0 }, skipped { 0 }, overBudget { 0 }, overMandatory { 0 }, overLooks { 0 };
        std::atomic<double> mostCredit { 0 }, deepestDebt { 0 };
        std::atomic<int> adoptions { 0 }, swaps { 0 }, restorations { 0 }, roomBuffersMost { 1 };
    };
    Counters counters;
    std::atomic<int> roomBuffers { 1 };   // undo-room-sized buffers alive (see roomBuffersNow)
    void roomBufferMade() { const int now = ++roomBuffers; if (now > counters.roomBuffersMost.load()) counters.roomBuffersMost = now; }
    void roomBufferFreed() { --roomBuffers; }

    JUCE_DECLARE_NON_COPYABLE (RigLooper)
};
