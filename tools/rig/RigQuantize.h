#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "Sequence.h"
#include "BassEngine.h"
#include "SynthEngine.h"
#include "DrumEngine.h"
#include "RigLooper.h"

#include <array>
#include <vector>

class RigEngine;

// Quantize: the same pull onto the beat for everything you've played, wherever you are.
//
// One set of settings (the grid, how hard it pulls, and swing) works on all of it:
//   - the MPC's sequence (its notes move in ticks, like its own Timing correct),
//   - the drum machine, the Bass and the Synth's arp (their steps move to the nearest grid line,
//     so a coarser grid tightens a busy 1/16 pattern into 1/8s, and a triplet grid gives it a
//     triplet feel),
//   - and loops on the looper, whatever they were recorded from (your mic, the line in, a tool,
//     the mix): it finds each hit in the sound and slides it onto the grid, the way Logic's Flex
//     slicing does, keeping the loop's length and where it sits in the bar.
// Strength pulls each note or hit that share of the way (100% is right on the grid). Swing pushes
// every other 1/8 or 1/16 line late, as on the MPC (50% is straight).
namespace quantize
{
enum Grid { quarter, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond, numGrids };
juce::String gridName (int);
double gridBeats (int);
bool gridSwings (int);   // 1/8 and 1/16

struct Settings
{
    int grid = sixteenth;
    float strength = 1.0f;   // 0..1
    int swing = 50;          // 50 (straight) .. 75
    // Quantize each new loop as its recording finishes.
    bool newLoops = false;

    juce::ValueTree toTree() const;
    static Settings fromTree (const juce::ValueTree&);
    bool operator== (const Settings&) const;
};

// Where grid line `n` falls, in beats from the start (with swing).
double lineBeat (const Settings&, long long n);
// The grid line nearest `beat`.
double nearestLine (const Settings&, double beat);
// `beat`, pulled toward its nearest grid line by the strength.
double pull (const Settings&, double beat);

// The MPC's sequence: every track's notes. Notes pulled past the end wrap to the top; two hits
// of a pad that land on the same tick become one (the louder). Returns how many notes moved.
int sequence (seq::Sequence&, const Settings&);

// Step patterns: where each of `length` steps, `stepBeats` apart, goes (its own index when it
// stays). Steps can only sit on steps, so a hit moves when the pull takes it past half a step.
std::vector<int> stepTargets (int length, double stepBeats, const Settings&);
int drumPattern (crate::drums::Pattern&, double stepBeats, const Settings&);
int bassPattern (crate::bass::Pattern&, double stepBeats, const Settings&);
int rhythm (crate::synthtool::Rhythm&, double stepBeats, const Settings&);

// Audio: where the hits start (in samples), loudest-rise first found, in order.
std::vector<int> findHits (const juce::AudioBuffer<float>&, double sampleRate);
struct AudioResult
{
    juce::AudioBuffer<float> audio;   // the same length as what went in
    int hits = 0, moved = 0;
};
// A loop `beats` long whose first sample is on a bar line. Each hit's slice moves onto the grid,
// with short crossfades between slices (a slice that moves later plays on a little longer to fill
// the gap; one that moves earlier is cut a little shorter). The loop wraps round.
AudioResult audio (const juce::AudioBuffer<float>&, double beats, double sampleRate, const Settings&);
}

//==============================================================================
// The Rig's quantize: its settings (saved with the show), what it can quantize, and one undo.
// Message thread.
class RigQuantizer
{
public:
    explicit RigQuantizer (RigEngine&);

    // What to quantize: a tool's pattern, every loop, or one loop.
    enum Target { none = -1, mpc, machine, bass, synth, allLoops, firstLoop };
    static constexpr int numTargets = firstLoop + RigLooper::kLayers;
    static int loopTarget (int layer) { return firstLoop + layer; }
    static juce::String targetName (int);
    // What the target is quantizing right now, in a few words ("Sequence 1", "Pattern 3A").
    juce::String targetDetail (int) const;

    quantize::Settings settings;

    // Quantizes it now and says what happened.
    juce::String apply (int target);
    // Puts back what the last quantize changed (if nothing has changed it since).
    bool canUndo() const { return undoTarget != none; }
    juce::String undo();
    int getUndoTarget() const { return undoTarget; }

    // Often (the engine's timer): quantizes loops that just finished recording, when settings.newLoops.
    // Returns what to say, or empty.
    juce::String service();
    juce::String lastAutoMessage;

    juce::ValueTree toTree() const { return settings.toTree(); }
    void fromTree (const juce::ValueTree& t) { settings = quantize::Settings::fromTree (t); undoTarget = none; }

private:
    juce::String applyLoop (int layer, bool keepUndo);
    double stepBeatsOf (int target) const;
    void forgetUndo();

    RigEngine& engine;
    int undoTarget = none;
    // What it was before.
    seq::Sequence undoSequence;
    int undoSequenceIndex = 0;
    crate::drums::Pattern undoDrums;
    int undoDrumIndex = 0, undoDrumVariation = 0;
    crate::bass::Pattern undoBass;
    int undoBassIndex = 0;
    crate::synthtool::Rhythm undoRhythm;
    struct LoopUndo
    {
        int layer = -1;
        juce::AudioBuffer<float> audio;
        int version = -1;   // the layer's version after quantizing
    };
    std::vector<LoopUndo> undoLoops;
    std::array<int, RigLooper::kLayers> lastStates {};
};
