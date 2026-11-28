#include "FixNotesSheet.h"

using namespace pocket;

namespace
{
constexpr int kSide = 16, kGap = 6, kButtonH = 50;
constexpr float kLabelW = 40.0f;
constexpr double kStep = 0.25;   // a 1/16, in beats

juce::String lengthName (double beats)
{
    const int sixteenths = (int) std::round (beats / kStep);
    if (std::abs (beats - sixteenths * kStep) > 1.0e-3)
        return juce::String (beats, 2) + " beats";
    switch (sixteenths)
    {
        case 1: return "1/16";
        case 2: return "1/8";
        case 3: return "3/16";
        case 4: return "1/4";
        case 6: return "3/8";
        case 8: return "1/2";
        case 16: return "1 bar";
        default: return juce::String (sixteenths) + "/16";
    }
}

double wrapBeat (double beat, double len)
{
    beat = std::fmod (beat, len);
    if (beat < 0)
        beat += len;
    return beat > len - 1.0e-6 ? 0.0 : beat;
}
}

FixNotesSheet::FixNotesSheet()
{
    setDragZone (56);
    done.setFontHeight (16.0f);
    done.onClick = [this] { press (doneId); };
    addAndMakeVisible (done);
    const std::pair<PButton*, int> buttons[] = { { &up, upId }, { &down, downId }, { &earlier, earlierId }, { &later, laterId },
                                                 { &shorter, shorterId }, { &longer, longerId }, { &remove, deleteId }, { &undo, undoId } };
    for (auto [b, id] : buttons)
    {
        b->setFontHeight (17.0f);
        b->onClick = [this, id = id] { press (id); };
        addAndMakeVisible (*b);
    }
}

FixNotesSheet::~FixNotesSheet() { stopTimer(); }

void FixNotesSheet::show (int l, const juce::String& w, const SynthPattern& p, SongKey k)
{
    shownLayer = l;
    what = w;
    pat = p;
    for (auto& n : pat.notes)
        n.skipUntil = -1;
    key = k;
    undos.clear();
    pickedNote = -1;
    bar = 0;
    drag = Drag::none;
    bars.clear();
    for (int i = 0; i < pat.bars; ++i)
    {
        auto b = std::make_unique<PButton> ("Bar " + juce::String (i + 1));
        b->setFontHeight (14.0f);
        b->onClick = [this, i] { press (barBase + i); };
        addAndMakeVisible (*b);
        bars.push_back (std::move (b));
    }
    fitRows();
    refresh();
    startTimerHz (30);
}

//==============================================================================
void FixNotesSheet::fitRows()
{
    int lo = 127, hi = 0;
    for (auto& n : pat.notes)
    {
        lo = juce::jmin (lo, n.note);
        hi = juce::jmax (hi, n.note);
    }
    if (pat.notes.empty())
        lo = hi = 48;
    const int mid = (lo + hi) / 2;
    lowNote = juce::jlimit (0, 127 - kRows + 1, mid - kRows / 2);
    if (hi - lo < kRows)
    {
        lowNote = juce::jmin (lowNote, lo);
        lowNote = juce::jmax (lowNote, hi - kRows + 1);
    }
    lowNote = juce::jlimit (0, 127 - kRows + 1, lowNote);
}

void FixNotesSheet::keepInView (int note)
{
    if (note < lowNote)
        lowNote = juce::jmax (0, note);
    else if (note > lowNote + kRows - 1)
        lowNote = juce::jmin (127 - kRows + 1, note - kRows + 1);
}

int FixNotesSheet::stepInKey (int note, int dir) const
{
    for (int i = 1; i <= 12; ++i)
    {
        const int n = note + dir * i;
        if (n < 0 || n > 127)
            break;
        if (key.inScale (n))
            return n;
    }
    return juce::jlimit (0, 127, note + dir);
}

void FixNotesSheet::changed (const SynthPattern& before)
{
    undos.push_back (before);
    if (undos.size() > 64)
        undos.erase (undos.begin());
    if (onChange)
        onChange (pat);
    refresh();
}

void FixNotesSheet::pick (int index)
{
    pickedNote = juce::isPositiveAndBelow (index, (int) pat.notes.size()) ? index : -1;
    if (pickedNote >= 0)
    {
        bar = juce::jlimit (0, pat.bars - 1, (int) std::floor (pat.notes[(size_t) pickedNote].beat / 4.0));
        keepInView (pat.notes[(size_t) pickedNote].note);
    }
    refresh();
}

void FixNotesSheet::showBar (int b)
{
    bar = juce::jlimit (0, juce::jmax (0, pat.bars - 1), b);
    refresh();
}

void FixNotesSheet::tapCell (int step, int note)
{
    const double beat = step * kStep;
    for (int i = 0; i < (int) pat.notes.size(); ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        if (n.note == note && beat >= n.beat - 1.0e-6 && beat < n.beat + juce::jmax (kStep, n.length) - 1.0e-6)
            return pick (i);
    }
    if ((int) pat.notes.size() >= kSynthMaxNotes)
        return;
    const auto before = pat;
    SynthNote n;
    n.beat = wrapBeat (beat, pat.lengthBeats());
    n.note = juce::jlimit (0, 127, note);
    n.length = pickedNote >= 0 ? pat.notes[(size_t) pickedNote].length : 0.5;
    n.velocity = pickedNote >= 0 ? pat.notes[(size_t) pickedNote].velocity : 0.8f;
    pat.notes.push_back (n);
    pickedNote = (int) pat.notes.size() - 1;
    changed (before);
}

void FixNotesSheet::press (int id)
{
    if (id == doneId)
    {
        stopTimer();
        if (onDone)
            onDone();
        return;
    }
    if (id >= barBase)
        return showBar (id - barBase);
    if (id == undoId)
    {
        if (undos.empty())
            return;
        pat = undos.back();
        undos.pop_back();
        if (pickedNote >= (int) pat.notes.size())
            pickedNote = -1;
        if (onChange)
            onChange (pat);
        return refresh();
    }
    if (pickedNote < 0 || pickedNote >= (int) pat.notes.size())
        return;
    const auto before = pat;
    auto& n = pat.notes[(size_t) pickedNote];
    const double len = pat.lengthBeats();
    switch (id)
    {
        case upId:      n.note = stepInKey (n.note, 1); keepInView (n.note); break;
        case downId:    n.note = stepInKey (n.note, -1); keepInView (n.note); break;
        case earlierId: n.beat = wrapBeat (n.beat - kStep, len); bar = (int) std::floor (n.beat / 4.0); break;
        case laterId:   n.beat = wrapBeat (n.beat + kStep, len); bar = (int) std::floor (n.beat / 4.0); break;
        case shorterId: n.length = n.length > kStep + 1.0e-6 ? std::ceil (n.length / kStep - 1.0e-6) * kStep - kStep : juce::jmax (0.0625, n.length * 0.5); break;
        case longerId:  n.length = juce::jmin (len, std::floor (n.length / kStep + 1.0e-6) * kStep + kStep); break;
        case deleteId:  pat.notes.erase (pat.notes.begin() + pickedNote); pickedNote = -1; break;
        default: return;
    }
    changed (before);
}

//==============================================================================
float FixNotesSheet::stepX (double s) const { return grid.getX() + (float) (s / 16.0) * grid.getWidth(); }
float FixNotesSheet::rowY (int note) const { return grid.getY() + (float) (lowNote + kRows - 1 - note) * grid.getHeight() / (float) kRows; }
int FixNotesSheet::rowNote (float y) const
{
    const int row = juce::jlimit (0, kRows - 1, (int) std::floor ((y - grid.getY()) / (grid.getHeight() / (float) kRows)));
    return lowNote + kRows - 1 - row;
}
double FixNotesSheet::stepAt (float x) const { return juce::jlimit (0.0, 15.999, (double) ((x - grid.getX()) / grid.getWidth() * 16.0f)); }

int FixNotesSheet::noteAt (juce::Point<float> p) const
{
    if (! grid.contains (p))
        return -1;
    const double beat = bar * 4.0 + stepAt (p.x) * kStep;
    const int note = rowNote (p.y);
    // the note in that row under the finger, else the nearest one starting within a step of it
    int best = -1;
    double bestD = 1.0e9;
    for (int i = 0; i < (int) pat.notes.size(); ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        if (n.note != note)
            continue;
        const double end = n.beat + juce::jmax (kStep, n.length);
        const double d = beat < n.beat ? n.beat - beat : beat > end ? beat - end : 0.0;
        if (d < bestD && d <= kStep * 0.6)
        {
            best = i;
            bestD = d;
        }
    }
    return best;
}

void FixNotesSheet::mouseDown (const juce::MouseEvent& e)
{
    downAt = e.position;
    drag = Drag::none;
    if (! grid.contains (e.position))
        return;
    const int n = noteAt (e.position);
    if (n >= 0)
    {
        pickedNote = n;
        drag = Drag::note;
        dragBefore = pat;
        dragSteps = dragSemis = 0;
        refresh();
    }
    else
        drag = Drag::tap;
}

void FixNotesSheet::mouseDrag (const juce::MouseEvent& e)
{
    const auto d = e.position - downAt;
    const float rowH = grid.getHeight() / (float) kRows, stepW = grid.getWidth() / 16.0f;
    if (drag == Drag::tap && std::abs (d.y) > 10.0f)
    {
        drag = Drag::scroll;
        scrollFrom = lowNote;
    }
    if (drag == Drag::scroll)
    {
        lowNote = juce::jlimit (0, 127 - kRows + 1, scrollFrom + (int) std::round (d.y / rowH));
        repaint();
    }
    else if (drag == Drag::note && pickedNote >= 0 && pickedNote < (int) dragBefore.notes.size())
    {
        const int steps = (int) std::round (d.x / stepW), semis = (int) std::round (-d.y / rowH);
        if (steps == dragSteps && semis == dragSemis)
            return;
        dragSteps = steps;
        dragSemis = semis;
        pat = dragBefore;
        auto& n = pat.notes[(size_t) pickedNote];
        n.beat = wrapBeat (n.beat + steps * kStep, pat.lengthBeats());
        n.note = juce::jlimit (0, 127, n.note + semis);
        keepInView (n.note);
        refresh();
    }
}

void FixNotesSheet::mouseUp (const juce::MouseEvent& e)
{
    const auto was = std::exchange (drag, Drag::none);
    if (was == Drag::note && (dragSteps != 0 || dragSemis != 0))
    {
        const auto& n = pat.notes[(size_t) pickedNote];
        bar = juce::jlimit (0, pat.bars - 1, (int) std::floor (n.beat / 4.0));
        changed (dragBefore);
    }
    else if (was == Drag::tap && grid.contains (e.position))
        tapCell (bar * 16 + (int) std::floor (stepAt (e.position.x)), rowNote (e.position.y));
}

void FixNotesSheet::timerCallback()
{
    const double b = playBeat ? playBeat() : -1.0;
    if (std::abs (b - playShown) > 1.0e-3)
    {
        playShown = b;
        repaint (grid.expanded (2).toNearestInt());
    }
}

//==============================================================================
juce::String FixNotesSheet::pickedText() const
{
    if (pickedNote < 0 || pickedNote >= (int) pat.notes.size())
        return pat.notes.empty() ? juce::String ("No notes. Tap a spot on the grid to add one.")
                                 : juce::String ("Tap a note to pick it, drag it to move it. Tap an empty spot to add one.");
    const auto& n = pat.notes[(size_t) pickedNote];
    const int step = (int) std::floor (wrapBeat (n.beat, 4.0) / kStep + 1.0e-6) + 1;
    return SongKey::noteName (n.note) + (key.inScale (n.note) ? juce::String() : juce::String (" (not in ") + key.label().toLowerCase() + ")")
           + U8(" \xc2\xb7 bar ") + juce::String ((int) std::floor (n.beat / 4.0) + 1) + ", step " + juce::String (step)
           + U8(" \xc2\xb7 ") + lengthName (n.length) + " long";
}

void FixNotesSheet::refresh()
{
    const bool on = pickedNote >= 0 && pickedNote < (int) pat.notes.size();
    for (auto* b : { &up, &down, &earlier, &later, &shorter, &longer, &remove })
        b->setLook (! on ? Look::ghost : b == &remove ? Look::red : Look::cream);
    undo.setLook (undos.empty() ? Look::ghost : Look::cream);
    for (int i = 0; i < (int) bars.size(); ++i)
    {
        int count = 0;
        for (auto& n : pat.notes)
            count += (int) std::floor (n.beat / 4.0) == i ? 1 : 0;
        bars[(size_t) i]->setLook (i == bar ? Look::amber : Look::cream);
        bars[(size_t) i]->setDetail (count > 0 ? juce::String (count) : juce::String());
    }
    repaint();
}

void FixNotesSheet::resized()
{
    const int w = getWidth(), inner = w - 2 * kSide;
    done.setBounds (w - kSide - 84, 12, 84, 40);
    infoRect = { (float) kSide, 58.0f, (float) inner, 20.0f };
    barRow = { (float) kSide, 84.0f, (float) inner, 34.0f };
    const int n = (int) bars.size();
    if (n > 1)
    {
        const float each = (barRow.getWidth() - (float) (n - 1) * 4.0f) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            bars[(size_t) i]->setVisible (true);
            bars[(size_t) i]->setBounds (juce::Rectangle<float> (barRow.getX() + (float) i * (each + 4.0f), barRow.getY(), each, barRow.getHeight()).toNearestInt());
        }
    }
    else
        for (auto& b : bars)
            b->setVisible (false);
    const float top = 124.0f, gh = 300.0f;
    labels = { (float) kSide, top, kLabelW, gh };
    grid = { (float) kSide + kLabelW + 4.0f, top, (float) inner - kLabelW - 4.0f, gh };
    int y = (int) (top + gh) + 12;
    const int bw = (inner - 3 * kGap) / 4;
    int x = kSide;
    for (auto* b : { &up, &down, &earlier, &later })
    {
        b->setBounds (x, y, bw, kButtonH);
        x += bw + kGap;
    }
    y += kButtonH + kGap;
    x = kSide;
    for (auto* b : { &shorter, &longer, &remove, &undo })
    {
        b->setBounds (x, y, bw, kButtonH);
        x += bw + kGap;
    }
}

void FixNotesSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (26.0f));
    g.drawText ("FIX NOTES", kSide, 16, 140, 36, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensedBold (14.0f)));
    g.drawText (what + U8(" \xc2\xb7 LAYER ") + juce::String (shownLayer + 1) + U8(" \xc2\xb7 ") + key.label(),
                kSide + 130, 16, getWidth() - 2 * kSide - 130 - 90, 36, juce::Justification::centredLeft);
    g.setColour (pickedNote >= 0 ? col::lcdAmber : col::inkDim);
    g.setFont (small (14.0f));
    g.drawFittedText (pickedText(), infoRect.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    if (bars.size() <= 1)
    {
        g.setColour (col::inkFaint);
        g.setFont (spaced (condensedBold (13.0f)));
        g.drawText (U8("1 BAR \xc2\xb7 16 STEPS \xc2\xb7 DRAG UP OR DOWN ON AN EMPTY SPOT FOR HIGHER OR LOWER"), barRow, juce::Justification::centredLeft);
    }

    // the grid: a row a semitone (the key's notes lit, the root brighter), a line a step (the beats stronger)
    drawWell (g, grid.expanded (3.0f), 10.0f);
    const float rowH = grid.getHeight() / (float) kRows;
    for (int r = 0; r < kRows; ++r)
    {
        const int note = lowNote + kRows - 1 - r;
        const auto row = juce::Rectangle<float> (grid.getX(), grid.getY() + (float) r * rowH, grid.getWidth(), rowH);
        const bool inKey = key.inScale (note), root = ((note - key.root) % 12 + 12) % 12 == 0;
        if (inKey)
        {
            g.setColour (root ? col::amber.withAlpha (0.12f) : col::creamLight.withAlpha (0.55f));
            g.fillRect (row.reduced (0.0f, 0.5f));
        }
        g.setColour (inKey ? (root ? col::amber : col::inkDim) : col::inkFaint.withAlpha (0.6f));
        g.setFont (condensedBold (juce::jmin (14.0f, rowH - 2.0f)));
        g.drawText (SongKey::noteName (note), labels.withY (row.getY()).withHeight (rowH), juce::Justification::centredRight);
    }
    for (int s = 0; s <= 16; ++s)
    {
        g.setColour (s % 4 == 0 ? col::inkFaint.withAlpha (0.7f) : col::rule.withAlpha (0.7f));
        const float x = stepX (s);
        g.drawLine (x, grid.getY(), x, grid.getBottom(), s % 4 == 0 ? 1.2f : 0.6f);
    }

    // the notes in the bar shown (a note out of view shows as a tick on the edge it's past)
    const double from = bar * 4.0, to = from + 4.0;
    for (int i = 0; i < (int) pat.notes.size(); ++i)
    {
        const auto& n = pat.notes[(size_t) i];
        const double end = n.beat + n.length;
        if (end <= from || n.beat >= to)
            continue;
        const float x0 = stepX ((juce::jmax (n.beat, from) - from) / kStep), x1 = stepX ((juce::jmin (end, to) - from) / kStep);
        const bool sel = i == pickedNote;
        if (n.note < lowNote || n.note > lowNote + kRows - 1)
        {
            const float y = n.note < lowNote ? grid.getBottom() - 4.0f : grid.getY();
            g.setColour ((sel ? col::green : col::amber).withAlpha (0.8f));
            g.fillRect (x0 + 1.0f, y, juce::jmax (4.0f, x1 - x0 - 2.0f), 4.0f);
            continue;
        }
        auto r = juce::Rectangle<float> (x0 + 1.0f, rowY (n.note) + 1.5f, juce::jmax (6.0f, x1 - x0 - 2.0f), rowH - 3.0f);
        g.setColour (sel ? col::green : col::amber.withAlpha (0.55f + 0.45f * juce::jlimit (0.0f, 1.0f, n.velocity)));
        g.fillRoundedRectangle (r, 3.0f);
        if (sel)
        {
            g.setColour (col::ink);
            g.drawRoundedRectangle (r.expanded (1.5f), 4.0f, 2.0f);
        }
    }

    // where the layer plays now
    if (playShown >= 0.0)
    {
        const double at = std::fmod (playShown, pat.lengthBeats());
        if (at >= from && at < to)
        {
            const float x = stepX ((at - from) / kStep);
            g.setColour (col::green);
            g.drawLine (x, grid.getY() - 2.0f, x, grid.getBottom() + 2.0f, 2.0f);
        }
    }
}
