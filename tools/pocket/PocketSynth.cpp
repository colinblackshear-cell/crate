#include "PocketSynth.h"

#include "rig/RigSounds.h"   // the Rig's named sounds for its PS-8 synth and BX-1 bass (just a header)

#include <algorithm>
#include <cmath>

namespace pocket
{
using crate::synthtool::Settings;
namespace st = crate::synthtool;
namespace sy = crate::synth;

const char* synthKindName (int kind)
{
    static const char* const names[kSynthKinds] = { "LEAD", "KEYS", "PAD", "PLUCK", "ORGAN", "STRINGS", "BELLS", "BRASS",
                                                    "SUB", "808", "FUNK", "ACID", "GROWL", "ANALOG", "PICKED", "DIGITAL" };
    return names[juce::jlimit (0, kSynthKinds - 1, kind)];
}

juce::Colour synthKindColour (int kind)
{
    static const juce::uint32 colours[kSynthKinds] = { 0xfff7c04a, 0xff2fc6a8, 0xffa877f5, 0xff9a5cf0, 0xfff7c04a, 0xffa877f5, 0xff5fe3c6, 0xfff59a2a,
                                                       0xfff59a2a, 0xffec4e8a, 0xfff7c04a, 0xff2fc6a8, 0xffec4e8a, 0xfff59a2a, 0xffa877f5, 0xff5fe3c6 };
    return juce::Colour (colours[juce::jlimit (0, kSynthKinds - 1, kind)]);
}

const char* synthShapeName (int shape)
{
    static const char* const names[4] = { "SAW", "SQUARE", "TRI", "SUPER" };
    return names[juce::jlimit (0, 3, shape)];
}

namespace
{
enum Kind { lead, keys, pad, pluck, organ, strings, bells, brass,
            sub, b808, funk, acid, growl, analog, picked, digital };

// A Rig sound's settings (RigSounds' parameter ids) onto the engine's settings.
void applyPs8 (Settings& s, const char* id, float v)
{
    const juce::String k (id);
    if (k == "wave") s.wave = (sy::Wave) juce::jlimit (0, 3, (int) v);
    else if (k == "detune") s.detune = v;
    else if (k == "octave") s.octave = (int) v;
    else if (k == "cutoff") s.cutoff = v;
    else if (k == "resonance") s.resonance = v;
    else if (k == "envMod") s.envMod = v;
    else if (k == "filterDecay") s.filterDecay = v;
    else if (k == "attack") s.attack = v;
    else if (k == "decay") s.decay = v;
    else if (k == "sustain") s.sustain = v;
    else if (k == "release") s.release = v;
    else if (k == "lfoDepth") s.lfoDepth = v;
    else if (k == "lfoRate") s.lfoRate = (int) v;
    else if (k == "lfoShape") s.lfoShape = (sy::LfoShape) (int) v;
}

// The BX-1 is a mono bass with its own filter envelope; on the PS-8 engine that is one mono voice, a
// gate-shaped volume and the filter's decay.
Settings bassBase()
{
    Settings s;
    s.wave = sy::Wave::saw;
    s.voiceMode = st::VoiceMode::mono;
    s.octave = -1;
    s.cutoff = 400.0f;
    s.resonance = 0.6f;
    s.envMod = 0.6f;
    s.filterDecay = 0.3f;
    s.filterSustain = 0.0f;
    s.attack = 0.003f;
    s.decay = 0.3f;
    s.sustain = 1.0f;
    s.release = 0.05f;
    s.subLevel = 0.4f;
    s.drive = 0.3f;
    s.detune = 0.2f;
    s.volume = 0.75f;
    return s;
}

void applyBx1 (Settings& s, const char* id, float v)
{
    const juce::String k (id);
    if (k == "wave") s.wave = (sy::Wave) juce::jlimit (0, 2, (int) v);
    else if (k == "cutoff") s.cutoff = v;
    else if (k == "resonance") s.resonance = v * 0.9f;
    else if (k == "envMod") s.envMod = v;
    else if (k == "decay") { s.filterDecay = v; s.decay = v; }
    else if (k == "drive") s.drive = v;
    else if (k == "sub") s.subLevel = v;
    else if (k == "glide") s.glide = v * 0.35f;
    else if (k == "lfoDepth") s.lfoDepth = v;
    else if (k == "lfoRate") s.lfoRate = (int) v;
}

std::vector<SynthPreset> build()
{
    std::vector<SynthPreset> out;
    auto findPs8 = [] (const char* name) -> const RigSounds::Sound*
    {
        for (auto& s : RigSounds::synth())
            if (juce::String (s.name) == name)
                return &s;
        return nullptr;
    };
    auto findBx1 = [] (const char* name) -> const RigSounds::Sound*
    {
        for (auto& s : RigSounds::bass())
            if (juce::String (s.name) == name)
                return &s;
        return nullptr;
    };
    auto ps8 = [&] (int kind, const char* name, std::function<void (Settings&)> tweak = {})
    {
        if (auto* snd = findPs8 (name))
        {
            SynthPreset p { kind, name, "PS-8", {} };
            for (const auto& [id, v] : snd->values)
                applyPs8 (p.settings, id, v);
            if (tweak)
                tweak (p.settings);
            out.push_back (p);
        }
    };
    auto bx1 = [&] (int kind, const char* name, std::function<void (Settings&)> tweak = {})
    {
        if (auto* snd = findBx1 (name))
        {
            SynthPreset p { kind, name, "BX-1", bassBase() };
            for (const auto& [id, v] : snd->values)
                applyBx1 (p.settings, id, v);
            if (tweak)
                tweak (p.settings);
            out.push_back (p);
        }
    };
    auto own = [&] (int kind, const char* name, std::function<void (Settings&)> make)
    {
        SynthPreset p { kind, name, "POCKET", {} };
        make (p.settings);
        out.push_back (p);
    };

    // ---- SYNTH family (the SYNTH tool) ----
    // LEAD
    for (auto* n : { "Big Supersaw", "Square Lead", "Chip Lead", "Screamer", "Soft Flute", "Random Bleeps" })
        ps8 (lead, n);
    own (lead, "Sync Lead", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 7; s.osc2Fine = 4; s.osc2Level = 0.45f;
        s.cutoff = 3200; s.resonance = 0.35f; s.envMod = 0.5f; s.filterDecay = 0.35f; s.voiceMode = st::VoiceMode::legato; s.glide = 0.06f;
        s.attack = 0.003f; s.sustain = 0.8f; s.release = 0.2f; s.lfoDepth = 0.06f; s.lfoRate = 6; s.lfoDest = st::LfoDest::pitch;
    });
    own (lead, "Glass Whistle", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 12; s.osc2Fine = 3; s.osc2Level = 0.5f;
        s.cutoff = 5000; s.resonance = 0.1f; s.envMod = 0.1f; s.attack = 0.04f; s.sustain = 0.9f; s.release = 0.35f;
        s.voiceMode = st::VoiceMode::mono; s.glide = 0.08f; s.lfoDepth = 0.05f; s.lfoRate = 7; s.lfoDest = st::LfoDest::pitch;
    });

    // KEYS
    ps8 (keys, "Electric Piano");
    own (keys, "Soft Keys", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 12; s.osc2Fine = 0; s.osc2Level = 0.35f;
        s.cutoff = 3000; s.resonance = 0.1f; s.envMod = 0.35f; s.filterDecay = 0.5f;
        s.attack = 0.002f; s.decay = 1.4f; s.sustain = 0.2f; s.release = 0.45f;
    });
    own (keys, "Clav", [] (Settings& s)
    {
        s.wave = sy::Wave::square; s.pulseWidth = 0.22f; s.cutoff = 1600; s.resonance = 0.45f; s.envMod = 0.7f; s.filterDecay = 0.15f;
        s.attack = 0.001f; s.decay = 0.35f; s.sustain = 0.1f; s.release = 0.12f;
    });
    own (keys, "Dusty Keys", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 0; s.osc2Fine = 9; s.osc2Level = 0.5f; s.noiseLevel = 0.04f;
        s.cutoff = 1800; s.resonance = 0.05f; s.envMod = 0.3f; s.filterDecay = 0.6f; s.filterType = sy::FilterType::low12;
        s.attack = 0.003f; s.decay = 1.8f; s.sustain = 0.15f; s.release = 0.6f; s.lfoDepth = 0.04f; s.lfoRate = 4; s.lfoDest = st::LfoDest::pitch;
    });
    own (keys, "Stab Keys", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.detune = 0.2f; s.osc2Wave = st::Osc2Wave::square; s.osc2Semi = 12; s.osc2Fine = 0; s.osc2Level = 0.3f;
        s.cutoff = 2400; s.resonance = 0.3f; s.envMod = 0.6f; s.filterDecay = 0.2f; s.attack = 0.001f; s.decay = 0.4f; s.sustain = 0.0f; s.release = 0.2f;
    });

    // PAD
    for (auto* n : { "Warm Pad", "Ambient Glass", "Dark Drone", "Trance Gate Pad" })
        ps8 (pad, n);
    own (pad, "Night Air", [] (Settings& s)
    {
        s.wave = sy::Wave::supersaw; s.detune = 0.45f; s.cutoff = 1400; s.resonance = 0.2f; s.envMod = 0.1f; s.noiseLevel = 0.03f;
        s.attack = 1.2f; s.sustain = 0.9f; s.release = 2.4f; s.width = 1.0f; s.lfoDepth = 0.25f; s.lfoRate = 2; s.lfoDest = st::LfoDest::filter;
    });
    own (pad, "Choir Pad", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 12; s.osc2Fine = 6; s.osc2Level = 0.3f;
        s.filterType = sy::FilterType::band; s.cutoff = 1100; s.resonance = 0.45f; s.envMod = 0.05f;
        s.attack = 0.6f; s.sustain = 0.9f; s.release = 1.5f; s.width = 0.8f; s.lfoDepth = 0.05f; s.lfoRate = 5; s.lfoDest = st::LfoDest::pitch;
    });

    // PLUCK
    for (auto* n : { "Pluck", "Supersaw Stab", "Marimba" })
        ps8 (pluck, n);
    own (pluck, "Kalimba", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 24; s.osc2Fine = 0; s.osc2Level = 0.25f;
        s.cutoff = 4000; s.resonance = 0.15f; s.envMod = 0.4f; s.filterDecay = 0.1f; s.attack = 0.001f; s.decay = 0.6f; s.sustain = 0.0f; s.release = 0.4f;
    });
    own (pluck, "Tropic Pluck", [] (Settings& s)
    {
        s.wave = sy::Wave::square; s.pulseWidth = 0.3f; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 12; s.osc2Fine = 5; s.osc2Level = 0.35f;
        s.cutoff = 1500; s.resonance = 0.5f; s.envMod = 0.75f; s.filterDecay = 0.12f; s.attack = 0.001f; s.decay = 0.3f; s.sustain = 0.0f; s.release = 0.25f; s.width = 0.5f;
    });

    // ORGAN
    ps8 (organ, "Organ");
    own (organ, "Drawbar", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 12; s.osc2Fine = 0; s.osc2Level = 0.7f; s.subLevel = 0.35f;
        s.cutoff = 6000; s.resonance = 0.0f; s.envMod = 0.0f; s.attack = 0.004f; s.sustain = 1.0f; s.release = 0.08f;
        s.lfoDepth = 0.08f; s.lfoRate = 6; s.lfoDest = st::LfoDest::pitch;
    });
    own (organ, "Perc Organ", [] (Settings& s)
    {
        s.wave = sy::Wave::square; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 19; s.osc2Fine = 0; s.osc2Level = 0.5f;
        s.cutoff = 3500; s.envMod = 0.5f; s.filterDecay = 0.18f; s.attack = 0.002f; s.sustain = 0.85f; s.release = 0.07f;
    });
    own (organ, "Church", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.detune = 0.15f; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = -12; s.osc2Fine = 0; s.osc2Level = 0.6f;
        s.cutoff = 2200; s.resonance = 0.05f; s.envMod = 0.0f; s.attack = 0.12f; s.sustain = 1.0f; s.release = 0.9f;
    });

    // STRINGS
    ps8 (strings, "Strings");
    own (strings, "Slow Strings", [] (Settings& s)
    {
        s.wave = sy::Wave::supersaw; s.detune = 0.35f; s.cutoff = 2800; s.resonance = 0.1f; s.envMod = 0.1f;
        s.attack = 0.9f; s.sustain = 0.9f; s.release = 1.6f; s.width = 0.8f; s.lfoDepth = 0.05f; s.lfoRate = 5;
    });
    own (strings, "Pizzicato", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.detune = 0.25f; s.cutoff = 1800; s.envMod = 0.6f; s.filterDecay = 0.12f;
        s.attack = 0.002f; s.decay = 0.22f; s.sustain = 0.0f; s.release = 0.18f; s.width = 0.6f;
    });
    own (strings, "Analog Strings", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.detune = 0.5f; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 12; s.osc2Fine = 6; s.osc2Level = 0.3f;
        s.cutoff = 4200; s.resonance = 0.15f; s.envMod = 0.05f; s.attack = 0.3f; s.sustain = 0.85f; s.release = 0.9f; s.width = 1.0f;
    });

    // BELLS
    ps8 (bells, "Bell Pluck");
    own (bells, "Music Box", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 24; s.osc2Fine = 7; s.osc2Level = 0.45f; s.octave = 1;
        s.cutoff = 6000; s.resonance = 0.0f; s.envMod = 0.2f; s.filterDecay = 0.3f; s.attack = 0.001f; s.decay = 1.2f; s.sustain = 0.0f; s.release = 1.0f;
    });
    own (bells, "Temple Bell", [] (Settings& s)
    {
        s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 19; s.osc2Fine = 14; s.osc2Level = 0.6f;
        s.cutoff = 3800; s.resonance = 0.2f; s.envMod = 0.3f; s.filterDecay = 0.8f; s.attack = 0.002f; s.decay = 3.0f; s.sustain = 0.0f; s.release = 2.5f; s.width = 0.6f;
    });
    own (bells, "Glass Mallet", [] (Settings& s)
    {
        s.wave = sy::Wave::square; s.pulseWidth = 0.12f; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 12; s.osc2Fine = 0; s.osc2Level = 0.5f;
        s.cutoff = 5200; s.resonance = 0.25f; s.envMod = 0.35f; s.filterDecay = 0.25f; s.attack = 0.001f; s.decay = 0.9f; s.sustain = 0.0f; s.release = 0.7f;
    });

    // BRASS
    ps8 (brass, "Brass");
    own (brass, "Horn Stab", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.detune = 0.25f; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 0; s.osc2Fine = 8; s.osc2Level = 0.5f;
        s.cutoff = 1300; s.resonance = 0.2f; s.envMod = 0.7f; s.filterAttack = 0.04f; s.filterDecay = 0.25f; s.filterSustain = 0.3f;
        s.attack = 0.02f; s.decay = 0.3f; s.sustain = 0.6f; s.release = 0.15f;
    });
    own (brass, "Soft Brass", [] (Settings& s)
    {
        s.wave = sy::Wave::saw; s.detune = 0.3f; s.cutoff = 1000; s.resonance = 0.1f; s.envMod = 0.5f; s.filterAttack = 0.15f; s.filterDecay = 0.6f; s.filterSustain = 0.5f;
        s.attack = 0.12f; s.sustain = 0.85f; s.release = 0.4f; s.width = 0.5f;
    });
    own (brass, "Big Synth Brass", [] (Settings& s)
    {
        s.wave = sy::Wave::supersaw; s.detune = 0.3f; s.cutoff = 1500; s.resonance = 0.25f; s.envMod = 0.65f; s.filterAttack = 0.03f; s.filterDecay = 0.4f; s.filterSustain = 0.35f;
        s.attack = 0.01f; s.sustain = 0.8f; s.release = 0.3f; s.width = 0.9f; s.drive = 0.15f;
    });

    // ---- BASS family (the BASS tool) ----
    // SUB
    for (auto* n : { "Deep Sub", "Soft Round" })
        bx1 (sub, n);
    own (sub, "Pure Sub", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 0; s.osc2Fine = 0; s.osc2Level = 0.8f;
        s.cutoff = 300; s.resonance = 0.0f; s.envMod = 0.05f; s.subLevel = 0.3f; s.drive = 0.05f; s.release = 0.08f;
    });
    own (sub, "Warm Sub", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::square; s.pulseWidth = 0.45f; s.cutoff = 260; s.resonance = 0.15f; s.envMod = 0.2f; s.filterDecay = 0.25f; s.drive = 0.25f;
    });

    // 808: the BX-1's boom, and the phone's own
    bx1 (b808, "808 Boom", [] (Settings& s) { s.glide = 0.03f; s.release = 0.3f; s.sustain = 0.0f; s.decay = 1.4f; });
    auto boom = [] (Settings& s, float decay, float drive, float glide)
    {
        s.wave = sy::Wave::triangle; s.voiceMode = st::VoiceMode::mono; s.octave = -1;
        s.osc2Wave = st::Osc2Wave::sine; s.osc2Semi = 0; s.osc2Fine = 0; s.osc2Level = 0.9f; s.subLevel = 0.5f;
        s.cutoff = 260; s.resonance = 0.05f; s.envMod = 0.25f; s.filterDecay = 0.08f;
        s.attack = 0.001f; s.decay = decay; s.sustain = 0.0f; s.release = decay * 0.4f; s.drive = drive; s.glide = glide; s.volume = 0.8f;
    };
    own (b808, "808 Long", [=] (Settings& s) { boom (s, 2.2f, 0.2f, 0.0f); });
    own (b808, "808 Punch", [=] (Settings& s) { boom (s, 0.5f, 0.55f, 0.0f); s.envMod = 0.6f; s.filterDecay = 0.04f; });
    own (b808, "808 Glide", [=] (Settings& s) { boom (s, 1.6f, 0.35f, 0.09f); s.voiceMode = st::VoiceMode::legato; s.sustain = 0.6f; });
    own (b808, "808 Dirty", [=] (Settings& s) { boom (s, 1.1f, 0.85f, 0.04f); s.cutoff = 900; s.envMod = 0.4f; });

    // FUNK
    for (auto* n : { "Rubber Funk", "Disco Octave" })
        bx1 (funk, n);
    own (funk, "Slap Synth", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::square; s.pulseWidth = 0.3f; s.cutoff = 700; s.resonance = 0.45f; s.envMod = 0.8f; s.filterDecay = 0.12f;
        s.decay = 0.25f; s.sustain = 0.5f; s.drive = 0.2f;
    });
    own (funk, "Boogie", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::saw; s.cutoff = 550; s.resonance = 0.5f; s.envMod = 0.65f; s.filterDecay = 0.2f; s.glide = 0.04f; s.voiceMode = st::VoiceMode::legato;
    });

    // ACID
    for (auto* n : { "Acid 303", "Square Acid", "Screaming Acid" })
        bx1 (acid, n);
    own (acid, "Rubber Acid", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::saw; s.cutoff = 380; s.resonance = 0.85f; s.envMod = 0.85f; s.filterDecay = 0.18f; s.glide = 0.07f;
        s.voiceMode = st::VoiceMode::legato; s.drive = 0.35f;
    });

    // GROWL
    for (auto* n : { "Growl", "Wobble Bass", "Reese", "Distorted Square" })
        bx1 (growl, n);
    ps8 (growl, "Wobble", [] (Settings& s) { s.voiceMode = st::VoiceMode::mono; });

    // ANALOG: the classic synth basses
    own (analog, "Fat Analog", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::saw; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 0; s.osc2Fine = 9; s.osc2Level = 0.7f;
        s.cutoff = 650; s.resonance = 0.3f; s.envMod = 0.55f; s.filterDecay = 0.3f; s.filterSustain = 0.25f; s.subLevel = 0.45f;
    });
    own (analog, "Moody Low", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::square; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = -12; s.osc2Fine = 0; s.osc2Level = 0.5f;
        s.cutoff = 420; s.resonance = 0.25f; s.envMod = 0.4f; s.filterDecay = 0.5f; s.filterSustain = 0.2f; s.release = 0.12f;
    });
    own (analog, "Pulse Bass", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::square; s.pulseWidth = 0.18f; s.cutoff = 900; s.resonance = 0.2f; s.envMod = 0.5f; s.filterDecay = 0.2f;
        s.lfoDepth = 0.2f; s.lfoRate = 8; s.lfoDest = st::LfoDest::pulseWidth;
    });

    // PICKED: short, played basses
    bx1 (picked, "Pluck Bass");
    own (picked, "Finger Bass", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::triangle; s.osc2Wave = st::Osc2Wave::saw; s.osc2Semi = 0; s.osc2Fine = 0; s.osc2Level = 0.3f;
        s.cutoff = 800; s.resonance = 0.1f; s.envMod = 0.5f; s.filterDecay = 0.15f; s.decay = 0.6f; s.sustain = 0.2f; s.release = 0.1f; s.drive = 0.1f;
    });
    own (picked, "Muted Pick", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::saw; s.cutoff = 600; s.resonance = 0.2f; s.envMod = 0.7f; s.filterDecay = 0.08f; s.decay = 0.18f; s.sustain = 0.0f; s.release = 0.06f;
    });

    // DIGITAL: bright, modern
    own (digital, "Digital Hollow", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::square; s.osc2Wave = st::Osc2Wave::square; s.osc2Semi = 12; s.osc2Fine = 0; s.osc2Level = 0.35f;
        s.filterType = sy::FilterType::band; s.cutoff = 700; s.resonance = 0.4f; s.envMod = 0.5f; s.filterDecay = 0.2f; s.drive = 0.3f;
    });
    own (digital, "Laser Bass", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::saw; s.cutoff = 2500; s.resonance = 0.55f; s.envMod = -0.0f; s.filterDecay = 0.1f;
        s.glide = 0.12f; s.voiceMode = st::VoiceMode::legato; s.lfoDepth = 0.3f; s.lfoRate = 9; s.lfoDest = st::LfoDest::filter; s.drive = 0.25f;
    });
    own (digital, "Bit Bass", [] (Settings& s)
    {
        s = bassBase(); s.wave = sy::Wave::square; s.pulseWidth = 0.25f; s.cutoff = 3000; s.resonance = 0.0f; s.envMod = 0.0f; s.subLevel = 0.5f;
        s.decay = 0.3f; s.sustain = 0.6f; s.release = 0.04f;
    });

    for (auto& p : out)
    {
        p.settings.arpOn = false;
        p.settings.latch = false;
    }
    return out;
}
}

const std::vector<SynthPreset>& synthPresets()
{
    static const std::vector<SynthPreset> all = build();
    return all;
}

int synthPresetIndex (const juce::String& name)
{
    const auto& all = synthPresets();
    for (int i = 0; i < (int) all.size(); ++i)
        if (all[(size_t) i].name == name)
            return i;
    return -1;
}

std::vector<int> synthPresetsOf (int kind)
{
    std::vector<int> out;
    const auto& all = synthPresets();
    for (int i = 0; i < (int) all.size(); ++i)
        if (all[(size_t) i].kind == kind)
            out.push_back (i);
    return out;
}

int presetShape (int preset)
{
    const auto& all = synthPresets();
    return (int) all[(size_t) juce::jlimit (0, (int) all.size() - 1, preset)].settings.wave;
}

bool synthKnobIsRelease (int preset)
{
    const int k = synthPresets()[(size_t) juce::jlimit (0, (int) synthPresets().size() - 1, preset)].kind;
    return k == pad || k == organ || k == strings || k == brass;
}

static float decayScale (float knob) { return std::exp2 ((juce::jlimit (0.0f, 1.0f, knob) - 0.5f) * 4.0f); }

Settings makeSynthSettings (int preset, const SynthKnobs& k, int octave)
{
    const auto& all = synthPresets();
    auto s = all[(size_t) juce::jlimit (0, (int) all.size() - 1, preset)].settings;
    s.wave = (sy::Wave) juce::jlimit (0, 3, k.shape);
    s.cutoff = juce::jlimit (60.0f, 14000.0f, s.cutoff * std::exp2 ((juce::jlimit (0.0f, 1.0f, k.tone) - 0.5f) * 6.0f));
    const float d = decayScale (k.decay);
    s.decay = juce::jlimit (0.01f, 6.0f, s.decay * d);
    s.release = juce::jlimit (0.01f, 8.0f, s.release * d);
    s.filterDecay = juce::jlimit (0.02f, 4.0f, s.filterDecay * d);
    s.octave = juce::jlimit (-3, 3, s.octave + octave);
    return s;
}

float synthDecaySeconds (int preset, const SynthKnobs& k)
{
    const auto s = makeSynthSettings (preset, k, 0);
    return synthKnobIsRelease (preset) ? s.release : s.decay;
}

//==============================================================================
std::vector<int> SynthPattern::notesIn (int stepIndex) const
{
    std::vector<int> out;
    const double a = stepIndex * kSynthStep - 1.0e-6, b = (stepIndex + 1) * kSynthStep - 1.0e-6;
    for (int i = 0; i < (int) notes.size(); ++i)
        if (notes[(size_t) i].beat >= a && notes[(size_t) i].beat < b)
            out.push_back (i);
    return out;
}

void SynthPattern::clearStep (int stepIndex)
{
    const double a = stepIndex * kSynthStep - 1.0e-6, b = (stepIndex + 1) * kSynthStep - 1.0e-6;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [=] (const SynthNote& n) { return n.beat >= a && n.beat < b; }), notes.end());
}

void SynthPattern::setStep (int stepIndex, int note, float velocity)
{
    clearStep (stepIndex);
    if ((int) notes.size() >= kSynthMaxNotes)
        return;
    SynthNote n;
    n.beat = stepIndex * kSynthStep;
    n.length = kSynthStep * 0.92;
    n.note = juce::jlimit (0, 127, note);
    n.velocity = velocity;
    notes.push_back (n);
}

bool SynthPattern::tieStep (int stepIndex)
{
    bool any = false;
    for (int i : notesIn (stepIndex))
    {
        auto& n = notes[(size_t) i];
        n.length = juce::jmin (lengthBeats(), n.length + kSynthStep);
        any = true;
    }
    return any;
}

bool SynthPattern::tiedInto (int stepIndex) const
{
    const double at = stepIndex * kSynthStep + 0.02;
    for (auto& n : notes)
        if (n.beat < stepIndex * kSynthStep - 1.0e-6 && n.beat + n.length > at)
            return true;
    return false;
}

void SynthPattern::setBars (int b)
{
    bars = b <= 1 ? 1 : b <= 2 ? 2 : b <= 4 ? 4 : 8;
    notes.erase (std::remove_if (notes.begin(), notes.end(), [this] (const SynthNote& n) { return n.beat >= lengthBeats() - 1.0e-6; }), notes.end());
    for (auto& n : notes)
        n.length = juce::jmin (n.length, lengthBeats());
}

juce::var SynthPattern::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("bars", bars);
    juce::Array<juce::var> list;
    for (auto& n : notes)
    {
        juce::Array<juce::var> one { n.beat, n.length, n.note, n.velocity };
        list.add (one);
    }
    o->setProperty ("notes", list);
    return juce::var (o);
}

SynthPattern SynthPattern::fromVar (const juce::var& v)
{
    SynthPattern p;
    if (! v.isObject())
        return p;
    p.setBars ((int) v.getProperty ("bars", 1));
    if (auto* list = v.getProperty ("notes", {}).getArray())
        for (auto& item : *list)
            if (auto* a = item.getArray(); a != nullptr && a->size() >= 4 && (int) p.notes.size() < kSynthMaxNotes)
            {
                SynthNote n;
                n.beat = juce::jlimit (0.0, p.lengthBeats() - 0.001, (double) (*a)[0]);
                n.length = juce::jlimit (0.02, p.lengthBeats(), (double) (*a)[1]);
                n.note = juce::jlimit (0, 127, (int) (*a)[2]);
                n.velocity = juce::jlimit (0.05f, 1.0f, (float) (double) (*a)[3]);
                p.notes.push_back (n);
            }
    return p;
}

//==============================================================================
void SynthCore::prepare (double rate)
{
    engine.prepare (rate);
    liveVersion = -1;
}

void SynthCore::setSettings (const Settings& s)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pending = s;
    ++version;
}

void SynthCore::render (float* left, float* right, int n, const juce::MidiBuffer& midi, const st::Clock& clock)
{
    if (liveVersion != version.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked())
        {
            engine.setSettings (pending);
            liveVersion = version.load();
        }
    }
    engine.render (left, right, n, midi, clock);
}

//==============================================================================
SynthEngine::SynthEngine (BeatsEngine& b) : beats (b)
{
    keyMidi.ensureSize (4096);
    patMidi.ensureSize (4096);
    tap.ensureSize (8192);
    prepare (44100.0);   // ready before the sound card says its rate (and for the screenshots)
}

void SynthEngine::setSettings (const Settings& s)
{
    keys.setSettings (s);
    auto p = s;
    p.arpOn = false;
    p.latch = false;
    pat.setSettings (p);
}

void SynthEngine::setPattern (const SynthPattern& p)
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    pendingCount = juce::jmin (kSynthMaxNotes, (int) p.notes.size());
    for (int i = 0; i < pendingCount; ++i)
        pendingNotes[(size_t) i] = p.notes[(size_t) i];
    pendingLength = p.lengthBeats();
    ++patternVersion;
}

void SynthEngine::keyDown (int note, float velocity)
{
    const auto scope = keyFifo.write (1);
    if (scope.blockSize1 > 0) keyQueue[(size_t) scope.startIndex1] = { note, velocity, true };
}

void SynthEngine::keyUp (int note)
{
    const auto scope = keyFifo.write (1);
    if (scope.blockSize1 > 0) keyQueue[(size_t) scope.startIndex1] = { note, 0.0f, false };
}

void SynthEngine::allKeysUp()
{
    const auto scope = keyFifo.write (1);
    if (scope.blockSize1 > 0) keyQueue[(size_t) scope.startIndex1] = { -1, 0.0f, false };
}

int SynthEngine::readPlayed (Played* out, int max)
{
    const auto scope = playedFifo.read (juce::jmin (max, playedFifo.getNumReady()));
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i)
        out[n++] = playedQueue[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i)
        out[n++] = playedQueue[(size_t) (scope.startIndex2 + i)];
    return n;
}

void SynthEngine::prepare (double rate)
{
    sampleRate = rate;
    keys.prepare (rate);
    pat.prepare (rate);
    numSounding = 0;
    wasRunning = false;
}

void SynthEngine::render (float* left, float* right, int n)
{
    renderAt (left, right, n, beats.clockRunning(), beats.clockBeat(), beats.getBpm());
}

void SynthEngine::renderAt (float* left, float* right, int n, bool running, double beat, double bpm)
{
    if (livePatternVersion != patternVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType sl (patternLock);
        if (sl.isLocked())
        {
            liveNotes = pendingNotes;
            liveCount = pendingCount;
            liveLength = pendingLength;
            livePatternVersion = patternVersion.load();
        }
    }

    // the keys, as they came in
    keyMidi.clear();
    {
        const auto scope = keyFifo.read (keyFifo.getNumReady());
        auto take = [this] (const KeyEvent& e)
        {
            if (e.note < 0)
                keyMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            else if (e.on)
                keyMidi.addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, e.note), juce::jlimit (0.05f, 1.0f, e.velocity)), 0);
            else
                keyMidi.addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, e.note)), 0);
        };
        for (int i = 0; i < scope.blockSize1; ++i)
            take (keyQueue[(size_t) (scope.startIndex1 + i)]);
        for (int i = 0; i < scope.blockSize2; ++i)
            take (keyQueue[(size_t) (scope.startIndex2 + i)]);
    }

    // the pattern's notes in this block, at the sample they fall on
    patMidi.clear();
    const double bps = bpm / (60.0 * sampleRate);
    const double start = beat, end = beat + n * bps;
    auto offset = [&] (double t) { return juce::jlimit (0, n - 1, (int) ((t - start) / bps)); };
    if (! running)
    {
        if (numSounding > 0 || wasRunning)
            patMidi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        numSounding = 0;
        step = -1;
    }
    else
    {
        if (! wasRunning)
            numSounding = 0;
        for (int i = 0; i < numSounding;)
            if (sounding[(size_t) i].off < end)
            {
                patMidi.addEvent (juce::MidiMessage::noteOff (1, sounding[(size_t) i].note), offset (sounding[(size_t) i].off));
                sounding[(size_t) i] = sounding[(size_t) --numSounding];
            }
            else
                ++i;
        const double from = playFrom.load();
        const bool muted = patternMuted.load();
        if (muted && ! wasMuted)
        {
            for (int i = 0; i < numSounding; ++i)
                patMidi.addEvent (juce::MidiMessage::noteOff (1, sounding[(size_t) i].note), 0);
            numSounding = 0;
        }
        wasMuted = muted;
        if (! muted && liveCount > 0 && liveLength > 0)
        {
            for (int i = 0; i < liveCount; ++i)
            {
                const auto& h = liveNotes[(size_t) i];
                const double k = std::ceil ((start - h.beat) / liveLength - 1.0e-9);
                const double t = h.beat + k * liveLength;
                if (t < start || t >= end || t < from - 1.0e-9 || (t < h.skipUntil && t > h.skipUntil - 0.5))
                    continue;
                const int at = offset (t);
                patMidi.addEvent (juce::MidiMessage::noteOn (1, h.note, juce::jlimit (0.05f, 1.0f, h.velocity)), at);
                patternNotesPlayed = patternNotesPlayed.load() + 1;
                const double off = t + juce::jmax (0.02, h.length);
                if (off < end)
                    patMidi.addEvent (juce::MidiMessage::noteOff (1, h.note), juce::jmax (at, offset (off)));
                else if (numSounding < (int) sounding.size())
                    sounding[(size_t) numSounding++] = { h.note, off };
            }
            step = beat >= from ? (int) ((long long) std::floor (std::fmod (beat, liveLength) / kSynthStep + 1.0e-9)) : -1;
        }
        else
            step = -1;
    }
    wasRunning = running;

    st::Clock clock;
    clock.playing = running;
    clock.ppq = beat;
    clock.bpm = bpm;

    tap.clear();
    keys.engine.noteTap = capture.load() ? &tap : nullptr;
    keys.render (left, right, n, keyMidi, clock);
    keys.engine.noteTap = nullptr;
    pat.render (left, right, n, patMidi, clock);

    if (capture.load() && running)
        for (const auto meta : tap)
        {
            const auto m = meta.getMessage();
            if (! (m.isNoteOn() || m.isNoteOff()))
                continue;
            const auto scope = playedFifo.write (1);
            if (scope.blockSize1 > 0)
                playedQueue[(size_t) scope.startIndex1] = { beat + meta.samplePosition * bps, m.getNoteNumber(), m.getFloatVelocity(), m.isNoteOn() };
        }

    float p = peak.load();
    for (int i = 0; i < n; ++i)
        p = juce::jmax (p, std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);
    peak = p;
}

//==============================================================================
SynthPart::SynthPart (BeatsEngine& b, int fam) : family (fam), eng (b)
{
    reset();
}

void SynthPart::reset()
{
    preset = juce::jmax (0, synthPresetIndex (family == bassFamily ? "Rubber Funk" : "Electric Piano"));
    knobs = {};
    knobs.shape = presetShape (preset);
    octave = 0;
    mode = keysMode;
    chromatic = false;
    chords = false;
    quantize = 0;
    arpStyle = 0;
    arpRate = 1;
    arpHold = false;
    live = true;
    pattern = {};
    stashed = false;
    takeOn = false;
    openAt.fill (-1.0);
    apply();
    patternChanged();
}

int SynthPart::kind() const
{
    return synthPresets()[(size_t) juce::jlimit (0, (int) synthPresets().size() - 1, preset)].kind;
}

void SynthPart::setPreset (int index)
{
    preset = juce::jlimit (0, (int) synthPresets().size() - 1, index);
    knobs.tone = 0.5f;
    knobs.decay = 0.5f;
    knobs.shape = presetShape (preset);
    apply();
}

void SynthPart::stepPreset (int delta)
{
    const auto list = synthPresetsOf (kind());
    int at = 0;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i] == preset)
            at = i;
    const int n = (int) list.size();
    setPreset (list[(size_t) (((at + delta) % n + n) % n)]);
}

void SynthPart::apply()
{
    eng.setPatternMuted (! live);
    auto s = makeSynthSettings (preset, knobs, octave);
    s.arpOn = mode == arpMode;
    s.arpMode = arpStyle == 0 ? st::ArpMode::up : arpStyle == 1 ? st::ArpMode::down : st::ArpMode::upDown;
    s.arpSpeed = arpRate == 0 ? 10 : 13;   // ArpRates: 1/8, 1/16
    s.arpSync = true;
    s.arpGate = 0.6f;
    s.arpOctaves = 1;
    s.latch = arpHold && live;   // OFF lets a held arp go
    eng.setSettings (s);
    eng.setEcho (knobs.echo);
}

void SynthPart::patternChanged()
{
    eng.setPattern (pattern);
}

int SynthPart::keyNote (int key, const SongKey& k) const
{
    const int base = 48 + 12 * octave;
    if (! chromatic)
        return base + k.root + k.degreeSemis (keyDegree (key));
    // piano order: the 8 white keys, then the 5 black ones
    static constexpr int whites[8] = { 0, 2, 4, 5, 7, 9, 11, 12 }, blacks[5] = { 1, 3, 6, 8, 10 };
    key = juce::jlimit (0, 12, key);
    return base + (key < 8 ? whites[key] : blacks[key - 8]);
}

std::vector<int> SynthPart::keyNotes (int key, const SongKey& k) const
{
    if (chromatic || ! chords)
        return { keyNote (key, k) };
    const int base = 48 + 12 * octave + k.root, d = keyDegree (key);
    return { base + k.degreeSemis (d), base + k.degreeSemis (d + 2), base + k.degreeSemis (d + 4) };
}

juce::var SynthPart::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("sound", synthPresets()[(size_t) preset].name);
    o->setProperty ("tone", knobs.tone);
    o->setProperty ("shape", knobs.shape);
    o->setProperty ("decay", knobs.decay);
    o->setProperty ("echo", knobs.echo);
    o->setProperty ("octave", octave);
    o->setProperty ("mode", mode);
    o->setProperty ("chromatic", chromatic);
    o->setProperty ("chords", chords);
    o->setProperty ("quantize", quantize);
    o->setProperty ("arpStyle", arpStyle);
    o->setProperty ("arpRate", arpRate);
    o->setProperty ("arpHold", arpHold);
    o->setProperty ("live", live);
    o->setProperty ("pattern", pattern.toVar());
    return juce::var (o);
}

void SynthPart::fromVar (const juce::var& v)
{
    reset();
    if (! v.isObject())
        return;
    const int p = synthPresetIndex (v.getProperty ("sound", {}).toString());
    if (p >= 0)
        preset = p;
    knobs.tone = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("tone", 0.5));
    knobs.shape = juce::jlimit (0, 3, (int) v.getProperty ("shape", presetShape (preset)));
    knobs.decay = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("decay", 0.5));
    knobs.echo = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("echo", 0.0));
    octave = juce::jlimit (-2, 2, (int) v.getProperty ("octave", 0));
    mode = juce::jlimit (0, 2, (int) v.getProperty ("mode", 0));
    chromatic = (bool) v.getProperty ("chromatic", false);
    chords = (bool) v.getProperty ("chords", false);
    quantize = juce::jlimit (0, 2, (int) v.getProperty ("quantize", 0));
    arpStyle = juce::jlimit (0, 2, (int) v.getProperty ("arpStyle", 0));
    arpRate = juce::jlimit (0, 1, (int) v.getProperty ("arpRate", 1));
    arpHold = (bool) v.getProperty ("arpHold", false);
    live = (bool) v.getProperty ("live", true);
    pattern = SynthPattern::fromVar (v.getProperty ("pattern", {}));
    apply();
    patternChanged();
}

//==============================================================================
void SynthPart::setLive (bool on)
{
    live = on;
    eng.setPatternMuted (! on);
    if (! on)
        eng.allKeysUp();
    apply();
}

void SynthPart::startTake (int bars, double from, double to)
{
    if (! live)
        setLive (true);   // REC: you hear what you play, and it plays round
    stash = pattern;
    stashed = true;
    pattern = {};
    pattern.setBars (bars);
    takeFrom = from;
    takeTo = to;
    takeEnd = to;
    openAt.fill (-1.0);
    takeOn = true;
    SynthEngine::Played junk[64];
    while (eng.readPlayed (junk, 64) > 0) {}
    eng.setCapture (true);
    eng.setPlayFrom (to);
    patternChanged();
}

void SynthPart::addTakeNote (double onBeat, double offBeat, int note, float velocity)
{
    if ((int) pattern.notes.size() >= kSynthMaxNotes)
        return;
    const double grid = quantize == 0 ? kSynthStep : quantize == 1 ? 0.5 : 0.0;
    const double q = grid > 0 ? std::round (onBeat / grid) * grid : onBeat;
    const double len = pattern.lengthBeats();
    SynthNote n;
    n.beat = std::fmod (q, len);
    if (n.beat < 0)
        n.beat += len;
    if (n.beat > len - 1.0e-6)
        n.beat = 0;
    n.length = juce::jlimit (0.06, len, offBeat - onBeat);
    n.note = note;
    n.velocity = velocity;
    // snapped forward to where the pattern plays from: it sounded already, so the pattern skips it once
    if (q >= takeTo - 1.0e-6)
        n.skipUntil = q + 0.01;
    pattern.notes.push_back (n);
}

void SynthPart::serviceTake()
{
    if (! takeOn)
        return;
    SynthEngine::Played got[256];
    bool changed = false;
    for (int n; (n = eng.readPlayed (got, 256)) > 0;)
        for (int i = 0; i < n; ++i)
        {
            const auto& e = got[i];
            const int note = juce::jlimit (0, 127, e.note);
            if (e.on)
            {
                if (e.beat >= takeFrom - kSynthStep && e.beat < takeEnd)
                {
                    openAt[(size_t) note] = e.beat;
                    openVel[(size_t) note] = e.velocity;
                }
            }
            else if (openAt[(size_t) note] >= 0)
            {
                addTakeNote (openAt[(size_t) note], juce::jmin (e.beat, takeEnd + 2.0), note, openVel[(size_t) note]);
                openAt[(size_t) note] = -1.0;
                changed = true;
            }
        }
    if (changed)
        patternChanged();
}

void SynthPart::endTake()
{
    if (! takeOn)
        return;
    serviceTake();
    for (int note = 0; note < 128; ++note)
        if (openAt[(size_t) note] >= 0)
        {
            addTakeNote (openAt[(size_t) note], juce::jmax (openAt[(size_t) note] + 0.06, juce::jmin (takeEnd, openAt[(size_t) note] + pattern.lengthBeats())), note, openVel[(size_t) note]);
            openAt[(size_t) note] = -1.0;
        }
    takeOn = false;
    eng.setCapture (false);
    eng.allKeysUp();   // a key still held now plays from the pattern, not twice
    patternChanged();
}

void SynthPart::clearKeepingUndo()
{
    stash = pattern;
    stashed = true;
    pattern.notes.clear();
    patternChanged();
}

void SynthPart::restoreStash()
{
    if (! stashed)
        return;
    pattern = stash;
    stashed = false;
    patternChanged();
}
}

//==============================================================================
namespace pocket
{
juce::String synthSelfCheck (juce::String& report)
{
    juce::StringArray problems;
    BeatsEngine clock;
    const double sr = 44100.0, bpm = 120.0;
    const int block = 512;
    std::vector<float> l (block), r (block);

    // a key sounds at its pitch: A3 (the first key of A minor) is 220 Hz
    {
        SynthPart part (clock);
        SongKey am { 9, true };
        const int n0 = part.keyNote (0, am), n2 = part.keyNote (2, am), n7 = part.keyNote (7, am);
        part.chromatic = true;
        const int c0 = part.keyNote (0, am), cs = part.keyNote (8, am);
        part.chromatic = false;
        report << "keys in A minor: key 1 = " << SongKey::noteName (n0) << ", key 3 = " << SongKey::noteName (n2) << ", key 8 = " << SongKey::noteName (n7)
               << "; chromatic key 1 = " << SongKey::noteName (c0) << ", first black = " << SongKey::noteName (cs) << "\n";
        if (n0 != 57 || n2 != 60 || n7 != 69 || c0 != 48 || cs != 49)
            problems.add ("the keys don't follow the song's key (A minor gave " + juce::String (n0) + ", " + juce::String (n2) + ", " + juce::String (n7) + ")");
        // the top row goes on up the scale; CHORDS: key 1 is A minor (A C E), key 2 is B diminished (B D F), all in the key
        const int n14 = part.keyNote (14, am), n8 = part.keyNote (8, am);
        part.chords = true;
        const auto c1 = part.keyNotes (0, am), c2 = part.keyNotes (1, am);
        part.chords = false;
        if (n14 != 79 || n8 != 69 || c1 != std::vector<int> { 57, 60, 64 } || c2 != std::vector<int> { 59, 62, 65 } || part.keyNotes (0, am).size() != 1)
            problems.add ("the second row of keys or CHORDS don't follow the song's key");

        SynthEngine e (clock);
        e.prepare (sr);
        Settings s;
        s.wave = sy::Wave::triangle;
        s.cutoff = 14000.0f;
        s.resonance = 0.0f;
        s.envMod = 0.0f;
        s.attack = 0.002f;
        s.sustain = 1.0f;
        s.lfoDepth = 0.0f;
        e.setSettings (s);
        e.keyDown (n0, 0.9f);
        std::vector<float> all;
        for (int done = 0; done < (int) sr; done += block)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.renderAt (l.data(), r.data(), block, false, 0.0, bpm);
            all.insert (all.end(), l.begin(), l.end());
        }
        int crossings = 0;
        const int a = (int) (0.2 * sr), b = (int) (0.7 * sr);
        int first = -1, last = -1;
        for (int i = a + 1; i < b; ++i)
            if (all[(size_t) i - 1] <= 0.0f && all[(size_t) i] > 0.0f)
            {
                if (first < 0)
                    first = i;
                last = i;
                ++crossings;
            }
        const double hz = crossings > 1 ? (crossings - 1) * sr / (double) (last - first) : 0.0;
        float peak = 0;
        for (auto v : all)
            peak = juce::jmax (peak, std::abs (v));
        report << "pitch: key 1 of A minor sounds at " << juce::String (hz, 1) << " Hz (220 expected), peak " << juce::String (peak, 2) << "\n";
        if (std::abs (hz - 220.0) > 3.0 || peak < 0.05f)
            problems.add ("a key played at " + juce::String (hz, 1) + " Hz, not 220 (peak " + juce::String (peak, 3) + ")");
    }

    // the pattern's notes start on their steps
    {
        SynthEngine e (clock);
        e.prepare (sr);
        Settings s;
        s.wave = sy::Wave::square;
        s.cutoff = 6000.0f;
        s.attack = 0.001f;
        s.decay = 0.05f;
        s.sustain = 1.0f;
        s.release = 0.01f;
        e.setSettings (s);
        SynthPattern p;
        p.setBars (1);
        p.setStep (0, 48);
        p.setStep (6, 55);
        e.setPattern (p);
        std::vector<float> all;
        double beat = 0;
        const double bps = bpm / (60.0 * sr);
        for (int done = 0; done < (int) (2.6 * sr); done += block)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.renderAt (l.data(), r.data(), block, true, beat, bpm);
            beat += block * bps;
            all.insert (all.end(), l.begin(), l.end());
        }
        std::vector<double> onsets;
        long long lastLoud = -100000;
        for (size_t i = 0; i < all.size(); ++i)
            if (std::abs (all[i]) > 0.01f)
            {
                if ((long long) i - lastLoud > (long long) (0.03 * sr))
                    onsets.push_back ((double) i / sr);
                lastLoud = (long long) i;
            }
        juce::String got;
        for (auto t : onsets)
            got << (got.isEmpty() ? "" : ", ") << juce::String (t * 1000.0, 1) << " ms";
        report << "pattern: steps 1 and 7 of a 1-bar pattern at 120 BPM start at " << got << " (0, 750 and 2000 ms expected)\n";
        const double want[3] = { 0.0, 0.75, 2.0 };
        bool ok = onsets.size() == 3;
        for (size_t i = 0; ok && i < 3; ++i)
            ok = std::abs (onsets[i] - want[i]) < 0.004;
        if (! ok)
            problems.add ("the pattern's notes didn't start on their steps (" + got + ")");
    }

    // ARP: held keys play one after another on the 1/16 grid, upwards
    {
        SynthPart part (clock);
        auto& e = part.engine();
        e.prepare (sr);
        part.mode = SynthPart::arpMode;
        part.arpStyle = 0;
        part.arpRate = 1;
        part.apply();
        e.setCapture (true);
        for (int n : { 64, 57, 60 })
            e.keyDown (n, 0.8f);
        double beat = 0;
        const double bps = bpm / (60.0 * sr);
        for (int done = 0; done < (int) (1.1 * sr); done += block)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.renderAt (l.data(), r.data(), block, true, beat, bpm);
            beat += block * bps;
        }
        SynthEngine::Played got[256];
        const int n = e.readPlayed (got, 256);
        juce::String seq;
        bool onGrid = true;
        int ons = 0;
        const int wantNotes[4] = { 57, 60, 64, 57 };
        bool order = true;
        for (int i = 0; i < n; ++i)
            if (got[i].on)
            {
                const double off = std::abs (got[i].beat / 0.25 - std::round (got[i].beat / 0.25)) * 0.25 / bps;
                onGrid = onGrid && off <= 1.5;
                if (ons < 4)
                    order = order && got[i].note == wantNotes[ons];
                if (ons < 6)
                    seq << (seq.isEmpty() ? "" : ", ") << SongKey::noteName (got[i].note) << "@" << juce::String (got[i].beat, 3);
                ++ons;
            }
        report << "arp (1/16 up, 120 BPM): " << seq << ", " << ons << " notes in 2.2 beats\n";
        if (! onGrid || ! order || ons < 8 || ons > 10)
            problems.add ("the arp didn't play up on the 1/16 grid (" + seq + ")");
    }
    return problems.joinIntoString ("; ");
}
}
