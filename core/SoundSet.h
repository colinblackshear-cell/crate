#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Audio.h"

#include <optional>
#include <vector>

// A set of sounds: one bank of 16 pads, or a whole kit of 64, with each pad's settings, saved
// so it can be loaded again on the Mac, on the phone or in Logic. A set is a plain folder you
// can see in Finder (or the Files app), so it travels by hand too:
//
//     <Sets>/Dusty break chops/set.json        what's on which pad and how it's set
//     <Sets>/Dusty break chops/A01 Dusty 1.wav one 44.1 kHz WAV per pad
//
// The Mac keeps them in ~/Music/Crate/Sets, the phone in Documents/Sets.
struct SoundSet
{
    struct Pad
    {
        int index = 0;                 // 0..63 in a kit; 0..15 in a one-bank set
        juce::String name;
        crate::SharedAudio audio;      // 44.1 kHz, 1 or 2 channels
        int volume = 100, pan = 50, tune = 0;   // 0..100, 0..100 (50 = centre), tenths of a semitone
        int mode = 0;                  // 0 one shot, 1 a new hit cuts the last one off, 2 plays while held
        bool mono = false;
        int muteGroup = 0;
        float cutoff = 20000.0f, resonance = 0.0f;
        int attack = 0, decay = 0;
        double beats = 0;              // a loop's length in beats, for keep in time (0 = not a loop)
    };

    juce::String name;
    bool wholeKit = false;             // 64 pads (A..D); else one bank of 16
    double bpm = 0;                    // the kit's tempo when it was saved (0 = not set)
    juce::String madeOn;               // "Mac" or "Phone"
    juce::Time saved;
    std::vector<Pad> pads;             // only the pads with a sound

    int padCount() const { return (int) pads.size(); }
    juce::String describe() const;     // "8 pads · one bank · from the phone"

    static juce::File macFolder();     // ~/Music/Crate/Sets
    // Writes the set into `root` as a folder named after it (a name already there gets " 2"...).
    // Returns the set's folder, or an empty File with `error` set.
    juce::File save (const juce::File& root, juce::String& error) const;
    // Reads a set's folder (or its set.json). Pads whose WAV is missing are left out.
    static std::optional<SoundSet> load (const juce::File& folder, juce::String& error);
    // Only set.json: the name, size and pads without their audio (fast, for lists).
    static std::optional<SoundSet> peek (const juce::File& folder);
    // The sets in `root`, newest first.
    static juce::Array<juce::File> list (const juce::File& root);
    static juce::String fileNameFor (const juce::String& name);   // safe as a folder or file name
};
