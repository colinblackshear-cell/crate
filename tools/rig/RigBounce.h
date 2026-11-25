#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

class RigEngine;

// Bounce + stems: the song (or one section, or the loops as they play now) out as files, for Logic or
// anywhere else. Everything lands in its own folder in ~/Music/Crate/Rig/To Logic, every file starting
// on the same bar, so in Logic you drop them all at bar 1 (or the section's bar) and they line up.
//
//   What     the song, one section of it, or the loops playing now (one pass of the longest)
//   Split    the mix only; each loop on its own (and the mix); or everything: each loop, each tool
//            (MPC, drum machine, bass, synth, sampler, drone, vocoder, input) and the mix, played
//            once in real time (the tools play live, so they can't be printed faster)
//   Format   WAV (24-bit, the Rig's rate) and/or MP3 (320 kbps)
//   As heard with the loops' effects and levels, or dry
//   Tail     a couple of seconds more for echoes and reverb to ring out
// Also "Song markers.mid" (the tempo, and a marker per section) for Logic.
//
// Loops print instantly (from their audio); "everything" plays the song once through the recorder
// (SongView drives that with TimelineView::recordSong). Message thread.
namespace RigBounce
{
enum class What { song, section, loops };
enum class Split { mix, eachLoop, everything };

struct Options
{
    What what = What::song;
    int section = -1;           // What::section: the timeline marker
    Split split = Split::eachLoop;
    bool wav = true, mp3 = true;
    bool dry = false;
    double tailSeconds = 2.0;
    juce::String name;          // the show's name, for the folder ("Song" when empty)
    // A friend's track (RigFriendTrack): in the files (the mix has it), or left out ("my parts only",
    // to send back). Only counts when there is one.
    bool withTrack = true;
    // Only the mix (no stems): "my parts mixed", or everything to listen to.
    bool mixOnly = false;
    // Silence before every file, so they start at the same moment as his mixdown (the time before his
    // bar 1). Set by the Song screen for a song bounce over a friend's track.
    double leadSeconds = 0;
};

struct Result
{
    juce::File folder;
    juce::StringArray files;    // file names written
    juce::String error;         // empty when it worked
};

// ~/Music/Crate/Rig/To Logic
juce::File toLogicFolder();
// A new folder for a bounce in `parent`: "<name> <what> <date time>".
juce::File newFolder (const juce::File& parent, const Options&);

// The bars a bounce covers: [from, to). Empty (to <= from) when there's nothing to bounce.
juce::Range<double> barsFor (RigEngine&, const Options&);
// The loops a bounce has in it (those with audio; for the song, those the song uses).
juce::Array<int> loopsFor (RigEngine&, const Options&);
// Says what it will do, for the panel ("Song, 48 bars: 5 loops + mix, WAV + MP3").
juce::String describe (RigEngine&, const Options&);

// The loops' part: each loop's stem and the mix of them, written now. With Split::everything the
// real-time pass adds the tools (and its own mix) afterwards, so this writes no mix then.
Result bounceLoops (RigEngine&, const Options&, const juce::File& folder);
// Writes `audio` as `<base>.wav` and/or `<base>.mp3` per `options`; adds the names to `files`. False on an error.
bool writeAudio (const juce::File& base, const juce::AudioBuffer<float>& audio, double rate, const Options&, juce::StringArray& files);
// The tempo and the sections over bars [from, to), as a MIDI file Logic reads markers from.
bool writeMarkers (RigEngine&, const juce::File& file, juce::Range<double> bars, double bpm);
// After the real-time pass: MP3 copies of the WAVs the recorder saved (and removes the WAVs when only
// MP3 was asked for). Runs on the calling thread.
void convertRecorded (const juce::Array<juce::File>& wavs, const Options&, juce::StringArray& files);
// Over a friend's track: whether there is one, and the note for him ("94 BPM, 4/4, F minor...") that
// goes in the folder. Its file name, or empty.
bool hasTrack (RigEngine&);
juce::String writeNoteForFriend (RigEngine&, const Options&, const juce::File& folder);
}
