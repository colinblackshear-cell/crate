#pragma once

#include "PerformFx.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// MOVES: the Mac's FX pad (PerformFx) over the whole mix, played from a bottom sheet. Nine mode chips, one big XY pad
// (finger down brings the effect in, dragging moves it, lifting lets it go) and a HOLD switch that keeps it in.
// The effect itself runs in the looper engine (LoopsEngine::moves()); this sheet only plays the pad.
class MovesSheet : public pocket::DragSheet, private juce::Timer
{
public:
    MovesSheet();

    PerformFx* fx = nullptr;   // the engine's (set by the app)

    enum Id { none = 0, modeBase = 1, holdId = 20 };   // modeBase + PerformFx::Mode
    // What the chips do (the self-checks use these too).
    void press (int id);
    juce::Component* chip (int id);
    bool holding() const { return hold; }
    void setHold (bool on);
    // The pad, as a finger on it (x, y in 0..1, y up). touchUp: lets go unless HOLD is on.
    void touchDown (float x, float y);
    void touchMove (float x, float y);
    void touchUp();
    juce::Rectangle<int> padArea() const { return padRect; }
    // The sheet is going away: the pad lets go, unless HOLD.
    void releasePad();
    void refresh();
    int preferredHeight() const { return 700; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void touchAt (juce::Point<int>);

    pocket::PButton chips[PerformFx::numModes];
    pocket::PButton holdB { "HOLD" };
    juce::Rectangle<int> padRect, hintArea;
    bool hold = false, fingerOn = false, wasLit = false;
};
