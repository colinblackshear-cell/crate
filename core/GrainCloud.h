#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

// Grains: the sound you're making, cut into many tiny overlapping pieces and played back as a
// cloud, shifted in pitch, scattered in time, some backwards. Freeze stops listening and keeps
// the last moment ringing on and on as a texture, so a word, a chord or a drum hit becomes a
// pad you can play over.
//
// Used by the Grain pedal (on any track) and by the FX pad's Freeze and Grain.
class GrainCloud
{
public:
    static constexpr int kMaxGrains = 40;
    static constexpr double kHistorySeconds = 3.0;

    struct Settings
    {
        float mix = 1.0f;          // 0 dry .. 1 only grains
        float sizeMs = 120.0f;     // each grain, 20..600 ms
        float density = 0.5f;      // 0..1: from sparse (3 a second) to thick (80 a second)
        float spray = 0.2f;        // 0..1: how far back in time grains reach (up to 2 seconds)
        float pitch = 0.0f;        // semitones, -24..24
        float pitchSpread = 0.0f;  // 0..1: up to an octave either way, at random (or to octaves at 1)
        float reverse = 0.0f;      // 0..1: the share of grains played backwards
        bool freeze = false;       // stop listening: the last moment goes on
    };

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        history.setSize (2, (int) (rate * kHistorySeconds) + 1);
        history.clear();
        write = 0;
        filled = 0;
        for (auto& g : grains)
            g.active = false;
        untilNext = 0;
    }

    void reset()
    {
        history.clear();
        filled = 0;
        for (auto& g : grains)
            g.active = false;
    }

    // In place. `s` is read once per block.
    void process (juce::AudioBuffer<float>& buffer, const Settings& s)
    {
        const int n = buffer.getNumSamples();
        const int size = history.getNumSamples();
        if (size < 2 || n <= 0)
            return;
        const int channels = juce::jmin (2, buffer.getNumChannels());
        auto* hl = history.getWritePointer (0);
        auto* hr = history.getWritePointer (1);
        const float mix = juce::jlimit (0.0f, 1.0f, s.mix);
        // Grains a second: 3 to 80.
        const double perSecond = 3.0 * std::pow (80.0 / 3.0, (double) juce::jlimit (0.0f, 1.0f, s.density));
        const double gap = rate / perSecond;
        const int grainLength = juce::jlimit (16, size / 4, (int) (rate * juce::jlimit (20.0f, 600.0f, s.sizeMs) * 0.001));
        // As grains overlap more, each is quieter, so the cloud stays about as loud as what goes in.
        const float overlap = (float) juce::jmax (1.0, grainLength / gap);
        const float grainGain = 1.0f / std::sqrt (overlap) * 1.4f;

        for (int i = 0; i < n; ++i)
        {
            const float inL = buffer.getSample (0, i);
            const float inR = channels > 1 ? buffer.getSample (1, i) : inL;
            if (! s.freeze)
            {
                hl[write] = inL;
                hr[write] = inR;
                write = (write + 1) % size;
                filled = juce::jmin (size, filled + 1);
                frozenAt = write;
            }

            if (--untilNext <= 0 && filled > grainLength * 2)
            {
                // A little jitter, so the cloud has no buzz of its own.
                untilNext = juce::jmax (1, (int) (gap * (0.7 + 0.6 * random.nextFloat())));
                start (s, grainLength, grainGain);
            }

            float l = 0, r = 0;
            for (auto& g : grains)
            {
                if (! g.active)
                    continue;
                const double p = g.pos;
                const int i0 = ((int) std::floor (p) % size + size) % size;
                const int i1 = (i0 + 1) % size;
                const float frac = (float) (p - std::floor (p));
                // A raised-cosine window over the grain.
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.length);
                l += (hl[i0] + (hl[i1] - hl[i0]) * frac) * w * g.gainL;
                r += (hr[i0] + (hr[i1] - hr[i0]) * frac) * w * g.gainR;
                g.pos += g.step;
                if (++g.age >= g.length)
                    g.active = false;
            }
            buffer.setSample (0, i, inL * (1.0f - mix) + l * mix);
            if (channels > 1)
                buffer.setSample (1, i, inR * (1.0f - mix) + r * mix);
        }
    }

    // Just listens (fills the history, no grains): so a Freeze pressed later has the moment
    // before it to hold. In place, unchanged.
    void listen (const juce::AudioBuffer<float>& buffer)
    {
        const int size = history.getNumSamples();
        if (size < 2)
            return;
        const int n = buffer.getNumSamples();
        const int channels = juce::jmin (2, buffer.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            const float l = buffer.getSample (0, i);
            history.setSample (0, write, l);
            history.setSample (1, write, channels > 1 ? buffer.getSample (1, i) : l);
            write = (write + 1) % size;
        }
        filled = juce::jmin (size, filled + n);
        frozenAt = write;
    }

    bool hasSound() const { return filled > 0; }

private:
    struct Grain
    {
        bool active = false;
        double pos = 0, step = 1;
        int age = 0, length = 1;
        float gainL = 1, gainR = 1;
    };

    void start (const Settings& s, int length, float gain)
    {
        Grain* g = nullptr;
        for (auto& c : grains)
            if (! c.active)
            {
                g = &c;
                break;
            }
        if (g == nullptr)
            return;
        const int size = history.getNumSamples();
        // How far back it starts: past its own length (so it never runs into the writing head),
        // plus a spray of up to two seconds.
        float semis = juce::jlimit (-24.0f, 24.0f, s.pitch);
        if (s.pitchSpread > 0.0f)
        {
            if (s.pitchSpread > 0.95f)
                semis += 12.0f * (float) (random.nextInt (3) - 1);   // octaves
            else
                semis += (random.nextFloat() * 2.0f - 1.0f) * 12.0f * s.pitchSpread;
        }
        const double ratio = std::pow (2.0, semis / 12.0);
        const bool backwards = random.nextFloat() < s.reverse;
        const double reach = length * juce::jmax (1.0, ratio) + 64;
        const double spray = juce::jlimit (0.0f, 1.0f, s.spray) * juce::jmin (rate * 2.0, (double) juce::jmax (0, filled - (int) reach * 2));
        const double back = reach + random.nextFloat() * spray;
        const int from = s.freeze ? frozenAt : write;
        g->pos = std::fmod ((double) from - back + size * 4.0, (double) size);
        if (backwards)
            g->pos = std::fmod (g->pos + length * ratio, (double) size);
        g->step = backwards ? -ratio : ratio;
        g->age = 0;
        g->length = length;
        const float pan = (random.nextFloat() * 2.0f - 1.0f) * 0.6f;
        g->gainL = gain * std::sqrt (0.5f * (1.0f - pan)) * 1.4142f;
        g->gainR = gain * std::sqrt (0.5f * (1.0f + pan)) * 1.4142f;
        g->active = true;
    }

    double rate = 44100.0;
    juce::AudioBuffer<float> history;
    int write = 0, filled = 0, frozenAt = 0;
    int untilNext = 0;
    std::array<Grain, kMaxGrains> grains {};
    juce::Random random { 0x6a41 };
};
