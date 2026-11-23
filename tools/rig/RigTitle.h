#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The Mac's opening title, the same as the phone's (Pocket's TitleScreen): a dark stage where Kaleidobox's kaleidoscope
// blooms in (core/ui/Kaleidoscope.h: one petal for each tool, in its colour), the name rising beside it, and the ways
// in: PICK UP WHERE YOU LEFT OFF (the song you had open), START MAKING (Return), TAKE THE 2-MINUTE TOUR, and the other
// recent songs. A click anywhere else, or Esc, goes straight in. Shown when the app opens (not in Logic).
class RigTitle : public juce::Component, private juce::Timer
{
public:
    RigTitle();
    ~RigTitle() override;

    std::function<void()> onStart, onTour, onContinue;
    std::function<void (const juce::File&)> onRecent;
    std::function<float()> micLevel;   // 0..1 since the last call (the petals bloom with the mic)

    // PICK UP WHERE YOU LEFT OFF (no name: the card is hidden), its line and its layers' colours.
    void setContinue (const juce::String& name, const juce::String& meta, std::vector<juce::Colour> layers);
    // ALSO RECENT: up to three other songs (name, file).
    void setRecent (std::vector<std::pair<juce::String, juce::File>>);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;
    void showOpen() { opened = 10.0f; angle = 0.3f; repaint(); }   // the screenshots: fully open

    juce::Rectangle<int> continueCard() const;
    juce::Button& startButton();
    juce::Button& tourButton();
    int recentCount() const { return (int) recent.size(); }
    juce::Rectangle<int> recentChip (int index) const;

private:
    class WayIn;
    void timerCallback() override;
    juce::Rectangle<float> mandalaRect() const;
    juce::Rectangle<int> column() const;

    std::unique_ptr<WayIn> start, tour;
    float angle = 0.0f, bloom = 0.0f, opened = 0.0f;
    juce::String continueName, continueMeta;
    std::vector<juce::Colour> continueLayers;
    std::vector<std::pair<juce::String, juce::File>> recent;
};
