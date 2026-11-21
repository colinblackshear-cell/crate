#pragma once

#include "PocketTheme.h"

// Every bottom sheet behaves the same: the same grabber bar on top, and it closes with a downward flick or a drag
// past a third of its height (as well as its DONE button, and a tap on the dim). The sheets derive from DragSheet and
// the app sets onDismiss to the sheet's own close, so the sheet slides the rest of the way and the dim goes.
namespace pocket
{
void drawGrabber (juce::Graphics&, int sheetWidth);   // the one bar: 44 x 5, 8 px from the top

class DragSheet : public juce::Component
{
public:
    DragSheet();
    ~DragSheet() override;

    std::function<void()> onDismiss;        // flicked or dragged away
    // Where a drag may start: the top `zone` px (the grabber and the title row), or anywhere on the sheet that is not
    // a button or a knob (`wholeSheet`, for sheets that are only buttons).
    void setDragZone (int zone, bool wholeSheet = false) { dragZone = zone; dragWhole = wholeSheet; }
    bool animated = true;                    // false in screenshot mode

    // What a drag does, for the mouse and for the self-checks. Returns true when the sheet was dismissed.
    void dragStart();
    void dragMove (float dy);
    bool dragEnd (float dy, float pxPerMs);
    bool isDragging() const { return dragging; }

private:
    struct Dragger : juce::MouseListener
    {
        explicit Dragger (DragSheet& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        DragSheet& owner;
        bool eligible = false;
        float downY = 0.0f, lastY = 0.0f, prevY = 0.0f;
        juce::uint32 lastT = 0, prevT = 0;
    };

    Dragger dragger { *this };
    juce::Rectangle<int> startBounds;
    int dragZone = 50;
    bool dragWhole = false, dragging = false;
};
}
