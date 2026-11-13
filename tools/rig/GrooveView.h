#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Vintage.h"

// The Groove screen (F1): the Rig as one groovebox.
//
// Down the left are the seven tracks (Drums, Machine, Bass, Synth, Vocoder, Sampler, Loops), each a dark
// card with its colour stripe, a lamp and a word for what it's doing (Playing, Muted, Ready...),
// a mute and a solo. Tap one to pick it: the middle then shows that track, the keys play it, and
// the keyboard's eight knobs turn its eight knobs.
//
// The middle is the same for every track, so there's one way to work: a well of steps (the
// drums' sounds as rows, the bass's steps, notes, accents and slides, the synth's arp steps and
// accents; the vocoder's parts, the sampler's pads, the loop layers as big tiles), a row of
// patterns (or sounds, modes, layers) under them, and eight knobs below.
class GrooveView : public juce::Component,
                   private juce::Timer,
                   private juce::MidiKeyboardState::Listener
{
public:
    explicit GrooveView (RigEngine&);
    ~GrooveView() override;

    // Drums are the MPC; Machine is the analog drum machine.
    enum Track { drums, machine, bass, synth, vocoder, sampler, loops, numTracks };
    static juce::String trackName (int);
    static juce::Colour trackColour (int);
    static int stripFor (int track);
    // The page with the track's full editor ("MPC", "Bass"...).
    static juce::String tabFor (int track);

    // Picks a track: the middle shows it, the keys play it, the keyboard's knobs turn it.
    void selectTrack (int);
    int getTrack() const { return track; }

    // What the track is doing, or why it's quiet, in a few words.
    juce::String stateOf (int track) const;

    // The middle's controls, as a click on them does (the tests use these too). pressStep is the
    // first row's step (for the drums, the first sound's).
    void pressStep (int step);
    void pressCell (int row, int step);
    void pressPattern (int index);
    void pressPart (int part);
    void pressSolo (int track);
    int numSteps() const;
    int numRows() const;

    // The vocoder's parts: the knobs under each part's switch (its level or wet/dry first, then what
    // shapes it). `part` is the switch's index.
    int numPartKnobs (int part) const;
    vintage::Knob* partKnob (int part, int index) const;

    // The setting knob `index` (0-7) of the picked track turns: the tool and its parameter ID.
    std::pair<int, juce::String> knobFor (int index) const;

    // Bass and Synth: their named sounds, Play/Stop, and keys on the screen.
    void pickSound (int index);
    void stepSound (int delta);
    juce::String soundName() const;
    int numSounds() const;
    void pressPlay();   // the Bass's (or the drum machine's) pattern: on (and the Rig plays) or off
    // The drum machine: a step's hit becomes a roll of 2, 3, 4 hits, then a single hit again.
    void pressRoll (int row, int step);
    bool hasKeys() const { return track == bass || track == synth; }
    juce::MidiKeyboardState& getScreenKeys() { return keysState; }

    // Re-reads everything (after a show is loaded).
    void refresh();

    std::function<void (const juce::String& tab)> onOpen;
    std::function<void (const juce::String&)> onSay;
    std::function<float (int strip)> meterOf;
    std::function<void()> onSample;
    // Its Quantize button: the Quantize panel on this track (a RigQuantizer::Target), by the button.
    std::function<void (int target, juce::Component* from)> onQuantize;
    std::function<bool()> isSampling;
    // Whether the Mic switch shows (not in Logic, where the side chain is the mic).
    bool showMic = true;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    class TrackCard;

private:
    void timerCallback() override;
    void handleNoteOn (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void handleNoteOff (juce::MidiKeyboardState*, int channel, int note, float velocity) override;
    void showSoundMenu();
    void say (const juce::String&);
    class PluginSlot* slotFor (int track) const;
    void buildTrack();
    void layoutPartKnobs();
    void updateButtons();
    void setFollowKnobs();
    juce::AudioProcessorValueTreeState* paramsOf (int tool) const;

    // A cell of the middle: what it shows.
    struct Cell
    {
        juce::String label, sub;
        bool lit = false, strong = false, dim = false, playing = false;
    };
    Cell cellAt (int row, int step) const;
    juce::String rowName (int row) const;
    int playheadStep() const;
    bool bigCells() const { return track == vocoder || track == sampler || track == loops; }
    juce::Rectangle<float> cellRect (int row, int step) const;
    std::pair<int, int> cellAt (juce::Point<int>) const;
    int rowLabelAt (juce::Point<int>) const;
    void releaseCell();
    void nudgeNote (int step, int semitones);
    void toggleAccent (int step);
    juce::String patternText() const;
    juce::String detailOf (int track) const;
    void showDrumRowMenu (int row);

    // Drums: one row per sound (pads of the bank the keyboard's pads play).
    static constexpr int kDrumRows = 4;
    int drumPad (int row) const;
    bool drumHit (int pad, int step) const;
    void setDrumHit (int pad, int step, bool on);
    void clearPattern();

    // The drum machine: one row per sound (picked from its 12), and which 16 steps show.
    static constexpr int kMachineRows = 5;
    std::array<int, kMachineRows> machineRows { crate::drums::kick, crate::drums::snare, crate::drums::clap,
                                                crate::drums::closedHat, crate::drums::openHat };
    int machinePage = 0;
    void showMachineRowMenu (int row);
    int machineLength() const;

    RigEngine& engine;
    int track = vocoder;
    std::array<int, kDrumRows> drumRows { -1, -1, -1, -1 };   // pad in the bank, -1 = pick from the kit
    int drumBar = 0;
    int heldCell = -1, dragStep = -1, dragStartNote = 0, dragStartY = 0;
    juce::String shownSignature;

    // Solo: the track soloed, and the mutes to put back.
    int soloTrack = -1;
    std::array<bool, RigEngine::numStrips> mutesBeforeSolo {};

    juce::OwnedArray<TrackCard> cards;
    juce::TextButton openButton, micButton { "Mic off" }, extraButton, barButton, clearButton { "Clear" }, quantizeButton { "Quantize" }, soundButton,
        soundPrev { "<" }, soundNext { ">" };
    juce::MidiKeyboardState keysState;
    juce::MidiKeyboardComponent screenKeys { keysState, juce::MidiKeyboardComponent::horizontalKeyboard };
    juce::Rectangle<int> keysArea;
    juce::OwnedArray<juce::TextButton> patternButtons;
    std::vector<std::unique_ptr<vintage::Knob>> knobs;
    std::vector<std::unique_ptr<vintage::Knob>> partKnobs;   // the vocoder's parts' own knobs
    std::vector<int> partOfKnob;
    juce::Rectangle<int> partsArea;
    juce::Rectangle<int> tracksArea, wellArea, headerArea, numbersArea, gridArea, patternArea, knobBox, knobHeader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GrooveView)
};
