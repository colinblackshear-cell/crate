#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "BankModel.h"
#include "core/SoundLibrary.h"

#include <array>

// The MPC tool as a plugin: the same code runs as the Mac app (Standalone), an Audio Unit
// instrument for Logic, and a VST3.
//
// MIDI notes play the current kit's pads on the notes the MPC uses (A01 is note 37), so a
// keyboard or a Logic track plays the same pads here as on the hardware. Clicking a pad goes
// through the same path as a note, and so will the sequencer.
//
// The kits live here rather than in the window, so closing and reopening the plugin window
// keeps them, and Logic saves them (sounds included) with your project.
class MpcProcessor : public juce::AudioProcessor,
                     private juce::ChangeListener,
                     private juce::Timer
{
public:
    MpcProcessor();
    ~MpcProcessor() override;

    Project& getProject() { return project; }
    SoundLibrary& getLibrary() { return library; }
    juce::AudioFormatManager& getFormats() { return formats; }

    // Plays a pad of the current kit, from the message thread (mouse clicks).
    void playPad (int pad, float velocity = 1.0f);
    void releasePad (int pad);
    void stopAll();

    // Plays part of any sound (the pad sound editor's Play button), and reports where it is.
    void preview (SharedAudio, juce::Range<int>);
    void stopPreview();
    int getPreviewPosition() const { return previewPosition.load(); }   // -1 when not playing

    // Pads played since the last call (bit n = pad n), from MIDI or clicks, to light them.
    juce::uint64 takePlayedPads() { return playedPads.exchange (0); }

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MPC Bank Builder"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static constexpr int kMaxVoices = 32;

private:
    // What the audio thread needs to play the current kit, made on the message thread
    // whenever the project changes (stretching for tempo happens there, not while playing).
    struct PadSound
    {
        SharedAudio audio;
        float gainLeft = 0, gainRight = 0;
        double rate = 1;            // playback speed from the pad's tune
        bool reverse = false;
        PadMode mode = PadMode::oneShot;
    };
    using Kit = std::array<PadSound, mpc::kPadCount>;

    struct Voice
    {
        std::shared_ptr<const Kit> kit;   // keeps the sound alive while it plays
        int pad = 0;
        double position = 0;
        double step = 1;
        float velocity = 1;
        float fade = 1;
        bool releasing = false;
    };

    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuildKit(); }
    void timerCallback() override;
    void rebuildKit();

    void handleMidi (const juce::MidiMessage&, const std::shared_ptr<const Kit>&);
    void render (juce::AudioBuffer<float>&, int start, int count);

    juce::AudioFormatManager formats;
    SoundLibrary library;
    Project project;

    // The kit the audio thread plays, swapped under `kitLock`. Old kits wait in `retired`
    // until no voice uses them, so they're never freed on the audio thread.
    juce::SpinLock kitLock;
    std::shared_ptr<const Kit> kit;
    std::vector<std::shared_ptr<const Kit>> retired;

    // Clicks on pads arrive here as MIDI notes and are played in the next block.
    juce::MidiMessageCollector clicks;

    std::vector<Voice> voices;              // audio thread only

    // The preview, swapped under `previewLock` on the message thread, so its sound is only ever
    // freed there.
    juce::SpinLock previewLock;
    SharedAudio previewAudio;
    juce::Range<int> previewRange;
    double previewPos = 0;
    float previewFade = 1;
    bool previewStopping = false, previewDone = true;
    std::atomic<int> previewPosition { -1 };
    void renderPreview (juce::AudioBuffer<float>&);
    std::array<int, 128> padForNoteNumber {};
    std::atomic<juce::uint64> playedPads { 0 };
    std::atomic<bool> stopRequested { false };
    double hostRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpcProcessor)
};
