#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"

// The song laid out as a grid: your loops (and the tools) down the side, the song's parts
// across the top. A lit cell plays in that part, a dark one is muted, and a dash leaves it as
// it was. Click a cell to switch it. Each part has a length in bars; in song mode a part plays
// its bars and the next one starts by itself. A loop that comes in with a part starts from its
// top on the part's first beat. Clicking a part's name plays it from the next bar; + Part keeps
// what's playing now as a new part.
//
// The parts are the same as the Song tab's scenes and the Main screen's A-H.
class SongGrid : public juce::Component
{
public:
    explicit SongGrid (RigEngine&);
    ~SongGrid() override;

    void tick();     // ~30 Hz: which part plays, which is next
    void refresh();  // the parts changed (show loaded, part added elsewhere)

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onSay;

    // A row of the grid: a loop layer (0-7) or a tool's mixer strip.
    struct RowInfo
    {
        bool isLoop;
        int index;   // layer, or RigEngine::StripId
    };
    static const std::vector<RowInfo>& rowList();
    // A cell's value in a part: 1 plays, 0 muted, -1 leaves it.
    static int cellValue (const RigScene&, const RowInfo&);
    static void setCell (RigScene&, const RowInfo&, int value);
    // What clicking does: leave -> plays -> muted -> plays...
    static int nextValue (int value) { return value == 1 ? 0 : 1; }

    // For the tests: clicks a cell.
    void clickCell (int part, int row);
    void addPart();
    void playSong();
    void exportForLogic();

private:
    class Cells;
    void say (const juce::String& s) { if (onSay) onSay (s); }

    RigEngine& engine;
    std::unique_ptr<Cells> cells;
    juce::Viewport viewport;
    juce::Label title, help;
    juce::TextButton addButton { "+ Part" }, songMode { "Song mode" }, playButton { "Play song" }, exportButton { "Export for Logic" }, close { "Close" };
    juce::Label steps;
    int knownParts = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongGrid)
};
