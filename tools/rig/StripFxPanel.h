#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "StripFx.h"
#include "core/ui/ParamControls.h"

// One rack of effects (StripFx), in a row: knobs for the filter, drive, crush, delay and
// reverb, and the beat repeat's buttons. The mixer strips use it, and so does each loop.
class StripFxPanel : public juce::Component
{
public:
    StripFxPanel (StripFx&, const juce::String& title, juce::Colour);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick();

    juce::TextButton resetButton { "Reset" }, closeButton { "Close" }, pluginButton, pluginOpen { "Open" };
    std::function<void (const juce::String&)> onSay;

private:
    void setRepeat (int choice);

    StripFx& fx;
    const juce::String title;
    const juce::Colour colour;
    std::vector<std::unique_ptr<crate::ParamKnob>> knobs;
    std::unique_ptr<crate::ParamChoice> delayTime;
    juce::OwnedArray<juce::TextButton> repeatButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripFxPanel)
};
