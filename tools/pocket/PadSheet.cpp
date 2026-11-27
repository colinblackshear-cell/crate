#include "PadSheet.h"

using namespace pocket;

PadSheet::PadSheet (Kit& k) : kit (k)
{
    auto add = [this] (const char* name, double lo, double hi, double start, std::function<juce::String (double)> fmt, std::function<void (double)> apply)
    {
        auto* knob = new KnobView (name, col::amber, lo, hi, start, 0, std::move (fmt), [] (double) { return true; });
        knob->onChange = [this, apply, knob]
        {
            if (kit.pads[(size_t) padIndex].empty())
                return;
            apply (knob->getValue());
            if (onChanged)
                onChanged (padIndex);
        };
        knobs.add (knob);
        addAndMakeVisible (knob);
    };
    add ("VOLUME", 0, 100, 100, [] (double v) { return juce::String ((int) std::round (v)); },
         [this] (double v) { kit.pads[(size_t) padIndex].volume = (int) std::round (v); });
    add ("PAN", 0, 100, 50, [] (double v)
         {
             const int p = (int) std::round (v) - 50;
             return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p));
         },
         [this] (double v) { kit.pads[(size_t) padIndex].pan = (int) std::round (v); });

    // PITCH, first and big: whole semitones (a finer tune from the Mac or a set is kept until the pitch is moved)
    pitch.onChange = [this] (int st) { setPitch (st); };
    addAndMakeVisible (pitch);
    for (auto* b : { &down, &up })
    {
        b->setFontHeight (24.0f);
        addAndMakeVisible (b);
    }
    down.onClick = [this] { setPitch (pitch.getValue() - 1); };
    up.onClick = [this] { setPitch (pitch.getValue() + 1); };
    keys.setFontHeight (17.0f);
    more.setFontHeight (17.0f);
    keys.onClick = [this] { if (! kit.pads[(size_t) padIndex].empty() && onPlayAsKeys) onPlayAsKeys (padIndex); };
    more.onClick = [this] { if (onMore) onMore (padIndex); };
    addAndMakeVisible (keys);
    addAndMakeVisible (more);

    const char* names[] = { "One shot", "Mono", "Note off" };
    for (int i = 0; i < 3; ++i)
    {
        modes[i].setText (names[i]);
        modes[i].setFontHeight (17.0f);
        modes[i].setLook (Look::ghost);
        modes[i].onClick = [this, i] { setMode ((PadMode) i); };
        addAndMakeVisible (modes[i]);
    }

    chop.setFontHeight (17.0f);
    trim.setFontHeight (17.0f);
    clear.setFontHeight (17.0f);
    done.setFontHeight (16.0f);
    chop.onClick = [this] { if (onChop) onChop (padIndex); };
    trim.onClick = [this] { if (onTrim) onTrim (padIndex); };
    done.onClick = [this] { if (onDone) onDone(); };
    clear.onClick = [this]
    {
        if (hasCleared)
        {
            kit.pads[(size_t) padIndex] = cleared;
            hasCleared = false;
            refresh();
            if (onUndoClear)
                onUndoClear (padIndex);
        }
        else if (! kit.pads[(size_t) padIndex].empty())
        {
            cleared = kit.pads[(size_t) padIndex];
            hasCleared = true;
            kit.clear (padIndex);
            refresh();
            if (onClear)
                onClear (padIndex);
        }
    };
    for (auto* b : { &chop, &trim, &clear, &done })
        addAndMakeVisible (b);
}

void PadSheet::show (int pad)
{
    padIndex = pad;
    hasCleared = false;
    refresh();
}

void PadSheet::setPitch (int st)
{
    auto& p = kit.pads[(size_t) padIndex];
    if (p.empty())
        return;
    st = juce::jlimit (-12, 12, st);
    p.tune = st * 10;
    pitch.setValue (st);
    if (onChanged)
        onChanged (padIndex);
}

void PadSheet::setMode (PadMode m)
{
    auto& p = kit.pads[(size_t) padIndex];
    if (p.empty())
        return;
    p.mode = m;
    refresh();
    if (onChanged)
        onChanged (padIndex);
}

void PadSheet::refresh()
{
    auto& p = kit.pads[(size_t) padIndex];
    knobs[0]->setValue (p.volume);
    knobs[1]->setValue (p.pan);
    pitch.setValue (juce::roundToInt (p.tune / 10.0));
    for (auto* b : { &down, &up, &keys })
        b->setEnabled (! p.empty());
    for (int i = 0; i < 3; ++i)
        modes[i].setLook (! p.empty() && (int) p.mode == i ? Look::amber : Look::ghost);
    clear.setText (hasCleared ? "Undo" : "Clear");
    chop.setEnabled (! p.empty());
    repaint();
}

void PadSheet::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::Path p;
    p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight() + 40, 28.0f);
    g.setColour (juce::Colour (0x40000000));
    g.fillPath (p, juce::AffineTransform::translation (0, -3));
    g.setColour (col::cream);
    g.fillPath (p);

    pocket::drawGrabber (g, getWidth());

    auto& pad = kit.pads[(size_t) padIndex];
    const juce::String title = "Pad " + Kit::padLabel (padIndex);
    g.setColour (col::ink);
    auto titleFont = fraunces (28.0f);
    g.setFont (titleFont);
    g.drawText (title, 20, 22, 150, 36, juce::Justification::centredLeft);
    const int titleW = (int) juce::GlyphArrangement::getStringWidth (titleFont, title) + 10;

    g.setColour (col::inkDim);
    g.setFont (small (15.0f));
    g.drawText (pad.empty() ? juce::String (U8("empty")) : pad.name + U8(" · ") + Kit::lengthText (pad.seconds()),
                juce::Rectangle<int> (20 + titleW, 24, getWidth() - 20 - titleW - 100, 34), juce::Justification::centredLeft);
}

void PadSheet::resized()
{
    const int w = getWidth();
    done.setBounds (w - 16 - 76, 20, 76, 44);

    // PITCH: - [strip] +
    down.setBounds (16, 88, 48, 48);
    up.setBounds (w - 16 - 48, 88, 48, 48);
    pitch.setBounds (16 + 48 + 6, 66, w - 2 * (16 + 48 + 6), 72);

    const int colW = (w - 40) / 2;
    for (int i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (20 + i * colW, 146, colW, 96);

    const int each = (w - 32) / 3;
    for (int i = 0; i < 3; ++i)
        modes[i].setBounds (16 + i * each, 250, each - 6, 44);

    const int half = (w - 32 - 6) / 2;
    keys.setBounds (16, 302, half, 46);
    trim.setBounds (16 + half + 6, 302, half, 46);
    clear.setBounds (w - 16 - 72, 356, 72, 46);
    more.setBounds (clear.getX() - 6 - 96, 356, 96, 46);
    chop.setBounds (16, 356, more.getX() - 6 - 16, 46);
}
