#include "TestHelpers.h"

#include "LooperView.h"
#include "RigEngine.h"
#include "RigScenes.h"
#include "RigTimeline.h"

// Sixteen loop layers: loops 9-16 play, sit on the song's lanes 15-22 (after the tools, so the
// lanes shows saved before keep their numbers), save with the show, and old shows and scenes
// load as they did.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
constexpr int kPerBeat = (int) (kRate / 2);   // at 120 BPM

juce::AudioBuffer<float> steadyLoop (int beats, float level)
{
    juce::AudioBuffer<float> a (2, beats * kPerBeat);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (a.getWritePointer (ch), level, a.getNumSamples());
    return a;
}

std::vector<float> run (RigEngine& engine, double beats)
{
    juce::AudioBuffer<float> out (2, kBlock);
    std::vector<float> heard;
    const int blocks = (int) std::round (beats * 60.0 / engine.getTransport().getBpm() * kRate / kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        for (int i = 0; i < kBlock; ++i)
            heard.push_back (out.getSample (0, i));
    }
    return heard;
}

void onlyLoops (RigEngine& engine)
{
    engine.vocoderOn = false;
    for (int st : { RigEngine::mpcStrip, RigEngine::samplerStrip, RigEngine::vocoderStrip, RigEngine::bassStrip,
                    RigEngine::synthStrip, RigEngine::inputStrip, RigEngine::drumsStrip })
        engine.getStrip (st).mute = true;
}

RigTimeline::Block block (int lane, double start, double length)
{
    RigTimeline::Block b;
    b.lane = lane;
    b.start = start;
    b.length = length;
    return b;
}
}

struct RigLayers16Tests : juce::UnitTest
{
    RigLayers16Tests() : juce::UnitTest ("Rig: sixteen loop layers", "Rig") {}

    void runTest() override
    {
        beginTest ("the lanes: loops 1-8 and the tools where they were, loops 9-16 after them");
        {
            expectEquals (RigLooper::kLayers, 16);
            expectEquals (RigTimeline::kLanes, 23);
            const auto& lanes = RigTimeline::lanes();
            for (int i = 0; i < 8; ++i)
            {
                expect (lanes[(size_t) i].isLoop && lanes[(size_t) i].index == i, "lane " + juce::String (i));
                expectEquals (RigTimeline::laneForLoop (i), i);
            }
            const int tools[] = { RigEngine::mpcStrip, RigEngine::bassStrip, RigEngine::synthStrip, RigEngine::vocoderStrip,
                                  RigEngine::samplerStrip, RigEngine::inputStrip, RigEngine::drumsStrip };
            for (int i = 0; i < 7; ++i)
            {
                expect (! lanes[(size_t) (8 + i)].isLoop && lanes[(size_t) (8 + i)].index == tools[i], "tool lane " + juce::String (8 + i));
                expectEquals (RigTimeline::laneForStrip (tools[i]), 8 + i);
            }
            for (int l = 8; l < 16; ++l)
            {
                const int lane = RigTimeline::laneForLoop (l);
                expectEquals (lane, 15 + (l - 8));
                expect (lanes[(size_t) lane].isLoop && lanes[(size_t) lane].index == l);
            }
            expectEquals (RigTimeline::laneForLoop (11), 18, "loop 12 is lane 18");
            expectEquals (RigTimeline::laneForLoop (12), 19, "layer 12 (loop 13) is lane 19");
        }

        beginTest ("layer 12 (loop 13) plays, and its block on lane 19 brings it in and out");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getTransport().setBpm (120.0);
            onlyLoops (engine);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (12, steadyLoop (4, 0.1f), 4.0);
            looper.getLayer (12).level = 1.0f;
            run (engine, 0.1);
            expect (looper.getState (12) == RigLooper::State::playing, "layer 12 has its loop");
            expectWithinAbsoluteError (looper.getLayer (12).lengthBeats.load(), 4.0, 1.0e-9);

            auto& timeline = engine.getTimeline();
            timeline.getBlocks() = { block (RigTimeline::laneForLoop (12), 1, 2) };   // bars 2-3
            timeline.changed();
            engine.playTimeline (0);
            const auto heard = run (engine, 14);
            const auto at = [&] (double beat) { return std::abs (heard[(size_t) (beat * kPerBeat)]); };
            expectLessThan (at (2.5), 1.0e-4f, "bar 1: not in its block");
            expectGreaterThan (at (4.5), 1.0e-3f, "bar 2: its block plays loop 13");
            expectGreaterThan (at (10.5), 1.0e-3f, "bar 3");
            expectLessThan (at (12.5), 1.0e-4f, "bar 4: after the block");
        }

        beginTest ("recording onto layer 12");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            looper.getLayer (12).bars = 1;
            looper.pressRecord (12);
            run (engine, 1);
            expect (looper.getState (12) == RigLooper::State::recording || looper.getState (12) == RigLooper::State::armed);
            for (int i = 0; i < 400 && looper.getState (12) != RigLooper::State::playing; ++i)
                run (engine, 0.5);
            expect (looper.getState (12) == RigLooper::State::playing, "a bar recorded on layer 12");
            expectWithinAbsoluteError (looper.getLayer (12).lengthBeats.load(), (double) engine.getTransport().getBeatsPerBar(), 1.0e-6);
        }

        beginTest ("saved with the show: loops 9-16 and their lanes come back");
        {
            juce::TemporaryFile showFile (".crateshow");
            {
                RigEngine engine;
                engine.prepare (kRate, kBlock);
                auto& looper = engine.getLooper();
                looper.setLayerAudio (2, steadyLoop (4, 0.1f), 4.0);
                looper.setLayerAudio (12, steadyLoop (8, 0.2f), 8.0);
                looper.setLayerAudio (15, steadyLoop (2, 0.3f), 2.0);
                run (engine, 0.1);
                looper.getLayer (12).level = 0.5f;
                looper.getLayer (15).mute = true;
                engine.setLoopName (12, "Thirteen");
                engine.getTimeline().getBlocks() = { block (2, 0, 4), block (RigTimeline::laneForLoop (12), 2, 4),
                                                     block (RigTimeline::laneForLoop (15), 4, 2) };
                engine.getTimeline().changed();
                auto xml = engine.saveShow().createXml();
                expect (xml != nullptr && xml->writeTo (showFile.getFile()));
                expect (engine.writeLoopAudio (showFile.getFile()));
            }
            {
                RigEngine engine;
                engine.prepare (kRate, kBlock);
                std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (showFile.getFile()));
                expect (xml != nullptr);
                if (xml == nullptr)
                    return;
                engine.loadShow (juce::ValueTree::fromXml (*xml));
                engine.readLoopAudio (showFile.getFile());
                run (engine, 0.1);
                auto& looper = engine.getLooper();
                expect (looper.getState (2) == RigLooper::State::playing, "loop 3");
                expect (looper.getState (12) == RigLooper::State::playing, "loop 13");
                expect (looper.getState (15) == RigLooper::State::playing, "loop 16");
                expect (looper.getState (8) == RigLooper::State::empty);
                expectWithinAbsoluteError (looper.getLayer (12).lengthBeats.load(), 8.0, 1.0e-6);
                expectWithinAbsoluteError (looper.getLayer (15).lengthBeats.load(), 2.0, 1.0e-6);
                expectWithinAbsoluteError (looper.getLayer (12).level.load(), 0.5f, 1.0e-6f);
                expect (looper.getLayer (15).mute.load());
                expectEquals (engine.getLoopName (12), juce::String ("Thirteen"));
                const auto& blocks = engine.getTimeline().getBlocks();
                expectEquals ((int) blocks.size(), 3);
                expect (engine.getTimeline().laneUsed (19) && engine.getTimeline().laneUsed (22) && engine.getTimeline().laneUsed (2));
            }
        }

        beginTest ("an old show's timeline (lanes 0-14) loads as it was");
        {
            // A show from before: a block on each of the 15 lanes it had, and a scene with 8 loop mutes.
            juce::ValueTree show;
            {
                RigEngine engine;
                auto& timeline = engine.getTimeline();
                for (int lane = 0; lane < 15; ++lane)
                    timeline.getBlocks().push_back (block (lane, lane, 1 + lane % 3));
                timeline.changed();
                show = engine.saveShow();
            }
            auto tl = show.getChildWithName (RigTimeline::kTreeType);
            expect (tl.isValid());
            for (auto b : tl)
                expectLessThan ((int) b.getProperty ("lane", 99), 15);
            juce::ValueTree scene ("Scene");
            scene.setProperty ("name", "Old verse", nullptr);
            scene.setProperty ("loopMutes", "1 0 1 0 -1 0 1 1", nullptr);
            show.appendChild (scene, nullptr);

            RigEngine engine;
            engine.loadShow (show);
            const auto& blocks = engine.getTimeline().getBlocks();
            expectEquals ((int) blocks.size(), 15);
            for (const auto& b : blocks)
            {
                expectWithinAbsoluteError (b.start, (double) b.lane, 1.0e-9, "lane " + juce::String (b.lane) + " where it was");
                expectWithinAbsoluteError (b.length, (double) (1 + b.lane % 3), 1.0e-9);
            }
            for (int lane = 0; lane < 15; ++lane)
                expect (engine.getTimeline().laneUsed (lane));
            for (int lane = 15; lane < RigTimeline::kLanes; ++lane)
                expect (! engine.getTimeline().laneUsed (lane), "loops 9-16 have no blocks");
            expect (RigTimeline::lanes()[14].index == RigEngine::drumsStrip, "lane 14 is still the drum machine");

            const auto& scenes = engine.getScenes().getScenes();
            expect (! scenes.empty());
            if (! scenes.empty())
            {
                const auto& mutes = scenes.back().state.loopMutes;
                const int old[] = { 1, 0, 1, 0, -1, 0, 1, 1 };
                for (int i = 0; i < 8; ++i)
                    expectEquals (mutes[(size_t) i], old[i]);
                for (int i = 8; i < 16; ++i)
                    expectEquals (mutes[(size_t) i], -1, "a scene from before leaves loops 9-16 as they are");
            }
        }

        beginTest ("scenes: an old 8-value loopMutes loads, and 16 values round trip");
        {
            juce::ValueTree t ("Scene");
            t.setProperty ("loopMutes", "0 1 0 1 0 1 0 1", nullptr);
            const auto old = RigScene::fromTree (t);
            for (int i = 0; i < 8; ++i)
                expectEquals (old.state.loopMutes[(size_t) i], i % 2);
            for (int i = 8; i < 16; ++i)
                expectEquals (old.state.loopMutes[(size_t) i], -1);

            RigScene s;
            for (int i = 0; i < 16; ++i)
                s.state.loopMutes[(size_t) i] = i >= 8 ? 1 : 0;
            const auto back = RigScene::fromTree (s.toTree());
            for (int i = 0; i < 16; ++i)
                expectEquals (back.state.loopMutes[(size_t) i], i >= 8 ? 1 : 0);
        }

        beginTest ("the Looper screen: Loops 1-8 / 9-16, and a layer on page 2 records");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            LooperView view (engine);
            view.setSize (1440, 900);
            expectEquals (view.getPage(), 0);
            expectEquals (view.firstShownLayer(), 0);
            view.showPage (1);
            expectEquals (view.getPage(), 1);
            expectEquals (view.firstShownLayer(), 8);
            view.showPage (0);
            view.pressLayer (11);
            run (engine, 0.1);
            expect (engine.getLooper().getState (11) != RigLooper::State::empty, "layer 12's number records it");
            expectEquals (view.getSelected(), 11);
            expectEquals (view.getPage(), 1, "its rows come up");
            view.showPage (0);

            // Rec next layer, with 1-8 full: layer 9, and the screen shows it.
            for (int i = 0; i < 8; ++i)
                engine.getLooper().setLayerAudio (i, steadyLoop (4, 0.1f), 4.0);
            run (engine, 0.1);
            engine.getLooper().clear (11);
            run (engine, 0.1);
            expectEquals (view.nextEmptyLayer(), 8);
            view.pressRecNext();
            expectEquals (view.getSelected(), 8);
            expectEquals (view.getPage(), 1, "page 2 comes up for layer 9");
        }
    }
};

static RigLayers16Tests rigLayers16Tests;
