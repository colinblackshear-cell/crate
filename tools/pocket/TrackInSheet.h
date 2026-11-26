#pragma once

#include "PocketSheet.h"
#include "PocketTheme.h"
#include "PocketKey.h"
#include "RigFriendTrack.h"
#include "core/Audio.h"
#include "core/TrackAnalysis.h"

#include <atomic>

// SONG > BRING IN A FRIEND'S TRACK: the phone's version of the Mac's "Bring in a track" sheet (FriendTrackSheet).
// The song as a waveform with its bar 1 line (drag it onto the first big beat, or nudge it), its tempo (heard,
// from the file's name, -/+, TAP, HALF, DOUBLE), LISTEN + CLICK to check they agree, the key it hears, and whether
// to set the song's tempo and key and mark its sections. While it's open the track is tried in the looper (LISTEN
// plays it); CANCEL puts everything back. The app does the saving.
class TrackInSheet : public pocket::DragSheet,
                     private juce::Timer
{
public:
    TrackInSheet();
    ~TrackInSheet() override;

    // The app's
    RigFriendTrack* track = nullptr;                  // the looper's
    std::function<double()> getBpm, clockBeat;
    std::function<void (double)> setBpm;
    std::function<void (bool)> listen;                // the clock from bar 1 with the click, or stopped
    std::function<bool()> clockRunning;
    std::function<void (pocket::SongKey)> setKey;
    std::function<bool()> songHasSections;
    std::function<void (const std::vector<crate::TrackAnalysis::Section>&)> markSections;
    std::function<void (crate::SharedAudio, const RigFriendTrack::Info&, bool isNew)> keep;   // saved with the song
    std::function<void (const juce::String&)> onDone;   // brought in: what to say (the app closes the sheet)
    std::function<void()> onCancel;

    // A new track from `file` (read and listened to in the background, or now: the self-checks).
    void openFile (const juce::File&, bool inBackground = true);
    // The one that's in: its tempo, bar 1 and key.
    void openCurrent();
    // Puts the looper and the tempo back as they were (CANCEL, or the sheet dragged away).
    void cancel();

    // What the buttons do (the self-checks use these too).
    bool isReady() const { return audio != nullptr; }
    bool isLoading() const { return loading; }
    juce::String getProblem() const { return problem; }
    const RigFriendTrack::Info& getInfo() const { return info; }
    double heardBpm() const { return heard.bpm; }
    void setTempo (double bpm);
    void tap();
    void setFirstBeat (double seconds);
    void nudge (double seconds) { setFirstBeat (info.firstBeat + seconds); }
    void nudgeBeats (int beats) { setFirstBeat (info.firstBeat + beats * 60.0 / info.bpm); }
    void findBarOne();
    void toggleListen();
    bool isListening() const { return listening; }
    juce::String bringIn();
    bool useTempo = true, useKey = true, useSections = true;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

    static constexpr int kHeight = 640;

private:
    void timerCallback() override;
    void loaded (crate::SharedAudio, crate::TrackAnalysis::Result, juce::String error, int generation);
    void begin();
    void tryIt();
    void update();
    void showKeyMenu();
    juce::Range<double> closeUp() const;   // seconds shown in the close-up: a bar either side of bar 1

    crate::SharedAudio audio;
    RigFriendTrack::Info info;
    crate::TrackAnalysis::Result heard;
    std::vector<float> peaks;
    juce::String problem;
    std::vector<double> taps;
    bool loading = false, listening = false, committed = true, isNew = false, tried = false;
    int generation = 0;
    double dragFrom = 0;

    // as the looper was before
    crate::SharedAudio beforeAudio;
    RigFriendTrack::Info beforeInfo;
    double beforeBpm = 120;

    juce::Rectangle<float> infoR, overR, closeR, bpmR;
    pocket::PButton minus { "-" }, plus { "+" }, tapButton { "TAP" }, halfButton { "HALF" }, doubleButton { "DOUBLE" }, heardButton { "HEARD" },
        beatLeft { U8("\xe2\x97\x80 BEAT") }, msLeft { U8("\xe2\x97\x80 10 MS") }, msRight { U8("10 MS \xe2\x96\xb6") }, beatRight { U8("BEAT \xe2\x96\xb6") },
        findButton { "FIND BAR 1" }, listenButton { U8("\xe2\x96\xb6 LISTEN + CLICK"), pocket::Look::green }, keyButton { "KEY" },
        tempoToggle { "SET TEMPO" }, keyToggle { "SET KEY" }, sectionsToggle { "MARK SECTIONS" },
        cancelButton { "CANCEL" }, bringButton { "BRING IT IN", pocket::Look::amber };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackInSheet)
};
