#include "AutomationPanel.h"

namespace
{
juce::String barsText (double beats, int bpb)
{
    const double bars = beats / juce::jmax (1, bpb);
    const bool whole = std::abs (bars - std::round (bars)) < 0.01;
    return juce::String (bars, whole ? 0 : 2) + (std::abs (bars - 1.0) < 0.01 ? " bar" : " bars");
}
}

//==============================================================================
// One lane: its setting, On, Loop/Once, length, the moves (draw on them), Clear.
class AutomationPanel::Row : public juce::Component
{
public:
    Row (AutomationPanel& p, int i) : panel (p), index (i)
    {
        onButton.setTooltip ("This lane moves its setting (off: it leaves it where you put it)");
        loopButton.setTooltip ("Loop: the moves go round and round, the lane's length each time, like a loop. "
                               "Once: they play once along the song, from the bar you recorded them on, then the setting stays put");
        shorter.setTooltip ("Halve the lane's length (the moves after the new end are cut)");
        longer.setTooltip ("Double the lane's length (it holds its last value over the new bars)");
        clearButton.setTooltip ("Clear this lane (Undo brings it back)");
        onButton.onClick = [this] { panel.pressLaneOn (index); };
        loopButton.onClick = [this] { panel.pressLaneLoop (index); };
        shorter.onClick = [this] { panel.pressLaneLength (index, false); };
        longer.onClick = [this] { panel.pressLaneLength (index, true); };
        clearButton.onClick = [this] { panel.pressLaneClear (index); };
        vintage::setLit (onButton, vintage::Lit::play);
        vintage::setLit (loopButton, vintage::Lit::current);
        for (auto* b : { &onButton, &loopButton, &shorter, &longer, &clearButton })
            addAndMakeVisible (b);
    }

    void set (const RigAutomation::Lane& l, const juce::String& n, int bpb)
    {
        lane = l;
        name = n;
        beatsPerBar = bpb;
        onButton.setToggleState (lane.on, juce::dontSendNotification);
        loopButton.setButtonText (lane.loop ? "Loop" : "Once");
        loopButton.setToggleState (lane.loop, juce::dontSendNotification);
        shorter.setEnabled (lane.lengthBeats >= 2.0);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto& auto_ = panel.engine.getAutomation();
        auto names = nameBox;
        g.setColour (vintage::pal().ink);
        g.setFont (vintage::cond (16.0f, vintage::Weight::bold));
        g.drawFittedText (name, names.removeFromTop (names.getHeight() / 2), juce::Justification::bottomLeft, 1);
        g.setColour (vintage::pal().ink2);
        g.setFont (vintage::barlow (13.0f));
        juce::String detail = barsText (lane.lengthBeats, beatsPerBar);
        detail << (lane.loop ? ", loops" : ", once from bar " + juce::String ((int) std::floor (lane.startBeat / juce::jmax (1, beatsPerBar)) + 1));
        if (auto_.isTouched (index))
            detail << ", you have it";
        g.drawFittedText (detail, names, juce::Justification::topLeft, 1);

        // The moves.
        auto box = curveArea.toFloat();
        vintage::drawWell (g, box, 8.0f);
        auto inner = box.reduced (6.0f, 6.0f);
        g.setColour (vintage::pal().wellInk2.withAlpha (0.25f));
        const int bars = (int) std::ceil (lane.lengthBeats / juce::jmax (1, beatsPerBar));
        if (bars > 1 && bars <= 64)
            for (int b = 1; b < bars; ++b)
            {
                const float x = inner.getX() + inner.getWidth() * (float) (b * beatsPerBar / lane.lengthBeats);
                g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            }
        if (! lane.points.empty() && lane.lengthBeats > 0)
        {
            juce::Path path;
            const int steps = juce::jmax (2, (int) inner.getWidth());
            for (int s = 0; s <= steps; ++s)
            {
                const double beat = lane.lengthBeats * s / steps;
                const float x = inner.getX() + inner.getWidth() * (float) s / (float) steps;
                const float y = inner.getBottom() - inner.getHeight() * lane.valueAt (beat);
                if (s == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            const auto colour = lane.on ? vintage::colours::lcdAmber : vintage::pal().wellInk2;
            g.setColour (colour);
            g.strokePath (path, juce::PathStrokeType (2.0f));
        }
        const float at = auto_.getLanePosition (index);
        if (at >= 0 && lane.on && auto_.reading.load())
        {
            g.setColour (vintage::colours::play);
            const float x = inner.getX() + inner.getWidth() * at;
            g.drawLine (x, inner.getY(), x, inner.getBottom(), 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (0, 4);
        nameBox = r.removeFromLeft (190);
        r.removeFromLeft (6);
        onButton.setBounds (r.removeFromLeft (46).reduced (0, 12));
        r.removeFromLeft (4);
        loopButton.setBounds (r.removeFromLeft (62).reduced (0, 12));
        r.removeFromLeft (4);
        shorter.setBounds (r.removeFromLeft (32).reduced (0, 12));
        longer.setBounds (r.removeFromLeft (32).reduced (0, 12));
        r.removeFromLeft (8);
        clearButton.setBounds (r.removeFromRight (64).reduced (0, 12));
        r.removeFromRight (8);
        curveArea = r;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! curveArea.contains (e.getPosition()))
            return;
        last = toLane (e.position);
        panel.drawOnLane (index, last.x, last.y, last.x, last.y, true);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! curveArea.contains (e.getMouseDownPosition()))
            return;
        const auto now = toLane (e.position);
        panel.drawOnLane (index, last.x, last.y, now.x, now.y, false);
        last = now;
    }

    juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::CrosshairCursor; }

private:
    juce::Point<float> toLane (juce::Point<float> p) const
    {
        const auto inner = curveArea.toFloat().reduced (6.0f, 6.0f);
        return { juce::jlimit (0.0f, 1.0f, (p.x - inner.getX()) / juce::jmax (1.0f, inner.getWidth())),
                 juce::jlimit (0.0f, 1.0f, (inner.getBottom() - p.y) / juce::jmax (1.0f, inner.getHeight())) };
    }

    AutomationPanel& panel;
    const int index;
    RigAutomation::Lane lane;
    juce::String name;
    int beatsPerBar = 4;
    juce::TextButton onButton { "On" }, loopButton { "Loop" }, shorter { juce::String::fromUTF8 ("\xc3\xb7" "2") }, longer { juce::String::fromUTF8 ("\xc3\x97" "2") },
                     clearButton { "Clear" };
    juce::Rectangle<int> nameBox, curveArea;
    juce::Point<float> last;
};

class AutomationPanel::List : public juce::Component
{
public:
    juce::OwnedArray<Row> rows;
    void resized() override
    {
        int y = 0;
        for (auto* r : rows)
        {
            r->setBounds (0, y, getWidth(), kRowHeight);
            y += kRowHeight;
        }
    }
};

//==============================================================================
AutomationPanel::AutomationPanel (RigEngine& e) : engine (e), list (std::make_unique<List>())
{
    recButton.setTooltip ("From the next bar (now, starting the clock, if it's stopped), records every setting you move, "
                          "on screen or with a knob, on any tool or its effects: a lane each. Press again to stop at the end of the bar.");
    readButton.setTooltip ("Read on: the lanes move their settings. Off: none do (they're kept), so you can play it straight");
    clearAllButton.setTooltip ("Clear every lane (Undo brings them back)");
    undoButton.setTooltip ("Take back the last change to the lanes (a recording, a drawing, a clear)");
    recButton.onClick = [this] { pressRec(); };
    readButton.onClick = [this] { pressRead(); };
    clearAllButton.onClick = [this] { pressClearAll(); };
    undoButton.onClick = [this] { pressUndo(); };
    vintage::setLit (recButton, vintage::Lit::arm);
    vintage::setLit (readButton, vintage::Lit::play);
    for (auto* b : { &recButton, &readButton, &clearAllButton, &undoButton })
        addAndMakeVisible (b);
    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setSize (kWidth, kHeight);
    update();
    startTimerHz (20);
}

AutomationPanel::~AutomationPanel() { stopTimer(); }

int AutomationPanel::getNumRows() const { return list->rows.size(); }

void AutomationPanel::say (const juce::String& text)
{
    status = text;
    repaint (statusArea);
    if (onSay)
        onSay (text);
}

void AutomationPanel::pressRec()
{
    auto& a = engine.getAutomation();
    const auto before = a.getRecState();
    engine.pressRecMoves();
    switch (before)
    {
        case RigAutomation::RecState::off:
            say (engine.songInCharge() ? "Rec moves from the next bar, over the song: turn any knob. Its lane plays once along the song."
                                       : "Rec moves from the next bar: turn any knob (the bass's Cutoff, an effect's Mix). Press again to stop.");
            break;
        case RigAutomation::RecState::armed: say ("Rec moves called off: nothing was recorded."); break;
        case RigAutomation::RecState::recording: say ("Moves close at the end of this bar."); break;
        case RigAutomation::RecState::closing: break;
    }
    update();
}

void AutomationPanel::pressRead()
{
    auto& a = engine.getAutomation();
    a.reading = ! a.reading.load();
    say (a.reading.load() ? "Read on: the lanes move their settings." : "Read off: the lanes are kept, but nothing moves by itself.");
    update();
}

void AutomationPanel::pressClearAll()
{
    auto& a = engine.getAutomation();
    if (a.getNumLanes() == 0)
    {
        say ("There are no moves to clear.");
        return;
    }
    a.clearAll();
    say ("Cleared every lane. Undo brings them back.");
    update();
}

void AutomationPanel::pressUndo()
{
    auto& a = engine.getAutomation();
    if (! a.canUndo())
    {
        say ("Nothing to undo.");
        return;
    }
    a.undo();
    say ("Undone.");
    update();
}

void AutomationPanel::pressLaneOn (int lane)
{
    auto& a = engine.getAutomation();
    const auto lanes = a.getLanes();
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return;
    a.setLaneOn (lane, ! lanes[(size_t) lane].on);
    say (a.laneName (lane) + (lanes[(size_t) lane].on ? ": off, it stays where you put it." : ": on, it moves again."));
    update();
}

void AutomationPanel::pressLaneLoop (int lane)
{
    auto& a = engine.getAutomation();
    const auto lanes = a.getLanes();
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return;
    a.setLaneLoop (lane, ! lanes[(size_t) lane].loop);
    say (a.laneName (lane) + (lanes[(size_t) lane].loop ? ": once along the song, from where you recorded it." : ": loops round and round."));
    update();
}

void AutomationPanel::pressLaneLength (int lane, bool longer)
{
    auto& a = engine.getAutomation();
    const auto lanes = a.getLanes();
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return;
    const double beats = lanes[(size_t) lane].lengthBeats * (longer ? 2.0 : 0.5);
    if (beats < 1.0)
        return;
    a.setLaneLength (lane, beats);
    say (a.laneName (lane) + ": " + barsText (beats, engine.getTransport().getBeatsPerBar()) + ".");
    update();
}

void AutomationPanel::pressLaneClear (int lane)
{
    auto& a = engine.getAutomation();
    const auto name = a.laneName (lane);
    if (name.isEmpty())
        return;
    a.clearLane (lane);
    say ("Cleared " + name + ". Undo brings it back.");
    update();
}

void AutomationPanel::drawOnLane (int lane, float x0, float y0, float x1, float y1, bool newStroke)
{
    auto& a = engine.getAutomation();
    const auto lanes = a.getLanes();
    if (! juce::isPositiveAndBelow (lane, (int) lanes.size()))
        return;
    const double len = lanes[(size_t) lane].lengthBeats;
    a.drawLine (lane, x0 * len, y0, x1 * len, y1, newStroke);
    if (newStroke)
        say ("Drawing on " + a.laneName (lane) + ". Undo takes it back.");
    update();
}

void AutomationPanel::timerCallback()
{
    update();
    for (auto* r : list->rows)
        r->repaint();   // where each lane is playing
}

void AutomationPanel::rebuildRows()
{
    auto& a = engine.getAutomation();
    const auto lanes = a.getLanes();
    while (list->rows.size() > (int) lanes.size())
        list->rows.removeLast();
    while (list->rows.size() < (int) lanes.size())
        list->addAndMakeVisible (list->rows.add (new Row (*this, list->rows.size())));
    const int bpb = engine.getTransport().getBeatsPerBar();
    for (int i = 0; i < (int) lanes.size(); ++i)
        list->rows[i]->set (lanes[(size_t) i], a.laneName (lanes[(size_t) i]), bpb);
    list->setSize (juce::jmax (1, viewport.getWidth() - viewport.getScrollBarThickness()), juce::jmax (1, (int) lanes.size() * kRowHeight));
    list->resized();
}

void AutomationPanel::update()
{
    auto& a = engine.getAutomation();
    if (a.getVersion() != shownVersion)
    {
        shownVersion = a.getVersion();
        rebuildRows();
        repaint (emptyArea);
    }
    if (const int made = a.takeFinishedLanes(); made >= 0)
        say (made == 0 ? juce::String ("Nothing moved, so no lanes were made. Rec moves, then turn a knob while it plays.")
                       : "Recorded " + juce::String (made) + (made == 1 ? " lane" : " lanes") + ". It plays back now; drag on a lane to change it.");
    const auto rec = a.getRecState();
    const int recNow = (int) rec * 100 + a.getNumRecording();
    if (recNow != shownRec)
    {
        shownRec = recNow;
        switch (rec)
        {
            case RigAutomation::RecState::off:       recButton.setButtonText ("Rec moves"); break;
            case RigAutomation::RecState::armed:     recButton.setButtonText ("Next bar..."); break;
            case RigAutomation::RecState::recording: recButton.setButtonText ("Stop moves (" + juce::String (a.getNumRecording()) + ")"); break;
            case RigAutomation::RecState::closing:   recButton.setButtonText ("Closing..."); break;
        }
        recButton.setToggleState (rec != RigAutomation::RecState::off, juce::dontSendNotification);
    }
    readButton.setToggleState (a.reading.load(), juce::dontSendNotification);
    readButton.setButtonText (a.reading.load() ? "Read on" : "Read off");
    undoButton.setEnabled (a.canUndo());
    clearAllButton.setEnabled (a.getNumLanes() > 0);
}

void AutomationPanel::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::engraved (g, "AUTOMATION", titleArea, juce::Justification::centredLeft, 22.0f);
    g.setColour (vintage::pal().ink2);
    g.setFont (vintage::barlow (13.0f));
    g.drawFittedText ("Rec moves, then turn any knob while it plays. Each setting you move gets a lane; drag on a lane to draw. "
                      "Moving a knob that has a lane takes over while you move it.", hintArea, juce::Justification::centredLeft, 2);
    if (list->rows.isEmpty())
    {
        g.setColour (vintage::pal().ink2);
        g.setFont (vintage::cond (17.0f, vintage::Weight::semi));
        g.drawFittedText ("No moves yet. Press Rec moves, play, and sweep the bass's Cutoff (or any effect's knob):\n"
                          "it plays back on the beat, in the song, and in what you record for Logic.", emptyArea, juce::Justification::centred, 3);
    }
    auto box = statusArea.toFloat();
    vintage::drawLcd (g, box);
    vintage::lcdText (g, status.isEmpty() ? juce::String ("REC MOVES, THEN TURN A KNOB") : status.toUpperCase(),
                      box.reduced (10, 0), 16.0f, juce::Justification::centredLeft);
}

void AutomationPanel::resized()
{
    auto r = getLocalBounds().reduced (18, 14);
    {
        auto row = r.removeFromTop (40);
        titleArea = row.removeFromLeft (170);
        undoButton.setBounds (row.removeFromRight (80).reduced (0, 3));
        row.removeFromRight (6);
        clearAllButton.setBounds (row.removeFromRight (96).reduced (0, 3));
        row.removeFromRight (6);
        readButton.setBounds (row.removeFromRight (96).reduced (0, 3));
        row.removeFromRight (6);
        recButton.setBounds (row.removeFromRight (170).reduced (0, 3));
    }
    hintArea = r.removeFromTop (36);
    statusArea = r.removeFromBottom (38);
    r.removeFromBottom (10);
    viewport.setBounds (r);
    emptyArea = r;
    rebuildRows();
}
