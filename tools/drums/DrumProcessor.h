#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/SafetyLimiter.h"

#include "DrumEngine.h"
#include "SampleKits.h"

#include <vector>

// The Drum Machine as a plugin: the Mac app, an Audio Unit instrument for Logic, a VST3, and a
// track in the Rig. Every sound's level, tune, decay, tone and pan are parameters (so Logic can
// automate them and the Rig can put them on knobs); the 16 patterns are saved with the project.
class DrumProcessor : public juce::AudioProcessor
{
public:
    DrumProcessor();

    juce::AudioProcessorValueTreeState params;

    // The parameter ID of a sound's setting: "kickLevel", "snareTune"... `what` is one of
    // Level, Tune, Decay, Tone, Snappy, Pan, Sound, Mute.
    static juce::String voiceParam (int voice, const juce::String& what);

    // Kits: the whole machine set up for a style (Classic, Deep sub, Latin...).
    static juce::StringArray kitNames();
    void loadKit (int index);

    // Your own kit (SampleKits.h): a sample on any of the twelve sounds, instead of its analog sound.
    // Message thread. Samples save with the project (the file's place, and the sound itself when it's
    // short, so a show still plays on another Mac).
    bool setVoiceSample (int voice, const juce::File&, juce::String& error);
    void setVoiceSample (int voice, crate::drums::SharedSample);   // nullptr: its analog sound again
    crate::drums::SharedSample getVoiceSample (int voice) const;
    bool hasSamples() const;
    void clearSamples();
    // A folder of drum hits as a kit (file names matched to the sounds); returns how many got one.
    int loadKitFolder (const juce::File& folder);
    // The same for a list of files (several dropped at once); the new kit replaces the old one.
    int loadKitFiles (const juce::Array<juce::File>& files, const juce::String& name);
    // Your kits, in Music/Crate/Drum Kits: the samples and each sound's knobs.
    bool saveUserKit (const juce::String& name, juce::File* savedTo = nullptr);
    bool loadUserKit (const juce::File&);
    juce::String getKitName() const { return kitName; }
    juce::AudioFormatManager formats;

    //==========================================================================
    // Patterns, edited on the message thread.
    // Each pattern has an A and a B (variation 0 and 1).
    crate::drums::Pattern getPattern (int index, int variation = 0) const;
    void setPattern (int index, const crate::drums::Pattern&, int variation = 0);
    // The one the window edits: the selected pattern's A or B.
    int editVariation = 0;
    crate::drums::Pattern getEditedPattern() const;
    void setEditedPattern (const crate::drums::Pattern&);
    // What plays: 0 A, 1 B, 2 A then B. Picking A or B edits it too.
    int getVariation() const;
    void setVariation (int);
    // The intro, played once when the clock starts from the top (-1: none).
    int getIntroPattern() const;
    // The chain: patterns played one after another while Play chain is on. Each entry is
    // pattern * 2 + 1 for its B. codeName gives "3A", "3B".
    std::vector<int> getChain() const;
    void setChain (const std::vector<int>&);
    bool isChainOn() const;
    static juce::String codeName (int code);
    int getPlayingCode() const { return playingCode.load(); }   // -1 when stopped
    int getChainPosition() const { return chainPosition.load(); }
    bool isPlayingIntro() const { return playingIntro.load(); }
    int getSelectedPattern() const;   // the one the Pattern parameter picks
    void selectPattern (int index);
    int getPlayingPattern() const;
    int getCurrentStep() const { return currentStep.load(); }
    bool isPlayingFill() const { return playingFill.load(); }
    // The pattern the Fill plays (-1: none).
    int getFillPattern() const;
    // Plays the fill once, the next time the pattern comes round.
    // With no fill picked, it picks one first: the next pattern if it has a beat, else the tom fill.
    void fillNow();

    // Ready-made beats to start from.
    static juce::StringArray grooveNames();
    static crate::drums::Pattern groove (int index);
    // A fresh beat: a kick and snare that make sense, hats, and a few extras.
    static crate::drums::Pattern randomPattern (juce::Random&);

    // Plays a sound now, from the window's pads (any thread).
    void playVoice (int voice, float velocity = 0.8f);
    // The window's pads and keys play through this.
    juce::MidiKeyboardState keyboardState;

    // Rec live: while it's on and the pattern plays, every note that plays a drum sound (the keys,
    // the pads, a controller) is also noted down for the window, with the step of the playing
    // pattern nearest to it (at the current step rate). The audio thread pushes, the window pops
    // and writes them into the pattern, so nothing on the audio thread touches the patterns.
    std::atomic<bool> recLive { false };
    struct RecordedHit { int voice = 0, step = 0, code = 0; };   // code: pattern * 2 + 1 for its B
    bool popRecordedHit (RecordedHit&);
    void clearRecordedHits();
    // How many times each sound has played (the window flashes it).
    int getHitCount (int voice) const { return engine.hitCounts[(size_t) voice].load(); }

    // In its own app there's no Logic or Rig clock, so it runs its own: Start and Stop, at the
    // Tempo setting.
    bool hasOwnClock() const { return wrapperType == wrapperType_Standalone; }
    std::atomic<bool> ownClockRunning { false };

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Drum Machine"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::ValueTree patternToTree (const crate::drums::Pattern&);
    static crate::drums::Pattern patternFromTree (const juce::ValueTree&);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    crate::drums::Settings readSettings() const;

    crate::drums::Engine engine;
    crate::SafetyLimiter limiter;
    std::array<crate::drums::SharedSample, crate::drums::kVoices> voiceSamples;
    std::vector<crate::drums::SharedSample> keptSamples;   // every one the engine was given: it may still be playing it
    juce::String kitName;
    void setParamValue (const juce::String& id, float value);   // the last stop: nothing over -0.3 dBFS, and no NaN

    juce::SpinLock patternLock;
    std::array<crate::drums::Pattern, crate::drums::kPatterns> patterns, patternsB;   // under patternLock
    std::array<std::int8_t, crate::drums::kChain> chain {};
    int chainLength = 0;
    std::atomic<int> patternVersion { 0 };
    // The audio thread's copies.
    std::array<crate::drums::Pattern, crate::drums::kPatterns> bankA, bankB;
    std::array<std::int8_t, crate::drums::kChain> playChain {};
    int playChainLength = 0, bankVersion = -1;
    std::atomic<int> playingPattern { 0 }, currentStep { -1 }, playingCode { -1 }, chainPosition { -1 };
    std::atomic<bool> playingFill { false }, fillRequested { false }, playingIntro { false };
    juce::AbstractFifo recFifo { 64 };
    std::array<RecordedHit, 64> recHits {};
    void noteRecordedHits (const juce::MidiBuffer&, const crate::drums::Clock&);
    double lastPpq = -1, ownPpq = 0;
    bool ownWasRunning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumProcessor)
};
