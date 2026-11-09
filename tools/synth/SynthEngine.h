#pragma once

#include "core/synth/SynthDsp.h"

#include <array>

// The Synth: an 8-voice synth (saw, square, triangle or a fat detuned supersaw) with a resonant
// filter, envelopes and a tempo-synced LFO, plus an arpeggiator sequencer: hold a chord and it
// plays the notes up, down, both ways, at random or as played, over 1-3 octaves, on the steps
// you switch on in its 16-step rhythm.
namespace crate::synthtool
{
constexpr int kVoices = 8;
constexpr int kRhythmSteps = 16;

enum class ArpMode { up, down, upDown, random, asPlayed };

struct Rhythm
{
    std::array<bool, kRhythmSteps> on {};
    std::array<bool, kRhythmSteps> accent {};
    int length = kRhythmSteps;
    Rhythm() { on.fill (true); }
};

struct Settings
{
    synth::Wave wave = synth::Wave::supersaw;
    float detune = 0.4f;          // 0..1
    int octave = 0;               // -2..2
    float cutoff = 2000.0f;       // Hz
    float resonance = 0.3f;
    float envMod = 0.4f;
    float filterDecay = 0.4f;     // s
    float attack = 0.005f, decay = 0.3f, sustain = 0.7f, release = 0.3f;
    float lfoDepth = 0.0f;
    int lfoRate = 5;              // SyncRates index (1/8)
    synth::LfoShape lfoShape = synth::LfoShape::sine;
    float volume = 0.7f;

    bool arpOn = false;
    ArpMode arpMode = ArpMode::up;
    int arpRate = 6;              // SyncRates index (1/16)
    int arpOctaves = 1;
    float arpGate = 0.5f;
    bool latch = false;
};

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
    void setRhythm (const Rhythm& r) { rhythm = r; }

    void render (float* left, float* right, int numSamples, const juce::MidiBuffer&, const Clock&);

    int getArpStep() const { return arpStep; }
    int getActiveVoices() const;

    // Optional: every note the synth plays (keys and arp) is also written here, at its sample
    // in the block, so another instrument can play them (the Rig's plugin slot).
    juce::MidiBuffer* noteTap = nullptr;

private:
    int tapSample = 0;
    struct Voice
    {
        int note = -1;
        float velocity = 1;
        std::array<synth::Oscillator, 3> osc;
        synth::LowPass filter;
        synth::Envelope amp, filterEnv;
        long long started = 0;
        bool held = false;
    };

    void voiceOn (int note, float velocity);
    void voiceOff (int note);
    void handleMidi (const juce::MidiMessage&);
    void keyDown (int note, float velocity);
    void keyUp (int note);
    int nextArpNote();
    void arpTick (long long stepIndex, double beat, double stepBeats);

    Settings settings;
    Rhythm rhythm;
    double sampleRate = 44100.0;
    std::array<Voice, kVoices> voices;
    long long counter = 0;
    synth::Lfo lfo;
    int filterCounter = 0;

    // Keys held (in the order played), and the arp's set of notes.
    std::array<int, 32> held {};
    int numHeld = 0;
    std::array<int, 32> arpNotes {};
    int numArp = 0;
    bool latchedSet = false;

    long long lastArpIndex = -1;
    int arpStep = -1;
    int arpPosition = -1;
    int arpDirection = 1;
    int arpPlaying = -1;
    double arpOffBeat = 0;
    double freeClock = 0;
    juce::Random random { 99 };
};
}
