#include "LoopEditSheet.h"

using namespace pocket;

namespace
{
const juce::Colour kKnobColours[kFxKnobs] = { juce::Colour (0xfff5b13d), juce::Colour (0xff5fb3c4), juce::Colour (0xff9a7fd0),
                                              juce::Colour (0xffb9a3dc), juce::Colour (0xfff0674f) };
const char* const kSources[7] = { "Mic", "Drums", "Vocoder", "Mix", "Voice + vocoder", "Synth", "Chords" };
constexpr int kWaveBins = 140;

bool hasLoop (LoopState s) { return s == LoopState::playing || s == LoopState::overdubWait || s == LoopState::overdubbing; }

juce::String clock (double seconds)
{
    const int s = juce::roundToInt (seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}
}

LoopEditSheet::LoopEditSheet (LoopsEngine& l) : loops (l)
{
    for (auto* b : { &done, &trimButton, &halfButton, &doubleButton, &reverseButton, &recMoves, &clearMoves, &undo })
    {
        addAndMakeVisible (*b);
        b->setFontHeight (16.0f);
    }
    done.onClick = [this] { if (onDone) onDone(); };
    trimButton.onClick = [this]
    {
        const double total = totalBeats();
        if (trimFrom <= 0.0 && trimTo >= total - 1.0e-6)
            return onToast ("Drag the yellow handles on the wave in, then TRIM keeps what's between them.");
        if (onEdit)
            onEdit (trim, trimFrom, trimTo);
    };
    halfButton.onClick = [this] { if (onEdit) onEdit (half, 0, 0); };
    doubleButton.onClick = [this] { if (onEdit) onEdit (twice, 0, 0); };
    reverseButton.onClick = [this] { if (onEdit) onEdit (reverse, 0, 0); };
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
    undo.onClick = [this] { if (onUndo) onUndo(); };
}

void LoopEditSheet::open (int l)
{
    if (current >= 0 && current != l)
        loops.setRecMoves (current, false);
    current = l;
    version = -1;
    for (int k = 0; k < kFxKnobs; ++k)
        if (loops.hasMoves (l, k))
        {
            laneKnob = k;
            break;
        }
    layerChanged();
    startTimerHz (30);
}

void LoopEditSheet::layerChanged()
{
    peaks = loops.peaks (current, kWaveBins);
    version = loops.version (current);
    trimFrom = 0;
    trimTo = totalBeats();
    repaint();
}

double LoopEditSheet::totalBeats() const
{
    auto t = current >= 0 ? loops.takeOf (current) : nullptr;
    return t != nullptr ? t->lengthBeats.load() : 0.0;
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
        loops.setRecMoves (current, false);
        if (onDone) onDone();
        return;
    }
    if (loops.version (current) != version)
        layerChanged();
    loops.recordMoves();
    const bool rec = loops.recMoves (current);
    recMoves.setText (rec ? U8("\xe2\x96\xa0 Stop rec") : U8("\xe2\x97\x8f Rec moves"));
    recMoves.setLook (rec ? Look::amber : Look::red);
    repaint();
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
void LoopEditSheet::resized()
{
    const float w = (float) getWidth();
    done.setBounds (juce::Rectangle<float> (w - 96, 12, 80, 34).toNearestInt());
    const float bw = (w - 32 - 18) / 4.0f;
    int i = 0;
    for (auto* b : { &trimButton, &halfButton, &doubleButton, &reverseButton })
        b->setBounds (juce::Rectangle<float> (16 + (float) i++ * (bw + 6), 178, bw, 40).toNearestInt());
    recMoves.setBounds (juce::Rectangle<float> (16, 346, (w - 40) * 0.55f, 46).toNearestInt());
    clearMoves.setBounds (juce::Rectangle<float> (recMoves.getRight() + 8, 346, w - 16 - recMoves.getRight() - 8, 46).toNearestInt());
    undo.setBounds (16, 400, 80, 36);
}

void LoopEditSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    g.setColour (col::rule);
    g.fillRoundedRectangle ((float) getWidth() / 2 - 20, 6, 40, 5, 2.5f);
    if (current < 0)
        return;

    auto take = loops.takeOf (current);
    const double total = totalBeats();
    g.setColour (col::ink);
    g.setFont (condensedBold (25.0f));
    g.drawText ("Layer " + juce::String (current + 1) + U8(" \xc2\xb7 ") + (take != nullptr ? kSources[juce::jlimit (0, 6, take->source)] : ""),
                juce::Rectangle<float> (18, 12, (float) getWidth() - 130, 34), juce::Justification::centredLeft);

    // the wave, the trim handles and where it's playing
    auto box = waveBox();
    drawWell (g, box, 12.0f);
    {
        const double secs = take != nullptr ? take->length.load() / take->sampleRate : 0.0;
        const double bars = total / 4.0;
        juce::String info = juce::String (bars, bars == std::floor (bars) ? 0 : 2) + (bars == 1.0 ? " BAR" : " BARS") + U8(" \xc2\xb7 ") + clock (secs) + U8(" \xc2\xb7 MAX 10 MIN");
        if (trimFrom > 0 || trimTo < total - 1.0e-6)
            info = "KEEP BEATS " + juce::String (juce::roundToInt (trimFrom) + 1) + "-" + juce::String (juce::roundToInt (trimTo)) + U8(" \xc2\xb7 TAP TRIM");
        g.setColour (col::lcdAmber);
        g.setFont (lcdFont (19.0f));
        g.drawText (info, box.withTrimmedLeft (10).withTrimmedRight (10).withHeight (22).translated (0, 2), juce::Justification::centredLeft);
    }
    auto wave = waveArea();
    const auto colour = kKnobColours[fxFilter].brighter (0.2f);
    if (! peaks.empty())
    {
        const float pitch = wave.getWidth() / (float) peaks.size();
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float x = wave.getX() + (float) i * pitch;
            const double beat = (x + pitch * 0.5f - wave.getX()) / wave.getWidth() * total;
            const bool kept = beat >= trimFrom && beat <= trimTo;
            const float p = std::sqrt (juce::jlimit (0.0f, 1.0f, peaks[i] * 1.6f));
            const float h = juce::jmax (2.0f, p * wave.getHeight());
            g.setColour (kept ? colour : colour.withAlpha (0.25f));
            g.fillRoundedRectangle (x, wave.getCentreY() - h * 0.5f, juce::jmax (1.2f, pitch - 0.8f), h, 0.6f);
        }
    }
    // beat ticks
    g.setColour (juce::Colour (0x22ffffff));
    for (int b = 1; b < (int) std::ceil (total); ++b)
        g.fillRect (beatX (b) - 0.5f, wave.getBottom() + 1, 1.0f, b % 4 == 0 ? 5.0f : 2.5f);
    for (int h = 0; h < 2; ++h)
    {
        const float x = beatX (h == 0 ? trimFrom : trimTo);
        g.setColour (col::amber);
        g.fillRect (x - 1.5f, wave.getY() - 4, 3.0f, wave.getHeight() + 8);
        g.fillRoundedRectangle (h == 0 ? x - 1.5f : x - 12.5f, wave.getY() - 6, 14, 12, 3.0f);
    }
    const float play = wave.getX() + loops.position (current) * wave.getWidth();
    if (loops.transportRunning())
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (play - 1.0f, wave.getY(), 2.0f, wave.getHeight());
    }

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

    // a line on what to do
    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    juce::String hint;
    if (loops.recMoves (current))
        hint = loops.transportRunning() ? "Recording moves: turn a knob, let go to hear it back." : "Press Play: moves record while the loop plays.";
    else if (loops.hasAnyMoves (current))
        hint = "Knobs with a dot play their moves. Touch one to take over.";
    else
        hint = "Double-tap a knob to reset it. Undo takes back edits.";
    g.drawFittedText (hint, juce::Rectangle<int> (106, 398, getWidth() - 122, 40), juce::Justification::centredLeft, 2, 0.9f);
}

//==============================================================================
void LoopEditSheet::mouseDown (const juce::MouseEvent& e)
{
    dragging = 0;
    if (current < 0)
        return;
    const auto p = e.position;
    auto wave = waveArea().expanded (0, 12);
    if (wave.contains (p))
    {
        const float a = beatX (trimFrom), b = beatX (trimTo);
        dragging = std::abs (p.x - a) < std::abs (p.x - b) ? 1 : 2;
        mouseDrag (e);
        return;
    }
    if (laneArea().contains (p))
    {
        // next knob that has moves (or simply the next knob)
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
        const double total = totalBeats();
        const double b = juce::jlimit (0.0, total, (double) juce::roundToInt (xBeat (e.position.x)));
        if (dragging == 1)
            trimFrom = juce::jlimit (0.0, juce::jmax (0.0, trimTo - 1.0), b);
        else
            trimTo = juce::jlimit (juce::jmin (total, trimFrom + 1.0), total, b >= std::floor (total) ? total : b);
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
    const int k = knobAt (e.position);
    if (k < 0 || current < 0 || loops.recMoves (current))
        return;
    loops.setKnob (current, k, fxDefault (k));
    if (onFxChanged) onFxChanged();
    repaint();
}
