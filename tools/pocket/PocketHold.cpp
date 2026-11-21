#include "PocketHold.h"

using namespace pocket;

namespace
{
const juce::Colour kHoldRed { 0xffd8452f };
}

void HoldMenu::open (HoldSpec s, juce::Rectangle<int> t, int top, int bottom)
{
    spec = std::move (s);
    target = t;
    topMargin = top;
    bottomMargin = bottom;
    scroll = 0.0f;
    pressedRow = -1;
    moved = false;
    layoutCard();
    setVisible (true);
    toFront (false);
    repaint();
}

void HoldMenu::close()
{
    if (! isVisible())
        return;
    setVisible (false);
    pressedRow = -1;
    if (onClosed)
        onClosed();
}

void HoldMenu::choose (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) spec.items.size()))
        return;
    auto item = spec.items[(size_t) index];
    close();
    if (item.enabled && item.action)
        item.action();
}

void HoldMenu::resized()
{
    if (isVisible())
        layoutCard();
}

void HoldMenu::layoutCard()
{
    const int W = getWidth(), H = getHeight();
    const int cw = juce::jmin (W - 24, 276);
    const int gap = 8;
    const int above = target.getY() - gap - topMargin;
    const int below = H - bottomMargin - target.getBottom() - gap;
    rowH = kRowHeight;
    if (kTitleHeight + contentHeight() > juce::jmax (above, below))
        rowH = juce::jlimit (44, kRowHeight, (juce::jmax (above, below) - kTitleHeight) / juce::jmax (1, (int) spec.items.size()));
    const int want = kTitleHeight + contentHeight();
    int ch = want, y;
    if (want <= below)
        y = target.getBottom() + gap;
    else if (want <= above)
        y = target.getY() - gap - want;
    else if (below >= above)
    {
        ch = juce::jmax (kTitleHeight + 44 * 2, below);
        y = target.getBottom() + gap;
    }
    else
    {
        ch = juce::jmax (kTitleHeight + 44 * 2, above);
        y = target.getY() - gap - ch;
    }
    y = juce::jlimit (topMargin, juce::jmax (topMargin, H - bottomMargin - ch), y);
    const int x = juce::jlimit (12, juce::jmax (12, W - cw - 12), target.getCentreX() - cw / 2);
    card = { x, y, cw, ch };
    clampScroll();
}

void HoldMenu::clampScroll()
{
    const float room = (float) (card.getHeight() - kTitleHeight);
    scroll = juce::jlimit (0.0f, juce::jmax (0.0f, (float) contentHeight() - room), scroll);
}

int HoldMenu::rowAt (juce::Point<float> p) const
{
    auto area = rowArea();
    if (! area.toFloat().contains (p))
        return -1;
    const int row = (int) std::floor ((p.y - (float) area.getY() + scroll) / (float) rowH);
    return juce::isPositiveAndBelow (row, (int) spec.items.size()) ? row : -1;
}

void HoldMenu::paint (juce::Graphics& g)
{
    // the dim, with the held thing left bright
    juce::Path dim;
    dim.addRectangle (getLocalBounds().toFloat());
    const bool spot = spec.spotlight && ! target.isEmpty();
    if (spot)
    {
        dim.addRoundedRectangle (target.toFloat().expanded (2.0f), 14.0f);
        dim.setUsingNonZeroWinding (false);
    }
    g.setColour (juce::Colours::black.withAlpha (0.42f));
    g.fillPath (dim);
    if (spot)
    {
        g.setColour (col::amber);
        g.drawRoundedRectangle (target.toFloat().expanded (2.0f), 14.0f, 2.5f);
    }

    // the card
    auto c = card.toFloat();
    g.setColour (juce::Colour (0x55000000));
    g.fillRoundedRectangle (c.translated (0, 5), 16.0f);
    juce::Path shape;
    shape.addRoundedRectangle (c, 16.0f);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    g.setColour (col::creamLight);
    g.fillRect (c);

    // the rows, under the title bar
    {
        juce::Graphics::ScopedSaveState rows (g);
        g.reduceClipRegion (rowArea());
        for (int i = 0; i < (int) spec.items.size(); ++i)
        {
            const auto& item = spec.items[(size_t) i];
            auto r = juce::Rectangle<float> (c.getX(), (float) rowArea().getY() + (float) (i * rowH) - scroll, c.getWidth(), (float) rowH);
            if (r.getBottom() < (float) rowArea().getY() || r.getY() > c.getBottom())
                continue;
            if (i == pressedRow && ! moved && item.enabled)
            {
                g.setColour (col::amber.withAlpha (0.35f));
                g.fillRect (r);
            }
            if (i > 0)
            {
                g.setColour (col::rule.withAlpha (0.7f));
                g.fillRect (r.getX() + 16, r.getY(), r.getWidth() - 32, 1.0f);
            }
            const auto ink = ! item.enabled ? col::inkFaint : item.danger ? kHoldRed : col::ink;
            auto in = r.reduced (16, 0);
            g.setColour (ink);
            g.setFont (spaced (condensedBold (18.0f), 0.05f));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), item.text) + 6.0f;
            auto textR = in;
            if (item.detail.isNotEmpty())
                textR = in.withWidth (juce::jmin (in.getWidth() - 40.0f, tw));
            g.drawFittedText (item.text, textR.toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
            if (item.detail.isNotEmpty())
            {
                g.setColour (item.danger && item.enabled ? kHoldRed.withAlpha (0.8f) : col::inkDim);
                g.setFont (small (13.5f));
                g.drawFittedText (item.detail, in.withTrimmedLeft (textR.getWidth() + 6.0f).toNearestInt(), juce::Justification::centredRight, 1, 0.75f);
            }
        }
        // a hint that there is more below
        if (scroll + (float) rowArea().getHeight() < (float) contentHeight() - 1.0f)
        {
            g.setColour (col::inkDim);
            juce::Path chevron;
            const float cx = c.getCentreX(), cy = c.getBottom() - 9.0f;
            chevron.startNewSubPath (cx - 7, cy - 3);
            chevron.lineTo (cx, cy + 3);
            chevron.lineTo (cx + 7, cy - 3);
            g.strokePath (chevron, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // the title bar
    g.setColour (col::well);
    g.fillRect (c.withHeight ((float) kTitleHeight));
    g.setColour (col::lcdAmber);
    g.setFont (spaced (lcdFont (19.0f), 0.04f));
    g.drawFittedText (spec.title, c.withHeight ((float) kTitleHeight).reduced (16, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
}

void HoldMenu::mouseDown (const juce::MouseEvent& e)
{
    downPos = e.position;
    downInCard = card.toFloat().contains (e.position);
    moved = false;
    scrollAtDown = scroll;
    pressedRow = downInCard ? rowAt (e.position) : -1;
    repaint();
}

void HoldMenu::mouseDrag (const juce::MouseEvent& e)
{
    if (! moved && e.position.getDistanceFrom (downPos) > 8.0f)
        moved = true;
    if (moved && downInCard)
    {
        scroll = scrollAtDown - (e.position.y - downPos.y);
        clampScroll();
    }
    repaint();
}

void HoldMenu::mouseUp (const juce::MouseEvent& e)
{
    const int row = pressedRow;
    pressedRow = -1;
    if (moved)
    {
        // a drag on the card scrolls it; a drag that began outside it is a tap on the dim
        if (! downInCard)
            close();
        repaint();
        return;
    }
    if (! downInCard || ! card.toFloat().contains (e.position))
        return close();
    if (row >= 0 && row == rowAt (e.position))
    {
        if (spec.items[(size_t) row].enabled)
            return choose (row);
        return repaint();
    }
    repaint();
}
