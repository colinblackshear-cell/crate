#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "RigTimeline.h"

class RigEngine;

// Bringing a song finished in Logic back into Kaleidobox, to play it live.
//
// In Logic: File > Export > All Tracks as Audio Files (WAV or AIFF, from the start of the song)
// into a folder, and File > Export > Selection as MIDI File (select all, so the tempo and the
// markers come with it) into the same folder. Then Song > Import from Logic... picks the folder.
//
// Each stem becomes a loop on its own layer (eight at most: any more are mixed together on the
// last one, "Other"), playing the whole song from bar 1 and following the tempo. Logic's markers
// become the song's sections (Intro, Verse, Chorus...), so live you can jump to one, loop it, mute
// or bring in stems, and play the vocoder, the MPC or anything else on top. Without a MIDI file
// the song is one section at the tempo Kaleidobox is set to.
namespace rigimport
{
// What the MIDI file says: the first tempo, the time signature, and the markers (in quarter-note
// beats from the start).
struct MidiInfo
{
    bool found = false;
    double bpm = 0;            // 0: the file has no tempo
    int numerator = 4, denominator = 4;
    std::vector<std::pair<double, juce::String>> markers;   // (beat, name)
    int tempoChanges = 0;      // after the first (Kaleidobox plays the song at the first)
};
MidiInfo readMidi (const juce::File&);
MidiInfo readMidi (const juce::MidiFile&);

// One stem, read.
struct Stem
{
    juce::String name;               // from the file name, without the project's name in front
    juce::AudioBuffer<float> audio;  // stereo
    double sampleRate = 44100.0;
    double seconds() const { return sampleRate > 0 ? audio.getNumSamples() / sampleRate : 0.0; }
};

// The song, ready to go on the box.
struct Song
{
    juce::String name;
    double bpm = 120;
    int beatsPerBar = 4;
    std::vector<Stem> layers;                    // eight at most
    std::vector<RigTimeline::Marker> sections;   // in bars, end to end, the last runs to the end
    double bars = 0;                             // how long the song is
    juce::StringArray notes;                     // what to tell you (mixed stems, tempo changes...)
};

// The audio files in the folder (WAV, AIFF), in name order; and its first MIDI file, if any.
juce::Array<juce::File> stemFiles (const juce::File& folder);
juce::File midiFile (const juce::File& folder);
// "My Song_Bass.wav", "My Song_Lead Vox.wav" -> "Bass", "Lead Vox": the part every name starts
// with goes (as do Logic's "#01" style numbers).
juce::StringArray stemNames (const juce::Array<juce::File>&);

// Reads the folder into a song; `fallbackBpm` when there's no MIDI file (or no tempo in it).
// Empty layers (with `notes` saying why) when nothing could be read.
Song read (const juce::File& folder, double fallbackBpm);
// From parts (tests): stems as read, the MIDI info (found=false: none).
Song make (std::vector<Stem> stems, const MidiInfo&, double fallbackBpm, const juce::String& name);

// Puts it on the box: the tempo and time signature, one loop per layer (the looper's current
// bank, named after the song), and the song's sections on the Song screen with every loop
// playing from bar 1 to the end. Replaces the song's loops and sections (start a new song first).
// Message thread. Returns what to tell you.
juce::String apply (RigEngine&, Song&&);
}
