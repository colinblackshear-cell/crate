#pragma once

#include "PocketIdeas.h"
#include "PocketTheme.h"

// The Record tab's home, like Voice Memos: every idea, newest first, how far along the seven steps it
// is and what comes next. Tap one to carry on; hold it to rename or delete; NEW IDEA starts recording.
class IdeasScreen : public juce::Component
{
public:
    explicit IdeasScreen (pocket::Ideas&);
    ~IdeasScreen() override;

    std::function<void (const juce::String& id)> onOpen;
    std::function<void()> onNew;
    std::function<void (const juce::String& id)> onRename, onDelete;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct List;
    pocket::Ideas& ideas;
    juce::Viewport view;
    std::unique_ptr<List> list;
    pocket::PButton newButton;
    juce::Rectangle<int> newBounds() const;
};
