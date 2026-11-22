#include "FactoryKits.h"

#include "DrumEngine.h"

#include <functional>

namespace crate::factory
{
namespace
{
constexpr double kRate = 44100.0;
using Buffer = juce::AudioBuffer<float>;
namespace dr = crate::drums;

// Cuts the tail once it's quiet and brings the peak to 0.9.
Buffer finish (Buffer b)
{
    const int n = b.getNumSamples();
    int last = n - 1;
    while (last > 64 && std::abs (b.getSample (0, last)) < 0.0005f)
        --last;
    const int keep = juce::jmin (n, last + 256);
    Buffer out (1, keep);
    out.copyFrom (0, 0, b, 0, 0, keep);
    const float peak = out.getMagnitude (0, keep);
    if (peak > 1.0e-6f)
        out.applyGain (0.9f / peak);
    const int fade = juce::jmin (keep, 256);
    out.applyGainRamp (keep - fade, fade, 1.0f, 0.0f);
    return out;
}

// One hit of the 808 engine, with these voice settings.
Buffer hit808 (int voice, dr::VoiceSettings vs, double seconds, float drive = 0.0f)
{
    dr::Engine e;
    e.prepare (kRate);
    dr::Settings s;
    vs.level = 1.0f;
    vs.pan = 0.0f;
    s.voices[(size_t) voice] = vs;
    s.drive = drive;
    s.volume = 1.0f;
    e.setSettings (s);
    e.trigger (voice, 1.0f, false);
    const int n = (int) (seconds * kRate);
    Buffer b (1, n);
    std::vector<float> l ((size_t) n, 0.0f), r ((size_t) n, 0.0f);
    juce::MidiBuffer none;
    dr::Clock clock;
    for (int i = 0; i < n; i += 512)
    {
        const int m = juce::jmin (512, n - i);
        e.render (l.data() + i, r.data() + i, m, none, clock);
    }
    for (int i = 0; i < n; ++i)
        b.setSample (0, i, 0.5f * (l[(size_t) i] + r[(size_t) i]));
    return finish (std::move (b));
}

dr::VoiceSettings vset (float tune, float decay, float tone, float snappy, int sound = 0)
{
    dr::VoiceSettings v;
    v.tune = tune;
    v.decay = decay;
    v.tone = tone;
    v.snappy = snappy;
    v.sound = sound;
    return v;
}

struct Noise
{
    juce::Random rng { 1234 };
    float next() { return rng.nextFloat() * 2.0f - 1.0f; }
};

// A simple state-variable filter for the synths.
struct Svf
{
    float ic1 = 0, ic2 = 0;
    float g = 0, k = 1;
    void set (float hz, float q) { g = std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, 20000.0f, hz) / (float) kRate); k = 1.0f / q; }
    float lp (float x) { float v1, v2; tick (x, v1, v2); return v2; }
    float bp (float x) { float v1, v2; tick (x, v1, v2); return v1; }
    float hp (float x) { float v1, v2; tick (x, v1, v2); return x - k * v1 - v2; }
    void tick (float x, float& v1, float& v2)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2;
        v1 = a1 * ic1 + a2 * v3;
        v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1;
        ic2 = 2 * v2 - ic2;
    }
};

template <typename F>
Buffer synth (double seconds, F&& f)
{
    const int n = (int) (seconds * kRate);
    Buffer b (1, n);
    auto* d = b.getWritePointer (0);
    for (int i = 0; i < n; ++i)
        d[i] = f ((float) (i / kRate));
    return finish (std::move (b));
}

// Lo-fi: fewer bits, a lower sample rate, darker, a little hiss.
Buffer lofi (Buffer b, int bits = 10, int hold = 3, float cut = 5200.0f)
{
    Svf f;
    f.set (cut, 0.7f);
    Noise nz;
    const float steps = (float) (1 << (bits - 1));
    float held = 0;
    auto* d = b.getWritePointer (0);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        if (i % hold == 0)
            held = std::round (d[i] * steps) / steps;
        d[i] = f.lp (held) + nz.next() * 0.0015f;
    }
    return finish (std::move (b));
}

float env (float t, float attack, float decay) { return t < attack ? t / attack : std::exp (-(t - attack) / decay); }

Buffer bass808 (float hz, double seconds, float drive)
{
    return synth (seconds, [hz, drive, ph = 0.0] (float t) mutable
    {
        const float f = hz * (1.0f + 1.5f * std::exp (-t * 40.0f));
        ph += f / kRate;
        const float s = std::sin ((float) (ph * juce::MathConstants<double>::twoPi));
        return std::tanh (s * (1.0f + drive * 4.0f)) * env (t, 0.003f, 0.9f);
    });
}

Buffer synthBass (float hz, double seconds)
{
    Svf f;
    return synth (seconds, [hz, f, ph = 0.0] (float t) mutable
    {
        ph += hz / kRate;
        const float saw = (float) (2.0 * (ph - std::floor (ph)) - 1.0);
        f.set (180.0f + 2200.0f * std::exp (-t * 9.0f), 1.4f);
        return f.lp (saw) * env (t, 0.004f, 0.45f);
    });
}

Buffer riser (double seconds)
{
    Svf f;
    Noise nz;
    return synth (seconds, [seconds, f, nz] (float t) mutable
    {
        const float x = t / (float) seconds;
        f.set (300.0f * std::pow (40.0f, x), 4.0f);
        return f.bp (nz.next()) * x * x * (x < 0.97f ? 1.0f : (1.0f - x) / 0.03f);
    });
}

Buffer shaker (float decay, float bright)
{
    Svf f;
    Noise nz;
    return synth (0.35, [decay, bright, f, nz] (float t) mutable
    {
        f.set (bright, 1.2f);
        const float e = env (t, 0.012f, decay) + 0.5f * env (std::max (0.0f, t - 0.06f), 0.01f, decay * 0.8f) * (t > 0.06f ? 1.0f : 0.0f);
        return f.hp (nz.next()) * e;
    });
}

Buffer tambourine()
{
    Svf f;
    Noise nz;
    return synth (0.6, [f, nz] (float t) mutable
    {
        f.set (7000.0f, 2.0f);
        float jingle = 0;
        for (float hz : { 5100.0f, 6450.0f, 7900.0f, 9200.0f })
            jingle += std::sin (juce::MathConstants<float>::twoPi * hz * t);
        return (f.hp (nz.next()) * 0.7f + jingle * 0.08f) * env (t, 0.002f, 0.16f);
    });
}

Buffer bell (float hz, float ratio, double seconds)
{
    return synth (seconds, [hz, ratio] (float t)
    {
        const float index = 4.0f * std::exp (-t * 3.0f);
        const float mod = std::sin (juce::MathConstants<float>::twoPi * hz * ratio * t) * index;
        return std::sin (juce::MathConstants<float>::twoPi * hz * t + mod) * env (t, 0.002f, 1.1f);
    });
}

Buffer gong()
{
    return synth (4.0, [] (float t)
    {
        float s = 0;
        const float partials[] = { 1.0f, 1.47f, 2.09f, 2.56f, 3.17f, 4.23f, 5.11f };
        for (int i = 0; i < 7; ++i)
            s += std::sin (juce::MathConstants<float>::twoPi * 92.0f * partials[i] * t * (1.0f + 0.002f * std::sin (t * 3.0f + (float) i)))
                 * std::exp (-t * (0.6f + 0.25f * (float) i)) / (1.0f + (float) i * 0.6f);
        return s * env (t, 0.02f, 10.0f);
    });
}

// A tabla-like drum: a tuned skin whose pitch bends.
Buffer skin (float hz, float bend, float decay, float ring)
{
    Noise nz;
    return synth (0.9, [hz, bend, decay, ring, nz, ph = 0.0, ph2 = 0.0] (float t) mutable
    {
        const float f = hz * (1.0f + bend * std::exp (-t * 18.0f));
        ph += f / kRate;
        ph2 += f * 2.76 / kRate;
        const float s = std::sin ((float) (ph * juce::MathConstants<double>::twoPi)) + ring * std::sin ((float) (ph2 * juce::MathConstants<double>::twoPi));
        return s * env (t, 0.002f, decay) + nz.next() * env (t, 0.0005f, 0.004f) * 0.5f;
    });
}

// A sung vowel: a buzzy voice through three formants. The vowel can move (hey: e to ee).
Buffer vowel (float hz, std::array<float, 3> from, std::array<float, 3> to, double seconds, float breath = 0.0f)
{
    std::array<Svf, 3> f;
    Svf src;
    Noise nz;
    double ph = 0;
    return synth (seconds, [=] (float t) mutable
    {
        const float vib = 1.0f + 0.006f * std::sin (juce::MathConstants<float>::twoPi * 5.2f * t) * juce::jmin (1.0f, t * 3.0f);
        ph += hz * vib / kRate;
        const float saw = (float) (2.0 * (ph - std::floor (ph)) - 1.0);
        src.set (1800.0f, 0.7f);
        const float buzz = src.lp (saw) * (1.0f - breath) + nz.next() * (0.15f + breath);
        const float x = juce::jlimit (0.0f, 1.0f, t / (float) seconds * 1.6f);
        float out = 0;
        const float gains[] = { 1.0f, 0.6f, 0.35f };
        for (int i = 0; i < 3; ++i)
        {
            f[(size_t) i].set (from[(size_t) i] + (to[(size_t) i] - from[(size_t) i]) * x, 9.0f);
            out += f[(size_t) i].bp (buzz) * gains[i];
        }
        const float a = env (t, 0.04f, 10.0f) * juce::jmin (1.0f, ((float) seconds - t) / 0.15f);
        return out * a;
    });
}

Buffer keysStab (std::initializer_list<float> notes, float decay)
{
    std::vector<float> hz (notes);
    return synth (1.6, [hz, decay] (float t)
    {
        float s = 0;
        for (float f : hz)
        {
            const float w = juce::MathConstants<float>::twoPi * f * t;
            s += std::sin (w) + 0.3f * std::sin (2 * w) * std::exp (-t * 6.0f) + 0.1f * std::sin (3 * w + std::sin (w));
        }
        return s * env (t, 0.003f, decay);
    });
}

Buffer vinylHit()
{
    // a dusty kick: an 808 kick with the low end rounded and crackle on top
    auto k = hit808 (dr::kick, vset (-2.0f, 0.35f, 0.3f, 0.6f), 0.8);
    return lofi (std::move (k), 9, 4, 3600.0f);
}

using Make = std::function<Buffer()>;
struct Sound { const char* name; Make make; };

std::vector<Sound> kitSounds (int index)
{
    switch (index)
    {
        case 0: return {   // Street kit
            { "Street kick", [] { return hit808 (dr::kick, vset (-1, 0.45f, 0.6f, 0.8f, 1), 1.2, 0.35f); } },
            { "Street snare", [] { return hit808 (dr::snare, vset (1, 0.45f, 0.6f, 0.75f, 1), 0.6, 0.2f); } },
            { "Street clap", [] { return hit808 (dr::clap, vset (0, 0.5f, 0.5f, 0.6f), 0.7); } },
            { "Street rim", [] { return hit808 (dr::rim, vset (0, 0.4f, 0.5f, 0.5f), 0.3); } },
            { "Street hat", [] { return hit808 (dr::closedHat, vset (0, 0.3f, 0.7f, 0.5f), 0.3); } },
            { "Street open hat", [] { return hit808 (dr::openHat, vset (0, 0.55f, 0.65f, 0.5f), 0.9); } },
            { "Street lo tom", [] { return hit808 (dr::lowTom, vset (-3, 0.5f, 0.5f, 0.4f), 0.9); } },
            { "Street hi tom", [] { return hit808 (dr::highTom, vset (0, 0.5f, 0.5f, 0.4f), 0.7); } },
            { "Street shaker", [] { return shaker (0.05f, 4500.0f); } },
            { "Street tamb", [] { return tambourine(); } },
            { "Street crash", [] { return hit808 (dr::cymbal, vset (0, 0.8f, 0.6f, 0.5f), 2.5); } },
            { "Street cowbell", [] { return hit808 (dr::cowbell, vset (0, 0.4f, 0.5f, 0.5f), 0.6); } },
            { "Street 808 C", [] { return bass808 (65.4f, 1.6, 0.4f); } },
            { "Street bass G", [] { return synthBass (49.0f, 0.9); } },
            { "Street hey", [] { return vowel (196.0f, { 530, 1840, 2480 }, { 270, 2290, 3010 }, 0.45, 0.2f); } },
            { "Street riser", [] { return riser (2.0); } } };
        case 1: return {   // Dusty lo-fi
            { "Dusty kick", [] { return vinylHit(); } },
            { "Dusty snare", [] { return lofi (hit808 (dr::snare, vset (-2, 0.35f, 0.3f, 0.45f), 0.5), 9, 4, 4200.0f); } },
            { "Dusty clap", [] { return lofi (hit808 (dr::clap, vset (-1, 0.4f, 0.4f, 0.5f), 0.6), 9, 3, 4500.0f); } },
            { "Dusty rim", [] { return lofi (hit808 (dr::rim, vset (-3, 0.3f, 0.4f, 0.4f), 0.3)); } },
            { "Dusty hat", [] { return lofi (hit808 (dr::closedHat, vset (-2, 0.25f, 0.35f, 0.4f), 0.25), 8, 3, 6500.0f); } },
            { "Dusty open hat", [] { return lofi (hit808 (dr::openHat, vset (-2, 0.45f, 0.35f, 0.4f), 0.8), 8, 3, 6500.0f); } },
            { "Dusty brush", [] { return lofi (shaker (0.09f, 2500.0f), 9, 3, 5000.0f); } },
            { "Dusty tom", [] { return lofi (hit808 (dr::midTom, vset (-4, 0.45f, 0.4f, 0.3f), 0.8)); } },
            { "Dusty keys Am7", [] { return lofi (keysStab ({ 220.0f, 261.6f, 329.6f, 392.0f }, 0.5f), 10, 3, 4000.0f); } },
            { "Dusty keys Dm9", [] { return lofi (keysStab ({ 146.8f, 174.6f, 220.0f, 261.6f, 329.6f }, 0.5f), 10, 3, 4000.0f); } },
            { "Dusty keys Fmaj7", [] { return lofi (keysStab ({ 174.6f, 220.0f, 261.6f, 329.6f }, 0.5f), 10, 3, 4000.0f); } },
            { "Dusty keys E7", [] { return lofi (keysStab ({ 164.8f, 207.7f, 246.9f, 293.7f }, 0.5f), 10, 3, 4000.0f); } },
            { "Dusty bass A", [] { return lofi (synthBass (55.0f, 0.8), 11, 2, 2500.0f); } },
            { "Dusty bass D", [] { return lofi (synthBass (73.4f, 0.8), 11, 2, 2500.0f); } },
            { "Dusty ooh", [] { return lofi (vowel (220.0f, { 300, 870, 2240 }, { 300, 870, 2240 }, 1.2), 10, 3, 4500.0f); } },
            { "Dusty crackle", [] { Noise nz; return lofi (synth (2.0, [nz] (float) mutable { const float x = nz.next(); return std::abs (x) > 0.995f ? x : x * 0.01f; }), 8, 2, 7000.0f); } } };
        case 2: {   // 808 classic: every voice, plus tuned bass
            std::vector<Sound> v;
            const char* names[dr::kVoices] = { "808 kick", "808 snare", "808 clap", "808 rim", "808 lo tom", "808 mid tom", "808 hi tom", "808 clave", "808 cowbell", "808 closed hat", "808 open hat", "808 cymbal" };
            for (int i = 0; i < dr::kVoices; ++i)
                v.push_back ({ names[i], [i] { return hit808 (i, vset (0, i == dr::cymbal ? 0.8f : 0.5f, 0.5f, 0.5f), i == dr::cymbal ? 3.0 : 1.4); } });
            v.push_back ({ "808 bass C", [] { return bass808 (65.4f, 2.0, 0.1f); } });
            v.push_back ({ "808 bass E", [] { return bass808 (82.4f, 2.0, 0.1f); } });
            v.push_back ({ "808 bass G", [] { return bass808 (98.0f, 2.0, 0.1f); } });
            v.push_back ({ "808 long kick", [] { return hit808 (dr::kick, vset (-2, 1.0f, 0.4f, 0.5f), 2.5); } });
            return v;
        }
        case 3: return {   // Psych percussion
            { "Tabla na", [] { return skin (330.0f, 0.15f, 0.25f, 0.5f); } },
            { "Tabla ge", [] { return skin (98.0f, -0.35f, 0.5f, 0.1f); } },
            { "Tabla tin", [] { return skin (440.0f, 0.05f, 0.12f, 0.7f); } },
            { "Tabla ka", [] { return skin (180.0f, 0.0f, 0.05f, 0.2f); } },
            { "Shaker short", [] { return shaker (0.035f, 6000.0f); } },
            { "Shaker long", [] { return shaker (0.12f, 4000.0f); } },
            { "Tambourine", [] { return tambourine(); } },
            { "Clave", [] { return hit808 (dr::clave, vset (0, 0.5f, 0.5f, 0.5f), 0.4); } },
            { "Bell A", [] { return bell (440.0f, 3.5f, 2.5); } },
            { "Bell E", [] { return bell (659.3f, 3.5f, 2.5); } },
            { "Glass bell", [] { return bell (880.0f, 1.41f, 2.0); } },
            { "Wood block", [] { return skin (720.0f, 0.0f, 0.04f, 0.6f); } },
            { "Gong", [] { return gong(); } },
            { "Conga lo", [] { return hit808 (dr::lowTom, vset (5, 0.35f, 0.7f, 0.8f, 1), 0.6); } },
            { "Conga hi", [] { return hit808 (dr::highTom, vset (5, 0.35f, 0.7f, 0.8f, 1), 0.5); } },
            { "Cowbell", [] { return hit808 (dr::cowbell, vset (2, 0.5f, 0.5f, 0.5f), 0.7); } } };
        default: return {   // Vocal chops, in A minor
            { "Ooh A", [] { return vowel (220.0f, { 300, 870, 2240 }, { 300, 870, 2240 }, 1.2); } },
            { "Ooh C", [] { return vowel (261.6f, { 300, 870, 2240 }, { 300, 870, 2240 }, 1.2); } },
            { "Ooh E", [] { return vowel (329.6f, { 300, 870, 2240 }, { 300, 870, 2240 }, 1.2); } },
            { "Ooh G", [] { return vowel (392.0f, { 300, 870, 2240 }, { 300, 870, 2240 }, 1.2); } },
            { "Aah A", [] { return vowel (220.0f, { 730, 1090, 2440 }, { 730, 1090, 2440 }, 1.2); } },
            { "Aah C", [] { return vowel (261.6f, { 730, 1090, 2440 }, { 730, 1090, 2440 }, 1.2); } },
            { "Aah E", [] { return vowel (329.6f, { 730, 1090, 2440 }, { 730, 1090, 2440 }, 1.2); } },
            { "Aah G", [] { return vowel (392.0f, { 730, 1090, 2440 }, { 730, 1090, 2440 }, 1.2); } },
            { "Hey", [] { return vowel (247.0f, { 530, 1840, 2480 }, { 270, 2290, 3010 }, 0.4, 0.25f); } },
            { "Hey low", [] { return vowel (147.0f, { 530, 1840, 2480 }, { 270, 2290, 3010 }, 0.4, 0.25f); } },
            { "Ee E", [] { return vowel (329.6f, { 270, 2290, 3010 }, { 270, 2290, 3010 }, 0.8); } },
            { "Oh A", [] { return vowel (220.0f, { 570, 840, 2410 }, { 570, 840, 2410 }, 0.8); } },
            { "Ah to ee", [] { return vowel (261.6f, { 730, 1090, 2440 }, { 270, 2290, 3010 }, 1.0); } },
            { "Ooh to aah", [] { return vowel (196.0f, { 300, 870, 2240 }, { 730, 1090, 2440 }, 1.0); } },
            { "Breath in", [] { return vowel (200.0f, { 600, 1200, 2600 }, { 400, 1500, 2800 }, 0.7, 0.85f); } },
            { "Breath out", [] { return vowel (200.0f, { 400, 1500, 2800 }, { 700, 1100, 2400 }, 0.7, 0.85f); } } };
    }
}
}

const std::vector<Kit>& kits()
{
    static const std::vector<Kit> list {
        { "Street kit", "punchy drums, 808 C, bass, hey, riser", 0xfff08a74 },
        { "Dusty lo-fi", "dusty drums, keys chords in A minor, bass", 0xffc9a472 },
        { "808 classic", "every 808 voice, tuned 808 bass", 0xff5fb3c4 },
        { "Psych percussion", "tabla, shakers, bells, gong, congas", 0xff8a6fb3 },
        { "Vocal chops", "oohs, aahs, heys and breaths in A minor", 0xff8fc06a } };
    return list;
}

std::vector<std::pair<juce::String, Buffer>> makeSounds (int index)
{
    std::vector<std::pair<juce::String, Buffer>> out;
    for (auto& s : kitSounds (index))
        out.emplace_back (s.name, s.make());
    return out;
}

std::vector<juce::String> soundNames (int index)
{
    std::vector<juce::String> out;
    for (auto& s : kitSounds (index))
        out.emplace_back (s.name);
    return out;
}

Buffer makeSound (int index, int sound)
{
    auto sounds = kitSounds (index);
    if (sound < 0 || sound >= (int) sounds.size())
        return {};
    return sounds[(size_t) sound].make();
}
}
