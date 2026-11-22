#pragma once

#include "PocketDrone.h"
#include "PocketFrame.h"
#include "PocketTheme.h"

// The DRONE tool, as the Mac's Drone screen laid out for the thumb: ON, its ready-made SOUND and its VOLUME
// up top, the four strings (tap one to pluck it), the note (the song's KEY, or one of your own), the octave and
// the first string, when it plucks, its knobs, and SING INTO THE STRINGS (how much they ring to your voice or a
// tool, what they ring to, and whether the mic is on). The thumb bar's REC records the drone onto the next loop
// layer (PocketApp drives it).
class DroneScreen : public juce::Component, private juce::Timer
{
public:
    explicit DroneScreen (pocket::DroneTool&);
    ~DroneScreen() override;

    std::function<void (const juce::String&)> toast;
    std::function<void()> onChanged;                 // something to save
    std::function<bool()> micOn;                     // the app's mic
    std::function<void()> askMic;                    // turns it on (asks first, as the vocoder does)
    std::function<int()> songRoot;                   // the song's KEY
    std::function<juce::String()> songKeyName;       // "A MINOR"

    void toolChanged() { layout(); repaint(); }
    juce::Rectangle<int> tourSpot() const { return stringsWell.toNearestInt(); }

    // what a tap on each part does (also for the self-checks)
    enum Id
    {
        none = 0, onId = 1, soundId, keyId, noteId, octBase = 10, firstBase = 20, syncBase = 30, knobBase = 40, feedId = 60, micId,
        stringBase = 70
    };
    enum Knob { paceKnob, jawariKnob, ringKnob, reedsKnob, volumeKnob, resonateKnob, numKnobs };
    void press (int id);
    juce::Rectangle<float> hitRect (int id) const;
    bool micWarningShown() const;
    void setKnob (int knob, float value01);
    float knobValue (int knob) const;
    void tickForDemo() { timerCallback(); }   // the strings' movement, as the timer does it (screenshots, checks)

    void paint (juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (isVisible()) layout(); }

private:
    struct Hit { juce::Rectangle<float> r; int id; };

    void timerCallback() override;
    void layout();
    int idAt (juce::Point<float>) const;
    void changed();
    void showSoundMenu();
    void showNoteMenu();
    void showFeedMenu();
    void drawKnobAt (juce::Graphics&, int knob);
    juce::String knobText (int knob) const;
    juce::String statusText() const;

    pocket::DroneTool& drone;
    std::vector<Hit> hits;
    juce::Rectangle<float> titleR, stringsWell, noteLabel, firstLabel, pluckLabel, singWell, singTitle, meterR, statusR;
    std::array<juce::Rectangle<float>, crate::drone::kStrings> stringR;
    std::array<juce::Rectangle<float>, numKnobs> knobR;
    std::array<int, crate::drone::kStrings> seenPlucks {};
    std::array<float, crate::drone::kStrings> flash {}, shown {};
    float meter = 0.0f;
    double phase = 0.0;
    int pressed = 0, dragKnob = -1;
    float dragStart = 0, dragValue = 0;
};
