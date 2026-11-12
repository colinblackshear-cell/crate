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

juce::StringArray soundNames (int v)
{
    switch (v)
    {
        case kick:      return { "Classic", "Long boom", "Punch", "Distorted" };
        case snare:     return { "Classic", "Tight", "Fat" };
        case clap:      return { "Clap", "Maracas" };
        case rim:       return { "Rim shot", "Side stick" };
        case lowTom: case midTom: case highTom:
                        return { "Tom", "Conga" };
        case clave:     return { "Clave", "Woodblock" };
        case cowbell:   return { "Cowbell", "Agogo" };
        case closedHat: return { "Classic", "Tight", "Shaker" };
        case openHat:   return { "Classic", "Trashy" };
        case cymbal:    return { "Crash", "Ride" };
        default:        return { "Classic" };
    }
}

bool Plan::sameRun (const Plan& o) const
{
    if (pattern != o.pattern || variation != o.variation || chainOn != o.chainOn || chainLength != o.chainLength)
        return false;
    for (int i = 0; i < juce::jlimit (0, kChain, chainLength); ++i)
        if (chain[(size_t) i] != o.chain[(size_t) i])
            return false;
    return true;
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
    static constexpr double values[] = { 0.5, 0.25, 1.0 / 6.0, 0.125, 1.0 / 3.0 };
    return values[juce::jlimit (0, 4, rate)];
}

juce::StringArray stepRateNames() { return { "1/8", "1/16", "1/16 T", "1/32", "1/8 T" }; }

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
    loopCount = 0;
    current = nullptr;
    position = 0;
    currentStep = -1;
    currentCode = -1;
    chainPos = 0;
    inFill = inIntro = false;
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
    if (vs.mute)
        return;
    auto& s = state[(size_t) v];
    const float ratio = std::exp2 (juce::jlimit (-24.0f, 24.0f, vs.tune) / 12.0f);
    const float rate = (float) sampleRate;
    const float tone = juce::jlimit (0.0f, 1.0f, vs.tone);
    const int sound = juce::jlimit (0, soundNames (v).size() - 1, vs.sound);
    float decay = decaySeconds (v, vs.decay);

    float human = 1.0f;
    if (settings.humanize > 0.0f)
        human = 1.0f - settings.humanize * 0.35f * (0.5f + 0.5f * noise());
    s.gain = juce::jlimit (0.0f, 1.0f, velocity) * vs.level * human * (accent ? 1.0f + 0.9f * settings.accent : 1.0f);
    s.active = true;
    s.t = 0;
    s.env = 1.0f;
    s.env2 = 1.0f;
    s.env2Coeff = 0;
    s.clickEnv = 1.0f;
    s.sound = sound;
    for (auto& p : s.phase)
        p = 0;
    s.bell = 0;

    switch (v)
    {
        case kick:
        {
            // Classic, Long boom (the deep sub with a slow pitch glide), Punch, Distorted.
            static constexpr float pitch[] = { 49.0f, 43.0f, 58.0f, 49.0f };
            static constexpr float drop[] = { 0.09f, 0.2f, 0.035f, 0.09f };
            s.pitch = pitch[sound] * ratio;
            s.env2Coeff = coeffFor (drop[sound] + (sound == 1 ? 0.15f : 0.05f) * (1.0f - tone), invRate);   // the pitch drop
            if (sound == 1)
                decay *= 1.8f;
            break;
        }
        case snare:
        {
            // Classic, Tight, Fat.
            static constexpr float pitch[] = { 182.0f, 238.0f, 150.0f };
            static constexpr float body[] = { 1.0f, 0.6f, 1.5f };
            s.pitch = pitch[sound] * ratio;
            s.env2Coeff = coeffFor ((0.12f + 0.25f * vs.decay) * body[sound], invRate);   // the drum's body
            if (sound == 1)
                s.f1.set (2600.0f + 3500.0f * tone, 0.7f, rate);
            else if (sound == 2)
                s.f1.set (900.0f + 1800.0f * tone, 0.6f, rate);
            else
                s.f1.set (1500.0f + 2500.0f * tone, 0.7f, rate);
            break;
        }
        case clap:
            if (sound == 1)
            {
                // Maracas: a short hiss of high noise.
                decay *= 0.25f;
                s.f1.set (5500.0f + 4500.0f * tone, 0.7f, rate);
            }
            else
            {
                s.f1.set (1100.0f * ratio * (0.7f + 0.6f * tone), 1.6f, rate);
            }
            break;
        case rim:
            s.pitch = (sound == 1 ? 880.0f : 1720.0f) * ratio;
            s.f1.set (sound == 1 ? 1400.0f : 380.0f, 0.7f, rate);
            if (sound == 1)
                decay *= 1.6f;
            break;
        case lowTom: case midTom: case highTom:
        {
            const int i = v - lowTom;
            static constexpr float tom[] = { 86.0f, 122.0f, 168.0f };
            static constexpr float conga[] = { 165.0f, 247.0f, 370.0f };
            s.pitch = (sound == 1 ? conga[i] : tom[i]) * ratio;
            s.env2Coeff = coeffFor (sound == 1 ? 0.03f : 0.25f, invRate);
            s.f1.set (2500.0f, 0.8f, rate);
            if (sound == 1)
                decay *= 0.55f;
            break;
        }
        case clave:
            s.pitch = (sound == 1 ? 980.0f : 2480.0f) * ratio;
            if (sound == 1)
                decay *= 1.4f;
            break;
        case cowbell:
            s.freqs[0] = (sound == 1 ? 920.0f : 540.0f) * ratio;
            s.freqs[1] = (sound == 1 ? 1385.0f : 800.0f) * ratio;
            s.env2Coeff = coeffFor (0.05f, invRate);
            s.f1.set ((sound == 1 ? 1900.0f : 2640.0f) * ratio * (0.75f + 0.5f * tone), sound == 1 ? 1.4f : 2.6f, rate);
            break;
        case closedHat: case openHat: case cymbal:
        {
            const bool ride = v == cymbal && sound == 1;
            const bool trashy = v == openHat && sound == 1;
            for (int i = 0; i < 6; ++i)
                s.freqs[i] = kMetal[i] * ratio * (ride ? 1.45f : trashy ? (i % 2 == 0 ? 1.27f : 0.93f) : 1.0f);
            s.env2Coeff = coeffFor (0.004f, invRate);   // the tick at the start
            if (v == cymbal)
            {
                s.f1.set ((ride ? 9000.0f : 7000.0f) * (0.7f + 0.6f * tone), 0.9f, rate);
                s.f2.set ((ride ? 6500.0f : 3500.0f) + 4000.0f * tone, 0.7f, rate);
                if (ride)
                    decay *= 1.3f;
            }
            else if (v == closedHat && sound == 1)
            {
                // Tight: higher and shorter.
                s.f1.set (12500.0f * (0.85f + 0.2f * tone), 1.1f, rate);
                s.f2.set (8000.0f + 4000.0f * tone, 0.7f, rate);
                decay *= 0.6f;
            }
            else if (v == closedHat && sound == 2)
            {
                // Shaker: noise, with a soft start.
                s.f1.set (6000.0f + 5000.0f * tone, 0.8f, rate);
                s.f2.set (4000.0f + 3000.0f * tone, 0.7f, rate);
                decay *= 1.4f;
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
        }
        default:
            break;
    }
    s.envCoeff = coeffFor (decay, invRate);
    ++hitCounts[(size_t) v];
    tap (v, velocity);
}

float Engine::renderVoice (int v, VoiceState& s)
{
    const auto& vs = settings.voices[(size_t) v];
    const float tone = juce::jlimit (0.0f, 1.0f, vs.tone);
    const float snappy = juce::jlimit (0.0f, 1.0f, vs.snappy);
    const int sound = s.sound;
    float out = 0, lp, bp, hp;
    float level = s.env;

    switch (v)
    {
        case kick:
        {
            // A sine that drops in pitch, with a click on top.
            const float sweep = sound == 1 ? 0.6f + 1.4f * tone : sound == 2 ? 2.0f + 3.0f * tone : 1.2f + 2.4f * tone;
            const float hz = s.pitch * (1.0f + sweep * s.env2 * s.env2);
            advance (s.phase[0], hz, invRate);
            const float body = sine (s.phase[0]);
            const float click = s.clickEnv * (0.6f * noise() + 0.8f) * snappy * (sound == 2 ? 0.9f : 0.55f);
            s.clickEnv *= sound == 2 ? 0.975f : 0.985f;
            out = (body * s.env + click * s.clickEnv) * 1.05f;
            if (sound == 3)
                out = std::tanh (out * 4.5f) * 0.62f;
            else if (sound == 1)
                out = std::tanh (out * 2.0f) * 0.7f;
            else
                out = std::tanh (out * 1.4f) * 0.8f;
            s.env2 *= s.env2Coeff;
            break;
        }
        case snare:
        {
            // Two tuned drums (Tone sets their balance) and the snares (Snappy).
            advance (s.phase[0], s.pitch, invRate);
            advance (s.phase[1], s.pitch * 1.76f, invRate);
            const float w2 = 0.15f + 0.4f * tone;
            const float body = ((1.0f - w2) * sine (s.phase[0]) + w2 * sine (s.phase[1])) * s.env2 * s.env2;
            s.f1.process (noise(), lp, bp, hp);
            const float snap = (sound == 2 ? bp * 1.4f : hp) * s.env;
            out = body * (0.95f - 0.4f * snappy) + snap * (0.15f + 1.2f * snappy);
            s.env2 *= s.env2Coeff;
            level = juce::jmax (s.env, s.env2);
            break;
        }
        case clap:
        {
            s.f1.process (noise(), lp, bp, hp);
            if (sound == 1)
            {
                // Maracas: Snappy makes the start sharper.
                const float attack = 0.001f + 0.009f * (1.0f - snappy);
                out = hp * s.env * juce::jmin (1.0f, s.t / attack) * 1.3f;
                break;
            }
            // A few quick bursts of noise, then the tail. Snappy makes the bursts sharper.
            const float tau = 0.0015f + 0.0045f * (1.0f - snappy);
            float e = 0;
            if (s.t < 0.034f)
                e = std::exp (-std::fmod (s.t, 0.0108f) / tau);
            const float tail = s.t > 0.022f ? s.env : 0.0f;
            out = bp * juce::jmax (e, tail) * 2.4f;
            level = juce::jmax (e, s.env);
            break;
        }
        case rim:
        {
            advance (s.phase[0], s.pitch, invRate);
            advance (s.phase[1], s.pitch * (sound == 1 ? 1.71f : 0.28f), invRate);
            const float click = s.clickEnv * noise() * snappy * 0.5f;
            s.clickEnv *= 0.99f;
            if (sound == 1)
            {
                const float x = sine (s.phase[0]) + 0.5f * tone * sine (s.phase[1]);
                s.f1.process (x + click, lp, bp, hp);
                out = (bp * 1.4f + x * 0.3f) * s.env * 0.7f;
                break;
            }
            const float x = 0.6f * square (s.phase[0]) * (0.4f + 0.6f * tone) + 0.7f * sine (s.phase[1]);
            s.f1.process (x + click, lp, bp, hp);
            out = hp * s.env * 0.75f;
            break;
        }
        case lowTom: case midTom: case highTom:
        {
            const float sweep = sound == 1 ? 0.05f + 0.25f * tone : 0.15f + 0.4f * tone;
            const float hz = s.pitch * (1.0f + sweep * s.env2);
            advance (s.phase[0], hz, invRate);
            float skin = 0;
            if (sound == 1)
            {
                // The conga's slap: a quick overtone.
                advance (s.phase[1], hz * 2.3f, invRate);
                skin = sine (s.phase[1]) * s.clickEnv * snappy * 0.6f;
                s.clickEnv *= 0.993f;
            }
            else
            {
                s.f1.process (noise(), lp, bp, hp);
                skin = lp * s.clickEnv * snappy * 0.5f;
                s.clickEnv *= 0.996f;
            }
            out = sine (s.phase[0]) * s.env * 0.9f + skin;
            s.env2 *= s.env2Coeff;
            break;
        }
        case clave:
        {
            advance (s.phase[0], s.pitch, invRate);
            advance (s.phase[1], s.pitch * (sound == 1 ? 2.72f : 1.58f), invRate);
            const float click = s.clickEnv * noise() * snappy * 0.4f;
            s.clickEnv *= 0.99f;
            out = (sine (s.phase[0]) + tone * 0.5f * sine (s.phase[1])) * s.env * (sound == 1 ? 0.8f : 0.7f) + click;
            break;
        }
        case cowbell:
        {
            advance (s.phase[0], s.freqs[0], invRate);
            advance (s.phase[1], s.freqs[1], invRate);
            const float x = sound == 1 ? 0.5f * (sine (s.phase[0]) + sine (s.phase[1])) : 0.5f * (square (s.phase[0]) + square (s.phase[1]));
            s.f1.process (x, lp, bp, hp);
            // Snappy: how much of the bright clank at the start.
            const float a = 0.3f + 0.6f * snappy;
            const float e = a * s.env2 + (1.0f - a) * s.env;
            out = (sound == 1 ? bp * 1.2f + x * 0.4f : bp * 1.6f) * e;
            s.env2 *= s.env2Coeff;
            level = e;
            break;
        }
        case closedHat: case openHat: case cymbal:
        {
            float x = 0;
            if (v == closedHat && sound == 2)
            {
                x = noise() * juce::jmin (1.0f, s.t / 0.006f);
            }
            else
            {
                for (int i = 0; i < 6; ++i)
                {
                    advance (s.phase[i], s.freqs[i], invRate);
                    x += square (s.phase[i]);
                }
                x *= 1.0f / 6.0f;
                if (v == openHat && sound == 1)
                    x = 0.7f * x + 0.35f * noise();
            }
            // Snappy: a tick of noise at the very start.
            x += s.env2 * noise() * snappy * 0.6f;
            s.env2 *= s.env2Coeff;
            s.f1.process (x, lp, bp, hp);
            float lp2, bp2, hp2;
            s.f2.process (bp, lp2, bp2, hp2);
            const float e = v == cymbal ? s.env * (0.75f + 0.25f * s.clickEnv) : s.env;
            out = hp2 * e * (v == cymbal ? 1.6f : 1.9f);
            if (v == cymbal && sound == 1)
            {
                // The ride's bell.
                advance (s.bell, s.freqs[5] * 4.1f, invRate);
                out += sine (s.bell) * s.env * s.clickEnv * tone * 0.18f;
            }
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

const Pattern* Engine::byCode (int code) const
{
    if (bankA == nullptr)
        return pattern;
    const int p = juce::jlimit (0, kPatterns - 1, code >> 1);
    return ((code & 1) != 0 && bankB != nullptr ? bankB : bankA) + p;
}

const Pattern* Engine::basePattern (long long loop, int& code) const
{
    if (bankA == nullptr)
    {
        code = 0;
        return pattern;
    }
    if (plan.chainOn && plan.chainLength > 0)
        code = plan.chain[(size_t) juce::jlimit (0, plan.chainLength - 1, chainPos)];
    else
        code = juce::jlimit (0, kPatterns - 1, plan.pattern) * 2
               + ((plan.variation == Plan::b || (plan.variation == Plan::aThenB && (loop & 1) != 0)) ? 1 : 0);
    return byCode (code);
}

namespace
{
int lengthOf (const Pattern* p) { return p != nullptr ? juce::jlimit (1, kMaxSteps, p->length) : 16; }
}

void Engine::resync (long long index)
{
    // Where the patterns are at this step, counting from the top (after a jump, a new pattern or
    // a start part-way through).
    inFill = inIntro = false;
    if (bankA != nullptr && plan.chainOn && plan.chainLength > 0)
    {
        const int entries = juce::jlimit (1, kChain, plan.chainLength);
        long long total = 0;
        for (int e = 0; e < entries; ++e)
            total += lengthOf (byCode (plan.chain[(size_t) e]));
        long long k = index % total;
        chainPos = 0;
        while (chainPos < entries - 1 && k >= lengthOf (byCode (plan.chain[(size_t) chainPos])))
            k -= lengthOf (byCode (plan.chain[(size_t) chainPos++]));
        position = (int) k;
        loopCount = (index / total) * entries + chainPos;
    }
    else
    {
        const int length = lengthOf (bankA != nullptr ? byCode (juce::jlimit (0, kPatterns - 1, plan.pattern) * 2) : pattern);
        loopCount = index / length;
        position = (int) (index % length);
        chainPos = 0;
    }
    current = basePattern (loopCount, currentCode);
    position %= lengthOf (current);
    if (position == 0)
    {
        const Pattern* fill = bankA != nullptr ? (plan.fill >= 0 ? bankA + juce::jlimit (0, kPatterns - 1, plan.fill) : nullptr) : fillPattern;
        const bool every = settings.fillEvery > 1 && (loopCount % settings.fillEvery) == settings.fillEvery - 1;
        const bool once = fillOnce.exchange (false);
        if (fill != nullptr && (every || once))
        {
            inFill = true;
            current = fill;
            currentCode = bankA != nullptr ? plan.fill * 2 : -1;
        }
    }
}

void Engine::nextLoop()
{
    const bool afterIntro = inIntro;
    inIntro = inFill = false;
    ++loopCount;
    if (! afterIntro && bankA != nullptr && plan.chainOn && plan.chainLength > 0)
        chainPos = (chainPos + 1) % juce::jlimit (1, kChain, plan.chainLength);
    current = basePattern (loopCount, currentCode);

    const Pattern* fill = bankA != nullptr ? (plan.fill >= 0 ? bankA + juce::jlimit (0, kPatterns - 1, plan.fill) : nullptr) : fillPattern;
    const bool every = settings.fillEvery > 1 && loopCount >= 0 && (loopCount % settings.fillEvery) == settings.fillEvery - 1;
    const bool once = fillOnce.exchange (false);
    if (fill != nullptr && (every || once))
    {
        inFill = true;
        current = fill;
        currentCode = bankA != nullptr ? plan.fill * 2 : -1;
    }
}

void Engine::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const Clock& clock)
{
    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    const bool sequencing = clock.playing && settings.sequencerOn && (pattern != nullptr || bankA != nullptr);
    if (wasPlaying && ! sequencing)
    {
        currentStep = -1;
        currentCode = -1;
        lastFired = -1;
        current = nullptr;
        inFill = inIntro = false;
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
                const bool fresh = lastFired < 0;
                const bool contiguous = ! fresh && index == lastFired + 1 && current != nullptr && plan.sameRun (lastPlan)
                                        && (bankA != nullptr || pattern == lastSimple);
                lastFired = index;
                lastPlan = plan;
                lastSimple = pattern;
                if (contiguous)
                {
                    if (++position >= lengthOf (current))
                    {
                        position = 0;
                        nextLoop();
                    }
                }
                else if (fresh && index == 0 && bankA != nullptr && plan.intro >= 0)
                {
                    // The intro plays once, then the beat (or the chain) from its top.
                    inFill = false;
                    inIntro = true;
                    chainPos = 0;
                    loopCount = -1;
                    position = 0;
                    currentCode = juce::jlimit (0, kPatterns - 1, plan.intro) * 2;
                    current = byCode (currentCode);
                }
                else
                {
                    resync (index);
                }
                currentStep = position;
                const double start = (double) index * sb + ((index & 1) != 0 ? swingOffset : 0.0);
                hitStep (position, *current, start, (index & 1) != 0 ? sb - swingOffset : sb + swingOffset);
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
