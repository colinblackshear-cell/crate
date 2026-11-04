#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "core/ui/Theme.h"

// The Stage: every tool on one screen, live, for playing a show. The song's parts run along the
// top (what's playing, what's next, and a button for each part). Below, a tile for each tool
// shows what it's doing right now: the MPC's sequence and where it is in it, the bass line and
// the arp stepping along, the looper's layers going round, the vocoder and the input, each with
// its level and a mute. The buttons on a tile play it (pick a sequence or the MPC's song, a bass
// pattern, the arp, a loop layer); click anywhere else on a tile to go into that tool.
//
// Everything runs on the Rig's one clock, so whatever you start here comes in on the beat.
class StageView : public juce::Component,
                  private juce::Timer
{
public:
    explicit StageView (RigEngine&);
    ~StageView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Re-reads the tools (after a show is loaded).
    void refresh();

    // Go into a tool: the name of its tab ("MPC", "Bass"...).
    std::function<void (const juce::String& tab)> onOpen;
    // A strip's meter, 0..1, as the mixer shows it.
    std::function<float (int strip)> meterOf;
    // The Rig's Sample button.
    std::function<void()> onSample;
    std::function<bool()> isSampling;

    class Tile;

private:
    class SongTile;
    class MpcTile;
    class BassTile;
    class SynthTile;
    class SwitchTile;
    class LooperTile;
    class SamplerTile;

    void timerCallback() override;

    RigEngine& engine;
    std::unique_ptr<SongTile> songTile;
    juce::OwnedArray<Tile> tiles;   // MPC, Bass, Synth / Looper, Vocoder, Input, Sampler

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageView)
};
