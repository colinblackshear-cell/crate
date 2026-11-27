#pragma once

#include "RigAsker.h"
#include "RigEngine.h"
#include "RigLogicImport.h"

#include <functional>
#include <memory>

// The standalone app's one way to replace the show you're on (New song, Open, an idea, a recent
// song, Import from Logic, the lesson, Catch an idea, a spark, a tile) and to save it (Cmd+S,
// Save show, Save As), so a show is never lost without your say. No windows of its own: it asks
// through a rigdirty::Asker, and the window it's in does what only it can (a Host). Message thread.
//
// A request, in order:
//  1. One at a time: while one is under way (a question or a file window is open), another does
//     nothing. Every answer that comes back carries its request's number, and is dropped if that
//     request is over (stale), another show was opened meanwhile, or the flow is gone.
//  2. What replaces the show is found first, where that can fail or be cancelled: Open's file
//     window and reading the file; Import's folder window and reading the stems. Cancelled or
//     failed there: nothing has happened.
//  3. Does the show need saving? Not if it's blank and untitled. Otherwise yes, unless it has a
//     name, the dirty model is on, its check finished within a bounded wait (verifyTimeoutMs), and
//     the answer is clean. An untitled show with something in it always asks (the Last show you
//     relaunched into is not a saved song). Pending, timed out, recording or model off: it asks.
//  4. Cancel: nothing changes, playback goes on. Save: to its file if it has one whose folder is
//     there, else Save As. A failed save or a cancelled Save As stops here, nothing replaced. A
//     save that worked: 3 again (it asks again if the show changed since, a loop is still
//     recording, or the check can't finish), at most maxRounds questions.
//  5. Don't Save, or nothing to save: the show is replaced there and then, in the same call
//     (nothing in between can fail: what can was done in 2).
//
// The Logic plugins and the one-tool plugins never use this (RigView keeps their old way).
class RigDocFlow
{
public:
    // What only the window can do.
    struct Host
    {
        virtual ~Host() = default;
        virtual juce::File getCurrentShow() const = 0;
        virtual void setCurrentShow (const juce::File&) = 0;
        virtual void say (const juce::String&) = 0;
        // Where Save As starts, and the Open window.
        virtual juce::File showsFolder() const = 0;
        // The engine has the new show: the window follows it (refresh, undo, windows...).
        enum class Replaced { blank, opened, imported };
        virtual void replaced (Replaced) = 0;
    };

    // What a replacement did, for the entry point's own next step.
    struct Outcome
    {
        juce::String saved;   // the show saved first (its name), or empty
        juce::String said;    // Import's report
        juce::File file;      // the show opened
        bool cleared = false; // the Rig was made a new song
    };
    using Then = std::function<void (const Outcome&)>;

    // The question's words ("{name}" is the show you're on).
    struct Wording
    {
        juce::String title, message, save, dontSave;
    };

    RigDocFlow (RigEngine&, rigdirty::Asker&, Host&);
    ~RigDocFlow();
    void setAsker (rigdirty::Asker& a) { asker = &a; }

    // Each returns false when it didn't start (another is under way).
    // A new song (New song, the lesson, Catch an idea, a spark, a tile). `clearWhenBlank`: a
    // blank untitled Rig is made new anyway (New song); otherwise it's left as it is.
    bool newSong (const Wording&, Then, bool clearWhenBlank = true);
    // Open show...: the file window, then the show.
    bool openChooser (Then = {});
    // A show you picked elsewhere (an idea, a recent song, Continue).
    bool openFile (const juce::File&, Then = {});
    // Import a song from Logic: the folder window, the stems, then the new song.
    bool importFromLogic (Then = {});
    // Cmd+S and Save show: to the show's file, or Save As. Save show as...: always Save As.
    bool save();
    bool saveAs();

    bool isBusy() const { return current != nullptr; }
    // The show was replaced another way: what's under way is dropped (its answers are stale).
    void abandon();

    // The words of each entry point (as the app had them).
    static Wording newSongWords();
    static Wording lessonWords();
    static Wording startWords (const juce::String& what);
    static Wording openWords (const juce::File& file);
    static Wording importWords();
    // What a document save says ("Saved My song: the show and 2 loops, in ...").
    static juce::String savedMessage (RigEngine&, const juce::File&, const rigshow::Result&);
    // The folder for Import's window.
    static juce::File importStart();

    struct Stats
    {
        int questions = 0, saves = 0, failedSaves = 0, saveAsCancelled = 0, replacements = 0;
        int ignored = 0, stale = 0, unresolved = 0, roundsExceeded = 0;
    };
    const Stats& getStats() const { return stats; }

    // How long step 3 waits for the dirty model's check (ms).
    static inline int verifyTimeoutMs = 2000;
    // At most this many questions for one request (each one answered by you).
    static inline int maxRounds = 4;
    // Tests only: step 3 takes the model's published state, without the check (the old fault: an
    // edit nothing noted reads clean).
    static inline bool cachedStateForTests = false;

private:
    enum class Kind { blank, open, import, saveOnly };
    struct Request
    {
        Kind kind = Kind::blank;
        Wording words;
        Then then;
        bool clearWhenBlank = true, saveAsOnly = false;
        juce::File file;              // the show to open
        juce::ValueTree tree;         // ... read
        std::unique_ptr<rigimport::Song> song;
        juce::File startShow;         // the show you were on when it started (or since saved)
        int rounds = 0;
        bool justSaved = false;
        juce::String savedName, changedWhy;
        juce::uint64 serial = 0;
    };

    RigEngine& engine;
    rigdirty::Asker* asker;
    Host& host;
    std::unique_ptr<Request> current;
    juce::uint64 serial = 0;
    std::shared_ptr<int> life = std::make_shared<int> (0);
    Stats stats;

    bool begin (std::unique_ptr<Request>);
    void end();
    // An answer for the request under way: dropped when it's stale.
    template <typename Arg>
    std::function<void (Arg)> guard (std::function<void (Arg)>);
    bool read (Request&);
    void decide();
    juce::String whyProtect (const Request&);
    void answered (rigdirty::Choice);
    void saveFirst();
    void saveTo (const juce::File&);
    void replaceNow();
    juce::String showName() const;

    JUCE_DECLARE_NON_COPYABLE (RigDocFlow)
};
