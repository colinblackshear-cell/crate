#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RigEngine.h"
#include "RigPanels.h"
#include "core/TrackAnalysis.h"

// A friend's track on the Mac (RigFriendTrack.h): the sheet that brings one in, and its row at the
// top of the Song screen. The phone has the same in its SONG tool (PocketApp::bringInTrack).

//==============================================================================
// Bring in a track: the song's waveform with its bar 1 line (drag it onto the first big beat), its
// tempo (typed, heard, tapped, half or double), Listen with click to check they agree, the key it
// hears (for the Chords), and whether to set Kaleidobox's tempo and mark the song's sections.
// While it's open the track is tried in the Rig (Listen plays it); Cancel puts everything back.
class FriendTrackSheet : public juce::Component,
                         private juce::Timer
{
public:
    // A new track from `file` (read and listened to in the background, or now: the tests).
    FriendTrackSheet (RigEngine&, const juce::File& file, bool inBackground = true);
    // The track that's in (to change its tempo, bar 1 or key).
    explicit FriendTrackSheet (RigEngine&);
    ~FriendTrackSheet() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    // Says what happened when it's brought in (the sheet then closes).
    std::function<void (const juce::String&)> onDone;
    std::function<void()> onClose;

    // What the buttons do (the tests use these too).
    bool isReady() const { return audio != nullptr; }
    const RigFriendTrack::Info& getInfo() const { return info; }
    double getHeardBpm() const { return heard.bpm; }
    void setBpm (double);
    void tap (double timeSeconds);
    void halfTempo() { setBpm (info.bpm / 2.0); }
    void doubleTempo() { setBpm (info.bpm * 2.0); }
    void setFirstBeat (double seconds);
    void nudge (double seconds) { setFirstBeat (info.firstBeat + seconds); }
    void nudgeBeats (int beats) { setFirstBeat (info.firstBeat + beats * 60.0 / info.bpm); }
    void findBarOne();
    void setKey (int key, bool minor);
    void toggleListen();
    bool isListening() const { return listening; }
    bool setTempo = true, setKeyOnChords = true, markSections = true;
    // Brings it in: plays from bar 1 in the song. Returns what to say.
    juce::String bringIn();
    juce::String getProblem() const { return problem; }

    static constexpr int kWidth = 920, kHeight = 600;

private:
    void timerCallback() override;
    void loaded (crate::SharedAudio, crate::TrackAnalysis::Result, std::vector<float> peaks, const juce::String& error);
    void build();
    void update();
    void tryIt();        // the track as set now, in the Rig
    void restore();      // as it was before the sheet opened
    double xToSeconds (juce::Rectangle<int>, double fromSeconds, double toSeconds, int x) const;
    juce::Range<double> closeUpRange() const;

    RigEngine& engine;
    juce::File file;
    crate::SharedAudio audio;
    RigFriendTrack::Info info;
    crate::TrackAnalysis::Result heard;
    std::vector<float> peaks;   // the whole song, one per column of the overview
    juce::String problem;
    std::atomic<bool> ready { false };
    std::vector<double> taps;
    bool listening = false, committed = false, wasChange = false;
    int dragging = 0;   // 1: the overview, 2: the close-up

    // As the Rig was before.
    crate::SharedAudio beforeAudio;
    RigFriendTrack::Info beforeInfo;
    double beforeBpm = 120;
    int beforeBeatsPerBar = 4;
    bool beforeMetronome = false, beforeTimeline = false;

    juce::TextEditor bpmBox;
    juce::TextButton tapButton { "Tap" }, heardButton { "Use heard" }, halfButton { "Half" }, doubleButton { "Double" },
        nudgeLeft { "< 10 ms" }, nudgeRight { "10 ms >" }, beatLeft { "< 1 beat" }, beatRight { "1 beat >" }, findButton { "Find bar 1" },
        listenButton { "Listen with click" }, keyButton { "Change key" }, cancelButton { "Cancel" }, bringButton { "Bring it in" };
    juce::ComboBox beatsBox;
    juce::ToggleButton tempoToggle, keyToggle, sectionsToggle;
    juce::Rectangle<int> titleArea, infoArea, overviewArea, closeUpArea, tempoArea, barArea, keyArea, statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FriendTrackSheet)
};

//==============================================================================
// The friend's track's row at the top of the Song screen: its name, tempo and key, the whole song as
// a waveform with where the clock is (click it to start from there), and Mute, its level, Practice
// loop, Change, Send it back and Take it out. With none in, one button: Bring in a track (or drop a
// file on it).
class FriendTrackStrip : public juce::Component,
                         public juce::FileDragAndDropTarget
{
public:
    explicit FriendTrackStrip (RigEngine&);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void tick();      // the Song screen's timer
    int preferredHeight() const;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;

    std::function<void()> onBringIn;                          // pick a file
    std::function<void (const juce::File&)> onBringInFile;    // a file dropped on it
    std::function<void()> onChange;                           // the sheet, on the track that's in
    std::function<void()> onSendBack;
    std::function<void (const juce::String&)> onSay;
    std::function<void()> onLayout;                           // its height changed

    // What the buttons do (public for the tests).
    void toggleMute();
    void showPracticeMenu();
    void takeOut();
    void putBack();

private:
    void update();
    juce::Rectangle<int> waveArea() const;

    RigEngine& engine;
    rigpanels::PanelButton bringButton { "+ Bring in a track" }, putBackButton { "Put it back" }, muteButton { "M" },
        practiceButton { "Practice loop" }, changeButton { "Tempo, bar 1, key" }, sendButton { "Send it back" }, removeButton { "x" };
    juce::Slider level;
    juce::String peaksId;
    std::vector<float> peaks;
    crate::SharedAudio removedAudio;
    RigFriendTrack::Info removedInfo;
    bool shownWithTrack = false;
    double lastBar = -1;
    bool dropHover = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FriendTrackStrip)
};

namespace friendtrack
{
// The whole song as `columns` peaks (0..1).
std::vector<float> peaksOf (const juce::AudioBuffer<float>&, int columns);
// Draws `peaks` over `area` (from..to as fractions of the song).
void drawPeaks (juce::Graphics&, const std::vector<float>& peaks, juce::Rectangle<float> area, juce::Colour, double from = 0, double to = 1);
}
