#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/Audio.h"
#include "core/SoundFx.h"

// The nine effect knobs on the Effects sheet, and how they map onto core/SoundFx::Settings.
namespace pocket
{
struct FxKnobs
{
    enum Index { pitch, lowCut, highCut, reso, drive, bits, rate, delay, reverb, count };

    double v[count] = { 0, 20, 20000, 0, 0, 16, 44100, 0, 0 };

    static bool isOn (int index, double value);
    int onCount() const;
    bool anyOn() const { return onCount() > 0; }
    crate::SoundFx::Settings toSettings() const;   // switched-off knobs stay neutral
    juce::var toVar() const;
    static FxKnobs fromVar (const juce::var&);
};

// One saved sound. The audio itself is Sounds/<file>.wav; the rest lives in sounds.json.
struct SoundItem
{
    juce::String id, name, tag, file;
    juce::Time date;
    double seconds = 0;
    FxKnobs fx;
    std::vector<int> markers;   // chop markers, in samples
};

inline const char* const kTagRecorded = "RECORDED";
inline const char* const kTagImported = "IMPORTED";
inline const char* const kTagFromMac = "FROM MAC";

// The app's sound library: the Documents folder (visible in the Files app), Sounds/*.wav and a
// sounds.json index next to it.
class Library : public juce::ChangeBroadcaster
{
public:
    explicit Library (juce::File root = defaultRoot());

    static juce::File defaultRoot();
    const juce::File& getRoot() const { return root; }
    // The idea being worked on: its pads, beat and loops live in this folder (Ideas/<id>). The sounds
    // stay shared by every idea. Until an idea is picked it's the root, as before ideas.
    const juce::File& ideaRoot() const { return ideaFolder; }
    void setIdeaRoot (juce::File f) { ideaFolder = std::move (f); }

    const std::vector<SoundItem>& items() const { return sounds; }   // newest first
    const SoundItem* find (const juce::String& id) const;
    juce::File fileFor (const juce::String& id) const;
    crate::SharedAudio load (const juce::String& id);

    // Returns the new id (empty on failure).
    juce::String add (const juce::String& name, const juce::String& tag, const juce::AudioBuffer<float>&,
                      const FxKnobs&, const std::vector<int>& markers, juce::Time date = juce::Time::getCurrentTime());
    // Replaces an existing sound's audio and settings (the Record tab editing a saved sound).
    bool update (const juce::String& id, const juce::String& name, const juce::AudioBuffer<float>&,
                 const FxKnobs&, const std::vector<int>& markers);
    bool rename (const juce::String& id, const juce::String& newName);
    bool remove (const juce::String& id);
    // Copies a WAV/AIFF/MP3 in as a WAV. Returns the id, or empty with `error` set.
    juce::String importFile (const juce::File&, juce::String& error);

    juce::AudioFormatManager& formats() { return formatManager; }

private:
    void saveIndex();
    void loadIndex();
    juce::File uniqueFile (const juce::String& name) const;

    juce::File root, soundsFolder, indexFile, ideaFolder;
    std::vector<SoundItem> sounds;
    juce::AudioFormatManager formatManager;
};
}
