#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LoopEditor.h"
#include "StripFxPanel.h"
#include "RigEngine.h"
#include "core/ui/Theme.h"

// The Looper tab: the eight layers, one row each. A big Record button (record / stop /
// overdub), what it records from, how many bars, the loop drawn with its playhead, mute,
// level, FX (the loop's own effects), Edit (where it starts and ends: LoopEditor), undo and clear, and a menu to import a loop or song, export the loop, or put Record
// and Mute on buttons of your keyboard.
class LooperView : public juce::Component,
                   private juce::Timer
{
public:
    explicit LooperView (RigEngine&);
    ~LooperView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Re-reads the layers' settings (after a show is loaded).
    void refresh();

    // Opens the start/end editor over the layers for one loop (if it has one).
    void editLoop (int layer);
    LoopEditor* getEditor() const { return editor.get(); }
    // Opens (or closes, if it's open) a loop's effects along the bottom.
    void showFx (int layer);
    StripFxPanel* getFxPanel() const { return fxPanel.get(); }

    // Status messages for the Rig's message line.
    std::function<void (const juce::String&)> onSay;

private:
    class Row;
    void timerCallback() override;

    RigEngine& engine;
    juce::OwnedArray<Row> rows;
    juce::Label help;
    std::unique_ptr<LoopEditor> editor;
    void closeEditor();
    std::unique_ptr<StripFxPanel> fxPanel;
    int fxLayer = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperView)
};
