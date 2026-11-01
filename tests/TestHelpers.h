#pragma once

#include "core/Audio.h"

// A sine wave, so tests can check levels and positions.
inline juce::AudioBuffer<float> sine (int samples, int channels = 1, float level = 0.5f, double hz = 441.0)
{
    juce::AudioBuffer<float> b (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            b.setSample (ch, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / crate::kSampleRate));
    return b;
}

// 0, 1, 2, ... scaled down, so tests can tell exactly which samples went where.
inline juce::AudioBuffer<float> ramp (int samples, int channels = 1)
{
    juce::AudioBuffer<float> b (channels, samples);
    for (int ch = 0; ch < channels; ++ch)
        for (int i = 0; i < samples; ++i)
            b.setSample (ch, i, (float) i / (float) samples * (ch == 0 ? 1.0f : -1.0f));
    return b;
}
