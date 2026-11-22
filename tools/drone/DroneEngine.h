#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <vector>

// The Drone box: an electronic tanpura (in the way of the little Raagini boxes), and strings that
// ring when something is played into them.
//
//   Four strings, plucked one after another, round and round: the first (Pa, Ma, Ni or Sa), two
//   middle Sa a hair apart (so they beat slowly), and the low Sa an octave down.
//
//   Each pluck is built from the string's harmonics. A tanpura's wide curved bridge (the jawari,
//   with its thread) makes the string graze it as it swings, which throws the sound up into the
//   high harmonics and lets it fall back down through them: the bloom you hear after every
//   pluck. Here that is a band of emphasis that starts high and sweeps down across the harmonics.
//
//   Reeds adds a held tone under the strings (a shruti box: bellows and reeds on the same notes).
//
//   Resonate: whatever is fed in (a voice, a drum, the whole mix) rings four strings tuned the
//   same way: sing the note and they sing back.
namespace crate::drone
{
constexpr int kStrings = 4;
constexpr int kHarmonics = 48;
constexpr int kReedNotes = 3, kReedHarmonics = 14;
constexpr int kControl = 32;   // samples between changes of the harmonics' levels

struct Settings
{
    bool on = false;
    int root = 0;            // 0 C ... 11 B
    int octave = 0;          // -1 low, 0 middle, 1 high
    int first = 0;           // the first string: 0 Pa, 1 Ma, 2 Ni, 3 Sa
    float paceSeconds = 1.1f;   // between plucks, when it keeps its own time
    int sync = 0;            // 0 own time, 1 a pluck every beat, 2 every 2 beats, 3 every bar
    float jawari = 0.6f;     // 0 a plain string ... 1 all buzz and bloom
    float decay = 0.5f;      // how long a string rings
    float fineCents = 0.0f;
    float width = 0.7f;
    float reeds = 0.0f;
    float resonate = 0.0f;   // how much the strings ring to what's fed in
    float strings = 1.0f;    // the plucked strings' level
};

struct Clock
{
    bool playing = false;
    double ppq = 0, bpm = 120, beatsPerBar = 4;
};

// The ready-made drones, the same on the Mac and the phone. (The note, On, and what the strings
// ring to aren't in them: those belong to the song.)
struct Preset
{
    const char* name;
    int first, octave, sync;
    float pace, jawari, decay, width, reeds, strings;
};

inline const std::array<Preset, 10>& presets()
{
    static const std::array<Preset, 10> list { {
        { "Classic (Pa)",        0,  0, 0, 1.10f, 0.60f, 0.50f, 0.70f, 0.00f, 1.0f },
        { "Evening (Ma)",        1,  0, 0, 1.30f, 0.55f, 0.60f, 0.70f, 0.00f, 1.0f },
        { "Yearning (Ni)",       2,  0, 0, 1.20f, 0.65f, 0.55f, 0.75f, 0.00f, 1.0f },
        { "Deep and slow",       0, -1, 0, 2.20f, 0.70f, 0.90f, 0.85f, 0.15f, 1.0f },
        { "Bright jawari",       0,  0, 0, 0.90f, 1.00f, 0.45f, 0.60f, 0.00f, 1.0f },
        { "Plain strings",       0,  0, 0, 1.10f, 0.10f, 0.50f, 0.70f, 0.00f, 1.0f },
        { "On the beat",         0,  0, 1, 1.00f, 0.60f, 0.40f, 0.70f, 0.00f, 1.0f },
        { "Shruti box",          0,  0, 0, 1.10f, 0.50f, 0.50f, 0.60f, 1.00f, 0.0f },
        { "Strings and reeds",   0,  0, 0, 1.40f, 0.60f, 0.65f, 0.80f, 0.45f, 1.0f },
        { "High shimmer",        3,  1, 0, 0.70f, 0.85f, 0.50f, 0.90f, 0.00f, 1.0f },
    } };
    return list;
}

// How far under the other tools it sits, at its fader's 0 dB (6 dB down: a drone is a bed under
// the song). The Mac and the phone both use it.
constexpr float kBedGain = 0.5f;

inline const char* firstName (int first)
{
    static const char* names[] = { "Pa", "Ma", "Ni", "Sa" };
    return names[juce::jlimit (0, 3, first)];
}

// The middle strings' note (Sa), in Hz.
inline double rootHz (int root, int octave, float fineCents)
{
    return 130.8127826502993 * std::pow (2.0, (juce::jlimit (0, 11, root) + fineCents / 100.0) / 12.0 + juce::jlimit (-1, 1, octave));
}

// Each string against the middle Sa (just intonation, as a tanpura is tuned by ear).
inline double stringRatio (int string, int first)
{
    static const double firsts[] = { 0.75, 2.0 / 3.0, 15.0 / 16.0, 1.0 };
    switch (string)
    {
        case 0:  return firsts[juce::jlimit (0, 3, first)];
        case 3:  return 0.5;
        default: return 1.0;
    }
}

class Engine
{
public:
    void prepare (double rate)
    {
        sampleRate = juce::jmax (8000.0, rate);
        for (auto& c : combs)
        {
            c.line.assign ((size_t) (sampleRate / 24.0) + 8, 0.0f);
            c.write = 0;
            c.damp = 0;
        }
        reset();
    }

    void reset()
    {
        for (auto& s : slots)
            s = {};
        for (auto& r : reedBank)
            r = {};
        for (auto& c : combs)
        {
            std::fill (c.line.begin(), c.line.end(), 0.0f);
            c.damp = 0;
        }
        untilPluck = 0;
        nextString = 0;
        controlPhase = 0;
        reedGain = resonateGain = 0;
        reedTime = 0;
        wasOn = false;
        lastPpq = -1;
        dcL = dcR = 0;
    }

    // Plucks a string now (the screen's strings; 0-3).
    void pluck (int string) { pendingPluck.store (string); }

    // For the screen: how many times each string has been plucked, and how loud it rings (0-1).
    std::array<std::atomic<int>, kStrings> pluckCounts {};
    std::array<std::atomic<float>, kStrings> ringing {};
    std::atomic<float> resonating { 0.0f };

    // Adds to left and right. `inL`/`inR` (may be null) are what's played into the strings.
    void process (float* left, float* right, const float* inL, const float* inR, int numSamples, const Settings& s, const Clock& clock)
    {
        const double sa = rootHz (s.root, s.octave, s.fineCents);
        if (s.on && ! wasOn)
        {
            untilPluck = 0;
            nextString = 0;
        }
        wasOn = s.on;

        // Where the plucks fall in this block.
        int pluckAt[4];
        int numPlucks = 0;
        if (s.on)
        {
            const double beats = s.sync == 1 ? 1.0 : s.sync == 2 ? 2.0 : s.sync == 3 ? clock.beatsPerBar : 0.0;
            if (beats > 0 && clock.playing && clock.bpm > 1)
            {
                const double perSample = clock.bpm / (60.0 * sampleRate);
                const double end = clock.ppq + numSamples * perSample;
                double next = std::ceil (clock.ppq / beats - 1.0e-9) * beats;
                if (lastPpq < 0 || clock.ppq < lastPpq - 1.0e-6)
                    lastPluckPpq = -1.0e9;   // (the clock started or jumped)
                for (; next < end && numPlucks < 4; next += beats)
                    if (next > lastPluckPpq + beats * 0.5)   // (never the same beat twice)
                    {
                        pluckAt[numPlucks++] = juce::jlimit (0, numSamples - 1, (int) ((next - clock.ppq) / perSample));
                        lastPluckPpq = next;
                    }
                lastPpq = end;
                untilPluck = (int) (beats * 60.0 / clock.bpm * sampleRate);   // (if the clock stops, it carries on)
                pluckSeconds = (float) (beats * 60.0 / clock.bpm);
            }
            else
            {
                lastPpq = -1;
                const double seconds = beats > 0 ? beats * 60.0 / juce::jmax (20.0, clock.bpm) : (double) juce::jlimit (0.2f, 6.0f, s.paceSeconds);
                const int interval = juce::jmax (64, (int) (seconds * sampleRate));
                pluckSeconds = (float) seconds;
                untilPluck = juce::jmin (untilPluck, interval);
                int at = untilPluck;
                while (at < numSamples && numPlucks < 4)
                {
                    pluckAt[numPlucks++] = at;
                    at += interval;
                }
                untilPluck = at - numSamples;
            }
        }
        const int byHand = pendingPluck.exchange (-1);

        const float reedTarget = s.on ? juce::jlimit (0.0f, 1.0f, s.reeds) : 0.0f;
        const float resTarget = juce::jlimit (0.0f, 1.0f, s.resonate);
        const float stringLevel = juce::jlimit (0.0f, 1.5f, s.strings);
        tuneCombs (sa, s.first, resTarget);

        int done = 0, pluckIndex = 0;
        if (byHand >= 0)
            startPluck (byHand, sa, s.first, 1.0f);
        while (done < numSamples)
        {
            while (pluckIndex < numPlucks && pluckAt[pluckIndex] <= done)
            {
                startPluck (nextString, sa, s.first, 0.86f + 0.14f * random.nextFloat());
                nextString = (nextString + 1) % kStrings;
                ++pluckIndex;
            }
            int n = juce::jmin (numSamples - done, kControl - controlPhase);
            if (pluckIndex < numPlucks)
                n = juce::jmin (n, juce::jmax (1, pluckAt[pluckIndex] - done));
            if (controlPhase == 0)
                control (s, sa, reedTarget, resTarget);

            for (int i = 0; i < n; ++i)
            {
                float l = 0, r = 0;
                // The strings.
                for (auto& slot : slots)
                {
                    if (! slot.active)
                        continue;
                    float sum = 0;
                    for (int k = 0; k < slot.count; ++k)
                    {
                        auto& p = slot.partials[(size_t) k];
                        const float re = p.re * p.cr - p.im * p.ci;
                        p.im = p.re * p.ci + p.im * p.cr;
                        p.re = re;
                        p.amp += p.step;
                        sum += p.im * p.amp;
                    }
                    l += sum * slot.gainL;
                    r += sum * slot.gainR;
                }
                l *= stringLevel;
                r *= stringLevel;
                // The reeds.
                if (reedsSounding)
                    for (int note = 0; note < kReedNotes; ++note)
                    {
                        float sum = 0;
                        auto& bank = reedBank[(size_t) note];
                        for (int k = 0; k < bank.count; ++k)
                        {
                            auto& p = bank.partials[(size_t) k];
                            const float re = p.re * p.cr - p.im * p.ci;
                            p.im = p.re * p.ci + p.im * p.cr;
                            p.re = re;
                            p.amp += p.step;
                            sum += p.im * p.amp;
                        }
                        l += sum * bank.gainL;
                        r += sum * bank.gainR;
                    }
                // The strings that ring to what's fed in.
                if (combsSounding)
                {
                    const float in = (inL != nullptr ? inL[done + i] : 0.0f) + (inR != nullptr ? inR[done + i] : 0.0f);
                    resonateGain += (resTarget - resonateGain) * resonateSmooth;
                    const float feed = in * 0.5f * combFeed * resonateGain;
                    for (auto& c : combs)
                    {
                        const int size = (int) c.line.size();
                        float at = (float) c.write - c.delay;
                        if (at < 0)
                            at += (float) size;
                        const int i0 = (int) at;
                        const float frac = at - (float) i0;
                        const float out = c.line[(size_t) i0] + (c.line[(size_t) ((i0 + 1) % size)] - c.line[(size_t) i0]) * frac;
                        c.damp += (out - c.damp) * (1.0f - kDamp);
                        const float next = feed + c.damp * c.feedback;
                        c.line[(size_t) c.write] = std::abs (next) < 1.0e-20f ? 0.0f : next;
                        c.write = (c.write + 1) % size;
                        c.level += (std::abs (out) - c.level) * 0.0005f;
                        l += out * c.gainL;
                        r += out * c.gainR;
                    }
                }
                // (Combs can hold a little DC.)
                dcL += (l - dcL) * 0.0006f;
                dcR += (r - dcR) * 0.0006f;
                left[done + i] += (l - dcL) * kOutput;
                right[done + i] += (r - dcR) * kOutput;
            }
            done += n;
            controlPhase = (controlPhase + n) % kControl;
            for (auto& slot : slots)
                slot.age += slot.active ? n : 0;
            reedTime += n;
        }
    }

    // Still making sound (after Off, the strings ring out).
    bool isSounding() const { return anySounding; }

private:
    struct Partial
    {
        float re = 1, im = 0, cr = 1, ci = 0, amp = 0, step = 0;
        float base = 0, tau = 1, hz = 0;
    };

    struct Slot   // one pluck of a string (a string re-plucked: its last pluck fades under the new one)
    {
        bool active = false, fading = false;
        int string = 0, count = 0, age = 0, fadeAge = 0;
        float velocity = 1, gainL = 0.7f, gainR = 0.7f;
        std::array<Partial, kHarmonics> partials;
    };

    struct ReedBank
    {
        int count = 0;
        float gainL = 0.7f, gainR = 0.7f;
        double hz = 0;
        std::array<Partial, kReedHarmonics> partials;
    };

    struct Comb
    {
        std::vector<float> line;
        int write = 0;
        float delay = 100, feedback = 0, damp = 0, gainL = 0.5f, gainR = 0.5f, level = 0;
    };

    static constexpr float kDamp = 0.28f;       // the ringing strings lose their top as they ring
    static constexpr float kOutput = 0.5f;      // (set so the strings sit with the other tools' sounds)

    void panFor (int string, float width, float& l, float& r) const
    {
        static const float where[] = { -0.75f, -0.25f, 0.25f, 0.75f };
        const float angle = (where[string] * juce::jlimit (0.0f, 1.0f, width) + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        l = std::cos (angle);
        r = std::sin (angle);
    }

    void startPluck (int string, double sa, int first, float velocity)
    {
        string = juce::jlimit (0, kStrings - 1, string);
        // Its last pluck fades away under this one; this one takes the other slot.
        Slot* use = nullptr;
        for (int i = 0; i < 2; ++i)
        {
            auto& slot = slots[(size_t) (string * 2 + i)];
            if (slot.active && ! slot.fading)
            {
                slot.fading = true;
                slot.fadeAge = 0;
            }
        }
        for (int i = 0; i < 2; ++i)
        {
            auto& slot = slots[(size_t) (string * 2 + i)];
            if (! slot.active)
                use = &slot;
        }
        if (use == nullptr)   // (both still fading: take the older)
            use = slots[(size_t) (string * 2)].fadeAge >= slots[(size_t) (string * 2 + 1)].fadeAge ? &slots[(size_t) (string * 2)] : &slots[(size_t) (string * 2 + 1)];

        auto& slot = *use;
        slot.active = true;
        slot.fading = false;
        slot.string = string;
        slot.age = 0;
        slot.fadeAge = 0;
        slot.velocity = velocity;
        // The two middle strings sit a hair apart, and no two plucks are quite the same.
        static const double apartCents[] = { 0.0, -0.9, 0.9, 0.0 };
        const double cents = apartCents[string] + (random.nextDouble() - 0.5) * 0.8;
        const double hz = sa * stringRatio (string, first) * std::pow (2.0, cents / 1200.0);
        const double top = juce::jmin (11000.0, sampleRate * 0.45);
        slot.count = 0;
        for (int k = 1; k <= kHarmonics; ++k)
        {
            const double f = hz * k * (1.0 + 1.2e-5 * k * k);   // a real string's harmonics run a little sharp
            if (f > top)
                break;
            auto& p = slot.partials[(size_t) slot.count++];
            const double w = juce::MathConstants<double>::twoPi * f / sampleRate;
            const double phase = random.nextDouble() * juce::MathConstants<double>::twoPi;
            p.re = (float) std::cos (phase);
            p.im = (float) std::sin (phase);
            p.cr = (float) std::cos (w);
            p.ci = (float) std::sin (w);
            p.amp = 0;
            p.step = 0;
            p.hz = (float) f;
        }
        pluckCounts[(size_t) string].fetch_add (1);
    }

    void tuneCombs (double sa, int first, float amount)
    {
        const float t60 = 1.2f + 3.8f * amount;
        for (int i = 0; i < kStrings; ++i)
        {
            auto& c = combs[(size_t) i];
            const double hz = sa * stringRatio (i, first) * (i == 2 ? 2.0 : 1.0);   // (one middle string an octave up: more to catch)
            const double period = sampleRate / hz;
            // (The damping in the loop adds its own little delay.)
            c.delay = (float) juce::jlimit (2.0, (double) c.line.size() - 4.0, period - kDamp / (1.0 - kDamp));
            c.feedback = (float) std::pow (10.0, -3.0 * (period / sampleRate) / t60);
            panFor (i, 0.8f, c.gainL, c.gainR);
        }
        // Even out how hard a held note drives them, whatever the ring time.
        combFeed = 0.04f * (1.0f - combs[1].feedback) * 60.0f;
        combFeed = juce::jlimit (0.005f, 0.2f, combFeed);
        resonateSmooth = (float) (1.0 - std::exp (-1.0 / (0.05 * sampleRate)));
    }

    // Every kControl samples: where each harmonic's level is heading.
    void control (const Settings& s, double sa, float reedTarget, float resTarget)
    {
        const float jawari = juce::jlimit (0.0f, 1.0f, s.jawari);
        const float t60 = 2.5f + 11.0f * juce::jlimit (0.0f, 1.0f, s.decay);
        const float tilt = 1.4f - 0.95f * jawari;      // the buzz keeps the high harmonics up
        const float bloom = 10.0f * jawari * jawari + 2.0f * jawari;
        const float body = 1.0f - 0.45f * jawari;       // ...and takes it out of the bottom two
        const float block = (float) kControl / (float) sampleRate;
        // As loud whatever the settings: more buzz, quicker plucks and the high octave all add up to more.
        const float evenDb = -16.0f * std::pow (jawari, 2.9f) + juce::jlimit (-6.0f, 5.0f, 8.0f * std::log10 (pluckSeconds / 1.1f)) - (s.octave > 0 ? 2.5f : 0.0f) - 0.1f;
        const float even = std::pow (10.0f, evenDb / 20.0f);
        bool any = false;
        std::array<float, kStrings> ring {};
        for (auto& slot : slots)
        {
            if (! slot.active)
                continue;
            const float t = (float) slot.age / (float) sampleRate + block;
            // The band the jawari throws the sound into: high at the pluck, falling through the harmonics.
            const float centre = 520.0f + 3900.0f * std::exp (-t / (0.55f + 0.5f * jawari));
            // Then it comes round again, lower and softer, as the string keeps grazing the bridge.
            const float again = 600.0f + 1500.0f * (0.5f + 0.5f * std::cos (t * 2.1f));
            const float attack = 1.0f - std::exp (-t / 0.016f);
            float fade = 1.0f;
            if (slot.fading)
            {
                slot.fadeAge += kControl;
                fade = std::exp (-(float) slot.fadeAge / (float) sampleRate / (s.on ? 0.10f : 0.35f));
            }
            else if (! s.on)
            {
                slot.fading = true;
                slot.fadeAge = 0;
            }
            float loudest = 0;
            for (int k = 0; k < slot.count; ++k)
            {
                auto& p = slot.partials[(size_t) k];
                const float n = (float) (k + 1);
                const float ring60 = t60 / (1.0f + 0.045f * n);
                const float env = std::exp (-6.91f * t / ring60);
                const float d1 = std::log2 (p.hz / centre) / 0.42f, d2 = std::log2 (p.hz / again) / 0.5f;
                const float shape = 1.0f + bloom * std::exp (-0.5f * d1 * d1) + 0.35f * bloom * std::exp (-0.5f * d2 * d2) * (1.0f - std::exp (-t / 0.8f));
                const float target = (k < 2 ? body : 1.0f) * even * slot.velocity * attack * fade * env * shape * std::pow (n, -tilt) * 0.5f;
                p.step = (target - p.amp) / (float) kControl;
                loudest = juce::jmax (loudest, target);
                // (Keep the spinning pair on the circle.)
                const float mag = p.re * p.re + p.im * p.im;
                const float fix = 0.5f * (3.0f - mag);
                p.re *= fix;
                p.im *= fix;
            }
            panFor (slot.string, s.width, slot.gainL, slot.gainR);
            ring[(size_t) slot.string] = juce::jmax (ring[(size_t) slot.string], juce::jlimit (0.0f, 1.0f, loudest * 2.0f * attack));
            if ((slot.fading && fade < 1.0e-4f) || loudest < 1.0e-6f * (t > 0.2f ? 1.0f : 0.0f))
                slot.active = false;
            any = any || slot.active;
        }
        for (int i = 0; i < kStrings; ++i)
            ringing[(size_t) i].store (ring[(size_t) i]);

        // The reeds: held, breathing slowly, each on its own breath.
        reedGain += (reedTarget - reedGain) * (1.0f - std::exp (-block / 0.35f));
        reedsSounding = reedGain > 1.0e-4f || reedTarget > 0;
        if (reedsSounding)
        {
            static const int which[] = { 3, 0, 1 };   // low Sa, the first string's note, Sa
            const float seconds = (float) (reedTime / sampleRate);
            for (int note = 0; note < kReedNotes; ++note)
            {
                auto& bank = reedBank[(size_t) note];
                const double hz = sa * stringRatio (which[note], s.first);
                if (std::abs (hz - bank.hz) > 1.0e-6 * hz)
                {
                    bank.hz = hz;
                    bank.count = 0;
                    const double top = juce::jmin (7000.0, sampleRate * 0.45);
                    for (int k = 1; k <= kReedHarmonics && hz * k < top; ++k)
                    {
                        auto& p = bank.partials[(size_t) bank.count++];
                        const double w = juce::MathConstants<double>::twoPi * hz * k / sampleRate;
                        p.cr = (float) std::cos (w);
                        p.ci = (float) std::sin (w);
                        p.hz = (float) (hz * k);
                    }
                }
                const float breath = 1.0f + 0.16f * std::sin (seconds * (0.55f + 0.13f * (float) note) + 2.1f * (float) note);
                static const float noteLevel[] = { 0.85f, 0.6f, 0.75f };
                for (int k = 0; k < bank.count; ++k)
                {
                    auto& p = bank.partials[(size_t) k];
                    const float n = (float) (k + 1);
                    // A reed: every harmonic there, the odd ones a little stronger, rolled off on top.
                    const float colour = ((k % 2) == 0 ? 1.0f : 0.7f) / (n * (1.0f + (p.hz / 2600.0f) * (p.hz / 2600.0f)));
                    const float target = reedGain * breath * noteLevel[note] * colour * 0.22f;
                    p.step = (target - p.amp) / (float) kControl;
                    const float mag = p.re * p.re + p.im * p.im;
                    const float fix = 0.5f * (3.0f - mag);
                    p.re *= fix;
                    p.im *= fix;
                }
                static const int where[] = { 1, 0, 2 };
                panFor (where[note] + (note == 2 ? 1 : 0), s.width * 0.6f, bank.gainL, bank.gainR);
            }
            any = true;
        }

        float combLevel = 0;
        for (auto& c : combs)
            combLevel = juce::jmax (combLevel, c.level);
        combsSounding = resTarget > 0 || resonateGain > 1.0e-4f || combLevel > 1.0e-5f;
        resonating.store (juce::jlimit (0.0f, 1.0f, combLevel * 6.0f));
        anySounding = any || combsSounding;
    }

    double sampleRate = 48000.0;
    std::array<Slot, kStrings * 2> slots;
    std::array<ReedBank, kReedNotes> reedBank;
    std::array<Comb, kStrings> combs;
    juce::Random random { 0x7a9b };
    std::atomic<int> pendingPluck { -1 };
    int untilPluck = 0, nextString = 0, controlPhase = 0;
    float pluckSeconds = 1.1f, reedGain = 0, resonateGain = 0, combFeed = 0.1f, resonateSmooth = 0.001f, dcL = 0, dcR = 0;
    double reedTime = 0, lastPpq = -1, lastPluckPpq = -1.0e9;
    bool wasOn = false, reedsSounding = false, combsSounding = false, anySounding = false;
};
}
