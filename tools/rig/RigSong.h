#pragma once

#include "RigTimeline.h"

#include <juce_core/juce_core.h>

class RigEngine;
struct RigScene;

// The song as the Song screen shows it: a row of sections (Intro, Verse, Chorus...) end to end
// over the bar ruler, and in each section a box for each track saying what it plays there.
//
// It's the same song as the timeline's (Bars view), not a copy: a section is a timeline marker
// and runs to the next one, and a box is a block on that track's lane that fills the section.
// So everything you do here is on the timeline, and fine edits there (a loop that comes in on
// bar 3, a long fade over two sections) show up here too.
//
// Message thread. Each edit tidies the timeline and calls changed().
namespace rigsong
{
struct Section
{
    juce::String name;
    double start = 0, bars = 4;
    double end() const { return start + bars; }
};

// The sections, in order. With no markers but some blocks, the whole song is one section.
std::vector<Section> sections (const RigTimeline&);
// Where the last section ends (bars).
double songBars (const RigTimeline&);
// The section playing at `bar` (-1 before the first or after the last).
int sectionAt (const RigTimeline&, double bar);
// Names for new sections: Intro, Verse, Chorus, Verse 2, Chorus 2, Bridge, Chorus 3, Outro...
juce::String suggestName (const RigTimeline&);
// The section's letter (A-Z): the footer's keys and the screen's labels.
juce::String letterFor (int section);
juce::String barsText (double bars);   // "1 bar", "8 bars", "2.5 bars"

// Sections: added at the end (returns its index, -1 when the song is full), longer or shorter
// (what comes after moves along with it), renamed, deleted (the boxes in it go and the rest
// moves up), copied (right after it, with its boxes) and moved (with their boxes).
constexpr int kMaxSections = 26;
int addSection (RigTimeline&, const juce::String& name, double bars);
void setSectionBars (RigTimeline&, int section, double bars);
void renameSection (RigTimeline&, int section, const juce::String&);
void removeSection (RigTimeline&, int section);
int duplicateSection (RigTimeline&, int section);
void moveSection (RigTimeline&, int from, int to);

// Boxes. The block that plays `lane` in the section (the first one there), or -1.
int boxBlock (const RigTimeline&, int section, int lane);
// True when that block began in an earlier section and carries on into this one.
bool boxCarriesOn (const RigTimeline&, int section, int lane);
// Fills the section's box with one block that plays `clip` (keeping the fades and level of
// what was there). Returns the block's index.
int setBox (RigTimeline&, int section, int lane, int clip);
// Empties it: the track is quiet in that section.
void clearBox (RigTimeline&, int section, int lane);
// Copies a section's boxes on a lane to another section ("Same as Intro").
void copyBox (RigTimeline&, int fromSection, int toSection, int lane);

// What a lane can play, as (clip, name): the MPC's sequences, the bass's or drum machine's
// patterns, the synth's keys or arp; for loops and live inputs just one, "plays" (-1).
std::vector<std::pair<int, juce::String>> clipChoices (RigEngine&, int lane);
juce::String clipName (RigEngine&, int lane, int clip);
// The track's name ("MPC drums", "Bass", "Loop 3") and a word on what it is.
juce::String laneName (int lane);
juce::String laneHint (RigEngine&, int lane);
// The lanes that have anything to play (a loop with audio, a tool with its patterns), or are
// in the song already: the Song screen shows these first.
bool laneHasSomething (RigEngine&, int lane);

// What's playing now, as a new section at the end: a box for every track that's on and has
// something to play, set to the pattern it plays. Returns its index (-1 when full).
int addSectionFromWhatsPlaying (RigEngine&, const juce::String& name, double bars);
// The Song builder's old parts (shows from before the Song screen) as sections, in order.
// Only when the song has no sections yet. Returns true if it made any.
bool sectionsFromParts (RigEngine&);
}
