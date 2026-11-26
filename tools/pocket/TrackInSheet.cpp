#include "TrackInSheet.h"

#include "PocketFrame.h"
#include "core/AudioFiles.h"

#include <cmath>

using namespace pocket;

namespace
{
constexpr int kPeaks = 600;

juce::String bpmText (double bpm)
{
    return juce::String (bpm, 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}

std::vector<float> peaksOf (const juce::AudioBuffer<float>& a, int columns)
{
    std::vector<float> out ((size_t) columns, 0.0f);
    const int n = a.getNumSamples();
    if (n <= 0)
        return out;
    for (int c = 0; c < columns; ++c)
    {
        const int from = (int) ((juce::int64) n * c / columns), to = juce::jmax (from + 1, (int) ((juce::int64) n * (c + 1) / columns));
        float p = 0.0f;
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            p = juce::jmax (p, a.getMagnitude (ch, from, juce::jmin (n, to) - from));
        out[(size_t) c] = juce::jmin (1.0f, p);
    }
    return out;
}

// The song's waveform between `from` and `to` (seconds) over `r`.
void drawWave (juce::Graphics& g, const juce::AudioBuffer<float>* a, const std::vector<float>& peaks, juce::Rectangle<float> r, double from, double to)
{
    if (a == nullptr || to <= from)
        return;
    const double length = a->getNumSamples() / RigFriendTrack::kRate;
    const int cols = (int) r.getWidth();
    g.setColour (col::blue.withAlpha (0.85f));
    for (int x = 0; x < cols; ++x)
    {
        const double t0 = from + (to - from) * x / cols, t1 = from + (to - from) * (x + 1) / cols;
        if (t1 <= 0.0 || t0 >= length)
            continue;
        float p = 0.0f;
        if ((to - from) / length > 0.25)
        {
            // the whole song: from the peaks
            const int i0 = (int) (juce::jmax (0.0, t0) / length * (double) peaks.size()), i1 = (int) (juce::jmin (length, t1) / length * (double) peaks.size());
            for (int i = i0; i <= i1 && i < (int) peaks.size(); ++i)
                p = juce::jmax (p, peaks[(size_t) i]);
        }
        else
        {
            const int s0 = (int) (juce::jmax (0.0, t0) * RigFriendTrack::kRate), s1 = juce::jmin (a->getNumSamples(), (int) (t1 * RigFriendTrack::kRate) + 1);
            for (int ch = 0; ch < a->getNumChannels() && s1 > s0; ++ch)
                p = juce::jmax (p, a->getMagnitude (ch, s0, s1 - s0));
        }
        const float h = juce::jmin (1.0f, p) * r.getHeight() * 0.48f;
        g.fillRect (r.getX() + (float) x, r.getCentreY() - h, 1.0f, juce::jmax (1.0f, h * 2.0f));
    }
}
}

TrackInSheet::TrackInSheet()
{
    for (auto* b : { &minus, &plus, &tapButton, &halfButton, &doubleButton, &heardButton, &beatLeft, &msLeft, &msRight, &beatRight, &findButton,
                     &listenButton, &keyButton, &tempoToggle, &keyToggle, &sectionsToggle, &cancelButton, &bringButton })
    {
        b->setFontHeight (15.0f);
        addAndMakeVisible (*b);
    }
    minus.setFontHeight (26.0f);
    plus.setFontHeight (26.0f);
    bringButton.setFontHeight (17.0f);
    minus.onClick = [this] { setTempo (std::round (info.bpm - 1.0)); };
    plus.onClick = [this] { setTempo (std::round (info.bpm + 1.0)); };
    tapButton.onClick = [this] { tap(); };
    halfButton.onClick = [this] { setTempo (info.bpm / 2.0); };
    doubleButton.onClick = [this] { setTempo (info.bpm * 2.0); };
    heardButton.onClick = [this] { if (heard.bpm > 0) setTempo (heard.bpm); };
    beatLeft.onClick = [this] { nudgeBeats (-1); };
    beatRight.onClick = [this] { nudgeBeats (1); };
    msLeft.onClick = [this] { nudge (-0.01); };
    msRight.onClick = [this] { nudge (0.01); };
    findButton.onClick = [this] { findBarOne(); };
    listenButton.onClick = [this] { toggleListen(); };
    keyButton.onClick = [this] { showKeyMenu(); };
    tempoToggle.onClick = [this] { useTempo = ! useTempo; tryIt(); update(); };
    keyToggle.onClick = [this] { useKey = ! useKey; update(); };
    sectionsToggle.onClick = [this] { useSections = ! useSections; update(); };
    cancelButton.onClick = [this] { cancel(); if (onCancel) onCancel(); };
    bringButton.onClick = [this]
    {
        const auto said = bringIn();
        if (committed && onDone)
            onDone (said);
        else if (! committed && said.isNotEmpty())
        {
            problem = said;
            update();
        }
    };
    setDragZone (50);
    update();
}

TrackInSheet::~TrackInSheet()
{
    stopTimer();
}

//==============================================================================
void TrackInSheet::begin()
{
    // what to put back
    beforeAudio = track != nullptr ? track->getAudio() : nullptr;
    beforeInfo = track != nullptr ? track->getInfo() : RigFriendTrack::Info {};
    beforeBpm = getBpm ? getBpm() : 120.0;
    committed = false;
    tried = false;
    listening = false;
    problem = {};
    taps.clear();
    ++generation;
    startTimerHz (20);
}

void TrackInSheet::openFile (const juce::File& file, bool inBackground)
{
    begin();
    isNew = true;
    audio = nullptr;
    peaks.clear();
    heard = {};
    info = {};
    info.name = file.getFileNameWithoutExtension();
    info.bpm = beforeBpm;
    loading = true;
    update();
    auto work = [file, gen = generation] (TrackInSheet* self, bool now)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        juce::String error;
        auto a = crate::AudioFiles::load (file, formats, error);
        crate::TrackAnalysis::Result h;
        if (a != nullptr)
            h = crate::TrackAnalysis::analyse (*a, RigFriendTrack::kRate, 4);
        // a tempo in its name beats a guess (it's what your friend wrote)
        if (a != nullptr)
            if (const double named = crate::TrackAnalysis::bpmFromName (file.getFileName()); named > 0)
            {
                if (h.bpm <= 0 || std::abs (named - h.bpm) > 0.6)
                    h.firstBeat = crate::TrackAnalysis::guessFirstBeat (*a, RigFriendTrack::kRate, named, 4);
                h.bpm = named;
            }
        if (now)
            self->loaded (a, h, error, gen);
        else
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TrackInSheet> (self), a, h, error, gen]
            {
                if (safe != nullptr)
                    safe->loaded (a, h, error, gen);
            });
    };
    if (inBackground)
        juce::Thread::launch ([work, self = this] { work (self, false); });
    else
        work (this, true);
}

void TrackInSheet::loaded (crate::SharedAudio a, crate::TrackAnalysis::Result h, juce::String error, int gen)
{
    if (gen != generation)
        return;   // an older file
    loading = false;
    if (a == nullptr)
    {
        problem = "Couldn't open " + info.name + (error.isNotEmpty() ? ": " + error : juce::String ("."));
        update();
        return;
    }
    audio = std::move (a);
    heard = h;
    // (the heard tempo, without the name's: for HEARD)
    heard.bpm = crate::TrackAnalysis::guessBpm (*audio, RigFriendTrack::kRate);
    peaks = peaksOf (*audio, kPeaks);
    info.bpm = h.bpm > 0 ? h.bpm : beforeBpm;
    info.firstBeat = h.firstBeat;
    info.key = h.key;
    info.minor = h.minor;
    info.beatsPerBar = 4;
    tryIt();
    update();
}

void TrackInSheet::openCurrent()
{
    begin();
    isNew = false;
    audio = beforeAudio;
    info = beforeInfo;
    info.beatsPerBar = 4;
    heard = {};
    loading = false;
    if (audio != nullptr)
    {
        peaks = peaksOf (*audio, kPeaks);
        heard.bpm = crate::TrackAnalysis::guessBpm (*audio, RigFriendTrack::kRate);
    }
    else
        problem = "There's no track in: bring one in first.";
    tried = true;
    update();
}

void TrackInSheet::cancel()
{
    stopTimer();
    ++generation;
    loading = false;
    if (committed)
        return;
    committed = true;
    if (listening && listen)
        listen (false);
    listening = false;
    if (track != nullptr && tried)
    {
        if (beforeAudio == nullptr)
            track->clear();
        else if (beforeAudio == track->getAudio())
            track->setInfo (beforeInfo);
        else
            track->set (beforeAudio, beforeInfo);
    }
    if (setBpm && getBpm && std::abs (getBpm() - beforeBpm) > 1.0e-6)
        setBpm (beforeBpm);
}

//==============================================================================
void TrackInSheet::tryIt()
{
    if (audio == nullptr || track == nullptr || committed)
        return;
    if (track->getAudio() != audio)
        track->set (audio, info);
    else
        track->setInfo (info);
    tried = true;
    if (setBpm && getBpm)
    {
        const double want = useTempo ? info.bpm : beforeBpm;
        if (std::abs (getBpm() - want) > 1.0e-6)
            setBpm (want);
    }
}

void TrackInSheet::setTempo (double bpm)
{
    if (! (bpm >= 40.0 && bpm <= 240.0))
        return;
    // (bar 1 stays where it is in the song)
    info.bpm = std::round (bpm * 100.0) / 100.0;
    tryIt();
    update();
}

void TrackInSheet::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (! taps.empty() && now - taps.back() > 2.0)
        taps.clear();
    taps.push_back (now);
    if (taps.size() > 8)
        taps.erase (taps.begin());
    if (const double b = crate::TrackAnalysis::tapTempo (taps); b > 0)
        setTempo (std::round (b * 10.0) / 10.0);
    else
        update();
}

void TrackInSheet::setFirstBeat (double seconds)
{
    if (audio == nullptr)
        return;
    const double length = audio->getNumSamples() / RigFriendTrack::kRate;
    info.firstBeat = juce::jlimit (0.0, juce::jmax (0.0, length - 1.0), seconds);
    tryIt();
    update();
}

void TrackInSheet::findBarOne()
{
    if (audio == nullptr)
        return;
    setFirstBeat (crate::TrackAnalysis::guessFirstBeat (*audio, RigFriendTrack::kRate, info.bpm, 4));
}

void TrackInSheet::toggleListen()
{
    if (audio == nullptr || ! listen)
        return;
    listening = ! listening;
    tryIt();
    listen (listening);
    update();
}

juce::String TrackInSheet::bringIn()
{
    if (audio == nullptr)
        return problem.isNotEmpty() ? problem : juce::String ("The track is still being read.");
    if (listening && listen)
        listen (false);
    listening = false;
    tryIt();   // the track as set here, in the looper
    committed = true;
    stopTimer();
    info.beatsPerBar = 4;
    const double bars = track != nullptr ? track->lengthBars() : 0.0;
    juce::String said;
    said << info.name << " is in: " << juce::String ((int) std::ceil (bars - 1.0e-6)) << " bars from bar 1 at " << bpmText (info.bpm) << " BPM.";
    if (setBpm)
        setBpm (useTempo ? info.bpm : beforeBpm);
    if (! useTempo && std::abs (beforeBpm - info.bpm) > 0.05)
        said << " The song stays at " << bpmText (beforeBpm) << " BPM, so it plays " << (beforeBpm > info.bpm ? "faster" : "slower") << ", like a tape.";
    if (useKey && info.key >= 0 && setKey)
    {
        setKey ({ info.key, info.minor });
        said << " Key: " << crate::TrackAnalysis::keyName (info.key, info.minor) << ".";
    }
    if (useSections && songHasSections && ! songHasSections() && markSections)
    {
        const auto secs = crate::TrackAnalysis::guessSections (*audio, RigFriendTrack::kRate, info.bpm, info.firstBeat, 4);
        if (! secs.empty())
        {
            markSections (secs);
            said << " Marked " << (int) secs.size() << " sections.";
        }
    }
    if (keep)
        keep (audio, info, isNew);
    return said;
}

//==============================================================================
void TrackInSheet::showKeyMenu()
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    juce::PopupMenu m, major, minor;
    for (int k = 0; k < 12; ++k)
    {
        minor.addItem (100 + k, juce::String (names[k]) + " minor", true, info.key == k && info.minor);
        major.addItem (200 + k, juce::String (names[k]) + " major", true, info.key == k && ! info.minor);
    }
    m.addSectionHeader ("WHAT KEY IT'S IN");
    if (heard.key >= 0)
        m.addItem (1, "Heard: " + crate::TrackAnalysis::keyName (heard.key, heard.minor));
    m.addSubMenu ("Minor", minor);
    m.addSubMenu ("Major", major);
    m.addItem (2, "Not sure (leave the song's key)", true, info.key < 0);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton),
                     [safe = juce::Component::SafePointer<TrackInSheet> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         auto& in = safe->info;
                         if (r == 1)
                         {
                             in.key = safe->heard.key;
                             in.minor = safe->heard.minor;
                         }
                         else if (r == 2)
                             in.key = -1;
                         else
                         {
                             in.key = (r % 100);
                             in.minor = r < 200;
                         }
                         safe->tryIt();
                         safe->update();
                     });
}

void TrackInSheet::update()
{
    const bool ok = audio != nullptr;
    for (auto* b : { &minus, &plus, &tapButton, &halfButton, &doubleButton, &beatLeft, &msLeft, &msRight, &beatRight, &findButton, &listenButton, &keyButton, &bringButton })
        b->setEnabled (ok);
    heardButton.setEnabled (ok && heard.bpm > 0);
    heardButton.setText (heard.bpm > 0 ? "HEARD " + bpmText (std::round (heard.bpm * 10.0) / 10.0) : juce::String ("HEARD"));
    listenButton.setText (listening ? U8("\xe2\x96\xa0 STOP") : U8("\xe2\x96\xb6 LISTEN + CLICK"));
    listenButton.setLook (listening ? Look::red : Look::green);
    keyButton.setText ("KEY: " + (info.key >= 0 ? crate::TrackAnalysis::keyName (info.key, info.minor).toUpperCase() : juce::String ("NOT SET")) + U8(" \xe2\x96\xbe"));
    auto toggle = [] (PButton& b, bool on) { b.setLamp (true, on); b.setLook (on ? Look::amber : Look::cream); };
    toggle (tempoToggle, useTempo);
    toggle (keyToggle, useKey);
    toggle (sectionsToggle, useSections);
    bringButton.setText (isNew || ! ok ? "BRING IT IN" : "DONE");
    repaint();
}

void TrackInSheet::timerCallback()
{
    if (listening && clockRunning && ! clockRunning())
    {
        listening = false;   // stopped from the thumb bar
        update();
    }
    if (listening || loading)
        repaint();
}

juce::Range<double> TrackInSheet::closeUp() const
{
    const double bar = 4.0 * 60.0 / juce::jmax (1.0, info.bpm);
    const double from = juce::jmax (0.0, info.firstBeat - bar);
    return { from, from + bar * 3.0 };
}

//==============================================================================
void TrackInSheet::resized()
{
    const int w = getWidth(), x0 = 20, iw = w - 40;
    infoR = { (float) x0, 52.0f, (float) iw, 40.0f };
    overR = { (float) x0, 96.0f, (float) iw, 60.0f };
    closeR = { (float) x0, 162.0f, (float) iw, 70.0f };
    int y = 242;
    auto rowOf = [&] (std::initializer_list<PButton*> bs, int h)
    {
        const int g = 6, n = (int) bs.size(), bw = (iw - g * (n - 1)) / n;
        int x = x0;
        for (auto* b : bs)
        {
            b->setBounds (x, y, bw, h);
            x += bw + g;
        }
        y += h + 8;
    };
    minus.setBounds (x0, y, 60, 52);
    plus.setBounds (x0 + iw - 60, y, 60, 52);
    bpmR = { (float) x0 + 68.0f, (float) y, (float) iw - 136.0f, 52.0f };
    y += 60;
    rowOf ({ &tapButton, &halfButton, &doubleButton, &heardButton }, 42);
    rowOf ({ &beatLeft, &msLeft, &msRight, &beatRight }, 42);
    rowOf ({ &findButton, &listenButton }, 44);
    rowOf ({ &keyButton }, 40);
    rowOf ({ &tempoToggle, &keyToggle, &sectionsToggle }, 40);
    rowOf ({ &cancelButton, &bringButton }, 48);
}

void TrackInSheet::paint (juce::Graphics& g)
{
    g.setColour (col::cream);
    g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedBottom (-20.0f), 22.0f);
    pocket::drawGrabber (g, getWidth());
    g.setColour (col::ink);
    g.setFont (condensedBold (24.0f));
    g.drawText ("Friend's track", 20, 14, getWidth() - 40, 34, juce::Justification::centredLeft);

    // what it is, what it heard
    juce::String line1 = info.name, line2;
    if (loading)
        line2 = U8("Listening to it\xe2\x80\xa6 (its tempo, bar 1 and key)");
    else if (problem.isNotEmpty())
        line2 = problem;
    else if (audio != nullptr)
    {
        const double secs = audio->getNumSamples() / RigFriendTrack::kRate;
        line2 << (int) secs / 60 << ":" << juce::String ((int) secs % 60).paddedLeft ('0', 2) << U8(" \xc2\xb7 bar 1 at ") << juce::String (info.firstBeat, 2) << " s";
        if (heard.key >= 0)
            line2 << U8(" \xc2\xb7 heard ") << crate::TrackAnalysis::keyName (heard.key, heard.minor);
        line2 << U8(". Drag the amber line onto the first big beat.");
    }
    g.setFont (condensedBold (16.0f));
    g.drawText (line1, infoR.withHeight (18.0f), juce::Justification::centredLeft, true);
    g.setColour (problem.isNotEmpty() ? col::red : col::inkDim);
    g.setFont (small (13.0f));
    g.drawFittedText (line2, infoR.withTrimmedTop (18.0f).toNearestInt(), juce::Justification::topLeft, 2);

    // the whole song, then a bar either side of bar 1 with its beats
    const double length = audio != nullptr ? audio->getNumSamples() / RigFriendTrack::kRate : 0.0;
    const double beat = 60.0 / juce::jmax (1.0, info.bpm);
    const bool playing = listening && clockRunning && clockRunning() && clockBeat;
    const double head = playing ? info.firstBeat + clockBeat() * 60.0 / juce::jmax (1.0, info.bpm) : -1.0;
    auto drawArea = [&] (juce::Rectangle<float> r, double from, double to, bool beats)
    {
        drawWell (g, r, 10.0f);
        auto inner = r.reduced (4.0f, 4.0f);
        drawWave (g, audio.get(), peaks, inner, from, to);
        if (audio == nullptr || to <= from)
            return;
        auto xOf = [&] (double t) { return inner.getX() + (float) ((t - from) / (to - from)) * inner.getWidth(); };
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (inner.toNearestInt());
        const double step = beats ? beat : beat * 4.0 * (length / beat / 4.0 > 64 ? 8.0 : 4.0);
        int k = (int) std::floor ((from - info.firstBeat) / step);
        for (double t = info.firstBeat + k * step; t < to; t += step, ++k)
        {
            if (t < from)
                continue;
            const bool bar = beats ? k % 4 == 0 : true;
            g.setColour (juce::Colours::white.withAlpha (bar ? 0.35f : 0.14f));
            g.drawVerticalLine ((int) xOf (t), inner.getY(), inner.getBottom());
        }
        g.setColour (col::amber);
        g.fillRect (xOf (info.firstBeat) - 1.5f, inner.getY(), 3.0f, inner.getHeight());
        if (head >= 0.0)
        {
            g.setColour (col::green);
            g.fillRect (xOf (head) - 1.0f, inner.getY(), 2.0f, inner.getHeight());
        }
    };
    drawArea (overR, 0.0, juce::jmax (1.0, length), false);
    const auto cu = closeUp();
    drawArea (closeR, cu.getStart(), cu.getEnd(), true);
    g.setColour (col::inkFaint);
    g.setFont (condensed (11.0f));
    g.drawText ("WHOLE SONG", overR.reduced (8.0f, 4.0f), juce::Justification::topRight);
    g.drawText ("BAR 1 CLOSE UP: DRAG THE WAVE", closeR.reduced (8.0f, 4.0f), juce::Justification::topRight);

    // the tempo
    drawWell (g, bpmR, 12.0f);
    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (44.0f));
    g.drawText (bpmText (info.bpm) + " BPM", bpmR, juce::Justification::centred);
}

void TrackInSheet::mouseDown (const juce::MouseEvent& e)
{
    dragFrom = info.firstBeat;
    if (audio != nullptr && overR.contains (e.position))
    {
        const double length = audio->getNumSamples() / RigFriendTrack::kRate;
        setFirstBeat ((double) ((e.position.x - overR.getX()) / overR.getWidth()) * length);
        dragFrom = info.firstBeat;
    }
}

void TrackInSheet::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr)
        return;
    const auto down = e.getMouseDownPosition().toFloat();
    if (overR.contains (down))
    {
        const double length = audio->getNumSamples() / RigFriendTrack::kRate;
        setFirstBeat ((double) (juce::jlimit (0.0f, 1.0f, (e.position.x - overR.getX()) / overR.getWidth())) * length);
    }
    else if (closeR.contains (down))
    {
        // the wave follows the finger under the line, at the close-up's scale
        const double span = 3.0 * 4.0 * 60.0 / juce::jmax (1.0, info.bpm);
        setFirstBeat (dragFrom - (double) ((e.position.x - down.x) / closeR.getWidth()) * span);
    }
}
