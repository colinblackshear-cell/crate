#include "MovesSheet.h"

using namespace pocket;

namespace
{
constexpr int kChipH = 46, kSide = 24, kGap = 8;
}

MovesSheet::MovesSheet()
{
    setDragZone (60);
    for (int m = 0; m < PerformFx::numModes; ++m)
    {
        chips[m].setText (PerformFx::modeName (m).toUpperCase());
        chips[m].setFontHeight (16.0f);
        chips[m].onClick = [this, m] { press (modeBase + m); };
        addAndMakeVisible (chips[m]);
    }
    holdB.setFontHeight (16.0f);
    holdB.onClick = [this] { press (holdId); };
    addAndMakeVisible (holdB);
}

juce::Component* MovesSheet::chip (int id)
{
    if (id == holdId)
        return &holdB;
    if (id >= modeBase && id < modeBase + PerformFx::numModes)
        return &chips[id - modeBase];
    return nullptr;
}

void MovesSheet::press (int id)
{
    if (id == holdId)
    {
        setHold (! hold);
        return;
    }
    if (fx != nullptr && id >= modeBase && id < modeBase + PerformFx::numModes)
        fx->mode = id - modeBase;
    refresh();
}

void MovesSheet::setHold (bool on)
{
    hold = on;
    if (! hold && ! fingerOn && fx != nullptr)
        fx->engaged = false;   // HOLD off: it lets go
    refresh();
}

void MovesSheet::touchDown (float x, float y)
{
    if (fx == nullptr)
        return;
    fingerOn = true;
    fx->x = juce::jlimit (0.0f, 1.0f, x);
    fx->y = juce::jlimit (0.0f, 1.0f, y);
    fx->engaged = true;
    repaint();
}

void MovesSheet::touchMove (float x, float y)
{
    if (fx == nullptr || ! fingerOn)
        return;
    fx->x = juce::jlimit (0.0f, 1.0f, x);
    fx->y = juce::jlimit (0.0f, 1.0f, y);
    repaint();
}

void MovesSheet::touchUp()
{
    fingerOn = false;
    if (fx != nullptr && ! hold)
        fx->engaged = false;
    repaint();
}

void MovesSheet::releasePad()
{
    fingerOn = false;
    if (fx != nullptr && ! hold)
        fx->engaged = false;
}

void MovesSheet::touchAt (juce::Point<int> p)
{
    if (padRect.isEmpty())
        return;
    const float x = (float) (p.x - padRect.getX()) / (float) padRect.getWidth();
    const float y = 1.0f - (float) (p.y - padRect.getY()) / (float) padRect.getHeight();
    if (fingerOn)
        touchMove (x, y);
    else
        touchDown (x, y);
}

void MovesSheet::mouseDown (const juce::MouseEvent& e)
{
    if (padRect.contains (e.getPosition()))
        touchAt (e.getPosition());
}

void MovesSheet::mouseDrag (const juce::MouseEvent& e)
{
    if (fingerOn)
        touchAt (e.getPosition());
}

void MovesSheet::mouseUp (const juce::MouseEvent&)
{
    if (fingerOn)
        touchUp();
}

void MovesSheet::visibilityChanged()
{
    if (isVisible())
        startTimerHz (30);
    else
    {
        stopTimer();
        releasePad();
    }
}

void MovesSheet::timerCallback()
{
    const bool lit = fx != nullptr && (fx->engaged.load() || fx->getRise() > 0.001f);
    if (lit || wasLit)
        repaint (padRect.expanded (4));
    wasLit = lit;
}

void MovesSheet::refresh()
{
    const int m = fx != nullptr ? fx->mode.load() : 0;
    for (int i = 0; i < PerformFx::numModes; ++i)
    {
        chips[i].setLook (i == m ? Look::red : Look::cream);
        chips[i].setToggled (i == m);
    }
    holdB.setLook (hold ? Look::amber : Look::cream);
    holdB.setToggled (hold);
    repaint();
}

void MovesSheet::resized()
{
    const int w = getWidth(), inner = w - 2 * kSide;
    int y = 78;
    const int each = (inner - 2 * kGap) / 3;
    for (int i = 0; i < PerformFx::numModes; ++i)
        chips[i].setBounds (kSide + (i % 3) * (each + kGap), y + (i / 3) * (kChipH + kGap), each, kChipH);
    y += 3 * (kChipH + kGap) + 6;
    const int side = juce::jmin (inner, 330);
    padRect = { (w - side) / 2, y + 20, side, side };
    hintArea = { kSide, y, inner, 18 };
    holdB.setBounds (kSide, padRect.getBottom() + 28, inner, 52);
}

void MovesSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (26.0f));
    g.drawText ("MOVES", kSide, 16, 200, 30, juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    g.drawText (juce::String::fromUTF8 ("Touch to play \xc2\xb7 let go and it's gone \xc2\xb7 HOLD keeps it"), kSide, 46, getWidth() - 2 * kSide, 20,
                juce::Justification::centredLeft);

    const int mode = fx != nullptr ? fx->mode.load() : 0;
    const bool on = fx != nullptr && fx->engaged.load();
    const auto pad = padRect.toFloat();
    drawWell (g, pad, 14.0f);
    // a few quiet stars
    g.setColour (col::inkFaint.withAlpha (0.35f));
    for (int i = 0; i < 18; ++i)
    {
        const float sx = pad.getX() + pad.getWidth() * (float) ((i * 37) % 100) / 100.0f;
        const float sy = pad.getY() + pad.getHeight() * (float) ((i * 53 + 11) % 100) / 100.0f;
        g.fillEllipse (sx, sy, 2.0f, 2.0f);
    }
    // the riser: how far it has risen, filling the pad from the bottom
    if (fx != nullptr && mode == PerformFx::riser)
    {
        const float rise = fx->getRise();
        if (rise > 0.001f)
        {
            g.setColour (col::red.withAlpha (0.35f));
            g.fillRoundedRectangle (pad.withTrimmedTop (pad.getHeight() * (1.0f - rise)).reduced (3.0f), 11.0f);
            g.setColour (col::red);
            const float ly = pad.getBottom() - pad.getHeight() * rise;
            g.drawLine (pad.getX() + 4.0f, ly, pad.getRight() - 4.0f, ly, 2.0f);
        }
    }
    // the captions
    g.setColour (col::inkDim);
    g.setFont (small (12.0f));
    g.drawText (PerformFx::axisText (mode, true), padRect.reduced (10, 6), juce::Justification::centredBottom);
    g.drawText (PerformFx::axisText (mode, false), padRect.reduced (10, 6), juce::Justification::topLeft);
    if (fx != nullptr)
    {
        const juce::Point<float> dot (pad.getX() + fx->x.load() * pad.getWidth(), pad.getBottom() - fx->y.load() * pad.getHeight());
        const float r = on ? 18.0f : 10.0f;
        if (on)
        {
            g.setColour (col::red.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (r * 3.2f, r * 3.2f).withCentre (dot));
        }
        g.setColour (on ? col::red : col::inkFaint.withAlpha (0.6f));
        g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (dot));
    }
}
