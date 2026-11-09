#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The Rig's "?" guide: short how-tos, step by step. It opens on the screen you're on, and the
// list on the left has the jobs that cross screens (record a loop, build a song, save and
// export, play live and sing, plug in a guitar).
class HelpPanel : public juce::Component
{
public:
    explicit HelpPanel (const juce::String& page);

    struct Guide
    {
        juce::String title;
        juce::StringArray steps;
        juce::String note;
    };
    static const std::vector<Guide>& guides();
    // The guide for a screen's page name ("Looper", "Groove"...), or the first one.
    static int guideFor (const juce::String& page);

    void show (int guide);
    int getShown() const { return shown; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void()> onClose;

private:
    juce::OwnedArray<juce::TextButton> list;
    juce::TextButton close { "Close" };
    juce::TextEditor body;
    int shown = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPanel)
};
