#include "PocketApp.h"

using namespace pocket;

//==============================================================================
// UNDO: one history of the song. Every few tenths of a second the song as it is (song.json's parts, the beat and the
// kit, every looper layer) is compared with the last step; once a change has settled it becomes a step. UNDO puts the
// step before back, whatever it was (a pad hit, a knob, a layer, ON / OFF, the tempo), and REDO goes forward again.
// Audio is shared, not copied: a step holds the takes it had. Nothing is taken while something records.

namespace
{
constexpr int kHistoryMs = 250;
constexpr int kSettleTicks = 2;        // unchanged for two looks: a knob turn becomes one step
constexpr size_t kHistorySteps = 64;
}

bool PocketApp::historyCanRun()
{
    return ! recBusy() && lockKind == LockKind::none && ! sPart().taking() && ! song->isLive();
}

PocketApp::HistoryStep PocketApp::captureStep()
{
    HistoryStep h;
    h.song = songToVar();
    h.chordsMuted = chordsTool.engine().progressionMuted();
    h.beats = beats.snapshot();
    h.loops = loopsScreen.snapshot();
    h.sig = juce::JSON::toString (h.song, true) + (h.chordsMuted ? "|m" : "|-") + "#" + BeatsScreen::signature (h.beats) + "#"
            + LoopsScreen::signature (h.loops);
    return h;
}

void PocketApp::historyReset()
{
    historyPast.clear();
    historyFuture.clear();
    historyValid = false;
    historyPendingSig = {};
    historyPendingTicks = 0;
    historyTick.startTimer (kHistoryMs);
}

bool PocketApp::canRedoNow()
{
    return ! historyFuture.empty() && historyCanRun();
}

void PocketApp::historyTickNow()
{
    if (const bool can = canRedoNow(); can != thumbCanRedo)   // REDO lights and dims at the right time (a recording starts or ends)
    {
        thumbCanRedo = can;
        thumb.repaint();
    }
    if (! historyCanRun())
    {
        historyPendingTicks = 0;   // (what a recording makes becomes one step once it lands)
        return;
    }
    auto now = captureStep();
    if (! historyValid)
    {
        historyNow = std::move (now);
        historyValid = true;
        return;
    }
    if (now.sig == historyNow.sig)
    {
        historyPendingSig = {};
        historyPendingTicks = 0;
        return;
    }
    if (now.sig != historyPendingSig)
    {
        historyPendingSig = now.sig;   // still changing: wait for it to settle
        historyPendingTicks = 1;
        return;
    }
    if (++historyPendingTicks < kSettleTicks)
        return;
    historyPast.push_back (std::move (historyNow));
    if (historyPast.size() > kHistorySteps)
        historyPast.erase (historyPast.begin());
    historyNow = std::move (now);
    historyFuture.clear();
    historyPendingSig = {};
    historyPendingTicks = 0;
    thumbCanRedo = false;
    thumb.repaint();
}

// Before something that changes the song in one go (REC, TO LOOPER): what's there now is a step of its own, settled or not.
void PocketApp::historyCommitNow()
{
    if (! historyCanRun())
        return;
    auto now = captureStep();
    if (! historyValid)
    {
        historyNow = std::move (now);
        historyValid = true;
    }
    else if (now.sig != historyNow.sig)
    {
        historyPast.push_back (std::move (historyNow));
        if (historyPast.size() > kHistorySteps)
            historyPast.erase (historyPast.begin());
        historyNow = std::move (now);
        historyFuture.clear();
        thumbCanRedo = false;
        thumb.repaint();
    }
    historyPendingSig = {};
    historyPendingTicks = 0;
}

juce::String PocketApp::describeChange (const HistoryStep& a, const HistoryStep& b)
{
    juce::StringArray what;
    auto prop = [] (const juce::var& v, const char* key) { return juce::JSON::toString (v.getProperty (key, {}), true); };
    if (prop (a.song, "synth") != prop (b.song, "synth"))
        what.add ("synth");
    if (prop (a.song, "bass") != prop (b.song, "bass"))
        what.add ("bass");
    if (prop (a.song, "chords") != prop (b.song, "chords") || a.chordsMuted != b.chordsMuted)
        what.add ("chords");
    if (prop (a.song, "drone") != prop (b.song, "drone"))
        what.add ("drone");
    if (prop (a.song, "arrangement") != prop (b.song, "arrangement"))
        what.add ("song");
    if (prop (a.song, "boards") != prop (b.song, "boards"))
        what.add ("effects");
    if (prop (a.song, "key") != prop (b.song, "key"))
        what.add ("key");
    if (juce::JSON::toString (a.beats.drums, true) != juce::JSON::toString (b.beats.drums, true))
        what.add ("beat");
    {
        BeatsScreen::Snapshot ka = a.beats, kb = b.beats;
        ka.drums = juce::var();
        kb.drums = juce::var();
        if (BeatsScreen::signature (ka) != BeatsScreen::signature (kb))
            what.add ("pads");
    }
    if (LoopsScreen::signature (a.loops) != LoopsScreen::signature (b.loops))
        what.add ("looper");
    return what.isEmpty() ? juce::String ("your last change") : what.joinIntoString (", ");
}

void PocketApp::restoreStep (const HistoryStep& h)
{
    loadSongVar (h.song);
    chordsTool.engine().setProgressionMuted (h.chordsMuted);
    if (h.chordsMuted)
        chordsTool.releaseAll();
    beats.restore (h.beats);
    loopsScreen.restore (h.loops);
    saveSong();
    // what's on screen follows
    synthScreen.partChanged();
    bassScreen.partChanged();
    chordsScreen.repaint();
    singScreen.repaint();
    droneScreen.repaint();
    songScreen->repaint();
    sentFingerprint = {};   // what TO LOOPER sent can be sent again
    sentDrumsValid = false;
    synthUndoLayer = chordsUndoLayer = -1;
    synthUndoPart = nullptr;
    // the step as it now reads back (so the next look doesn't take the undo itself as a change)
    historyNow = captureStep();
    historyPendingSig = {};
    historyPendingTicks = 0;
    thumb.repaint();
    toolRow.repaint();
    repaint();
}

void PocketApp::historyUndo()
{
    if (! historyCanRun())
        return toast ("Stop the recording first (STOP), then UNDO.");
    // a change that hasn't settled yet is a step of its own
    if (auto now = captureStep(); historyValid && now.sig != historyNow.sig)
    {
        historyPast.push_back (std::move (historyNow));
        historyNow = std::move (now);
        historyFuture.clear();
    }
    if (historyPast.empty())
        return toast ("Nothing to undo yet");
    const auto what = describeChange (historyPast.back(), historyNow);
    historyFuture.push_back (std::move (historyNow));
    auto back = std::move (historyPast.back());
    historyPast.pop_back();
    restoreStep (back);
    toastAction ("Undid: " + what + (historyPast.empty() ? juce::String (". That's as far back as it goes.") : juce::String (". UNDO again goes back another step.")),
                 "REDO", [this] { historyRedo(); });
}

void PocketApp::historyRedo()
{
    if (historyFuture.empty())
        return toast ("Nothing to redo");
    if (! historyCanRun())
        return toast ("Stop the recording first (STOP), then REDO.");
    auto next = std::move (historyFuture.back());
    historyFuture.pop_back();
    const auto what = describeChange (historyNow, next);
    historyPast.push_back (std::move (historyNow));
    restoreStep (next);
    if (historyFuture.empty())
        toast ("Redid: " + what + ".");
    else
        toastAction ("Redid: " + what + ".", "REDO", [this] { historyRedo(); });
}

// REDO on the thumb bar: after a change and UNDO it is lit and puts the change back; a new change makes it dim again.
// Also draws the thumb bar at 393 and 375 pt wide, REDO dimmed and lit (PNGs in `folder`), and notes the button widths.
juce::String PocketApp::runRedoChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    hideToast();
    stopEverything();
    switchIdea (ideas.create ("Redo check"));
    showTool (Tool::drums);
    historyTickNow();   // the starting point
    expect (! thumb.state().canRedo, "REDO lit before anything was undone");

    const auto before = captureStep().sig;
    beats.tapStep (0);
    historyCommitNow();
    const auto changed = captureStep().sig;
    expect (changed != before, "a tap on a drum step did not change the song (the check cannot go on)");

    thumbPressed (ThumbBar::undo);
    expect (captureStep().sig == before, "UNDO did not take the change back");
    expect (thumb.state().canRedo, "REDO not lit after UNDO");
    thumbPressed (ThumbBar::redo);
    expect (captureStep().sig == changed, "REDO on the thumb bar did not put the change back");
    expect (! thumb.state().canRedo, "REDO still lit with nothing left to redo");

    // undo again, then a new change throws the redo away
    thumbPressed (ThumbBar::undo);
    expect (thumb.state().canRedo, "REDO not lit after the second UNDO");
    beats.tapStep (5);
    historyCommitNow();
    expect (! thumb.state().canRedo, "a new change did not clear REDO");
    thumbPressed (ThumbBar::redo);   // dim: it only says so
    expect (historyFuture.empty(), "REDO with nothing to redo left a future behind");

    // the bar itself: the layout and the dimmed / lit look at two phone widths
    for (int width : { 393, 375 })
    {
        ThumbBar bar;
        bar.setSize (width, thumb.getHeight() > 0 ? thumb.getHeight() : 86);
        juce::String widths;
        float narrowest = 1.0e9f, prevRight = -1.0f;
        for (int b = 0; b < ThumbBar::count; ++b)
        {
            const auto r = bar.buttonRect (b);
            widths << (b ? ", " : "") << juce::String (r.getWidth(), 1);
            narrowest = juce::jmin (narrowest, r.getWidth());
            expect (r.getX() >= prevRight, "thumb bar buttons overlap at " + juce::String (width) + " pt");
            prevRight = r.getRight();
        }
        expect (prevRight <= (float) width, "thumb bar runs off the screen at " + juce::String (width) + " pt");
        expect (narrowest >= 40.0f, "a thumb bar button is narrower than 40 pt at " + juce::String (width) + " pt");
        report << width << " pt button widths (BPM, CLICK, REC, PLAY, UNDO, REDO, VISUALS): " << widths << "\n";
        for (bool lit : { false, true })
        {
            bar.state = [lit] { ThumbBar::State s; s.canRedo = lit; return s; };
            auto image = bar.createComponentSnapshot (bar.getLocalBounds(), true, 3.0f);
            auto file = folder.getChildFile ("thumb-" + juce::String (width) + (lit ? "-redo-lit.png" : "-redo-dim.png"));
            file.deleteFile();
            if (auto out = file.createOutputStream())
            {
                juce::PNGImageFormat png;
                png.writeImageToStream (image, *out);
            }
        }
    }
    stopEverything();
    report << (problems.isEmpty() ? "REDO: lit after UNDO, puts the change back, dims after a new change\n" : "REDO: failed\n");
    return problems.joinIntoString ("; ");
}
