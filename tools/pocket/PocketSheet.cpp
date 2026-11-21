#include "PocketSheet.h"

using namespace pocket;

void pocket::drawGrabber (juce::Graphics& g, int sheetWidth)
{
    g.setColour (juce::Colour (0xffa59a85));
    g.fillRoundedRectangle ((float) sheetWidth * 0.5f - 22.0f, 8.0f, 44.0f, 5.0f, 2.5f);
}

DragSheet::DragSheet()
{
    addMouseListener (&dragger, true);
}

DragSheet::~DragSheet()
{
    removeMouseListener (&dragger);
}

void DragSheet::dragStart()
{
    juce::Desktop::getInstance().getAnimator().cancelAnimation (this, false);
    startBounds = getBounds();
    dragging = true;
}

void DragSheet::dragMove (float dy)
{
    setTopLeftPosition (startBounds.getX(), startBounds.getY() + (int) juce::jmax (0.0f, dy));
}

bool DragSheet::dragEnd (float dy, float pxPerMs)
{
    dragging = false;
    const bool away = dy > (float) startBounds.getHeight() / 3.0f || (pxPerMs > 0.7f && dy > 24.0f);
    if (away && onDismiss)
    {
        onDismiss();
        return true;
    }
    if (animated)
        juce::Desktop::getInstance().getAnimator().animateComponent (this, startBounds, 1.0f, 160, false, 1.0, 1.0);
    else
        setBounds (startBounds);
    return false;
}

void DragSheet::Dragger::mouseDown (const juce::MouseEvent& e)
{
    const auto local = owner.getLocalPoint (nullptr, e.getScreenPosition());
    const auto* c = e.originalComponent;
    eligible = (owner.dragWhole || local.y <= (float) owner.dragZone) && dynamic_cast<const PButton*> (c) == nullptr
               && dynamic_cast<const KnobView*> (c) == nullptr;
    downY = lastY = prevY = e.getScreenPosition().y;
    lastT = prevT = juce::Time::getMillisecondCounter();
}

void DragSheet::Dragger::mouseDrag (const juce::MouseEvent& e)
{
    if (! eligible)
        return;
    const float y = e.getScreenPosition().y;
    const float dy = y - downY, dx = e.getScreenPosition().x - e.getMouseDownScreenPosition().x;
    if (! owner.dragging)
    {
        if (dy > 10.0f && dy > std::abs (dx))
            owner.dragStart();
        else
            return;
    }
    prevY = lastY;
    prevT = lastT;
    lastY = y;
    lastT = juce::Time::getMillisecondCounter();
    owner.dragMove (dy);
}

void DragSheet::Dragger::mouseUp (const juce::MouseEvent& e)
{
    if (! owner.dragging)
        return;
    const float dy = e.getScreenPosition().y - downY;
    const auto now = juce::Time::getMillisecondCounter();
    // a finger that rested before lifting is not a flick
    const float v = now - lastT > 90 ? 0.0f : (lastY - prevY) / (float) juce::jmax<juce::uint32> (1, lastT - prevT);
    owner.dragEnd (dy, v);
}
