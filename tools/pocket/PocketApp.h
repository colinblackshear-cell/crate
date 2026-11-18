#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "DrumEngine.h"

class DrumProcessor;
class MpcProcessor;
class VocoderProcessor;

// The placeholder screen: cream background, the name, and a Play button that plays an 808-style
// kick pattern from the drum engine. It also creates the MPC and vocoder processors so the build
// proves they link.
class PocketApp : public juce::Component, private juce::AudioIODeviceCallback
{
public:
    PocketApp();
    ~PocketApp() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;

    juce::AudioDeviceManager devices;
    juce::TextButton play { "Play" };

    crate::drums::Engine engine;
    crate::drums::Pattern pattern;
    std::atomic<bool> playing { false };
    double sampleRate = 44100.0, ppq = 0, bpm = 100;

    std::unique_ptr<DrumProcessor> drumProcessor;
    std::unique_ptr<MpcProcessor> mpcProcessor;
    std::unique_ptr<VocoderProcessor> vocoderProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PocketApp)
};
