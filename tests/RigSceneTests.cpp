#include "TestHelpers.h"

#include "RigEngine.h"

namespace
{
constexpr double kSceneRate = 48000.0;
constexpr int kSceneBlock = 480;

// Runs the Rig for `beats` (at its tempo), ticking the scenes like the window does.
void runBeats (RigEngine& engine, double beats)
{
    juce::AudioBuffer<float> out (2, kSceneBlock);
    const int blocks = (int) std::round (beats * 60.0 / engine.getTransport().getBpm() * kSceneRate / kSceneBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kSceneBlock);
        engine.tickScenes();
    }
}

RigScene sceneWith (RigEngine& engine, const juce::String& name)
{
    RigScene s;
    s.name = name;
    s.state = engine.captureScene();
    return s;
}
}

struct RigSceneTests : juce::UnitTest
{
    RigSceneTests() : juce::UnitTest ("Rig scenes", "Rig") {}

    void runTest() override
    {
        beginTest ("a scene keeps what's playing, and comes back at the next bar");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            auto& scenes = engine.getScenes().getScenes();

            scenes.push_back (sceneWith (engine, "Verse"));            // nothing muted
            engine.getStrip (RigEngine::bassStrip).mute = true;
            engine.getLooper().getLayer (2).mute = true;
            scenes.push_back (sceneWith (engine, "Break"));            // bass and loop 3 muted
            expectEquals (scenes[1].state.stripMutes[(size_t) RigEngine::bassStrip], 1);
            expectEquals (scenes[1].state.stripMutes[(size_t) RigEngine::masterStrip], -1, "the master isn't part of a scene");

            // Stopped: a scene starts straight away.
            engine.tickScenes();
            engine.getScenes().launch (0);
            runBeats (engine, 0.1);
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load());
            expect (! engine.getLooper().getLayer (2).mute.load());
            expectEquals (engine.getScenes().getCurrent(), 0);

            // Playing: it waits for the bar.
            engine.getTransport().play();
            runBeats (engine, 1.5);
            engine.getScenes().launch (1);
            expectEquals (engine.getScenes().getQueued(), 1);
            runBeats (engine, 2.0);   // beat 3.5
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load(), "not before the bar");
            runBeats (engine, 1.0);   // past beat 4
            expect (engine.getStrip (RigEngine::bassStrip).mute.load(), "at the bar");
            expect (engine.getLooper().getLayer (2).mute.load());
            expectEquals (engine.getScenes().getCurrent(), 1);
            expectEquals (engine.getScenes().getQueued(), -1);
        }

        beginTest ("a scene changes the MPC's sequence and the bass pattern");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            auto& project = engine.getMpc().getProject();
            project.addSequence ("B section");
            project.setCurrentSequence (0);
            auto& scenes = engine.getScenes().getScenes();
            RigScene s;
            s.name = "B";
            s.state.mpcSequence = 1;
            s.state.bassPattern = 3;
            s.state.arp = 1;
            scenes.push_back (s);
            engine.tickScenes();
            engine.getScenes().launch (0);
            runBeats (engine, 0.1);
            expectEquals (project.getCurrentSequenceIndex(), 1);
            expectEquals (engine.getBass().getSelectedPattern(), 3);
            expect (engine.getSynth().params.getRawParameterValue ("arpOn")->load() > 0.5f);
        }

        beginTest ("song mode: a part with a length moves on by itself");
        {
            RigEngine engine;
            engine.prepare (kSceneRate, kSceneBlock);
            engine.vocoderOn = false;
            engine.getTransport().setBpm (120.0);
            auto& rig = engine.getScenes();
            for (auto name : { "Intro", "Verse", "Outro" })
            {
                auto s = sceneWith (engine, name);
                s.bars = 1;
                rig.getScenes().push_back (s);
            }
            rig.songMode = true;
            engine.tickScenes();
            rig.launch (0);
            engine.getTransport().play();
            runBeats (engine, 0.5);
            expectEquals (rig.getCurrent(), 0);
            runBeats (engine, 4.0);
            expectEquals (rig.getCurrent(), 1, "one bar later");
            runBeats (engine, 4.0);
            expectEquals (rig.getCurrent(), 2);
            runBeats (engine, 8.0);
            expectEquals (rig.getCurrent(), 2, "the last part stays");

            beginTest ("Next and Back from keyboard buttons");
            rig.backPresses += 1;
            runBeats (engine, 4.5);
            expectEquals (rig.getCurrent(), 1);
            rig.nextPresses += 1;
            runBeats (engine, 0.1);
            expectEquals (rig.getQueued(), 2);
        }

        beginTest ("scenes are saved with the show");
        {
            RigEngine engine;
            auto& scenes = engine.getScenes();
            auto s = sceneWith (engine, "Drop");
            s.bars = 8;
            s.state.loopMutes[3] = 1;
            scenes.getScenes().push_back (s);
            scenes.songMode = true;
            const auto xml = engine.saveShow().createXml();

            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            expectEquals ((int) other.getScenes().getScenes().size(), 1);
            expect (other.getScenes().songMode);
            if (! other.getScenes().getScenes().empty())
            {
                const auto& back = other.getScenes().getScenes()[0];
                expectEquals (back.name, juce::String ("Drop"));
                expectEquals (back.bars, 8);
                expectEquals (back.state.loopMutes[3], 1);
                expectEquals (back.state.stripMutes[(size_t) RigEngine::bassStrip], 0);
            }
        }
    }
};

static RigSceneTests rigSceneTests;
