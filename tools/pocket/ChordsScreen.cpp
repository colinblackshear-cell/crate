#include "ChordsScreen.h"

using namespace pocket;

namespace
{
const juce::Colour kChords { 0xff9a5cf0 }, kPadDark { 0xff4a3d63 }, kPadText { 0xfff3eadb }, kBoxDark { 0xff221b2d };
const char* const kStyleNames[4] = { "HOLD", "STRUM", "ARP", "STABS" };
constexpr float kChipH = 40.0f, kChipRow = kChipH + 6.0f;   // CHORDS ON / OFF over the pads
const char* const kPlaysNames[3] = { "SYNTH", "MY PAD SOUND", "VOCODER NOTES" };
}

ChordsScreen::ChordsScreen (ChordsTool& t) : tool (t)
{
    setOpaque (false);
    startTimerHz (30);
}

ChordsScreen::~ChordsScreen()
{
    stopTimer();
}

void ChordsScreen::leave()
{
    // a chord under a finger lets go; a held (latched) chord, or the progression, keeps playing (and keeps
    // driving the vocoder) on the other tools
    if (pressed >= padBase && pressed < padBase + kChordPads)
        pressPad (pressed - padBase, false);
    pressed = 0;
}

void ChordsScreen::visibilityChanged()
{
    if (! isVisible())
        leave();
    else
        layout();
}

void ChordsScreen::toolChanged()
{
    leave();
    layout();
    repaint();
}

void ChordsScreen::changed()
{
    if (onChanged)
        onChanged();
    repaint();
}

int ChordsScreen::sounding() const { return demoPad > -2 ? demoPad : tool.soundingPad(); }
int ChordsScreen::step() const { return demoStep > -2 ? demoStep : tool.engine().progressionStep(); }

void ChordsScreen::timerCallback()
{
    if (! isVisible())
        return;
    const int s = sounding(), st = step();
    const bool muted = tool.engine().progressionMuted();
    const int live = tool.engine().playingShift() * 4 + (tool.liveWaiting() ? 1 + (int) ((juce::Time::getMillisecondCounter() / 250) % 2) : 0);
    if (s != shownPad || st != shownStep || muted != shownMuted || live != shownLive)
    {
        shownLive = live;
        shownPad = s;
        shownStep = st;
        shownMuted = muted;
        repaint();
    }
}

//==============================================================================
void ChordsScreen::layout()
{
    hits.clear();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = 16.0f, iw = w - 32.0f;
    auto addHit = [this] (juce::Rectangle<float> r, int id) { hits.push_back ({ r, id }); };
    const bool roomy = h > 520.0f;
    const float rowH = 44.0f, gap = roomy ? 8.0f : 5.0f;

    // Chords · A minor, KEY, SOUND
    float y = 6.0f;
    {
        const float soundW = juce::jlimit (96.0f, 170.0f, iw * 0.34f), keyW = 52.0f, liveW = 56.0f;
        addHit ({ x0 + iw - soundW, y, soundW, rowH }, soundId);
        addHit ({ x0 + iw - soundW - 6.0f - keyW, y, keyW, rowH }, keyId);
        addHit ({ x0 + iw - soundW - keyW - 12.0f - liveW, y, liveW, rowH }, liveId);
        titleR = { x0, y, iw - soundW - keyW - liveW - 24.0f, rowH };
    }
    y += rowH + gap;

    liveWell = {};
    if (tool.liveOpen)
    {
        // LIVE KEY: a title with what plays, the twelve keys as a keyboard, then OCT - / OCT + / BACK / ON BAR
        const float room = h - 8.0f - y - kChipRow - 150.0f;   // leave the pads at least 150
        const float bedH = juce::jlimit (96.0f, 150.0f, room - 6.0f - 30.0f - 8.0f - rowH - 12.0f);
        liveWell = { x0, y, iw, 6.0f + 30.0f + 8.0f + bedH + 8.0f + rowH + 10.0f };
        liveTitle = { x0 + 12.0f, y + 6.0f, iw * 0.4f, 30.0f };
        liveLcd = { x0 + iw * 0.4f + 12.0f, y + 6.0f, iw * 0.6f - 24.0f, 30.0f };
        liveBed = { x0 + 10.0f, y + 6.0f + 30.0f + 8.0f, iw - 20.0f, bedH };
        static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        const float ww = liveBed.getWidth() / 7.0f;
        for (int k = 0; k < 12; ++k)
        {
            const bool black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
            liveKeyR[(size_t) k] = black ? juce::Rectangle<float> (liveBed.getX() + (whiteIndex[k] + 1) * ww - ww * 0.36f, liveBed.getY(), ww * 0.72f, bedH * 0.56f)
                                         : juce::Rectangle<float> (liveBed.getX() + whiteIndex[k] * ww + 2.0f, liveBed.getY(), ww - 4.0f, bedH);
        }
        // black keys first, so a touch on one isn't taken by the white key under it
        for (int k : { 1, 3, 6, 8, 10, 0, 2, 4, 5, 7, 9, 11 })
            hits.push_back ({ liveKeyR[(size_t) k], liveKeyBase + k });
        const float cy = liveBed.getBottom() + 8.0f, cw = (iw - 20.0f - 3 * 6.0f) / 4.0f;
        int n = 0;
        for (int id : { liveOctDownId, liveOctUpId, liveHomeId, liveBarId })
            addHit ({ x0 + 10.0f + (float) n++ * (cw + 6.0f), cy, cw, rowH }, id);
        y = liveWell.getBottom() + gap;

        const float roomPads = h - 8.0f - y - kChipRow;
        const float wellH = juce::jmax (120.0f, roomPads);
        padWell = { x0, h - 8.0f - wellH, iw, wellH };
        placeChordsChip (y);
        auto pw = padWell.reduced (8.0f);
        const float pcw = pw.getWidth() / 4.0f, pch = pw.getHeight() / 2.0f;
        for (int i = 0; i < kChordPads; ++i)
        {
            padR[(size_t) i] = { pw.getX() + (float) (i % 4) * pcw + 3.0f, pw.getY() + (float) (i / 4) * pch + 3.0f, pcw - 6.0f, pch - 6.0f };
            addHit (padR[(size_t) i], padBase + i);
        }
        progWell = {};
        boxR.fill ({});
        colourLabel = playLabel = {};
        return;
    }

    // a label, then chips sharing the rest of the row by weight
    const float labelW = 60.0f;
    auto row = [&] (std::initializer_list<std::pair<float, int>> cells, float left, float width)
    {
        float sum = 0;
        for (auto& c : cells)
            sum += c.first;
        const float g = 5.0f, scale = (width - g * (float) (cells.size() - 1)) / sum;
        float x = left;
        for (auto& c : cells)
        {
            if (c.second != 0)
                addHit ({ x, y, c.first * scale, rowH }, c.second);
            x += c.first * scale + g;
        }
    };
    colourLabel = { x0, y, labelW, rowH };
    row ({ { 58, colourBase }, { 46, colourBase + 1 }, { 46, colourBase + 2 }, { 46, colourBase + 3 }, { 60, colourBase + 4 } }, x0 + labelW, iw - labelW);
    y += rowH + gap;
    playLabel = { x0, y, labelW, rowH };
    row ({ { 50, holdId }, { 56, styleBase + 1 }, { 44, styleBase + 2 }, { 54, styleBase + 3 }, { 8, 0 }, { 62, bassId } }, x0 + labelW, iw - labelW);
    y += rowH + gap;

    // the progression: a title with ON, 4/8 and CLEAR, then the boxes
    {
        const int n = tool.progLength;
        const bool oneRow = n <= 4 || (iw - 20.0f) / (float) n >= 52.0f;
        const float boxH = 44.0f, boxGap = 6.0f;
        const int perRow = oneRow ? n : 4, rows = oneRow ? 1 : 2;
        progWell = { x0, y, iw, 6.0f + rowH + 6.0f + (float) rows * boxH + (float) (rows - 1) * boxGap + 10.0f };
        const float cx = x0 + iw - 10.0f;
        const float onW = 58.0f, lenW = 54.0f, clearW = 64.0f;
        addHit ({ cx - clearW, y + 6.0f, clearW, rowH }, progClearId);
        addHit ({ cx - clearW - 5.0f - lenW, y + 6.0f, lenW, rowH }, progLenId);
        addHit ({ cx - clearW - lenW - 10.0f - onW, y + 6.0f, onW, rowH }, progOnId);
        progTitle = { x0 + 12.0f, y + 6.0f, iw - 24.0f - onW - lenW - clearW - 12.0f, rowH };
        const float bw = (iw - 20.0f - boxGap * (float) (perRow - 1)) / (float) perRow;
        for (int i = 0; i < kProgressionMax; ++i)
        {
            boxR[(size_t) i] = {};
            if (i >= n)
                continue;
            boxR[(size_t) i] = { x0 + 10.0f + (float) (i % perRow) * (bw + boxGap), y + 6.0f + rowH + 6.0f + (float) (i / perRow) * (boxH + boxGap), bw, boxH };
            addHit (boxR[(size_t) i], boxBase + i);
        }
        y = progWell.getBottom() + gap;
    }

    // PLAYS
    row ({ { 70, playsBase }, { 84, playsBase + 1 }, { 92, playsBase + 2 } }, x0, iw);
    y += rowH + 4.0f;

    // the pads, low; what's going on just above them
    const float room = h - 8.0f - y - kChipRow;
    const float wellH = juce::jlimit (120.0f, 220.0f, room);
    padWell = { x0, h - 8.0f - wellH, iw, wellH };
    placeChordsChip (y);
    auto pw = padWell.reduced (8.0f);
    const float cw = pw.getWidth() / 4.0f, ch = pw.getHeight() / 2.0f;
    for (int i = 0; i < kChordPads; ++i)
    {
        padR[(size_t) i] = { pw.getX() + (float) (i % 4) * cw + 3.0f, pw.getY() + (float) (i / 4) * ch + 3.0f, cw - 6.0f, ch - 6.0f };
        addHit (padR[(size_t) i], padBase + i);
    }
}

void ChordsScreen::placeChordsChip (float y)
{
    // CHORDS ON / OFF on the left, right over the pads it changes (in both layouts, so it's there while you
    // play LIVE KEY too); what's going on beside it.
    const float x0 = 16.0f, iw = (float) getWidth() - 32.0f, chipW = juce::jlimit (112.0f, 150.0f, iw * 0.34f);
    const float rowH = juce::jmax (kChipH, padWell.getY() - y - 4.0f);
    const auto chip = juce::Rectangle<float> (x0, y + (rowH - kChipH) * 0.5f, chipW, kChipH);
    hits.push_back ({ chip, chordsOnId });
    statusR = { x0 + chipW + 10.0f, y, iw - chipW - 10.0f, rowH };
}

int ChordsScreen::idAt (juce::Point<float> p) const
{
    for (auto& hit : hits)
        if (hit.r.expanded (2.0f).contains (p))
            return hit.id;
    return 0;
}

//==============================================================================
void ChordsScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    hold.release();
    if (pressed >= padBase && pressed < padBase + kChordPads)
    {
        pressPad (pressed - padBase, true);   // a pad sounds on the touch, not the lift; the hold only adds a menu later
        hold.press (e.position);
    }
    else if (pressed >= liveKeyBase && pressed < liveKeyBase + 12)
        pressLiveKey (pressed - liveKeyBase);   // a key changes on the touch too, in time
    repaint();
}

void ChordsScreen::mouseDrag (const juce::MouseEvent& e)
{
    hold.move (e.position);
}

// The hold menu: which progression box the chord goes in. The chord keeps sounding under the finger.
HoldSpec ChordsScreen::padMenuSpec (int pad)
{
    HoldSpec s;
    s.title = "CHORD " + tool.padName (pad) + (tool.isOwn (pad) ? U8(" \xc2\xb7 YOUR OWN") : juce::String());
    s.items.push_back ({ "EDIT CHORD", tool.isOwn (pad) ? juce::String ("your own") : juce::String (U8("root \xc2\xb7 type \xc2\xb7 keys")), false, true,
                         [this, pad] { if (onEditChord) onEditChord (pad); } });
    s.items.push_back ({ U8("COPY CHORD TO\xe2\x80\xa6"), "another pad", false, true,
                         [this, pad] { if (showHold) showHold (copyMenuSpec (pad), *this, padR[(size_t) pad].getCentre(), padR[(size_t) pad]); } });
    if (tool.isOwn (pad))
        s.items.push_back ({ "RESET TO SONG KEY", tool.key.label().toLowerCase(), false, true, [this, pad]
                             {
                                 tool.resetToKey (pad);
                                 changed();
                                 if (toast)
                                     toast ("Pad " + juce::String (pad + 1) + " plays " + tool.padName (pad) + " again, the song key's chord.");
                             } });
    for (int box = 0; box < tool.progLength; ++box)
    {
        const int now = tool.progression[(size_t) box];
        s.items.push_back ({ "PUT IN PROGRESSION BOX " + juce::String (box + 1), now >= 0 ? "now " + tool.padName (now) : juce::String ("empty"),
                             false, true, [this, pad, box] { putInBox (pad, box); } });
    }
    return s;
}

HoldSpec ChordsScreen::copyMenuSpec (int pad)
{
    HoldSpec s;
    s.title = "COPY " + tool.padName (pad) + " TO";
    s.spotlight = true;
    for (int to = 0; to < kChordPads; ++to)
    {
        if (to == pad)
            continue;
        s.items.push_back ({ "PAD " + juce::String (to + 1), "now " + tool.padName (to), false, true, [this, pad, to]
                             {
                                 tool.copyChord (pad, to);
                                 changed();
                                 if (toast)
                                     toast ("Pad " + juce::String (to + 1) + " plays " + tool.padName (to) + " too, as its own chord.");
                             } });
    }
    return s;
}

void ChordsScreen::putInBox (int pad, int box)
{
    if (! juce::isPositiveAndBelow (box, tool.progLength) || ! juce::isPositiveAndBelow (pad, kChordPads))
        return;
    tool.progression[(size_t) box] = pad;
    tool.editingBox = box + 1 < tool.progLength ? box + 1 : -1;
    tool.progressionEdited();
    changed();
    if (toast)
        toast (tool.padName (pad) + " is in box " + juce::String (box + 1) + (tool.progressionOn ? "." : ". Turn the progression ON to hear them play a bar each."));
}

void ChordsScreen::holdPad (int pad)
{
    if (showHold && juce::isPositiveAndBelow (pad, kChordPads))
        showHold (padMenuSpec (pad), *this, padR[(size_t) pad].getCentre(), padR[(size_t) pad]);
}

void ChordsScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = idAt (e.position), was = pressed;
    pressed = 0;
    hold.release();
    if (was >= padBase && was < padBase + kChordPads)
        pressPad (was - padBase, false);
    else if (was >= liveKeyBase && was < liveKeyBase + 12)
        {}   // (it changed on the touch)
    else if (id != 0 && id == was)
        press (id);
    repaint();
}

void ChordsScreen::pressPad (int pad, bool down)
{
    if (down)
    {
        const int box = tool.editingBox;
        tool.padDown (pad);
        if (box >= 0)
            changed();   // a box took the chord
    }
    else
        tool.padUp (pad);
    repaint();
}

void ChordsScreen::pressLiveKey (int pc)
{
    tool.setLiveKey (pc);
    const int shift = tool.liveShift();
    const bool waits = tool.liveOnBar && tool.engine().clockRunning() && tool.liveWaiting();
    changed();
    if (toast)
    {
        const auto name = SongKey::pitchName (pc);
        toast ((waits ? "Next bar: " : "Playing in ") + name
               + (shift == 0 ? juce::String (" (the song's key)") : " (" + juce::String (shift > 0 ? "+" : "") + juce::String (shift) + ")") + ".");
    }
}

void ChordsScreen::pressChordsOn()
{
    tool.setChordsOn (! tool.chordsOn);
    if (toast)
        toast (tool.chordsOn ? "Chords on: each pad plays its chord." : "Chords off: each pad plays one note, its chord's lowest.");
    changed();
}

void ChordsScreen::press (int id)
{
    if (id == chordsOnId)
        return pressChordsOn();
    if (id == liveId)
    {
        tool.liveOpen = ! tool.liveOpen;
        layout();
        return changed();
    }
    if (id >= liveKeyBase && id < liveKeyBase + 12)
        return pressLiveKey (id - liveKeyBase);
    if (id == liveOctDownId || id == liveOctUpId)
    {
        tool.nudgeLiveOctave (id == liveOctUpId ? 1 : -1);
        if (toast)
            toast ("Octave " + juce::String (tool.liveOctave() > 0 ? "+" : "") + juce::String (tool.liveOctave()) + ".");
        return changed();
    }
    if (id == liveHomeId)
    {
        tool.liveHome();
        if (toast)
            toast ("Back to the song's key.");
        return changed();
    }
    if (id == liveBarId)
    {
        tool.liveOnBar = ! tool.liveOnBar;
        tool.nudgeLiveOctave (0);   // passes ON BAR to the chords
        if (toast)
            toast (tool.liveOnBar ? "ON BAR: a new key lands on the next bar while the song plays."
                                  : "A new key lands the moment you tap it.");
        return changed();
    }
    if (id == keyId)
    {
        for (auto& hit : hits)
            if (hit.id == keyId && onKeyMenu)
                onKeyMenu (*this, hit.r.toNearestInt());
        return;
    }
    if (id == soundId)
        return showSoundMenu();
    if (id >= colourBase && id < colourBase + kChordColours)
    {
        tool.colour = id - colourBase;
        tool.refreshNotes();
        return changed();
    }
    if (id == holdId)
    {
        tool.hold = ! tool.hold;
        if (! tool.hold)
            tool.releaseAll();
        if (toast)
            toast (tool.hold ? "HOLD on: a chord rings until you tap it again or another one."
                             : "HOLD off: a chord plays while your finger is on it.");
        return changed();
    }
    if (id > styleBase && id < styleBase + 4)
    {
        const int s = id - styleBase;
        tool.style = tool.style == s ? styleChord : s;   // tap again: back to plain chords
        tool.apply();
        return changed();
    }
    if (id == bassId)
    {
        tool.bass = ! tool.bass;
        tool.refreshNotes();
        return changed();
    }
    if (id == progOnId)
    {
        if (tool.engine().progressionMuted())
        {
            tool.progressionEdited();   // it was on a layer: hear it here again
            if (toast)
                toast ("The progression plays here again, over its layer. Turn it off or UNDO the layer to stop the doubling.");
        }
        else
        {
            tool.progressionOn = ! tool.progressionOn;
            tool.progressionEdited();
            if (tool.progressionOn && tool.usedBoxes() == 0 && toast)
                toast ("Tap a box, then a chord. While the song plays, the boxes play one bar each.");
        }
        return changed();
    }
    if (id == progLenId)
    {
        tool.progLength = tool.progLength == 4 ? 8 : 4;
        if (tool.editingBox >= tool.progLength)
            tool.editingBox = -1;
        tool.progressionEdited();
        layout();
        return changed();
    }
    if (id == progClearId)
    {
        tool.progression.fill (-1);
        tool.editingBox = tool.progLength > 0 ? 0 : -1;
        tool.progressionEdited();
        return changed();
    }
    if (id >= boxBase && id < boxBase + kProgressionMax)
    {
        const int box = id - boxBase;
        if (tool.editingBox == box)
        {
            // the picked box again: empty it
            tool.progression[(size_t) box] = -1;
            tool.editingBox = -1;
            tool.progressionEdited();
        }
        else
            tool.editingBox = box;
        return changed();
    }
    if (id >= playsBase && id < playsBase + 3)
    {
        const int k = id - playsBase;
        if (k == 0)
            tool.playsSynth = ! tool.playsSynth;
        else if (k == 2)
            tool.playsVocoder = ! tool.playsVocoder;
        else if (tool.playsPad)
            tool.playsPad = false;
        else if (tool.engine().hasPadSound())
            tool.playsPad = true;
        else
            return showSoundMenu();   // no pad picked yet: pick one
        tool.apply();
        return changed();
    }
}

juce::String ChordsScreen::soundLabel() const
{
    if (tool.playsPad && ! tool.playsSynth && tool.padSoundPad >= 0)
        return "SOUND: PAD " + juce::String (tool.padSoundId.isNotEmpty() ? tool.padSoundId : juce::String (tool.padSoundPad + 1));
    return "SOUND: " + synthPresets()[(size_t) tool.synthPreset].name.toUpperCase();
}

void ChordsScreen::showSoundMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("SYNTH SOUND");
    for (int k = 0; k < kSynthKinds; ++k)
    {
        if (k == kKindsPerFamily)
            m.addSectionHeader ("BASS SOUND");
        juce::PopupMenu sub;
        bool any = false;
        for (int p : synthPresetsOf (k))
        {
            sub.addItem (1 + p, synthPresets()[(size_t) p].name, true, p == tool.synthPreset);
            any = any || p == tool.synthPreset;
        }
        m.addSubMenu (synthKindName (k), sub, true, nullptr, any);
    }
    m.addSectionHeader ("MY PAD SOUND");
    const auto names = padSounds ? padSounds() : juce::StringArray();
    const auto pads = padSoundPads ? padSoundPads() : juce::Array<int>();
    if (names.isEmpty())
        m.addItem (-1, "(no pads with a sound yet)", false);
    for (int i = 0; i < names.size() && i < pads.size(); ++i)
        m.addItem (1000 + pads[i], names[i], true, tool.playsPad && tool.padSoundPad == pads[i]);
    juce::Rectangle<int> at;
    for (auto& hit : hits)
        if (hit.id == soundId)
            at = hit.r.toNearestInt();
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (at)),
                     [safe = juce::Component::SafePointer<ChordsScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         auto& t = safe->tool;
                         if (r >= 1000)
                         {
                             if (safe->onPadSound && safe->onPadSound (r - 1000))
                             {
                                 t.playsPad = true;
                                 t.apply();
                             }
                         }
                         else
                         {
                             t.synthPreset = r - 1;
                             t.playsSynth = true;
                             t.apply();
                         }
                         safe->changed();
                     });
}

juce::String ChordsScreen::statusText() const
{
    if (! tool.chordsOn && tool.editingBox < 0)
        return "Chords off: each pad plays one note, its chord's lowest. Tap CHORDS OFF for the chords again.";
    if (tool.liveOpen)
        return tool.liveOctave() != 0 || tool.liveShift() != 0
                   ? "Your chords, moved to a new key. BACK returns to " + SongKey::pitchName (tool.key.root) + ". LIVE shows the chord settings again."
                   : "Tap a key and the chords move to it, the shapes the same. ON BAR: it lands on the next bar.";
    if (tool.editingBox >= 0)
        return "Box " + juce::String (tool.editingBox + 1) + ": tap a chord for it. Tap the box again to empty it.";
    const auto locked = lockedText ? lockedText() : juce::String();
    if (tool.engine().progressionMuted())
        return locked.isNotEmpty() ? locked + U8(" \xc2\xb7 muted here \xc2\xb7 tap MUTED to hear it") : U8("Progression on a layer \xc2\xb7 muted here");
    if (tool.progressionOn && tool.usedBoxes() > 0)
        return "The progression plays one chord a bar while the song plays. A pad you hold plays over it.";
    if (! tool.playsSynth && ! tool.playsPad && ! tool.playsVocoder)
        return "The chords play nothing: pick SYNTH, MY PAD SOUND or VOCODER NOTES.";
    if (locked.isNotEmpty())
        return locked;
    return tool.hold ? "Tap a chord: it rings until you tap it again. REC records the chords on the next layer."
                     : "Hold a chord to hear it. REC records the chords on the next layer.";
}

//==============================================================================
void ChordsScreen::paint (juce::Graphics& g)
{
    // Chords · A minor
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (19.0f), 0.02f));
    const auto keyName = SongKey::pitchName (tool.key.root) + (tool.key.minor ? " minor" : " major");
    g.drawFittedText ("Chords " + juce::String (U8("\xc2\xb7 ")) + keyName, titleR.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    for (auto [r, t] : { std::pair { colourLabel, "COLOUR" }, std::pair { playLabel, "PLAY" } })
    {
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.1f));
        g.drawText (t, r, juce::Justification::centredLeft);
    }

    // LIVE KEY: its well, what plays, the twelve keys
    if (! liveWell.isEmpty())
    {
        drawWell (g, liveWell, 12.0f);
        g.setColour (juce::Colour (0xffb7a9c2));
        g.setFont (spaced (condensedBold (15.0f), 0.12f));
        g.drawText ("LIVE KEY", liveTitle, juce::Justification::centredLeft);
        const int playingKey = tool.playingLiveKey(), asked = tool.liveKey(), home = tool.key.root;
        const bool waiting = tool.liveWaiting() && asked != playingKey;
        g.setColour (juce::Colour (0xff0f0c15));
        g.fillRoundedRectangle (liveLcd, 6.0f);
        juce::String text = waiting ? "NEXT: " + SongKey::pitchName (asked) : "IN " + SongKey::pitchName (playingKey);
        if (tool.liveOctave() != 0)
            text << "  OCT " << (tool.liveOctave() > 0 ? "+" : "") << tool.liveOctave();
        g.setColour (waiting ? col::lcdAmber.brighter (0.2f) : col::lcdAmber);
        g.setFont (spaced (lcdFont (17.0f), 0.04f));
        g.drawFittedText (text, liveLcd.reduced (8.0f, 0).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        auto drawKey = [&] (int k)
        {
            const bool black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
            auto r = liveKeyR[(size_t) k];
            const bool down = pressed == liveKeyBase + k;
            juce::Colour top = black ? juce::Colour (0xff3a2f4f) : juce::Colour (0xfff3eadb);
            juce::Colour bottom = black ? juce::Colour (0xff1a1612) : juce::Colour (0xffc9bcd6);
            const bool lit = k == playingKey || (waiting && k == asked && blinkOn);
            if (lit)
                top = col::lcdAmber.brighter (0.25f), bottom = col::lcdAmber;
            g.setColour (black ? juce::Colours::black : juce::Colour (0xff8e7fa0));
            g.fillRoundedRectangle (r.translated (0, 3.0f), 6.0f);
            r = r.translated (0, down ? 2.0f : 0.0f);
            g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            if (k == home)
            {
                g.setColour (kChords);
                g.fillRoundedRectangle (r.removeFromBottom (5.0f).reduced (6.0f, 0.0f).translated (0.0f, -2.0f), 2.0f);
            }
            g.setColour (lit || ! black ? col::onBright : kPadText);
            g.setFont (condensedBold (black ? 15.0f : 18.0f));
            g.drawFittedText (SongKey::pitchName (k), r.withTrimmedBottom (5.0f).removeFromBottom (24.0f).toNearestInt(), juce::Justification::centred, 1, 0.6f);
        };
        for (int k : { 0, 2, 4, 5, 7, 9, 11 })
            drawKey (k);
        for (int k : { 1, 3, 6, 8, 10 })
            drawKey (k);
    }

    // the progression's well
    if (! progWell.isEmpty())
    {
        drawWell (g, progWell, 12.0f);
        g.setColour (juce::Colour (0xffb7a9c2));
        g.setFont (spaced (lcdFont (14.0f), 0.04f));
        g.drawFittedText (U8("PROGRESSION \xc2\xb7 ONE CHORD PER BAR"), progTitle.toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
    }
    const int st = step();
    const int s = sounding();
    for (int i = 0; i < tool.progLength; ++i)
    {
        auto r = boxR[(size_t) i];
        const int pad = tool.progression[(size_t) i];
        const bool picked = tool.editingBox == i, playing = st == i && tool.progressionOn;
        const bool down = pressed == boxBase + i;
        g.setColour (playing ? kChords : kBoxDark);
        g.fillRoundedRectangle (r.translated (0, down ? 1.0f : 0.0f), 8.0f);
        if (picked)
        {
            g.setColour (col::lcdAmber);
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.5f);
        }
        g.setColour (playing ? col::onBright : (pad >= 0 ? kPadText : juce::Colour (0xff8e7fa0)));
        g.setFont (condensedBold (pad >= 0 ? 17.0f : 15.0f));
        g.drawFittedText (pad >= 0 ? tool.livePadName (pad) : juce::String (i + 1), r.reduced (3, 0).toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    // the chips
    for (auto& hit : hits)
    {
        const int id = hit.id;
        if (id == 0 || (id >= boxBase && id < boxBase + kProgressionMax) || (id >= padBase && id < liveId) || (id >= liveKeyBase && id < liveKeyBase + 12))
            continue;
        const bool down = pressed == id;
        juce::String text;
        bool lit = false;
        juce::Colour litColour;
        if (id == keyId) text = "KEY";
        else if (id == liveId) { text = "LIVE"; lit = tool.liveOpen; }
        else if (id == liveOctDownId) text = "OCT -";
        else if (id == liveOctUpId) text = "OCT +";
        else if (id == liveHomeId) { text = "BACK"; lit = false; }
        else if (id == liveBarId) { text = "ON BAR"; lit = tool.liveOnBar; }
        else if (id == chordsOnId) { text = tool.chordsOn ? "CHORDS ON" : "CHORDS OFF"; lit = tool.chordsOn; litColour = kChords; }
        else if (id == soundId) text = soundLabel();
        else if (id >= colourBase && id < colourBase + kChordColours) { text = chordColourName (id - colourBase); lit = tool.colour == id - colourBase; }
        else if (id == holdId) { text = "HOLD"; lit = tool.hold; }
        else if (id > styleBase && id < styleBase + 4) { text = kStyleNames[id - styleBase]; lit = tool.style == id - styleBase; }
        else if (id == bassId) { text = "+ BASS"; lit = tool.bass; }
        else if (id == progOnId)
        {
            const bool muted = tool.engine().progressionMuted();
            text = muted ? "MUTED" : tool.progressionOn ? "ON" : "OFF";
            lit = tool.progressionOn && ! muted;
            litColour = kChords;
        }
        else if (id == progLenId) text = tool.progLength == 4 ? "4 BARS" : "8 BARS";
        else if (id == progClearId) text = "CLEAR";
        else if (id >= playsBase && id < playsBase + 3)
        {
            const int k = id - playsBase;
            text = (k == 0 ? juce::String ("PLAYS: ") : juce::String()) + kPlaysNames[k];
            lit = k == 0 ? tool.playsSynth : k == 1 ? tool.playsPad : tool.playsVocoder;
        }
        else
            continue;
        drawChip (g, hit.r, text, lit, down, id == soundId ? 13.0f : 14.0f, litColour);
    }

    // what's going on
    g.setColour (col::inkDim);
    g.setFont (small (13.5f));
    g.drawFittedText (statusText(), statusR.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);

    // the pads
    drawWell (g, padWell, 12.0f);
    for (int i = 0; i < kChordPads; ++i)
    {
        auto p = padR[(size_t) i];
        const bool lit = s == i;
        const float down = pressed == padBase + i ? 2.0f : 0.0f;
        g.setColour (juce::Colour (0xff0f0c15));
        g.fillRoundedRectangle (p.translated (0, 3), 10.0f);
        g.setColour (lit ? kChords : kPadDark);
        g.fillRoundedRectangle (p.translated (0, down), 10.0f);
        if (tool.editingBox >= 0)
        {
            g.setColour (col::lcdAmber.withAlpha (0.5f));
            g.drawRoundedRectangle (p.translated (0, down).reduced (1.0f), 10.0f, 1.5f);
        }
        g.setColour (lit ? col::onBright : kPadText);
        g.setFont (condensedBold (p.getHeight() > 70.0f ? 20.0f : 17.0f));
        // Chords off: the note the pad plays big, its chord small under it.
        juce::String big = tool.livePadName (i), under = tool.padNumeral (i);
        if (! tool.chordsOn)
        {
            const auto notes = tool.padNotes (i);
            if (! notes.isEmpty())
                big = SongKey::pitchName (((notes[0] + tool.engine().playingShift()) % 12 + 12) % 12);
            under = tool.livePadName (i);
        }
        g.drawFittedText (big, p.translated (0, down).withTrimmedBottom (p.getHeight() * 0.42f).reduced (3, 0).toNearestInt(), juce::Justification::centredBottom, 1, 0.6f);
        if (tool.isOwn (i))
        {
            // your own chord: a lilac corner dot (amber on the lit pad)
            g.setColour (lit ? col::onBright : kChords);
            g.fillEllipse (p.getRight() - 15.0f, p.getY() + 7.0f + down, 8.0f, 8.0f);
        }
        g.setColour (tool.isOwn (i) ? (lit ? juce::Colour (0xff0f0c15) : kChords) : lit ? juce::Colour (0xff0f0c15) : juce::Colour (0xffb7a9c2));
        g.setFont (lcdFont (15.0f));
        g.drawFittedText (under, p.translated (0, down).withTrimmedTop (p.getHeight() * 0.6f).reduced (3, 0).toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
    }
}
