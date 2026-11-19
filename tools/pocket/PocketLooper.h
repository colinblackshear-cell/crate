#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "BeatsEngine.h"
#include "PocketAudio.h"

#include <array>
#include <atomic>
#include <memory>

// The Loops tab's looper: eight mono layers recorded live, in time with the shared clock (the one
// inside BeatsEngine). One renderer for the whole phone: it renders the Beats engine, plays the layers
// on top and records the picked source. Written compactly for the phone; the Mac's RigLooper is tied
// to the Rig's effects racks and transport, so it isn't used here.
//
// A layer records from the next bar line and stops at its length, so every loop is a whole number of
// bars. Playback is locked to the clock's beat (the loop's place is read from the beat position), so
// loops stay in sync with the drums and follow tempo changes. The screens talk to it from the message
// thread through a lock-free queue; the audio thread never allocates.
namespace pocket
{
constexpr int kLoopLayers = 8;

enum class LoopSource { mic, drums, vocoder, mix, count };
enum class LoopState { empty, waiting, recording, playing, overdubWait, overdubbing };

// One layer's recorded sound. The audio thread writes `data` while the layer records or overdubs.
struct LoopTake
{
    std::vector<float> data;               // capacity; the loop is the first `length` samples
    std::atomic<int> length { 0 };
    std::atomic<double> lengthBeats { 0 };
    double sampleRate = 44100.0;
    int source = 0;
};
using SharedTake = std::shared_ptr<LoopTake>;

class LoopsEngine : public PocketAudio::Renderer
{
public:
    explicit LoopsEngine (BeatsEngine&);

    //==========================================================================
    // Message thread
    // The vocoder's hookup: call this once with whatever renders the vocoder and the VOCODER source
    // works. Not owned. The vocoder always runs, so the VOCODER source can record it; it only reaches the
    // speaker (and the MIX source) while `monitor` is true (HEAR ME). nullptr = always heard.
    void setVocoderSource (PocketAudio::Renderer* r, const std::atomic<bool>* monitor = nullptr) { vocoder = r; vocoderMonitor = monitor; }
    bool hasVocoder() const { return vocoder != nullptr; }
    void setCountIn (bool on) { countIn = on; }

    // Arms an empty layer: it starts at the next bar (or after a one-bar count-in when the clock is
    // stopped and the count-in is on) and runs `bars` bars (0 = until stop()).
    bool arm (int layer, LoopSource, int bars);
    // Recording: ends at the end of the bar. Waiting for the next bar: cancels.
    void stop (int layer);
    // A playing layer records on top of itself from the next loop.
    bool dub (int layer);
    void setMute (int layer, bool);
    bool isMuted (int layer) const { return layers[(size_t) layer].mute.load(); }
    void clear (int layer);
    // Puts a loop on a layer (restoring a saved one, or undoing). nullptr empties it.
    void setLayer (int layer, SharedTake, bool mute);
    SharedTake takeOf (int layer) const { return takes[(size_t) layer]; }
    // A copy of the layer's loop, to put back with setLayer later (overdub's undo).
    SharedTake copyOf (int layer) const;
    static SharedTake makeTake (int samples, double sampleRate);

    // The clock, for loops that start it themselves: running until stopped here.
    void setTransport (bool on);
    bool transportRunning() const { return beats.clockRunning(); }

    LoopState state (int layer) const { return (LoopState) layers[(size_t) layer].uiState.load(); }
    bool settling (int layer) const { return layers[(size_t) layer].pending.load() > 0; }   // a command the audio hasn't handled yet
    float position (int layer) const { return layers[(size_t) layer].uiPos.load(); }   // 0..1 through the loop
    int version (int layer) const { return layers[(size_t) layer].version.load(); }
    double startBeat (int layer) const { return layers[(size_t) layer].uiStart.load(); }
    int recordingBars (int layer) const { return layers[(size_t) layer].uiBars.load(); }
    int recordedSamples (int layer) const { return layers[(size_t) layer].uiRecorded.load(); }
    double beatNow() const { return beats.clockBeat(); }
    double bpm() const { return beats.getBpm(); }
    bool countingIn() const { for (auto& l : layers) if (l.uiCounting.load()) return true; return false; }
    // Message thread, often: lets go of sounds the audio thread no longer uses.
    void maintain();
    // The longest-running layer's length in beats (0 = nothing recorded), for FREE and the display.
    double masterBeats() const;
    // The layer's loudness in `count` slices, for drawing it (also while it records).
    std::vector<float> peaks (int layer, int count) const;
    // The mic's loudness since the last call, 0..1.
    float takeInputLevel() { return inputPeak.exchange (0.0f); }

    // Raw access to the engine's block for tests: gives the mic channels the next render() sees.
    void setInput (const float* const* in, int numIn) override { inPtr = in; inChannels = numIn; }
    void setLatency (int samples) override { latency = juce::jmax (0, samples); }

    //==========================================================================
    // Audio thread
    void prepare (double sampleRate) override;
    void render (float* left, float* right, int numSamples) override;

private:
    static constexpr int kChunk = 256;

    struct Cmd { enum Kind { arm, stop, dub, clear, set } kind = arm; int layer = 0, a = 0, b = 0, c = 0; LoopTake* take = nullptr; };

    struct Layer
    {
        // read by the screen
        std::atomic<int> uiState { (int) LoopState::empty };
        std::atomic<float> uiPos { 0.0f };
        std::atomic<int> uiRecorded { 0 };
        std::atomic<int> version { 0 };
        std::atomic<bool> mute { false };
        std::atomic<int> pending { 0 };   // commands pushed and not yet handled
        std::atomic<bool> uiCounting { false };
        std::atomic<double> uiStart { 0.0 };   // beat the loop (or the recording) started on
        std::atomic<int> uiBars { 0 };         // the recording's length in bars, 0 = until stopped
        // the audio thread's
        LoopTake* take = nullptr;
        LoopState st = LoopState::empty;
        LoopSource source = LoopSource::mic;
        int bars = 0, writeIdx = 0;
        double armBeat = -1, startBeat = 0, lenBeats = 0, endBeat = -1, dubBeat = 0;
        bool fresh = false, withCountIn = false;   // armed while the clock was stopped
        float gain = 0.0f;
    };

    void push (const Cmd&);
    void handle (const Cmd&);
    void setState (Layer&, LoopState);
    void chunk (float* left, float* right, int n);
    void playLayer (Layer&, int index, double blockBeat, double bps, int n);
    void recordLayer (Layer&, int index, double blockBeat, double bps, int n);
    void finishRecording (Layer&, double endBeat);
    void cancelRecording (Layer&);
    void release (Layer&);

    BeatsEngine& beats;
    PocketAudio::Renderer* vocoder = nullptr;
    const std::atomic<bool>* vocoderMonitor = nullptr;
    bool vocoderHeard = true;
    std::array<Layer, kLoopLayers> layers;
    std::array<SharedTake, kLoopLayers> takes;                          // the message thread's
    std::vector<std::pair<SharedTake, juce::uint32>> retired;           // kept a while after the audio thread lets go
    bool countIn = true;

    juce::AbstractFifo fifo { 128 };
    std::array<Cmd, 128> cmds;
    juce::SpinLock pushLock;

    const float* const* inPtr = nullptr;
    int inChannels = 0;
    std::atomic<int> latency { 0 };
    double sampleRate = 44100.0;
    std::atomic<bool> transportOn { false };
    std::atomic<float> inputPeak { 0.0f };

    // audio thread state
    bool wasRunning = false;
    double nextBeat = 0;
    int chunkOffset = 0;
    std::array<std::vector<float>, kLoopLayers> layerOut;
    std::vector<float> srcMic, srcDrums, srcVoc, srcMix, vocL, vocR;
};
}
