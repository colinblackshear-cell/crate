#pragma once

#include <juce_events/juce_events.h>

#include "core/Edits.h"

// The sound being edited, with every step kept for undo and redo.
//
// Each step is a whole new buffer (edits never change one in place), so undo is just going
// back to the previous buffer. Old steps are dropped when there are too many or they use too
// much memory.
class EditDocument : public juce::ChangeBroadcaster
{
public:
    // Brings in a new sound (recorded, opened, or from the library). If there's already a
    // sound, this is an undoable step like any edit, so recording over your work never loses
    // it. `isSaved` says whether the sound is already safely in a file (opened from one).
    void load (crate::SharedAudio, const juce::String& name, const juce::String& actionName,
               const juce::File& source = {}, bool isSaved = false);
    void close();

    // Records an edit. `selectionBefore` is restored if the edit is undone.
    void apply (crate::Edits::Result, const juce::String& actionName, juce::Range<int> selectionBefore);

    bool canUndo() const { return index > 0; }
    bool canRedo() const { return index + 1 < (int) steps.size(); }
    juce::String getUndoName() const { return canUndo() ? steps[(size_t) index].action : juce::String(); }
    juce::String getRedoName() const { return canRedo() ? steps[(size_t) index + 1].action : juce::String(); }
    bool undo();
    bool redo();

    const crate::SharedAudio& getAudio() const;
    bool hasAudio() const { return getAudio() != nullptr && getAudio()->getNumSamples() > 0; }
    // What should be selected now (after an edit, undo or redo).
    juce::Range<int> getSelection() const;

    // The name and file of the current step's sound.
    juce::String getName() const;
    void setName (const juce::String&);
    juce::File getSourceFile() const;

    bool hasUnsavedChanges() const { return index != savedIndex; }
    void markSaved() { savedIndex = index; sendChangeMessage(); }

    static constexpr int kMaxSteps = 200;
    static constexpr size_t kMaxBytes = (size_t) 1500 * 1024 * 1024;

private:
    struct Step
    {
        crate::SharedAudio audio;
        juce::String action;               // the edit that made this step
        juce::Range<int> selectionAfter;   // selection once this step is reached by doing it
        juce::Range<int> selectionBefore;  // selection to restore when it's undone
        juce::String name;
        juce::File source;
    };

    void trim();

    std::vector<Step> steps;
    int index = -1;
    int savedIndex = -1;
    bool lastWasUndo = false;
};
