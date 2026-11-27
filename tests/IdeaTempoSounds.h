#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

// Made-up takes for the idea-tempo tests: claps, a sung jingle, a hum (at 48 kHz, with a little room hiss
// and loose timing, as a person would).
namespace ideatempo
{
constexpr double kRate = 48000.0;

inline void addTone (juce::AudioBuffer<float>& b, double at, double seconds, double hz, float amp, double attack)
{
    const int s0 = (int) (at * kRate), n = (int) (seconds * kRate);
    for (int i = 0; i < n && s0 + i < b.getNumSamples(); ++i)
    {
        if (s0 + i < 0)
            continue;
        const double t = i / kRate;
        const double env = juce::jmin (1.0, t / attack) * juce::jmin (1.0, (seconds - t) / 0.03);
        const auto v = (float) (amp * env * (std::sin (juce::MathConstants<double>::twoPi * hz * t) + 0.3 * std::sin (2.0 * juce::MathConstants<double>::twoPi * hz * t)));
        for (int c = 0; c < b.getNumChannels(); ++c)
            b.addSample (c, s0 + i, v);
    }
}

inline void addHiss (juce::AudioBuffer<float>& b, float amp, juce::Random& rng)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.addSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * amp);
}

// up to ±20 ms off, like a person
inline double loose (juce::Random& rng) { return (rng.nextDouble() * 2.0 - 1.0) * 0.02; }

inline juce::AudioBuffer<float> claps (double bpm, double first, int count, double seconds)
{
    juce::Random rng (11);
    juce::AudioBuffer<float> b (2, (int) (seconds * kRate));
    b.clear();
    addHiss (b, 0.0008f, rng);
    for (int k = 0; k < count; ++k)
        addTone (b, first + k * 60.0 / bpm + loose (rng), 0.08, 2000.0, 0.5f, 0.001);
    return b;
}

// Two bars sung twice, words on the beats and the 1/8s between, and a pickup half a beat before bar 1 (at `bar1`).
inline juce::AudioBuffer<float> jingle (double bpm, double bar1, double seconds)
{
    juce::Random rng (5);
    juce::AudioBuffer<float> b (2, (int) (seconds * kRate));
    b.clear();
    addHiss (b, 0.0015f, rng);
    const double beat = 60.0 / bpm;
    const double at[] = { 0, 1, 1.5, 2.5, 3, 4, 4.5, 5, 6, 7 };
    const double len[] = { 0.9, 0.45, 0.9, 0.45, 0.9, 0.45, 0.45, 0.9, 0.9, 0.9 };
    const double hz[] = { 330, 392, 440, 392, 330, 294, 330, 392, 440, 330 };
    addTone (b, bar1 - 0.5 * beat + loose (rng), 0.4 * beat, 294.0, 0.3f, 0.03);
    for (int r = 0; r < 2; ++r)
        for (int i = 0; i < 10; ++i)
            addTone (b, bar1 + (r * 8 + at[i]) * beat + loose (rng), len[i] * beat, hz[i], 0.3f * (0.7f + 0.3f * (float) (i % 3)), 0.025);
    return b;
}
}
