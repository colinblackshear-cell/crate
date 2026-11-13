#include "QuantizePanel.h"

QuantizePanel::QuantizePanel (RigEngine& e, int t) : engine (e)
{
    auto& q = engine.getQuantizer();

    for (int i = 0; i < RigQuantizer::numTargets; ++i)
        what.addItem (RigQuantizer::targetName (i), i + 1);
    what.setTooltip ("What to quantize");
    what.onChange = [this] { setTarget (what.getSelectedId() - 1); };
    addAndMakeVisible (what);

    for (int g = 0; g < quantize::numGrids; ++g)
    {
        auto* b = grids.add (new juce::TextButton (quantize::gridName (g)));
        b->setTooltip (g == quantize::eighthTriplet || g == quantize::sixteenthTriplet
                           ? "Triplets: three to the beat (1/8 T) or six (1/16 T), for a shuffle or a swung feel"
                           : "Pull notes onto every " + quantize::gridName (g));
        b->onClick = [this, g] { pickGrid (g); };
        addAndMakeVisible (b);
    }

    strength.slider.setRange (0, 100, 1);
    strength.slider.setTextValueSuffix ("%");
    strength.slider.setValue (q.settings.strength * 100.0, juce::dontSendNotification);
    strength.slider.setTooltip ("How far each note moves toward the grid: 100% is right on it, 50% halfway (tighter but still you)");
    strength.slider.onValueChange = [this]
    {
        engine.getQuantizer().settings.strength = (float) strength.slider.getValue() / 100.0f;
        strength.repaint();
    };
    swing.slider.setRange (50, 75, 1);
    swing.slider.setTextValueSuffix ("%");
    swing.slider.setValue (q.settings.swing, juce::dontSendNotification);
    swing.slider.setTooltip ("Swing pushes every other 1/8 or 1/16 late, as on the MPC: 50% is straight, 66% a triplet feel");
    swing.slider.onValueChange = [this]
    {
        engine.getQuantizer().settings.swing = (int) swing.slider.getValue();
        swing.repaint();
    };
    for (auto* k : { &strength, &swing })
    {
        k->nameSize = 15.0f;
        addAndMakeVisible (k);
    }

    newLoops.setColour (juce::ToggleButton::textColourId, vintage::pal().ink);
    newLoops.setColour (juce::ToggleButton::tickColourId, vintage::pal().ink);
    newLoops.setColour (juce::ToggleButton::tickDisabledColourId, vintage::pal().ink2);
    newLoops.setToggleState (q.settings.newLoops, juce::dontSendNotification);
    newLoops.setTooltip ("Each new loop is quantized as its recording finishes (Undo puts it back)");
    newLoops.onClick = [this] { engine.getQuantizer().settings.newLoops = newLoops.getToggleState(); };
    addAndMakeVisible (newLoops);

    quantizeButton.setTooltip ("Quantize it now (Cmd+U does it from any screen)");
    quantizeButton.onClick = [this] { pressQuantize(); };
    undoButton.setTooltip ("Put back what the last quantize moved");
    undoButton.onClick = [this] { pressUndo(); };
    vintage::setLit (quantizeButton, vintage::Lit::play);
    quantizeButton.setToggleState (true, juce::dontSendNotification);   // lit green, the button to press
    for (auto* b : { &quantizeButton, &undoButton })
        addAndMakeVisible (b);

    setTarget (t);
    setSize (kWidth, kHeight);
    startTimerHz (4);
}

QuantizePanel::~QuantizePanel() { stopTimer(); }

void QuantizePanel::setTarget (int t)
{
    target = juce::jlimit (0, RigQuantizer::numTargets - 1, t < 0 ? (int) RigQuantizer::allLoops : t);
    what.setSelectedId (target + 1, juce::dontSendNotification);
    update();
}

void QuantizePanel::pickGrid (int g)
{
    engine.getQuantizer().settings.grid = juce::jlimit (0, quantize::numGrids - 1, g);
    update();
}

void QuantizePanel::pressQuantize()
{
    say (engine.getQuantizer().apply (target));
    update();
}

void QuantizePanel::pressUndo()
{
    say (engine.getQuantizer().undo());
    update();
}

void QuantizePanel::say (const juce::String& text)
{
    status = text;
    repaint (statusArea);
    if (onSay)
        onSay (text);
}

void QuantizePanel::timerCallback()
{
    // A loop finishing, or the pattern changing elsewhere: keep what it says up to date.
    update();
}

void QuantizePanel::update()
{
    const auto& s = engine.getQuantizer().settings;
    for (int g = 0; g < grids.size(); ++g)
    {
        vintage::setLit (*grids[g], vintage::Lit::current);
        grids[g]->setToggleState (g == s.grid, juce::dontSendNotification);
    }
    swing.setEnabled (quantize::gridSwings (s.grid));
    swing.setAlpha (quantize::gridSwings (s.grid) ? 1.0f : 0.45f);
    undoButton.setEnabled (engine.getQuantizer().canUndo());
    repaint (detailArea);
}

void QuantizePanel::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::engraved (g, "QUANTIZE", titleArea, juce::Justification::centredLeft, 22.0f);
    vintage::engraved (g, "GRID", gridLabel, juce::Justification::centredLeft, 14.0f);
    g.setColour (vintage::pal().ink2);
    g.setFont (vintage::barlow (14.0f));
    g.drawText (engine.getQuantizer().targetDetail (target), detailArea, juce::Justification::centredLeft, true);

    auto box = statusArea.toFloat();
    vintage::drawLcd (g, box);
    vintage::lcdText (g, status.isEmpty() ? juce::String ("PICK A GRID, THEN QUANTIZE") : status.toUpperCase(),
                      box.reduced (10, 0), 17.0f, juce::Justification::centredLeft);
}

void QuantizePanel::resized()
{
    auto r = getLocalBounds().reduced (18, 14);
    {
        auto row = r.removeFromTop (34);
        titleArea = row.removeFromLeft (128);
        what.setBounds (row.removeFromLeft (170).reduced (0, 2));
        row.removeFromLeft (12);
        detailArea = row;
    }
    r.removeFromTop (10);
    {
        auto row = r.removeFromTop (40);
        gridLabel = row.removeFromLeft (52);
        const int w = row.getWidth() / grids.size();
        for (auto* b : grids)
            b->setBounds (row.removeFromLeft (w).reduced (3, 0));
    }
    r.removeFromTop (10);
    {
        auto row = r.removeFromTop (110);
        strength.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (8);
        swing.setBounds (row.removeFromLeft (120));
        row.removeFromLeft (16);
        auto col = row;
        newLoops.setBounds (col.removeFromTop (34).withTrimmedTop (6));
        col.removeFromTop (10);
        auto buttons = col.removeFromBottom (52);
        quantizeButton.setBounds (buttons.removeFromLeft (buttons.getWidth() * 3 / 5).reduced (0, 2));
        buttons.removeFromLeft (8);
        undoButton.setBounds (buttons.reduced (0, 2));
    }
    r.removeFromTop (12);
    statusArea = r.removeFromTop (40);
}
