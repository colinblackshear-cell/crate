#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "RigRecorder.h"

#include <array>
#include <atomic>

// Records one tool into another: the bass onto an MPC pad, the synth into the Sampler, the mix
// onto a pad to chop... Press Sample and it records the source as you hear it, from the next bar
// (straight away if the Rig is stopped), for a number of bars or until you press again. Then the
// window hands the sound to where it's going.
//
// The buffer is made in prepare(), so the audio thread only ever copies into it.
class RigCapture
{
public:
    enum Source { mic, mpc, vocoder, bass, synth, input, sampler, loops, mix, drums, numSources };
    static juce::String sourceName (int);
    enum Dest { toSampler, toMpcPad, numDests };
    static juce::String destName (int);
    static const std::array<int, 6>& barChoices();   // 0 (until you press again), 1, 2, 4, 8, 16

    enum class State { idle, armed, recording, done };
    static constexpr double kMaxSeconds = 60.0;

    // Message thread (prepare while the audio is stopped).
    void prepare (double sampleRate);
    bool start (int source, int bars);   // when idle
    void stop();                         // ends it now (or cancels it before it starts)
    State getState() const { return (State) state.load(); }
    int getSource() const { return source; }
    int getBars() const { return bars; }
    double getRecordedSeconds() const { return written.load() / juce::jmax (1.0, rate); }
    // When done: the sound (at the device's rate) and how many beats long it is; back to idle.
    juce::AudioBuffer<float> take (double& sampleRate, double& beats);

    // Audio thread.
    void process (const RigRecorder::Clock&, const std::array<const juce::AudioBuffer<float>*, numSources>&, int numSamples);

private:
    std::atomic<int> state { (int) State::idle };
    std::atomic<bool> stopRequested { false };
    juce::AudioBuffer<float> buffer;
    double rate = 44100;
    int source = mix, bars = 0;
    std::atomic<int> written { 0 };
    int target = 0;            // samples to record; 0 = until stopped
    bool startedPlaying = false;
    std::atomic<double> beatsPerSecond { 2.0 };
};
