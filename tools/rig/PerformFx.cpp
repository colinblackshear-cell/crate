#include "PerformPad.h"
#include "Vintage.h"

namespace
{
constexpr double kHistorySeconds = 6.0;
const double repeatBeats[] = { 1.0, 0.5, 0.25, 0.125 };
const double gateBeats[] = { 0.5, 0.25, 0.125 };
const double riseBeats[] = { 4.0, 8.0, 16.0 };
}

juce::String PerformFx::modeName (int m)
{
    static const char* names[] = { "Filter", "Repeat", "Tape stop", "Gate", "Riser" };
    return names[juce::jlimit (0, numModes - 1, m)];
}

juce::String PerformFx::axisText (int m, bool xAxis)
{
    switch (m)
    {
        case filter:   return xAxis ? juce::String::fromUTF8 ("\xe2\x86\x90 low-pass \xc2\xb7 high-pass \xe2\x86\x92") : juce::String ("resonance");
        case repeat:   return xAxis ? juce::String::fromUTF8 ("1/4 \xe2\x86\x92 1/32") : juce::String ("how much");
        case tapeStop: return xAxis ? juce::String() : juce::String::fromUTF8 ("slow stop \xe2\x86\x91 \xc2\xb7 quick stop \xe2\x86\x93");
        case gate:     return xAxis ? juce::String::fromUTF8 ("1/8 \xc2\xb7 1/16 \xc2\xb7 1/32") : juce::String ("depth");
        case riser:    return xAxis ? juce::String::fromUTF8 ("1 bar \xc2\xb7 2 bars \xc2\xb7 4 bars") : juce::String ("level");
        default:       return {};
    }
}

void PerformFx::prepare (double sampleRate)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    wet.reset (rate, 0.012);
    xs.reset (rate, 0.03);
    ys.reset (rate, 0.03);
    wet.setCurrentAndTargetValue (0.0f);
    xs.setCurrentAndTargetValue (x.load());
    ys.setCurrentAndTargetValue (y.load());
    history.setSize (2, (int) (rate * kHistorySeconds));
    history.clear();
    writePos = 0;
    for (auto* f : { &filters[0], &filters[1], &thin[0], &thin[1], &noiseFilter })
        f->clear();
    wasEngaged = false;
}

void PerformFx::process (juce::AudioBuffer<float>& mix, double ppq, double bpm, bool playing)
{
    const int n = mix.getNumSamples();
    if (history.getNumSamples() == 0 || mix.getNumChannels() < 2)
        return;
    bpm = juce::jlimit (20.0, 400.0, bpm);
    const double beatsPerSample = bpm / 60.0 / rate;
    double beat = playing ? ppq : freeBeat;

    const bool on = engaged.load();
    const int m = juce::jlimit (0, numModes - 1, mode.load());
    if (m != lastMode)
    {
        // A new mode starts fresh.
        lastMode = m;
        repeatChoice = -1;
        riseStartBeat = -1;
        for (auto& f : filters)
            f.clear();
    }
    wet.setTargetValue (on ? 1.0f : 0.0f);
    xs.setTargetValue (x.load());
    ys.setTargetValue (y.load());

    const int size = history.getNumSamples();
    const bool starting = on && ! wasEngaged;
    wasEngaged = on;
    if (starting)
    {
        repeatChoice = -1;
        tapePos = writePos;
        tapeSpeed = 1.0;
        riseStartBeat = beat;
    }
    if (! on && wet.getCurrentValue() <= 0.0001f && ! wet.isSmoothing())
    {
        // Off: just keep the history going.
        for (int i = 0; i < n; ++i)
        {
            for (int ch = 0; ch < 2; ++ch)
                history.setSample (ch, writePos, mix.getSample (ch, i));
            writePos = (writePos + 1) % size;
        }
        rise = 0;
        if (! playing)
            freeBeat += n * beatsPerSample;
        return;
    }

    auto* left = mix.getWritePointer (0);
    auto* right = mix.getWritePointer (1);
    float g = 0, k = 1;
    int filterKind = 0;   // 0 low, 2 high, -1 open
    for (int i = 0; i < n; ++i, beat += beatsPerSample)
    {
        const float dryL = left[i], dryR = right[i];
        history.setSample (0, writePos, dryL);
        history.setSample (1, writePos, dryR);
        const float w = wet.getNextValue(), xv = xs.getNextValue(), yv = ys.getNextValue();
        float outL = dryL, outR = dryR;

        switch (m)
        {
            case filter:
            {
                if ((i & 15) == 0)
                {
                    float hz;
                    if (xv < 0.47f)
                    {
                        filterKind = 0;
                        hz = 60.0f * std::pow (300.0f, xv / 0.47f);
                    }
                    else if (xv > 0.53f)
                    {
                        filterKind = 2;
                        hz = 20.0f * std::pow (350.0f, (xv - 0.53f) / 0.47f);
                    }
                    else
                    {
                        filterKind = -1;
                        hz = 1000.0f;
                    }
                    g = std::tan (juce::MathConstants<float>::pi * juce::jmin (hz, (float) rate * 0.45f) / (float) rate);
                    k = 1.0f / (0.6f + yv * 9.0f);
                }
                if (filterKind >= 0)
                {
                    outL = filters[0].run (dryL, g, k)[(size_t) filterKind];
                    outR = filters[1].run (dryR, g, k)[(size_t) filterKind];
                }
                break;
            }
            case repeat:
            {
                const int choice = juce::jlimit (0, 3, (int) (xv * 4.0f));
                if (choice != repeatChoice)
                {
                    // Catch a fresh slice from here.
                    repeatChoice = choice;
                    repeatLength = juce::jlimit (64, size / 2, (int) (repeatBeats[choice] * 60.0 / bpm * rate));
                    repeatStart = writePos;
                    repeatPos = 0;
                }
                // The first time round, the slice is what's going in right now.
                const int caught = (repeatStart + repeatPos) % size;
                // A short fade at the slice's ends, so it doesn't click.
                const int fadeLen = juce::jmin (96, repeatLength / 4);
                float fade = 1.0f;
                if (repeatPos < fadeLen)
                    fade = (float) repeatPos / (float) fadeLen;
                else if (repeatPos > repeatLength - fadeLen)
                    fade = (float) (repeatLength - repeatPos) / (float) fadeLen;
                const float amount = 0.4f + 0.6f * yv;
                outL = dryL * (1.0f - amount) + history.getSample (0, caught) * fade * amount;
                outR = dryR * (1.0f - amount) + history.getSample (1, caught) * fade * amount;
                repeatPos = (repeatPos + 1) % repeatLength;
                break;
            }
            case tapeStop:
            {
                const double stopBeats = 0.25 + (1.0 - yv) * 1.75;
                const double step = 1.0 / (stopBeats * 60.0 / bpm * rate);
                if (on)
                    tapeSpeed = juce::jmax (0.0, tapeSpeed - step);
                const int a = ((int) std::floor (tapePos) % size + size) % size, b = (a + 1) % size;
                const float frac = (float) (tapePos - std::floor (tapePos));
                const float level = (float) juce::jmin (1.0, tapeSpeed * 4.0);
                outL = (history.getSample (0, a) + (history.getSample (0, b) - history.getSample (0, a)) * frac) * level;
                outR = (history.getSample (1, a) + (history.getSample (1, b) - history.getSample (1, a)) * frac) * level;
                tapePos += tapeSpeed;
                break;
            }
            case gate:
            {
                const double length = gateBeats[juce::jlimit (0, 2, (int) (xv * 3.0f))];
                const double phase = std::fmod (beat / length, 1.0);
                // Open for the first half, with soft edges.
                const float open = phase < 0.5 ? (float) juce::jmin (1.0, phase * 40.0) : (float) juce::jmax (0.0, 1.0 - (phase - 0.5) * 40.0);
                const float gain = 1.0f - yv * (1.0f - open);
                outL = dryL * gain;
                outR = dryR * gain;
                break;
            }
            case riser:
            {
                const double length = riseBeats[juce::jlimit (0, 2, (int) (xv * 3.0f))];
                const float p = on ? (float) juce::jlimit (0.0, 1.0, (beat - riseStartBeat) / length) : rise.load();
                if (on)
                    rise = p;
                if ((i & 15) == 0)
                {
                    const float centre = 200.0f * std::pow (60.0f, p);
                    g = std::tan (juce::MathConstants<float>::pi * juce::jmin (centre, (float) rate * 0.45f) / (float) rate);
                    k = 0.5f;
                }
                const float noise = noiseFilter.run (random.nextFloat() * 2.0f - 1.0f, g, k)[1] * (0.15f + 0.85f * p) * yv * 0.8f;
                const float thinHz = 20.0f * std::pow (20.0f, p);
                const float tg = std::tan (juce::MathConstants<float>::pi * thinHz / (float) rate);
                outL = thin[0].run (dryL, tg, 1.2f)[2] + noise;
                outR = thin[1].run (dryR, tg, 1.2f)[2] + noise;
                break;
            }
            default: break;
        }

        left[i] = dryL + (outL - dryL) * w;
        right[i] = dryR + (outR - dryR) * w;
        writePos = (writePos + 1) % size;
    }
    if (! on)
        rise = 0;
    if (! playing)
        freeBeat = beat;
}

//==============================================================================
PerformPad::PerformPad (PerformFx& f) : fx (f)
{
    for (int m = 0; m < PerformFx::numModes; ++m)
    {
        auto* b = modeButtons.add (new rigpanels::PanelButton (PerformFx::modeName (m)));
        b->inWell = true;
        b->fontSize = 18.0f;
        b->onClick = [this, m] { setMode (m); };
        addAndMakeVisible (b);
    }
    holdButton.inWell = true;
    holdButton.fontSize = 18.0f;
    holdButton.setTooltip ("Hold: the effect stays on after you let go. Press again to let it go.");
    holdButton.onClick = [this] { setHold (! hold); };
    addAndMakeVisible (holdButton);
    setMode (fx.mode.load());
    startTimerHz (30);
}

void PerformPad::setMode (int m)
{
    fx.mode = juce::jlimit (0, PerformFx::numModes - 1, m);
    for (int i = 0; i < modeButtons.size(); ++i)
    {
        modeButtons[i]->lit = i == fx.mode.load() ? vintage::Lit::next : vintage::Lit::none;
        modeButtons[i]->repaint();
    }
    repaint();
}

void PerformPad::setHold (bool on)
{
    hold = on;
    holdButton.lit = hold ? vintage::Lit::play : vintage::Lit::none;
    holdButton.repaint();
    if (! hold && ! touching)
        fx.engaged = false;
    repaint();
}

void PerformPad::touch (float x01, float y01)
{
    fx.x = juce::jlimit (0.0f, 1.0f, x01);
    fx.y = juce::jlimit (0.0f, 1.0f, y01);
    fx.engaged = true;
    touching = true;
    repaint();
}

void PerformPad::release()
{
    touching = false;
    if (! hold)
        fx.engaged = false;
    repaint();
}

void PerformPad::moveTo (juce::Point<int> p)
{
    const auto pad = padArea.toFloat();
    touch ((p.x - pad.getX()) / juce::jmax (1.0f, pad.getWidth()), 1.0f - (p.y - pad.getY()) / juce::jmax (1.0f, pad.getHeight()));
}

void PerformPad::mouseDown (const juce::MouseEvent& e)
{
    if (padArea.contains (e.getPosition()))
        moveTo (e.getPosition());
}

void PerformPad::mouseDrag (const juce::MouseEvent& e)
{
    if (touching)
        moveTo (e.getPosition());
}

void PerformPad::mouseUp (const juce::MouseEvent&)
{
    if (touching)
        release();
}

void PerformPad::timerCallback()
{
    if (isShowing() && (fx.engaged.load() || fx.getRise() > 0))
        repaint (padArea);
}

void PerformPad::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    vintage::drawWell (g, getLocalBounds().toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (vintage::cond (21.0f, vintage::Weight::extra, 0.08f));
    g.drawText ("FX PAD", captionArea.removeFromTop (24), juce::Justification::centredLeft);
    vintage::engraved (g, juce::String::fromUTF8 ("Touch to play \xc2\xb7 let go and it's gone \xc2\xb7 Hold keeps it"), captionArea, juce::Justification::centredLeft, 11.0f, true);

    // The pad.
    const auto pad = padArea.toFloat();
    const bool on = fx.engaged.load();
    g.setColour (juce::Colour (0xff0e0c0a));
    g.fillRoundedRectangle (pad, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (int i = 1; i < 4; ++i)
    {
        g.drawVerticalLine ((int) (pad.getX() + pad.getWidth() * i / 4), pad.getY() + 4, pad.getBottom() - 4);
        g.drawHorizontalLine ((int) (pad.getY() + pad.getHeight() * i / 4), pad.getX() + 4, pad.getRight() - 4);
    }
    const auto accent = vintage::colours::next;
    if (on)
    {
        vintage::glow (g, pad, 10.0f, accent, 10.0f, 0.4f);
        g.setColour (accent.withAlpha (0.6f));
        g.drawRoundedRectangle (pad, 10.0f, 2.0f);
    }
    // The riser's rise.
    if (fx.mode.load() == PerformFx::riser && fx.getRise() > 0)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillRoundedRectangle (pad.withTop (pad.getBottom() - pad.getHeight() * fx.getRise()), 10.0f);
    }
    g.setColour (p.wellInk2);
    g.setFont (vintage::barlow (12.0f));
    g.drawText (PerformFx::axisText (fx.mode.load(), true), pad.reduced (10, 6), juce::Justification::centredBottom);
    g.drawText (PerformFx::axisText (fx.mode.load(), false), pad.reduced (10, 6), juce::Justification::topLeft);

    const juce::Point<float> dot (pad.getX() + fx.x.load() * pad.getWidth(), pad.getBottom() - fx.y.load() * pad.getHeight());
    const float r = on ? 16.0f : 10.0f;
    if (on)
        vintage::glow (g, juce::Rectangle<float> (r * 2, r * 2).withCentre (dot), r, accent, 12.0f, 0.7f);
    g.setColour (on ? accent : p.wellInk2.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (r * 2, r * 2).withCentre (dot));
}

void PerformPad::resized()
{
    auto r = getLocalBounds().reduced (14, 12);
    captionArea = r.removeFromTop (40);
    r.removeFromTop (6);
    auto buttons = r.removeFromRight (juce::jmin (130, r.getWidth() / 3));
    r.removeFromRight (12);
    padArea = r;
    const int count = modeButtons.size() + 1;
    const int bh = juce::jmin (44, (buttons.getHeight() - 6 * (count - 1)) / count);
    for (auto* b : modeButtons)
    {
        b->setBounds (buttons.removeFromTop (bh));
        buttons.removeFromTop (6);
    }
    holdButton.setBounds (buttons.removeFromBottom (bh));
}
