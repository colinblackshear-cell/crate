#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DrumEngine.h"

// The Drum Machine as a plugin: the Mac app, an Audio Unit instrument for Logic, a VST3, and a
// track in the Rig. Every sound's level, tune, decay, tone and pan are parameters (so Logic can
// automate them and the Rig can put them on knobs); the 16 patterns are saved with the project.
class DrumProcessor : public juce::AudioProcessor
{
public:
    DrumProcessor();

    juce::AudioProcessorValueTreeState params;

    // The parameter ID of a sound's setting: "kickLevel", "snareTune"... `what` is one of
    // Level, Tune, Decay, Tone, Pan.
    static juce::String voiceParam (int voice, const juce::String& what);

    //==========================================================================
    // Patterns, edited on the message thread.
    crate::drums::Pattern getPattern (int index) const;
    void setPattern (int index, const crate::drums::Pattern&);
    int getSelectedPattern() const;   // the one the Pattern parameter picks
    void selectPattern (int index);
    int getPlayingPattern() const { return playingPattern.load(); }
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

    juce::SpinLock patternLock;
    std::array<crate::drums::Pattern, crate::drums::kPatterns> patterns;   // under patternLock
    crate::drums::Pattern playing, playingFillPattern;                     // audio thread
    std::atomic<int> playingPattern { 0 }, currentStep { -1 };
    std::atomic<bool> playingFill { false }, fillRequested { false };
    double lastPpq = -1, ownPpq = 0;
    bool ownWasRunning = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumProcessor)
};
