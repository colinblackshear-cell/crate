#include "LoopEditSheet.h"

using namespace pocket;

namespace
{
const juce::Colour kKnobColours[kFxKnobs] = { juce::Colour (0xfff5b13d), juce::Colour (0xff5fb3c4), juce::Colour (0xff9a7fd0),
                                              juce::Colour (0xffb9a3dc), juce::Colour (0xfff0674f) };
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
                     &reverseButton, &undo, &recMoves, &clearMoves, &undoFx, &fullBoard })
        addAndMakeVisible (*b);
    for (auto& b : snaps) addAndMakeVisible (b);
    for (auto& b : fitButtons) addAndMakeVisible (b);
    for (auto& b : grids) addAndMakeVisible (b);
    for (auto* b : { &done, &tabShape, &tabFx, &halfButton, &doubleButton, &reverseButton, &undo, &recMoves, &clearMoves, &undoFx, &trimButton, &fullBoard })
        b->setFontHeight (16.0f);
    fullBoard.onClick = [this] { if (onFullBoard) onFullBoard(); };

    done.onClick = [this] { if (onDone) onDone(); };
    tabShape.onClick = [this] { showPage (shapePage); };
    tabFx.onClick = [this] { showPage (effectsPage); };
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
    const bool shape = pageShown == shapePage;
    done.setBounds (juce::Rectangle<float> (w - 88, 10, 72, 38).toNearestInt());
    tabFx.setBounds (juce::Rectangle<float> (done.getX() - 8 - 78, 10, 78, 38).toNearestInt());
    tabShape.setBounds (juce::Rectangle<float> (tabFx.getX() - 4 - 70, 10, 70, 38).toNearestInt());
    tabShape.setLook (shape ? Look::dark : Look::cream);
    tabFx.setLook (shape ? Look::cream : Look::dark);
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
    for (auto* b : { &recMoves, &clearMoves, &undoFx, &fullBoard })
        b->setVisible (! shape);

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
    const bool shape = pageShown == shapePage;
    g.setColour (col::ink);
    g.setFont (condensedBold (24.0f));
    g.drawFittedText ("Layer " + juce::String (current + 1) + U8(" \xc2\xb7 ") + (take != nullptr ? titleCase (layerToolName (take->tool)) : juce::String()),
                      juce::Rectangle<int> (16, 10, tabShape.getX() - 22, 38), juce::Justification::centredLeft, 1, 0.7f);

    // the wave, always: bar numbers, IN and OUT (or the moves lane), where it's playing
    auto box = waveBox();
    drawWell (g, box, 12.0f);
    auto wave = waveArea();
    const auto colour = take != nullptr ? layerToolColour (take->tool) : kKnobColours[fxFilter];
    const double sound = contentBeats();
    if (shape)
    {
        g.setFont (lcdFont (15.0f));
        for (int bar = 0; bar * 4.0 < total - 1.0e-6; ++bar)
        {
            const float x = beatX (bar * 4.0);
            g.setColour (juce::Colour (0x40ffffff));
            g.fillRect (x, box.getY() + 6, 1.0f, wave.getBottom() - box.getY() - 6);
            if (total <= 64.0 || bar % 4 == 0)
            {
                g.setColour (juce::Colour (0xffa89a86));
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
            g.setColour (col::ink);
            g.setFont (f);
            g.drawText (text, flag, juce::Justification::centred);
        }
    }
    else
    {
        // the moves of one knob (tap to see another)
        auto lane = laneArea();
        g.setColour (juce::Colour (0xff2a241f));
        g.fillRoundedRectangle (lane, 5.0f);
        const bool has = loops.hasMoves (current, laneKnob);
        g.setColour (has ? kKnobColours[laneKnob] : juce::Colour (0xffa89a86));
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
            boxTitle (g, qb.reduced (10, 0).withHeight (28).translated (0, 1), "QUANTIZE", rest);
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
        g.setColour (juce::Colour (0xffe8dfc9));
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
        g.setColour (juce::Colour (0xffe8a24a));
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

void LoopEditSheet::mouseUp (const juce::MouseEvent&)
{
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
    if (pageShown != effectsPage)
        return;
    const int k = knobAt (e.position);
    if (k < 0 || current < 0 || loops.recMoves (current))
        return;
    loops.setKnob (current, k, fxDefault (k));
    if (onFxChanged) onFxChanged();
    repaint();
}
