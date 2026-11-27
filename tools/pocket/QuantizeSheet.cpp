#include "QuantizeSheet.h"

using namespace pocket;

namespace
{
constexpr int kChipH = 48, kSide = 24, kGap = 8;
}

QuantizeSheet::QuantizeSheet()
{
    setDragZone (60);
    for (int i = 0; i < kQuantizeGrids; ++i)
    {
        grids[i].setText (quantizeGridName (i));
        grids[i].setFontHeight (17.0f);
        grids[i].onClick = [this, i] { press (gridBase + i); };
        addAndMakeVisible (grids[i]);
    }
    halfB.onClick = [this] { press (half); };
    fullB.onClick = [this] { press (full); };
    swingB.onClick = [this] { press (swingId); };
    applyB.onClick = [this] { press (applyId); };
    cancelB.onClick = [this] { press (cancelId); };
    for (auto* b : { &halfB, &fullB, &swingB, &cancelB })
    {
        b->setFontHeight (16.0f);
        addAndMakeVisible (*b);
    }
    applyB.setFontHeight (22.0f);
    addAndMakeVisible (applyB);
}

juce::Component* QuantizeSheet::chip (int id)
{
    if (id >= gridBase && id < gridBase + kQuantizeGrids)
        return &grids[id - gridBase];
    switch (id)
    {
        case half: return &halfB;
        case full: return &fullB;
        case swingId: return &swingB;
        case applyId: return &applyB;
        case cancelId: return &cancelB;
        default: break;
    }
    return nullptr;
}

void QuantizeSheet::show (const juce::String& what, const juce::String& noun, int count)
{
    title = "QUANTIZE " + what;
    nounText = noun;
    itemCount = count;
    refresh();
}

void QuantizeSheet::press (int id)
{
    if (id >= gridBase && id < gridBase + kQuantizeGrids)
        spec.grid = id - gridBase;
    else if (id == half)
        spec.strength = 0.5f;
    else if (id == full)
        spec.strength = 1.0f;
    else if (id == swingId)
    {
        if (spec.grid >= 4)
            return;   // (swing is for the straight grids; the triplets are already swung)
        spec.swing = ! spec.swing;
    }
    else if (id == applyId)
    {
        if (onApply)
            onApply (spec);
        return;
    }
    else if (id == cancelId)
    {
        if (onCancel)
            onCancel();
        return;
    }
    else
        return;
    refresh();
}

void QuantizeSheet::refresh()
{
    auto pick = [] (PButton& b, bool on) { b.setLook (on ? Look::amber : Look::cream); b.setToggled (on); };
    for (int i = 0; i < kQuantizeGrids; ++i)
        pick (grids[i], spec.grid == i);
    pick (halfB, spec.strength < 0.99f);
    pick (fullB, spec.strength >= 0.99f);
    const bool straight = spec.grid < 4;
    pick (swingB, spec.swing && straight);
    if (! straight)
        swingB.setLook (Look::ghost);
    resized();
    repaint();
}

void QuantizeSheet::resized()
{
    const int w = getWidth(), inner = w - 2 * kSide;
    int y = 78;
    gridLabel = { kSide, y, inner, 18 };
    y += 20;
    const int each = (inner - kGap * (kQuantizeGrids - 1)) / kQuantizeGrids;
    for (int i = 0; i < kQuantizeGrids; ++i)
        grids[i].setBounds (kSide + i * (each + kGap), y, each, kChipH);
    y += kChipH + 12;
    strengthLabel = { kSide, y, inner, 18 };
    y += 20;
    const int third = (inner - 2 * kGap) / 3;
    halfB.setBounds (kSide, y, third, kChipH);
    fullB.setBounds (kSide + third + kGap, y, third, kChipH);
    swingB.setBounds (kSide + 2 * (third + kGap), y, inner - 2 * (third + kGap), kChipH);
    y += kChipH + 10;
    hintArea = { kSide, y, inner, 40 };
    y += 40 + 8;
    const int cancelW = (inner - kGap) * 34 / 100;
    cancelB.setBounds (kSide, y, cancelW, 56);
    applyB.setBounds (kSide + cancelW + kGap, y, inner - cancelW - kGap, 56);
}

void QuantizeSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (26.0f));
    g.drawText (title, kSide, 16, getWidth() - 2 * kSide, 36, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (14.0f));
    g.drawText (juce::String (itemCount) + " " + nounText + (itemCount == 1 ? "" : "s") + " in it. They move onto the grid; the ones already on it stay.",
                kSide, 50, getWidth() - 2 * kSide, 20, juce::Justification::centredLeft);

    g.setFont (spaced (condensedBold (14.0f)));
    g.drawText ("GRID", gridLabel, juce::Justification::centredLeft);
    g.drawText ("STRENGTH", strengthLabel, juce::Justification::centredLeft);

    const juce::String grid = quantizeGridName (spec.grid);
    juce::String hint = spec.strength >= 0.99f ? "Every " + juce::String (nounText) + " lands on a " + grid + " line."
                                               : "Every " + juce::String (nounText) + " moves halfway to the " + grid + " line, so it still feels played.";
    if (spec.swing && spec.grid < 4)
        hint += " Every second line is a little late: swing.";
    else if (spec.grid >= 4)
        hint += " Triplets: three to a beat (1/8T) or six (1/16T).";
    g.setColour (col::inkDim);
    g.setFont (small (14.0f));
    g.drawFittedText (hint, hintArea, juce::Justification::topLeft, 2, 0.9f);
}
