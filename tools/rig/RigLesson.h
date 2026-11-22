#pragma once

#include "RigPanels.h"

// "Make a beat" on the Mac: the same four step coach card as Kaleidobox Pocket's (PocketLesson.h), over
// the drum machine's grid on Play. It says what to click, and the grid glows the suggested steps (only a
// glow: nothing is filled in for you). NEXT is always there, and Skip the lesson ends it.
//   1 Start with the kick   2 Now the snare   3 Add hats   4 Press Rec (the bottom bar's Rec)
// When the take lands on a layer, a last card says so and offers Add another tool and Done.
class RigLesson : public juce::Component
{
public:
    enum Phase { kick, snare, hats, rec, landed };
    static constexpr int kWidth = 420, kHeight = 210;

    RigLesson();

    std::function<void (int phase)> onPhase;   // the card moved to this phase: the Rig picks the drums, glows steps...
    std::function<void()> onEnd;               // Skip the lesson, or Done
    std::function<void()> onAddTool;           // Add another tool
    std::function<void()> onPressRec;          // the card's own Rec on step 4 (the same as the bottom bar's)
    std::function<int()> nextLayerNumber;      // where the next take lands, counted from 1

    void begin() { goTo (kick); }
    void goTo (int phase);
    void showLanded (int layerNumber);
    void setRecBusy (bool busy);
    int phase() const { return current; }
    int landedLayer() const { return landedOn; }
    bool isRecBusy() const { return busy; }

    rigpanels::PanelButton primary { "Next" }, secondary { "Skip the lesson" };

    void paint (juce::Graphics&) override;
    void resized() override;

    struct Words { juce::String title, body, next; };
    Words words() const;

private:
    int current = kick, landedOn = 0;
    bool busy = false;
};
