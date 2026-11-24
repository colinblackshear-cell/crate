#include "RigDirtyTestHelpers.h"

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
static constexpr bool sanitized = true;
#else
static constexpr bool sanitized = false;
#endif

// Overdub and undo with the dirty model on (the overdub/undo design's test 20): a clean show,
// an overdub, then its undo, by swap and by restoration. The loop's version rises once at the
// undo (and never while the capture or the restoration runs alone), a hash of an older version
// is discarded, and the show is clean again.

namespace
{
using namespace dirtytest;
using DirtyState = RigDirty::State;

class RigOverdubUndoShowTests final : public juce::UnitTest
{
public:
    RigOverdubUndoShowTests() : juce::UnitTest ("Rig overdub undo with the show", "Rig") {}

    juce::File folder, show;

    void makeShow()
    {
        folder = tempFolder ("RigOverdubUndoShowTests");
        show = folder.getChildFile ("Show.crateshow");
        auto engine = std::make_unique<RigEngine>();   // (too big for the stack)
        auto& e = *engine;
        e.prepare (kRate, kBlock);
        e.getTransport().setBpm (120.0);
        // Loop 1 is the longest (the room is its size: undo swaps); loop 2 is shorter than the
        // room (undo restores).
        e.getLooper().setLayerAudio (0, noise ((int) kRate * 40, 11), 80.0);
        e.getLooper().setLayerAudio (1, noise ((int) kRate * 34, 12), 68.0);
        Runner r { e };
        r.seconds (0.05);
        const auto saved = e.saveShowFile (show);
        expect (show.existsAsFile(), "saved");
        juce::ignoreUnused (saved);
    }

    // Overdub `layer` for two beats, stop, and undo it right as the dirty model's hash of the
    // overdubbed loop is on its way.
    void overdubAndUndo (int layer, bool swapExpected)
    {
        auto e = openSaved (show);
        auto& looper = e->getLooper();
        Runner r { *e };
        e->getTransport().play();
        looper.maintain();
        r.beats (2);
        expect (settle (*e).state == DirtyState::clean, "clean before the overdub");

        // The engine played without its timers (so the dirty model asks for no hash yet).
        const auto playOnly = [&] (int blocks)
        {
            juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
            in.clear();
            for (int i = 0; i < blocks; ++i)
                e->process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
        };

        r.input = true;
        looper.pressRecord (layer);
        r.beats (2);
        e->getDirty().resetStats();
        looper.pressRecord (layer);
        r.input = false;
        playOnly (1);
        // Let the capture finish (it's discretionary: a few blocks for these lengths). The
        // capture alone never changes the version.
        const int stopped = looper.getLayer (layer).version.load();
        playOnly (50);
        expectEquals (looper.getLayer (layer).version.load(), stopped, "the capture alone never changes the version");
        expect (looper.isUndoCaptureCompleteForTests(), "the capture finished");
        expect (looper.canUndo (layer), "the overdub has its undo");

        // The hash of the overdubbed loop is asked for, and copied (the engine plays on, its
        // timers held so the result waits); then the undo, before the model takes the result.
        expectEquals (e->getDirty().getStats().hashJobsPosted, 0, "no hash asked for yet");
        int posted = 0;
        for (int i = 0; i < 2000 && posted == 0; ++i)
        {
            r.block();
            posted = e->getDirty().getStats().hashJobsPosted;
        }
        expect (posted > 0, "the overdubbed loop's hash was asked for");
        for (int i = 0; i < 200 && e->getDirty().getStats().jobsStarted == 0; ++i)
            juce::Thread::sleep (2);
        playOnly (100);   // the copy is made a slice a block
        juce::Thread::sleep (sanitized ? 5000 : 1000);   // and hashed
        const auto counters0 = looper.getCopyCounters();
        const int before = looper.getLayer (layer).version.load();
        looper.undo (layer);
        playOnly (1);
        const int at = looper.getLayer (layer).version.load();
        expectEquals (at, before + 1, "the version rises once, in the block that handles the undo");
        const bool restoringNow = looper.isUndoRestoring();
        // While any restoration finishes, the version stays.
        int moved = 0;
        for (int i = 0; i < 3000 && looper.isUndoRestoring(); ++i)
        {
            r.block();
            moved += looper.getLayer (layer).version.load() != at ? 1 : 0;
        }
        expectEquals (moved, 0, "the restoration alone never changes the version");
        expect (! looper.isUndoRestoring(), "the restoration finished");
        const auto counters1 = looper.getCopyCounters();
        const auto swaps = counters1.swaps - counters0.swaps, restorations = counters1.restorations - counters0.restorations;
        logMessage ("  loop " + juce::String (layer + 1) + ": " + (swaps > 0 ? "by swap" : "by restoration")
                    + (restoringNow ? " (restoring after the undo's block)" : ""));
        if (swapExpected)
            expect (swaps == 1 && restorations == 0, "undone by swap");
        else
            expect (swaps == 0 && restorations == 1, "undone by restoration");

        for (int i = 0; i < 400 && e->getDirty().checkNow().state != DirtyState::clean; ++i)
            r.beats (0.25);
        const auto s = settle (*e);
        expect (s.state == DirtyState::clean, "clean again after the undo: " + stateName (s.state));
        const auto stats = e->getDirty().getStats();
        logMessage ("  hashes posted " + juce::String (stats.hashJobsPosted) + ", completed " + juce::String (stats.hashJobsCompleted)
                    + ", stale results discarded " + juce::String (stats.staleResults) + ", discarded " + juce::String (stats.discarded));
        expectGreaterThan (stats.hashJobsPosted, 1, "the undone loop was hashed again");
        expectGreaterOrEqual (stats.staleResults + stats.discarded, 1, "the overdubbed version's hash was discarded");
    }

    void runTest() override
    {
        beginTest ("20. Dirty tracking: a clean show, an overdub, undo by swap: clean again, the version once");
        makeShow();
        overdubAndUndo (0, true);
        beginTest ("20. Dirty tracking: a clean show, an overdub, undo by restoration: clean again, the version once");
        overdubAndUndo (1, false);
        folder.deleteRecursively();
    }
};

static RigOverdubUndoShowTests rigOverdubUndoShowTests;
} // namespace
