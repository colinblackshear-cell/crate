#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The Rig's Guide (its button is always at the top right): a walkthrough of the whole tool, one
// stop at a time on the real screens, and short how-tos, step by step. The how-tos open on the
// screen you're on, and the list on the left has the jobs that cross screens (record a loop,
// build a song, save and export, play live and sing, plug in a guitar).
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

    // The walkthrough: the whole of Kaleidobox, one stop at a time, each on its real screen.
    // `place` is where the stop shows: "Play:Drums", "Edit:Vocoder", "Song", "Mix", "Sounds",
    // "Chords", "Settings" (a view, and the track to pick on the rail), or "" to stay.
    struct Stop
    {
        juce::String title, text, place;
    };
    static const std::vector<Stop>& tour();
    // Walkthrough mode: one stop, with Back and Next, small enough to see the screen under it.
    void startTour (int stop = 0);
    bool isTour() const { return touring; }
    int getStop() const { return stop; }
    void showStop (int);
    // A stop's place, to go to (the window does it); and the panel changing size (tour or list).
    std::function<void (const juce::String& place)> onPlace;
    std::function<void()> onModeChanged;
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
    juce::TextButton tourButton { juce::String::fromUTF8 ("\xe2\x96\xb6 Walkthrough: the whole tool") };
    juce::TextButton back { juce::String::fromUTF8 ("\xe2\x80\xb9 Back") }, next { juce::String::fromUTF8 ("Next \xe2\x80\xba") },
        allHowTos { "All how-tos" };
    juce::TextEditor body;
    int shown = 0, stop = 0;
    bool touring = false;
    void setTouring (bool);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HelpPanel)
};
