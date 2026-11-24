#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>

#include "VisualsRenderer.h"

// Kaleidobox Visuals on screen: the picture, full size, with the same few controls on the phone
// and the Mac. Tap the picture to show or hide them; swipe left or right for the next look.
//   top:    X (close), the look's name, Drift (a new look every 8-bar phrase)
//   bottom: the five looks; WILD, TRAILS and COLOURS
// COLOURS opens the colour sets and, for the note colours, the 12 keys: tap a key to give its note
// your own colour. A picture with no controls (the Mac's second screen) is only the picture.
namespace crate::visuals
{
class VisualsView : public juce::Component,
                    private juce::Timer
{
public:
    struct Options
    {
        bool controls = true;   // false: only the picture (a second screen)
        bool phone = false;     // thumb-sized controls, the safe area kept clear
        std::function<juce::Font (float)> font;   // the app's Barlow Condensed
    };
    VisualsView (VisualTap&, VisualState&, Options);
    ~VisualsView() override;

    std::function<Clock()> clock;           // the song's clock (message thread)
    std::function<void()> onClose;          // X
    std::function<void()> onChanged;        // a pick changed: save it

    // The phone's notch and home bar (the picture runs under them; the controls don't).
    void setSafeInsets (juce::BorderSize<int> b) { insets = b; resized(); }
    void showControls (bool);
    bool controlsShown() const { return controlsOn; }
    // Picks a look (0..4) by hand: Drift stops, as when you pick one on screen.
    void pickLook (int);
    void nextLook (int direction);
    void toggleDrift();
    bool coloursOpen() const;
    bool drawing() const { return context.isAttached(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override { attachIfShowing(); }
    void parentHierarchyChanged() override { attachIfShowing(); }

    // For the tests: the controls, by their label.
    juce::Component* control (const juce::String& name);

    class Chip;
    class ColoursPanel;

private:
    void timerCallback() override;
    void attachIfShowing();
    void refresh();
    void changed();
    juce::Font font (float h) const;

    VisualTap& tap;
    VisualState& state;
    Options options;
    juce::OpenGLContext context;
    VisualsRenderer renderer;
    bool listening = false, controlsOn = true;
    juce::BorderSize<int> insets;
    juce::Point<float> downAt;
    juce::uint32 downTime = 0;

    std::unique_ptr<Chip> close, name, drift, wild, trails, colours;
    std::array<std::unique_ptr<Chip>, kLooks> looks;
    std::unique_ptr<ColoursPanel> panel;

    // What the picture last drew, for the controls (written by the picture's thread).
    std::atomic<int> drawnLook { -1 };
    std::array<std::atomic<float>, 12> drawnNotes {};
    std::atomic<bool> drawnAny { false };
    int shownStamp = -1, snapshotTicks = 0;
};
} // namespace crate::visuals
