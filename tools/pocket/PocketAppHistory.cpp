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

void PocketApp::historyTickNow()
{
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
