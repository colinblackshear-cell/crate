#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_devices/juce_audio_devices.h>

#include "BankModel.h"
#include "SequencerEngine.h"
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

    //==========================================================================
    // The sequencer. It plays the current sequence on the current kit.
    SequencerEngine& getSequencer() { return sequencer; }
    // Starts or stops recording; saves an undo step first.
    void toggleRecord();
    // The track recorded notes go on (of the current sequence).
    int recordTrack = 0;

    // A Launchkey's drum pads (channel 10, notes 36-51) play the 16 pads of the bank on screen,
    // instead of the MPC notes those numbers would be. Off inside the Rig, which maps them itself.
    std::atomic<bool> launchkeyPads { true };
    std::atomic<int> padBankOnScreen { 0 };

    // Undo for sequence edits: call beginEdit() before changing the current sequence.
    void beginEdit();
    bool undoEdit();
    bool redoEdit();
    bool canUndo() const { return ! undoStack.empty() && undoSequence == project.getCurrentSequenceIndex(); }
    bool canRedo() const { return ! redoStack.empty() && undoSequence == project.getCurrentSequenceIndex(); }
    static constexpr int kMaxUndo = 50;

    // Adds the notes recorded since the last call to the sequence. Runs on a timer; tests call it.
    int collectRecorded();

    //==========================================================================
    // Playing live. Picking a sequence while one plays waits for the end of the one playing (or
    // the next bar line), like the MPC's next sequence; stopped, it switches straight away.
    void selectSequence (int index);
    int getQueuedSequence() const { return queuedSequence; }   // -1 when nothing is waiting
    bool switchAtBar = false;

    // Song mode: plays the current song's steps in order, each sequence as many times as it says.
    bool playSong (int fromStep = 0);
    bool isSongPlaying() const { return songPlaying; }
    int getSongStep() const { return songStep; }
    int getSongRepeat() const { return songRepeat; }

    // Stop button: stops, leaves song mode and forgets a queued sequence.
    void stopTransport();
    // Keeps the current sequence and song position in step with what's playing. Runs on the
    // timer; tests call it.
    void followTransport();

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
        int muteGroup = 0;
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

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rebuildSequence();
    void renderClicks (juce::AudioBuffer<float>&);
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

    SequencerEngine sequencer;
    juce::MidiBuffer sequenced;
    std::array<SequencerEngine::Click, 16> newClicks {};
    struct ClickVoice { int age = 0; bool accent = false; bool active = false; };
    std::array<ClickVoice, 8> clickVoices {};

    std::vector<seq::Sequence> undoStack, redoStack;
    int undoSequence = -1;

    void advanceSong();
    void queueForSong();
    bool songPlaying = false, wasPlaying = false;
    int songStep = 0, songRepeat = 0, lastPasses = 0;
    int queuedSequence = -1, queuedAtPasses = 0, immediateSequence = -1;
    std::atomic<juce::uint64> playedPads { 0 };
    std::atomic<bool> stopRequested { false };
    juce::MidiBuffer remapped;
    void remapDrumPads (juce::MidiBuffer&);
    double hostRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpcProcessor)
};
