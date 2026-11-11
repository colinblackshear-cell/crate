#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "RigEngine.h"

// The Rig as a plugin, for Logic: an instrument ("Crate Rig") that runs the whole Rig inside
// a Logic song. It follows Logic's tempo, time signature and Play/Stop, plays from the
// track's MIDI (your Launchkey through Logic), and hears your mic through the side chain
// (pick the mic's track or input in Side Chain at the top of the plugin window). The
// main output is the Rig's mix; each tool can also have its own output, so it gets its own
// track in Logic's mixer (click + on the instrument's channel strip in Logic).
// The show is saved with the Logic song.
class RigPlugin : public juce::AudioProcessor
{
public:
    RigPlugin();
    ~RigPlugin() override;

    RigEngine& getEngine() { return *engine; }

    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double sampleRate, int maximumBlockSize) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kaleidobox"; }
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

private:
    static BusesProperties buses();

    juce::File loopsFile() const;

    std::unique_ptr<RigEngine> engine;
    juce::String songId;   // names this song's loops folder
    juce::AudioBuffer<float> mic;
    std::array<std::array<float*, 2>, RigEngine::numStems> stemChannels {};   // the side chain, copied before the outputs are written

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigPlugin)
};
