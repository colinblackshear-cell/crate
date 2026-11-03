#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "BassProcessor.h"
#include "core/ui/ParamControls.h"
#include "core/ui/Theme.h"

// The Bass window: 8 patterns, a step grid (click a note in a column to put it on that step;
// accent and slide rows below), the sound's knobs, and a keyboard.
class BassView : public juce::AudioProcessorEditor,
                 private juce::Timer
{
public:
    explicit BassView (BassProcessor&);
    ~BassView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The step grid, as its own component so the tests can click it.
    class StepGrid : public juce::Component
    {
    public:
        explicit StepGrid (BassProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        static constexpr int kNoteRows = 12;   // one octave from the root; the octave row moves it
        static constexpr int kRows = kNoteRows + 3;   // + octave, accent, slide

    private:
        juce::Rectangle<float> cell (int column, int row) const;
        void click (juce::Point<float>, bool drag);
        BassProcessor& processor;
        int dragRow = -1;
        bool dragValue = false;
        int lastColumn = -1;
    };

private:
    void timerCallback() override;
    void editPattern (const std::function<void (crate::bass::Pattern&)>&);

    BassProcessor& processor;
    crate::Theme::LookAndFeel lookAndFeel;

    juce::OwnedArray<juce::TextButton> patternButtons;
    crate::ParamSwitch sequencer, keysTranspose;
    juce::TextButton shorter { "-" }, longer { "+" }, randomButton { "Random" }, clearButton { "Clear" }, copyButton { "Copy to next" };
    juce::Label lengthLabel;
    crate::ParamChoice stepRate, wave, lfoRate, lfoShape;
    juce::OwnedArray<crate::ParamKnob> knobs;
    StepGrid grid;
    juce::MidiKeyboardComponent keyboard;
    juce::Random random;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassView)
};
