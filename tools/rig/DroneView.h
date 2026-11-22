#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "RigEngine.h"
#include "RigColours.h"
#include "RigPanels.h"
#include "Vintage.h"

// The Drone track's screen (Edit): the Drone box in full, left to right.
//
//   Strings: the tanpura's four strings, ringing as you hear them (click one to pluck it), with
//            its On and its ready-made sounds above them.
//   Tuning:  the note (C to B), the octave, what the first string plays (Pa, Ma, Ni or Sa), a fine
//            tune, Follow keys, and the chord builder's key.
//   Playing: when it plucks (its own time, or on the beat), how fast, and the sound of the strings.
//   Resonate: strings that ring to what a track plays into them (they work with the drone off too).
//   Pattern (under them): the order the strings go round in, or steps you draw; Swing and Human.
//   Tone: the instrument (tanpura, bowed, swarmandal, electric, bell), Bright, Body, Detune and Space.
//
// Every control is one of the drone's settings, so knob Learn, macros and automation see it as they
// see any tool's.
class DroneView : public juce::Component,
                  private juce::Timer
{
public:
    explicit DroneView (RigEngine&);
    ~DroneView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    // What the screen says it did (the Rig's status line).
    std::function<void (const juce::String&)> onSay;

    // A string's lane in the Strings well (0 at the top), and what a click on one does.
    juce::Rectangle<int> stringArea (int string) const;
    void pressString (int string);
    // The controls, for the window and the tests.
    rigpanels::PanelButton onButton { "On" }, soundButton { "Sound" };
    juce::TextButton followButton { "Follow keys" }, chordsKeyButton { "Use the chords' key" }, feedButton;
    // The drone's volume, right here: the same as its fader on the Mixer.
    vintage::Knob levelKnob { "Volume", RigColours::drone };
    // Shown while singing into the strings can't be heard (the mic is off): turns the mic on.
    juce::TextButton micButton { "Mic off: turn it on" };
    void turnMicOn();
    juce::OwnedArray<juce::TextButton> noteKeys, octaveButtons, firstButtons, pluckButtons;
    // Pattern: the order the strings go round in, or your own steps (a grid: a row for each string), their
    // swing and feel. Tone: the instrument the strings are, and its sound.
    juce::OwnedArray<juce::TextButton> orderButtons, stepCountButtons, stepRateButtons, voiceButtons;
    juce::TextButton clearStepsButton { "Clear" };
    // A short window: the strings (0) or Pattern and Tone (1), a tab for each.
    juce::TextButton stringsTab { "Strings and tuning" }, patternTab { "Pattern and tone" };
    void showPage (int);
    int getPage() const { return page; }
    bool isCompact() const { return compact; }
    juce::Rectangle<int> stepCell (int step, int string) const;   // empty while it's hidden
    void pressStep (int step, int string);
    vintage::Knob* knobFor (const juce::String& paramId) const;
    void showSoundMenu();
    void showFeedMenu();
    // The strings' and the meter's movement, as the timer does it (tests call it too).
    void tick();

private:
    void timerCallback() override;
    void say (const juce::String&);
    void setChoice (const char* id, int index);
    int choice (const char* id) const;
    void update();

    RigEngine& engine;
    std::vector<std::unique_ptr<vintage::Knob>> knobs;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> followAttachment;
    juce::Rectangle<int> stringsWell, tuningWell, playingWell, resonateWell, stringsArea, meterArea, captionArea;
    juce::Rectangle<int> patternWell, toneWell, orderLabel, stepsLabel, gridArea, gridHint, voiceLabel;
    int gridLabelW = 40, shownStep = -1, page = 0;
    bool compact = false;
    std::vector<juce::Component*> mainControls();
    std::vector<juce::Component*> moreControls();
    juce::Rectangle<int> noteLabel, octaveLabel, firstLabel, pluckLabel, feedLabel;
    std::array<int, crate::drone::kStrings> seenPlucks {};
    std::array<float, crate::drone::kStrings> flash {}, shown {};
    float meter = 0.0f;
    int titleTwoLines = 24;   // the Resonate well's title: one line or two
    double phase = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneView)
};
