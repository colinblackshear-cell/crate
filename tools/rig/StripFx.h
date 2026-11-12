#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginSlot.h"

// A rack of live effects on one of the Rig's mixer strips (and on each loop): a pedalboard.
// In the order the sound goes through them:
//
//   Gain in, Gate, Comp, Wah (an envelope filter), Filter (DJ style: one knob, left is
//   low-pass, right is high-pass, the middle is off), Drive + Crush, Amp (clean, crunch, lead,
//   fuzz or a bass amp, with its cabinet), EQ, Chorus, Phaser, Tremolo, Delay and Repeat in
//   time with the Rig's tempo, Reverb, then your own plugin.
//
// Every setting is a parameter, so the Rig's knob Learn can put it on a knob of your keyboard
// and it's saved with the show. (The pedals added later come last in the parameter list, so
// learned knobs and saved shows keep theirs.) Bypass switches the whole rack out at once.
//
// All effects at their defaults leave the sound untouched.
class StripFx : public juce::AudioProcessor
{
public:
    StripFx();

    juce::AudioProcessorValueTreeState params;

    // A plugin of your own (any installed effect: Quake, a Valhalla...) after the rack.
    PluginSlot plugin { false };
    void setPlayHead (juce::AudioPlayHead*) override;

    // Anything turned up? (The window lights the strip's FX button.)
    bool isActive() const;
    // Every effect back to its default (off).
    void reset();

    static const juce::StringArray& delayNames();    // 1/16 ... 1/2
    static const juce::StringArray& repeatNames();   // Off, 1/4 ... 1/32
    static const juce::StringArray& ampNames();      // Clean, Crunch, Lead, Fuzz, Bass amp
    static const juce::StringArray& tremoloNames();  // 1/4, 1/8, 1/8 triplet, 1/16

    // The tuner: while it's listening, the rack keeps the last few thousand samples of what
    // comes in (after Gain in, before the pedals) for the window to read. Any thread.
    std::atomic<bool> tunerListening { false };
    static constexpr int kTunerSize = 4096;
    // The newest `count` samples (mono), oldest first. Message thread; a block may land while
    // it copies, which only matters for a moment of the tuner's display.
    void readTuner (float* dest, int count) const;
    double getSampleRate() const { return rate; }

    // How hard the gate and compressor are working now (0..1, for the pedals' lamps).
    float getGateOpen() const { return gateOpenNow.load(); }
    float getCompReduction() const { return compReductionNow.load(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    const juce::String getName() const override { return "FX"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    double currentBpm() const;

    double rate = 44100.0;

    // Filter: a state-variable filter per side.
    struct Svf
    {
        float ic1 = 0, ic2 = 0;
        void clear() { ic1 = ic2 = 0; }
    };
    std::array<Svf, 2> svf;
    juce::SmoothedValue<float> filterSmooth, resonanceSmooth;

    // Crush: sample-and-hold state.
    std::array<float, 2> held {};
    float holdCount = 0;

    // Delay: a ping-pong delay line of up to 4 seconds.
    juce::AudioBuffer<float> delayLine;
    int delayWrite = 0;
    juce::SmoothedValue<float> delaySamples;
    std::array<float, 2> feedbackLow {};

    // Repeat: catches one slice of the beat, then loops it while it's on.
    juce::AudioBuffer<float> repeatBuffer;
    int repeatChoice = 0, repeatLength = 0, repeatPos = 0;
    bool repeatFilled = false;
    juce::SmoothedValue<float> repeatMix;

    juce::Reverb reverb;
    juce::AudioBuffer<float> wet;
    juce::MidiBuffer noPluginMidi;

    std::atomic<float>* filter = nullptr;
    std::atomic<float>* resonance = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* crush = nullptr;
    std::atomic<float>* delayTime = nullptr;
    std::atomic<float>* feedback = nullptr;
    std::atomic<float>* delayMix = nullptr;
    std::atomic<float>* reverbSize = nullptr;
    std::atomic<float>* reverbMix = nullptr;
    std::atomic<float>* repeat = nullptr;

    // The pedals added for the pedalboard.
    std::atomic<float>* inputGain = nullptr;
    std::atomic<float>* bypass = nullptr;
    std::atomic<float>* gateOn = nullptr;
    std::atomic<float>* gateThreshold = nullptr;
    std::atomic<float>* compOn = nullptr;
    std::atomic<float>* compAmount = nullptr;
    std::atomic<float>* compLevel = nullptr;
    std::atomic<float>* wahOn = nullptr;
    std::atomic<float>* wahSense = nullptr;
    std::atomic<float>* wahRange = nullptr;
    std::atomic<float>* wahPeak = nullptr;
    std::atomic<float>* ampOn = nullptr;
    std::atomic<float>* ampType = nullptr;
    std::atomic<float>* ampGain = nullptr;
    std::atomic<float>* ampTone = nullptr;
    std::atomic<float>* ampLevel = nullptr;
    std::atomic<float>* eqOn = nullptr;
    std::atomic<float>* eqLow = nullptr;
    std::atomic<float>* eqMid = nullptr;
    std::atomic<float>* eqHigh = nullptr;
    std::atomic<float>* chorusOn = nullptr;
    std::atomic<float>* chorusMix = nullptr;
    std::atomic<float>* chorusRate = nullptr;
    std::atomic<float>* chorusDepth = nullptr;
    std::atomic<float>* phaserOn = nullptr;
    std::atomic<float>* phaserMix = nullptr;
    std::atomic<float>* phaserRate = nullptr;
    std::atomic<float>* phaserDepth = nullptr;
    std::atomic<float>* phaserFeedback = nullptr;
    std::atomic<float>* tremOn = nullptr;
    std::atomic<float>* tremDepth = nullptr;
    std::atomic<float>* tremRate = nullptr;
    std::atomic<float>* tremShape = nullptr;

    void processDynamics (float* left, float* right, int n);
    void processWah (float* left, float* right, int n);
    void processAmp (float* left, float* right, int n);
    void processEq (juce::AudioBuffer<float>&, int n);
    void processModulation (float* left, float* right, int n, double bpm);
    void processRepeat (float* left, float* right, int n, double bpm);
    double currentPpq (bool& known) const;

    // Tuner.
    std::vector<float> tunerRing = std::vector<float> ((size_t) kTunerSize, 0.0f);
    std::atomic<int> tunerWrite { 0 };

    // Gate and comp.
    float gateEnv = 0, gateGain = 1, compEnv = 0, compGain = 1;
    std::atomic<float> gateOpenNow { 1.0f }, compReductionNow { 0.0f };
    juce::SmoothedValue<float> trimSmooth;

    // Wah: an envelope follower opens a resonant band-pass per side.
    float wahEnv = 0;
    std::array<Svf, 2> wahSvf;

    // Amp: drive into a shaper, its tone, and the cabinet (a high-pass and two low-passes).
    std::array<float, 2> ampPre {}, ampTone1 {}, ampCab1 {}, ampCab2 {}, ampDc {};

    // EQ.
    std::array<juce::IIRFilter, 2> eqLowF, eqMidF, eqHighF;
    float eqShown[3] { 0, 0, 0 };
    bool eqWasOn = false;

    // Chorus: a short modulated delay; phaser: six all-passes swept by an LFO.
    juce::AudioBuffer<float> chorusLine;
    int chorusWrite = 0;
    double chorusPhase = 0, phaserPhase = 0, tremFreePhase = 0;
    std::array<std::array<float, 6>, 2> phaserState {};
    std::array<float, 2> phaserLast {};
    juce::SmoothedValue<float> chorusAmount, phaserAmount, tremAmount;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripFx)
};
