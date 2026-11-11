#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DrumProcessor.h"

// The Drum Machine's panel, styled like an early-80s analog rhythm box: a charcoal face with
// cream lettering, a column of knobs for each of the 12 sounds (level, tune, decay, tone, pan)
// with a pad to hit it, and below them the step grid with its red, orange, yellow and cream
// step numbers. The bar along the middle has the 16 patterns and what they do (length, fill,
// swing, accent...).
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

    struct Column
    {
        std::array<std::unique_ptr<juce::Slider>, 5> knobs;
        std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 5> attachments;
        std::unique_ptr<juce::TextButton> pad;
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
        grooveButton { "Beats..." }, fillButton { "Fill" }, startButton { "Start" }, sequencerButton { "Pattern on" };
    juce::ComboBox stepRate, fillPattern, fillEvery;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stepRateAttachment, fillAttachment, fillEveryAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> sequencerAttachment;
    std::unique_ptr<juce::Slider> tempo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment;
    StepGrid grid;
    juce::Random random;

    juce::Rectangle<int> header, voiceArea, barArea, gridArea, globalArea;
    int shownStep = -2, shownPattern = -1;
    bool shownFill = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumView)
};
