#pragma once

#include <juce_core/juce_core.h>

// Searching sounds by tempo, shared by the Mac's Sounds tab and the phone's Sounds screen.
//
// A tempo filter is a range of BPM: "92" means 92 (anything that rounds to it), "90-100" (or
// "90 to 100") a range, and "near 120" within 8% of 120, the way the Sounds tab's Near tempo
// has always worked. With halfDouble on, a 92 search also finds 46 and 184 loops, which play
// in time at 92 (a half-time or double-time feel).
namespace crate::Tempo
{
struct Filter
{
    double lo = 0, hi = 0;     // inclusive; 0 / 0 = any tempo
    bool halfDouble = false;
    double target = 0;         // the tempo asked for (the middle of a range): closest sounds first

    bool isActive() const { return hi > 0; }
    bool isExact() const { return isActive() && std::abs ((hi - lo) - 1.0) < 1.0e-6; }
    bool isNear() const;
    bool matches (double bpm) const;
    // How far a sound's tempo is from what was asked (0 = spot on, half/double counted as a match).
    double distance (double bpm) const;
    // "92 BPM", "90-100 BPM", "near 120 BPM", with " (or half/double)" when halfDouble is on.
    juce::String describe() const;
    // For a small button: "92", "90-100", "~120" (empty when any tempo).
    juce::String shortText() const;

    static Filter exactly (double bpm, bool halfDouble = false);
    static Filter between (double a, double b, bool halfDouble = false);
    static Filter near (double bpm, bool halfDouble = true);
};

// What someone typed in a BPM box: "92", "92 bpm", "90-100", "90 to 100", "~120" / "near 120".
// Returns an inactive filter when it isn't a tempo (empty, a word, out of 20-400).
Filter parse (const juce::String& typed, bool halfDouble = false);

// Takes a tempo written into a search ("dusty drums 92bpm", "keys 90-100 bpm") out of the text,
// leaving the other words. A bare number stays a word to search for (it could be "808" or a
// sample number), so the tempo needs "bpm" written next to it. Returns an inactive filter when
// there's none.
Filter takeFromSearch (juce::String& text, bool halfDouble = false);

// A loop that has no tempo in its name or file: if it's a whole number of bars (1, 2, 4, 8, 16 or
// 32 of 4/4), the tempo that makes it so, picked between 80 and 160 BPM. 0 when it's too short
// or long to be a loop. It's a guess (a 70 BPM bar looks like 140), so it's shown as one.
double guessFromLength (double seconds);
}
