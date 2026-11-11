#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "RigPanels.h"

// The Song builder (the Song page, under More; also the footer's "edit parts" and the Looper's
// Song builder button). A song is a row of parts, left to right (the same parts as A-H along the
// bottom). Each column is a part; each row a track:
//
//   - "Save as new part" keeps what's playing now (which tracks and loops play, the MPC's
//     sequence, the bass pattern, the arp) as the next column.
//   - Click a cell to switch that track on or off in that part (right-click: leave it as it is).
//   - A part's length is how many bars it plays before the next one starts, when the song plays
//     in order. Hold stays until you move on.
//   - Play song starts at part A and plays the parts in order; press any part (or keys 1-8) to
//     jump to it at the next bar.
//
// The strip above the grid is the whole song to scale, with where it is now.
class SongView : public juce::Component,
                 private juce::Timer
{
public:
    explicit SongView (RigEngine&);
    ~SongView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Rebuilds the grid (after a show is loaded, or parts were added elsewhere).
    void refresh();

    std::function<void (const juce::String&)> onSay;
    // Export for Logic (RigView's, so it's the same everywhere). Without it, exports straight
    // to the default folder.
    std::function<void()> onExport;

    // A row of the grid: a tool's mixer strip, or a loop layer.
    struct Track
    {
        bool isLoop;
        int index;   // RigEngine::StripId, or the layer
    };
    static const std::vector<Track>& tracks();
    static juce::String trackName (const Track&);
    // A cell: 1 plays, 0 muted, -1 leaves it as it is.
    static int cellValue (const RigScene&, const Track&);
    static void setCell (RigScene&, const Track&, int value);
    // "8 bars", "1 bar", "Hold".
    static juce::String lengthText (int bars);
    // The part's letter: A-Z, then numbers.
    static juce::String letterFor (int part);

    // What the buttons do (public for the tests).
    void addPart();
    void playSong();
    // Click: leave -> plays -> muted -> plays... The part that's playing changes straight away.
    void clickCell (int part, int row);
    void setCellTo (int part, int row, int value);

private:
    class Grid;
    void timerCallback() override;
    void showKeyboardMenu();
    void say (const juce::String& s) { if (onSay) onSay (s); }
    void paintStrip (juce::Graphics&);
    void paintSteps (juce::Graphics&, juce::Rectangle<int>);
    int partAtStrip (juce::Point<int>) const;
    void mouseUp (const juce::MouseEvent&) override;

    RigEngine& engine;
    rigpanels::PanelButton addButton { "+ New part" }, playButton { "Play song" }, inOrder { "In order" }, back { "Back" },
                           next { "Next" }, keys { "Keys" }, exportButton { "Export" },
                           bigAdd { "+ Save what's playing as part A" };
    std::unique_ptr<Grid> grid;
    juce::Viewport viewport;
    juce::Rectangle<int> titleArea, nowArea, stripArea, stepsArea;
    int knownParts = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongView)
};
