#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

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
// The window and MIDI ask for things through atomics; the audio thread does them.
class RigLooper
{
public:
    static constexpr int kLayers = 8;
    static constexpr double kMaxRecordSeconds = 32.0;

    enum class State { empty, armed, recording, playing, overdubbing };
    enum Source { input, vocoder, bass, synth, mpc, sampler, mix, numSources };
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
        juce::SpinLock lock;                  // the audio, while the window swaps it
        juce::AudioBuffer<float> audio;       // capacity; the loop is the first `length` samples
        int length = 0;
        double startBeat = 0, stopBeat = -1;
        int writeIndex = 0;
        float gain = 0.8f;
    };

    RigLooper();

    Layer& getLayer (int i) { return layers[(size_t) i]; }
    const Layer& getLayer (int i) const { return layers[(size_t) i]; }
    State getState (int i) const { return (State) layers[(size_t) i].state.load(); }

    // Any thread (the window, MIDI): record / stop / overdub, undo the last overdub, clear.
    void pressRecord (int layer) { ++layers[(size_t) layer].presses; }
    void undo (int layer) { ++layers[(size_t) layer].undos; }
    void clear (int layer) { ++layers[(size_t) layer].clears; }

    // Message thread: puts a sound on a layer, `beats` long, playing from beat 0.
    void setLayerAudio (int layer, juce::AudioBuffer<float> audio, double beats);
    // Message thread: a copy of a layer's loop (empty if it has none).
    juce::AudioBuffer<float> copyLayerAudio (int layer) const;
    // Message thread: the loop's loudness in `count` slices, for drawing it (also while it
    // records). Reads without locking: the audio thread only ever changes the samples.
    std::vector<float> readPeaks (int layer, int count) const;
    // Message thread: empties a layer now, dropping any presses not yet handled (loading a show).
    void reset (int layer);
    // Message thread: gives an empty layer room to record again after holding a long file.
    void makeRoom (int layer);

    //==========================================================================
    // Audio thread.
    void prepare (double sampleRate);

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

    double sampleRate = 44100.0;
    std::array<Layer, kLayers> layers;
    juce::AudioBuffer<float> undoAudio;
    int undoLayer = -1, undoLength = 0;

    JUCE_DECLARE_NON_COPYABLE (RigLooper)
};
