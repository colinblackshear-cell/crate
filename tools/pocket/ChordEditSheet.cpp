#include "ChordEditSheet.h"

using namespace pocket;

namespace
{
const juce::Colour kChordLilac { 0xff9a5cf0 };
}

ChordEditSheet::ChordEditSheet (ChordsTool& t) : tool (t)
{
    setDragZone (58);
    for (int i = 0; i < 12; ++i)
    {
        roots[i].setText (SongKey::pitchName (i));
        roots[i].setFontHeight (17.0f);
        roots[i].onClick = [this, i] { pickRoot (i); };
        addAndMakeVisible (roots[i]);
    }
    const auto& kinds = ownChordTypes();
    for (int i = 0; i < 15; ++i)
    {
        types[i].setText (i < (int) kinds.size() ? kinds[(size_t) i].label : "");
        types[i].setFontHeight (16.0f);
        types[i].onClick = [this, i] { pickType (i); };
        addAndMakeVisible (types[i]);
    }
    for (auto* b : { &invDown, &invUp, &octDown, &octUp, &keysDown, &keysUp })
    {
        b->setFontHeight (22.0f);
        addAndMakeVisible (*b);
    }
    invDown.onClick = [this] { invert (-1); };
    invUp.onClick = [this] { invert (1); };
    octDown.onClick = [this] { shiftOctave (-1); };
    octUp.onClick = [this] { shiftOctave (1); };
    keysDown.onClick = [this] { moveKeys (-1); };
    keysUp.onClick = [this] { moveKeys (1); };
    hearButton.setFontHeight (17.0f);
    resetButton.setFontHeight (15.0f);
    doneButton.setFontHeight (18.0f);
    hearButton.onClick = [this] { hear(); };
    resetButton.onClick = [this] { resetToKey(); };
    doneButton.onClick = [this] { if (onDone) onDone(); };
    for (auto* b : { &hearButton, &resetButton, &doneButton })
        addAndMakeVisible (*b);
}

void ChordEditSheet::open (int pad)
{
    padIndex = juce::jlimit (0, kChordPads - 1, pad);
    work = tool.ownChord (padIndex);
    if (! work.on)
    {
        // the song key's chord, to start from
        work = {};
        work.notes = tool.keyNotes (padIndex);
        if (! chordmath::detectOwn (work.notes, work.root, work.type))
            work.type = -1;
    }
    const int low = work.notes.isEmpty() ? 48 : work.notes.getFirst();
    kbLow = juce::jlimit (24, 84, 12 * (low / 12));
    refresh();
}

void ChordEditSheet::apply (bool audition)
{
    work.on = true;
    tool.setOwnChord (padIndex, work);
    // the keys follow a chord that moved out of sight
    if (! work.notes.isEmpty() && (work.notes.getFirst() < kbLow || work.notes.getLast() >= kbLow + 24))
        kbLow = juce::jlimit (24, 84, 12 * (work.notes.getFirst() / 12));
    if (audition)
        tool.audition (padIndex, 650);
    if (onChanged)
        onChanged();
    refresh();
}

void ChordEditSheet::refresh()
{
    for (int i = 0; i < 12; ++i)
        roots[i].setLook (work.type >= 0 && work.root == i ? Look::amber : Look::cream);
    for (int i = 0; i < 15; ++i)
        types[i].setLook (work.type == i ? Look::amber : Look::cream);
    repaint();
}

void ChordEditSheet::pickRoot (int pc)
{
    work.root = juce::jlimit (0, 11, pc);
    if (work.type < 0)
        work.type = 0;
    work.notes = chordmath::buildOwn (work.root, work.type, work.inversion, work.octave);
    apply();
}

void ChordEditSheet::pickType (int t)
{
    work.type = juce::jlimit (0, (int) ownChordTypes().size() - 1, t);
    work.notes = chordmath::buildOwn (work.root, work.type, work.inversion, work.octave);
    apply();
}

// INV: the lowest note up an octave (+), or the highest down (-), on whatever notes the chord has
void ChordEditSheet::invert (int direction)
{
    if (work.notes.isEmpty())
        return;
    if (direction > 0)
        work.notes.set (0, juce::jmin (115, work.notes[0] + 12));
    else
        work.notes.set (work.notes.size() - 1, juce::jmax (12, work.notes.getLast() - 12));
    work.notes.sort();
    work.inversion += direction > 0 ? 1 : -1;
    apply();
}

void ChordEditSheet::shiftOctave (int direction)
{
    if (work.notes.isEmpty())
        return;
    const int d = direction > 0 ? 12 : -12;
    if (work.notes.getFirst() + d < 12 || work.notes.getLast() + d > 115)
    {
        if (toast)
            toast ("That's as far as it goes");
        return;
    }
    for (auto& n : work.notes)
        n += d;
    work.octave += direction > 0 ? 1 : -1;
    apply();
}

void ChordEditSheet::toggleNote (int midi)
{
    if (work.notes.contains (midi))
    {
        if (work.notes.size() == 1)
        {
            if (toast)
                toast ("A chord needs one note at least");
            return;
        }
        work.notes.removeFirstMatchingValue (midi);
    }
    else
    {
        if (work.notes.size() >= kMaxChordNotes)
        {
            if (toast)
                toast ("Eight notes at most");
            return;
        }
        work.notes.add (midi);
        work.notes.sort();
    }
    int r = work.root, t = -1;
    if (chordmath::detectOwn (work.notes, r, t))
    {
        work.root = r;
        work.type = t;
    }
    else
        work.type = -1;   // CUSTOM: no ROOT or TYPE lit
    apply();
}

void ChordEditSheet::moveKeys (int octaves)
{
    kbLow = juce::jlimit (24, 84, kbLow + 12 * octaves);
    repaint();
}

void ChordEditSheet::hear()
{
    tool.audition (padIndex, 1100);
}

void ChordEditSheet::resetToKey()
{
    tool.resetToKey (padIndex);
    open (padIndex);
    if (onChanged)
        onChanged();
    tool.audition (padIndex, 650);
    if (toast)
        toast ("Pad " + juce::String (padIndex + 1) + " plays " + tool.padName (padIndex) + U8(" again, the song key's chord"));
}

//==============================================================================
bool ChordEditSheet::isBlack (int midi)
{
    const int pc = ((midi % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

juce::Rectangle<float> ChordEditSheet::keyRect (int midi) const
{
    if (midi < kbLow || midi >= kbLow + 24)
        return {};
    static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    const float ww = keysArea.getWidth() / 14.0f;
    const int rel = midi - kbLow, oct = rel / 12, pc = rel % 12;
    const float x = keysArea.getX() + ((float) (oct * 7 + whiteIndex[pc])) * ww;
    if (! isBlack (midi))
        return { x, keysArea.getY(), ww, keysArea.getHeight() };
    const float bw = ww * 0.62f;
    return { x + ww - bw * 0.5f, keysArea.getY(), bw, keysArea.getHeight() * 0.6f };
}

int ChordEditSheet::keyAt (juce::Point<float> p) const
{
    if (! keysArea.contains (p))
        return -1;
    for (int m = kbLow; m < kbLow + 24; ++m)
        if (isBlack (m) && keyRect (m).contains (p))
            return m;
    for (int m = kbLow; m < kbLow + 24; ++m)
        if (! isBlack (m) && keyRect (m).contains (p))
            return m;
    return -1;
}

void ChordEditSheet::mouseDown (const juce::MouseEvent& e)
{
    if (const int m = keyAt (e.position); m >= 0)
        toggleNote (m);
}

//==============================================================================
void ChordEditSheet::resized()
{
    const int w = getWidth(), h = getHeight(), x0 = 16, iw = w - 32;
    // a short phone (an SE): the ROOT and TYPE labels go and the gaps close up, so the keys keep a playable height
    const bool compact = h - bottomInset < 640;
    const int gap = compact ? 4 : 8, label = compact ? 0 : 16;
    nameArea = { (float) w * 0.5f, 14.0f, (float) w * 0.5f - 16.0f, 44.0f };
    notesArea = { 18.0f, 58.0f, (float) iw, 18.0f };
    int y = compact ? 82 : 80;
    rootLabel = compact ? juce::Rectangle<float>() : juce::Rectangle<float> ((float) x0, (float) y, 120.0f, 14.0f);
    y += label;
    const int rw = (iw - 5 * 6) / 6;
    for (int i = 0; i < 12; ++i)
        roots[i].setBounds (x0 + (i % 6) * (rw + 6), y + (i / 6) * 50, rw, 44);
    y += 94 + gap;
    typeLabel = compact ? juce::Rectangle<float>() : juce::Rectangle<float> ((float) x0, (float) y, 120.0f, 14.0f);
    y += label;
    const int tw = (iw - 4 * 6) / 5;
    for (int i = 0; i < 15; ++i)
        types[i].setBounds (x0 + (i % 5) * (tw + 6), y + (i / 5) * 50, tw, 44);
    y += 144 + gap;
    // INV [-] [+]   OCT [-] [+]
    {
        const int half = (iw - 12) / 2;
        invLabel = { (float) x0, (float) y, (float) (half - 2 * 50), 44.0f };
        invDown.setBounds (x0 + half - 2 * 50 + 6, y, 44, 44);
        invUp.setBounds (x0 + half - 50 + 6, y, 44, 44);
        const int ox = x0 + half + 12;
        octLabel = { (float) ox, (float) y, (float) (half - 2 * 50), 44.0f };
        octDown.setBounds (ox + half - 2 * 50 + 6, y, 44, 44);
        octUp.setBounds (ox + half - 50 + 6, y, 44, 44);
    }
    y += 44 + gap;
    keysLabel = { (float) x0, (float) y, (float) (iw - 2 * 50), 44.0f };
    keysDown.setBounds (x0 + iw - 94, y, 44, 44);
    keysUp.setBounds (x0 + iw - 44, y, 44, 44);
    y += 44 + (compact ? 4 : 6);
    const int bottomY = h - bottomInset - 8 - 52;
    const int kh = juce::jlimit (70, 150, bottomY - (compact ? 8 : 10) - y);
    keysArea = { (float) x0, (float) y, (float) iw, (float) kh };
    auto row = juce::Rectangle<int> (x0, bottomY, iw, 52);
    hearButton.setBounds (row.removeFromLeft (96));
    row.removeFromLeft (8);
    doneButton.setBounds (row.removeFromRight (96));
    row.removeFromRight (8);
    resetButton.setBounds (row);
}

void ChordEditSheet::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (fraunces (24.0f));
    g.drawText ("Edit chord", 18, 18, getWidth() / 2, 32, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (13.0f), 0.1f));
    g.drawText ("PAD " + juce::String (padIndex + 1), 18, 46, 120, 14, juce::Justification::centredLeft);

    // the chord's name, and whether it's the pad's own
    const auto name = chordmath::nameNotes (work.notes);
    g.setColour (col::ink);
    g.setFont (condensedBold (30.0f));
    g.drawFittedText (name.isNotEmpty() ? name : juce::String ("CUSTOM"), nameArea.withTrimmedBottom (14).toNearestInt(), juce::Justification::centredRight, 1, 0.6f);
    g.setColour (work.on ? kChordLilac.darker (0.35f) : col::inkDim);
    g.setFont (spaced (condensedBold (12.0f), 0.12f));
    g.drawText (work.on ? U8("\xe2\x97\x8f OWN CHORD") : U8("SONG KEY \xc2\xb7 TAP TO MAKE IT YOUR OWN"), nameArea.withTrimmedTop (30), juce::Justification::centredRight);
    juce::StringArray noteNames;
    for (int n : work.notes)
        noteNames.add (SongKey::noteName (n));
    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    g.drawFittedText (noteNames.joinIntoString ("  "), notesArea.withTrimmedLeft (110).toNearestInt(), juce::Justification::centredRight, 1, 0.8f);

    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (12.0f), 0.12f));
    g.drawText ("ROOT", rootLabel, juce::Justification::centredLeft);
    g.drawText ("TYPE", typeLabel, juce::Justification::centredLeft);
    g.setFont (spaced (condensedBold (14.0f), 0.08f));
    g.setColour (col::ink);
    g.drawText ("INVERSION " + juce::String (work.inversion), invLabel, juce::Justification::centredLeft);
    g.drawText ("OCTAVE " + (work.octave > 0 ? "+" + juce::String (work.octave) : juce::String (work.octave)), octLabel, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (12.0f), 0.1f));
    g.drawFittedText ("KEYS " + SongKey::noteName (kbLow) + U8("\xe2\x80\x93") + SongKey::noteName (kbLow + 23) + U8(" \xc2\xb7 TAP A KEY TO ADD OR TAKE A NOTE"),
                      keysLabel.toNearestInt(), juce::Justification::centredLeft, 2, 0.8f);

    // the keys: white, then black on top; the chord's notes lit
    drawWell (g, keysArea.expanded (4.0f), 10.0f);
    for (int pass = 0; pass < 2; ++pass)
        for (int m = kbLow; m < kbLow + 24; ++m)
        {
            if (isBlack (m) != (pass == 1))
                continue;
            auto r = keyRect (m);
            const bool lit = work.notes.contains (m);
            if (pass == 0)
            {
                g.setColour (lit ? kChordLilac : col::creamLight);
                g.fillRoundedRectangle (r.reduced (1.0f, 0.0f), 4.0f);
                if (m % 12 == 0)
                {
                    g.setColour (col::inkDim);
                    g.setFont (small (11.0f));
                    g.drawText (SongKey::noteName (m), r.withTrimmedTop (r.getHeight() - 18.0f), juce::Justification::centred);
                }
            }
            else
            {
                g.setColour (lit ? kChordLilac.darker (0.25f) : juce::Colour (0xff221b2d));
                g.fillRoundedRectangle (r, 3.0f);
            }
            if (lit)
            {
                g.setColour (col::ink);
                g.fillEllipse (r.getCentreX() - 4.0f, r.getBottom() - (pass == 0 ? 30.0f : 14.0f), 8.0f, 8.0f);
            }
        }
}
