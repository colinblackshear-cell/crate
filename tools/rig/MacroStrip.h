#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RigEngine.h"
#include "RigPanels.h"

// The show's eight macro knobs, under the FX pad: each turns many settings at once.
//
//   The knob      turns every setting on it, from where it started to where it ends up.
//   Assign        then move settings (any knob on any tool or its effects) from start to end:
//                 each joins the macro. Press Done when it has them all.
//   Right-click   rename it, see what's on it (click one to take it off), clear it.
class MacroStrip : public juce::Component,
                   private juce::Timer
{
public:
    explicit MacroStrip (RigEngine&);
    ~MacroStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // As the controls do (the tests use these).
    void pressAssign (int macro);
    void turn (int macro, float value01);
    void showMenu (int macro);
    juce::Slider& knob (int macro) { return *knobs[macro]; }
    void update();   // shows the macros as they are now

    std::function<void (const juce::String&)> onSay;

private:
    void timerCallback() override;
    void say (const juce::String&);

    RigEngine& engine;
    juce::OwnedArray<juce::Slider> knobs;
    juce::OwnedArray<rigpanels::PanelButton> assign;
    std::vector<juce::Rectangle<int>> nameAreas, countAreas;
    juce::Rectangle<int> titleArea;
    int shownVersion = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MacroStrip)
};
