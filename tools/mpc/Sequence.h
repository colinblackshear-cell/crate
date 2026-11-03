#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

// The sequencer's data, MPC2000XL style: a sequence is a loop of bars with its own tempo, and
// holds tracks of pad hits. Time is counted in ticks, 96 to a beat like the MPC2000XL.
namespace seq
{
    constexpr int kTicksPerBeat = 96;
    constexpr int kMaxTracks = 64;
    constexpr int kMaxBars = 999;

    struct Note
    {
        int tick = 0;               // from the start of the sequence
        int pad = 0;                // 0..63, A01..D16
        int velocity = 100;         // 1..127
        int length = 24;            // ticks; only note-off pads use it

        bool operator< (const Note& o) const { return tick < o.tick || (tick == o.tick && pad < o.pad); }
        bool operator== (const Note& o) const { return tick == o.tick && pad == o.pad && velocity == o.velocity && length == o.length; }
    };

    struct Track
    {
        juce::String name;
        bool mute = false;
        bool solo = false;          // when any track is soloed, only soloed tracks play
        std::vector<Note> notes;    // kept sorted by tick

        void add (Note);
        // Removes the notes of `pad` that start in [from, to). Returns how many went.
        int remove (int pad, int from, int to);
    };

    struct Sequence
    {
        juce::String name;
        double bpm = 90.0;
        int bars = 2;
        int beatsPerBar = 4;
        bool loop = true;
        std::vector<Track> tracks;

        Sequence();
        int ticksPerBar() const { return beatsPerBar * kTicksPerBeat; }
        int lengthTicks() const { return bars * ticksPerBar(); }
        int usedTracks() const;
        bool isEmpty() const { return usedTracks() == 0; }

        // Changes the number of bars; notes past the new end are dropped.
        void setBars (int);

        juce::ValueTree toValueTree() const;
        static Sequence fromValueTree (const juce::ValueTree&);
    };

    // A song plays sequences in order, MPC style: each step is a sequence and how many times
    // it repeats (verse x4, chorus x2...).
    struct SongStep
    {
        int sequence = 0;
        int repeats = 1;            // 1..99
    };

    struct Song
    {
        juce::String name;
        std::vector<SongStep> steps;
        bool loop = false;          // start again after the last step

        int totalBars (const std::vector<Sequence>&) const;
        // Removes steps that use sequence `index`, and renumbers those after it.
        void sequenceRemoved (int index);

        juce::ValueTree toValueTree() const;
        static Song fromValueTree (const juce::ValueTree&, int numSequences);
    };

    //==========================================================================
    // Timing correct (the MPC's name for quantize): the grid notes snap to, with swing.
    enum class Grid
    {
        off, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond, thirtySecondTriplet
    };

    int gridTicks (Grid);                           // 0 for off
    juce::String gridName (Grid);
    bool gridSwings (Grid);                         // swing applies to 1/8 and 1/16, as on the MPC

    // Where grid step `n` lands, with swing (50 = straight, up to 75) pushing the off-beats late.
    int stepTick (Grid, int swing, int n);
    // The nearest grid step to `tick`, wrapped into the sequence's length.
    int correct (int tick, Grid, int swing, int lengthTicks);
    // Snaps every note of a track to the grid.
    void correct (Track&, Grid, int swing, int lengthTicks);

    // Bars.beats.ticks as the MPC shows it: "001.1.00".
    juce::String formatPosition (double tick, int beatsPerBar);

    //==========================================================================
    // A standard MIDI file of the sequence: one track per sequence track, drum notes on channel
    // 10 using the MPC's pad notes, so the MPC2000XL (or anything else) can load it.
    juce::MemoryBlock toMidiFile (const Sequence&);
    // Reads a MIDI file's notes onto pads (notes the pads don't use are skipped).
    bool fromMidiFile (const void* data, size_t size, Sequence&, juce::String& error);
}
