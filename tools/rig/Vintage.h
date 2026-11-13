#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// The Rig's look: a 70s/80s piece of studio gear. A cream panel between walnut cheeks, chunky
// cream buttons that light up, recessed dark wells, amber LCDs, lamps and knobs with a coloured
// value ring. One layout, two themes: Studio (cream, for home and daylight) and Stage (black
// panel, for dark rooms).
//
// Every state is a lit lamp plus a word, and the colours differ in brightness too, so they read
// from across a stage and for colour-blind players:
//   green = playing, red = armed (records on the next bar), amber = next (queued) or selected,
//   blue = muted, dark = off or empty.
namespace vintage
{
struct Palette
{
    juce::Colour face, titleBar, ink, ink2, engrave, well, wellInk, wellInk2, btn1, btn2, btnInk, wood1, wood2, line, ringOff;
};

const Palette& studio();
const Palette& stage();
// The theme the Rig shows (Studio unless switched to Stage).
const Palette& pal();
bool isStage();
void setStage (bool);

// The track colours (the same on every screen).
namespace colours
{
    inline const juce::Colour drums { 0xffe85a3a }, bass { 0xfff39b33 }, synth { 0xffefcb45 }, vocoder { 0xff5fb3c4 },
                              loops { 0xff9cc27a }, sampler { 0xffc58fd6 }, input { 0xffd8c49a }, master { 0xfff1e7d1 };
    inline const juce::Colour play { 0xff57e389 }, arm { 0xffe8432f }, next { 0xffffb23e }, mute { 0xff5fb3e8 },
                              selected { 0xffffc35a }, lcdAmber { 0xffffb54a }, lcdGreen { 0xff7cf29a }, lcdBack { 0xff140f06 };
}

// The fonts: Barlow Condensed for labels and buttons, Barlow for small print, VT323 for the
// LCDs (anything that changes: tempo, bar, values), Fraunces only for the wordmark.
// Sizes are CSS pixels, as in the design.
enum class Weight { semi, bold, extra };
juce::Font cond (float size, Weight = Weight::bold, float tracking = 0.0f);
juce::Font barlow (float size);
juce::Font lcd (float size);
juce::Font wordmark (float size);

// An engraved label: Barlow Condensed bold, upper case, tracked out.
void engraved (juce::Graphics&, const juce::String&, juce::Rectangle<int>, juce::Justification = juce::Justification::centredLeft,
               float size = 13.0f, bool inWell = false, juce::Colour = {});

enum class Lit { none, play, arm, next, mute, current };
juce::Colour litColour (Lit);

// A soft glow around a rounded rectangle (cheap: a few rings).
void glow (juce::Graphics&, juce::Rectangle<float>, float radius, juce::Colour, float size = 12.0f, float strength = 0.5f);

// The parts.
void drawFace (juce::Graphics&, juce::Rectangle<int>);
void drawCheek (juce::Graphics&, juce::Rectangle<int>);
void drawWell (juce::Graphics&, juce::Rectangle<float>, float radius = 12.0f);
void drawButton (juce::Graphics&, juce::Rectangle<float>, Lit, bool down, bool over, bool inWell, float radius = 9.0f);
void drawLamp (juce::Graphics&, juce::Point<float> centre, Lit, float diameter = 16.0f);
void drawLcd (juce::Graphics&, juce::Rectangle<float>, float radius = 7.0f);
// LCD text with its glow.
void lcdText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float size, juce::Justification = juce::Justification::centred,
              juce::Colour = colours::lcdAmber);
void drawKnob (juce::Graphics&, juce::Rectangle<float>, float value01, juce::Colour ring);
void drawVu (juce::Graphics&, juce::Rectangle<float>, float level01, const juce::String& label);
// A step of a step sequencer: on (lit in the track's colour), off, or under the play head.
void drawStep (juce::Graphics&, juce::Rectangle<float>, bool on, bool playhead, bool altGroup, juce::Colour, float alpha = 1.0f);

// TextButtons and rotary Sliders in the Rig's look. A button can carry two properties:
// "lit" (a Lit, as an int) for its colour when on, and "well" (true) when it sits in a dark well.
// Without "lit", a toggled-on button lights amber.
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    // Vertical sliders are faders, as on the Mixer.
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle,
                           juce::Slider&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
};

void setLit (juce::Button&, Lit);
void setInWell (juce::Button&, bool);

// A value as a person reads it: "35%", "1.2 kHz", "40 ms", "+7 st", a choice's name.
juce::String readable (juce::RangedAudioParameter*, double value);

// A knob for a plugin setting, as on the Groove screen: its name above, an amber LCD with the
// value below.
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, juce::Colour ring);
    // A knob for a setting that isn't a parameter: set its range and onValueChange yourself.
    Knob (const juce::String& name, juce::Colour ring);
    void paint (juce::Graphics&) override;
    void resized() override;

    juce::Slider slider;
    juce::String name;
    bool inWell = false;   // on a dark well: the name in light ink
    float nameSize = 16.0f;
    bool sideways = false;   // the knob on the left, its name and value beside it (for a short row)

private:
    void setUp (juce::Colour ring);
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> nameArea, valueArea;
};
}
