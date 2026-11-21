#include "SoundBankSheet.h"

using namespace pocket;

SoundBankSheet::SoundBankSheet()
{
    for (int i = 0; i < 5; ++i)
    {
        loads[i].setText ("Load");
        loads[i].setFontHeight (15.0f);
        loads[i].onClick = [this, i] { if (onLoad) onLoad (i); };
        addAndMakeVisible (loads[i]);
    }
    for (auto* b : { &import, &openSounds, &done })
    {
        b->setFontHeight (16.0f);
        addAndMakeVisible (*b);
    }
    import.onClick = [this] { if (onImport) onImport(); };
    openSounds.onClick = [this] { if (onOpenSounds) onOpenSounds(); };
    done.onClick = [this] { if (onDone) onDone(); };
}

void SoundBankSheet::resized()
{
    for (int i = 0; i < 5; ++i)
    {
        auto r = row (i);
        loads[i].setBounds ((int) r.getRight() - 78, (int) r.getY() + 8, 70, 36);
    }
    done.setBounds (getWidth() - 100, 12, 84, 38);
    const int y = (int) row (4).getBottom() + 112;
    import.setBounds (16, y, (getWidth() - 40) * 3 / 5, 44);
    openSounds.setBounds (import.getRight() + 8, y, getWidth() - 16 - import.getRight() - 8, 44);
}

void SoundBankSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (24.0f));
    g.drawText ("Factory kits", 18, 14, 200, 36, juce::Justification::centredLeft);

    const auto loaded = loadedKitName != nullptr ? loadedKitName() : juce::String();
    auto& kits = factoryKits();
    for (int i = 0; i < (int) kits.size() && i < 5; ++i)
    {
        auto r = row (i);
        const bool on = Kit::mpcName (juce::String (kits[(size_t) i].name).toUpperCase()) == loaded;
        g.setColour (on ? col::creamLight : col::cream);
        g.fillRoundedRectangle (r, 12.0f);
        if (on)
        {
            g.setColour (col::amber);
            g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
        }
        g.setColour (juce::Colour (kits[(size_t) i].colour));
        g.fillRoundedRectangle (r.getX() + 8, r.getY() + 8, 36, 36, 9.0f);
        g.setColour (col::ink);
        g.setFont (small (16.0f).boldened());
        g.drawText (kits[(size_t) i].name, juce::Rectangle<float> (r.getX() + 54, r.getY() + 6, r.getWidth() - 140, 22), juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (small (13.0f));
        g.drawFittedText (kits[(size_t) i].what, juce::Rectangle<int> ((int) r.getX() + 54, (int) r.getY() + 27, (int) r.getWidth() - 140, 20), juce::Justification::centredLeft, 1);
    }

    auto box = juce::Rectangle<float> (14, row (4).getBottom() + 10, (float) getWidth() - 28, 96);
    g.setColour (col::well);
    g.fillRoundedRectangle (box, 14.0f);
    g.setColour (col::amber);
    g.setFont (spaced (condensedBold (15.0f), 0.08f));
    g.drawText ("YOUR OWN SOUNDS (SPLICE, ANYWHERE)", box.reduced (14, 8).withHeight (20), juce::Justification::centredLeft);
    g.setColour (juce::Colour (0xfff1e7d1));
    g.setFont (small (13.5f));
    g.drawFittedText (U8("Files app › On My iPhone › Kaleidobox Pocket › Drop Sounds Here: anything you put there comes in when you open this. From Splice or Mail, Save to Files into that folder. Or send sounds from the Mac over the link."),
                      box.reduced (14, 8).withTrimmedTop (22).toNearestInt(), juce::Justification::topLeft, 4);
    g.setColour (col::inkDim);
    g.setFont (small (13.0f));
    g.drawText ("LOAD puts the kit on the bank you're on (its 16 pads).", 16, (int) row (4).getBottom() + 162, getWidth() - 32, 20, juce::Justification::centredLeft);
}
