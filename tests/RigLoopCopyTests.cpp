#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigExport.h"
#include "RigLooper.h"

#include <algorithm>
#include <thread>

// Copying a loop (saving a show, storing a loop bank, Trim, Export) while the loops play.
//
// Each loop is a sine at its own frequency, a whole number of cycles per 256-sample block, and
// an audio thread runs in real time, checking every block for every loop (Goertzel). A block
// where a loop drops below half its level counts as missing: that's a dropout you'd hear, which
// a late-callback count doesn't show (the old copy held the layer's lock, and the audio thread
// skipped that loop, on time, for the whole copy).
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr int kBins[] = { 6, 11, 17, 23 };   // x 187.5 Hz
constexpr int kTones = 4;
// 32768 samples a beat, so the clock moves an exact number of samples each block and an overdub
// writes each sample of the loop exactly once per pass (the test can tell when it was written).
constexpr double kBpm = kRate * 60.0 / 32768.0;
constexpr double kSamplesPerBeat = 32768.0;

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

juce::AudioBuffer<float> tone (int tone, double beats, float level = 0.1f)
{
    const int n = (int) std::round (beats * kSamplesPerBeat);
    juce::AudioBuffer<float> b (2, n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * kBins[tone] * (i % kBlock) / kBlock));
    return b;
}

bool same (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, float tolerance = 0.0f)
{
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() < 2 || b.getNumChannels() < 2)
        return false;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (std::abs (a.getSample (ch, i) - b.getSample (ch, i)) > tolerance)
                return false;
    return true;
}

// Runs `process` every block in real time on its own thread, and counts the blocks where each
// tone is missing from the output (while measuring) and the blocks that came late.
struct AudioThread
{
    std::function<void (juce::AudioBuffer<float>&)> process;
    int tones = kTones;
    // A bursty host: this many blocks back to back, then a wait for all of them.
    std::atomic<int> burst { 1 };
    // Once: a gap this long before the next block (a host that stalls), then catching up.
    std::atomic<int> gapOnceMs { 0 };
    // The longest a block took (ms), while measuring.
    std::atomic<double> worstMs { 0 };

    std::atomic<bool> measuring { false };
    std::atomic<int> blocks { 0 }, late { 0 };
    std::array<std::atomic<int>, kTones> missing {};
    std::array<float, kTones> reference {};

    void start()
    {
        running = true;
        thread = std::thread ([this] { run(); });
        while (! warm.load())
            juce::Thread::sleep (5);
    }
    void stop()
    {
        running = false;
        if (thread.joinable())
            thread.join();
    }
    ~AudioThread() { stop(); }

    // Measures while `f` runs (and a little after), returns the blocks with any tone missing.
    int during (const std::function<void()>& f)
    {
        for (auto& m : missing)
            m = 0;
        blocks = 0;
        late = 0;
        worstMs = 0;
        measuring = true;
        f();
        juce::Thread::sleep (60);
        measuring = false;
        int total = 0;
        for (int t = 0; t < tones; ++t)
            total += missing[(size_t) t].load();
        return total;
    }

private:
    std::thread thread;
    std::atomic<bool> running { false }, warm { false };

    void run()
    {
        juce::AudioBuffer<float> out (2, kBlock);
        std::array<double, kTones> sum {};
        int count = 0;
        const double period = 1000.0 * kBlock / kRate;
        auto next = juce::Time::getMillisecondCounterHiRes();
        while (running)
        {
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            process (out);
            const auto took = juce::Time::getMillisecondCounterHiRes() - t0;
            if (took > period)
                ++late;
            if (measuring && took > worstMs.load())
                worstMs = took;
            for (int t = 0; t < tones; ++t)
            {
                const float m = goertzel (out.getReadPointer (0), kBlock, kBins[t]);
                if (count < 100)
                    sum[(size_t) t] += m;
                else if (count == 100)
                    reference[(size_t) t] = (float) (sum[(size_t) t] / 100.0);
                else if (measuring && m < 0.5f * reference[(size_t) t])
                    ++missing[(size_t) t];
            }
            if (count == 101)
                warm = true;
            if (measuring)
                ++blocks;
            ++count;
            next += period;
            if (const int gap = gapOnceMs.exchange (0); gap > 0)
                juce::Thread::sleep (gap);   // then it catches up, block after block
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 1.0 && (count + 1) % juce::jmax (1, burst.load()) == 0)
                juce::Thread::sleep ((int) wait);
        }
    }
};

// A looper with `tones` loops playing, driven in real time; `dc` is a source an overdub adds.
struct PlayingLooper
{
    RigLooper looper;
    std::atomic<double> beat { 0 };
    juce::AudioBuffer<float> dc { 2, kBlock };
    AudioThread audio;

    explicit PlayingLooper (int tones = kTones, double beats = 8.0)
    {
        looper.prepare (kRate, kBlock);
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 1.0f / 1024.0f, kBlock);
        for (int t = 0; t < tones; ++t)
            looper.setLayerAudio (t, tone (t, beats), beats);
        audio.tones = tones;
        audio.process = [this] (juce::AudioBuffer<float>& out)
        {
            RigLooper::Clock clock;
            clock.beat = beat.load();
            clock.bpm = kBpm;
            clock.playing = true;
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            sources[RigLooper::input] = &dc;
            looper.process (clock, sources, out);
            beat = clock.beat + kBlock / kSamplesPerBeat;
        };
        audio.start();
    }
    ~PlayingLooper() { audio.stop(); }
};

// While a pause in the copy is set, the audio thread is known to run at least `ms` during it
// (and copies `slice` samples a block, so a copy takes many blocks).
struct CopyPause
{
    CopyPause (int ms, bool underLockToo = false, std::function<void (int)> during = {}, int slice = 8192)
    {
        RigLooper::copySliceForTests = slice;
        RigLooper::copyPauseForTests = [ms, underLockToo, during] (int layer, bool underLock)
        {
            if (underLock && ! underLockToo)
                return;
            if (during)
                during (layer);
            juce::Thread::sleep (ms);
        };
    }
    ~CopyPause()
    {
        RigLooper::copyPauseForTests = nullptr;
        RigLooper::copyUnderLockForTests = false;
        RigLooper::copySliceForTests = 0;
    }
};

// What an overdub of the dc source added to `before`: for each sample, whether it was written.
// A copy made at one moment has one run of written samples (round the loop); a torn copy has gaps.
int writtenRuns (const juce::AudioBuffer<float>& before, const juce::AudioBuffer<float>& after, int& written, bool& exact)
{
    const int n = before.getNumSamples();
    written = 0;
    exact = after.getNumSamples() == n;
    if (! exact)
        return 0;
    std::vector<bool> w ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float b = before.getSample (ch, i), a = after.getSample (ch, i);
            const bool hit = juce::exactlyEqual (a, b + 1.0f / 1024.0f);
            if (! juce::exactlyEqual (a, b) && ! hit)
                exact = false;
            if (ch == 0)
                w[(size_t) i] = hit;
            else if (hit != w[(size_t) i])
                exact = false;
        }
        written += w[(size_t) i] ? 1 : 0;
    }
    int edges = 0;
    for (int i = 0; i < n; ++i)
        if (w[(size_t) i] != w[(size_t) ((i + n - 1) % n)])
            ++edges;
    return edges / 2;
}
}

// The same for an overdub that has gone round many times: how many passes each sample holds.
// A copy of one moment holds k+1 passes before the write position and k after: at most two
// stretches. Returns the stretches; `low`/`high` the fewest and most passes.
int passStretches (const juce::AudioBuffer<float>& before, const juce::AudioBuffer<float>& after, int& low, int& high, bool& exact)
{
    const int n = before.getNumSamples();
    exact = after.getNumSamples() == n;
    low = 1 << 30;
    high = -1;
    if (! exact)
        return 0;
    std::vector<int> k ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const double d = ((double) after.getSample (ch, i) - before.getSample (ch, i)) * 1024.0;
            const int passes = (int) std::lround (d);
            if (std::abs (d - passes) > 1.0e-3)
                exact = false;
            if (ch == 0)
                k[(size_t) i] = passes;
            else if (passes != k[(size_t) i])
                exact = false;
        }
        low = juce::jmin (low, k[(size_t) i]);
        high = juce::jmax (high, k[(size_t) i]);
    }
    int edges = 0;
    for (int i = 0; i < n; ++i)
        if (k[(size_t) i] != k[(size_t) ((i + n - 1) % n)])
            ++edges;
    return juce::jmax (1, edges);
}

struct RigLoopCopyTests : juce::UnitTest
{
    RigLoopCopyTests() : juce::UnitTest ("Rig loop copy while playing", "Rig") {}

    void runTest() override
    {
        beginTest ("The detector catches the old way: a copy under the lock drops that loop out");
        {
            PlayingLooper rig;
            RigLooper::copyUnderLockForTests = true;
            const CopyPause pause (150, true);
            juce::AudioBuffer<float> copy;
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            logMessage ("  under the lock: " + juce::String (rig.audio.missing[0].load()) + " blocks of loop 1 missing, "
                        + juce::String (rig.audio.late.load()) + " late");
            expectGreaterThan (rig.audio.missing[0].load(), 15, "the paused copy should silence loop 1 (else the detector is blind)");
            expectEquals (missing, rig.audio.missing[0].load(), "only the loop being copied");
            expect (same (copy, tone (0, 8.0)));
        }

        beginTest ("Copying a playing loop: no block missing, and the copy is exact");
        {
            PlayingLooper rig;
            const CopyPause pause (150);
            juce::AudioBuffer<float> copy;
            const auto before = rig.looper.getCopyStats();
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            const auto after = rig.looper.getCopyStats();
            logMessage ("  " + juce::String (rig.audio.blocks.load()) + " blocks, " + juce::String (missing) + " missing, "
                        + juce::String (rig.audio.late.load()) + " late");
            expectEquals (missing, 0);
            expectEquals (after.byAudioThread - before.byAudioThread, 1);
            expectEquals (after.underLock - before.underLock, 0);
            expectEquals (after.finishedUnderLock - before.finishedUnderLock, 0);
            expect (same (copy, tone (0, 8.0)), "sample for sample");
            for (int t = 1; t < kTones; ++t)
                expect (same (rig.looper.copyLayerAudio (t), tone (t, 8.0)));
            expect (rig.looper.copyLayerAudio (9).getNumSamples() == 0, "an empty layer copies as nothing");
        }

        beginTest ("Overdubbing while it's copied: no block missing, and the copy is one moment of the overdub");
        {
            PlayingLooper rig;
            const auto original = tone (0, 8.0);
            rig.looper.getLayer (0).source = RigLooper::input;
            rig.looper.pressRecord (0);   // overdub the dc source over loop 1
            juce::Thread::sleep (400);
            expect (rig.looper.getState (0) == RigLooper::State::overdubbing);
            // The overdub writes ~1 s more while it's copied (a page copied every 4 blocks).
            const CopyPause pause (250, false, {}, 256);
            juce::AudioBuffer<float> copy;
            const auto before = rig.looper.getCopyStats();
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            const auto after = rig.looper.getCopyStats();
            rig.looper.pressRecord (0);
            juce::Thread::sleep (50);
            int written = 0, writtenNow = 0;
            bool exact = false, exactNow = false;
            const int runs = writtenRuns (original, copy, written, exact);
            writtenRuns (original, rig.looper.copyLayerAudio (0), writtenNow, exactNow);
            const int onWrite = after.pagesCopiedBeforeOverdub - before.pagesCopiedBeforeOverdub;
            logMessage ("  " + juce::String (missing) + " blocks missing; the copy has " + juce::String (written) + " samples overdubbed in "
                        + juce::String (runs) + " run(s) (the loop now " + juce::String (writtenNow) + "); "
                        + juce::String (onWrite) + " pages copied just before the overdub wrote them");
            expectEquals (missing, 0);
            expect (exact, "every sample is either the loop or the loop plus one pass of the overdub");
            expectEquals (runs, 1, "one moment: what was overdubbed is one unbroken stretch");
            expectGreaterThan (written, (int) (0.3 * kRate), "what was overdubbed before it was asked for");
            expectGreaterThan (writtenNow - written, (int) (0.4 * kRate), "and none of what came after");
            expectGreaterThan (onWrite, 5);
            expectEquals (after.byAudioThread - before.byAudioThread, 1);
            expectEquals (after.underLock - before.underLock, 0);
        }

        beginTest ("A loop bank swapping in at the bar while it's copied: the copy is all the new loop");
        {
            PlayingLooper rig;
            const auto next = tone (0, 8.0, -0.1f);   // as loud, so it isn't counted as missing
            bool queued = false;
            const CopyPause pause (120, false, [&] (int)
            {
                if (! std::exchange (queued, true))
                    rig.looper.queueLayerAudio (0, next, 8.0, rig.beat.load() + 0.05, 0.8f, false);
            });
            juce::AudioBuffer<float> copy;
            const auto before = rig.looper.getCopyStats();
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            const auto after = rig.looper.getCopyStats();
            expectEquals (missing, 0);
            expect (same (copy, next), "exactly the loop that came in, never part of each");
            expectGreaterThan (after.retries - before.retries, 0);
            rig.looper.maintain();
        }

        beginTest ("Undo and clear while it's copied");
        {
            PlayingLooper rig;
            const auto original = tone (1, 8.0);
            rig.looper.getLayer (1).source = RigLooper::input;
            rig.looper.pressRecord (1);
            juce::Thread::sleep (200);
            rig.looper.pressRecord (1);   // overdub done
            juce::Thread::sleep (50);
            expect (rig.looper.canUndo (1));
            bool asked = false;
            {
                const CopyPause pause (100, false, [&] (int) { if (! std::exchange (asked, true)) rig.looper.undo (1); });
                juce::AudioBuffer<float> copy;
                const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (1); });
                expectEquals (missing, 0);
                expect (same (copy, original), "the copy is the loop after the undo");
            }
            asked = false;
            {
                const CopyPause pause (100, false, [&] (int) { if (! std::exchange (asked, true)) rig.looper.clear (2); });
                juce::AudioBuffer<float> copy;
                rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (2); });
                expectEquals (copy.getNumSamples(), 0, "cleared while it was copied: nothing");
            }
        }

        beginTest ("Replaced on every try: kCopyRounds tries, then under the lock, exact");
        {
            PlayingLooper rig (1);
            const auto a = tone (0, 8.0), b = tone (0, 8.0, -0.1f);
            int calls = 0;
            const CopyPause pause (30, false, [&] (int) { rig.looper.setLayerAudio (0, ++calls % 2 == 1 ? b : a, 8.0); });
            const auto before = rig.looper.getCopyStats();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            juce::AudioBuffer<float> copy;
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto after = rig.looper.getCopyStats();
            logMessage ("  " + juce::String (calls) + " replacements, " + juce::String (after.retries - before.retries) + " retries, "
                        + juce::String (ms, 0) + " ms, " + juce::String (missing) + " blocks missing");
            expectEquals (after.retries - before.retries, RigLooper::kCopyRounds, "it tries kCopyRounds times");
            expectEquals (after.underLock - before.underLock, 1, "then copies under the lock");
            expect (same (copy, calls % 2 == 1 ? b : a), "the loop as it is then, whole");
            expectLessThan (ms, 2000.0, "and it doesn't keep trying");
            rig.looper.maintain();
        }

        beginTest ("Continuous overdub round and round a short loop during a slow copy: one moment, exact");
        {
            PlayingLooper rig (1, 1.0);   // 32 pages
            const auto original = tone (0, 1.0);
            rig.looper.getLayer (0).source = RigLooper::input;
            rig.looper.pressRecord (0);
            juce::Thread::sleep (1700);   // the loop is 0.68 s: 2 passes and more already
            // A page every 16 blocks: ~2.7 s a copy, the overdub going round ~4 more times.
            const CopyPause pause (100, false, {}, 64);
            juce::AudioBuffer<float> copy;
            const auto before = rig.looper.getCopyStats();
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            const auto after = rig.looper.getCopyStats();
            rig.looper.pressRecord (0);
            juce::Thread::sleep (50);
            int low = 0, high = 0, lowNow = 0, highNow = 0;
            bool exact = false, exactNow = false;
            const int stretches = passStretches (original, copy, low, high, exact);
            passStretches (original, rig.looper.copyLayerAudio (0), lowNow, highNow, exactNow);
            const int onWrite = after.pagesCopiedBeforeOverdub - before.pagesCopiedBeforeOverdub;
            logMessage ("  copy holds " + juce::String (low) + "-" + juce::String (high) + " passes in " + juce::String (stretches)
                        + " stretch(es); the loop now " + juce::String (lowNow) + "-" + juce::String (highNow) + "; "
                        + juce::String (onWrite) + " of 32 pages copied before the overdub wrote them; " + juce::String (missing) + " missing");
            expectEquals (missing, 0);
            expect (exact, "every sample is the loop plus a whole number of passes");
            expect (stretches <= 2 && high - low <= 1, "one moment: k+1 passes up to where it was writing, k after");
            expectGreaterOrEqual (low, 2, "the passes from before it was asked for");
            expectGreaterOrEqual (lowNow - high, 3, "none of the passes during the copy");
            expectGreaterThan (onWrite, 16, "the overdub kept running into pages not yet copied");
            expectEquals (after.byAudioThread - before.byAudioThread, 1);
            expectEquals (after.underLock + after.finishedUnderLock - before.underLock - before.finishedUnderLock, 0);
        }

        beginTest ("A slow copy of a playing loop: many blocks, nothing missing, never the lock");
        {
            PlayingLooper rig;
            const CopyPause pause (0, false, {}, 512);   // ~500 blocks
            const auto before = rig.looper.getCopyStats();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            juce::AudioBuffer<float> copy;
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (3); });
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto after = rig.looper.getCopyStats();
            logMessage ("  " + juce::String (ms, 0) + " ms, " + juce::String (missing) + " missing, worst block "
                        + juce::String (rig.audio.worstMs.load(), 3) + " ms");
            expectGreaterThan (ms, 1500.0);
            expectEquals (missing, 0);
            expect (same (copy, tone (3, 8.0)));
            expectEquals (after.byAudioThread - before.byAudioThread, 1);
            expectEquals (after.underLock + after.finishedUnderLock - before.underLock - before.finishedUnderLock, 0);
        }

        beginTest ("The audio stops partway through a copy: it finishes under the lock, exact");
        {
            PlayingLooper rig;
            const auto original = tone (0, 8.0);
            rig.looper.getLayer (0).source = RigLooper::input;
            rig.looper.pressRecord (0);
            juce::Thread::sleep (300);
            const CopyPause pause (100, false, [&] (int) { rig.audio.stop(); }, 1024);
            const auto before = rig.looper.getCopyStats();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto copy = rig.looper.copyLayerAudio (0);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto after = rig.looper.getCopyStats();
            int written = 0;
            bool exact = false;
            const int runs = writtenRuns (original, copy, written, exact);
            logMessage ("  finished in " + juce::String (ms, 0) + " ms, " + juce::String (written) + " samples overdubbed in " + juce::String (runs) + " run(s)");
            expectEquals (after.finishedUnderLock - before.finishedUnderLock, 1);
            expect (exact && runs == 1, "still one moment");
            expectGreaterThan (ms, (double) RigLooper::kStallFloorMs);
            expectLessThan (ms, (double) RigLooper::kStallCapMs + 1000.0);
        }

        beginTest ("The audio stops before a copy starts: it's made under the lock");
        {
            PlayingLooper rig;
            rig.audio.stop();
            const auto before = rig.looper.getCopyStats();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto copy = rig.looper.copyLayerAudio (1);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto after = rig.looper.getCopyStats();
            logMessage ("  " + juce::String (ms, 0) + " ms");
            expect (same (copy, tone (1, 8.0)));
            expectEquals (after.underLock - before.underLock, 1);
            expectLessThan (ms, (double) RigLooper::kStallCapMs + 1000.0);
        }

        beginTest ("A copy called off: the loop cleared from another thread mid-copy copies as nothing");
        {
            PlayingLooper rig;
            const CopyPause pause (50, false, [&] (int) { rig.looper.reset (2); }, 1024);
            const auto before = rig.looper.getCopyStats();
            juce::AudioBuffer<float> copy;
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (2); });
            const auto after = rig.looper.getCopyStats();
            expectEquals (copy.getNumSamples(), 0);
            expectEquals (after.retries - before.retries, 1);
            expectEquals (missing, rig.audio.missing[2].load(), "only the loop that was cleared");
            expect (same (rig.looper.copyLayerAudio (1), tone (1, 8.0)), "the next copy is fine");
        }

        beginTest ("A bursty host and a long gap between callbacks: the copy still runs on the audio thread");
        {
            PlayingLooper rig;
            rig.audio.burst = 24;   // 24 blocks at once, then ~128 ms of nothing
            juce::Thread::sleep (600);
            {
                const CopyPause pause (0, false, {}, 2048);   // ~130 blocks, several bursts
                const auto before = rig.looper.getCopyStats();
                juce::AudioBuffer<float> copy;
                const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
                const auto after = rig.looper.getCopyStats();
                logMessage ("  bursts: " + juce::String (missing) + " missing");
                expectEquals (missing, 0);
                expect (same (copy, tone (0, 8.0)));
                expectEquals (after.byAudioThread - before.byAudioThread, 1, "no lock: a burst's tiny gaps aren't a stall");
                expectEquals (after.underLock + after.finishedUnderLock - before.underLock - before.finishedUnderLock, 0);
            }
            rig.audio.burst = 1;
            juce::Thread::sleep (300);
            {
                // A 700 ms gap while it copies, then a burst catching up.
                const CopyPause pause (0, false, [&] (int) { rig.audio.gapOnceMs = 700; }, 2048);
                const auto before = rig.looper.getCopyStats();
                const auto copy = rig.looper.copyLayerAudio (1);
                const auto after = rig.looper.getCopyStats();
                expect (same (copy, tone (1, 8.0)));
                expectEquals (after.byAudioThread - before.byAudioThread, 1, "a 700 ms gap isn't the audio stopping");
                expectEquals (after.underLock + after.finishedUnderLock - before.underLock - before.finishedUnderLock, 0);
            }
            {
                // Callbacks in bursts 300 ms apart (a host that's barely keeping up): still not a stall.
                rig.audio.burst = 56;
                juce::Thread::sleep (1500);
                const CopyPause pause (0, false, {}, 4096);
                const auto before = rig.looper.getCopyStats();
                const auto copy = rig.looper.copyLayerAudio (2);
                const auto after = rig.looper.getCopyStats();
                expect (same (copy, tone (2, 8.0)));
                expectEquals (after.byAudioThread - before.byAudioThread, 1);
                rig.audio.burst = 1;
            }
        }

        beginTest ("A copy on another thread while the window replaces loops and frees old ones");
        {
            PlayingLooper rig (2);
            const auto a = tone (0, 8.0), b = tone (0, 8.0, -0.1f);
            const CopyPause pause (15);
            std::atomic<bool> done { false };
            std::atomic<int> good { 0 }, bad { 0 };
            std::thread host ([&]
            {
                for (int i = 0; i < 25; ++i)
                {
                    const auto copy = rig.looper.copyLayerAudio (0);
                    (same (copy, a) || same (copy, b) ? good : bad)++;
                }
                done = true;
            });
            for (int i = 0; ! done; ++i)
            {
                rig.looper.setLayerAudio (0, i % 2 == 0 ? b : a, 8.0);
                if (i % 3 == 0)
                    rig.looper.queueLayerAudio (0, i % 2 == 0 ? a : b, 8.0, -1.0, 0.8f, false);
                rig.looper.maintain();
                juce::Thread::sleep (7);
            }
            host.join();
            rig.looper.maintain();
            expectEquals (good.load(), 25, "every copy is exactly one of the loops");
            expectEquals (bad.load(), 0);
        }

        beginTest ("No audio running: the copy is made at once, under the lock");
        {
            RigLooper looper;
            looper.prepare (kRate, kBlock);
            looper.setLayerAudio (0, tone (0, 8.0), 8.0);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto copy = looper.copyLayerAudio (0);
            expectLessThan (juce::Time::getMillisecondCounterHiRes() - t0, 100.0);
            expect (same (copy, tone (0, 8.0)));
            expectEquals (looper.getCopyStats().underLock, 1);
            expectEquals (looper.getCopyStats().byAudioThread, 0);
        }

        beginTest ("A layer recording copies as nothing, and a take that ends copies whole");
        {
            PlayingLooper rig (2);
            rig.looper.getLayer (5).source = RigLooper::input;
            rig.looper.getLayer (5).bars = 1;
            rig.looper.pressRecord (5);
            for (int i = 0; i < 200 && rig.looper.getState (5) != RigLooper::State::recording; ++i)
                juce::Thread::sleep (20);
            expect (rig.looper.getState (5) == RigLooper::State::recording);
            expectEquals (rig.looper.copyLayerAudio (5).getNumSamples(), 0);
            for (int i = 0; i < 200 && rig.looper.getState (5) != RigLooper::State::playing; ++i)
                juce::Thread::sleep (20);
            int length = 0;
            const int missing = rig.audio.during ([&] { length = rig.looper.copyLayerAudio (5).getNumSamples(); });
            expectWithinAbsoluteError (length, (int) (4 * kSamplesPerBeat), 1);
            expectEquals (missing, 0);
        }

        engineTests();
    }

    // The same through the Rig: saving a show, storing a loop bank, Trim, Export for Logic.
    void engineTests()
    {
        auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kb-loop-copy-tests");
        folder.deleteRecursively();
        folder.createDirectory();
        constexpr double beats = 40.0;   // 20 s loops

        auto e = std::make_unique<RigEngine>();
        e->prepare (kRate, kBlock);
        e->getTransport().setBpm (kBpm);
        for (int t = 0; t < kTones; ++t)
            e->getLooper().setLayerAudio (t, tone (t, beats), beats);
        e->saveLoopBank ("A");
        e->duplicateLoopBank (0);
        e->switchLoopBank (0);
        e->serviceLoopBanks();
        if (auto* p = e->getDrums().params.getParameter ("sequencer"))
            p->setValueNotifyingHost (0.0f);
        if (auto* p = e->getBass().params.getParameter ("sequencer"))
            p->setValueNotifyingHost (0.0f);
        e->getTransport().play();

        AudioThread audio;
        audio.process = [&] (juce::AudioBuffer<float>& out) { e->process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock); };
        audio.start();
        const CopyPause pause (40);   // every copy overlaps several blocks

        const auto report = [&] (const char* what, int missing)
        {
            logMessage (juce::String ("  ") + what + ": " + juce::String (audio.blocks.load()) + " blocks, " + juce::String (missing)
                        + " with a loop missing, " + juce::String (audio.late.load()) + " late");
            expectEquals (missing, 0, what);
        };

        beginTest ("Rig: saving a show while the loops play drops nothing");
        const auto show = folder.getChildFile ("Copy test.crateshow");
        rigshow::Result first, again;
        report ("first save", audio.during ([&] { first = e->saveShowFile (show); }));
        report ("save with nothing changed", audio.during ([&] { again = e->saveShowFile (show); }));
        expect (first.ok && again.ok, first.error + again.error);
        {
            RigEngine reopened;
            reopened.prepare (kRate, kBlock);
            if (auto xml = juce::parseXML (show))
                reopened.loadShow (juce::ValueTree::fromXml (*xml));
            reopened.readLoopAudio (show);
            for (int t = 0; t < kTones; ++t)
                expect (same (reopened.getLooper().copyLayerAudio (t), tone (t, beats), 1.0e-6f), "loop " + juce::String (t + 1) + " saved whole");
        }

        beginTest ("Rig: storing a loop bank and switching drops nothing");
        report ("bank switch", audio.during ([&] { e->switchLoopBank (1); e->serviceLoopBanks(); }));
        juce::Thread::sleep (3200);   // the new bank comes in at the bar
        e->getLooper().maintain();
        report ("switch back", audio.during ([&] { e->switchLoopBank (0); e->serviceLoopBanks(); }));
        juce::Thread::sleep (3200);
        e->getLooper().maintain();

        beginTest ("Rig: opening Trim and Export for Logic drop nothing");
        report ("open Trim", audio.during ([&] { expect (e->getLooper().openTrim (2)); }));
        report ("Export render", audio.during ([&] { expect (RigExport::renderLoop (*e, 3, kBpm, kRate, false).getNumSamples() > 0); }));

        audio.stop();
        e->getTransport().stop();
        e = nullptr;
        folder.deleteRecursively();
    }
};

static RigLoopCopyTests rigLoopCopyTests;

//==============================================================================
// The waveform the window draws (readPeaks): right for every way a sound gets on a layer, and
// safe to read at any moment while the audio thread records, overdubs, undoes, swaps and clears.
namespace
{
// A bin of a loop `n` long: the fewest samples (2^shift) that fit it in RigLooper::kPeakBins.
int peakBin (int n)
{
    int shift = 0;
    while (((juce::int64) RigLooper::kPeakBins << shift) < n)
        ++shift;
    return 1 << shift;
}

// The loudest sample (either channel, up to 1) of `a` from `from` to `to`: round the loop of `n`
// if `wrap`, else only what's inside it.
float loudest (const juce::AudioBuffer<float>& a, int n, juce::int64 from, juce::int64 to, bool wrap)
{
    if (! wrap)
    {
        from = juce::jmax ((juce::int64) 0, from);
        to = juce::jmin ((juce::int64) n, to);
    }
    const float* l = a.getReadPointer (0);
    const float* r = a.getReadPointer (1);
    float m = 0;
    for (auto i = from; i < to; ++i)
    {
        const auto j = (size_t) (((i % n) + n) % n);
        m = juce::jmax (m, std::abs (l[j]), std::abs (r[j]));
    }
    return juce::jmin (1.0f, m);
}

// How many of readPeaks' slices (of a loop `n` long) are not between the loudest sample of
// `ref` in that slice (`offset` samples on) and the loudest in it widened by `slack` each side.
int outside (const std::vector<float>& peaks, const juce::AudioBuffer<float>& ref, int n, int offset, int slack, bool wrap)
{
    int bad = 0;
    const auto slices = (juce::int64) peaks.size();
    for (juce::int64 p = 0; p < slices; ++p)
    {
        const auto from = p * n / slices, to = (p + 1) * n / slices;
        const float v = peaks[(size_t) p];
        if (to <= from)
        {
            bad += juce::exactlyEqual (v, 0.0f) ? 0 : 1;   // a slice with no samples in it (more slices than samples)
            continue;
        }
        const float low = loudest (ref, n, from + offset, to + offset, wrap);
        const float high = loudest (ref, n, from + offset - slack, to + offset + slack, wrap);
        if (! std::isfinite (v) || v < low || v > high)
            ++bad;
    }
    return bad;
}

// A test sound: a level that changes every 3000 samples, alternating in sign, and (with spikes)
// a loud single sample every 7919 on the left and 5003 on the right: the loudest, which a scan
// that looks at every Nth sample misses.
juce::AudioBuffer<float> testSound (int n, int seed, bool spikes = true)
{
    juce::Random random (seed);
    juce::AudioBuffer<float> b (2, n);
    float level = 0;
    auto* l = b.getWritePointer (0);
    auto* r = b.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        if (i % 3000 == 0)
            level = 0.05f + 0.6f * random.nextFloat();
        const float v = (i & 1) != 0 ? -level : level;
        l[i] = spikes && i % 7919 == 1234 ? 0.9f : v;
        r[i] = spikes && i % 5003 == 17 ? -0.8f : -0.5f * v;
    }
    return b;
}

// How many slices are quieter than the loudest sample of `ref` in them.
int under (const std::vector<float>& peaks, const juce::AudioBuffer<float>& ref, int n)
{
    int count = 0;
    const auto slices = (juce::int64) peaks.size();
    for (juce::int64 p = 0; p < slices; ++p)
        count += peaks[(size_t) p] < loudest (ref, n, p * n / slices, (p + 1) * n / slices, false) ? 1 : 0;
    return count;
}

bool allZero (const std::vector<float>& peaks)
{
    return std::all_of (peaks.begin(), peaks.end(), [] (float v) { return juce::exactlyEqual (v, 0.0f); });
}

// Drives a looper block by block: `fill` writes the block's sources (the mix and the input),
// given the samples since the clock started.
struct PeakRig
{
    RigLooper looper;
    RigLooper::Clock clock;
    juce::AudioBuffer<float> mix { 2, kBlock }, in { 2, kBlock }, out { 2, kBlock };
    std::function<void (juce::AudioBuffer<float>&, juce::AudioBuffer<float>&, juce::int64)> fill;
    juce::int64 fed = 0;
    int worstOps = 0;   // the most waveform work a layer's block did

    explicit PeakRig (double bpm = 120.0)
    {
        looper.prepare (kRate, kBlock);
        clock.bpm = bpm;
        clock.beatsPerBar = 4;
    }
    void blocks (int count)
    {
        for (int k = 0; k < count; ++k)
        {
            mix.clear();
            in.clear();
            if (fill)
                fill (mix, in, fed);
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            sources[RigLooper::mix] = &mix;
            sources[RigLooper::input] = &in;
            const bool wasPlaying = clock.playing;
            if (looper.process (clock, sources, out))
                clock.playing = true;
            if (wasPlaying)
            {
                fed += kBlock;
                clock.beat += kBlock * clock.bpm / (60.0 * kRate);
            }
            worstOps = juce::jmax (worstOps, looper.takePeakOpsForTests());
        }
    }
    void seconds (double s) { blocks ((int) std::ceil (s * kRate / kBlock)); }
    bool until (int layer, RigLooper::State state, double maxSeconds)
    {
        for (int k = 0; k < (int) (maxSeconds * kRate / kBlock) && looper.getState (layer) != state; ++k)
        {
            blocks (1);
            if (k % 100 == 0)
                looper.maintain();
        }
        return looper.getState (layer) == state;
    }
};

void copySource (const juce::AudioBuffer<float>& sound, juce::AudioBuffer<float>& to, juce::int64 at)
{
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
            to.setSample (ch, i, at + i < sound.getNumSamples() ? sound.getSample (ch, (int) (at + i)) : 0.0f);
}
}

struct RigWaveformPeakTests : juce::UnitTest
{
    RigWaveformPeakTests() : juce::UnitTest ("Rig waveform peaks", "Rig") {}

    // Each slice holds every bin it touches, and a bin is the loudest of its 2^shift samples, so
    // a slice reads at most a bin less one sample more at each end: never less than the loudest
    // sample in it, never more than the loudest within that slack.
    void expectPeaks (RigLooper& looper, int layer, const juce::AudioBuffer<float>& ref, int n, const juce::String& what)
    {
        for (const int count : { 1, 400, 2000 })
            expectEquals (outside (looper.readPeaks (layer, count), ref, n, 0, peakBin (n) - 1, false), 0,
                          what + ", " + juce::String (n) + " samples, " + juce::String (count) + " slices");
    }

    void runTest() override
    {
        beginTest ("A sound put in, swapped in or replaced: each slice is its loudest sample, to a bin");
        {
            PeakRig rig;
            rig.clock.playing = true;
            // Shorter than the table (a bin a sample) and than 400 or 2000 slices, odd lengths, 8 bars
            // at 120, and 10 minutes.
            for (const int n : { 100, 5000, 100003, 768000, (int) (600 * kRate) })
            {
                {
                    const auto a = testSound (n, 1);
                    expect (rig.looper.setLayerAudio (0, a, 4.0) > 0);
                    expectPeaks (rig.looper, 0, a, n, "setLayerAudio");
                }
                {
                    // A bank swap at the next beat, done by the audio thread.
                    const auto b = testSound (n, 2);
                    rig.looper.queueLayerAudio (0, b, 4.0, rig.clock.beat + 0.5, 0.8f, false);
                    rig.blocks ((int) (0.3 * kRate / kBlock));
                    expect (! rig.looper.isSwapPending (0));
                    expectPeaks (rig.looper, 0, b, n, "a bank swap");
                }
                {
                    const auto c = testSound (n, 3);
                    expect (rig.looper.replaceLayerAudio (0, c));
                    expectPeaks (rig.looper, 0, c, n, "replaceLayerAudio");
                }
                {
                    const auto d = testSound (n, 4);
                    rig.looper.queueLayerAudio (0, d, 4.0, -1.0, 0.8f, false);   // right away, on this thread
                    expectPeaks (rig.looper, 0, d, n, "a swap right away");
                }
                rig.looper.maintain();   // frees the swapped-out sound
            }
            rig.looper.reset (0);
        }

        beginTest ("Recording: the waveform is what's recorded so far, through the table doubling, then the loop");
        {
            PeakRig rig;
            constexpr int take = 768000;   // 8 bars at 120
            constexpr int delay = 100;     // the live delay, samples
            const auto spiky = testSound (take + 48000, 7);
            const auto flat = testSound (take + 48000, 8, false);
            rig.fill = [&] (juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>& in, juce::int64 fed)
            {
                copySource (spiky, mix, fed);
                copySource (flat, in, fed);
            };
            rig.looper.setLiveDelay (delay);
            rig.looper.getLayer (0).source = RigLooper::mix;
            rig.looper.getLayer (1).source = RigLooper::input;   // a live source: turned by the delay as it closes
            for (int l = 0; l < 2; ++l)
            {
                rig.looper.getLayer (l).bars = 8;
                rig.looper.pressRecord (l);
            }
            rig.blocks (1);   // armed; the clock starts
            int checks = 0, wrongLength = 0, wrong = 0;
            while (rig.looper.getState (0) == RigLooper::State::recording || rig.looper.getState (0) == RigLooper::State::armed)
            {
                rig.blocks (50);
                if (rig.looper.getState (0) != RigLooper::State::recording)
                    break;
                // Recording starts with the clock, so what's recorded is everything fed since.
                const int n = (int) rig.fed;
                wrongLength += (int) std::lround (rig.looper.recordedSeconds (0) * kRate) == n ? 0 : 1;
                for (const int count : { 400, 2000 })
                    wrong += outside (rig.looper.readPeaks (0, count), spiky, n, 0, peakBin (n) - 1, false);
                ++checks;
            }
            logMessage ("  " + juce::String (checks) + " looks while it recorded, the table doubled "
                        + juce::String (std::log2 (peakBin (take))) + " times");
            expectGreaterThan (checks, 50);
            expectGreaterOrEqual (peakBin (take), 32, "it doubled several times");
            expectEquals (wrongLength, 0);
            expectEquals (wrong, 0, "the slices of what's recorded so far");

            expect (rig.looper.getState (0) == RigLooper::State::playing && rig.looper.getState (1) == RigLooper::State::playing);
            const auto loop0 = rig.looper.copyLayerAudio (0), loop1 = rig.looper.copyLayerAudio (1);
            const int n0 = loop0.getNumSamples(), n1 = loop1.getNumSamples();
            expect (std::abs (n0 - take) <= 1 && n1 == n0, juce::String (n0));
            // Not turned: exactly what was played (the fades only make the loop quieter than it).
            expectPeaks (rig.looper, 0, spiky, n0, "a closed take");
            for (const int count : { 400, 2000 })
                expectEquals (under (rig.looper.readPeaks (0, count), loop0, n0), 0, "never under the loop as it is");
            // Turned `delay` earlier as it closed: the waveform is of the loop `delay` on (round it),
            // to a bin, and the 96-sample fades (the sound is flat for 3000 samples, so within that).
            for (const int count : { 400, 2000 })
                expectEquals (outside (rig.looper.readPeaks (1, count), loop1, n1, -delay, peakBin (n1) - 1 + 96, true), 0,
                              "a live take, " + juce::String (count) + " slices");
        }

        beginTest ("An overdub raises the waveform; undo puts back exactly what it was");
        {
            PeakRig rig;
            constexpr int n = 768000;
            const auto quiet = testSound (n, 5);
            rig.looper.setLayerAudio (0, quiet, 32.0);
            rig.looper.getLayer (0).source = RigLooper::input;
            rig.fill = [] (juce::AudioBuffer<float>&, juce::AudioBuffer<float>& in, juce::int64 fed)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < kBlock; ++i)
                        in.setSample (ch, i, ((fed + i) / 1000) % 2 == 0 ? 0.3f : -0.2f);
            };
            rig.clock.playing = true;
            const auto before = rig.looper.readPeaks (0, 2000), before400 = rig.looper.readPeaks (0, 400);
            rig.looper.pressRecord (0);
            rig.blocks (1);
            expect (rig.looper.getState (0) == RigLooper::State::overdubbing);
            rig.seconds (8.0);   // half the loop
            rig.looper.pressRecord (0);
            rig.blocks (1);
            expect (rig.looper.canUndo (0));
            const auto after = rig.looper.readPeaks (0, 2000);
            int lower = 0, raised = 0;
            for (size_t p = 0; p < after.size(); ++p)
            {
                lower += after[p] < before[p] ? 1 : 0;
                raised += after[p] > before[p] ? 1 : 0;
            }
            logMessage ("  " + juce::String (raised) + " of 2000 slices louder after the overdub");
            expectEquals (lower, 0, "never lower");
            expectGreaterThan (raised, 600, "about half the loop was overdubbed");
            // Each bin is the loudest of what was there and what the overdub made of it.
            const auto dubbed = rig.looper.copyLayerAudio (0);
            juce::AudioBuffer<float> either (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    either.setSample (ch, i, std::abs (quiet.getSample (ch, i)) > std::abs (dubbed.getSample (ch, i)) ? quiet.getSample (ch, i) : dubbed.getSample (ch, i));
            for (const int count : { 400, 2000 })
            {
                const auto peaks = rig.looper.readPeaks (0, count);
                expectEquals (outside (peaks, either, n, 0, peakBin (n) - 1, false), 0, "within the loudest of before and after");
                expectEquals (under (peaks, dubbed, n), 0, "never under the loop as it is now");
            }
            rig.looper.undo (0);
            rig.blocks (1);
            expect (rig.looper.readPeaks (0, 2000) == before, "undo: the same waveform as before the overdub, exactly");
            expect (rig.looper.readPeaks (0, 400) == before400);
        }

        beginTest ("Clear, reset and a swap to nothing leave no waveform");
        {
            PeakRig rig;
            rig.clock.playing = true;
            for (int l = 0; l < 4; ++l)
                rig.looper.setLayerAudio (l, testSound (100003, l), 4.0);
            rig.looper.clear (0);
            rig.looper.reset (1);
            rig.looper.queueLayerAudio (2, {}, 0.0, -1.0, 0.8f, false);
            rig.looper.queueLayerAudio (3, {}, 0.0, rig.clock.beat + 0.25, 0.8f, false);
            rig.blocks ((int) (0.3 * kRate / kBlock));
            for (int l = 0; l < 4; ++l)
            {
                expect (rig.looper.getState (l) == RigLooper::State::empty);
                expect (allZero (rig.looper.readPeaks (l, 400)) && allZero (rig.looper.readPeaks (l, 1)), "layer " + juce::String (l + 1));
            }
            // A new take there starts from nothing: silence draws as silence.
            rig.looper.getLayer (0).source = RigLooper::input;
            rig.looper.pressRecord (0);
            expect (rig.until (0, RigLooper::State::recording, 3.0));
            rig.seconds (0.5);
            expect (allZero (rig.looper.readPeaks (0, 400)), "a silent take");
        }

        beginTest ("Reading the waveform while the audio thread records, overdubs, undoes, swaps and clears");
        {
            // Run under ThreadSanitizer for the real check: every read is of atomics only.
            RigLooper looper;
            looper.prepare (kRate, kBlock);
            constexpr double bpm = 480.0;   // a bar is 24000 samples: a 2-bar take doubles the table twice
            looper.getLayer (0).source = RigLooper::input;
            std::atomic<bool> running { true };
            std::atomic<double> beatNow { 0 };
            std::atomic<int> cycles { 0 }, reads { 0 }, bad { 0 };
            std::thread audio ([&]
            {
                RigLooper::Clock clock;
                clock.bpm = bpm;
                clock.playing = true;
                juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
                juce::Random random (11);
                std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
                sources[RigLooper::input] = &in;
                for (int block = 0; running.load(); ++block)
                {
                    switch (block % 500)
                    {
                        case 0:   looper.getLayer (0).bars = 2; looper.pressRecord (0); break;   // record 2 bars, closes itself
                        case 300: looper.pressRecord (0); break;   // overdub
                        case 360: looper.pressRecord (0); break;
                        case 400: looper.undo (0); break;
                        case 450: looper.clear (0); ++cycles; break;
                        default: break;
                    }
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlock; ++i)
                            in.setSample (ch, i, random.nextFloat() * 1.6f - 0.8f);
                    looper.process (clock, sources, out);
                    clock.beat += kBlock * bpm / (60.0 * kRate);
                    beatNow = clock.beat;
                }
            });
            std::thread reader ([&]
            {
                for (int k = 0; running.load(); ++k)
                    for (int l = 0; l < 4; ++l)
                    {
                        for (const float v : looper.readPeaks (l, k % 2 == 0 ? 400 : 2000))
                            if (! std::isfinite (v) || v < 0.0f || v > 1.0f)
                                ++bad;
                        ++reads;
                    }
            });
            const auto started = juce::Time::getMillisecondCounter();
            for (int k = 0; juce::Time::getMillisecondCounter() - started < 3000; ++k)
            {
                // Bank swaps (at the next beat, or now; to a sound or to nothing), replacements, new sounds.
                looper.queueLayerAudio (1, k % 3 == 2 ? juce::AudioBuffer<float>() : testSound (24000 + 1000 * (k % 5), k), 4.0,
                                        k % 2 == 0 ? beatNow.load() + 1.0 : -1.0, 0.8f, false);
                if (k == 0)
                    looper.setLayerAudio (2, testSound (24000, 99), 4.0);
                else
                    looper.replaceLayerAudio (2, testSound (24000, 100 + k));
                if (k % 2 == 0)
                    looper.setLayerAudio (3, testSound (12000 + 7 * k, 200 + k), 2.0);
                else
                    looper.reset (3);
                juce::Thread::sleep (5);
            }
            running = false;
            audio.join();
            reader.join();
            logMessage ("  " + juce::String (reads.load()) + " reads over " + juce::String (cycles.load())
                        + " record / overdub / undo / clear rounds");
            expectEquals (bad.load(), 0, "every value finite and between 0 and 1");
            expectGreaterThan (reads.load(), 100);
            expectGreaterThan (cycles.load(), 1);
        }

        beginTest ("The waveform's work in a block doesn't grow with the loop: 32 seconds and 10 minutes");
        {
            std::array<int, 2> worst {};
            for (int which = 0; which < 2; ++which)
            {
                const double seconds = which == 0 ? 32.0 : 596.0;   // (ends at its bar: under kMaxUndoSeconds)
                PeakRig rig;
                juce::Random random (3);
                rig.fill = [&] (juce::AudioBuffer<float>&, juce::AudioBuffer<float>& in, juce::int64)
                {
                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < kBlock; ++i)
                            in.setSample (ch, i, random.nextFloat() * 0.5f - 0.25f);
                };
                auto& looper = rig.looper;
                looper.getLayer (0).source = RigLooper::input;
                looper.pressRecord (0);
                expect (rig.until (0, RigLooper::State::recording, 1.0));
                for (int k = 0; k < (int) (seconds * kRate / kBlock); k += 100)
                {
                    rig.blocks (100);
                    looper.maintain();   // its room grows as it records
                }
                looper.pressRecord (0);   // closes at the end of the bar
                expect (rig.until (0, RigLooper::State::playing, 3.0));
                const int taken = (int) std::lround (looper.getLayer (0).lengthBeats.load() * 60.0 / rig.clock.bpm * kRate);
                looper.maintain();        // room to undo an overdub of it
                looper.pressRecord (0);   // overdub
                rig.seconds (0.5);
                looper.pressRecord (0);
                rig.blocks (2);
                expect (looper.canUndo (0), "undo room for it");
                looper.undo (0);
                rig.blocks (2);
                looper.queueLayerAudio (0, looper.copyLayerAudio (0), looper.getLayer (0).lengthBeats.load(), rig.clock.beat + 0.5, 0.8f, false);
                rig.seconds (0.5);   // swapped in at the beat
                expect (! looper.isSwapPending (0));
                looper.clear (0);
                rig.blocks (2);
                worst[(size_t) which] = rig.worstOps;
                logMessage ("  " + juce::String (seconds) + " s take (" + juce::String (std::log2 (peakBin (taken)))
                            + " doublings): at most " + juce::String (rig.worstOps) + " bins touched in a block");
                expectLessOrEqual (rig.worstOps, RigLooper::kMaxPeakOpsPerBlock);
            }
            expectLessOrEqual (worst[1], worst[0] + kBlock, "no more for 10 minutes than for 32 seconds");
        }

        beginTest ("Reading the waveforms all the time never drops a loop out");
        {
            PlayingLooper rig;
            int reads = 0;
            const int missing = rig.audio.during ([&]
            {
                const auto started = juce::Time::getMillisecondCounter();
                while (juce::Time::getMillisecondCounter() - started < 1500)
                    for (int l = 0; l < kTones; ++l, ++reads)
                        rig.looper.readPeaks (l, 2000);
            });
            logMessage ("  " + juce::String (reads) + " reads, " + juce::String (rig.audio.blocks.load()) + " blocks, "
                        + juce::String (missing) + " missing");
            expectEquals (missing, 0);
            expectGreaterThan (reads, 100);
        }
    }
};

static RigWaveformPeakTests rigWaveformPeakTests;
