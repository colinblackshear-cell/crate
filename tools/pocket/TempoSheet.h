#pragma once

#include "PocketTheme.h"

// The Tempo sheet (tap the BPM in the header): one tempo for the whole song. -/+, drag the bar,
// TAP TEMPO, HALF, DOUBLE, the click and the count-in.
class TempoSheet : public juce::Component
{
public:
    TempoSheet();

    std::function<double()> getBpm;
    std::function<void (double)> setBpm;
    std::function<bool()> getClick, getCountIn;
    std::function<void (bool)> setClick, setCountIn;
    std::function<void()> onDone;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void change (double bpm);
    void tap();
    juce::Rectangle<float> barRect() const { return { 28.0f, 168.0f, (float) getWidth() - 56.0f, 12.0f }; }

    pocket::PButton minus { "-" }, plus { "+" }, tapButton { "Tap tempo", pocket::Look::amber }, half { "Half" }, twice { "Double" },
        clickButton, countButton, done { "Done" };
    std::vector<double> taps;
};
