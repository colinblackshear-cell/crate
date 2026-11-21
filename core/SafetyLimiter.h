#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

namespace crate
{
// The last thing a program's sound goes through: a transparent peak limiter that keeps the output
// under -0.3 dBFS, and a guard against NaN and infinity.
//
// Below -3 dBFS it does nothing at all (the samples pass untouched). Above that a soft knee eases
// the peaks towards the ceiling (they never reach it), and it clamps down on the very sample
// that's too loud (no lookahead, so it adds no delay and nothing shifts against the clock), then
// lets go over about 80 ms. Stereo is linked, so a loud side doesn't move the image.
class SafetyLimiter
{
public:
    static constexpr float kCeilingDb = -0.3f;
    static constexpr float kKneeDb = -3.0f;
    static constexpr float kReleaseSeconds = 0.08f;

    void prepare (double rate)
    {
        release = std::exp (-1.0f / (kReleaseSeconds * (float) juce::jmax (8000.0, rate)));
        reset();
    }

    void reset() { gain = 1.0f; }

    // `right` may be null (a mono signal). Returns true if the block had to be silenced because
    // something in it wasn't a number.
    bool process (float* left, float* right, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float sum = left[i] + (right != nullptr ? right[i] : 0.0f);
            if (! std::isfinite (sum) || ! std::isfinite (left[i]))
            {
                juce::FloatVectorOperations::clear (left, numSamples);
                if (right != nullptr)
                    juce::FloatVectorOperations::clear (right, numSamples);
                reset();
                return true;
            }
        }

        const float ceilingGain = juce::Decibels::decibelsToGain (kCeilingDb);
        for (int i = 0; i < numSamples; ++i)
        {
            const float l = left[i], r = right != nullptr ? right[i] : 0.0f;
            const float peak = juce::jmax (std::abs (l), std::abs (r));
            // What gain this sample needs, and let go slowly after.
            gain = juce::jmin (gainFor (peak), 1.0f - (1.0f - gain) * release);
            // (The clamp is only for rounding: the gain already keeps it under.)
            left[i] = juce::jlimit (-ceilingGain, ceilingGain, l * gain);
            if (right != nullptr)
                right[i] = juce::jlimit (-ceilingGain, ceilingGain, r * gain);
        }
        return false;
    }

    float getGain() const { return gain; }

private:
    // 1 up to the knee, then the peak eased into the ceiling (never reaching it).
    static float gainFor (float peak)
    {
        static const float knee = juce::Decibels::decibelsToGain (kKneeDb);
        static const float ceiling = juce::Decibels::decibelsToGain (kCeilingDb);
        if (peak <= knee)
            return 1.0f;
        const float room = ceiling - knee;
        return (knee + room * std::tanh ((peak - knee) / room)) / peak;
    }

    float gain = 1.0f, release = 0.999f;
};
}
