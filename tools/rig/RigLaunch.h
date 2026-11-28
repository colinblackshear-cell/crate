#pragma once

#include "RigEngine.h"

// What the standalone app remembers between launches about the show you're on, and what it opens
// at launch (P2 CP4). Live only with the app's dirty model on (riglaunch::active, the one switch);
// off (the default), the app keeps its Last show as it always has (RigLastShow.h) and nothing
// here runs.
//
// The record is one value in the app's settings file ("launchDocument"), written with the
// settings' own save (JUCE writes a temporary file and renames it over the old one). It's
// rewritten whenever the truth changes: a show opened, saved or Saved As, a new untitled show, a
// rename, just before a document save starts, the launch itself (running: not a clean quit) and
// the quit's decision (a clean quit).
//
// At launch: a named show is opened as it is in its file (never the Last show, never recreated,
// never written to); a missing, unreadable or non-show file gives a new song and a notice; a file
// that differs from what Kaleidobox last saved or opened there is opened as it is now, with a
// notice, and its next Save asks first (the Save As window, which asks before replacing it).
namespace riglaunch
{
// The one switch for all of CP4 (the quit question, the launch record, the unsaved dot).
inline bool active (RigEngine& e) { return e.getDirty().isEnabled(); }

// A show file as Kaleidobox last wrote (or opened) it: the loops' revision it names, the size
// and modification time (the cheap first look), and a hash of the whole XML.
struct Identity
{
    juce::String revision, hash;
    juce::int64 size = -1, modified = 0;
    bool known() const { return hash.isNotEmpty(); }
    bool sameContent (const Identity& o) const { return known() && o.known() && size == o.size && hash == o.hash; }
    bool sameStat (const Identity& o) const { return known() && size == o.size && modified == o.modified; }
};
Identity identify (const juce::File&);                           // reads the file
Identity identify (const juce::File&, const juce::MemoryBlock&); // its bytes, already read
Identity statOf (const juce::File&);                             // size and time only

struct Record
{
    enum class Intent { empty, reopenDocument };
    Intent intent = Intent::empty;
    juce::File document;          // the named show (reopenDocument)
    Identity identity;            // as Kaleidobox last saved or opened it
    bool cleanShutdown = true;    // false while Kaleidobox runs; true once a quit was decided
    juce::File saving;            // a document save was starting (cleared when it's recorded)
    bool present = false;         // (read: there was a record)
};
static constexpr const char* kKey = "launchDocument";
Record read (juce::PropertiesFile&);

// Keeps the record up to date (message thread). The window calls it; the app makes it.
class Tracker
{
public:
    explicit Tracker (juce::PropertiesFile&);
    const Record& record() const { return rec; }
    const Record& previous() const { return before; }   // what the last run left

    // A named show is the one you're on now (its identity read from its file). The Last show is
    // never one: untitled instead.
    void opened (const juce::File&);
    void opened (const juce::File&, const Identity&, bool confirmFirstSave);
    void untitled();
    void renamed (const juce::File& to);
    // Around a document save.
    void saving (const juce::File&);
    void saved (const juce::File&, const rigshow::Result&);
    // This run started (not a clean quit until quitting).
    void launched();
    // The quit's decision: reopen `current` next time (if it has a name) or a new song; a clean quit.
    bool quitting (const juce::File& current);
    // Saving over `file` asks first (the Save As window): it isn't as Kaleidobox last saved or
    // opened it (moved, deleted, changed elsewhere: its size or time differ), or the launch said
    // it had changed.
    bool confirmBeforeSaving (const juce::File&) const;

    int writes = 0, failedWrites = 0;
    // Tests only: the settings file can't be written (the record stays as it was on disk).
    static inline bool failWritesForTests = false;

private:
    bool store();
    juce::PropertiesFile& settings;
    Record rec, before;
    bool confirmFirst = false;
};

// What the launch did.
struct Launch
{
    bool opened = false;          // a named show (else a new song)
    juce::File document;
    juce::String title, notice;   // what to tell you (empty: nothing)
    bool changed = false, interrupted = false;
};
// Puts the show the record says on `engine` (fresh, its model on): the named show as it is in its
// file, or a new song. Then records this run (Tracker::launched). Never reads the Last show.
Launch launch (RigEngine&, Tracker&);
} // namespace riglaunch
