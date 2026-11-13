#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "StripFx.h"

#include <array>
#include <atomic>

// The Rig's looper: eight layers of sound recorded live, in time with the Rig's clock.
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
    static constexpr int kLayers = 8;
    // Each layer starts with room for kStartSeconds; while it records, maintain() gives it more
    // in the background, up to kMaxRecordSeconds (so a long loop only takes memory when you make one).
    static constexpr double kStartSeconds = 32.0, kMaxRecordSeconds = 600.0;
    static constexpr double kGrowAheadSeconds = 12.0;   // grows when less than this is left

    enum class State { empty, armed, recording, playing, overdubbing };
    // line: the Input strip (a guitar, keytar... on your interface, through its effects). New
    // sources go at the end, so saved shows keep theirs.
    enum Source { input, vocoder, bass, synth, mpc, sampler, mix, line, drums, numSources };
    static juce::String sourceName (int);
    static const std::array<int, 6>& barChoices();   // 0 (until you press again), 1, 2, 4, 8, 16

    struct Layer
    {
        std::atomic<int> source { mix };
        std::atomic<int> bars { 0 };          // 0: record until pressed again
        std::atomic<float> level { 0.8f };
        std::atomic<bool> mute { false };

        // Read by the window.
        std::atomic<int> state { (int) State::empty };
        std::atomic<double> lengthBeats { 0 };
        std::atomic<double> position { 0 };   // 0..1 through the loop
        std::atomic<float> peak { 0 };
        std::atomic<int> version { 0 };       // changes when the sound does (for the waveform)

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
    };

    RigLooper();

    Layer& getLayer (int i) { return layers[(size_t) i]; }
    const Layer& getLayer (int i) const { return layers[(size_t) i]; }
    State getState (int i) const { return (State) layers[(size_t) i].state.load(); }

    // Any thread (the window, MIDI): record / stop / overdub, undo the last overdub, clear.
    void pressRecord (int layer) { ++layers[(size_t) layer].presses; }
    // Any thread: the loop starts again from its top at `beat` (a song part coming in).
    void restartAt (int layer, double beat) { layers[(size_t) layer].restartBeat = beat; }
    void undo (int layer) { ++layers[(size_t) layer].undos; }
    void clear (int layer) { ++layers[(size_t) layer].clears; }

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
    struct Trim
    {
        juce::AudioBuffer<float> whole;   // the loop before trimming
        double wholeBeats = 0;
        double start = 0, end = 0;        // the part that plays, in beats of `whole`
    };
    bool openTrim (int layer);            // false if the layer has no loop
    const Trim& getTrim (int layer) const { return trims[(size_t) layer]; }
    void setTrim (int layer, double startBeat, double endBeat, double playBeats);
    // The part of `audio` (which is `beats` long) between two beats, with short fades at the cuts.
    static juce::AudioBuffer<float> cropBeats (const juce::AudioBuffer<float>& audio, double beats, double from, double to);

    //==========================================================================
    // Audio thread.
    void prepare (double sampleRate, int maxBlock = 4096);
    // Before prepare: the effects each layer plays through (null for none). Not owned.
    void setEffects (int layer, StripFx* fx) { effects[(size_t) layer] = fx; }
    // The song timeline's level for a layer over the next block, from `from` to `to` (1 when
    // the timeline isn't playing it). Before its effects, so an echo rings out after a fade.
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

    double sampleRate = 44100.0;
    std::array<Layer, kLayers> layers;
    juce::SpinLock undoLock;              // the undo room, while maintain() makes it bigger
    juce::AudioBuffer<float> undoAudio;
    int undoLayer = -1, undoLength = 0;
    std::array<StripFx*, kLayers> effects {};
    std::array<float, kLayers> arrangeFrom { 1, 1, 1, 1, 1, 1, 1, 1 }, arrangeTo { 1, 1, 1, 1, 1, 1, 1, 1 };
    std::array<juce::AudioBuffer<float>, kLayers> layerOut;   // a layer's sound, before its effects
    std::array<int, kLayers> fxTail {};                        // samples left of an effect's tail
    std::array<Trim, kLayers> trims;
    std::array<int, kLayers> trimVersions {};   // the layer's version when its trim was set

    JUCE_DECLARE_NON_COPYABLE (RigLooper)
};
