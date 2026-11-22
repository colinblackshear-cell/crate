#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// The factory sound bank: five kits of 16 sounds, all made in code (the 808 engine plus original
// one-shots synthesised here), so Kaleidobox has a full bank on day one with nothing to license.
// The same kits on the iPhone (Kaleidobox Pocket's pads) and the Mac (the MPC pads and the Sounds bank).
namespace crate::factory
{
struct Kit
{
    const char* name;
    const char* what;
    juce::uint32 colour;
};

const std::vector<Kit>& kits();

// Makes one kit's sounds, in pad order: name and audio at 44.1 kHz, mono.
std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> makeSounds (int index);

// A kit's sound names, in pad order, and one sound (so a sound already on disk needn't be made again).
std::vector<juce::String> soundNames (int index);
juce::AudioBuffer<float> makeSound (int index, int sound);

constexpr double kSampleRate = 44100.0;
}
