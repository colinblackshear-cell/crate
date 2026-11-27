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
        beginTest ("the factory kits, 16 sounds each, none silent");
        {
            const auto& kits = crate::factory::kits();
            expectEquals ((int) kits.size(), 11);
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

        beginTest ("your own sound over a factory pad: put the factory sound back, one pad or all 16");
        {
            RigEngine engine;
            auto& project = engine.getMpc().getProject();
            engine.loadFactoryKit (0, 0);
            const auto kick = project.currentBank().pads[0].audio;
            expectEquals (engine.factoryKitOf (0), 0);
            expectEquals (engine.factoryPadName (0), juce::String ("Street kick"));
            expect (! engine.padDiffersFromFactory (0), "still the factory kick");

            // Your own sound on A01, and A02 removed.
            juce::AudioBuffer<float> mine (1, 1000);
            mine.clear();
            mine.setSample (0, 10, 0.5f);
            auto& a01 = project.currentBank().pads[0];
            a01.audio = crate::share (std::move (mine));
            a01.name = "My sound";
            project.currentBank().pads[1] = {};
            expect (engine.padDiffersFromFactory (0) && engine.padDiffersFromFactory (1));
            expect (engine.factoryPadName (16).isEmpty(), "B01 isn't part of the kit");

            const auto said = engine.restoreFactoryPad (0);
            expect (said.contains ("Street kick"), said);
            expect (project.currentBank().pads[0].audio == kick, "the very same factory kick");
            expect (engine.padDiffersFromFactory (1), "A02 is still empty");

            const auto reset = engine.resetFactoryKit();
            expect (reset.contains ("16"), reset);
            expectEquals (project.currentBank().usedPads(), 16);
            expectEquals ((int) project.getBanks().size(), 1, "on the same kit, not a new one");
            for (int p = 0; p < 16; ++p)
                expect (! engine.padDiffersFromFactory (p));

            // A kit that isn't a factory kit has nothing to put back.
            project.currentBank().name = "My kit";
            expectEquals (engine.factoryKitOf (0), -1);
            expect (! engine.padDiffersFromFactory (0));
            expect (engine.resetFactoryKit().contains ("isn't a factory kit"));
        }

        beginTest ("the tests' Rigs start with empty pads");
        {
            RigEngine engine;
            expectEquals (engine.getMpc().getProject().currentBank().usedPads(), 0);
        }
    }
};

static RigFactoryKitTests rigFactoryKitTests;
