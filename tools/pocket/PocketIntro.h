#pragma once

#include "PocketTheme.h"

// The opening title and the tour.
//
// TitleScreen: the box opens. A dark stage where Kaleidobox's kaleidoscope blooms in, one petal for each tool in its
// colour, then the name rises under it. The petals turn slowly, and bloom with the mic. Under it: PICK UP WHERE YOU
// LEFT OFF (the song you had open, its layers as a strip of colours), START MAKING and TAKE THE 2-MINUTE TOUR.
// Shown each time the app opens; a tap anywhere else goes straight in. The Mac's opening (RigTitle) is the same.
//
// TourOverlay: seven coach marks on the real controls (the tool row, REC, BPM and CLICK, the pads, the loop
// layers, the vocoder's headphones, the menu icon that opens the song drawer). Each stop opens the real screen,
// dims everything but one spotlit control (which still works, so you can hit the pad it points at) and
// says what it does, with NEXT and Skip tour.
class TitleScreen : public juce::Component, private juce::Timer
{
public:
    TitleScreen();

    std::function<void()> onTour, onStart, onContinue;
    std::function<float()> micLevel;   // 0..1 since the last call

    // The song PICK UP WHERE YOU LEFT OFF opens (no name: the card is hidden), its line and its layers' colours.
    void setContinue (const juce::String& name, const juce::String& meta, std::vector<juce::Colour> layers);
    bool hasContinue() const { return continueName.isNotEmpty(); }
    juce::Rectangle<int> continueCard() const;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;
    void setDemoAngle (float a) { angle = a; bloom = 0.35f; opened = 10.0f; repaint(); }   // the screenshots: fully open
    float openedFor() const { return opened; }   // seconds since it showed (the bloom-in plays over the first ~3)

    // The tools' colours, in the petals' order (the Mac's opening has the same).
    static const std::vector<juce::Colour>& petalColours();

private:
    void timerCallback() override;
    juce::Rectangle<float> mandalaRect() const;

    pocket::PButton tour { "Take the 2-minute tour", pocket::Look::dark }, start { "Start making", pocket::Look::amber };
    float angle = 0.0f, bloom = 0.0f, opened = 0.0f;
    juce::String continueName, continueMeta;
    std::vector<juce::Colour> continueLayers;
    bool pressingContinue = false;
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
