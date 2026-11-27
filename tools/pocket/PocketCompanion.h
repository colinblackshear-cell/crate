#pragma once

#include "PocketFrame.h"
#include "PocketLoopFx.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// The companion flow (Colin's 2026-11-26 redesign): an idea is a stack of layers, and one big + ADD A LAYER asks, in
// plain words, what to record with. After each take, a sheet offers a vibe, TRY AGAIN, MAKE IT A PAD or KEEP IT.
namespace pocket
{
// + ADD A LAYER: eight equal tiles (MY VOICE, SAMPLER, PADS, DRUMS, BASS, SYNTH, VOCODER, RESAMPLE; Colin: no one is most important), the rec setup in one line, and the
// deeper tools as chips (CHORDS, DRONE, SONG, EFFECTS).
class AddLayerSheet : public DragSheet
{
public:
    enum Choice { voice, sampler, pads, drums, bass, synth, vocoder, resample, kChoices };

    AddLayerSheet();
    ~AddLayerSheet() override;
    std::function<void (int choice)> onPick;
    std::function<void (Tool)> onMoreTool;
    std::function<void()> onRecSetup;
    std::function<juce::String()> setupText;     // "4 bars \xc2\xb7 count in 1 bar"

    int preferredHeight() const { return 680; }
    void refresh() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void pick (int choice) { if (onPick) onPick (choice); }   // (the self-checks)

    // MY VOICE asks how: TO A CLICK (a click in your headphones and a count-in) or FREE (no click: your beat is found
    // after and the take fitted to the song's). onVoice(free).
    std::function<void (bool free)> onVoice;
    std::function<bool()> wearingPhones;
    void showVoicePage (bool shown);
    bool voicePageShown() const { return voicePage; }
    void pickVoice (bool free) { if (onVoice) onVoice (free); }

private:
    class Tile;
    juce::OwnedArray<Tile> tiles;
    std::unique_ptr<Tile> clickTile, freeTile;
    PButton back { U8("\xe2\x80\xb9 Back"), Look::ghost };
    bool voicePage = false;
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

// EFFECTS on a layer, the simple way: pick a vibe, then one finger on the pad runs the whole sound (left-right how much,
// up-down bright or dark). RECORD MY MOVES writes what the finger does into the loop. KNOBS and FULL PEDALBOARD go
// deeper (the loop editor's five knobs; the layer's board of pedals).
class VibeSheet : public DragSheet
{
public:
    VibeSheet();
    ~VibeSheet() override;
    void show (int layer, const juce::String& what, int vibe, juce::Point<float> xy, bool recording);
    int layer() const { return shownLayer; }
    // The layer's effects for a vibe and a place on the pad (x: amount 0..1, y: 0 dark .. 1 bright; 0.5, 0.5 = the vibe as is).
    static LayerFxState fxFor (LayerFxState fx, int vibe, juce::Point<float> xy);

    std::function<void (int vibe, juce::Point<float> xy)> onChange;
    std::function<void (bool down)> onTouch;           // a finger on the pad / lifted (moves are written while it's down)
    std::function<void (bool on)> onRecordMoves;
    std::function<void()> onKnobs, onBoard, onDone;
    void setRecording (bool on);

    int preferredHeight() const { return 600; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void moveDot (juce::Point<float> xy01);   // (the self-checks)

private:
    class Pad;
    void refresh();
    int shownLayer = -1, vibe = 0;
    juce::Point<float> dot { 0.5f, 0.5f };
    bool rec = false;
    juce::String what;
    std::unique_ptr<Pad> pad;
    PButton vibes[TakeSheet::kVibes], recButton { U8("\xe2\x97\x8f Record my moves") }, knobs { U8("Knobs \xe2\x80\xba") },
            board { U8("Full pedalboard \xe2\x80\xba") }, done { "Done", Look::green };
};
}
