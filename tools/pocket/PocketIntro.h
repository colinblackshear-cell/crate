#pragma once

#include "PocketTheme.h"

// The opening title and the tour.
//
// TitleScreen: Kaleidobox's kaleidoscope (a mandala of petals that turns, and blooms with the mic) and
// two ways in: TAKE THE 2-MINUTE TOUR or START MAKING. Shown each time the app opens.
//
// TourOverlay: seven coach marks on the real controls (the tool row, REC, BPM and CLICK, the pads, the loop
// layers, the vocoder's headphones, the menu icon that opens the song drawer). Each stop opens the real screen,
// dims everything but one spotlit control (which still works, so you can hit the pad it points at) and
// says what it does, with NEXT and Skip tour.
class TitleScreen : public juce::Component, private juce::Timer
{
public:
    TitleScreen();

    std::function<void()> onTour, onStart;
    std::function<float()> micLevel;   // 0..1 since the last call

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;
    void setDemoAngle (float a) { angle = a; bloom = 0.35f; repaint(); }

private:
    void timerCallback() override;
    juce::Rectangle<float> mandalaRect() const;

    pocket::PButton tour { "Take the 2-minute tour", pocket::Look::amber }, start { "Start making" };
    float angle = 0.0f, bloom = 0.0f;
};

class TourOverlay : public juce::Component
{
public:
    struct Stop
    {
        juce::String tag;                 // for the card's "STOP n OF 7 · REC"
        juce::Colour colour;              // its dot
        juce::String title, body, next;   // "Hit it!", what it does, the button ("I HIT IT")
    };

    TourOverlay();

    static const std::vector<Stop>& stops();
    // Opens the stop's screen and returns the spotlight, in this overlay's coordinates.
    std::function<juce::Rectangle<int> (int stop)> show;
    std::function<void()> onFinish;   // the last NEXT, or Skip tour

    void begin();
    void goTo (int stop);
    int current() const { return index; }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;   // the spotlight lets touches through

private:
    juce::Rectangle<float> cardRect() const;

    int index = 0;
    juce::Rectangle<int> spot;
    pocket::PButton nextButton { "Next", pocket::Look::amber }, skip { "Skip tour", pocket::Look::ghost };
};
