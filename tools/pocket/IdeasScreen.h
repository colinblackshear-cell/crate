#pragma once

#include "PocketIdeas.h"
#include "PocketTheme.h"

// The Record tab's home, "Your songs", like Voice Memos: every idea (a song in the making), sorted by
// recent, name or how far along it is, with how far along the seven steps it is, what comes next and a
// play button for what it has so far. Tap one to carry on; hold it to rename or delete. Three ways to
// start a new one: record a sound, make a beat (a walk-through, nothing pre-made), or a factory kit.
class IdeasScreen : public juce::Component
{
public:
    explicit IdeasScreen (pocket::Ideas&);
    ~IdeasScreen() override;

    std::function<void (const juce::String& id)> onOpen;
    std::function<void()> onNew, onNewBeat, onNewKit;
    std::function<void (const juce::String& id)> onPlay;        // play (or stop) what the song has so far
    std::function<bool (const juce::String& id)> isPlaying;
    std::function<void (const juce::String& id)> onRename, onDelete;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseUp (const juce::MouseEvent&) override;

private:
    struct List;
    std::vector<int> order() const;   // the list in the picked sort
    juce::Rectangle<float> chip (int i) const;
    juce::Rectangle<float> beatRect() const;
    juce::Rectangle<float> kitRect() const;
    int sortBy = 0;   // 0 recent, 1 name, 2 furthest along
    pocket::Ideas& ideas;
    juce::Viewport view;
    std::unique_ptr<List> list;
    pocket::PButton newButton;
    juce::Rectangle<int> newBounds() const;
};
