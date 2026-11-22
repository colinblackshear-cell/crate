#include "PocketApp.h"
#include "PocketStress.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#if JUCE_LINUX || JUCE_MAC
 #include <pthread.h>
 #include <unistd.h>
#endif
#if JUCE_LINUX
 #include <sys/prctl.h>
#endif

// POCKET_STRESS=<folder> (the desktop preview only; POCKET_STRESS_SECONDS sets the random part, 60 by default).
//
// The screenshots and self-checks run every engine by hand on one thread, with no sound card. The phone runs the
// renderer on a real-time audio thread while the screens change things from the message thread. This mode does what
// the phone does: the app's own renderer (the looper, with the drums, pads, synth, chords and vocoder inside it) runs
// on its own high-priority thread at the pace of a sound card (512 frames at 48 kHz, with a mic input), while a
// script on the message thread taps the real UI (through the window's peer, as a finger would): a new song, CLICK a
// few times, PADS, REC, hits on the pads, DRUMS, tools back and forth, PLAY, then random taps on the tool row, the
// thumb bar and the screens.
//
// A watchdog thread fails the run when the message thread stops answering for 2 s (or the audio thread is stuck in
// one block for 2 s): it prints the last scripted step, writes every thread's backtrace (gdb, if it is there) into
// <folder>/stall.txt and quits with exit code 2. A finished run writes <folder>/stress.txt starting with OK.
// Run it under xvfb-run, like the screenshots. With an ASan/UBSan build it also finds races' memory errors.
//
// How to run it (POCKET_SIZE=WxH and POCKET_INSETS=top,bottom try other phone sizes):
//   POCKET_STRESS=/tmp/st POCKET_STRESS_SECONDS=60 xvfb-run -a ".../Release/Kaleidobox Pocket"
// Sanitizers: configure a separate build with -DCMAKE_BUILD_TYPE=RelWithDebInfo and
//   -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"   (or -fsanitize=thread)
// and also -DCRATE_POCKET_PHONE_EXTRAS=ON, so the desktop app links the same tools/drums and tools/mpc
// sources the iPhone app does. Without it a clash between those and Pocket's own classes (like the two
// StepGrids that froze DRUMS) cannot show up here.
class PocketStress : private juce::Timer
{
public:
    PocketStress (PocketApp& a, juce::File out, int randomSeconds, std::function<void (bool)> finished)
        : app (a), folder (std::move (out)), seconds (randomSeconds), done (std::move (finished))
    {
        folder.createDirectory();
        buildScript();
        beat = now();
        heartbeat.startTimer (20);
        startAudio();
        watchdog = std::thread ([this] { watch(); });
        startTimer (300);
    }

    ~PocketStress() override
    {
        stopTimer();
        heartbeat.stopTimer();
        stopAll = true;
        if (audioThread.joinable())
            audioThread.join();
        if (watchdog.joinable())
            watchdog.join();
    }

private:
    using Tool = pocket::Tool;
    using Clock = std::chrono::steady_clock;
    static juce::int64 now() { return juce::Time::currentTimeMillis(); }

    //==========================================================================
    // The sound card: the app's one renderer, the way PocketAudio calls it.
    void startAudio()
    {
        auto& r = app.loopsScreen.engine();
        r.prepare (48000.0);   // (audioDeviceAboutToStart, before the callbacks start)
        r.setLatency (512);
        audioThread = std::thread ([this, &r]
        {
           #if JUCE_LINUX || JUCE_MAC
            sched_param sp {};
            sp.sched_priority = sched_get_priority_max (SCHED_FIFO) - 2;
            pthread_setschedparam (pthread_self(), SCHED_FIFO, &sp);   // (falls back to normal priority without the right)
           #endif
            constexpr int n = 512;
            constexpr double rate = 48000.0;
            std::vector<float> l (n), rr (n), in (n);
            const float* ins[1] = { in.data() };
            juce::Random noise (7);
            double phase = 0;
            auto next = Clock::now();
            const auto period = std::chrono::nanoseconds ((long long) (1.0e9 * n / rate));
            while (! stopAll.load())
            {
                for (int i = 0; i < n; ++i)
                {
                    in[(size_t) i] = 0.02f * (noise.nextFloat() - 0.5f) + 0.1f * (float) std::sin (phase);
                    phase += 2.0 * juce::MathConstants<double>::pi * 220.0 / rate;
                }
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (rr.begin(), rr.end(), 0.0f);
                inBlock = now();
                const auto t0 = Clock::now();
                r.setInput (ins, 1);
                r.render (l.data(), rr.data(), n);
                renderMicros += (juce::int64) std::chrono::duration_cast<std::chrono::microseconds> (Clock::now() - t0).count();
                const auto took = now() - inBlock.load();
                inBlock = 0;
                if (took > worstBlock.load())
                    worstBlock = took;
                for (auto s : l)
                    if (! std::isfinite (s))
                        nonFinite = true;
                ++blocks;
                next += period;
                const auto t = Clock::now();
                if (next < t - std::chrono::milliseconds (100))
                    next = t;   // fell far behind (a breakpoint, a slow machine): carry on from now
                std::this_thread::sleep_until (next);
            }
        });
    }

    //==========================================================================
    void watch()
    {
       #if JUCE_LINUX
        prctl (PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
       #endif
        while (! stopAll.load())
        {
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
            const auto t = now();
            const auto inside = inBlock.load();
            const bool uiStuck = t - beat.load() > 2000;
            const bool audioStuck = inside != 0 && t - inside > 2000;
            if (! uiStuck && ! audioStuck)
                continue;
            juce::String what;
            what << (uiStuck ? "STALL: the message thread has not answered for " + juce::String (t - beat.load()) + " ms"
                             : "STALL: the audio thread has been in one block for " + juce::String (t - inside) + " ms")
                 << "\nlast step: " << juce::String (marker[(size_t) (markerIndex.load() & 1)].data())
                 << "\naudio blocks rendered: " << juce::String ((juce::int64) blocks.load()) << "\n";
            std::fprintf (stderr, "%s", what.toRawUTF8());
            auto file = folder.getChildFile ("stall.txt");
            file.replaceWithText (what);
           #if JUCE_LINUX || JUCE_MAC
            const auto cmd = juce::String ("gdb -p ") + juce::String ((int) getpid())
                           + " -batch -ex 'set pagination off' -ex 'thread apply all bt' >> '" + file.getFullPathName() + "' 2>&1";
            std::system (cmd.toRawUTF8());
           #endif
            std::fprintf (stderr, "backtraces in %s\n", file.getFullPathName().toRawUTF8());
            std::_Exit (2);
        }
    }

    void mark (const juce::String& step)
    {
        const int i = (markerIndex.load() + 1) & 1;
        auto& m = marker[(size_t) i];
        step.copyToUTF8 (m.data(), m.size());
        markerIndex = i;
        const auto line = juce::String (now() - started) + " ms  " + step + "   [" + state() + "]\n";
        log << line;
        std::fprintf (stderr, "stress: %s", line.toRawUTF8());
    }

    // what the app is doing, for the log
    juce::String state() const
    {
        auto& b = app.beats.engine();
        juce::String s;
        s << (app.page == PocketApp::Page::tool ? juce::String ("tool ") + pocket::toolName (app.tool) : juce::String ("page ") + juce::String ((int) app.page))
          << " click " << (app.appSettings.click ? "on" : "off") << "/" << (b.clickIsOn() ? "on" : "off")
          << " clock " << (b.clockRunning() ? juce::String (b.clockBeat(), 2) : juce::String ("stopped"))
          << " drums " << (b.isPlaying() ? "on" : "off") << " pattern " << (b.patternPlaying() ? "on" : "off")
          << " lock " << (int) app.lockKind << (app.lockArmed ? " armed" : "") << " recPads " << (app.beats.recordingPads() ? 1 : 0)
          << " hits " << (int) app.beats.drums().padPattern.hits.size() << " layers " << app.loopsScreen.layerCount()
          << " blocks " << juce::String ((juce::int64) blocks.load());
        return s;
    }

    //==========================================================================
    // A finger on the screen: down and up at a point of a component, through the window, like a real touch.
    void tap (juce::Component& c, juce::Point<float> at, int holdMs = 0)
    {
        auto* peer = app.getPeer();
        if (peer == nullptr || ! c.isShowing())
            return;
        const auto p = peer->globalToLocal (c.localPointToGlobal (at));
        if (std::getenv ("POCKET_STRESS_DEBUG") != nullptr)
        {
            auto* hit = peer->getComponent().getComponentAt (p.roundToInt());
            std::fprintf (stderr, "tap %s at %s -> %s\n", c.getName().toRawUTF8(), p.toString().toRawUTF8(),
                          hit != nullptr ? typeid (*hit).name() : "nothing");
            std::fprintf (stderr, "  modal %d\n", juce::Component::getNumCurrentlyModalComponents());
        }
        const auto t = now();
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, p, juce::ModifierKeys::leftButtonModifier, 1.0f, 0.0f, t);
        if (holdMs <= 0)
            peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, p, juce::ModifierKeys(), 0.0f, 0.0f, t + 40);
        else
            juce::Timer::callAfterDelay (holdMs, [safe = juce::Component::SafePointer<PocketApp> (&app), p]
            {
                if (auto* a = safe.getComponent())
                    if (auto* pr = a->getPeer())
                        pr->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, p, juce::ModifierKeys(), 0.0f, 0.0f, juce::Time::currentTimeMillis());
            });
    }

    void tapThumb (int button) { tap (app.thumb, app.thumb.buttonRect (button).getCentre()); }
    void tapTool (Tool t)
    {
        for (int i = 0; i < (int) app.toolRow.tools.size(); ++i)
            if (app.toolRow.tools[(size_t) i] == t)
                tap (app.toolRow, app.toolRow.cellRect (i).getCentre());
    }
    void tapPad (int pad) { tap (app.beats, app.beats.testPadBounds (pad).getCentre()); }

    void closeModals()
    {
        // an alert or a menu a random tap opened: away with it, so the taps go on reaching the app
        juce::PopupMenu::dismissAllActiveMenus();
        for (int i = juce::Component::getNumCurrentlyModalComponents(); --i >= 0;)
            if (auto* m = juce::Component::getCurrentlyModalComponent (i))
                m->exitModalState (0);
    }

    //==========================================================================
    // run returns the wait before the next step in ms; a negative wait runs the same step again after that long
    struct Step { juce::String name; std::function<int()> run; };
    std::vector<Step> script;
    size_t at = 0;

    void add (const juce::String& name, int waitMs, std::function<void()> f)
    {
        script.push_back ({ name, [waitMs, f] { f(); return waitMs; } });
    }

    void buildScript()
    {
        // The owner's first report: the app opens, a new song, CLICK.
        add ("title: start", 600, [this] { if (app.title->onStart) app.title->onStart(); });
        add ("+ NEW SONG", 900, [this] { app.ideasScreen->onNew(); });
        for (int i = 0; i < 6; ++i)
            add ("CLICK tap " + juce::String (i + 1) + " (stopped)", 350, [this] { tapThumb (pocket::ThumbBar::click); });
        add ("PLAY", 1500, [this] { tapThumb (pocket::ThumbBar::play); });
        for (int i = 0; i < 4; ++i)
            add ("CLICK tap " + juce::String (i + 1) + " (playing)", 400, [this] { tapThumb (pocket::ThumbBar::click); });
        add ("STOP", 600, [this] { tapThumb (pocket::ThumbBar::play); });

        // The second: a beat on PADS, then DRUMS.
        add ("tool row: PADS", 700, [this] { tapTool (Tool::pads); });
        add ("REC on PADS", 200, [this] { tapThumb (pocket::ThumbBar::rec); });
        script.push_back ({ "wait for the count-in", [this, waited = 0] () mutable
        {
            auto& b = app.beats.engine();
            waited += 100;
            if (waited > 15000)
                mark ("(the count-in never ended: clock " + juce::String (b.clockRunning() ? "running" : "stopped") + ")");
            return (b.clockRunning() && b.clockBeat() >= app.padsFrom) || waited > 15000 ? 1 : -100;
        } });
        for (int i = 0; i < 24; ++i)
        {
            static const int pattern[] = { 12, 14, 13, 14 };   // kick, hat, snare, hat (bank A, the Street kit)
            add ("pad hit " + juce::String (i + 1), 220, [this, i] { tapPad (pattern[i % 4]); });
        }
        add ("tool row: DRUMS (pads still recording or locking)", 1200, [this] { tapTool (Tool::drums); });
        add ("tool row: PADS", 900, [this] { tapTool (Tool::pads); });
        script.push_back ({ "wait for the lock onto a layer", [this, waited = 0] () mutable
        {
            waited += 100;
            return app.lockKind == PocketApp::LockKind::none || waited > 20000 ? 1 : -100;
        } });
        add ("tool row: DRUMS (beat on a layer)", 1500, [this] { tapTool (Tool::drums); });
        add ("CLICK", 400, [this] { tapThumb (pocket::ThumbBar::click); });
        add ("tool row: PADS", 500, [this] { tapTool (Tool::pads); });
        add ("tool row: DRUMS", 500, [this] { tapTool (Tool::drums); });
        add ("PLAY/STOP", 900, [this] { tapThumb (pocket::ThumbBar::play); });
        add ("PLAY/STOP", 900, [this] { tapThumb (pocket::ThumbBar::play); });
        for (auto t : { Tool::loops, Tool::drums, Tool::pads, Tool::synth, Tool::chords, Tool::vocoder, Tool::effects, Tool::drums, Tool::pads, Tool::drums })
            add (juce::String ("tool row: ") + pocket::toolName (t), 400, [this, t] { tapTool (t); });
        add ("REC on DRUMS", 3000, [this] { tapThumb (pocket::ThumbBar::rec); });
        add ("tool row: PADS", 600, [this] { tapTool (Tool::pads); });
        add ("tool row: DRUMS", 600, [this] { tapTool (Tool::drums); });
        add ("REC (stop)", 800, [this] { tapThumb (pocket::ThumbBar::rec); });

        // MAKE A BEAT (the other way to start a song): the lesson, CLICK, steps, PLAY LIVE hits, then PADS and back
        add ("MAKE A BEAT", 900, [this] { app.makeABeat(); });
        for (int i = 0; i < 3; ++i)
            add ("CLICK in the lesson " + juce::String (i + 1), 350, [this] { tapThumb (pocket::ThumbBar::click); });
        for (int s : { 0, 10, 4, 12 })
            add ("step " + juce::String (s + 1), 250, [this, s] { app.beats.tapStep (s); });
        add ("lesson: next", 600, [this] { app.lesson->goTo (app.lesson->phase() + 1); });
        add ("PLAY LIVE", 500, [this] { app.beats.setDrumMode (1); });
        for (int i = 0; i < 12; ++i)
            add ("live hit " + juce::String (i + 1), 180, [this, i] { app.beats.hitLive (i % 3 == 0 ? 0 : i % 3 == 1 ? 9 : 1); });
        add ("tool row: PADS", 500, [this] { tapTool (Tool::pads); });
        add ("tool row: DRUMS", 500, [this] { tapTool (Tool::drums); });
        add ("REC on DRUMS (PLAY LIVE)", 2500, [this] { tapThumb (pocket::ThumbBar::rec); });
        for (int i = 0; i < 8; ++i)
            add ("live hit while recording " + juce::String (i + 1), 230, [this, i] { app.beats.hitLive (i % 2 == 0 ? 0 : 1); });
        add ("tool row: PADS (drums still recording)", 700, [this] { tapTool (Tool::pads); });
        add ("tool row: DRUMS", 2500, [this] { tapTool (Tool::drums); });
        add ("STEPS", 400, [this] { app.beats.setDrumMode (0); });

        // then random taps for a while
        script.push_back ({ "random taps", [this, until = (juce::int64) 0, rng = juce::Random (1234)] () mutable
        {
            if (until == 0)
                until = now() + seconds * 1000;
            if (now() > until)
                return 1;
            closeModals();
            const int kind = rng.nextInt (10);
            if (kind < 3)
            {
                const auto t = app.toolRow.tools[(size_t) rng.nextInt ((int) app.toolRow.tools.size())];
                mark (juce::String ("random: tool row ") + pocket::toolName (t));
                tapTool (t);
            }
            else if (kind < 6)
            {
                const int b = rng.nextInt (pocket::ThumbBar::count);
                mark ("random: thumb bar " + juce::String (b));
                if (b == pocket::ThumbBar::bpm)
                    app.closeAllSheets();   // (no Tempo sheet over everything)
                else
                    tapThumb (b);
            }
            else if (kind < 8 && app.page == PocketApp::Page::tool && app.tool == Tool::pads)
            {
                const int pad = rng.nextInt (16);
                mark ("random: pad " + juce::String (pad));
                tapPad (pad);
            }
            else
            {
                // anywhere on the tool on screen (not the header: no song drawer, no Mac link)
                const auto content = app.contentBounds().toFloat();
                const juce::Point<float> p (content.getX() + 24.0f + rng.nextFloat() * (content.getWidth() - 48.0f),
                                            content.getY() + rng.nextFloat() * content.getHeight());
                mark ("random: tap at " + p.toString());
                tap (app, p);
            }
            return -(120 + rng.nextInt (300));   // (stays on this step)
        } });
        add ("STOP everything", 500, [this] { if (app.beats.engine().clockRunning()) app.togglePlay(); });
    }

    void timerCallback() override
    {
        stopTimer();
        if (started == 0)
            started = now();
        if (at >= script.size())
            return finish();
        auto& s = script[at];
        if (! s.name.startsWith ("random") && ! s.name.startsWith ("wait"))
            mark (s.name);
        const int wait = s.run();
        if (wait >= 0)
            ++at;
        startTimer (juce::jmax (1, std::abs (wait)));
    }

    void finish()
    {
        heartbeat.stopTimer();
        stopAll = true;
        if (audioThread.joinable())
            audioThread.join();
        if (watchdog.joinable())
            watchdog.join();
        juce::String text;
        const bool ok = ! nonFinite.load();
        text << (ok ? "OK" : "FAILED: the mix went NaN or infinite") << "\n"
             << "audio blocks rendered: " << juce::String ((juce::int64) blocks.load()) << ", slowest block "
             << juce::String (worstBlock.load()) << " ms, on average " << juce::String ((double) renderMicros.load() / 1000.0 / (double) juce::jmax<juce::uint64> (1, blocks.load()), 2)
             << " ms (a block is 10.7 ms)\n"
             << "layers at the end: " << app.loopsScreen.layerCount() << "\n\n" << log;
        folder.getChildFile ("stress.txt").replaceWithText (text);
        std::fprintf (stderr, "stress run finished: %s\n", ok ? "OK" : "FAILED");
        if (done)
            done (ok);
    }

    PocketApp& app;
    juce::File folder;
    int seconds;
    std::function<void (bool)> done;
    juce::String log;
    juce::int64 started = 0;

    std::thread audioThread, watchdog;
    std::atomic<bool> stopAll { false }, nonFinite { false };
    std::atomic<juce::int64> beat { 0 }, inBlock { 0 }, worstBlock { 0 }, renderMicros { 0 };
    std::atomic<juce::uint64> blocks { 0 };
    std::array<std::array<char, 200>, 2> marker {};
    std::atomic<int> markerIndex { 0 };
    juce::TimedCallback heartbeat { [this] { beat = now(); } };
};

std::shared_ptr<void> startPocketStress (PocketApp& app, const juce::File& folder, int randomSeconds, std::function<void (bool)> done)
{
    return std::make_shared<PocketStress> (app, folder, randomSeconds, std::move (done));
}
