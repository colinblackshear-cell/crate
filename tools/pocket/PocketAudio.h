#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "core/Audio.h"

// The phone's sound card: records the mic straight to a WAV file, plays a stretch of a sound, and
// measures the input level for the meter.
class PocketAudio : private juce::AudioIODeviceCallback
{
public:
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

    juce::AudioDeviceManager devices;
    bool running = false;
    double sampleRate = 44100.0;

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
