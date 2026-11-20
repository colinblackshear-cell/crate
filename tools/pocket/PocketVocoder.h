#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "BeatsEngine.h"
#include "PocketAudio.h"

#include "PluginProcessor.h"   // the Kaleidobox vocoder (its own repo), driven here with the phone's mic

#include <atomic>

// The Vocoder tab's engine: the real Kaleidobox vocoder processor, with the mic as the voice and its own
// synth, playing the chords you tap, as the carrier. It is a PocketAudio::Renderer, so it runs on the
// audio thread inside LoopsEngine (the one renderer): in time with the shared clock, and its sound is
// what the Loops tab's VOCODER source records. Nothing in render() allocates or waits.
namespace pocket
{
constexpr int kPedals = 14;
enum class Pedal { voice, tune, harmony, robot, character, synth, chords, arp, filter, vintage, dirt, chorus, echo, space };

// How a pedal knob's number reads under it.
enum class KnobText { percent, text, semitones };
struct KnobDef { const char* id; const char* label; KnobText text; };
struct PedalDef
{
    const char* name;
    const char* about;        // "your voice shapes the synth"
    juce::uint32 colour;
    int knobs;                // how many of the (up to) four knobs it has
    KnobDef knob[4];
};
const PedalDef& pedalDef (int pedal);

class VocoderEngine : public PocketAudio::Renderer, private juce::AudioPlayHead
{
public:
    explicit VocoderEngine (BeatsEngine&);
    ~VocoderEngine() override;

    VocoderProcessor& processor() { return proc; }

    //==========================================================================
    // Message thread
    // Sounds: the vocoder's own factory presets.
    int presetCount() const;
    juce::String presetName (int) const;
    juce::String presetCategory (int) const;
    int preset() const;
    void setPreset (int);
    void stepPreset (int delta);

    // Pedals: the Mac's thirteen. On and off the way a footswitch does it.
    bool pedalOn (int pedal) const;
    void stomp (int pedal);
    int pedalsOn() const;
    int knobCount (int pedal) const { return pedalDef (pedal).knobs; }
    float knob01 (int pedal, int knob) const;               // where the knob sits, 0..1
    void setKnob01 (int pedal, int knob, float value01);
    void resetKnob (int pedal, int knob);
    juce::String knobText (int pedal, int knob) const;

    // Chords of the key. Tap one to hold it as the carrier; tap it again to let go.
    int key() const;           // 0 = C ... 11 = B
    bool minor() const;
    void setKey (int key, bool minor);
    static juce::String keyName (int key);                       // "C", "A♭"
    juce::String keyLabel() const;                               // "C MINOR"
    juce::String chordName (int degree) const;                   // "Cm", "Dm°", "E♭"
    int activeChord() const { return chord; }                    // -1 = none
    void pressChord (int degree);
    // Sing's eight chord pads: the seven chords of the key, then the dominant seventh (V7).
    static constexpr int kChordPads = 8;
    juce::Array<int> padNotes (int pad) const;
    juce::String padName (int pad) const;          // "Am", "E7"
    juce::String padNumeral (int pad) const;       // "i", "V7"
    void pressPad (int pad);                       // the same pad again lets go
    bool drone() const;
    void toggleDrone();

    // Hearing yourself. Off until asked: the mic through the speaker would squeal.
    void setMonitor (bool on) { monitor = on; }
    bool monitoring() const { return monitor.load(); }
    const std::atomic<bool>& monitorFlag() const { return monitor; }

    float takeMicLevel() { return micPeak.exchange (0.0f); }
    float outputLevel() const { return proc.getOutputLevel(); }

    // REC A TAKE: the vocoder's sound to a WAV, until stopTake().
    bool startTake (const juce::File&);
    void stopTake();
    bool taking() const { return recording.load(); }
    double takeSeconds() const { return (double) takenSamples.load() / sampleRate; }

    // Saved to Documents/Vocoder/vocoder.xml: the sound (every setting, so the preset and the pedals), the
    // key and the chord.
    void save (const juce::File&) const;
    void load (const juce::File&);
    bool consumeChanged() { return changed.exchange (false); }
    void markChanged() { changed = true; }

    // Runs the real engine on made-up voices and checks it makes sound that follows the voice, that the
    // chords and pedals do what they say, and that the VOCODER loop source records it. Returns what was
    // wrong (empty = fine).
    static juce::String selfCheck (juce::String& report);

    //==========================================================================
    // Audio thread
    void setInput (const float* const* in, int numIn) override { inPtr = in; inChannels = numIn; }
    void prepare (double rate) override;
    void render (float* left, float* right, int numSamples) override;

private:
    static constexpr int kBlock = 1024;

    juce::Optional<PositionInfo> getPosition() const override;
    float param (const char* id) const;
    void setParam (const char* id, float value);
    void setParamNormalised (const char* id, float value01);
    juce::Array<int> notesOf (int degree) const;
    void requestChord (const juce::Array<int>& notes);
    void ensureChord();

    BeatsEngine& beats;
    mutable VocoderProcessor proc;
    double sampleRate = 44100.0;

    std::atomic<bool> monitor { false };
    std::atomic<float> micPeak { 0.0f };
    std::atomic<bool> changed { false };
    int chord = -1;

    // what a pedal comes back as when it's stomped on again
    float keptFormant = -7.0f, keptDrive = 0.35f, keptCrush = 0.0f, keptChorus = 0.4f, keptEcho = 0.3f, keptSpace = 0.3f,
          keptCutoff = 1500.0f, keptFilterMode = 1.0f, keptHarmony = 3.0f;

    // audio thread
    const float* const* inPtr = nullptr;
    int inChannels = 0;
    std::array<std::vector<float>, 2> block;
    float* channels[2] = { nullptr, nullptr };
    juce::MidiBuffer midi;

    juce::TimeSliceThread writerThread { "Pocket vocoder take" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::SpinLock writerLock;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> takenSamples { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocoderEngine)
};
}
