#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "StripFx.h"

#include <array>
#include <atomic>

// The Rig's looper: sixteen layers of sound recorded live, in time with the Rig's clock.
//
// Press record on a layer and it starts at the next bar (or right away if the clock was
// stopped: the clock starts with it). Press again and it stops at the end of that bar, so a
// loop is always a whole number of bars and stays locked to the beat. Pick a length first
// (1, 2, 4, 8 or 16 bars) and it stops by itself. Press record on a playing layer to overdub
// over it; Undo takes back the last overdub. Each layer records from one source: your audio
// input, one of the tools, or the whole Rig mix (never the loops themselves).
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
    // Each layer starts with room for kStartSeconds; while it records, maintain() gives it more
    // in the background, up to kMaxRecordSeconds (so a long loop only takes memory when you make one):
    // a take as long as a whole long set. All the layers together stop growing at
    // kTotalRecordSeconds, so memory stays bounded (about 1 GB per 45 minutes at 48 kHz). Undo of
    // an overdub keeps a copy of the loop, so only loops up to kMaxUndoSeconds have one.
    static constexpr double kStartSeconds = 32.0, kMaxRecordSeconds = 3600.0, kTotalRecordSeconds = 2.5 * 3600.0;
    static constexpr double kMaxUndoSeconds = 600.0;
    static constexpr double kGrowAheadSeconds = 12.0;   // grows when less than this is left
    static constexpr double kGrowStepSeconds = 300.0;   // grows by at most this at a time

    enum class State { empty, armed, recording, playing, overdubbing };
    // line: the Input strip (a guitar, keytar... on your interface, through its effects). New
    // sources go at the end, so saved shows keep theirs.
    enum Source { input, vocoder, bass, synth, mpc, sampler, mix, line, drums, drone, numSources };
    static juce::String sourceName (int);
    static const std::array<int, 6>& barChoices();   // 0 (until you press again), 1, 2, 4, 8, 16

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
        // The bend now and the one it's fading from: where in the loop (in beats) and on which
        // clock beat it took over, so it carries on from there without a jump.
        struct BendAt
        {
            int mode = 0;
            double from = 0, at = 0;
        };
        BendAt bendNow, bendBefore;
        int bendFade = 0;   // samples left of the fade from bendBefore
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

    // Message thread: puts a sound on a layer, `beats` long, playing from beat 0.
    void setLayerAudio (int layer, juce::AudioBuffer<float> audio, double beats);
    // Message thread: swaps a layer's loop for another (a loop bank coming in) at `beat`, from
    // its top, or right away if `beat` < 0. `audio` `beats` long, or empty (or `beats` 0) to leave
    // the layer empty. Its level and mute change with it. A newer swap replaces one not yet done.
    void queueLayerAudio (int layer, juce::AudioBuffer<float> audio, double beats, double beat, float level, bool mute);
    bool isSwapPending (int layer) const { return layers[(size_t) layer].swapPending.load(); }
    // Message thread: forgets a swap that hasn't happened yet.
    void cancelSwap (int layer);
    // Message thread: new sound for a playing loop, the same length (quantizing it): it keeps its
    // place in the bar, level and effects. False (and nothing changes) while it records or
    // overdubs, or if the length differs.
    bool replaceLayerAudio (int layer, juce::AudioBuffer<float> audio);
    // Message thread: a copy of a layer's loop (empty if it has none).
    juce::AudioBuffer<float> copyLayerAudio (int layer) const;
    // Message thread: the loop's loudness in `count` slices, for drawing it (also while it
    // records). Reads without locking: the audio thread only ever changes the samples.
    std::vector<float> readPeaks (int layer, int count) const;
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
    void handleRequests (Layer&, int index, const Clock&, bool& wantsPlay);
    void finishRecording (Layer&);
    void applySwap (Layer&, int index);   // under the layer's lock
    // Where a bend plays from (beats into the loop), given where straight would be.
    static double bentPosition (const Layer::BendAt&, double straightPos, double beat, double lengthBeats, int beatsPerBar);

    double sampleRate = 44100.0;
    std::array<Layer, kLayers> layers;
    juce::SpinLock undoLock;              // the undo room, while maintain() makes it bigger
    juce::AudioBuffer<float> undoAudio;
    std::atomic<int> undoLayer { -1 };
    int undoLength = 0;
    std::array<StripFx*, kLayers> effects {};
    std::atomic<int> liveDelay { 0 };
    std::array<float, kLayers> arrangeFrom = filled (1.0f), arrangeTo = filled (1.0f);
    static std::array<float, kLayers> filled (float v) { std::array<float, kLayers> a; a.fill (v); return a; }
    std::array<juce::AudioBuffer<float>, kLayers> layerOut;   // a layer's sound, before its effects
    std::array<int, kLayers> fxTail {};                        // samples left of an effect's tail
    std::array<Trim, kLayers> trims;
    std::array<int, kLayers> trimVersions {};   // the layer's version when its trim was set

    JUCE_DECLARE_NON_COPYABLE (RigLooper)
};
