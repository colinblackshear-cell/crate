#include "TestHelpers.h"

#include "RigEngine.h"
#include "FactoryKits.h"

// The phone's factory kits on the Mac: the same five kits, made by the same code, the Street kit on
// the pads of a new Rig (and after New song, which still counts as blank), and any kit onto a new kit.
class RigFactoryKitTests : public juce::UnitTest
{
public:
    RigFactoryKitTests() : juce::UnitTest ("Rig: the phone's factory kits on the MPC pads", "Rig") {}

    void runTest() override
    {
        beginTest ("the five factory kits, 16 sounds each, none silent");
        {
            const auto& kits = crate::factory::kits();
            expectEquals ((int) kits.size(), 5);
            for (int k = 0; k < (int) kits.size(); ++k)
            {
                const auto sounds = crate::factory::makeSounds (k);
                expectEquals ((int) sounds.size(), 16, kits[(size_t) k].name);
                expectEquals ((int) crate::factory::soundNames (k).size(), 16);
                for (auto& [name, audio] : sounds)
                    expect (audio.getNumSamples() > 100 && audio.getMagnitude (0, 0, audio.getNumSamples()) > 0.1f, name + " has sound");
            }
        }

        beginTest ("a new Rig has the Street kit on A01-A16, and is still blank");
        {
            RigEngine::startWithFactoryKit = true;
            RigEngine engine;
            auto& project = engine.getMpc().getProject();
            expectEquals ((int) project.getBanks().size(), 1);
            expectEquals (project.currentBank().name, juce::String ("Street kit"));
            expectEquals (project.currentBank().usedPads(), 16);
            expectEquals (project.currentBank().pads[0].name, juce::String ("Street kick"));
            expect (project.currentBank().pads[16].isEmpty(), "pad B01 stays free");
            expect (engine.isBlank(), "the factory kit doesn't count as something made");

            // Another kit goes on a new kit and plays.
            const auto said = engine.loadFactoryKit (2);
            expect (said.contains ("808 classic"), said);
            expectEquals ((int) project.getBanks().size(), 2);
            expectEquals (project.getCurrentIndex(), 1);
            expectEquals (project.currentBank().usedPads(), 16);
            expect (! engine.isBlank(), "a kit you picked is something made");

            // New song: back to the Street kit alone, and blank.
            engine.newShow();
            expectEquals ((int) engine.getMpc().getProject().getBanks().size(), 1);
            expectEquals (engine.getMpc().getProject().currentBank().name, juce::String ("Street kit"));
            expect (engine.isBlank(), "New song is blank again");
            RigEngine::startWithFactoryKit = false;
        }

        beginTest ("the tests' Rigs start with empty pads");
        {
            RigEngine engine;
            expectEquals (engine.getMpc().getProject().currentBank().usedPads(), 0);
        }
    }
};

static RigFactoryKitTests rigFactoryKitTests;
