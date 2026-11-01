#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Audio.h"

#include <unordered_map>

// Reading and writing sound files.
namespace crate::AudioFiles
{
    // The longest sound the tools will load or record.
    constexpr double kMaxSeconds = 20.0 * 60.0;

    juce::String wildcard (juce::AudioFormatManager&);   // e.g. "*.wav;*.aif;..."
    bool isAudioFile (const juce::File&, juce::AudioFormatManager&);

    // Any file JUCE can decode (WAV, AIFF, MP3, FLAC, Ogg) as 1 or 2 channels at 44.1 kHz.
    // Files with more channels are folded to stereo.
    SharedAudio load (const juce::File&, juce::AudioFormatManager&, juce::String& error);

    // Expands dropped folders into the audio files inside them, sorted by name.
    juce::Array<juce::File> expandDropped (const juce::StringArray& paths, juce::AudioFormatManager&);

    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>&, double sourceRate);

    // Writes 44.1 kHz audio as WAV (24-bit) or AIFF (24-bit), by the file's extension.
    // `metadata` uses JUCE's WavAudioFormat keys and is only written to WAV files.
    bool write (const juce::File&, const juce::AudioBuffer<float>&,
                const std::unordered_map<juce::String, juce::String>& metadata = {});

    // The same as a WAV file in memory, for saving inside a plugin's state.
    juce::MemoryBlock toWavData (const juce::AudioBuffer<float>&);
    SharedAudio fromWavData (const void* data, size_t size);
}
