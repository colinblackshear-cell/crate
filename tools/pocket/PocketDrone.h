#pragma once

#include "BeatsEngine.h"
#include "PocketLooper.h"
#include "core/SafetyLimiter.h"
#include "drone/DroneEngine.h"

#include <array>
#include <atomic>

// The DRONE tool: the Mac's Drone box on the phone (the same engine, tanpura strings, reeds and the same
// ready-made drones), inside the looper like SYNTH and CHORDS. Sing into the mic and its strings ring
// along (Resonate). Its settings are atomics: the screen writes them, the audio thread reads them.
namespace pocket
{
// What the strings ring to (the Mac's "Rings to", the sources the phone has).
enum class DroneFeed { voice, vocoder, drums, synth, chords, bass, count };
const char* droneFeedName (DroneFeed);

class DroneTool : public DroneVoice
{
public:
    explicit DroneTool (BeatsEngine&);

    // The settings (message thread writes, audio thread reads).
    std::atomic<bool> on { false };
    std::atomic<int> root { 0 }, octave { 0 }, first { 0 }, sync { 0 }, feed { (int) DroneFeed::voice };
    std::atomic<float> pace { 1.1f }, jawari { 0.6f }, ring { 0.5f }, fine { 0.0f }, width { 0.7f }, strings { 1.0f }, reeds { 0.0f },
                       resonate { 0.0f };
    // The pattern: the order the strings go in, or steps you draw (stepCount 0 off, 1 = 8 steps, 2 = 16; each step's
    // bits 0-3 the strings it plucks; stepRate 0 = 1/4, 1 = 1/8, 2 = 1/16), Swing and Human. The tone: the
    // instrument the strings are, Bright, Body, Detune and Space. (As the Mac's.)
    std::atomic<int> order { 0 }, stepCount { 0 }, stepRate { 1 }, voice { 0 };
    std::array<std::atomic<std::uint8_t>, 16> steps {};
    std::atomic<float> swing { 0.0f }, human { 0.0f }, bright { 0.5f }, body { 0.5f }, detune { 0.0f }, space { 0.0f };
    void setStepCount (int choice);   // turning steps on with none drawn draws a first pattern to start from
    void toggleStep (int step, int string);
    void clearSteps();
    int currentStep() const { return engine.currentStep.load(); }
    // MIRACLE TONES (as the Mac's): the tone picked (0-11, 11 = your own Hz, ownHz), whether it plays, FIT (0 over the
    // drone, 1 the drone tuned to it), SOUND (0 pure, 1 warm, 2 bowl), BINAURAL (0 off, 1-3: 2, 6, 10 Hz), LEVEL, SWELL,
    // TONE ONLY (the strings rest).
    std::atomic<bool> toneOn { false }, toneOnly { false };
    std::atomic<int> tone { 5 }, toneFit { 0 }, toneSound { 0 }, toneBeat { 0 };
    std::atomic<float> ownHz { 111.0f }, toneLevel { 0.6f }, toneSwell { 0.3f };
    double toneHz() const { return crate::drone::toneHzFor (tone.load(), ownHz.load()); }
    // Picking the tone that's playing turns it off; another plays it (and turns the drone on). Returns true if it plays.
    bool pickTone (int index);
    void setOwnToneHz (double hz);   // plays it, as YOUR OWN
    bool toneSounding() const { return engine.isToneSounding(); }
    juce::String stringNoteNow (int string) const;   // as stringNote, tuned to the tone when FIT tunes the drone to it
    // Its volume, as the Mac's fader: 0.8 is 0 dB, the top +6 dB.
    std::atomic<float> level { 0.8f };
    // The drone's note follows the song's KEY (on until you pick a note yourself).
    bool followKey = true;

    static float faderGain (float level);
    static juce::String volumeText (float level);   // "-3.0 dB"
    static juce::String noteName (int root);        // C, C#, D... (as the Mac names them)

    void loadPreset (int index);
    int presetIndex() const;   // the ready-made drone the settings are, or -1
    juce::String presetName() const;
    void pluck (int string) { engine.pluck (juce::jlimit (0, crate::drone::kStrings - 1, string)); plucked = true; }
    int pluckCount (int string) const { return engine.pluckCounts[(size_t) string].load(); }
    float ringing (int string) const { return engine.ringing[(size_t) string].load(); }
    float resonating() const { return engine.resonating.load(); }
    juce::String stringNote (int string) const;   // "G2"
    void setSongKey (int keyRoot) { if (followKey) root = juce::jlimit (0, 11, keyRoot); }

    void reset();   // a new song: off, the Classic drone, on the song's note
    juce::var toVar() const;
    void fromVar (const juce::var&, int songRoot);

    // DroneVoice / Instrument (audio thread)
    int feedSource() const override { return feed.load(); }
    void setFeed (const float* mono) override { feedIn = mono; }
    void prepare (double sampleRate) override;
    void render (float* left, float* right, int numSamples) override;
    // Renders without a sound card (the tests and screenshots).
    void renderAt (float* left, float* right, int numSamples, bool running, double beat, double bpm);

private:
    crate::drone::Settings read() const;

    BeatsEngine& beats;
    crate::drone::Engine engine;
    crate::SafetyLimiter limiter;
    std::vector<float> outL, outR;
    const float* feedIn = nullptr;
    std::atomic<bool> plucked { false };
    float gainNow = 0.8f;
    double rate = 44100.0;
};
}
