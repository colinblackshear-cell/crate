#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RigEngine.h"
#include "Vintage.h"

// The Automation panel: every knob move you've recorded, a lane per setting, from the track
// rail's Automation button (or Cmd+Shift+A).
//
//   Rec moves   from the next bar, records whatever you turn (on screen or with a learned knob,
//               on any tool or its effects); press again to close at the end of the bar.
//   Read        lanes move their settings (off: none do, so you can play it straight).
//   Clear all / Undo
//   Each lane   its setting, On, Loop or Once (round and round, or once along the song from
//               where you recorded it), its length in bars (halve / double), the moves drawn
//               over that length with where it's playing (drag on it to draw new moves), Clear.
class AutomationPanel : public juce::Component,
                        private juce::Timer
{
public:
    explicit AutomationPanel (RigEngine&);
    ~AutomationPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // As the buttons do (the tests use these).
    void pressRec();
    void pressClearAll();
    void pressUndo();
    void pressRead();
    void pressLaneOn (int lane);
    void pressLaneLoop (int lane);
    void pressLaneLength (int lane, bool longer);
    void pressLaneClear (int lane);
    // Draws on a lane, from x0 to x1 (0..1 across it) at heights y0 and y1 (0..1, 1 is the top).
    void drawOnLane (int lane, float x0, float y0, float x1, float y1, bool newStroke);
    int getNumRows() const;
    juce::String getStatus() const { return status; }

    std::function<void (const juce::String&)> onSay;

    static constexpr int kWidth = 860, kHeight = 480, kRowHeight = 66;

private:
    class Row;
    class List;
    void timerCallback() override;
    void update();
    void rebuildRows();
    void say (const juce::String&);

    RigEngine& engine;
    juce::String status;
    juce::TextButton recButton { "Rec moves" }, readButton { "Read" }, clearAllButton { "Clear all" }, undoButton { "Undo" };
    std::unique_ptr<List> list;
    juce::Viewport viewport;
    juce::Rectangle<int> titleArea, hintArea, statusArea, emptyArea;
    int shownVersion = -1;
    int shownRec = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationPanel)
};
