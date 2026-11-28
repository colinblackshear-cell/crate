#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <vector>

// Something Undo and Redo work on (a loop layer, the song, a screen's pattern...): a token that
// names the state it's in. Each change gives it a new one; a step undone puts back the token it
// had before, so the step under it still finds what it left. A change no step knows of (an edit
// with no step, a screen's own Undo) gives it a new token, and every step on it goes stale.
struct RigHistorySite
{
    int token = 0;
    int touch() { return token = ++serial; }
private:
    int serial = 0;
};

// Undo and Redo on the Mac, as on the phone: one history for everything, up to 64 steps. The
// phone keeps a copy of the whole song for each step; the Mac can't (putting a show back reloads
// its plugins), so it keeps a journal of steps that each know how to take themselves back and do
// themselves again. A step that can't any more (what it changed has changed since) is dropped
// and the one before tried: no step ever puts something back over newer work.
//
// Message thread only.
class RigHistory
{
public:
    struct Step
    {
        juce::String what;                  // "take on loop 1": the status line and the dock say it
        std::function<bool()> stillValid;   // null: always
        std::function<void()> undo, redo;
        juce::String site;                  // where it was made ("loop 1", "song"), for a screen's own Undo
    };
    static constexpr int kMaxSteps = 64;

    // A new change: it goes on top, and what Redo had is gone. Ignored while a step is being
    // undone or redone (the work it does is never a new step).
    void push (Step);
    // Takes back the newest step that still can (or does it again). Returns what it was ("" for none).
    juce::String undo();
    juce::String redo();
    bool canUndo();
    bool canRedo();
    // What Undo (Redo) would take back, without doing it ("" for none).
    juce::String nextUndo();
    juce::String nextRedo();
    // Whether the newest step Undo would take back was made at `site`.
    bool newestIs (const juce::String& site);
    void clear();
    bool isBusy() const { return busy; }
    int undoSteps() const { return (int) past.size(); }
    int redoSteps() const { return (int) future.size(); }
    // Called after anything changes what Undo or Redo would do.
    std::function<void()> onChange;

    // A step on a site: `before` is the site's token before the change (the change has touched
    // it since). It's good while the site still has the token it left; `alsoValid` (undoing: true
    // for Undo, false for Redo) can ask more.
    static Step siteStep (RigHistorySite&, const juce::String& site, const juce::String& what, int before,
                          std::function<void()> undo, std::function<void()> redo,
                          std::function<bool (bool undoing)> alsoValid = {});

    // The status line's words.
    static juce::String undidText (const juce::String& what, bool more);
    static juce::String redidText (const juce::String& what);
    static juce::String nothingToUndoText() { return "Nothing to undo yet."; }
    static juce::String nothingToRedoText() { return "Nothing to redo."; }

private:
    std::vector<Step> past, future;   // newest last
    bool busy = false;
    bool prune (std::vector<Step>&);
    juce::String take (std::vector<Step>& from, std::vector<Step>& to, bool undoing);
    void changed();
};
