#pragma once

#include "Vintage.h"

#include <juce_audio_utils/juce_audio_utils.h>

// The look of the Rig's own synths, the Bass and the Synth: an old analog synthesizer. A black
// panel between walnut cheeks, a brushed metal nameplate, white legends, sections boxed in thin
// lines with a coloured title tab, big skirted knobs with a 0-10 scale, LED push buttons for the
// choices, and a wooden key bed. (An original design in the spirit of 70s and 80s instruments,
// not a copy of any one.)
//
// Every control is tied to a plugin parameter, so Knob Learn, the show file and Logic all see it.
namespace analog
{
namespace colours
{
    inline const juce::Colour panel { 0xff1b1916 }, panelDark { 0xff121110 }, legend { 0xffefe6d2 }, legendDim { 0xffa79d8a },
                              line { 0x59efe6d2 }, led { 0xffff4a32 }, ledOff { 0xff3a1612 }, metal1 { 0xffd9d6cf }, metal2 { 0xff8f8b84 };
}

juce::Font legendFont (float size, vintage::Weight = vintage::Weight::bold);

void drawPanel (juce::Graphics&, juce::Rectangle<int>);
// The brushed metal strip along the top, with the model name and its stripes.
void drawNameplate (juce::Graphics&, juce::Rectangle<int>, const juce::String& model, const juce::String& title, juce::Colour accent);
// A section's box: thin legend-coloured lines and a coloured tab with its title.
void drawSection (juce::Graphics&, juce::Rectangle<int>, const juce::String& title, juce::Colour);
void drawLed (juce::Graphics&, juce::Point<float> centre, bool on, float diameter = 9.0f, juce::Colour = colours::led);
// A knob with its printed scale. `steps` > 1 draws a tick per step (a stepped switch).
void drawKnob (juce::Graphics&, juce::Rectangle<float>, float value01, juce::Colour accent, int steps = 0);

// A knob for a parameter: its name above, the scale around it, its value in amber below.
// Choice and whole-number parameters click from step to step. `low`/`high` label the ends
// (e.g. SLOW and FAST).
class Knob : public juce::Component,
             public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, juce::Colour accent,
          const juce::String& low = {}, const juce::String& high = {});
    // A knob that isn't a parameter (the plugin shell's macros): set the slider up yourself.
    Knob (const juce::String& name, juce::Colour accent);
    ~Knob() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void setName (const juce::String& n) { name = n; repaint(); }
    juce::String getKnobName() const { return name; }

    juce::Slider slider;
    std::function<void()> onNameClicked;   // the shell's macros: click the name to pick a setting
    void mouseUp (const juce::MouseEvent&) override;

private:
    void setUp();
    juce::String name, low, high;
    juce::Colour accent;
    int steps = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    juce::Rectangle<int> nameArea, valueArea;
};

// A row or column of LED push buttons for a choice parameter: press one, its LED lights.
class Selector : public juce::Component,
                 public juce::SettableTooltipClient
{
public:
    // `labels` replace the parameter's own names when given (shorter ones for the panel).
    Selector (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, juce::Colour accent,
              juce::StringArray labels = {}, int columns = 1);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int getSelected() const;
    // How wide it wants to be.
    int preferredWidth() const;

private:
    juce::Rectangle<float> cell (int i) const;
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String name;
    juce::StringArray labels;
    juce::Colour accent;
    int columns = 1, selected = 0;
};

// An on/off parameter as a square push button with an LED above it.
class Switch : public juce::Component,
               public juce::SettableTooltipClient
{
public:
    Switch (juce::AudioProcessorValueTreeState&, const juce::String& id, const juce::String& name, juce::Colour accent,
            bool invert = false);
    // A switch that isn't a parameter.
    Switch (const juce::String& name, juce::Colour accent);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool isOn() const { return on; }
    void setOn (bool);
    std::function<void()> onClick;   // for one that isn't a parameter

private:
    juce::RangedAudioParameter* parameter = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::String name;
    juce::Colour accent;
    bool on = false, down = false, invert = false;
};

// A plain push button on the panel (Random, Clear, Load...).
class Button : public juce::Button
{
public:
    Button (const juce::String& text, juce::Colour accent = colours::legend);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    bool lit = false;

private:
    juce::Colour accent;
};

// The keys, in a wooden key bed: cream and black keys, the held ones lit.
class Keys : public juce::MidiKeyboardComponent
{
public:
    Keys (juce::MidiKeyboardState&, juce::Colour accent);

protected:
    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour, juce::Colour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float>, bool down, bool over, juce::Colour) override;

private:
    juce::Colour accent;
};
}
