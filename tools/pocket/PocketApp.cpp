#include "PocketApp.h"

#include "DrumProcessor.h"
#include "MpcProcessor.h"
#include "PluginProcessor.h"   // the vocoder's

PocketApp::PocketApp()
{
    setSize (390, 844);

    // An 808-style kick: boom on the beat, plus a ghost hit before beat 3.
    for (int step : { 0, 4, 8, 10, 12 })
        pattern.hits[crate::drums::kick][(size_t) step] = 1;
    pattern.length = 16;

    // Instantiated only to prove the engines build and link on the phone.
    drumProcessor = std::make_unique<DrumProcessor>();
    mpcProcessor = std::make_unique<MpcProcessor>();
    vocoderProcessor = std::make_unique<VocoderProcessor>();

    addAndMakeVisible (play);
    play.onClick = [this]
    {
        const bool now = ! playing.load();
        if (now)
            ppq = 0;
        playing = now;
        play.setButtonText (now ? "Stop" : "Play");
    };

    juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [this] (bool granted)
    {
        devices.initialise (granted ? 1 : 0, 2, nullptr, true);
        devices.addAudioCallback (this);
    });
}

PocketApp::~PocketApp()
{
    devices.removeAudioCallback (this);
}

void PocketApp::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xffefe7d6));
    g.setColour (juce::Colour (0xff2b2418));
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("Kaleidobox Pocket", getLocalBounds().removeFromTop (getHeight() / 2).withTrimmedBottom (40),
                juce::Justification::centred);
}

void PocketApp::resized()
{
    play.setBounds (getLocalBounds().withSizeKeepingCentre (160, 56).translated (0, 30));
}

void PocketApp::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    sampleRate = device->getCurrentSampleRate();
    engine.prepare (sampleRate);
    engine.setPatterns (&pattern, nullptr);
}

void PocketApp::audioDeviceStopped() {}

void PocketApp::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                                  int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr)
            juce::FloatVectorOperations::clear (out[c], numSamples);
    if (numOut < 1 || out[0] == nullptr)
        return;

    crate::drums::Clock clock;
    clock.playing = playing.load();
    clock.ppq = ppq;
    clock.bpm = bpm;
    juce::MidiBuffer midi;
    engine.render (out[0], numOut > 1 ? out[1] : out[0], numSamples, midi, clock);
    ppq += numSamples / sampleRate * bpm / 60.0;
}
