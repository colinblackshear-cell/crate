#pragma once

#include "PocketIdeas.h"
#include "PocketTheme.h"

#include <map>

// Home: HOW IT WORKS (pick a tool, press REC, it loops), + NEW SONG (an empty song, opened on LOOPS), SHOW ME: MAKE A
// BEAT (the lesson) and WATCH THE TOUR, then YOUR SONGS: each with its date, tempo, how many layers and the tools on
// them, and a play button for what it has so far. Tap one to carry on; ··· (or hold it) to rename or delete. The
// small sort chip cycles RECENT, A-Z and MOST LAYERS.
class IdeasScreen : public juce::Component
{
public:
    explicit IdeasScreen (pocket::Ideas&);
    ~IdeasScreen() override;

    std::function<void (const juce::String& id)> onOpen;
    std::function<void()> onNew, onNewBeat, onTour;
    std::function<void (const juce::String& id)> onPlay;        // play (or stop) what the song has so far
    std::function<bool (const juce::String& id)> isPlaying;
    std::function<void (const juce::String& id)> onRename, onDelete;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // What the list shows of a song, read from its folder (Loops/loops.json, Beats/drums.json).
    struct Facts { double bpm = 0.0; int layers = 0; std::vector<int> tools; };   // tools: pocket::LayerTool, one per layer
    static Facts factsOf (const juce::File& songFolder);
    juce::Rectangle<int> tourSpot() const { return newButton.getBounds(); }

private:
    struct List;
    std::vector<int> order() const;   // the list in the picked sort
    juce::Rectangle<float> sortChip() const { return { (float) getWidth() - 136.0f, 270.0f, 120.0f, 34.0f }; }
    int sortBy = 0;   // 0 recent, 1 name, 2 most layers
    pocket::Ideas& ideas;
    std::map<juce::String, Facts> facts;
    juce::Viewport view;
    std::unique_ptr<List> list;
    pocket::PButton newButton { "+ New song", pocket::Look::amber }, lessonButton { "Show me: make a beat" }, tourButton { "Watch the tour" };
};
