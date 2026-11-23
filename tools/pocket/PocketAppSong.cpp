#include "PocketApp.h"

// The SONG tool in the app: the song's hookup to the looper and the clock, its timer, and its checks.
using namespace pocket;

void PocketApp::setupSong()
{
    auto& L = loopsScreen.engine();
    PocketSong::Host h;
    h.hasLoop = [this] (int l) { return loopsScreen.hasLayer (l); };
    h.isMuted = [&L] (int l) { return L.isMuted (l); };
    h.setMute = [&L] (int l, bool m) { L.setMute (l, m); };
    h.loopBeats = [&L] (int l) { const auto t = L.takeOf (l); return t != nullptr ? t->lengthBeats.load() : 0.0; };
    h.loopStart = [&L] (int l) { return L.startBeat (l); };
    h.clockBeat = [this] { return beats.engine().clockBeat(); };
    h.clockRunning = [this] { return beats.engine().clockRunning(); };
    h.setClock = [this] (bool on)
    {
        if (on)
            loopsScreen.engine().setTransport (true);
        else
            stopClock();
        thumb.repaint();
    };
    h.countIn = [&L] (double until) { L.setSongCountIn (until); };
    song = std::make_unique<PocketSong> (std::move (h));
    L.setSong (song.get());

    songScreen = std::make_unique<SongScreen> (*song, L);
    songScreen->toast = [this] (const juce::String& t) { toast (t); };
    songScreen->onChanged = [this] { saveSong(); };
}

void PocketApp::songTickNow()
{
    if (song == nullptr)
        return;
    const bool wasLive = song->isLive(), wasOn = song->isOn();
    song->tick();
    if (wasLive != song->isLive() || wasOn != song->isOn())
    {
        thumb.repaint();
        applyClick();
        if (wasLive && ! song->isLive() && song->message().isNotEmpty())
            toast (song->message());
    }
    // what REC LIVE wrote (and edits made by hand), saved a moment later
    const auto now = juce::Time::getMillisecondCounter();
    if (! song->isLive() && song->edits() != savedSongEdits && now - songSavedAt > 1000)
    {
        songSavedAt = now;
        saveSong();
    }
}

//==============================================================================
// REC LIVE through the app's own engine (no sound card): a take written as the layers go in and out, played back
// gated by its blocks, a split, a punch, and the song saved and opened again.
juce::String PocketApp::runSongChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 120.0;
    const bool wasDemo = loopsScreen.demoMode();
    loopsScreen.setDemoMode (false);
    appSettings.click = false;
    applyClick();
    micOn = micHear = false;
    applyMic();
    hideToast();

    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    float peak = 0.0f;
    auto pump = [&] (int blocks)
    {
        for (int k = 0; k < blocks; ++k)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
            for (float v : outL)
                peak = juce::jmax (peak, std::abs (v));
        }
    };
    // the clock (and the song's timer) on to `bar`
    auto runTo = [&] (double bar)
    {
        for (int guard = 0; guard < 20000; ++guard)
        {
            pump (3);
            songTickNow();
            if (beats.engine().clockRunning() && song->barNow() >= bar)
                return true;
            if (! beats.engine().clockRunning() && ! song->isOn() && ! song->isLive() && guard > 40)
                return false;
        }
        return false;
    };
    // how loud the song is over the next half bar
    auto loudness = [&]
    {
        peak = 0.0f;
        for (int k = 0; k < 20; ++k)
        {
            pump (2);
            songTickNow();
        }
        return peak;
    };
    auto putLoop = [&] (int layer, LayerTool t, float level)
    {
        const int n = (int) (8.0 * 60.0 / bpm * sr);   // two bars
        auto take = LoopsEngine::makeTake (n, sr);
        for (int i = 0; i < n; ++i)
            take->data[(size_t) i] = level * (float) std::sin ((double) i * 0.03);
        take->length = n;
        take->lengthBeats = 8.0;
        take->tool = (int) t;
        L.setLayer (layer, take, false, 0.0);
    };

    showIdeasList();
    switchIdea (ideas.create ("Song check"));
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    pump (20);
    putLoop (0, LayerTool::drums, 0.4f);
    putLoop (1, LayerTool::synth, 0.3f);
    putLoop (2, LayerTool::chords, 0.3f);
    pump (20);
    showTool (Tool::song, false);
    expect (songScreen->isVisible(), "the SONG tool didn't open");
    expect (song->blocks.empty() && song->takes().empty(), "a new song isn't empty");
    savePng (folder.getChildFile ("song-0-empty.png"));

    // ------------------------------------------------------------------ REC LIVE: 1 bar count-in, layers in and out
    L.setMute (1, true);
    L.setMute (2, true);
    song->countInBars = 1;
    recPressed();   // the thumb's REC on SONG
    expect (song->isLive(), "REC on SONG didn't start REC LIVE");
    expect (recBusy(), "REC LIVE doesn't show as recording");
    expect (runTo (0.5), "the clock didn't start for REC LIVE");
    expect (song->getState() == PocketSong::State::waiting, "REC LIVE isn't counting in");
    expect (song->running() && song->gain (0, song->barNow()) < 1.0e-4f, "the layers play over the count-in");
    runTo (1.2);
    expect (song->getState() == PocketSong::State::recording, "REC LIVE didn't start after the count-in");
    expect (loudness() > 0.02f, "layer 1 isn't heard in REC LIVE");
    runTo (2.3);
    songScreen->press (SongScreen::tileBase + 1);   // layer 2 in, at bar 4 (song bar 3)
    expect (song->changeAt (1) == 3.0, "layer 2 isn't waiting for the next bar");
    savePng (folder.getChildFile ("song-1-rec-live.png"));
    runTo (4.4);
    L.setMute (0, true);   // layer 1's M in LOOPS: out at the next bar
    runTo (5.6);
    expect (song->changeAt (0) < 0 && ! song->isIn (0), "layer 1's M didn't take it out at the bar");
    songScreen->press (SongScreen::markId);
    runTo (7.2);
    songScreen->press (SongScreen::tileBase + 2);   // layer 3 in at bar 8
    runTo (9.5);
    songScreen->pressRecLive();   // STOP + KEEP
    expect (! song->isLive(), "STOP + KEEP didn't end REC LIVE");
    pump (4);
    songTickNow();
    expect (! beats.engine().clockRunning(), "STOP + KEEP didn't stop the clock");
    expect (song->takes().size() == 1 && song->currentTake() == 0, "the run isn't Take 1");
    auto findBlock = [&] (int layer, int nth) -> const SongBlock*
    {
        for (const auto& b : song->blocks)
            if (b.layer == layer && nth-- == 0)
                return &b;
        return nullptr;
    };
    const auto* a = findBlock (0, 0);
    const auto* b = findBlock (1, 0);
    const auto* c = findBlock (2, 0);
    expect (a != nullptr && a->start == 0.0 && std::abs (a->end() - 4.0) < 1.0e-6, "layer 1's block isn't bars 1-4");
    expect (b != nullptr && b->start == 2.0 && std::abs (b->end() - 9.0) < 1.0e-6, "layer 2's block isn't bars 3-9");
    expect (b != nullptr && std::abs (b->offset) < 1.0e-6, "layer 2 came in on its loop's top (bar 3 of a 2-bar loop)");
    expect (c != nullptr && c->start == 7.0 && std::abs (c->offset - 1.0) < 1.0e-6, "layer 3 didn't keep its groove (bar 8: its loop's 2nd bar)");
    expect (song->markers.size() == 2 && song->markers[1].bar == 5.0, "MARK didn't start a section at bar 6");
    expect (! L.isMuted (1) && ! L.isMuted (2) && L.isMuted (0), "the layers in at the end aren't left playing");
    report << "REC LIVE: " << song->message() << "\n";

    // ------------------------------------------------------------------ played back: each layer only in its blocks
    songScreen->setPlayhead (0);
    L.setMute (0, false);
    togglePlay();   // the thumb's PLAY on SONG
    expect (song->isOn(), "PLAY on SONG didn't play the song");
    runTo (0.2);
    const float bar1 = loudness();
    runTo (5.5);
    expect (bar1 > 0.02f, "the song's first bar is quiet");
    expect (song->running() && song->gain (0, song->barNow()) < 1.0e-4f && song->gain (1, song->barNow()) > 0.99f,
            "layer 1 plays after its block (or layer 2 doesn't play in its)");
    report << "playback at bar " << juce::String (song->barNow(), 2) << ": on " << (int) song->isOn() << ", clock " << (int) beats.engine().clockRunning() << "\n";
    // stops at its end
    runTo (8.5);
    for (int k = 0; k < 150 && song->isOn(); ++k)
    {
        pump (4);
        songTickNow();
    }
    pump (4);   // (the clock stops at its next block)
    if (song->isOn() || beats.engine().clockRunning())
        report << "end: bar " << juce::String (song->barNow(), 2) << ", length " << song->lengthBars() << ", on " << (int) song->isOn()
               << ", clock " << (int) beats.engine().clockRunning() << ", drums " << (int) beats.engine().isPlaying() << "\n";
    expect (! song->isOn() && ! beats.engine().clockRunning(), "the song didn't stop at its end");

    // ------------------------------------------------------------------ editing: split, fades, level, slip
    int bi = -1;
    for (int i = 0; i < (int) song->blocks.size(); ++i)
        if (song->blocks[(size_t) i].layer == 1)
            bi = i;
    songScreen->selectBlock (bi);
    songScreen->setPlayhead (5.0);
    songScreen->press (SongScreen::splitId);
    expect (song->blocks.size() == 4, "SPLIT didn't make two blocks");
    const auto* second = findBlock (1, 1);
    expect (second != nullptr && second->start == 5.0 && std::abs (second->offset - 1.0) < 1.0e-6, "the split's second half doesn't carry the loop on");
    songScreen->press (SongScreen::fadeInId);
    songScreen->press (SongScreen::fadeInId);
    songScreen->press (SongScreen::fadeOutId);
    songScreen->press (SongScreen::levelId);
    const auto& picked = song->blocks[(size_t) songScreen->selectedBlock()];
    expect (std::abs (picked.fadeIn - 1.0) < 1.0e-6 && std::abs (picked.fadeOut - 0.5) < 1.0e-6, "FADE IN / FADE OUT didn't step");
    expect (std::abs (picked.level - 0.8f) < 1.0e-4f, "LEVEL didn't step to 80%");
    savePng (folder.getChildFile ("song-2-edit.png"));

    // ------------------------------------------------------------------ punch bars 6-9 (the second section)
    songScreen->selectSection (1);
    songScreen->press (SongScreen::punchId);
    expect (song->isLive() && song->isPunch(), "PUNCH didn't start");
    runTo (4.2);
    expect (song->getState() == PocketSong::State::waiting, "PUNCH isn't in its run-up");
    runTo (6.3);
    songScreen->press (SongScreen::tileBase + 0);   // layer 1 back in at bar 8
    runTo (9.3);
    for (int k = 0; k < 40 && song->isLive(); ++k)
    {
        pump (3);
        songTickNow();
    }
    expect (! song->isLive(), "PUNCH didn't end at its last bar");
    expect (song->takes().size() == 2 && song->takes()[1].name.contains ("6-9 again"), "the punch isn't Take 2 (bars 6-9 again)");
    const auto* back = findBlock (0, 1);
    expect (back != nullptr && back->start == 7.0 && std::abs (back->end() - 9.0) < 1.0e-6, "layer 1's punched block isn't bars 8-9");
    report << "PUNCH: " << song->message() << "\n";
    if (song->isOn())
        song->stopPlaying();
    pump (4);

    // ------------------------------------------------------------------ saved with the song, takes too
    const auto blocksBefore = song->blocks.size();
    saveSong();
    song->clear();
    loadSong();
    expect (song->blocks.size() == blocksBefore && song->takes().size() == 2 && song->currentTake() == 1, "the song didn't come back from song.json");
    expect (song->useTake (0) && song->blocks.size() == 4, "Take 1 didn't come back with its edits (the split)");
    savePng (folder.getChildFile ("song-3-take-1.png"));

    loopsScreen.setDemoMode (wasDemo);
    report << "blocks " << (int) song->blocks.size() << ", takes " << (int) song->takes().size() << "\n";
    return problems.joinIntoString ("; ");
}
