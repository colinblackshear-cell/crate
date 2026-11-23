#pragma once

#include "DrumEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>

// Your own kits on the drum machine: any of its twelve sounds can be a sample (a WAV, AIFF, MP3...)
// instead of its analog sound. A folder of drum hits (a Splice pack, a Cymatics kit, an MPC folder)
// loads as a kit by matching the file names to the sounds (kick, snare, hat, tom...). A kit you put
// together saves under your own name in Music/Crate/Drum Kits as a small .drumkit file (JSON: each
// sound's file and its knobs), next to the sounds it points at.
namespace crate::drums::kits
{
// The sound a file name sounds like, or -1. `taken` says which sounds already have one, so a
// second "tom" goes to the next tom and a second hat to the open hat.
int voiceForName (const juce::String& fileName, const std::array<bool, kVoices>& taken);

// Which file goes on which sound, for a folder (or a list) of files. Files that match nothing go
// on the sounds still empty among the percussion (clave, cowbell, rim, cymbal), in name order.
std::array<juce::File, kVoices> mapFiles (juce::Array<juce::File> files);

// The audio files in a folder (and the folders inside it, a few deep), sorted by name.
juce::Array<juce::File> audioFilesIn (const juce::File& folder, juce::AudioFormatManager&);

// Where your kits are saved, and the ones there.
juce::File userKitsFolder();
juce::Array<juce::File> userKits();

// A kit: each sound's file (empty: its analog sound) and, optionally, its knobs.
struct Kit
{
    juce::String name;
    std::array<juce::File, kVoices> files;
    std::array<VoiceSettings, kVoices> voices {};
    bool hasVoices = false;
};
bool save (const Kit&, const juce::File&);
bool load (const juce::File&, Kit&);
}
