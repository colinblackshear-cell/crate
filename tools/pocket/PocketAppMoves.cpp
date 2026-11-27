#include "PocketApp.h"

// MOVES in the app: the FX pad over the whole mix, opened from the thumb bar on LOOPER and SONG (and from MORE).
// Its checks: the sheet opens, every chip is big and sets its mode, a touch on TAPE STOP changes the rendered mix,
// letting go brings the dry mix back, HOLD keeps it, closing lets go, and VISUALS is still in MORE.
using namespace pocket;

juce::String PocketApp::runMovesChecks (juce::String& report, const juce::File& folder)
{
    juce::StringArray problems;
    auto expect = [&problems] (bool ok, const juce::String& what) { if (! ok) problems.add (what); };
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 120.0;
    const bool wasDemo = loopsScreen.demoMode();
    loopsScreen.setDemoMode (false);
    appSettings.click = false;
    applyClick();
    hideToast();

    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    // the mix's loudness over `blocks` blocks
    auto listen = [&] (int blocks)
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < blocks; ++k)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
            for (float v : outL)
                sum += (double) v * v;
            n += 512;
        }
        return std::sqrt (sum / juce::jmax (1, n));
    };

    showIdeasList();
    switchIdea (ideas.create ("Moves check"));
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    listen (20);
    for (int layer = 0; layer < 2; ++layer)
    {
        const int n = (int) (8.0 * 60.0 / bpm * sr);
        auto take = LoopsEngine::makeTake (n, sr);
        for (int i = 0; i < n; ++i)
            take->data[(size_t) i] = 0.35f * (float) std::sin ((double) i * (0.03 + 0.01 * layer));
        take->length = n;
        take->lengthBeats = 8.0;
        take->tool = (int) (layer == 0 ? LayerTool::drums : LayerTool::synth);
        L.setLayer (layer, take, false, 0.0);
    }
    listen (20);
    showTool (Tool::loops, false);
    togglePlay();
    listen (60);
    expect (beats.engine().clockRunning(), "the looper didn't start playing");

    // the thumb bar's slot: MOVES on the looper, VISUALS on the other tools
    expect (thumb.state().moves, "LOOPER's thumb bar doesn't show MOVES");
    savePng (folder.getChildFile ("30a-looper-moves-slot.png"));
    thumbPressed (ThumbBar::visuals);
    expect (movesOpen && movesSheet.isVisible(), "MOVES in the thumb slot didn't open the sheet");
    expect (! visualsShown(), "MOVES in the thumb slot turned VISUALS on");
    expect (movesSheet.getHeight() > 0 && ! movesSheet.padArea().isEmpty(), "the MOVES sheet has no pad");
    expect (movesSheet.padArea().getWidth() == movesSheet.padArea().getHeight(), "the MOVES pad isn't square");

    // every chip is >= 44 pt and sets its mode
    int chipsOk = 0;
    for (int m = 0; m < PerformFx::numModes; ++m)
    {
        auto* c = movesSheet.chip (MovesSheet::modeBase + m);
        if (c == nullptr || c->getHeight() < 44 || c->getWidth() < 44)
        {
            problems.add ("MOVES chip " + PerformFx::modeName (m) + " is smaller than 44 pt");
            continue;
        }
        movesSheet.press (MovesSheet::modeBase + m);
        if (L.moves().mode.load() != m)
            problems.add ("MOVES chip " + PerformFx::modeName (m) + " didn't set the mode");
        else
            ++chipsOk;
    }
    expect (movesSheet.chip (MovesSheet::holdId)->getHeight() >= 44, "the HOLD chip is smaller than 44 pt");

    // TAPE STOP: a finger on the pad changes the mix, letting go brings it back
    movesSheet.press (MovesSheet::modeBase + PerformFx::tapeStop);
    const double dry = listen (60);
    movesSheet.touchDown (0.5f, 0.1f);
    expect (L.moves().engaged.load(), "a finger on the MOVES pad didn't engage it");
    listen (80);
    savePng (folder.getChildFile ("30b-moves-sheet-touching.png"));
    const double touched = listen (60);
    expect (std::abs (touched - dry) > 0.25 * dry, "TAPE STOP: the mix didn't change with a finger on the pad (dry " + juce::String (dry, 4) + ", touched " + juce::String (touched, 4) + ")");
    movesSheet.touchUp();
    expect (! L.moves().engaged.load(), "lifting the finger didn't release the pad");
    listen (120);
    const double back = listen (60);
    expect (std::abs (back - dry) < 0.25 * dry, "TAPE STOP: the mix didn't come back after letting go (dry " + juce::String (dry, 4) + ", back " + juce::String (back, 4) + ")");

    // HOLD keeps it after the finger lifts; HOLD off lets go
    movesSheet.press (MovesSheet::holdId);
    movesSheet.touchDown (0.5f, 0.1f);
    movesSheet.touchUp();
    expect (L.moves().engaged.load(), "HOLD: lifting the finger released the pad");
    listen (80);
    const double held = listen (60);
    expect (std::abs (held - dry) > 0.25 * dry, "HOLD: the effect didn't stay on (dry " + juce::String (dry, 4) + ", held " + juce::String (held, 4) + ")");
    movesSheet.press (MovesSheet::holdId);
    expect (! L.moves().engaged.load(), "turning HOLD off didn't release the pad");
    listen (120);

    // closing the sheet lets go (no HOLD)
    movesSheet.touchDown (0.5f, 0.1f);
    closeMoves();
    expect (! movesOpen && ! L.moves().engaged.load(), "closing the MOVES sheet didn't release the pad");
    // ... and keeps a HOLD
    openMoves();
    movesSheet.press (MovesSheet::holdId);
    movesSheet.touchDown (0.5f, 0.1f);
    movesSheet.touchUp();
    closeMoves();
    expect (L.moves().engaged.load(), "closing the sheet with HOLD on released the pad");
    openMoves();
    movesSheet.press (MovesSheet::holdId);
    closeMoves();
    expect (! L.moves().engaged.load(), "HOLD off then closing left the pad on");
    listen (120);

    // the riser's progress shows on the pad
    openMoves();
    movesSheet.press (MovesSheet::modeBase + PerformFx::riser);
    movesSheet.touchDown (0.0f, 0.8f);
    listen (150);
    expect (L.moves().getRise() > 0.02f, "RISER: no progress while the pad is held");
    savePng (folder.getChildFile ("30c-moves-sheet-riser.png"));
    movesSheet.touchUp();
    movesSheet.press (MovesSheet::modeBase + PerformFx::filter);
    closeMoves();
    listen (120);

    // SONG has MOVES in the slot too; the other tools keep VISUALS
    showTool (Tool::song, false);
    expect (thumb.state().moves, "SONG's thumb bar doesn't show MOVES");
    showTool (Tool::chords, false);
    expect (! thumb.state().moves, "CHORDS' thumb bar shows MOVES");

    // MORE: MOVES and VISUALS
    openMoreTools();
    bool hasMoves = false, hasVisuals = false;
    for (auto& i : holdCard.current().items)
    {
        hasMoves = hasMoves || i.text == "MOVES";
        hasVisuals = hasVisuals || i.text == "VISUALS";
    }
    expect (hasMoves, "MORE doesn't list MOVES");
    expect (hasVisuals, "MORE doesn't list VISUALS (it's no longer reachable on LOOPER and SONG)");
    holdCard.close();

    togglePlay();
    loopsScreen.setDemoMode (wasDemo);
    report << "MOVES: LOOPER/SONG thumb slot opens the sheet, " << chipsOk << " of 9 chips >= 44 pt set their mode, TAPE STOP changed the mix (RMS "
           << juce::String (dry, 3) << " -> " << juce::String (touched, 3) << ") and let go came back (" << juce::String (back, 3)
           << "), HOLD kept it (" << juce::String (held, 3) << "), closing released; MORE lists MOVES and VISUALS\n";
    return problems.joinIntoString ("\n");
}
