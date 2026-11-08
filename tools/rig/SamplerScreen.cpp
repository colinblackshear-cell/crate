#include "SamplerScreen.h"

#include "EditorView.h"
#include "core/SoundFx.h"

using namespace vintage;

namespace
{
constexpr int kBins = 6000;          // the peaks kept for the whole sound
constexpr float kBarWidth = 3.0f;    // the wave is drawn as bars, like the design
constexpr float kBarGap = 1.0f;

juce::String seconds (int samples)
{
    return juce::String (samples / crate::kSampleRate, 2) + "s";
}

const char* const kModeNames[] = { "One shot", "Mono", "Note off" };
}

//==============================================================================
SamplerScreen::SamplerScreen (RigEngine& e, EditorView& full)
    : engine (e), editor (full)
{
    setOpaque (true);

    for (int i = 0; i < 3; ++i)
    {
        setInWell (chopButtons[i], true);
        setLit (chopButtons[i], Lit::current);
        chopButtons[i].getProperties().set ("fontSize", 16.0f);
        chopButtons[i].onClick = [this, i] { pressChop (i); };
        addAndMakeVisible (chopButtons[i]);
    }
    chopButtons[transients].setTooltip ("Finds the hits (kicks, snares...) and puts a slice on each.");
    chopButtons[even16].setTooltip ("Cuts the sound into 16 equal slices, one per pad.");
    chopButtons[byHand].setTooltip ("Click the strip above the wave to add a marker, drag one to move it, right-click one to remove it.");

    for (auto* b : { &snapButton, &zoomOut, &zoomIn, &openButton, &editorButton })
    {
        setInWell (*b, true);
        b->getProperties().set ("fontSize", 16.0f);
        addAndMakeVisible (*b);
    }
    zoomOut.getProperties().set ("fontSize", 20.0f);
    zoomIn.getProperties().set ("fontSize", 20.0f);
    snapButton.setTooltip ("Moves every marker to the nearest point where the wave crosses zero, so slices start without a click.");
    snapButton.onClick = [this] { pressSnap(); };
    zoomIn.onClick = [this]
    {
        const auto centre = numSlices() > 0 && peaksOf != nullptr ? sliceRange (selectedSlice).getStart() : 0;
        const auto& doc = engine.getSampler().getDocument();
        const double length = view.getLength() / 2.0;
        double mid = view.getStart() + view.getLength() / 2.0;
        if (doc.hasAudio())
        {
            const double s = (double) centre / doc.getAudio()->getNumSamples();
            if (view.contains (s))
                mid = s;
        }
        const double minLength = doc.hasAudio() ? juce::jmin (1.0, 2000.0 / doc.getAudio()->getNumSamples()) : 1.0;
        const double l = juce::jmax (minLength, length);
        view = juce::Range<double> (mid - l / 2, mid + l / 2).movedToStartAt (juce::jlimit (0.0, 1.0 - l, mid - l / 2));
        repaint();
    };
    zoomOut.onClick = [this]
    {
        const double l = juce::jmin (1.0, view.getLength() * 2.0);
        const double mid = view.getStart() + view.getLength() / 2.0;
        view = juce::Range<double> (0, l).movedToStartAt (juce::jlimit (0.0, 1.0 - l, mid - l / 2));
        repaint();
    };
    openButton.setTooltip ("Open a sound file (or drop one here).");
    openButton.onClick = [this] { editor.chooseSoundFile(); };
    editorButton.setTooltip ("The full sample editor: record, trim, fades, effects, keys.");
    editorButton.onClick = [this] { if (onOpen) onOpen ("Sampler editor"); };

    toPadsButton.getProperties().set ("fontSize", 16.0f);
    toPadsButton.setTooltip ("Puts the slices on the MPC's pads as a new kit, slice 1 on A01.");
    toPadsButton.onClick = [this] { pressToPads(); };
    addAndMakeVisible (toPadsButton);

    // The picked pad's settings, as the MPC has them.
    const std::array<std::pair<const char*, juce::Colour>, 5> knobNames { { { "Pitch", colours::sampler }, { "Level", colours::sampler },
                                                                            { "Pan", colours::sampler }, { "Mode", colours::sampler },
                                                                            { "Choke", colours::sampler } } };
    for (int i = 0; i < 5; ++i)
    {
        auto knob = std::make_unique<Knob> (knobNames[(size_t) i].first, knobNames[(size_t) i].second);
        auto& s = knob->slider;
        switch (i)
        {
            case 0:
                s.setRange (-12.0, 12.0, 0.1);
                s.setDoubleClickReturnValue (true, 0.0);
                s.textFromValueFunction = [] (double v)
                {
                    return std::abs (v) < 0.05 ? juce::String ("0 st") : (v > 0 ? "+" : "") + juce::String (v, 1) + " st";
                };
                break;
            case 1:
                s.setRange (0.0, 100.0, 1.0);
                s.setDoubleClickReturnValue (true, 100.0);
                s.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)); };
                break;
            case 2:
                s.setRange (0.0, 100.0, 1.0);
                s.setDoubleClickReturnValue (true, 50.0);
                s.textFromValueFunction = [] (double v)
                {
                    const int p = juce::roundToInt (v) - 50;
                    return p == 0 ? juce::String ("C") : (p < 0 ? "L" : "R") + juce::String (std::abs (p));
                };
                break;
            case 3:
                s.setRange (0.0, 2.0, 1.0);
                s.setMouseDragSensitivity (90);
                s.textFromValueFunction = [] (double v) { return juce::String (kModeNames[juce::jlimit (0, 2, juce::roundToInt (v))]); };
                break;
            default:
                s.setRange (0.0, 32.0, 1.0);
                s.setDoubleClickReturnValue (true, 0.0);
                s.textFromValueFunction = [] (double v)
                {
                    const int group = juce::roundToInt (v);
                    return group == 0 ? juce::String ("Off") : "Group " + juce::String (group);
                };
                break;
        }
        auto* k = knob.get();
        s.onValueChange = [this, i, k] { k->repaint(); setPadValue (i, k->slider.getValue()); };
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }
    knobs[0]->slider.setTooltip ("How high the pad plays, in semitones.");
    knobs[3]->slider.setTooltip ("One shot plays to the end; Mono cuts its last hit off; Note off stops when you let go.");
    knobs[4]->slider.setTooltip ("Pads in the same choke group cut each other off (a closed hat stops the open one).");

    for (int i = 0; i < 3; ++i)
    {
        setInWell (mediumButtons[i], true);
        setLit (mediumButtons[i], Lit::current);
        mediumButtons[i].getProperties().set ("fontSize", 15.0f);
        mediumButtons[i].onClick = [this, i] { medium = i; updateButtons(); repaint (sendWell); };
        addAndMakeVisible (mediumButtons[i]);
    }
    mediumButtons[0].setTooltip ("A CF card in a USB reader: put it in the MPC's CF slot (or SCSI card reader).");
    mediumButtons[1].setTooltip ("A Zip disk (100 MB) in a USB Zip drive.");
    mediumButtons[2].setTooltip ("A 1.44 MB floppy: only small kits fit.");

    setInWell (sendButton, true);
    setLit (sendButton, Lit::next);
    sendButton.setToggleState (true, juce::dontSendNotification);
    sendButton.getProperties().set ("fontSize", 22.0f);
    sendButton.setTooltip ("Writes every kit (its .SND sounds and .PGM program) to the card, ready to LOAD on the MPC.");
    sendButton.onClick = [this]
    {
        size_t bytes = 0;
        for (const auto& bank : engine.getMpc().getProject().getBanks())
            bytes += bank.memoryBytes();
        if (bytes == 0)
        {
            if (onSay)
                onSay ("Nothing to send yet: chop a sound and press To pads, or drop sounds on the MPC's pads.");
            return;
        }
        if (const auto cap = capacityOf (medium); cap > 0 && bytes > cap)
        {
            if (onSay)
                onSay ("Too big for a " + mediumName (medium).toLowerCase() + ": the kits need "
                       + juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB. Save pads as mono or trim them, or use a bigger card.");
            return;
        }
        if (onSendToMpc)
            onSendToMpc();
    };
    addAndMakeVisible (sendButton);

    for (int i = 0; i < 4; ++i)
    {
        setLit (bankButtons[i], Lit::current);
        bankButtons[i].getProperties().set ("fontSize", 22.0f);
        bankButtons[i].onClick = [this, i] { pressBank (i); };
        addAndMakeVisible (bankButtons[i]);
    }
    setLit (fullLevel, Lit::current);
    fullLevel.getProperties().set ("fontSize", 16.0f);
    fullLevel.setClickingTogglesState (true);
    fullLevel.setTooltip ("Full level: every hit plays at full volume, however hard you hit the pad.");
    fullLevel.onClick = [this] { engine.getMpc().fullLevel = fullLevel.getToggleState(); };
    addAndMakeVisible (fullLevel);

    refresh();
    startTimerHz (30);
}

SamplerScreen::~SamplerScreen()
{
    stopTimer();
}

//==============================================================================
size_t SamplerScreen::capacityOf (int m)
{
    if (m == 1) return (size_t) 100 * 1000 * 1000;   // Zip 100
    if (m == 2) return (size_t) 1440 * 1024;         // 1.44 MB floppy
    return 0;
}

juce::String SamplerScreen::mediumName (int m)
{
    return m == 1 ? "Zip disk" : m == 2 ? "Floppy" : "CF card";
}

int SamplerScreen::numSlices() const
{
    const auto& doc = engine.getSampler().getDocument();
    return doc.hasAudio() ? (int) engine.getSampler().getChops().size() + 1 : 0;
}

juce::Range<int> SamplerScreen::sliceRange (int slice) const
{
    const auto& doc = engine.getSampler().getDocument();
    if (! doc.hasAudio())
        return {};
    const auto& chops = engine.getSampler().getChops();
    const int length = doc.getAudio()->getNumSamples();
    slice = juce::jlimit (0, (int) chops.size(), slice);
    const int start = slice == 0 ? 0 : chops[(size_t) slice - 1];
    const int end = slice < (int) chops.size() ? chops[(size_t) slice] : length;
    return { start, juce::jmax (start, end) };
}

int SamplerScreen::getSelectedPad() const
{
    return juce::jlimit (0, mpc::kPadCount - 1, selectedSlice);
}

//==============================================================================
void SamplerScreen::pressChop (int mode)
{
    chopMode = juce::jlimit (0, 2, mode);
    const auto& doc = engine.getSampler().getDocument();
    juce::String text;
    if (! doc.hasAudio())
        text = "There's no sound to chop yet: drop one here, press Open, or sample a tool (More > Sample).";
    else if (chopMode == transients)
    {
        editor.runChop ("findHits");
        const int n = numSlices();
        text = n <= 1 ? "No separate hits found. Try Even 16, or By hand."
                      : "Found " + juce::String (n) + " slices. Click one to hear it; To pads puts them on the MPC.";
    }
    else if (chopMode == even16)
    {
        editor.runChop ("split16");
        text = "Cut into 16 equal slices. To pads puts them on A01 to A16.";
    }
    else
        text = "By hand: click the strip above the wave to add a marker, drag one to move it, right-click one to remove it.";

    selectedSlice = juce::jlimit (0, juce::jmax (0, numSlices() - 1), selectedSlice);
    view = { 0.0, 1.0 };
    if (onSay)
        onSay (text);
    updateButtons();
    repaint();
}

void SamplerScreen::pressSnap()
{
    auto& sampler = engine.getSampler();
    const auto& doc = sampler.getDocument();
    if (! doc.hasAudio() || sampler.getChops().empty())
    {
        if (onSay)
            onSay ("There are no markers to snap yet.");
        return;
    }
    sampler.setChops (crate::SoundFx::snapToZeroCrossings (*doc.getAudio(), sampler.getChops()));
    editor.chopsChanged();
    if (onSay)
        onSay ("Every marker is on a zero crossing now, so no slice starts with a click.");
    repaint();
}

void SamplerScreen::pressToPads()
{
    auto& sampler = engine.getSampler();
    const auto& doc = sampler.getDocument();
    if (! doc.hasAudio() || sampler.getChops().empty())
    {
        if (onSay)
            onSay ("Chop the sound first (Transients, Even 16 or By hand), then To pads puts the slices on the pads.");
        return;
    }
    std::vector<crate::SharedAudio> slices;
    for (auto& s : crate::SoundFx::slice (*doc.getAudio(), sampler.getChops()))
        slices.push_back (crate::share (std::move (s)));
    const auto name = doc.getName().isNotEmpty() ? doc.getName() : juce::String ("Chop");
    const auto text = engine.sendSlicesToMpc (std::move (slices), name);
    pressBank (0);
    if (onSay)
        onSay (text);
    refresh();
}

void SamplerScreen::selectSlice (int slice)
{
    selectedSlice = juce::jlimit (0, juce::jmax (0, numSlices() - 1), slice);
    const int bank = getSelectedPad() / kPadsPerBank;
    if (bank != engine.getMpc().padBankOnScreen.load())
        pressBank (bank);
    updateKnobs();
    repaint();
}

void SamplerScreen::pressPad (int padInBank)
{
    padInBank = juce::jlimit (0, kPadsPerBank - 1, padInBank);
    const int pad = engine.getMpc().padBankOnScreen.load() * kPadsPerBank + padInBank;
    engine.getMpc().playPad (pad);
    selectedSlice = pad;
    updateKnobs();
    repaint();
}

void SamplerScreen::pressBank (int bank)
{
    engine.getMpc().padBankOnScreen = juce::jlimit (0, 3, bank);
    if (getSelectedPad() / kPadsPerBank != bank)
        selectedSlice = bank * kPadsPerBank + getSelectedPad() % kPadsPerBank;
    flashUntil.fill (0);
    updateButtons();
    updateKnobs();
    repaint();
}

void SamplerScreen::addMarker (int sample)
{
    auto& sampler = engine.getSampler();
    const auto& doc = sampler.getDocument();
    if (! doc.hasAudio() || sample <= 0 || sample >= doc.getAudio()->getNumSamples())
        return;
    auto chops = sampler.getChops();
    chops.push_back (sample);
    sampler.setChops (chops);
    editor.chopsChanged();
    chopMode = byHand;
    updateButtons();
    repaint();
}

void SamplerScreen::removeMarker (int index)
{
    auto& sampler = engine.getSampler();
    auto chops = sampler.getChops();
    if (! juce::isPositiveAndBelow (index, (int) chops.size()))
        return;
    chops.erase (chops.begin() + index);
    sampler.setChops (chops);
    editor.chopsChanged();
    selectedSlice = juce::jlimit (0, juce::jmax (0, numSlices() - 1), selectedSlice);
    chopMode = byHand;
    updateButtons();
    repaint();
}

void SamplerScreen::moveMarker (int index, int sample)
{
    auto& sampler = engine.getSampler();
    auto chops = sampler.getChops();
    if (! juce::isPositiveAndBelow (index, (int) chops.size()))
        return;
    // A marker stays between its neighbours.
    const int lo = index == 0 ? 1 : chops[(size_t) index - 1] + 1;
    const int hi = index + 1 < (int) chops.size() ? chops[(size_t) index + 1] - 1
                                                  : sampler.getDocument().getAudio()->getNumSamples() - 1;
    if (hi < lo)
        return;
    chops[(size_t) index] = juce::jlimit (lo, hi, sample);
    sampler.setChops (chops);
    editor.chopsChanged();
    chopMode = byHand;
    repaint();
}

void SamplerScreen::setPadValue (int knob, double value)
{
    auto& project = engine.getMpc().getProject();
    auto& pad = project.currentBank().pads[(size_t) getSelectedPad()];
    if (pad.isEmpty())
        return;
    switch (knob)
    {
        case 0: pad.tune = juce::roundToInt (value * 10.0); break;
        case 1: pad.volume = juce::roundToInt (value); break;
        case 2: pad.pan = juce::roundToInt (value); break;
        case 3: pad.mode = (PadMode) juce::jlimit (0, 2, juce::roundToInt (value)); break;
        default: pad.muteGroup = juce::roundToInt (value); break;
    }
    project.changed();
}

void SamplerScreen::play (juce::Range<int> range)
{
    const auto& doc = engine.getSampler().getDocument();
    if (doc.hasAudio() && ! range.isEmpty())
        engine.getSampler().play (doc.getAudio(), range, false);
}

//==============================================================================
void SamplerScreen::refresh()
{
    fullLevel.setToggleState (engine.getMpc().fullLevel.load(), juce::dontSendNotification);
    selectedSlice = juce::jlimit (0, juce::jmax (0, juce::jmax (numSlices(), mpc::kPadCount) - 1), selectedSlice);
    updateButtons();
    updateKnobs();
    repaint();
}

void SamplerScreen::updateButtons()
{
    for (int i = 0; i < 3; ++i)
        chopButtons[i].setToggleState (chopMode == i && numSlices() > 1, juce::dontSendNotification);
    const int bank = engine.getMpc().padBankOnScreen.load();
    for (int i = 0; i < 4; ++i)
        bankButtons[i].setToggleState (bank == i, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
        mediumButtons[i].setToggleState (medium == i, juce::dontSendNotification);
    const bool chopped = numSlices() > 1;
    snapButton.setEnabled (chopped);
    toPadsButton.setEnabled (chopped);
}

void SamplerScreen::updateKnobs()
{
    const auto& pad = engine.getMpc().getProject().currentBank().pads[(size_t) getSelectedPad()];
    const std::array<double, 5> values { pad.tune / 10.0, (double) pad.volume, (double) pad.pan, (double) (int) pad.mode, (double) pad.muteGroup };
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i]->slider.setValue (values[i], juce::dontSendNotification);
        knobs[i]->setEnabled (! pad.isEmpty());
        knobs[i]->setAlpha (pad.isEmpty() ? 0.45f : 1.0f);
        knobs[i]->repaint();
    }
}

void SamplerScreen::rebuildPeaks()
{
    const auto& doc = engine.getSampler().getDocument();
    const auto* audio = doc.hasAudio() ? doc.getAudio().get() : nullptr;
    if (audio == peaksOf)
        return;
    peaksOf = audio;
    peaks.clear();
    view = { 0.0, 1.0 };
    if (audio == nullptr)
        return;

    const int length = audio->getNumSamples();
    const int bins = juce::jmin (kBins, length);
    peaks.assign ((size_t) bins, 0.0f);
    for (int b = 0; b < bins; ++b)
    {
        const int start = (int) ((juce::int64) b * length / bins);
        const int end = juce::jmax (start + 1, (int) ((juce::int64) (b + 1) * length / bins));
        float peak = 0;
        for (int ch = 0; ch < audio->getNumChannels(); ++ch)
            peak = juce::jmax (peak, audio->getMagnitude (ch, start, end - start));
        peaks[(size_t) b] = juce::jmin (1.0f, peak);
    }
}

void SamplerScreen::timerCallback()
{
    // Pads that played light for a moment.
    const auto played = engine.getMpc().takePlayedPads (1);
    const int bank = engine.getMpc().padBankOnScreen.load();
    const auto now = juce::Time::getMillisecondCounter();
    bool flashed = false;
    for (int p = 0; p < kPadsPerBank; ++p)
    {
        if ((played >> (bank * kPadsPerBank + p)) & 1u)
        {
            flashUntil[(size_t) p] = now + 160;
            flashed = true;
        }
        else if (flashUntil[(size_t) p] != 0 && now > flashUntil[(size_t) p])
        {
            flashUntil[(size_t) p] = 0;
            flashed = true;
        }
    }
    if (flashed)
        repaint (padsWell);

    if (! isShowing())
        return;

    auto& sampler = engine.getSampler();
    const auto& doc = sampler.getDocument();
    auto& project = engine.getMpc().getProject();
    juce::String sig;
    sig << juce::String::toHexString ((juce::pointer_sized_int) (doc.hasAudio() ? doc.getAudio().get() : nullptr)) << "/" << doc.getName()
        << "/" << (int) sampler.getChops().size() << "/" << bank << "/" << project.getCurrentIndex() << "/" << project.getBanks().size()
        << "/" << engine.getTransport().getBpm() << "/" << (int) engine.getMpc().fullLevel.load();
    for (const auto& c : sampler.getChops())
        sig << "," << c;
    for (int p = 0; p < kPadsPerBank; ++p)
    {
        const auto& pad = project.currentBank().pads[(size_t) (bank * kPadsPerBank + p)];
        sig << "|" << pad.name << (pad.isEmpty() ? "0" : "1");
    }
    if (sig != shownSignature)
    {
        const bool soundChanged = shownSignature.upToFirstOccurrenceOf ("/", false, false)
                                  != sig.upToFirstOccurrenceOf ("/", false, false);
        shownSignature = sig;
        if (soundChanged)
            selectedSlice = 0;
        rebuildPeaks();
        refresh();
    }

    if (sampler.isPlaying() || sampler.getPlayPosition() >= 0)
        repaint (waveArea);
}

//==============================================================================
float SamplerScreen::xForSample (int sample) const
{
    const auto& doc = engine.getSampler().getDocument();
    if (! doc.hasAudio())
        return (float) waveArea.getX();
    const double share = (double) sample / doc.getAudio()->getNumSamples();
    return (float) (waveArea.getX() + 8 + (share - view.getStart()) / view.getLength() * (waveArea.getWidth() - 16));
}

int SamplerScreen::sampleForX (float x) const
{
    const auto& doc = engine.getSampler().getDocument();
    if (! doc.hasAudio())
        return 0;
    const double share = view.getStart() + (x - waveArea.getX() - 8) / juce::jmax (1.0, (double) waveArea.getWidth() - 16) * view.getLength();
    return juce::jlimit (0, doc.getAudio()->getNumSamples() - 1, (int) std::round (share * doc.getAudio()->getNumSamples()));
}

int SamplerScreen::markerAt (juce::Point<float> p) const
{
    const auto& chops = engine.getSampler().getChops();
    int best = -1;
    float bestDistance = 9.0f;
    for (size_t i = 0; i < chops.size(); ++i)
    {
        const float d = std::abs (xForSample (chops[i]) - p.x);
        if (d < bestDistance)
            best = (int) i, bestDistance = d;
    }
    return best;
}

juce::Rectangle<float> SamplerScreen::padRect (int padInBank) const
{
    auto r = padsWell.reduced (14).toFloat();
    const float gap = 10.0f;
    const float w = (r.getWidth() - 3 * gap) / 4.0f;
    const float h = juce::jlimit (w * 0.6f, w * 1.1f, (r.getHeight() - 3 * gap) / 4.0f);
    const auto grid = juce::Rectangle<float> (w * 4 + gap * 3, h * 4 + gap * 3).withCentre (r.getCentre());
    const int column = padInBank % 4, row = 3 - padInBank / 4;   // pad 1 bottom-left, like the MPC
    return { grid.getX() + column * (w + gap), grid.getY() + row * (h + gap), w, h };
}

int SamplerScreen::padAt (juce::Point<float> p) const
{
    for (int i = 0; i < kPadsPerBank; ++i)
        if (padRect (i).contains (p))
            return i;
    return -1;
}

//==============================================================================
void SamplerScreen::paint (juce::Graphics& g)
{
    rebuildPeaks();
    drawFace (g, getLocalBounds());
    const auto& p = pal();
    auto& sampler = engine.getSampler();
    const auto& doc = sampler.getDocument();
    const auto& chops = sampler.getChops();

    // The sound.
    drawWell (g, waveWell.toFloat(), 12.0f);
    {
        auto row = titleRow;
        const auto name = doc.hasAudio() ? (doc.getName().isNotEmpty() ? doc.getName() : juce::String ("Untitled")) : juce::String ("No sound");
        g.setColour (p.wellInk);
        g.setFont (cond (24.0f, Weight::extra, 0.08f));
        const int nameWidth = juce::jmin (row.getWidth() / 3, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name.toUpperCase()) + 4);
        g.drawText (name.toUpperCase(), row.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        row.removeFromLeft (12);

        juce::String info;
        if (doc.hasAudio())
        {
            const auto& audio = *doc.getAudio();
            const double secs = audio.getNumSamples() / crate::kSampleRate;
            const double bars = secs * engine.getTransport().getBpm() / 240.0;
            info << (std::abs (bars - std::round (bars)) < 0.08 ? juce::String (juce::roundToInt (bars)) : juce::String (bars, 1))
                 << (std::abs (bars - 1.0) < 0.08 ? " bar" : " bars") << juce::String::fromUTF8 (" \xc2\xb7 ") << juce::String (secs, 1)
                 << juce::String::fromUTF8 (" s \xc2\xb7 ") << (audio.getNumChannels() > 1 ? "stereo" : "mono");
        }
        const int chopLeft = chopButtons[0].getX() - 52;
        engraved (g, info, row.withRight (chopLeft - 8), juce::Justification::centredLeft, 13.0f, true);
        engraved (g, "Chop", juce::Rectangle<int> (chopLeft, row.getY(), 46, row.getHeight()), juce::Justification::centredRight, 13.0f, true);
    }

    // The wave.
    const auto wave = waveArea.toFloat();
    g.setColour (juce::Colour (0xff0e0c0a));
    g.fillRoundedRectangle (wave, 8.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (waveArea);

        if (! doc.hasAudio() || peaks.empty())
        {
            g.setColour (p.wellInk2);
            g.setFont (barlow (16.0f));
            g.drawText ("Drop a sound here, press Open, or sample any tool (More > Sample).", waveArea, juce::Justification::centred);
        }
        else
        {
            const int length = doc.getAudio()->getNumSamples();
            const auto selected = sliceRange (selectedSlice);

            // The picked slice, lit.
            const float sx = juce::jmax (wave.getX(), xForSample (selected.getStart()));
            const float ex = juce::jmin (wave.getRight(), xForSample (selected.getEnd()));
            if (ex > sx)
            {
                g.setColour (colours::selected.withAlpha (0.14f));
                g.fillRect (juce::Rectangle<float> (sx, (float) rulerArea.getBottom(), ex - sx, wave.getBottom() - rulerArea.getBottom()));
            }

            // The bars.
            const auto body = juce::Rectangle<float> (wave.getX() + 8, (float) rulerArea.getBottom() + 4, wave.getWidth() - 16,
                                                      wave.getBottom() - rulerArea.getBottom() - 12);
            const int bars = (int) (body.getWidth() / (kBarWidth + kBarGap));
            for (int b = 0; b < bars; ++b)
            {
                const double s0 = view.getStart() + view.getLength() * b / bars;
                const double s1 = view.getStart() + view.getLength() * (b + 1) / bars;
                const int i0 = juce::jlimit (0, (int) peaks.size() - 1, (int) (s0 * peaks.size()));
                const int i1 = juce::jlimit (i0 + 1, (int) peaks.size(), (int) std::ceil (s1 * peaks.size()));
                float peak = 0;
                for (int i = i0; i < i1; ++i)
                    peak = juce::jmax (peak, peaks[(size_t) i]);
                const float h = juce::jmax (2.0f, std::sqrt (peak) * body.getHeight());
                const int sample = (int) ((s0 + s1) * 0.5 * length);
                g.setColour (selected.contains (sample) ? colours::selected : juce::Colour (0xffe6d8b8).withAlpha (0.8f));
                g.fillRoundedRectangle (body.getX() + b * (kBarWidth + kBarGap), body.getCentreY() - h / 2, kBarWidth, h, 1.0f);
            }

            // The markers, each slice numbered at its start.
            const int slices = (int) chops.size() + 1;
            g.setFont (lcd (19.0f));
            for (int s = 0; s < slices; ++s)
            {
                const int start = s == 0 ? 0 : chops[(size_t) s - 1];
                const float x = xForSample (start);
                if (x < wave.getX() - 30 || x > wave.getRight())
                    continue;
                const bool isSelected = s == selectedSlice;
                if (s > 0)
                {
                    g.setColour ((isSelected ? colours::selected : juce::Colour (0xffe6d8b8)).withAlpha (0.75f));
                    g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) rulerArea.getY() + 2, 2.0f, wave.getBottom() - rulerArea.getY() - 2));
                }
                const auto tag = juce::Rectangle<float> (x + (s == 0 ? 0.0f : 1.0f), (float) rulerArea.getY() + 3,
                                                         s + 1 >= 10 ? 26.0f : 18.0f, (float) rulerArea.getHeight() - 5);
                g.setColour (isSelected ? colours::selected : juce::Colour (0xff2e2925));
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (isSelected ? juce::Colour (0xff2a1a05) : p.wellInk);
                g.drawText (juce::String (s + 1), tag, juce::Justification::centred);
            }

            // Where it's playing.
            const int position = sampler.getPlayPosition();
            if (position >= 0)
            {
                const float x = xForSample (position);
                glow (g, juce::Rectangle<float> (x - 1.5f, (float) rulerArea.getBottom(), 3.0f, wave.getBottom() - rulerArea.getBottom()),
                      1.0f, juce::Colour (0xfffff3d6), 8.0f, 0.4f);
                g.setColour (juce::Colour (0xfffff3d6));
                g.fillRect (juce::Rectangle<float> (x - 1.5f, (float) rulerArea.getBottom(), 3.0f, wave.getBottom() - rulerArea.getBottom()));
            }
        }
    }

    // The scroll bar.
    {
        const auto track = scrollArea.toFloat();
        g.setColour (juce::Colour (0xff2e2925));
        g.fillRoundedRectangle (track, 5.0f);
        const auto inner = track.reduced (2.0f);
        const auto thumb = juce::Rectangle<float> (inner.getX() + (float) view.getStart() * inner.getWidth(), inner.getY(),
                                                   juce::jmax (12.0f, (float) view.getLength() * inner.getWidth()), inner.getHeight());
        g.setColour (colours::selected.withAlpha (0.35f));
        g.fillRoundedRectangle (thumb, 4.0f);
        g.setColour (colours::selected);
        g.drawRoundedRectangle (thumb.reduced (1.0f), 4.0f, 2.0f);
    }

    // The picked slice's pad.
    {
        g.setColour (p.line);
        g.drawRoundedRectangle (sliceBox.toFloat().reduced (1.0f), 12.0f, 2.0f);
        const int pad = getSelectedPad();
        const auto& padData = engine.getMpc().getProject().currentBank().pads[(size_t) pad];
        juce::String title;
        if (selectedSlice < numSlices())
            title << "Slice " << (selectedSlice + 1) << juce::String::fromUTF8 (" \xe2\x86\x92 ");
        title << "Pad " << padLabel (pad);
        if (padData.isEmpty())
            title << juce::String::fromUTF8 (" \xc2\xb7 empty");
        g.setColour (p.ink);
        g.setFont (cond (22.0f, Weight::extra, 0.1f));
        auto t = sliceTitle;
        t.setRight (toPadsButton.getX() - 8);
        g.drawText (title.toUpperCase(), t, juce::Justification::centredLeft, true);

        if (doc.hasAudio() && selectedSlice < numSlices())
        {
            const auto r = sliceRange (selectedSlice);
            const auto box = juce::Rectangle<float> (0, 0, 176, 32).withPosition ((float) sliceTitle.getRight() - 176,
                                                                                  (float) sliceTitle.getCentreY() - 16);
            drawLcd (g, box);
            lcdText (g, seconds (r.getStart()) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + seconds (r.getEnd()), box, 22.0f);
        }
    }

    // Send to the MPC2000XL.
    {
        drawWell (g, sendWell.toFloat(), 12.0f);
        g.setColour (p.wellInk);
        g.setFont (cond (22.0f, Weight::extra, 0.08f));
        g.drawText ("SEND TO MPC2000XL", sendTitle, juce::Justification::centredLeft, true);

        auto& project = engine.getMpc().getProject();
        int kits = 0, sounds = 0;
        size_t bytes = 0;
        for (const auto& bank : project.getBanks())
            if (const int used = bank.usedPads(); used > 0)
                ++kits, sounds += used, bytes += bank.memoryBytes();
        const auto cap = capacityOf (medium);
        const bool fits = cap == 0 || bytes <= cap;
        drawLamp (g, { (float) sendTitle.getRight() - 8, (float) sendTitle.getCentreY() }, sounds == 0 ? Lit::none : fits ? Lit::play : Lit::arm, 14.0f);

        juce::String info;
        if (sounds == 0)
            info = "No sounds on the pads yet. Chop a sound and press To pads.";
        else
            info << (kits == 1 ? "Kit " + project.currentBank().name : juce::String (kits) + " kits") << juce::String::fromUTF8 (" \xc2\xb7 ")
                 << sounds << " samples" << juce::String::fromUTF8 (" \xc2\xb7 ")
                 << juce::String ((double) bytes / (1024.0 * 1024.0), 1) << " MB" << juce::String::fromUTF8 (" \xc2\xb7 16-bit 44.1 kHz \xc2\xb7 ")
                 << "sequences as .MID" << juce::String::fromUTF8 (" \xc2\xb7 ") << "names cut to 16 characters";
        if (! fits)
            info << ". Too big for a " << mediumName (medium).toLowerCase() << ".";
        g.setColour (fits ? p.wellInk : colours::arm.brighter (0.3f));
        g.setFont (barlow (14.0f));
        g.drawFittedText (info, sendInfo, juce::Justification::topLeft, 3, 0.9f);
    }

    // The pads.
    {
        engraved (g, "Pad bank", padsHeader.withWidth (84), juce::Justification::centredLeft, 13.0f);
        drawWell (g, padsWell.toFloat(), 12.0f);
        const int bank = engine.getMpc().padBankOnScreen.load();
        const auto& kit = engine.getMpc().getProject().currentBank();
        const auto now = juce::Time::getMillisecondCounter();
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            const auto r = padRect (i);
            const int pad = bank * kPadsPerBank + i;
            const auto& padData = kit.pads[(size_t) pad];
            const bool lit = flashUntil[(size_t) i] != 0 && now <= flashUntil[(size_t) i];
            const bool picked = pad == getSelectedPad();
            const bool down = heldPad == i;
            {
                juce::Graphics::ScopedSaveState s (g);
                if (padData.isEmpty() && ! lit)
                    g.setOpacity (0.55f);
                drawButton (g, r, lit ? Lit::play : Lit::none, down, false, false, 8.0f);
            }
            const auto face = r.withTrimmedBottom (3.0f).translated (0, down ? 2.0f : 0.0f);
            if (picked)
            {
                g.setColour (colours::selected);
                g.drawRoundedRectangle (face.expanded (2.0f), 10.0f, 3.0f);
            }
            auto inner = face.reduced (9.0f, 7.0f);
            const auto ink = lit ? juce::Colour (0xff12361f) : p.btnInk.withAlpha (padData.isEmpty() ? 0.6f : 1.0f);
            auto top = inner.removeFromTop (22.0f);
            drawLamp (g, { top.getRight() - 6.0f, top.getCentreY() }, lit ? Lit::play : Lit::none, 12.0f);
            g.setColour (ink);
            g.setFont (cond (juce::jmin (18.0f, r.getHeight() * 0.2f), Weight::extra, 0.06f));
            g.drawText (padLabel (pad), top, juce::Justification::centredLeft);
            g.setFont (barlow (juce::jmin (12.0f, r.getHeight() * 0.13f)));
            g.setColour (ink.withMultipliedAlpha (0.85f));
            g.drawFittedText (padData.isEmpty() ? juce::String ("Empty") : padData.name, inner.toNearestInt(), juce::Justification::bottomLeft, 2, 0.9f);
        }
        engraved (g, "Like the MPC: pad 1 bottom-left " + juce::String::fromUTF8 ("\xc2\xb7") + " Launchkey pads play them", padsNote,
                  juce::Justification::centred, 11.0f);
    }
}

void SamplerScreen::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (22).withTrimmedRight (22).withTrimmedTop (6).withTrimmedBottom (14);
    const float scale = juce::jlimit (0.7f, 1.0f, r.getHeight() / 560.0f);

    auto right = r.removeFromRight (juce::jlimit (300, 430, getWidth() * 30 / 100));
    r.removeFromRight (16);
    leftArea = r;

    // Pads.
    padsHeader = right.removeFromTop (46);
    {
        auto h = padsHeader;
        h.removeFromLeft (88);
        for (auto& b : bankButtons)
        {
            b.setBounds (h.removeFromLeft (52));
            h.removeFromLeft (8);
        }
        fullLevel.setBounds (h.removeFromRight (juce::jmin (120, h.getWidth())));
    }
    right.removeFromTop (12);
    padsNote = right.removeFromBottom (20);
    right.removeFromBottom (8);
    padsWell = right;

    // The sound.
    const int waveHeight = juce::roundToInt (210 * scale);
    waveWell = leftArea.removeFromTop (14 + 44 + 10 + waveHeight + 10 + 44 + 14);
    leftArea.removeFromTop (14);
    {
        auto w = waveWell.reduced (16, 14);
        titleRow = w.removeFromTop (44);
        {
            auto t = titleRow;
            snapButton.setBounds (t.removeFromRight (122));
            t.removeFromRight (10);
            const int widths[3] = { 112, 92, 92 };
            for (int i = 2; i >= 0; --i)
            {
                chopButtons[i].setBounds (t.removeFromRight (widths[i]));
                t.removeFromRight (6);
            }
        }
        w.removeFromTop (10);
        waveArea = w.removeFromTop (waveHeight);
        rulerArea = waveArea.withHeight (24);
        w.removeFromTop (10);
        zoomRow = w;
        auto z = zoomRow;
        editorButton.setBounds (z.removeFromRight (86));
        z.removeFromRight (6);
        openButton.setBounds (z.removeFromRight (74));
        z.removeFromRight (16);
        zoomOut.setBounds (z.removeFromLeft (46));
        zoomIn.setBounds (z.removeFromRight (46));
        scrollArea = z.reduced (10, 0).withSizeKeepingCentre (z.getWidth() - 20, 20);
    }

    // Send to MPC, and the picked slice's pad.
    sendWell = leftArea.removeFromRight (juce::jmin (360, leftArea.getWidth() / 2));
    leftArea.removeFromRight (14);
    sliceBox = leftArea;
    {
        auto s = sendWell.reduced (16, 12);
        sendTitle = s.removeFromTop (30);
        sendButton.setBounds (s.removeFromBottom (juce::roundToInt (52 * scale)));
        s.removeFromBottom (8);
        auto mediums = s.removeFromBottom (40);
        const int w = (mediums.getWidth() - 12) / 3;
        for (int i = 0; i < 3; ++i)
        {
            mediumButtons[i].setBounds (mediums.removeFromLeft (w));
            mediums.removeFromLeft (6);
        }
        s.removeFromBottom (6);
        sendInfo = s;
    }
    {
        auto s = sliceBox.reduced (16, 12);
        sliceTitle = s.removeFromTop (34);
        toPadsButton.setBounds (sliceTitle.getRight() - 176 - 10 - 96, sliceTitle.getY(), 96, sliceTitle.getHeight());
        s.removeFromTop (6);
        const int w = s.getWidth() / (int) knobs.size();
        for (auto& k : knobs)
            k->setBounds (s.removeFromLeft (w).withSizeKeepingCentre (juce::jmin (w, 110), juce::jmin (s.getHeight(), 150)));
    }
}

//==============================================================================
void SamplerScreen::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    dragMarker = -1;
    draggingScroll = false;

    if (const int pad = padAt (p); pad >= 0)
    {
        heldPad = pad;
        pressPad (pad);
        return;
    }

    if (scrollArea.expanded (0, 6).contains (p.toInt()))
    {
        const auto inner = scrollArea.toFloat().reduced (2.0f);
        const double at = (p.x - inner.getX()) / juce::jmax (1.0f, inner.getWidth());
        if (! view.contains (at))
            view = view.movedToStartAt (juce::jlimit (0.0, 1.0 - view.getLength(), at - view.getLength() / 2));
        draggingScroll = true;
        scrollGrab = at - view.getStart();
        repaint();
        return;
    }

    const auto& doc = engine.getSampler().getDocument();
    if (! waveArea.contains (p.toInt()) || ! doc.hasAudio())
        return;

    const int marker = markerAt (p);
    if (rulerArea.contains (p.toInt()) || (marker >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown())))
    {
        if (marker >= 0 && (e.mods.isPopupMenu() || e.mods.isAltDown()))
            removeMarker (marker);
        else if (marker >= 0)
            dragMarker = marker;
        else
            addMarker (sampleForX (p.x));
        return;
    }
    if (marker >= 0)
    {
        dragMarker = marker;
        return;
    }

    // A click in the wave picks the slice there and plays it.
    const int sample = sampleForX (p.x);
    const auto& chops = engine.getSampler().getChops();
    const int slice = (int) (std::upper_bound (chops.begin(), chops.end(), sample) - chops.begin());
    selectSlice (slice);
    play (sliceRange (slice));
}

void SamplerScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (draggingScroll)
    {
        const auto inner = scrollArea.toFloat().reduced (2.0f);
        const double at = (e.position.x - inner.getX()) / juce::jmax (1.0f, inner.getWidth());
        view = view.movedToStartAt (juce::jlimit (0.0, 1.0 - view.getLength(), at - scrollGrab));
        repaint();
    }
    else if (dragMarker >= 0)
        moveMarker (dragMarker, sampleForX (e.position.x));
}

void SamplerScreen::mouseUp (const juce::MouseEvent&)
{
    if (heldPad >= 0)
    {
        engine.getMpc().releasePad (engine.getMpc().padBankOnScreen.load() * kPadsPerBank + heldPad);
        heldPad = -1;
        repaint (padsWell);
    }
    dragMarker = -1;
    draggingScroll = false;
}

void SamplerScreen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! waveArea.contains (e.position.toInt()) || peaksOf == nullptr)
        return;
    if (std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        view = view.movedToStartAt (juce::jlimit (0.0, 1.0 - view.getLength(), view.getStart() - wheel.deltaX * view.getLength()));
    }
    else
    {
        // Zoom around the pointer.
        const auto& doc = engine.getSampler().getDocument();
        const double at = (double) sampleForX (e.position.x) / doc.getAudio()->getNumSamples();
        const double minLength = juce::jmin (1.0, 2000.0 / doc.getAudio()->getNumSamples());
        const double l = juce::jlimit (minLength, 1.0, view.getLength() * (wheel.deltaY > 0 ? 0.8 : 1.25));
        const double share = (at - view.getStart()) / view.getLength();
        view = juce::Range<double> (0, l).movedToStartAt (juce::jlimit (0.0, 1.0 - l, at - share * l));
    }
    repaint();
}

bool SamplerScreen::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf"))
            return true;
    return false;
}

void SamplerScreen::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf"))
        {
            editor.openSoundFile (juce::File (f));
            return;
        }
}
