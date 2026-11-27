#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "PocketAudio.h"
#include "core/Audio.h"
#include "drums/DrumEngine.h"

#include <array>
#include <atomic>

// Everything that plays on the Beats tab, in one audio-thread renderer: the Mac app's 808 engine
// stepping through the patterns, and the 64 MPC pads playing samples. The drum machine and the pads
// share one clock and one tempo. The screens talk to it from the message thread: edits are
// published under a try-lock, hits go through a lock-free queue, and nothing in render() allocates.
namespace pocket
{
constexpr int kPatternSlots = 6;
constexpr int kPads = 64;

enum class PadMode { oneShot, mono, noteOff };

// The pads played into a pattern (REC PADS): each hit is a pad at a beat inside the pattern.
constexpr int kMaxPadHits = 512;
// How one hit differs from its pad's own settings (16 LEVELS, as on the MPC2000XL): its pitch, its filter, its decay and
// its attack. All zero = the pad as it is. tune is tenths of a semitone added to the pad's tune; cutoff (Hz) opens the low-pass
// filter (0 = no filter); decay (seconds) fades the sound out after that long (0 = the whole sound); attack (seconds) fades it in.
struct PadMods
{
    int tune = 0;
    float cutoff = 0.0f, decay = 0.0f, attack = 0.0f;
    bool any() const { return tune != 0 || cutoff > 0.0f || decay > 0.0f || attack > 0.0f; }
    bool operator== (const PadMods& o) const
    {
        return tune == o.tune && std::abs (cutoff - o.cutoff) < 1.0e-4f && std::abs (decay - o.decay) < 1.0e-6f && std::abs (attack - o.attack) < 1.0e-6f;
    }
};

struct PadHit
{
    int pad = 0;
    double beat = 0;          // from the pattern's start
    float velocity = 1.0f;
    PadMods mods;             // 16 LEVELS: how this hit differs from its pad (none = the pad as it is)
    int skipRun = -1;         // just played by hand: not played again by the pattern in this run of the clock
    double skipUntil = 0;     // ... before this beat
};
struct PadPattern
{
    int bars = 2;             // 1, 2 or 4
    int quantize = 4;         // hits per beat it snaps to (4 = 1/16, 2 = 1/8, 0 = off)
    std::vector<PadHit> hits;
    double lengthBeats() const { return bars * 4.0; }
    juce::var toVar() const;
    static PadPattern fromVar (const juce::var&);
};

// The drum machine's whole state, as the screens edit it and as it's saved to Documents.
struct DrumData
{
    std::array<std::array<crate::drums::Pattern, 2>, kPatternSlots> patterns {};   // [slot][A or B]
    crate::drums::Settings settings;
    int slot = 0;          // the pattern that plays
    int variation = 0;     // 0 A, 1 B, 2 A then B (crate::drums::Plan::Variation)
    bool editB = false;    // the grid edits the B variation
    double bpm = 97.0;
    int repeatRate = 3;    // NOTE REPEAT's rate: an index into noterepeat::kRates (3 = 1/16)
    PadPattern padPattern;
    // Your own kit: the id (in the phone's sound library) of the sound on each of the twelve drum sounds;
    // empty = its analog sound. The sounds themselves stay in the library, so an idea reopens with its kit.
    std::array<juce::String, crate::drums::kVoices> sampleIds {};

    DrumData();
    crate::drums::Pattern& edited() { return patterns[(size_t) slot][editB ? 1 : 0]; }
    const crate::drums::Pattern& edited() const { return patterns[(size_t) slot][editB ? 1 : 0]; }

    juce::var toVar() const;
    static DrumData fromVar (const juce::var&);
    static crate::drums::Pattern demoPattern();
};

struct PadSetup
{
    crate::SharedAudio audio;   // null = empty pad
    int volume = 100;           // 0..100
    int pan = 50;               // 0..100
    int tune = 0;               // tenths of a semitone
    PadMode mode = PadMode::oneShot;
    float cutoff = 0.0f, decay = 0.0f, attack = 0.0f;   // the pad's own filter (Hz, 0 = none), decay and attack (seconds, 0 = none)
};

class BeatsEngine : public PocketAudio::Renderer
{
public:
    BeatsEngine();

    // Message thread
    void setDrums (const DrumData&);
    // A sound of your own on one of the twelve drum sounds (nullptr: its analog sound again). The engine keeps
    // every sound it was given until it goes (a hit may still be playing one).
    void setDrumSample (int voice, crate::drums::SharedSample);
    crate::drums::SharedSample getDrumSample (int voice) const;
    void setPlaying (bool);
    bool isPlaying() const { return wantPlay.load(); }
    void setBpm (double);
    double getBpm() const { return bpm.load(); }
    int currentStep() const { return step.load(); }            // -1 when stopped
    int playingVariation() const { return playingB.load(); }   // 0 A, 1 B
    void hitVoice (int voice, float velocity = 0.85f, bool accent = false);
    // A hit played by hand just before its step line, already written into the pattern: the grid skips that
    // voice on that step once (until `untilBeat` on the clock), so the hit isn't heard twice. `slot` and `b`
    // say which pattern holds it, `cell` the step in it.
    void skipStepOnce (int voice, int slot, bool b, int cell, double untilBeat);
    int voiceHits (int voice) const { return drums.hitCounts[(size_t) juce::jlimit (0, crate::drums::kVoices - 1, voice)].load(); }

    // The shared clock. It runs while the drum machine plays or while something else (the looper)
    // holds it; the drums only sound while they are playing. Any thread.
    void holdClock (bool on) { clockHeld = on; }
    bool clockRunning() const { return clockOn.load(); }
    double clockBeat() const { return clockPpq.load(); }   // beats at the start of the last block rendered

    void setPad (int pad, const PadSetup&);
    void padOn (int pad, float velocity = 1.0f, PadMods mods = {});
    void padOff (int pad);
    void stopPads();
    juce::uint64 soundingPads() const { return sounding.load(); }

    // NOTE REPEAT (the REPEAT strip): while it's on, every pad held down (padOn without its padOff) plays again at the
    // rate. The press itself is the first hit; repeats start at the next position of the rate on the shared clock while it
    // runs, or count from the press at the tempo while it's stopped. Any thread.
    void setNoteRepeat (bool on) { repeatOn = on; }
    bool noteRepeatOn() const { return repeatOn.load(); }
    void setRepeatBeats (double beats) { repeatBeats = juce::jlimit (1.0 / 32.0, 4.0, beats); }   // a quarter note is 1 beat
    double getRepeatBeats() const { return repeatBeats.load(); }
    int repeatsPlayed() const { return repeatCount.load(); }   // how many repeats have sounded (for the self-check)
    // The repeats that sounded while the clock ran, for REC PADS to write into the pattern (the screen calls this on the
    // message thread; the audio thread writes them into a lock-free queue). `beat` is where on the clock each one fell.
    struct RepeatReport { int pad = 0; float velocity = 1.0f; double beat = 0; PadMods mods; };
    int takeRepeats (RepeatReport* dest, int max);

    // The pad pattern: plays on the shared clock (and runs it) while on. REC adds a click on each beat.
    void setPadPattern (const PadPattern&);
    void setPatternPlaying (bool on) { patternOn = on; }
    bool patternPlaying() const { return patternOn.load(); }
    void setClick (bool on) { clickOn = on; }
    bool clickIsOn() const { return clickOn.load(); }
    // The count-in: the click sounds on every beat before this one, even with the click off. -1 = no
    // count-in. Any thread (the looper sets it from the audio thread before each block).
    void setCountInClick (double untilBeat) { countInUntil = untilBeat; }
    // Each start of the clock is a new run (the clock counts from 0 again).
    int clockRun() const { return run.load(); }
    int patternHitsPlayed() const { return hitsPlayed.load(); }   // how many pattern hits have played (for the self-check)

    // Audio thread
    void prepare (double sampleRate) override;
    void render (float* left, float* right, int numSamples) override { renderWithClick (left, right, numSamples, nullptr); }
    // The same, with the click added into `clickOut` (mono, numSamples long) instead of left and right, so
    // whoever records the drums leaves it out. nullptr = in left and right. With `padsLeft` / `padsRight` the pads
    // are added there instead of into left and right (the drum machine and the pads each through its own board).
    void renderWithClick (float* left, float* right, int numSamples, float* clickOut, float* padsLeft = nullptr, float* padsRight = nullptr);

private:
    struct Event { enum Kind { drum, padOn, padOff, stopPads, skip } kind = drum; int index = 0; float velocity = 0; bool accent = false;
                   int slot = 0, cell = 0; double until = 0; PadMods mods; };
    struct Skip { int voice = 0, slot = 0, cell = 0; bool b = false; double until = -1; std::uint8_t kept = 0; };
    std::array<Skip, 8> skips {};
    int numSkips = 0;

    struct Slot
    {
        std::atomic<const juce::AudioBuffer<float>*> audio { nullptr };
        std::atomic<float> left { 1.0f }, right { 1.0f };
        std::atomic<int> tune { 0 }, mode { 0 };
        std::atomic<float> cutoff { 0.0f }, decay { 0.0f }, attack { 0.0f };
    };

    struct Voice
    {
        const juce::AudioBuffer<float>* audio = nullptr;
        double position = 0, step = 1;
        float gainL = 0, gainR = 0, release = 1.0f;
        int pad = -1;
        bool releasing = false;
        juce::uint32 age = 0;
        // 16 LEVELS: this hit's low-pass filter (two one-poles a channel), fade-in and fade-out, in samples
        bool shaped = false;
        float lpCoef = 0.0f, lp1L = 0, lp2L = 0, lp1R = 0, lp2R = 0;
        double played = 0, attackLen = 0, decayLen = 0;
    };

    void push (const Event&);
    void handle (const Event&);
    void renderClick (float* left, float* right, float* clickOut, int n, double startBeat, double beatsPerSample);
    void startPad (int pad, float velocity, const PadMods& mods = {});
    void renderPads (float* left, float* right, int n);

    // drums
    struct DrumState
    {
        std::array<crate::drums::Pattern, crate::drums::kPatterns> a, b;
        crate::drums::Settings settings;
    };
    crate::drums::Engine drums;
    std::array<crate::drums::SharedSample, crate::drums::kVoices> drumSamples;
    std::vector<crate::drums::SharedSample> keptDrumSamples;
    DrumState pending, live;
    int slotWanted = 0, variationWanted = 0;
    juce::SpinLock drumLock;
    std::atomic<int> drumVersion { 0 };
    int liveVersion = -1;
    std::atomic<int> wantedSlot { 0 }, wantedVariation { 0 };
    int playingSlot = 0;
    double ppq = 0, lastPpq = -1;
    bool wasRunning = false;
    juce::MidiBuffer noMidi;
    std::atomic<bool> wantPlay { false }, clockHeld { false }, clockOn { false }, patternOn { false }, clickOn { false };
    std::atomic<int> hitsPlayed { 0 };
    std::atomic<int> run { 0 };
    // the pad pattern: published under a try-lock like the drums
    std::array<PadHit, kMaxPadHits> pendingHits {}, liveHits {};
    int pendingCount = 0, liveCount = 0;
    double pendingLength = 8.0, liveLength = 8.0;
    juce::SpinLock patternLock;
    std::atomic<int> patternVersion { 0 };
    int livePatternVersion = -1;
    struct Trigger { int offset; int pad; float velocity; bool repeat; PadMods mods; };
    std::array<Trigger, 64> triggers {};
    // note repeat: the pads held down now, as the audio thread sees them
    struct Held { bool down = false; float velocity = 1.0f; double pressPpq = -1; long long pressSample = 0; PadMods mods; };
    std::array<Held, kPads> heldPads {};
    long long sampleClock = 0;
    std::atomic<bool> repeatOn { false };
    std::atomic<double> repeatBeats { 0.25 };
    std::atomic<int> repeatCount { 0 };
    juce::AbstractFifo reportFifo { 128 };
    std::array<RepeatReport, 128> reports {};
    float clickEnv = 0, clickPhase = 0, clickFreq = 1000;
    std::atomic<double> clockPpq { 0.0 };
    std::atomic<double> countInUntil { -1.0 };
    std::atomic<double> bpm { 97.0 };
    std::atomic<int> step { -1 };
    std::atomic<int> playingB { 0 };

    // pads
    std::array<Slot, kPads> slots;
    std::array<Voice, 16> voices;
    std::vector<crate::SharedAudio> keepAlive;   // replaced sounds stay until the engine goes: a voice may still read them
    std::atomic<juce::uint64> sounding { 0 };
    juce::uint32 voiceClock = 0;
    double sampleRate = 44100.0;

    juce::AbstractFifo fifo { 256 };
    std::array<Event, 256> events;
    juce::SpinLock pushLock;
};
}
