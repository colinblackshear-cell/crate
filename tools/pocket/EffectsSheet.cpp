#include "EffectsSheet.h"

using namespace pocket;

namespace
{
juce::String hz (double v) { return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz" : juce::String ((int) std::round (v)) + " Hz"; }
juce::String pct (double v) { return juce::String ((int) std::round (v * 100.0)) + "%"; }
}

EffectsSheet::EffectsSheet()
{
    using K = FxKnobs;
    auto add = [this] (const char* name, juce::uint32 colour, double lo, double hi, double start, double centre,
                       std::function<juce::String (double)> fmt, int index)
    {
        auto* k = new KnobView (name, juce::Colour (colour), lo, hi, start, centre, std::move (fmt),
                                [index] (double v) { return K::isOn (index, v); });
        k->onChange = [this] { updateCount(); if (onKnobsChange) onKnobsChange(); };
        knobs.add (k);
        addAndMakeVisible (k);
    };

    add ("PITCH", 0xfff0c030, -12, 12, 0, 0, [] (double v) { return juce::String ((int) std::round (v)) + " st"; }, K::pitch);
    add ("LOW CUT", 0xff4aa8c8, 20, 2000, 20, 200, [] (double v) { return K::isOn (K::lowCut, v) ? hz (v) : juce::String ("Off"); }, K::lowCut);
    add ("HIGH CUT", 0xff4aa8c8, 1000, 20000, 20000, 6000, [] (double v) { return K::isOn (K::highCut, v) ? hz (v) : juce::String ("Off"); }, K::highCut);
    add ("RESO", 0xfff0c030, 0, 1, 0, 0, pct, K::reso);
    add ("DRIVE", 0xffe5533a, 0, 1, 0, 0, pct, K::drive);
    add ("BITS", 0xffb48ad8, 4, 16, 16, 0, [] (double v) { return K::isOn (K::bits, v) ? juce::String ((int) std::round (v)) : juce::String ("Off"); }, K::bits);
    add ("RATE", 0xffb48ad8, 2000, 44100, 44100, 12000, [] (double v) { return K::isOn (K::rate, v) ? hz (v) : juce::String ("Off"); }, K::rate);
    add ("DELAY", 0xff8cc070, 0, 1, 0, 0, [] (double v) { return v > 0.01 ? pct (v) : juce::String ("Off"); }, K::delay);
    add ("REVERB", 0xff8cc070, 0, 1, 0, 0, pct, K::reverb);

    hear.setLamp (true, true);
    hear.setToggled (true);
    hear.setFontHeight (16.0f);
    hear.onClick = [this]
    {
        hear.setToggled (! hear.isToggled());
        hear.setLamp (true, hear.isToggled());
        if (onHearChanged)
            onHearChanged();
    };
    reset.onClick = [this] { resetKnobs(); if (onKnobsChange) onKnobsChange(); };
    apply.onClick = [this] { if (onApply) onApply(); };
    findHits.onClick = [this] { if (onFindHits) onFindHits(); };
    clear.onClick = [this] { if (onClear) onClear(); };
    done.onClick = [this] { if (onDone) onDone(); };
    even.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Even 4");
        m.addItem (2, "Even 8");
        m.addItem (3, "Even 16");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&even).withMinimumWidth (110),
                         [this] (int r) { if (r > 0 && onEven) onEven (r == 1 ? 4 : r == 2 ? 8 : 16); });
    };
    even.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour ink)
    {
        g.setColour (ink);
        g.setFont (spaced (condensed (16.0f)));
        g.drawText ("EVEN", r.withTrimmedRight (22), juce::Justification::centred);
        juce::Path t;
        t.addTriangle (r.getRight() - 26, r.getCentreY() - 3, r.getRight() - 16, r.getCentreY() - 3, r.getRight() - 21, r.getCentreY() + 3);
        g.fillPath (t);
    };
    for (auto* b : { &hear, &reset, &apply, &findHits, &even, &clear, &done })
        addAndMakeVisible (b);
    for (auto* b : { &reset, &apply, &findHits, &even, &clear })
        b->setFontHeight (16.0f);
    done.setFontHeight (16.0f);
    clear.setFontHeight (16.0f);
    even.setFontHeight (16.0f);

    slider.onChange = [this] { sens = slider.value; repaint(); };
    addAndMakeVisible (slider);

    chopWave.setMode (PocketWave::Mode::markers);
    chopWave.setCompact (true);
    chopWave.setEmptyText ("Record or pick a sound to chop it");
    addAndMakeVisible (chopWave);
}

void EffectsSheet::updateCount()
{
    onCount = getKnobs().onCount();
    repaint (0, 0, getWidth(), 70);
}

void EffectsSheet::setKnobs (const FxKnobs& k)
{
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setValue (k.v[i]);
    updateCount();
}

FxKnobs EffectsSheet::getKnobs() const
{
    FxKnobs k;
    for (int i = 0; i < knobs.size(); ++i)
        k.v[i] = knobs[i]->getValue();
    return k;
}

void EffectsSheet::resetKnobs()
{
    setKnobs (FxKnobs());
}

void EffectsSheet::Slider01::set (const juce::MouseEvent& e)
{
    const float a = 14.0f, w = (float) getWidth() - 28.0f;
    value = juce::jlimit (0, 100, (int) std::round ((e.position.x - a) / w * 100.0f));
    repaint();
    if (onChange)
        onChange();
}

void EffectsSheet::Slider01::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float a = 14.0f, w = r.getWidth() - 28.0f, cy = r.getCentreY();
    g.setColour (juce::Colour (0xffcfc4ab));
    g.fillRoundedRectangle (a, cy - 4, w, 8, 4);
    const float x = a + w * (float) value / 100.0f;
    g.setColour (col::amber);
    g.fillRoundedRectangle (a, cy - 4, x - a, 8, 4);
    g.setColour (juce::Colour (0xffa89b82));
    g.fillEllipse (x - 14, cy - 12, 28, 28);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffaf5e8), x, cy - 14, juce::Colour (0xffd8ccb2), x, cy + 14, false));
    g.fillEllipse (x - 14, cy - 14, 28, 28);
    g.setColour (juce::Colour (0x33000000));
    g.drawEllipse (x - 14, cy - 14, 28, 28, 1.0f);
}

void EffectsSheet::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 40, 28.0f);
    g.setColour (juce::Colour (0x40000000));
    g.fillPath (p, juce::AffineTransform::translation (0, -3));
    g.setColour (col::cream);
    g.fillPath (p);

    pocket::drawGrabber (g, getWidth());

    g.setColour (col::ink);
    g.setFont (fraunces (28.0f));
    g.drawText ("Effects", 20, 28, 160, 36, juce::Justification::centredLeft);
    g.setFont (fraunces (28.0f));
    g.drawText ("Chop", 20, 428, 160, 36, juce::Justification::centredLeft);

    g.setColour (col::inkDim);
    g.setFont (spaced (condensed (12.0f), 0.12f));
    g.drawText (juce::String (onCount) + " ON", getWidth() - 160, 28, 78, 36, juce::Justification::centredRight);
    g.drawText ("TAP THE WAVE TO ADD A MARKER", getWidth() - 260, 428, 244, 36, juce::Justification::centredRight);

    g.setColour (col::rule);
    g.fillRect (16.0f, 424.0f, r.getWidth() - 32.0f, 1.5f);

    g.setColour (col::ink);
    g.setFont (spaced (condensed (14.0f), 0.1f));
    g.drawText ("SENSITIVITY", 16, 520, 100, 36, juce::Justification::centredLeft);
    g.setFont (lcdFont (24.0f));
    g.drawText (juce::String (sens), getWidth() - 56, 520, 40, 36, juce::Justification::centredRight);
}

void EffectsSheet::resized()
{
    const int w = getWidth(), h = getHeight();
    done.setBounds (w - 16 - 76, 26, 76, 44);

    const int colW = (w - 40) / 3;
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (20 + (i % 3) * colW, 68 + (i / 3) * 104, colW, 100);

    hear.setBounds (16, 378, 150, 44);
    apply.setBounds (w - 16 - 84, 378, 84, 44);
    reset.setBounds (apply.getX() - 8 - 76, 378, 76, 44);

    findHits.setBounds (16, 472, 104, 44);
    even.setBounds (findHits.getRight() + 8, 472, 84, 44);
    clear.setBounds (w - 16 - 72, 472, 72, 44);

    slider.setBounds (112, 520, w - 112 - 60, 36);

    const int bottom = h - 34 - 4;   // keep clear of the home indicator
    chopWave.setBounds (16, 566, w - 32, juce::jmax (60, bottom - 566));
}
