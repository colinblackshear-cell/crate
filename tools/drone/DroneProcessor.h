#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/SafetyLimiter.h"

#include "DroneEngine.h"

// The Drone box as a tool in the Rig: an electronic tanpura (four plucked strings with the
// jawari's bloom, tuned to the song's note), reeds under it, and strings that ring to whatever is
// fed into them. Every setting is a parameter, so it can go on knobs, macros and automation.
class DroneProcessor : public juce::AudioProcessor
{
public:
    DroneProcessor();

    juce::AudioProcessorValueTreeState params;

    // What the ringing strings listen to (the "droneFeed" choice, in this order).
    enum Feed { feedInput, feedVocoder, feedBass, feedSynth, feedPads, feedSampler, feedDrums, feedAll, numFeeds };
    static juce::StringArray feedNames();
    static juce::StringArray rootNames();
    int getFeed() const;
    bool isOn() const;
    // True while it has something to play: on, ringing out, or strings set to ring to the feed.
    bool wantsToPlay() const;

    // Ready-made drones (Classic Pa, Ma for evening ragas, Deep and slow, Bright jawari, Reeds only...).
    static juce::StringArray presetNames();
    void loadPreset (int index);

    static constexpr float kBedGain = crate::drone::kBedGain;

    // Miracle tones: the tone picked (0-11, 11 = your own), its Hz, and whether it's playing. Picking the tone
    // that's playing turns it off; picking another plays it (and turns the drone on).
    int getTone() const;
    double getToneHz() const;
    bool isToneOn() const;
    void pickTone (int index);
    void setOwnToneHz (double hz);   // plays it, as Your own
    bool isToneSounding() const { return engine.isToneSounding(); }

    // Steps: what each of the 16 steps plucks (bits 0-3: the strings, 0 a rest). Saved with the drone's
    // settings; read by the audio thread.
    int getStep (int step) const;
    void setStep (int step, int mask);
    void clearSteps();
    // Off (0), 8 steps (1) or 16 (2); turning them on with none drawn draws a first pattern to start from.
    void setStepCount (int choice);
    int getCurrentStep() const { return engine.currentStep.load(); }

    // Plucks a string from the screen (0-3).
    void pluck (int string) { engine.pluck (string); }
    int getPluckCount (int string) const { return engine.pluckCounts[(size_t) string].load(); }
    float getRinging (int string) const { return engine.ringing[(size_t) string].load(); }
    float getResonating() const { return engine.resonating.load(); }
    // The four strings' notes as they are now, named as Logic names them ("G1", "C2", "C2", "C1").
    juce::StringArray stringNotes() const;

    // As processBlock, with what the strings should ring to (null: nothing).
    void process (juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>* feed, juce::MidiBuffer&);

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override { process (buffer, nullptr, midi); }
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "Drone"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    crate::drone::Settings readSettings() const;

    void stepsFromState();

    crate::drone::Engine engine;
    crate::SafetyLimiter limiter;
    std::array<std::atomic<std::uint8_t>, 16> steps {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DroneProcessor)
};
