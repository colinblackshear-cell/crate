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
            line (juce::String (c.name) + " | " + (on ? "on " : "off") + " | " + juce::String ((int) times.size()) + " | " + juce::String (percentile (times, 0.5), 3) + " | "
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
    expect (true);
    if (const auto out = juce::SystemStats::getEnvironmentVariable ("KB_DIRTY_BENCH_OUT", {}); out.isNotEmpty())
        juce::File (out).replaceWithText (table);
}
