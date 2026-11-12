#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DrumProcessor.h"

// The Drum Machine's panel, styled like an early-80s analog rhythm box: a charcoal face with
// cream lettering, a column for each of the 12 sounds (its name, which mutes it; its choice of
// sound; level, tune, decay, tone, snappy and pan knobs; a pad to hit it), and below them the
// step grid with its red, orange, yellow and cream step numbers. The bar along the middle has
// the 16 patterns, their A and B variations and what they do (length, fill, kits...), and the
// row under it the intro and the chain.
//
// In the grid, click a step to put a hit there (drag to paint several). Right-click (or
// Option-click) a hit to make it a roll of 2, 3 or 4 hits. The top row is the accent.
class DrumView : public juce::AudioProcessorEditor,
                 private juce::Timer
{
public:
    explicit DrumView (DrumProcessor&);
    ~DrumView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The step grid, as its own component so the tests can click it.
    class StepGrid : public juce::Component,
                     public juce::SettableTooltipClient
    {
    public:
        explicit StepGrid (DrumProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

        static constexpr int kRows = crate::drums::kVoices + 1;   // the accent row, then the sounds
        int columns() const;
        juce::Rectangle<float> cell (int column, int row) const;
        // Row 0 is the accent; rows 1-12 the sounds.
        std::pair<int, int> cellAt (juce::Point<float>) const;
        void click (int column, int row, bool roll);

        std::function<void (int voice)> onAudition;

    private:
        DrumProcessor& processor;
        int dragRow = -1;
        int dragValue = 0;
        int lastColumn = -1;
    };

    StepGrid& getGrid() { return grid; }

private:
    void timerCallback() override;
    void editPattern (const std::function<void (crate::drums::Pattern&)>&);
    void updatePatternButtons();
    void showGrooveMenu();
    void showKitMenu();
    void showChainSlotMenu (int slot);
    void mouseDown (const juce::MouseEvent&) override;
    void updateVariationButtons();

    struct Column
    {
        std::array<std::unique_ptr<juce::Slider>, 6> knobs;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 6> attachments;
        std::unique_ptr<juce::TextButton> pad, name;   // the name mutes it
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
    juce::ComboBox stepRate, fillPattern, fillEvery, introPattern;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stepRateAttachment, fillAttachment, fillEveryAttachment, introAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sequencerAttachment, chainAttachment;
    std::unique_ptr<juce::Slider> tempo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment;
    StepGrid grid;
    juce::Random random;

    juce::Rectangle<int> header, voiceArea, barArea, chainArea, chainSlots, gridArea, globalArea;
    int shownStep = -2, shownPattern = -1, shownCode = -2, shownChainPos = -2, shownVariation = -1;
    bool shownFill = false, shownIntro = false;

public:
    // The chain's slots, for the tests: where slot `i` is drawn.
    juce::Rectangle<int> chainSlot (int i) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumView)
};
