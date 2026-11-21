#pragma once

#include "PocketTheme.h"

// "Make a beat": a four step coach card for the DRUMS tool. It sits above the thumb bar (so it never covers
// the step buttons it talks about), says what to tap, and the app glows the suggested steps on the real grid.
// Nothing is filled in for you, NEXT is always there, and Skip the lesson ends it.
//   1 Start with the kick   2 Now the snare   3 Add hats   4 Press REC (spotlight on the thumb bar's REC)
// When the take lands on a layer, a last card says so and offers ADD ANOTHER TOOL and DONE.
class BeatLesson : public juce::Component, private juce::Timer
{
public:
    enum Phase { kick, snare, hats, rec, landed };
    static constexpr int kCardHeight = 222;

    BeatLesson();

    std::function<void (int phase)> onPhase;          // the card moved to this phase: the app selects the voice, glows steps...
    std::function<void()> onEnd;                       // Skip the lesson, or DONE
    std::function<void()> onAddTool;                   // ADD ANOTHER TOOL
    std::function<void()> onPressRec;                  // the card's own button on step 4 (the same as the thumb bar's REC)
    std::function<juce::Rectangle<int>()> recSpot;    // the thumb bar's REC, in this component's coordinates
    std::function<int()> nextLayerNumber;              // the layer the next take lands on, counted from 1 (0 = none free)

    void begin() { goTo (kick); }
    void goTo (int phase);
    void showLanded (int layerNumber);   // the take is on this layer (counted from 1)
    void setRecBusy (bool busy);         // REC is counting in or recording
    int phase() const { return current; }
    int landedLayer() const { return landedOn; }

    void setCardBounds (juce::Rectangle<int> r) { card = r; resized(); repaint(); }
    juce::Rectangle<int> cardBounds() const { return card; }
    void setAnimated (bool on) { animated = on; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override { return card.contains (x, y); }   // everything but the card is the app's
    void visibilityChanged() override;

private:
    void timerCallback() override;
    struct Words { juce::String title, body, next; };
    Words words() const;

    int current = kick, landedOn = 0;
    bool busy = false, animated = true;
    juce::Rectangle<int> card;
    pocket::PButton primary { "Next", pocket::Look::amber }, secondary { "Skip the lesson", pocket::Look::ghost };
};
