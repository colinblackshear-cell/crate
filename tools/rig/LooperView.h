#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LoopEditor.h"
#include "StripFxPanel.h"
#include "SongGrid.h"
#include "TimelineView.h"
#include "RigEngine.h"
#include "core/ui/Theme.h"
#include "Vintage.h"

// The Looper screen (F3): eight layers in time with the Rig.
//
// Along the top: Rec next layer (records the next empty layer from the next bar; press again to
// finish it), Overdub and Undo (on the layer picked last), the length and what the next layer
// records from (your mic, the vocoder or the whole mix), whether it's in sync, and an LCD saying
// what happens next ("LAYER 6 IN 2 BEATS").
//
// Then a row per layer: its number (click to record it, as keys 1-8 do), its name and source
// (click to change them), a lamp and a word for its state, the loop with its playhead and its
// ends in yellow (double-click to trim them: LoopEditor), its effect slot (the loop's own
// effects), its length in bars, level, mute, solo, and a menu (edit, undo, clear, import a loop
// or song, save it, or put Record and Mute on buttons of your keyboard). Song grid lays the
// loops (and tools) out into the song's parts.
//
// Under the top row, the loop bank: all eight layers as one named batch. Back and Next step
// through your banks, the name picks one (or renames, copies or deletes it), Save bank keeps the
// layers in it, and + New batch keeps them and gives you eight empty layers to start again.
// While the clock plays, a bank comes in at the next bar. Song parts remember their bank.
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
    // Opens (or closes) the song grid: loops and tools down the side, song parts across.
    void showSongGrid (bool show);
    // Set by the Rig: the Song button opens the Song builder page instead of the grid here.
    std::function<void()> onSongBuilder;
    // And the Timeline button opens the Song screen's Bars view (the same song, bar by bar).
    std::function<void()> onBarsView;
    // A loop's All pedals: its effects on the Effects screen.
    std::function<void (int layer)> onPedals;
    // Quantize (the top row's button, a loop's menu): the Quantize panel on a RigQuantizer::Target.
    std::function<void (int target, juce::Component* from)> onQuantize;
    SongGrid* getSongGrid() const { return songGrid.get(); }
    // Opens (or closes) the timeline: the song composer, lanes of blocks over a bar ruler.
    void showTimeline (bool show);
    TimelineView* getTimeline() const { return timeline.get(); }

    // Status messages for the Rig's message line.
    std::function<void (const juce::String&)> onSay;

    // The top row, as clicks do (the tests use these).
    void pressRecNext();
    void pressOverdub();
    void pressUndo();
    void setNextBars (int bars);       // for the empty layers
    void setNextSource (int source);   // RigLooper::Source, for the empty layers
    void soloLayer (int layer);        // again to un-solo
    void pressLayer (int layer);       // the layer's number
    int nextEmptyLayer() const;        // -1 when they're all in use
    int busyLayer() const;             // armed or recording, or -1
    juce::String statusText() const;   // what the LCD says
    int getSelected() const { return selected; }
    // The loop bank row (RigEngine's loop banks).
    void pressSaveBank();
    void pressNewBank();
    void stepBank (int delta);         // Back (-1) and Next (+1)
    void pickBank (int bank);
    juce::String bankText() const;     // what the bank's name button says
    int getSoloed() const { return soloed; }

private:
    class Row;
    void timerCallback() override;

    RigEngine& engine;
    juce::OwnedArray<Row> rows;
    juce::Label help;
    int selected = -1, soloed = -1;
    std::array<bool, RigLooper::kLayers> mutesBeforeSolo {};
    juce::TextButton recNext { "Rec next layer" }, overdub { "Overdub" }, undoButton { "Undo" }, quantizeButton { "Quantize" };
    juce::OwnedArray<juce::TextButton> lengthButtons, sourceButtons;
    juce::Rectangle<int> controlsArea, lengthLabel, sourceLabel, syncArea, statusArea, headings;
    juce::String shownStatus;
    void updateTop();
    std::unique_ptr<LoopEditor> editor;
    void closeEditor();
    std::unique_ptr<StripFxPanel> fxPanel;
    int fxLayer = -1;
    std::unique_ptr<SongGrid> songGrid;
    juce::TextButton songButton { "Song" };
    std::unique_ptr<TimelineView> timeline;
    juce::TextButton timelineButton { "Timeline" };
    juce::TextButton bankBack { "Back" }, bankName, bankNext { "Next" }, saveBank { "Save bank" }, newBank { "+ New batch" };
    juce::Rectangle<int> bankArea;
    int overlayTop = 86;
    int shownQueuedBank = -1;
    void showBankMenu();
    void say (const juce::String& s) { if (onSay && s.isNotEmpty()) onSay (s); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LooperView)
};
