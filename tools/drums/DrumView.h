#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DrumProcessor.h"
#include "StepTools.h"

// The Drum Machine's panel, styled like an early-80s analog rhythm box: walnut cheeks either side
// of a brushed charcoal face, cream silkscreen lettering, red, orange, yellow and cream stripes,
// and an amber seven-segment display (pattern, bar and step). It is the Rig's EDIT page for the
// drum machine, the standalone app and the Logic plugin.
//
// Top to bottom: a column for each of the 12 sounds (its name, which mutes it; its choice of
// sound; level, tune, decay, tone, snappy and pan knobs; and its SELECT key, which picks the sound
// the step keys write and plays it), the STEP TOOL strip (Hit, Roll x N, Builds, Flam, Accent,
// Soft/Loud, Erase; Rec live; Undo and Redo), the 16 big step keys of the selected sound (press one
// with a tool picked to write that step: the classic tap-write), the BAR row (Bar 1 to 4, Follow,
// the length, the step rate, Copy bar, Double), the step grid of every sound (each row has shift,
// reverse and a ... menu with copy, paste, clear, spread evenly and its own length), the pattern
// bar (16 patterns, A, B and A+B, fill, Beats, Kits, Random, Clear) and the intro and chain.
//
// Pick a tool, then click a step on the keys or the grid. Pick Roll x 3 and click a step for a
// stutter roll of three hits. (Right-click or Option-click a step in the grid still cycles the
// roll: 2, 3, 4, 6, 8 hits, then back to one.) The top row of the grid is the accent. When the
// window is short, KEYS and GRID take turns in the same space; tall windows show both.
class DrumView : public juce::AudioProcessorEditor,
                 private juce::Timer
{
public:
    explicit DrumView (DrumProcessor&);
    ~DrumView() override;

    using Tool = crate::drums::steptools::Tool;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The step grid (the 16 steps of the shown bar), as its own component so the tests can click it.
    class StepGrid : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        explicit StepGrid (DrumView& v) : owner (v) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        static constexpr int kRows = crate::drums::kVoices + 1;   // the accent row, then the sounds
        static constexpr int kColumns = 16;                       // one bar
        int columns() const { return kColumns; }
        juce::Rectangle<float> cell (int column, int row) const;
        // Row 0 is the accent; rows 1-12 the sounds.
        std::pair<int, int> cellAt (juce::Point<float>) const;
        // The row tool under a point: row 1-12 and 0 shift left, 1 shift right, 2 reverse, 3 the ... menu.
        std::pair<int, int> rowToolAt (juce::Point<float>) const;
        juce::Rectangle<float> rowTool (int row, int tool) const;
        // A click on a step of the shown bar (what the mouse does): the picked tool, or the roll cycle.
        void click (int column, int row, bool roll);

        std::function<void (int voice)> onAudition;

    private:
        DrumView& owner;
        int dragRow = -1;
        int dragValue = 0;
        int lastColumn = -1;
    };

    // The 16 big step keys of the selected sound.
    class StepKeys : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        explicit StepKeys (DrumView& v) : owner (v) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::Rectangle<float> keyRect (int key) const;   // the cap
        int keyAt (juce::Point<float>) const;

    private:
        DrumView& owner;
    };

    StepGrid& getGrid() { return grid; }
    StepKeys& getKeys() { return keys; }

    // The SELECT keys, the tools, the keys and the bars: what the buttons do, for the tests and the
    // keyboard too.
    void selectVoice (int voice, bool audition = true);
    int getSelectedVoice() const { return selected; }
    void setTool (Tool);
    Tool getTool() const { return tool; }
    void setRollCount (int n);   // 2..8
    int getRollCount() const { return rollCount; }
    void pressKey (int key);     // one of the 16 step keys of the shown bar
    void setBar (int bar);       // 0..3 (a bar past the end makes the pattern longer)
    void setFollow (bool);
    bool isFollowing() const { return follow; }
    int getShownBar() const;     // the bar the keys and the grid show now
    void copyBarToNext();
    void doubleLength();
    void shiftRow (int voice, int by);
    void reverseRow (int voice);
    void spreadRow (int voice, int hits);
    void clearRow (int voice);
    void copyRow (int voice);
    void pasteRow (int voice);
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    // Rec live: writes what the audio thread noted down into the pattern (also run by the timer).
    void flushRecorded();
    // Which of KEYS and GRID show (both, when the window is tall enough).
    bool keysShown() const { return keys.isVisible(); }
    bool gridShown() const { return grid.isVisible(); }
    void setShowKeys (bool keysNotGrid);

private:
    void timerCallback() override;
    // Every change to the pattern goes through here, so Undo covers it. `remember` false for the
    // steps of one drag after the first.
    void editPattern (const std::function<void (crate::drums::Pattern&)>&, bool remember = true);
    void remember (int pattern, int variation);
    void gridClick (int column, int row, bool roll);
    void updatePatternButtons();
    void updateToolButtons();
    void updateBarButtons();
    void updateUndoButtons();
    void updateSelection();
    void showGrooveMenu();
    void showKitMenu();
    void showChainSlotMenu (int slot);
    void showRowMenu (int voice, juce::Rectangle<int> screenArea);
    void mouseDown (const juce::MouseEvent&) override;
    void updateVariationButtons();
    bool playingHere() const;
    void rebuildFace();

    struct Column
    {
        std::array<std::unique_ptr<juce::Slider>, 6> knobs;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 6> attachments;
        std::unique_ptr<juce::TextButton> pad, name;   // the pad is the SELECT key; the name mutes it
        std::unique_ptr<juce::ComboBox> sound;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> soundAttachment;
        int shownHits = 0;
        float flash = 0;
    };

    DrumProcessor& processor;
    class Look;
    std::unique_ptr<Look> look;

    std::array<Column, crate::drums::kVoices> columns;
    juce::OwnedArray<juce::Slider> globalKnobs;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> globalAttachments;
    juce::OwnedArray<juce::TextButton> patternButtons;
    juce::TextButton shorter { "-" }, longer { "+" }, randomButton { "Random" }, clearButton { "Clear" }, copyButton { "Copy to next" },
        grooveButton { "Beats..." }, fillButton { "Fill" }, startButton { "Start" }, sequencerButton { "Pattern on" },
        kitButton { "Kits..." }, copyAB { "A to B" }, chainButton { "Play chain" }, addChain { "Add to chain" }, clearChain { "Clear chain" };
    std::array<juce::TextButton, 3> variationButtons { juce::TextButton ("A"), juce::TextButton ("B"), juce::TextButton ("A+B") };
    // The step tools.
    std::array<juce::TextButton, (size_t) Tool::numTools> toolButtons;
    juce::TextButton rollLess { "-" }, rollMore { "+" }, keysMode { "Keys" }, gridMode { "Grid" }, recButton { "Rec live" },
        undoButton { "Undo" }, redoButton { "Redo" };
    // The bars.
    std::array<juce::TextButton, 4> barButtons { juce::TextButton ("Bar 1"), juce::TextButton ("Bar 2"), juce::TextButton ("Bar 3"), juce::TextButton ("Bar 4") };
    juce::TextButton followButton { "Follow" }, copyBarButton { "Copy bar" }, doubleButton { "Double" };
    juce::ComboBox stepRate, fillPattern, fillEvery, introPattern;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stepRateAttachment, fillAttachment, fillEveryAttachment, introAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sequencerAttachment, chainAttachment;
    std::unique_ptr<juce::Slider> tempo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment;
    StepGrid grid;
    StepKeys keys;
    juce::Random random;

    // The tools' state.
    int selected = 0, rollCount = 3, bar = 0;
    Tool tool = Tool::hit;
    bool follow = false, showKeysWhenShort = true;

    // Undo: the patterns as they were before each change (50 levels).
    struct Snapshot { int pattern = 0, variation = 0; crate::drums::Pattern was; };
    std::vector<Snapshot> undoStack, redoStack;
    // Copy row / Paste row.
    struct RowClip
    {
        bool valid = false;
        std::array<std::uint8_t, crate::drums::kMaxSteps> hits {}, shape {}, chance {}, cond {}, velocity {};
        std::array<std::int8_t, crate::drums::kMaxSteps> nudge {}, pitch {}, decayLock {};
        std::uint8_t rowLength = 0;
    } rowClip;

    juce::Image faceImage;
    juce::Rectangle<int> header, displayArea, voiceArea, columnsArea, toolArea, keysArea, barRow, gridArea, patternRow, optionsRow, chainArea, chainSlots,
        globalArea;
    bool fullVoices = true;
    int shownStep = -2, shownPattern = -1, shownCode = -2, shownChainPos = -2, shownVariation = -1, shownBarNow = -1;
    bool shownFill = false, shownIntro = false;

public:
    // The chain's slots, for the tests: where slot `i` is drawn.
    juce::Rectangle<int> chainSlot (int i) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumView)
};
