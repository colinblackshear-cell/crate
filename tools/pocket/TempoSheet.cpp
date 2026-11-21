#include "TempoSheet.h"

using namespace pocket;

namespace
{
constexpr double kMin = 40.0, kMax = 240.0;
}

TempoSheet::TempoSheet()
{
    for (auto* b : { &minus, &plus, &tapButton, &half, &twice, &clickButton, &countButton, &done })
    {
        b->setFontHeight (17.0f);
        addAndMakeVisible (*b);
    }
    minus.setFontHeight (28.0f);
    plus.setFontHeight (28.0f);
    minus.onClick = [this] { change (std::round (getBpm() - 1.0)); };
    plus.onClick = [this] { change (std::round (getBpm() + 1.0)); };
    half.onClick = [this] { change (getBpm() * 0.5); };
    twice.onClick = [this] { change (getBpm() * 2.0); };
    tapButton.onClick = [this] { tap(); };
    clickButton.onClick = [this] { setClick (! getClick()); refresh(); };
    countButton.onClick = [this] { setCountIn ((getCountIn() + 1) % 3); refresh(); };
    done.onClick = [this] { if (onDone) onDone(); };
}

void TempoSheet::refresh()
{
    if (getClick == nullptr)
        return;
    clickButton.setText (getClick() ? "Click: on" : "Click: off");
    clickButton.setLook (getClick() ? Look::amber : Look::cream);
    const int bars = getCountIn();
    countButton.setText (bars == 0 ? "Count-in: off" : bars == 1 ? "Count-in: 1 bar" : "Count-in: 2 bars");
    countButton.setLook (bars > 0 ? Look::amber : Look::cream);
    repaint();
}

void TempoSheet::change (double bpm)
{
    setBpm (juce::jlimit (kMin, kMax, std::round (bpm * 10.0) / 10.0));
    repaint();
}

void TempoSheet::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000.0)
        taps.clear();
    taps.push_back (now);
    if (taps.size() > 6)
        taps.erase (taps.begin());
    if (taps.size() >= 2)
        change (60000.0 * (double) (taps.size() - 1) / (taps.back() - taps.front()));
}

void TempoSheet::resized()
{
    const int w = getWidth();
    minus.setBounds (28, 70, 64, 64);
    plus.setBounds (w - 92, 70, 64, 64);
    const int third = (w - 56 - 16) / 3;
    tapButton.setBounds (28, 204, third, 54);
    half.setBounds (28 + third + 8, 204, third, 54);
    twice.setBounds (28 + 2 * (third + 8), 204, third, 54);
    clickButton.setBounds (28, 270, (w - 64) / 2, 46);
    countButton.setBounds (36 + (w - 64) / 2, 270, (w - 64) / 2, 46);
    done.setBounds (w - 108, 14, 84, 40);
}

void TempoSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (26.0f));
    g.drawText ("Tempo", 28, 16, 200, 36, juce::Justification::centredLeft);

    const double bpm = getBpm != nullptr ? getBpm() : 120.0;
    auto lcd = juce::Rectangle<float> (104, 70, (float) getWidth() - 208, 64);
    drawWell (g, lcd, 14.0f);
    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (64.0f));
    g.drawText (juce::String (bpm, 1), lcd, juce::Justification::centred);

    auto bar = barRect();
    g.setColour (col::creamDeep);
    g.fillRoundedRectangle (bar, 6.0f);
    const float x = bar.getX() + bar.getWidth() * (float) ((bpm - kMin) / (kMax - kMin));
    g.setColour (col::amber);
    g.fillRoundedRectangle (bar.withRight (x), 6.0f);
    drawRaised (g, juce::Rectangle<float> (x - 15, bar.getCentreY() - 15, 30, 30), Look::cream, false, 15.0f);

    g.setColour (col::inkDim);
    g.setFont (small (14.0f));
    g.drawFittedText (U8("One tempo for the whole song: the beat, the click and new loop layers. Set it before you record; layers already recorded keep the speed they were played at."),
                      juce::Rectangle<int> (28, 328, getWidth() - 56, 60), juce::Justification::topLeft, 3);
}

void TempoSheet::mouseDown (const juce::MouseEvent& e)
{
    if (barRect().expanded (0, 18).contains (e.position))
        mouseDrag (e);
}

void TempoSheet::mouseDrag (const juce::MouseEvent& e)
{
    auto bar = barRect();
    if (! bar.expanded (20, 40).contains (e.getMouseDownPosition().toFloat()))
        return;
    const double t = juce::jlimit (0.0, 1.0, (double) ((e.position.x - bar.getX()) / bar.getWidth()));
    change (std::round (kMin + t * (kMax - kMin)));
}
