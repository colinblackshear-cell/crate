#include "PocketApp.h"

#include "core/AudioFiles.h"

// SONG > FRIEND'S TRACK in the app: bringing one in from Files (the sheet), saving it with the song, taking it out
// and putting it back, LISTEN + CLICK, and its self-checks.
using namespace pocket;

namespace
{
juce::File trackFileIn (const juce::File& ideaRoot)
{
    return ideaRoot.getChildFile (RigFriendTrack::kAssetPath);   // "Friend track/Track.wav", as the Mac's show
}
}

void PocketApp::setupTrack()
{
    auto& L = loopsScreen.engine();
    trackSheet.track = &L.track();
    trackSheet.getBpm = [this] { return beats.drums().bpm; };
    trackSheet.setBpm = [this] (double b)
    {
        beats.setBpm (b);
        repaint();
        thumb.repaint();
    };
    trackSheet.clockBeat = [this] { return beats.engine().clockBeat(); };
    trackSheet.clockRunning = [this] { return beats.engine().clockRunning(); };
    trackSheet.listen = [this] (bool on) { listenToTrack (on); };
    trackSheet.setKey = [this] (SongKey k) { setSongKey (k); };
    trackSheet.songHasSections = [this] { return ! song->markers.empty(); };
    trackSheet.markSections = [this] (const std::vector<crate::TrackAnalysis::Section>& secs)
    {
        song->markers.clear();
        for (const auto& s : secs)
            song->markers.push_back ({ s.bar, s.name });
        song->changed();
    };
    trackSheet.keep = [this] (crate::SharedAudio a, const RigFriendTrack::Info& info, bool isNew) { keepTrack (std::move (a), info, isNew); };
    trackSheet.onDone = [this] (const juce::String& said)
    {
        closeTrackSheet (false);
        toast (said);
        if (songScreen != nullptr)
            songScreen->refresh();
    };
    trackSheet.onCancel = [this] { closeTrackSheet (false); };

    songScreen->onBringInTrack = [this] { bringInTrack(); };
    songScreen->onChangeTrack = [this] { changeTrack(); };
    songScreen->onTakeOutTrack = [this] { takeOutTrack(); };
    songScreen->onPutBackTrack = [this] { putBackTrack(); };
    songScreen->canPutBackTrack = [this] { return removedTrack != nullptr; };
}

//==============================================================================
void PocketApp::bringInTrack()
{
    if (song->isLive())
        return toast ("After STOP + KEEP.");
    chooser = std::make_unique<juce::FileChooser> ("Choose your friend's track", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.m4a;*.flac;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f != juce::File())
            bringInTrackFile (f);
    });
}

void PocketApp::bringInTrackFile (const juce::File& file, bool inBackground)
{
    if (song->isOn())
        song->stopPlaying();
    trackSheet.openFile (file, inBackground);
    openTrackSheet();
}

void PocketApp::changeTrack()
{
    if (! loopsScreen.engine().track().hasAudio())
        return bringInTrack();
    if (song->isOn())
        song->stopPlaying();
    trackSheet.openCurrent();
    openTrackSheet();
}

void PocketApp::takeOutTrack()
{
    auto& t = loopsScreen.engine().track();
    if (! t.hasAudio())
        return;
    removedTrack = t.getAudio();
    removedTrackInfo = t.getInfo();
    t.clear();
    song->setPractice (0, 0);
    saveSong();
    toast ("Took the track out. + FRIEND'S TRACK > PUT IT BACK brings it back.");
}

void PocketApp::putBackTrack()
{
    if (removedTrack == nullptr)
        return;
    auto& t = loopsScreen.engine().track();
    t.set (removedTrack, removedTrackInfo);
    removedTrack = nullptr;
    saveSong();
    toast (removedTrackInfo.name + " is back.");
}

void PocketApp::keepTrack (crate::SharedAudio a, const RigFriendTrack::Info&, bool isNew)
{
    removedTrack = nullptr;
    if (isNew && a != nullptr)
    {
        // its sound beside song.json (written away from the screen: a song is a few tens of MB)
        const auto file = trackFileIn (lib.ideaRoot());
        file.getParentDirectory().createDirectory();
        juce::Thread::launch ([file, a]
        {
            const auto temp = file.getSiblingFile ("Track writing.wav");
            if (crate::AudioFiles::write (temp, *a))
                temp.moveFileTo (file);
        });
    }
    saveSong();
}

juce::var PocketApp::trackToVar()
{
    const auto& t = loopsScreen.engine().track();
    if (! t.hasAudio())
        return {};
    const auto info = t.getInfo();
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", info.name);
    o->setProperty ("bpm", info.bpm);
    o->setProperty ("firstBeat", info.firstBeat);
    o->setProperty ("key", info.key);
    o->setProperty ("minor", info.minor);
    o->setProperty ("level", t.level.load());
    o->setProperty ("mute", t.mute.load());
    o->setProperty ("file", RigFriendTrack::kAssetPath);
    return juce::var (o);
}

void PocketApp::trackFromVar (const juce::var& v)
{
    auto& t = loopsScreen.engine().track();
    removedTrack = nullptr;
    t.level = 0.8f;
    t.mute = false;
    if (! v.isObject())
    {
        t.clear();
        return;
    }
    RigFriendTrack::Info info;
    info.name = v.getProperty ("name", "Friend's track").toString();
    info.bpm = (double) v.getProperty ("bpm", 120.0);
    info.firstBeat = (double) v.getProperty ("firstBeat", 0.0);
    info.key = (int) v.getProperty ("key", -1);
    info.minor = (bool) v.getProperty ("minor", false);
    info.beatsPerBar = 4;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::String error;
    auto a = crate::AudioFiles::load (trackFileIn (lib.ideaRoot()), formats, error);
    t.set (a, info);
    t.level = juce::jlimit (0.0f, 1.0f, (float) v.getProperty ("level", 0.8f));
    t.mute = (bool) v.getProperty ("mute", false);
}

void PocketApp::listenToTrack (bool on)
{
    if (song->isOn())
        song->stopPlaying();
    if (! on)
    {
        stopClock();
        applyClick();
        thumb.repaint();
        return;
    }
    auto start = [this]
    {
        beats.engine().setClick (true);
        loopsScreen.engine().setTransport (true);   // from bar 1
        thumb.repaint();
    };
    if (beats.engine().clockRunning())
    {
        stopClock();
        juce::Timer::callAfterDelay (120, [safe = juce::Component::SafePointer<PocketApp> (this), start]
        {
            if (safe != nullptr && safe->trackOpen && safe->trackSheet.isListening())
                start();
        });
    }
    else
        start();
}

//==============================================================================
//==============================================================================
// A made-up friend's track (a kick on every beat, louder on the bar, from 0.5 s, at 100 BPM, in A minor's
// notes): brought in through the sheet, heard under the song only while the clock runs and never in a layer,
// PRACTICE going round, saved and opened again, and SEND BACK lined up with the file.
juce::String PocketApp::runTrackChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, trackBpm = 100.0, lead = 0.5;
    const bool wasDemo = loopsScreen.demoMode();
    loopsScreen.setDemoMode (false);
    appSettings.click = false;
    applyClick();
    micOn = micHear = false;
    applyMic();

    // the file
    const int seconds = 40, n = (int) (seconds * sr);
    juce::AudioBuffer<float> made (2, n);
    made.clear();
    const double beat = 60.0 / trackBpm;
    for (int k = 0; lead + k * beat < seconds - 1.0; ++k)
    {
        const int at = (int) ((lead + k * beat) * sr);
        const float amp = k % 4 == 0 ? 0.9f : 0.45f;
        for (int i = 0; i < (int) (0.12 * sr) && at + i < n; ++i)
        {
            const float env = std::exp (-(float) i / (float) (0.03 * sr));
            const float s = amp * env * std::sin (2.0f * juce::MathConstants<float>::pi * (55.0f + 90.0f * env) * (float) i / (float) sr);
            made.setSample (0, at + i, s);
            made.setSample (1, at + i, s);
        }
        // A minor's notes over it, for the key
        static const float notes[] = { 220.0f, 261.63f, 329.63f, 196.0f };
        const float f = notes[(k / 4) % 4];
        for (int i = 0; i < (int) (beat * sr) && at + i < n; ++i)
        {
            const float s = 0.08f * std::sin (2.0f * juce::MathConstants<float>::pi * f * (float) i / (float) sr);
            made.addSample (0, at + i, s);
            made.addSample (1, at + i, s);
        }
    }
    const auto file = folder.getChildFile ("Jay - idea 100bpm.wav");
    expect (crate::AudioFiles::write (file, made), "couldn't write the made-up track");

    // the sheet: heard, from the name, set, brought in
    const double bpmBefore = beats.drums().bpm;
    bringInTrackFile (file, false);
    expect (trackOpen && trackSheet.isReady(), "the track sheet didn't open the file");
    const auto heard = trackSheet.getInfo();
    report << "heard: " << juce::String (trackSheet.heardBpm(), 2) << " BPM (name 100), bar 1 at " << juce::String (heard.firstBeat, 3) << " s, key "
           << crate::TrackAnalysis::keyName (heard.key, heard.minor) << "\n";
    expect (std::abs (heard.bpm - trackBpm) < 0.01, "the tempo in the file's name wasn't used");
    expect (std::abs (heard.firstBeat - lead) < 0.03, "bar 1 isn't on the first kick (" + juce::String (heard.firstBeat, 3) + " s)");
    // CANCEL puts it all back
    trackSheet.cancel();
    closeTrackSheet (false);
    expect (! L.track().hasAudio() && std::abs (beats.drums().bpm - bpmBefore) < 1.0e-6, "CANCEL didn't put the looper and the tempo back");
    showTool (Tool::song, false);
    bringInTrackFile (file, false);
    savePng (folder.getChildFile ("song-5-friend-track-sheet.png"));
    trackSheet.useSections = true;
    const auto said = trackSheet.bringIn();
    if (trackSheet.onDone)
        trackSheet.onDone (said);
    report << "brought in: " << said << "\n";
    expect (L.track().hasAudio() && std::abs (beats.drums().bpm - trackBpm) < 1.0e-6, "BRING IT IN didn't set the track and the tempo");
    expect (std::abs (song->trackBars() - (seconds - lead) * trackBpm / 240.0) < 0.05, "the track's length in bars is wrong");
    expect (! trackOpen, "the sheet didn't close");
    hideToast();
    savePng (folder.getChildFile ("song-6-friend-track.png"));

    // heard while the clock runs, silent while it's stopped
    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    auto pump = [&] (int blocks)
    {
        float p = 0.0f;
        for (int k = 0; k < blocks; ++k)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
            for (float v : outL)
                p = juce::jmax (p, std::abs (v));
        }
        return p;
    };
    stopClock();
    pump (4);
    expect (pump (20) < 1.0e-6, "the track sounds with the clock stopped");
    songScreen->setPlayhead (0);
    songScreen->pressPlay();   // no layers: the track alone
    for (int i = 0; i < 40 && ! beats.engine().clockRunning(); ++i)
    {
        pump (2);
        songTickNow();
    }
    expect (song->isOn() && beats.engine().clockRunning(), "PLAY SONG didn't play the track alone");
    expect (pump (90) > 0.2f, "the track isn't heard under the song");

    // PRACTICE: bars 3-4 round and round
    song->setPractice (2.0, 4.0);
    song->play (2.0);
    double lowest = 1.0e9, highest = -1.0;
    for (int i = 0; i < 2000; ++i)
    {
        pump (1);
        songTickNow();
        if (beats.engine().clockRunning() && song->isOn())
        {
            lowest = juce::jmin (lowest, song->barNow());
            highest = juce::jmax (highest, song->barNow());
        }
    }
    report << "practice 3-4: bars " << juce::String (lowest, 2) << " to " << juce::String (highest, 2) << "\n";
    expect (song->isOn() && lowest >= 1.99 && highest < 4.01 && highest > 3.8, "PRACTICE didn't go round bars 3-4");
    song->setPractice (0, 0);
    song->stopPlaying();
    pump (4);

    // saved and opened again (the WAV is written in the background)
    L.track().level = 0.6f;
    saveSong();
    for (int i = 0; i < 200 && ! trackFileIn (lib.ideaRoot()).existsAsFile(); ++i)
        juce::Thread::sleep (20);
    juce::Thread::sleep (200);
    L.track().clear();
    loadSong();
    expect (L.track().hasAudio() && std::abs (L.track().getInfo().firstBeat - heard.firstBeat) < 1.0e-6 && std::abs (L.track().level.load() - 0.6f) < 1.0e-6,
            "the track didn't come back with the song");

    // SEND BACK: my parts mixed, lined up with his file (silence until his bar 1)
    if (songScreen != nullptr)
    {
        juce::String bounced;
        auto mine = songScreen->sendBackPreset (1);
        mine.tailSeconds = 0.0;
        const auto files = bounceSong (mine, bounced);
        report << "send back (my parts): " << bounced << "\n";
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        if (files.isEmpty())
            expect (bounced.contains ("Nothing"), "SEND BACK with no parts said: " + bounced);   // no layers in this song
        for (const auto& f : files)
            if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (f) })
            {
                // silence until his bar 1, so it lines up from 0:00
                juce::AudioBuffer<float> start (2, (int) (lead * sr));
                r->read (&start, 0, start.getNumSamples(), 0, true, true);
                expect (start.getMagnitude (0, (int) ((lead - 0.02) * sr)) < 1.0e-6, "my parts don't start with his lead-in's silence");
            }
        auto all = songScreen->sendBackPreset (2);
        all.mp3 = false;
        all.wav = true;
        all.tailSeconds = 0.0;
        const auto both = bounceSong (all, bounced);
        report << "send back (everything): " << bounced << "\n";
        expect (both.size() == 1, "SEND BACK everything didn't write one file");
        if (both.size() == 1)
            if (std::unique_ptr<juce::AudioFormatReader> r { formats.createReaderFor (both[0]) })
            {
                const double expectSecs = std::ceil (song->songBars() - 1.0e-6) * 240.0 / trackBpm;
                expect (std::abs ((double) r->lengthInSamples / r->sampleRate - expectSecs) < 0.01, "the bounce isn't the track's length");
                juce::AudioBuffer<float> first (2, 2000);
                r->read (&first, 0, 2000, 0, true, true);
                expect (first.getMagnitude (0, 0, 400) > 0.2f, "the bounce doesn't start on his bar 1");
            }
        expect (both.size() == 1 && both[0].getSiblingFile ("Read me - tempo and lining up.txt").existsAsFile(), "SEND BACK wrote no note");
    }

    // out, back, and out for good (the song goes on without it)
    takeOutTrack();
    expect (! L.track().hasAudio() && removedTrack != nullptr, "TAKE IT OUT didn't");
    putBackTrack();
    expect (L.track().hasAudio(), "PUT IT BACK didn't");
    takeOutTrack();
    removedTrack = nullptr;
    song->markers.clear();
    song->changed();
    beats.setBpm (bpmBefore);
    saveSong();
    hideToast();
    loopsScreen.setDemoMode (wasDemo);
    return problems.joinIntoString ("; ");
}
