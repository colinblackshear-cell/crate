#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

// Listening to a whole song (a friend's mixdown) to play over it: its tempo, where its bar 1 is,
// its key and its sections. Shared by the Mac's "Bring in a track" and the phone's. All guesses:
// the screens show them so you can check (Listen with click) and change them.
//
// Message thread (or any thread but the audio one): a few hundred milliseconds for a 4-minute song.
namespace crate::TrackAnalysis
{
struct Result
{
    double bpm = 0;            // 0: couldn't tell
    double firstBeat = 0;      // seconds into the file where bar 1 starts (its first downbeat)
    int key = -1;              // 0 (C) - 11 (B), -1: couldn't tell
    bool minor = false;
};

// The whole guess, from audio at `rate` (any number of channels). `beatsPerBar` for the downbeat.
Result analyse (const juce::AudioBuffer<float>&, double rate, int beatsPerBar = 4);

// Just the tempo (70-180 BPM), or 0.
double guessBpm (const juce::AudioBuffer<float>&, double rate);
// Where bar 1 starts at `bpm` (seconds into the file): the first downbeat at or after the music starts.
double guessFirstBeat (const juce::AudioBuffer<float>&, double rate, double bpm, int beatsPerBar = 4);
// The first hit at all (seconds): where the music starts, for "Find first hit".
double firstHit (const juce::AudioBuffer<float>&, double rate);
// The key: 0-11 (and whether it's minor), or -1.
int guessKey (const juce::AudioBuffer<float>&, double rate, bool& minor);

// A tempo written in a file name ("Jay idea 94bpm.wav", "beat_128_BPM"), or 0.
double bpmFromName (const juce::String& fileName);

// "F minor", "C major" ("" for -1).
juce::String keyName (int key, bool minor);

// The song's sections, as heard: runs of 8 bars by how loud they are, named Intro, Verse, Hook,
// Break, Outro (repeats numbered: "Verse 2"). Bars count from bar 1 (0.0) at `firstBeat`.
struct Section
{
    double bar = 0, bars = 8;
    juce::String name;
};
std::vector<Section> guessSections (const juce::AudioBuffer<float>&, double rate, double bpm, double firstBeat, int beatsPerBar = 4);

// Tap tempo: the BPM of the taps so far (times in seconds, oldest first), or 0 with fewer than 3.
// Taps more than 2 seconds apart start again (the caller keeps only the last run).
double tapTempo (const std::vector<double>& tapTimes);
}
