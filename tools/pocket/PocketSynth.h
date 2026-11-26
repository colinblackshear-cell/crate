#pragma once

#include "BeatsEngine.h"
#include "PocketKey.h"
#include "PocketLooper.h"

#include "synth/SynthEngine.h"   // the Mac's PS-8 synth engine (tools/synth), its DSP reused as it is

#include <array>
#include <atomic>
#include <vector>

// The SYNTH tool's sound: the desktop app's synth engine (crate::synthtool::Engine, the Rig's PS-8) with
// the Rig's named sounds (RigSounds: the PS-8's and the BX-1 bass's, mapped onto the same engine) plus a
// few of the phone's own to fill out eight kinds. The phone puts four knobs on top of a sound (TONE, SHAPE,
// DECAY, ECHO).
//
// SynthEngine is an Instrument inside the looper (the one renderer): keys come in through a lock-free
// queue, the note pattern plays on the shared clock, and the keys' notes (and the arp's) can be captured
// for REC. Two engines run side by side: the keys' (with the arp) and the pattern's (never arpeggiated, so
// a pattern recorded from the arp plays back as it was). Nothing on the audio thread allocates or waits.
namespace pocket
{
constexpr int kSynthKinds = 8;
const char* synthKindName (int kind);           // "BASS"
juce::Colour synthKindColour (int kind);

struct SynthPreset
{
    int kind = 0;
    juce::String name;
    juce::String from;                          // "PS-8", "BX-1" (the Mac's), "POCKET"
    crate::synthtool::Settings settings;
};
const std::vector<SynthPreset>& synthPresets();
int synthPresetIndex (const juce::String& name);   // -1 = not found
// The presets of one kind, as indexes into synthPresets().
std::vector<int> synthPresetsOf (int kind);

// The four knobs on a sound. 0.5 is the sound as it was made, for TONE and DECAY.
struct SynthKnobs
{
    float tone = 0.5f;     // the filter: +-3 octaves round the sound's cutoff
    int shape = 0;         // the wave: 0 saw, 1 square, 2 triangle, 3 supersaw
    float decay = 0.5f;    // the envelope's lengths: x0.25 .. x4
    float echo = 0.0f;     // send to the shared delay and reverb
};
const char* synthShapeName (int shape);   // "SAW"
int presetShape (int preset);
crate::synthtool::Settings makeSynthSettings (int preset, const SynthKnobs&, int octave);
// The DECAY knob's seconds: the release for sustained kinds (PAD, ORGAN, STRINGS), else the decay.
float synthDecaySeconds (int preset, const SynthKnobs&);
bool synthKnobIsRelease (int preset);
// The echo knob as sends: into the delay and the reverb (the same as a loop layer's DELAY and REVERB).
inline float echoToDelay (float echo) { return echo * 0.55f; }
inline float echoToReverb (float echo) { return echo * 0.3f; }

//==============================================================================
// The note pattern: notes at beats inside 1, 2, 4 or 8 bars (polyphonic). Edited as 16 steps a bar.
constexpr int kSynthMaxNotes = 256;
constexpr double kSynthStep = 0.25;
struct SynthNote
{
    double beat = 0;           // from the pattern's start
    double length = 0.23;      // in beats
    int note = 60;
    float velocity = 0.8f;
    double skipUntil = -1;     // just played by hand: the pattern doesn't play it again before this beat
};
struct SynthPattern
{
    int bars = 1;
    std::vector<SynthNote> notes;
    double lengthBeats() const { return bars * 4.0; }
    int steps() const { return bars * 16; }
    bool empty() const { return notes.empty(); }
    // Step editing (16 steps a bar): the notes starting in a step, setting one, clearing, tying on.
    std::vector<int> notesIn (int step) const;
    void setStep (int step, int note, float velocity = 0.8f);
    void clearStep (int step);
    bool tieStep (int step);       // the step's notes last one step longer; false when it has none
    bool tiedInto (int step) const;   // a note from an earlier step still sounds through this one
    void setBars (int bars);       // notes past the new length go
    juce::var toVar() const;
    static SynthPattern fromVar (const juce::var&);
};

//==============================================================================
// The desktop engine with its settings handed to the audio thread under a try-lock (like the drums).
class SynthCore
{
public:
    void prepare (double sampleRate);
    void setSettings (const crate::synthtool::Settings&);   // message thread
    // Audio thread
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer&, const crate::synthtool::Clock&);
    crate::synthtool::Engine engine;

private:
    crate::synthtool::Settings pending;
    juce::SpinLock lock;
    std::atomic<int> version { 0 };
    int liveVersion = -1;
};

class SynthEngine : public Instrument
{
public:
    explicit SynthEngine (BeatsEngine&);

    //==========================================================================
    // Message thread
    void setSettings (const crate::synthtool::Settings&);
    void setPattern (const SynthPattern&);
    // The pattern only plays from this beat on (a REC pass: what you play goes in, and plays from the pass's end).
    void setPlayFrom (double beat) { playFrom = beat; }
    void keyDown (int note, float velocity = 0.8f);
    void keyUp (int note);
    void allKeysUp();
    void setEcho (float echo01) { echo = echo01; }
    float echoSend() const override { return echo.load(); }
    // REC: the notes the keys (and the arp) play, at their beat on the clock, for the message thread.
    struct Played { double beat = 0; int note = 0; float velocity = 0; bool on = false; };
    void setCapture (bool on) { capture = on; }
    int readPlayed (Played* out, int max);
    int patternStep() const { return step.load(); }   // -1 when the pattern isn't playing
    float takePeak() { return peak.exchange (0.0f); }
    int notesPlayed() const { return patternNotesPlayed.load(); }

    //==========================================================================
    // Audio thread
    void prepare (double sampleRate) override;
    void render (float* left, float* right, int numSamples) override;
    // The same with the clock given (the self-checks run it without the drums).
    void renderAt (float* left, float* right, int numSamples, bool running, double beat, double bpm);

private:
    struct KeyEvent { int note = 0; float velocity = 0; bool on = false; };   // note -1: all up
    BeatsEngine& beats;
    SynthCore keys, pat;
    double sampleRate = 44100.0;
    std::atomic<float> echo { 0.0f }, peak { 0.0f };
    std::atomic<double> playFrom { -1.0e9 };
    std::atomic<bool> capture { false };
    std::atomic<int> step { -1 }, patternNotesPlayed { 0 };

    juce::AbstractFifo keyFifo { 256 };
    std::array<KeyEvent, 256> keyQueue {};
    juce::AbstractFifo playedFifo { 1024 };
    std::array<Played, 1024> playedQueue {};

    std::array<SynthNote, kSynthMaxNotes> pendingNotes {}, liveNotes {};
    int pendingCount = 0, liveCount = 0;
    double pendingLength = 4.0, liveLength = 4.0;
    juce::SpinLock patternLock;
    std::atomic<int> patternVersion { 0 };
    int livePatternVersion = -1;

    // audio thread
    juce::MidiBuffer keyMidi, patMidi, tap;
    struct Sounding { int note = 0; double off = 0; };
    std::array<Sounding, 64> sounding {};
    int numSounding = 0;
    bool wasRunning = false;
};

//==============================================================================
// The SYNTH tool's part (message thread): the sound, the knobs, the mode, the pattern. Saved with the song.
class SynthPart
{
public:
    explicit SynthPart (BeatsEngine&);
    SynthEngine& engine() { return eng; }

    enum Mode { keysMode, patternMode, arpMode };
    int preset = 0;
    SynthKnobs knobs;
    int octave = 0;            // -2..2
    int mode = keysMode;
    bool chromatic = false;
    bool chords = false;       // in key: each key plays its chord (the key's note, a third and a fifth up, all in the key)
    int quantize = 0;          // REC: 0 1/16, 1 1/8, 2 off (as played)
    int arpStyle = 0;          // 0 up, 1 down, 2 up and down
    int arpRate = 1;           // 0 1/8, 1 1/16
    bool arpHold = false;
    SynthPattern pattern;

    void setPreset (int index);   // the knobs go back to the sound's own
    void stepPreset (int delta);  // inside the kind
    int kind() const;
    void apply();                 // sound, knobs, mode and arp to the engine
    void patternChanged();        // the pattern to the engine
    void reset();                 // a new song
    juce::var toVar() const;
    void fromVar (const juce::var&);

    // The keys: in the key (16 keys in two rows: the scale from the root up two octaves) or chromatic
    // (a piano octave from C: 8 white keys and 5 black).
    int keyCount() const { return chromatic ? 13 : 16; }
    // The notes a key sounds: its note, and with CHORDS (in key) the third and fifth above it in the key.
    std::vector<int> keyNotes (int key, const SongKey&) const;
    // In key, the scale step a key plays: the low row root to root (0-7), the top row from that root up (7-14).
    static int keyDegree (int key) { key = juce::jlimit (0, 15, key); return key < 8 ? key : key - 1; }
    int keyNote (int key, const SongKey&) const;

    // REC into the pattern: what the keys and the arp play from `from` to `to` (beats on the clock) goes in,
    // snapped by `quantize`, at its place in a pattern of `bars` bars. The pattern before is kept (stash).
    void startTake (int bars, double from, double to);
    void serviceTake();           // often, while it records
    void endTake();               // notes still held end at the pass's end
    bool taking() const { return takeOn; }
    // Loop until STOP: notes keep going in pass after pass (wrapped into the pattern) until this beat.
    void setTakeEnd (double beat) { takeEnd = beat; }
    double takeEndBeat() const { return takeEnd; }
    bool hasStash() const { return stashed; }
    void restoreStash();
    void clearKeepingUndo();      // the part went on a layer: an empty pattern, the part kept for UNDO
    void dropStash() { stashed = false; }
    SynthPattern stash;

private:
    SynthEngine eng;
    bool takeOn = false, stashed = false;
    double takeFrom = 0, takeTo = 0, takeEnd = 0;   // takeTo: the first pass ends (the pattern plays from there); takeEnd: notes stop going in
    std::array<double, 128> openAt {};
    std::array<float, 128> openVel {};
    void addTakeNote (double onBeat, double offBeat, int note, float velocity);
};

// Runs the engine by hand: a key sounds at its pitch, the pattern's notes start on their steps, the arp plays
// in time. Returns what was wrong (empty = fine).
juce::String synthSelfCheck (juce::String& report);
}
