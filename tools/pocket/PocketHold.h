#pragma once

#include "PocketTheme.h"

// Press and hold: a long press (about 450 ms, cancelled when the finger moves more than about 10 px) opens a compact
// menu card next to the finger, over a dim, with the thing that was held kept bright. One component for every hold
// menu in the app (a loop layer, a pad, a chord pad, a song) and for the small TOUR · LESSON menu.
namespace pocket
{
struct HoldItem
{
    juce::String text;            // "QUANTIZE"
    juce::String detail;          // dimmer, on the right: "1/16", "board off", "undo brings it back"
    bool danger = false;          // red (CLEAR, DELETE, REMOVE SOUND)
    bool enabled = true;
    std::function<void()> action;
};

struct HoldSpec
{
    juce::String title;           // the dark bar on top: "LAYER 2 · SYNTH BASS"
    std::vector<HoldItem> items;
    bool spotlight = true;        // keep the held thing bright (false: a plain tap menu)
};

// Asks the app to open a hold menu. `at` and `highlight` are in `source`'s coordinates (the finger, and the thing held).
using HoldShow = std::function<void (const HoldSpec&, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight)>;

// Counts the press. Call press() on mouse down, move() on every drag and release() on mouse up (the component checks
// fired() on mouse up so the lift after a hold does not also count as a tap).
class HoldDetector : private juce::Timer
{
public:
    explicit HoldDetector (std::function<void()> onHoldFired, int holdMs = 450, float slopPx = 10.0f)
        : fire (std::move (onHoldFired)), ms (holdMs), slop (slopPx) {}
    ~HoldDetector() override { stopTimer(); }

    void press (juce::Point<float> p)
    {
        cancel();
        down = p;
        didFire = false;
        armed = true;
        startTimer (ms);
    }
    void move (juce::Point<float> p)
    {
        if (armed && p.getDistanceFrom (down) > slop)
            cancel();
    }
    void release() { cancel(); }
    void cancel()
    {
        armed = false;
        stopTimer();
    }
    bool fired() const { return didFire; }
    bool isArmed() const { return armed; }
    void fireNow()   // the self-checks
    {
        armed = false;
        stopTimer();
        didFire = true;
        if (fire)
            fire();
    }

private:
    void timerCallback() override
    {
        stopTimer();
        if (armed)
            fireNow();
    }

    std::function<void()> fire;
    int ms;
    float slop;
    juce::Point<float> down;
    bool armed = false, didFire = false;
};

// The card. Added over the whole app; open() takes the card's target and the app's safe margins.
class HoldMenu : public juce::Component
{
public:
    static constexpr int kRowHeight = 48, kTitleHeight = 34;

    HoldMenu() { setVisible (false); }

    // target: the held thing, in this component's coordinates. The card goes below it (or above, where there's room).
    void open (HoldSpec, juce::Rectangle<int> target, int topMargin, int bottomMargin);
    void close();
    bool isOpen() const { return isVisible(); }
    int itemCount() const { return (int) spec.items.size(); }
    const HoldSpec& current() const { return spec; }
    juce::Rectangle<int> cardBounds() const { return card; }
    void choose (int index);   // what a tap on that row does (also for the self-checks)

    std::function<void()> onClosed;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Rectangle<int> rowArea() const { return card.withTrimmedTop (kTitleHeight); }
    int contentHeight() const { return (int) spec.items.size() * rowH; }
    int rowAt (juce::Point<float>) const;
    void layoutCard();
    void clampScroll();

    HoldSpec spec;
    juce::Rectangle<int> target, card;
    int topMargin = 0, bottomMargin = 0;
    float scroll = 0.0f, scrollAtDown = 0.0f;
    int pressedRow = -1;
    int rowH = kRowHeight;   // 48, or down to 44 when that lets the whole menu fit
    bool moved = false, downInCard = false;
    juce::Point<float> downPos;
};
}
