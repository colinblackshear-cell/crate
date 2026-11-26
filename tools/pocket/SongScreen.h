#pragma once

#include "PocketFrame.h"
#include "PocketLooper.h"
#include "PocketSong.h"
#include "PocketTheme.h"

// The SONG tool: your loop layers laid out as a song, the phone's version of the Mac's Song screen.
//
//   REC LIVE      after a count-in, tap the layers in and out (the big tiles, or their M in LOOPS): each change
//                 lands on the next bar and is written down as you play. MARK starts a new section; STOP + KEEP
//                 keeps the run as a new take (nothing is recorded over); CANCEL throws it away.
//   PLAY SONG     plays it from the playhead (tap the ruler to move it), and stops at its end.
//   The lanes     one per layer. Tap an empty spot to put the layer's loop there; tap a block to pick it, drag it
//                 to move it, drag its ends to trim it (the left end keeps the sound where it is). The chips under
//                 the lanes work on the picked block: SPLIT at the playhead, COPY, DELETE, SLIP (start further into
//                 its loop), FADE IN, FADE OUT and LEVEL.
//   Sections      tap a section on the ruler to pick it: PUNCH plays from 2 bars before and records those bars again.
//   TAKES         every run of REC LIVE, to switch between; the count-in; REC LIVE from the end.
//   TRACK         a friend's song to play over (it plays from bar 1 under the layers): bring one in from Files,
//                 its tempo, bar 1 and key, M, PRACTICE (a section round and round) and SEND BACK (your parts,
//                 lined up with his file, or everything as an MP3 to listen to).
// The thumb bar's REC is REC LIVE here (PocketApp drives it).
class SongScreen : public juce::Component, private juce::Timer
{
public:
    SongScreen (pocket::PocketSong&, pocket::LoopsEngine&);
    ~SongScreen() override;

    std::function<void (const juce::String&)> toast;
    std::function<void()> onChanged;          // something to save

    // BOUNCE: what, how it's split, the format. The app writes the files (and shares them).
    struct Bounce
    {
        enum What { song, part, loops } what = song;   // part: one section
        int section = -1;
        bool eachLayer = true, wav = true, mp3 = true;
        double tailSeconds = 2.0;
        bool withTrack = true;    // a friend's track in the mix (off: only your parts, lined up with his file)
        bool sendBack = false;    // SEND BACK: named for the track, with a note on lining it up
    };
    Bounce bounceSettings;
    std::function<void (const Bounce&)> onBounce;
    std::function<void()> onBounceToMac;
    void showBounceMenu();
    void showSendBackMenu();
    // SEND BACK's three: 0 my parts each (+ mixed), 1 my parts mixed, 2 everything as an MP3 to listen to.
    Bounce sendBackPreset (int which) const;

    // A friend's track (the looper's): bring one in, change it, take it out, put it back. The app does these.
    std::function<void()> onBringInTrack, onChangeTrack, onTakeOutTrack, onPutBackTrack;
    std::function<bool()> canPutBackTrack;
    void showTrackMenu();
    void showPracticeMenu();
    // What the buttons do (also for the thumb bar and the self-checks).
    void pressRecLive();
    void pressPlay();
    double playhead() const { return playFrom; }
    void setPlayhead (double bar);
    int selectedBlock() const { return selected; }
    void selectBlock (int b) { selected = b; layout(); repaint(); }
    int selectedSection() const { return section; }
    void selectSection (int s) { section = s; layout(); repaint(); }
    void refresh() { layout(); repaint(); }

    enum Id
    {
        none = 0, recId = 1, playId, takesId, markId, cancelId, keepId,
        splitId, copyId, deleteId, slipId, fadeInId, fadeOutId, levelId, punchId, endId, zoomOutId, zoomInId, bounceId,
        trackId, trackMuteId, practiceId, sendBackId,
        tileBase = 40, laneArea = 60, rulerArea = 61
    };
    void press (int id);
    juce::Rectangle<float> hitRect (int id) const;
    juce::Rectangle<int> tourSpot() const { return wellR.toNearestInt(); }

    void paint (juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (isVisible()) layout(); }

private:
    struct Hit { juce::Rectangle<float> r; int id; };
    enum class Drag { none, pan, move, trimStart, trimEnd };

    void timerCallback() override;
    void layout();
    int idAt (juce::Point<float>) const;
    void changed();
    void showTakesMenu();
    juce::String statusText() const;
    double barAtX (float x) const;
    float xOfBar (double bar) const;
    int laneAtY (float y) const;
    juce::Rectangle<float> blockRect (const pocket::SongBlock&) const;
    int blockAtPoint (juce::Point<float>) const;
    void paintLanes (juce::Graphics&);
    void paintTiles (juce::Graphics&);
    bool hasLoop (int layer) const;
    bool hasTrack() const { return loops.track().hasAudio(); }
    void paintTrackChip (juce::Graphics&, juce::Rectangle<float>, bool down);
    void say (const juce::String& s) { if (toast) toast (s); }

    pocket::PocketSong& song;
    pocket::LoopsEngine& loops;
    std::vector<Hit> hits;
    juce::Rectangle<float> wellR, statusR, rulerR, lanesR, labelsR;
    std::array<juce::Rectangle<float>, pocket::kLoopLayers> tileR;
    int pressed = 0, selected = -1, section = -1;
    double playFrom = 0, scroll = 0;
    float barW = 22.0f;
    int shownEdits = -1;
    bool shownLive = false, shownTrack = false;

    Drag drag = Drag::none;
    juce::Point<float> downAt;
    bool moved = false;
    pocket::SongBlock dragOrig;
    double scrollOrig = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongScreen)
};
