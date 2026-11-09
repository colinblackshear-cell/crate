#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

class RigEngine;

// Export for Logic: the looper's layers as WAV files you can drag into Logic, all starting on
// bar 1 and lined up with each other.
//
//   Loops/      every loop once, as heard (its effects and level) and dry
//   Song/       if you've built a song (Song grid): one file per loop, the whole song long,
//               with the loop playing in the parts where it's on, so the files together are
//               the song. Also "Song markers.mid": the tempo and a marker where each part
//               starts (drag it into Logic to get the tempo and the part names).
//   Tempo.txt   the tempo and the time signature
//
// The Rig's built-in effects are printed into the "as heard" files; a plugin of your own on a
// loop isn't (Record set records those). Message thread.
namespace RigExport
{
struct Result
{
    juce::File folder;
    int files = 0;
    juce::String error;   // empty when it worked
};

// Writes everything into a new folder inside `parent` (named with the date and time).
Result exportForLogic (RigEngine&, const juce::File& parent);
// Where exports go unless told otherwise: ~/Music/Crate/Rig/Exports.
juce::File defaultFolder();

// The pieces, for the tests.
// One pass of a loop at `bpm` (as many samples as its beats take at that tempo).
juce::AudioBuffer<float> renderLoop (RigEngine&, int layer, double bpm, double sampleRate, bool withEffects);
// A loop laid out over the song: on in the parts where the song grid has it on.
juce::AudioBuffer<float> renderSongStem (RigEngine&, int layer, double bpm, double sampleRate, bool withEffects);
// The song's length in bars (a part that holds counts as 4 bars).
int songBars (RigEngine&);
}
