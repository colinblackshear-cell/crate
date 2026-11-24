#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"

// The song composer: a bar ruler over the click, a lane for each loop and tool, and blocks on
// the lanes for when each one plays.
//
//   Click a lane        adds a block there (a loop's own length, or 4 bars); drag to draw it longer
//   Drag a block        moves it; its right edge makes it longer (a loop repeats), its left edge
//                       starts it later; the small squares at its top corners fade it in and out
//   Alt-drag a block    copies it; Cmd-drag slides the loop inside it (slip); Shift-drag up or down
//                       sets its level
//   Right-click a block fades, split, level, copy, delete (S splits at the playhead)
//   Click the ruler     where Play starts (while it plays: jumps there at the next bar)
//   Drag on the ruler   cycles those bars
//   The marker row      section names (Verse, Chorus...); click one to go there
//   Effect moves        under the tracks, a row for each knob move recorded (Rec moves): drag on
//                       one to draw its line; right-click for Loop/Once, on/off, clear
//
// Delete removes the picked block, Cmd+D copies it after itself, Cmd+Z undoes.
// Rec onto song: a loop you record while the song plays goes on its lane where you played it.
// Record song plays it once and records every tool as its own WAV, for Logic.
class TimelineView : public juce::Component
{
public:
    void select (int block);   // picks a block (-1: none)
    explicit TimelineView (RigEngine&);
    ~TimelineView() override;

    void tick();      // ~30 Hz
    void refresh();   // the show changed

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onSay;
    // Opens the Automation panel (the effect moves' full editor).
    std::function<void()> onOpenAutomation;

    // What the buttons and clicks do (the tests use them too).
    int addBlock (int lane, double bar);   // returns the new block's index
    void play();
    void stop();
    void fromParts();     // replaces the timeline with the Song grid's parts
    void recordSong();
    // Bounce's real-time pass: plays the song from `fromBar` and records every tool into `folder` (as Record song
    // does), stopping by itself at `untilBeat` (the end bar plus the tail). False (and why) when it can't start.
    bool recordSongRange (const juce::File& folder, double fromBar, double untilBeat, juce::String* why = nullptr);
    bool isRecordingSong() const { return recordingSong; }
    void exportForLogic();
    void deleteSelected();
    void duplicateSelected();
    void undo();
    void setFade (int block, bool in, double bars);
    // Splits the picked block at `bar` (the second half carries on where the loop was).
    bool splitSelected (double bar);
    void setLevel (int block, float level);
    // A loop block's slip: how far into the loop (bars) it starts.
    void slip (int block, double offset);
    double loopBars (int lane) const;
    static double wrapBars (double x, double length);
    void addMarker (double bar, const juce::String& name);
    int getSelected() const { return selected; }
    double getStartBar() const { return startBar; }
    void setStartBar (double bar);
    void setCycle (double from, double to);
    bool recordOntoSong = false;

private:
    class Lanes;
    class Names;
    void say (const juce::String& s) { if (onSay) onSay (s); }
    void pushUndo();
    void edited();   // the blocks changed: tells the engine, redraws
    void updateButtons();
    void watchRecording();
    double defaultLength (int lane) const;
    int addBlockNoUndo (int lane, double bar);

    RigEngine& engine;
    RigTimeline& timeline;
    std::unique_ptr<Lanes> lanes;
    std::unique_ptr<Names> names;
    juce::Viewport viewport;
    juce::Label title, steps;
    juce::TextButton playButton { "Play" }, stopButton { "Stop" }, cycleButton { "Cycle" }, clickButton { "Click" },
        snapButton { "Snap: bar" }, zoomOut { "-" }, zoomIn { "+" }, onButton { "Timeline on" }, recOnto { "Rec onto song" },
        partsButton { "From parts..." }, recordButton { "Record song" }, exportButton { "Export for Logic" }, close { "Close" };

    int selected = -1;
    double startBar = 0;
    bool snapBeats = false;
    float pxPerBar = 30.0f;
    std::vector<std::pair<std::vector<RigTimeline::Block>, std::vector<RigTimeline::Marker>>> undoStack;

    // Rec onto song.
    std::array<int, RigLooper::kLayers> lastState {};
    std::array<double, RigLooper::kLayers> recordStart {};
    // Record song: waits to see the clock play, then stops recording when it stops.
    bool recordingSong = false, seenPlaying = false;
    double recordUntilBeat = -1;   // a bounce stops here
    bool stopAtEndBefore = true;
    bool wasPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TimelineView)
};
