#pragma once

#include "RigBounce.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "RigPanels.h"
#include "RigSong.h"

class TimelineView;
class FriendTrackStrip;
class FriendTrackSheet;

// The Song screen (F3). Your song is a row of sections (Intro, Verse, Chorus...) left to right,
// each as wide as its bars, over a bar ruler and the click. Down the side are the tracks you're
// using; each box is what that track plays in that section, by name ("Beat 1", "Pattern 2",
// "Guitar"). An empty box is silent.
//
//   + Section          adds one at the end: empty, or what's playing now
//   Click a box        the panel on the right picks what it plays, how it fades in and out,
//                      which bar it starts on and how loud it is
//   Double-click one   its track opens in Edit on the pattern it plays (Bass pattern 3), to
//                      change it; "Open in Play" opens it on the Play view instead
//   Click a section    its name, length, loop bank; copy, move or delete it.
//                      Drag its right edge for more or fewer bars
//   Click the ruler    where Play starts
//   Jam / Song         what the big Play at the top plays: whatever you've switched on, or this
//                      song from the start
//   Bars view          the same song as lanes of blocks, for fine edits (a loop that comes in on
//                      bar 3, a fade across two sections)
//
// The keys along the bottom (A-H, keys 1-8) are the sections: press one to go there at the
// next bar. Rec records the song as stems for Logic; Export saves the loops laid out on it.
class SongView : public juce::Component,
                 private juce::Timer
{
public:
    explicit SongView (RigEngine&);
    ~SongView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Re-reads the song (after a show is loaded, or it changed elsewhere).
    void refresh();

    std::function<void (const juce::String&)> onSay;
    // Export for Logic (RigView's, so it's the same everywhere).
    std::function<void()> onExport;
    // More > Import a song from Logic... (the window asks about saving, then picks the folder).
    std::function<void()> onImportFromLogic;
    // "Edit" on a box: the screen where that track's patterns are made.
    std::function<void (const juce::String& page)> onGoTo;
    // Double-click on a box (or its Edit / Open in Play): its track, opened on the pattern the box
    // plays (clip, -1 for "as it is"), in Edit (true) or Play.
    std::function<void (int lane, int clip, bool edit)> onOpenBox;
    // Add a layer with (a button per instrument, under the song): that instrument opens, ready to
    // record into the picked section (Rec into), with the song playing round it. `track` is a
    // GrooveView::Track.
    std::function<void (int track)> onAddLayer;
    juce::OwnedArray<rigpanels::PanelButton> layerButtons;
    static const std::array<int, 7>& layerTracks();
    void openBox (int section, int lane, bool edit);

    //==========================================================================
    // What the buttons and clicks do (public for the tests).
    int addEmptySection();
    int addSectionFromPlaying();
    void setSongMode (bool song);
    void playFromStart();
    void loopSection (int section);
    void showBars (bool);
    bool isShowingBars() const { return barsShown; }
    TimelineView* getBarsView() const { return bars.get(); }

    // A box (section, lane), or a whole section (lane -1). -1, -1: nothing.
    void select (int section, int lane);
    int getSelectedSection() const { return selSection; }
    int getSelectedLane() const { return selLane; }
    // For the picked box: what it plays (kSilent empties it), its fades (bars), the bar of the
    // section it comes in on (1 = the first), and its level.
    static constexpr int kSilent = -100;
    void pickClip (int clip);
    void setFadeIn (double bars);
    void setFadeOut (double bars);
    void setStartBar (int bar);
    void setLevel (float);
    // For the picked section.
    void setSectionLength (double bars);
    void setSectionBank (int bank);

    // Deleting, in one click, each with Undo (Cmd+Z):
    //   the x on a section's name     deletes the section (its boxes go, what comes after moves up)
    //   the x on a box                empties it: that track is quiet in that section
    //   the x on a track's name       takes the track out of the whole song (every box on its row)
    // The Delete button (and the Delete key) does the picked one: a picked box is emptied, a
    // picked section deleted. With nothing picked the button offers a menu of the sections.
    bool deleteSection (int section);
    bool emptyBox (int section, int lane);
    bool removeLaneFromSong (int lane);
    void pressDelete();
    // Undo (the button and Cmd+Z): takes back the last change to the song, made here, in Bars
    // view, or by To Song from any screen. False when there's nothing to take back.
    bool undo();
    bool canUndo() const { return ! undoStack.empty(); }
    // Starts the undo afresh (a show loaded, a new song): nothing to take back into it.
    void forgetUndo();
    // A track picked on the rail: its row lights up on the picked section (or the one playing,
    // or the first). Says so when the track isn't in the song yet.
    void showLane (int lane);

    // Rec live (the song, played): starts a new take, or with one going, stops and keeps it.
    // Punch: plays it again over one section only. The pads bring tracks in and out.
    void pressRecLive();
    bool recLiveFrom (int section);   // punch over that section
    void pressLivePad (int lane);
    void showTakesMenu();
    bool isLivePanelShown() const;

    // Bounce + stems (the Bounce button's panel). startBounce writes the loops at once; with Everything it plays
    // the song once and recordingSaved() (RigView calls it when the recorder has saved a take) finishes it.
    void showBouncePanel();
    bool startBounce (const RigBounce::Options&);
    bool bounceRunning() const { return bouncing; }
    bool recordingSaved();   // true when the take was the bounce's (then it said so itself)
    const juce::File& lastBounceFolder() const { return bounceFolder; }
    std::function<juce::String()> getShowName;
    bool revealAfterBounce = true;   // opens the folder in the Finder (the tests turn it off)

    // A friend's track (its row along the top): pick a file and bring it in, the sheet for one that's
    // in (tempo, bar 1, key), and Send it back (the Bounce panel set for your parts only).
    void bringInTrack();
    void bringInTrackFile (const juce::File&);
    void changeTrack();
    void showSendBack();
    FriendTrackStrip* getTrackStrip() const { return trackStrip.get(); }

    // The lanes the board shows, top to bottom.
    std::vector<int> shownLanes() const;
    bool showingAllLanes = false;

    static juce::Colour laneColour (int lane);

private:
    class Board;
    class Names;
    class Inspector;
    class LivePads;
    RigBounce::Options bounceOptions;
    RigBounce::Options bounceRunningOptions;
    bool bouncing = false;
    juce::File bounceFolder;
    juce::StringArray bounceFiles;
    void timerCallback() override;
    void mouseUp (const juce::MouseEvent&) override;
    void say (const juce::String& s) { if (onSay) onSay (s); }
    void deleted (const juce::String& what);   // says so, and lights Undo for a few seconds
    void updateDeleteButton();
    void edited();   // the song changed: redraw everything
    void showAddMenu();
    void showMoreMenu();
    void showSectionMenu (int section);
    void showBoxMenu (int section, int lane);
    void renameSection (int section);
    juce::String pageForLane (int lane) const;
    RigTimeline& timeline() const { return engine.getTimeline(); }

    RigEngine& engine;
    rigpanels::PanelButton addButton { "+ Section" }, jamButton { "Jam" }, songButton { "Song" }, playButton { "From start" },
        loopButton { "Loop section" }, barsButton { "Bars view" }, recButton { "Rec song" }, exportButton { "Bounce" },
        moreButton { juce::String::fromUTF8 ("\xe2\x8b\xaf") }, deleteButton { "Delete" }, undoButton { "Undo" }, bigAdd { "+ First section: what's playing now" },
        bigEmpty { "+ Empty section" }, liveButton { "Rec live" }, takesButton { "Takes" };
    std::unique_ptr<Board> board;
    std::unique_ptr<Names> names;
    std::unique_ptr<Inspector> inspector;
    std::unique_ptr<LivePads> livePads;
    bool wasLive = false;
    int lastTakes = -1, lastTake = -2, keptTake = -2;
    std::unique_ptr<TimelineView> bars;
    std::unique_ptr<FriendTrackStrip> trackStrip;
    std::shared_ptr<juce::FileChooser> trackChooser;
    void showTrackSheet (std::unique_ptr<FriendTrackSheet>);
    juce::Viewport viewport;
    juce::Rectangle<int> titleArea, stepsArea, boardArea, moreLanesArea, emptyArea, addLayerArea;
    int selSection = -1, selLane = -1;
    bool barsShown = false, recordingSong = false, seenPlaying = false;
    int lastHash = 0;
    int undoGlow = 0;   // timer ticks left with Undo lit, after a delete
    // The song as it was before each change (blocks and sections), newest last.
    using Snapshot = std::pair<std::vector<RigTimeline::Block>, std::vector<RigTimeline::Marker>>;
    std::vector<Snapshot> undoStack;
    Snapshot kept;   // the song as it is (the next change pushes this)
    bool sameAsKept() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongView)
};
