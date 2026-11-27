#include "BouncePanel.h"

#include "Mp3Writer.h"
#include "RigSong.h"

using What = RigBounce::What;
using Split = RigBounce::Split;

BouncePanel::BouncePanel (RigEngine& e, RigBounce::Options start) : engine (e), options (std::move (start))
{
    songButton.setTooltip ("The whole song, from bar 1 to its end");
    songButton.onClick = [this] { pickWhat (What::song); };
    loopsButton.setTooltip ("The loops that are on now, once round the longest (a loop series, not the song)");
    loopsButton.onClick = [this] { pickWhat (What::loops); };
    sectionBox.setTextWhenNothingSelected ("A section...");
    sectionBox.setTooltip ("One section of the song");
    const auto secs = rigsong::sections (engine.getTimeline());
    for (int i = 0; i < (int) secs.size(); ++i)
        sectionBox.addItem (secs[(size_t) i].name + " (bars " + juce::String ((int) secs[(size_t) i].start + 1) + "-"
                                + juce::String ((int) std::round (secs[(size_t) i].end())) + ")",
                            i + 1);
    sectionBox.onChange = [this] { if (sectionBox.getSelectedId() > 0) pickSection (sectionBox.getSelectedId() - 1); };

    mixButton.setTooltip ("One file: everything mixed");
    mixButton.onClick = [this] { pickSplit (Split::mix); };
    eachButton.setTooltip ("Each loop as its own file (stems), and the mix of them. Instant.");
    eachButton.onClick = [this] { pickSplit (Split::eachLoop); };
    allButton.setTooltip ("Each loop, and every tool (MPC, drum machine, bass, synth, sampler, drone, vocoder, input) "
                          "as its own file with the mix and the MIDI you played: the song plays once in real time to record it.");
    allButton.onClick = [this] { pickSplit (Split::everything); };

    wavButton.setTooltip ("24-bit WAV at the Rig's rate: for Logic");
    wavButton.onClick = [this] { setFormats (! options.wav, options.aiff, options.mp3); };
    aiffButton.setTooltip ("24-bit AIFF: for Logic, Pro Tools, anything on a Mac");
    aiffButton.onClick = [this] { setFormats (options.wav, ! options.aiff, options.mp3); };
    mp3Button.setTooltip ("MP3, 320 kbps: to share, or for anywhere else");
    mp3Button.onClick = [this] { setFormats (options.wav, options.aiff, ! options.mp3); };
    dryButton.setTooltip ("The loops without their effects and levels (as recorded)");
    dryButton.onClick = [this] { options.dry = ! options.dry; update(); };

    tailBox.addItem ("No tail", 1);
    tailBox.addItem ("2 s tail", 2);
    tailBox.addItem ("4 s tail", 3);
    tailBox.addItem ("8 s tail", 4);
    tailBox.setTooltip ("Time after the last bar for echoes and reverb to ring out");
    tailBox.setSelectedId (options.tailSeconds <= 0.0 ? 1 : options.tailSeconds <= 2.0 ? 2 : options.tailSeconds <= 4.0 ? 3 : 4,
                           juce::dontSendNotification);
    tailBox.onChange = [this]
    {
        static const double secs[] = { 0.0, 2.0, 4.0, 8.0 };
        options.tailSeconds = secs[juce::jlimit (0, 3, tailBox.getSelectedId() - 1)];
        update();
    };

    bounceButton.setTooltip ("Writes the files into Music > Crate > Rig > To Logic, in a folder of their own");
    bounceButton.onClick = [this] { pressBounce(); };
    vintage::setLit (bounceButton, vintage::Lit::play);
    bounceButton.setToggleState (true, juce::dontSendNotification);

    trackRow = RigBounce::hasTrack (engine);
    eachPartButton.setTooltip ("Each of your parts as its own file, without his track, all starting where his mixdown starts "
                               "(plus a mix of your parts). Plays the song once in real time.");
    eachPartButton.onClick = [this] { pickSendBack (SendBack::myPartsEach); };
    mixedPartsButton.setTooltip ("One file of everything you added, without his track. Plays the song once in real time.");
    mixedPartsButton.onClick = [this] { pickSendBack (SendBack::myPartsMixed); };
    listenButton.setTooltip ("His track and your parts in one MP3, to listen to. Plays the song once in real time.");
    listenButton.onClick = [this] { pickSendBack (SendBack::everythingToListen); };
    for (auto* b : { &eachPartButton, &mixedPartsButton, &listenButton })
    {
        addChildComponent (b);
        b->setVisible (trackRow);
    }

    for (auto* c : std::initializer_list<juce::Component*> { &songButton, &loopsButton, &sectionBox, &mixButton, &eachButton, &allButton, &wavButton,
                                                             &aiffButton, &mp3Button, &dryButton, &tailBox, &bounceButton })
        addAndMakeVisible (c);
    if (! mp3::available())
        options.mp3 = false;
    mp3Button.setEnabled (mp3::available());
    if (options.what == What::section && ! juce::isPositiveAndBelow (options.section, (int) secs.size()))
        options.what = What::song;
    setSize (kWidth, kHeight + (trackRow ? kTrackRowHeight : 0));
    update();
}

void BouncePanel::pickSendBack (SendBack s)
{
    options.what = What::song;
    options.split = Split::everything;
    options.withTrack = s == SendBack::everythingToListen;
    options.mixOnly = s != SendBack::myPartsEach;
    if (s == SendBack::everythingToListen && mp3::available())
    {
        options.mp3 = true;
        options.wav = false;
        options.aiff = false;
    }
    else if (s != SendBack::everythingToListen)
        options.wav = true;
    update();
}

void BouncePanel::pickWhat (What w)
{
    options.what = w;
    options.withTrack = true;   // (the Send back row is its own choice)
    options.mixOnly = false;
    if (w == What::loops && options.split == Split::everything)
        options.split = Split::eachLoop;   // the tools play the song; the loops on their own are instant
    update();
}

void BouncePanel::pickSection (int s)
{
    options.what = What::section;
    options.withTrack = true;
    options.mixOnly = false;
    options.section = s;
    update();
}

void BouncePanel::pickSplit (Split s)
{
    options.split = s;
    options.withTrack = true;
    options.mixOnly = false;
    if (s == Split::everything && options.what == What::loops)
        options.what = What::song;
    update();
}

void BouncePanel::setFormats (bool wav, bool aiff, bool mp3)
{
    if (! wav && ! aiff && ! mp3)
        return;   // one at least
    options.wav = wav;
    options.aiff = aiff;
    options.mp3 = mp3 && mp3::available();
    if (! options.wav && ! options.aiff && ! options.mp3)
        options.wav = true;
    update();
}

void BouncePanel::pressBounce()
{
    if (onBounce)
        onBounce (options);
}

void BouncePanel::update()
{
    auto lit = [] (juce::TextButton& b, bool on)
    {
        vintage::setLit (b, vintage::Lit::current);
        b.setToggleState (on, juce::dontSendNotification);
    };
    lit (songButton, options.what == What::song);
    lit (loopsButton, options.what == What::loops);
    sectionBox.setSelectedId (options.what == What::section ? options.section + 1 : 0, juce::dontSendNotification);
    lit (mixButton, options.split == Split::mix);
    lit (eachButton, options.split == Split::eachLoop);
    lit (allButton, options.split == Split::everything);
    lit (wavButton, options.wav);
    lit (aiffButton, options.aiff);
    lit (mp3Button, options.mp3);
    lit (dryButton, options.dry);
    allButton.setButtonText (options.split == Split::everything ? "Everything (real time)" : "Everything");
    const bool sendBack = options.what == What::song && options.split == Split::everything;
    lit (eachPartButton, sendBack && ! options.withTrack && ! options.mixOnly);
    lit (mixedPartsButton, sendBack && ! options.withTrack && options.mixOnly);
    lit (listenButton, sendBack && options.withTrack && options.mixOnly);
    if (onChange)
        onChange (options);
    repaint (statusArea);
}

void BouncePanel::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::engraved (g, "BOUNCE + STEMS", titleArea, juce::Justification::centredLeft, 22.0f);
    vintage::engraved (g, "WHAT", whatLabel, juce::Justification::centredLeft, 14.0f);
    vintage::engraved (g, "SPLIT", splitLabel, juce::Justification::centredLeft, 14.0f);
    vintage::engraved (g, "FORMAT", formatLabel, juce::Justification::centredLeft, 14.0f);
    if (trackRow)
        vintage::engraved (g, "SEND BACK", trackLabel, juce::Justification::centredLeft, 14.0f);
    auto box = statusArea.toFloat();
    vintage::drawLcd (g, box);
    auto text = RigBounce::describe (engine, options);
    if (! mp3::available())
        text << " (MP3 isn't in this build)";
    vintage::lcdText (g, text.toUpperCase(), box.reduced (10, 0), 15.0f, juce::Justification::centredLeft);
}

void BouncePanel::resized()
{
    auto r = getLocalBounds().reduced (18, 14);
    titleArea = r.removeFromTop (34);
    r.removeFromTop (8);
    auto row = [&] (juce::Rectangle<int>& label, std::initializer_list<std::pair<juce::Component*, int>> cells)
    {
        auto line = r.removeFromTop (38);
        label = line.removeFromLeft (70);
        for (auto& c : cells)
            c.first->setBounds (line.removeFromLeft (c.second).reduced (3, 2));
        r.removeFromTop (10);
    };
    row (whatLabel, { { &songButton, 110 }, { &sectionBox, 200 }, { &loopsButton, 200 } });
    row (splitLabel, { { &mixButton, 110 }, { &eachButton, 180 }, { &allButton, 220 } });
    row (formatLabel, { { &wavButton, 80 }, { &aiffButton, 80 }, { &mp3Button, 80 }, { &dryButton, 80 }, { &tailBox, 120 } });
    if (trackRow)
        row (trackLabel, { { &eachPartButton, 160 }, { &mixedPartsButton, 160 }, { &listenButton, 160 } });
    statusArea = r.removeFromTop (44);
    r.removeFromTop (12);
    bounceButton.setBounds (r.removeFromTop (48).removeFromRight (200));
}
