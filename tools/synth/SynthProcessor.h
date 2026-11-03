#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "SynthEngine.h"

// The Synth as a plugin: the Mac app, an Audio Unit instrument for Logic, a VST3, and a block in
// the Rig. Sound and arp settings are parameters; the arp's rhythm is saved with the project.
class SynthProcessor : public juce::AudioProcessor
{
public:
    SynthProcessor();

    juce::AudioProcessorValueTreeState params;
    juce::MidiKeyboardState keyboardState;

    crate::synthtool::Rhythm getRhythm() const;
    void setRhythm (const crate::synthtool::Rhythm&);
    int getArpStep() const { return arpStep.load(); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    crate::synthtool::Settings readSettings() const;

    crate::synthtool::Engine engine;
    juce::SpinLock rhythmLock;
    crate::synthtool::Rhythm rhythm;     // under rhythmLock
    std::atomic<int> arpStep { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SynthProcessor)
};
