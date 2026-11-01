#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditDocument.h"
#include "core/SoundLibrary.h"

#include <array>

// The Sample Editor as a plugin. The same code runs as the Mac app (Standalone), an Audio Unit
// for Logic, and a VST3.
//
// Audio in is what you record (the mic in the app; the track's input in Logic). Audio out is
// the sound you're editing, when you press play. The sound itself lives here rather than in
// the window, so closing and reopening the plugin window keeps your work, and Logic saves the
// sound with your project.
class EditorProcessor : public juce::AudioProcessor
{
public:
    EditorProcessor();
    ~EditorProcessor() override;

    EditDocument& getDocument() { return document; }
    SoundLibrary& getLibrary() { return library; }
    juce::AudioFormatManager& getFormats() { return formats; }

    // Copy and paste between edits (and between windows).
    juce::AudioBuffer<float> clipboard;

    //==========================================================================
    // Playback of the sound being edited, from the message thread.
    void play (crate::SharedAudio, juce::Range<int> range, bool loop);
    void stop();
    void setLooping (bool);
    bool isPlaying() const { return playing.load(); }
    int getPlayPosition() const { return playPosition.load(); }   // -1 when stopped

    //==========================================================================
    // Recording from the audio input.
    void startRecording();
    // Ends the recording and returns it at 44.1 kHz, mono unless the input really was stereo.
    juce::AudioBuffer<float> stopRecording();
    bool isRecording() const { return recording.load(); }
    double getRecordedSeconds() const;
    float takeInputLevel() { return inputLevel.exchange (0.0f); }   // peak since the last call
    bool hasInput() const { return getTotalNumInputChannels() > 0; }
    // Moves recorded audio off the audio thread; call it often (e.g. from a 30 Hz timer).
    void collectRecording();

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sample Editor"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // Saves the sound (as WAV data) and its name, so a Logic project reopens with it.
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Sounds longer than this aren't saved inside the project (they're in the library anyway).
    static constexpr double kMaxSavedSeconds = 10.0 * 60.0;

private:
    juce::AudioFormatManager formats;
    SoundLibrary library;
    EditDocument document;

    // Playback state, shared with the audio thread under `lock`.
    juce::SpinLock lock;
    crate::SharedAudio playAudio;
    double playPos = 0;
    juce::Range<int> playRange;
    bool loop = false;
    float fadeGain = 1.0f;
    bool stopping = false;
    std::atomic<bool> playing { false };
    std::atomic<int> playPosition { -1 };
    double hostRate = 44100.0;

    // Recording: the audio thread writes into a ring buffer; collectRecording() empties it.
    std::atomic<bool> recording { false };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<int> recordChannels { 1 };
    juce::AbstractFifo fifo { 1 << 18 };
    juce::AudioBuffer<float> ring { 2, 1 << 18 };
    std::array<std::vector<float>, 2> recorded;
    double recordRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorProcessor)
};
