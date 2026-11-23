#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "StepTools.h"
#include "Vintage.h"

// The Groove screen (F1): the Rig as one groovebox.
//
// Down the left are the nine tracks (MPC, Machine, Bass, Synth, Vocoder, Sampler, Loops, Line in, Drone),
// every instrument Kaleidobox has, each a dark card with its colour stripe, a lamp and a word for
// what it's doing (Playing, Muted, Ready...), its own Play / Stop (or On / Off, or Rec), a mute and
// a solo. Tap one to pick it: the middle then shows that track, the keys play it, and
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

    // Drums are the MPC (its card says MPC); Machine is the analog drum machine; Line in is the
    // Input strip (a guitar, keytar or second mic on your interface). The Drone came last.
    enum Track { drums, machine, bass, synth, vocoder, sampler, loops, lineIn, drone, numTracks };
    static juce::String trackName (int);
    // Its name on the rail, as on the phone's tool row ("Pads · MPC" for the MPC), and its
    // picture (a crate::toolicons::Tool).
    static juce::String railName (int);
    static int railIcon (int);
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
    void clearPatternNow() { clearPattern(); }
    // Takes back the last Clear of a beat or pattern (Clear turns into "Undo clear" until you
    // change that pattern again). Cmd+Z does it too. True if there was one to take back.
    bool undoClear();
    bool canUndoClear() const { return clearUndo.track >= 0 && clearUndo.track == track; }
    void pressPattern (int index);
    void pressPart (int part);
    void pressSolo (int track);

    // Each card's own button, so every instrument starts and stops from here without opening it:
    // MPC, Machine and Bass play / stop their pattern (and start Kaleidobox if it's stopped), the
    // Synth's arp, the Sampler's sound; the Vocoder and Line in switch on and off; Loops records
    // the next empty layer (press again to close it). isGoing: what the button shows as lit.
    void pressTrackButton (int track);
    bool isGoing (int track) const;
    // The button's word ("Play", "Stop", "On", "Off", "Rec") and what it does.
    juce::String trackButtonText (int track) const;
    juce::String trackButtonTip (int track) const;
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
    void pressPlay();   // the Bass's (or the drum machine's, or Line in's) pattern: on (and the Rig plays) or off
    // The drum machine, right-click (Option-click): a step's hit becomes a roll of 2, 3, 4, 6, 8 hits, then a single hit again.
    // (The ROLL chip does it with one click: pick it, then a step.)
    void pressRoll (int row, int step);
    bool hasKeys() const { return track == bass || track == synth; }
    juce::MidiKeyboardState& getScreenKeys() { return keysState; }

    // The Make a beat lesson: glows the suggested steps (bit i = step i) on a row of the grid
    // (-1 = none). Only a glow: nothing is written for you.
    void setGuide (int row, juce::uint32 stepMask) { guideRow = row; guideMask = stepMask; repaint(); }
    int getGuideRow() const { return guideRow; }
    juce::uint32 getGuideMask() const { return guideMask; }
    // The grid's cell for a row and step, in this view's coordinates (the lesson points at it).
    juce::Rectangle<int> stepBounds (int row, int step) const { return cellRect (row, step).toNearestInt(); }

    // Re-reads everything (after a show is loaded).
    void refresh();

    std::function<void (const juce::String& tab)> onOpen;
    // A track card was clicked (picked), wherever the cards are; double-clicked (open it in full).
    std::function<void (int track)> onTrackPicked, onTrackOpened;
    // Loops: the layer picked on Play (0-7).
    int getPickedLayer() const { return pickedLayer; }

    // The track cards, for the window's track rail: they move there (and stay this view's), and
    // the Groove's middle takes the whole width.
    juce::Array<juce::Component*> takeTrackCards();
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
    void songHint();
    // What to do on this track's screen, in a line.
    juce::String trackHint() const;
    // What the last Clear took away, to put back.
    struct ClearUndo
    {
        int track = -1, index = 0, variation = 0;
        std::vector<seq::Track> mpcTracks;
        crate::drums::Pattern machine;
        crate::bass::Pattern bass;
        crate::synthtool::Rhythm synth;
    } clearUndo;

    // Pattern life (crate::life) on the step grids: what a click on a lit step changes (the hit
    // itself, its chance, its condition or its nudge), Mutate, and each row's menu (its own
    // length, Euclid, turn).
public:
    // Pitch, Decay and Level are the drum machine's step locks (the ritual kit).
    enum LifeMode { lifeHits, lifeChance, lifeCond, lifeNudge, lifePitch, lifeDecay, lifeLevel, numLifeModes };
    // Feel: a ready-made grid and swing for the drum machine (12/8, 6/8, triplets, shuffle...).
    void applyFeel (int feel);
    static juce::StringArray feelNames();
    void setLifeMode (int);
    int getLifeMode() const { return lifeMode; }
    bool hasLife() const { return track == machine || track == bass || track == drums; }
    void mutate (float amount);   // 0..1
    bool undoMutate();
    void euclidRow (int row, int hits, int steps, int rotate = 0);
    void turnRow (int row, int by);
    void setRowLength (int row, int steps);   // the drum machine (0: the pattern's)
    void showRowMenu (int row);

    // The drum machine's step tools (in HITS): a chip picked, then a click on a step writes that tool
    // (the ROLL chip a stutter roll of N, BUILDS a roll that builds, FLAM a grace hit, SOFT/LOUD steps
    // the hit's level). No chip picked: a click lights or clears a step, as it always did.
    using StepTool = crate::drums::steptools::Tool;
    void setMachineTool (StepTool, bool on);
    bool machineToolOn (StepTool t) const { return machineToolPicked && machineTool == t; }
    void setMachineRoll (int n);
    int getMachineRoll() const { return machineRoll; }
    // Which bar the drum machine's steps show (0-3): Bar 1 to 4 of up to 64 steps. A bar past the
    // end makes the pattern longer.
    void setMachinePage (int bar);
    int getMachinePage() const { return machinePage; }
private:
    bool machineToolPicked = false;
    StepTool machineTool = StepTool::roll;
    int machineRoll = 3;
    std::array<juce::TextButton, 4> machineBars { juce::TextButton ("Bar 1"), juce::TextButton ("Bar 2"), juce::TextButton ("Bar 3"), juce::TextButton ("Bar 4") };
    std::array<juce::TextButton, 4> toolChips { juce::TextButton ("Roll"), juce::TextButton ("Builds"), juce::TextButton ("Flam"), juce::TextButton ("Soft/Loud") };
    juce::Rectangle<int> toolsArea;
    bool pressLife (int row, int i, bool back);
    void showMutateMenu();
    int lifeMode = lifeHits;
    int guideRow = -1;
    juce::uint32 guideMask = 0;
    juce::OwnedArray<juce::TextButton> lifeButtons;
    juce::TextButton mutateButton { "Mutate" }, feelButton { "Feel" };
    void showFeelMenu();
    int beatGroup() const;   // steps to a beat on the grid: 4, or 3 for triplet feels
    juce::Rectangle<int> lifeArea;
    ClearUndo lifeUndo;
    juce::Random lifeRandom;
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
        int hits = 0, shape = 0;   // the drum machine's: a roll's size and how it plays
    };
    Cell cellAt (int row, int step) const;
    juce::String rowName (int row) const;
    int playheadStep() const;
    bool bigCells() const { return track == vocoder || track == sampler || track == loops || track == lineIn || track == drone; }
    // The drone: its On, and a string's name ("Pa", "Sa", "Low Sa").
    bool droneOn() const;
    void setDroneOn (bool);
    juce::String droneStringName (int string) const;
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

    // Line in: on (and the audio input with it), which jack, and a loop of it.
    bool lineInLive() const;
    void setLineIn (bool on);
    void loopLineIn();

    // The drum machine, as its own page shows it: the accent row, then all 12 sounds in its order,
    // the pattern and the variation (A or B) its page is editing, and which 16 steps show.
    static constexpr int kMachineRows = crate::drums::kVoices + 1;
    static int machineVoice (int row) { return row - 1; }   // row 0 is the accent
    int machinePage = 0;
    crate::drums::Pattern machinePattern() const;
    void setMachinePattern (const crate::drums::Pattern&);
    void auditionMachineRow (int row);
    int machineLength() const;

    RigEngine& engine;
    int track = vocoder;
    bool cardsElsewhere = false;
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
    // Loops: the layer you've picked (a tap on a layer with sound picks it; it never starts
    // recording on top), and what you can do to it: record on top, take that back, clear it,
    // or clear it and record it again from the next bar.
    int pickedLayer = 0;
    juce::TextButton overdubButton { "Overdub" }, undoButton { "Undo" }, redoButton { "Redo" };
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
