#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

// Sound as every Crate tool passes it around: 1 or 2 channels of floats at 44.1 kHz, shared
// and never changed in place (an edit makes a new buffer), so the audio thread can keep
// playing one version while the UI makes the next.
namespace crate
{
    using SharedAudio = std::shared_ptr<const juce::AudioBuffer<float>>;

    // The family's one sample rate. It matches the MPC2000XL, so sounds move between the tools
    // and the bank builder without resampling.
    constexpr double kSampleRate = 44100.0;

    inline SharedAudio share (juce::AudioBuffer<float> b)
    {
        return std::make_shared<const juce::AudioBuffer<float>> (std::move (b));
    }

    inline double seconds (int samples) { return samples / kSampleRate; }
    inline int samples (double secondsValue) { return (int) std::round (secondsValue * kSampleRate); }

    // "1.25 s", or "1:02.5" for a minute and over.
    inline juce::String formatTime (int sampleCount)
    {
        const double s = seconds (sampleCount);
        if (s < 60.0)
            return juce::String (s, s < 10.0 ? 3 : 2) + " s";
        const int minutes = (int) (s / 60.0);
        return juce::String (minutes) + ":" + juce::String (s - minutes * 60.0, 1).paddedLeft ('0', 4);
    }
}
