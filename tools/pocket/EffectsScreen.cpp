#include "EffectsScreen.h"
#include "PocketFrame.h"

using namespace pocket;

namespace
{
const juce::Colour kTileOff { 0xff3a2f4f }, kTileText { 0xffb7a9c2 }, kLabel { 0xffb7a9c2 };
const juce::Colour kGreenBg { 0xff17332f }, kGreenEdge { 0xff2fc6a8 }, kAmberBg { 0xff3a2f1c };
const juce::Colour kKnobFaces[4] { juce::Colour (0xffa877f5), juce::Colour (0xff2fc6a8), juce::Colour (0xffc9bcd6), juce::Colour (0xfff59a2a) };
constexpr float kRowH = 46.0f;
}

EffectsScreen::EffectsScreen (Model m) : model (std::move (m))
{
    setOpaque (true);
    tunerBuffer.assign ((size_t) StripFx::kTunerSize, 0.0f);
}

EffectsScreen::~EffectsScreen()
{
    if (tuner)
        model.board (boardVoice).setTuner (false);
}

//==============================================================================
const StripFx& EffectsScreen::view() const
{
    if (auto* e = model.board (src).existing())
        return *e;
    if (fresh == nullptr)
        fresh = std::make_unique<StripFx>();   // only read: the starting values
    return *fresh;
}

juce::RangedAudioParameter* EffectsScreen::param (const char* id) const
{
    return const_cast<StripFx&> (view()).params.getParameter (id);
}

float EffectsScreen::norm (const char* id) const
{
    auto* p = param (id);
    return p != nullptr ? p->getValue() : 0.0f;
}

void EffectsScreen::setNorm (const char* id, float n)
{
    if (auto* p = board().fx().params.getParameter (id))
        if (std::abs (p->getValue() - n) > 1.0e-6f)
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, n));
    repaint();
}

void EffectsScreen::edited()
{
    if (model.changed)
        model.changed (src);
    repaint();
}

bool EffectsScreen::isOn (int pedal) const
{
    if (pedal == kGainIn)
        return std::abs (param ("inputGain")->convertFrom0to1 (norm ("inputGain"))) > 0.05f;
    return pedalboard::isOn (view(), pedal);
}

juce::Colour EffectsScreen::pedalColour (int pedal) const
{
    if (pedal < 0 || pedal >= pedalboard::numPedals)
        return col::amber;
    return juce::Colour (pedalboard::pedals()[(size_t) pedal].colour);
}

std::vector<int> EffectsScreen::sources() const
{
    std::vector<int> s { boardDrums, boardPads, boardSynth, boardChords, boardVoice, boardVocoder };
    for (int l = 0; l < kLayerBoards; ++l)
        if (model.shown && model.shown (layerBoardId (l)))
            s.push_back (layerBoardId (l));
    s.push_back (boardAll);
    s.push_back (boardMaster);
    return s;
}

void EffectsScreen::reloadChoices()
{
    choices = boardChoices (src, model.ownFolder ? model.ownFolder() : juce::File());
    const int i = choiceIndexOf (choices, board());
    if (i >= 0)
        lastChoice = i;
    lastChoice = juce::jlimit (0, juce::jmax (0, (int) choices.size() - 1), lastChoice);
}

//==============================================================================
void EffectsScreen::setSource (int id)
{
    id = juce::jlimit (0, kBoards - 1, id);
    if (tuner && id != boardVoice)
        showTuner (false);
    src = id;
    mode = boardMode;
    padDown = false;
    reloadChoices();
    layout();
    // keep its chip in view
    for (auto& [b, r] : chipRects)
        if (b == id)
        {
            const float room = chipsR.getWidth() - 60.0f;
            if (r.getRight() - chipScroll > room + 44.0f)
                chipScroll = r.getRight() - room - 44.0f;
            if (r.getX() - chipScroll < 44.0f)
                chipScroll = r.getX() - 44.0f;
            chipScroll = juce::jlimit (0.0f, juce::jmax (0.0f, chipsWidth - chipsR.getWidth() + 16.0f), chipScroll);
        }
    repaint();
}

void EffectsScreen::selectPedal (int pedal)
{
    sel = pedal;
    layout();
    repaint();
}

void EffectsScreen::showList (bool on)
{
    mode = on ? listMode : boardMode;
    listScroll = 0.0f;
    if (on)
        reloadChoices();
    repaint();
}

void EffectsScreen::showPad (bool on)
{
    if (! on && padDown)
        padTouch (padDot.x, padDot.y, false);
    mode = on ? padMode : boardMode;
    repaint();
}

void EffectsScreen::showTuner (bool on)
{
    tuner = on && src == boardVoice;
    model.board (boardVoice).setTuner (tuner);
    tunerHz = 0.0f;
    if (tuner)
        startTimerHz (15);
    repaint();
}

void EffectsScreen::refresh()
{
    const auto list = sources();
    if (std::find (list.begin(), list.end(), src) == list.end())
        src = boardSynth;   // its layer went
    reloadChoices();
    layout();
    repaint();
}

void EffectsScreen::visibilityChanged()
{
    if (isVisible())
    {
        refresh();
        startTimerHz (tuner ? 15 : 4);
    }
    else
    {
        if (tuner)
            showTuner (false);
        if (padDown)
            padTouch (padDot.x, padDot.y, false);
        stopTimer();
    }
}

void EffectsScreen::timerCallback()
{
    if (tuner)
    {
        auto* fx = model.board (boardVoice).existing();
        if (fx != nullptr)
        {
            fx->readTuner (tunerBuffer.data(), (int) tunerBuffer.size());
            tunerHz = pedalboard::detectPitch (tunerBuffer.data(), (int) tunerBuffer.size(), fx->getSampleRate());
        }
        repaint();
        return;
    }
    // the chips' lamps and the layers that come and go
    juce::uint32 h = 17;
    for (int id : sources())
        h = h * 31u + (juce::uint32) id * 3u + (model.board (id).audible() ? 1u : 0u);
    if (h != shownState)
    {
        shownState = h;
        refresh();
    }
}

//==============================================================================
void EffectsScreen::stepBoard (int delta)
{
    reloadChoices();
    if (choices.empty())
        return;
    int i = choiceIndexOf (choices, board());
    if (i < 0)
        i = lastChoice;
    i = (i + delta + (int) choices.size()) % (int) choices.size();
    if (applyChoice (board(), choices[(size_t) i]))
        lastChoice = i;
    edited();
}

void EffectsScreen::toggleBypass()
{
    const bool by = norm ("bypass") > 0.5f;
    setNorm ("bypass", by ? 0.0f : 1.0f);
    edited();
}

void EffectsScreen::stomp (int pedal)
{
    if (pedal == kGainIn)
    {
        selectPedal (kGainIn);
        return;
    }
    auto& fx = board().fx();
    pedalboard::setOn (fx, pedal, ! pedalboard::isOn (fx, pedal), kept[(size_t) src][(size_t) pedal].data());
    sel = pedal;
    layout();
    edited();
}

void EffectsScreen::saveBoardAs (const juce::String& name)
{
    const auto file = saveOwnBoard (board(), name, model.ownFolder ? model.ownFolder() : juce::File());
    if (file == juce::File())
    {
        if (model.toast)
            model.toast ("Couldn't save that board. Give it a name.");
        return;
    }
    board().setName (name.trim().substring (0, 40));
    reloadChoices();
    edited();
    if (model.toast)
        model.toast ("Saved \"" + board().name + U8("\". It's in the list (tap the board's name) for every sound."));
}

void EffectsScreen::askSaveName()
{
    juce::String start = board().name;
    if (start.isEmpty() || choiceIndexOf (choices, board()) >= 0)
    {
        int n = 1;
        for (auto& c : choices)
            n += c.file != juce::File() ? 1 : 0;
        start = start.isNotEmpty() && board().edited() ? start + " 2" : "My board " + juce::String (n);
    }
    auto* w = new juce::AlertWindow ("Save this board", "It goes in the list with the ready-made boards, for every sound.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", start);
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<EffectsScreen> (this), w] (int r)
    {
        if (r == 1 && safe != nullptr)
            safe->saveBoardAs (w->getTextEditorContents ("name"));
    }), true);
}

void EffectsScreen::deleteChoice (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) choices.size()) || choices[(size_t) index].file == juce::File())
        return;
    const auto c = choices[(size_t) index];
    auto* w = new juce::AlertWindow ("Delete \"" + c.name + "\"?", "Boards using it keep their settings.", juce::MessageBoxIconType::NoIcon, this);
    w->addButton ("Delete", 1);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<EffectsScreen> (this), c] (int r)
    {
        if (r != 1 || safe == nullptr)
            return;
        c.file.deleteFile();
        if (safe->board().name == c.name)
            safe->board().setName ({});   // its settings stay, as your own
        safe->reloadChoices();
        safe->edited();
        if (safe->model.toast)
            safe->model.toast ("Deleted \"" + c.name + "\"");
    }), true);
}

void EffectsScreen::openCopyMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("COPY " + boardLabel (src) + "'S BOARD TO");
    for (int id : sources())
        if (id != src)
            m.addItem (1 + id, boardTitle (id) + (model.board (id).audible() ? U8("  \xc2\xb7  replaces its board") : juce::String()));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (copyR.toNearestInt())).withMinimumWidth (220),
                     [safe = juce::Component::SafePointer<EffectsScreen> (this)] (int r)
    {
        if (safe == nullptr || r <= 0)
            return;
        const int to = r - 1;
        safe->model.board (to).copyFrom (safe->board());
        if (safe->model.changed)
            safe->model.changed (to);
        if (safe->model.toast)
            safe->model.toast (boardLabel (safe->src) + U8("'s board is on ") + boardLabel (to) + " too.");
        safe->repaint();
    });
}

void EffectsScreen::padTouch (float x, float y, bool down)
{
    x = juce::jlimit (0.0f, 1.0f, x);
    y = juce::jlimit (0.0f, 1.0f, y);
    auto& b = board();
    if (down && ! padDown)
    {
        // momentary: what the board was, to go back to when the finger lifts (unless HOLD)
        padSaved = b.settings();
        padSavedName = b.name;
        padSavedHash = b.namedHash;
        padDown = true;
        if (sel != kGainIn && ! pedalboard::isOn (b.fx(), sel))
            pedalboard::setOn (b.fx(), sel, true, kept[(size_t) src][(size_t) sel].data());
    }
    if (! down)
    {
        if (padDown && ! hold)
        {
            b.setSettings (padSaved);
            b.name = padSavedName;
            b.namedHash = padSavedHash;
        }
        else if (padDown)
            edited();
        padDown = false;
        repaint();
        return;
    }
    padDot = { x, y };
    const auto& page = pedalPage (sel);
    if (page.knobs.empty())
    {
        // Repeat: across is the slice, from 1/4 to 1/32
        if (! page.chips.empty())
            pedalboard::set (b.fx(), page.chips[0].id, (float) (1 + juce::jlimit (0, 3, (int) (x * 4.0f))));
    }
    else
    {
        setNorm (page.knobs[0].id, x);
        if (page.knobs.size() > 1)
            setNorm (page.knobs[1].id, y);
    }
    repaint();
}

//==============================================================================
void EffectsScreen::resized() { layout(); }

void EffectsScreen::layout()
{
    const float W = (float) getWidth(), H = (float) getHeight();
    float y = 6.0f;
    chipsR = { 0.0f, y, W, 44.0f };
    {
        chipRects.clear();
        float x = 46.0f;
        for (int id : sources())
        {
            const auto label = boardLabel (id);
            const float w = juce::jmax (58.0f, juce::GlyphArrangement::getStringWidth (spaced (condensedBold (14.0f), 0.06f), label) + 28.0f);
            chipRects.push_back ({ id, { x, y, w, 44.0f } });
            x += w + 6.0f;
        }
        chipsWidth = x;
        chipScroll = juce::jlimit (0.0f, juce::jmax (0.0f, chipsWidth - W + 16.0f), chipScroll);
    }
    y += 50.0f;
    const bool voice = src == boardVoice;
    bannerR = voice ? juce::Rectangle<float> (16.0f, y, W - 32.0f, 46.0f) : juce::Rectangle<float>();
    if (voice)
    {
        tunerR = { bannerR.getRight() - 6.0f - 72.0f, bannerR.getY() + 5.0f, 72.0f, 36.0f };
        micR = { tunerR.getX() - 6.0f - 78.0f, tunerR.getY(), 78.0f, 36.0f };
        hearR = micR;
        y += 52.0f;
    }
    bypassR = { W - 16.0f - 80.0f, y, 80.0f, 44.0f };
    saveR = { bypassR.getX() - 6.0f - 64.0f, y, 64.0f, 44.0f };
    lcdR = { 16.0f, y, saveR.getX() - 6.0f - 16.0f, 44.0f };
    y += 52.0f;

    bottomR = { 16.0f, H - 8.0f - 44.0f, W - 32.0f, 44.0f };
    padR = bottomR.withWidth ((bottomR.getWidth() - 8.0f) * 0.5f);
    copyR = bottomR.withLeft (padR.getRight() + 8.0f);
    middleR = { 16.0f, y, W - 32.0f, juce::jmax (100.0f, bottomR.getY() - 8.0f - y) };

    const float avail = middleR.getHeight();
    const float minGrid = 3.0f * 44.0f + 2.0f * 6.0f + 16.0f;
    const float panelH = juce::jlimit (120.0f, 184.0f, avail - minGrid - 8.0f);
    const float tileH = juce::jlimit (30.0f, 64.0f, (avail - panelH - 8.0f - 16.0f - 12.0f) / 3.0f);
    const float gridH = tileH * 3.0f + 12.0f + 16.0f;
    gridR = { middleR.getX(), y, middleR.getWidth(), gridH };
    panelR = { middleR.getX(), gridR.getBottom() + 8.0f, middleR.getWidth(), panelH };
    const float spare = bottomR.getY() - 8.0f - panelR.getBottom();
    hintR = spare >= 30.0f ? juce::Rectangle<float> (16.0f, panelR.getBottom() + 6.0f, W - 32.0f, spare - 6.0f) : juce::Rectangle<float>();

    {
        auto inner = gridR.reduced (8.0f);
        const float tw = (inner.getWidth() - 4.0f * 6.0f) / 5.0f;
        for (int c = 0; c < kGridPedals; ++c)
            tiles[(size_t) c] = { inner.getX() + (float) (c % 5) * (tw + 6.0f), inner.getY() + (float) (c / 5) * (tileH + 6.0f), tw, tileH };
        gainTile = tiles[12].withX (tiles[12].getRight() + 6.0f).withWidth (inner.getRight() - tiles[12].getRight() - 6.0f);
    }

    // the picked pedal's panel: its header, its chips, its knobs
    knobRects.clear();
    optRects.clear();
    const auto& page = pedalPage (sel);
    float py = panelR.getY() + 34.0f;
    if (! page.chips.empty())
    {
        int count = 0;
        for (auto& g : page.chips)
            count += (int) g.labels.size();
        const float gaps = (float) (count - 1) * 5.0f + (float) (page.chips.size() - 1) * 8.0f;
        const float cw = juce::jmin (64.0f, (panelR.getWidth() - 24.0f - gaps) / (float) count);
        float x = panelR.getX() + 12.0f;
        for (int gi = 0; gi < (int) page.chips.size(); ++gi)
        {
            for (int i = 0; i < (int) page.chips[(size_t) gi].labels.size(); ++i)
            {
                optRects.push_back ({ optBase + gi * 10 + i, { x, py, cw, 38.0f } });
                x += cw + 5.0f;
            }
            x += 8.0f;
        }
        py += 44.0f;
    }
    if (! page.knobs.empty())
    {
        const float area = panelR.getBottom() - 6.0f - py;
        const int n = (int) page.knobs.size();
        const float cell = juce::jmin (88.0f, (panelR.getWidth() - 24.0f) / (float) juce::jmax (2, n));
        float x = panelR.getCentreX() - cell * (float) n * 0.5f;
        for (int k = 0; k < n; ++k)
        {
            knobRects.push_back ({ x, py, cell, area });
            x += cell;
        }
    }

    // the pad and the list
    padHoldR = { middleR.getRight() - 10.0f - 70.0f - 6.0f - 70.0f, middleR.getY() + 8.0f, 70.0f, 40.0f };
    padDoneR = { middleR.getRight() - 10.0f - 70.0f, middleR.getY() + 8.0f, 70.0f, 40.0f };
    padArea = juce::Rectangle<float> (middleR.getX() + 12.0f, middleR.getY() + 56.0f, middleR.getWidth() - 24.0f, middleR.getHeight() - 68.0f);
    listDoneR = padDoneR;
}

//==============================================================================
int EffectsScreen::idAt (juce::Point<float> p) const
{
    if (mode == listMode && middleR.contains (p))
    {
        if (listDoneR.contains (p))
            return listDoneId;
        const float top = middleR.getY() + 56.0f;
        if (p.y < top)
            return none;
        const int row = (int) ((p.y - top + listScroll) / kRowH);
        if (! juce::isPositiveAndBelow (row, (int) choices.size()))
            return none;
        if (choices[(size_t) row].file != juce::File() && p.x > middleR.getRight() - 90.0f)
            return deleteBase + row;
        return rowBase + row;
    }
    if (mode == padMode && middleR.contains (p))
    {
        if (padHoldR.contains (p))
            return padHoldId;
        if (padDoneR.contains (p))
            return padDoneId;
        if (padArea.expanded (8.0f).contains (p))
            return padAreaId;
        return none;
    }
    if (chipsR.contains (p))
    {
        for (auto& [id, r] : chipRects)
            if (r.translated (-chipScroll, 0.0f).contains (p) && p.x > 40.0f)
                return chipBase + id;
        return none;
    }
    if (! bannerR.isEmpty() && bannerR.contains (p))
    {
        const bool on = model.micOn && model.micOn();
        if (on && tunerR.expanded (3.0f).contains (p))
            return tunerId;
        if (micR.expanded (3.0f).contains (p))
            return on && model.route && model.route() != Route::none && model.hearing && ! model.hearing() ? hearId : micId;
        if (! on && tunerR.expanded (3.0f).contains (p))
            return micId;
        return none;
    }
    if (lcdR.contains (p))
    {
        if (p.x < lcdR.getX() + 40.0f)
            return prevId;
        if (p.x > lcdR.getRight() - 40.0f)
            return nextId;
        return lcdName;
    }
    if (saveR.contains (p))
        return saveId;
    if (bypassR.contains (p))
        return bypassId;
    if (gridR.contains (p))
    {
        for (int c = 0; c < kGridPedals; ++c)
        {
            const auto& t = tiles[(size_t) c];
            if (! t.expanded (3.0f).contains (p))
                continue;
            // the stomp dot: the lower part of the tile, round its middle
            const bool dot = p.y > t.getY() + t.getHeight() * 0.42f && std::abs (p.x - t.getCentreX()) < juce::jmax (22.0f, t.getWidth() * 0.32f);
            return (dot ? dotBase : tileBase) + c;
        }
        if (gainTile.contains (p))
            return gainId;
        return none;
    }
    if (panelR.contains (p) && ! tuner)
    {
        for (auto& [id, r] : optRects)
            if (r.expanded (2.0f, 4.0f).contains (p))
                return id;
        for (int k = 0; k < (int) knobRects.size(); ++k)
            if (knobRects[(size_t) k].contains (p))
                return knobBase + k;
        if (p.y < panelR.getY() + 34.0f)
            return headerId;
        return none;
    }
    if (padR.contains (p))
        return fxPadId;
    if (copyR.contains (p))
        return copyId;
    return none;
}

void EffectsScreen::press (int id)
{
    if (id >= chipBase && id < chipBase + kBoards)
    {
        setSource (id - chipBase);
        if (model.picked)
            model.picked (src);
        return;
    }
    if (id >= tileBase && id < tileBase + kGridPedals)
        return selectPedal (gridPedal (id - tileBase));
    if (id >= dotBase && id < dotBase + kGridPedals)
        return stomp (gridPedal (id - dotBase));
    if (id >= optBase && id < optBase + 100)
    {
        const auto& page = pedalPage (sel);
        const int g = (id - optBase) / 10, i = (id - optBase) % 10;
        if (juce::isPositiveAndBelow (g, (int) page.chips.size()))
        {
            auto& fx = board().fx();
            pedalboard::set (fx, page.chips[(size_t) g].id, (float) i);
            // a division picked on a pedal that's off: it comes on
            if (sel != pedalboard::repeat && ! pedalboard::isOn (fx, sel))
                pedalboard::setOn (fx, sel, true, kept[(size_t) src][(size_t) sel].data());
            edited();
        }
        return;
    }
    if (id >= rowBase && id < deleteBase)
    {
        const int row = id - rowBase;
        if (juce::isPositiveAndBelow (row, (int) choices.size()) && applyChoice (board(), choices[(size_t) row]))
            lastChoice = row;
        mode = boardMode;
        edited();
        return;
    }
    if (id >= deleteBase)
        return deleteChoice (id - deleteBase);

    switch (id)
    {
        case prevId: stepBoard (-1); break;
        case nextId: stepBoard (1); break;
        case lcdName: showList (true); break;
        case saveId: reloadChoices(); askSaveName(); break;
        case bypassId: toggleBypass(); break;
        case gainId: selectPedal (kGainIn); break;
        case headerId:
            if (sel != kGainIn && sel != pedalboard::repeat)
                stomp (sel);
            break;
        case micId: if (model.micButton) model.micButton(); repaint(); break;
        case hearId: if (model.hearButton) model.hearButton(); repaint(); break;
        case tunerId: showTuner (! tuner); break;
        case fxPadId: showPad (mode != padMode); break;
        case copyId: openCopyMenu(); break;
        case padHoldId:
            hold = ! hold;
            if (! hold)
                edited();
            repaint();
            break;
        case padDoneId: showPad (false); break;
        case listDoneId: showList (false); break;
        default: break;
    }
}

void EffectsScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    moved = false;
    draggingChips = pressed >= chipBase && pressed < chipBase + kBoards;
    draggingList = mode == listMode && pressed >= rowBase;
    dragStartScroll = draggingChips ? chipScroll : listScroll;
    if (pressed >= knobBase && pressed < knobBase + 8)
    {
        dragKnob = pressed - knobBase;
        dragStartValue = norm (pedalPage (sel).knobs[(size_t) dragKnob].id);
    }
    if (pressed == padAreaId)
        padTouch ((e.position.x - padArea.getX()) / padArea.getWidth(), (padArea.getBottom() - e.position.y) / padArea.getHeight(), true);
    repaint();
}

void EffectsScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingChips)
    {
        if (std::abs (e.getDistanceFromDragStartX()) > 6)
            moved = true;
        if (moved)
        {
            chipScroll = juce::jlimit (0.0f, juce::jmax (0.0f, chipsWidth - (float) getWidth() + 16.0f), dragStartScroll - (float) e.getDistanceFromDragStartX());
            repaint();
        }
        return;
    }
    if (draggingList)
    {
        if (std::abs (e.getDistanceFromDragStartY()) > 6)
            moved = true;
        if (moved)
        {
            const float most = juce::jmax (0.0f, (float) choices.size() * kRowH - (middleR.getHeight() - 60.0f));
            listScroll = juce::jlimit (0.0f, most, dragStartScroll - (float) e.getDistanceFromDragStartY());
            repaint();
        }
        return;
    }
    if (dragKnob >= 0)
    {
        moved = true;
        setNorm (pedalPage (sel).knobs[(size_t) dragKnob].id, dragStartValue - (float) e.getDistanceFromDragStartY() / 220.0f);
        return;
    }
    if (pressed == padAreaId)
        padTouch ((e.position.x - padArea.getX()) / padArea.getWidth(), (padArea.getBottom() - e.position.y) / padArea.getHeight(), true);
}

void EffectsScreen::mouseUp (const juce::MouseEvent& e)
{
    const int was = pressed;
    pressed = none;
    if (dragKnob >= 0)
    {
        dragKnob = -1;
        edited();
        return;
    }
    if (was == padAreaId)
    {
        padTouch (padDot.x, padDot.y, false);
        return;
    }
    if (! moved && was != none && idAt (e.position) == was)
        press (was);
    repaint();
}

void EffectsScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    if (id >= knobBase && id < knobBase + 8)
    {
        const auto* knobId = pedalPage (sel).knobs[(size_t) (id - knobBase)].id;
        if (auto* p = board().fx().params.getParameter (knobId))
            p->setValueNotifyingHost (p->getDefaultValue());
        edited();
    }
}

//==============================================================================
juce::String EffectsScreen::boardDisplayName() const
{
    const auto& b = board();
    if (b.name.isNotEmpty())
        return b.name.toUpperCase() + (b.edited() ? " *" : "");
    return b.isNeutral() ? juce::String ("EMPTY") : juce::String ("MY SETTINGS");
}

juce::String EffectsScreen::hintText() const
{
    if (isLayerBoard (src))
        return U8("After the layer's five knobs. REC resamples this layer through it onto the next layer.");
    switch (src)
    {
        case boardAll: return U8("Live only: TO MAC sends each layer with its own board, not ALL. REC resamples all the loops onto the next layer.");
        case boardMaster: return U8("Live only: TO MAC sends each layer with its own board, not MASTER. REC resamples the whole mix onto the next layer.");
        case boardVoice: return U8("REC records what you play or sing through this board onto the next layer, like every other tool.");
        case boardVocoder: return U8("After the vocoder's own pedals. REC records the vocoder through it onto the next layer.");
        default: return U8("REC records this tool through its board onto the next layer. That layer's own board starts empty.");
    }
}

void EffectsScreen::drawButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, int id, bool lit, float font)
{
    drawRaised (g, r, lit ? Look::amber : Look::cream, pressed == id, 11.0f);
    g.setColour (lit ? col::onBright : col::ink);
    g.setFont (spaced (condensedBold (font), 0.08f));
    g.drawFittedText (text, r.withTrimmedBottom (3.0f).reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

void EffectsScreen::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);
    drawChips (g);
    if (! bannerR.isEmpty())
        drawBanner (g);
    drawLcd (g);
    if (mode == listMode)
        drawList (g);
    else if (mode == padMode)
        drawPad (g);
    else
    {
        drawGrid (g);
        drawPanel (g);
        if (! hintR.isEmpty())
        {
            g.setColour (col::inkDim);
            g.setFont (small (13.5f));
            g.drawFittedText (hintText(), hintR.toNearestInt(), juce::Justification::topLeft, 3, 0.9f);
        }
    }
    drawButton (g, padR, "FX PAD", fxPadId, mode == padMode);
    drawButton (g, copyR, U8("COPY TO\xe2\x80\xa6"), copyId);
}

void EffectsScreen::drawChips (juce::Graphics& g)
{
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (12.0f), 0.12f));
    g.drawText ("ON", juce::Rectangle<float> (16.0f, chipsR.getY(), 26.0f, chipsR.getHeight() - 3.0f), juce::Justification::centredLeft);
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (juce::Rectangle<float> (42.0f, chipsR.getY() - 2.0f, chipsR.getWidth() - 42.0f, chipsR.getHeight() + 6.0f).toNearestInt());
    for (auto& [id, r0] : chipRects)
    {
        auto r = r0.translated (-chipScroll, 0.0f);
        if (r.getRight() < 0 || r.getX() > (float) getWidth())
            continue;
        drawChip (g, r, boardLabel (id), id == src, pressed == chipBase + id, 14.0f);
        if (model.board (id).audible())
        {
            g.setColour (id == src ? col::onBright : col::green.darker (0.2f));
            g.fillEllipse (r.getRight() - 11.0f, r.getY() + 5.0f, 6.0f, 6.0f);
        }
    }
    // there's more off the edge
    if (chipsWidth - chipScroll > (float) getWidth())
    {
        juce::ColourGradient fade (col::cream.withAlpha (0.0f), (float) getWidth() - 28.0f, 0.0f, col::cream, (float) getWidth(), 0.0f, false);
        g.setGradientFill (fade);
        g.fillRect (juce::Rectangle<float> ((float) getWidth() - 28.0f, chipsR.getY(), 28.0f, chipsR.getHeight()));
    }
}

void EffectsScreen::drawBanner (juce::Graphics& g)
{
    auto b = bannerR;
    const bool on = model.micOn && model.micOn();
    const auto route = model.route ? model.route() : Route::none;
    const bool hear = model.hearing && model.hearing();
    const bool good = on && route == Route::wired && hear;
    g.setColour (good ? kGreenBg : kAmberBg);
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (good ? kGreenEdge : col::amber);
    g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
    juce::String head, tail;
    if (! on)
    {
        head = "Mic off. ";
        tail = "Turn it on for your voice or line in.";
    }
    else
    {
        head = route == Route::wired ? "Wired headphones. " : route == Route::bluetooth ? "Bluetooth. " : "No headphones. ";
        tail = route == Route::none ? U8("Mic records \xc2\xb7 you don't hear it.") : hear ? U8("Mic or line in \xc2\xb7 you hear it.") : U8("Mic on \xc2\xb7 hear myself off.");
    }
    juce::AttributedString a;
    a.append (head, small (13.5f).boldened(), col::ink);
    a.append (tail, small (13.5f), col::ink);
    a.setWordWrap (juce::AttributedString::byWord);
    a.setJustification (juce::Justification::centredLeft);
    a.draw (g, b.withTrimmedLeft (12.0f).withRight ((on ? micR.getX() : tunerR.getX()) - 6.0f).reduced (0.0f, 3.0f));
    auto button = [&] (juce::Rectangle<float> r, const juce::String& text, bool lit, int id)
    {
        drawRaised (g, r, lit ? Look::amber : Look::cream, pressed == id, 10.0f);
        g.setColour (lit ? col::onBright : col::ink);
        g.setFont (spaced (condensedBold (13.0f), 0.08f));
        g.drawFittedText (text, r.withTrimmedBottom (3.0f).reduced (4.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    };
    if (! on)
        button (tunerR, "MIC ON", true, micId);
    else
    {
        const bool offerHear = route != Route::none && ! hear;
        button (micR, offerHear ? "HEAR: OFF" : "MIC OFF", false, offerHear ? hearId : micId);
        button (tunerR, "TUNER", tuner, tunerId);
    }
}

void EffectsScreen::drawLcd (juce::Graphics& g)
{
    drawWell (g, lcdR, 12.0f);
    const int idx = choiceIndexOf (choices, board());
    const auto arrows = juce::Colour (0xffb7a9c2);
    g.setColour (pressed == prevId ? col::amber : arrows);
    g.setFont (lcdFont (26.0f));
    g.drawText (U8("\xe2\x80\xb9"), juce::Rectangle<float> (lcdR.getX(), lcdR.getY(), 40.0f, lcdR.getHeight()), juce::Justification::centred);
    g.setColour (pressed == nextId ? col::amber : arrows);
    g.drawText (U8("\xe2\x80\xba"), juce::Rectangle<float> (lcdR.getRight() - 40.0f, lcdR.getY(), 40.0f, lcdR.getHeight()), juce::Justification::centred);
    auto mid = lcdR.reduced (40.0f, 0.0f);
    g.setColour (arrows.withAlpha (0.8f));
    g.setFont (spaced (condensedBold (10.5f), 0.12f));
    const juce::String count = (idx >= 0 ? juce::String (idx + 1) : U8("\xe2\x80\x93")) + " OF " + juce::String ((int) choices.size());
    g.drawText (count + U8(" \xc2\xb7 TAP FOR LIST"), mid.withHeight (17.0f).translated (0.0f, 4.0f), juce::Justification::centred);
    g.setColour (board().isNeutral() ? col::lcdAmber.withAlpha (0.6f) : col::lcdAmber);
    g.setFont (spaced (lcdFont (19.0f), 0.06f));
    g.drawFittedText (boardDisplayName(), mid.withTrimmedTop (18.0f).withTrimmedBottom (3.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);

    drawButton (g, saveR, "SAVE", saveId, false, 14.0f);
    const bool by = norm ("bypass") > 0.5f;
    drawButton (g, bypassR, by ? "BYPASSED" : "BYPASS", bypassId, by, 14.0f);
}

void EffectsScreen::drawGrid (juce::Graphics& g)
{
    drawWell (g, gridR, 14.0f);
    const bool by = norm ("bypass") > 0.5f;
    for (int c = 0; c < kGridPedals; ++c)
    {
        const int p = gridPedal (c);
        auto r = tiles[(size_t) c];
        const bool on = isOn (p), picked = sel == p;
        auto colour = on ? pedalColour (p) : kTileOff;
        if (by && on)
            colour = colour.withMultipliedSaturation (0.4f).darker (0.4f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 9.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r, 9.0f);
        if (picked)
        {
            g.setColour (col::creamLight);
            g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 2.5f);
        }
        g.setColour (on ? col::onBright : kTileText);
        g.setFont (spaced (condensedBold (r.getHeight() < 50.0f ? 12.0f : 13.0f), 0.06f));
        g.drawFittedText (pedalShortName (p), r.withHeight (juce::jmin (24.0f, r.getHeight() * 0.42f)).withTrimmedTop (3.0f).reduced (3.0f, 0.0f).toNearestInt(),
                          juce::Justification::centred, 1, 0.8f);
        // the stomp dot
        const float d = juce::jmin (16.0f, r.getHeight() * 0.3f);
        const juce::Point<float> dc (r.getCentreX(), r.getBottom() - d * 0.5f - juce::jmax (5.0f, r.getHeight() * 0.14f));
        g.setColour (juce::Colour (0xff0f0c15));
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (dc));
        g.setColour (on ? col::creamLight : juce::Colour (0xff3a2f4f));
        g.fillEllipse (juce::Rectangle<float> (d * 0.5f, d * 0.5f).withCentre (dc));
        if (pressed == dotBase + c)
        {
            g.setColour (juce::Colours::white.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (d + 10.0f, d + 10.0f).withCentre (dc));
        }
    }
    // GAIN IN in the spare cells
    {
        auto r = gainTile;
        const bool picked = sel == kGainIn;
        g.setColour (kTileOff.darker (0.25f));
        g.fillRoundedRectangle (r, 9.0f);
        if (picked)
        {
            g.setColour (col::creamLight);
            g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 2.5f);
        }
        auto* p = param ("inputGain");
        const float db = p->convertFrom0to1 (p->getValue());
        g.setColour (kTileText);
        g.setFont (spaced (condensedBold (12.0f), 0.1f));
        g.drawText ("GAIN IN", r.withTrimmedBottom (r.getHeight() * 0.5f).withTrimmedTop (3.0f), juce::Justification::centred);
        g.setColour (col::lcdAmber);
        g.setFont (lcdFont (19.0f));
        g.drawText ((db > 0.05f ? "+" : "") + juce::String (db, std::abs (db) >= 9.95f ? 0 : 1) + " dB", r.withTrimmedTop (r.getHeight() * 0.45f), juce::Justification::centred);
    }
}

void EffectsScreen::drawKnob (juce::Graphics& g, juce::Rectangle<float> r, const PedalKnob& k, juce::Colour face)
{
    auto* p = param (k.id);
    if (p == nullptr)
        return;
    const float v = p->getValue();
    const float d = juce::jmax (24.0f, juce::jmin (r.getWidth() - 18.0f, r.getHeight() - 36.0f, 66.0f));
    auto c = juce::Rectangle<float> (r.getCentreX() - d * 0.5f, r.getY() + juce::jmax (0.0f, (r.getHeight() - 36.0f - d) * 0.4f), d, d);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.translated (0.0f, 3.0f));
    g.setColour (face);
    g.fillEllipse (c);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (c.reduced (1.5f), 1.2f);
    const float a = juce::degreesToRadians (-135.0f + 270.0f * v);
    const auto cc = c.getCentre();
    g.setColour (col::ink);
    g.drawLine (cc.x + std::sin (a) * d * 0.14f, cc.y - std::cos (a) * d * 0.14f, cc.x + std::sin (a) * d * 0.4f, cc.y - std::cos (a) * d * 0.4f, 3.0f);
    g.setColour (kLabel);
    g.setFont (spaced (condensedBold (12.0f), 0.1f));
    g.drawFittedText (k.label, juce::Rectangle<float> (r.getX(), c.getBottom() + 4.0f, r.getWidth(), 14.0f).toNearestInt(), juce::Justification::centred, 1, 0.8f);
    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (16.0f));
    g.drawFittedText (p->getCurrentValueAsText().toUpperCase(), juce::Rectangle<float> (r.getX(), c.getBottom() + 18.0f, r.getWidth(), 16.0f).toNearestInt(),
                      juce::Justification::centred, 1, 0.75f);
}

void EffectsScreen::drawPanel (juce::Graphics& g)
{
    drawWell (g, panelR, 14.0f);
    if (tuner)
        return drawTuner (g, panelR);
    const bool on = isOn (sel);
    const auto colour = pedalColour (sel);
    auto head = juce::Rectangle<float> (panelR.getX() + 14.0f, panelR.getY() + 8.0f, panelR.getWidth() - 28.0f, 22.0f);
    g.setColour (on ? colour : kLabel);
    g.setFont (spaced (lcdFont (19.0f), 0.08f));
    const juce::String state = sel == kGainIn ? juce::String ("THE LEVEL IN") : on ? juce::String ("ON") : juce::String ("OFF");
    g.drawText (pedalShortName (sel) + U8(" \xc2\xb7 ") + state, head, juce::Justification::centredLeft);
    if (sel != kGainIn)
    {
        g.setColour (kLabel.withAlpha (0.7f));
        g.setFont (spaced (condensedBold (11.0f), 0.1f));
        const juce::String kind = juce::String (pedalboard::pedals()[(size_t) sel].kind).toUpperCase();
        g.drawText (sel == pedalboard::repeat ? kind : kind + U8(" \xc2\xb7 TAP HERE: ON / OFF"), head, juce::Justification::centredRight);
    }

    const auto& page = pedalPage (sel);
    for (auto& [id, r] : optRects)
    {
        const int gi = (id - optBase) / 10, i = (id - optBase) % 10;
        const auto& chips = page.chips[(size_t) gi];
        auto* p = param (chips.id);
        const bool lit = p != nullptr && juce::roundToInt (p->convertFrom0to1 (p->getValue())) == i && (on || sel == pedalboard::repeat);
        const bool down = pressed == id;
        g.setColour (lit ? colour : kTileOff);
        g.fillRoundedRectangle (r.translated (0.0f, down ? 1.0f : 0.0f), 8.0f);
        g.setColour (lit ? col::onBright : juce::Colour (0xffe4dbee));
        g.setFont (spaced (condensedBold (14.0f), 0.04f));
        g.drawFittedText (chips.labels[(size_t) i], r.toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }
    for (int k = 0; k < (int) knobRects.size(); ++k)
        drawKnob (g, knobRects[(size_t) k], page.knobs[(size_t) k], kKnobFaces[k % 4]);
    if (page.knobs.empty() && sel == pedalboard::repeat)
    {
        g.setColour (kLabel);
        g.setFont (small (13.5f));
        g.drawFittedText ("Pick a slice: it catches that much of the beat and loops it until OFF. FX PAD plays it by hand.",
                          panelR.withTrimmedTop (84.0f).reduced (14.0f, 4.0f).toNearestInt(), juce::Justification::centredLeft, 3, 0.9f);
    }
}

void EffectsScreen::drawTuner (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (kLabel);
    g.setFont (spaced (condensedBold (12.0f), 0.12f));
    g.drawText (U8("TUNER \xc2\xb7 VOICE / LINE IN"), r.withHeight (26.0f).translated (14.0f, 6.0f), juce::Justification::centredLeft);
    const auto note = pedalboard::nearestNote (tunerHz);
    auto big = r.withTrimmedTop (28.0f).withHeight (r.getHeight() * 0.45f);
    if (tunerHz <= 0.0f)
    {
        g.setColour (kLabel.withAlpha (0.7f));
        g.setFont (small (15.0f));
        g.drawFittedText ("Play or sing one note.", big.toNearestInt(), juce::Justification::centred, 1, 0.9f);
    }
    else
    {
        const bool inTune = std::abs (note.cents) < 5.0f;
        g.setColour (inTune ? col::green : col::lcdAmber);
        g.setFont (lcdFont (big.getHeight() * 0.9f));
        g.drawText (note.name + juce::String (note.octave), big, juce::Justification::centred);
        g.setColour (kLabel);
        g.setFont (lcdFont (16.0f));
        g.drawText ((note.cents >= 0 ? "+" : "") + juce::String (juce::roundToInt (note.cents)) + " CENTS  " + juce::String (tunerHz, 1) + " HZ",
                    big.withY (big.getBottom() - 6.0f).withHeight (18.0f), juce::Justification::centred);
    }
    // the needle's scale, -50 .. +50 cents
    auto scale = juce::Rectangle<float> (r.getX() + 24.0f, r.getBottom() - 30.0f, r.getWidth() - 48.0f, 14.0f);
    g.setColour (juce::Colour (0xff3a2f4f));
    g.fillRoundedRectangle (scale, 7.0f);
    g.setColour (col::green.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (scale.getCentreX() - scale.getWidth() * 0.05f, scale.getY(), scale.getWidth() * 0.1f, scale.getHeight()));
    if (tunerHz > 0.0f)
    {
        const float x = scale.getCentreX() + scale.getWidth() * 0.5f * juce::jlimit (-1.0f, 1.0f, note.cents / 50.0f);
        g.setColour (col::creamLight);
        g.fillRoundedRectangle (x - 3.0f, scale.getY() - 5.0f, 6.0f, scale.getHeight() + 10.0f, 3.0f);
    }
}

void EffectsScreen::drawList (juce::Graphics& g)
{
    drawWell (g, middleR, 14.0f);
    g.setColour (col::lcdAmber);
    g.setFont (spaced (lcdFont (19.0f), 0.08f));
    g.drawText (U8("BOARDS \xc2\xb7 ") + boardLabel (src), juce::Rectangle<float> (middleR.getX() + 14.0f, middleR.getY() + 8.0f, middleR.getWidth() - 110.0f, 40.0f),
                juce::Justification::centredLeft);
    drawButton (g, listDoneR, "DONE", listDoneId, true, 14.0f);
    const float top = middleR.getY() + 56.0f;
    juce::Graphics::ScopedSaveState s (g);
    g.reduceClipRegion (juce::Rectangle<float> (middleR.getX(), top, middleR.getWidth(), middleR.getBottom() - 6.0f - top).toNearestInt());
    const int current = choiceIndexOf (choices, board());
    for (int i = 0; i < (int) choices.size(); ++i)
    {
        auto row = juce::Rectangle<float> (middleR.getX() + 8.0f, top + (float) i * kRowH - listScroll, middleR.getWidth() - 16.0f, kRowH - 4.0f);
        if (row.getBottom() < top || row.getY() > middleR.getBottom())
            continue;
        const auto& c = choices[(size_t) i];
        g.setColour (i == current ? juce::Colour (0xff3a2f4f) : (pressed == rowBase + i ? kTileOff.brighter (0.2f) : kTileOff));
        g.fillRoundedRectangle (row, 8.0f);
        g.setColour (i == current ? col::lcdAmber : col::creamLight);
        g.setFont (condensedBold (17.0f));
        g.drawFittedText (c.name, row.reduced (12.0f, 0.0f).withTrimmedRight (110.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        juce::String tag;
        if (c.empty)
            tag = "EVERY PEDAL OFF";
        else if (c.file != juce::File())
            tag = "MINE";
        else
            tag = juce::String (pedalboard::presets()[(size_t) c.preset].forWhat).toUpperCase();
        g.setColour (kLabel.withAlpha (0.75f));
        g.setFont (spaced (condensedBold (11.0f), 0.1f));
        if (c.file != juce::File())
        {
            auto del = juce::Rectangle<float> (row.getRight() - 80.0f, row.getY() + 5.0f, 74.0f, row.getHeight() - 10.0f);
            g.drawText (tag, row.withRight (del.getX() - 8.0f), juce::Justification::centredRight);
            g.setColour (pressed == deleteBase + i ? col::red : col::red.darker (0.3f));
            g.fillRoundedRectangle (del, 7.0f);
            g.setColour (col::creamLight);
            g.setFont (spaced (condensedBold (13.0f), 0.08f));
            g.drawText ("DELETE", del, juce::Justification::centred);
        }
        else
            g.drawText (tag, row.withTrimmedRight (12.0f), juce::Justification::centredRight);
    }
}

void EffectsScreen::drawPad (juce::Graphics& g)
{
    drawWell (g, middleR, 14.0f);
    const auto& page = pedalPage (sel);
    const auto colour = pedalColour (sel);
    g.setColour (colour);
    g.setFont (spaced (lcdFont (19.0f), 0.08f));
    g.drawText (U8("FX PAD \xc2\xb7 ") + pedalShortName (sel), juce::Rectangle<float> (middleR.getX() + 14.0f, middleR.getY() + 8.0f, padHoldR.getX() - middleR.getX() - 20.0f, 40.0f),
                juce::Justification::centredLeft);
    drawButton (g, padHoldR, "HOLD", padHoldId, hold, 14.0f);
    drawButton (g, padDoneR, "DONE", padDoneId, false, 14.0f);

    g.setColour (juce::Colour (0xff221b2d));
    g.fillRoundedRectangle (padArea, 12.0f);
    g.setColour (juce::Colour (0xff3a2f4f));
    for (int i = 1; i < 4; ++i)
    {
        const float x = padArea.getX() + padArea.getWidth() * (float) i / 4.0f, y = padArea.getY() + padArea.getHeight() * (float) i / 4.0f;
        g.drawLine (x, padArea.getY() + 6.0f, x, padArea.getBottom() - 6.0f, 1.0f);
        g.drawLine (padArea.getX() + 6.0f, y, padArea.getRight() - 6.0f, y, 1.0f);
    }
    juce::String xName, yName;
    juce::Point<float> at = padDot;
    if (page.knobs.empty())
    {
        xName = U8("SLICE  1/4 \xe2\x80\xa6 1/32  \xe2\x86\x92");
        if (! padDown)
            at = { 0.5f, 0.5f };
    }
    else
    {
        xName = juce::String (page.knobs[0].label) + U8("  \xe2\x86\x92");
        yName = page.knobs.size() > 1 ? U8("\xe2\x86\x91  ") + juce::String (page.knobs[1].label) : juce::String();
        if (! padDown)
            at = { norm (page.knobs[0].id), page.knobs.size() > 1 ? norm (page.knobs[1].id) : 0.5f };
    }
    g.setColour (kLabel);
    g.setFont (spaced (condensedBold (12.0f), 0.1f));
    g.drawText (xName, padArea.reduced (12.0f, 8.0f), juce::Justification::bottomRight);
    if (yName.isNotEmpty())
        g.drawText (yName, padArea.reduced (12.0f, 8.0f), juce::Justification::topLeft);
    g.setColour (kLabel.withAlpha (0.7f));
    g.setFont (small (13.0f));
    g.drawFittedText (hold ? "HOLD: where you let go stays." : "Touch and move: it springs back when you let go.",
                      padArea.reduced (12.0f, 8.0f).toNearestInt(), juce::Justification::topRight, 2, 0.9f);
    const juce::Point<float> p (padArea.getX() + at.x * padArea.getWidth(), padArea.getBottom() - at.y * padArea.getHeight());
    g.setColour (colour.withAlpha (padDown ? 0.35f : 0.18f));
    g.fillEllipse (juce::Rectangle<float> (padDown ? 80.0f : 54.0f, padDown ? 80.0f : 54.0f).withCentre (p));
    g.setColour (padDown ? colour : colour.withAlpha (0.7f));
    g.fillEllipse (juce::Rectangle<float> (30.0f, 30.0f).withCentre (p));
}
