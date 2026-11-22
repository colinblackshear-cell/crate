#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigExport.h"
#include "RigLooper.h"

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
            if (juce::Time::getMillisecondCounterHiRes() - t0 > period)
                ++late;
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
            const auto wait = next - juce::Time::getMillisecondCounterHiRes();
            if (wait > 1.0)
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

// While a pause in the copy is set, the audio thread is known to run at least `ms` during it.
struct CopyPause
{
    CopyPause (int ms, bool underLockToo = false, std::function<void (int)> during = {})
    {
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
            expectEquals (after.withoutLock - before.withoutLock, 1);
            expectEquals (after.underLock - before.underLock, 0);
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
            const CopyPause pause (250);   // the overdub writes ~250 ms more while it's copied
            juce::AudioBuffer<float> copy;
            const auto before = rig.looper.getCopyStats();
            const int missing = rig.audio.during ([&] { copy = rig.looper.copyLayerAudio (0); });
            const auto after = rig.looper.getCopyStats();
            int written = 0;
            bool exact = false;
            const int runs = writtenRuns (original, copy, written, exact);
            logMessage ("  " + juce::String (missing) + " blocks missing; the copy has " + juce::String (written) + " samples overdubbed in "
                        + juce::String (runs) + " run(s); " + juce::String (after.pagesCopiedAgain - before.pagesCopiedAgain)
                        + " pages copied again by the audio thread");
            expectEquals (missing, 0);
            expect (exact, "every sample is either the loop or the loop plus one pass of the overdub");
            expectEquals (runs, 1, "one moment: what was overdubbed is one unbroken stretch");
            expectGreaterThan (written, (int) (0.5 * kRate), "it includes what was written during the copy");
            expectGreaterThan (after.pagesCopiedAgain - before.pagesCopiedAgain, 5);
            expectEquals (after.underLock - before.underLock, 0);
            rig.looper.pressRecord (0);
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

        beginTest ("An overdub that writes too much while it's copied: a few tries, then under the lock, still exact");
        {
            PlayingLooper rig (1, 16.0);
            const auto original = tone (0, 16.0);
            rig.looper.getLayer (0).source = RigLooper::input;
            rig.looper.pressRecord (0);
            juce::Thread::sleep (100);
            // 1.6 s of overdub per try is more than kCopyMaxPages pages.
            const CopyPause pause (1600);
            juce::AudioBuffer<float> copy;
            const auto before = rig.looper.getCopyStats();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            copy = rig.looper.copyLayerAudio (0);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto after = rig.looper.getCopyStats();
            int written = 0;
            bool exact = false;
            const int runs = writtenRuns (original, copy, written, exact);
            expectEquals (after.retries - before.retries, RigLooper::kCopyRounds, "it tries kCopyRounds times");
            expectEquals (after.underLock - before.underLock, 1, "then copies under the lock");
            expect (exact && runs == 1, "still one moment of the loop");
            expectLessThan (ms, RigLooper::kCopyRounds * 1600.0 + 1000.0, "and it doesn't keep trying");
            rig.looper.pressRecord (0);
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
