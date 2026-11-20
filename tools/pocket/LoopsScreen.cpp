#include "LoopsScreen.h"

#include "core/AudioFiles.h"

using namespace pocket;

namespace
{
constexpr float kRowTop = 205.0f, kRowPitch = 46.7f, kRowHeight = 42.0f;
constexpr int kPeakBins = 45;
const int kBarChoices[4] = { 1, 2, 4, 8 };
const char* const kSourceNames[4] = { "MIC", "DRUMS", "VOCODER", "MIX" };
const char* const kLengthNames[5] = { "1", "2", "4", "8", "FREE" };

const juce::Colour kRowGreen { 0xff82cf6a }, kRowRed { 0xfff0674f }, kRowAmber { 0xfff5b13d }, kRowBlue { 0xff56b3d6 },
                   kRowGrey { 0xff8a8174 }, kRowText { 0xffe8dfc9 };

juce::Colour stateColour (LoopState s, bool muted)
{
    if (muted && s != LoopState::empty && s != LoopState::waiting && s != LoopState::recording)
        return kRowBlue;
    switch (s)
    {
        case LoopState::playing:
        case LoopState::overdubWait: return kRowGreen;
        case LoopState::recording:
        case LoopState::overdubbing: return kRowRed;
        case LoopState::waiting: return kRowAmber;
        default: return kRowGrey;
    }
}

juce::String stateLabel (LoopState s, bool muted)
{
    if (muted && s != LoopState::empty && s != LoopState::waiting && s != LoopState::recording)
        return "MUTED";
    switch (s)
    {
        case LoopState::playing:
        case LoopState::overdubWait: return "PLAYING";
        case LoopState::recording: return "REC";
        case LoopState::overdubbing: return "DUBBING";
        case LoopState::waiting: return "NEXT BAR";
        default: return "EMPTY";
    }
}

bool hasLoop (LoopState s) { return s == LoopState::playing || s == LoopState::overdubWait || s == LoopState::overdubbing; }
bool isBusy (LoopState s) { return s == LoopState::waiting || s == LoopState::recording || s == LoopState::overdubbing; }
}

//==============================================================================
LoopsScreen::LoopsScreen (PocketHost& h, Library& l, BeatsEngine& b, AppSettings& s)
    : host (h), library (l), beats (b), settings (s), loops (b)
{
    loops.setCountIn (settings.countIn);
    restore();
    startTimerHz (20);
}

LoopsScreen::~LoopsScreen()
{
    stopTimer();
    save();
}

int LoopsScreen::layerCount() const
{
    int n = 0;
    for (int i = 0; i < kLoopLayers; ++i)
        n += hasLoop (loops.state (i)) ? 1 : 0;
    return n;
}

void LoopsScreen::leave()
{
    loops.setTransport (false);
    beats.setPlaying (false);
}

juce::Rectangle<float> LoopsScreen::rowRect (int l) const { return { 16.0f, kRowTop + (float) l * kRowPitch, (float) getWidth() - 32.0f, kRowHeight }; }
juce::Rectangle<float> LoopsScreen::actRect (int l) const { auto r = rowRect (l); return { r.getRight() - 106.0f, r.getY() + 3, 58, 36 }; }
juce::Rectangle<float> LoopsScreen::muteRect (int l) const { auto r = rowRect (l); return { r.getRight() - 40.0f, r.getY() + 3, 34, 36 }; }

juce::Rectangle<float> LoopsScreen::sourceCell (int i) const
{
    const float x0 = 85.0f, w = ((float) getWidth() - 15.0f - x0) / 4.0f;
    return { x0 + (float) i * w, 99.0f, w, 42.0f };
}

juce::Rectangle<float> LoopsScreen::lengthCell (int i) const
{
    const float x0 = 82.0f, w = ((float) getWidth() - 15.0f - x0) / 5.0f;
    return { x0 + (float) i * w, 152.0f, w, 42.0f };
}

//==============================================================================
void LoopsScreen::refresh()
{
    for (int l = 0; l < kLoopLayers; ++l)
    {
        auto& r = rows[(size_t) l];
        const auto st = loops.state (l);
        const int v = loops.version (l);
        const bool changed = st != r.st || v != r.version || st == LoopState::recording;
        r.st = st;
        r.muted = loops.isMuted (l);
        if (changed)
        {
            r.version = v;
            r.peaks = loops.peaks (l, kPeakBins);
        }
    }
}

void LoopsScreen::timerCallback()
{
    if (! isVisible())
    {
        saveIfChanged();
        loops.maintain();
        return;
    }
    inputLevel = juce::jmax (inputLevel * 0.85f, loops.takeInputLevel());
    refresh();
    if ((++ticks % 10) == 0)
    {
        loops.maintain();
        saveIfChanged();
    }
    repaint();
}

int LoopsScreen::barsForNext() const
{
    if (lengthChoice < 4)
        return kBarChoices[lengthChoice];
    const double master = loops.masterBeats();
    return master > 0 ? juce::jmax (1, juce::roundToInt (master / 4.0)) : 0;   // FREE: the first layer sets it, later ones snap
}

bool LoopsScreen::startRecording (int layer, int source)
{
    const auto src = (LoopSource) (source >= 0 ? source : sourceChoice);
    if (src == LoopSource::vocoder && ! loops.hasVocoder())
    {
        host.toast ("The vocoder source comes with the Vocoder");
        return false;
    }
    if (loops.state (layer) != LoopState::empty)
        return false;
    Undo u;
    u.items.push_back ({ layer, nullptr, false });
    if (! loops.arm (layer, src, barsForNext()))
    {
        host.toast ("That layer is busy. Try again in a moment.");
        return false;
    }
    pushUndo (std::move (u));
    selected = lastLayer = layer;
    return true;
}

int LoopsScreen::recordVocoder()
{
    int layer = -1;
    for (int l = 0; l < kLoopLayers && layer < 0; ++l)
        if (loops.state (l) == LoopState::empty)
            layer = l;
    if (layer < 0)
    {
        host.toast ("All 8 layers are used. Clear one in Loops to record.");
        return -1;
    }
    if (! startRecording (layer, (int) LoopSource::vocoder))
        return -1;   // it said why
    vocLayer = layer;
    return layer;
}

int LoopsScreen::vocoderLayer() const
{
    if (vocLayer < 0)
        return -1;
    const auto st = loops.state (vocLayer);
    return st == LoopState::waiting || st == LoopState::recording ? vocLayer : -1;
}

void LoopsScreen::stopVocoder()
{
    if (vocoderLayer() >= 0)
        loops.stop (vocLayer);
}

void LoopsScreen::pushUndo (Undo u)
{
    undoStack.push_back (std::move (u));
    if (undoStack.size() > 24)
        undoStack.erase (undoStack.begin());
}

void LoopsScreen::undo()
{
    if (undoStack.empty())
        return host.toast ("Nothing to undo");
    auto u = std::move (undoStack.back());
    undoStack.pop_back();
    for (auto& one : u.items)
    {
        if (one.before == nullptr)
            loops.clear (one.layer);
        else
            loops.setLayer (one.layer, one.before, one.mute);
        lastLayer = one.layer;
    }
}

void LoopsScreen::clearLayer (int layer, bool withUndo)
{
    if (loops.state (layer) == LoopState::empty)
        return;
    if (withUndo)
    {
        Undo u;
        auto take = loops.takeOf (layer);
        if (hasLoop (loops.state (layer)) && take != nullptr)
            u.items.push_back ({ layer, take, loops.isMuted (layer) });
        else
            u.items.push_back ({ layer, nullptr, false });
        pushUndo (std::move (u));
    }
    loops.clear (layer);
}

void LoopsScreen::clearAll()
{
    Undo u;
    int n = 0;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        const auto st = loops.state (l);
        if (st == LoopState::empty)
            continue;
        ++n;
        if (hasLoop (st) && loops.takeOf (l) != nullptr)
            u.items.push_back ({ l, loops.takeOf (l), loops.isMuted (l) });
        loops.clear (l);
    }
    if (n == 0)
        return host.toast ("No layers to clear");
    if (! u.items.empty())
        pushUndo (std::move (u));
    selected = -1;
    host.toast ("Cleared " + juce::String (n) + (n == 1 ? " layer. Undo brings it back." : " layers. Undo brings them back."));
}

void LoopsScreen::press (int id)
{
    if (id == rec)
    {
        int busy = -1;
        for (int l = 0; l < kLoopLayers && busy < 0; ++l)
            if (loops.state (l) == LoopState::recording || loops.state (l) == LoopState::waiting)
                busy = l;
        if (busy >= 0)
        {
            loops.stop (busy);
            return;
        }
        int layer = selected >= 0 && loops.state (selected) == LoopState::empty ? selected : -1;
        for (int l = 0; l < kLoopLayers && layer < 0; ++l)
            if (loops.state (l) == LoopState::empty)
                layer = l;
        if (layer < 0)
            return host.toast ("All 8 layers are used. Clear one to record.");
        startRecording (layer);
    }
    else if (id >= srcBase && id < srcBase + 4)
    {
        if ((LoopSource) (id - srcBase) == LoopSource::vocoder && ! loops.hasVocoder())
            return host.toast ("The vocoder source comes with the Vocoder");
        sourceChoice = id - srcBase;
        settingsDirty = true;
    }
    else if (id >= lenBase && id < lenBase + 5)
    {
        lengthChoice = id - lenBase;
        settingsDirty = true;
    }
    else if (id == lcd || id == playId)
    {
        if (! loops.transportRunning() && layerCount() == 0)
            host.toast ("Nothing recorded yet: playing the beat. REC records a layer over it.");
        host.togglePlay();
    }
    else if (id == undoId)
        undo();
    else if (id == clearId)
    {
        int layer = selected >= 0 && loops.state (selected) != LoopState::empty ? selected : -1;
        if (layer < 0 && lastLayer >= 0 && loops.state (lastLayer) != LoopState::empty)
            layer = lastLayer;
        for (int l = kLoopLayers - 1; l >= 0 && layer < 0; --l)
            if (loops.state (l) != LoopState::empty)
                layer = l;
        if (layer < 0)
            return host.toast ("No layers to clear");
        clearLayer (layer, true);
        host.toast ("Cleared layer " + juce::String (layer + 1) + ". Undo brings it back.");
    }
    else if (id == clearAllId)
        clearAll();
    else if (id == toMacId)
        sendToMac (true);
    else if (id >= rowSelect && id < rowSelect + kLoopLayers)
        selected = lastLayer = id - rowSelect;
    else if (id >= rowAct && id < rowAct + kLoopLayers)
    {
        const int l = id - rowAct;
        selected = lastLayer = l;
        switch (loops.state (l))
        {
            case LoopState::empty: startRecording (l); break;
            case LoopState::waiting:
                if (! undoStack.empty() && undoStack.back().items.size() == 1 && undoStack.back().items[0].layer == l && undoStack.back().items[0].before == nullptr)
                    undoStack.pop_back();
                loops.stop (l);
                break;
            case LoopState::recording:
            case LoopState::overdubWait:
            case LoopState::overdubbing: loops.stop (l); break;
            case LoopState::playing:
            {
                Undo u;
                u.items.push_back ({ l, loops.copyOf (l), loops.isMuted (l) });
                if (loops.dub (l))
                    pushUndo (std::move (u));
                break;
            }
        }
    }
    else if (id >= rowMute && id < rowMute + kLoopLayers)
    {
        const int l = id - rowMute;
        loops.setMute (l, ! loops.isMuted (l));
        lastLayer = l;
    }
}

int LoopsScreen::idAt (juce::Point<float> p) const
{
    if (recRect().expanded (4).contains (p))
        return rec;
    if (playRect().expanded (4).contains (p))
        return playId;
    if (lcdRect().contains (p))
        return lcd;
    for (int i = 0; i < 4; ++i)
        if (sourceCell (i).contains (p))
            return srcBase + i;
    for (int i = 0; i < 5; ++i)
        if (lengthCell (i).contains (p))
            return lenBase + i;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        if (muteRect (l).expanded (3, 3).contains (p))
            return rowMute + l;
        if (actRect (l).expanded (3, 3).contains (p))
            return rowAct + l;
        if (rowRect (l).contains (p))
            return rowSelect + l;
    }
    if (undoRect().contains (p)) return undoId;
    if (clearRect().contains (p)) return clearId;
    if (clearAllRect().contains (p)) return clearAllId;
    if (toMacRect().contains (p)) return toMacId;
    return 0;
}

void LoopsScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    repaint();
}

void LoopsScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    const int was = pressed;
    pressed = 0;
    if (id != 0 && id == was)
        press (id);
    repaint();
}

void LoopsScreen::resized() {}

//==============================================================================
void LoopsScreen::drawLcd (juce::Graphics& g)
{
    auto r = lcdRect();
    drawWell (g, r, 14.0f);

    int active = -1;
    int waiting = -1;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        const auto st = rows[(size_t) l].st;
        if (st == LoopState::recording || st == LoopState::overdubbing)
            active = l;
        else if (st == LoopState::waiting)
            waiting = l;
    }

    const bool running = loops.transportRunning();
    const double beat = loops.beatNow();
    const double bpm = loops.bpm();
    juce::String line1, line2;
    float progress = 0.0f;
    auto bar = [] (double rel) { return juce::String ((int) std::floor (rel / 4.0) + 1) + "." + juce::String ((int) std::fmod (std::floor (rel), 4.0) + 1); };
    const juce::String tempo = juce::String (juce::roundToInt (bpm)) + " BPM";

    if (active >= 0)
    {
        const double rel = juce::jmax (0.0, beat - loops.startBeat (active));
        const int bars = loops.state (active) == LoopState::overdubbing ? juce::jmax (1, juce::roundToInt (loops.masterBeats() / 4.0))
                                                                         : loops.recordingBars (active);
        line1 = (loops.state (active) == LoopState::overdubbing ? "DUB LAYER " : "REC LAYER ") + juce::String (active + 1);
        line2 = "BAR " + bar (rel) + " / " + (bars > 0 ? juce::String (bars) : juce::String ("FREE")) + U8(" · ") + tempo;
        progress = bars > 0 ? (float) (std::fmod (rel, bars * 4.0) / (bars * 4.0)) : (float) (std::fmod (rel, 16.0) / 16.0);
    }
    else if (waiting >= 0)
    {
        line1 = (loops.countingIn() ? "COUNT-IN " : "READY ") + juce::String (U8("· LAYER ")) + juce::String (waiting + 1);
        line2 = running ? "BAR " + bar (beat) + " / 1" + U8(" · ") + tempo : "STARTING" + juce::String (U8(" · ")) + tempo;
        progress = running ? (float) (std::fmod (beat, 4.0) / 4.0) : 0.0f;
    }
    else
    {
        line1 = "LOOPS " + juce::String (U8("· ")) + juce::String (layerCount()) + " OF 8";
        const double master = loops.masterBeats();
        if (running && master > 0)
        {
            line2 = "BAR " + bar (std::fmod (beat, master)) + " / " + juce::String (juce::roundToInt (master / 4.0)) + U8(" · ") + tempo;
            progress = (float) (std::fmod (beat, master) / master);
        }
        else if (running)
            line2 = "BAR " + bar (beat) + U8(" · ") + tempo;
        else
            line2 = (layerCount() > 0 ? juce::String ("READY") : juce::String ("STOPPED")) + U8(" · ") + tempo;
    }

    g.setColour (col::lcdText);
    g.setFont (spaced (lcdFont (31.0f), 0.03f));
    g.drawFittedText (line1, r.withTrimmedLeft (14).withTrimmedRight (70).withHeight (34).translated (0, 6).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    g.setColour (juce::Colour (0xffe8a24a));
    g.setFont (spaced (lcdFont (17.0f), 0.04f));
    g.drawFittedText (line2, r.withTrimmedLeft (15).withTrimmedRight (70).withHeight (20).translated (0, 38).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);

    // PLAY / STOP: the whole song, the same as the header's button
    {
        const bool on = beats.clockRunning();
        auto pr = playRect();
        drawRaised (g, pr, on ? Look::red : Look::green, pressed == playId, 10.0f);
        auto c = pr.getCentre().translated (0, -8);
        g.setColour (on ? juce::Colours::white : col::ink);
        if (on)
            g.fillRoundedRectangle (c.x - 6, c.y - 6, 12, 12, 2.0f);
        else
        {
            juce::Path tri;
            tri.addTriangle (c.x - 5, c.y - 7, c.x - 5, c.y + 7, c.x + 7, c.y);
            g.fillPath (tri);
        }
        g.setFont (spaced (condensedBold (12.0f), 0.08f));
        g.drawText (on ? "STOP" : "PLAY", pr.withTrimmedTop (24).withTrimmedBottom (4), juce::Justification::centred);
    }

    const int segs = 16;
    const float x0 = r.getX() + 14.0f, segW = (r.getWidth() - 28.0f - 3.0f * (float) (segs - 1)) / (float) segs;
    const int lit = juce::roundToInt (progress * (float) segs);
    for (int i = 0; i < segs; ++i)
    {
        g.setColour (i < lit ? col::lcdText : juce::Colour (0xff3b2f25));
        g.fillRoundedRectangle (x0 + (float) i * (segW + 3.0f), r.getBottom() - 15.0f, segW, 6.0f, 1.5f);
    }
}

void LoopsScreen::drawChoice (juce::Graphics& g, juce::Rectangle<float> cell, const juce::String& text, bool on, bool dim)
{
    if (on)
        drawRaised (g, cell.reduced (3, 0), Look::amber, false, 12.0f);
    g.setColour (on ? col::ink : (dim ? col::inkDim : col::ink));
    g.setFont (spaced (condensed (17.0f), 0.08f));
    g.drawText (text, cell.withTrimmedBottom (3), juce::Justification::centred);
}

void LoopsScreen::drawRow (juce::Graphics& g, int l)
{
    auto r = rowRect (l);
    const auto& row = rows[(size_t) l];
    const auto st = row.st;
    drawWell (g, r, 12.0f);
    if (selected == l)
    {
        g.setColour (col::amber.withAlpha (0.8f));
        g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 1.5f);
    }

    const auto colour = stateColour (st, row.muted);
    g.setColour (kRowText);
    g.setFont (condensed (21.0f));
    g.drawText (juce::String (l + 1), juce::Rectangle<float> (r.getX() + 7, r.getY(), 22, r.getHeight()), juce::Justification::centredLeft);
    g.setColour (colour);
    g.setFont (spaced (condensedBold (14.0f), 0.08f));
    g.drawText (stateLabel (st, row.muted), juce::Rectangle<float> (r.getX() + 30, r.getY(), 76, r.getHeight()), juce::Justification::centredLeft);

    auto wave = juce::Rectangle<float> (r.getX() + 101, r.getY() + 4, r.getWidth() - 101 - 126, r.getHeight() - 8);
    if (st == LoopState::waiting)
    {
        g.setColour (kRowAmber);
        g.setFont (spaced (condensedBold (15.0f), 0.1f));
        g.drawText (loops.countingIn() ? "COUNT-IN, THEN REC" : "STARTS NEXT BAR", wave, juce::Justification::centred);
    }
    else if (st != LoopState::empty && ! row.peaks.empty())
    {
        const float pitch = wave.getWidth() / (float) row.peaks.size();
        const int shown = (int) row.peaks.size();
        g.setColour (colour);
        for (int i = 0; i < shown; ++i)
        {
            const float p = std::sqrt (juce::jlimit (0.0f, 1.0f, row.peaks[(size_t) i] * 1.6f));
            const float h = juce::jmax (2.0f, p * wave.getHeight());
            g.fillRoundedRectangle (wave.getX() + (float) i * pitch, wave.getCentreY() - h * 0.5f, juce::jmax (1.6f, pitch - 1.4f), h, 0.8f);
        }
    }

    // the action button
    juce::String text;
    Look look = Look::dark;
    switch (st)
    {
        case LoopState::empty: text = "REC"; break;
        case LoopState::waiting: text = "WAIT"; look = Look::amber; break;
        case LoopState::recording:
        case LoopState::overdubbing: text = "STOP"; look = Look::red; break;
        case LoopState::overdubWait: text = "DUB"; look = Look::amber; break;
        default: text = "DUB"; break;
    }
    auto act = actRect (l);
    drawRaised (g, act, look, pressed == rowAct + l, 10.0f);
    g.setColour (look == Look::amber ? col::ink : juce::Colour (0xfff1e7d1));
    g.setFont (spaced (condensed (15.0f), 0.08f));
    if (st == LoopState::empty)
    {
        g.drawText (text, act.withTrimmedBottom (3).withTrimmedLeft (10), juce::Justification::centred);
        g.fillEllipse (act.getX() + 11, act.getCentreY() - 4.5f, 6, 6);
    }
    else
        g.drawText (text, act.withTrimmedBottom (3), juce::Justification::centred);

    auto mute = muteRect (l);
    drawRaised (g, mute, row.muted && st != LoopState::empty ? Look::blue : Look::dark, pressed == rowMute + l, 10.0f);
    g.setColour (row.muted && st != LoopState::empty ? col::ink : juce::Colour (0xfff1e7d1));
    g.setFont (condensed (16.0f));
    g.drawText ("M", mute.withTrimmedBottom (3), juce::Justification::centred);
}

void LoopsScreen::paint (juce::Graphics& g)
{
    drawLcd (g);

    // the big REC / STOP button, with the dark tab that joins it to the display
    bool busy = false;
    for (auto& r : rows)
        busy = busy || r.st == LoopState::recording || r.st == LoopState::waiting;
    {
        auto b = recRect();
        g.setColour (juce::Colour (0xffa8301f));
        juce::Path tab;
        tab.addRoundedRectangle (b.getRight() - 14, b.getY() + 10, 30.0f, b.getHeight() - 20, 6.0f);
        g.fillPath (tab);
        auto body = b.withTrimmedBottom (3);
        g.setColour (juce::Colour (0xff922a1c));
        g.fillEllipse (body.translated (0, pressed == rec ? 1.0f : 3.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff58b78), 0, body.getY(), juce::Colour (0xffd8452f), 0, body.getBottom(), false));
        g.fillEllipse (body.translated (0, pressed == rec ? 2.0f : 0.0f));
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawEllipse (body.reduced (1), 1.0f);
        auto c = body.getCentre().translated (0, pressed == rec ? 2.0f : 0.0f);
        g.setColour (juce::Colour (0xff2b1410));
        if (busy)
            g.fillRoundedRectangle (c.x - 7, c.y - 19, 14, 14, 2.0f);
        else
            g.fillEllipse (c.x - 8, c.y - 19, 16, 16);
        g.setFont (spaced (condensed (13.0f), 0.1f));
        g.drawText (busy ? "STOP" : "REC", juce::Rectangle<float> (c.x - 30, c.y + 1, 60, 18), juce::Justification::centred);
    }

    g.setColour (col::ink);
    g.setFont (spaced (condensed (14.0f), 0.12f));
    g.drawText ("FROM", juce::Rectangle<float> (16, 99, 60, 42), juce::Justification::centredLeft);
    for (int i = 0; i < 4; ++i)
        drawChoice (g, sourceCell (i), kSourceNames[i], sourceChoice == i);
    g.setColour (col::ink);
    g.setFont (spaced (condensed (14.0f), 0.12f));
    g.drawText ("LENGTH", juce::Rectangle<float> (16, 152, 66, 42), juce::Justification::centredLeft);
    for (int i = 0; i < 5; ++i)
        drawChoice (g, lengthCell (i), kLengthNames[i], lengthChoice == i);

    for (int l = 0; l < kLoopLayers; ++l)
        drawRow (g, l);

    drawRaised (g, undoRect(), Look::cream, pressed == undoId, 12.0f);
    drawRaised (g, clearRect(), Look::cream, pressed == clearId, 12.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensed (18.0f), 0.08f));
    g.drawText ("UNDO", undoRect().withTrimmedBottom (3), juce::Justification::centred);
    g.drawText ("CLEAR", clearRect().withTrimmedBottom (3), juce::Justification::centred);
    g.setColour (pressed == clearAllId ? col::ink : col::inkDim);
    g.drawText ("CLEAR ALL", clearAllRect().withTrimmedBottom (3), juce::Justification::centred);
    drawRaised (g, toMacRect(), Look::amber, pressed == toMacId, 12.0f);
    g.setColour (col::ink);
    g.drawText ("TO MAC", toMacRect().withTrimmedBottom (3), juce::Justification::centred);
}

//==============================================================================
// Saving: Documents/Loops/layerN.wav (mono) and loops.json
void LoopsScreen::saveIfChanged()
{
    bool busy = false, differs = settingsDirty;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        const auto st = loops.state (l);
        busy = busy || isBusy (st) || loops.settling (l);   // settling: what's shown isn't what was asked for yet
        auto& s = saved[(size_t) l];
        const auto take = loops.takeOf (l);
        const bool present = hasLoop (st) && take != nullptr;
        if (present != s.present || (present && (take.get() != s.take || loops.version (l) != s.version || loops.isMuted (l) != s.mute)))
            differs = true;
    }
    if (differs && ! busy && ! demo)
        save();
}

void LoopsScreen::save()
{
    if (demo)
        return;
    auto dir = folder();
    dir.createDirectory();
    auto* root = new juce::DynamicObject();
    juce::Array<juce::var> list;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        auto& s = saved[(size_t) l];
        const auto st = loops.state (l);
        auto take = loops.takeOf (l);
        auto file = dir.getChildFile ("layer" + juce::String (l + 1) + ".wav");
        const bool present = (hasLoop (st) || st == LoopState::empty) && take != nullptr && hasLoop (st);
        if (! present)
        {
            if (! isBusy (st))
                file.deleteFile();
            s = {};
            continue;
        }
        if (take.get() != s.take || loops.version (l) != s.version)
        {
            const int n = take->length.load();
            file.deleteFile();
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
            {
                juce::WavAudioFormat wav;
                if (std::unique_ptr<juce::AudioFormatWriter> w { wav.createWriterFor (stream.get(), take->sampleRate, 1, 24, {}, 0) })
                {
                    stream.release();
                    const float* ch[1] = { take->data.data() };
                    w->writeFromFloatArrays (ch, 1, n);
                }
            }
        }
        s = { take.get(), loops.version (l), true, loops.isMuted (l) };
        auto* o = new juce::DynamicObject();
        o->setProperty ("layer", l);
        o->setProperty ("file", file.getFileName());
        o->setProperty ("beats", take->lengthBeats.load());
        o->setProperty ("mute", loops.isMuted (l));
        o->setProperty ("source", take->source);
        list.add (juce::var (o));
    }
    root->setProperty ("bpm", loops.bpm());
    root->setProperty ("from", sourceChoice);
    root->setProperty ("length", lengthChoice);
    root->setProperty ("layers", list);
    dir.getChildFile ("loops.json").replaceWithText (juce::JSON::toString (juce::var (root)));
    settingsDirty = false;
}

void LoopsScreen::sendToMac (bool onlyPicked)
{
    // The picked layer, or every layer with a loop, onto the Mac's loop layers (stretched to its tempo).
    pocket::MacSend send;
    auto addLayer = [&] (int l)
    {
        auto take = loops.copyOf (l);
        if (take == nullptr || take->length.load() <= 0)
            return;
        juce::AudioBuffer<float> b (1, take->length.load());
        b.copyFrom (0, 0, take->data.data(), b.getNumSamples());
        if (std::abs (take->sampleRate - crate::kSampleRate) > 1.0)
            b = crate::AudioFiles::resample (b, take->sampleRate);   // the phone records at its own rate; sounds travel at 44.1 kHz
        static const char* sources[] = { "Mic", "Drums", "Vocoder", "Mix" };
        send.items.push_back ({ "Pocket loop " + juce::String (l + 1) + " " + sources[juce::jlimit (0, 3, take->source)],
                                crate::share (std::move (b)), take->lengthBeats.load() });
    };
    if (onlyPicked && selected >= 0 && loops.state (selected) != LoopState::empty)
        addLayer (selected);
    else
        for (int l = 0; l < kLoopLayers; ++l)
            if (loops.state (l) != LoopState::empty)
                addLayer (l);
    if (send.items.empty())
        return host.toast ("No loops to send yet. Record a layer first.");
    send.title = send.items.size() == 1 ? send.items.front().name : juce::String ((int) send.items.size()) + " loop layers";
    const double bars = send.items.front().beats / 4.0;
    send.detail = send.items.size() == 1 ? juce::String (bars, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + (bars == 1.0 ? " bar" : " bars")
                                         : juce::String ("WAVs");
    send.dests = { "loop", "sounds", "pad" };
    send.dest = "loop";
    host.sendToMac (std::move (send));
}

void LoopsScreen::reloadIdea()
{
    leave();
    for (int l = 0; l < kLoopLayers; ++l)
    {
        loops.setLayer (l, nullptr, false);
        saved[(size_t) l] = {};
    }
    undoStack.clear();
    restore();
    repaint();
}

void LoopsScreen::restore()
{
    auto v = juce::JSON::parse (folder().getChildFile ("loops.json"));
    auto* root = v.getDynamicObject();
    if (root == nullptr)
        return;
    sourceChoice = juce::jlimit (0, 3, (int) root->getProperty ("from"));
    if (sourceChoice == (int) LoopSource::vocoder)
        sourceChoice = 0;
    lengthChoice = juce::jlimit (0, 4, root->hasProperty ("length") ? (int) root->getProperty ("length") : 2);

    juce::AudioFormatManager& formats = library.formats();
    if (auto* layers = root->getProperty ("layers").getArray())
        for (auto& item : *layers)
        {
            const int l = (int) item.getProperty ("layer", -1);
            if (l < 0 || l >= kLoopLayers)
                continue;
            auto file = folder().getChildFile (item.getProperty ("file", {}).toString());
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            const double beats_ = (double) item.getProperty ("beats", 0.0);
            if (reader == nullptr || reader->lengthInSamples < 2 || beats_ <= 0.0)
                continue;
            const int n = (int) juce::jmin ((juce::int64) 1 << 28, reader->lengthInSamples);
            auto take = LoopsEngine::makeTake (n, reader->sampleRate);
            juce::AudioBuffer<float> tmp (1, n);
            reader->read (&tmp, 0, n, 0, true, false);
            std::copy (tmp.getReadPointer (0), tmp.getReadPointer (0) + n, take->data.begin());
            take->length = n;
            take->lengthBeats = beats_;
            take->source = (int) item.getProperty ("source", 0);
            loops.setLayer (l, take, (bool) item.getProperty ("mute", false));
            saved[(size_t) l] = { take.get(), 0, true, (bool) item.getProperty ("mute", false) };
        }
}

//==============================================================================
// Screenshot mode and the self-check, both driven through the real engine by rendering blocks by hand.
namespace
{
struct Pump
{
    BeatsEngine& beats;
    LoopsEngine& engine;
    std::vector<float> l, r, in;
    juce::int64 counter = 0;
    std::function<float (juce::int64)> input;

    Pump (BeatsEngine& b, LoopsEngine& e) : beats (b), engine (e), l (512), r (512), in (512) {}

    void run (int samples)
    {
        for (int done = 0; done < samples; done += 512)
        {
            const int n = juce::jmin (512, samples - done);
            for (int i = 0; i < n; ++i)
                in[(size_t) i] = input ? input (counter + i) : 0.0f;
            const float* ptr = in.data();
            engine.setInput (&ptr, 1);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            engine.render (l.data(), r.data(), n);
            counter += n;
        }
    }
};

SharedTake syntheticTake (int kind, double beats, double bpm, double sampleRate)
{
    const int n = (int) (beats * 60.0 / bpm * sampleRate);
    auto t = LoopsEngine::makeTake (n, sampleRate);
    juce::Random rng (kind * 31 + 7);
    for (int i = 0; i < n; ++i)
    {
        const double ph = (double) i / (double) n;
        float env;
        if (kind == 0)   // hits: a thump on every beat, with room in between
            env = (float) std::exp (-std::fmod (ph * beats, 1.0) * 9.0) * (std::fmod (ph * beats, 4.0) < 1.0 ? 1.0f : 0.55f);
        else             // a slow swell
            env = (float) (0.25 + 0.75 * std::pow (std::sin (ph * juce::MathConstants<double>::pi * 3.0), 2.0));
        t->data[(size_t) i] = (rng.nextFloat() * 2 - 1) * env * 0.7f;
    }
    t->length = n;
    t->lengthBeats = beats;
    return t;
}
}

void LoopsScreen::showDemo()
{
    demo = true;
    stopTimer();
    const double bpm = 97.0;
    beats.setBpm (bpm);
    loops.prepare (44100.0);
    loops.setLatency (0);
    loops.setCountIn (false);
    Pump pump (beats, loops);
    juce::Random rng (11);
    pump.input = [&rng] (juce::int64 n)
    {
        const double env = 0.2 + 0.8 * std::pow (std::sin ((double) n / 44100.0 * 2.2), 2.0);
        return (float) ((rng.nextFloat() * 2 - 1) * env * 0.7);
    };

    loops.setLayer (0, syntheticTake (0, 16.0, bpm, 44100.0), false);
    loops.setLayer (1, syntheticTake (1, 16.0, bpm, 44100.0), false);
    loops.setLayer (4, syntheticTake (0, 16.0, bpm, 44100.0), true);
    pump.run (512);
    loops.setTransport (true);
    pump.run (4096);
    loops.arm (2, LoopSource::mic, 4);                 // layer 3 starts at the next bar
    pump.run (512);
    while (loops.state (2) != LoopState::recording)
        pump.run (512);
    pump.run ((int) (60.0 / bpm * 44100.0 * 9.0));    // two bars and a beat in: bar 3, beat 2
    loops.arm (3, LoopSource::mic, 4);                 // layer 4 waits for the next bar
    pump.run (512);
    sourceChoice = 0;
    lengthChoice = 2;
    selected = -1;
    refresh();
    repaint();
}

juce::String LoopsScreen::selfCheck (juce::String& report)
{
    juce::StringArray problems;
    const double sr = 44100.0, bpm = 97.0;
    const double d = 1.0 / 300000.0;   // the made-up source is a ramp: sample n is n * d, so any gap or repeat shows

    for (const int latency : { 0, 300 })
    {
        auto beatsEngine = std::make_unique<BeatsEngine>();
        auto engine = std::make_unique<LoopsEngine> (*beatsEngine);
        engine->prepare (sr);
        engine->setLatency (latency);
        engine->setCountIn (false);
        beatsEngine->setBpm (bpm);
        Pump pump (*beatsEngine, *engine);
        pump.input = [d] (juce::int64 n) { return (float) ((double) n * d); };

        engine->arm (0, LoopSource::mic, 2);
        for (int guard = 0; guard < 600000 && engine->state (0) != LoopState::playing; guard += 512)
            pump.run (512);
        if (engine->state (0) != LoopState::playing)
        {
            problems.add ("The 2-bar layer never finished recording");
            continue;
        }

        auto take = engine->takeOf (0);
        const int len = take->length.load();
        const double expected = 8.0 * 60.0 / bpm * sr;   // 2 bars of 4 beats
        const juce::String tag = "latency " + juce::String (latency) + ": ";
        report << tag << "2 bars at 97 BPM = " << len << " samples (expected " << juce::String (expected, 1) << ")\n";
        if (std::abs ((double) len - expected) > 2.0)
            problems.add (tag + "layer length " + juce::String (len) + " samples, expected about " + juce::String (juce::roundToInt (expected)));

        // the first recorded sample is what the device heard `latency` samples after the bar line
        const double first = (double) take->data[0], want = (double) latency * d;
        report << tag << "first sample " << juce::String (first * 300000.0, 1) << " (compensated: " << latency << ")\n";
        if (std::abs (first - want) > 3.0 * d)
            problems.add (tag + "latency not compensated: first sample " + juce::String (first * 300000.0, 1) + ", expected " + juce::String (latency));

        // it plays 3 loops: look for a dropped or doubled sample anywhere but the loop point
        std::vector<float> out;
        out.reserve ((size_t) len * 3 + 4096);
        for (int i = 0; i < len * 3; i += 512)
        {
            pump.run (512);
            out.insert (out.end(), pump.l.begin(), pump.l.begin() + 512);
        }
        const double playRate = (double) len / (8.0 * 60.0 / bpm * sr);   // samples of the loop per output sample
        const double expectedDiff = 0.8 * d * playRate;
        int wraps = 0, bad = 0;
        long lastWrap = -1;
        long minSpacing = 1L << 40, maxSpacing = 0;
        for (size_t k = 3000; k + 1 < out.size(); ++k)
        {
            const double diff = (double) out[k + 1] - (double) out[k];
            if (diff < -0.1)
            {
                if (lastWrap >= 0 && (long) k - lastWrap < 4)
                    continue;   // the loop point spans two samples: the last one blends into the first
                ++wraps;
                if (lastWrap >= 0)
                {
                    minSpacing = juce::jmin (minSpacing, (long) k - lastWrap);
                    maxSpacing = juce::jmax (maxSpacing, (long) k - lastWrap);
                }
                lastWrap = (long) k;
            }
            else if (std::abs (diff - expectedDiff) > expectedDiff * 0.5)
                ++bad;
        }
        report << tag << "playback: " << wraps << " loop points, spacing " << (wraps > 1 ? juce::String (minSpacing) : "n/a")
               << " to " << (wraps > 1 ? juce::String (maxSpacing) : "n/a") << ", " << bad << " gaps or repeats\n";
        if (wraps < 2 || bad != 0 || minSpacing < len - 1 || maxSpacing > len + 1)
            problems.add (tag + "loop is not seamless (" + juce::String (wraps) + " loop points, " + juce::String (bad) + " gaps or repeats)");

        if (latency == 0)
        {
            // overdub: it records over the next pass and goes back to playing
            std::vector<float> before (take->data.begin(), take->data.begin() + len);
            engine->dub (0);
            bool dubbed = false;
            for (int guard = 0; guard < len * 4; guard += 512)
            {
                pump.run (512);
                dubbed = dubbed || engine->state (0) == LoopState::overdubbing;
                if (dubbed && engine->state (0) == LoopState::playing)
                    break;
            }
            const bool grew = take->data[(size_t) len / 2] > before[(size_t) len / 2] + 0.01f;
            report << "overdub: " << (dubbed && engine->state (0) == LoopState::playing ? "went through overdubbing and back to playing" : "did not finish") << ", mid-loop sound added: " << (grew ? "yes" : "no") << "\n";
            if (! dubbed || engine->state (0) != LoopState::playing || ! grew)
                problems.add ("overdub did not record on top of the layer");
        }
    }
    return problems.joinIntoString ("\n");
}
