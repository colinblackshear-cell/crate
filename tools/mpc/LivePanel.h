#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MpcProcessor.h"

// Playing live: songs (sequences chained with repeats), what plays next and when it switches,
// tap tempo, and a row of track mutes and solos you can hit while it plays.

class SongEditor : public juce::Component
{
public:
    SongEditor (MpcProcessor&, std::function<void()> changed);

    void rebuild();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Row
    {
        juce::Label number;
        juce::ComboBox sequence;
        juce::Slider repeats;
        juce::TextButton up { "Up" }, remove { "X" };
    };

    void edit (std::function<void (seq::Song&)>);

    MpcProcessor& processor;
    Project& project;
    std::function<void()> changed;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Label title, total, empty;
    juce::TextButton add { "Add a step" };
    juce::ToggleButton loop { "Loop the song" };
    juce::Component rowHolder;
    juce::Viewport viewport;
};

//==============================================================================
class LivePanel : public juce::Component,
                  private juce::ChangeListener,
                  private juce::Timer
{
public:
    explicit LivePanel (MpcProcessor&);
    ~LivePanel() override;

    void refresh();
    void tap();
    std::function<void (const juce::String&, bool warning)> onStatus;

    static constexpr int kTrackButtons = 16;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
    void timerCallback() override;
    void songMenu();
    void editSong();
    void trackClicked (int track, bool solo);
    void say (const juce::String& t, bool warning = false) { if (onStatus) onStatus (t, warning); }

    MpcProcessor& processor;
    Project& project;

    juce::Label songLabel { {}, "Song" }, songStatus, nextLabel, tracksLabel { {}, "Tracks" };
    juce::ComboBox song, switchAt;
    juce::TextButton editSteps { "Steps..." }, playSong { "Play song" }, tapButton { "Tap" }, allOn { "All on" };
    std::array<juce::TextButton, kTrackButtons> tracks;
    std::vector<double> taps;
    juce::String lastStatus;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LivePanel)
};
