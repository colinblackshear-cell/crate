#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>

// Small building blocks for the Crate synths (Bass and Synth): band-limited oscillators, a
// resonant filter, envelopes and an LFO. All per-sample, no allocation, safe on the audio thread.
namespace crate::synth
{
inline float noteToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

// PolyBLEP: rounds off a saw or square's jump so it doesn't alias.
inline float polyBlep (float t, float dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

enum class Wave { saw, square, triangle, supersaw };

struct Oscillator
{
    float phase = 0.0f;

    // `dt` is the frequency as a share of the sample rate.
    float saw (float dt)
    {
        float v = 2.0f * phase - 1.0f - polyBlep (phase, dt);
        advance (dt);
        return v;
    }

    float square (float dt, float width = 0.5f)
    {
        float v = phase < width ? 1.0f : -1.0f;
        v += polyBlep (phase, dt);
        float shifted = phase + (1.0f - width);
        shifted -= std::floor (shifted);
        v -= polyBlep (shifted, dt);
        advance (dt);
        return v;
    }

    float triangle (float dt)
    {
        const float v = 1.0f - 4.0f * std::abs (phase - 0.5f);
        advance (dt);
        return v;
    }

    float sine (float dt)
    {
        const float v = std::sin (juce::MathConstants<float>::twoPi * phase);
        advance (dt);
        return v;
    }

    void advance (float dt)
    {
        phase += dt;
        phase -= std::floor (phase);
    }
};

// A resonant low-pass: two state-variable stages (24 dB/octave) with soft saturation, so high
// resonance squelches rather than screams.
class LowPass
{
public:
    void prepare (double rate) { sampleRate = (float) rate; reset(); }
    void reset() { first = {}; second = {}; }

    // Resonance 0..1 (just under self-oscillation at the top).
    void set (float cutoffHz, float resonance)
    {
        cutoffHz = juce::jlimit (20.0f, sampleRate * 0.45f, cutoffHz);
        const float g = std::tan (juce::MathConstants<float>::pi * cutoffHz / sampleRate);
        first.set (g, 2.0f - 1.96f * juce::jlimit (0.0f, 1.0f, resonance));
        second.set (g, 1.414f);   // the second stage is plain, so there's one resonant peak
    }

    float process (float x)
    {
        return second.process (first.process (std::tanh (x)));
    }

private:
    // One state-variable stage (Andy Simper's form), low-pass output.
    struct Stage
    {
        float a1 = 1, a2 = 0, a3 = 0, ic1 = 0, ic2 = 0;
        void set (float g, float k)
        {
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }
        float process (float x)
        {
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }
    };

    float sampleRate = 44100.0f;
    Stage first, second;
};

// Attack, decay, sustain, release. Times in seconds.
class Envelope
{
public:
    void prepare (double rate) { sampleRate = (float) rate; }
    void set (float attack, float decay, float sustainLevel, float release)
    {
        attackStep = 1.0f / juce::jmax (1.0f, attack * sampleRate);
        decayCoef = coef (decay);
        sustain = juce::jlimit (0.0f, 1.0f, sustainLevel);
        releaseCoef = coef (release);
    }
    void gateOn (bool retrigger = true)
    {
        if (retrigger || stage == Stage::idle || stage == Stage::release)
            stage = Stage::attack;
    }
    void gateOff()
    {
        if (stage != Stage::idle)
            stage = Stage::release;
    }
    void kill() { stage = Stage::idle; level = 0.0f; }
    bool isActive() const { return stage != Stage::idle; }

    float next()
    {
        switch (stage)
        {
            case Stage::attack:
                level += attackStep;
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                level = sustain + (level - sustain) * decayCoef;
                break;
            case Stage::release:
                level *= releaseCoef;
                if (level < 1.0e-4f)
                {
                    level = 0.0f;
                    stage = Stage::idle;
                }
                break;
            case Stage::idle:
                break;
        }
        return level;
    }

    float getLevel() const { return level; }

private:
    enum class Stage { idle, attack, decay, release };
    float coef (float seconds) const { return std::exp (-1.0f / juce::jmax (1.0f, seconds * sampleRate * 0.25f)); }

    Stage stage = Stage::idle;
    float sampleRate = 44100.0f, level = 0.0f;
    float attackStep = 0.01f, decayCoef = 0.999f, sustain = 1.0f, releaseCoef = 0.999f;
};

// LFO shapes: sine, triangle, saw down, square, and sample-and-hold (random steps).
enum class LfoShape { sine, triangle, saw, square, random };

class Lfo
{
public:
    // Sets the rate as cycles per sample (so it can follow the tempo).
    void setIncrement (double cyclesPerSample) { increment = cyclesPerSample; }
    void syncTo (double cycles)
    {
        const double p = cycles - std::floor (cycles);
        if (p < phase)
            held = random.nextFloat() * 2.0f - 1.0f;
        phase = p;
    }
    void setShape (LfoShape s) { shape = s; }

    // -1..1
    float next()
    {
        const auto p = (float) phase;
        float v = 0.0f;
        switch (shape)
        {
            case LfoShape::sine:     v = std::sin (juce::MathConstants<float>::twoPi * p); break;
            case LfoShape::triangle: v = 1.0f - 4.0f * std::abs (p - 0.5f); break;
            case LfoShape::saw:      v = 1.0f - 2.0f * p; break;
            case LfoShape::square:   v = p < 0.5f ? 1.0f : -1.0f; break;
            case LfoShape::random:   v = held; break;
        }
        phase += increment;
        if (phase >= 1.0)
        {
            phase -= std::floor (phase);
            held = random.nextFloat() * 2.0f - 1.0f;
        }
        return v;
    }

private:
    double phase = 0.0, increment = 0.0;
    LfoShape shape = LfoShape::sine;
    float held = 0.0f;
    juce::Random random { 1234 };
};

// Tempo-synced rates the synths offer, as beats per cycle (or per step).
struct SyncRates
{
    static juce::StringArray names() { return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4 T", "1/8 T", "1/16 T" }; }
    static double beats (int index)
    {
        static constexpr double values[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
        return values[juce::jlimit (0, 10, index)];
    }
};
}
