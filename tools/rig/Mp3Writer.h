#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// MP3 out, for Bounce + stems on the Mac and the phone (JUCE reads MP3 but can't write it).
// Stereo, constant bit rate. The sound is resampled to 44.1 or 48 kHz when it's at another rate.
namespace mp3
{
// False on a build without an encoder (then a bounce writes WAV only and says so).
bool available();
// Writes `audio` (one or two channels) to `file`. False when it couldn't.
bool write (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate, int kbps = 320);
}
