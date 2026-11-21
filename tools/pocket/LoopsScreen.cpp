#include "LoopsScreen.h"

#include "core/AudioFiles.h"
#include "PocketChordPlayer.h"

using namespace pocket;

namespace
{
constexpr int kPeakBins = 96;
const char* const kSourceNames[4] = { "MIC", "DRUMS", "VOCODER", "MIX" };

const juce::Colour kRowRed { 0xfff0674f }, kRowAmber { 0xfff5b13d }, kRowBlue { 0xff56b3d6 }, kRowGrey { 0xff8a8174 }, kRowText { 0xffe8dfc9 };

bool hasLoop (LoopState s) { return s == LoopState::playing || s == LoopState::overdubWait || s == LoopState::overdubbing; }
bool isBusy (LoopState s) { return s == LoopState::waiting || s == LoopState::recording || s == LoopState::overdubbing; }

juce::String titleCase (const juce::String& s) { return s.substring (0, 1) + s.substring (1).toLowerCase(); }

juce::String barsText (double beats)
{
    const double bars = beats / 4.0;
    return juce::String (bars, std::abs (bars - std::round (bars)) < 0.05 ? 0 : 1) + (std::abs (bars - 1.0) < 0.05 ? " BAR" : " BARS");
}

Tool openToolFor (int layerTool, bool& has)
{
    has = true;
    switch ((LayerTool) layerTool)
    {
        case LayerTool::drums: return Tool::drums;
        case LayerTool::pads: return Tool::pads;
        case LayerTool::synth: return Tool::synth;
        case LayerTool::chords: return Tool::chords;
        case LayerTool::vocoder: return Tool::vocoder;
        default: has = false; return Tool::loops;
    }
}
}

//==============================================================================
LoopsScreen::LoopsScreen (PocketHost& h, Library& l, BeatsEngine& b, AppSettings& s)
    : host (h), library (l), beats (b), settings (s), loops (b)
{
    loops.setCountInBars (settings.countInBars);
    restore();
    for (int i = 0; i < kPanelButtons; ++i)
    {
        addChildComponent (panel[(size_t) i]);
        panel[(size_t) i].setFontHeight (15.0f);
        panel[(size_t) i].onClick = [this, i] { pressPanel (i); };
    }
    addChildComponent (editor);
    editor.onDone = [this] { closeEditor(); };
    editor.onUndo = [this] { undo(); };
    editor.onPlay = [this] { if (! loops.transportRunning()) host.togglePlay(); };
    editor.onToast = [this] (const juce::String& t) { host.toast (t); };
    editor.onEdit = [this] (int edit, double a, double b) { editLayer (editor.layer(), edit, a, b); };
    editor.onFit = [this] (int bars, double a, double b) { fitLayer (editor.layer(), bars, a, b); };
    editor.onQuantize = [this] (const LoopQuantize& q) { quantizeLayer (editor.layer(), q); };
    editor.onFxChanged = [this] { repaint(); };
    editor.onFullBoard = [this] { if (onFullBoard && editor.layer() >= 0) onFullBoard (editor.layer()); };
    editor.isBusy = [this] { return jobLayer >= 0 && jobUndo; };
    editor.onHeightChanged = [this] { if (editor.isVisible()) editor.setBounds (0, editorTop(), getWidth(), getHeight() - editorTop()); };
    shownBpm = loops.bpm();
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

//==============================================================================
// Layout: FROM chips, the bar ruler, eight rows, the panel for the picked layer
juce::Rectangle<float> LoopsScreen::rowRect (int l) const
{
    const float bottom = panelRect().getY() - 6.0f;
    const float pitch = juce::jmin (52.0f, juce::jmax (40.0f, (bottom - rowsTop()) / (float) kLoopLayers));
    return { 16.0f, rowsTop() + (float) l * pitch, (float) getWidth() - 32.0f, pitch - 4.0f };
}
juce::Rectangle<float> LoopsScreen::muteRect (int l) const { auto r = rowRect (l); return { r.getRight() - 46.0f, r.getY() + 4, 40, r.getHeight() - 8 }; }
juce::Rectangle<float> LoopsScreen::waveRect (int l) const
{
    auto r = rowRect (l);
    return { r.getX() + 60.0f, r.getY() + 5, muteRect (l).getX() - 56.0f - (r.getX() + 60.0f), r.getHeight() - 10 };
}
juce::Rectangle<float> LoopsScreen::rulerRect() const { auto w = waveRect (0); return { w.getX(), 48.0f, w.getWidth(), 16.0f }; }

juce::Rectangle<float> LoopsScreen::sourceCell (int i) const
{
    const float x0 = 64.0f, w = ((float) getWidth() - 13.0f - x0) / 4.0f;
    return { x0 + (float) i * w, 4.0f, w, 40.0f };
}

juce::Rectangle<int> LoopsScreen::tourSpot (int which) const
{
    if (which == 1)
        return panel[(size_t) pToMac].getBounds().expanded (2);
    return rowRect (0).getUnion (rowRect (5)).expanded (2).toNearestInt();
}

double LoopsScreen::rulerBeats() const
{
    double longest = 0.0;
    for (int l = 0; l < kLoopLayers; ++l)
        if (auto t = loops.takeOf (l); t != nullptr && hasLoop (loops.state (l)))
            longest = juce::jmax (longest, t->lengthBeats.load());
    return longest > 0.0 ? juce::jmin (longest, 256.0) : juce::jmax (4, settings.recBars() * 4) * 1.0;
}

double LoopsScreen::rulerStart() const
{
    double longest = 0.0, start = 0.0;
    for (int l = 0; l < kLoopLayers; ++l)
        if (auto t = loops.takeOf (l); t != nullptr && hasLoop (loops.state (l)) && t->lengthBeats.load() > longest)
        {
            longest = t->lengthBeats.load();
            start = loops.startBeat (l);
        }
    return start;
}

void LoopsScreen::layoutPanel()
{
    auto p = panelRect();
    const float bw = (p.getWidth() - 16.0f - 18.0f) / 4.0f;
    auto cell = [&] (int row, int col) { return juce::Rectangle<float> (p.getX() + 8.0f + (float) col * (bw + 6.0f), p.getY() + 32.0f + (float) row * 50.0f, bw, 44.0f).toNearestInt(); };
    for (int i = 0; i < kPanelButtons; ++i)
        panel[(size_t) i].setBounds (cell (i / 4, i % 4));

    for (auto& b : panel)
        b.setVisible (false);
    panel[pToMac].setVisible (true);
    panel[pClearAll].setVisible (true);
    panel[pToMac].setText ("To Mac");
    panel[pToMac].setLook (Look::amber);
    panel[pClearAll].setText ("Clear all");

    const int l = selected;
    if (l < 0 || l >= kLoopLayers)
        return;
    const auto st = loops.state (l);
    auto take = loops.takeOf (l);
    if (st == LoopState::waiting || st == LoopState::recording)
    {
        panel[pQuant].setVisible (true);
        panel[pQuant].setText (st == LoopState::waiting ? "Call off" : "Stop");
        panel[pQuant].setLook (st == LoopState::waiting ? Look::amber : Look::red);
        return;
    }
    if (! hasLoop (st) || take == nullptr)
        return;
    for (auto& b : panel)
        b.setVisible (true);
    const bool notes = take->part != nullptr;
    static const char* grids[] = { "", " 1/4", " 1/8", " 1/16" };
    panel[pQuant].setText (notes ? juce::String ("Quantize") + grids[juce::jlimit (0, 3, take->quant.grid)] : juce::String ("Fit"));
    panel[pQuant].setLook (notes && take->quant.grid > 0 ? Look::amber : Look::cream);
    panel[pInOut].setText ("In / Out");
    panel[pFx].setText ("Effects");
    panel[pFx].setLook (loops.fxOf (l).isNeutral() ? Look::cream : Look::amber);
    const bool dubbing = st == LoopState::overdubbing || st == LoopState::overdubWait;
    panel[pDub].setText (dubbing ? "Stop dub" : "Dub");
    panel[pDub].setLook (dubbing ? Look::red : Look::dark);
    bool has = false;
    const auto t = openToolFor (take->tool, has);
    panel[pTool].setText (has ? juce::String ("Open ") + toolName (t) : juce::String ("Double"));
    panel[pTool].setFontHeight (has && t == Tool::vocoder ? 13.0f : 15.0f);
    panel[pClear].setText ("Clear");
    panel[pToMac].setText ("To Mac");
}

void LoopsScreen::resized()
{
    layoutPanel();
    if (editor.isVisible())
        editor.setBounds (0, editorTop(), getWidth(), getHeight() - editorTop());
}

//==============================================================================
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

void LoopsScreen::attachPart (int layer, std::shared_ptr<const NotePart> part)
{
    if (! juce::isPositiveAndBelow (layer, kLoopLayers))
        return;
    if (auto t = loops.takeOf (layer))
    {
        t->part = std::move (part);
        t->quant = {};
        settingsDirty = true;
        layoutPanel();
    }
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
        const bool stateChanged = st != r.st || r.muted != loops.isMuted (l);
        r.st = st;
        r.muted = loops.isMuted (l);
        if (changed)
        {
            r.version = v;
            r.peaks = loops.peaks (l, kPeakBins);
        }
        if (stateChanged && l == selected)
            layoutPanel();
    }
}

void LoopsScreen::timerCallback()
{
    // the tempo: once it has settled, layers that aren't at it are stretched to it (one at a time)
    const double bpm = loops.bpm();
    const auto now = juce::Time::getMillisecondCounter();
    if (std::abs (bpm - shownBpm) > 1.0e-6)
    {
        shownBpm = bpm;
        bpmChangedAt = now;
    }
    else if (now - bpmChangedAt > 400 && ! demo)
        followTempo();

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
        layoutPanel();
    }
    repaint();
}

bool LoopsScreen::startRecording (int layer, int source, int bars, int tool, int tap)
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
    if (! loops.arm (layer, src, bars, tool, tap))
    {
        host.toast ("That layer is busy. Try again in a moment.");
        return false;
    }
    pushUndo (std::move (u));
    selected = lastLayer = layer;
    layoutPanel();
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

bool LoopsScreen::hasLayer (int l) const
{
    if (! juce::isPositiveAndBelow (l, kLoopLayers))
        return false;
    const auto st = const_cast<LoopsEngine&> (loops).state (l);
    return st == LoopState::playing || st == LoopState::overdubWait || st == LoopState::overdubbing;
}

int LoopsScreen::recordFrom (int source, int bars, int tool, int tap)
{
    const int layer = nextLayer();
    if (layer < 0)
    {
        host.toast ("All 8 layers are used. Clear one in Loops to record.");
        return -1;
    }
    return startRecording (layer, source, bars, tool, tap) ? layer : -1;
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
            loops.setLayer (one.layer, one.before, one.mute, one.start);
            if (one.withFx)
            {
                loops.setFx (one.layer, one.fx);
                loops.board (layerBoardId (one.layer)).fromVar (one.board);
            }
        }
        lastLayer = one.layer;
    }
    if (editor.isVisible())
        editor.layerChanged();
    layoutPanel();
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
            u.items.push_back ({ layer, take, loops.isMuted (layer), true, loops.fxOf (layer), loops.startBeat (layer), loops.board (layerBoardId (layer)).toVar() });
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
            u.items.push_back ({ l, loops.takeOf (l), loops.isMuted (l), true, loops.fxOf (l), loops.startBeat (l), loops.board (layerBoardId (l)).toVar() });
        loops.clear (l);
    }
    if (n == 0)
        return host.toast ("No layers to clear");
    if (! u.items.empty())
        pushUndo (std::move (u));
    selected = -1;
    closeEditor();
    layoutPanel();
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
    else if (id >= rowSelect && id < rowSelect + kLoopLayers)
    {
        const int l = id - rowSelect;
        selected = lastLayer = (selected == l && hasLoop (loops.state (l))) ? -1 : l;   // tap it again to let go
        layoutPanel();
    }
    else if (id >= rowMute && id < rowMute + kLoopLayers)
    {
        const int l = id - rowMute;
        loops.setMute (l, ! loops.isMuted (l));
        lastLayer = l;
    }
}

void LoopsScreen::pressPanel (int which)
{
    if (which == pToMac)
        return sendToMac (true);
    if (which == pClearAll)
        return clearAll();
    const int l = selected;
    if (l < 0)
        return;
    const auto st = loops.state (l);
    if (st == LoopState::waiting || st == LoopState::recording)
    {
        if (which == pQuant)
            stopTake (l);
        return;
    }
    auto take = loops.takeOf (l);
    if (! hasLoop (st) || take == nullptr)
        return;
    const bool dubbing = st == LoopState::overdubbing || st == LoopState::overdubWait;
    switch (which)
    {
        case pQuant:
        case pInOut:
        case pFx:
            if (dubbing)
                return host.toast ("Let the dub finish first (or STOP DUB).");
            openEditor (l, which == pFx ? LoopEditSheet::effectsPage : LoopEditSheet::shapePage,
                        which == pQuant ? (take->part != nullptr ? LoopEditSheet::quantizeFocus : LoopEditSheet::fitFocus) : LoopEditSheet::noFocus);
            break;
        case pDub:
            if (dubbing)
                loops.stop (l);
            else
            {
                Undo u;
                u.items.push_back ({ l, loops.copyOf (l), loops.isMuted (l), false, {}, loops.startBeat (l) });
                if (loops.dub (l))
                {
                    pushUndo (std::move (u));
                    if (take->part != nullptr)
                        host.toast ("Dubbing over it: the layer is audio from now on, so QUANTIZE goes (UNDO brings it back).");
                    take->part = nullptr;   // what's dubbed isn't in the notes
                    take->origin = nullptr;
                }
            }
            break;
        case pTool:
        {
            bool has = false;
            const auto t = openToolFor (take->tool, has);
            if (! has)
                editLayer (l, LoopEditSheet::twice, 0, 0);
            else if (onOpenTool)
                onOpenTool (t);
            break;
        }
        case pClear:
            clearLayer (l, true);
            host.toast ("Cleared layer " + juce::String (l + 1) + ". Undo brings it back.");
            break;
        default: break;
    }
    layoutPanel();
}

int LoopsScreen::idAt (juce::Point<float> p) const
{
    for (int i = 0; i < 4; ++i)
        if (sourceCell (i).contains (p))
            return srcBase + i;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        if (loops.state (l) != LoopState::empty && muteRect (l).expanded (3, 2).contains (p))
            return rowMute + l;
        if (rowRect (l).expanded (0, 2).contains (p))
            return rowSelect + l;
    }
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

void LoopsScreen::openEditor (int layer, int page, int focus)
{
    if (layer < 0 || loops.state (layer) != LoopState::playing)
        return host.toast (hasLoop (loops.state (layer)) ? "Let the overdub finish first." : "Record this layer first.");
    selected = lastLayer = layer;
    editor.open (layer, page, focus);
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
    layoutPanel();
}

void LoopsScreen::closeEditor()
{
    if (! editor.isVisible())
        return;
    editor.close();
    juce::Desktop::getInstance().getAnimator().cancelAnimation (&editor, false);
    editor.setVisible (false);
    layoutPanel();
    repaint();
}

//==============================================================================
// Edits: TRIM, HALF, DOUBLE and REVERSE at once; FIT, QUANTIZE and the tempo on the worker
void LoopsScreen::applyEdited (int layer, const LoopTake* was, Edited e)
{
    auto take = loops.takeOf (layer);
    if (e.error.isNotEmpty())
        return host.toast (e.error);
    if (e.take == nullptr || take == nullptr || take.get() != was)
        return;
    const double start = loops.startBeat (layer);
    Undo u;
    u.items.push_back ({ layer, take, loops.isMuted (layer), true, loops.fxOf (layer), start, loops.board (layerBoardId (layer)).toVar() });
    pushUndo (std::move (u));
    const double len = e.take->lengthBeats.load();
    double newStart = start + e.shift;
    if (len > 0)
        newStart -= std::floor (newStart / len) * len;
    loops.setLayer (layer, e.take, loops.isMuted (layer), newStart);
    loops.setFx (layer, e.fx);
    if (e.said.isNotEmpty())
        host.toast (e.said);
    layoutPanel();
}

void LoopsScreen::editLayer (int layer, int edit, double fromBeat, double toBeat)
{
    if (layer < 0 || loops.state (layer) != LoopState::playing)
        return host.toast ("Let the layer finish recording first.");
    if (jobLayer == layer)
        return host.toast (U8("Still working on the last change\xe2\x80\xa6"));
    auto take = loops.takeOf (layer);
    if (take == nullptr)
        return;
    const int len = take->length.load();
    const double beatsLen = take->lengthBeats.load();
    if (len < 2 || beatsLen <= 0)
        return;
    const float* d = take->data.data();
    Edited e;
    e.fx = loops.fxOf (layer);
    auto made = [&] (int samples)
    {
        auto m = LoopsEngine::makeTake (samples, take->sampleRate);
        m->length = samples;
        m->source = take->source;
        m->tool = take->tool;
        return m;
    };
    const juce::String asAudio = take->part != nullptr ? juce::String (" It's plain audio now: UNDO brings QUANTIZE back.") : juce::String();

    switch (edit)
    {
        case LoopEditSheet::trim:
            e = trimTake (*take, e.fx, fromBeat, toBeat);
            break;
        case LoopEditSheet::half:
        {
            if (beatsLen < 2.0 - 1.0e-6)
                return host.toast ("It's one beat already.");
            const int b = juce::jmax (1, len / 2);
            e.take = made (b);
            std::copy (d, d + b, e.take->data.begin());
            e.take->lengthBeats = beatsLen / 2.0;
            if (take->content > 0 && take->content < b)
                e.take->content = take->content;
            e.fx.remap (0.0, 0.5, 1, false);
            e.said = "Half: the first " + juce::String (beatsLen / 2.0, beatsLen / 2.0 == std::floor (beatsLen / 2.0) ? 0 : 1) + " beats." + asAudio;
            break;
        }
        case LoopEditSheet::twice:
        {
            if ((double) len * 2.0 / take->sampleRate > 600.0)
                return host.toast ("Doubled it would pass 10 minutes.");
            e.take = made (len * 2);
            std::copy (d, d + len, e.take->data.begin());
            std::copy (d, d + len, e.take->data.begin() + len);
            e.take->lengthBeats = beatsLen * 2.0;
            e.fx.remap (0.0, 1.0, 2, false);
            e.said = "Doubled: it plays twice, so you can dub something different over the second half." + asAudio;
            break;
        }
        case LoopEditSheet::reverse:
        {
            e.take = made (len);
            std::reverse_copy (d, d + len, e.take->data.begin());
            e.take->lengthBeats = beatsLen;
            e.fx.remap (0.0, 1.0, 1, true);
            e.said = "Reversed. Undo turns it back." + asAudio;
            break;
        }
        default: return;
    }
    applyEdited (layer, take.get(), std::move (e));
}

void LoopsScreen::fitLayer (int layer, int bars, double fromBeat, double toBeat)
{
    auto take = loops.takeOf (layer);
    if (take == nullptr || loops.state (layer) != LoopState::playing)
        return host.toast ("Let the layer finish recording first.");
    auto copy = loops.copyOf (layer);
    auto fx = loops.fxOf (layer);
    const double bpm = loops.bpm();
    runJob (layer, [copy, fx, fromBeat, toBeat, bars, bpm] { return fitTake (*copy, fx, fromBeat, toBeat, bars, bpm); }, true);
}

void LoopsScreen::quantizeLayer (int layer, const LoopQuantize& q)
{
    auto take = loops.takeOf (layer);
    if (take == nullptr || take->part == nullptr || loops.state (layer) != LoopState::playing)
        return host.toast ("QUANTIZE works on a layer made from notes (PADS or SYNTH), before it's trimmed or dubbed.");
    if (take->part->kind == NotePart::pads && take->part->kit.empty() && kitSounds)
    {
        auto filled = std::make_shared<NotePart> (*take->part);
        filled->kit = kitSounds();
        take->part = filled;
    }
    auto copy = loops.copyOf (layer);
    auto fx = loops.fxOf (layer);
    const double bpm = loops.bpm();
    runJob (layer, [copy, fx, q, bpm] { return quantizeTake (*copy, fx, q, bpm); }, true);
}

void LoopsScreen::followTempo()
{
    if (jobLayer >= 0)
        return;
    const double bpm = loops.bpm();
    for (int l = 0; l < kLoopLayers; ++l)
    {
        auto take = loops.takeOf (l);
        if (take == nullptr || loops.state (l) != LoopState::playing || loops.settling (l))
            continue;
        const double native = nativeBpm (*take);
        if (native <= 0.0 || std::abs (native - bpm) / bpm < 0.002)
            continue;
        if (take->part != nullptr && take->part->kind == NotePart::pads && take->part->kit.empty() && kitSounds)
        {
            auto filled = std::make_shared<NotePart> (*take->part);
            filled->kit = kitSounds();
            take->part = filled;
        }
        auto copy = loops.copyOf (l);
        runJob (l, [copy, bpm] { Edited e; e.take = tempoTake (*copy, bpm); return e; }, false);
        return;   // one at a time
    }
}

void LoopsScreen::runJob (int layer, std::function<Edited()> work, bool withUndo)
{
    if (jobLayer >= 0)
    {
        if (withUndo)
            host.toast (U8("Still working on the last change\xe2\x80\xa6"));
        return;
    }
    auto take = loops.takeOf (layer);
    if (take == nullptr)
        return;
    jobLayer = layer;
    jobUndo = withUndo;
    jobWas = take.get();
    const int id = ++jobId;
    if (syncJobs)
        return jobDone (id, work());
    juce::Thread::launch ([work = std::move (work), safe = juce::Component::SafePointer<LoopsScreen> (this), id]
    {
        auto e = work();
        juce::MessageManager::callAsync ([safe, id, e = std::move (e)]() mutable
        {
            if (safe != nullptr)
                safe->jobDone (id, std::move (e));
        });
    });
    repaint();
}

void LoopsScreen::jobDone (int id, Edited e)
{
    if (id != jobId || jobLayer < 0)
        return;
    const int layer = jobLayer;
    const bool withUndo = jobUndo;
    const auto* was = jobWas;
    jobLayer = -1;
    jobWas = nullptr;
    auto take = loops.takeOf (layer);
    if (take == nullptr || take.get() != was || loops.state (layer) != LoopState::playing)
        return;   // it changed meanwhile: the tempo follower looks again, an edit is simply dropped
    if (withUndo)
        applyEdited (layer, was, std::move (e));
    else if (e.take != nullptr)
        loops.setLayer (layer, e.take, loops.isMuted (layer), loops.startBeat (layer));   // the tempo: no undo step, effects as they are
    repaint();
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
    drawWell (g, r, 11.0f);
    if (selected == l)
    {
        g.setColour (col::amber);
        g.drawRoundedRectangle (r.reduced (1.0f), 11.0f, 2.2f);
    }

    g.setColour (kRowText);
    g.setFont (condensed (19.0f));
    g.drawText (juce::String (l + 1), juce::Rectangle<float> (r.getX() + 7, r.getY(), 18, r.getHeight()), juce::Justification::centredLeft);
    auto iconBox = juce::Rectangle<float> (r.getX() + 25, r.getCentreY() - 15, 30, 30);

    if (st == LoopState::empty)
    {
        // Empty: the one the next take lands on says so (tap another empty row to pick it instead).
        const bool next = l == nextLayer();
        juce::Path outline, dash;
        outline.addRoundedRectangle (iconBox, 7.0f);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.6f).createDashedStroke (dash, outline, dashes, 2);
        g.setColour (next ? kRowRed : kRowGrey.withAlpha (0.6f));
        g.fillPath (dash);
        g.setFont (spaced (condensedBold (14.0f), 0.1f));
        if (next)
        {
            g.fillEllipse (iconBox.getRight() + 10, r.getCentreY() - 3.5f, 7, 7);
            g.drawFittedText ("NEXT RECORDING LANDS HERE", juce::Rectangle<float> (iconBox.getRight() + 23, r.getY(), r.getRight() - iconBox.getRight() - 30, r.getHeight()).toNearestInt(),
                              juce::Justification::centredLeft, 1, 0.8f);
        }
        else
            g.drawText ("EMPTY", juce::Rectangle<float> (iconBox.getRight() + 10, r.getY(), 120, r.getHeight()), juce::Justification::centredLeft);
        return;
    }

    auto take = loops.takeOf (l);
    const int tool = take != nullptr ? take->tool : 0;
    const auto colour = layerToolColour (tool);
    const bool muted = row.muted && hasLoop (st);
    g.setColour (juce::Colour (0xff2c2620));
    g.fillRoundedRectangle (iconBox, 7.0f);
    drawLayerToolIcon (g, tool, iconBox.getCentre(), muted ? colour.withAlpha (0.4f) : colour);

    // the name (or what it's doing) on the right, before M
    auto wave = waveRect (l);
    juce::String label = layerToolName (tool);
    auto labelColour = muted ? kRowGrey : colour.withAlpha (0.85f);
    if (st == LoopState::recording) { label = "REC"; labelColour = kRowRed; }
    else if (st == LoopState::waiting) { label = "WAIT"; labelColour = kRowAmber; }
    else if (st == LoopState::overdubbing) { label = "DUB"; labelColour = kRowRed; }
    else if (st == LoopState::overdubWait) { label = "DUB NEXT"; labelColour = kRowAmber; }
    g.setColour (labelColour);
    g.setFont (spaced (condensedBold (12.0f), 0.08f));
    g.drawFittedText (label, juce::Rectangle<float> (wave.getRight() + 3, r.getY(), 50, r.getHeight()).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);

    if (st == LoopState::waiting)
    {
        g.setColour (kRowAmber);
        g.setFont (spaced (condensedBold (14.0f), 0.1f));
        g.drawFittedText (loops.countingIn() ? "COUNT-IN, THEN REC" : "STARTS NEXT BAR", wave.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }
    else if (st == LoopState::recording)
    {
        if (! row.peaks.empty())
        {
            const float pitch = wave.getWidth() / (float) row.peaks.size();
            g.setColour (kRowRed);
            for (int i = 0; i < (int) row.peaks.size(); ++i)
            {
                const float p = std::sqrt (juce::jlimit (0.0f, 1.0f, row.peaks[(size_t) i] * 1.6f));
                const float h = juce::jmax (2.0f, p * wave.getHeight());
                g.fillRoundedRectangle (wave.getX() + (float) i * pitch, wave.getCentreY() - h * 0.5f, juce::jmax (1.4f, pitch - 1.2f), h, 0.8f);
            }
        }
    }
    else if (take != nullptr && take->lengthBeats.load() > 0)
    {
        // on the ruler's time: a short loop shows as many times as it plays in the longest one
        const double L = rulerBeats(), S0 = rulerStart(), len = take->lengthBeats.load(), start = loops.startBeat (l);
        const auto c = muted ? colour.withAlpha (0.3f) : colour;
        auto layerBeatAt = [&] (double r01)
        {
            double b = std::fmod (S0 + r01 * L - start, len);
            return b < 0 ? b + len : b;
        };
        if (take->part != nullptr)
        {
            // notes: little bars, high notes (or later pads) up
            const auto& part = *take->part;
            const double patLen = juce::jmax (1.0, part.patternBeats());
            struct N { double beat, length; int row; };
            std::vector<N> notes;
            int lo = 127, hi = 0;
            if (part.kind == NotePart::synth)
            {
                for (auto& n : part.notes.notes)
                {
                    notes.push_back ({ quantizedBeat (n.beat, take->quant, patLen), juce::jmax (0.12, n.length), n.note });
                    lo = juce::jmin (lo, n.note);
                    hi = juce::jmax (hi, n.note);
                }
            }
            else
            {
                for (auto& h : part.pattern.hits)
                {
                    notes.push_back ({ quantizedBeat (h.beat, take->quant, patLen), 0.18, h.pad % 16 });
                    lo = juce::jmin (lo, h.pad % 16);
                    hi = juce::jmax (hi, h.pad % 16);
                }
            }
            const int span = juce::jmax (4, hi - lo);
            g.setColour (c);
            const double pxPerBeat = wave.getWidth() / L;
            for (double copyStart = -std::fmod (S0 - start, len) - len; copyStart < L; copyStart += len)
                for (double rep = 0; rep < len - 1.0e-9; rep += patLen)
                    for (auto& n : notes)
                    {
                        const double at = copyStart + rep + n.beat;
                        if (at + n.length < 0 || at >= L)
                            continue;
                        const float x0 = wave.getX() + (float) (juce::jmax (0.0, at) * pxPerBeat);
                        const float x1 = wave.getX() + (float) (juce::jmin (L, at + n.length) * pxPerBeat);
                        const float y = wave.getBottom() - 3.0f - (float) (n.row - lo) / (float) span * (wave.getHeight() - 8.0f);
                        g.fillRoundedRectangle (x0, y - 2.5f, juce::jmax (2.5f, x1 - x0 - 1.0f), 4.0f, 1.5f);
                    }
        }
        else if (! row.peaks.empty())
        {
            const int bins = (int) row.peaks.size();
            const double sound = (double) take->contentSamples() / juce::jmax (1, take->length.load());
            g.setColour (c);
            for (float x = 0; x < wave.getWidth() - 1.0f; x += 3.0f)
            {
                const double lb = layerBeatAt ((x + 1.0) / wave.getWidth()) / len;
                if (lb > sound)
                    continue;   // the silence a trim padded it with
                const float p = std::sqrt (juce::jlimit (0.0f, 1.0f, row.peaks[(size_t) juce::jlimit (0, bins - 1, (int) (lb * bins))] * 1.6f));
                const float h = juce::jmax (2.0f, p * wave.getHeight());
                g.fillRoundedRectangle (wave.getX() + x, wave.getCentreY() - h * 0.5f, 2.0f, h, 0.8f);
            }
        }
        if (jobLayer == l)
        {
            g.setColour (juce::Colour (0xcc1a1613));
            g.fillRect (wave);
            g.setColour (kRowText);
            g.setFont (spaced (condensedBold (13.0f), 0.1f));
            g.drawText (U8("WORKING\xe2\x80\xa6"), wave, juce::Justification::centred);
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
    g.drawText ("FROM", juce::Rectangle<float> (16, 4, 48, 40), juce::Justification::centredLeft);
    for (int i = 0; i < 4; ++i)
        drawChoice (g, sourceCell (i), kSourceNames[i], sourceChoice == i);

    // the bar ruler over the waves
    const double L = rulerBeats();
    auto ruler = rulerRect();
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (12.0f), 0.08f));
    g.drawText (barsText (L),
                juce::Rectangle<float> (16, ruler.getY() - 2, ruler.getX() - 18, 18), juce::Justification::centredLeft);
    g.setColour (col::amber);
    g.fillRoundedRectangle (ruler.getX(), ruler.getY() + 4, ruler.getWidth(), 3.0f, 1.5f);
    const int bars = juce::jmax (1, (int) std::ceil (L / 4.0 - 1.0e-6));
    const int every = bars <= 8 ? 1 : bars <= 16 ? 2 : bars <= 32 ? 4 : 8;
    for (int b = 0; b < bars; ++b)
    {
        const float x = ruler.getX() + (float) (b * 4.0 / L) * ruler.getWidth();
        g.setColour (col::inkDim);
        g.fillRect (x, ruler.getY() + 2, 1.0f, b % every == 0 ? 12.0f : 7.0f);
        if (b % every == 0)
        {
            g.setFont (spaced (condensedBold (11.0f), 0.04f));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 3, ruler.getY() + 5, 22, 12), juce::Justification::centredLeft);
        }
    }

    for (int l = 0; l < kLoopLayers; ++l)
        drawRow (g, l);

    // the playhead, across the rows
    if (loops.transportRunning() && layerCount() > 0)
    {
        double ph = std::fmod (loops.beatNow() - rulerStart(), L);
        if (ph < 0)
            ph += L;
        const float x = ruler.getX() + (float) (ph / L) * ruler.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.fillRect (x - 1.0f, rowRect (0).getY() + 3, 2.0f, rowRect (kLoopLayers - 1).getBottom() - rowRect (0).getY() - 6);
        if (! editor.isVisible() || editor.getY() > (int) ruler.getBottom())
        {
            g.setColour (col::ink);
            g.fillRect (x - 1.0f, ruler.getY(), 2.0f, ruler.getHeight());
        }
    }

    // the panel: what the picked layer can do
    auto p = panelRect();
    g.setColour (col::creamLight);
    g.fillRoundedRectangle (p, 14.0f);
    g.setColour (col::rule);
    g.drawRoundedRectangle (p.reduced (0.5f), 14.0f, 1.4f);
    auto titleRow = p.reduced (12, 0).withHeight (30).translated (0, 2);
    const int l = selected;
    auto take = l >= 0 ? loops.takeOf (l) : nullptr;
    const auto st = l >= 0 ? loops.state (l) : LoopState::empty;
    juce::String title, right, hint;
    if (l >= 0 && hasLoop (st) && take != nullptr)
    {
        title = "Layer " + juce::String (l + 1) + U8(" \xc2\xb7 ") + titleCase (layerToolName (take->tool));
        const double beatsLen = take->lengthBeats.load() * (double) take->contentSamples() / juce::jmax (1, take->length.load());
        right = juce::String (take->part != nullptr ? "NOTES" : "AUDIO") + U8(" \xc2\xb7 ") + barsText (beatsLen);
    }
    else if (l >= 0 && (st == LoopState::waiting || st == LoopState::recording))
    {
        title = "Layer " + juce::String (l + 1) + (st == LoopState::waiting ? U8(" \xc2\xb7 waiting for the bar") : U8(" \xc2\xb7 recording"));
        hint = st == LoopState::waiting ? "CALL OFF, or REC on the thumb bar." : "STOP ends it at the end of the bar.";
    }
    else if (l >= 0 && st == LoopState::empty && l == nextLayer())
    {
        title = "Layer " + juce::String (l + 1) + " is empty";
        hint = "The next take lands here. Pick a tool below, press REC.";
    }
    else
    {
        title = layerCount() > 0 ? "Tap a layer to work on it" : "No layers yet";
        hint = layerCount() > 0 ? "IN / OUT, quantize or fit, effects, dub." : "Pick a tool below, press REC: each take lands on its own layer, in time.";
    }
    g.setColour (col::ink);
    g.setFont (condensedBold (19.0f));
    g.drawFittedText (title, titleRow.withTrimmedRight (right.isNotEmpty() ? 120 : 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    if (right.isNotEmpty())
    {
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.08f));
        g.drawText (right, titleRow, juce::Justification::centredRight);
    }
    if (hint.isNotEmpty())
    {
        // in the first row of the panel, beside whatever button is there
        auto area = juce::Rectangle<float> (p.getX() + 12, p.getY() + 32, p.getWidth() - 24, 44);
        if (panel[pQuant].isVisible())
            area.setLeft ((float) panel[pQuant].getRight() + 10);
        g.setColour (col::inkDim);
        g.setFont (small (13.5f));
        g.drawFittedText (hint, area.toNearestInt(), juce::Justification::centredLeft, 2, 0.85f);
    }
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
                                                  || (! loops.recMoves (l) && fxHash (loops.fxOf (l)) != s.fx)
                                                  || loops.board (layerBoardId (l)).hash() != s.board)))
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
        const bool present = take != nullptr && hasLoop (st);
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
        s = { take.get(), loops.version (l), true, loops.isMuted (l), fxHash (fx), loops.board (layerBoardId (l)).hash() };
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
        // the layer's EFFECTS board (after the knobs), when it has one
        if (const auto b = loops.board (layerBoardId (l)).toVar(); ! b.isVoid())
            o->setProperty ("board", b);
        o->setProperty ("layer", l);
        o->setProperty ("file", file.getFileName());
        o->setProperty ("beats", take->lengthBeats.load());
        o->setProperty ("mute", loops.isMuted (l));
        o->setProperty ("source", take->source);
        o->setProperty ("tool", take->tool);
        o->setProperty ("start", loops.startBeat (l));
        if (take->content > 0)
            o->setProperty ("content", take->content);
        if (take->part != nullptr)
        {
            o->setProperty ("part", take->part->toVar());
            auto* q = new juce::DynamicObject();
            q->setProperty ("grid", take->quant.grid);
            q->setProperty ("strength", take->quant.strength);
            q->setProperty ("swing", take->quant.swing);
            o->setProperty ("quantize", juce::var (q));
        }
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
        if (! loops.fxOf (l).isNeutral() || ! loops.board (layerBoardId (l)).isNeutral())
            if (auto printed = loops.printed (l))
                b = std::move (*printed);   // with its effects and moves and its board, in stereo
        if (std::abs (take->sampleRate - crate::kSampleRate) > 1.0)
            b = crate::AudioFiles::resample (b, take->sampleRate);   // the phone records at its own rate; sounds travel at 44.1 kHz
        send.items.push_back ({ "Pocket loop " + juce::String (l + 1) + " " + titleCase (layerToolName (take->tool)),
                                crate::share (std::move (b)), take->lengthBeats.load() });
    };
    if (onlyPicked && selected >= 0 && hasLoop (loops.state (selected)))
        addLayer (selected);
    else
        for (int l = 0; l < kLoopLayers; ++l)
            if (hasLoop (loops.state (l)))
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
    jobLayer = -1;   // a job still out for the old song is dropped when it comes back
    ++jobId;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        loops.setLayer (l, nullptr, false);
        saved[(size_t) l] = {};
    }
    undoStack.clear();
    selected = lastLayer = vocLayer = -1;   // the new song's next take goes on its first empty layer
    restore();
    layoutPanel();
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
            take->tool = item.hasProperty ("tool") ? juce::jlimit (0, (int) LayerTool::count - 1, (int) item.getProperty ("tool", 0)) : toolForSource (take->source);
            take->content = juce::jlimit (0, n, (int) item.getProperty ("content", 0));
            if (auto part = NotePart::fromVar (item.getProperty ("part", {})))
            {
                take->part = part;
                const auto q = item.getProperty ("quantize", {});
                take->quant.grid = juce::jlimit (0, 3, (int) q.getProperty ("grid", 0));
                take->quant.strength = (float) (double) q.getProperty ("strength", 1.0);
                take->quant.swing = (bool) q.getProperty ("swing", false);
            }
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
            loops.board (layerBoardId (l)).fromVar (item.getProperty ("board", {}));   // older songs: none, neutral
            saved[(size_t) l] = { take.get(), 0, true, (bool) item.getProperty ("mute", false), fxHash (loops.fxOf (l)), loops.board (layerBoardId (l)).hash() };
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
    syncJobs = true;
    stopTimer();
    const double bpm = 92.0, sr = 44100.0;
    beats.setBpm (bpm);
    loops.prepare (sr);
    loops.setLatency (0);
    loops.setCountInBars (0);
    loops.setMic (true, false);
    Pump pump (beats, loops);

    // 1 drums, 2 a synth bass part, 3 a vocoder take recorded FREE (4.2 bars of sound in 5), 4 a pads pattern,
    // 5 your voice (muted), 6 where the next take lands
    auto drums = syntheticTake (0, 16.0, bpm, sr);
    drums->tool = (int) LayerTool::drums;
    loops.setLayer (0, drums, false);

    auto bass = std::make_shared<NotePart>();
    bass->kind = NotePart::synth;
    bass->preset = juce::jmax (0, synthPresetIndex ("Rubber Funk"));
    bass->notes.bars = 1;
    const double line[][3] = { { 0.0, 0.4, 45 }, { 0.78, 0.2, 45 }, { 1.05, 0.4, 48 }, { 1.53, 0.2, 52 }, { 2.0, 0.6, 50 }, { 2.77, 0.2, 48 }, { 3.02, 0.4, 43 }, { 3.5, 0.3, 45 } };
    for (auto& n : line)
        bass->notes.notes.push_back ({ n[0], n[1], (int) n[2] });
    bass->lengthBeats = 16.0;
    bass->startBeat = 16.0;
    const int bassSamples = (int) (16.0 * 60.0 / bpm * sr);
    auto bassTake = LoopsEngine::makeTake (bassSamples, sr);
    auto rendered = renderPart (*bass, {}, bpm, sr, bassSamples);
    std::copy (rendered.begin(), rendered.end(), bassTake->data.begin());
    bassTake->length = bassSamples;
    bassTake->lengthBeats = 16.0;
    bassTake->source = (int) LoopSource::synth;
    bassTake->tool = (int) LayerTool::synth;
    bassTake->part = bass;
    loops.setLayer (1, bassTake, false);

    {
        const int n = (int) (20.0 * 60.0 / bpm * sr), sound = (int) (4.2 * 4.0 * 60.0 / bpm * sr);
        auto voc = LoopsEngine::makeTake (n, sr);
        juce::Random rng (5);
        for (int i = 0; i < sound; ++i)
        {
            const double t = (double) i / sr;
            const double env = std::pow (std::sin (juce::MathConstants<double>::pi * std::fmod (t * bpm / 60.0 / 2.0, 1.0)), 0.6) * (0.5 + 0.5 * std::sin (t * 1.3));
            voc->data[(size_t) i] = (float) (env * (0.55 * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.15 * (rng.nextFloat() * 2 - 1)));
        }
        voc->length = n;
        voc->lengthBeats = 20.0;
        voc->content = sound;
        voc->source = (int) LoopSource::vocoder;
        voc->tool = (int) LayerTool::vocoder;
        loops.setLayer (2, voc, false);
    }

    auto padsPart = std::make_shared<NotePart>();
    padsPart->kind = NotePart::pads;
    padsPart->pattern.bars = 2;
    for (double b = 0; b < 8.0; b += 0.5)
        padsPart->pattern.hits.push_back ({ std::fmod (b, 2.0) < 0.1 ? 0 : (std::fmod (b, 1.0) > 0.1 ? 3 : 6), b + (std::fmod (b * 7.0, 3.0) - 1.0) * 0.02 });
    padsPart->lengthBeats = 8.0;
    auto pads = syntheticTake (0, 8.0, bpm, sr);
    pads->tool = (int) LayerTool::pads;
    pads->part = padsPart;
    loops.setLayer (3, pads, false);

    auto voice = syntheticTake (1, 16.0, bpm, sr);
    voice->tool = (int) LayerTool::mic;
    loops.setLayer (4, voice, true);

    LayerFxState fx;
    fx.knob[fxReverb] = 0.3f;
    loops.setFx (2, fx);

    pump.run (512);
    loops.setTransport (true);
    pump.run ((int) (60.0 / bpm * sr * 5.3));   // into bar 2
    sourceChoice = 0;
    selected = 1;
    refresh();
    layoutPanel();
    repaint();
}

void LoopsScreen::showEditorDemo (int which)
{
    Pump pump (beats, loops);
    if (which == 0)
    {
        selected = 2;
        openEditor (2, LoopEditSheet::shapePage);
        editor.setSnap (1);
    }
    else if (which == 1)
    {
        quantizeLayer (1, [] { LoopQuantize q; q.grid = 3; return q; }());
        pump.run (1024);
        selected = 1;
        openEditor (1, LoopEditSheet::shapePage);
        editor.setSnap (0);
        editor.setMarkers (4.0, 12.0);
    }
    else
    {
        // a filter sweep recorded, some reverb and delay
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
        openEditor (1, LoopEditSheet::effectsPage);
    }
    juce::Desktop::getInstance().getAnimator().cancelAllAnimations (true);
    editor.setBounds (0, editorTop(), getWidth(), getHeight() - editorTop());
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

