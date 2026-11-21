#include "ChordsScreen.h"

using namespace pocket;

namespace
{
const juce::Colour kChords { 0xffb9a3dc }, kPadDark { 0xff4a4037 }, kPadText { 0xfff1e7d1 }, kBoxDark { 0xff2b2520 };
const char* const kStyleNames[4] = { "HOLD", "STRUM", "ARP", "STABS" };
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
    if (s != shownPad || st != shownStep || muted != shownMuted)
    {
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
        const float soundW = juce::jlimit (110.0f, 170.0f, iw * 0.4f), keyW = 56.0f;
        addHit ({ x0 + iw - soundW, y, soundW, rowH }, soundId);
        addHit ({ x0 + iw - soundW - 6.0f - keyW, y, keyW, rowH }, keyId);
        titleR = { x0, y, iw - soundW - keyW - 18.0f, rowH };
    }
    y += rowH + gap;

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
    const float room = h - 8.0f - y - 22.0f;
    const float wellH = juce::jlimit (120.0f, 220.0f, room);
    padWell = { x0, h - 8.0f - wellH, iw, wellH };
    statusR = { x0, y, iw, juce::jmax (18.0f, padWell.getY() - y - 4.0f) };
    auto pw = padWell.reduced (8.0f);
    const float cw = pw.getWidth() / 4.0f, ch = pw.getHeight() / 2.0f;
    for (int i = 0; i < kChordPads; ++i)
    {
        padR[(size_t) i] = { pw.getX() + (float) (i % 4) * cw + 3.0f, pw.getY() + (float) (i / 4) * ch + 3.0f, cw - 6.0f, ch - 6.0f };
        addHit (padR[(size_t) i], padBase + i);
    }
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
    if (pressed >= padBase && pressed < padBase + kChordPads)
        pressPad (pressed - padBase, true);   // a pad sounds on the touch, not the lift
    repaint();
}

void ChordsScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = idAt (e.position), was = pressed;
    pressed = 0;
    if (was >= padBase && was < padBase + kChordPads)
        pressPad (was - padBase, false);
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

void ChordsScreen::press (int id)
{
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

    // the progression's well
    drawWell (g, progWell, 12.0f);
    g.setColour (juce::Colour (0xffcdbfa5));
    g.setFont (spaced (lcdFont (14.0f), 0.04f));
    g.drawFittedText (U8("PROGRESSION \xc2\xb7 ONE CHORD PER BAR"), progTitle.toNearestInt(), juce::Justification::centredLeft, 2, 0.7f);
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
        g.setColour (playing ? col::ink : (pad >= 0 ? kPadText : juce::Colour (0xff6a5c4e)));
        g.setFont (condensedBold (pad >= 0 ? 17.0f : 15.0f));
        g.drawFittedText (pad >= 0 ? tool.padName (pad) : juce::String (i + 1), r.reduced (3, 0).toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }

    // the chips
    for (auto& hit : hits)
    {
        const int id = hit.id;
        if (id == 0 || (id >= boxBase && id < boxBase + kProgressionMax) || id >= padBase)
            continue;
        const bool down = pressed == id;
        juce::String text;
        bool lit = false;
        juce::Colour litColour;
        if (id == keyId) text = "KEY";
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
        g.setColour (juce::Colour (0xff120f0c));
        g.fillRoundedRectangle (p.translated (0, 3), 10.0f);
        g.setColour (lit ? kChords : kPadDark);
        g.fillRoundedRectangle (p.translated (0, down), 10.0f);
        if (tool.editingBox >= 0)
        {
            g.setColour (col::lcdAmber.withAlpha (0.5f));
            g.drawRoundedRectangle (p.translated (0, down).reduced (1.0f), 10.0f, 1.5f);
        }
        g.setColour (lit ? col::ink : kPadText);
        g.setFont (condensedBold (p.getHeight() > 70.0f ? 20.0f : 17.0f));
        g.drawFittedText (tool.padName (i), p.translated (0, down).withTrimmedBottom (p.getHeight() * 0.42f).reduced (3, 0).toNearestInt(), juce::Justification::centredBottom, 1, 0.6f);
        g.setColour (lit ? juce::Colour (0xff3a2a55) : juce::Colour (0xffa89a86));
        g.setFont (lcdFont (15.0f));
        g.drawFittedText (tool.padNumeral (i), p.translated (0, down).withTrimmedTop (p.getHeight() * 0.6f).reduced (3, 0).toNearestInt(), juce::Justification::centredTop, 1, 0.7f);
    }
}
