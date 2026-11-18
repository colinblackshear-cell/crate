#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Kaleidobox Pocket's look: a cream panel with raised buttons, dark wells for the LCD and
// the waveform, amber for what's picked, green for play, red for record. Drawn in code so it
// matches the Mac app's vintage panel and Colin's phone design.
#define U8(text) juce::String (juce::CharPointer_UTF8 (text))

namespace pocket
{
namespace col
{
    inline const juce::Colour cream { 0xffefe7d6 }, creamDeep { 0xffe4dac3 }, creamLight { 0xfffaf6ec };
    inline const juce::Colour ink { 0xff2b2418 }, inkDim { 0xff6e6455 }, inkFaint { 0xffa59a85 };
    inline const juce::Colour rule { 0xffcfc4ab };
    inline const juce::Colour amber { 0xfff5b13d }, amberDark { 0xffe59a22 };
    inline const juce::Colour green { 0xff4cd07a }, red { 0xffe8432f }, blue { 0xff4aa8c8 };
    inline const juce::Colour well { 0xff1a1613 }, wellEdge { 0xff2c2620 };
    inline const juce::Colour lcdText { 0xfff0795f }, lcdAmber { 0xfff5b13d };
    inline const juce::Colour grey { 0xff9a917f };
}

juce::Font fraunces (float height);                    // wordmark and sheet titles
juce::Font condensed (float height);                   // labels and buttons (Barlow Condensed)
juce::Font condensedBold (float height);
juce::Font lcdFont (float height);                     // VT323
juce::Font small (float height);                       // Barlow, small text
juce::Font spaced (juce::Font, float kerning = 0.06f);

enum class Look { cream, amber, green, red, blue, dark, ghost, pad };

void drawRaised (juce::Graphics&, juce::Rectangle<float>, Look, bool pressed, float radius = 12.0f);
void drawWell (juce::Graphics&, juce::Rectangle<float>, float radius = 14.0f);
void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float value01, juce::Colour arc, bool active);

// A raised button, with text or a drawn glyph. Always at least as big as the bounds it's given.
class PButton : public juce::Component
{
public:
    explicit PButton (juce::String text = {}, Look look = Look::cream) : label (std::move (text)), style (look) {}

    void setText (juce::String t) { label = std::move (t); repaint(); }
    void setLook (Look l) { style = l; repaint(); }
    void setLamp (bool shown, bool lit) { lamp = shown; lampLit = lit; repaint(); }
    void setFontHeight (float h) { fontHeight = h; repaint(); }
    void setLeftAligned (bool b) { leftAligned = b; repaint(); }
    void setDetail (juce::String t) { detail = std::move (t); repaint(); }   // dimmer text on the right
    void setPaintInset (float y) { inset = y; repaint(); }   // paint smaller than the touch area
    void setToggled (bool on) { toggled = on; repaint(); }
    bool isToggled() const { return toggled; }
    std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> glyph;
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override { down = true; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { down = false; repaint(); }

private:
    juce::String label, detail;
    Look style;
    bool down = false, lamp = false, lampLit = false, leftAligned = false, toggled = false;
    float fontHeight = 15.0f, inset = 0.0f;
};

// One effect knob: drag up or down to turn, double-tap to put it back.
class KnobView : public juce::Component
{
public:
    KnobView (juce::String name, juce::Colour arc, double min, double max, double start, double skewCentre,
              std::function<juce::String (double)> format, std::function<bool (double)> isActive);

    double getValue() const { return value; }
    void setValue (double v, bool notify = false);
    bool active() const { return isActive (value); }
    void reset() { setValue (start, true); }

    std::function<void()> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override { reset(); }

private:
    float normalised() const;
    void setNormalised (float n);

    juce::String name;
    juce::Colour arc;
    double min, max, start, skewCentre, value;
    double dragStart = 0;
    std::function<juce::String (double)> format;
    std::function<bool (double)> isActive;
};
}
