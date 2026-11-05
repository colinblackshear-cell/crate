#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <atomic>
#include <vector>

// The sound bank: every sample you have, from your own folders and Splice, in one place you
// can search. It scans the folders in the background, sorts each sound into a kind (kick,
// snare, hat, bass, vocal...) from its name and the folders it's in, and picks up the tempo
// and key from names like "Funky_Bass_Loop_92_Fmin.wav" (or a WAV's ACID tempo). What it
// learns is kept in an index file, so the next scan only looks at new and changed files.
//
// The files stay where they are; the bank only reads them.
class SoundBank : public juce::ChangeBroadcaster,
                  private juce::Thread
{
public:
    enum Kind { kick, snare, clap, hat, cymbal, tom, perc, bass, synth, keys, guitar, vocal, fx, other, numKinds };
    static juce::String kindName (int);     // "Kicks", "Snares"...

    struct Sound
    {
        juce::File file;
        juce::String name;      // the file name without its extension
        juce::String where;     // the folders it's in, under the bank folder ("Splice/sounds/packs/...")
        int kind = other;
        bool loop = false;
        double bpm = 0;         // 0 = not known
        juce::String key;       // e.g. "F min", empty if not known
        float seconds = 0;
        juce::int64 size = 0, modified = 0;
    };

    // The folders and the index live in `home` (normally ~/Music/Crate).
    explicit SoundBank (juce::File home = defaultHome());
    ~SoundBank() override;

    static juce::File defaultHome();
    // Folders worth scanning that exist on this Mac: the Crate sounds folder, Splice's, and
    // the usual sample folders.
    static juce::Array<juce::File> suggestedFolders();

    //==========================================================================
    // Message thread.
    juce::Array<juce::File> getFolders() const;
    void addFolder (const juce::File&);
    void removeFolder (const juce::File&);
    // Looks through the folders again, in the background (new and changed files only).
    void rescan();
    bool isScanning() const { return isThreadRunning(); }
    int getFilesFound() const { return found.load(); }

    enum class Length { any, loops, oneShots };
    struct Query
    {
        juce::String text;      // every word must be in the name or folders
        int kind = -1;          // -1 = any
        Length length = Length::any;
        double nearBpm = 0;     // > 0: loops within 8% of this tempo (or half / double it)
        int limit = 2000;
    };
    std::vector<Sound> search (const Query&) const;
    int size() const;

    //==========================================================================
    // Working out what a sound is from its name (public for tests).
    static void describe (Sound&, const juce::String& relativePath);

private:
    void run() override;
    void loadIndex();
    void saveIndex() const;
    void saveFolders() const;

    juce::File home;
    juce::Array<juce::File> folders;
    mutable juce::CriticalSection lock;   // sounds and folders
    std::vector<Sound> sounds;
    std::atomic<int> found { 0 };
    std::atomic<bool> again { false };
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE (SoundBank)
};
