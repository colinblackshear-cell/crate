#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RigEngine.h"
#include "Vintage.h"

// The Quantize panel: one place to pull what you've played onto the beat, the same from every
// screen. It opens from the top bar's Quantize button (on what the screen you're on is showing),
// or a tool's own Quantize button (on that tool), and Cmd+U quantizes straight away without it.
//
//   What        the MPC's sequence, the drum machine's pattern, the Bass's pattern, the Synth's
//               arp steps, all loops, or one loop.
//   Grid        1/4, 1/8, 1/8 T, 1/16, 1/16 T, 1/32.
//   Strength    how far each note moves toward the grid (100% is right on it).
//   Swing       pushes every other 1/8 or 1/16 late (50% is straight).
//   New loops   quantizes each loop as its recording finishes.
//   Quantize    does it; Undo puts it back.
class QuantizePanel : public juce::Component,
                      private juce::Timer
{
public:
    QuantizePanel (RigEngine&, int target);
    ~QuantizePanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void setTarget (int);
    int getTarget() const { return target; }
    // As the buttons do (the tests use these).
    void pressQuantize();
    void pressUndo();
    void pickGrid (int grid);
    juce::String getStatus() const { return status; }
    // Shows what happened in its LCD (quantized from somewhere else: Cmd+U, a new loop).
    void setStatus (const juce::String& text) { status = text; update(); repaint(); }

    std::function<void (const juce::String&)> onSay;

    static constexpr int kWidth = 560, kHeight = 290;

private:
    void timerCallback() override;
    void update();
    void say (const juce::String&);

    RigEngine& engine;
    int target = RigQuantizer::allLoops;
    juce::String status;
    juce::ComboBox what;
    juce::OwnedArray<juce::TextButton> grids;
    vintage::Knob strength { "Strength", vintage::colours::play }, swing { "Swing", vintage::colours::next };
    juce::ToggleButton newLoops { "Quantize new loops as they finish" };
    juce::TextButton quantizeButton { "Quantize" }, undoButton { "Undo" };
    juce::Rectangle<int> titleArea, gridLabel, detailArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QuantizePanel)
};
