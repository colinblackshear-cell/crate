#include "LoopEditSheet.h"

using namespace pocket;

namespace
{
const juce::Colour kKnobColours[kFxKnobs] = { juce::Colour (0xfff7c04a), juce::Colour (0xffa877f5), juce::Colour (0xff9a5cf0),
                                              juce::Colour (0xff9a5cf0), juce::Colour (0xfff59a2a) };
constexpr int kWaveBins = 150;

bool hasLoop (LoopState s) { return s == LoopState::playing || s == LoopState::overdubWait || s == LoopState::overdubbing; }

juce::String clock (double seconds, bool hundredths = false)
{
    if (hundredths)
    {
        const int cs = juce::roundToInt (seconds * 100.0);
        return juce::String (cs / 6000) + ":" + juce::String ((cs / 100) % 60).paddedLeft ('0', 2) + "." + juce::String (cs % 100).paddedLeft ('0', 2);
    }
    const int s = juce::roundToInt (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

juce::String signedSemis (float v)
{
    const int st = juce::roundToInt (v);
    return (st < 0 ? juce::String (U8("\xe2\x88\x92")) : st > 0 ? "+" : "") + juce::String (std::abs (st)) + " st";
}

juce::String titleCase (const juce::String& s) { return s.substring (0, 1) + s.substring (1).toLowerCase(); }

void boxTitle (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& bold, const juce::String& rest)
{
    auto f = spaced (condensedBold (14.0f), 0.04f);
    auto f2 = small (13.0f);
    const float wb = juce::GlyphArrangement::getStringWidth (f, bold);
    g.setColour (col::ink);
    g.setFont (f);
    g.drawText (bold, r.withWidth (wb + 2), juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (f2);
    g.drawFittedText (rest, r.withTrimmedLeft (wb + 6).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
}
}

LoopEditSheet::LoopEditSheet (LoopsEngine& l) : loops (l)
{
    for (auto* b : { &done, &tabShape, &tabFx, &setIn, &setOut, &hear, &trimButton, &swingButton, &half50, &full100, &halfButton, &doubleButton,
                     &reverseButton, &undo, &recMoves, &clearMoves, &undoFx, &fullBoard, &notesButton })
        addAndMakeVisible (*b);
    for (auto* b : { &tabWarp, &warpSnap, &warpUndo, &hitsMinus, &hitsPlus, &pitchMinus, &pitchPlus, &fineMinus, &finePlus, &formantMinus, &formantPlus,
                     &originalButton, &printButton, &resetButton })
        addAndMakeVisible (*b);
    for (auto& b : gridChips) addAndMakeVisible (b);
    for (auto& b : strengthChips) addAndMakeVisible (b);
    for (auto& b : fitBars) addAndMakeVisible (b);
    for (auto& b : styles) addAndMakeVisible (b);
    for (auto& b : snaps) addAndMakeVisible (b);
    for (auto& b : fitButtons) addAndMakeVisible (b);
    for (auto& b : grids) addAndMakeVisible (b);
    for (auto* b : { &tabShape, &tabWarp, &tabFx })
        b->setFontHeight (14.5f);
    done.setFontHeight (14.5f);
    for (auto* b : { &warpSnap, &warpUndo, &hitsMinus, &hitsPlus, &pitchMinus, &pitchPlus, &fineMinus, &finePlus, &formantMinus, &formantPlus,
                     &originalButton, &printButton, &resetButton })
        b->setFontHeight (13.5f);
    for (auto& b : gridChips) b.setFontHeight (13.0f);
    for (auto& b : strengthChips) b.setFontHeight (13.0f);
    for (auto& b : fitBars) b.setFontHeight (14.0f);
    for (auto& b : styles) b.setFontHeight (13.5f);
    for (auto* b : { &halfButton, &doubleButton, &reverseButton, &undo, &recMoves, &clearMoves, &undoFx, &trimButton, &fullBoard })
        b->setFontHeight (16.0f);
    fullBoard.onClick = [this] { if (onFullBoard) onFullBoard(); };
    notesButton.setFontHeight (13.5f);
    notesButton.onClick = [this] { if (! busy() && onNotes) onNotes(); };

    done.onClick = [this] { if (onDone) onDone(); };
    tabShape.onClick = [this] { showPage (shapePage); };
    tabFx.onClick = [this] { showPage (effectsPage); };
    tabWarp.onClick = [this] { showPage (warpPage); };
    warpSnap.onClick = [this] { pressWarpSnap(); };
    warpUndo.onClick = [this] { if (! busy() && onUndo) onUndo(); };
    hitsMinus.onClick = [this] { stepHits (-1); };
    hitsPlus.onClick = [this] { stepHits (1); };
    for (int i = 0; i < 4; ++i)
    {
        gridChips[i].onClick = [this, i] { setGrid (i); };
        fitBars[i].onClick = [this, i] { pressFitBars (1 << i); };
        styles[i].onClick = [this, i] { pressStyle ((crate::Warp::Style) i); };
    }
    for (int i = 0; i < 3; ++i)
        strengthChips[i].onClick = [this, i] { setStrength (i); };
    pitchMinus.onClick = [this] { stepPitch (-1); };
    pitchPlus.onClick = [this] { stepPitch (1); };
    fineMinus.onClick = [this] { stepFine (-10); };
    finePlus.onClick = [this] { stepFine (10); };
    formantMinus.onClick = [this] { stepFormant (-1); };
    formantPlus.onClick = [this] { stepFormant (1); };
    originalButton.onClick = [this] { pressOriginal(); };
    printButton.onClick = [this] { pressPrint(); };
    resetButton.onClick = [this] { resetWarp(); };
    for (int i = 0; i < 4; ++i)
        snaps[i].onClick = [this, i] { setSnap (i); };
    setIn.onClick = [this] { setAtPlayhead (true); };
    setOut.onClick = [this] { setAtPlayhead (false); };
    hear.onClick = [this] { setHearing (! hearing); };
    trimButton.onClick = [this] { pressTrim(); };
    for (int i = 0; i < 3; ++i)
        fitButtons[i].onClick = [this, i] { fit (i); };
    for (int i = 0; i < 3; ++i)
        grids[i].onClick = [this, i] { quantize (i + 1, -1, -1); };
    swingButton.onClick = [this] { quantize (-1, -1, -2); };
    half50.onClick = [this] { quantize (-1, 0, -1); };
    full100.onClick = [this] { quantize (-1, 1, -1); };
    halfButton.onClick = [this] { if (! busy() && onEdit) onEdit (half, 0, 0); };
    doubleButton.onClick = [this] { if (! busy() && onEdit) onEdit (twice, 0, 0); };
    reverseButton.onClick = [this] { if (! busy() && onEdit) onEdit (reverse, 0, 0); };
    undo.onClick = [this] { if (! busy() && onUndo) onUndo(); };
    undoFx.onClick = [this] { if (! busy() && onUndo) onUndo(); };
    recMoves.onClick = [this]
    {
        if (loops.recMoves (current))
        {
            loops.setRecMoves (current, false);
            if (onFxChanged) onFxChanged();
        }
        else
            startRecMoves();
        repaint();
    };
    clearMoves.onClick = [this]
    {
        if (! loops.hasAnyMoves (current))
            return onToast ("No moves recorded on this layer yet.");
        loops.setRecMoves (current, false);
        loops.clearMoves (current);
        if (onFxChanged) onFxChanged();
        onToast ("Moves cleared. The knobs stay where they are.");
    };
}

void LoopEditSheet::open (int l, int p, int focus)
{
    if (current >= 0 && current != l)
        close();
    current = l;
    version = -1;
    for (int k = 0; k < kFxKnobs; ++k)
        if (loops.hasMoves (l, k))
        {
            laneKnob = k;
            break;
        }
    layerChanged();
    flash = focus != noFocus ? focus * 100 + 24 : 0;
    showPage (p);
    startTimerHz (30);
}

void LoopEditSheet::close()
{
    if (current >= 0)
    {
        loops.setRecMoves (current, false);
        loops.setRegion (current, 0, 0);
    }
    hearing = false;
}

void LoopEditSheet::showPage (int p)
{
    pageShown = p;
    if (p != effectsPage && current >= 0)
        loops.setRecMoves (current, false);
    layout();
    repaint();
}

void LoopEditSheet::layerChanged()
{
    peaks = loops.peaks (current, kWaveBins);
    version = loops.version (current);
    trimFrom = 0;
    trimTo = contentBeats();
    hearing = false;
    if (current >= 0)
        loops.setRegion (current, 0, 0);
    readWarp();
    layout();
    repaint();
}

double LoopEditSheet::totalBeats() const
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    return t != nullptr ? t->lengthBeats.load() : 0.0;
}

double LoopEditSheet::contentBeats() const
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    if (t == nullptr || t->length.load() <= 0)
        return 0.0;
    return t->lengthBeats.load() * (double) t->contentSamples() / (double) t->length.load();
}

bool LoopEditSheet::hasPart() const
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    return t != nullptr && t->part != nullptr;
}

bool LoopEditSheet::hasNotes() const
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    return t != nullptr && t->part != nullptr && t->part->kind == pocket::NotePart::synth;
}

bool LoopEditSheet::markersMoved() const
{
    return trimFrom > 1.0e-6 || std::abs (trimTo - contentBeats()) > 1.0e-6;
}

double LoopEditSheet::regionBars() const
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    if (t == nullptr || t->length.load() <= 0 || totalBeats() <= 0)
        return 0.0;
    // the sound's real length in seconds, at the song's tempo
    const double seconds = (trimTo - trimFrom) / totalBeats() * (double) t->length.load() / t->sampleRate;
    return seconds * loops.bpm() / 60.0 / 4.0;
}

juce::String LoopEditSheet::placeText (double beat) const
{
    const int b = (int) std::floor (beat + 1.0e-6);
    switch (snap)
    {
        case 0: return "BAR " + juce::String (b / 4 + 1);
        case 1: return juce::String (b / 4 + 1) + "." + juce::String (b % 4 + 1);
        case 2: return juce::String (b / 4 + 1) + "." + juce::String (b % 4 + 1) + "." + juce::String ((int) std::floor ((beat - b) * 4.0 + 1.0e-6) + 1);
        default: break;
    }
    return clock (beat * 60.0 / juce::jmax (40.0, loops.bpm()), true);
}

void LoopEditSheet::setSnap (int s)
{
    snap = juce::jlimit (0, 3, s);
    // the markers move onto the new grid
    const double total = contentBeats();
    if (snap < 3)
    {
        trimFrom = juce::jlimit (0.0, total, snapBeat (trimFrom, snap));
        trimTo = trimTo >= total - 1.0e-6 ? total : juce::jlimit (0.0, total, snapBeat (trimTo, snap));
        if (trimTo <= trimFrom)
            trimTo = juce::jmin (total, trimFrom + snapBeats (snap));
        if (trimTo <= trimFrom)
            trimFrom = juce::jmax (0.0, trimTo - snapBeats (snap));
    }
    updateRegion();
    refreshButtons();
    repaint();
}

void LoopEditSheet::setAtPlayhead (bool in)
{
    if (current < 0 || busy())
        return;
    if (! loops.transportRunning())
    {
        if (onPlay) onPlay();
        return onToast (juce::String ("Playing. Tap SET ") + (in ? "IN" : "OUT") + " when you hear where it should " + (in ? "start." : "end."));
    }
    const double total = totalBeats(), sound = contentBeats();
    double at = juce::jlimit (0.0, sound, snapBeat (loops.position (current) * total, snap));
    if (in)
    {
        if (at >= trimTo - 1.0e-6)
            trimTo = sound;
        trimFrom = juce::jmin (at, juce::jmax (0.0, trimTo - juce::jmax (0.25, snapBeats (snap))));
    }
    else
    {
        if (at <= trimFrom + 1.0e-6)
            trimFrom = 0.0;
        trimTo = juce::jmax (at, juce::jmin (sound, trimFrom + juce::jmax (0.25, snapBeats (snap))));
    }
    updateRegion();
    repaint();
}

void LoopEditSheet::pressTrim()
{
    if (busy())
        return;
    if (! markersMoved())
        return onToast ("Move IN or OUT first (drag them, or SET IN / SET OUT as it plays). TRIM keeps what's between them.");
    setHearing (false);
    if (onEdit)
        onEdit (trim, trimFrom, trimTo);
}

void LoopEditSheet::setHearing (bool on)
{
    hearing = on && current >= 0;
    if (hearing && ! loops.transportRunning() && onPlay)
        onPlay();
    updateRegion();
    refreshButtons();
}

void LoopEditSheet::updateRegion()
{
    if (current < 0)
        return;
    if (hearing)
        loops.setRegion (current, trimFrom, trimTo);
    else
        loops.setRegion (current, 0, 0);
    refreshButtons();
}

void LoopEditSheet::fit (int which)
{
    if (busy() || current < 0)
        return;
    const int bars = which == 0 ? choices.nearest : which == 1 ? choices.smaller : choices.larger;
    if (bars <= 0)
        return;
    setHearing (false);
    if (onFit)
        onFit (bars, trimFrom, trimTo);
}

void LoopEditSheet::quantize (int grid, int strength, int swing)
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    if (busy() || t == nullptr || t->part == nullptr)
        return;
    auto q = t->quant;
    if (grid > 0)
        q.grid = q.grid == grid ? 0 : grid;   // the lit grid again: as played
    if (strength >= 0)
        q.strength = strength == 0 ? 0.5f : 1.0f;
    if (swing == -2)
        q.swing = ! q.swing;
    if (q.grid == 0 && grid < 0)
        q.grid = 3;   // 50%, 100% or SWING with no grid yet: on the 1/16s
    if (onQuantize)
        onQuantize (q);
}

void LoopEditSheet::startRecMoves()
{
    loops.setRecMoves (current, true);
    if (! loops.transportRunning() && onPlay)
        onPlay();
    onToast ("Turn a knob as the loop plays: its moves record. Let go and they play back every pass.");
}

void LoopEditSheet::timerCallback()
{
    if (! isVisible() || current < 0)
        return stopTimer();
    if (! hasLoop (loops.state (current)))
    {
        close();
        if (onDone) onDone();
        return;
    }
    if (loops.version (current) != version)
        layerChanged();
    loops.recordMoves();
    const bool rec = loops.recMoves (current);
    recMoves.setText (rec ? U8("\xe2\x96\xa0 Stop rec") : U8("\xe2\x97\x8f Rec moves"));
    recMoves.setLook (rec ? Look::amber : Look::red);
    fullBoard.setLamp (true, loops.board (layerBoardId (current)).audible());   // lit: the layer's board is in use
    if (flash > 0)
        --flash;
    if (flash % 100 == 0)
        flash = 0;
    refreshButtons();
    repaint();
}

juce::Rectangle<float> LoopEditSheet::waveArea() const
{
    auto b = waveBox();
    if (pageShown == effectsPage)
        return { b.getX() + 10, b.getY() + 28, b.getWidth() - 20, 46 };
    if (pageShown == warpPage)
        return { b.getX() + 10, b.getY() + 28, b.getWidth() - 20, b.getHeight() - 52 };
    return { b.getX() + 10, b.getY() + 30, b.getWidth() - 20, b.getHeight() - 52 };
}

juce::Rectangle<float> LoopEditSheet::knobCell (int k) const
{
    auto p = knobPanel();
    const float w = p.getWidth() / (float) kFxKnobs;
    return { p.getX() + (float) k * w, p.getY(), w, p.getHeight() };
}

int LoopEditSheet::knobAt (juce::Point<float> p) const
{
    for (int k = 0; k < kFxKnobs; ++k)
        if (knobCell (k).contains (p))
            return k;
    return -1;
}

float LoopEditSheet::beatX (double beat) const
{
    auto w = waveArea();
    const double total = totalBeats();
    return w.getX() + (total > 0 ? (float) (beat / total) * w.getWidth() : 0.0f);
}

double LoopEditSheet::xBeat (float x) const
{
    auto w = waveArea();
    return (double) (x - w.getX()) / w.getWidth() * totalBeats();
}

//==============================================================================
void LoopEditSheet::resized() { layout(); }

void LoopEditSheet::layout()
{
    const float w = (float) getWidth(), inner = w - 32.0f;
    const bool shape = pageShown == shapePage, warp = pageShown == warpPage, fx = pageShown == effectsPage;
    done.setBounds (juce::Rectangle<float> (w - 82, 10, 66, 38).toNearestInt());
    tabFx.setBounds (juce::Rectangle<float> ((float) done.getX() - 6 - 66, 10, 66, 38).toNearestInt());
    tabWarp.setBounds (juce::Rectangle<float> ((float) tabFx.getX() - 4 - 54, 10, 54, 38).toNearestInt());
    tabShape.setBounds (juce::Rectangle<float> ((float) tabWarp.getX() - 4 - 54, 10, 54, 38).toNearestInt());
    tabShape.setLook (shape ? Look::dark : Look::cream);
    tabWarp.setLook (warp ? Look::dark : Look::cream);
    tabFx.setLook (fx ? Look::dark : Look::cream);
    if (preferredHeight() != shownHeight)
    {
        shownHeight = preferredHeight();
        if (onHeightChanged)
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LoopEditSheet> (this)] { if (safe != nullptr && safe->onHeightChanged) safe->onHeightChanged(); });
    }

    for (auto& b : snaps) b.setVisible (shape);
    for (auto& b : fitButtons) b.setVisible (false);
    for (auto& b : grids) b.setVisible (shape && hasPart());
    for (auto* b : { &setIn, &setOut, &hear, &trimButton, &halfButton, &doubleButton, &reverseButton, &undo })
        b->setVisible (shape);
    for (auto* b : { &swingButton, &half50, &full100 })
        b->setVisible (shape && hasPart());
    notesButton.setVisible (shape && hasNotes());
    for (auto* b : { &recMoves, &clearMoves, &undoFx, &fullBoard })
        b->setVisible (fx);
    for (auto* b : { &warpSnap, &warpUndo, &hitsMinus, &hitsPlus, &pitchMinus, &pitchPlus, &fineMinus, &finePlus, &formantMinus, &formantPlus,
                     &originalButton, &printButton, &resetButton })
        b->setVisible (warp);
    for (auto& b : gridChips) b.setVisible (warp);
    for (auto& b : strengthChips) b.setVisible (warp);
    for (auto& b : fitBars) b.setVisible (warp);
    for (auto& b : styles) b.setVisible (warp);

    if (shape)
    {
        // SNAP  BAR BEAT 1/16 OFF   SET IN  SET OUT
        float y = 178;
        const float label = 40, gap = 4, setW = 60;
        const float chipW = (inner - label - 2 * setW - 6 * gap) / 4.0f;
        float x = 16 + label;
        for (auto& b : snaps)
        {
            b.setBounds (juce::Rectangle<float> (x, y, chipW, 40).toNearestInt());
            b.setFontHeight (15.0f);
            x += chipW + gap;
        }
        x += gap;
        setIn.setBounds (juce::Rectangle<float> (x, y, setW, 40).toNearestInt());
        setOut.setBounds (juce::Rectangle<float> (x + setW + gap, y, w - 16 - (x + setW + gap), 40).toNearestInt());
        setIn.setFontHeight (14.0f);
        setOut.setFontHeight (14.0f);

        y += 46;
        hear.setBounds (juce::Rectangle<float> (16, y, inner * 0.6f - 3, 42).toNearestInt());
        hear.setFontHeight (15.0f);
        trimButton.setBounds (juce::Rectangle<float> (16 + inner * 0.6f + 3, y, inner * 0.4f - 3, 42).toNearestInt());

        y += 50;
        fitBox = { 16, y, inner, 100 };
        const float fy = y + 26;
        const float bw = (inner - 16 - 12) / 4.0f;
        fitButtons[0].setBounds (juce::Rectangle<float> (24, fy, bw * 2, 42).toNearestInt());
        fitButtons[1].setBounds (juce::Rectangle<float> (24 + bw * 2 + 6, fy, bw, 42).toNearestInt());
        fitButtons[2].setBounds (juce::Rectangle<float> (24 + bw * 3 + 12, fy, bw, 42).toNearestInt());
        y += 108;

        if (hasPart())
        {
            quantBox = { 16, y, inner, 82 };
            notesButton.setBounds (juce::Rectangle<float> (16 + inner - 8 - 104, y + 3, 104, 26).toNearestInt());
            const float qy = y + 30, cw = (inner - 16 - 5 * 5 - 10) / 6.0f;
            float qx = 24;
            for (auto& b : grids)
            {
                b.setBounds (juce::Rectangle<float> (qx, qy, cw, 42).toNearestInt());
                qx += cw + 5;
            }
            swingButton.setBounds (juce::Rectangle<float> (qx, qy, cw, 42).toNearestInt());
            qx += cw + 5 + 10;
            half50.setBounds (juce::Rectangle<float> (qx, qy, cw, 42).toNearestInt());
            full100.setBounds (juce::Rectangle<float> (qx + cw + 5, qy, w - 24 - (qx + cw + 5), 42).toNearestInt());
            for (auto* b : { &grids[0], &grids[1], &grids[2], &swingButton, &half50, &full100 })
                b->setFontHeight (15.0f);
            y += 90;
        }
        else
            quantBox = {};

        const float bw4 = (inner - 18) / 4.0f;
        int i = 0;
        for (auto* b : { &halfButton, &doubleButton, &reverseButton, &undo })
            b->setBounds (juce::Rectangle<float> (16 + (float) i++ * (bw4 + 6), y, bw4, 42).toNearestInt());
    }
    else if (warp)
    {
        auto at = [] (float x, float y, float bw, float bh = 38.0f) { return juce::Rectangle<float> (x, y, bw, bh).toNearestInt(); };
        infoRect = { 16, 172, inner, 22 };
        const float h = 38;
        float y = 198, x = 16;
        // SNAP  Beat 1/8 1/16 1/32  50% 75% 100%
        warpSnap.setBounds (at (x, y, 52));
        x += 52 + 6;
        const float cw = (inner - 52 - 6 - 8 - 5 * 3) / 7.0f;
        for (auto& b : gridChips) { b.setBounds (at (x, y, cw, h)); x += cw + 3; }
        x += 5;
        for (auto& b : strengthChips) { b.setBounds (at (x, y, cw, h)); x += cw + 3; }

        // HITS - 12 +   FIT BARS 1 2 4 8
        y += h + 6;
        hitsMinus.setBounds (at (16, y, 38));
        hitsRect = { 16 + 38, y, 62, h };
        hitsPlus.setBounds (at (16 + 38 + 62, y, 38));
        x = 16 + 138 + 10;
        fitLabel = { x, y, 50, h };
        x += 52;
        const float fw = (w - 16 - x - 3 * 3) / 4.0f;
        for (auto& b : fitBars) { b.setBounds (at (x, y, fw, h)); x += fw + 3; }

        // STRETCH  Voice Beats Texture Tape
        y += h + 6;
        stretchLabel = { 16, y, 54, h };
        x = 16 + 56;
        const float sw = (w - 16 - x - 3 * 3) / 4.0f;
        for (auto& b : styles) { b.setBounds (at (x, y, sw, h)); x += sw + 3; }

        // - st  + st   Pitch -1 st +10 ct   - ct  + ct
        y += h + 6;
        pitchMinus.setBounds (at (16, y, 46));
        pitchPlus.setBounds (at (16 + 49, y, 46));
        finePlus.setBounds (at (w - 16 - 46, y, 46));
        fineMinus.setBounds (at (w - 16 - 46 - 49, y, 46));
        pitchRect = { 16 + 98 + 4, y, w - 32 - 196 - 8, h };

        // - st  Formant +2 st  + st
        y += h + 6;
        formantMinus.setBounds (at (16, y, 46));
        formantPlus.setBounds (at (w - 16 - 46, y, 46));
        formantRect = { 16 + 50, y, w - 32 - 100, h };

        // ORIGINAL  PRINT  RESET  UNDO
        y += h + 8;
        const float qw = (inner - 9) / 4.0f;
        x = 16;
        for (auto* b : { &originalButton, &printButton, &resetButton, &warpUndo })
        {
            b->setBounds (at (x, y, qw, 42));
            x += qw + 3;
        }
        hintRect = { 16, y + 46, inner, 34 };
    }
    else
    {
        recMoves.setBounds (juce::Rectangle<float> (16, 300, inner * 0.55f, 46).toNearestInt());
        clearMoves.setBounds (juce::Rectangle<float> ((float) recMoves.getRight() + 8, 300, w - 16 - (float) recMoves.getRight() - 8, 46).toNearestInt());
        undoFx.setBounds (16, 354, 80, 42);
        fullBoard.setBounds (juce::Rectangle<float> (w - 16 - 170, 354, 170, 42).toNearestInt());
    }
    refreshButtons();
}

void LoopEditSheet::refreshButtons()
{
    if (pageShown == warpPage)
    {
        warpSnap.setToggled (snapOn);
        const double grids[] = { 1.0, 0.5, 0.25, 0.125 }, strengths[] = { 0.5, 0.75, 1.0 };
        for (int i = 0; i < 4; ++i)
        {
            gridChips[i].setToggled (std::abs (gridBeats - grids[i]) < 1.0e-6);
            fitBars[i].setToggled (std::abs (plan.targetBeats - 4.0 * (double) (1 << i)) < 1.0e-6);
            styles[i].setToggled ((int) plan.settings.style == i);
        }
        for (int i = 0; i < 3; ++i)
            strengthChips[i].setToggled (std::abs ((double) strength - strengths[i]) < 1.0e-3);
        originalButton.setToggled (isOriginal && isOriginal());
        return;
    }
    for (int i = 0; i < 4; ++i)
        snaps[i].setToggled (snap == i);
    hear.setToggled (hearing);
    hear.setText (hearing ? U8("\xe2\x96\xa0 Hearing in-out") : U8("\xe2\x96\xb6 Hear in-out"));

    // BEAT MATCH: the nearest whole bars, and the next smaller and larger sensible lengths
    if (pageShown == shapePage && current >= 0)
    {
        choices = fitChoices (regionBars());
        const bool exactAlready = choices.exact && ! markersMoved();
        fitButtons[0].setVisible (! exactAlready);
        fitButtons[0].setText ("Fit to " + juce::String (choices.nearest) + (choices.nearest == 1 ? " bar" : " bars"));
        fitButtons[1].setVisible (choices.smaller > 0);
        fitButtons[1].setText ("Fit to " + juce::String (choices.smaller));
        fitButtons[2].setVisible (choices.larger > 0);
        fitButtons[2].setText ("Fit to " + juce::String (choices.larger));
        for (auto& b : fitButtons)
            b.setFontHeight (15.0f);
        if (auto t = loops.takeOf (current); t != nullptr && t->part != nullptr)
        {
            for (int i = 0; i < 3; ++i)
                grids[i].setToggled (t->quant.grid == i + 1);
            swingButton.setToggled (t->quant.swing);
            half50.setToggled (t->quant.grid > 0 && t->quant.strength < 0.99f);
            full100.setToggled (t->quant.grid > 0 && t->quant.strength >= 0.99f);
        }
    }
}

void LoopEditSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    if (current < 0)
        return;

    auto take = loops.takeOf (current);
    const double total = totalBeats();
    const bool shape = pageShown == shapePage, warp = pageShown == warpPage;
    g.setColour (col::ink);
    g.setFont (condensedBold (24.0f));
    {
        auto title = "Layer " + juce::String (current + 1) + U8(" \xc2\xb7 ") + (take != nullptr ? titleCase (layerToolName (take->tool)) : juce::String());
        const int room = tabShape.getX() - 22;
        if (juce::GlyphArrangement::getStringWidth (condensedBold (24.0f), title) > (float) room)
            title = "Layer " + juce::String (current + 1);
        g.drawFittedText (title, juce::Rectangle<int> (16, 10, room, 38), juce::Justification::centredLeft, 1, 0.7f);
    }

    // the wave, always: bar numbers, IN and OUT (or the moves lane), where it's playing
    auto box = waveBox();
    drawWell (g, box, 12.0f);
    auto wave = waveArea();
    const auto colour = take != nullptr ? layerToolColour (take->tool) : kKnobColours[fxFilter];
    const double sound = contentBeats();
    if (shape || warp)
    {
        g.setFont (lcdFont (15.0f));
        for (int bar = 0; bar * 4.0 < total - 1.0e-6; ++bar)
        {
            const float x = beatX (bar * 4.0);
            g.setColour (juce::Colour (0x40ffffff));
            g.fillRect (x, box.getY() + 6, 1.0f, wave.getBottom() - box.getY() - 6);
            if (total <= 64.0 || bar % 4 == 0)
            {
                g.setColour (juce::Colour (0xffb7a9c2));
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 3, box.getY() + 4, 24, 14), juce::Justification::centredLeft);
            }
        }
    }
    else
    {
        const double secs = take != nullptr ? take->length.load() / take->sampleRate : 0.0;
        const double bars = total / 4.0;
        g.setColour (col::lcdAmber);
        g.setFont (lcdFont (19.0f));
        g.drawText (juce::String (bars, bars == std::floor (bars) ? 0 : 2) + (bars == 1.0 ? " BAR" : " BARS") + U8(" \xc2\xb7 ") + clock (secs) + U8(" \xc2\xb7 ")
                        + juce::String (loops.bpm(), 0) + " BPM",
                    box.withTrimmedLeft (10).withTrimmedRight (10).withHeight (22).translated (0, 2), juce::Justification::centredLeft);
    }
    if (! peaks.empty())
    {
        const float pitch = wave.getWidth() / (float) peaks.size();
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = wave.getX() + (float) i * pitch;
            const double beat = (x + pitch * 0.5f - wave.getX()) / wave.getWidth() * total;
            const bool kept = ! shape || (beat >= trimFrom && beat <= trimTo);
            const float p = std::sqrt (juce::jlimit (0.0f, 1.0f, peaks[i] * 1.6f));
            const float h = juce::jmax (2.0f, p * wave.getHeight());
            g.setColour (beat > sound + 1.0e-6 ? juce::Colour (0x22ffffff) : kept ? colour : colour.withAlpha (0.28f));
            g.fillRoundedRectangle (x, wave.getCentreY() - h * 0.5f, juce::jmax (1.2f, pitch - 0.8f), h, 0.6f);
        }
    }
    if (shape)
    {
        // IN · BAR 3 on top of its line, OUT · BAR 7 under its own
        for (int h = 0; h < 2; ++h)
        {
            const float x = beatX (h == 0 ? trimFrom : trimTo);
            g.setColour (col::amber);
            g.fillRect (x - 1.5f, wave.getY() - 8, 3.0f, wave.getHeight() + 16);
            const auto text = juce::String (h == 0 ? "IN" : "OUT") + U8(" \xc2\xb7 ") + placeText (h == 0 ? trimFrom : trimTo);
            auto f = spaced (condensedBold (12.5f), 0.06f);
            const float tw = juce::GlyphArrangement::getStringWidth (f, text) + 12.0f;
            float fx = h == 0 ? x - 1.5f : x + 1.5f - tw;
            fx = juce::jlimit (box.getX() + 2, box.getRight() - 2 - tw, fx);
            auto flag = juce::Rectangle<float> (fx, h == 0 ? wave.getY() - 24 : wave.getBottom() + 6, tw, 17);
            g.fillRoundedRectangle (flag, 4.0f);
            g.setColour (col::onBright);
            g.setFont (f);
            g.drawText (text, flag, juce::Justification::centred);
        }
    }
    else if (warp)
        paintWarp (g, wave);
    else
    {
        // the moves of one knob (tap to see another)
        auto lane = laneArea();
        g.setColour (juce::Colour (0xff221b2d));
        g.fillRoundedRectangle (lane, 5.0f);
        const bool has = loops.hasMoves (current, laneKnob);
        g.setColour (has ? kKnobColours[laneKnob] : juce::Colour (0xffb7a9c2));
        g.setFont (spaced (condensedBold (11.0f), 0.1f));
        const bool anyMoves = loops.hasAnyMoves (current);
        g.drawText (juce::String (fxKnobName (laneKnob)) + " MOVES" + (has ? juce::String() : juce::String (anyMoves ? "  (TAP: NEXT KNOB)" : U8("  \xc2\xb7 NONE YET: REC MOVES, THEN TURN A KNOB"))),
                    lane.reduced (6, 2), juce::Justification::topLeft);
        if (has)
        {
            const auto m = loops.moves (current, laneKnob);
            juce::Path p;
            for (int i = 0; i <= 128; ++i)
            {
                const float v = m[(size_t) juce::jmin (kMoveBins - 1, i * kMoveBins / 128)];
                const float x = lane.getX() + lane.getWidth() * (float) i / 128.0f, y = lane.getBottom() - 3 - v * (lane.getHeight() - 6);
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (kKnobColours[laneKnob]);
            g.strokePath (p, juce::PathStrokeType (2.2f));
        }
        if (loops.transportRunning())
        {
            g.setColour (loops.recMoves (current) ? col::red : juce::Colours::white.withAlpha (0.5f));
            g.fillRect (lane.getX() + loops.position (current) * lane.getWidth() - 1.0f, lane.getY(), 2.0f, lane.getHeight());
        }
    }
    if (loops.transportRunning())
    {
        const float play = wave.getX() + loops.position (current) * wave.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (play - 1.0f, wave.getY() - 4, 2.0f, wave.getHeight() + 8);
    }

    if (warp)
    {
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (13.0f), 0.12f));
        g.drawText ("FIT BARS", fitLabel, juce::Justification::centredLeft);
        g.drawText ("STRETCH", stretchLabel, juce::Justification::centredLeft);
        g.setFont (lcdFont (15.0f));
        g.drawText (juce::String ((int) hits.size()) + (hits.size() == 1 ? " hit" : " hits"), hitsRect, juce::Justification::centred);
        g.setFont (condensedBold (16.0f));
        g.drawText (pitchText(), pitchRect, juce::Justification::centred);
        g.drawText ("Formant " + signedSemis (plan.settings.formant), formantRect, juce::Justification::centred);
        g.setColour (col::inkDim);
        g.setFont (small (13.0f));
        g.drawFittedText (infoText(), infoRect.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        if (busy())
        {
            g.setColour (col::amber.darker (0.3f));
            g.setFont (spaced (condensedBold (13.0f), 0.08f));
            g.drawText (U8("Warping\xe2\x80\xa6"), infoRect, juce::Justification::centredRight);
        }
        g.setColour (col::inkDim);
        g.setFont (small (12.5f));
        g.drawFittedText (U8("Drag a marker onto a beat \xc2\xb7 tap a hit to pin it \xc2\xb7 double-tap to add or remove"), hintRect.toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
        return;
    }
    if (shape)
    {
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (13.0f), 0.12f));
        g.drawText ("SNAP", juce::Rectangle<float> (16, 178, 40, 40), juce::Justification::centredLeft);

        // BEAT MATCH
        auto fb = fitBox;
        g.setColour (col::creamLight);
        g.fillRoundedRectangle (fb, 12.0f);
        g.setColour (flash / 100 == fitFocus ? col::amber : col::rule);
        g.drawRoundedRectangle (fb.reduced (0.5f), 12.0f, flash / 100 == fitFocus ? 2.5f : 1.2f);
        const double bars = regionBars();
        const auto barsText = juce::String (bars, 1) + (std::abs (bars - 1.0) < 0.05 ? " bar" : " bars");
        const auto bpmText = juce::String (loops.bpm(), 0) + " BPM";
        boxTitle (g, fb.reduced (10, 0).withHeight (26).translated (0, 1), "BEAT MATCH",
                  markersMoved() ? U8("\xc2\xb7 IN-OUT is ") + barsText + " long at " + bpmText
                                 : (choices.exact ? U8("\xc2\xb7 this take is exactly ") + juce::String (choices.nearest) + (choices.nearest == 1 ? " bar" : " bars") + " at " + bpmText
                                                  : U8("\xc2\xb7 this take is ") + barsText + " long at " + bpmText));
        if (choices.exact && ! markersMoved())
        {
            g.setColour (col::inkDim);
            g.setFont (small (13.0f));
            g.drawFittedText (U8("On the bars already \xe2\x9c\x93"), juce::Rectangle<int> (24, (int) fb.getY() + 26, fitButtons[1].getX() - 30, 42), juce::Justification::centredLeft, 2, 0.8f);
        }
        g.setColour (col::inkDim);
        g.setFont (small (12.5f));
        g.drawFittedText ("Stretches the sound so it lands exactly on the bars, without changing its pitch.",
                          fb.reduced (10, 0).withTrimmedTop (72).toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);

        if (! quantBox.isEmpty())
        {
            auto qb = quantBox;
            g.setColour (col::creamLight);
            g.fillRoundedRectangle (qb, 12.0f);
            g.setColour (flash / 100 == quantizeFocus ? col::amber : col::rule);
            g.drawRoundedRectangle (qb.reduced (0.5f), 12.0f, flash / 100 == quantizeFocus ? 2.5f : 1.2f);
            juce::String rest = U8("\xc2\xb7 played on ");
            if (take != nullptr && take->part != nullptr)
                rest << take->part->describe();
            rest << (take != nullptr && take->quant.grid > 0 ? U8(" \xc2\xb7 tap the lit grid for as played") : U8(" \xc2\xb7 as played now"));
            boxTitle (g, qb.reduced (10, 0).withHeight (28).translated (0, 1).withTrimmedRight (hasNotes() ? 110.0f : 0.0f), "QUANTIZE", rest);
        }
        if (busy())
        {
            auto cover = fitBox.getUnion (quantBox.isEmpty() ? fitBox : quantBox);
            g.setColour (col::cream.withAlpha (0.85f));
            g.fillRoundedRectangle (cover, 12.0f);
            g.setColour (col::ink);
            g.setFont (spaced (condensedBold (18.0f), 0.1f));
            g.drawText (U8("WORKING\xe2\x80\xa6"), cover, juce::Justification::centred);
        }
        return;
    }

    // the knobs
    drawWell (g, knobPanel(), 12.0f);
    for (int k = 0; k < kFxKnobs; ++k)
    {
        auto cell = knobCell (k);
        const float v = loops.shownKnob (current, k);
        const bool active = std::abs (v - fxDefault (k)) > 0.01f || k == fxVolume;
        g.setColour (juce::Colour (0xffe4dbee));
        g.setFont (spaced (condensed (13.0f), 0.1f));
        g.drawText (fxKnobName (k), cell.withHeight (20).translated (0, 4), juce::Justification::centred);
        auto kr = cell.withSizeKeepingCentre (54, 54).translated (0, 1);
        drawKnob (g, kr, v, kKnobColours[k], active, true);
        if (dragging == 10 + k)
        {
            g.setColour (kKnobColours[k].withAlpha (0.5f));
            g.drawEllipse (kr.expanded (2), 2.0f);
        }
        if (loops.hasMoves (current, k))
        {
            g.setColour (loops.recMoves (current) && dragging == 10 + k ? col::red : kKnobColours[k]);
            g.fillEllipse (kr.getRight() - 6, kr.getY() - 2, 8, 8);
        }
        g.setColour (juce::Colour (0xfff59a2a));
        g.setFont (lcdFont (16.0f));
        g.drawText (fxValueText (k, v), cell.withTrimmedTop (cell.getHeight() - 24), juce::Justification::centred);
    }

    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    juce::String hint;
    if (loops.recMoves (current))
        hint = loops.transportRunning() ? "Recording moves: turn a knob, let go to hear it back." : "Press Play: moves record while the loop plays.";
    else if (loops.hasAnyMoves (current))
        hint = "Knobs with a dot play their moves. Touch one to take over.";
    else
        hint = "Double-tap a knob to reset it. Moves and effects stay with the layer through trims, fits and quantize.";
    g.drawFittedText (hint, juce::Rectangle<int> (16, undoFx.getBottom() + 6, getWidth() - 32, 38), juce::Justification::centredLeft, 2, 0.9f);
}

//==============================================================================
void LoopEditSheet::mouseDown (const juce::MouseEvent& e)
{
    dragging = 0;
    if (current < 0)
        return;
    const auto p = e.position;
    if (pageShown == warpPage)
    {
        warpDrag = tapHit = -1;
        if (! waveBox().contains (p) || ! plan.isValid())
            return;
        if (const int m = markerNear (p.x, plan); m >= 0)
        {
            warpDrag = m;
            dragPlan = plan;
        }
        else if (p.y > waveArea().getBottom() - 8)
            tapHit = hitNear (p.x);
        return;
    }
    if (pageShown == shapePage)
    {
        if (waveBox().contains (p) && ! busy())
        {
            const float a = beatX (trimFrom), b = beatX (trimTo);
            dragging = std::abs (p.x - a) < std::abs (p.x - b) ? 1 : 2;
            mouseDrag (e);
        }
        return;
    }
    if (laneArea().contains (p))
    {
        for (int i = 1; i <= kFxKnobs; ++i)
        {
            const int k = (laneKnob + i) % kFxKnobs;
            if (loops.hasMoves (current, k) || ! loops.hasAnyMoves (current))
            {
                laneKnob = k;
                break;
            }
        }
        repaint();
        return;
    }
    const int k = knobAt (p);
    if (k >= 0)
    {
        dragging = 10 + k;
        laneKnob = k;
        dragStartValue = loops.shownKnob (current, k);
        loops.setKnob (current, k, dragStartValue);
        loops.touchKnob (current, k, true);
        repaint();
    }
}

void LoopEditSheet::mouseDrag (const juce::MouseEvent& e)
{
    if (pageShown == warpPage)
    {
        if (warpDrag >= 0 && warpDrag < (int) plan.markers.size())
        {
            dragPlan = plan;
            crate::Warp::moveMarker (dragPlan, warpDrag, gridSnapped (xBeat (e.position.x)));
            repaint();
        }
        return;
    }
    if (dragging == 1 || dragging == 2)
    {
        const double sound = contentBeats();
        const double gap = juce::jmax (0.25, snapBeats (snap));
        double b = juce::jlimit (0.0, sound, snapBeat (xBeat (e.position.x), snap));
        if (b > sound - gap * 0.5 && dragging == 2)
            b = sound;   // the end of the sound is always a place OUT can go
        if (dragging == 1)
            trimFrom = juce::jlimit (0.0, juce::jmax (0.0, trimTo - gap), b);
        else
            trimTo = juce::jlimit (juce::jmin (sound, trimFrom + gap), sound, b);
        updateRegion();
        repaint();
    }
    else if (dragging >= 10)
    {
        const int k = dragging - 10;
        const float v = juce::jlimit (0.0f, 1.0f, dragStartValue - (float) e.getDistanceFromDragStartY() / 160.0f
                                                       + (float) e.getDistanceFromDragStartX() / 400.0f);
        loops.setKnob (current, k, v);
        repaint();
    }
}

void LoopEditSheet::mouseUp (const juce::MouseEvent& e)
{
    if (pageShown == warpPage)
    {
        if (warpDrag >= 0 && e.mouseWasDraggedSinceMouseDown())
        {
            plan = dragPlan;
            commitWarp();
        }
        else if (tapHit >= 0 && tapHit < (int) hits.size() && ! e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() == 1)
        {
            // pin the hit where it plays now
            crate::Warp::addMarker (plan, plan.targetAt (hits[(size_t) tapHit]));
            commitWarp();
        }
        warpDrag = tapHit = -1;
        repaint();
        return;
    }
    if (dragging >= 10)
    {
        loops.recordMoves();   // the last bit up to the finger lifting
        loops.touchKnob (current, dragging - 10, false);
        if (onFxChanged) onFxChanged();
    }
    dragging = 0;
    repaint();
}

void LoopEditSheet::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (pageShown == warpPage)
    {
        if (current < 0 || ! plan.isValid() || ! waveBox().contains (e.position))
            return;
        warpDrag = tapHit = -1;
        if (const int m = markerNear (e.position.x, plan); m >= 0)
            crate::Warp::removeMarker (plan, m);
        else
        {
            const double t = xBeat (e.position.x);
            if (t <= 0.02 || t >= plan.targetBeats - 0.02)
                return;
            crate::Warp::addMarker (plan, t);
        }
        commitWarp();
        return;
    }
    if (pageShown != effectsPage)
        return;
    const int k = knobAt (e.position);
    if (k < 0 || current < 0 || loops.recMoves (current))
        return;
    loops.setKnob (current, k, fxDefault (k));
    if (onFxChanged) onFxChanged();
    repaint();
}

//==============================================================================
// WARP
void LoopEditSheet::readWarp()
{
    plan = {};
    hits.clear();
    soundsLike = 0.0;
    warpSrc = {};
    auto take = current >= 0 ? loops.takeOf (current) : nullptr;
    if (take == nullptr)
        return;
    warpSrc = warpSourceOf (*take);
    plan = warpSrc.plan;
    if (! plan.isValid())
        return;
    hits = warpHits (warpSrc, take->sampleRate, sensitivity);
    soundsLike = warpSoundsLike (warpSrc, hits, take->sampleRate, loops.bpm());
    snapOn = false;
    for (auto& m : plan.markers)
        snapOn = snapOn || ! m.manual;
}

void LoopEditSheet::commitWarp()
{
    if (onWarp && plan.isValid())
        onWarp (plan);
    refreshButtons();
    repaint();
}

void LoopEditSheet::resnap()
{
    crate::Warp::snap (plan, hits, gridBeats, snapOn ? strength : 0.0f);
}

void LoopEditSheet::pressWarpSnap()
{
    if (current < 0 || ! plan.isValid())
        return;
    snapOn = ! snapOn;
    resnap();
    commitWarp();
}

void LoopEditSheet::setGrid (int i)
{
    const double grids[] = { 1.0, 0.5, 0.25, 0.125 };
    gridBeats = grids[juce::jlimit (0, 3, i)];
    if (snapOn)
    {
        resnap();
        commitWarp();
    }
    else
        refreshButtons();
}

void LoopEditSheet::setStrength (int i)
{
    const float strengths[] = { 0.5f, 0.75f, 1.0f };
    strength = strengths[juce::jlimit (0, 2, i)];
    if (snapOn)
    {
        resnap();
        commitWarp();
    }
    else
        refreshButtons();
}

void LoopEditSheet::stepHits (int dir)
{
    if (current < 0 || ! plan.isValid())
        return;
    sensitivity = juce::jlimit (0.0f, 1.0f, sensitivity + (float) dir * 0.15f);
    auto take = loops.takeOf (current);
    if (take == nullptr)
        return;
    hits = warpHits (warpSrc, take->sampleRate, sensitivity);
    soundsLike = warpSoundsLike (warpSrc, hits, take->sampleRate, loops.bpm());
    if (snapOn)
    {
        resnap();
        commitWarp();
    }
    repaint();
}

void LoopEditSheet::pressFitBars (int bars)
{
    if (current < 0 || ! plan.isValid() || bars < 1)
        return;
    crate::Warp::setLength (plan, bars * 4.0);
    if (snapOn)
        resnap();
    commitWarp();
}

void LoopEditSheet::pressStyle (crate::Warp::Style s)
{
    if (current < 0 || ! plan.isValid())
        return;
    plan.settings.style = s;
    commitWarp();
}

void LoopEditSheet::stepPitch (int semis)
{
    if (current < 0 || ! plan.isValid())
        return;
    plan.settings.transpose = juce::jlimit (-12.0f, 12.0f, plan.settings.transpose + (float) semis);
    commitWarp();
}

void LoopEditSheet::stepFine (int cents)
{
    if (current < 0 || ! plan.isValid())
        return;
    plan.settings.transpose = juce::jlimit (-12.0f, 12.0f, plan.settings.transpose + (float) cents / 100.0f);
    commitWarp();
}

void LoopEditSheet::stepFormant (int semis)
{
    if (current < 0 || ! plan.isValid())
        return;
    plan.settings.formant = juce::jlimit (-12.0f, 12.0f, plan.settings.formant + (float) semis);
    commitWarp();
}

void LoopEditSheet::dragWarpMarker (int index, double targetBeat)
{
    if (index < 0 || index >= (int) plan.markers.size())
        return;
    crate::Warp::moveMarker (plan, index, targetBeat);
    commitWarp();
}

void LoopEditSheet::resetWarp()
{
    if (current < 0 || ! plan.isValid())
        return;
    const double len = plan.sourceBeats;
    plan = {};
    plan.sourceBeats = plan.targetBeats = len;
    snapOn = false;
    commitWarp();
}

void LoopEditSheet::pressOriginal()
{
    if (current < 0 || ! onOriginal || ! isOriginal)
        return;
    onOriginal (! isOriginal());
    refreshButtons();
    repaint();
}

int LoopEditSheet::pressPrint()
{
    if (current < 0 || ! onPrint)
        return -1;
    const int to = onPrint();
    if (onToast)
        onToast (to >= 0 ? "Printed onto layer " + juce::String (to + 1) + "." : "No empty layer to print onto.");
    return to;
}

double LoopEditSheet::gridSnapped (double beat) const
{
    beat = juce::jlimit (0.0, plan.targetBeats, beat);
    const double g = std::round (beat / gridBeats) * gridBeats;
    return std::abs (beatX (g) - beatX (beat)) <= 12.0f ? g : beat;
}

int LoopEditSheet::markerNear (float x, const crate::Warp::Plan& p) const
{
    int best = -1;
    float bestD = 18.0f;
    for (size_t i = 0; i < p.markers.size(); ++i)
    {
        const float d = std::abs (beatX (p.markers[i].target) - x);
        if (d < bestD)
        {
            bestD = d;
            best = (int) i;
        }
    }
    return best;
}

int LoopEditSheet::hitNear (float x) const
{
    int best = -1;
    float bestD = 14.0f;
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const float d = std::abs (beatX (plan.targetAt (hits[i])) - x);
        if (d < bestD)
        {
            bestD = d;
            best = (int) i;
        }
    }
    return best;
}

juce::String LoopEditSheet::pitchText() const
{
    const float t = plan.settings.transpose;
    const int st = juce::roundToInt (t), ct = juce::roundToInt ((t - (float) st) * 100.0f);
    auto sign = [] (int v) { return v < 0 ? juce::String (U8("\xe2\x88\x92")) : v > 0 ? "+" : ""; };
    return "Pitch " + sign (st) + juce::String (std::abs (st)) + " st " + sign (ct) + juce::String (std::abs (ct)) + " ct";
}

juce::String LoopEditSheet::infoText() const
{
    const double bars = plan.targetBeats / 4.0;
    const juce::String dot = U8(" \xc2\xb7 ");
    juce::String s = soundsLike > 0.0 ? "Sounds like " + juce::String (soundsLike, 1) + " BPM" + dot + "song " + juce::String (loops.bpm(), 0)
                                       : juce::String ("Tempo: can't tell from its hits");
    s << dot << (int) hits.size() << (hits.size() == 1 ? " hit" : " hits") << dot << (int) plan.markers.size() << (plan.markers.size() == 1 ? " marker" : " markers")
      << dot << juce::String (bars, bars == std::floor (bars) ? 0 : 2) << (bars == 1.0 ? " bar" : " bars");
    return s;
}

// The warp page's wave: beat lines, the hits, the markers (and where each would play unwarped).
void LoopEditSheet::paintWarp (juce::Graphics& g, juce::Rectangle<float> wave)
{
    const auto& shown = warpDrag >= 0 ? dragPlan : plan;
    const double total = totalBeats();
    auto box = waveBox();
    g.setColour (juce::Colour (0x22ffffff));
    for (int b = 1; (double) b < total - 1.0e-6; ++b)
        if (b % 4 != 0)
            g.fillRect (beatX (b) - 0.5f, wave.getY(), 1.0f, wave.getHeight());
    if (! plan.isValid())
        return;
    // the hits: small ticks along the bottom (amber once pinned)
    for (double h : hits)
    {
        bool pinned = false;
        for (auto& m : shown.markers)
            pinned = pinned || std::abs (m.source - h) < 0.03;
        const float x = beatX (shown.targetAt (h));
        g.setColour (pinned ? col::amber : juce::Colours::white.withAlpha (0.6f));
        g.fillRoundedRectangle (x - 1.5f, wave.getBottom() + 4, 3.0f, 12.0f, 1.2f);
    }
    // the markers: amber line, a handle on top, a dashed ghost where its sound would sit unwarped
    const double scale = shown.sourceBeats > 0 ? shown.targetBeats / shown.sourceBeats : 1.0;
    for (size_t i = 0; i < shown.markers.size(); ++i)
    {
        const auto& m = shown.markers[i];
        const float gx = beatX (m.source * scale), x = beatX (m.target);
        if (std::abs (gx - x) > 2.0f)
        {
            const float dashes[] = { 3.0f, 3.0f };
            g.setColour (col::amber.withAlpha (0.55f));
            g.drawDashedLine (juce::Line<float> (gx, wave.getY() - 2, gx, wave.getBottom() + 2), dashes, 2, 1.2f);
        }
        g.setColour (col::amber);
        g.fillRect (x - 1.25f, wave.getY() - 4, 2.5f, wave.getHeight() + 8);
        auto handle = juce::Rectangle<float> (x - 11, box.getY() + 4, 22, 20);
        g.fillRoundedRectangle (handle, 5.0f);
        g.setColour (col::ink.withAlpha (0.7f));
        for (int k = -1; k <= 1; ++k)
            g.fillRect (x + (float) k * 4.0f - 0.75f, handle.getY() + 5, 1.5f, 10.0f);
        if (! m.manual)
            continue;
        g.setColour (col::ink);
        g.fillEllipse (x - 2, wave.getBottom() + 1, 4, 4);
    }
}
