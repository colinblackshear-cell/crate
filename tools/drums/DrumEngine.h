#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>
#include <cstdint>

// The Drum Machine: a classic analog-style rhythm box. Every sound is made from oscillators,
// noise and filters, the way the early-80s machines did it (no samples): a deep booming kick,
// a snappy snare, clap, rim shot, three toms, clave, cowbell, closed and open hats (the closed
// hat cuts the open one off) and a cymbal.
//
// A pattern is a grid of steps (up to 32) for the 12 sounds, with an accent row that makes a
// whole step hit harder. A hit can be a roll (2, 3 or 4 quick hits in its step). Swing pushes
// every second step late. A fill pattern can take over every few times round, or once when
// you press Fill.
//
// Like the classic box it's modelled on, every pattern has an A and a B variation, which play
// as A, B, or A then B (a two-bar beat). An intro can play once when you press Play, and a
// chain plays patterns one after another (the old "rhythm track"). Each sound has its own
// knobs (level, tune, decay, tone, snappy, pan), a mute, and a choice of sounds: the toms
// switch to congas, the clap to maracas, the kick to a long sub boom, and more.
//
// The keys and the pads play the sounds too, from C (kick) up the octave; any octave works.
namespace crate::drums
{
constexpr int kVoices = 12;
constexpr int kMaxSteps = 32;
constexpr int kPatterns = 16;
constexpr int kChain = 32;   // patterns in the chain

enum Voice { kick, snare, clap, rim, lowTom, midTom, highTom, clave, cowbell, closedHat, openHat, cymbal };

// Names for the panel and short ones for small spaces.
const char* voiceName (int voice);
const char* voiceShort (int voice);
// The note that plays a voice (C2 = 36 is the kick; the usual drum-map notes where they fit:
// rim C#, snare D, clap D#, closed hat F#, open hat A#), and the voice a note plays (any
// octave), or -1.
int noteForVoice (int voice);
int voiceForNote (int note);

// Each sound's choice of sounds ("Classic", "Long boom"...), the first being the classic one.
juce::StringArray soundNames (int voice);

struct Pattern
{
    // 0 = no hit, 1 = a hit, 2-4 = a roll of that many hits in the step.
    std::array<std::array<std::uint8_t, kMaxSteps>, kVoices> hits {};
    std::array<bool, kMaxSteps> accent {};
    int length = 16;   // 1..32 steps

    bool isEmpty() const;
    bool operator== (const Pattern&) const;
};

struct VoiceSettings
{
    float level = 0.8f;   // 0..1
    float tune = 0.0f;    // semitones, -12..12
    float decay = 0.5f;   // 0..1, short to long (each sound has its own range)
    float tone = 0.5f;    // 0..1: kick sweep, snare colour, hat brightness...
    float snappy = 0.4f;  // 0..1: the snare's snares; the attack (click, skin, tick) on the others
    float pan = 0.0f;     // -1..1
    int sound = 0;        // which of soundNames (voice)
    bool mute = false;
};

struct Settings
{
    std::array<VoiceSettings, kVoices> voices {};
    float accent = 0.5f;     // 0..1: how much harder accented steps hit
    float swing = 0.0f;      // 0..1: every second step later, up to a heavy shuffle
    float drive = 0.0f;      // 0..1: analog-style saturation on the whole kit
    float volume = 0.8f;     // 0..1
    float humanize = 0.0f;   // 0..1: hits a little softer or harder at random
    int stepRate = 1;        // 0 1/8, 1 1/16, 2 1/16 triplets, 3 1/32, 4 1/8 triplets
    bool sequencerOn = true;
    int fillEvery = 0;       // 0: off; else the fill plays every Nth time round
};

double stepBeats (int stepRate);
juce::StringArray stepRateNames();

// Which patterns play, when the engine has the whole bank (DrumProcessor's 16 patterns, A and B).
struct Plan
{
    enum Variation { a, b, aThenB };
    int pattern = 0;          // 0..15
    int variation = a;
    int fill = -1;            // the fill's pattern (its A), or -1
    int intro = -1;           // plays once when the clock starts from the top, or -1
    bool chainOn = false;
    int chainLength = 0;
    // Each entry: pattern * 2 + (1 for its B).
    std::array<std::int8_t, kChain> chain {};

    bool sameRun (const Plan& o) const;   // the same patterns playing (so no need to jump)
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
    Engine();
    void prepare (double sampleRate);
    void reset();
    void setSettings (const Settings& s) { settings = s; }
    const Settings& getSettings() const { return settings; }

    // Audio thread: the patterns to play (kept alive by the caller until they're replaced).
    // `fill` may be null.
    void setPatterns (const Pattern* main, const Pattern* fill) { pattern = main; fillPattern = fill; bankA = bankB = nullptr; }
    // Or the whole bank: `a` and `b` are kPatterns patterns each, and the plan says which play.
    void setBank (const Pattern* a, const Pattern* b, const Plan& p) { bankA = a; bankB = b; plan = p; }
    // Plays the fill once, the next time the pattern comes round to its start.
    void requestFill() { fillOnce = true; }

    // Plays one block: the keys (MIDI), the pattern if the clock plays, into `left` and `right`
    // (added to what's there).
    void render (float* left, float* right, int numSamples, const juce::MidiBuffer&, const Clock&);

    // Plays one voice now (the window's pads).
    void trigger (int voice, float velocity, bool accent);

    // For the window.
    int getCurrentStep() const { return currentStep; }
    bool isPlayingFill() const { return inFill; }
    bool isPlayingIntro() const { return inIntro; }
    // The pattern playing (pattern * 2 + 1 for its B), -1 for none; and the chain's place.
    int getCurrentCode() const { return currentCode; }
    int getChainPosition() const { return chainPos; }
    // Counts up each time a voice sounds, so the window can flash it.
    std::array<std::atomic<int>, kVoices> hitCounts {};

    // Optional: every hit is also written here as a note at its sample in the block (the Rig's
    // plugin slot and recorder can use it).
    juce::MidiBuffer* noteTap = nullptr;

private:
    struct Svf
    {
        float g = 0, k = 1, ic1 = 0, ic2 = 0;
        void set (float hz, float q, float sampleRate);
        // Returns low, band, high.
        void process (float x, float& lp, float& bp, float& hp);
        void reset() { ic1 = ic2 = 0; }
    };

    struct VoiceState
    {
        bool active = false;
        float t = 0;            // seconds since the hit
        float gain = 0;         // velocity, accent, level
        float env = 0, env2 = 0, envCoeff = 0, env2Coeff = 0;
        double phase[6] {};
        float freqs[6] {};
        Svf f1, f2;
        float pitch = 0;        // base Hz
        float clickEnv = 0;
        float lastOut = 0;
        double bell = 0;        // the ride's bell
        int sound = 0;          // the sound picked when it was hit
    };

    void start (int voice, float velocity, bool accent);
    float renderVoice (int voice, VoiceState&);
    float noise();
    void handleMidi (const juce::MidiMessage&);
    void hitStep (int step, const Pattern&, double stepStartBeat, double stepLength);
    const Pattern* byCode (int code) const;
    const Pattern* basePattern (long long loop, int& code) const;
    void resync (long long index);
    void nextLoop();
    void tap (int voice, float velocity);

    Settings settings;
    const Pattern* pattern = nullptr;
    const Pattern* fillPattern = nullptr;
    const Pattern* bankA = nullptr;
    const Pattern* bankB = nullptr;
    Plan plan, lastPlan;
    const Pattern* lastSimple = nullptr;
    double sampleRate = 44100.0;
    float invRate = 1.0f / 44100.0f;
    std::array<VoiceState, kVoices> state {};
    std::uint32_t seed = 0x1234567u;
    int tapSample = 0;

    // Sequencer.
    long long lastFired = -1;
    long long loopCount = 0;
    const Pattern* current = nullptr;
    int position = 0;
    int currentStep = -1;
    int currentCode = -1;
    int chainPos = 0;
    bool inFill = false, inIntro = false;
    std::atomic<bool> fillOnce { false };
    bool fillQueued = false;
    bool wasPlaying = false;
    struct Roll { int left = 0; double next = 0, gap = 0; float velocity = 0; bool accent = false; };
    std::array<Roll, kVoices> rolls {};
    float driveState = 0;
};
}
