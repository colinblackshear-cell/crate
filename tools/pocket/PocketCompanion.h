#pragma once

#include "PocketFrame.h"
#include "PocketLoopFx.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// The companion flow (Colin's 2026-11-26 redesign): an idea is a stack of layers, and one big + ADD A LAYER asks, in
// plain words, what to record with. After each take, a sheet offers a vibe, TRY AGAIN, MAKE IT A PAD or KEEP IT.
namespace pocket
{
// + ADD A LAYER: six big tiles (MY VOICE, PADS, DRUMS, SYNTH, VOCODER, RESAMPLE), the rec setup in one line, and the
// deeper tools as chips (CHORDS, DRONE, SONG, EFFECTS).
class AddLayerSheet : public DragSheet
{
public:
    enum Choice { voice, pads, drums, synth, vocoder, resample, kChoices };

    AddLayerSheet();
    ~AddLayerSheet() override;
    std::function<void (int choice)> onPick;
    std::function<void (Tool)> onMoreTool;
    std::function<void()> onRecSetup;
    std::function<juce::String()> setupText;     // "4 bars \xc2\xb7 count in 1 bar"

    int preferredHeight() const { return 560; }
    void refresh() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void pick (int choice) { if (onPick) onPick (choice); }   // (the self-checks)

private:
    class Tile;
    juce::OwnedArray<Tile> tiles;
    PButton change { "Change" }, chords { "Chords" }, drone { "Drone" }, song { "Song" }, effects { "Effects" };
    juce::Rectangle<float> setupCard;
};

// After a take lands: hear it looping, try a vibe, then keep it, try again or make it a pad.
class TakeSheet : public DragSheet
{
public:
    enum Vibe { none, space, echo, radio, underwater, crunch, kVibes };

    TakeSheet();
    ~TakeSheet() override;
    void show (int layer, const juce::String& what);   // "Synth", "My voice"...
    int layer() const { return shownLayer; }
    void setVibe (int v) { vibe = v; refresh(); }
    int currentVibe() const { return vibe; }
    // The layer's effects with a vibe on (the layer's volume kept).
    static LayerFxState withVibe (LayerFxState fx, int vibe);
    static const char* vibeName (int v);

    std::function<void (int vibe)> onVibe;
    std::function<void()> onKeep, onTryAgain, onMakePad, onMoreEffects;

    int preferredHeight() const { return 440; }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    int shownLayer = -1, vibe = none;
    juce::String takeName;
    PButton vibes[kVibes], more { U8("More effects and knobs \xe2\x80\xba"), Look::ghost }, again { "Try again" }, makePad { "Make it a pad" },
            keep { "Keep it", Look::green };
};
}
