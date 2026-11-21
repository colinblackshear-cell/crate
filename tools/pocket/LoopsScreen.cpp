#include "LoopsScreen.h"

#include "core/AudioFiles.h"
#include "PocketChordPlayer.h"

using namespace pocket;

namespace
{
constexpr float kRowTop = 60.0f;
constexpr int kPeakBins = 34;
const char* const kSourceNames[4] = { "MIC", "DRUMS", "VOCODER", "MIX" };

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
        case LoopState::waiting: return "WAITING";
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
    loops.setCountInBars (settings.countInBars);
    restore();
    addChildComponent (editor);
    editor.onDone = [this] { closeEditor(); };
    editor.onUndo = [this] { undo(); };
    editor.onPlay = [this] { if (! loops.transportRunning()) host.togglePlay(); };
    editor.onToast = [this] (const juce::String& t) { host.toast (t); };
    editor.onEdit = [this] (int edit, double a, double b) { editLayer (editor.layer(), edit, a, b); };
    editor.onFxChanged = [this] { repaint(); };
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
    closeEditor();
    loops.setTransport (false);
    beats.setPlaying (false);
}

// The eight layers fill the room between the FROM chips and the bottom buttons.
juce::Rectangle<float> LoopsScreen::rowRect (int l) const
{
    const float pitch = juce::jmax (46.0f, (buttonsY() - 8.0f - kRowTop) / (float) kLoopLayers);
    return { 16.0f, kRowTop + (float) l * pitch, (float) getWidth() - 32.0f, pitch - 5.0f };
}
juce::Rectangle<float> LoopsScreen::actRect (int l) const { auto r = rowRect (l); return { r.getRight() - 112.0f, r.getY() + 4, 62, r.getHeight() - 8 }; }
juce::Rectangle<float> LoopsScreen::editRect (int l) const { auto r = rowRect (l); return { r.getRight() - 172.0f, r.getY() + 4, 54, r.getHeight() - 8 }; }
juce::Rectangle<float> LoopsScreen::muteRect (int l) const { auto r = rowRect (l); return { r.getRight() - 44.0f, r.getY() + 4, 38, r.getHeight() - 8 }; }

juce::Rectangle<float> LoopsScreen::sourceCell (int i) const
{
    const float x0 = 72.0f, w = ((float) getWidth() - 13.0f - x0) / 4.0f;
    return { x0 + (float) i * w, 8.0f, w, 44.0f };
}

int LoopsScreen::nextLayer() const
{
    if (selected >= 0 && loops.state (selected) == LoopState::empty && ! loops.settling (selected))
        return selected;
    for (int l = 0; l < kLoopLayers; ++l)
        if (loops.state (l) == LoopState::empty && ! loops.settling (l))
            return l;
    return -1;
}

int LoopsScreen::busyLayer() const
{
    for (int l = 0; l < kLoopLayers; ++l)
        if (loops.state (l) == LoopState::recording || loops.state (l) == LoopState::waiting)
            return l;
    return -1;
}

void LoopsScreen::stopTake (int l)
{
    if (l < 0)
        return;
    // Called off while it waited for its bar: its undo step goes with it.
    if (loops.state (l) == LoopState::waiting && ! undoStack.empty() && undoStack.back().items.size() == 1
        && undoStack.back().items[0].layer == l && undoStack.back().items[0].before == nullptr)
        undoStack.pop_back();
    loops.stop (l);
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
    loops.recordMoves();
    inputLevel = juce::jmax (inputLevel * 0.85f, loops.takeInputLevel());
    refresh();
    if ((++ticks % 10) == 0)
    {
        loops.maintain();
        saveIfChanged();
    }
    repaint();
}

bool LoopsScreen::startRecording (int layer, int source, int bars)
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
    if (bars < 0)
        bars = settings.recBars();
    if (bars == 0)   // FREE: the first layer sets the length, later ones snap to it
        bars = loops.masterBeats() > 0 ? juce::jmax (1, juce::roundToInt (loops.masterBeats() / 4.0)) : 0;
    if (! loops.arm (layer, src, bars))
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

int LoopsScreen::recordFrom (int source, int bars)
{
    const int layer = nextLayer();
    if (layer < 0)
    {
        host.toast ("All 8 layers are used. Clear one in Loops to record.");
        return -1;
    }
    return startRecording (layer, source, bars) ? layer : -1;
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
        {
            loops.setLayer (one.layer, one.before, one.mute);
            if (one.withFx)
                loops.setFx (one.layer, one.fx);
        }
        lastLayer = one.layer;
    }
    if (editor.isVisible())
        editor.layerChanged();
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
            u.items.push_back ({ layer, take, loops.isMuted (layer), true, loops.fxOf (layer) });
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
            u.items.push_back ({ l, loops.takeOf (l), loops.isMuted (l), true, loops.fxOf (l) });
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
    if (id >= srcBase && id < srcBase + 4)
    {
        if ((LoopSource) (id - srcBase) == LoopSource::vocoder && ! loops.hasVocoder())
            return host.toast ("The vocoder source comes with the Vocoder");
        sourceChoice = id - srcBase;
        settingsDirty = true;
        if (onSourceChanged)
            onSourceChanged();
    }
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
            case LoopState::empty: break;   // picked: the next take lands here
            case LoopState::waiting: stopTake (l); break;
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
    else if (id >= rowEdit && id < rowEdit + kLoopLayers)
    {
        const int l = id - rowEdit;
        selected = lastLayer = l;
        if (loops.state (l) == LoopState::playing)
            openEditor (l);
        else if (hasLoop (loops.state (l)))
            host.toast ("Let the overdub finish, then EDIT.");
        else
            host.toast ("Record this layer first, then EDIT trims it and adds effects.");
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
    for (int i = 0; i < 4; ++i)
        if (sourceCell (i).contains (p))
            return srcBase + i;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        if (loops.state (l) == LoopState::empty)
        {
            if (rowRect (l).contains (p))
                return rowSelect + l;
            continue;
        }
        if (muteRect (l).expanded (3, 3).contains (p))
            return rowMute + l;
        if (editRect (l).expanded (2, 3).contains (p))
            return rowEdit + l;
        if (actRect (l).expanded (3, 3).contains (p))
            return rowAct + l;
        if (rowRect (l).contains (p))
            return rowSelect + l;
    }
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

void LoopsScreen::resized()
{
    if (editor.isVisible())
        editor.setBounds (0, editorTop(), getWidth(), getHeight() - editorTop());
}

void LoopsScreen::openEditor (int layer)
{
    editor.open (layer);
    const juce::Rectangle<int> to (0, editorTop(), getWidth(), getHeight() - editorTop());
    if (! editor.isVisible())
    {
        editor.setBounds (to.withY (getHeight()));
        editor.setVisible (true);
        editor.toFront (false);
        juce::Desktop::getInstance().getAnimator().animateComponent (&editor, to, 1.0f, demo ? 0 : 220, false, 1.0, 0.0);
    }
    else
        editor.setBounds (to);
}

void LoopsScreen::closeEditor()
{
    if (! editor.isVisible())
        return;
    if (editor.layer() >= 0)
        loops.setRecMoves (editor.layer(), false);
    juce::Desktop::getInstance().getAnimator().cancelAnimation (&editor, false);
    editor.setVisible (false);
    repaint();
}

void LoopsScreen::editLayer (int layer, int edit, double fromBeat, double toBeat)
{
    if (layer < 0 || loops.state (layer) != LoopState::playing)
        return host.toast ("Let the layer finish recording first.");
    auto take = loops.takeOf (layer);
    if (take == nullptr)
        return;
    const int len = take->length.load();
    const double beatsLen = take->lengthBeats.load();
    if (len < 2 || beatsLen <= 0)
        return;
    const double perBeat = (double) len / beatsLen;
    const float* d = take->data.data();
    auto fx = loops.fxOf (layer);
    SharedTake made;
    juce::String said;

    switch (edit)
    {
        case LoopEditSheet::trim:
        {
            fromBeat = juce::jlimit (0.0, beatsLen, fromBeat);
            toBeat = juce::jlimit (fromBeat, beatsLen, toBeat);
            const int a = juce::jlimit (0, len - 1, juce::roundToInt (fromBeat * perBeat));
            const int b = juce::jlimit (a + 1, len, juce::roundToInt (toBeat * perBeat));
            if (toBeat - fromBeat < 0.99)
                return host.toast ("Keep at least one beat.");
            made = LoopsEngine::makeTake (b - a, take->sampleRate);
            std::copy (d + a, d + b, made->data.begin());
            made->lengthBeats = toBeat - fromBeat;
            fx.remap (fromBeat / beatsLen, toBeat / beatsLen, 1, false);
            said = "Trimmed to " + juce::String (toBeat - fromBeat, 0) + " beats.";
            break;
        }
        case LoopEditSheet::half:
        {
            if (beatsLen < 2.0 - 1.0e-6)
                return host.toast ("It's one beat already.");
            const int b = juce::jmax (1, len / 2);
            made = LoopsEngine::makeTake (b, take->sampleRate);
            std::copy (d, d + b, made->data.begin());
            made->lengthBeats = beatsLen / 2.0;
            fx.remap (0.0, 0.5, 1, false);
            said = "Half: the first " + juce::String (beatsLen / 2.0, beatsLen / 2.0 == std::floor (beatsLen / 2.0) ? 0 : 1) + " beats.";
            break;
        }
        case LoopEditSheet::twice:
        {
            if ((double) len * 2.0 / take->sampleRate > 600.0)
                return host.toast ("Doubled it would pass 10 minutes.");
            made = LoopsEngine::makeTake (len * 2, take->sampleRate);
            std::copy (d, d + len, made->data.begin());
            std::copy (d, d + len, made->data.begin() + len);
            made->lengthBeats = beatsLen * 2.0;
            fx.remap (0.0, 1.0, 2, false);
            said = "Doubled: it plays twice, so you can dub something different over the second half.";
            break;
        }
        case LoopEditSheet::reverse:
        {
            made = LoopsEngine::makeTake (len, take->sampleRate);
            std::reverse_copy (d, d + len, made->data.begin());
            made->lengthBeats = beatsLen;
            fx.remap (0.0, 1.0, 1, true);
            said = "Reversed. Undo turns it back.";
            break;
        }
        default: return;
    }
    made->length = (int) made->data.size();
    made->source = take->source;

    Undo u;
    u.items.push_back ({ layer, take, loops.isMuted (layer), true, loops.fxOf (layer) });
    pushUndo (std::move (u));
    loops.setLayer (layer, made, loops.isMuted (layer));
    loops.setFx (layer, fx);
    host.toast (said);
}

juce::uint32 LoopsScreen::fxHash (const LayerFxState& fx)
{
    juce::uint32 h = 2166136261u;
    auto mix = [&h] (float v) { h = (h ^ (juce::uint32) juce::roundToInt (v * 10000.0f)) * 16777619u; };
    for (int k = 0; k < kFxKnobs; ++k)
    {
        mix (fx.knob[(size_t) k]);
        mix ((float) fx.moves[(size_t) k].size());
        for (auto v : fx.moves[(size_t) k])
            mix (v);
    }
    return h;
}

//==============================================================================
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
    g.drawText (juce::String (l + 1), juce::Rectangle<float> (r.getX() + 9, r.getY(), 22, r.getHeight()), juce::Justification::centredLeft);

    if (st == LoopState::empty)
    {
        // Empty: the one the next take lands on says so (tap another empty row to pick it instead).
        const bool next = l == nextLayer();
        auto box = juce::Rectangle<float> (r.getX() + 34, r.getCentreY() - 14, 28, 28);
        juce::Path outline, dash;
        outline.addRoundedRectangle (box, 7.0f);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dash, outline, dashes, 2);
        g.setColour (next ? kRowRed : kRowGrey.withAlpha (0.6f));
        g.fillPath (dash);
        g.setFont (spaced (condensedBold (15.0f), 0.1f));
        if (next)
        {
            g.fillEllipse (box.getRight() + 12, r.getCentreY() - 3.5f, 7, 7);
            g.drawFittedText ("NEXT RECORDING LANDS HERE", juce::Rectangle<float> (box.getRight() + 25, r.getY(), r.getRight() - box.getRight() - 35, r.getHeight()).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
        else
            g.drawText ("EMPTY", juce::Rectangle<float> (box.getRight() + 12, r.getY(), 120, r.getHeight()), juce::Justification::centredLeft);
        return;
    }

    g.setColour (colour);
    g.setFont (spaced (condensedBold (14.0f), 0.08f));
    g.drawFittedText (stateLabel (st, row.muted), juce::Rectangle<float> (r.getX() + 30, r.getY(), 62, r.getHeight()).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

    auto wave = juce::Rectangle<float> (r.getX() + 94, r.getY() + 5, editRect (l).getX() - 8 - (r.getX() + 94), r.getHeight() - 10);
    if (st == LoopState::waiting)
    {
        g.setColour (kRowAmber);
        g.setFont (spaced (condensedBold (15.0f), 0.1f));
        g.drawFittedText (loops.countingIn() ? "COUNT-IN, THEN REC" : "STARTS NEXT BAR", wave.toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
    else if (! row.peaks.empty())
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

    // the action button: DUB a playing layer, STOP a recording or dub, WAIT (tap to call off)
    juce::String text;
    Look look = Look::dark;
    switch (st)
    {
        case LoopState::waiting: text = "WAIT"; look = Look::amber; break;
        case LoopState::recording:
        case LoopState::overdubbing: text = "STOP"; look = Look::red; break;
        case LoopState::overdubWait: text = "DUB"; look = Look::amber; break;
        default: text = "DUB"; break;
    }
    auto act = actRect (l);
    drawRaised (g, act, look, pressed == rowAct + l, 10.0f);
    g.setColour (look == Look::amber ? col::ink : juce::Colour (0xfff1e7d1));
    g.setFont (spaced (condensed (16.0f), 0.08f));
    g.drawText (text, act.withTrimmedBottom (3), juce::Justification::centred);

    // EDIT: trim and effects (amber when the layer has effects on)
    if (hasLoop (st))
    {
        auto ed = editRect (l);
        const bool fxOn = ! loops.fxOf (l).isNeutral();
        drawRaised (g, ed, fxOn ? Look::amber : Look::cream, pressed == rowEdit + l, 10.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensed (15.0f), 0.08f));
        g.drawText ("EDIT", ed.withTrimmedBottom (3), juce::Justification::centred);
        if (loops.hasAnyMoves (l))
        {
            g.setColour (loops.recMoves (l) ? col::red : col::ink.withAlpha (0.6f));
            g.fillEllipse (ed.getRight() - 9, ed.getY() + 4, 5, 5);
        }
    }

    auto mute = muteRect (l);
    drawRaised (g, mute, row.muted ? Look::blue : Look::dark, pressed == rowMute + l, 10.0f);
    g.setColour (row.muted ? col::ink : juce::Colour (0xfff1e7d1));
    g.setFont (condensed (17.0f));
    g.drawText ("M", mute.withTrimmedBottom (3), juce::Justification::centred);
}

void LoopsScreen::paint (juce::Graphics& g)
{
    g.setColour (col::ink);
    g.setFont (spaced (condensed (14.0f), 0.12f));
    g.drawText ("FROM", juce::Rectangle<float> (16, 8, 56, 44), juce::Justification::centredLeft);
    for (int i = 0; i < 4; ++i)
        drawChoice (g, sourceCell (i), kSourceNames[i], sourceChoice == i);

    for (int l = 0; l < kLoopLayers; ++l)
        drawRow (g, l);

    drawRaised (g, clearRect(), Look::cream, pressed == clearId, 12.0f);
    drawRaised (g, clearAllRect(), Look::cream, pressed == clearAllId, 12.0f);
    drawRaised (g, toMacRect(), Look::amber, pressed == toMacId, 12.0f);
    g.setColour (col::ink);
    g.setFont (spaced (condensed (18.0f), 0.08f));
    g.drawText ("CLEAR", clearRect().withTrimmedBottom (3), juce::Justification::centred);
    g.drawText ("CLEAR ALL", clearAllRect().withTrimmedBottom (3), juce::Justification::centred);
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
        if (present != s.present || (present && (take.get() != s.take || loops.version (l) != s.version || loops.isMuted (l) != s.mute
                                                  || (! loops.recMoves (l) && fxHash (loops.fxOf (l)) != s.fx))))
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
        const auto fx = loops.fxOf (l);
        s = { take.get(), loops.version (l), true, loops.isMuted (l), fxHash (fx) };
        auto* o = new juce::DynamicObject();
        if (! fx.isNeutral())
        {
            juce::Array<juce::var> knobs, moves;
            for (int k = 0; k < kFxKnobs; ++k)
            {
                knobs.add (std::round (fx.knob[(size_t) k] * 1000.0f) / 1000.0f);
                juce::Array<juce::var> m;
                for (auto v : fx.moves[(size_t) k])
                    m.add (std::round (v * 1000.0f) / 1000.0f);
                moves.add (fx.moves[(size_t) k].empty() ? juce::var() : juce::var (m));
            }
            o->setProperty ("fx", knobs);
            o->setProperty ("moves", moves);
        }
        o->setProperty ("layer", l);
        o->setProperty ("file", file.getFileName());
        o->setProperty ("beats", take->lengthBeats.load());
        o->setProperty ("mute", loops.isMuted (l));
        o->setProperty ("source", take->source);
        list.add (juce::var (o));
    }
    root->setProperty ("bpm", loops.bpm());
    root->setProperty ("from", sourceChoice);
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
        if (! loops.fxOf (l).isNeutral())
            if (auto printed = loops.printed (l))
                b = std::move (*printed);   // with its effects and moves, in stereo
        if (std::abs (take->sampleRate - crate::kSampleRate) > 1.0)
            b = crate::AudioFiles::resample (b, take->sampleRate);   // the phone records at its own rate; sounds travel at 44.1 kHz
        static const char* sources[] = { "Mic", "Drums", "Vocoder", "Mix", "Voice and vocoder", "Synth", "Chords" };
        send.items.push_back ({ "Pocket loop " + juce::String (l + 1) + " " + sources[juce::jlimit (0, 6, take->source)],
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
    closeEditor();
    for (int l = 0; l < kLoopLayers; ++l)
    {
        loops.setLayer (l, nullptr, false);
        saved[(size_t) l] = {};
    }
    undoStack.clear();
    selected = lastLayer = vocLayer = -1;   // the new song's next take goes on its first empty layer
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
            LayerFxState fx;
            if (auto* knobs = item.getProperty ("fx", {}).getArray())
                for (int k = 0; k < kFxKnobs && k < knobs->size(); ++k)
                    fx.knob[(size_t) k] = (float) (double) (*knobs)[k];
            if (auto* moves = item.getProperty ("moves", {}).getArray())
                for (int k = 0; k < kFxKnobs && k < moves->size(); ++k)
                    if (auto* m = (*moves)[k].getArray(); m != nullptr && m->size() == kMoveBins)
                    {
                        auto& lane = fx.moves[(size_t) k];
                        lane.resize ((size_t) kMoveBins);
                        for (int i = 0; i < kMoveBins; ++i)
                            lane[(size_t) i] = (float) (double) (*m)[i];
                    }
            loops.setFx (l, fx);
            saved[(size_t) l] = { take.get(), 0, true, (bool) item.getProperty ("mute", false), fxHash (loops.fxOf (l)) };
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
    loops.setCountInBars (0);
    loops.setMic (true, false);
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
        engine->setCountInBars (0);
        engine->setMic (true, false);
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

    // effects: moves play back (volume off for the first half of the loop, full for the second)
    {
        auto beatsEngine = std::make_unique<BeatsEngine>();
        auto engine = std::make_unique<LoopsEngine> (*beatsEngine);
        engine->prepare (sr);
        engine->setLatency (0);
        engine->setCountInBars (0);
        engine->setMic (true, false);
        beatsEngine->setBpm (bpm);
        Pump pump (*beatsEngine, *engine);
        const int n = (int) (4.0 * 60.0 / bpm * sr);
        auto take = LoopsEngine::makeTake (n, sr);
        std::fill (take->data.begin(), take->data.end(), 0.5f);
        take->length = n;
        take->lengthBeats = 4.0;
        engine->setLayer (0, take, false);
        LayerFxState fx;
        fx.moves[fxVolume].assign ((size_t) kMoveBins, 0.75f);
        std::fill (fx.moves[fxVolume].begin(), fx.moves[fxVolume].begin() + kMoveBins / 2, 0.0f);
        engine->setFx (0, fx);
        engine->setTransport (true);
        pump.run (n + 4096);
        int silent = 0, full = 0;
        for (int done = 0; done < n; done += 512)
        {
            pump.run (512);
            for (auto v : pump.l)
            {
                silent += std::abs (v) < 0.01f ? 1 : 0;
                full += std::abs (v - 0.4f) < 0.02f ? 1 : 0;
            }
        }
        report << "moves: volume moves gave " << silent << " silent and " << full << " full samples of " << n << "\n";
        if (silent < n * 2 / 5 || full < n * 2 / 5)
            problems.add ("recorded volume moves did not play back (" + juce::String (silent) + " silent, " + juce::String (full) + " full of " + juce::String (n) + ")");

        // recording moves: a finger on the filter knob while REC MOVES is on writes where the loop is
        engine->setRecMoves (0, true);
        engine->touchKnob (0, fxFilter, true);
        engine->setKnob (0, fxFilter, 0.9f);
        pump.run (2048);
        engine->recordMoves();
        const int bin = juce::jlimit (0, kMoveBins - 1, (int) (engine->position (0) * kMoveBins));
        pump.run (4096);
        engine->recordMoves();
        engine->touchKnob (0, fxFilter, false);
        engine->setRecMoves (0, false);
        const auto lane = engine->moves (0, fxFilter);
        const bool wrote = lane.size() == (size_t) kMoveBins && std::abs (lane[(size_t) bin] - 0.9f) < 1.0e-4f
                           && std::abs (lane[(size_t) ((bin + kMoveBins / 2) % kMoveBins)] - 0.5f) < 1.0e-4f;
        report << "moves: REC MOVES wrote the filter " << (wrote ? "where the loop was, and left the rest" : "wrongly") << "\n";
        if (! wrote)
            problems.add ("REC MOVES did not write the knob into its moves");

        // printing: reverb leaves a tail after a click; no effects leaves the loop as it was
        LayerFxState wet;
        wet.knob[fxReverb] = 0.6f;
        std::vector<float> click ((size_t) n, 0.0f);
        click[100] = 1.0f;
        auto printedWet = renderLayer (click.data(), n, 4.0, sr, wet);
        auto printedDry = renderLayer (click.data(), n, 4.0, sr, LayerFxState {});
        const float tail = printedWet.getMagnitude (0, n / 4, n / 4);
        const bool dryExact = printedDry.getSample (0, 100) == 1.0f && printedDry.getMagnitude (0, 200, n - 200) == 0.0f;
        report << "print: reverb tail " << juce::String (tail, 4) << ", no effects " << (dryExact ? "untouched" : "changed") << "\n";
        if (tail < 1.0e-4f || ! dryExact)
            problems.add ("printing a layer with its effects went wrong (tail " + juce::String (tail, 5) + ")");

        // moves follow an edit: doubled, the first half plays again in the second
        LayerFxState ramp;
        ramp.moves[fxDelay].resize ((size_t) kMoveBins);
        for (int i = 0; i < kMoveBins; ++i)
            ramp.moves[fxDelay][(size_t) i] = (float) i / kMoveBins;
        ramp.remap (0.0, 1.0, 2, false);
        const auto& m = ramp.moves[fxDelay];
        if (std::abs (m[10] - m[(size_t) kMoveBins / 2 + 10]) > 0.01f || std::abs (m[(size_t) kMoveBins / 2 - 1] - 1.0f) > 0.02f)
            problems.add ("moves did not follow a doubled loop");
    }
    // Sing's BOTH source: your voice plus the chord pads in a pad's sound
    {
        auto beatsEngine = std::make_unique<BeatsEngine>();
        auto engine = std::make_unique<LoopsEngine> (*beatsEngine);
        ChordPlayer player;
        engine->setChordSource (&player);
        engine->prepare (sr);
        engine->setLatency (0);
        engine->setCountInBars (0);
        engine->setMic (true, false);
        beatsEngine->setBpm (bpm);
        Pump pump (*beatsEngine, *engine);
        pump.input = [] (juce::int64) { return 0.1f; };
        juce::AudioBuffer<float> tone (1, (int) crate::kSampleRate * 4);
        for (int i = 0; i < tone.getNumSamples(); ++i)
            tone.setSample (0, i, 0.5f * (float) std::sin (i * 2.0 * juce::MathConstants<double>::pi * 261.6 / crate::kSampleRate));
        player.setSound (crate::share (std::move (tone)), 0.0);
        player.play ({ 60, 64, 67 });
        engine->arm (0, LoopSource::both, 1);
        for (int guard = 0; guard < 400000 && engine->state (0) != LoopState::playing; guard += 512)
            pump.run (512);
        auto take = engine->takeOf (0);
        double mean = 0, swing = 0;
        const int len = take != nullptr ? take->length.load() : 0;
        for (int i = 0; i < len; ++i)
        {
            mean += take->data[(size_t) i];
            swing = juce::jmax (swing, (double) std::abs (take->data[(size_t) i] - 0.1f));
        }
        mean /= juce::jmax (1, len);
        report << "sing: BOTH took " << len << " samples, voice level " << juce::String (mean, 3) << ", chord swing " << juce::String (swing, 3) << "\n";
        if (len < 1000 || std::abs (mean - 0.1) > 0.03 || swing < 0.1)
            problems.add ("Sing's BOTH source did not record the voice and the chord pads");
        engine->setChordSource (nullptr);
    }
    return problems.joinIntoString ("\n");
}

void LoopsScreen::showEditorDemo()
{
    // layer 2 with a filter sweep recorded, some reverb and delay
    LayerFxState fx;
    fx.knob[fxReverb] = 0.35f;
    fx.knob[fxDelay] = 0.2f;
    fx.moves[fxFilter].resize ((size_t) kMoveBins);
    for (int i = 0; i < kMoveBins; ++i)
    {
        const double t = (double) i / kMoveBins;
        fx.moves[fxFilter][(size_t) i] = (float) (t < 0.2 ? 0.5 : (t < 0.45 ? 0.5 - (t - 0.2) * 1.4 : (t < 0.7 ? 0.15 : 0.15 + (t - 0.7) * 1.1)));
    }
    loops.setFx (1, fx);
    selected = 1;
    openEditor (1);
    juce::Desktop::getInstance().getAnimator().cancelAllAnimations (true);
    editor.setBounds (0, editorTop(), getWidth(), getHeight() - editorTop());
    refresh();
    repaint();
}
