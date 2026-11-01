#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include "Audio.h"

// The sound library every Crate tool shares: a plain folder of WAV files
// (~/Music/Crate/Sounds), so you can also manage it in Finder or drag sounds into Logic.
//
// What the tools know about a sound travels inside the WAV file itself, so it survives
// renames and copies: tempo, beats and root note in the standard ACID chunk (Logic and most
// DAWs read it), and the tool that made it plus your tags in the INFO chunk.
class SoundLibrary : public juce::ChangeBroadcaster
{
public:
    struct Info
    {
        juce::String name;
        double bpm = 0;            // 0 = not set
        double beats = 0;          // length in beats; 0 = not a loop / unknown
        int rootNote = -1;         // MIDI note, -1 = not set
        juce::StringArray tags;
        juce::String tool;         // which Crate tool made it, e.g. "Sample Editor"
    };

    struct Entry
    {
        juce::File file;
        Info info;
        double seconds = 0;
        int channels = 0;
        juce::Time modified;
    };

    explicit SoundLibrary (juce::AudioFormatManager&, juce::File folder = defaultFolder());

    static juce::File defaultFolder();
    const juce::File& getFolder() const { return folder; }

    void rescan();
    const std::vector<Entry>& getEntries() const { return entries; }   // newest first

    // Saves 44.1 kHz audio as a 24-bit WAV named after info.name, never overwriting a sound
    // that's already there. Returns the new file, or an empty File on failure.
    juce::File save (const juce::AudioBuffer<float>&, const Info&);
    bool rename (const Entry&, const juce::String& newName);
    bool remove (const Entry&);   // moves the file to the Trash

    // Reading and writing the metadata on its own (also used for exported files).
    static std::unordered_map<juce::String, juce::String> toMetadata (const Info&);
    static Info fromMetadata (const juce::StringPairArray&, const juce::String& name);

    // Prefix for drag-and-drop descriptions of library sounds.
    static constexpr const char* dragPrefix = "crate-sound:";

private:
    juce::AudioFormatManager& formats;
    juce::File folder;
    std::vector<Entry> entries;
};
