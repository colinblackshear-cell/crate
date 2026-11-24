#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/Audio.h"
#include "core/visuals/VisualTap.h"

// The phone's sound card: records the mic straight to a WAV file, plays a stretch of a sound, and
// measures the input level for the meter.
class PocketAudio : private juce::AudioIODeviceCallback
{
public:
    // Something else that makes sound through the same device (the drum machine and pads). Its
    // render runs on the audio thread and adds into the buffers: no allocations, no locks.
    struct Renderer
    {
        virtual ~Renderer() = default;
        virtual void prepare (double sampleRate) = 0;
        virtual void render (float* left, float* right, int numSamples) = 0;   // right may be null
        // Called just before render() in each audio callback: this callback's microphone channels
        // (numIn may be 0) and the device's input + output latency in samples.
        virtual void setInput (const float* const* in, int numIn) { juce::ignoreUnused (in, numIn); }
        virtual void setLatency (int samples) { juce::ignoreUnused (samples); }
    };
    // Set once, before start(). Not owned.
    void setRenderer (Renderer* r) { renderer = r; }

    PocketAudio();
    ~PocketAudio() override;

    // Asks for the microphone, then opens the audio device. `done` gets an error text (empty = fine).
    void start (std::function<void (juce::String)> done);
    bool isRunning() const { return running; }

    // Recording
    bool startRecording (const juce::File& wavFile);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    double recordedSeconds() const;
    float takeLevel() { return peak.exchange (0.0f); }   // loudest sample since the last call, 0..1
    double deviceRate() const { return sampleRate; }
    int latencySamples() const { return latency.load(); }   // input + output, as the device reports
    // After the route changed (AirPods in, headphones out): the audio session's new input + output
    // latency, in samples, for the takes' latency compensation. Ignored when <= 0. Message thread.
    void updateLatency (int samples);
    bool hasOutputDevice() const { return running; }

    // The mic after a route change (headphones in or out, AirPods). Message thread.
    //   inputDead:     the mic is allowed but the device opened with no live input channel
    //   silentSeconds: how long the live input has given exact zeros (a real mic never does)
    //   reopenInput:   closes and reopens the device with the mic on; never while a take records
    bool inputDead() const;
    double silentSeconds() const { return (double) silentRun.load() / sampleRate; }
    bool reopenInput();

    // Kaleidobox Visuals' ears: a copy of what plays out (only while the picture is open).
    crate::visuals::VisualTap visualTap;

    // Playback of `audio` from sample `start` up to `end`.
    void play (crate::SharedAudio audio, int start, int end);
    void stopPlaying();
    bool isPlaying() const { return playing.load(); }
    int position() const { return (int) playPos.load(); }   // sample in the sound being played

private:
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int,
                                           const juce::AudioIODeviceCallbackContext&) override;

    Renderer* renderer = nullptr;
    juce::AudioDeviceManager devices;
    bool running = false;
    bool micAllowed = false;
    std::atomic<juce::int64> silentRun { 0 };
    double sampleRate = 44100.0;
    std::atomic<int> latency { 0 };

    juce::TimeSliceThread writerThread { "Pocket recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    juce::SpinLock writerLock;
    std::atomic<bool> recording { false };
    std::atomic<juce::int64> recordedSamples { 0 };
    std::atomic<float> peak { 0.0f };

    juce::SpinLock playLock;
    crate::SharedAudio playAudio;
    double playEnd = 0;
    std::atomic<double> playPos { 0.0 };
    std::atomic<bool> playing { false };
};
