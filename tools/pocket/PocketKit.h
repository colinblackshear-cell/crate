#pragma once

#include "BeatsEngine.h"
#include "PocketLibrary.h"

#include <filesystem>

namespace pocket
{
// One pad of the kit: a sound from the Sounds library (or one slice of it) plus the pad's settings.
struct KitPad
{
    juce::String soundId, name;
    int from = 0, to = -1;      // the slice of the sound in samples; to < 0 = to the end
    int volume = 100;           // 0..100
    int pan = 50;               // 0..100, 50 = centre
    int tune = 0;               // tenths of a semitone, -120..120
    PadMode mode = PadMode::oneShot;
    crate::SharedAudio audio;   // the sound as it plays, with its effects baked in; null = empty pad

    bool empty() const { return audio == nullptr; }
    double seconds() const { return audio != nullptr ? (double) audio->getNumSamples() / crate::kSampleRate : 0.0; }
    PadSetup setup() const { return { audio, volume, pan, tune, mode }; }
};

// The MPC pad kit: 64 pads in banks A to D, saved to Documents/Kits as json that points at the Sounds
// WAV files, and exportable as a real MPC2000XL program with its sounds.
class Kit
{
public:
    explicit Kit (Library&);

    juce::String name = "PORCHKIT";
    std::array<KitPad, kPads> pads;

    int usedPads() const;
    size_t memoryBytes() const;                       // as the MPC would hold it (16-bit)
    static constexpr size_t mpcMemoryBytes = 32u * 1024 * 1024;

    // Puts a sound (or a slice of it) on a pad. Returns false when the sound can't be read.
    bool assign (int pad, const juce::String& soundId, int from = 0, int to = -1, const juce::String& padName = {});
    // The same, from audio already loaded.
    void assign (int pad, const SoundItem&, crate::SharedAudio whole, int from, int to, const juce::String& padName);
    void clear (int pad) { pads[(size_t) pad] = {}; }
    int nextEmpty (int bank) const;                   // lowest empty pad of the bank, or -1
    // Cuts the sound at its chop markers and fills the pads from `first` to the end of its bank (only
    // the empty ones when `onlyEmpty`). Returns how many pads it filled.
    int chop (int first, const juce::String& soundId, bool onlyEmpty);

    static juce::String padLabel (int pad);           // "A01".."D16"
    static juce::String mpcName (const juce::String&, int maxLength = 16);   // MPC-safe: capitals, digits, space, - and _
    static juce::String lengthText (double seconds);  // "0:02"

    juce::File kitsFolder() const;
    void save() const;
    void load();

    struct Export
    {
        bool ok = false;
        juce::String error, folderName;
        juce::File zip, folder;
        int pads = 0;
        size_t bytes = 0;
    };
    // Writes the program and sounds into `work`/<FOLDER>/ and zips them to `work`/<FOLDER>.zip.
    Export exportForMpc (const juce::File& work) const;
    // Reads an export back with the MPC file code and checks it matches the kit. Empty = fine.
    juce::String verify (const Export&) const;

private:
    crate::SharedAudio sliceOf (const SoundItem&, const juce::AudioBuffer<float>&, int from, int to) const;

    Library& library;
};
}
