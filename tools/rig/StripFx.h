#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginSlot.h"

// A rack of live effects on one of the Rig's mixer strips: a DJ-style filter (one knob: left
// is low-pass, right is high-pass, the middle is off), drive, bit crush, a delay and a beat
// repeat in time with the Rig's tempo, and reverb. Every setting is a parameter, so the Rig's
// knob Learn can put it on a knob of your keyboard and it's saved with the show.
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StripFx)
};
