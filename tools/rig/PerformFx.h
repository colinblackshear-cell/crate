#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

// The performance effects on the whole mix, played from an XY pad: touch to bring one in,
// let go and it's gone (or Hold it). All in time with the Rig.
//
//   Filter    X: low-pass (left) to high-pass (right), the middle is open. Y: resonance.
//   Repeat    catches the beat you touch and loops it. X: how long (1/4 to 1/32). Y: how much.
//   Tape stop the whole mix slows down to a stop, like pulling the plug. Y: how long it takes.
//   Gate      chops the mix in rhythm. X: 1/8, 1/16 or 1/32. Y: how deep.
//   Riser     a noise build that rises while you hold, the mix thinning under it.
//             X: how long it takes to rise (1, 2 or 4 bars). Y: how loud.
class PerformFx
{
public:
    enum Mode { filter, repeat, tapeStop, gate, riser, numModes };
    static juce::String modeName (int);
    static juce::String axisText (int mode, bool xAxis);

    // The pad (any thread).
    std::atomic<int> mode { filter };
    std::atomic<bool> engaged { false };
    std::atomic<float> x { 0.5f }, y { 0.5f };

    // Audio thread.
    void prepare (double sampleRate);
    void process (juce::AudioBuffer<float>& mix, double ppq, double bpm, bool playing);

    // What the pad shows: how far a riser has risen, 0..1.
    float getRise() const { return rise.load(); }

private:
    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        void clear() { ic1 = ic2 = 0; }
        // low, band, high
        std::array<float, 3> run (float in, float g, float k)
        {
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = in - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return { v2, v1, in - k * v1 - v2 };
        }
    };

    double rate = 44100.0;
    juce::SmoothedValue<float> wet, xs, ys;
    std::array<Svf, 2> filters, thin;
    Svf noiseFilter;
    juce::Random random;

    // A few seconds of the mix, for Repeat and Tape stop.
    juce::AudioBuffer<float> history;
    int writePos = 0;
    // Repeat.
    int repeatStart = 0, repeatLength = 0, repeatPos = 0, repeatChoice = -1;
    // Tape stop.
    double tapePos = 0, tapeSpeed = 1;
    // Riser.
    double riseStartBeat = -1;
    double freeBeat = 0;
    std::atomic<float> rise { 0 };
    bool wasEngaged = false;
    int lastMode = -1;
};
