#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "core/ui/Theme.h"

// The Song tab: the scenes (parts) of the song, in order. Add a scene to keep what's playing
// now as a part; press a scene to have it start at the next bar. In song mode, a scene with a
// length moves on to the next one by itself.
class SongView : public juce::Component,
                 private juce::Timer
{
public:
    explicit SongView (RigEngine&);
    ~SongView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Rebuilds the rows (after a show is loaded).
    void refresh();

    std::function<void (const juce::String&)> onSay;

private:
    class Row;
    void timerCallback() override;
    void addScene();
    void showKeyboardMenu();
    void say (const juce::String& s) { if (onSay) onSay (s); }
    juce::String describe (const RigScene::State&) const;

    RigEngine& engine;
    juce::TextButton addButton { "+ Add scene" }, songMode { "Song mode" }, back { "< Back" }, next { "Next >" },
                     keyboard { "Keyboard..." };
    juce::Label help;
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SongView)
};
