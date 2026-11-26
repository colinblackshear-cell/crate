#include "PocketApp.h"

#include "core/AudioFiles.h"
#include "Mp3Writer.h"
#include "core/TrackAnalysis.h"

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
    h.trackBars = [&L] { return L.track().lengthBars(); };
    song = std::make_unique<PocketSong> (std::move (h));
    L.setSong (song.get());

    songScreen = std::make_unique<SongScreen> (*song, L);
    songScreen->toast = [this] (const juce::String& t) { toast (t); };
    songScreen->onChanged = [this] { saveSong(); };
    songScreen->onBounce = [this] (const SongScreen::Bounce& b)
    {
        juce::String said;
        const auto files = bounceSong (b, said);
        toast (said);
        shareBounce (files);
    };
    songScreen->onBounceToMac = [this] { bounceToMac(); };
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

    // ------------------------------------------------------------------ BOUNCE: each layer + the mix, WAV
    {
        SongScreen::Bounce b;
        b.wav = true;
        b.mp3 = false;
        b.tailSeconds = 0;
        juce::String said;
        const auto files = bounceSong (b, said);
        expect (files.size() == 4, "BOUNCE didn't write 3 layers + the mix (" + juce::String (files.size()) + "): " + said);
        juce::WavAudioFormat wav;
        if (files.size() > 0)
            if (std::unique_ptr<juce::AudioFormatReader> r { wav.createReaderFor (files.getLast().createInputStream().release(), true) })
                expect (std::abs ((double) r->lengthInSamples - std::ceil (song->lengthBars() - 1.0e-6) * 4.0 * 60.0 / bpm * crate::kSampleRate) < 2.0,
                        "the bounced mix isn't the song's length");
        report << said << " (" << files.size() << " files)\n";
        b.what = SongScreen::Bounce::loops;
        b.eachLayer = false;
        expect (bounceSong (b, said).size() == 1, "BOUNCE of the layers playing now didn't write one mix");
        if (mp3::available())
        {
            b.wav = false;
            b.mp3 = true;
            const auto mp3s = bounceSong (b, said);
            expect (mp3s.size() == 1 && mp3s[0].hasFileExtension ("mp3") && mp3s[0].getSize() > 10000, "BOUNCE as MP3 didn't write an MP3");
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            if (mp3s.size() == 1)
                if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (mp3s[0]) })
                    report << "MP3: " << juce::String ((double) r->lengthInSamples / r->sampleRate, 2) << " s, " << (int) (mp3s[0].getSize() / 1024) << " KB\n";
                else
                    expect (false, "the bounced MP3 doesn't read back");
        }
    }

    loopsScreen.setDemoMode (wasDemo);
    report << "blocks " << (int) song->blocks.size() << ", takes " << (int) song->takes().size() << "\n";
    return problems.joinIntoString ("; ");
}

//==============================================================================
// BOUNCE on SONG: the song, a section or the layers playing now, as the mix or each layer + the mix, as WAV and/or MP3,
// into the song's Bounces folder (Files > Kaleidobox Pocket), then the Share sheet (AirDrop to the Mac, Files...).
juce::Array<juce::File> PocketApp::bounceSong (const SongScreen::Bounce& o, juce::String& said)
{
    juce::Array<juce::File> files;
    const double bpm = beats.drums().bpm, rate = crate::kSampleRate;
    const double samplesPerBar = 4.0 * 60.0 / bpm * rate;
    juce::Range<double> bars;
    juce::String what = "Song";
    // a friend's track: in the mix (withTrack), or left out and your parts lined up with his file
    auto& track = loopsScreen.engine().track();
    const auto trackAudio = o.what != SongScreen::Bounce::loops ? track.getAudio() : nullptr;
    const auto trackInfo = track.getInfo();
    const bool withTrack = trackAudio != nullptr && o.withTrack;
    if (o.what == SongScreen::Bounce::loops)
        what = "Loops";
    else if (o.what == SongScreen::Bounce::part)
    {
        const auto secs = song->sections();
        if (! juce::isPositiveAndBelow (o.section, (int) secs.size()))
        {
            said = "Tap a section on the ruler first.";
            return files;
        }
        bars = { secs[(size_t) o.section].start, secs[(size_t) o.section].end };
        what = secs[(size_t) o.section].name;
    }
    else
        bars = { 0.0, std::ceil (song->songBars() - 1.0e-6) };
    if (o.sendBack && trackAudio != nullptr)
        what = o.withTrack ? trackInfo.name + " + mine" : "My parts for " + trackInfo.name;

    // the layers in it, as they sound (effects, moves and board)
    struct Part { int layer; crate::SharedAudio audio; double beats; juce::String name; };
    std::vector<Part> parts;
    double longest = 0;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        if (! loopsScreen.hasLayer (l))
            continue;
        if (o.what == SongScreen::Bounce::loops ? loopsScreen.engine().isMuted (l) : ! song->layerUsed (l))
            continue;
        juce::String name;
        double b = 0;
        if (auto a = loopsScreen.layerSound (l, &name, &b); a != nullptr && b > 0)
        {
            parts.push_back ({ l, a, b, name });
            longest = juce::jmax (longest, b);
        }
    }
    if (o.what == SongScreen::Bounce::loops)
        bars = { 0.0, std::ceil (longest / 4.0 - 1.0e-6) };
    if (o.sendBack && trackAudio == nullptr)
    {
        said = "Bring in a friend's track first: SEND BACK lines your parts up with it.";
        return files;
    }
    if ((parts.empty() && ! withTrack) || bars.getLength() <= 0.0)
    {
        said = o.what == SongScreen::Bounce::loops ? "No layers playing to bounce." : "Nothing in the song to bounce yet.";
        return files;
    }

    const int body = (int) std::round (bars.getLength() * samplesPerBar);
    const int total = body + (int) (rate * o.tailSeconds);
    // Your parts without his track start where his file's bar 1 is, so they line up from 0:00 in any DAW.
    const int lead = trackAudio != nullptr && ! withTrack && o.what == SongScreen::Bounce::song
                         ? (int) std::round (trackInfo.firstBeat * trackInfo.bpm / bpm * rate) : 0;
    auto folder = lib.ideaRoot().getChildFile ("Bounces").getNonexistentChildFile (
        juce::File::createLegalFileName (ideas.current().name + " " + what + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M")), {}, false);
    folder.createDirectory();
    const auto tempo = juce::String (bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM";
    auto write = [&] (const juce::String& base, const juce::AudioBuffer<float>& body)
    {
        juce::AudioBuffer<float> padded;
        if (lead > 0)
        {
            padded.setSize (2, body.getNumSamples() + lead);
            padded.clear();
            for (int ch = 0; ch < 2; ++ch)
                padded.copyFrom (ch, lead, body, juce::jmin (ch, body.getNumChannels() - 1), 0, body.getNumSamples());
        }
        const auto& audio = lead > 0 ? padded : body;
        if (o.wav || ! mp3::available())
        {
            const auto f = folder.getChildFile (juce::File::createLegalFileName (base) + ".wav");
            juce::WavAudioFormat wav;
            if (auto stream = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
                if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24)))
                    if (writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()))
                        files.add (f);
        }
        if (o.mp3 && mp3::available())
        {
            const auto f = folder.getChildFile (juce::File::createLegalFileName (base) + ".mp3");
            if (mp3::write (f, audio, rate, 320))
                files.add (f);
        }
    };

    juce::AudioBuffer<float> mix (2, juce::jmax (1, total));
    mix.clear();
    for (const auto& p : parts)
    {
        juce::AudioBuffer<float> stem (2, juce::jmax (1, total));
        stem.clear();
        const auto& src = *p.audio;
        const int n = src.getNumSamples();
        const double samplesPerBeat = (double) n / p.beats;
        float g = 0.0f;
        for (int i = 0; i < body; ++i)
        {
            const double bar = bars.getStart() + i / samplesPerBar;
            const float target = o.what == SongScreen::Bounce::loops ? 1.0f : song->gainOf (p.layer, bar);
            g += (target - g) * 0.01f;   // a few milliseconds, as the phone plays it
            if (g < 1.0e-5f && target <= 0.0f)
                continue;
            double top = o.what == SongScreen::Bounce::loops ? 0.0 : song->originBarOf (p.layer, bar);
            if (top < -1.0e8)
                top = 0.0;
            const double beat = PocketSong::wrap ((bar - top) * 4.0, p.beats);
            const int at = juce::jlimit (0, n - 1, (int) (beat * samplesPerBeat));
            for (int ch = 0; ch < 2; ++ch)
                stem.setSample (ch, i, src.getSample (juce::jmin (ch, src.getNumChannels() - 1), at) * g);
        }
        if (o.eachLayer)
            write (what + " - Layer " + juce::String (p.layer + 1) + " " + p.name.fromFirstOccurrenceOf ("Pocket loop ", false, false).fromFirstOccurrenceOf (" ", false, false)
                       + " - " + tempo,
                   stem);
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, stem, ch, 0, total);
    }
    if (withTrack)
    {
        const auto t = RigFriendTrack::renderRange (*trackAudio, trackInfo, bpm, rate, 4, bars.getStart(), bars.getEnd(), o.tailSeconds, track.gain());
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, t, ch, 0, juce::jmin (total, t.getNumSamples()));
    }
    if (const float peak = mix.getMagnitude (0, mix.getNumSamples()); peak > 0.98f)
        mix.applyGain (0.98f / peak);
    write (what + (trackAudio != nullptr && ! withTrack ? " - Mix of my parts - " : " - Mix - ") + tempo, mix);
    if (trackAudio != nullptr && o.sendBack)
    {
        // what he needs to know to drop them in
        juce::String note;
        note << "From Kaleidobox Pocket, for " << trackInfo.name << ".\n\n";
        if (! withTrack)
            note << "These start at 0:00 of your file: drop them in at the very start and they line up.\n";
        note << "Tempo: " << tempo << ".";
        if (std::abs (bpm - trackInfo.bpm) > 0.05)
            note << " (Your track is " << juce::String (trackInfo.bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
                 << " BPM: they were played to it at " << tempo << ", so it ran a little faster or slower here.)";
        note << "\n";
        if (trackInfo.key >= 0)
            note << "Key: " << crate::TrackAnalysis::keyName (trackInfo.key, trackInfo.minor) << ".\n";
        folder.getChildFile ("Read me - tempo and lining up.txt").replaceWithText (note);
    }
    said = "Bounced " + juce::String (files.size()) + (files.size() == 1 ? " file" : " files") + " to Files > Kaleidobox Pocket > " + ideas.current().name
           + " > Bounces.";
    lastBounce = files;
    return files;
}

void PocketApp::shareBounce (const juce::Array<juce::File>& files)
{
    juce::Array<juce::URL> urls;
    for (const auto& f : files)
        urls.add (juce::URL (f));
    if (urls.isEmpty())
        return;
    bounceSharing = juce::ContentSharer::shareFilesScoped (urls, [safe = juce::Component::SafePointer<PocketApp> (this)] (bool ok, const juce::String& error)
    {
        if (safe != nullptr && ! ok && error.isNotEmpty())
            safe->toast ("Couldn't share: " + error);
    }, this);
}

void PocketApp::bounceToMac()
{
    if (lastBounce.isEmpty())
        return toast ("Bounce first: then TO MAC sends its files.");
    MacSend send;
    for (const auto& f : lastBounce)
        if (f.hasFileExtension ("wav"))
        {
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            juce::String error;
            if (auto a = crate::AudioFiles::load (f, formats, error))
                send.items.push_back ({ f.getFileNameWithoutExtension(), a, 0.0 });
        }
    if (send.items.empty())
        return toast ("The bounce has no WAVs to send (it's MP3 only): use SHARE to AirDrop them.");
    send.title = juce::String ((int) send.items.size()) + (send.items.size() == 1 ? " bounce" : " stems");
    send.detail = "WAVs";
    send.dests = { "sounds" };
    send.dest = "sounds";
    sendToMac (std::move (send));
}
