#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "core/ui/Theme.h"
#include "core/ui/ParamControls.h"

// The Main screen: the Rig as one groovebox.
//
// Down the left are the six tracks (Drums, Bass, Synth, Vocoder, Sampler, Loops). Each says in
// a few words what it's doing, or why it's quiet ("Muted", "Mic is off", "No notes yet: light
// some steps"), and has a mute. Click one to pick it: the middle then shows that track, the
// keys play it, and the keyboard's eight knobs turn its eight knobs.
//
// The middle is the same for every track, so there's one way to work: a lane (the bass's notes,
// the drum sounds), 16 big steps, a row of patterns (or sounds, modes, layers), and eight knobs.
// Song parts A-H run along the bottom: an empty one saves what's playing, a full one plays it
// from the next bar.
class GrooveView : public juce::Component,
                   private juce::Timer
{
public:
    explicit GrooveView (RigEngine&);
    ~GrooveView() override;

    enum Track { drums, bass, synth, vocoder, sampler, loops, numTracks };
    static juce::String trackName (int);
    static juce::Colour trackColour (int);
    static int stripFor (int track);
    // The tab with the track's full editor ("MPC", "Bass"...).
    static juce::String tabFor (int track);

    // Picks a track: the middle shows it, the keys play it, the keyboard's knobs turn it.
    void selectTrack (int);
    int getTrack() const { return track; }

    // What the track is doing, or why it's quiet, in a few words.
    juce::String stateOf (int track) const;

    // The middle's controls, as a click on them does (the tests use these too).
    void pressStep (int step);
    void pressPattern (int index);
    void pressPart (int part);
    int numSteps() const;

    // The setting knob `index` (0-7) of the picked track turns: the tool and its parameter ID.
    std::pair<int, juce::String> knobFor (int index) const;

    // Re-reads everything (after a show is loaded).
    void refresh();

    std::function<void (const juce::String& tab)> onOpen;
    std::function<float (int strip)> meterOf;
    std::function<void()> onSample;
    std::function<bool()> isSampling;
    // Whether the Mic switch shows (not in Logic, where the side chain is the mic).
    bool showMic = true;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    class TrackButton;

private:
    void timerCallback() override;
    void buildTrack();
    void updateButtons();
    void setFollowKnobs();
    juce::AudioProcessorValueTreeState* paramsOf (int tool) const;

    // The middle's cells: what each shows.
    struct Cell
    {
        juce::String label, sub;
        bool lit = false, strong = false, dim = false, playing = false;
    };
    Cell cellAt (int i) const;
    int playheadStep() const;
    int laneCount() const;
    Cell laneCell (int i) const;
    void pressLane (int i);
    void releaseCell (int i);
    juce::Rectangle<float> stepRect (int i) const;
    juce::Rectangle<float> laneRect (int i) const;
    int stepAt (juce::Point<int>) const;
    int laneAt (juce::Point<int>) const;
    void nudgeNote (int step, int semitones);
    void toggleAccent (int step);

    // Drums: the steps of one pad in one bar of the MPC's sequence.
    bool drumHit (int pad, int step) const;
    void setDrumHit (int pad, int step, bool on);
    int drumPad() const;

    RigEngine& engine;
    int track = vocoder;
    int drumPadIndex = 0, drumBar = 0;
    int heldCell = -1, dragStep = -1, dragStartNote = 0, dragStartY = 0;
    juce::String shownSignature;

    juce::OwnedArray<TrackButton> trackButtons;
    juce::TextButton openButton, micButton { "Mic off" }, extraButton, barButton;
    juce::OwnedArray<juce::TextButton> patternButtons, partButtons;
    std::vector<std::unique_ptr<crate::ParamKnob>> knobs;
    juce::Rectangle<int> headerArea, laneArea, stepArea, knobArea, partsArea, centreArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveView)
};
