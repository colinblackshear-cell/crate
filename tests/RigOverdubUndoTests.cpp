#include "OverdubUndoHelpers.h"

#include "RigLooper.h"

#include <atomic>
#include <map>
#include <mutex>
#include <thread>

// Overdub and undo: what you hear and what is kept is exactly as before (group A), no loop ever
// misses a block (group B), and the audio thread's work in a block has a bound that doesn't
// depend on how long the loop is (group C). The numbers are those of the overdub/undo design's
// tests section.
//
// Tests marked "expected to fail on the old code" check what only the new undo makes true; they
// only report while RigLooper::kIncrementalUndo is false.

//==============================================================================
// Counting the audio thread's allocations: every malloc and free made while a thread has
// `countHere` set. Under the sanitizers through their hooks, on Linux by standing in for
// malloc; elsewhere not counted (the test says so).
namespace oduAlloc
{
thread_local bool countHere = false;
std::atomic<int> allocations { 0 }, frees { 0 };
}

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
 #include <sanitizer/allocator_interface.h>
namespace oduAlloc
{
constexpr bool counted = true;
void onMalloc (const volatile void*, size_t) { if (countHere) ++allocations; }
void onFree (const volatile void*) { if (countHere) ++frees; }
const int installed = __sanitizer_install_malloc_and_free_hooks (onMalloc, onFree);
}
#elif JUCE_LINUX
extern "C"
{
void* __libc_malloc (size_t);
void* __libc_calloc (size_t, size_t);
void* __libc_realloc (void*, size_t);
void __libc_free (void*);
void* malloc (size_t n)
{
    if (oduAlloc::countHere)
        ++oduAlloc::allocations;
    return __libc_malloc (n);
}
void* calloc (size_t a, size_t b)
{
    if (oduAlloc::countHere)
        ++oduAlloc::allocations;
    return __libc_calloc (a, b);
}
void* realloc (void* p, size_t n)
{
    if (oduAlloc::countHere)
        ++oduAlloc::allocations;
    return __libc_realloc (p, n);
}
void free (void* p)
{
    if (oduAlloc::countHere && p != nullptr)
        ++oduAlloc::frees;
    __libc_free (p);
}
}
namespace oduAlloc { constexpr bool counted = true; }
#else
namespace oduAlloc { constexpr bool counted = false; }
#endif

namespace
{
using odu::Drive;
using odu::same;
using State = RigLooper::State;
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr float kDub = 1.0f / 1024.0f;   // what the overdub adds each pass (exact on markedLoop)

// Samples of one beat at `bpm`.
inline double beatSamples (double rate, double bpm) { return rate * 60.0 / bpm; }

// A loop of `samples` on `layer` that plays at its natural speed at the clock's tempo.
void putLoop (Drive& d, int layer, const juce::AudioBuffer<float>& audio)
{
    d.looper.setLayerAudio (layer, audio, audio.getNumSamples() / beatSamples (d.rate, d.clock.bpm));
    d.looper.getLayer (layer).source = RigLooper::input;
}

void dubInput (Drive& d)
{
    d.input = [] (juce::int64, int) { return kDub; };
}

// Two looper drives with the same loops, one of which is overdubbed and undone: from the block
// that handles the undo on, they must sound the same, sample for sample.
struct Twin
{
    Drive a, b;
    int firstDiffBlock = -1;
    bool comparing = false;
    juce::int64 compared = 0;

    Twin (double rate, int block, double bpm = 120.0) : a (rate, block, bpm), b (rate, block, bpm) {}
    void loop (int layer, const juce::AudioBuffer<float>& audio)
    {
        putLoop (a, layer, audio);
        putLoop (b, layer, audio);
    }
    void block()
    {
        b.blockSize = a.blockSize;
        b.clock.bpm = a.clock.bpm;
        for (int l = 0; l < RigLooper::kLayers; ++l)
            b.looper.setBend (l, a.looper.getBend (l));
        const int n = a.block();
        b.block();
        if (comparing)
        {
            for (int ch = 0; ch < 2; ++ch)
                if (std::memcmp (a.out.getReadPointer (ch), b.out.getReadPointer (ch), sizeof (float) * (size_t) n) != 0 && firstDiffBlock < 0)
                    firstDiffBlock = (int) a.blockNumber - 1;
            compared += n;
        }
    }
    void blocks (int k)
    {
        for (int i = 0; i < k; ++i)
            block();
    }
};
}

//==============================================================================
struct RigOverdubUndoTests : juce::UnitTest
{
    RigOverdubUndoTests() : juce::UnitTest ("Rig overdub undo", "Rig") {}

    static bool longTests() { return juce::SystemStats::getEnvironmentVariable ("CRATE_LONG_LOOP_TESTS", {}) == "1"; }

    // Overdubs `layer` for `blocks`, stops, and returns the loop as it was before.
    juce::AudioBuffer<float> overdub (Drive& d, int layer, int blocks, bool stop = true)
    {
        auto before = d.looper.soundingLoopForTests (layer);
        d.looper.pressRecord (layer);
        d.blocks (juce::jmax (1, blocks));
        expect (d.looper.getState (layer) == State::overdubbing || blocks == 0, "it overdubs");
        if (stop)
        {
            d.looper.pressRecord (layer);
            d.block();
        }
        return before;
    }

    // Undo, then the loop is `original` at once and once any work left is done.
    void expectUndone (Drive& d, int layer, const juce::AudioBuffer<float>& original, const juce::String& what)
    {
        expect (d.looper.canUndo (layer), what + ": undo offered");
        d.looper.undo (layer);
        d.block();
        expect (! d.looper.canUndo (layer), what + ": undo used");
        expect (d.looper.getState (layer) == State::playing, what + ": playing");
        const int diff = odu::firstDifference (d.looper.soundingLoopForTests (layer), original);
        expect (diff == -1, what + ": the loop as it was, at once (first difference at " + juce::String (diff) + ")");
        d.seconds (4.0);
        expect (same (d.looper.copyLayerAudio (layer), original), what + ": and after");
    }

    void runTest() override
    {
        exact();
        manyPoints();
        overdubUnchanged();
        passes();
        singleUndo();
        cancelling();
        lengthsRatesBlocks();
        playbackRates();
        randomized();
    }

    //==========================================================================
    void exact()
    {
        beginTest ("1. Undo is exact");
        Drive d (kRate, kBlock);
        dubInput (d);
        putLoop (d, 0, odu::markedLoop (768000, 1));
        d.blocks (3);
        const auto before = d.looper.copyLayerAudio (0);
        overdub (d, 0, 400);
        expect (! same (d.looper.copyLayerAudio (0), before), "the overdub changed it");
        expectUndone (d, 0, before, "undo");
        expect (same (d.looper.copyLayerAudio (0), before), "a copy equals the copy before, bit for bit");
    }

    //==========================================================================
    // With a slowed budget (copySliceForTests: samples of copying a block), undo at points
    // through what would be the capture, and at every block of a short loop's.
    void manyPoints()
    {
        beginTest ("2. Undo at many points of the capture");
        RigLooper::copySliceForTests = 4 * RigLooper::kCopyPageSamples;   // 4 pages a block
        constexpr int pages = 750;                                        // an 8-bar loop at 120
        for (const double fraction : { 0.0, 0.01, 0.25, 0.5, 0.75, 0.99, 1.0, 1.1 })
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (pages * 1024, 2));
            d.blocks (2);
            const int blocks = juce::jmax (1, (int) std::round (fraction * pages / 4.0));
            const auto before = overdub (d, 0, blocks, fraction < 0.5);
            expectUndone (d, 0, before, juce::String (fraction * 100.0, 0) + "% of the capture");
        }
        // A 5-page loop: every block of its capture (2 pages a block).
        RigLooper::copySliceForTests = 2 * RigLooper::kCopyPageSamples;
        for (int k = 1; k <= 4; ++k)
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (5 * 1024 - 7, 3));
            d.blocks (2);
            const auto before = overdub (d, 0, k, false);
            expectUndone (d, 0, before, "block " + juce::String (k) + " of a 5-page loop's capture");
        }
        RigLooper::copySliceForTests = 0;
    }

    //==========================================================================
    void overdubUnchanged()
    {
        beginTest ("3. The overdub is unchanged: the engine's loop equals a plain model, bit for bit");
        for (const int liveDelay : { 0, 300 })
            for (int bend = 0; bend < RigLooper::numBends; ++bend)
            {
                Drive d (kRate, kBlock);
                const int n = 3 * 1024 * 16 + 100;   // passes the wrap several times
                const auto loop = odu::markedLoop (n, 4 + bend);
                putLoop (d, 0, loop);
                d.looper.setLiveDelay (liveDelay);
                d.input = [] (juce::int64 at, int ch) { return (float) ((at * 7 + ch * 3) % 64 - 32) / 4096.0f; };
                odu::DubModel model;
                model.loop = loop;
                model.lengthBeats = n / beatSamples (kRate, 120.0);
                model.live = true;   // the input is a live source
                d.after = [&] (const RigLooper::Clock& clock, const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>&, int blockSize)
                {
                    if (d.looper.getState (0) == State::overdubbing)
                        model.block (clock, kRate, liveDelay, in, blockSize);
                };
                d.blocks (1);
                d.looper.setBend (0, bend);
                d.looper.pressRecord (0);
                d.seconds (3.5);   // ~3.4 passes
                d.looper.pressRecord (0);
                d.block();
                const int diff = odu::firstDifference (d.looper.copyLayerAudio (0), model.loop);
                expect (diff == -1, "live delay " + juce::String (liveDelay) + ", " + RigLooper::bendName (bend) + ": first difference at " + juce::String (diff));
            }
    }

    //==========================================================================
    void passes()
    {
        beginTest ("4. Repeated passes: undo takes back the whole overdub; overdub, stop, overdub, undo takes back the second");
        const int n = 48000;   // 1 s
        for (const int times : { 1, 2, 3 })
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (n, 5));
            d.block();
            const auto before = overdub (d, 0, (int) std::ceil (times * n / (double) kBlock));
            expectUndone (d, 0, before, juce::String (times) + " time(s) round");
        }
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (n, 6));
            d.block();
            overdub (d, 0, 100);
            d.blocks (20);
            const auto afterFirst = d.looper.copyLayerAudio (0);
            overdub (d, 0, 100);
            expectUndone (d, 0, afterFirst, "overdub, stop, overdub");
        }
    }

    //==========================================================================
    void singleUndo()
    {
        beginTest ("5. Single global undo");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (40000, 7));
            putLoop (d, 1, odu::markedLoop (50000, 8));
            d.block();
            const auto a0 = d.looper.copyLayerAudio (0);
            overdub (d, 0, 50);
            const auto a1 = d.looper.copyLayerAudio (0);
            const auto b0 = overdub (d, 1, 50);
            expect (! d.looper.canUndo (0) && d.looper.canUndo (1), "only B is offered");
            d.looper.undo (0);
            d.block();
            expect (same (d.looper.copyLayerAudio (0), a1), "undo on A does nothing");
            expectUndone (d, 1, b0, "B");
            d.looper.undo (1);
            d.block();
            d.seconds (1.0);
            expect (same (d.looper.copyLayerAudio (1), b0), "undo twice: the second does nothing");
            expect (same (d.looper.copyLayerAudio (0), a1));
        }
        {
            // B over 10 minutes has no undo: A's survives. (At 8 kHz, so it's small.)
            constexpr double rate = 8000.0;
            Drive d (rate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (40000, 9));
            putLoop (d, 1, odu::markedLoop ((int) (rate * RigLooper::kMaxUndoSeconds) + 1, 10));
            d.looper.maintain();
            d.block();
            const auto a0 = overdub (d, 0, 30);
            overdub (d, 1, 30);
            expect (d.looper.canUndo (0) && ! d.looper.canUndo (1), "B (10 min and a sample) has no undo; A's stays");
            expectUndone (d, 0, a0, "A after B");
        }
    }

    //==========================================================================
    // Each event during the overdub's capture (a slowed budget), during an undo's restoration,
    // and after: undo goes, and a later undo changes nothing.
    void cancelling()
    {
        beginTest ("6. Events that cancel the undo");
        const juce::StringArray events { "clear", "new sound", "bank swap", "quantize", "trim", "show load", "prepare" };
        for (const auto& event : events)
            for (int when = 0; when < 3; ++when)   // during the capture, during a restoration, after
            {
                RigLooper::copySliceForTests = when == 2 ? 0 : 2 * RigLooper::kCopyPageSamples;
                Drive d (kRate, kBlock);
                dubInput (d);
                putLoop (d, 0, odu::markedLoop (300 * 1024, 11));
                d.blocks (2);
                overdub (d, 0, 40);
                if (when == 1)
                {
                    d.looper.undo (0);
                    d.block();
                    overdub (d, 0, 40);   // (a restoration needs an undo first: this one's is kept)
                    d.looper.undo (0);
                    d.block();
                }
                if (when == 2)
                    d.seconds (2.0);
                const bool undoBefore = d.looper.canUndo (0);
                auto other = odu::markedLoop (300 * 1024, 12);
                const double beats = other.getNumSamples() / beatSamples (kRate, 120.0);
                if (event == "clear")
                    d.looper.clear (0);
                else if (event == "new sound")
                    d.looper.setLayerAudio (0, other, beats);
                else if (event == "bank swap")
                    d.looper.queueLayerAudio (0, other, beats, -1.0, 0.8f, false);
                else if (event == "quantize")
                    d.looper.replaceLayerAudio (0, other);
                else if (event == "trim")
                {
                    d.looper.openTrim (0);
                    d.looper.setTrim (0, 0.0, beats * 0.5, beats * 0.5);
                }
                else if (event == "show load")
                    d.looper.reset (0);
                else if (event == "prepare")
                    d.looper.prepare (kRate, kBlock);
                d.block();
                const auto now = d.looper.copyLayerAudio (0);
                const juce::String what = event + (when == 0 ? " during the capture" : when == 1 ? " during a restoration" : " after");
                expect (when == 1 || undoBefore, what + ": there was an undo");
                expect (! d.looper.canUndo (0), what + ": no undo after it");
                d.looper.undo (0);
                d.block();
                d.seconds (0.5);
                expect (same (d.looper.copyLayerAudio (0), now), what + ": a later undo changes nothing");
                d.looper.maintain();
            }
        RigLooper::copySliceForTests = 0;
    }

    //==========================================================================
    void lengthsRatesBlocks()
    {
        beginTest ("7. Lengths, sample rates and block sizes");
        // Lengths: a sample under a page, a page, a sample over, 8 bars, 10 minutes.
        for (const int n : { 1023, 1024, 1025, 768000, (int) (kRate * RigLooper::kMaxUndoSeconds) })
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (n, 13));
            d.looper.maintain();
            d.blocks (2);
            const auto before = overdub (d, 0, 200);
            expectUndone (d, 0, before, juce::String (n) + " samples");
        }
        {
            // 10 minutes and a sample: no undo (the overdub still happens).
            Drive d (kRate, kBlock);
            dubInput (d);
            const int n = (int) (kRate * RigLooper::kMaxUndoSeconds) + 1;
            putLoop (d, 0, odu::markedLoop (n, 14));
            d.looper.maintain();
            d.blocks (2);
            const auto before = overdub (d, 0, 200);
            expect (! d.looper.canUndo (0), "10 minutes and a sample: no undo");
            expect (! same (d.looper.copyLayerAudio (0), before), "but it overdubbed");
        }
        // Every rate with every block size, and a host that changes the size every block.
        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const int block : { 16, 32, 64, 256, 1024, 4096, 8192, -1 })
            {
                Drive d (rate, block < 0 ? 1024 : block);
                if (block < 0)
                    d.blockSize = [] (juce::int64 k) { return 1 + (int) ((k * 7919) % 1024); };
                dubInput (d);
                putLoop (d, 0, odu::markedLoop (40 * 1024 + 333, 15));
                d.blocks (2);
                const auto before = d.looper.copyLayerAudio (0);
                d.looper.pressRecord (0);
                d.seconds (0.4);
                d.looper.pressRecord (0);
                d.block();
                expectUndone (d, 0, before, juce::String (rate / 1000.0, 1) + " kHz, " + (block < 0 ? juce::String ("varying") : juce::String (block)) + " samples");
            }
        if (longTests())
        {
            for (const double rate : { 96000.0, 192000.0 })
            {
                Drive d (rate, kBlock);
                dubInput (d);
                putLoop (d, 0, odu::markedLoop ((int) (rate * RigLooper::kMaxUndoSeconds), 16));
                d.looper.maintain();
                d.blocks (2);
                const auto before = overdub (d, 0, 200);
                expectUndone (d, 0, before, "10 minutes at " + juce::String (rate / 1000.0, 0) + " kHz");
            }
            {
                // 60 minutes at 48 kHz: the press does a constant amount, with no undo.
                Drive d (kRate, kBlock);
                dubInput (d);
                putLoop (d, 0, odu::markedLoop ((int) (kRate * 3600.0), 17));
                d.looper.maintain();
                d.blocks (2);
                d.timing = true;
                d.looper.pressRecord (0);
                d.block();
                d.timing = false;
                logMessage ("  60-minute loop: the press's block took " + juce::String (d.wall.most(), 0) + " us");
                expect (! d.looper.canUndo (0));
                expectLessThan (d.wall.most(), 20000.0, "no copy of the loop at the press");
            }
        }
        else
            logMessage ("  (10 minutes at 96 and 192 kHz and 60 minutes run with CRATE_LONG_LOOP_TESTS=1)");
    }

    //==========================================================================
    void playbackRates()
    {
        beginTest ("8. Playback rates: a loop made at 40 BPM at 240, a tempo ramp, a sound squeezed to the limit");
        {
            Drive d (kRate, kBlock, 240.0);
            dubInput (d);
            const int n = 4 * (int) beatSamples (kRate, 40.0);   // one bar at 40 BPM
            d.looper.setLayerAudio (0, odu::markedLoop (n, 18), 4.0);
            d.looper.getLayer (0).source = RigLooper::input;
            d.block();
            const auto before = overdub (d, 0, 300);
            expectUndone (d, 0, before, "made at 40 BPM, played at 240");
        }
        {
            Drive d (kRate, kBlock, 40.0);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (8 * 24000, 19));
            d.block();
            const auto before = d.looper.copyLayerAudio (0);
            d.looper.pressRecord (0);
            for (int k = 0; k < 600; ++k)
            {
                d.clock.bpm = 40.0 + 200.0 * k / 600.0;   // up to 240
                d.block();
            }
            d.looper.pressRecord (0);
            d.block();
            expectUndone (d, 0, before, "a tempo ramp from 40 to 240 BPM during the overdub");
        }
        {
            // Squeezed: 768,000 samples a beat is the most (G) allows at 48 kHz.
            Drive d (kRate, kBlock);
            dubInput (d);
            const int n = 768000 * 2;
            d.looper.setLayerAudio (0, odu::markedLoop (n, 20), 2.0);   // just at the limit: has undo
            d.looper.getLayer (0).source = RigLooper::input;
            d.looper.maintain();
            d.block();
            const auto before = overdub (d, 0, 300);
            expectUndone (d, 0, before, "squeezed to just under the limit");
        }
    }

    //==========================================================================
    // Random presses, undos, clears, new sounds, bank swaps, saves (on another thread), room
    // growth, bends, tempo changes, relocations and clock stops, checking every loop against a
    // plain model after every block. A failure prints its seed.
    void randomized()
    {
        beginTest ("9. Randomized sequences against a plain model");
        for (const int seed : { 1, 2, 3 })
            randomRun (seed, 6000);
    }

    struct ModelLayer
    {
        juce::AudioBuffer<float> loop;   // empty: nothing
        double lengthBeats = 0, startBeat = 0;
        juce::AudioBuffer<float> undoImage;
        State before = State::empty;
    };

    static juce::uint64 hashOf (const juce::AudioBuffer<float>& b)
    {
        juce::uint64 h = 1469598103934665603ull ^ (juce::uint64) b.getNumSamples();
        for (int ch = 0; ch < b.getNumChannels() && b.getNumSamples() > 0; ++ch)
        {
            const auto* p = reinterpret_cast<const juce::uint32*> (b.getReadPointer (ch));
            for (int i = 0; i < b.getNumSamples(); ++i)
                h = (h ^ p[i]) * 1099511628211ull;
        }
        return h;
    }

    void randomRun (int seed, int blockCount)
    {
        constexpr int layers = 4;
        juce::Random random (seed);
        Drive d (kRate, 512);
        d.blockSize = [&random] (juce::int64) { return 16 + random.nextInt (497); };
        d.input = [] (juce::int64 at, int ch) { return (float) ((at * 13 + ch * 5) % 32 - 16) / 8192.0f; };
        std::array<ModelLayer, layers> model;
        int holder = -1, silentLosses = 0;
        const auto newSound = [&] (int)
        {
            const int n = 3000 + random.nextInt (40000);
            auto audio = odu::markedLoop (n, seed * 1000 + random.nextInt (100000));
            const double beats = n / beatSamples (kRate, 120.0) * (0.5 + random.nextDouble());
            return std::make_pair (audio, beats);
        };
        const auto install = [&] (int l, const juce::AudioBuffer<float>& audio, double beats, double start)
        {
            model[(size_t) l].loop = audio;
            model[(size_t) l].lengthBeats = beats;
            model[(size_t) l].startBeat = start;
            if (holder == l)
                holder = -1;
        };
        for (int l = 0; l < layers; ++l)
        {
            auto [audio, beats] = newSound (l);
            d.looper.setLayerAudio (l, audio, beats);
            d.looper.getLayer (l).source = RigLooper::input;
            install (l, audio, beats, 0.0);
        }

        // Saves on another thread: each copy must be the model's loop at the copy's version.
        std::map<std::pair<int, int>, juce::uint64> hashes;
        juce::CriticalSection hashLock;
        std::atomic<bool> running { true };
        std::atomic<int> copies { 0 }, badCopies { 0 };
        std::vector<std::tuple<int, int, juce::uint64>> got;
        juce::CriticalSection gotLock;
        std::thread saver ([&]
        {
            juce::Random r (seed + 99);
            while (running)
            {
                const int l = r.nextInt (layers);
                int version = -1;
                const auto copy = d.looper.copyLayerAudio (l, &version);
                if (version >= 0)
                {
                    const juce::ScopedLock sl (gotLock);
                    got.emplace_back (l, version, hashOf (copy));
                }
                juce::Thread::sleep (2);
            }
        });

        std::array<bool, layers> undoAsked {};
        std::array<double, layers> restartAt {};
        std::array<bool, layers> pressed {};
        bool restarted = false;
        d.after = [&] (const RigLooper::Clock& clock, const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>&, int n)
        {
            for (int l = 0; l < layers; ++l)
            {
                auto& m = model[(size_t) l];
                const auto st = d.looper.getState (l);
                if (restarted && clock.playing && m.loop.getNumSamples() > 0)
                    m.startBeat = std::floor (clock.beat / 4 + 1.0e-9) * 4;
                if (restartAt[(size_t) l] >= 0 && m.loop.getNumSamples() > 0)
                    m.startBeat = restartAt[(size_t) l];
                if (undoAsked[(size_t) l] && holder == l)
                {
                    if (m.before == State::playing || m.before == State::overdubbing)
                        m.loop = m.undoImage;
                    holder = -1;
                }
                // A new overdub: with undo if the looper offers it now.
                if (m.before == State::playing && st == State::overdubbing && d.looper.canUndo (l))
                {
                    m.undoImage = m.loop;
                    holder = l;
                }
                if (st == State::overdubbing)
                {
                    odu::DubModel w;
                    w.loop = m.loop;
                    w.lengthBeats = m.lengthBeats;
                    w.startBeat = m.startBeat;
                    w.live = true;
                    w.block (clock, kRate, 0, in, n);
                    m.loop = w.loop;
                }
                m.before = st;
            }
            restarted = false;
        };

        int checks = 0, wrong = 0, wrongUndo = 0;
        juce::String firstWrong;
        for (int k = 0; k < blockCount; ++k)
        {
            undoAsked = {};
            pressed = {};
            restartAt.fill (-1.0);
            const int l = random.nextInt (layers);
            const int what = random.nextInt (1000);
            auto& m = model[(size_t) l];
            if (what < 20)
            {
                d.looper.pressRecord (l);
                pressed[(size_t) l] = true;
                if (m.before == State::empty)
                    d.looper.pressRecord (l);   // (an empty layer would arm: press again to change its mind)
            }
            else if (what < 32)
            {
                d.looper.undo (l);
                undoAsked[(size_t) l] = true;
            }
            else if (what < 35 && m.before == State::playing)
            {
                d.looper.clear (l);
                d.block();
                m.loop = {};
                m.before = State::empty;
                if (holder == l)
                    holder = -1;
                auto [audio, beats] = newSound (l);
                d.looper.setLayerAudio (l, audio, beats);
                install (l, audio, beats, 0.0);
                m.before = State::playing;
            }
            else if (what < 38 && m.before == State::playing)
            {
                auto [audio, beats] = newSound (l);
                d.looper.queueLayerAudio (l, audio, beats, -1.0, 0.8f, false);
                install (l, audio, beats, juce::jmax (0.0, -1.0));
            }
            else if (what < 50)
                d.looper.maintain();
            else if (what < 60)
                d.looper.setBend (l, random.nextInt (RigLooper::numBends));
            else if (what < 66)
                d.clock.bpm = 60.0 + random.nextInt (150);
            else if (what < 70 && m.before != State::empty)
            {
                const double at = std::floor (d.clock.beat);
                d.looper.restartAt (l, at);
                restartAt[(size_t) l] = at;
            }
            else if (what < 73)
            {
                d.clock.playing = ! d.clock.playing;
                restarted = d.clock.playing;
            }
            d.block();

            // Every loop against the model.
            for (int i = 0; i < layers; ++i)
            {
                const auto sounding = d.looper.soundingLoopForTests (i);
                if (! same (sounding, model[(size_t) i].loop) && ! (sounding.getNumSamples() == 0 && model[(size_t) i].loop.getNumSamples() == 0))
                {
                    if (wrong++ == 0)
                        firstWrong = "seed " + juce::String (seed) + ", block " + juce::String (k) + ", loop " + juce::String (i + 1)
                                     + ": first difference at " + juce::String (odu::firstDifference (sounding, model[(size_t) i].loop));
                    model[(size_t) i].loop = sounding;   // carry on from what it is
                }
                {
                    const juce::ScopedLock sl (hashLock);
                    hashes[{ i, d.looper.getLayer (i).version.load() }] = hashOf (model[(size_t) i].loop);
                }
                ++checks;
            }
            if (holder >= 0 && ! d.looper.canUndo (holder))
            {
                ++silentLosses;   // the old code drops an undo when the room grows
                holder = -1;
            }
            for (int i = 0; i < layers; ++i)
                wrongUndo += d.looper.canUndo (i) != (holder == i) ? 1 : 0;
        }
        running = false;
        saver.join();
        int checkedCopies = 0;
        for (auto& [l, v, h] : got)
            if (auto it = hashes.find ({ l, v }); it != hashes.end())
            {
                ++checkedCopies;
                badCopies += it->second == h ? 0 : 1;
            }
        logMessage ("  seed " + juce::String (seed) + ": " + juce::String (checks) + " loop checks, " + juce::String (checkedCopies)
                    + " saves checked, " + juce::String (silentLosses) + " undos lost without an event");
        expectEquals (wrong, 0, firstWrong);
        expectEquals (wrongUndo, 0, "seed " + juce::String (seed) + ": undo offered exactly as the model says");
        expectEquals (badCopies.load(), 0, "seed " + juce::String (seed) + ": every save is the loop at its version");
        expectGreaterThan (checkedCopies, 10);
        if (RigLooper::kIncrementalUndo)
            expectEquals (silentLosses, 0, "seed " + juce::String (seed) + ": no undo is ever lost without an event");
    }
};

static RigOverdubUndoTests rigOverdubUndoTests;

//==============================================================================
// Group B: no loop misses a block, the others are untouched, and undo lands on time.
namespace
{
constexpr int kToneBins[] = { 6, 11, 17, 23 };   // x 187.5 Hz: whole cycles in a 256-sample block

float goertzel (const float* x, int n, int bin)
{
    const double w = juce::MathConstants<double>::twoPi * bin / n, c = 2.0 * std::cos (w);
    double s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (float) std::sqrt (juce::jmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / (float) n;
}

// A looper with four tone loops, run in real time on its own thread, counting the blocks where
// a tone is missing from the output, the late blocks, and the longest block.
struct LiveRig
{
    RigLooper looper;
    std::atomic<double> beat { 0 };
    juce::AudioBuffer<float> dc { 2, kBlock };
    std::atomic<bool> running { false }, warm { false }, measuring { false };
    // late: blocks whose work (the audio thread's CPU time) took longer than they play for;
    // lateWall: by the clock on the wall, which also counts the thread being put aside by the
    // system (this test's thread isn't a real-time one).
    std::atomic<int> blocks { 0 }, late { 0 }, lateWall { 0 }, missing { 0 };
    std::atomic<double> worstMs { 0 }, worstCpuMs { 0 };
    std::array<float, 4> reference {};
    std::thread thread;
    static constexpr double kBpm = kRate * 60.0 / 32768.0;   // whole blocks a beat

    explicit LiveRig (int loopSamples)
    {
        looper.prepare (kRate, kBlock);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (dc.getWritePointer (ch), kDub, kBlock);
        for (int t = 0; t < 4; ++t)
        {
            looper.setLayerAudio (t, odu::toneLoop (loopSamples, kToneBins[t]), loopSamples / 32768.0);
            looper.getLayer (t).source = RigLooper::input;
        }
        looper.maintain();
        running = true;
        thread = std::thread ([this] { run(); });
        while (! warm)
            juce::Thread::sleep (5);
    }
    ~LiveRig()
    {
        running = false;
        thread.join();
    }
    void run()
    {
        juce::AudioBuffer<float> out (2, kBlock);
        std::array<double, 4> sum {};
        const double period = 1000.0 * kBlock / kRate;
        auto next = juce::Time::getMillisecondCounterHiRes();
        for (int count = 0; running; ++count)
        {
            RigLooper::Clock clock;
            clock.beat = beat.load();
            clock.bpm = kBpm;
            clock.playing = true;
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            sources[RigLooper::input] = &dc;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto c0 = odu::threadCpuMicros();
            looper.process (clock, sources, out);
            const auto cpu = (odu::threadCpuMicros() - c0) / 1000.0;
            const auto took = juce::Time::getMillisecondCounterHiRes() - t0;
            beat = clock.beat + kBlock / 32768.0;
            if (measuring)
            {
                ++blocks;
                late += cpu > period ? 1 : 0;
                lateWall += took > period ? 1 : 0;
                if (took > worstMs.load())
                    worstMs = took;
                if (cpu > worstCpuMs.load())
                    worstCpuMs = cpu;
            }
            for (int t = 0; t < 4; ++t)
            {
                const float m = goertzel (out.getReadPointer (0), kBlock, kToneBins[t]);
                if (count < 50)
                    sum[(size_t) t] += m;
                else if (count == 50)
                    reference[(size_t) t] = (float) (sum[(size_t) t] / 50.0);
                else if (measuring && m < 0.5f * reference[(size_t) t])
                    ++missing;
            }
            if (count == 51)
                warm = true;
            next += period;
            if (const auto wait = next - juce::Time::getMillisecondCounterHiRes(); wait > 1.0)
                juce::Thread::sleep ((int) wait);
        }
    }
};
}

struct RigOverdubUndoGapTests : juce::UnitTest
{
    RigOverdubUndoGapTests() : juce::UnitTest ("Rig overdub undo gaps", "Rig") {}

    void runTest() override
    {
        beginTest ("12. Missing blocks: four 10-minute loops play while one is overdubbed and undone again and again");
        {
            LiveRig rig ((int) (kRate * RigLooper::kMaxUndoSeconds) / kBlock * kBlock);
            const auto skipped0 = rig.looper.getCopyCounters().skippedLayerBlocks;
            rig.measuring = true;
            for (int cycle = 0; cycle < 6; ++cycle)
            {
                rig.looper.pressRecord (0);
                juce::Thread::sleep (cycle % 2 == 0 ? 150 : 1200);   // undo during the capture, then after it
                if (cycle % 3 == 2)
                {
                    rig.looper.pressRecord (0);
                    juce::Thread::sleep (30);
                }
                rig.looper.undo (0);
                juce::Thread::sleep (cycle % 2 == 0 ? 400 : 1500);
                rig.looper.maintain();
            }
            rig.measuring = false;
            const auto c = rig.looper.getCopyCounters();
            logMessage ("  " + juce::String (rig.blocks.load()) + " blocks, " + juce::String (rig.missing.load()) + " with a loop missing, "
                        + juce::String (rig.late.load()) + " late by their work (longest " + juce::String (rig.worstCpuMs.load(), 2) + " ms CPU), "
                        + juce::String (rig.lateWall.load()) + " by the wall clock (longest " + juce::String (rig.worstMs.load(), 2) + " ms); layer blocks skipped "
                        + juce::String (c.skippedLayerBlocks - skipped0) + "; whole-loop copies " + juce::String (c.bytes[RigLooper::wholeLoop] / 1048576) + " MB");
            expectEquals (rig.missing.load(), 0);
            expectEquals ((int) (c.skippedLayerBlocks - skipped0), 0, "no layer's block skipped");
            if (RigLooper::kIncrementalUndo)
            {
                expectEquals (rig.late.load(), 0, "no block's work took longer than it plays for");
                expectEquals ((int) c.bytes[RigLooper::wholeLoop], 0, "no whole-loop copy");
            }
        }

        beginTest ("13. The other loops are untouched: their output equals a run with no overdub, sample for sample");
        {
            const int n = (int) (kRate * 60.0);
            Drive a (kRate, kBlock), b (kRate, kBlock);
            dubInput (a);
            dubInput (b);
            for (int t = 0; t < 4; ++t)
            {
                const auto loop = odu::toneLoop (n - 1000 * t, kToneBins[t]);
                putLoop (a, t, loop);
                if (t > 0)
                    putLoop (b, t, loop);
            }
            a.looper.getLayer (0).mute = true;   // only loop 1's own sound is left out
            RigLooper::copySliceForTests = 64 * RigLooper::kCopyPageSamples;   // a capture of many blocks
            int differentBlocks = 0, compared = 0;
            const auto both = [&] (int k)
            {
                for (int i = 0; i < k; ++i)
                {
                    const int bn = a.block();
                    b.block();
                    if (a.blockNumber < 400)
                        continue;   // (loop 1's level fading to its mute)
                    ++compared;
                    for (int ch = 0; ch < 2; ++ch)
                        if (std::memcmp (a.out.getReadPointer (ch), b.out.getReadPointer (ch), sizeof (float) * (size_t) bn) != 0)
                        {
                            ++differentBlocks;
                            break;
                        }
                }
            };
            both (400);
            for (int cycle = 0; cycle < 4; ++cycle)
            {
                a.looper.pressRecord (0);
                both (cycle % 2 == 0 ? 30 : 500);
                a.looper.undo (0);
                both (600);
            }
            RigLooper::copySliceForTests = 0;
            logMessage ("  " + juce::String (compared) + " blocks compared");
            expectEquals (differentBlocks, 0);
        }

        beginTest ("14. Undo lands on time: the first block after it plays the original loop");
        for (const int slice : { 0, 2 })
            for (const int dubBlocks : { 1, 20, 400 })
            {
                RigLooper::copySliceForTests = slice * RigLooper::kCopyPageSamples;
                Twin t (kRate, kBlock);
                dubInput (t.a);
                dubInput (t.b);
                t.loop (0, odu::markedLoop (600 * 1024, 21));
                t.blocks (3);
                t.a.looper.pressRecord (0);
                t.blocks (dubBlocks);
                t.a.looper.undo (0);
                t.comparing = true;
                t.blocks (800);
                expectEquals (t.firstDiffBlock, -1, (slice == 0 ? juce::String ("normal budget") : juce::String ("slowed budget")) + ", undo after "
                                                        + juce::String (dubBlocks) + " blocks of overdub");
            }
        RigLooper::copySliceForTests = 0;
    }
};

static RigOverdubUndoGapTests rigOverdubUndoGapTests;

//==============================================================================
// Group C: the audio thread's work in a block.
struct RigOverdubUndoWorkTests : juce::UnitTest
{
    RigOverdubUndoWorkTests() : juce::UnitTest ("Rig overdub undo work", "Rig") {}

    void expectBounded (RigLooper& looper, const juce::String& what)
    {
        const auto c = looper.getCopyCounters();
        logMessage ("  " + what + ": most a block " + juce::String (c.maxTotal) + " pages (mandatory " + juce::String (c.maxMandatory)
                    + ", in order " + juce::String (c.maxDiscretionary) + "), looks " + juce::String (c.maxLooks) + "; blocks over the bound "
                    + juce::String (c.overBudgetBlocks) + ", mandatory over " + juce::String (c.overMandatoryBlocks) + ", looks over "
                    + juce::String (c.overLookBlocks) + "; whole-loop copies " + juce::String (c.pages[RigLooper::wholeLoop]) + " pages");
        if (! RigLooper::kIncrementalUndo)
            return;   // expected to fail on the old code
        expectEquals ((int) c.overBudgetBlocks, 0, what + ": never more than the block's bound");
        expectEquals ((int) c.overMandatoryBlocks, 0, what + ": mandatory work within its bound");
        expectEquals ((int) c.overLookBlocks, 0, what + ": looks within their limit");
        expectEquals ((int) c.pages[RigLooper::wholeLoop], 0, what + ": no whole-loop copy");
    }

    // Overdub and undo on a loop at a rate and block size, a few times.
    void cycle (double rate, int block, int samples, bool varying, const juce::String& what)
    {
        Drive d (rate, varying ? 1024 : block);
        if (varying)
            d.blockSize = [] (juce::int64 k) { return 1 + (int) ((k * 7919) % 1024); };
        dubInput (d);
        putLoop (d, 0, odu::markedLoop (samples, 30));
        putLoop (d, 1, odu::markedLoop (samples / 2 + 77, 31));
        d.looper.maintain();
        d.blocks (2);
        d.looper.resetCopyCounters();
        for (int k = 0; k < 3; ++k)
        {
            d.looper.pressRecord (k % 2);
            d.seconds (0.05 + 0.4 * k);
            d.looper.undo (k % 2);
            d.seconds (0.6);
        }
        expectBounded (d.looper, what);
    }

    void runTest() override
    {
        beginTest ("15. Copy counters: every block within its bound, at every rate and block size");
        for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
            for (const int block : { 16, 64, 256, 4096 })
                cycle (rate, block, 200 * 1024 + 5, false, juce::String (rate / 1000.0, 1) + " kHz, " + juce::String (block));
        cycle (48000.0, 0, 200 * 1024 + 5, true, "48 kHz, a size that changes every block");

        beginTest ("17. No allocation or free on the audio thread: overdub start, capture, undo, restoration, adoption");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (300 * 1024, 32));
            d.looper.maintain();
            d.blocks (4);
            oduAlloc::allocations = 0;
            oduAlloc::frees = 0;
            const auto counted = [&] (int blocks)
            {
                oduAlloc::countHere = true;
                d.blocks (blocks);
                oduAlloc::countHere = false;
            };
            for (int k = 0; k < 4; ++k)
            {
                d.looper.pressRecord (0);
                counted (k % 2 == 0 ? 2 : 200);
                d.looper.undo (0);
                counted (300);
                // A longer loop elsewhere: a bigger room, made off the audio thread and taken in.
                putLoop (d, 2 + k, odu::markedLoop ((400 + 100 * k) * 1024, 33 + k));
                d.looper.maintain();
                counted (5);
                d.looper.maintain();
            }
            logMessage (oduAlloc::counted ? "  " + juce::String (oduAlloc::allocations.load()) + " allocations, " + juce::String (oduAlloc::frees.load()) + " frees"
                                          : juce::String ("  (allocations aren't counted on this platform)"));
            expectEquals (oduAlloc::allocations.load(), 0);
            expectEquals (oduAlloc::frees.load(), 0);
        }

        beginTest ("18. No page faults on the audio thread across the first overdub of a fresh 10-minute loop");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop ((int) (kRate * RigLooper::kMaxUndoSeconds), 40));
            d.looper.maintain();
            long faults = 0;
            std::thread audio ([&]
            {
                d.blocks (50);   // its stack and buffers, first
                const long f0 = odu::threadPageFaults();
                d.looper.pressRecord (0);
                d.seconds (1.0);   // the capture
                d.looper.undo (0);
                d.seconds (0.5);
                faults = odu::threadPageFaults() - f0;
            });
            audio.join();
            logMessage ("  " + juce::String ((int) faults) + " page faults on the audio thread");
            if (RigLooper::kIncrementalUndo && faults >= 0)
                expectEquals ((int) faults, 0);
        }

        beginTest ("19. Block times for overdub start and undo on a 10-minute loop (reported)");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop ((int) (kRate * RigLooper::kMaxUndoSeconds), 41));
            d.looper.maintain();
            d.blocks (10);
            odu::Times start, undo;
            for (int k = 0; k < 5; ++k)
            {
                d.timing = true;
                d.resetTiming();
                d.looper.pressRecord (0);
                d.block();
                start.add (d.wall.most());
                d.blocks (k % 2 == 0 ? 5 : 200);
                d.resetTiming();
                d.looper.undo (0);
                d.block();
                undo.add (d.wall.most());
                d.timing = false;
                d.seconds (1.0);
            }
            logMessage ("  overdub start: median " + juce::String (start.median() / 1000.0, 2) + " ms, most " + juce::String (start.most() / 1000.0, 2)
                        + " ms; undo: median " + juce::String (undo.median() / 1000.0, 2) + " ms, most " + juce::String (undo.most() / 1000.0, 2) + " ms");
            expect (true);
        }
    }
};

static RigOverdubUndoWorkTests rigOverdubUndoWorkTests;

//==============================================================================
// The undo room: made ready ahead of need off the audio thread, handed over without a copy,
// never cancelling an undo, and never more than two room-sized buffers at once.
struct RigOverdubUndoRoomTests : juce::UnitTest
{
    RigOverdubUndoRoomTests() : juce::UnitTest ("Rig overdub undo room", "Rig") {}

    void runTest() override
    {
        beginTest ("A bigger room handed over doesn't cancel an undo; it's taken when no undo is held");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (100 * 1024, 50));
            d.blocks (2);
            const auto before = d.looper.soundingLoopForTests (0);
            d.looper.pressRecord (0);
            d.blocks (30);
            d.looper.pressRecord (0);
            d.block();
            expect (d.looper.canUndo (0));
            const int room0 = d.looper.undoRoomSamples();
            // A longer loop on another layer: a bigger room is made, and waits.
            putLoop (d, 1, odu::markedLoop (room0 + 50 * 1024, 51));
            d.looper.maintain();
            expect (d.looper.isSpareRoomReady(), "a bigger room is ready");
            const auto c0 = d.looper.getCopyCounters();
            d.blocks (20);
            expect (d.looper.canUndo (0), "the undo is kept");
            expectEquals (d.looper.undoRoomSamples(), room0, "and its room with it");
            expect (d.looper.isSpareRoomReady(), "the bigger room still waits");
            d.looper.undo (0);
            d.block();
            expect (same (d.looper.soundingLoopForTests (0), before), "the undo is exact");
            d.seconds (1.0);
            const auto c1 = d.looper.getCopyCounters();
            expect (! d.looper.isSpareRoomReady(), "then the bigger room is taken");
            expectGreaterOrEqual (d.looper.undoRoomSamples(), room0 + 50 * 1024);
            expectEquals (c1.adoptions - c0.adoptions, 1);
            d.looper.maintain();   // frees the old room
            expectEquals (d.looper.roomBuffersNow(), 1);
        }

        beginTest ("Taking a room over copies nothing and allocates nothing");
        {
            Drive d (kRate, kBlock);
            putLoop (d, 0, odu::markedLoop (40 * 48000, 52));   // 40 s: a bigger room than 32 s
            expect (d.looper.isSpareRoomReady(), "made ready as the sound went in");
            d.looper.resetCopyCounters();
            oduAlloc::allocations = 0;
            oduAlloc::frees = 0;
            oduAlloc::countHere = true;
            d.block();
            oduAlloc::countHere = false;
            const auto c = d.looper.getCopyCounters();
            juce::int64 pages = 0;
            for (auto p : c.pages)
                pages += p;
            expectEquals (c.adoptions, 1, "taken in the first block");
            expectEquals ((int) pages, 0, "no page copied");
            expectEquals (oduAlloc::allocations.load() + oduAlloc::frees.load(), 0, "nothing allocated or freed");
            expectGreaterOrEqual (d.looper.undoRoomSamples(), 40 * 48000);
        }

        beginTest ("A press that needs the bigger room takes it, and the other loop's undo goes as it would");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (100 * 1024, 53));
            d.blocks (2);
            d.looper.pressRecord (0);
            d.blocks (10);
            expect (d.looper.canUndo (0));
            putLoop (d, 1, odu::markedLoop (60 * 48000, 54));   // 1 minute
            d.blocks (5);
            expect (d.looper.isSpareRoomReady(), "waits: an undo is held");
            const auto before = d.looper.soundingLoopForTests (1);
            d.looper.pressRecord (1);
            d.block();
            expect (d.looper.canUndo (1) && ! d.looper.canUndo (0), "the long loop's overdub has the undo");
            d.blocks (100);
            d.looper.pressRecord (1);
            d.block();
            d.looper.undo (1);
            d.block();
            d.seconds (3.0);
            expect (same (d.looper.copyLayerAudio (1), before), "exact");
        }

        beginTest ("12c. The room is ready before the first possible press: an import, a queued bank swap, a show load, a recording");
        {
            Drive d (kRate, kBlock);
            putLoop (d, 0, odu::markedLoop (3 * 60 * 48000, 55));   // an import, 3 minutes
            d.block();
            expectGreaterOrEqual (d.looper.undoRoomSamples(), 3 * 60 * 48000, "import");
            const int n = 4 * 60 * 48000;
            d.looper.queueLayerAudio (1, odu::markedLoop (n, 56), n / 24000.0, d.clock.beat + 8.0, 0.8f, false);
            d.block();
            expectGreaterOrEqual (d.looper.undoRoomSamples(), n, "a bank swap queued for later: ready before it comes in");
            for (int l = 2; l < 6; ++l)   // a show load: each sound in turn
            {
                d.looper.reset (l);
                putLoop (d, l, odu::markedLoop ((5 + l) * 60 * 48000 / 2, 57 + l));
            }
            d.block();
            expectGreaterOrEqual (d.looper.undoRoomSamples(), 7 * 60 * 48000 / 2, "a show load");
            d.looper.maintain();
            expectLessOrEqual (d.looper.getCopyCounters().roomBuffersMost, 2);
        }
        {
            // A take recorded for 40 s: the room grows with it as maintain gives it more.
            Drive d (kRate, kBlock);
            d.input = [] (juce::int64 at, int) { return (float) (at % 100) / 1000.0f; };
            d.looper.getLayer (0).source = RigLooper::input;
            d.looper.pressRecord (0);
            for (int k = 0; d.fed < 40 * 48000; ++k)
            {
                d.block();
                if (k % 20 == 0)
                    d.looper.maintain();
            }
            d.looper.pressRecord (0);
            expect (d.until ([&] { return d.looper.getState (0) == State::playing; }, 2000), "the take closed");
            const int length = d.looper.copyLayerAudio (0).getNumSamples();
            expectGreaterOrEqual (d.looper.undoRoomSamples(), length, "a recording: ready as it closes");
            d.looper.pressRecord (0);
            d.block();
            expect (d.looper.canUndo (0), "its first overdub has an undo");
        }

        beginTest ("Safeguard 7: never more than two room-sized buffers, through growth, hand-over and freeing");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            d.looper.resetCopyCounters();
            int most = d.looper.roomBuffersNow();
            putLoop (d, 0, odu::markedLoop (100 * 1024, 60));
            d.blocks (2);
            for (int k = 0; k < 12; ++k)
            {
                // Overdub something, then a longer loop arrives while its undo is held.
                d.looper.pressRecord (k % 3);
                d.blocks (5);
                most = juce::jmax (most, d.looper.roomBuffersNow());
                putLoop (d, 3 + k % 4, odu::markedLoop ((40 + 8 * k) * 48000, 61 + k));
                most = juce::jmax (most, d.looper.roomBuffersNow());
                d.looper.maintain();
                most = juce::jmax (most, d.looper.roomBuffersNow());
                d.blocks (5);
                d.looper.pressRecord (k % 3);
                d.blocks (2);
                if (k % 2 == 0)
                {
                    d.looper.undo (k % 3);
                    d.blocks (10);
                }
                d.looper.maintain();
                most = juce::jmax (most, d.looper.roomBuffersNow());
                if (k % 3 == 0)
                    putLoop (d, k % 3, odu::markedLoop ((20 + k) * 1024, 90 + k));
            }
            const auto c = d.looper.getCopyCounters();
            logMessage ("  most at once: " + juce::String (c.roomBuffersMost) + " (seen " + juce::String (most) + "), "
                        + juce::String (c.adoptions) + " hand-overs");
            expectLessOrEqual (c.roomBuffersMost, 2);
            expectGreaterThan (c.adoptions, 3);
        }
    }
};

static RigOverdubUndoRoomTests rigOverdubUndoRoomTests;

//==============================================================================
// The copy budget: one credit for all the layers, earned by the block's duration.
struct RigCopyBudgetTests : juce::UnitTest
{
    RigCopyBudgetTests() : juce::UnitTest ("Rig copy budget", "Rig") {}

    // Copies `layer` on another thread while `d` plays here; returns the blocks it took.
    int copyWhilePlaying (Drive& d, int layer, juce::AudioBuffer<float>* result = nullptr)
    {
        std::atomic<bool> done { false };
        juce::AudioBuffer<float> copy;
        const auto stats0 = d.looper.getCopyStats();
        std::thread copier ([&] { copy = d.looper.copyLayerAudio (layer); done = true; });
        // The blocks the copy took: from its first page copied in order to its last.
        int blocks = 0, k = 0;
        auto pages = d.looper.getCopyCounters().pages[RigLooper::saveInOrder];
        while (! done)
        {
            d.block();
            const auto now = d.looper.getCopyCounters().pages[RigLooper::saveInOrder];
            blocks += now != pages ? 1 : 0;
            pages = now;
            if (++k % 64 == 0)
                std::this_thread::yield();
        }
        copier.join();
        expectEquals (d.looper.getCopyStats().byAudioThread - stats0.byAudioThread, 1, "copied by the audio thread");
        if (result != nullptr)
            *result = copy;
        return blocks;
    }

    void runTest() override
    {
        beginTest ("Safeguard 3: credit never bursts, with the block size changed every block and after a long idle stretch");
        {
            Drive d (kRate, 1024);
            juce::Random r (5);
            d.blockSize = [&r] (juce::int64 k) { return (k / 500) % 2 == 0 ? 1 + r.nextInt (1024) : 16; };
            putLoop (d, 0, odu::markedLoop (2000 * 1024, 70));
            d.looper.resetCopyCounters();
            d.blocks (3000);   // idle: nothing to copy
            juce::AudioBuffer<float> copy;
            copyWhilePlaying (d, 0, &copy);
            d.blocks (2000);
            copyWhilePlaying (d, 0);
            const auto c = d.looper.getCopyCounters();
            logMessage ("  most credit " + juce::String (c.mostCredit, 2) + " pages; most a block " + juce::String (c.maxTotal) + " pages");
            expectEquals ((int) c.overCreditBlocks, 0, "never more than one block's worth and a fraction of a page");
            expectEquals ((int) c.overBudgetBlocks, 0, "never more copied than that");
            expectLessOrEqual (c.mostCredit, 1025.0);
            expect (same (copy, odu::markedLoop (2000 * 1024, 70)), "and the copy is exact");
        }

        beginTest ("A save's copy takes its time from the block's duration: 2 and 4 times the blocks at 96 and 192 kHz");
        {
            std::array<int, 3> took {};
            const double rates[] = { 48000.0, 96000.0, 192000.0 };
            for (int k = 0; k < 3; ++k)
            {
                Drive d (rates[k], 256);
                putLoop (d, 0, odu::markedLoop (4096 * 1024, 71));
                d.blocks (4);
                took[(size_t) k] = copyWhilePlaying (d, 0);
                const double perBlock = 48000.0 * 256 / rates[k];
                logMessage ("  " + juce::String (rates[k] / 1000.0, 0) + " kHz: " + juce::String (took[(size_t) k]) + " blocks for 4096 pages ("
                            + juce::String (perBlock, 0) + " a block)");
                expectGreaterOrEqual (took[(size_t) k], (int) (4096 / perBlock));
                expectLessOrEqual (took[(size_t) k], (int) (4096 / perBlock) + 4);
            }
        }

        beginTest ("A save's copies before an overdub writes are charged to the same credit");
        {
            RigLooper::copySliceForTests = 4 * RigLooper::kCopyPageSamples;   // 4 pages a block
            Drive d (kRate, kBlock);
            dubInput (d);
            // Squeezed 8 times: the overdub crosses 2 pages a block.
            d.looper.setLayerAudio (0, odu::markedLoop (600 * 1024, 72), 600 * 1024 / 192000.0);
            d.looper.getLayer (0).source = RigLooper::input;
            d.blocks (2);
            d.looper.pressRecord (0);
            d.blocks (2);
            d.looper.resetCopyCounters();
            const int took = copyWhilePlaying (d, 0);
            expect (d.looper.getState (0) == State::overdubbing);
            d.looper.pressRecord (0);
            d.block();
            RigLooper::copySliceForTests = 0;
            logMessage ("  the copy took " + juce::String (took) + " blocks");
            const auto c = d.looper.getCopyCounters();
            logMessage ("  " + juce::String (c.pages[RigLooper::saveBeforeWrite]) + " pages before the overdub wrote them, "
                        + juce::String (c.pages[RigLooper::saveInOrder]) + " in order; deepest debt " + juce::String (c.deepestDebt, 2) + " pages");
            expectGreaterThan ((int) c.pages[RigLooper::saveBeforeWrite], 10);
            expectGreaterThan ((int) c.pages[RigLooper::saveInOrder], 10);
            // 2 or 3 pages a block must be copied before the overdub writes them; uncharged, the
            // copy in order would add its 4 to them.
            expectLessOrEqual (c.maxTotal, 5, "never more than the block's credit (4 pages and the fraction)");
            expectLessOrEqual (c.deepestDebt, 8.0, "a debt only of what had to be copied");
            expectEquals ((int) c.overBudgetBlocks, 0);
            expectEquals ((int) c.overCreditBlocks, 0);
        }
    }
};

static RigCopyBudgetTests rigCopyBudgetTests;

//==============================================================================
// The capture: decided at the press, never withdrawn, and the window told why when there's none.
struct RigOverdubUndoCaptureTests : juce::UnitTest
{
    RigOverdubUndoCaptureTests() : juce::UnitTest ("Rig overdub undo capture", "Rig") {}

    void runTest() override
    {
        beginTest ("8. A sound squeezed just over the guarantee's limit: no undo, the reason set, and the overdub still exact");
        {
            Drive d (kRate, kBlock);
            const int n = 768000 * 2;
            const auto loop = odu::markedLoop (n, 80);
            d.looper.setLayerAudio (0, loop, 2.0 * 0.999);   // a little more than 768,000 samples a beat
            d.looper.getLayer (0).source = RigLooper::input;
            d.input = [] (juce::int64 at, int) { return (float) (at % 50) / 4096.0f; };
            odu::DubModel model;
            model.loop = loop;
            model.lengthBeats = 2.0 * 0.999;
            d.after = [&] (const RigLooper::Clock& clock, const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>&, int bn)
            {
                if (d.looper.getState (0) == State::overdubbing)
                    model.block (clock, kRate, 0, in, bn);
            };
            d.looper.maintain();
            d.block();
            expect (d.looper.undoLimit (0) == RigLooper::NoUndo::squeezed, "known before the press");
            d.looper.pressRecord (0);
            d.blocks (300);
            d.looper.pressRecord (0);
            d.block();
            expect (! d.looper.canUndo (0), "no undo");
            expect (d.looper.peekNoUndo (0) == RigLooper::NoUndo::squeezed);
            expect (d.looper.takeNoUndo (0) == RigLooper::NoUndo::squeezed, "said once");
            expect (d.looper.takeNoUndo (0) == RigLooper::NoUndo::none, "and only once");
            expectEquals (odu::firstDifference (d.looper.copyLayerAudio (0), model.loop), -1, "the overdub is exact");
        }

        beginTest ("12b. Messages: each reason once at the press, no Undo offered, and the temporary one clears when the room is ready");
        {
            constexpr double rate = 8000.0;   // (10 minutes is small here)
            Drive d (rate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop ((int) (rate * RigLooper::kMaxUndoSeconds) + 1, 81));
            d.block();
            expect (d.looper.undoLimit (0) == RigLooper::NoUndo::tooLong, "known before the press");
            d.looper.pressRecord (0);
            d.block();
            expect (! d.looper.canUndo (0));
            expect (d.looper.takeNoUndo (0) == RigLooper::NoUndo::tooLong, "over 10 minutes");
            expect (d.looper.takeNoUndo (0) == RigLooper::NoUndo::none);
            d.looper.pressRecord (0);
            d.block();
            expect (d.looper.takeNoUndo (0) == RigLooper::NoUndo::none, "stopping it says nothing");

            // The room not ready (here: every mark used, so new marks are made off the audio thread).
            putLoop (d, 1, odu::markedLoop (20000, 82));
            d.block();
            d.looper.setUndoGenerationForTests ((1u << 30) - 1);
            d.looper.pressRecord (1);
            d.block();
            expect (! d.looper.canUndo (1));
            expect (d.looper.takeNoUndo (1) == RigLooper::NoUndo::roomNotReady, "not ready");
            expect (RigLooper::noUndoMessage (RigLooper::NoUndo::roomNotReady).contains ("next one"), "worded as temporary");
            d.looper.pressRecord (1);
            d.block();
            d.looper.maintain();   // new marks
            d.blocks (2);
            const auto before = d.looper.soundingLoopForTests (1);
            d.looper.pressRecord (1);
            d.blocks (20);
            expect (d.looper.canUndo (1), "the next overdub has its undo");
            expect (d.looper.peekNoUndo (1) == RigLooper::NoUndo::none && d.looper.takeNoUndo (1) == RigLooper::NoUndo::none, "and the reason is gone");
            d.looper.pressRecord (1);
            d.block();
            d.looper.undo (1);
            d.block();
            d.seconds (1.0);
            expect (same (d.looper.copyLayerAudio (1), before), "exact");
        }

        beginTest ("Undo is never withdrawn during an eligible overdub, under budget pressure");
        {
            // The fastest loop that passes, a save on another overdubbing loop, a slowed budget, and
            // the room growing: undo stays offered from the press to the end of every overdub.
            RigLooper::copySliceForTests = 2 * RigLooper::kCopyPageSamples;
            Drive d (kRate, kBlock);
            dubInput (d);
            d.looper.setLayerAudio (0, odu::markedLoop (768000, 83), 768000 / 768000.0);   // at the limit
            d.looper.getLayer (0).source = RigLooper::input;
            putLoop (d, 1, odu::markedLoop (300 * 1024, 84));
            d.block();
            int withdrawn = 0, overdubBlocks = 0;
            std::atomic<bool> saving { true };
            std::thread saver ([&] { while (saving) d.looper.copyLayerAudio (1); });
            for (int round = 0; round < 6; ++round)
            {
                d.looper.pressRecord (1);   // the other loop overdubs while it's saved
                d.block();
                d.looper.pressRecord (0);
                d.block();
                expect (d.looper.canUndo (0));
                for (int k = 0; k < 200 + 100 * round; ++k)
                {
                    d.block();
                    ++overdubBlocks;
                    withdrawn += d.looper.canUndo (0) ? 0 : 1;
                    if (k == 50)
                    {
                        putLoop (d, 2 + round, odu::markedLoop ((40 + 10 * round) * 48000, 85 + round));
                        d.looper.maintain();
                    }
                }
                d.looper.pressRecord (0);
                d.looper.pressRecord (1);
                d.block();
                withdrawn += d.looper.canUndo (0) ? 0 : 1;
                d.blocks (50);
            }
            saving = false;
            saver.join();
            RigLooper::copySliceForTests = 0;
            logMessage ("  " + juce::String (overdubBlocks) + " overdub blocks checked");
            expectEquals (withdrawn, 0);
        }
    }
};

static RigOverdubUndoCaptureTests rigOverdubUndoCaptureTests;

//==============================================================================
// Undo by swap and by restoration, and what happens around a restoration.
struct RigOverdubUndoRestoreTests : juce::UnitTest
{
    RigOverdubUndoRestoreTests() : juce::UnitTest ("Rig overdub undo restore", "Rig") {}

    std::vector<int> capacities (RigLooper& l)
    {
        std::vector<int> c;
        for (int i = 0; i < RigLooper::kLayers; ++i)
            c.push_back (l.getCapacity (i));
        return c;
    }
    // roomLeftSeconds as the capacities say it should be.
    bool roomLeftAccurate (RigLooper& l, double rate)
    {
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            juce::int64 others = 0;
            for (int j = 0; j < RigLooper::kLayers; ++j)
                if (j != i)
                    others += l.getCapacity (j);
            const double want = juce::jmax (0.0, juce::jmin (RigLooper::kMaxRecordSeconds, RigLooper::kTotalRecordSeconds - (double) others / rate));
            if (std::abs (l.roomLeftSeconds (i) - want) > 1.0e-9)
                return false;
        }
        return true;
    }
    // Runs until no undo work is left.
    void settle (Drive& d)
    {
        d.until ([&] { return ! d.looper.isUndoRestoring(); }, 200000);
        d.blocks (2);
    }

    void runTest() override
    {
        beginTest ("Both ways of undoing are exact: by swap (the room's buffer the loop's size) and by restoration (a bigger room)");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            // (The room is never under 32 s, so a loop's buffer is the room's size only from there.)
            putLoop (d, 0, odu::markedLoop (40 * 48000, 100));   // the longest: the room is its size
            putLoop (d, 1, odu::markedLoop (35 * 48000, 101));   // the room is bigger than its buffer
            d.blocks (3);
            for (int layer : { 0, 1 })
            {
                const auto before = d.looper.soundingLoopForTests (layer);
                const auto peaks = d.looper.readPeaks (layer, 2000);
                const int version0 = d.looper.getLayer (layer).version.load();
                const double beats0 = d.looper.getLayer (layer).lengthBeats.load();
                d.looper.resetCopyCounters();
                d.looper.pressRecord (layer);
                d.seconds (1.5);   // the capture completes
                expect (d.looper.isUndoCaptureCompleteForTests());
                d.looper.pressRecord (layer);
                d.block();
                const int capBefore = d.looper.getCapacity (layer);
                const int versionDubbed = d.looper.getLayer (layer).version.load();
                d.looper.undo (layer);
                d.block();
                const auto c = d.looper.getCopyCounters();
                const juce::String way = layer == 0 ? "swap" : "restoration";
                expectEquals (c.swaps, layer == 0 ? 1 : 0, way);
                expectEquals (c.restorations, layer == 0 ? 0 : 1, way);
                expect (same (d.looper.soundingLoopForTests (layer), before), way + ": the loop as it was, at once");
                expect (d.looper.readPeaks (layer, 2000) == peaks, way + ": its waveform as it was");
                expectEquals (d.looper.getLayer (layer).version.load(), versionDubbed + 1, way + ": the version goes up once");
                expectGreaterThan (versionDubbed, version0);
                expectEquals (d.looper.getLayer (layer).lengthBeats.load(), beats0, way + ": its length in beats");
                expectLessOrEqual (d.looper.getCapacity (layer), capBefore, way + ": its buffer never bigger");
                settle (d);
                expect (same (d.looper.copyLayerAudio (layer), before), way + ": and once it's all done");
                expectEquals (d.looper.getLayer (layer).version.load(), versionDubbed + 1, way + ": the restoration doesn't move the version");
                // The next overdub, on any layer, still has its undo.
                const auto again = d.looper.soundingLoopForTests (1 - layer);
                d.looper.pressRecord (1 - layer);
                d.blocks (40);
                expect (d.looper.canUndo (1 - layer), way + ": the next overdub has an undo");
                d.looper.pressRecord (1 - layer);
                d.block();
                d.looper.undo (1 - layer);
                d.block();
                settle (d);
                expect (same (d.looper.copyLayerAudio (1 - layer), again));
            }
        }

        beginTest ("A swap keeps the loop's place in the bar, its length and the room's size for the next undo");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (34 * 48000, 102));
            d.blocks (50);
            const int room0 = d.looper.undoRoomSamples();
            d.looper.pressRecord (0);
            d.seconds (1.0);
            d.looper.pressRecord (0);
            d.block();
            const double position = d.looper.getLayer (0).position.load();
            d.looper.undo (0);
            d.block();
            expectEquals (d.looper.getCopyCounters().swaps, 1);
            expectWithinAbsoluteError (d.looper.getLayer (0).position.load(), position + kBlock / (34.0 * 48000), 1.0e-9, "still in its place");
            expectEquals (d.looper.copyLayerAudio (0).getNumSamples(), 34 * 48000, "its length");
            expectGreaterOrEqual (d.looper.undoRoomSamples(), juce::jmin (room0, 34 * 48000), "a room for the next undo");
        }

        beginTest ("A completed capture where the overdub wrote every page, undone by restoration");
        for (const int with : { 0, 1, 2, 3 })   // alone, a save, a bank switch, deferred presses (and one cancelled)
        {
            RigLooper::copySliceForTests = with == 0 ? 0 : 3 * RigLooper::kCopyPageSamples;   // slow, so it lasts
            Drive d (kRate, kBlock);
            dubInput (d);
            putLoop (d, 0, odu::markedLoop (700 * 1024, 103));   // the room is this long
            const int n = 100 * 1024 + 17;
            putLoop (d, 1, odu::markedLoop (n, 104));
            putLoop (d, 2, odu::markedLoop (50 * 1024, 105));
            d.blocks (3);
            const auto before = d.looper.soundingLoopForTests (1);
            d.looper.pressRecord (1);
            d.seconds (2.0 * n / kRate + 0.1);   // twice round: every page written
            d.looper.pressRecord (1);
            d.block();
            d.looper.undo (1);
            d.block();
            expect (with == 0 || d.looper.isUndoRestoring(), "restoring");
            expect (same (d.looper.soundingLoopForTests (1), before), "sounds as it was at once");
            juce::String what = with == 0 ? "alone" : with == 1 ? "with a save" : with == 2 ? "with a bank switch" : "with deferred presses";
            if (with == 1)
            {
                // A save asked for during the restoration: the loop as it was, at the undo's version.
                int version = -1;
                juce::AudioBuffer<float> copy;
                std::atomic<bool> done { false };
                std::thread saver ([&] { copy = d.looper.copyLayerAudio (1, &version); done = true; });
                d.until ([&] { return done.load(); }, 100000);
                saver.join();
                expect (same (copy, before), what + ": the save is the loop as it was");
                expectEquals (version, d.looper.getLayer (1).version.load());
            }
            if (with == 2)
            {
                // Another loop's bank swap comes in at the next beat while this one restores.
                const auto next = odu::markedLoop (60 * 1024, 106);
                d.looper.queueLayerAudio (2, next, 60 * 1024 / 24000.0, d.clock.beat + 0.25, 0.8f, false);
                d.seconds (0.5);
                expect (same (d.looper.copyLayerAudio (2), next), what + ": the other loop swapped in");
                // And a swap of the restoring loop itself ends the restoration.
                d.looper.queueLayerAudio (1, before, n / 24000.0, -1.0, 0.8f, false);
                expect (! d.looper.isUndoRestoring(), what + ": a swap of this loop ends it");
            }
            if (with == 3)
            {
                d.looper.pressRecord (1);   // deferred: on this loop
                d.looper.pressRecord (2);   // and on another
                d.looper.pressRecord (2);   // on then off, in order
                d.block();
                expect (d.looper.getState (1) == State::playing && d.looper.getState (2) == State::playing, what + ": held back");
                d.looper.clear (2);         // cancels the other loop's presses
                d.block();
                settle (d);
                expect (d.looper.getState (1) == State::overdubbing, what + ": applied once it's done");
                expect (d.looper.getState (2) == State::empty, what + ": the cleared loop's presses dropped (not armed)");
                expect (d.looper.canUndo (1), what + ": the held-back overdub has its undo");
                d.looper.pressRecord (1);
                d.block();
            }
            settle (d);
            if (with != 2)
            {
                if (with == 3)
                {
                    d.looper.undo (1);
                    d.block();
                    settle (d);
                }
                expect (same (d.looper.copyLayerAudio (1), before), what + ": exact once it's done");
            }
            RigLooper::copySliceForTests = 0;
        }

        beginTest ("Amendment A: overdub and undo on loops of many lengths, again and again: no layer's buffer grows, two rooms at most");
        {
            Drive d (kRate, kBlock);
            dubInput (d);
            const int lengths[] = { 5000, 40 * 1024 + 3, 150 * 1024, 300 * 1024 + 99, 40 * 48000, 1024, 77777, 36 * 48000 };
            std::vector<juce::AudioBuffer<float>> originals;
            for (int l = 0; l < 8; ++l)
            {
                originals.push_back (odu::markedLoop (lengths[l], 110 + l));
                putLoop (d, l, originals.back());
            }
            d.blocks (3);
            d.looper.resetCopyCounters();
            auto caps = capacities (d.looper);
            int grew = 0, wrong = 0;
            for (int round = 0; round < 3; ++round)
                for (int l = 0; l < 8; ++l)
                {
                    d.looper.pressRecord (l);
                    d.blocks (round == 1 ? 3 : 200);   // undo during the capture, and after it
                    d.looper.pressRecord (l);
                    d.block();
                    d.looper.undo (l);
                    d.block();
                    wrong += same (d.looper.soundingLoopForTests (l), originals[(size_t) l]) ? 0 : 1;
                    settle (d);
                    d.looper.maintain();
                    const auto now = capacities (d.looper);
                    for (int i = 0; i < RigLooper::kLayers; ++i)
                        grew += now[(size_t) i] > caps[(size_t) i] ? 1 : 0;
                    caps = now;
                }
            const auto c = d.looper.getCopyCounters();
            logMessage ("  " + juce::String (c.swaps) + " by swap, " + juce::String (c.restorations) + " by restoration; rooms at once "
                        + juce::String (c.roomBuffersMost));
            for (int l = 0; l < 8; ++l)
                wrong += same (d.looper.copyLayerAudio (l), originals[(size_t) l]) ? 0 : 1;
            expectEquals (grew, 0, "no layer's buffer ever grew");
            expectEquals (wrong, 0, "every undo exact");
            expectLessOrEqual (c.roomBuffersMost, 2);
            expectGreaterThan (c.swaps, 0);
            expectGreaterThan (c.restorations, 0);
        }

        beginTest ("All 16 layers with an oversized undo room: buffers never grow, and the room left stays right");
        {
            constexpr double rate = 16000.0;
            Drive d (rate, kBlock);
            dubInput (d);
            d.looper.prepareUndoRoom ((int) (rate * RigLooper::kMaxUndoSeconds));   // 10 minutes of room
            d.block();
            expectEquals (d.looper.undoRoomSamples(), (int) (rate * RigLooper::kMaxUndoSeconds), "an oversized room");
            std::vector<juce::AudioBuffer<float>> originals;
            for (int l = 0; l < RigLooper::kLayers; ++l)
            {
                originals.push_back (odu::markedLoop (3000 + 9000 * l, 130 + l));
                d.looper.setLayerAudio (l, originals.back(), originals.back().getNumSamples() / (rate * 0.5));
                d.looper.getLayer (l).source = RigLooper::input;
            }
            d.blocks (3);
            const auto caps0 = capacities (d.looper);
            int grew = 0, inaccurate = 0, wrong = 0;
            d.looper.resetCopyCounters();
            for (int round = 0; round < 3; ++round)
                for (int l = 0; l < RigLooper::kLayers; ++l)
                {
                    d.looper.pressRecord (l);
                    d.blocks (round == 0 ? 2 : 60);
                    d.looper.pressRecord (l);
                    d.block();
                    d.looper.undo (l);
                    d.block();
                    settle (d);
                    d.looper.maintain();
                    const auto now = capacities (d.looper);
                    for (int i = 0; i < RigLooper::kLayers; ++i)
                        grew += now[(size_t) i] > caps0[(size_t) i] ? 1 : 0;
                    inaccurate += roomLeftAccurate (d.looper, rate) ? 0 : 1;
                    wrong += same (d.looper.copyLayerAudio (l), originals[(size_t) l]) ? 0 : 1;
                }
            const auto c = d.looper.getCopyCounters();
            logMessage ("  " + juce::String (c.swaps) + " by swap, " + juce::String (c.restorations) + " by restoration");
            expectEquals (grew, 0);
            expectEquals (inaccurate, 0);
            expectEquals (wrong, 0);
            expectEquals (c.swaps, 0, "the room is bigger than every loop's buffer: never swapped");
            expectLessOrEqual (c.roomBuffersMost, 2);
        }

        beginTest ("10, 12a. Restore before read: with bends switched during a long restoration, every sample is the original's");
        for (const int block : { 16, 256 })
        {
            RigLooper::copySliceForTests = RigLooper::kCopyPageSamples / 4;   // a page every 4 blocks
            Twin t (kRate, block);
            dubInput (t.a);
            dubInput (t.b);
            t.loop (0, odu::markedLoop (900 * 1024, 140));   // (a bigger room: restoration)
            t.loop (1, odu::markedLoop (200 * 1024, 141));
            t.blocks (3);
            t.a.looper.pressRecord (1);
            t.blocks (block == 16 ? 30000 : 2000);
            t.a.looper.pressRecord (1);
            t.block();
            t.a.looper.undo (1);
            t.comparing = true;
            juce::Random r (7);
            int changes = 0;
            for (int k = 0; k < (block == 16 ? 40000 : 4000) && (t.a.looper.isUndoRestoring() || k < 10); ++k)
            {
                if (k % 7 == 0)
                {
                    t.a.looper.setBend (1, r.nextInt (RigLooper::numBends));   // (each change fades over 8 ms: several at once)
                    ++changes;
                }
                t.block();
            }
            const bool stillRestoring = t.a.looper.isUndoRestoring();
            expectEquals (t.firstDiffBlock, -1, juce::String (block) + "-sample blocks, " + juce::String (changes) + " bend changes");
            // The audio stops partway: a copier takes over and gets the loop as it was.
            const auto original = t.b.looper.copyLayerAudio (1);
            juce::AudioBuffer<float> copy;
            std::thread copier ([&] { copy = t.a.looper.copyLayerAudio (1); });
            copier.join();
            expect (same (copy, original), "a copier taking over gets the original" + juce::String (stillRestoring ? " (mid-restoration)" : ""));
            RigLooper::copySliceForTests = 0;
        }

        beginTest ("11. Deferred presses: once, in order, on their own layer; dropped after clear, new sound, bank swap, show load");
        {
            const juce::StringArray cancels { "", "clear", "new sound", "bank swap", "show load" };
            for (const auto& cancel : cancels)
            {
                RigLooper::copySliceForTests = RigLooper::kCopyPageSamples;
                Drive d (kRate, kBlock);
                dubInput (d);
                putLoop (d, 0, odu::markedLoop (600 * 1024, 150));
                putLoop (d, 1, odu::markedLoop (200 * 1024, 151));
                putLoop (d, 2, odu::markedLoop (100 * 1024, 152));
                d.blocks (3);
                d.looper.pressRecord (1);
                d.blocks (300);
                d.looper.undo (1);
                d.block();
                expect (d.looper.isUndoRestoring());
                d.looper.pressRecord (2);
                d.block();
                expect (d.looper.getState (2) == State::playing, "held back while it restores");
                if (cancel == "clear")
                    d.looper.clear (2);
                else if (cancel == "new sound")
                    d.looper.setLayerAudio (2, odu::markedLoop (100 * 1024, 153), 100 * 1024 / 24000.0);
                else if (cancel == "bank swap")
                    d.looper.queueLayerAudio (2, odu::markedLoop (100 * 1024, 154), 100 * 1024 / 24000.0, -1.0, 0.8f, false);
                else if (cancel == "show load")
                    d.looper.reset (2);
                // A press on an empty layer isn't held back: it arms at once (the next bar line).
                d.looper.pressRecord (5);
                d.block();
                expect (d.looper.getState (5) == State::armed, "an empty layer's press is never held back");
                d.looper.pressRecord (5);
                d.block();
                int appliedAt = -1;
                for (int k = 0; k < 20000 && appliedAt < 0; ++k)
                {
                    const bool was = d.looper.isUndoRestoring();
                    d.block();
                    if (was && ! d.looper.isUndoRestoring())
                        appliedAt = k;
                }
                d.block();
                if (cancel.isEmpty())
                {
                    expect (d.looper.getState (2) == State::overdubbing, "applied once the restoration is done");
                    expect (d.looper.canUndo (2), "with its undo");
                    expect (d.looper.getState (1) == State::playing, "on the layer it was made on only");
                    d.looper.pressRecord (2);
                    d.block();
                    expect (d.looper.getState (2) == State::playing, "once");
                }
                else
                    expect (d.looper.getState (2) != State::overdubbing && d.looper.getState (2) != State::armed, cancel + ": dropped");
                RigLooper::copySliceForTests = 0;
            }
        }

        beginTest ("16, safeguard 5. The mandatory bound at its worst: 16 samples at 192 kHz, the fastest loop that passes, a wrap, a bend fade, a save on another overdub");
        {
            constexpr double rate = 192000.0;
            Drive d (rate, 16);
            dubInput (d);
            // r_worst = 16 is the most (G) allows at 192 kHz: 8 times natural speed at 120 BPM (just under).
            const int n = 2000 * 1024 + 5;
            const double spb = rate * 60.0 / 120.0;
            d.looper.setLayerAudio (0, odu::markedLoop (n, 160), n / (7.99 * spb));
            d.looper.setLayerAudio (1, odu::markedLoop (n + 1000, 161), (n + 1000) / (7.99 * spb));
            d.looper.setLayerAudio (2, odu::markedLoop (900 * 1024, 162), 900 * 1024 / spb);   // (the room is bigger: restoration)
            for (int l = 0; l < 3; ++l)
                d.looper.getLayer (l).source = RigLooper::input;
            d.blocks (3);
            d.blocks (8000);            // half way round: ahead of the capture in order, so pages are captured before writes
            d.looper.pressRecord (1);   // overdubbed while it's saved
            d.block();
            d.looper.pressRecord (0);   // the undo's overdub (the last one pressed)
            d.block();
            expect (d.looper.canUndo (0));
            d.looper.resetCopyCounters();
            std::atomic<bool> saving { true };
            std::thread saver ([&] { while (saving) d.looper.copyLayerAudio (1); });
            for (int k = 0; k < 12000; ++k)
            {
                if (k % 300 == 0)
                    d.looper.setBend (0, (k / 300) % 2 == 0 ? RigLooper::twice : RigLooper::reverse);
                d.block();
            }
            const auto capturing = d.looper.getCopyCounters();
            d.looper.pressRecord (0);
            d.block();
            d.looper.undo (0);
            d.looper.resetCopyCounters();
            for (int k = 0; k < 12000; ++k)
            {
                if (k % 300 == 0)
                    d.looper.setBend (0, (k / 300) % 3);
                d.block();
            }
            const auto restoring = d.looper.getCopyCounters();
            saving = false;
            saver.join();
            // Short block: x = 16 * 8 / 1024 under a third of a page, so M_w <= 2, M_r <= 6, M_s <= 2.
            logMessage ("  capturing: most mandatory " + juce::String (capturing.maxMandatory) + " pages a block (undo "
                        + juce::String (capturing.maxPages[RigLooper::undoBeforeWrite]) + ", save " + juce::String (capturing.maxPages[RigLooper::saveBeforeWrite])
                        + "); restoring: most " + juce::String (restoring.maxMandatory) + " (before read " + juce::String (restoring.maxPages[RigLooper::undoBeforeRead])
                        + ", save " + juce::String (restoring.maxPages[RigLooper::saveBeforeWrite]) + "); bound 8 pages (64 KB)");
            expectLessOrEqual (capturing.maxPages[RigLooper::undoBeforeWrite], 2);
            expectLessOrEqual (capturing.maxPages[RigLooper::saveBeforeWrite], 2);
            expectLessOrEqual (restoring.maxPages[RigLooper::undoBeforeRead], 6);
            expectLessOrEqual (capturing.maxMandatory, 8);
            expectLessOrEqual (restoring.maxMandatory, 8);
            expectLessOrEqual ((int) (capturing.maxBytes[RigLooper::undoBeforeWrite] + capturing.maxBytes[RigLooper::saveBeforeWrite]), 65536);
            expectEquals ((int) (capturing.overBudgetBlocks + restoring.overBudgetBlocks), 0);
            expectEquals ((int) (capturing.overMandatoryBlocks + restoring.overMandatoryBlocks), 0);
            expectGreaterThan ((int) capturing.pages[RigLooper::undoBeforeWrite], 0, "it did copy before writes");
        }

        beginTest ("21. Concurrent saves: copies made during a capture, across an undo, during a restoration, and called off for budget each equal the loop at one block");
        {
            // Each block's sounding loop is hashed under its version (a version names one sound);
            // a saver copies all the while, and each copy must be the sound its version names.
            const auto hash = [] (const juce::AudioBuffer<float>& b)
            {
                juce::uint64 h = 1469598103934665603ull ^ (juce::uint64) b.getNumSamples();
                for (int ch = 0; ch < b.getNumChannels() && b.getNumSamples() > 0; ++ch)
                {
                    const auto* p = reinterpret_cast<const juce::uint32*> (b.getReadPointer (ch));
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        h = (h ^ p[i]) * 1099511628211ull;
                }
                return h;
            };
            struct Copy { int version; juce::uint64 hash; };
            const auto run = [&] (const juce::String& what, Drive& d, int layer, const std::function<void (const std::function<void (int)>&)>& script)
            {
                std::map<int, juce::uint64> sounds;
                int contradictions = 0;
                const auto note = [&]
                {
                    const int v = d.looper.getLayer (layer).version.load();
                    const auto h = hash (d.looper.soundingLoopForTests (layer));
                    if (auto it = sounds.find (v); it == sounds.end())
                        sounds[v] = h;
                    else if (it->second != h)
                        ++contradictions;
                };
                const auto blocks = [&] (int k)
                {
                    for (int i = 0; i < k; ++i)
                    {
                        d.block();
                        note();
                    }
                };
                note();
                std::mutex m;
                std::vector<Copy> copies;
                std::atomic<bool> saving { true };
                const auto stats0 = d.looper.getCopyStats();
                std::thread saver ([&]
                {
                    while (saving)
                    {
                        int v = -1;
                        const auto c = d.looper.copyLayerAudio (layer, &v);
                        if (v >= 0)
                        {
                            const std::lock_guard<std::mutex> g (m);
                            copies.push_back ({ v, hash (c) });
                        }
                    }
                });
                script (blocks);
                saving = false;
                saver.join();
                const auto stats1 = d.looper.getCopyStats();
                int wrong = 0, unknown = 0;
                for (const auto& c : copies)
                {
                    if (auto it = sounds.find (c.version); it == sounds.end())
                        ++unknown;
                    else if (it->second != c.hash)
                        ++wrong;
                }
                logMessage ("  " + what + ": " + juce::String ((int) copies.size()) + " copies, " + juce::String (wrong) + " not their version's sound, "
                            + juce::String (unknown) + " of a version no block had; retries " + juce::String (stats1.retries - stats0.retries));
                expectEquals (contradictions, 0, what + ": a version names one sound");
                expectGreaterThan ((int) copies.size(), 0, what + ": copies were made");
                expectEquals (wrong, 0, what + ": each copy is the loop at one block");
                expectEquals (unknown, 0, what);
                return stats1.retries - stats0.retries;
            };
            RigLooper::copySliceForTests = 4 * RigLooper::kCopyPageSamples;   // captures and restorations take many blocks
            {
                Drive d (kRate, kBlock);
                dubInput (d);
                putLoop (d, 0, odu::markedLoop (5 * 48000, 210));
                d.blocks (3);
                run ("during a capture, then across its undo, then during the restoration", d, 0, [&] (const std::function<void (int)>& blocks)
                {
                    for (int round = 0; round < 4; ++round)
                    {
                        d.looper.pressRecord (0);
                        blocks (30);
                        expect (! d.looper.isUndoCaptureCompleteForTests(), "the capture is still running");
                        d.looper.undo (0);   // the undo interrupts the capture (and the copies)
                        blocks (1);
                        blocks (10);
                        d.looper.pressRecord (0);
                        blocks (200);         // a second overdub, its capture complete and more written
                        d.looper.pressRecord (0);
                        blocks (1);
                        d.looper.undo (0);
                        blocks (1);
                        expect (d.looper.isUndoRestoring(), "restoring");
                        for (int k = 0; k < 2000 && d.looper.isUndoRestoring(); ++k)
                            blocks (1);
                        blocks (5);
                    }
                });
            }
            {
                // A loop overdubbed faster than the guarantee allows: its copies are called off and
                // tried again; another loop's undo is untouched throughout.
                Drive d (kRate, kBlock);
                dubInput (d);
                putLoop (d, 0, odu::markedLoop (40 * 48000, 211));
                const int n = 768000 * 2;
                d.looper.setLayerAudio (1, odu::markedLoop (n, 212), 2.0 * 0.999);
                d.looper.getLayer (1).source = RigLooper::input;
                d.blocks (3);
                const auto original = d.looper.soundingLoopForTests (0);
                d.looper.pressRecord (0);
                d.seconds (1.0);
                d.looper.pressRecord (0);
                d.block();
                expect (d.looper.canUndo (0), "loop 1 holds the undo");
                int lost = 0;
                const auto retries = run ("a squeezed loop overdubbing, its copies called off", d, 1, [&] (const std::function<void (int)>& blocks)
                {
                    d.looper.pressRecord (1);
                    for (int k = 0; k < 300; ++k)
                    {
                        blocks (1);
                        lost += d.looper.canUndo (0) ? 0 : 1;
                    }
                    d.looper.pressRecord (1);
                    blocks (3);
                });
                expectGreaterThan (retries, 0, "the copies of the squeezed overdub were called off and tried again");
                expectEquals (lost, 0, "loop 1's undo stays throughout");
                d.looper.undo (0);
                settle (d);
                expect (same (d.looper.copyLayerAudio (0), original), "and loop 1's undo is exact");
            }
            RigLooper::copySliceForTests = 0;
        }
    }
};

static RigOverdubUndoRestoreTests rigOverdubUndoRestoreTests;
