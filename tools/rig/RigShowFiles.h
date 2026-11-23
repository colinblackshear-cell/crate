#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <vector>

// Saving a show's loop audio without ever breaking the last saved show.
//
// A show is a .crateshow XML file plus WAVs (loops, takes, other loop banks). One save is one
// *revision*: the XML and one complete, checked folder of WAVs. The XML names its folder with
// one attribute, audioRevision="<id>". Writing the XML (JUCE writes a temporary file and
// renames it over the old one) is the single commit point:
//
//   1. Stage: every WAV goes into a new "<show> loops.staging-<n>" folder, made fresh and used
//      by nothing else. Unchanged WAVs are hard links to files already saved (copies if a link
//      can't be made). Each file is flushed to disk and read back. The index
//      "Kaleidobox audio.xml" goes in last. The folder is flushed and renamed to
//      "<show> loops.r-<id>", and the folder holding it is flushed.
//   2. Commit: the XML is written with audioRevision="<id>".
//   3. Tidy, after the commit: the old "<show> loops" is renamed to its own "<show> loops.r-<old id>"
//      (or "<show> loops.r-legacy-<time>" if it came from an older build), and the new revision
//      is renamed to "<show> loops", so older builds, which only know "<show> loops", read it.
//
// Anything that stops before step 2 leaves the previous XML and its folder as they were. After
// step 2, loading looks for the XML's revision in "<show> loops" first and then in
// "<show> loops.r-<id>", so a stop in step 3 still loads the new revision. A revision that
// can't be found loads no loop audio (and says so) rather than pairing the XML with other audio.
//
// No saved revision is ever deleted here (older XML copies, Logic undo states and saved Logic
// projects may still name one). A save that fails removes its own staging folder; one that is
// killed leaves it ("<show> loops.staging-...", never referenced). Cleaning those up, and old
// revisions, is left for later (it needs to know what still refers to them).
//
// What's guaranteed: against the process stopping at any point (a crash, a kill, a force quit),
// the show loads its last committed revision, complete. Against sudden power loss: every file
// and folder is flushed before the commit (F_FULLFSYNC on macOS, which asks the drive to write
// its cache), and the folder is flushed after each rename, which is what APFS needs to keep
// them in order; this hasn't been tested with a real power cut, so it isn't promised.
//
// WAV files are only ever created new, in a fresh staging folder (the write refuses a path that
// already exists), and are never opened for writing again, so hard links never change.
//
// Message thread (or the host's state thread for the plugin); never the audio thread. Saves
// in one process are taken one at a time (saveLock).
namespace rigshow
{
static constexpr const char* kIndexName = "Kaleidobox audio.xml";
static constexpr const char* kRevisionAttribute = "audioRevision";
// The plugin's state when its loops couldn't be saved and there's no earlier state to give.
static constexpr const char* kUnsavedRevision = "unsaved";

// One WAV of the show, by its path in the loops folder ("Loop 3.wav", "Takes/Loop 5/Take 2.wav",
// "Bank 2/Loop 1.wav").
struct Asset
{
    juce::String path;
    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    // What the sound is in the engine, for its dirty model (saving ignores these): a live loop
    // layer at a version, a loop bank's buffer (`audio` itself), or a take by its id.
    enum class Kind { other, liveLayer, bank, take };
    Kind kind = Kind::other;
    int layer = -1, bank = -1, version = -1;
    juce::uint64 takeId = 0;
};

// Whether a save is the show's own (Save, Save As: what's on disk is now the show) or a copy of
// it (the last show kept for the next launch, the plugin's state for the host).
enum class SaveKind { copy, document };

// A WAV that's on disk in a saved revision.
struct Entry
{
    juce::String path;
    juce::int64 frames = 0, bytes = 0, modified = 0;
    int channels = 0;
    juce::uint64 sourceKey = 0;    // the audio that was written
    juce::uint64 decodedKey = 0;   // the audio as it reads back (what a reload has in memory)
};

// What is saved for the show an engine has: which show, which revision, and its files.
struct Committed
{
    juce::File showFile;
    juce::String revision;   // empty: an older build's folder (no index)
    std::vector<Entry> entries;
};

struct Result
{
    bool ok = false;
    juce::String error;      // why it failed (nothing was committed)
    juce::String warning;    // saved, but something after the commit didn't finish
    juce::String revision;
    int written = 0, linked = 0;
    bool unchanged = false;  // the audio was already saved as it is (nothing written)
};

// The staged, checked revision (step 1), ready for the XML to name it.
struct Staged
{
    juce::File showFile, folder;
    juce::String revision;
    std::vector<Entry> entries;
    bool created = false;    // this save made the folder (so it may remove it if the commit fails)
    std::vector<juce::uint64> keys;   // each asset's hash as it is in memory, in the assets' order
};

juce::File currentFolder (const juce::File& showFile);                         // "<show> loops"
juce::File revisionFolder (const juce::File& showFile, const juce::String& revision);

// The folder holding `revision` of the show (an empty revision is an older build's show:
// "<show> loops"). An invalid File, with why in `problem`, when it isn't there.
juce::File resolve (const juce::File& showFile, const juce::String& revision, juce::String* problem = nullptr);

// Whether `folder` holds `revision`, complete (index matches, every file there at its size).
bool holdsRevision (const juce::File& folder, const juce::String& revision);

juce::uint64 hashAudio (const juce::AudioBuffer<float>&);

// Step 1. False (with result.error) when anything failed; nothing is left referenced.
bool stage (const juce::File& showFile, const std::vector<Asset>&, double sampleRate,
            const Committed& previous, Staged& out, Result& result);
// Before the commit, when it failed: removes the staged folder if this save made it.
void discard (const Staged&);
// Step 2: writes the show XML to a new file beside it, flushes it to the disk, renames it over the
// show (atomic) and flushes the folder. False (the old show untouched) if any of that failed.
bool commitXml (const juce::XmlElement&, const juce::File& showFile);
// Step 3, after the commit. Never fails the save; a problem goes in result.warning.
void finish (const Staged&, Result&);

// The staged revision as what's now saved.
Committed committedFrom (const Staged&);

juce::CriticalSection& saveLock();

// For tests: called at each step ("stage", "wav", "wavChunk", "index", "stageRename",
// "beforeCommit", "commit" (the XML is flushed, not yet renamed), "afterCommit", "aside", "promote", "done"). Returning false makes
// that step fail as a disk error would.
struct Hooks
{
    std::function<bool (const juce::String& point, int n)> at;
};
Hooks& hooks();
bool reach (const juce::String& point, int n = 0);
} // namespace rigshow
