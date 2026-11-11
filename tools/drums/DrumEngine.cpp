#include "DrumEngine.h"

#include <cmath>

namespace crate::drums
{
namespace
{
constexpr float kTwoPi = 6.283185307f;

// The sounds' semitone in the octave from C: kick C, snare D, clap D#, rim C#, low tom F,
// mid tom G, high tom A, clave E, cowbell G#, closed hat F#, open hat A#, cymbal B.
constexpr int kSemitone[kVoices] = { 0, 2, 3, 1, 5, 7, 9, 4, 8, 6, 10, 11 };

// Seconds to fade away (60 dB) at decay 0 and at decay 1.
constexpr float kDecayRange[kVoices][2] = {
    { 0.12f, 2.4f },   // kick: a tight thump up to a long boom
    { 0.06f, 0.6f },   // snare (its noise)
    { 0.08f, 0.9f },   // clap tail
    { 0.012f, 0.07f }, // rim
    { 0.12f, 1.3f },   // low tom
    { 0.10f, 1.1f },   // mid tom
    { 0.08f, 0.9f },   // high tom
    { 0.015f, 0.14f }, // clave
    { 0.06f, 0.9f },   // cowbell
    { 0.02f, 0.25f },  // closed hat
    { 0.12f, 1.6f },   // open hat
    { 0.3f, 3.5f },    // cymbal
};

// The six square-wave oscillators of the classic metallic sound (hats and cymbal), in Hz.
constexpr float kMetal[6] = { 205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f };

float decaySeconds (int voice, float d)
{
    const float lo = kDecayRange[voice][0], hi = kDecayRange[voice][1];
    return lo * std::pow (hi / lo, juce::jlimit (0.0f, 1.0f, d));
}

float coeffFor (float seconds, float invRate)
{
    // Down 60 dB over `seconds`.
    return std::exp (-6.9f * invRate / juce::jmax (0.0005f, seconds));
}

inline float square (double phase) { return phase < 0.5 ? 1.0f : -1.0f; }
inline float sine (double phase) { return std::sin (kTwoPi * (float) phase); }
inline void advance (double& phase, float hz, float invRate)
{
    phase += hz * invRate;
    phase -= std::floor (phase);
}
}

const char* voiceName (int v)
{
    static const char* names[kVoices] = { "Kick", "Snare", "Clap", "Rim shot", "Low tom", "Mid tom", "High tom",
                                          "Clave", "Cowbell", "Closed hat", "Open hat", "Cymbal" };
    return juce::isPositiveAndBelow (v, kVoices) ? names[v] : "";
}

const char* voiceShort (int v)
{
    static const char* names[kVoices] = { "BD", "SD", "CP", "RS", "LT", "MT", "HT", "CL", "CB", "CH", "OH", "CY" };
    return juce::isPositiveAndBelow (v, kVoices) ? names[v] : "";
}

int noteForVoice (int v) { return juce::isPositiveAndBelow (v, kVoices) ? 36 + kSemitone[v] : -1; }

int voiceForNote (int note)
{
    if (! juce::isPositiveAndBelow (note, 128))
        return -1;
    const int semi = note % 12;
    for (int v = 0; v < kVoices; ++v)
        if (kSemitone[v] == semi)
            return v;
    return -1;
}

bool Pattern::isEmpty() const
{
    for (const auto& row : hits)
        for (auto h : row)
            if (h != 0)
                return false;
    return true;
}

bool Pattern::operator== (const Pattern& o) const { return hits == o.hits && accent == o.accent && length == o.length; }

double stepBeats (int rate)
{
    static constexpr double values[] = { 0.5, 0.25, 1.0 / 6.0, 0.125 };
    return values[juce::jlimit (0, 3, rate)];
}

juce::StringArray stepRateNames() { return { "1/8", "1/16", "1/16 T", "1/32" }; }

//==============================================================================
void Engine::Svf::set (float hz, float q, float rate)
{
    hz = juce::jlimit (20.0f, rate * 0.45f, hz);
    g = std::tan (juce::MathConstants<float>::pi * hz / rate);
    k = 1.0f / juce::jmax (0.3f, q);
}

void Engine::Svf::process (float x, float& lp, float& bp, float& hp)
{
    const float a1 = 1.0f / (1.0f + g * (g + k));
    const float v3 = x - ic2;
    const float v1 = a1 * ic1 + g * a1 * v3;
    const float v2 = ic2 + g * v1;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    lp = v2;
    bp = v1;
    hp = x - k * v1 - v2;
}

Engine::Engine()
{
    for (auto& h : hitCounts)
        h = 0;
}

void Engine::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
    invRate = (float) (1.0 / sampleRate);
    reset();
}

void Engine::reset()
{
    for (auto& s : state)
        s = VoiceState {};
    rolls = {};
    lastFired = -1;
    lastLoop = -1;
    currentStep = -1;
    inFill = false;
    fillQueued = false;
    wasPlaying = false;
    driveState = 0;
}

float Engine::noise()
{
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return (float) (int) seed * (1.0f / 2147483648.0f);
}

void Engine::tap (int voice, float velocity)
{
    if (noteTap == nullptr)
        return;
    const auto v = (juce::uint8) juce::jlimit (1, 127, juce::roundToInt (velocity * 127.0f));
    noteTap->addEvent (juce::MidiMessage::noteOn (10, noteForVoice (voice), v), tapSample);
    noteTap->addEvent (juce::MidiMessage::noteOff (10, noteForVoice (voice)), tapSample);
}

void Engine::trigger (int voice, float velocity, bool accent) { start (voice, velocity, accent); }

void Engine::start (int v, float velocity, bool accent)
{
    if (! juce::isPositiveAndBelow (v, kVoices))
        return;
    const auto& vs = settings.voices[(size_t) v];
    auto& s = state[(size_t) v];
    const float ratio = std::exp2 (juce::jlimit (-24.0f, 24.0f, vs.tune) / 12.0f);
    const float rate = (float) sampleRate;
    const float tone = juce::jlimit (0.0f, 1.0f, vs.tone);
    const float decay = decaySeconds (v, vs.decay);

    float human = 1.0f;
    if (settings.humanize > 0.0f)
        human = 1.0f - settings.humanize * 0.35f * (0.5f + 0.5f * noise());
    s.gain = juce::jlimit (0.0f, 1.0f, velocity) * vs.level * human * (accent ? 1.0f + 0.9f * settings.accent : 1.0f);
    s.active = true;
    s.t = 0;
    s.env = 1.0f;
    s.envCoeff = coeffFor (decay, invRate);
    s.env2 = 1.0f;
    s.env2Coeff = 0;
    s.clickEnv = 1.0f;
    for (auto& p : s.phase)
        p = 0;

    switch (v)
    {
        case kick:
            s.pitch = 49.0f * ratio;
            s.env2Coeff = coeffFor (0.09f + 0.05f * (1.0f - tone), invRate);   // the pitch drop
            break;
        case snare:
            s.pitch = 182.0f * ratio;
            s.env2Coeff = coeffFor (0.12f + 0.25f * vs.decay, invRate);           // the drum's body
            s.f1.set (1500.0f + 2500.0f * tone, 0.7f, rate);
            break;
        case clap:
            s.f1.set (1100.0f * ratio * (0.7f + 0.6f * tone), 1.6f, rate);
            break;
        case rim:
            s.pitch = 1720.0f * ratio;
            s.f1.set (380.0f, 0.7f, rate);
            break;
        case lowTom: case midTom: case highTom:
            s.pitch = (v == lowTom ? 86.0f : v == midTom ? 122.0f : 168.0f) * ratio;
            s.env2Coeff = coeffFor (0.25f, invRate);
            s.f1.set (2500.0f, 0.8f, rate);
            break;
        case clave:
            s.pitch = 2480.0f * ratio;
            break;
        case cowbell:
            s.freqs[0] = 540.0f * ratio;
            s.freqs[1] = 800.0f * ratio;
            s.env2Coeff = coeffFor (0.05f, invRate);
            s.f1.set (2640.0f * ratio * (0.75f + 0.5f * tone), 2.6f, rate);
            break;
        case closedHat: case openHat: case cymbal:
            for (int i = 0; i < 6; ++i)
                s.freqs[i] = kMetal[i] * ratio;
            if (v == cymbal)
            {
                s.f1.set (7000.0f * (0.7f + 0.6f * tone), 0.9f, rate);
                s.f2.set (3500.0f + 4000.0f * tone, 0.7f, rate);
            }
            else
            {
                s.f1.set (10000.0f * (0.8f + 0.3f * tone), 1.1f, rate);
                s.f2.set (5500.0f + 4500.0f * tone, 0.7f, rate);
            }
            // The closed hat cuts the open one off, as on the old machines.
            if (v == closedHat && state[openHat].active)
                state[openHat].envCoeff = coeffFor (0.012f, invRate);
            break;
        default:
            break;
    }
    ++hitCounts[(size_t) v];
    tap (v, velocity);
}

float Engine::renderVoice (int v, VoiceState& s)
{
    const auto& vs = settings.voices[(size_t) v];
    const float tone = juce::jlimit (0.0f, 1.0f, vs.tone);
    float out = 0, lp, bp, hp;
    float level = s.env;

    switch (v)
    {
        case kick:
        {
            // A sine that drops in pitch, with a click on top.
            const float hz = s.pitch * (1.0f + (1.2f + 2.4f * tone) * s.env2 * s.env2);
            advance (s.phase[0], hz, invRate);
            const float body = sine (s.phase[0]);
            const float click = s.clickEnv * (0.6f * noise() + 0.8f) * tone * 0.55f;
            s.clickEnv *= 0.985f;
            out = (body * s.env + click * s.clickEnv) * 1.05f;
            out = std::tanh (out * 1.4f) * 0.8f;
            s.env2 *= s.env2Coeff;
            break;
        }
        case snare:
        {
            advance (s.phase[0], s.pitch, invRate);
            advance (s.phase[1], s.pitch * 1.76f, invRate);
            const float body = (0.65f * sine (s.phase[0]) + 0.35f * sine (s.phase[1])) * s.env2 * s.env2;
            s.f1.process (noise(), lp, bp, hp);
            const float snap = hp * s.env;
            out = body * (0.9f - 0.45f * tone) + snap * (0.35f + 0.85f * tone);
            s.env2 *= s.env2Coeff;
            level = juce::jmax (s.env, s.env2);
            break;
        }
        case clap:
        {
            // A few quick bursts of noise, then the tail.
            float e = 0;
            if (s.t < 0.034f)
                e = std::exp (-std::fmod (s.t, 0.0108f) / 0.0028f);
            const float tail = s.t > 0.022f ? s.env : 0.0f;
            s.f1.process (noise(), lp, bp, hp);
            out = bp * juce::jmax (e, tail) * 2.4f;
            level = juce::jmax (e, s.env);
            break;
        }
        case rim:
        {
            advance (s.phase[0], s.pitch, invRate);
            advance (s.phase[1], s.pitch * 0.28f, invRate);
            const float x = 0.6f * square (s.phase[0]) * (0.4f + 0.6f * tone) + 0.7f * sine (s.phase[1]);
            s.f1.process (x, lp, bp, hp);
            out = hp * s.env * 0.75f;
            break;
        }
        case lowTom: case midTom: case highTom:
        {
            const float hz = s.pitch * (1.0f + 0.35f * s.env2);
            advance (s.phase[0], hz, invRate);
            s.f1.process (noise(), lp, bp, hp);
            const float skin = lp * s.clickEnv * tone * 0.5f;
            s.clickEnv *= 0.996f;
            out = sine (s.phase[0]) * s.env * 0.9f + skin;
            s.env2 *= s.env2Coeff;
            break;
        }
        case clave:
        {
            advance (s.phase[0], s.pitch, invRate);
            advance (s.phase[1], s.pitch * 1.58f, invRate);
            out = (sine (s.phase[0]) + tone * 0.5f * sine (s.phase[1])) * s.env * 0.7f;
            break;
        }
        case cowbell:
        {
            advance (s.phase[0], s.freqs[0], invRate);
            advance (s.phase[1], s.freqs[1], invRate);
            const float x = 0.5f * (square (s.phase[0]) + square (s.phase[1]));
            s.f1.process (x, lp, bp, hp);
            const float e = 0.6f * s.env2 + 0.4f * s.env;
            out = bp * e * 1.6f;
            s.env2 *= s.env2Coeff;
            level = e;
            break;
        }
        case closedHat: case openHat: case cymbal:
        {
            float x = 0;
            for (int i = 0; i < 6; ++i)
            {
                advance (s.phase[i], s.freqs[i], invRate);
                x += square (s.phase[i]);
            }
            x *= 1.0f / 6.0f;
            s.f1.process (x, lp, bp, hp);
            float lp2, bp2, hp2;
            s.f2.process (bp, lp2, bp2, hp2);
            const float e = v == cymbal ? s.env * (0.75f + 0.25f * s.clickEnv) : s.env;
            out = hp2 * e * (v == cymbal ? 1.6f : 1.9f);
            s.clickEnv *= 0.999f;
            break;
        }
        default:
            break;
    }

    s.env *= s.envCoeff;
    s.t += invRate;
    if (s.t > 0.05f && level < 1.0e-4f)
        s.active = false;
    return out * s.gain;
}

void Engine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        const int v = voiceForNote (m.getNoteNumber());
        if (v >= 0)
            start (v, m.getFloatVelocity(), m.getFloatVelocity() > 0.92f);
    }
    else if (m.isAllSoundOff() || m.isAllNotesOff())
    {
        for (auto& s : state)
            s.active = false;
        rolls = {};
    }
}

void Engine::hitStep (int step, const Pattern& p, double stepStart, double stepLength)
{
    const bool accent = p.accent[(size_t) step];
    for (int v = 0; v < kVoices; ++v)
    {
        const int h = p.hits[(size_t) v][(size_t) step];
        if (h == 0)
            continue;
        start (v, 1.0f, accent);
        if (h >= 2)
        {
            auto& r = rolls[(size_t) v];
            r.left = juce::jmin (3, h - 1);
            r.gap = stepLength / juce::jmin (4, h);
            r.next = stepStart + r.gap;
            r.velocity = 0.8f;
            r.accent = accent;
        }
        else
        {
            rolls[(size_t) v].left = 0;
        }
    }
}

void Engine::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const Clock& clock)
{
    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    const bool sequencing = clock.playing && settings.sequencerOn && pattern != nullptr;
    if (wasPlaying && ! sequencing)
    {
        currentStep = -1;
        lastFired = -1;
        lastLoop = -1;
        inFill = false;
        rolls = {};
    }
    wasPlaying = sequencing;

    const double sb = stepBeats (settings.stepRate);
    const double swingOffset = juce::jlimit (0.0f, 1.0f, settings.swing) * 0.5 * sb;
    double beat = clock.ppq;

    std::array<float, kVoices> panL {}, panR {};
    for (int v = 0; v < kVoices; ++v)
    {
        const float pan = juce::jlimit (-1.0f, 1.0f, settings.voices[(size_t) v].pan);
        panL[(size_t) v] = juce::jmin (1.0f, 1.0f - pan);
        panR[(size_t) v] = juce::jmin (1.0f, 1.0f + pan);
    }
    const float driveGain = 1.0f + 4.0f * juce::jlimit (0.0f, 1.0f, settings.drive);
    const float driveMakeUp = 1.0f / std::sqrt (driveGain);

    auto event = midi.begin();
    for (int i = 0; i < numSamples; ++i, beat += beatsPerSample)
    {
        tapSample = i;
        for (; event != midi.end() && (*event).samplePosition <= i; ++event)
            handleMidi ((*event).getMessage());

        if (sequencing)
        {
            const auto raw = (long long) std::floor (beat / sb + 1.0e-9);
            const double into = beat - (double) raw * sb;
            const long long index = ((raw & 1) != 0 && into < swingOffset) ? raw - 1 : raw;
            if (index != lastFired && index >= 0)
            {
                lastFired = index;
                const int length = juce::jlimit (1, kMaxSteps, pattern->length);
                const long long loop = index / length;
                const int s = (int) (index % length);
                if (s == 0 || loop != lastLoop)
                {
                    if (loop != lastLoop)
                    {
                        lastLoop = loop;
                        const bool every = settings.fillEvery > 1 && (loop % settings.fillEvery) == settings.fillEvery - 1;
                        const bool once = s == 0 && fillOnce.exchange (false);
                        inFill = fillPattern != nullptr && s == 0 && (every || once);
                    }
                }
                const Pattern& p = inFill ? *fillPattern : *pattern;
                const int ps = s % juce::jlimit (1, kMaxSteps, p.length);
                currentStep = s;
                const double start = (double) index * sb + ((index & 1) != 0 ? swingOffset : 0.0);
                hitStep (ps, p, start, (index & 1) != 0 ? sb - swingOffset : sb + swingOffset);
            }
            for (int v = 0; v < kVoices; ++v)
            {
                auto& r = rolls[(size_t) v];
                if (r.left > 0 && beat >= r.next)
                {
                    start (v, r.velocity, r.accent);
                    r.velocity *= 0.9f;
                    r.next += r.gap;
                    --r.left;
                }
            }
        }

        float l = 0, r = 0;
        for (int v = 0; v < kVoices; ++v)
        {
            auto& s = state[(size_t) v];
            if (! s.active)
                continue;
            const float x = renderVoice (v, s);
            l += x * panL[(size_t) v];
            r += x * panR[(size_t) v];
        }
        if (driveGain > 1.0f)
        {
            l = std::tanh (l * driveGain) * driveMakeUp;
            r = std::tanh (r * driveGain) * driveMakeUp;
        }
        l *= settings.volume;
        r *= settings.volume;
        if (right != nullptr)
        {
            left[i] += l;
            right[i] += r;
        }
        else
        {
            left[i] += 0.5f * (l + r);
        }
    }
    for (; event != midi.end(); ++event)
        handleMidi ((*event).getMessage());
}
}
