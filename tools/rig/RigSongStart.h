#pragma once

#include "RigEngine.h"

// Starting a song from nothing, from the top bar: New song clears the Rig to a blank one
// (after saving the one you were on), and New pattern starts the first thing in it, or the
// next: a beat on the MPC pads or the drum machine, a bass line, a melody on the keys, a
// vocoder part, a loop or a sample. Each one picks an empty pattern on its track (so nothing
// you made is written over), switches it on, and says what to do next. When it sounds right,
// "Keep as the next song part" saves what's playing as a part of the song (Intro, Verse...).
namespace songstart
{
enum Start { mpcBeat, machineBeat, bassLine, keys, vocoder, loop, sample, numStarts };

// "A beat on the MPC pads", for the menu.
juce::String name (int start);

// The MPC beats and bass patterns New pattern looks through: the eight on the Groove screen's
// pattern row. (The drum machine's first eight come with beats in them, so it looks through
// all 16 of its own.)
constexpr int kPatternsShown = 8;

// The first empty pattern of each, or -1 if all of them have something in them. The MPC's
// beats count from its first sequence; one it doesn't have yet is empty.
int firstEmptyMpcBeat (RigEngine&);
int firstEmptyMachinePattern (RigEngine&);
int firstEmptyBassPattern (RigEngine&);

struct Result
{
    juce::String page = "Groove";   // the page to show
    int track = -1;                 // the Groove screen's track to pick (GrooveView::Track), or -1
    int pattern = -1;               // the pattern (or MPC beat) it started, or -1
    juce::String message;           // what to do next, for the footer
};

// Starts a new pattern of `start` (one of Start): picks it, switches it on, unmutes its track.
Result begin (RigEngine&, int start);
}
