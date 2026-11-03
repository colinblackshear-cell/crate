#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "SynthProcessor.h"
#include "core/ui/ParamControls.h"
#include "core/ui/Theme.h"

// The Synth window: oscillator, filter, envelope and LFO knobs, the arpeggiator with its
// 16-step rhythm (click a step to switch it on or off, the lower row for accents), and a keyboard.
class SynthView : public juce::AudioProcessorEditor,
                  private juce::Timer
{
public:
    explicit SynthView (SynthProcessor&);
    ~SynthView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    class RhythmRow : public juce::Component
    {
    public:
        explicit RhythmRow (SynthProcessor& p) : processor (p) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        void click (juce::Point<float>, bool drag);
        juce::Rectangle<float> cell (int step, int row) const;
        SynthProcessor& processor;
        bool dragValue = true;
        int dragRow = 0, lastStep = -1;
    };

private:
    void timerCallback() override;

    SynthProcessor& processor;
    crate::Theme::LookAndFeel lookAndFeel;

    crate::ParamChoice wave, lfoRate, lfoShape, arpMode, arpRate;
    crate::ParamSwitch arpOn, latch;
    juce::OwnedArray<crate::ParamKnob> soundKnobs, arpKnobs;
    juce::TextButton shorter { "-" }, longer { "+" };
    juce::Label lengthLabel;
    RhythmRow rhythm;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthView)
};
