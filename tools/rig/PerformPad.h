#pragma once

#include "PerformFx.h"
#include "RigPanels.h"

// The pad itself: the mode buttons, the square you touch, Hold.
class PerformPad : public juce::Component,
                   private juce::Timer
{
public:
    explicit PerformPad (PerformFx&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // As a touch does (the tests).
    void touch (float x01, float y01);
    void release();
    void setMode (int);
    void setHold (bool);

private:
    void timerCallback() override;
    void moveTo (juce::Point<int>);

    PerformFx& fx;
    bool hold = false, touching = false;
    juce::OwnedArray<rigpanels::PanelButton> modeButtons;
    rigpanels::PanelButton holdButton { "Hold" };
    juce::Rectangle<int> padArea, captionArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PerformPad)
};
