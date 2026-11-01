#pragma once

#include "Audio.h"

// The editing moves every tool shares. Each takes a sound and a selection and returns the new
// sound plus what should be selected afterwards; nothing is changed in place, which is what
// makes unlimited undo cheap.
//
// An empty selection means "the whole sound" for moves that make sense that way (reverse,
// gain, normalize); moves that only work on a part (trim, delete, fades) leave the sound alone.
namespace crate::Edits
{
    struct Result
    {
        juce::AudioBuffer<float> audio;
        juce::Range<int> selection;
    };

    enum class FadeCurve
    {
        linear,    // straight line
        smooth,    // S-shaped: gentle at both ends
        fast,      // most of the change happens early (good for fade-outs of hits)
        slow       // most of the change happens late (good for swells)
    };

    // Gain (0..1) of a fade-in at position t (0..1). A fade-out uses 1 - t.
    float fadeShape (FadeCurve, float t);

    Result crop (const juce::AudioBuffer<float>&, juce::Range<int>);       // keep only the selection
    Result remove (const juce::AudioBuffer<float>&, juce::Range<int>);     // delete the selection
    Result silence (const juce::AudioBuffer<float>&, juce::Range<int>);    // make it quiet, keep its length
    Result insert (const juce::AudioBuffer<float>&, int at, const juce::AudioBuffer<float>& clip);
    // Replaces the selection with the clip (or inserts it at the selection's start if empty).
    Result paste (const juce::AudioBuffer<float>&, juce::Range<int>, const juce::AudioBuffer<float>& clip);
    juce::AudioBuffer<float> copy (const juce::AudioBuffer<float>&, juce::Range<int>);

    // `amount` is how far the level drops: 1 = all the way to silence, 0.5 = to half volume.
    Result fade (const juce::AudioBuffer<float>&, juce::Range<int>, bool fadeIn, FadeCurve, float amount = 1.0f);
    Result reverse (const juce::AudioBuffer<float>&, juce::Range<int>);
    Result gain (const juce::AudioBuffer<float>&, juce::Range<int>, float decibels);
    // Raises (or lowers) the level so the loudest peak sits at `peakDecibels`.
    Result normalize (const juce::AudioBuffer<float>&, juce::Range<int>, float peakDecibels = -0.3f);
    // Cuts quiet bits off both ends (quieter than `thresholdDecibels`), with a short fade on each.
    Result trimSilence (const juce::AudioBuffer<float>&, float thresholdDecibels = -48.0f);

    // Turns a stereo sound whose sides are the same (or one side silent) into mono.
    juce::AudioBuffer<float> collapseFakeStereo (juce::AudioBuffer<float>);

    // Moves each edge of the selection to the nearest zero crossing (within 10 ms), so cuts
    // don't click.
    juce::Range<int> snapToZeroCrossings (const juce::AudioBuffer<float>&, juce::Range<int>);

    // The selection, or the whole sound if nothing is selected, clipped to the sound.
    juce::Range<int> orWhole (const juce::AudioBuffer<float>&, juce::Range<int>);
}
