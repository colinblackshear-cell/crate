#pragma once

#include "PocketKit.h"
#include "PocketLibrary.h"

// The factory sound bank: five kits of 16 sounds, all made in code (the 808 engine plus original
// one-shots synthesised here), so the app has a full bank on day one with nothing to license.
// A kit's sounds are written into Sounds (tagged FACTORY) the first time it's loaded.
namespace pocket
{
inline const char* const kTagFactory = "FACTORY";

struct FactoryKit
{
    const char* name;
    const char* what;
    juce::uint32 colour;
};

const std::vector<FactoryKit>& factoryKits();

// Puts kit `index` on bank `bank` of the pads (16 pads), making its sounds first if needed.
// Returns how many pads it filled.
int loadFactoryKit (Library&, Kit&, int index, int bank = 0);

// Makes one kit's sounds (for tests): name and audio at 44.1 kHz, mono.
std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> makeFactorySounds (int index);
}
