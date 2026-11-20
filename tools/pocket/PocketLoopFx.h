#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

// The effects on each loop layer (the loop editor's five knobs) and their recorded moves.
// FxDsp runs one layer's filter, drive and volume and feeds its delay and reverb sends; FxBus is the
// delay and reverb those sends go to. The looper runs them live; renderLayer() runs the same code
// offline to print a layer with its effects (for TO MAC).
namespace pocket
{
enum FxKnob { fxVolume, fxFilter, fxDelay, fxReverb, fxDrive, kFxKnobs };
constexpr int kMoveBins = 512;   // a recorded move: one value per 1/512 of the loop

const char* fxKnobName (int k);                  // "VOLUME"
float fxDefault (int k);                         // where each knob starts: volume 0.75 (0 dB), filter 0.5 (off), the rest 0
juce::String fxValueText (int k, float v);       // "0 dB", "LP 1.2k", "35%"

// Everything about one layer's effects, to save, undo or print.
struct LayerFxState
{
    std::array<float, kFxKnobs> knob {};
    std::array<std::vector<float>, kFxKnobs> moves;   // empty = no moves for that knob, else kMoveBins values
    LayerFxState() { for (int k = 0; k < kFxKnobs; ++k) knob[(size_t) k] = fxDefault (k); }
    bool hasMoves() const { for (auto& m : moves) if (! m.empty()) return true; return false; }
    bool isNeutral() const;
    float valueAt (int k, double frac) const;          // the knob at a place in the loop (0..1)
    // When the loop is edited, its moves follow: the part [from, to) of the loop becomes the whole, `times`
    // over, backwards if asked.
    void remap (double from, double to, int times, bool reversed);
};

class FxDsp
{
public:
    void reset() { ic1 = ic2 = 0; vol = 1; drive = 0; dly = 0; rev = 0; filterOn = false; }
    // In place on `x`; adds the sends to `toDelay` and `toReverb`. `values` are the knobs for this block;
    // volume, drive and the sends glide to them across the block.
    void process (float* x, float* toDelay, float* toReverb, int n, const float* values, double sampleRate);

private:
    float ic1 = 0, ic2 = 0, vol = 1, drive = 0, dly = 0, rev = 0;
    bool filterOn = false;
};

class FxBus
{
public:
    void prepare (double sampleRate);
    void clear();
    // Adds the delay and reverb returns to `left`/`right`. `beatSamples` sets the delay (a dotted eighth).
    // Cheap when nothing has been sent for a while.
    void process (const float* toDelay, const float* toReverb, float* left, float* right, int n, double beatSamples);

private:
    std::vector<float> dL, dR, rL, rR;
    int write = 0, quiet = 1 << 30;
    double sampleRate = 44100.0;
    juce::Reverb reverb;
};

// The layer printed with its effects and moves: one pass of the loop, stereo, with the tails from the
// end wrapped round into the start so it loops cleanly.
juce::AudioBuffer<float> renderLayer (const float* data, int length, double lengthBeats, double sampleRate, const LayerFxState&);
}
