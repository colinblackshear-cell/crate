#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "RigPanels.h"
#include "PerformPad.h"

// The Mixer (F5): a mixing desk for everything Kaleidobox plays, and the way into every tool.
//
// Along the top, a button for each instrument and tool (Groove, MPC, Drum machine, Bass,
// Synth, Vocoder, Sampler, Looper, Song, Sounds and Setup); each lamp lights
// while that tool is sounding. Below, a channel strip per tool, as on a real desk, top to
// bottom: its name (click it to go into the tool), its on switch (the vocoder's, the Input's
// with its jack), FX (the strip's effects rack), two sends (A to the shared Reverb, B to the
// shared Echo, after the fader), Pan, Mute and Solo, a tall fader with an LED meter, the level
// in dB, and Learn (put a fader of your keyboard on it). Then the Reverb and Echo returns
// (their level, size or feedback, tone or time), and the Master.
//
// "Loop layers" swaps the tool strips for the looper's eight layers: each with its lamp, Rec
// (record / stop / overdub, on the bar), FX, Mute and its own fader, into the Loops strip.
// "FX pad" swaps them for the performance pad on the whole mix (filter sweeps, repeat, tape
// stop, gate, riser), with the returns and the master beside it (it was on the old Big stage).
class MixerView : public juce::Component,
                  private juce::Timer
{
public:
    explicit MixerView (RigEngine&);
    ~MixerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Re-reads every strip (after a show is loaded).
    void refresh();

    // Go into a tool (the name of its page: "MPC", "Looper", "Setup"...).
    std::function<void (const juce::String& page)> onOpen;
    // A strip's FX button: show (or hide) that strip's effects. A layer's: its loop effects.
    std::function<void (int strip)> onFx;
    std::function<void (int layer)> onLayerFx;
    // A strip's Learn button: put a keyboard fader on `control` (a RigRouting control).
    std::function<void (int control, juce::Button&)> onLearn;
    // A strip's meter, 0..1, falling slowly (the window keeps them).
    std::function<float (int strip)> meterOf;
    std::function<void (const juce::String&)> onSay;

    // An effects rack (or anything) shown along the bottom of the desk, over the strips' feet;
    // null takes it away. The caller owns it.
    void setPanel (juce::Component*);
    juce::Component* getPanel() const { return panel.getComponent(); }

    // The tools' strips, left to right, then the returns and the master.
    static const std::vector<int>& toolStrips();
    static int faderControl (int strip);   // its Learn control (RigRouting), or -1
    static juce::String pageFor (int strip);
    // The hub's buttons, as their pages are named.
    static const juce::StringArray& hubPages();
    // The row of buttons into every tool along the top (off when the window has a track rail).
    void showHub (bool);

    // Loop layers in place of the tools' strips.
    void showLayers (bool);
    bool isShowingLayers() const { return layersShown; }
    // The FX pad in place of the strips (the returns and master stay).
    void showPad (bool);
    bool isShowingPad() const { return padShown; }
    PerformPad& getPad() { return pad; }

    class Channel;
    // A strip by its id (RigEngine::StripId), a return (`returnId`), or a layer; null if not shown.
    Channel* channelFor (int strip) const;
    Channel* returnChannel (int returnId) const;
    Channel* layerChannel (int layer) const;
    // The Learn button of every strip that has one, with its control.
    std::vector<std::pair<juce::Button*, int>> learnButtons() const;

    juce::OwnedArray<rigpanels::PanelButton> hub;
    bool hubShown = true;
    rigpanels::PanelButton toolsBank { "Tools" }, layersBank { "Loop layers" }, clearSolo { "Clear solo" }, padBank { "FX pad" };

private:
    void timerCallback() override;
    void layoutStrips (juce::Rectangle<int>);

    RigEngine& engine;
    std::unique_ptr<juce::LookAndFeel> look;
    juce::OwnedArray<Channel> tools, layers, returnsAndMaster;
    juce::Component::SafePointer<juce::Component> panel;
    bool layersShown = false, padShown = false;
    PerformPad pad;
    juce::Rectangle<int> hubArea, deskArea, legendArea;
    juce::String shownHub;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerView)
};

//==============================================================================
// One strip of the desk.
class MixerView::Channel : public juce::Component
{
public:
    enum class Kind { tool, layer, reverb, echo, master };
    // `index`: the strip (RigEngine::StripId) for a tool or the master, the layer, or unused.
    Channel (MixerView&, Kind, int index);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh();   // the controls from the engine (not while you hold one)
    void tick();      // the meter and the lamps

    juce::String title() const;
    juce::Colour colour() const;
    // What it's doing, in a word ("Playing", "Muted", "Off"...), and its lamp.
    std::pair<juce::String, vintage::Lit> status() const;
    float getMeter() const { return meter; }

    const Kind kind;
    const int index;

    juce::Slider fader { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    // Tools: send A (reverb), send B (echo), pan. Returns: amount (size / feedback), tone.
    juce::Slider sendA, sendB, pan, amount, tone;
    juce::TextButton mute { "M" }, solo { "S" }, fx { "FX" }, learn { "Learn" }, on { "On" }, rec { "Rec" };
    juce::ComboBox jack, time;

private:
    RigEngine& engine() const;
    juce::Rectangle<int> nameArea, statusArea, knobRowA, knobRowB, meterArea, dbArea, faderArea;
    MixerView& desk;
    float meter = 0;
    juce::String shown;
};
