#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigExport.h"
#include "RigTimeline.h"
#include "TimelineView.h"
#include "SongGrid.h"
#include "LooperView.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
constexpr int kPerBeat = (int) (kRate / 2);   // at 120 BPM

// An `beats`-beat loop whose sound tells which beat of it is playing (1, 2, 3...: / 20).
juce::AudioBuffer<float> countingLoop (int beats)
{
    juce::AudioBuffer<float> a (2, beats * kPerBeat);
    for (int i = 0; i < a.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            a.setSample (ch, i, (float) (i / kPerBeat + 1) / 20.0f);
    return a;
}

// Plays `beats` and returns what the left side heard.
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
                    RigEngine::synthStrip, RigEngine::inputStrip })
        engine.getStrip (st).mute = true;
}

RigTimeline::Block block (int lane, double start, double length, double fadeIn = 0, double fadeOut = 0)
{
    RigTimeline::Block b;
    b.lane = lane;
    b.start = start;
    b.length = length;
    b.fadeIn = fadeIn;
    b.fadeOut = fadeOut;
    return b;
}
}

struct RigTimelineTests : juce::UnitTest
{
    RigTimelineTests() : juce::UnitTest ("Rig timeline", "Rig") {}

    void runTest() override
    {
        beginTest ("blocks: where a lane plays, and its fades");
        {
            RigTimeline t;
            expectEquals (t.gainAt (0, 3.0), 1.0f, "a lane with no blocks plays live");
            t.getBlocks() = { block (0, 2, 4, 1, 2), block (9, 0, 8) };
            t.changed();
            expectEquals (t.gainAt (0, 1.0), 0.0f, "before its block");
            expectWithinAbsoluteError (t.gainAt (0, 2.5), std::sin (0.25f * juce::MathConstants<float>::pi), 1.0e-4f, "half way into the fade in");
            expectEquals (t.gainAt (0, 3.5), 1.0f);
            expectWithinAbsoluteError (t.gainAt (0, 5.0), std::sin (0.25f * juce::MathConstants<float>::pi), 1.0e-4f, "half way through the fade out");
            expectEquals (t.gainAt (0, 6.0), 0.0f, "after it");
            expectEquals (t.gainAt (1, 3.0), 1.0f, "another loop's lane is still live");
            expectEquals (t.lengthBars(), 8.0);
            expectEquals (t.blockAt (0, 4.0), 1, "sorted by start: the Bass block is first");
            expectEquals (RigTimeline::blockStartAt (t.getBlocks().data(), 2, 0, 3.0), 2.0);

            t.getBlocks().push_back (block (0, 10, 2, 3, 3));
            t.changed();
            const auto& b = t.getBlocks().back();
            expectLessOrEqual (b.fadeIn + b.fadeOut, b.length, "fades fit in the block");

            t.getMarkers().push_back ({ 4.0, "Chorus" });
            t.cycle = true;
            t.cycleStart = 2.0;
            t.cycleEnd = 6.0;
            RigTimeline copy;
            copy.fromTree (t.toTree());
            expectEquals ((int) copy.getBlocks().size(), 3);
            expectEquals (copy.getMarkers().front().name, juce::String ("Chorus"));
            expect (copy.cycle.load());
            expectEquals (copy.cycleEnd.load(), 6.0);
            expectEquals (copy.gainAt (0, 3.5), 1.0f);
            copy.fromTree ({});
            expect (copy.isEmpty(), "an old show has no timeline");
        }

        beginTest ("playing the timeline: a loop plays in its block, from its top, then the song stops");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getTransport().setBpm (120.0);
            onlyLoops (engine);
            engine.getLooper().setLayerAudio (0, countingLoop (8), 8.0);
            engine.getLooper().getLayer (0).level = 1.0f;
            engine.getLooper().getLayer (0).mute = true;   // muted by a part before: the timeline brings it back
            auto& timeline = engine.getTimeline();
            timeline.getBlocks() = { block (0, 1, 2) };   // bars 2-3
            timeline.changed();

            engine.playTimeline (0);
            expect (! engine.getLooper().getLayer (0).mute.load(), "the timeline unmutes what it plays");
            const auto heard = run (engine, 18);
            const auto at = [&] (double beat) { return std::abs (heard[(size_t) (beat * kPerBeat)]); };
            expectLessThan (at (2.5), 1.0e-4f, "bar 1: not in a block");
            expectGreaterThan (at (4.5), 1.0e-3f, "bar 2: its block");
            expectWithinAbsoluteError (at (5.5) / at (4.5), 2.0f, 0.05f, "from the loop's first beat");
            expectWithinAbsoluteError (at (11.5) / at (4.5), 8.0f, 0.2f);
            expectLessThan (at (12.5), 1.0e-4f, "bar 4: after the block");
            expect (! engine.getTransport().isPlaying(), "stops a bar after the end");
        }

        beginTest ("fades: one loop fades out while another fades in");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getTransport().setBpm (120.0);
            onlyLoops (engine);
            juce::AudioBuffer<float> steady (2, 4 * kPerBeat);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (steady.getWritePointer (ch), 0.1f, steady.getNumSamples());
            engine.getLooper().setLayerAudio (0, steady, 4.0);
            engine.getLooper().getLayer (0).level = 1.0f;
            auto& timeline = engine.getTimeline();
            timeline.getBlocks() = { block (0, 0, 4, 0, 2) };   // fades out over bars 3-4
            timeline.changed();
            engine.playTimeline (0);
            const auto heard = run (engine, 16);
            const auto at = [&] (double beat) { return std::abs (heard[(size_t) (beat * kPerBeat)]); };
            const float full = at (2.0);
            expectGreaterThan (full, 0.01f);
            expectWithinAbsoluteError (at (12.0) / full, std::sin (0.25f * juce::MathConstants<float>::pi), 0.02f, "half way through the fade");
            expectLessThan (at (14.0), at (12.0));
            expectLessThan (at (15.9) / full, 0.05f, "nearly silent at the end");
        }

        beginTest ("cycle and jumps land exactly on the beat");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getTransport().setBpm (120.0);
            onlyLoops (engine);
            auto& timeline = engine.getTimeline();
            timeline.getBlocks() = { block (8, 0, 16) };
            timeline.changed();
            timeline.cycle = true;
            timeline.cycleStart = 0.0;
            timeline.cycleEnd = 1.0;
            engine.playTimeline (0);
            run (engine, 6);
            expectWithinAbsoluteError (engine.getTransport().getPositionInBeats(), 2.0, 0.02, "round the one-bar cycle");

            timeline.cycle = false;
            timeline.jumpTo (8.0);
            run (engine, 3);   // at beat 4 (the next bar) it goes to bar 9 (beat 32)
            expectWithinAbsoluteError (engine.getTransport().getPositionInBeats(), 33.0, 0.02, "jumped at the bar");
            expect (timeline.getPendingJump() < 0);

            engine.getTransport().stop();
            run (engine, 1);
            engine.playTimeline (4.0);
            run (engine, 0.5);
            expectWithinAbsoluteError (engine.getTransport().getPositionInBeats(), 16.5, 0.02, "Play starts where you pick");
        }

        beginTest ("the timeline screen: add, copy, fade, undo, from the song grid's parts, saved with the show");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getTransport().setBpm (120.0);
            engine.getLooper().setLayerAudio (0, countingLoop (8), 8.0);
            engine.getLooper().setLayerAudio (1, countingLoop (4), 4.0);
            TimelineView view (engine);
            view.setBounds (0, 0, 1400, 700);
            auto& timeline = engine.getTimeline();

            const int first = view.addBlock (0, 0);
            expectEquals (first, 0);
            expectEquals (timeline.getBlocks()[0].length, 2.0, "a loop's block is the loop's length");
            expectEquals (timeline.getBlocks()[(size_t) view.addBlock (9, 2)].length, 4.0, "a tool's is 4 bars");
            view.duplicateSelected();   // nothing picked
            expectEquals ((int) timeline.getBlocks().size(), 2);
            const auto rows = SongGrid::rowList();
            juce::ignoreUnused (rows);
            view.setFade (0, true, 1.0);
            expectEquals (timeline.getBlocks()[0].fadeIn, 1.0);
            view.undo();
            expectEquals (timeline.getBlocks()[0].fadeIn, 0.0, "undo");

            // From the song grid's parts.
            RigScene a, b;
            a.name = "Intro";
            a.bars = 2;
            a.state.loopMutes[0] = 0;
            a.state.loopMutes[1] = 1;
            b.name = "Verse";
            b.bars = 4;
            b.state.loopMutes[0] = 0;
            b.state.loopMutes[1] = 0;
            engine.getScenes().getScenes() = { a, b };
            view.fromParts();
            expectEquals ((int) timeline.getMarkers().size(), 2);
            expectEquals (timeline.getMarkers()[1].bar, 2.0);
            const int loop2 = timeline.blockAt (1, 3.0);
            expect (loop2 >= 0, "loop 2 comes in with the verse");
            if (loop2 >= 0)
            {
                expectEquals (timeline.getBlocks()[(size_t) loop2].start, 2.0);
                expectEquals (timeline.getBlocks()[(size_t) loop2].length, 4.0);
            }
            expect (timeline.blockAt (0, 0.5) >= 0, "loop 1 plays all the way");
            expect (timeline.blockAt (1, 0.5) < 0);

            const auto show = engine.saveShow();
            RigEngine other;
            other.loadShow (show);
            expectEquals ((int) other.getTimeline().getBlocks().size(), (int) timeline.getBlocks().size(), "saved with the show");
            expectEquals (other.getTimeline().getMarkers()[0].name, juce::String ("Intro"));

            // Export follows the timeline.
            engine.getLooper().getLayer (1).level = 1.0f;
            const auto stem = RigExport::renderTimelineStem (engine, 1, 120.0, kRate, false);
            expectEquals (stem.getNumSamples(), 6 * 4 * kPerBeat, "the song is 6 bars");
            expectLessThan (std::abs (stem.getSample (0, kPerBeat * 4)), 1.0e-6f, "loop 2 is out in the intro");
            expectWithinAbsoluteError (stem.getSample (0, kPerBeat * 8 + 300), 0.05f, 1.0e-4f, "and starts from its top in the verse");
            expectWithinAbsoluteError (stem.getSample (0, kPerBeat * 13 + 300), 0.10f, 1.0e-4f);

            const auto parent = juce::File::createTempFile ("rigtimeline");
            parent.createDirectory();
            const auto result = RigExport::exportForLogic (engine, parent);
            expect (result.error.isEmpty(), result.error);
            expectEquals (result.folder.getChildFile ("Song").findChildFiles (juce::File::findFiles, false, "*.wav").size(), 2);
            expect (result.folder.getChildFile ("Read me.txt").loadFileAsString().contains ("bar 3: Verse"));
            parent.deleteRecursively();

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TIMELINE_SNAPSHOT", {}); path.isNotEmpty())
            {
                timeline.getBlocks().push_back (block (9, 0, 2, 0, 1));
                timeline.getBlocks().push_back (block (9, 2, 6, 2, 0));
                timeline.getBlocks().push_back (block (11, 4, 4, 1, 1));
                timeline.getBlocks().push_back (block (0, 6, 4, 0, 2));
                timeline.changed();
                view.setCycle (2, 6);
                view.setStartBar (2);
                view.refresh();
                view.addBlock (2, 8);
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
                juce::File file (path);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
        }

        beginTest ("the Looper opens the timeline (and closes the song grid)");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            LooperView looper (engine);
            looper.setBounds (0, 0, 1180, 760);
            looper.showSongGrid (true);
            looper.showTimeline (true);
            expect (looper.getTimeline() != nullptr && looper.getTimeline()->isVisible());
            expect (looper.getSongGrid() == nullptr, "one at a time");
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_LOOPER_TIMELINE_SNAPSHOT", {}); path.isNotEmpty())
            {
                const auto image = looper.createComponentSnapshot (looper.getLocalBounds(), true, 1.0f);
                juce::File file (path);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
            looper.showTimeline (false);
            expect (looper.getTimeline() == nullptr);
        }
    }
};

static RigTimelineTests rigTimelineTests;
