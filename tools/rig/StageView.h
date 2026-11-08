#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "RigPanels.h"

// The Stage (F5): the screen for playing a show, on a black panel, readable from across a room.
// Giant Play, Stop and Rec; the tempo big on an amber LCD with -, Tap and +; the part playing as
// a big green letter with its name, the bar in the part, a row of bar lamps and what's next.
// Below, a channel for each track (lamp and word, a tall LED meter, a big Mute), the loop
// layers as round lamps (press one to record, stop or overdub it; right-click to mute it), the
// vocoder's parts as switches, and the master VU meters with a clip lamp. The song parts run
// along the bottom as huge buttons that switch on the next bar.
class StageView : public juce::Component,
                  private juce::Timer
{
public:
    explicit StageView (RigEngine&);
    ~StageView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // Re-reads the tools (after a show is loaded).
    void refresh();

    // Go into a tool: the name of its page ("MPC", "Bass"...). A click on a channel's name.
    std::function<void (const juce::String& tab)> onOpen;
    // A strip's meter, 0..1, as the mixer shows it.
    std::function<float (int strip)> meterOf;
    // The Rig's Rec (record the set), Tap and tempo nudge, and how Rec looks now.
    std::function<void()> onRecord, onTap;
    std::function<void (double)> onNudgeTempo;
    std::function<std::pair<juce::String, vintage::Lit>()> recordState;

    // The channels: Drums, Bass, Synth, Vocoder, Loops.
    static constexpr int kChannels = 5;
    static int channelStrip (int channel);
    static juce::String channelName (int channel);
    static juce::String channelPage (int channel);

    // As a click does (the tests use these).
    void pressMute (int channel);
    void pressLayer (int layer, bool mute);
    void pressVocoderPart (int part);
    juce::Rectangle<int> channelArea (int channel) const { return channelAreas[(size_t) juce::jlimit (0, kChannels - 1, channel)]; }

    rigpanels::PanelButton play { "Play" }, stop { "Stop" }, rec { "Rec" }, minus { juce::String::fromUTF8 ("\xe2\x88\x92") },
        tap { "Tap" }, plus { "+" };
    juce::OwnedArray<rigpanels::PanelButton> parts, mutes;

private:
    void timerCallback() override;
    juce::Rectangle<int> layerRect (int layer) const;
    juce::Rectangle<int> vocoderPartRect (int part) const;

    RigEngine& engine;
    std::array<float, kChannels> levels {};
    float vuLevel = 0, clipHold = 0;
    juce::Rectangle<int> transportArea, tempoArea, tempoLcd, partArea, channelsArea, loopsArea, masterArea, partsLabel;
    std::array<juce::Rectangle<int>, kChannels> channelAreas;
    juce::String shownSignature;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageView)
};
