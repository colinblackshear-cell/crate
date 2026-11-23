// The dirty model's parts: what pending means, how loop audio is named and hashed (and never
// copied on the message thread), what a save may move the baseline to, how often it checks, and
// the audio thread's side of it. Benchmarks at the end, with KB_DIRTY_BENCH=1.

#include "RigDirtyTestHelpers.h"

#include <thread>

using namespace dirtytest;

namespace
{
using State = RigDirty::State;

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

double percentile (std::vector<double> v, double p)
{
    if (v.empty())
        return 0;
    std::sort (v.begin(), v.end());
    return v[juce::jlimit<size_t> (0, v.size() - 1, (size_t) (p * (double) v.size()))];
}

// The engine's callback in real time on its own thread (a block every 256 samples' worth),
// timing each one.
struct LiveAudio
{
    RigEngine& engine;
    int block = 256;
    std::atomic<bool> running { false }, measuring { false };
    std::atomic<int> blocks { 0 };
    std::vector<double> times;   // (written by the audio thread only while measuring; read after)
    std::atomic<int> count { 0 };
    std::thread thread;

    explicit LiveAudio (RigEngine& e, int b = 256) : engine (e), block (b) { times.resize (200000); }
    ~LiveAudio() { stop(); }
    void start()
    {
        running = true;
        thread = std::thread ([this]
        {
            juce::AudioBuffer<float> out (2, block);
            const double period = 1000.0 * block / kRate;
            auto next = nowMs();
            while (running.load())
            {
                const auto t0 = nowMs();
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, block);
                const auto took = nowMs() - t0;
                if (measuring.load())
                    if (const int n = count.load(); n < (int) times.size())
                    {
                        times[(size_t) n] = took;
                        count.store (n + 1);
                    }
                blocks.fetch_add (1);
                next += period;
                if (const auto wait = next - nowMs(); wait > 0)
                    juce::Thread::sleep ((int) wait);
                else if (wait < -100)
                    next = nowMs();
            }
        });
        const int b0 = blocks.load();
        while (blocks.load() < b0 + 4)
            juce::Thread::sleep (2);
    }
    void stop()
    {
        running = false;
        if (thread.joinable())
            thread.join();
    }
    void waitBlocks (int n)
    {
        const int b0 = blocks.load();
        while (blocks.load() < b0 + n)
            juce::Thread::sleep (2);
    }
    // Times the callback while `f` runs.
    std::vector<double> during (const std::function<void()>& f)
    {
        count = 0;
        measuring = true;
        f();
        measuring = false;
        juce::Thread::sleep (20);
        return std::vector<double> (times.begin(), times.begin() + count.load());
    }
};

// The message thread's timer, for a while: services the model (and what the engine's timers
// do) every 10 ms, the way the app's timer does.
void serviceFor (RigEngine& e, double ms, const std::function<bool()>& until = {})
{
    const auto end = nowMs() + ms;
    while (nowMs() < end)
    {
        e.serviceLoopBanks();
        e.serviceTakes();
        e.getAutomation().service();
        e.getDirty().service();
        if (until && until())
            return;
        juce::Thread::sleep (10);
    }
}

std::unique_ptr<RigEngine> makeShow (const juce::File& file, int loopSeconds, bool takes = false)
{
    auto e = std::make_unique<RigEngine>();
    e->prepare (kRate, kBlock);
    buildRich (*e, loopSeconds);
    addNoteClip (*e);
    if (takes)
        addTakes (*e);
    Runner r { *e };
    r.seconds (0.05);
    e->saveShowFile (file);
    return e;
}

// The loop-copy tests' detector: each loop a sine at its own frequency, a whole number of cycles
// a 256-sample block, checked every block (Goertzel). A block where a loop drops below half
// its level is a dropout you'd hear.
namespace tones
{
constexpr int kBlock = 256;
constexpr int kBins[] = { 6, 11, 17, 23 };
constexpr int kTones = 4;
constexpr double kSamplesPerBeat = 32768.0;
constexpr double kBpm = kRate * 60.0 / kSamplesPerBeat;

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

juce::AudioBuffer<float> tone (int t, double beats, float level = 0.1f)
{
    const int n = (int) std::round (beats * kSamplesPerBeat);
    juce::AudioBuffer<float> b (2, n);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            b.setSample (ch, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * kBins[t] * (i % kBlock) / kBlock));
    return b;
}

struct Detector
{
    RigEngine& engine;
    std::atomic<bool> running { false }, warm { false }, measuring { false };
    std::atomic<int> blocks { 0 }, late { 0 }, missing { 0 };
    std::array<float, kTones> reference {};
    std::thread thread;

    explicit Detector (RigEngine& e) : engine (e) {}
    ~Detector() { stop(); }
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
    void during (const std::function<void()>& f)
    {
        missing = 0;
        late = 0;
        blocks = 0;
        measuring = true;
        f();
        juce::Thread::sleep (60);
        measuring = false;
    }

private:
    void run()
    {
        juce::AudioBuffer<float> out (2, kBlock);
        std::array<double, kTones> sum {};
        int count = 0;
        const double period = 1000.0 * kBlock / kRate;
        auto next = nowMs();
        while (running)
        {
            const auto t0 = nowMs();
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            const auto took = nowMs() - t0;
            if (measuring && took > period)
                ++late;
            bool gone = false;
            for (int t = 0; t < kTones; ++t)
            {
                const float m = goertzel (out.getReadPointer (0), kBlock, kBins[t]);
                if (count < 100)
                    sum[(size_t) t] += m;
                else if (count == 100)
                    reference[(size_t) t] = (float) (sum[(size_t) t] / 100.0);
                else if (measuring && m < 0.5f * reference[(size_t) t])
                    gone = true;
            }
            if (gone)
                ++missing;
            if (count == 101)
                warm = true;
            if (measuring)
                ++blocks;
            ++count;
            next += period;
            const auto wait = next - nowMs();
            if (wait > 1.0)
                juce::Thread::sleep ((int) wait);
        }
    }
};
} // namespace tones
} // namespace

struct RigDirtyTests : juce::UnitTest
{
    RigDirtyTests() : juce::UnitTest ("Rig dirty state", "Rig") {}

    juce::File folder, show, withTakes;

    void expectState (RigEngine& e, State want, const juce::String& what, int timeoutMs = 20000)
    {
        const auto s = settle (e, timeoutMs);
        expect (s.state == want, what + ": " + stateName (s.state) + " (want " + stateName (want) + ")");
        if (s.state != want)
            for (auto& d : e.getDirty().differences (8))
                logMessage ("    " + d);
    }

    void runTest() override
    {
        folder = tempFolder ("kb-dirty-state");
        show = folder.getChildFile ("Show.crateshow");
        withTakes = folder.getChildFile ("Takes.crateshow");
        makeShow (show, 2);
        makeShow (withTakes, 2, true);

        disabled();
        pending();
        busyLoops();
        staleResult();
        pinnedBanks();
        takeIds();
        coldOpen();
        readOnly();
        saves();
        debounce();
        audioThread();
        sceneRetries();
        savesFirst();
        {
            beginTest ("Tools whose settings can't be put back");
            auto e = openSaved (show);
            const auto list = e->getDirty().unmatchedTools();
            logMessage ("  not normalised: " + (list.isEmpty() ? juce::String ("(none)") : list.joinIntoString ("; ")));
            expect (true);
        }
        if (juce::SystemStats::getEnvironmentVariable ("KB_DIRTY_BENCH", {}) == "1")
            bench();
        folder.deleteRecursively();
    }

    //==========================================================================
    void disabled()
    {
        beginTest ("Off: reports pending, does nothing");
        RigEngine e;
        e.prepare (kRate, kBlock);
        if (auto xml = juce::parseXML (show))
            e.loadShow (juce::ValueTree::fromXml (*xml));
        e.readLoopAudio (show);
        auto& d = e.getDirty();
        expect (! d.isEnabled());
        Runner r { e };
        e.getTransport().play();
        r.beats (8);
        hand (e.getBass(), "cutoff", 1200.0f);
        d.noteChange();
        for (int i = 0; i < 50; ++i)
            d.service();
        expect (d.getState() == State::pending);
        expect (d.checkNow().state == State::pending);
        expect (! d.waitUntilResolved (50));
        const auto s = d.getStats();
        expectEquals (s.checks, 0);
        expectEquals (s.hashJobsPosted, 0);
        expectEquals (s.layerCopies, 0);
    }

    void pending()
    {
        {
            beginTest ("Pending: no baseline yet");
            RigEngine e;
            e.prepare (kRate, kBlock);
            e.getDirty().setEnabled (true);
            if (auto xml = juce::parseXML (show))
                e.loadShow (juce::ValueTree::fromXml (*xml));
            expect (e.getDirty().checkNow().state == State::pending, "loaded, not opened");
            expect (e.getDirty().getState() == State::pending);
            expect (! e.getDirty().waitUntilResolved (100));
            e.readLoopAudio (show);
            expectState (e, State::clean, "opened");
        }
        {
            beginTest ("Pending: a change not looked at yet");
            auto e = openSaved (show);
            expectState (*e, State::clean, "opened");
            expect (e->getDirty().getState() == State::clean);
            hand (e->getBass(), "cutoff", 1200.0f);
            expect (e->getDirty().getState() == State::pending, "straight after the edit");
            e->getDirty().noteChange();
            expect (e->getDirty().getState() == State::pending, "a bare note");
            expectState (*e, State::dirty, "looked at");
            expect (e->getDirty().getState() == State::dirty);
        }
        {
            beginTest ("Pending: a sound not hashed yet");
            auto e = openSaved (show);
            expectState (*e, State::clean, "opened");
            const auto same = e->getLooper().copyLayerAudio (0);
            e->getLooper().setLayerAudio (0, same, e->getLooper().getLayer (0).lengthBeats.load());
            const auto s = e->getDirty().checkNow();
            expect (s.state == State::pending, "the same sound again, before its hash: " + stateName (s.state));
            expect (s.unresolvedAudio > 0);
            expect (e->getDirty().getState() == State::pending);
            expectState (*e, State::clean, "hashed");
            e->getLooper().setLayerAudio (0, noise ((int) kRate * 2, 77), 4.0);
            expect (e->getDirty().checkNow().state == State::pending, "a new sound, before its hash");
            expectState (*e, State::dirty, "hashed");
        }
        {
            beginTest ("Pending: a bank on its way in");
            auto e = openSaved (show);
            Runner r { *e };
            e->getTransport().play();
            r.beats (1);
            expectState (*e, State::clean, "playing");
            e->switchLoopBank (1);
            const bool moving = e->getQueuedLoopBank() >= 0 || e->getLooper().isSwapPending (0) || e->getLooper().isSwapPending (1);
            expect (moving, "the switch waits for the audio thread");
            if (moving)
                expect (e->getDirty().checkNow().state == State::pending, "mid switch");
            r.beats (8);
            e->serviceLoopBanks();
            expectState (*e, State::dirty, "bank B by hand, in");
            e->switchLoopBank (0);
            r.beats (8);
            e->serviceLoopBanks();
            e->getTransport().stop();
            r.seconds (0.05);
            expectState (*e, State::clean, "back to bank A");
        }
    }

    void busyLoops()
    {
        beginTest ("A loop overdubbing: dirty for now, no copies; hashed once it stops");
        auto e = openSaved (show);
        Runner r { *e };
        e->getTransport().play();
        r.beats (3.9);
        expectState (*e, State::clean, "playing");
        e->getDirty().resetStats();
        const auto copies0 = e->getLooper().getCopyStats();
        r.input = true;
        e->getLooper().pressRecord (1);
        r.beats (0.5);
        int busy = 0, provisional = 0;
        for (int i = 0; i < 20; ++i)
        {
            r.beats (0.2);
            const auto st = e->getLooper().getState (1);
            if (st != RigLooper::State::overdubbing && st != RigLooper::State::recording)
                continue;
            ++busy;
            const auto s = e->getDirty().checkNow();
            provisional += s.state == State::dirty && s.provisional ? 1 : 0;
        }
        expect (busy > 10, "it overdubbed");
        expectEquals (provisional, busy, "every check while it overdubs: dirty, for now");
        auto stats = e->getDirty().getStats();
        expectEquals (stats.hashJobsPosted, 0, "no hashing while it overdubs");
        expectEquals (stats.layerCopies, 0, "no copies while it overdubs");
        const auto copies1 = e->getLooper().getCopyStats();
        expectEquals (copies1.byAudioThread + copies1.underLock, copies0.byAudioThread + copies0.underLock, "the looper copied nothing");
        e->getLooper().pressRecord (1);
        r.input = false;
        r.beats (2);
        // (the copy is made by the audio thread, so keep playing while the hash comes)
        for (int i = 0; i < 400 && e->getDirty().checkNow().state == State::pending; ++i)
            r.beats (0.25);
        const auto s = e->getDirty().checkNow();
        expect (s.state == State::dirty && ! s.provisional, "stopped: dirty for real: " + stateName (s.state));
        stats = e->getDirty().getStats();
        expectEquals (stats.layerCopies, 1, "loop 2 copied once");
        expectEquals (stats.hashJobsPosted, 1, "and hashed once");
    }

    void staleResult()
    {
        beginTest ("A hash of a sound that changed meanwhile is dropped");
        auto e = openSaved (show);
        expectState (*e, State::clean, "opened");
        e->getLooper().setLayerAudio (0, noise ((int) kRate * 2, 31), 4.0);
        e->getDirty().resetStats();
        juce::WaitableEvent entered, go;
        std::atomic<bool> fired { false };
        RigLooper::copyPauseForTests = [&] (int layer, bool underLock)
        {
            if (layer == 0 && underLock && ! fired.exchange (true))
            {
                entered.signal();
                go.wait (10000);
            }
        };
        expect (e->getDirty().checkNow().state == State::pending, "loop 1's hash on its way");
        expect (entered.wait (10000), "the copy started");
        // Loop 1 changes while its copy is held: the change waits for the lock, then the copy's hash arrives.
        std::thread changer ([&] { e->getLooper().setLayerAudio (0, noise ((int) kRate * 2, 32), 4.0); });
        go.signal();
        changer.join();
        for (int i = 0; i < 500 && e->getDirty().getStats().hashJobsCompleted < 1; ++i)
            juce::Thread::sleep (10);
        RigLooper::copyPauseForTests = nullptr;
        expect (e->getDirty().checkNow().state == State::pending, "the old hash doesn't count for the new sound");
        expectEquals (e->getDirty().getStats().staleResults, 1, "dropped");
        expectState (*e, State::dirty, "hashed again");
        expectEquals (e->getDirty().getStats().hashJobsPosted, 2);
    }

    void pinnedBanks()
    {
        beginTest ("A bank's sound: the cache holds it (no reused address), and lets go");
        auto e = openSaved (show);
        Runner r { *e };
        expectState (*e, State::clean, "opened");
        const auto original = *e->peekLoopBanks()[1].layers[0].audio;
        int wrongClean = 0;
        for (int k = 0; k < 20; ++k)
        {
            e->switchLoopBank (1);
            e->serviceLoopBanks();
            e->getLooper().setLayerAudio (0, noise ((int) kRate * 2, 300 + k), 4.0);
            e->switchLoopBank (0);
            e->serviceLoopBanks();
            r.seconds (0.02);
            wrongClean += settle (*e).state == State::clean ? 1 : 0;
        }
        expectEquals (wrongClean, 0, "every new sound in bank B: dirty");
        const int held = e->getDirty().getStats().pinnedBuffers;
        expect (held <= 2 * RigLooper::kLayers, "the cache keeps only what banks hold: " + juce::String (held));
        e->switchLoopBank (1);
        e->serviceLoopBanks();
        e->getLooper().setLayerAudio (0, original, 4.0);
        e->switchLoopBank (0);
        e->serviceLoopBanks();
        r.seconds (0.02);
        expectState (*e, State::clean, "bank B's first sound back");
        e->deleteLoopBank (1);
        expectEquals (e->getNumLoopBanks(), 1);
        expectState (*e, State::dirty, "bank B deleted");
        std::set<const void*> stored;
        for (const auto& bank : e->peekLoopBanks())
            for (const auto& l : bank.layers)
                if (l.audio != nullptr)
                    stored.insert (l.audio.get());
        expect (e->getDirty().getStats().pinnedBuffers <= (int) stored.size(), "bank B's sounds let go: the cache holds "
                + juce::String (e->getDirty().getStats().pinnedBuffers) + ", the banks " + juce::String ((int) stored.size()));
    }

    void takeIds()
    {
        beginTest ("Takes: ids never repeat, and a new take's hash comes from its loop");
        {
            auto a = openSaved (withTakes);
            auto b = openSaved (withTakes);
            std::set<juce::uint64> ids;
            int n = 0;
            for (auto* e : { a.get(), b.get() })
                for (const auto& set : e->getTakes().getSets())
                    for (const auto& take : set.takes)
                        ids.insert (take.id), ++n;
            expect (n >= 4);
            expectEquals ((int) ids.size(), n, "every take its own id");
            expectState (*a, State::clean, "opened");
            expectEquals (a->getDirty().getStats().takeFallbacks, 0);
        }
        auto e = openSaved (show);
        Runner r { *e };
        expectState (*e, State::clean, "opened");
        e->getDirty().resetStats();
        r.input = true;
        juce::String why;
        expect (e->recordInto (1, 6, RigLooper::input, false, &why), why);
        r.beats (12);
        r.input = false;
        e->getTransport().stop();
        r.seconds (0.2);
        const auto* set = e->getTakes().setFor (6);
        expect (set != nullptr && ! set->takes.empty(), "a take");
        for (int i = 0; i < 400 && e->getDirty().checkNow().state == State::pending; ++i)
            r.seconds (0.05);
        expectState (*e, State::dirty, "a take recorded");
        const auto s = e->getDirty().getStats();
        expectEquals (s.takeFallbacks, 0, "its hash came from loop 7's");
        expect (s.seedsUsed > 0);
    }

    void coldOpen()
    {
        beginTest ("Opening a heavy show: clean with no hashing at all");
        const auto heavy = folder.getChildFile ("Heavy.crateshow");
        {
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, kBlock);
            buildRich (*e, 2);
            addTakes (*e);
            for (int b = 0; b < 3; ++b)
            {
                if (b > 0)
                {
                    e->newLoopBank ("Heavy " + juce::String (b));
                    e->switchLoopBank (b);
                    e->serviceLoopBanks();
                }
                for (int i = 0; i < 4; ++i)
                    e->getLooper().setLayerAudio (i, noise ((int) kRate * 20, 50 * b + i), 40.0);
            }
            e->switchLoopBank (0);
            e->serviceLoopBanks();
            expect (e->saveShowFile (heavy).ok);
        }
        auto e = openSaved (heavy);
        expectState (*e, State::clean, "opened");
        const auto s = e->getDirty().getStats();
        expectEquals (s.hashJobsPosted, 0, "nothing hashed");
        expectEquals (s.layerCopies, 0, "nothing copied");
        int sounds = 0;
        for (int i = 0; i < RigLooper::kLayers; ++i)
            sounds += e->getLooper().getState (i) == RigLooper::State::playing ? 1 : 0;
        for (int b = 0; b < (int) e->peekLoopBanks().size(); ++b)
            if (b != e->getCurrentLoopBank())
                for (const auto& l : e->peekLoopBanks()[(size_t) b].layers)
                    sounds += l.audio != nullptr && l.beats > 0 && l.audio->getNumSamples() > 0 ? 1 : 0;
        for (const auto& set : e->getTakes().getSets())
            for (const auto& take : set.takes)
                sounds += take.audio.getNumSamples() > 0 ? 1 : 0;
        expect (sounds >= 10, juce::String (sounds) + " sounds");
        expectEquals (s.seedsUsed, sounds, "every sound named by what the file said");
    }

    void readOnly()
    {
        beginTest ("Looking at the banks changes nothing, and copies only off the message thread");
        auto e = openSaved (show);
        expectState (*e, State::clean, "opened");
        struct Seen { const void* audio; int version; };
        auto look = [&e]
        {
            std::vector<Seen> v;
            for (const auto& bank : e->peekLoopBanks())
                for (const auto& l : bank.layers)
                    v.push_back ({ l.audio.get(), l.version });
            return v;
        };
        const auto before = look();
        const auto copies0 = e->getLooper().getCopyStats();
        const auto self = juce::Thread::getCurrentThreadId();
        std::atomic<int> onMessageThread { 0 }, elsewhere { 0 };
        RigLooper::copyPauseForTests = [&] (int, bool)
        {
            (juce::Thread::getCurrentThreadId() == self ? onMessageThread : elsewhere).fetch_add (1);
        };
        for (int i = 0; i < 10; ++i)
            e->getDirty().checkNow();
        e->getLooper().setLayerAudio (1, noise ((int) kRate * 2, 41), 4.0);
        expectState (*e, State::dirty, "loop 2 new");
        RigLooper::copyPauseForTests = nullptr;
        const auto after = look();
        bool same = before.size() == after.size();
        for (size_t i = 0; same && i < before.size(); ++i)
            same = before[i].audio == after[i].audio && before[i].version == after[i].version;
        expect (same, "the banks as they were");
        const auto copies1 = e->getLooper().getCopyStats();
        expectEquals (copies1.underLock + copies1.byAudioThread - copies0.underLock - copies0.byAudioThread, 1, "one copy (loop 2's)");
        expectEquals (onMessageThread.load(), 0, "none on the message thread");
        expect (elsewhere.load() >= 1, "on the hashing thread");
    }

    void saves()
    {
        auto edited = [this]
        {
            auto e = openSaved (show);
            expectState (*e, State::clean, "opened");
            hand (e->getBass(), "cutoff", 1500.0f);
            expectState (*e, State::dirty, "edited");
            return e;
        };
        auto failAt = [this, &edited] (const char* point, const juce::String& what)
        {
            beginTest ("A save that fails (" + what + ") keeps the baseline");
            auto e = edited();
            const auto id = settle (*e).baselineId;
            rigshow::hooks().at = [point] (const juce::String& p, int) { return p != point; };
            const auto r = e->saveShowFile (folder.getChildFile ("Failed.crateshow"), rigshow::SaveKind::document);
            rigshow::hooks().at = nullptr;
            expect (! r.ok, "it failed");
            const auto s = settle (*e);
            expect (s.state == State::dirty, "still dirty");
            expect (s.baselineId == id, "the same baseline");
        };
        failAt ("commit", "writing the show");
        failAt ("stage", "stopped before the loops");
        {
            beginTest ("A copy's save (the last show, the host's) keeps the baseline");
            auto e = edited();
            const auto id = settle (*e).baselineId;
            expect (e->saveShowFile (folder.getChildFile ("Copy.crateshow")).ok);
            juce::ValueTree host;
            rigshow::Result result;
            host = e->saveShowForHost (folder.getChildFile ("Host.crateshow"), result);
            expect (result.ok);
            const auto s = settle (*e);
            expect (s.state == State::dirty && s.baselineId == id);
        }
        {
            beginTest ("Two saves at once: the older one never moves the baseline");
            auto e = edited();
            auto& d = e->getDirty();
            const auto id = settle (*e).baselineId;
            auto a = d.beginSave();
            auto b = d.beginSave();
            d.beforeTree (a);
            d.captureSave (a, e->saveShow(), {}, {});
            d.finishSave (a, true, rigshow::SaveKind::document);
            auto s = settle (*e);
            expect (s.state == State::dirty && s.baselineId == id, "the older save finished first: not adopted");
            d.finishSave (b, false, rigshow::SaveKind::document);
            s = settle (*e);
            expect (s.state == State::dirty && s.baselineId == id, "the newer one failed");
        }
        {
            beginTest ("A document's save moves the baseline");
            auto e = edited();
            const auto id = settle (*e).baselineId;
            expect (e->saveShowFile (folder.getChildFile ("Saved.crateshow"), rigshow::SaveKind::document).ok);
            const auto s = settle (*e);
            expect (s.state == State::clean, "clean: " + stateName (s.state));
            expect (s.baselineId != id, "a new baseline");
            const auto stats = e->getDirty().getStats();
            expectEquals (stats.hashJobsPosted, 0, "the save's own hashes named every sound");
        }
        {
            beginTest ("An edit made while the save writes stays an edit");
            auto e = openSaved (show);
            expectState (*e, State::clean, "opened");
            auto* bass = &e->getBass();
            rigshow::hooks().at = [bass] (const juce::String& p, int)
            {
                if (p == "beforeCommit")
                    hand (*bass, "cutoff", 2000.0f);
                return true;
            };
            const auto r = e->saveShowFile (folder.getChildFile ("Saved during.crateshow"), rigshow::SaveKind::document);
            rigshow::hooks().at = nullptr;
            expect (r.ok);
            expectState (*e, State::dirty, "after the save");
        }
    }

    void debounce()
    {
        beginTest ("Checks: once after a burst of changes, never when nothing happens");
        auto e = openSaved (show);
        expectState (*e, State::clean, "opened");
        auto& d = e->getDirty();
        d.resetStats();
        for (int i = 0; i < 200; ++i)
        {
            d.service();
            juce::Thread::sleep (2);
        }
        expectEquals (d.getStats().checks, 0, "nothing noted: no checks");
        for (int i = 0; i < 20; ++i)
        {
            hand (e->getBass(), "cutoff", 500.0f + 20.0f * (float) i);
            d.service();
            juce::Thread::sleep (5);
        }
        expectEquals (d.getStats().checks, 0, "not while it keeps changing");
        for (int i = 0; i < 60; ++i)
        {
            d.service();
            juce::Thread::sleep (5);
        }
        expectEquals (d.getStats().checks, 1, "one check after it");
        expect (d.getState() == State::dirty);
        d.resetStats();
        for (int i = 0; i < 400 && d.getStats().checks == 0; ++i)
        {
            d.noteMidi();
            d.service();
            juce::Thread::sleep (10);
        }
        const auto checks = d.getStats().checks;
        expect (checks <= 1, "MIDI coming in all the time: at most one check in 4 s");
    }

    void audioThread()
    {
        beginTest ("The audio thread writing, the message thread checking: never a wrong clean");
        auto e = openSaved (show, 256);
        expectState (*e, State::clean, "opened");
        auto* cutoff = paramOf (e->getBass(), "cutoff");
        const float saved = cutoff->getValue();
        LiveAudio audio (*e);
        e->getTransport().play();
        audio.start();
        int wrong = 0, edits = 0, cleanAfterUndo = 0, resolved = 0;
        juce::Random random (3);
        for (int k = 0; k < 30; ++k)
        {
            const float away = juce::jlimit (0.0f, 1.0f, saved + 0.1f + 0.3f * random.nextFloat());
            cutoff->setValueNotifyingHost (away);
            ++edits;
            for (int i = 0; i < 5; ++i)
            {
                const auto s = e->getDirty().checkNow();
                wrong += s.state == State::clean ? 1 : 0;
                juce::Thread::sleep (random.nextInt (4));
            }
            serviceFor (*e, 20 + random.nextInt (30));
            if (e->getDirty().getState() == State::clean)
                ++wrong;
            cutoff->setValueNotifyingHost (saved);
            if (e->getDirty().waitUntilResolved (3000))
                ++resolved, cleanAfterUndo += e->getDirty().getState() == State::clean ? 1 : 0;
            if (k % 5 == 4)
            {
                handStripMute (*e, RigEngine::bassStrip, true);
                wrong += settle (*e).state == State::clean ? 1 : 0;
                handStripMute (*e, RigEngine::bassStrip, false);
            }
        }
        audio.stop();
        expectEquals (wrong, 0, "a hand edit never read clean");
        logMessage ("  " + juce::String (edits) + " edits; put back and resolved " + juce::String (resolved) + ", of which clean " + juce::String (cleanAfterUndo)
                    + "; field-epoch retries " + juce::String (e->getDirty().getStats().retries));
        e->getTransport().stop();
        Runner r { *e };
        r.seconds (0.05);
        expectState (*e, State::clean, "stopped, everything put back");
    }

    void sceneRetries()
    {
        beginTest ("Song parts changing mutes on the audio thread while the message thread checks: torn checks retried");
        const auto file = folder.getChildFile ("Parts.crateshow");
        {
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, 256);
            e->getTransport().setBpm (240.0);
            e->getLooper().setLayerAudio (0, noise ((int) kRate, 1), 4.0);
            e->getLooper().setLayerAudio (1, noise ((int) kRate, 2), 4.0);
            RigAutomation::Lane lane;
            lane.tool = RigEngine::bassTool;
            lane.parameter = indexOf (e->getBass(), "cutoff");
            lane.paramId = "cutoff";
            lane.lengthBeats = 4;
            lane.points = { { 0.0, 0.0f }, { 4.0, 1.0f } };
            e->getAutomation().addLane (lane);
            // Eight one-bar parts, each flipping the mutes, the vocoder and the input.
            for (int k = 0; k < 8; ++k)
            {
                RigScene part;
                part.name = "Part " + juce::String (k + 1);
                part.bars = 1;
                const int on = k % 2;
                part.state.stripMutes[(size_t) RigEngine::bassStrip] = on;
                part.state.stripMutes[(size_t) RigEngine::synthStrip] = 1 - on;
                part.state.loopMutes[0] = on;
                part.state.loopMutes[1] = 1 - on;
                part.state.vocoderOn = on;
                part.state.inputOn = 1 - on;
                e->getScenes().getScenes().push_back (part);
            }
            e->getScenes().songMode = true;
            expect (e->saveShowFile (file).ok);
        }
        auto e = openSaved (file, 256);
        expectState (*e, State::clean, "opened");
        expectEquals ((int) e->getScenes().getScenes().size(), 8);
        auto* resonance = paramOf (e->getBass(), "resonance");
        const float saved = resonance->getValue();
        e->getDirty().resetStats();
        LiveAudio audio (*e);
        audio.start();
        e->getTransport().play();
        e->getScenes().launch (0);
        // A bar is a second: about 12 parts in 12 s, each landing while a check is (almost
        // always) between reading the show and reading the slots.
        int wrong = 0, checks = 0, torn = 0, edits = 0, clean = 0;
        const auto end = nowMs() + 12000;
        int phase = 0;
        double phaseStart = nowMs();
        while (nowMs() < end)
        {
            e->tickScenes();
            if (e->getScenes().getQueued() < 0 && e->getScenes().getCurrent() == 7)
                e->getScenes().launch (0);
            if (nowMs() - phaseStart > 700)
            {
                phaseStart = nowMs();
                phase = 1 - phase;
                resonance->setValueNotifyingHost (phase == 1 ? juce::jlimit (0.0f, 1.0f, saved + 0.25f) : saved);
                edits += phase;
            }
            const auto st = e->getDirty().checkNow();
            ++checks;
            torn += st.state == State::pending ? 1 : 0;
            if (phase == 1 && st.state == State::clean)
                ++wrong;
            if (phase == 0 && st.state == State::clean)
                ++clean;
        }
        const auto applied = e->getScenes().getCurrent();
        e->getTransport().stop();
        audio.waitBlocks (4);
        audio.stop();
        resonance->setValueNotifyingHost (saved);
        const auto stats = e->getDirty().getStats();
        logMessage ("  " + juce::String (checks) + " checks, " + juce::String (stats.retries) + " torn by a part changing (retried), "
                    + juce::String (torn) + " pending results, " + juce::String (edits) + " hand edits; clean while unedited " + juce::String (clean)
                    + "; last part " + juce::String (applied + 1));
        expect (stats.retries > 0, "a part landed mid-check at least once");
        expectEquals (wrong, 0, "never clean while the hand edit stood");
        expect (clean > 0, "clean while nothing was edited, between parts");
        expectState (*e, State::clean, "stopped, nothing edited");
    }

    //==========================================================================
    // Saves go first: the hashing thread starts no copy while one runs, and the save's own
    // hashes answer what was waiting.
    void savesFirst()
    {
        auto longLoops = [] (RigEngine& e, int seed, int seconds)
        {
            for (int i = 0; i < 4; ++i)
                e.getLooper().setLayerAudio (i, noise ((int) kRate * seconds, seed + i), seconds * 2.0);
        };
        {
            beginTest ("A save while a hash copy runs: no new copy starts until it ends, and its hashes answer the rest");
            auto e = openSaved (show, 256);
            LiveAudio audio (*e);
            audio.start();
            e->getTransport().play();
            longLoops (*e, 500, 60);
            e->getDirty().resetStats();
            e->getDirty().checkNow();   // four copies asked for
            for (int i = 0; i < 5000 && e->getDirty().getStats().jobsStarted < 1; ++i)
                juce::Thread::sleep (1);
            const auto started = e->getDirty().getStats().jobsStarted;
            expectEquals (started, 1, "one copy running");
            std::vector<int> seen;
            auto* dirty = &e->getDirty();
            rigshow::hooks().at = [&] (const juce::String& p, int)
            {
                if (p == "stage" || p == "beforeCommit" || p == "done")
                    seen.push_back (dirty->getStats().jobsStarted);
                return true;
            };
            const auto t0 = nowMs();
            const bool ok = e->saveShowFile (folder.getChildFile ("Yield.crateshow"), rigshow::SaveKind::document).ok;
            const auto ms = nowMs() - t0;
            rigshow::hooks().at = nullptr;
            expect (ok);
            expectEquals ((int) seen.size(), 3);
            bool none = true;
            for (const int n : seen)
                none = none && n == started;
            expect (none, "no copy began while the save ran");
            serviceFor (*e, 20000, [&] { return e->getDirty().getState() != State::pending; });
            const auto st = e->getDirty().getStats();
            logMessage ("  save " + juce::String (ms, 0) + " ms; jobs started " + juce::String (st.jobsStarted) + ", run " + juce::String (st.hashJobsCompleted)
                        + ", answered by the save " + juce::String (st.jobsSatisfiedBySave) + ", stale " + juce::String (st.staleResults));
            expectEquals (st.jobsSatisfiedBySave, 3, "the three waiting loops were answered by the save");
            expectEquals (st.jobsStarted, 1, "and never copied again");
            expectEquals (st.hashJobsCompleted, 1, "only the one that was running");
            expect (e->getDirty().getState() == State::clean, "saved as a document: clean");
            audio.stop();
        }
        {
            beginTest ("Saves while hashing never stops: each waits at most the copy already running");
            auto e = openSaved (show, 256);
            LiveAudio audio (*e);
            audio.start();
            e->getTransport().play();
            double idle = 0;
            auto timeToStage = [&] (const juce::File& f, double& total)
            {
                double atStage = 0;
                const auto t0 = nowMs();
                rigshow::hooks().at = [&] (const juce::String& p, int)
                {
                    if (p == "stage")
                        atStage = nowMs() - t0;
                    return true;
                };
                expect (e->saveShowFile (f, rigshow::SaveKind::document).ok);
                total = nowMs() - t0;
                rigshow::hooks().at = nullptr;
                return atStage;
            };
            longLoops (*e, 600, 30);
            serviceFor (*e, 20000, [&] { return e->getDirty().getState() != State::pending; });
            double total = 0;
            idle = timeToStage (folder.getChildFile ("Idle.crateshow"), total);
            e->getDirty().resetStats();
            double worstExtra = 0;
            int saves = 0;
            for (int k = 0; k < 6; ++k)
            {
                e->getLooper().setLayerAudio (k % 4, noise ((int) kRate * 30, 700 + k), 60.0);
                e->getLooper().setLayerAudio ((k + 1) % 4, noise ((int) kRate * 30, 800 + k), 60.0);
                e->getDirty().checkNow();
                for (int i = 0; i < 2000 && e->getDirty().getStats().jobsStarted <= k; ++i)
                    juce::Thread::sleep (1);
                const double wait = timeToStage (folder.getChildFile ("Busy " + juce::String (k) + ".crateshow"), total);
                worstExtra = juce::jmax (worstExtra, wait - idle);
                ++saves;
                serviceFor (*e, 30);
            }
            serviceFor (*e, 30000, [&] { return e->getDirty().getState() != State::pending; });
            const auto st = e->getDirty().getStats();
            const double oneCopy = st.maxJobMs;
            logMessage ("  copying for the save, hasher idle " + juce::String (idle, 0) + " ms; with hashing going on, worst extra wait " + juce::String (worstExtra, 0)
                        + " ms; one hash job (copy + hash) at most " + juce::String (oneCopy, 0) + " ms; " + juce::String (st.jobsStarted) + " jobs started, "
                        + juce::String (st.jobsSatisfiedBySave) + " answered by saves");
            expectEquals (saves, 6, "every save finished");
            expect (worstExtra <= oneCopy + juce::jmax (300.0, idle), "a save waits at most for the one copy running");
            audio.stop();
        }
        {
            beginTest ("A loop changed after the save copied it is hashed again");
            auto e = openSaved (show, 256);
            LiveAudio audio (*e);
            audio.start();
            e->getTransport().play();
            e->getLooper().setLayerAudio (0, noise ((int) kRate * 4, 901), 8.0);
            expect (e->saveShowFile (folder.getChildFile ("Stale.crateshow"), rigshow::SaveKind::document).ok);
            serviceFor (*e, 10000, [&] { return e->getDirty().getState() != State::pending; });
            expect (e->getDirty().getState() == State::clean, "saved: clean");
            e->getDirty().resetStats();
            e->getLooper().setLayerAudio (0, noise ((int) kRate * 4, 902), 8.0);
            expect (e->getDirty().checkNow().state == State::pending, "a sound the save never saw: not known");
            serviceFor (*e, 20000, [&] { return e->getDirty().getState() != State::pending; });
            expect (e->getDirty().getState() == State::dirty, "and it differs");
            expect (e->getDirty().getStats().hashJobsCompleted >= 1, "hashed again");
            audio.stop();
        }
        {
            beginTest ("Loops playing, hashing and saving all at once: no block missing");
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, tones::kBlock);
            e->getDirty().setEnabled (true);
            e->getTransport().setBpm (tones::kBpm);
            for (int t = 0; t < tones::kTones; ++t)
                e->getLooper().setLayerAudio (t, tones::tone (t, 40.0), 40.0);
            if (auto* p = e->getDrums().params.getParameter ("sequencer"))
                p->setValueNotifyingHost (0.0f);
            if (auto* p = e->getBass().params.getParameter ("sequencer"))
                p->setValueNotifyingHost (0.0f);
            e->getDirty().markOpened();
            e->getTransport().play();
            tones::Detector audio (*e);
            audio.start();
            auto workload = [&] (const juce::String& name)
            {
                int k = 0;
                const auto end = nowMs() + 6000;
                while (nowMs() < end)
                {
                    // A quiet loop 6 replaced again and again (each one a new sound to hash),
                    // and a save every so often.
                    juce::AudioBuffer<float> quiet (2, (int) (tones::kSamplesPerBeat * 40));
                    juce::Random random (k);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < quiet.getNumSamples(); ++i)
                            quiet.setSample (c, i, (random.nextFloat() - 0.5f) * 1.0e-4f);
                    e->getLooper().setLayerAudio (5, std::move (quiet), 40.0);
                    e->getDirty().checkNow();
                    if (k % 3 == 0)
                        expect (e->saveShowFile (folder.getChildFile (name + ".crateshow"), rigshow::SaveKind::document).ok);
                    serviceFor (*e, 200);
                    e->getLooper().maintain();
                    ++k;
                }
            };
            audio.during ([&] { workload ("All at once"); });
            const int missingOn = audio.missing.load(), lateOn = audio.late.load(), blocksOn = audio.blocks.load();
            const auto st = e->getDirty().getStats();
            e->getDirty().setEnabled (false);
            audio.during ([&] { workload ("All at once, off"); });
            const int missingOff = audio.missing.load(), lateOff = audio.late.load(), blocksOff = audio.blocks.load();
            audio.stop();
            logMessage ("  model on: " + juce::String (blocksOn) + " blocks, " + juce::String (missingOn) + " with a loop missing, " + juce::String (lateOn) + " late; "
                        + juce::String (st.hashJobsCompleted) + " hash jobs, " + juce::String (st.jobsSatisfiedBySave) + " answered by saves | model off: "
                        + juce::String (blocksOff) + " blocks, " + juce::String (missingOff) + " missing, " + juce::String (lateOff) + " late");
            expectEquals (missingOn, 0, "no loop missing with the model on");
            expectEquals (missingOff, 0, "nor off");
            expect (st.hashJobsCompleted > 0, "it hashed meanwhile");
            // (Late blocks are reported, not asserted: on a shared or loaded machine the count
            // moves by a few either way with the model on or off. A missing loop is the failure.)
        }
    }

    //==========================================================================
    void bench();
};

static RigDirtyTests rigDirtyTests;

//==============================================================================
void RigDirtyTests::bench()
{
    beginTest ("Benchmarks");
    juce::String table;
    auto line = [&table] (const juce::String& s) { table << s << "\n"; std::cout << s << std::endl; };
    line ("dirty model benchmarks: 48 kHz, 256-sample blocks, a real-time audio thread; times in ms");
    line ("case | model | blocks | callback median | p99 | worst | checks | check median | check max");
    line ("(rows marked \"on, forced\": checkNow called every 100 ms plus a hand edit every second, to load the model; not the natural check rate, see \"natural checks\" below)");

    struct Case { const char* name; std::function<std::unique_ptr<RigEngine>()> make; std::function<void (RigEngine&)> start; };
    const auto heavyFile = folder.getChildFile ("Bench heavy.crateshow");
    auto playback = [this]
    {
        auto e = openSaved (show, 256);
        return e;
    };
    auto heavy = [&, this]
    {
        if (! heavyFile.existsAsFile())
        {
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, 256);
            buildRich (*e, 2);
            addTakes (*e);
            for (int b = 1; b < 3; ++b)
            {
                e->newLoopBank ("Heavy " + juce::String (b));
                e->switchLoopBank (b);
                e->serviceLoopBanks();
                for (int i = 0; i < 4; ++i)
                    e->getLooper().setLayerAudio (i, noise ((int) kRate * 60, 50 * b + i), 120.0);
            }
            e->switchLoopBank (0);
            e->serviceLoopBanks();
            for (int i = 0; i < 4; ++i)
            {
                juce::AudioBuffer<float> b (2, (int) (kRate * 600));
                for (int c = 0; c < 2; ++c)
                {
                    auto* d = b.getWritePointer (c);
                    for (int n = 0; n < b.getNumSamples(); ++n)
                        d[n] = ((n / 4) % 2 ? 1.0f : -1.0f) * 0.02f * (float) (i + 1);
                }
                e->getLooper().setLayerAudio (i, std::move (b), 1200.0);
            }
            RigLooper::copyUnderLockForTests = true;   // (no audio thread here: copy straight away)
            expect (e->saveShowFile (heavyFile).ok);
            RigLooper::copyUnderLockForTests = false;
        }
        const auto t0 = nowMs();
        auto e = openSaved (heavyFile, 256);
        const auto opened = nowMs();
        const bool ok = e->getDirty().waitUntilResolved (60000);
        line ("heavy show (4 x 10 min loops, 2 more banks of 4 x 60 s, 2 takes): open " + juce::String (opened - t0, 0) + " ms, then "
              + (ok ? stateName (e->getDirty().getState()) : juce::String ("unresolved")) + " in " + juce::String (nowMs() - opened, 1)
              + " ms, hash jobs " + juce::String (e->getDirty().getStats().hashJobsPosted));
        return e;
    };
    std::vector<Case> cases {
        { "a. playback: automation + song", playback, [] (RigEngine& e) { e.launchSection (0); } },
        { "b. overdubbing a loop", playback, [] (RigEngine& e) { e.getTransport().play(); e.getLooper().pressRecord (1); } },
        { "c. heavy show playing", heavy, [] (RigEngine& e) { e.getTransport().play(); } },
    };
    std::vector<std::unique_ptr<RigEngine>> keep;
    for (auto& c : cases)
        for (const bool on : { false, true })
        {
            auto e = c.make();
            if (! on)
                e->getDirty().setEnabled (false);
            LiveAudio audio (*e);
            audio.start();
            c.start (*e);
            audio.waitBlocks (100);
            e->getDirty().resetStats();
            std::vector<double> checks;
            const auto times = audio.during ([&]
            {
                const auto end = nowMs() + 6000;
                int n = 0;
                while (nowMs() < end)
                {
                    if (on && ++n % 10 == 0)
                    {
                        // A hand edit now and then, so the checks have something to do.
                        hand (e->getSynth(), "resonance", 0.2f + 0.01f * (float) (n % 30));
                    }
                    if (on)
                    {
                        const auto t0 = nowMs();
                        e->getDirty().checkNow();
                        checks.push_back (nowMs() - t0);
                    }
                    if (c.start != nullptr && ! e->getTransport().isPlaying())
                        c.start (*e);
                    serviceFor (*e, 90);
                }
            });
            if (c.name[0] == 'b')
                e->getLooper().pressRecord (1);
            audio.stop();
            line (juce::String (c.name) + " | " + (on ? "on, forced" : "off") + " | " + juce::String ((int) times.size()) + " | " + juce::String (percentile (times, 0.5), 3) + " | "
                  + juce::String (percentile (times, 0.99), 3) + " | " + juce::String (times.empty() ? 0.0 : *std::max_element (times.begin(), times.end()), 3) + " | "
                  + juce::String ((int) checks.size()) + " | " + juce::String (percentile (checks, 0.5), 2) + " | "
                  + juce::String (checks.empty() ? 0.0 : *std::max_element (checks.begin(), checks.end()), 2));
        }

    // Latencies, with the app's timer pace (service every 10 ms, debounced).
    {
        auto e = openSaved (show, 256);
        LiveAudio audio (*e);
        audio.start();
        e->launchSection (0);
        serviceFor (*e, 500);
        e->getDirty().waitUntilResolved (5000);
        std::vector<double> took;
        for (int k = 0; k < 10; ++k)
        {
            hand (e->getBass(), "resonance", k % 2 ? 0.3f : 0.6f);
            const auto t0 = nowMs();
            serviceFor (*e, 3000, [&] { return e->getDirty().getState() != State::pending; });
            took.push_back (nowMs() - t0);
            if (! e->getTransport().isPlaying())
                e->launchSection (0);
        }
        line ("hand edit -> resolved (debounced timer, playing): median " + juce::String (percentile (took, 0.5), 0) + " ms, max "
              + juce::String (*std::max_element (took.begin(), took.end()), 0) + " ms (150 ms quiet is the floor)");

        hand (e->getBass(), "resonance", 0.6f);   // (as saved: only the loop differs, so it must be hashed)
        e->getDirty().resetStats();
        e->getTransport().play();
        e->getLooper().pressRecord (1);
        audio.waitBlocks (400);
        e->getLooper().pressRecord (1);
        const auto t0 = nowMs();
        serviceFor (*e, 20000, [&] { return e->getDirty().getState() != State::pending && e->getLooper().getState (1) == RigLooper::State::playing
                                            && ! e->getDirty().checkNow().provisional; });
        const auto st = e->getDirty().getStats();
        line ("overdub stopped -> resolved " + stateName (e->getDirty().getState()) + ": " + juce::String (nowMs() - t0, 0) + " ms ("
              + juce::String (st.layerCopies) + " loop copied by the audio thread, " + juce::String ((double) st.bytesHashed / 1048576.0, 1) + " MB hashed)");
        audio.stop();
    }
    {
        // Hashing alone (the hasher's function, on this thread): a 60 s stereo sound.
        const auto b = noise ((int) kRate * 60, 6);
        const auto t0 = nowMs();
        const auto h = rigshow::hashAudio (b);
        const auto ms = nowMs() - t0;
        const double mb = (double) b.getNumSamples() * 2 * sizeof (float) / 1048576.0;
        line ("hash throughput: " + juce::String (mb / (ms / 1000.0), 0) + " MB/s (" + juce::String (mb, 1) + " MB in " + juce::String (ms, 1) + " ms) [" + juce::String::toHexString ((juce::int64) h).substring (0, 4) + "]");
    }
    {
        // The heavy show re-baselined with no hash known: the sounds are the same ones (same identity), so none is hashed.
        auto e = openSaved (heavyFile, 256);
        e->getDirty().waitUntilResolved (20000);
        e->getDirty().resetStats();
        RigLooper::copyUnderLockForTests = true;   // (no audio thread here)
        e->getDirty().setEnabled (false);   // (forgets every hash)
        e->getDirty().setEnabled (true);
        e->getDirty().resetStats();
        const auto t0 = nowMs();
        e->getDirty().markOpened();
        const bool ok = e->getDirty().waitUntilResolved (120000);
        const auto ms = nowMs() - t0;
        RigLooper::copyUnderLockForTests = false;
        const auto st = e->getDirty().getStats();
        line ("heavy show, re-baselined with no hash known: " + (ok ? stateName (e->getDirty().getState()) : juce::String ("unresolved")) + " in " + juce::String (ms, 0) + " ms, "
              + juce::String (st.hashJobsCompleted) + " jobs, " + juce::String ((double) st.bytesHashed / 1048576.0, 0) + " MB, "
              + juce::String (st.hashMs > 0 ? (double) st.bytesHashed / 1048576.0 / (st.hashMs / 1000.0) : 0.0, 0) + " MB/s (identity alone decides)");
    }
    {
        // A take whose loop moved on before its hash came: its sound is copied and hashed itself.
        auto e = openSaved (show, 256);
        e->getDirty().resetStats();
        juce::AudioBuffer<float> in (2, 256), out (2, 256);
        juce::Random random (9);
        auto block = [&]
        {
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 256; ++i)
                    in.setSample (c, i, (random.nextFloat() - 0.5f) * 0.3f);
            e->process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256);
        };
        e->recordInto (1, 6, RigLooper::input, false);
        for (int i = 0; i < 4000; ++i)
        {
            block();
            if (i % 3 == 0)
                e->serviceTakes();
            if (const auto* set = e->getTakes().setFor (6); set != nullptr && ! set->takes.empty())
                break;
        }
        e->getTransport().stop();
        block();
        e->getLooper().setLayerAudio (6, noise ((int) kRate * 2, 5), 4.0);
        for (int i = 0; i < 3; ++i)
            e->getDirty().checkNow();
        e->getDirty().waitUntilResolved (20000);
        const auto st = e->getDirty().getStats();
        line ("take fallback copies: " + juce::String (st.takeFallbacks) + ", " + juce::String (st.takeFallbacks > 0 ? st.takeFallbackMs / st.takeFallbacks : 0.0, 2)
              + " ms each (a " + juce::String (e->getTakes().setFor (6) != nullptr && ! e->getTakes().setFor (6)->takes.empty()
                                                ? e->getTakes().setFor (6)->takes.back().audio.getNumSamples() / kRate : 0.0, 1) + " s take)");
    }
    // Natural checks: the song with automation, the model on, and only the app's timers.
    {
        auto e = openSaved (show, 256);
        LiveAudio audio (*e);
        audio.start();
        e->launchSection (0);
        serviceFor (*e, 300);
        e->getDirty().waitUntilResolved (5000);
        e->getDirty().resetStats();
        double serviceMs = 0, longest = 0;
        int calls = 0;
        const auto t0 = nowMs();
        double room = t0, window = t0;
        while (nowMs() < t0 + 6000)
        {
            if (nowMs() >= window)
            {
                window += 33.0;   // the window's timer
                e->tickScenes();
                if (! e->getTransport().isPlaying())
                    e->launchSection (0);
            }
            if (nowMs() >= room)
            {
                room += 100.0;    // the engine's LoopRoom timer, 10 Hz, as it is
                e->getLooper().maintain();
                e->serviceLoopBanks();
                e->getQuantizer().service();
                if (e->getAutomation().service())
                    e->getDirty().noteChange();
                e->serviceTakes();
                const auto s0 = nowMs();
                e->getDirty().service();
                const auto took = nowMs() - s0;
                serviceMs += took;
                longest = juce::jmax (longest, took);
                ++calls;
            }
            juce::Thread::sleep (2);
        }
        audio.stop();
        const auto st = e->getDirty().getStats();
        static const char* names[] = { "loop versions", "loop states", "swaps pending", "takes", "queued bank", "automation", "macros", "song", "banks" };
        juce::String by;
        for (int i = 0; i < RigDirty::Stats::numWatched; ++i)
            if (st.watchedBy[(size_t) i] > 0)
                by << names[i] << " " << st.watchedBy[(size_t) i] << ", ";
        line ("natural checks, 6 s of song + automation, timers only (" + juce::String (calls) + " service calls): " + juce::String (st.checks) + " checks, "
              + juce::String (st.notes) + " changes noted, watched counters that moved: " + (by.isEmpty() ? juce::String ("none") : by.dropLastCharacters (2))
              + " | message thread in service(): " + juce::String (serviceMs, 1) + " ms total, longest " + juce::String (longest, 2) + " ms, check max " + juce::String (st.maxCheckMs, 2) + " ms");
    }

    // Real hashing with copies while the heavy show plays: four new 10-minute sounds that match
    // nothing known, then saves with the hasher idle and with a copy in flight.
    {
        auto e = heavy();
        LiveAudio audio (*e);
        audio.start();
        e->getTransport().play();
        audio.waitBlocks (200);
        int set = 0;
        auto fresh = [&set]
        {
            ++set;
            std::vector<juce::AudioBuffer<float>> v;
            for (int i = 0; i < 4; ++i)
            {
                juce::AudioBuffer<float> b (2, (int) (kRate * 600));
                for (int c = 0; c < 2; ++c)
                {
                    auto* d = b.getWritePointer (c);
                    const int period = 3 + i + 5 * set;
                    for (int n = 0; n < b.getNumSamples(); ++n)
                        d[n] = ((n / period) % 2 ? 1.0f : -1.0f) * 0.015f * (float) (i + 1);
                }
                v.push_back (std::move (b));
            }
            return v;
        };
        auto replace = [&e] (std::vector<juce::AudioBuffer<float>>& v)
        {
            for (int i = 0; i < 4; ++i)
                e->getLooper().setLayerAudio (i, std::move (v[(size_t) i]), 1200.0);
        };
        auto stats = [] (const std::vector<double>& t)
        {
            return juce::String (percentile (t, 0.5), 3) + " / " + juce::String (percentile (t, 0.99), 3) + " / "
                   + juce::String (t.empty() ? 0.0 : *std::max_element (t.begin(), t.end()), 3);
        };

        auto a = fresh();
        e->getDirty().resetStats();
        double resolvedMs = 0;
        State resolvedAs = State::pending;
        const auto onTimes = audio.during ([&]
        {
            const auto t0 = nowMs();
            replace (a);
            serviceFor (*e, 180000, [&] { return e->getDirty().getStats().hashJobsCompleted >= 4 && e->getDirty().getState() != State::pending; });
            resolvedMs = nowMs() - t0;
            resolvedAs = e->getDirty().getState();
        });
        const auto st = e->getDirty().getStats();
        const double mb = (double) st.bytesHashed / 1048576.0;
        line ("cold hashing while playing (4 new 10-min loops): resolved " + stateName (resolvedAs) + " in " + juce::String (resolvedMs, 0) + " ms; "
              + juce::String (st.hashJobsCompleted) + " jobs, " + juce::String (mb, 0) + " MB; per loop copy+hash " + juce::String (st.hashMs / juce::jmax (1, st.hashJobsCompleted), 0)
              + " ms (copy " + juce::String (st.copyMs / juce::jmax (1, st.hashJobsCompleted), 0) + ", longest job " + juce::String (st.maxJobMs, 0) + "); "
              + juce::String (st.hashMs > 0 ? mb / (st.hashMs / 1000.0) : 0.0, 0) + " MB/s with copies");
        line ("  callback median / p99 / worst, model on, while it hashed: " + stats (onTimes) + " (" + juce::String ((int) onTimes.size()) + " blocks)");

        const auto idleFile = folder.getChildFile ("Bench save idle.crateshow");
        double idleSave = 0, busySave = 0;
        const auto idleTimes = audio.during ([&]
        {
            const auto s0 = nowMs();
            expect (e->saveShowFile (idleFile).ok);
            idleSave = nowMs() - s0;
        });

        auto b = fresh();
        std::atomic<bool> copying { false };
        RigLooper::copyPauseForTests = [&copying] (int, bool) { copying = true; };
        replace (b);
        e->getDirty().resetStats();
        e->getDirty().checkNow();   // (the four copies asked for)
        for (int i = 0; i < 2000 && ! copying.load(); ++i)
            juce::Thread::sleep (1);
        const bool inFlight = copying.load() && e->getDirty().getStats().hashJobsCompleted == 0;
        const auto busyFile = folder.getChildFile ("Bench save busy.crateshow");
        const auto busyTimes = audio.during ([&]
        {
            const auto s0 = nowMs();
            expect (e->saveShowFile (busyFile).ok);
            busySave = nowMs() - s0;
        });
        RigLooper::copyPauseForTests = nullptr;
        line ("saveShowFile, 4 new 10-min loops: hasher idle " + juce::String (idleSave, 0) + " ms; started with a hash copy in flight "
              + juce::String (busySave, 0) + " ms (" + (inFlight ? "a copy was in flight when it started" : "copy NOT confirmed in flight") + ")");
        line ("  callback median / p99 / worst during the idle save: " + stats (idleTimes) + "; during the save with hashing: " + stats (busyTimes));
        const auto r0 = nowMs();
        serviceFor (*e, 180000, [&] { return e->getDirty().getState() != State::pending; });
        const auto after = e->getDirty().getStats();
        line ("  after that save: resolved " + stateName (e->getDirty().getState()) + " " + juce::String (nowMs() - r0, 0) + " ms later; hash jobs posted "
              + juce::String (after.hashJobsPosted) + ", run " + juce::String (after.hashJobsCompleted) + ", " + juce::String ((double) after.bytesHashed / 1048576.0, 0) + " MB hashed");

        e->getDirty().setEnabled (false);
        auto c = fresh();
        const auto offTimes = audio.during ([&]
        {
            const auto t0 = nowMs();
            replace (c);
            while (nowMs() - t0 < resolvedMs)
                juce::Thread::sleep (10);
        });
        line ("  callback median / p99 / worst, model off, same window: " + stats (offTimes) + " (" + juce::String ((int) offTimes.size()) + " blocks)");
        audio.stop();
    }

    expect (true);
    if (const auto out = juce::SystemStats::getEnvironmentVariable ("KB_DIRTY_BENCH_OUT", {}); out.isNotEmpty())
        juce::File (out).replaceWithText (table);
}
