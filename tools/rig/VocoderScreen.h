#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Vintage.h"

// The Vocoder screen (F2): your voice through a pedalboard, with every setting a click away.
//
// Along the top, the sound: step through the vocoder's sounds (or pick one from the list, or
// Surprise me), save your own, send the keys to the vocoder, Bypass all.
//
// Then the pedalboard: thirteen pedals in the order the sound goes through them. Tune, Harmony,
// Robot, Character, Synth, Chords, Arp, Filter, Vintage, Dirt, Chorus, Echo and Space. Each has a
// lamp, one knob for the thing you'll want most, and a footswitch. Click a pedal to open it in
// the rack below: every one of its settings as a knob, what it does in plain words, Reset. So it
// starts simple (stomp, turn one knob) and goes as deep as you like (open the rack).
//
// Beside the rack: Raw voice (the mic's wave, Mic gain, Monitor, Rec voice) and Out (the level,
// Dry/wet, To looper). Full window shows the vocoder's own window.
class VocoderScreen : public juce::Component,
                      private juce::Timer
{
public:
    // Takes `studio` (the vocoder's own window) and shows it in Full window.
    VocoderScreen (RigEngine&, juce::Component* studio, bool standalone);
    ~VocoderScreen() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh();

    std::function<void (const juce::String&)> onSay;
    std::function<float (int strip)> meterOf;
    // The mic's own level, as it comes in (before the Mic switch).
    std::function<float()> micLevel;
    // Rec voice: start or stop sampling (RigView's Sample, set to the mic and the Sampler).
    std::function<void()> onRecVoice;
    std::function<bool()> isSampling;
    // The mic switch (standalone).
    std::function<void()> onMic;

    // The pedals, in the chain's order.
    enum PedalId { tune, harmony, robot, character, synth, chords, arp, filter, vintageMic, dirt, chorus, echo, space, numPedals };
    static juce::String pedalName (int);

    // As a click does (the tests use these).
    void setStudio (bool);
    bool isStudio() const { return studioShown; }
    void stomp (int pedal);
    bool isOn (int pedal) const;
    void turnKnob (int pedal, double value);   // the pedal's own knob, in its setting's range
    void selectPedal (int pedal);               // opens it in the rack
    int getSelectedPedal() const { return selected; }
    int numRackKnobs (int pedal) const;
    void resetPedal (int pedal);                // its settings back to how they start
    void pressChord (int degree);               // 0-6: the chords of the key; again to stop
    int getActiveChord() const { return activeChord; }
    void pickSound (int index);                 // one of the vocoder's sounds
    void stepSound (int delta);
    juce::String soundName() const { return shownSound; }
    void pressBypassAll();
    void pressMonitor();
    void pressToLooper();
    void pressNotesFrom();
    void setDryWet (double wet01);
    double getDryWet() const;
    int getLooperLayer() const { return looperLayer; }

    // Your own sounds, saved as files in Music/Crate/Vocoder Sounds.
    static juce::File userSoundsFolder();
    bool saveSound (const juce::String& name);
    bool loadSound (const juce::File&);

    class Pedal;

private:
    void timerCallback() override;
    void updateButtons();
    void layoutRack();
    void showSoundMenu();
    void askToSave();
    juce::Array<int> chordNotes (int degree) const;
    float param (const char* id) const;
    void setParam (const char* id, float value);

    RigEngine& engine;
    const bool standalone;
    std::unique_ptr<juce::Component> studio;
    bool studioShown = false;

    juce::OwnedArray<Pedal> pedals;
    std::array<std::vector<std::unique_ptr<vintage::Knob>>, numPedals> rackKnobs;
    std::array<bool, numPedals> beforeBypass {};
    bool bypassed = false;
    int selected = robot, activeChord = -1;

    // What a pedal comes back as when it's switched on again.
    float keptFormant = -7.0f, keptDrive = 0.35f, keptCrush = 0.0f, keptChorus = 0.4f, keptEcho = 0.3f, keptSpace = 0.3f, keptCutoff = 1500.0f,
          keptFilterMode = 1.0f, keptHarmony = 3.0f;
    int looperLayer = -1;   // the layer To looper is recording, or -1
    juce::String shownSound { "Raw Voice" };

    juce::TextButton boardButton { "Pedalboard" }, studioButton { "Full window" }, bypassButton { "Bypass all" }, monitorButton { "Monitor" },
        recVoiceButton { "Rec voice" }, toLooperButton { "To looper" }, notesButton, prevSound { "<" }, nextSound { ">" },
        soundsButton { "Sounds" }, surpriseButton { "Surprise me" }, rackPower { "On" }, rackReset { "Reset" };
    juce::TextButton chordButtons[7];
    std::unique_ptr<vintage::Knob> micGain;
    juce::Slider dryWet;
    std::vector<float> inputHistory;
    float inputLevel = 0, outLevel = 0;
    juce::String shownSignature, chordSignature;

    juce::Rectangle<int> modeRow, soundLcd, boardArea, rawWell, rawTitle, rawSub, scopeArea, inputLeds, rackWell, rackTitle, rackText,
        rackKnobArea, outWell, outTitle, outLeds, dryWetLabel, micLamp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocoderScreen)
};
