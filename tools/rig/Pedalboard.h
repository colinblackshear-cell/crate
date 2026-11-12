#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "StripFx.h"

#include <vector>

// The effects processor's model, without any of its screen: the pedals of a StripFx rack in
// the order the sound goes through them, what switches each one on and off, the ready-made
// pedal settings (Clean guitar, Funk wah, Dub delay...), your own saved ones, and the tuner.
//
// Every sound in the Rig has a rack of its own (the line in, the vocoder, bass, synth, MPC,
// drum machine, sampler, each loop, all the loops and the master), so each one can use every
// pedal with its own settings. The Effects screen shows one rack at a time.
namespace pedalboard
{
struct Setting
{
    const char* id;
    const char* label;
};

struct Pedal
{
    const char* name;
    const char* kind;          // under the name
    const char* description;   // in the rack
    juce::uint32 colour;
    Setting knob;              // the pedal's own knob
    // What switches it: a parameter that's on or off, or (null) the amount itself (the
    // pedals that came first: Filter, Dirt, Delay, Reverb and Repeat are off at zero).
    const char* onId;
    std::vector<Setting> rack;
};

enum PedalId { gate, comp, wah, filter, dirt, amp, eq, chorus, phaser, tremolo, delay, reverb, repeat, numPedals };
const std::vector<Pedal>& pedals();

// On and off, the way a footswitch does it. Turning an amount pedal off remembers its amount in
// `kept` (one per pedal), and turning it on brings that back (or a good starting amount).
bool isOn (const StripFx&, int pedal);
void setOn (StripFx&, int pedal, bool on, float* kept = nullptr);
// Its settings back to how they start (and off).
void resetPedal (StripFx&, int pedal);
// How many pedals are on.
int countOn (const StripFx&);

float get (const StripFx&, const juce::String& id);
void set (StripFx&, const juce::String& id, float value);   // in the setting's own range

//==============================================================================
// Ready-made settings for the whole board. Picking one resets the rack first (your plugin
// stays).
struct Preset
{
    const char* name;
    const char* forWhat;   // the menu's heading: Guitar, Bass, Keys and synths, Drums, Anything
    std::vector<std::pair<const char*, float>> values;
};
const std::vector<Preset>& presets();
void applyPreset (StripFx&, const Preset&);

// Your own, as files in Music/Crate/Effects. A copy of the board's settings, without the plugin.
juce::File userPresetsFolder();
juce::ValueTree copySettings (const StripFx&);
void pasteSettings (StripFx&, const juce::ValueTree&);
bool savePreset (const StripFx&, const juce::File&);
bool loadPreset (StripFx&, const juce::File&);

//==============================================================================
// The tuner: the pitch of a stretch of sound (0 when there isn't a clear note), the nearest
// note's name, and how far off it is in cents (-50..50).
float detectPitch (const float* samples, int count, double sampleRate);
struct Note
{
    juce::String name;   // "E", "A#"...
    int octave = 0;
    float cents = 0;
};
Note nearestNote (float hz);
}
