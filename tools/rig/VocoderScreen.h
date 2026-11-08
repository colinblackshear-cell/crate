#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Vintage.h"

// The Vocoder screen (F2): your voice through a row of stomp boxes.
//
// Easy shows a pedalboard. On the left, Raw voice: the mic's wave, Mic gain, the input level,
// Monitor (hear your own voice) and Rec voice (sample the mic into the Sampler). Then five
// pedals in a chain, each with one big knob and a footswitch: Robot (Beep-ness), Monster
// (Growl), Vintage mic (Old-timey: which decade's microphone), Arp (Speed) and Chords (Happy to
// sad: sing one note, hear a chord in that mood). On the right, Out: the level, Dry/wet (your
// voice against the robot) and To looper (record it onto the next free loop layer).
//
// Studio shows the vocoder's own full window, every setting.
class VocoderScreen : public juce::Component,
                      private juce::Timer
{
public:
    // Takes `studio` (the vocoder's own window) and shows it in Studio.
    VocoderScreen (RigEngine&, juce::Component* studio, bool standalone);
    ~VocoderScreen() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    void refresh();

    std::function<void (const juce::String&)> onSay;
    std::function<float (int strip)> meterOf;
    // Rec voice: start or stop sampling (RigView's Sample, set to the mic and the Sampler).
    std::function<void()> onRecVoice;
    std::function<bool()> isSampling;
    // The mic switch (standalone).
    std::function<void()> onMic;

    // The pedals, in the chain's order.
    enum PedalId { robot, monster, vintageMic, arp, chords, numPedals };
    static juce::String pedalName (int);

    // As a click does (the tests use these).
    void setStudio (bool);
    bool isStudio() const { return studioShown; }
    void stomp (int pedal);
    bool isOn (int pedal) const;
    void turnKnob (int pedal, double value);   // in the knob's own range
    void pressBypassAll();
    void pressMonitor();
    void pressToLooper();
    void pressNotesFrom();
    void setDryWet (double wet01);
    double getDryWet() const;
    int getLooperLayer() const { return looperLayer; }

    class Pedal;

private:
    void timerCallback() override;
    void updateButtons();
    float param (const char* id) const;
    void setParam (const char* id, float value);

    RigEngine& engine;
    const bool standalone;
    std::unique_ptr<juce::Component> studio;
    bool studioShown = false;

    juce::OwnedArray<Pedal> pedals;
    std::array<bool, numPedals> beforeBypass {};
    bool bypassed = false;
    float monsterGrowl = 7.0f;   // how deep Monster goes when it's on (formant down, in semitones)
    int looperLayer = -1;        // the layer To looper is recording, or -1

    juce::TextButton easyButton { "Easy" }, studioButton { "Studio" }, bypassButton { "Bypass all" }, monitorButton { "Monitor" },
        recVoiceButton { "Rec voice" }, toLooperButton { "To looper" }, notesButton;
    std::unique_ptr<vintage::Knob> micGain;
    juce::Slider dryWet;
    std::vector<float> inputHistory;
    float inputLevel = 0, outLevel = 0;
    juce::String shownSignature;

    juce::Rectangle<int> modeRow, captionArea, notesLabel, rawWell, rawTitle, rawSub, scopeArea, inputLeds, chainLabel, chainArea, outWell,
        outTitle, outLeds, dryWetLabel, arrow1, arrow2, micLamp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocoderScreen)
};
