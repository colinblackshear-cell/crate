#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
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

    // The pattern. Order: which string comes next (Classic Pa-Sa-Sa-low Sa, Reverse, Wander, Pairs of
    // two at once, Roll: all four in a quick strum on every pluck).
    int order = 0;
    // Steps: 0 = off, else 8 or 16 steps, each the strings it plucks (bits 0-3, 0 = a rest), one step
    // every stepBeats beats, in time with the song (stopped, at its tempo). Overrides Order and Pluck.
    int stepCount = 0;
    std::array<std::uint8_t, 16> steps {};
    float stepBeats = 0.5f;
    float swing = 0.0f;      // every second step later (0 straight ... 1 a full triplet swing)
    float human = 0.0f;      // a player's drift: each pluck a little early or late, softer or harder

    // The tone. Voice: the instrument (Tanpura, Bowed, Swarmandal, Electric, Bell).
    int voice = 0;
    float bright = 0.5f;     // dark ... bright (0.5: as the voice is)
    float body = 0.5f;       // the low harmonics: thin ... big gourd (0.5: as the voice is)
    float detune = 0.0f;     // the strings drift apart and beat against each other
    float space = 0.0f;      // its own reverb

    // Miracle tones: a held tone at an exact frequency (the Solfeggio tones, 432, Om, or your own), heard while
    // the drone is on. Fit: 0 over the drone (the drone stays on its note), 1 the drone tuned to the tone (its Sa
    // becomes the tone, octaves down). Sound: 0 pure (a sine), 1 warm (with a soft octave under it), 2 bowl (a
    // singing bowl's partials). Beat: binaural, the right ear this many Hz higher (0 off). Swell: how slowly and
    // deeply it breathes. Only: the strings and reeds rest, so it plays just the tone.
    bool toneOn = false;
    double toneHz = 528.0;
    int toneFit = 0;
    int toneSound = 0;
    float toneBeat = 0.0f;
    float toneLevel = 0.6f;
    float toneSwell = 0.3f;
    bool toneOnly = false;
};

enum Order { orderClassic, orderReverse, orderWander, orderPairs, orderRoll, numOrders };
enum Voice { voiceTanpura, voiceBowed, voiceSwarmandal, voiceElectric, voiceBell, numVoices };
inline const char* orderName (int o)
{
    static const char* names[] = { "Classic", "Reverse", "Wander", "Pairs", "Roll" };
    return names[juce::jlimit (0, (int) numOrders - 1, o)];
}
inline const char* voiceName (int v)
{
    static const char* names[] = { "Tanpura", "Bowed", "Swarmandal", "Electric", "Bell" };
    return names[juce::jlimit (0, (int) numVoices - 1, v)];
}

// A first pattern for the steps, so picking 8 or 16 steps plays at once: the four strings in turn, a rest between.
inline std::array<std::uint8_t, 16> starterSteps()
{
    std::array<std::uint8_t, 16> steps {};
    for (int i = 0; i < 16; i += 2)
        steps[(size_t) i] = (std::uint8_t) (1 << ((i / 2) % 4));
    return steps;
}

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
    int voice = 0, order = 0;
    float human = 0.0f, bright = 0.5f, body = 0.5f, detune = 0.0f, space = 0.0f;
};

inline const std::array<Preset, 15>& presets()
{
    //                                       first oct sync pace   jawari decay width reeds strings voice order human bright body detune space
    static const std::array<Preset, 15> list { {
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
        { "Wandering tanpura",   0,  0, 0, 1.00f, 0.65f, 0.55f, 0.75f, 0.00f, 1.0f, 0, 2, 0.6f, 0.5f, 0.5f, 0.10f, 0.20f },
        { "Bowed drone",         0,  0, 0, 2.60f, 0.30f, 0.70f, 0.80f, 0.00f, 1.0f, 1, 0, 0.2f, 0.45f, 0.45f, 0.15f, 0.25f },
        { "Swarmandal shimmer",  3,  0, 0, 1.60f, 0.75f, 0.50f, 0.90f, 0.00f, 1.0f, 2, 4, 0.3f, 0.6f, 0.7f, 0.10f, 0.35f },
        { "Electric bed",        0, -1, 0, 3.00f, 0.00f, 0.80f, 0.90f, 0.20f, 1.0f, 3, 3, 0.0f, 0.5f, 0.6f, 0.35f, 0.30f },
        { "Temple bells",        0,  0, 0, 1.80f, 0.00f, 0.60f, 0.85f, 0.00f, 1.0f, 4, 2, 0.4f, 0.5f, 0.5f, 0.00f, 0.45f },
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

// The miracle tones, the same on the Mac and the phone. Their names and meanings are tradition and belief
// (Solfeggio, sound healing), shown as that; the frequencies themselves are exact. The last choice is your own Hz.
struct Tone
{
    double hz;
    const char* name;
    const char* meaning;
};

constexpr int kNumTones = 12;   // the eleven below and Your own
constexpr int kOwnTone = 11;
inline const std::array<Tone, 11>& tones()
{
    static const std::array<Tone, 11> list { {
        { 174.0, "FOUNDATION", "Grounding and ease, in sound-healing tradition." },
        { 285.0, "RESTORE", "Renewal of the body, in sound-healing tradition." },
        { 396.0, "UT - RELEASE", "Letting go of guilt and fear (Solfeggio UT)." },
        { 417.0, "RE - CHANGE", "Undoing what is stuck, making way for change (RE)." },
        { 432.0, "EARTH A", "The A of 'natural' A = 432 tuning, said to feel warmer." },
        { 528.0, "MI - MIRACLE", "Transformation and love. The one called the miracle tone (MI)." },
        { 639.0, "FA - CONNECT", "Relationships and harmony between people (FA)." },
        { 741.0, "SOL - AWAKEN", "Intuition and finding your voice (SOL)." },
        { 852.0, "LA - SPIRIT", "Returning to spiritual order (LA)." },
        { 963.0, "SI - CROWN", "Oneness and light, the crown (SI)." },
        { 136.1, "OM", "The Earth-year tone, used to tune gongs and bowls for Om." },
    } };
    return list;
}

constexpr double kOwnToneMin = 20.0, kOwnToneMax = 2000.0;
constexpr int kNumToneBeats = 4;
inline float toneBeatHz (int choice)
{
    static const float beats[] = { 0.0f, 2.0f, 6.0f, 10.0f };
    return beats[juce::jlimit (0, kNumToneBeats - 1, choice)];
}
inline const char* toneBeatName (int choice)
{
    static const char* names[] = { "Off: both ears hear the same tone.", "Delta, deep rest.", "Theta, dreamy and meditative.", "Alpha, calm and awake." };
    return names[juce::jlimit (0, kNumToneBeats - 1, choice)];
}
inline const char* toneSoundName (int sound)
{
    static const char* names[] = { "PURE", "WARM", "BOWL" };
    return names[juce::jlimit (0, 2, sound)];
}

// The tone `index` plays (Your own: `ownHz`).
inline double toneHzFor (int index, double ownHz)
{
    if (index >= 0 && index < (int) tones().size())
        return tones()[(size_t) index].hz;
    return juce::jlimit (kOwnToneMin, kOwnToneMax, ownHz);
}
inline juce::String toneHzText (double hz)
{
    return std::abs (hz - std::round (hz)) < 0.05 ? juce::String (juce::roundToInt (hz)) : juce::String (hz, 1);
}
// The nearest note and how far off it is: "C +16 cents".
inline juce::String nearestNoteText (double hz)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double midi = 69.0 + 12.0 * std::log2 (juce::jmax (1.0, hz) / 440.0);
    const int note = (int) std::lround (midi);
    const int cents = (int) std::lround ((midi - note) * 100.0);
    return juce::String (names[((note % 12) + 12) % 12]) + " " + (cents >= 0 ? "+" : "-") + juce::String (std::abs (cents)) + " cents";
}

// The middle strings' note (Sa), in Hz.
inline double rootHz (int root, int octave, float fineCents)
{
    return 130.8127826502993 * std::pow (2.0, (juce::jlimit (0, 11, root) + fineCents / 100.0) / 12.0 + juce::jlimit (-1, 1, octave));
}

// Fit "Tune drone to it": the drone's Sa is the tone, folded down (or up) by octaves into the octave the
// drone plays in (528 Hz in the middle octave: 132 Hz).
inline double toneDroneSa (double toneHz, int octave)
{
    const double lo = rootHz (0, octave, 0.0f);
    double hz = juce::jmax (1.0, toneHz);
    while (hz >= lo * 2.0) hz *= 0.5;
    while (hz < lo) hz *= 2.0;
    return hz;
}

// The Sa the strings play now (the tone's, when they're tuned to it).
inline double droneSa (const Settings& s)
{
    if (s.toneOn && s.toneFit == 1 && s.toneHz > 0)
        return toneDroneSa (s.toneHz, s.octave);
    return rootHz (s.root, s.octave, s.fineCents);
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
        reverb.setSampleRate (sampleRate);
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
        numEvents = 0;
        stepIndex = -1;
        controlPhase = 0;
        reverb.reset();
        reverbTail = 0;
        reverbSpace = -1.0f;
        toneGain = 0;
        toneHzNow = 0;
        toneSwellPhase = 0;
        tonePhases = {};
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
        const double sa = droneSa (s);
        silentStrings = s.toneOn && s.toneOnly;
        if (s.on && ! wasOn)
        {
            untilPluck = 0;
            nextString = 0;
            stepIndex = -1;
        }
        wasOn = s.on;

        // Where the pattern's ticks fall in this block, and the plucks they make. A roll, swing and a player's
        // drift put some plucks a little later: they wait in `events` (into the next block if need be).
        if (s.on)
        {
            const bool stepping = s.stepCount > 0;
            const double beats = stepping ? (double) juce::jlimit (0.125f, 2.0f, s.stepBeats)
                                          : s.sync == 1 ? 1.0 : s.sync == 2 ? 2.0 : s.sync == 3 ? clock.beatsPerBar : 0.0;
            if (beats > 0 && clock.playing && clock.bpm > 1)
            {
                const double perSample = clock.bpm / (60.0 * sampleRate);
                const double end = clock.ppq + numSamples * perSample;
                double next = std::ceil (clock.ppq / beats - 1.0e-9) * beats;
                if (lastPpq < 0 || clock.ppq < lastPpq - 1.0e-6)
                    lastPluckPpq = -1.0e9;   // (the clock started or jumped)
                const int tickSamples = (int) (beats * 60.0 / clock.bpm * sampleRate);
                for (int ticks = 0; next < end && ticks < 8; next += beats, ++ticks)
                    if (next > lastPluckPpq + beats * 0.5)   // (never the same beat twice)
                    {
                        tick (juce::jlimit (0, numSamples - 1, (int) ((next - clock.ppq) / perSample)), (long long) std::llround (next / beats), s, tickSamples);
                        lastPluckPpq = next;
                    }
                lastPpq = end;
                untilPluck = tickSamples;   // (if the clock stops, it carries on)
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
                for (int ticks = 0; at < numSamples && ticks < 8; ++ticks)
                {
                    tick (at, -1, s, interval);
                    at += interval;
                }
                untilPluck = juce::jmax (0, at - numSamples);
            }
            if (! stepping)
                currentStep.store (-1);
        }
        else
        {
            numEvents = 0;
            currentStep.store (-1);
        }
        const int byHand = pendingPluck.exchange (-1);

        const float reedTarget = s.on && ! silentStrings ? juce::jlimit (0.0f, 1.0f, s.reeds) : 0.0f;
        const float resTarget = juce::jlimit (0.0f, 1.0f, s.resonate);
        const float stringLevel = juce::jlimit (0.0f, 1.5f, s.strings);
        tuneCombs (sa, s.first, resTarget);
        prepareTone (s);

        int done = 0;
        if (byHand >= 0)
            startPluck (byHand, sa, s, 1.0f);
        while (done < numSamples)
        {
            int nextEvent = numSamples;
            for (int e = 0; e < numEvents;)
            {
                auto& ev = events[(size_t) e];
                if (ev.at <= done)
                {
                    for (int k = 0; k < kStrings; ++k)
                        if ((ev.mask >> k) & 1)
                            startPluck (k, sa, s, ev.velocity);
                    events[(size_t) e] = events[(size_t) --numEvents];
                    continue;
                }
                nextEvent = juce::jmin (nextEvent, ev.at);
                ++e;
            }
            int n = juce::jmin (numSamples - done, kControl - controlPhase);
            n = juce::jmin (n, juce::jmax (1, nextEvent - done));
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
                // The miracle tone.
                if (toneSounding)
                    renderTone (l, r);
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
        for (int e = 0; e < numEvents; ++e)
            events[(size_t) e].at -= numSamples;

        // Space: its own reverb over what it played (left and right hold only the drone: both callers clear them).
        const float space = juce::jlimit (0.0f, 1.0f, s.space);
        if (space > 0.001f)
            reverbTail = (int) (6.0 * sampleRate);
        if (reverbTail > 0)
        {
            if (std::abs (space - reverbSpace) > 1.0e-3f)
            {
                reverbSpace = space;
                juce::Reverb::Parameters p;
                p.roomSize = 0.86f;
                p.damping = 0.45f;
                p.wetLevel = 0.3f * space;   // (the reverb doubles its dry and triples its wet inside)
                p.dryLevel = 0.5f * (1.0f - 0.5f * space);
                p.width = 1.0f;
                reverb.setParameters (p);
            }
            reverb.processStereo (left, right, numSamples);
            reverbTail -= numSamples;
            if (space <= 0.001f && reverbTail <= 0)
                reverb.reset();
        }
    }

    // The step the pattern is on (0-15), or -1 while Steps is off, for the screen.
    std::atomic<int> currentStep { -1 };

    // Still making sound (after Off, the strings ring out).
    bool isSounding() const { return anySounding || reverbTail > 0 || toneSounding; }
    // The tone is playing (for the screen).
    bool isToneSounding() const { return toneSounding; }

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
        int voice = 0;
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

    // One tick of the pattern at sample `at` of this block: the plucks it makes, by Steps or by Order.
    // `index`: the tick's number counted from the song's start (-1 off the clock: the next step).
    void tick (int at, long long index, const Settings& s, int tickSamples)
    {
        const float human = juce::jlimit (0.0f, 1.0f, s.human);
        auto push = [this] (int when, int mask, float velocity)
        {
            if (mask != 0 && numEvents < (int) events.size())
                events[(size_t) numEvents++] = { when, (std::uint8_t) mask, velocity };
        };
        auto late = [&] { return (int) (random.nextFloat() * human * 0.028f * (float) sampleRate); };
        auto velocity = [&] { return (0.86f + 0.14f * random.nextFloat()) * (1.0f + human * 0.5f * (random.nextFloat() - 0.5f)); };
        if (s.stepCount > 0)
        {
            const int count = juce::jlimit (1, 16, s.stepCount);
            stepIndex = index >= 0 ? (int) (index % count) : (stepIndex + 1) % count;
            currentStep.store (stepIndex);
            const int swing = (stepIndex % 2) == 1 ? (int) (juce::jlimit (0.0f, 1.0f, s.swing) * (float) tickSamples / 3.0f) : 0;
            const int mask = s.steps[(size_t) stepIndex] & 0x0f;
            const int many = (mask & 1) + ((mask >> 1) & 1) + ((mask >> 2) & 1) + ((mask >> 3) & 1);
            push (at + swing + late(), mask, velocity() * (many > 1 ? 0.8f : 1.0f));
            return;
        }
        switch (s.order)
        {
            case orderReverse:
                push (at + late(), 1 << (kStrings - 1 - nextString), velocity());
                nextString = (nextString + 1) % kStrings;
                break;
            case orderWander:
            {
                // Any string but the last one, Sa most often.
                static const float weight[] = { 1.0f, 1.6f, 1.6f, 1.2f };
                float total = 0;
                for (int k = 0; k < kStrings; ++k)
                    total += k == lastString ? 0.0f : weight[k];
                float pick = random.nextFloat() * total;
                int string = 0;
                for (int k = 0; k < kStrings; ++k)
                {
                    if (k == lastString)
                        continue;
                    string = k;
                    if ((pick -= weight[k]) <= 0)
                        break;
                }
                lastString = string;
                push (at + late(), 1 << string, velocity());
                break;
            }
            case orderPairs:   // the first string with the low Sa, then the two middle ones
                push (at + late(), (nextString % 2) == 0 ? 0x9 : 0x6, velocity() * 0.8f);
                nextString = (nextString + 1) % kStrings;
                break;
            case orderRoll:
            {
                const int gap = (int) (0.05 * sampleRate);
                for (int k = 0; k < kStrings; ++k)
                    push (at + k * gap + late() / 2, 1 << k, velocity() * (k == 0 ? 0.62f : 0.53f));
                break;
            }
            default:
                push (at + late(), 1 << nextString, velocity());
                nextString = (nextString + 1) % kStrings;
                break;
        }
    }

    void startPluck (int string, double sa, const Settings& s, float velocity)
    {
        if (silentStrings)
            return;   // (Tone only: the strings rest)
        const int first = s.first;
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
        slot.voice = juce::jlimit (0, (int) numVoices - 1, s.voice);
        // The two middle strings sit a hair apart, and no two plucks are quite the same (Detune: much more).
        static const double apartCents[] = { 0.0, -0.9, 0.9, 0.0 };
        const double spread = 1.0 + 8.0 * juce::jlimit (0.0f, 1.0f, s.detune);
        const double cents = apartCents[string] * spread + (random.nextDouble() - 0.5) * 0.8 * spread * (string == 0 || string == 3 ? 0.6 : 1.0);
        double hz = sa * stringRatio (string, first) * std::pow (2.0, cents / 1200.0);
        if (slot.voice == voiceSwarmandal)
            hz *= 2.0;   // a harp-zither's strings sit an octave up
        // How far a string's harmonics run sharp (a real string's do a little; a bow and a synth lock them).
        const double stretch = slot.voice == voiceSwarmandal ? 4.0e-5 : slot.voice == voiceTanpura ? 1.2e-5 : 0.0;
        // A bell's partials aren't harmonics: these are a cast bell's, against its hum note.
        static const double bellRatios[] = { 0.5, 1.0, 1.183, 1.506, 2.0, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215 };
        const int maxPartials = slot.voice == voiceBell ? (int) std::size (bellRatios) : kHarmonics;
        const double top = juce::jmin (11000.0, sampleRate * 0.45);
        slot.count = 0;
        for (int k = 1; k <= maxPartials; ++k)
        {
            const double f = slot.voice == voiceBell ? hz * 2.0 * bellRatios[k - 1] : hz * k * (1.0 + stretch * k * k);
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

    // How each voice is made from the same harmonics.
    struct VoiceShape
    {
        float attack, ring, ringFall, tilt, tiltJawari, bloom, bodyJawari, handOver, trim;
    };
    static const VoiceShape& voiceShape (int voice)
    {
        //                                attack  ring   ringFall tilt  tiltJ  bloom bodyJ handOver trim
        static const VoiceShape shapes[] = { { 0.016f, 1.0f,  0.045f,  1.4f,  0.95f, 1.0f, 0.45f, 0.10f, 1.0f },    // Tanpura
                                             { 0.45f,  6.0f,  0.010f,  1.65f, 0.6f,  0.2f, 0.2f,  0.80f, 0.58f },   // Bowed: held, no pluck
                                             { 0.004f, 0.55f, 0.030f,  0.95f, 0.3f,  0.0f, 0.0f,  0.30f, 0.99f },   // Swarmandal: bright, an octave up
                                             { 0.08f,  6.0f,  0.0f,    0.9f,  0.0f,  0.0f, 0.0f,  0.60f, 0.40f },   // Electric: a filtered saw, held
                                             { 0.003f, 0.9f,  0.12f,   0.35f, 0.0f,  0.0f, 0.0f,  0.60f, 0.66f } };  // Bell: glassy, inharmonic
        return shapes[juce::jlimit (0, (int) numVoices - 1, voice)];
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
        const float brightTilt = 0.9f * (juce::jlimit (0.0f, 1.0f, s.bright) - 0.5f);   // Bright: the harmonics' slope
        const float bodyGain = 0.4f + 1.2f * juce::jlimit (0.0f, 1.0f, s.body);           // Body: the bottom two
        const float brightBy = juce::jlimit (0.0f, 1.0f, s.bright) - 0.5f;
        const float brightTrim = std::pow (10.0f, -(brightBy > 0 ? 17.0f : 10.0f) * brightBy / 20.0f);
        const float block = (float) kControl / (float) sampleRate;
        // As loud whatever the settings: more buzz, quicker plucks and the high octave all add up to more.
        const float jawariDb = -16.0f * std::pow (jawari, 2.9f);   // (as much as each instrument buzzes: below)
        const float evenDb = juce::jlimit (-6.0f, 5.0f, 8.0f * std::log10 (pluckSeconds / 1.1f)) - (s.octave > 0 ? 2.5f : 0.0f) - 0.1f;
        const float even = std::pow (10.0f, evenDb / 20.0f);
        bool any = false;
        std::array<float, kStrings> ring {};
        for (auto& slot : slots)
        {
            if (! slot.active)
                continue;
            const auto& v = voiceShape (slot.voice);
            const float tilt = v.tilt - v.tiltJawari * jawari - brightTilt;   // the buzz keeps the high harmonics up
            const float bloom = v.bloom * (10.0f * jawari * jawari + 2.0f * jawari);
            const float body = (1.0f - v.bodyJawari * jawari) * bodyGain;     // ...and takes it out of the bottom two
            // (Bright keeps its loudness: a brighter slope adds up to more, so it comes down to match.)
            const float level = even * std::pow (10.0f, jawariDb * v.bloom / 20.0f) * v.trim * brightTrim;
            const float t = (float) slot.age / (float) sampleRate + block;
            // The band the jawari throws the sound into: high at the pluck, falling through the harmonics.
            const float centre = 520.0f + 3900.0f * std::exp (-t / (0.55f + 0.5f * jawari));
            // Then it comes round again, lower and softer, as the string keeps grazing the bridge.
            const float again = 600.0f + 1500.0f * (0.5f + 0.5f * std::cos (t * 2.1f));
            const float attack = 1.0f - std::exp (-t / v.attack);
            float fade = 1.0f;
            if (slot.fading)
            {
                slot.fadeAge += kControl;
                fade = std::exp (-(float) slot.fadeAge / (float) sampleRate / (s.on ? v.handOver : juce::jmax (0.35f, v.handOver)));
            }
            else if (! s.on)
            {
                slot.fading = true;
                slot.fadeAge = 0;
            }
            // Bowed: the bow's slow swell; Electric: a slow filter sweep, each string its own.
            const float tremolo = slot.voice == voiceBowed ? 1.0f + 0.05f * std::sin (t * 32.0f + (float) slot.string * 1.7f) : 1.0f;
            const float cutoff = slot.voice == voiceElectric
                                     ? (300.0f + 2600.0f * (0.5f + 0.5f * std::sin (t * 0.45f + (float) slot.string * 1.3f))) * (0.45f + 1.1f * juce::jlimit (0.0f, 1.0f, s.bright))
                                     : 0.0f;
            float loudest = 0;
            for (int k = 0; k < slot.count; ++k)
            {
                auto& p = slot.partials[(size_t) k];
                const float n = (float) (k + 1);
                const float ring60 = t60 * v.ring / (1.0f + v.ringFall * n);
                const float env = std::exp (-6.91f * t / ring60);
                const float d1 = std::log2 (p.hz / centre) / 0.42f, d2 = std::log2 (p.hz / again) / 0.5f;
                float shape = 1.0f + bloom * std::exp (-0.5f * d1 * d1) + 0.35f * bloom * std::exp (-0.5f * d2 * d2) * (1.0f - std::exp (-t / 0.8f));
                if (cutoff > 0)
                {
                    const float x = p.hz / cutoff;
                    shape /= 1.0f + x * x * x * x;
                }
                const float slope = slot.voice == voiceBell ? std::pow (n, -0.35f - brightTilt) : std::pow (n, -tilt);
                const float target = (k < 2 ? body : 1.0f) * level * slot.velocity * attack * fade * env * shape * tremolo * slope * 0.5f;
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

    // The miracle tone. Its partials: frequency ratio to the tone and level (a bowl's pairs sit a hair apart and beat).
    static constexpr int kTonePartials = 8;
    struct TonePartial { double ratio; float amp; };

    void prepareTone (const Settings& s)
    {
        const bool want = s.on && s.toneOn && s.toneHz > 0;
        const double hz = juce::jlimit (8.0, 4000.0, s.toneHz);
        const int sound = juce::jlimit (0, 2, s.toneSound);
        // A new tone or sound: fade the old one out quickly, then start the new one from silence (no click).
        const bool changed = std::abs (hz - toneHzNow) > 1.0e-6 || sound != toneSoundNow;
        if (changed && toneGain < 1.0e-3f)
        {
            toneHzNow = hz;
            toneSoundNow = sound;
            tonePhases = {};
            static const TonePartial pure[] = { { 1.0, 1.0f } };
            static const TonePartial warm[] = { { 0.5, 0.45f }, { 1.0, 1.0f }, { 2.0, 0.16f }, { 3.0, 0.05f } };
            static const TonePartial bowl[] = { { 1.0, 0.5f }, { 1.0016, 0.5f }, { 2.76, 0.25f }, { 2.7652, 0.25f },
                                                { 5.40, 0.12f }, { 5.4108, 0.12f }, { 8.93, 0.05f }, { 8.948, 0.05f } };
            const TonePartial* list = sound == 2 ? bowl : sound == 1 ? warm : pure;
            toneCount = sound == 2 ? 8 : sound == 1 ? 4 : 1;
            float power = 0;   // (each sound as loud as a pure sine: the same power)
            for (int k = 0; k < toneCount; ++k)
                power += list[k].amp * list[k].amp;
            const float norm = 1.0f / std::sqrt (power);
            for (int k = 0; k < toneCount; ++k)
                toneParts[(size_t) k] = { list[k].ratio, list[k].amp * norm };
        }
        toneTarget = want && ! changed ? juce::jlimit (0.0f, 1.0f, s.toneLevel) : 0.0f;
        toneCoef = changed ? (float) (1.0 - std::exp (-1.0 / (0.012 * sampleRate)))    // (out quickly, for the next)
                           : (float) (1.0 - std::exp (-1.0 / ((toneTarget > 0 ? 0.35 : 0.2) * sampleRate)));   // (in and out gently)
        toneBeatNow = juce::jlimit (0.0f, 40.0f, s.toneBeat);
        const float swell = juce::jlimit (0.0f, 1.0f, s.toneSwell);
        toneSwellDepth = 0.6f * swell;
        toneSwellRate = 0.22 - 0.16 * swell;   // deeper breathes slower
        if (toneTarget <= 0 && toneGain < 1.0e-4f)
            toneGain = 0;   // (faded out: about -90 dB)
        toneSounding = toneTarget > 0 || toneGain > 0;
    }

    void renderTone (float& l, float& r)
    {
        toneGain += (toneTarget - toneGain) * toneCoef;
        toneSwellPhase += toneSwellRate / sampleRate;
        if (toneSwellPhase >= 1.0)
            toneSwellPhase -= 1.0;
        const float breathe = 1.0f - toneSwellDepth * 0.5f * (1.0f - (float) std::cos (juce::MathConstants<double>::twoPi * toneSwellPhase));
        const double incL = toneHzNow / sampleRate, incR = (toneHzNow + toneBeatNow) / sampleRate;
        float sl = 0, sr = 0;
        for (int k = 0; k < toneCount; ++k)
        {
            const auto& p = toneParts[(size_t) k];
            auto& ph = tonePhases[(size_t) k];
            ph[0] += incL * p.ratio;
            ph[1] += incR * p.ratio;
            ph[0] -= std::floor (ph[0]);
            ph[1] -= std::floor (ph[1]);
            sl += p.amp * (float) std::sin (juce::MathConstants<double>::twoPi * ph[0]);
            sr += p.amp * (float) std::sin (juce::MathConstants<double>::twoPi * ph[1]);
        }
        const float g = kToneOut * toneGain * breathe;
        l += sl * g;
        r += sr * g;
    }

    static constexpr float kToneOut = 0.4f;   // (level 60%: about as loud as the strings' presets)
    std::array<TonePartial, kTonePartials> toneParts {};
    std::array<std::array<double, 2>, kTonePartials> tonePhases {};
    int toneCount = 1, toneSoundNow = -1;
    double toneHzNow = 0, toneSwellPhase = 0, toneSwellRate = 0.2;
    float toneGain = 0, toneTarget = 0, toneCoef = 0.001f, toneBeatNow = 0, toneSwellDepth = 0;
    bool toneSounding = false, silentStrings = false;

    double sampleRate = 48000.0;
    std::array<Slot, kStrings * 2> slots;
    std::array<ReedBank, kReedNotes> reedBank;
    std::array<Comb, kStrings> combs;
    juce::Random random { 0x7a9b };
    std::atomic<int> pendingPluck { -1 };
    juce::Reverb reverb;
    int reverbTail = 0;
    float reverbSpace = -1.0f;
    struct Event { int at = 0; std::uint8_t mask = 0; float velocity = 1; };
    std::array<Event, 32> events {};
    int numEvents = 0, stepIndex = -1, lastString = -1;
    int untilPluck = 0, nextString = 0, controlPhase = 0;
    float pluckSeconds = 1.1f, reedGain = 0, resonateGain = 0, combFeed = 0.1f, resonateSmooth = 0.001f, dcL = 0, dcR = 0;
    double reedTime = 0, lastPpq = -1, lastPluckPpq = -1.0e9;
    bool wasOn = false, reedsSounding = false, combsSounding = false, anySounding = false;
};
}
