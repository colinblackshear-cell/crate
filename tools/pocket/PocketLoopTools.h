#pragma once

#include "BeatsEngine.h"
#include "PocketLooper.h"
#include "PocketSynth.h"

// The loop editor's offline work, apart from any screen so the self-checks can run it: IN / OUT and TRIM,
// BEAT MATCH (FIT TO n BARS, a time-stretch that keeps the pitch), QUANTIZE (a layer that came from notes is
// rendered again from its notes, re-timed) and following a tempo change. Every function here takes copies and
// returns a new take, so it can run on a worker thread while the old take keeps playing.
//
// The rules:
//  - TRIM keeps IN-OUT. The loop's length is IN-OUT rounded up to a length that keeps it on the song's grid:
//    1 beat, 2 beats or whole bars. The sound is cut where IN and OUT are (sample accurate with SNAP OFF) and
//    any rest up to that length is silence, so the next pass starts on the beat (or the bar). BEAT MATCH then
//    offers FIT, which stretches the sound to whole bars instead.
//  - FIT TO n BARS stretches IN-OUT (the whole sound when the markers are at its ends) to exactly n bars at the
//    song's tempo, keeping its pitch.
//  - A tempo change: every layer is stretched to the new tempo from the audio it had before any tempo change
//    (a layer made from notes is rendered again from them instead), so its pitch stays and changes don't pile up.
namespace pocket
{
const char* layerToolName (int tool);                // "SYNTH"
juce::Colour layerToolColour (int tool);
void drawLayerToolIcon (juce::Graphics&, int tool, juce::Point<float> centre, juce::Colour);

// The notes a layer was printed from, kept with the layer (and saved with the song) for QUANTIZE.
struct NotePart
{
    enum Kind { pads, synth };
    int kind = pads;
    double lengthBeats = 4.0;      // the layer's length
    double startBeat = 0.0;        // the clock beat the layer started on (the pattern's top)

    // PADS: the pad pattern (and the drum machine, when it was playing too) on the kit's sounds
    PadPattern pattern;
    bool drumsOn = false;
    DrumData drums;
    std::vector<PadSetup> kit;     // the pads' sounds (not saved: the song's kit fills it after loading)
    std::array<crate::drums::SharedSample, crate::drums::kVoices> drumSamples {};   // the drum machine's own sounds (the same)

    // SYNTH: the note pattern in its sound
    SynthPattern notes;
    int preset = 0, octave = 0;
    SynthKnobs knobs;

    // The boards the take was recorded through (PocketBoards settings, {} = none): SYNTH's, or for PADS the drum
    // machine's (`board`) and the pads' (`padsBoard`). A re-render goes through them again, so it sounds as recorded.
    juce::var board, padsBoard;

    double patternBeats() const { return kind == pads ? pattern.lengthBeats() : notes.lengthBeats(); }
    juce::String describe() const;   // "PADS" / "SYNTH"
    juce::var toVar() const;
    static std::shared_ptr<NotePart> fromVar (const juce::var&);
};

// Where a note at `beat` goes (wrapped into [0, length)).
double quantizedBeat (double beat, const LoopQuantize&, double length);
// The part's notes, re-timed, as the looper would have recorded them: mono, `lengthSamples` long, with the tails
// of the pass before wrapped in so it loops cleanly.
std::vector<float> renderPart (const NotePart&, const LoopQuantize&, double bpm, double sampleRate, int lengthSamples);

// SNAP: 0 BAR, 1 BEAT, 2 1/16, 3 OFF. The grid in beats (0 = none).
double snapBeats (int snap);
double snapBeat (double beat, int snap);
// TRIM's loop length for IN-OUT of `contentBeats`: 1 beat, 2 beats or whole bars, never shorter.
double loopLengthFor (double contentBeats);
// BEAT MATCH's choices for a sound `bars` long: the nearest whole number of bars, then the next smaller and
// larger of 1, 2, 4, 8, 16 (0 = none).
struct FitChoices { int nearest = 1, smaller = 0, larger = 0; bool exact = false; };
FitChoices fitChoices (double bars);

// A new length, same pitch (the Mac's stretcher, core/SoundFx).
std::vector<float> stretchAudio (const float* data, int n, int newLength);

// The edits. Each returns the new take (null + `error` when it can't), and the effects with their moves
// following the edit; `shift` is how far the loop's top moves on the clock, so the sound stays where it was.
struct Edited
{
    SharedTake take;
    LayerFxState fx;
    double shift = 0.0;
    juce::String said, error;
};
Edited trimTake (const LoopTake&, const LayerFxState&, double fromBeat, double toBeat);
Edited fitTake (const LoopTake&, const LayerFxState&, double fromBeat, double toBeat, int bars, double bpm);
Edited quantizeTake (const LoopTake&, const LayerFxState&, const LoopQuantize&, double bpm);
// The take at a new tempo (stretched from its origin, or rendered again from its notes). Null when it's already there.
SharedTake tempoTake (const LoopTake&, double bpm);
// LOOP LAB's WARP. warpSourceOf: the take's warp, or a fresh one (its recording, a straight plan) when it has none.
// warpTake renders the take's recording by `plan` at the tempo: the new take carries the plan (one UNDO step each).
// originalTake: the recording as it was at the tempo (ORIGINAL), carrying a straight plan.
WarpSource warpSourceOf (const LoopTake&);
Edited warpTake (const LoopTake&, const LayerFxState&, const crate::Warp::Plan&, double bpm);
SharedTake originalTake (const LoopTake&, double bpm);
// The hits in the recording (in its beats), and the tempo they sound like (0 = can't tell).
std::vector<double> warpHits (const WarpSource&, double sampleRate, float sensitivity);
double warpSoundsLike (const WarpSource&, const std::vector<double>& hits, double sampleRate, double songBpm);
// FIT TO THE BEAT (a take sung free, no click): the hits are found, the tempo it was sung at read from them (near the
// song's), the take stretched (keeping its pitch) to the whole bars it was sung over at the song's tempo, and each hit
// pulled most of the way onto the nearest 1/8. `soundsLike` says the tempo it heard (0: couldn't tell; then it's only
// rounded to whole bars and snapped).
crate::Warp::Plan fitToBeatPlan (const WarpSource&, double sampleRate, double songBpm, double& soundsLike);
// The tempo a take plays at its own pitch.
double nativeBpm (const LoopTake&);

// FIT keeps the length and pitch exact, QUANTIZE moves notes onto the grid (and back with the original part),
// TRIM gives a loop of the snapped length with its moves following. Returns what was wrong (empty = fine).
juce::String loopToolsSelfCheck (juce::String& report);
}
