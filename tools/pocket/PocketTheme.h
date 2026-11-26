#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Kaleidobox Pocket's look: the Kaleidobox night sky. A deep plum page with raised violet-grey
// buttons, still-darker wells with a brass rim for the LCD and the waveform, gold for what's
// picked, teal for play, magenta for record, violet for the alternate. Light text on the page,
// dark text (col::onBright) on anything bright. Drawn in code to match the app icon.
#define U8(text) juce::String (juce::CharPointer_UTF8 (text))

namespace pocket
{
namespace col
{
    // The names keep their old "cream" spelling so every screen picks up the new look: cream is the page,
    // creamLight the raised chip, creamDeep the sunken track. ink is LIGHT text on the page and panels;
    // onBright is dark text/glyphs for anything sitting on gold, teal, violet, orange or the pale buttons.
    inline const juce::Colour cream { 0xff17131f }, creamDeep { 0xff0f0c15 }, creamLight { 0xff2e2540 };
    inline const juce::Colour ink { 0xfff3eadb }, inkDim { 0xffb7a9c2 }, inkFaint { 0xff8e7fa0 };
    inline const juce::Colour onBright { 0xff0f0c15 };
    inline const juce::Colour rule { 0xff3a2f4f };
    inline const juce::Colour amber { 0xfff7c04a }, amberDark { 0xffd9a233 };
    inline const juce::Colour green { 0xff2fc6a8 }, red { 0xffec4e8a }, blue { 0xffa877f5 };
    inline const juce::Colour well { 0xff0f0c15 }, wellEdge { 0xff8e7433 }, wellCard { 0xff221b2d };
    inline const juce::Colour brass { 0xffc9a54e };
    inline const juce::Colour lcdText { 0xfff59a2a }, lcdAmber { 0xfff7c04a };
    inline const juce::Colour grey { 0xff8e7fa0 };
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
void drawKnob (juce::Graphics&, juce::Rectangle<float> bounds, float value01, juce::Colour arc, bool active, bool onDark = false);

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
