#pragma once

#include "PocketDrone.h"
#include "PocketFrame.h"
#include "PocketTheme.h"

// The DRONE tool, as the Mac's Drone screen laid out for the thumb: ON, its ready-made SOUND and its VOLUME
// up top, the four strings (tap one to pluck it), the note (the song's KEY, or one of your own), the octave and
// the first string, when it plucks, its knobs, and SING INTO THE STRINGS (how much they ring to your voice or a
// tool, what they ring to, and whether the mic is on). PATTERN / TONE (the chip up top) turns to its second page:
// the order the strings go in or steps you draw, SWING and HUMAN, the instrument, BRIGHT, BODY, DETUNE and SPACE.
// MIRACLE (the third chip) is MIRACLE TONES: a held tone at an exact frequency (the Solfeggio tones, 432, Om or your
// own), FIT (over the drone, or the drone tuned to it), SOUND, BINAURAL (headphones), LEVEL, SWELL and TONE ONLY.
// The thumb bar's REC records the drone onto the next loop layer (PocketApp drives it).
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
        none = 0, onId = 1, soundId, keyId, noteId, pageId, octBase = 10, firstBase = 20, syncBase = 30, knobBase = 40, feedId = 60, micId,
        stringBase = 70, orderBase = 80, stepCountBase = 90, stepRateBase = 93, clearId = 96, voiceBase = 100,
        pageBase = 110,   // + 0 STRINGS, 1 PATTERN, 2 MIRACLE
        toneBase = 120, fitBase = 140, toneSoundBase = 143, beatBase = 146, toneOnlyId = 150,
        cellBase = 200   // + step * 4 + string
    };
    enum Knob { paceKnob, jawariKnob, ringKnob, reedsKnob, volumeKnob, resonateKnob, swingKnob, humanKnob, brightKnob, bodyKnob, detuneKnob,
                spaceKnob, toneLevelKnob, toneSwellKnob, numKnobs };
    // Three pages, as the Mac's three tabs on a short window: the strings (0), PATTERN and TONE (1), MIRACLE TONES (2).
    void showPage (int);
    int getPage() const { return page; }
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
    void layoutPattern (float y);
    void paintPattern (juce::Graphics&);
    void layoutMiracle (float y);
    void paintMiracle (juce::Graphics&);
    void askOwnHz();
    juce::String knobText (int knob) const;
    juce::String statusText() const;

    pocket::DroneTool& drone;
    std::vector<Hit> hits;
    juce::Rectangle<float> gridR, orderLabel, stepsLabel, toneLabel;
    juce::Rectangle<float> readoutR, fitLabelR, soundLabelR, beatLabelR, toneNoteR;
    int page = 0, shownStep = -1;
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
