#include "NoteEditor.h"
#include "RigColours.h"
#include "RigSong.h"
#include "Vintage.h"

#include <cmath>

namespace
{
const char* snapNames[] = { "Snap to bars", "Snap to beats", "Snap to 8ths", "Snap to 16ths", "Snap to 32nds", "Snap to 16th triplets", "Free (no snap)" };
constexpr int kSnapChoices = 7;
} // namespace

double NoteEditor::snapBeats (int choice, int bpb)
{
    switch (choice)
    {
        case 0: return juce::jmax (1, bpb);
        case 1: return 1.0;
        case 2: return 0.5;
        case 3: return 0.25;
        case 4: return 0.125;
        case 5: return 1.0 / 6.0;
        default: return 0.0;
    }
}

//==============================================================================
class NoteEditor::Grid : public juce::Component
{
public:
    explicit Grid (NoteEditor& e) : owner (e) {}

    static constexpr int keysWidth = 46, velocityHeight = 56, rows = 30;
    int lowNote = 48;   // the bottom row's note

    void fitToNotes()
    {
        const auto& notes = owner.getNotes();
        if (notes.empty())
        {
            lowNote = owner.strip == RigEngine::mpcStrip || owner.strip == RigEngine::drumsStrip ? 35 : 45;
            return;
        }
        int lo = 127, hi = 0;
        for (auto& n : notes)
        {
            lo = juce::jmin (lo, n.note);
            hi = juce::jmax (hi, n.note);
        }
        lowNote = juce::jlimit (0, 127 - rows + 1, (lo + hi) / 2 - rows / 2);
    }

    juce::Rectangle<int> notesArea() const { return getLocalBounds().withTrimmedLeft (keysWidth).withTrimmedBottom (velocityHeight + 4); }
    juce::Rectangle<int> velocityArea() const { return getLocalBounds().withTrimmedLeft (keysWidth).removeFromBottom (velocityHeight); }
    double beats() const
    {
        const auto* c = owner.clip();
        return c != nullptr ? c->bars * juce::jmax (1, owner.engine.getTransport().getBeatsPerBar()) : 4.0;
    }
    float rowHeight() const { return notesArea().getHeight() / (float) rows; }
    float xFor (double beat) const { const auto a = notesArea(); return (float) a.getX() + (float) (beat / beats()) * (float) a.getWidth(); }
    double beatAt (float x) const { const auto a = notesArea(); return juce::jlimit (0.0, beats(), (x - a.getX()) / (double) juce::jmax (1, a.getWidth()) * beats()); }
    float yFor (int note) const { return (float) notesArea().getBottom() - (note - lowNote + 1) * rowHeight(); }
    int noteAt (float y) const { return juce::jlimit (0, 127, lowNote + (int) std::floor ((notesArea().getBottom() - y) / rowHeight())); }
    juce::Rectangle<float> rectFor (const rignotes::Note& n) const
    {
        const float x0 = xFor (n.beat), x1 = xFor (n.beat + n.length);
        return { x0, yFor (n.note) + 1.0f, juce::jmax (3.0f, x1 - x0), rowHeight() - 2.0f };
    }
    double snap() const { return snapBeats (owner.snap.getSelectedItemIndex(), owner.engine.getTransport().getBeatsPerBar()); }
    double snapped (double b) const { const double s = snap(); return s > 0 ? std::round (b / s) * s : b; }
    double snappedDown (double b) const { const double s = snap(); return s > 0 ? std::floor (b / s + 1.0e-6) * s : b; }

    int hit (juce::Point<float> p, bool& onEnd) const
    {
        const auto& notes = owner.getNotes();
        for (int i = (int) notes.size(); --i >= 0;)
        {
            const auto r = rectFor (notes[(size_t) i]);
            if (r.expanded (0, 0.5f).contains (p))
            {
                onEnd = p.x > r.getRight() - juce::jmin (8.0f, r.getWidth() * 0.35f);
                return i;
            }
        }
        onEnd = false;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = vintage::pal();
        const auto area = notesArea();
        const int bpb = juce::jmax (1, owner.engine.getTransport().getBeatsPerBar());
        const auto colour = RigColours::forStrip (owner.strip);
        g.setColour (p.well);
        g.fillRect (getLocalBounds());

        // Rows: the black keys a shade darker, every C marked.
        for (int r = 0; r < rows; ++r)
        {
            const int note = lowNote + r;
            const float y = yFor (note);
            const bool black = juce::MidiMessage::isMidiNoteBlack (note);
            g.setColour (black ? p.well.darker (0.25f) : p.well.brighter (0.06f));
            g.fillRect ((float) area.getX(), y, (float) area.getWidth(), rowHeight());
            // The key.
            g.setColour (black ? juce::Colour (0xff2a2520) : juce::Colour (0xffe9e1cf));
            g.fillRect (0.0f, y, (float) keysWidth - 2.0f, rowHeight() - 1.0f);
            if (note % 12 == 0 || rowHeight() >= 13.0f)
            {
                g.setColour (black ? juce::Colours::white.withAlpha (0.7f) : juce::Colour (0xff3a3128));
                g.setFont (vintage::cond (juce::jmin (12.0f, rowHeight()), vintage::Weight::semi));
                g.drawText (rignotes::noteName (note), juce::Rectangle<float> (2.0f, y, keysWidth - 6.0f, rowHeight()), juce::Justification::centredRight);
            }
            if (note % 12 == 0)
            {
                g.setColour (p.wellInk2.withAlpha (0.35f));
                g.drawHorizontalLine ((int) (y + rowHeight()), (float) area.getX(), (float) area.getRight());
            }
        }
        // Bars, beats and the snap.
        const double total = beats();
        const double s = snap();
        for (double b = 0; b <= total + 1.0e-9; b += (s > 0 && s < 1.0 ? s : 1.0))
        {
            const bool bar = std::abs (std::fmod (b, bpb)) < 1.0e-6;
            const bool beat = std::abs (b - std::round (b)) < 1.0e-6;
            g.setColour (p.wellInk2.withAlpha (bar ? 0.6f : beat ? 0.3f : 0.12f));
            g.drawVerticalLine ((int) xFor (b), (float) area.getY(), (float) getHeight());
            if (bar && b < total - 1.0e-6)
            {
                g.setColour (p.wellInk);
                g.setFont (vintage::cond (12.0f));
                const auto* c = owner.clip();
                g.drawText ("bar " + juce::String ((int) ((c != nullptr ? c->startBar : 0) + b / bpb) + 1),
                            juce::Rectangle<float> (xFor (b) + 3.0f, (float) area.getY(), 60.0f, 14.0f), juce::Justification::centredLeft);
            }
        }

        // The notes.
        const auto& notes = owner.getNotes();
        const auto vel = velocityArea();
        g.setColour (p.wellInk2.withAlpha (0.25f));
        g.drawHorizontalLine (vel.getY() - 2, (float) vel.getX(), (float) vel.getRight());
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const auto& n = notes[(size_t) i];
            const bool picked = owner.selected.count (i) > 0;
            if (n.note >= lowNote && n.note < lowNote + rows)
            {
                const auto r = rectFor (n);
                g.setColour (colour.withMultipliedBrightness (0.55f + 0.45f * n.velocity));
                g.fillRoundedRectangle (r, 2.5f);
                g.setColour (picked ? vintage::colours::selected : juce::Colours::black.withAlpha (0.5f));
                g.drawRoundedRectangle (r, 2.5f, picked ? 2.0f : 1.0f);
            }
            // Its hit, along the bottom.
            const float x = xFor (n.beat);
            const float h = n.velocity * (vel.getHeight() - 4);
            g.setColour (picked ? vintage::colours::selected : colour);
            g.fillRect (x, (float) vel.getBottom() - h, 3.0f, h);
        }
        // Notes out of sight (above or below).
        int above = 0, below = 0;
        for (auto& n : notes)
        {
            above += n.note >= lowNote + rows ? 1 : 0;
            below += n.note < lowNote ? 1 : 0;
        }
        g.setColour (vintage::colours::next);
        g.setFont (vintage::cond (12.0f));
        if (above > 0)
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb2 ") + juce::String (above) + " higher (scroll)", notesArea().removeFromTop (14).toFloat(), juce::Justification::centredRight);
        if (below > 0)
            g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbc ") + juce::String (below) + " lower (scroll)", notesArea().removeFromBottom (14).toFloat(), juce::Justification::centredRight);

        // The rubber band.
        if (! band.isEmpty())
        {
            g.setColour (vintage::colours::selected.withAlpha (0.15f));
            g.fillRect (band);
            g.setColour (vintage::colours::selected);
            g.drawRect (band, 1.0f);
        }
        // Where the song is.
        const auto* c = owner.clip();
        auto& clock = owner.engine.getTransport();
        if (c != nullptr && clock.isPlaying())
        {
            const double at = clock.getPositionInBeats() - c->startBar * bpb;
            if (at >= 0 && at <= total)
            {
                g.setColour (vintage::colours::play);
                g.drawVerticalLine ((int) xFor (at), (float) area.getY(), (float) getHeight());
            }
        }
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        lowNote = juce::jlimit (0, 127 - rows + 1, lowNote + (w.deltaY > 0 ? 2 : w.deltaY < 0 ? -2 : 0));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const auto pos = e.position;
        dragMode = none;
        if (velocityArea().contains (e.getPosition()))
        {
            // The hit bar nearest the click.
            const auto& notes = owner.getNotes();
            int best = -1;
            float bestDx = 8.0f;
            for (int i = 0; i < (int) notes.size(); ++i)
                if (const float dx = std::abs (xFor (notes[(size_t) i].beat) + 1.5f - pos.x); dx < bestDx)
                {
                    bestDx = dx;
                    best = i;
                }
            if (best >= 0)
            {
                if (owner.selected.count (best) == 0)
                    owner.selected = { best };
                owner.undos.push_back (notes);
                dragMode = velocity;
                setVelocityFrom (pos.y);
            }
            return;
        }
        bool onEnd = false;
        const int i = hit (pos, onEnd);
        if (i < 0)
        {
            if (! e.mods.isShiftDown())
                owner.selected.clear();
            dragMode = banding;
            bandStart = pos;
            band = {};
            repaint();
            owner.updateText();
            return;
        }
        if (e.mods.isShiftDown())
        {
            if (owner.selected.count (i) > 0)
                owner.selected.erase (i);
            else
                owner.selected.insert (i);
        }
        else if (owner.selected.count (i) == 0)
            owner.selected = { i };
        startNotes = owner.getNotes();
        owner.undos.push_back (startNotes);
        downBeat = beatAt (pos.x);
        downNote = noteAt (pos.y);
        grabbed = i;
        dragMode = onEnd ? resizing : moving;
        moved = false;
        repaint();
        owner.updateText();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto* c = owner.clip();
        if (c == nullptr)
            return;
        const auto pos = e.position;
        if (dragMode == velocity)
        {
            setVelocityFrom (pos.y);
            return;
        }
        if (dragMode == banding)
        {
            band = juce::Rectangle<float> (bandStart, pos);
            owner.selected.clear();
            const auto& notes = owner.getNotes();
            for (int i = 0; i < (int) notes.size(); ++i)
                if (band.intersects (rectFor (notes[(size_t) i])))
                    owner.selected.insert (i);
            repaint();
            return;
        }
        if (dragMode != moving && dragMode != resizing)
            return;
        moved = true;
        const double total = beats();
        auto& notes = c->notes;
        notes = startNotes;
        if (dragMode == moving)
        {
            // The grabbed note's start goes onto the snap; the others keep their places from it.
            const auto& g0 = startNotes[(size_t) grabbed];
            const double wanted = g0.beat + (beatAt (pos.x) - downBeat);
            const double delta = snapped (wanted) - g0.beat;
            const int dNote = noteAt (pos.y) - downNote;
            for (int i : owner.selected)
            {
                auto& n = notes[(size_t) i];
                n.beat = juce::jlimit (0.0, juce::jmax (0.0, total - 1.0e-3), n.beat + delta);
                n.note = juce::jlimit (0, 127, n.note + dNote);
            }
        }
        else
        {
            const double s = snap() > 0 ? snap() : 1.0 / 64.0;
            const auto& g0 = startNotes[(size_t) grabbed];
            const double newEnd = juce::jmax (g0.beat + s, snapped (beatAt (pos.x)));
            const double delta = newEnd - (g0.beat + g0.length);
            for (int i : owner.selected)
            {
                auto& n = notes[(size_t) i];
                n.length = juce::jlimit (s * 0.5, juce::jmax (s * 0.5, total - n.beat), n.length + delta);
            }
        }
        owner.engine.notesChanged();   // (heard straight away; the indexes stay till let go)
        repaint();
        owner.updateText();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if ((dragMode == moving || dragMode == resizing) && ! moved && ! owner.undos.empty())
            owner.undos.pop_back();   // a click, not a change
        if (dragMode == moving || dragMode == resizing || dragMode == velocity)
            owner.changed();
        dragMode = none;
        band = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (velocityArea().contains (e.getPosition()))
            return;
        bool onEnd = false;
        const int i = hit (e.position, onEnd);
        if (i >= 0)
        {
            owner.selected = { i };
            owner.deleteSelected();
            return;
        }
        const double s = snap() > 0 ? snap() : 0.25;
        owner.addNote (snappedDown (beatAt (e.position.x)), noteAt (e.position.y), juce::jmin (s, 1.0));
    }

private:
    void setVelocityFrom (float y)
    {
        auto* c = owner.clip();
        if (c == nullptr)
            return;
        const auto vel = velocityArea();
        const float v = juce::jlimit (0.05f, 1.0f, (vel.getBottom() - y) / (float) juce::jmax (1, vel.getHeight() - 4));
        for (int i : owner.selected)
            if (juce::isPositiveAndBelow (i, (int) c->notes.size()))
                c->notes[(size_t) i].velocity = v;
        owner.engine.notesChanged();
        repaint();
        owner.updateText();
    }

    NoteEditor& owner;
    enum Mode { none, moving, resizing, banding, velocity } dragMode = none;
    std::vector<rignotes::Note> startNotes;
    double downBeat = 0;
    int downNote = 60, grabbed = -1;
    bool moved = false;
    juce::Point<float> bandStart;
    juce::Rectangle<float> band;
};

//==============================================================================
NoteEditor::NoteEditor (RigEngine& e, int s, double bar) : engine (e), strip (s), startBar (bar)
{
    grid = std::make_unique<Grid> (*this);
    addAndMakeVisible (*grid);
    grid->setWantsKeyboardFocus (false);
    grid->fitToNotes();
    setWantsKeyboardFocus (true);

    for (auto* l : { &title, &info, &hint })
        addAndMakeVisible (*l);
    title.setFont (vintage::cond (20.0f, vintage::Weight::extra, 0.04f));
    info.setFont (vintage::barlow (14.0f));
    hint.setFont (vintage::barlow (13.0f));
    hint.setText ("Drag a note to move it, its right end to make it longer; double-click to add or delete; drag the bars at the bottom "
                  "for how hard each note is hit. Arrows move the picked notes (Shift: an octave). It plays as you change it.",
                  juce::dontSendNotification);
    for (int i = 0; i < kSnapChoices; ++i)
        snap.addItem (snapNames[i], i + 1);
    snap.setSelectedItemIndex (3, juce::dontSendNotification);
    snap.onChange = [this] { grid->repaint(); };
    snap.setTooltip ("What notes stick to as you move, stretch or add them.");
    addAndMakeVisible (snap);

    quantize.onClick = [this] { showQuantizeMenu(); };
    quantize.setTooltip ("Moves the picked notes (all, if none is picked) onto the beat.");
    down12.onClick = [this] { transposeSelected (-12); };
    down1.onClick = [this] { transposeSelected (-1); };
    up1.onClick = [this] { transposeSelected (1); };
    up12.onClick = [this] { transposeSelected (12); };
    for (auto* b : { &down12, &down1, &up1, &up12 })
        b->setTooltip ("Moves the picked notes (all, if none is picked) up or down: by a semitone, or an octave.");
    all.onClick = [this] { selectAll(); };
    remove.onClick = [this] { deleteSelected(); };
    remove.setTooltip ("Deletes the picked notes.");
    undoButton.onClick = [this] { undo(); };
    play.onClick = [this]
    {
        auto& clock = engine.getTransport();
        if (clock.isPlaying())
            clock.togglePlay();
        else
        {
            const auto list = rigsong::sections (engine.getTimeline());
            for (int i = 0; i < (int) list.size(); ++i)
                if (std::abs (list[(size_t) i].start - startBar) < 1.0e-6)
                {
                    engine.cycleSection (i, true);
                    engine.launchSection (i);
                    return;
                }
            clock.togglePlay();
        }
    };
    play.setTooltip ("Plays the song round this section (or stops it), to hear the notes as you change them.");
    done.onClick = [this]
    {
        if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
            w->closeButtonPressed();
    };
    for (auto* b : { &quantize, &down12, &down1, &up1, &up12, &all, &remove, &undoButton, &play, &done })
    {
        addAndMakeVisible (*b);
        vintage::setInWell (*b, true);
        b->getProperties().set ("fontSize", 15.0f);
    }
    vintage::setLit (play, vintage::Lit::play);
    updateText();
    setSize (980, 560);
    startTimerHz (20);
}

NoteEditor::~NoteEditor() = default;

rignotes::Clip* NoteEditor::clip()
{
    const int c = engine.noteClipAt (strip, startBar);
    return c >= 0 ? &engine.getNoteClips()[(size_t) c] : nullptr;
}

const rignotes::Clip* NoteEditor::clip() const
{
    return const_cast<NoteEditor*> (this)->clip();
}

bool NoteEditor::isValid() const { return clip() != nullptr; }

const std::vector<rignotes::Note>& NoteEditor::getNotes() const
{
    static const std::vector<rignotes::Note> none;
    const auto* c = clip();
    return c != nullptr ? c->notes : none;
}

void NoteEditor::edit (std::function<void (std::vector<rignotes::Note>&)> change)
{
    auto* c = clip();
    if (c == nullptr)
        return;
    undos.push_back (c->notes);
    if (undos.size() > 100)
        undos.erase (undos.begin());
    change (c->notes);
    changed();
}

void NoteEditor::changed()
{
    // Back in order of time, keeping which ones are picked.
    if (auto* c = clip())
    {
        std::vector<std::pair<rignotes::Note, bool>> tagged;
        for (int i = 0; i < (int) c->notes.size(); ++i)
            tagged.push_back ({ c->notes[(size_t) i], selected.count (i) > 0 });
        std::stable_sort (tagged.begin(), tagged.end(), [] (const auto& a, const auto& b) { return a.first.beat < b.first.beat; });
        selected.clear();
        c->notes.clear();
        for (int i = 0; i < (int) tagged.size(); ++i)
        {
            c->notes.push_back (tagged[(size_t) i].first);
            if (tagged[(size_t) i].second)
                selected.insert (i);
        }
    }
    engine.notesChanged();
    grid->repaint();
    updateText();
}

void NoteEditor::selectAll()
{
    selected.clear();
    for (int i = 0; i < (int) getNotes().size(); ++i)
        selected.insert (i);
    grid->repaint();
    updateText();
}

void NoteEditor::quantizeSelected (double gridBeats, float strength)
{
    const auto picked = selected;
    edit ([&] (std::vector<rignotes::Note>& notes)
    {
        for (int i = 0; i < (int) notes.size(); ++i)
            if (picked.empty() || picked.count (i) > 0)
            {
                std::vector<rignotes::Note> one { notes[(size_t) i] };
                rignotes::quantize (one, gridBeats, strength);
                notes[(size_t) i] = one[0];
            }
    });
}

void NoteEditor::transposeSelected (int semis)
{
    const auto picked = selected;
    edit ([&] (std::vector<rignotes::Note>& notes)
    {
        for (int i = 0; i < (int) notes.size(); ++i)
            if (picked.empty() || picked.count (i) > 0)
                notes[(size_t) i].note = juce::jlimit (0, 127, notes[(size_t) i].note + semis);
    });
}

void NoteEditor::deleteSelected()
{
    if (selected.empty())
        return;
    const auto picked = selected;
    selected.clear();
    edit ([&] (std::vector<rignotes::Note>& notes)
    {
        std::vector<rignotes::Note> kept;
        for (int i = 0; i < (int) notes.size(); ++i)
            if (picked.count (i) == 0)
                kept.push_back (notes[(size_t) i]);
        notes = std::move (kept);
    });
}

void NoteEditor::addNote (double beat, int note, double length)
{
    const auto* c = clip();
    if (c == nullptr)
        return;
    const double total = c->bars * juce::jmax (1, engine.getTransport().getBeatsPerBar());
    rignotes::Note n;
    n.beat = juce::jlimit (0.0, juce::jmax (0.0, total - 1.0e-3), beat);
    n.length = juce::jlimit (1.0e-3, juce::jmax (1.0e-3, total - n.beat), length);
    n.note = juce::jlimit (0, 127, note);
    n.velocity = 0.8f;
    // The channel the others are on (the pads' channel for drums).
    n.channel = c->notes.empty() ? (strip == RigEngine::mpcStrip || strip == RigEngine::drumsStrip ? 10 : 1) : c->notes.front().channel;
    selected.clear();
    edit ([&] (std::vector<rignotes::Note>& notes)
    {
        notes.push_back (n);
        selected.insert ((int) notes.size() - 1);
    });
}

void NoteEditor::undo()
{
    auto* c = clip();
    if (c == nullptr || undos.empty())
        return;
    c->notes = undos.back();
    undos.pop_back();
    selected.clear();
    changed();
}

void NoteEditor::showQuantizeMenu()
{
    juce::PopupMenu menu;
    const int bpb = engine.getTransport().getBeatsPerBar();
    const std::pair<int, const char*> grids[] = { { 1, "Beats" }, { 2, "8ths" }, { 3, "16ths" }, { 4, "32nds" }, { 5, "16th triplets" } };
    menu.addSectionHeader (selected.empty() ? "Quantize all the notes to" : "Quantize the picked notes to");
    for (auto [choice, name] : grids)
    {
        juce::PopupMenu sub;
        const double g = snapBeats (choice, bpb);
        for (int pct : { 100, 75, 50, 25 })
            sub.addItem (juce::String (pct) + "% of the way" + (pct == 100 ? " (right on it)" : ""),
                         [this, g, pct] { quantizeSelected (g, pct / 100.0f); });
        menu.addSubMenu (name, sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (quantize));
}

void NoteEditor::updateText()
{
    const auto* c = clip();
    const auto list = rigsong::sections (engine.getTimeline());
    juce::String where;
    for (int i = 0; i < (int) list.size(); ++i)
        if (c != nullptr && std::abs (list[(size_t) i].start - c->startBar) < 1.0e-6)
            where = rigsong::letterFor (i) + " " + list[(size_t) i].name;
    juce::String tool = strip == RigEngine::bassStrip ? "Bass" : strip == RigEngine::synthStrip ? "Synth" : strip == RigEngine::samplerStrip ? "Sampler"
                      : strip == RigEngine::drumsStrip ? "Drum machine" : strip == RigEngine::mpcStrip ? "MPC" : "Notes";
    title.setText (tool + " notes" + (where.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + where : juce::String()), juce::dontSendNotification);
    title.setColour (juce::Label::textColourId, RigColours::forStrip (strip));
    juce::String text;
    if (c == nullptr)
        text = "These notes are gone (a new song?).";
    else
    {
        text << (int) c->notes.size() << " notes over " << rigsong::barsText (c->bars);
        if (! selected.empty())
        {
            text << juce::String::fromUTF8 (" \xc2\xb7 ") << (int) selected.size() << " picked";
            if (selected.size() == 1)
            {
                const auto& n = c->notes[(size_t) *selected.begin()];
                text << ": " << rignotes::noteName (n.note) << ", hit " << (int) std::round (n.velocity * 127) << " of 127";
            }
        }
    }
    info.setText (text, juce::dontSendNotification);
    undoButton.setEnabled (! undos.empty());
    remove.setEnabled (! selected.empty());
}

void NoteEditor::timerCallback()
{
    play.setButtonText (engine.getTransport().isPlaying() ? "Stop" : "Play");
    if (engine.getTransport().isPlaying())
        grid->repaint();
}

bool NoteEditor::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();
    const bool shift = key.getModifiers().isShiftDown();
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        deleteSelected();
        return true;
    }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undo();
        return true;
    }
    if (key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectAll();
        return true;
    }
    if (selected.empty())
        return false;
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
    {
        transposeSelected ((code == juce::KeyPress::upKey ? 1 : -1) * (shift ? 12 : 1));
        return true;
    }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const double s = snapBeats (snap.getSelectedItemIndex(), engine.getTransport().getBeatsPerBar());
        const double step = (s > 0 ? s : 1.0 / 64.0) * (code == juce::KeyPress::rightKey ? 1 : -1);
        const auto picked = selected;
        const auto* c = clip();
        const double total = c != nullptr ? c->bars * juce::jmax (1, engine.getTransport().getBeatsPerBar()) : 0;
        edit ([&] (std::vector<rignotes::Note>& notes)
        {
            for (int i : picked)
                notes[(size_t) i].beat = juce::jlimit (0.0, juce::jmax (0.0, total - 1.0e-3), notes[(size_t) i].beat + step);
        });
        return true;
    }
    return false;
}

void NoteEditor::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
}

void NoteEditor::resized()
{
    auto area = getLocalBounds().reduced (14, 10);
    auto top = area.removeFromTop (32);
    done.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    play.setBounds (top.removeFromRight (70));
    top.removeFromRight (12);
    undoButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (6);
    remove.setBounds (top.removeFromRight (70));
    top.removeFromRight (6);
    all.setBounds (top.removeFromRight (76));
    top.removeFromRight (12);
    for (auto* b : { &up12, &up1, &down1, &down12 })
    {
        b->setBounds (top.removeFromRight (40));
        top.removeFromRight (3);
    }
    top.removeFromRight (9);
    quantize.setBounds (top.removeFromRight (96));
    top.removeFromRight (6);
    snap.setBounds (top.removeFromRight (160).reduced (0, 2));
    title.setBounds (top);
    area.removeFromTop (4);
    info.setBounds (area.removeFromTop (22));
    hint.setBounds (area.removeFromBottom (34));
    area.removeFromBottom (4);
    grid->setBounds (area);
}
