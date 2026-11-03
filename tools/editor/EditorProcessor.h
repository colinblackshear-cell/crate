#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditDocument.h"
#include "core/KeyboardSampler.h"
#include "core/SoundLibrary.h"

#include <array>

// The Sample Editor as a plugin. The same code runs as the Mac app (Standalone), an Audio Unit
// for Logic, and a VST3.
//
// Audio in is what you record (the mic in the app; the track's input in Logic). Audio out is
// the sound you're editing, when you press play. The sound itself lives here rather than in
// the window, so closing and reopening the plugin window keeps your work, and Logic saves the
// sound with your project.
//
// It's also a sampler: MIDI notes (from a keyboard, Logic, or the on-screen keys) play the
// sound, or the selected part of it, across the keys from a root note.
//
// Built with CRATE_INSTRUMENT=1 it's the instrument version for Logic: no audio input (so no
// recording), shown under Instruments rather than Audio Units effects.
class EditorProcessor : public juce::AudioProcessor,
                        private juce::ChangeListener
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
    // Switches what's playing to another version of the sound (e.g. with effects), carrying on
    // from the same point. Plays all of `audio`.
    void swapAudio (crate::SharedAudio audio);
    // Where playback is as a share (0..1) of the range being played; -1 when stopped.
    double getPlayFraction() const;
    bool isPlaying() const { return playing.load(); }
    int getPlayPosition() const { return playPosition.load(); }   // -1 when stopped

    //==========================================================================
    // Playing the sound from the keys. The on-screen keyboard feeds `keyboardState`; MIDI from
    // the host or a keyboard comes in through processBlock.
    crate::KeyboardSampler& getKeys() { return keys; }

    // Chop markers (where slices start; 0 is implied), which the keys play in slices mode.
    // They're cleared when an edit changes the sound's length.
    void setChops (std::vector<int>);
    const std::vector<int>& getChops() const { return chops; }
    // Brings the keys and chops up to date with the document; runs on every document change,
    // and the window calls it too so it never sees them out of date.
    void syncToDocument();
    juce::MidiKeyboardState keyboardState;
    static constexpr bool isInstrument()
    {
       #if CRATE_INSTRUMENT
        return true;
       #else
        return false;
       #endif
    }

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

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return keys.getRelease(); }

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
    static BusesProperties makeBuses();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void renderPlayback (juce::AudioBuffer<float>&);

    juce::AudioFormatManager formats;
    SoundLibrary library;
    EditDocument document;
    crate::KeyboardSampler keys;
    const void* keysAudio = nullptr;
    int keysLength = 0;
    std::vector<int> chops;

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
    std::atomic<double> playFraction { 0.0 };
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
