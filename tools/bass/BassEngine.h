#pragma once

#include "core/synth/SynthDsp.h"

#include <array>
#include <memory>

// The Bass: a deep mono synth with a 16-step sequencer, in the spirit of the classic acid
// boxes. Each step has a note, an accent and a slide (the note glides into the next one
// without restarting). The sequencer plays in time with the host (Logic or the Rig) while it
// plays; the keys play the bass directly, or transpose the running pattern.
namespace crate::bass
{
constexpr int kSteps = 16;
constexpr int kPatterns = 8;

struct Step
{
    bool on = false;
    int note = 0;          // semitones above the root note (-12..24)
    bool accent = false;
    bool slide = false;    // glide into the next step instead of retriggering
};

struct Pattern
{
    std::array<Step, kSteps> steps {};
    int length = kSteps;   // 1..16 steps
};

struct Settings
{
    synth::Wave wave = synth::Wave::saw;
    float cutoff = 400.0f;       // Hz
    float resonance = 0.6f;      // 0..1
    float envMod = 0.6f;         // 0..1: how far the filter envelope opens the filter
    float decay = 0.3f;          // seconds, the filter envelope
    float accent = 0.6f;         // 0..1: how much harder accented steps hit
    float glide = 0.06f;         // seconds, for slides and overlapping keys
    float drive = 0.3f;          // 0..1
    float sub = 0.4f;            // 0..1: a sine an octave down
    float lfoDepth = 0.0f;       // 0..1: up to +/- 3 octaves on the cutoff
    int lfoRate = 4;             // index into SyncRates
    synth::LfoShape lfoShape = synth::LfoShape::sine;
    float volume = 0.8f;
    int root = 36;               // MIDI note of step note 0 (C2)
    int stepRate = 6;            // index into SyncRates (1/16)
    float gate = 0.55f;          // share of a step a note holds
    bool keysTranspose = true;   // while the pattern plays, a key moves it to that key
    bool sequencerOn = true;
};

// What the host says about time this block.
struct Clock
{
    bool playing = false;
    double ppq = 0;
    double bpm = 120;
};

class Engine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings& s) { settings = s; }
    const Settings& getSettings() const { return settings; }

    // Audio thread: the pattern to play (kept alive by the caller until it's replaced).
    void setPattern (const Pattern* p) { pattern = p; }

    // Plays one block: MIDI from the keys, the pattern if the clock is playing, into `left` and
    // `right` (added to what's there).
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer&, const Clock&);

    // For the window: the step playing now (-1 when stopped) and the transpose from the keys.
    int getCurrentStep() const { return currentStep; }
    int getTranspose() const { return transpose; }

private:
    void noteOn (int note, float velocity, bool accent, bool legato);
    void noteOff();
    void handleMidi (const juce::MidiMessage&, bool sequencing);
    float renderSample (double beatsPerSample);

    Settings settings;
    const Pattern* pattern = nullptr;
    double sampleRate = 44100.0;

    synth::Oscillator osc, subOsc;
    synth::LowPass filter;
    synth::Envelope amp, filterEnv;
    synth::Lfo lfo;

    float currentNote = 36.0f, targetNote = 36.0f;
    bool gliding = false;
    float noteAccent = 0.0f, velocity = 1.0f;
    int filterCounter = 0;

    // Keys.
    std::array<int, 16> heldKeys {};
    int numHeld = 0;
    int transpose = 0;

    // Sequencer.
    long long lastStepIndex = -1;
    int currentStep = -1;
    bool stepGateOpen = false;
    double stepEndBeat = 0;
    bool wasPlaying = false;
    double freeClock = 0;
};
}
