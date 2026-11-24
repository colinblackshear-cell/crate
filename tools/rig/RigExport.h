#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

class RigEngine;

// Export for Logic: the looper's layers as WAV files you can drag into Logic, all starting on
// bar 1 and lined up with each other.
//
//   Loops/      every loop once, as heard (its effects and level) and dry
//   Song/       if you've built a song: one file per loop, the whole song long, with the loop
//               playing where the song has it, so the files together are the song. The song
//               is the Timeline if it has anything on it, otherwise the Song grid's parts.
//               Also "Song markers.mid": the tempo and a marker for each section or part
//               (drag it into Logic to get the tempo and the names).
//   Automation.mid  if you've recorded knob moves: a track per lane, its moves as a MIDI CC
//               (named after the setting and its CC number) over the song, from bar 1. Read me
//               lists which CC is which setting.
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
// A loop laid out over the timeline: playing in its lane's blocks, with their fades.
juce::AudioBuffer<float> renderTimelineStem (RigEngine&, int layer, double bpm, double sampleRate, bool withEffects);
int timelineBars (RigEngine&);
// The same over bars [fromBar, toBar) only (a section), with `tailSeconds` more for its effects to ring out.
juce::AudioBuffer<float> renderTimelineRange (RigEngine&, int layer, double bpm, double sampleRate, bool withEffects,
                                              double fromBar, double toBar, double tailSeconds);
// The loop going round for `bars` bars from its top (the loops as they play now), with a tail.
juce::AudioBuffer<float> renderLoopPass (RigEngine&, int layer, double bpm, double sampleRate, bool withEffects, double bars,
                                         double tailSeconds);
// The CC each automation lane is written on (the CCs MIDI leaves free: 102-119, then 20-31,
// 85-90, 3, 9, 14, 15), or -1 past the last.
int ccForLane (int lane);
// The automation lanes as MIDI CCs over `bars` (a track each; the tempo on the first), at `bpm`.
juce::MidiFile automationMidi (RigEngine&, int bars, double bpm);
}
