#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// The first idea sets the song's tempo: a take caught free (CATCH AN IDEA, no click, no song yet)
// is listened to for the beat it was sung or played at, and the song is set to that tempo, so the
// idea plays round in time and everything added after fits it. Shared by the Mac and the phone.
//
// How it hears the beat: how sharply the sound starts (each word, syllable or hit) is followed in
// 10 ms steps, and the tempo picked (60-180 BPM, leaning to 70-150) whose beats line up best with
// those starts, then where those beats fall. The loop starts on the beat at the first sound (a
// pickup just before it is kept, folded round to the loop's end) and lasts the whole bars that were
// sung. When it can't hear a beat (a long held note, a hum with no edges) the take's own length
// sets the tempo instead: the whole bars (1, 2, 4, 8...) that give a tempo between 70 and 140.
//
// The recording is never stretched: the tempo is fitted to it. Any thread but the audio one (a few
// milliseconds for a minute of sound).
namespace crate::IdeaTempo
{
struct Fit
{
    double bpm = 0;       // the song's tempo from now on (to 0.1 BPM)
    double beats = 0;     // the loop's length in beats: whole bars
    int start = 0;        // the sample of the recording where the loop's bar 1 is (can be < 0: silence first)
    int length = 0;       // the loop in samples: exactly `beats` at `bpm`
    bool heard = false;   // true: the beat was heard in the take; false: the tempo came from its length
    bool isValid() const { return bpm > 0 && beats > 0 && length > 0; }
};

// `endSample`: where Rec was pressed to stop (the recording can run on to a bar line past it); -1 = all
// of it. Silence after the last sound isn't counted either. Invalid when there's too little sound.
Fit fit (const juce::AudioBuffer<float>& recording, double sampleRate, int endSample = -1, int beatsPerBar = 4);

// The loop from the recording: `fit.length` samples from `fit.start`. What was played just before bar 1
// (a pickup, an early breath) is faded in at the loop's end, and what rang on past its last bar is faded
// out over its start, so it goes round as one take with no click.
juce::AudioBuffer<float> cut (const juce::AudioBuffer<float>& recording, const Fit&);
}
