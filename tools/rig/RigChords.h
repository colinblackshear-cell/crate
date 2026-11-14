#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include "RigRouting.h"

#include <array>
#include <atomic>

// The chord builder: your own chords, played on any instrument.
//
// Sixteen chord pads, each any set of notes (build one on the keys, catch one you hold on your
// keyboard, or fill them from a key, scale and chord type, as the vocoder's chord buttons do).
// A pad plays its chord on the instrument it's sent to: the track the keys play (the one picked
// on the rail), or the vocoder, bass, synth, sampler, MPC or drum machine. Click a pad, or play
// them from your keyboard: the Launchkey's 16 drum pads, the 16 lowest keys, or every key (each
// key plays the picked chord's shape from that note, one finger chords). Hold keeps a chord
// sounding until the next; Strum spreads its notes out.
//
// Then a progression: eight steps, each a chord pad (or a rest), a beat to 2 bars each, that plays
// on Kaleidobox's clock, so the chords can go onto a loop layer (To loop) or into a song. Whatever
// the chords play is also in Record set's MIDI, and Rec MIDI / To Logic write MIDI files of your
// own to drag into Logic.
//
// The chords are notes into the instrument's MIDI, so they play its sound (your own sounds and
// plugins on the Bass and Synth), its arp, and the vocoder's robot.
//
// Message thread: the pads, the progression and the settings. The audio thread reads atomic
// copies of them (process()).
class RigChords
{
public:
    RigChords();

    static constexpr int kPads = 16, kMaxNotes = 8, kSteps = 8;
    // Where the chords go: the keys' instrument (follow), or one of RigRouting's.
    static constexpr int kFollowKeys = -1;
    enum class Trigger { screen, pads, lowKeys, everyKey };
    static juce::String triggerName (int);
    // How long each progression step lasts, in beats.
    static const std::array<double, 5>& stepLengths();
    static juce::String stepLengthName (int);
    // Strum: none, fast, slow (ms between notes).
    static const std::array<int, 4>& strumTimes();
    static juce::String strumName (int);

    //==========================================================================
    // The pads (message thread).
    struct Pad
    {
        juce::Array<int> notes;   // MIDI notes, lowest first
        juce::String name;        // your own name; empty: the chord's name (nameOf)
    };
    const Pad& getPad (int pad) const { return pads[(size_t) juce::jlimit (0, kPads - 1, pad)]; }
    void setPad (int pad, const juce::Array<int>& notes, const juce::String& name = {});
    // The name shown on a pad: your name, or the chord's ("Am7", "C/E"), or empty.
    juce::String padName (int pad) const;
    bool isEmpty (int pad) const { return getPad (pad).notes.isEmpty(); }
    // Building on the keys: adds a note, or takes it out if it's there.
    void toggleNote (int pad, int note);
    // Voicing: the lowest note up an octave (or the highest down, for -1); everything an octave up
    // or down; Spread opens it up (every other note up an octave); Close packs it into one octave.
    void invert (int pad, int direction);
    void octave (int pad, int delta);
    void spread (int pad);
    void close (int pad);
    void clearPad (int pad) { setPad (pad, {}); }
    void copyPad (int from, int to);
    // Fills pads 1-7 with the chords of a key (vox::Chords: key 0-11, scale, type), and pad 8
    // with the key's chord an octave up, as the vocoder's chord buttons have them.
    void fillFromKey (int key, int scale, int type);
    int fillKey = 0, fillScale = 0, fillType = 0;   // what Fill uses (saved)
    // Ready-made sets to start from (each fills the pads and the progression).
    static juce::StringArray starterNames();
    void loadStarter (int index);

    // A chord's name from its notes: "C", "Am7", "Fmaj9", "Dsus4", "G/B", "C5"; notes it can't
    // name are listed ("C E F#"). Empty for no notes.
    static juce::String nameOf (const juce::Array<int>& notes);
    static juce::String noteName (int note);   // "C3", "F#4"

    //==========================================================================
    // Playing (message thread: clicks on the screen). With Hold on, press starts the pad and it
    // keeps sounding until another pad (or the same again) is pressed; off, it sounds until release.
    void press (int pad, float velocity = 0.8f);
    void release (int pad);
    // Plays a pad for `ms` then lets it go (hearing a chord as you build it).
    void audition (int pad, int ms = 450);
    void stopAll();
    // The pad sounding now (the latest pressed), or -1. And whether any is.
    int getSounding() const { return sounding.load(); }
    bool isPadDown (int pad) const;
    // The notes the chord builder is playing now, for the keys on screen.
    bool isNoteSounding (int note) const;
    bool anySounding() const;

    std::atomic<int> target { kFollowKeys };
    std::atomic<int> trigger { (int) Trigger::screen };
    std::atomic<bool> hold { false };
    std::atomic<int> strum { 0 };               // index into strumTimes()
    std::atomic<int> lowKey { 36 };             // Low keys: the first of the 16 (C1 on a Launchkey 61)
    std::atomic<int> shapePad { 0 };            // Every key plays this pad's shape (the pad being edited)
    // Where a press went: the instrument (a RigRouting::Dest) the chords play now.
    int resolveTarget (int keysTo) const;

    // Catch: the next chord you hold on your keyboard's keys becomes `pad` (they still play as usual).
    void catchFromKeys (int pad);
    bool isCatching() const { return catchPad.load() >= 0; }
    int getCatchPad() const { return catchPad.load(); }
    void cancelCatch() { catchPad = -1; }
    // Message thread (the window's timer): finishes a catch once every key is up. True when a pad changed.
    bool serviceCatch();

    //==========================================================================
    // The progression (message thread for the steps; it plays on the audio thread).
    std::array<std::atomic<int>, kSteps> steps;   // a pad, or -1 for a rest
    std::atomic<int> stepLength { 2 };             // index into stepLengths(): 1 bar
    std::atomic<bool> progressionOn { false };
    // Steps up to the last used one (0: none used).
    int progressionSteps() const;
    double progressionBeats() const;
    int getPlayingStep() const { return playingStep.load(); }

    //==========================================================================
    // MIDI files for Logic. Rec MIDI records what the chord builder plays from now (on any
    // instrument) until stopped; the take, or the progression, is written as a .mid file.
    void startTake();
    void stopTake();
    bool isTaking() const { return taking.load(); }
    // Message thread, often: moves played notes off the audio thread into the take.
    void serviceTake();
    bool hasTake() const { return take.getNumEvents() > 0; }
    bool writeTake (const juce::File&, double bpm) const;
    bool writeProgression (const juce::File&, double bpm, int beatsPerBar) const;
    // ~/Music/Crate/Rig/To Logic, where the files go (to drag into Logic).
    static juce::File toLogicFolder();

    //==========================================================================
    // Audio thread. Before routing: takes the notes that trigger chords (the pads, the low keys,
    // every key) out of `midi`, into the builder, and watches the keys for Catch.
    void takeTriggers (juce::MidiBuffer& midi, int padChannel, int splitNote);
    // After the clock's beginBlock: plays what was pressed, the triggers and the progression into
    // the instruments' MIDI (`to`, by RigRouting::Dest).
    struct Clock { double beat = 0, bpm = 120, sampleRate = 44100; int beatsPerBar = 4; bool playing = false; };
    void process (const Clock&, int numSamples, int keysTo, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to);
    void prepare (double sampleRate);

    //==========================================================================
    juce::ValueTree toTree() const;
    void fromTree (const juce::ValueTree&);   // an invalid tree: the chords a new Kaleidobox has
    static constexpr const char* kTreeType = "Chords";
    // Your own chord sets, as files (~/Music/Crate/Chords/*.chords).
    static juce::File setsFolder();
    bool saveSet (const juce::File&) const;
    bool loadSet (const juce::File&);
    void reset();

private:
    void syncPad (int pad);   // the pad's notes, to the audio thread's copy
    std::array<Pad, kPads> pads;

    // The audio thread's copy of each pad: kMaxNotes notes (-1 past the last).
    std::array<std::array<std::atomic<int>, kMaxNotes>, kPads> live;

    // Presses from the screen, for the audio thread: pad + 1 (negative: release), velocity.
    struct Press { int pad = 0; float velocity = 0; };
    juce::AbstractFifo pressFifo { 256 };
    std::array<Press, 256> pressQueue {};
    void queue (int pad, float velocity);
    std::array<std::atomic<bool>, kPads> down {};   // message thread's view (pressed, not released)

    // Audio thread: what each source is playing. A source is a pad pressed on the screen, a
    // trigger key (pads, low keys) or an every-key note, or the progression.
    struct Voice
    {
        int dest = -1, count = 0;
        std::array<int, kMaxNotes> notes {};
        int source = -1;   // pad (0-15), 100 + key note (every key / triggers), 300 progression
    };
    static constexpr int kVoices = 32;
    std::array<Voice, kVoices> voices;
    // Notes waiting for their strum time: samples to go, dest, note, velocity, voice.
    struct Pending { int samples = 0, dest = 0, note = 0, voice = -1; float velocity = 0; };
    std::array<Pending, 128> pending {};
    int numPending = 0;
    // How many sources hold each note down, by destination, so one chord letting go doesn't stop
    // the same note in another.
    std::array<std::array<juce::uint8, 128>, RigRouting::kNumDests> held {};
    juce::MidiBuffer triggers;   // taken from the keyboard this block (before routing)
    // What each key or pad down (by channel and note) triggered: channel * 256 + code + 1, or 0.
    std::array<std::array<juce::int16, 128>, 17> takenBy {};
    int lastSoundingVoice = -1;

    void startVoice (int source, int pad, int transpose, float velocity, int dest, int at,
                     std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to, int strumSamples);
    void stopVoice (int source, int at, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to);
    void noteOn (int dest, int note, float velocity, int at, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to);
    void noteOff (int dest, int note, int at, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to);
    int heldPad = -1;   // Hold: the pad latched from the screen or a trigger
    int heldSource = -1;
    std::atomic<int> sounding { -1 };
    std::atomic<int> playingStep { -1 };
    int progressionStepNow = -1;
    std::array<std::atomic<juce::uint64>, 2> soundingMask {};
    void updateMask();
    std::atomic<bool> stopRequested { false };
    double sampleRate = 44100;
    int blockSize = 0;   // this block's, while process() runs

    // Catch.
    std::atomic<int> catchPad { -1 };
    std::array<std::atomic<juce::uint64>, 2> catchMask {}, catchHeld {};
    std::atomic<bool> catchDone { false };

    // The take: note events off the audio thread (beats from the take's start), then a sequence.
    struct TakeEvent { double beat = 0; int note = 0; float velocity = 0; };   // velocity 0: off
    juce::AbstractFifo takeFifo { 4096 };
    std::array<TakeEvent, 4096> takeQueue {};
    std::atomic<bool> taking { false };
    std::atomic<bool> takeReset { false };
    double takeBeats = 0;   // audio thread: beats since the take started
    double takeOffset = 0;  // audio thread: this block's position within the take, per sample
    double takeBeatsPerSample = 0;
    juce::MidiMessageSequence take;
    void addToTake (int note, float velocity, int at);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigChords)
};
