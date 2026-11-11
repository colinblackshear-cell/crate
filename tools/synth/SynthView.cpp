#include "SynthView.h"

namespace Theme = crate::Theme;

namespace
{
const juce::Colour synthColour { 0xfff06292 };
}

juce::Rectangle<float> SynthView::RhythmRow::cell (int step, int row) const
{
    const float w = (float) getWidth() / crate::synthtool::kRhythmSteps;
    const float h = (float) getHeight() * (row == 0 ? 0.65f : 0.35f);
    return { step * w, row == 0 ? 0.0f : (float) getHeight() * 0.65f, w, h };
}

void SynthView::RhythmRow::paint (juce::Graphics& g)
{
    const auto r = processor.getRhythm();
    const int now = processor.getArpStep();
    for (int i = 0; i < crate::synthtool::kRhythmSteps; ++i)
    {
        const bool inLength = i < r.length;
        for (int row = 0; row < 2; ++row)
        {
            auto c = cell (i, row).reduced (2.0f);
            const bool lit = row == 0 ? r.on[(size_t) i] : r.accent[(size_t) i];
            auto colour = lit ? (row == 0 ? synthColour : Theme::accent) : (i % 4 == 0 ? Theme::raised.brighter (0.06f) : Theme::raised);
            if (! inLength)
                colour = colour.darker (0.6f);
            g.setColour (colour);
            g.fillRoundedRectangle (c, 4.0f);
        }
        if (i == now)
        {
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.fillRect (cell (i, 0).getUnion (cell (i, 1)));
        }
    }
}

void SynthView::RhythmRow::click (juce::Point<float> at, bool drag)
{
    for (int i = 0; i < crate::synthtool::kRhythmSteps; ++i)
        for (int row = 0; row < 2; ++row)
            if (cell (i, row).contains (at))
            {
                if (drag && (i == lastStep || row != dragRow))
                    return;
                lastStep = i;
                auto r = processor.getRhythm();
                bool& flag = row == 0 ? r.on[(size_t) i] : r.accent[(size_t) i];
                if (! drag)
                {
                    dragValue = ! flag;
                    dragRow = row;
                }
                flag = dragValue;
                processor.setRhythm (r);
                repaint();
                return;
            }
}

void SynthView::RhythmRow::mouseDown (const juce::MouseEvent& e)
{
    lastStep = -1;
    click (e.position, false);
}

void SynthView::RhythmRow::mouseDrag (const juce::MouseEvent& e) { click (e.position, true); }

//==============================================================================
SynthView::SynthView (SynthProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      wave (p.params, "wave", "Wave"),
      lfoRate (p.params, "lfoRate", "LFO rate"),
      lfoShape (p.params, "lfoShape", "LFO shape"),
      arpMode (p.params, "arpMode", "Mode"),
      arpRate (p.params, "arpSpeed", "Rate"),
      arpOn (p.params, "arpOn", "Arp on", synthColour),
      latch (p.params, "latch", "Latch", synthColour),
      rhythm (p),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel (&lookAndFeel);

    const std::pair<const char*, const char*> sound[] = { { "detune", "Detune" }, { "octave", "Octave" }, { "cutoff", "Cutoff" },
                                                          { "resonance", "Resonance" }, { "envMod", "Env mod" },
                                                          { "filterDecay", "Filter decay" }, { "attack", "Attack" },
                                                          { "decay", "Decay" }, { "sustain", "Sustain" }, { "release", "Release" },
                                                          { "lfoDepth", "LFO depth" }, { "volume", "Volume" } };
    for (auto [id, name] : sound)
        addAndMakeVisible (soundKnobs.add (new crate::ParamKnob (p.params, id, name, synthColour)));
    const std::pair<const char*, const char*> arp[] = { { "arpOctaves", "Octaves" }, { "arpGate", "Gate" } };
    for (auto [id, name] : arp)
        addAndMakeVisible (arpKnobs.add (new crate::ParamKnob (p.params, id, name, synthColour)));

    arpOn.setTooltip ("Hold a chord and the arpeggiator plays its notes in time, on the steps lit below.");
    latch.setTooltip ("Keeps the arp playing after you let go; play a new chord to change it.");
    auto editLength = [this] (int delta)
    {
        auto r = processor.getRhythm();
        r.length = juce::jlimit (1, crate::synthtool::kRhythmSteps, r.length + delta);
        processor.setRhythm (r);
    };
    shorter.onClick = [editLength] { editLength (-1); };
    longer.onClick = [editLength] { editLength (1); };
    lengthLabel.setJustificationType (juce::Justification::centred);

    for (auto* c : std::initializer_list<juce::Component*> { &wave, &lfoRate, &lfoShape, &arpMode, &arpRate, &arpOn, &latch,
                                                             &shorter, &longer, &lengthLabel, &rhythm, &keyboard })
        addAndMakeVisible (c);

    keyboard.setAvailableRange (36, 96);
    keyboard.setLowestVisibleKey (36);
    keyboard.setOctaveForMiddleC (3);

    setResizable (true, true);
    setResizeLimits (900, 520, 3000, 2000);
    setSize (1200, 700);
    startTimerHz (30);
}

SynthView::~SynthView() { setLookAndFeel (nullptr); }

void SynthView::timerCallback()
{
    lengthLabel.setText (juce::String (processor.getRhythm().length) + " steps", juce::dontSendNotification);
    rhythm.repaint();
}

void SynthView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    auto area = getLocalBounds().reduced (10);
    g.setColour (synthColour);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("SYNTH", area.removeFromTop (34), juce::Justification::centredLeft);

    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().withTop (wave.getY() - 26).withBottom (soundKnobs.getLast()->getBottom() + 8).reduced (10, 0).toFloat(), 8.0f);
    g.fillRoundedRectangle (getLocalBounds().withTop (arpOn.getY() - 26).withBottom (rhythm.getBottom() + 8).reduced (10, 0).toFloat(), 8.0f);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("SOUND", wave.getX(), wave.getY() - 24, 200, 18, juce::Justification::centredLeft);
    g.drawText ("ARPEGGIATOR", arpOn.getX(), arpOn.getY() - 24, 200, 18, juce::Justification::centredLeft);
}

void SynthView::resized()
{
    auto area = getLocalBounds().reduced (20, 10);
    area.removeFromTop (34);
    keyboard.setBounds (area.removeFromBottom (80).expanded (10, 0));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 35.0f);   // C2-C7 fills the width
    area.removeFromBottom (10);

    // Sound: two rows of controls.
    area.removeFromTop (30);
    auto sound = area.removeFromTop (220);
    const int w = sound.getWidth() / 8;
    auto row1 = sound.removeFromTop (110);
    auto row2 = sound;
    wave.setBounds (row1.removeFromLeft (w).reduced (4, 30));
    for (int i = 0; i < 7; ++i)
        soundKnobs[i]->setBounds (row1.removeFromLeft (w));
    lfoRate.setBounds (row2.removeFromLeft (w).reduced (4, 30));
    lfoShape.setBounds (row2.removeFromLeft (w).reduced (4, 30));
    for (int i = 7; i < soundKnobs.size(); ++i)
        soundKnobs[i]->setBounds (row2.removeFromLeft (w));

    // Arp.
    area.removeFromTop (40);
    auto arpTop = area.removeFromTop (100);
    arpOn.setBounds (arpTop.removeFromLeft (100).withSizeKeepingCentre (100, 34));
    arpTop.removeFromLeft (8);
    latch.setBounds (arpTop.removeFromLeft (80).withSizeKeepingCentre (80, 34));
    arpTop.removeFromLeft (8);
    arpMode.setBounds (arpTop.removeFromLeft (150).reduced (4, 26));
    arpRate.setBounds (arpTop.removeFromLeft (110).reduced (4, 26));
    for (auto* k : arpKnobs)
        k->setBounds (arpTop.removeFromLeft (100));
    arpTop.removeFromLeft (20);
    auto lengthBox = arpTop.removeFromLeft (160).withSizeKeepingCentre (160, 30);
    shorter.setBounds (lengthBox.removeFromLeft (30));
    longer.setBounds (lengthBox.removeFromRight (30));
    lengthLabel.setBounds (lengthBox);

    area.removeFromTop (6);
    rhythm.setBounds (area.removeFromTop (juce::jmin (90, area.getHeight())));
}
