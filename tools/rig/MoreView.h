#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigPanels.h"

// The More page: everything that isn't one of the five screens, as a panel of big buttons.
// On the left the tools in full (Song builder, MPC, Sampler editor, Bass, Synth, Sounds, Mixer),
// each a module with a picture of what it does; on the right your show (save, open, export for
// Logic) and the set-up (audio and MIDI, click, dark panel, pop a screen out).
//
// It only asks: RigView does the work through the callbacks.
class MoreView : public juce::Component
{
public:
    // `standalone`: the app (it has an audio and MIDI set-up); false in Logic.
    explicit MoreView (bool standalone);
    ~MoreView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override { if (isVisible()) tick(); }

    // The tools, as their pages are named, in the order they show.
    struct ToolInfo
    {
        juce::String page, title, blurb;
        juce::Colour colour;
    };
    static const std::vector<ToolInfo>& toolList();

    std::function<void (const juce::String& page)> onOpen;
    std::function<void()> onSave, onSaveAs, onOpenShow, onExport, onAudio, onClick, onDark, onPopOut;
    // What the lamps show.
    std::function<bool()> isClickOn, isDark;
    std::function<juce::String()> showName;
    // Which pages the Rig has: The Guide's button shows only when it has a "Guide" page.
    std::function<bool (const juce::String& page)> hasPage;

    // Re-reads the lamps (RigView's timer).
    void tick();

    class Tile;
    juce::OwnedArray<juce::Button> tiles;   // the tools' Tiles; public for the tests
    std::unique_ptr<juce::Button> guideTile;   // The Guide (the front page), top right
    static const ToolInfo& guideInfo();

private:
    const bool standalone;
    rigpanels::PanelButton save { "Save show" }, saveAs { "Save as..." }, open { "Open show..." }, exportButton { "Export for Logic" },
                           audio { "Audio + MIDI" }, click { "Click" }, dark { "Dark panel" }, popOut { "Pop out" };
    juce::Rectangle<int> toolsArea, showArea, setupArea, toolsLabel, showLabel, setupLabel, showNameArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MoreView)
};
