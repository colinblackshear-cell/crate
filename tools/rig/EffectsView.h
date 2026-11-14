#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Pedalboard.h"
#include "Vintage.h"

// The Effects screen (F6): one effects processor for every sound in the Rig, as a pedalboard.
//
// Down the left, what goes through the pedals: the Line in (a guitar, bass or keytar plugged
// into your interface), the Vocoder (after its own pedals), the Bass, Synth, MPC, Drum machine,
// Sampler, each loop on its own, all the loops, and the Master. Each one has its own board, so
// the bass can have a dub delay while loop 3 has a phaser and the guitar a crunchy amp.
//
// Along the top: the sound's name, ready-made settings (Clean guitar, Funk wah, Fat bass amp...)
// and your own, Save, Copy and Paste between sounds, Bypass, Reset, and your own plugin last.
//
// Then thirteen pedals in the order the sound goes through them (Gate, Comp, Wah, Filter, Dirt,
// Amp, EQ, Chorus, Phaser, Tremolo, Delay, Reverb, Repeat). Stomp the footswitch, turn the knob;
// click a pedal to open all its settings in the rack below.
//
// Beside the rack: the source. For the Line in that's Plug in, which input, Gain in, a tuner and
// the level; for every source, Rec to looper, Rec to sampler and Rec to MPC pad (what you
// record has the pedals on it), and a button to open the tool itself.
class EffectsView : public juce::Component,
                    private juce::Timer
{
public:
    explicit EffectsView (RigEngine&);
    ~EffectsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (const juce::String&)> onSay;
    std::function<void (const juce::String&)> onOpen;   // a page, by name ("Vocoder", "Bass"...)
    std::function<void()> onSample;                     // start or stop sampling (RigView's Sample)
    std::function<bool()> isSampling;
    std::function<float (int strip)> meterOf;

    // What can go through the pedals.
    struct Source
    {
        const char* name;
        const char* kind;
        int strip;           // a mixer strip's rack, or -1
        int loop;            // a loop layer's rack, or -1
        const char* page;    // the tool's own page (empty: none)
        int looperSource;    // what the looper records it as (-1: it can't)
        int captureSource;   // what sampling records it as (-1: it can't)
    };
    static const std::vector<Source>& sources();
    enum { lineIn, vocoderSource, bassSource, synthSource, mpcSource, drumsSource, samplerSource, firstLoop,
           allLoops = firstLoop + RigLooper::kLayers, masterSource, numSources };

    void showSource (int);
    int getSource() const { return current; }
    void showStrip (int strip);      // the source with that mixer strip's rack
    void showLoop (int layer);
    StripFx& rack();
    StripFx& rackOf (int source);

    // As a click does (the tests use these).
    void stomp (int pedal);
    bool isOn (int pedal);
    void turnKnob (int pedal, double value);
    void selectPedal (int pedal);
    int getSelectedPedal() const { return selected; }
    int numRackKnobs() const { return (int) rackKnobs.size(); }
    void pickPreset (int index);    // one of pedalboard::presets()
    void pressBypass();
    void pressCopy();
    void pressPaste();
    void pressResetAll();
    void pressPlugIn();             // the Line in on or off
    void pressRecToLooper();
    void pressRecTo (int dest);     // RigCapture::toSampler or toMpcPad
    int getLooperLayer() const { return looperLayer; }
    juce::String getTunerText() const { return tunerText; }

    class Stomp;

private:
    void timerCallback() override;
    void rebuildRack();
    void attachPedals();
    void updateButtons();
    void showPresetMenu();
    void askToSave();
    void updateTuner();
    void say (const juce::String&);

    RigEngine& engine;
    int current = lineIn, selected = pedalboard::amp;
    juce::OwnedArray<Stomp> stomps;
    std::vector<std::unique_ptr<vintage::Knob>> rackKnobs;
    std::unique_ptr<vintage::Knob> gainIn;
    std::vector<std::array<float, pedalboard::numPedals * 2>> kept;
    std::vector<int> beforeBypass;
    juce::ValueTree clipboard;
    juce::String clipboardFrom, presetShown;
    int looperLayer = -1;

    juce::OwnedArray<juce::TextButton> sourceButtons;
    juce::TextButton prevSource { "<" }, nextSource { ">" }, presetsButton { "Presets" }, saveButton { "Save" }, copyButton { "Copy" },
        pasteButton { "Paste" }, bypassButton { "Bypass" }, resetButton { "Reset all" }, pluginButton, pluginOpen { "Open" },
        rackPower { "On" }, rackReset { "Reset" }, plugInButton { "Plug in" }, toLooperButton { "To looper" },
        toSamplerButton { "To sampler" }, toPadButton { "To MPC pad" }, toLogicButton { "To Logic" }, openTool;
    // The last recording for Logic, to drag out onto a Logic track.
    std::unique_ptr<juce::Component> dragToLogic;
    juce::ComboBox jack;

    // The tuner.
    std::vector<float> tunerBuffer;
    juce::String tunerText;
    float tunerCents = 0;
    bool tunerHasNote = false;
    float outLevel = 0, inLevel = 0;
    juce::String shownSignature;

    juce::Rectangle<int> topRow, sourceLcd, column, toolsLabel, loopsLabel, masterLabel, boardArea, sideWell, sideTitle, sideSub,
        tunerArea, inLeds, outLeds, rackWell, rackTitle, rackText, rackKnobArea, plugLamp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsView)
};
