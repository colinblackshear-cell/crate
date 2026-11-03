#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BassEngine.h"

// The Bass as a plugin: the Mac app, an Audio Unit instrument for Logic, a VST3, and a block in
// the Rig. Sound settings are parameters (so Logic can automate them and the Rig can put them on
// knobs); the 8 patterns are saved with the project.
class BassProcessor : public juce::AudioProcessor
{
public:
    BassProcessor();

    juce::AudioProcessorValueTreeState params;

    //==========================================================================
    // Patterns, edited on the message thread.
    crate::bass::Pattern getPattern (int index) const;
    void setPattern (int index, const crate::bass::Pattern&);
    int getSelectedPattern() const;              // the one the Pattern parameter picks
    void selectPattern (int index);
    int getPlayingPattern() const { return playingPattern.load(); }
    int getCurrentStep() const { return currentStep.load(); }
    int getTranspose() const { return transpose.load(); }

    // A fresh acid line in the current key, for when you need inspiration fast.
    static crate::bass::Pattern randomPattern (juce::Random&, int scale = 0);

    // Plays notes from the window's keyboard.
    juce::MidiKeyboardState keyboardState;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Bass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.1; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::ValueTree patternToTree (const crate::bass::Pattern&);
    static crate::bass::Pattern patternFromTree (const juce::ValueTree&);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    crate::bass::Settings readSettings() const;

    crate::bass::Engine engine;

    juce::SpinLock patternLock;
    std::array<crate::bass::Pattern, crate::bass::kPatterns> patterns;   // under patternLock
    crate::bass::Pattern playing;                                        // audio thread
    std::atomic<int> playingPattern { 0 }, currentStep { -1 }, transpose { 0 };
    double lastPpq = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassProcessor)
};
