#pragma once

#include "IdeasScreen.h"
#include "PocketHold.h"
#include "PocketIdeas.h"
#include "PocketLinkClient.h"
#include "PocketTheme.h"

// The song drawer: a panel about 78% of the width that slides in from the left over a dimmed app. The Kaleidobox
// wordmark, + NEW SONG, YOUR SONGS (tap one to switch to it and stay on the tool you are on; hold one to rename or
// delete it), and at the bottom a 2-column grid: SOUNDS, RECORD A SOUND, MAC LINK, REMOTE, SETTINGS, TOUR · LESSON.
// It opens from the header's menu icon, a tap on the song name, or a swipe from the left edge, and closes with a swipe
// left, a tap on the dim, or by picking something. The component covers the whole app; it is only visible while open.
class SongDrawer : public juce::Component, private juce::Timer
{
public:
    enum Tile { sounds, record, macLink, remote, settings, tourLesson, kTiles };

    SongDrawer (pocket::Ideas&, pocket::LinkClient&);
    ~SongDrawer() override;

    std::function<void()> onNew;
    std::function<void (const juce::String& id)> onPick, onPlay;
    std::function<bool (const juce::String& id)> isPlaying;
    std::function<void (const juce::String& id, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight)> onHoldSong;
    std::function<void (int tile)> onTile;
    std::function<int()> currentLayers;    // the open song's layers (what is on screen, not yet saved)
    std::function<double()> currentBpm;
    std::function<void()> onClosed;

    juce::BorderSize<int> insets;
    bool animated = true;

    void refresh();                         // read the songs again
    void setProgress (float p);             // 0 closed .. 1 open (the finger is dragging it)
    float progress() const { return shown; }
    void animateTo (bool open);
    bool isOpen() const { return goal > 0.5f && isVisible(); }
    int panelWidth() const { return juce::roundToInt ((float) getWidth() * 0.78f); }
    juce::Rectangle<int> tileBounds (int tile) const;   // in the app's coordinates, with the panel open
    juce::Rectangle<int> songRowBounds (int row) const; // the same, for the self-checks
    int songRows() const { return (int) rows.size(); }
    juce::String songIdAt (int row) const { return rows[(size_t) row].id; }
    void holdRow (int row);                 // what a long press on that row does (the self-checks)

    // A swipe left on the panel, for the app's finger and the self-checks.
    void dragStart();
    void dragTo (float dx);                 // dx <= 0: how far the panel has been pulled back
    void dragEnd (float dx, float pxPerMs);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    struct Row { juce::String id, name, meta; bool current = false; };
    class List;
    class Panel;

    void timerCallback() override;
    void layoutPanel();
    void finish();

    pocket::Ideas& ideas;
    pocket::LinkClient& link;
    std::vector<Row> rows;
    std::unique_ptr<Panel> panel;
    std::unique_ptr<juce::MouseListener> swipe;
    float shown = 0.0f, goal = 0.0f;
    bool scrimTap = false;
};
