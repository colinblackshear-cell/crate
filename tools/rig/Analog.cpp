#include "Analog.h"

namespace analog
{
namespace
{
constexpr float kStart = juce::MathConstants<float>::pi * 1.25f, kSweep = juce::MathConstants<float>::pi * 1.5f;

// The knob drawn by the Slider's own look, so dragging feels like the rest of the Rig.
struct KnobLook : juce::LookAndFeel_V4
{
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider& s) override
    {
        const int steps = (int) s.getProperties()["steps"];
        drawKnob (g, juce::Rectangle<int> (x, y, w, h).toFloat(), pos, s.findColour (juce::Slider::rotarySliderFillColourId), steps);
    }
};

KnobLook& knobLook()
{
    static KnobLook look;
    return look;
}
}

juce::Font legendFont (float size, vintage::Weight weight) { return vintage::cond (size, weight, 0.1f); }

void drawPanel (juce::Graphics& g, juce::Rectangle<int> r)
{
    const auto f = r.toFloat();
    g.setGradientFill (juce::ColourGradient (colours::panel, f.getX(), f.getY(), colours::panelDark, f.getX(), f.getBottom(), false));
    g.fillRect (r);
    // A fine brushed grain.
    juce::Random grain (7);
    for (int y = r.getY(); y < r.getBottom(); y += 3)
    {
        g.setColour (juce::Colours::white.withAlpha (0.008f + grain.nextFloat() * 0.012f));
        g.fillRect (r.getX(), y, r.getWidth(), 1);
    }
    // Screws in the corners.
    for (auto c : { juce::Point<float> (f.getX() + 12, f.getY() + 12), { f.getRight() - 12, f.getY() + 12 },
                    { f.getX() + 12, f.getBottom() - 12 }, { f.getRight() - 12, f.getBottom() - 12 } })
    {
        g.setGradientFill (juce::ColourGradient (colours::metal1, c.x - 3, c.y - 3, colours::metal2.darker (0.5f), c.x + 4, c.y + 4, false));
        g.fillEllipse (c.x - 5, c.y - 5, 10, 10);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawLine (c.x - 3.5f, c.y + 1.5f, c.x + 3.5f, c.y - 1.5f, 1.4f);
    }
}

void drawNameplate (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& model, const juce::String& title, juce::Colour accent)
{
    const auto f = r.toFloat();
    juce::ColourGradient metal (colours::metal1, f.getX(), f.getY(), colours::metal2, f.getX(), f.getBottom(), false);
    metal.addColour (0.5, juce::Colour (0xffc4c0b8));
    g.setGradientFill (metal);
    g.fillRoundedRectangle (f, 4.0f);
    juce::Random grain (3);
    for (float x = f.getX(); x < f.getRight(); x += 2.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (grain.nextFloat() * 0.035f));
        g.fillRect (x, f.getY(), 1.0f, f.getHeight());
    }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (f.reduced (0.5f), 4.0f, 1.0f);

    // The stripes on the right: the track's colour and two warmer ones.
    auto stripes = f.withTrimmedLeft (f.getWidth() - 120).reduced (0, f.getHeight() * 0.3f);
    const juce::Colour bands[] = { accent, accent.withRotatedHue (-0.05f).darker (0.1f), juce::Colour (0xffd8452f) };
    const float h = stripes.getHeight() / 3.0f;
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (bands[i]);
        g.fillRect (stripes.getX(), stripes.getY() + h * (float) i + 1.0f, stripes.getWidth(), h - 2.0f);
    }

    auto text = r.reduced (14, 0);
    g.setColour (juce::Colour (0xff1c1a17));
    g.setFont (vintage::wordmark (f.getHeight() * 0.62f));
    const int modelWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), model)) + 14;
    g.drawText (model, text.removeFromLeft (modelWidth), juce::Justification::centredLeft);
    g.setFont (legendFont (f.getHeight() * 0.32f, vintage::Weight::extra));
    g.setColour (juce::Colour (0xff3a3632));
    g.drawText (title.toUpperCase(), text.withTrimmedRight (130), juce::Justification::centredLeft);
}

void drawSection (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title, juce::Colour colour)
{
    const auto f = r.toFloat().reduced (0.5f);
    g.setColour (colours::line);
    g.drawRoundedRectangle (f.withTrimmedTop (9.0f), 5.0f, 1.2f);
    // The title tab.
    g.setFont (legendFont (13.0f, vintage::Weight::extra));
    const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title.toUpperCase()) + 22.0f;
    auto tab = juce::Rectangle<float> (f.getX() + 12.0f, f.getY(), w, 18.0f);
    g.setColour (colours::panel);
    g.fillRect (tab.expanded (3.0f, 0));
    g.setColour (colour);
    g.fillRoundedRectangle (tab, 3.0f);
    g.setColour (colour.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1b1916) : colours::legend);
    g.drawText (title.toUpperCase(), tab, juce::Justification::centred);
}

void drawLed (juce::Graphics& g, juce::Point<float> c, bool on, float d, juce::Colour colour)
{
    const auto r = juce::Rectangle<float> (d, d).withCentre (c);
    if (on)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillEllipse (r.expanded (d * 0.45f));
        g.setGradientFill (juce::ColourGradient (colour.brighter (0.8f), c.x - d * 0.2f, c.y - d * 0.25f, colour, c.x + d * 0.4f, c.y + d * 0.4f, true));
    }
    else
    {
        g.setColour (colour.darker (2.2f).withAlpha (0.9f));
    }
    g.fillEllipse (r);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (r, 1.0f);
}

void drawKnob (juce::Graphics& g, juce::Rectangle<float> area, float v, juce::Colour accent, int steps)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto r = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
    const auto c = r.getCentre();
    const float outer = size * 0.5f;
    auto at = [&] (float angle, float radius) { return juce::Point<float> (c.x + std::sin (angle) * radius, c.y - std::cos (angle) * radius); };

    // The printed scale: 11 ticks (0-10), or one per step.
    const int ticks = steps > 1 ? steps : 11;
    g.setColour (colours::legendDim);
    for (int i = 0; i < ticks; ++i)
    {
        const float a = kStart + kSweep * (float) i / (float) (ticks - 1);
        const bool major = steps > 1 || i % 5 == 0;
        g.drawLine (juce::Line<float> (at (a, outer * (major ? 0.8f : 0.86f)), at (a, outer * 0.98f)), major ? 1.6f : 1.0f);
    }

    // The skirt: black, fluted, with a lit line where it points.
    const auto skirt = r.reduced (size * 0.17f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (skirt.translated (0, size * 0.05f).expanded (1.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3c3934), skirt.getCentreX(), skirt.getY(), juce::Colour (0xff0b0a09),
                                             skirt.getCentreX(), skirt.getBottom(), false));
    g.fillEllipse (skirt);
    const float sr = skirt.getWidth() * 0.5f;
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    for (int a = 0; a < 360; a += 15)
    {
        const float rad = juce::degreesToRadians ((float) a);
        g.drawLine (juce::Line<float> (at (rad, sr * 0.8f), at (rad, sr * 0.99f)), juce::jmax (1.0f, size * 0.02f));
    }

    // The metal cap, and its pointer in the track's colour.
    const auto cap = r.reduced (size * 0.29f);
    const auto hot = cap.getRelativePoint (0.35f, 0.25f);
    juce::ColourGradient capFill (juce::Colour (0xfff4f2ec), hot.x, hot.y, juce::Colour (0xff6f6b64), hot.x + cap.getWidth() * 0.8f,
                                  hot.y + cap.getHeight() * 0.8f, true);
    capFill.addColour (0.5, juce::Colour (0xffb9b5ad));
    g.setGradientFill (capFill);
    g.fillEllipse (cap);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (cap, 1.0f);

    const float angle = kStart + kSweep * v;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawLine (juce::Line<float> (at (angle, sr * 0.15f), at (angle, sr * 0.97f)).withShortenedStart (0), juce::jmax (3.0f, size * 0.07f));
    g.setColour (accent);
    g.drawLine (juce::Line<float> (at (angle, sr * 0.2f), at (angle, sr * 0.95f)), juce::jmax (2.0f, size * 0.045f));
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& n, juce::Colour a,
            const juce::String& lo, const juce::String& hi)
    : name (n), low (lo), high (hi), accent (a)
{
    setUp();
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, id, slider);
    if (auto* p = state.getParameter (id))
    {
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
        const auto range = p->getNormalisableRange();
        if (range.interval >= 1.0f)
            steps = juce::roundToInt ((range.end - range.start) / range.interval) + 1;
        if (steps > 24)
            steps = 0;
        slider.getProperties().set ("steps", steps);
    }
    setTooltip (n + ". Drag up or down (or scroll); double-click for the default. Learn knob puts it on your keyboard.");
}

Knob::Knob (const juce::String& n, juce::Colour a) : name (n), accent (a) { setUp(); }

Knob::~Knob() { slider.setLookAndFeel (nullptr); }

void Knob::setUp()
{
    slider.setLookAndFeel (&knobLook());
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (kStart, kStart + kSweep, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    slider.setMouseDragSensitivity (200);
    slider.onValueChange = [this] { repaint (valueArea); };
    addAndMakeVisible (slider);
}

void Knob::paint (juce::Graphics& g)
{
    g.setFont (legendFont (juce::jlimit (11.0f, 14.0f, (float) getWidth() * 0.19f)));
    g.setColour (colours::legend);
    g.drawFittedText (name.toUpperCase(), nameArea, juce::Justification::centred, 1, 0.8f);
    if (onNameClicked)
    {
        g.setColour (colours::legendDim);
        g.drawLine ((float) nameArea.getCentreX() - 14.0f, (float) nameArea.getBottom() - 1.0f, (float) nameArea.getCentreX() + 14.0f,
                    (float) nameArea.getBottom() - 1.0f, 1.0f);
    }

    if (low.isNotEmpty() || high.isNotEmpty())
    {
        g.setFont (legendFont (10.0f, vintage::Weight::extra));
        g.setColour (colours::legendDim);
        const auto knob = slider.getBounds();
        g.drawText (low, knob.getX() - 30, knob.getBottom() - 12, 34, 12, juce::Justification::centredRight);
        g.drawText (high, knob.getRight() - 4, knob.getBottom() - 12, 34, 12, juce::Justification::centredLeft);
    }

    auto box = valueArea.toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (box, 3.0f);
    vintage::lcdText (g, slider.getTextFromValue (slider.getValue()), box, juce::jmin (19.0f, box.getHeight() * 0.95f));
}

void Knob::resized()
{
    auto r = getLocalBounds();
    nameArea = r.removeFromTop (17);
    valueArea = r.removeFromBottom (17).withSizeKeepingCentre (juce::jmin (r.getWidth() - 4, 70), 17);
    r.removeFromBottom (2);
    const int size = juce::jmin (r.getWidth(), r.getHeight());
    slider.setBounds (r.withSizeKeepingCentre (size, size));
}

void Knob::mouseUp (const juce::MouseEvent& e)
{
    if (onNameClicked && nameArea.contains (e.getPosition()))
        onNameClicked();
}

//==============================================================================
Selector::Selector (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& n, juce::Colour a,
                    juce::StringArray l, int cols)
    : name (n), labels (std::move (l)), accent (a), columns (juce::jmax (1, cols))
{
    parameter = state.getParameter (id);
    if (parameter == nullptr)
        return;
    if (labels.isEmpty())
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
            labels = choice->choices;
    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float v)
                                                              {
                                                                  selected = juce::roundToInt (v);
                                                                  repaint();
                                                              });
    attachment->sendInitialUpdate();
    setTooltip (n + ": press one.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Selector::getSelected() const { return selected; }

int Selector::preferredWidth() const
{
    const auto font = legendFont (12.5f);
    float widest = juce::GlyphArrangement::getStringWidth (legendFont (13.0f), name.toUpperCase());
    float cellWidest = 0;
    for (const auto& l : labels)
        cellWidest = juce::jmax (cellWidest, juce::GlyphArrangement::getStringWidth (font, l.toUpperCase()));
    return juce::roundToInt (juce::jmax (widest, (cellWidest + 22.0f) * (float) columns)) + 8;
}

juce::Rectangle<float> Selector::cell (int i) const
{
    auto area = getLocalBounds().toFloat().withTrimmedTop (17.0f).reduced (2.0f, 0);
    const int rows = (labels.size() + columns - 1) / columns;
    const float h = juce::jmin (20.0f, area.getHeight() / (float) juce::jmax (1, rows));
    const float w = area.getWidth() / (float) columns;
    return { area.getX() + w * (float) (i / rows), area.getY() + h * (float) (i % rows), w, h };
}

void Selector::paint (juce::Graphics& g)
{
    g.setFont (legendFont (juce::jlimit (11.0f, 14.0f, (float) getWidth() * 0.16f)));
    g.setColour (colours::legend);
    g.drawFittedText (name.toUpperCase(), getLocalBounds().removeFromTop (17), juce::Justification::centred, 1, 0.8f);
    g.setFont (legendFont (12.5f));
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto c = cell (i);
        const bool here = i == selected;
        drawLed (g, { c.getX() + 8.0f, c.getCentreY() }, here, juce::jmin (9.0f, c.getHeight() * 0.5f), accent);
        g.setColour (here ? colours::legend : colours::legendDim);
        g.drawFittedText (labels[i].toUpperCase(), c.withTrimmedLeft (17.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
}

void Selector::mouseDown (const juce::MouseEvent& e)
{
    if (attachment == nullptr)
        return;
    for (int i = 0; i < labels.size(); ++i)
        if (cell (i).contains (e.position))
        {
            attachment->setValueAsCompleteGesture ((float) i);
            return;
        }
}

//==============================================================================
Switch::Switch (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& n, juce::Colour a, bool inv)
    : name (n), accent (a), invert (inv)
{
    parameter = state.getParameter (id);
    if (parameter != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float v)
                                                                  {
                                                                      on = (v > 0.5f) != invert;
                                                                      repaint();
                                                                  });
        attachment->sendInitialUpdate();
    }
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

Switch::Switch (const juce::String& n, juce::Colour a) : name (n), accent (a) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

void Switch::setOn (bool o)
{
    on = o;
    repaint();
}

void Switch::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setFont (legendFont (juce::jlimit (11.0f, 13.5f, r.getWidth() * 0.18f)));
    g.setColour (colours::legend);
    g.drawFittedText (name.toUpperCase(), r.removeFromTop (17.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    drawLed (g, { r.getCentreX(), r.getY() + 7.0f }, on, 9.0f, accent);
    r.removeFromTop (15.0f);
    auto cap = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 6.0f, 46.0f), juce::jmin (r.getHeight(), 26.0f));
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (cap.translated (0, down ? 0.5f : 2.0f), 3.0f);
    const auto top = down ? cap.translated (0, 1.5f) : cap;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9e3d4), top.getX(), top.getY(), juce::Colour (0xffa8a090), top.getX(),
                                             top.getBottom(), false));
    g.fillRoundedRectangle (top, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawRoundedRectangle (top, 3.0f, 1.0f);
}

void Switch::mouseDown (const juce::MouseEvent&)
{
    down = true;
    repaint();
}

void Switch::mouseUp (const juce::MouseEvent& e)
{
    down = false;
    if (! getLocalBounds().contains (e.getPosition()))
    {
        repaint();
        return;
    }
    if (attachment != nullptr)
        attachment->setValueAsCompleteGesture ((! on) != invert ? 1.0f : 0.0f);
    else
        setOn (! on);
    if (onClick)
        onClick();
    repaint();
}

//==============================================================================
Button::Button (const juce::String& text, juce::Colour a) : juce::Button (text), accent (a)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Button::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = lit || getToggleState();
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0, down ? 0.5f : 1.5f), 3.0f);
    const auto top = down ? r.translated (0, 1.0f) : r;
    const auto face = on ? accent : juce::Colour (0xff3a3632);
    g.setGradientFill (juce::ColourGradient (face.brighter (over ? 0.25f : 0.12f), top.getX(), top.getY(), face.darker (0.25f), top.getX(),
                                             top.getBottom(), false));
    g.fillRoundedRectangle (top, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (top, 3.0f, 1.0f);
    g.setColour (on && accent.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff1b1916) : colours::legend);
    g.setFont (legendFont (juce::jlimit (11.0f, 14.5f, top.getHeight() * 0.5f), vintage::Weight::extra));
    g.drawFittedText (getButtonText().toUpperCase(), top.reduced (4.0f, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

//==============================================================================
Keys::Keys (juce::MidiKeyboardState& state, juce::Colour a) : juce::MidiKeyboardComponent (state, horizontalKeyboard), accent (a)
{
    setColour (keySeparatorLineColourId, juce::Colour (0xff6b6253));
    setColour (shadowColourId, juce::Colours::black.withAlpha (0.35f));
    setColour (upDownButtonBackgroundColourId, juce::Colour (0xff2a2622));
    setColour (upDownButtonArrowColourId, colours::legend);
    setColour (textLabelColourId, juce::Colour (0xff6b6253));
}

void Keys::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour, juce::Colour)
{
    auto key = area.reduced (0.6f, 0).withTrimmedBottom (1.0f);
    juce::ColourGradient ivory (juce::Colour (0xfff6efdf), key.getX(), key.getY(), juce::Colour (0xffd8ccb2), key.getX(), key.getBottom(), false);
    g.setGradientFill (ivory);
    g.fillRoundedRectangle (key, 2.0f);
    if (down || over)
    {
        g.setColour (accent.withAlpha (down ? 0.7f : 0.18f));
        g.fillRoundedRectangle (key, 2.0f);
    }
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRect (key.removeFromBottom (3.0f));
    if (note % 12 == 0)
    {
        g.setColour (juce::Colour (0xff6b6253));
        g.setFont (legendFont (10.0f));
        g.drawText ("C" + juce::String (note / 12 - 2), area.removeFromBottom (16.0f), juce::Justification::centred);
    }
}

void Keys::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool down, bool over, juce::Colour)
{
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (area, 2.0f);
    auto top = area.reduced (area.getWidth() * 0.12f, 0).withTrimmedBottom (area.getHeight() * 0.1f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3934), top.getX(), top.getY(), juce::Colour (0xff141210), top.getX(),
                                             top.getBottom(), false));
    g.fillRoundedRectangle (top, 1.5f);
    if (down || over)
    {
        g.setColour (accent.withAlpha (down ? 0.85f : 0.25f));
        g.fillRoundedRectangle (top, 1.5f);
    }
}
}
