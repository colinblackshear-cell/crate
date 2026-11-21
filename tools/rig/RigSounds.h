#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

// Named sounds for the Rig's own Bass and Synth: a kind ("Leads", "Pads"...), a name, and the
// settings that make it. Picking one puts every sound setting back to its default first, then
// applies the sound's, so sounds don't leak into each other. The performance settings (the
// pattern, step rate, volume, the arp) stay as they are. The name is kept in the tool's state.
namespace RigSounds
{
struct Sound
{
    const char* kind;
    const char* name;
    std::vector<std::pair<const char*, float>> values;
    // The sound's level trim in dB (set by the level check, RigLevelTests): it brings every sound to
    // the same loudness at its default settings, without touching the Volume knob.
    float trimDb = 0.0f;
};

// Choice indexes. Synth wave: Saw, Square, Triangle, Supersaw. Bass wave: Saw, Square, Triangle.
// Rates (SyncRates): 0 4 bars, 1 2 bars, 2 1 bar, 3 1/2, 4 1/4, 5 1/8, 6 1/16, 7 1/32.
inline const std::vector<Sound>& synth()
{
    static const std::vector<Sound> sounds {
        { "Start", "Init Supersaw", {}, 7.4f },
        { "Leads", "Big Supersaw", { { "wave", 3 }, { "detune", 0.55f }, { "cutoff", 6000 }, { "resonance", 0.2f }, { "envMod", 0.3f }, { "release", 0.4f } }, 7.1f },
        { "Leads", "Square Lead", { { "wave", 1 }, { "cutoff", 3500 }, { "resonance", 0.35f }, { "envMod", 0.45f }, { "filterDecay", 0.25f }, { "sustain", 0.8f } }, 1.6f },
        { "Leads", "Chip Lead", { { "wave", 1 }, { "cutoff", 12000 }, { "resonance", 0.0f }, { "envMod", 0.0f }, { "attack", 0.001f }, { "decay", 0.1f }, { "sustain", 0.9f }, { "release", 0.05f } }, 0.6f },
        { "Leads", "Screamer", { { "wave", 0 }, { "cutoff", 1800 }, { "resonance", 0.8f }, { "envMod", 0.75f }, { "filterDecay", 0.5f }, { "sustain", 0.85f } }, 5.1f },
        { "Leads", "Soft Flute", { { "wave", 2 }, { "cutoff", 2500 }, { "resonance", 0.1f }, { "envMod", 0.1f }, { "attack", 0.06f }, { "sustain", 0.9f }, { "release", 0.35f }, { "lfoDepth", 0.12f }, { "lfoRate", 6 } }, 3.3f },
        { "Plucks", "Pluck", { { "wave", 0 }, { "cutoff", 900 }, { "resonance", 0.35f }, { "envMod", 0.8f }, { "filterDecay", 0.18f }, { "decay", 0.25f }, { "sustain", 0.0f }, { "release", 0.2f } }, 12.5f },
        { "Plucks", "Bell Pluck", { { "wave", 2 }, { "octave", 1 }, { "cutoff", 4000 }, { "envMod", 0.5f }, { "filterDecay", 0.3f }, { "decay", 0.6f }, { "sustain", 0.0f }, { "release", 0.7f } }, 7.5f },
        { "Plucks", "Supersaw Stab", { { "wave", 3 }, { "detune", 0.45f }, { "cutoff", 1400 }, { "envMod", 0.85f }, { "filterDecay", 0.15f }, { "decay", 0.2f }, { "sustain", 0.1f }, { "release", 0.15f } }, 15.0f },
        { "Plucks", "Marimba", { { "wave", 2 }, { "cutoff", 2000 }, { "envMod", 0.6f }, { "filterDecay", 0.08f }, { "attack", 0.001f }, { "decay", 0.18f }, { "sustain", 0.0f }, { "release", 0.12f } }, 10.8f },
        { "Pads", "Warm Pad", { { "wave", 3 }, { "detune", 0.5f }, { "cutoff", 1500 }, { "resonance", 0.15f }, { "envMod", 0.15f }, { "attack", 0.8f }, { "sustain", 0.85f }, { "release", 1.8f } }, 7.6f },
        { "Pads", "Strings", { { "wave", 0 }, { "detune", 0.3f }, { "cutoff", 3500 }, { "envMod", 0.1f }, { "attack", 0.35f }, { "sustain", 0.9f }, { "release", 1.0f }, { "lfoDepth", 0.06f }, { "lfoRate", 5 } }, 5.8f },
        { "Pads", "Ambient Glass", { { "wave", 2 }, { "octave", 1 }, { "cutoff", 5000 }, { "attack", 1.5f }, { "sustain", 0.8f }, { "release", 3.5f }, { "lfoDepth", 0.25f }, { "lfoRate", 1 } }, 4.8f },
        { "Pads", "Dark Drone", { { "wave", 0 }, { "octave", -1 }, { "cutoff", 400 }, { "resonance", 0.5f }, { "attack", 1.2f }, { "sustain", 1.0f }, { "release", 2.5f }, { "lfoDepth", 0.35f }, { "lfoRate", 0 } }, 7.7f },
        { "Keys and Brass", "Brass", { { "wave", 0 }, { "cutoff", 1200 }, { "resonance", 0.2f }, { "envMod", 0.55f }, { "filterDecay", 0.35f }, { "attack", 0.04f }, { "sustain", 0.75f }, { "release", 0.25f } }, 7.9f },
        { "Keys and Brass", "Organ", { { "wave", 1 }, { "cutoff", 5000 }, { "envMod", 0.0f }, { "attack", 0.003f }, { "sustain", 1.0f }, { "release", 0.06f }, { "lfoDepth", 0.05f }, { "lfoRate", 6 } }, -0.3f },
        { "Keys and Brass", "Electric Piano", { { "wave", 2 }, { "cutoff", 2400 }, { "envMod", 0.4f }, { "filterDecay", 0.6f }, { "decay", 1.2f }, { "sustain", 0.25f }, { "release", 0.5f } }, 8.3f },
        { "Movement", "Wobble", { { "wave", 0 }, { "octave", -1 }, { "cutoff", 700 }, { "resonance", 0.65f }, { "envMod", 0.2f }, { "lfoDepth", 0.8f }, { "lfoRate", 5 }, { "sustain", 1.0f } }, 5.2f },
        { "Movement", "Trance Gate Pad", { { "wave", 3 }, { "detune", 0.5f }, { "cutoff", 3000 }, { "sustain", 1.0f }, { "lfoDepth", 0.7f }, { "lfoRate", 6 }, { "lfoShape", 3 } }, 4.7f },
        { "Movement", "Random Bleeps", { { "wave", 1 }, { "octave", 1 }, { "cutoff", 2500 }, { "resonance", 0.6f }, { "lfoDepth", 0.7f }, { "lfoRate", 6 }, { "lfoShape", 4 }, { "sustain", 0.6f } }, 3.7f },
    };
    return sounds;
}

inline const std::vector<Sound>& bass()
{
    static const std::vector<Sound> sounds {
        { "Start", "Init Bass", {}, -8.7f },
        { "Acid", "Acid 303", { { "wave", 0 }, { "cutoff", 350 }, { "resonance", 0.82f }, { "envMod", 0.75f }, { "decay", 0.25f }, { "accent", 0.8f }, { "drive", 0.35f }, { "sub", 0.1f } }, -9.4f },
        { "Acid", "Square Acid", { { "wave", 1 }, { "cutoff", 300 }, { "resonance", 0.85f }, { "envMod", 0.8f }, { "decay", 0.2f }, { "accent", 0.9f }, { "drive", 0.5f }, { "sub", 0.0f } }, -12.0f },
        { "Acid", "Screaming Acid", { { "wave", 0 }, { "cutoff", 600 }, { "resonance", 0.95f }, { "envMod", 0.9f }, { "decay", 0.4f }, { "accent", 1.0f }, { "drive", 0.8f } }, -11.3f },
        { "Deep", "Deep Sub", { { "wave", 2 }, { "cutoff", 250 }, { "resonance", 0.1f }, { "envMod", 0.1f }, { "decay", 0.6f }, { "sub", 0.9f }, { "drive", 0.1f } }, -5.6f },
        { "Deep", "808 Boom", { { "wave", 2 }, { "cutoff", 180 }, { "resonance", 0.0f }, { "envMod", 0.0f }, { "decay", 1.5f }, { "sub", 1.0f }, { "drive", 0.25f }, { "glide", 0.12f } }, -5.9f },
        { "Deep", "Soft Round", { { "wave", 2 }, { "cutoff", 600 }, { "resonance", 0.2f }, { "envMod", 0.25f }, { "decay", 0.4f }, { "sub", 0.5f }, { "drive", 0.0f } }, -4.0f },
        { "Funk", "Rubber Funk", { { "wave", 1 }, { "cutoff", 500 }, { "resonance", 0.6f }, { "envMod", 0.65f }, { "decay", 0.15f }, { "sub", 0.4f }, { "drive", 0.2f } }, -10.8f },
        { "Funk", "Pluck Bass", { { "wave", 0 }, { "cutoff", 700 }, { "resonance", 0.4f }, { "envMod", 0.7f }, { "decay", 0.1f }, { "sub", 0.3f } }, -8.5f },
        { "Funk", "Disco Octave", { { "wave", 0 }, { "cutoff", 1200 }, { "resonance", 0.3f }, { "envMod", 0.4f }, { "decay", 0.2f }, { "sub", 0.2f }, { "drive", 0.15f } }, -6.8f },
        { "Heavy", "Growl", { { "wave", 0 }, { "cutoff", 450 }, { "resonance", 0.7f }, { "envMod", 0.4f }, { "decay", 0.5f }, { "drive", 0.85f }, { "lfoDepth", 0.4f }, { "lfoRate", 6 } }, -11.2f },
        { "Heavy", "Wobble Bass", { { "wave", 0 }, { "cutoff", 400 }, { "resonance", 0.7f }, { "envMod", 0.2f }, { "decay", 1.0f }, { "drive", 0.6f }, { "sub", 0.5f }, { "lfoDepth", 0.85f }, { "lfoRate", 5 } }, -9.9f },
        { "Heavy", "Reese", { { "wave", 0 }, { "cutoff", 900 }, { "resonance", 0.25f }, { "envMod", 0.1f }, { "decay", 1.2f }, { "drive", 0.45f }, { "sub", 0.6f }, { "lfoDepth", 0.2f }, { "lfoRate", 2 } }, -9.7f },
        { "Heavy", "Distorted Square", { { "wave", 1 }, { "cutoff", 1500 }, { "resonance", 0.3f }, { "envMod", 0.3f }, { "decay", 0.35f }, { "drive", 1.0f }, { "sub", 0.3f } }, -12.8f },
    };
    return sounds;
}

// The settings a sound never changes.
inline bool keeps (const juce::String& id)
{
    for (auto* kept : { "pattern", "sequencer", "keysTranspose", "stepRate", "gate", "root", "volume", "arpOn", "arpMode", "arpSpeed", "arpSync", "arpHz",
                        "arpOctaves", "arpGate", "arpSwing", "arpRepeats", "arpChance", "latch" })
        if (id == kept)
            return true;
    return false;
}

inline void apply (juce::AudioProcessorValueTreeState& state, const Sound& sound)
{
    for (auto* param : state.processor.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (! keeps (p->getParameterID()))
                p->setValueNotifyingHost (p->getDefaultValue());
    for (const auto& [id, value] : sound.values)
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    state.state.setProperty ("soundName", sound.name, nullptr);
}

inline juce::String nameOf (const juce::AudioProcessorValueTreeState& state, const std::vector<Sound>& sounds)
{
    const auto name = state.state.getProperty ("soundName").toString();
    return name.isNotEmpty() ? name : juce::String (sounds.front().name);
}

inline int indexOf (const juce::AudioProcessorValueTreeState& state, const std::vector<Sound>& sounds)
{
    const auto name = state.state.getProperty ("soundName").toString();
    for (int i = 0; i < (int) sounds.size(); ++i)
        if (name == sounds[(size_t) i].name)
            return i;
    return 0;
}

// The vocoder's sounds are the vocoder's own (its presets, in its own repo), so their trims are
// here, by the sound's name (0 dB for a name not listed).
inline float vocoderTrimDb (const juce::String& name)
{
    static const std::vector<std::pair<const char*, float>> trims {
        { "Classic Robot", 1.1f },
        { "French Touch", 0.3f },
        { "Choir Pad", 0.7f },
        { "Whisper Ghost", -1.1f },
        { "Trance Gate", 2.5f },
        { "Stutter Chop", -2.8f },
        { "Arp Robot", -3.5f },
        { "Acid Wobble", -7.7f },
        { "Auto-Wah Funk", -2.7f },
        { "Lo-Fi Radio", -2.0f },
        { "Monster", -2.4f },
        { "Chipmunk Bot", -4.2f },
        { "Space Echo", 0.7f },
        { "Hard Tune", 1.6f },
        { "Sing the Synth", -3.5f },
        { "Harmony Choir", 2.1f },
        { "Talk Box Funk", -3.7f },
        { "80s Synth Pop", -4.4f },
        { "Underwater", 3.0f },
        { "Glitch Bot", -2.4f },
        { "Cathedral", -3.6f },
        { "Ice Crystal", -2.4f },
        { "Lullaby", 2.5f },
        { "Disco Bounce", 1.4f },
        { "Gallop Rider", 2.6f },
        { "Dubstep Growl", -5.1f },
        { "Pop Star", -0.3f },
        { "1940s Crooner", -1.1f },
        { "Gramophone Ghost", -1.5f },
        { "Old Radio Robot", -1.5f },
        { "Cassette Dream", 0.1f },
        { "Boom Bap Vocal", -2.1f },
        { "Phone Call", -3.0f },
        { "Great Mic", -0.9f },
        { "Tube Vocal", -2.4f },
        { "Pop Shine", 1.2f },
        { "Radio Voice", -1.8f },
        { "Ribbon Crooner", -2.2f },
        { "Studio Robot", 2.4f },
    };
    for (const auto& [n, db] : trims)
        if (name == n)
            return db;
    return 0.0f;
}

// Follows the sound picked on a tool and holds its trim as a gain, for the audio thread (which
// can't read the tool's state). The Rig applies it before the strip's effects and fader.
class Trim : private juce::ValueTree::Listener
{
public:
    Trim() = default;
    ~Trim() override
    {
        if (tree.isValid())
            tree.removeListener (this);
    }

    // `property` is what changes when another sound is picked, and `db` what the picked sound's
    // trim is now.
    void attach (juce::ValueTree state, const char* property, std::function<float()> db)
    {
        tree = state;
        watched = property;
        trimDb = std::move (db);
        tree.addListener (this);
        update();
    }

    // The Bass's or Synth's sounds: the picked one's name is in the state.
    void attach (juce::AudioProcessorValueTreeState& state, const std::vector<Sound>& list)
    {
        auto root = state.state;
        attach (root, "soundName", [root, &list]
                {
                    const auto name = root.getProperty ("soundName").toString();
                    if (name.isEmpty())
                        return list.front().trimDb;
                    for (const auto& s : list)
                        if (name == s.name)
                            return s.trimDb;
                    return 0.0f;
                });
    }

    float gain() const { return linear.load(); }

private:
    void update() { linear = juce::Decibels::decibelsToGain (trimDb()); }

    void valueTreePropertyChanged (juce::ValueTree& changed, const juce::Identifier& property) override
    {
        if (changed == tree && property == watched)
            update();
    }

    juce::ValueTree tree;
    juce::Identifier watched;
    std::function<float()> trimDb;
    std::atomic<float> linear { 1.0f };
};
}
