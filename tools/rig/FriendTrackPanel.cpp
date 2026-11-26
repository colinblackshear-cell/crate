#include "FriendTrackPanel.h"

#include "RigSong.h"
#include "Vintage.h"
#include "core/AudioFiles.h"

#include <algorithm>

using Lit = vintage::Lit;

namespace
{
const juce::Colour kTrackColour { 0xffffc35a };

juce::String bpmText (double bpm)
{
    return juce::String (bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
}

juce::String infoLine (const RigFriendTrack::Info& info, double bars)
{
    juce::String s = bpmText (info.bpm) + " BPM  " + juce::String (info.beatsPerBar) + "/4";
    if (juce::isPositiveAndBelow (info.key, 12))
        s << "  " << crate::TrackAnalysis::keyName (info.key, info.minor);
    if (bars > 0)
        s << "  " << juce::String ((int) std::round (bars)) << " bars";
    return s;
}
}

std::vector<float> friendtrack::peaksOf (const juce::AudioBuffer<float>& audio, int columns)
{
    std::vector<float> out ((size_t) juce::jmax (1, columns), 0.0f);
    const int n = audio.getNumSamples();
    if (n == 0)
        return out;
    for (int c = 0; c < columns; ++c)
    {
        const int a = (int) ((juce::int64) n * c / columns), b = juce::jmax (a + 1, (int) ((juce::int64) n * (c + 1) / columns));
        float p = 0;
        for (int ch = 0; ch < audio.getNumChannels(); ++ch)
            p = juce::jmax (p, audio.getMagnitude (ch, a, juce::jmin (n, b) - a));
        out[(size_t) c] = p;
    }
    const float top = *std::max_element (out.begin(), out.end());
    if (top > 0)
        for (auto& v : out)
            v /= top;
    return out;
}

void friendtrack::drawPeaks (juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<float> area, juce::Colour colour, double from, double to)
{
    if (peaks.empty() || area.getWidth() <= 0)
        return;
    g.setColour (colour);
    const float mid = area.getCentreY(), half = area.getHeight() * 0.5f;
    const int columns = (int) area.getWidth();
    for (int x = 0; x < columns; ++x)
    {
        const double f = from + (to - from) * (x + 0.5) / columns;
        const int i = juce::jlimit (0, (int) peaks.size() - 1, (int) (f * (double) peaks.size()));
        const float h = juce::jmax (0.5f, peaks[(size_t) i] * half);
        g.fillRect (area.getX() + (float) x, mid - h, 1.0f, h * 2.0f);
    }
}

//==============================================================================
FriendTrackSheet::FriendTrackSheet (RigEngine& e, const juce::File& f, bool inBackground) : engine (e), file (f)
{
    build();
    info.name = f.getFileNameWithoutExtension();
    const auto work = [f, bpb = engine.getTransport().getBeatsPerBar()] (crate::SharedAudio& sound, crate::TrackAnalysis::Result& heardOut,
                                                                         std::vector<float>& peaksOut, juce::String& error)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        sound = crate::AudioFiles::load (f, formats, error);
        if (sound != nullptr)
        {
            heardOut = crate::TrackAnalysis::analyse (*sound, RigFriendTrack::kRate, bpb);
            peaksOut = friendtrack::peaksOf (*sound, 1400);
        }
    };
    if (! inBackground)
    {
        crate::SharedAudio sound;
        crate::TrackAnalysis::Result h;
        std::vector<float> p;
        juce::String error;
        work (sound, h, p, error);
        loaded (sound, h, std::move (p), error);
        return;
    }
    // Read and listen in the background: a song takes a moment.
    juce::Thread::launch ([safe = juce::Component::SafePointer<FriendTrackSheet> (this), work]
    {
        crate::SharedAudio sound;
        crate::TrackAnalysis::Result h;
        std::vector<float> p;
        juce::String error;
        work (sound, h, p, error);
        juce::MessageManager::callAsync ([safe, sound, h, p = std::move (p), error]() mutable
        {
            if (safe != nullptr)
                safe->loaded (sound, h, std::move (p), error);
        });
    });
}

FriendTrackSheet::FriendTrackSheet (RigEngine& e) : engine (e)
{
    build();
    wasChange = true;
    const auto& ft = engine.getFriendTrack();
    if (ft.hasAudio())
    {
        info = ft.getInfo();
        heard.bpm = info.bpm;
        heard.key = info.key;
        heard.minor = info.minor;
        audio = ft.getAudio();
        peaks = friendtrack::peaksOf (*audio, 1400);
        ready = true;
    }
    markSections = false;
    setTempo = std::abs (engine.getTransport().getBpm() - info.bpm) > 0.05;
    update();
}

void FriendTrackSheet::build()
{
    auto& ft = engine.getFriendTrack();
    beforeAudio = ft.getAudio();
    beforeInfo = ft.getInfo();
    beforeBpm = engine.getTransport().getBpm();
    beforeBeatsPerBar = engine.getTransport().getBeatsPerBar();
    beforeMetronome = engine.getTransport().metronome.load();
    beforeTimeline = engine.getTimeline().on.load();
    info.beatsPerBar = beforeBeatsPerBar;

    bpmBox.setJustification (juce::Justification::centred);
    bpmBox.setFont (vintage::lcd (34.0f));
    bpmBox.setInputRestrictions (6, "0123456789.");
    bpmBox.setColour (juce::TextEditor::backgroundColourId, vintage::colours::lcdBack);
    bpmBox.setColour (juce::TextEditor::textColourId, vintage::colours::lcdAmber);
    bpmBox.setColour (juce::TextEditor::outlineColourId, vintage::colours::next);
    bpmBox.setTooltip ("Its tempo: type the BPM your friend told you");
    bpmBox.onReturnKey = [this] { setBpm (bpmBox.getText().getDoubleValue()); };
    bpmBox.onFocusLost = [this] { setBpm (bpmBox.getText().getDoubleValue()); };

    tapButton.setTooltip ("Tap along to the song (3 taps or more)");
    tapButton.onClick = [this] { tap (juce::Time::getMillisecondCounterHiRes() / 1000.0); };
    heardButton.setTooltip ("The tempo Kaleidobox heard in the song's beats");
    heardButton.onClick = [this] { if (heard.bpm > 0) setBpm (heard.bpm); };
    halfButton.setTooltip ("Half the tempo (a 140 that should be 70)");
    halfButton.onClick = [this] { halfTempo(); };
    doubleButton.setTooltip ("Twice the tempo (a 70 that should be 140)");
    doubleButton.onClick = [this] { doubleTempo(); };
    for (int b = 2; b <= 7; ++b)
        beatsBox.addItem (juce::String (b) + "/4", b);
    beatsBox.setTooltip ("Beats in a bar");
    beatsBox.onChange = [this]
    {
        info.beatsPerBar = juce::jlimit (2, 7, beatsBox.getSelectedId());
        tryIt();
        update();
    };

    nudgeLeft.setTooltip ("Bar 1 a little earlier");
    nudgeLeft.onClick = [this] { nudge (-0.01); };
    nudgeRight.setTooltip ("Bar 1 a little later");
    nudgeRight.onClick = [this] { nudge (0.01); };
    beatLeft.setTooltip ("Bar 1 a beat earlier (when the bars start on the wrong beat)");
    beatLeft.onClick = [this] { nudgeBeats (-1); };
    beatRight.setTooltip ("Bar 1 a beat later");
    beatRight.onClick = [this] { nudgeBeats (1); };
    findButton.setTooltip ("Puts bar 1 on the first downbeat Kaleidobox hears");
    findButton.onClick = [this] { findBarOne(); };
    listenButton.setTooltip ("Plays the song from bar 1 with the click, so you can hear the tempo and bar 1 agree");
    listenButton.onClick = [this] { toggleListen(); };

    keyButton.setTooltip ("The song's key, for the Chords (and so the synths' chords fit)");
    keyButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem ("No key", true, info.key < 0, [this] { setKey (-1, false); });
        for (int mode = 0; mode < 2; ++mode)
        {
            juce::PopupMenu sub;
            for (int k = 0; k < 12; ++k)
                sub.addItem (crate::TrackAnalysis::keyName (k, mode == 1), true, info.key == k && info.minor == (mode == 1),
                             [this, k, mode] { setKey (k, mode == 1); });
            menu.addSubMenu (mode == 0 ? "Major" : "Minor", sub);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyButton));
    };
    tempoToggle.onClick = [this] { setTempo = tempoToggle.getToggleState(); };
    keyToggle.setButtonText ("Set the Chords to this key");
    keyToggle.onClick = [this] { setKeyOnChords = keyToggle.getToggleState(); };
    sectionsToggle.onClick = [this] { markSections = sectionsToggle.getToggleState(); };
    cancelButton.onClick = [this] { if (onClose) onClose(); };
    bringButton.onClick = [this]
    {
        const auto said = bringIn();
        if (onDone)
            onDone (said);
    };
    vintage::setLit (bringButton, Lit::play);
    bringButton.setToggleState (true, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &bpmBox, &tapButton, &heardButton, &halfButton, &doubleButton, &beatsBox, &nudgeLeft,
                                                             &nudgeRight, &beatLeft, &beatRight, &findButton, &listenButton, &keyButton, &tempoToggle,
                                                             &keyToggle, &sectionsToggle, &cancelButton, &bringButton })
    {
        addAndMakeVisible (c);
        if (auto* b = dynamic_cast<juce::Button*> (c))
            b->setWantsKeyboardFocus (false);
    }
    for (auto* t : { &tempoToggle, &keyToggle, &sectionsToggle })
        t->setColour (juce::ToggleButton::textColourId, vintage::pal().ink);
    setSize (kWidth, kHeight);
    startTimerHz (15);
    update();
}

FriendTrackSheet::~FriendTrackSheet()
{
    if (! committed)
        restore();
}

void FriendTrackSheet::loaded (crate::SharedAudio a, crate::TrackAnalysis::Result h, std::vector<float> p, const juce::String& error)
{
    if (a == nullptr)
    {
        problem = "Couldn't open " + file.getFileName() + (error.isNotEmpty() ? ": " + error : juce::String (".")) ;
        ready = true;
        update();
        return;
    }
    audio = std::move (a);
    heard = h;
    peaks = std::move (p);
    // A tempo in its name beats a guess (it's what your friend wrote).
    const double named = crate::TrackAnalysis::bpmFromName (file.getFileName());
    info.bpm = named > 0 ? named : heard.bpm > 0 ? heard.bpm : engine.getTransport().getBpm();
    if (named > 0 && heard.bpm > 0 && std::abs (named - heard.bpm) > 0.6)
        info.firstBeat = crate::TrackAnalysis::guessFirstBeat (*audio, RigFriendTrack::kRate, info.bpm, info.beatsPerBar);
    else
        info.firstBeat = heard.firstBeat;
    info.key = heard.key;
    info.minor = heard.minor;
    ready = true;
    tryIt();
    update();
}

//==============================================================================
void FriendTrackSheet::setBpm (double bpm)
{
    if (! (bpm >= 20.0 && bpm <= 400.0))
    {
        update();
        return;
    }
    // (bar 1 stays where it is in the song)
    info.bpm = std::round (bpm * 100.0) / 100.0;
    tryIt();
    update();
}

void FriendTrackSheet::tap (double t)
{
    if (! taps.empty() && t - taps.back() > 2.0)
        taps.clear();
    taps.push_back (t);
    if (const double bpm = crate::TrackAnalysis::tapTempo (taps); bpm > 0)
        setBpm (bpm);
    else
        update();
}

void FriendTrackSheet::setFirstBeat (double seconds)
{
    if (audio == nullptr)
        return;
    info.firstBeat = juce::jlimit (0.0, juce::jmax (0.0, audio->getNumSamples() / RigFriendTrack::kRate - 1.0), seconds);
    tryIt();
    update();
}

void FriendTrackSheet::findBarOne()
{
    if (audio == nullptr)
        return;
    setFirstBeat (crate::TrackAnalysis::guessFirstBeat (*audio, RigFriendTrack::kRate, info.bpm, info.beatsPerBar));
}

void FriendTrackSheet::setKey (int key, bool minor)
{
    info.key = juce::isPositiveAndBelow (key, 12) ? key : -1;
    info.minor = minor;
    update();
}

void FriendTrackSheet::toggleListen()
{
    if (audio == nullptr)
        return;
    auto& transport = engine.getTransport();
    listening = ! listening;
    if (listening)
    {
        tryIt();
        transport.stop();
        transport.setBpm (info.bpm);
        transport.setBeatsPerBar (info.beatsPerBar);
        transport.metronome = true;
        engine.getTimeline().on = false;   // the song's own boxes stay out of it: just the track and the click
        transport.play();
    }
    else
    {
        transport.stop();
        transport.metronome = beforeMetronome;
    }
    update();
}

void FriendTrackSheet::tryIt()
{
    if (audio == nullptr)
        return;
    auto& ft = engine.getFriendTrack();
    if (ft.getAudio() == audio)
        ft.setInfo (info);
    else
        ft.set (audio, info);
    if (listening)
    {
        engine.getTransport().setBpm (info.bpm);
        engine.getTransport().setBeatsPerBar (info.beatsPerBar);
    }
}

void FriendTrackSheet::restore()
{
    auto& transport = engine.getTransport();
    if (listening)
        transport.stop();
    listening = false;
    transport.metronome = beforeMetronome;
    transport.setBpm (beforeBpm);
    transport.setBeatsPerBar (beforeBeatsPerBar);
    engine.getTimeline().on = beforeTimeline;
    auto& ft = engine.getFriendTrack();
    if (ft.getAudio() != beforeAudio || wasChange)
    {
        if (ft.getAudio() == beforeAudio && beforeAudio != nullptr)
            ft.setInfo (beforeInfo);
        else
            ft.set (beforeAudio, beforeInfo);
    }
}

juce::String FriendTrackSheet::bringIn()
{
    if (audio == nullptr)
        return problem.isNotEmpty() ? problem : juce::String ("The track is still being read.");
    auto& transport = engine.getTransport();
    if (listening)
        transport.stop();
    listening = false;
    transport.metronome = beforeMetronome;
    engine.getTimeline().on = beforeTimeline;
    committed = true;
    auto said = engine.bringInFriendTrack (audio, info, setTempo, setKeyOnChords);
    if (! setTempo)
    {
        // (it plays like a tape at the Rig's tempo, locked to the bars)
        transport.setBpm (beforeBpm);
        transport.setBeatsPerBar (beforeBeatsPerBar);
        said << " Kaleidobox stays at " << bpmText (beforeBpm) << " BPM, so it plays "
             << (beforeBpm > info.bpm ? "faster" : "slower") << ", like a tape.";
    }
    auto& tl = engine.getTimeline();
    if (markSections && tl.getMarkers().empty())
    {
        const auto secs = crate::TrackAnalysis::guessSections (*audio, RigFriendTrack::kRate, info.bpm, info.firstBeat, info.beatsPerBar);
        for (const auto& s : secs)
        {
            RigTimeline::Marker m;
            m.bar = s.bar;
            m.name = s.name;
            m.bars = s.bars;
            tl.getMarkers().push_back (m);
        }
        if (! secs.empty())
        {
            tl.changed();
            said << " Marked " << (int) secs.size() << " sections (" << secs.front().name << ", " << secs[secs.size() > 1 ? 1 : 0].name
                 << "...): rename or move them on the Song screen.";
        }
    }
    return said;
}

//==============================================================================
void FriendTrackSheet::timerCallback()
{
    if (listening && ! engine.getTransport().isPlaying())
    {
        listening = false;
        engine.getTransport().metronome = beforeMetronome;
        update();
    }
    if (listening)
        repaint (overviewArea.getUnion (closeUpArea));
}

void FriendTrackSheet::update()
{
    if (! bpmBox.hasKeyboardFocus (true))
        bpmBox.setText (bpmText (info.bpm), juce::dontSendNotification);
    beatsBox.setSelectedId (info.beatsPerBar, juce::dontSendNotification);
    heardButton.setButtonText (heard.bpm > 0 ? "Use " + bpmText (heard.bpm) : juce::String ("Use heard"));
    const bool ok = audio != nullptr;
    for (auto* c : std::initializer_list<juce::Component*> { &bpmBox, &tapButton, &halfButton, &doubleButton, &beatsBox, &nudgeLeft, &nudgeRight,
                                                             &beatLeft, &beatRight, &findButton, &listenButton, &keyButton, &tempoToggle, &keyToggle,
                                                             &sectionsToggle, &bringButton })
        c->setEnabled (ok);
    heardButton.setEnabled (ok && heard.bpm > 0);
    vintage::setLit (listenButton, Lit::play);
    listenButton.setToggleState (listening, juce::dontSendNotification);
    listenButton.setButtonText (listening ? "Stop" : "Listen with click");
    tempoToggle.setButtonText ("Set Kaleidobox to " + bpmText (info.bpm) + " BPM");
    tempoToggle.setToggleState (setTempo, juce::dontSendNotification);
    keyToggle.setToggleState (setKeyOnChords && info.key >= 0, juce::dontSendNotification);
    keyToggle.setEnabled (ok && info.key >= 0);
    const bool hasSections = ! engine.getTimeline().getMarkers().empty();
    sectionsToggle.setButtonText (hasSections ? "Mark its sections (your song has sections: they stay)" : "Mark the song's sections for me (you can move them)");
    sectionsToggle.setToggleState (markSections && ! hasSections, juce::dontSendNotification);
    sectionsToggle.setEnabled (ok && ! hasSections);
    bringButton.setButtonText (wasChange ? "Done" : "Bring it in");
    repaint();
}

juce::Range<double> FriendTrackSheet::closeUpRange() const
{
    const double bar = 60.0 / juce::jmax (1.0, info.bpm) * juce::jmax (1, info.beatsPerBar);
    const double from = juce::jmax (0.0, info.firstBeat - bar);
    return { from, from + bar * 4.0 };
}

double FriendTrackSheet::xToSeconds (juce::Rectangle<int> area, double from, double to, int x) const
{
    return from + (to - from) * juce::jlimit (0.0, 1.0, (x - area.getX()) / (double) juce::jmax (1, area.getWidth()));
}

void FriendTrackSheet::mouseDown (const juce::MouseEvent& e)
{
    dragging = 0;
    if (audio == nullptr)
        return;
    if (overviewArea.contains (e.getPosition()))
        dragging = 1;
    else if (closeUpArea.contains (e.getPosition()))
        dragging = 2;
    mouseDrag (e);
}

void FriendTrackSheet::mouseDrag (const juce::MouseEvent& e)
{
    if (audio == nullptr || dragging == 0)
        return;
    const double length = audio->getNumSamples() / RigFriendTrack::kRate;
    if (dragging == 1)
        setFirstBeat (xToSeconds (overviewArea, 0.0, length, e.x));
    else
    {
        const auto r = closeUpRange();
        setFirstBeat (xToSeconds (closeUpArea, r.getStart(), r.getEnd(), e.x));
    }
}

void FriendTrackSheet::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::engraved (g, "BRING IN A TRACK", titleArea, juce::Justification::centredLeft, 22.0f);
    g.setColour (vintage::pal().ink2);
    g.setFont (vintage::barlow (14.0f));
    juce::String about = file.getFileName().isNotEmpty() ? file.getFileName() : info.name;
    if (audio != nullptr)
    {
        const double secs = audio->getNumSamples() / RigFriendTrack::kRate;
        about << "  " << (int) (secs / 60) << ":" << juce::String ((int) secs % 60).paddedLeft ('0', 2)
              << (audio->getNumChannels() > 1 ? "  stereo" : "  mono");
    }
    g.drawText (about, infoArea, juce::Justification::centredLeft);

    const auto well = [&g] (juce::Rectangle<int> r) { vintage::drawWell (g, r.toFloat(), 8.0f); };
    well (overviewArea);
    well (closeUpArea);
    if (audio == nullptr)
    {
        vintage::lcdText (g, problem.isNotEmpty() ? problem.toUpperCase() : juce::String ("LISTENING TO THE SONG..."),
                          overviewArea.toFloat().reduced (12, 0), 18.0f, juce::Justification::centredLeft);
    }
    else
    {
        const double length = audio->getNumSamples() / RigFriendTrack::kRate;
        const auto inner = overviewArea.toFloat().reduced (6, 8);
        friendtrack::drawPeaks (g, peaks, inner, kTrackColour.withAlpha (0.85f));
        // bar numbers every 8 bars, and bar 1
        const double bar = 60.0 / info.bpm * info.beatsPerBar;
        g.setFont (vintage::cond (12.0f));
        for (int b = 0; info.firstBeat + b * bar < length; b += 8)
        {
            const float x = inner.getX() + (float) ((info.firstBeat + b * bar) / length) * inner.getWidth();
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawVerticalLine ((int) x, inner.getY(), inner.getBottom());
            g.setColour (vintage::colours::lcdAmber);
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x + 2, inner.getY() - 2, 30, 14), juce::Justification::centredLeft);
        }
        const auto line = [&g] (juce::Rectangle<float> r, float x, const juce::String& label)
        {
            g.setColour (vintage::colours::next);
            g.fillRect (x - 1.5f, r.getY() - 4, 3.0f, r.getHeight() + 8);
            if (label.isNotEmpty())
            {
                g.setFont (vintage::cond (12.0f));
                const auto tag = juce::Rectangle<float> (x + 3, r.getBottom() - 16, 90, 16);
                g.fillRoundedRectangle (tag, 3.0f);
                g.setColour (vintage::colours::lcdBack);
                g.drawText (label, tag.reduced (4, 0), juce::Justification::centredLeft);
            }
        };
        line (inner, inner.getX() + (float) (info.firstBeat / length) * inner.getWidth(), "BAR 1 " + juce::String (info.firstBeat, 2) + " S");
        if (listening)
        {
            const double t = info.firstBeat + engine.getTransport().getPositionInBeats() * 60.0 / info.bpm;
            g.setColour (juce::Colours::white);
            g.fillRect (inner.getX() + (float) (t / length) * inner.getWidth(), inner.getY(), 1.5f, inner.getHeight());
        }

        // Close up: the bar before bar 1 and the three after, with the beats.
        const auto r = closeUpRange();
        const auto c = closeUpArea.toFloat().reduced (6, 8);
        const int from = (int) (r.getStart() * RigFriendTrack::kRate), to = juce::jmin (audio->getNumSamples(), (int) (r.getEnd() * RigFriendTrack::kRate));
        if (to > from)
        {
            juce::AudioBuffer<float> part (const_cast<float* const*> (audio->getArrayOfReadPointers()), audio->getNumChannels(), from, to - from);
            const auto close = friendtrack::peaksOf (part, juce::jmax (1, (int) c.getWidth()));
            friendtrack::drawPeaks (g, close, c.withWidth (c.getWidth() * (float) ((to - from) / RigFriendTrack::kRate / r.getLength())),
                                    kTrackColour.withAlpha (0.85f));
        }
        const double beat = 60.0 / info.bpm;
        for (int k = -info.beatsPerBar; k <= info.beatsPerBar * 3; ++k)
        {
            const double t = info.firstBeat + k * beat;
            if (t < r.getStart() || t > r.getEnd())
                continue;
            const float x = c.getX() + (float) ((t - r.getStart()) / r.getLength()) * c.getWidth();
            const bool down = k % info.beatsPerBar == 0;
            g.setColour (juce::Colours::white.withAlpha (down ? 0.45f : 0.18f));
            g.drawVerticalLine ((int) x, c.getY(), c.getBottom());
        }
        line (c, c.getX() + (float) ((info.firstBeat - r.getStart()) / r.getLength()) * c.getWidth(), {});
    }
    vintage::engraved (g, "TEMPO", tempoArea.withHeight (18), juce::Justification::centredLeft, 13.0f);
    vintage::engraved (g, "LINE UP BAR 1", barArea.withHeight (18), juce::Justification::centredLeft, 13.0f);
    vintage::engraved (g, "KEY", keyArea.withHeight (18), juce::Justification::centredLeft, 13.0f);
    {
        auto k = keyArea.withTrimmedTop (22).withHeight (34).toFloat();
        vintage::drawLcd (g, k);
        vintage::lcdText (g, info.key >= 0 ? crate::TrackAnalysis::keyName (info.key, info.minor).toUpperCase() : juce::String ("NO KEY"),
                          k.reduced (10, 0), 22.0f, juce::Justification::centredLeft);
    }
    auto s = statusArea.toFloat();
    vintage::drawLcd (g, s);
    juce::String hint;
    if (audio == nullptr)
        hint = problem.isNotEmpty() ? problem : juce::String ("Reading the song and listening for its tempo...");
    else if (listening)
        hint = "Playing from bar 1 with the click. Drag the orange line or nudge it until the click sits on the beat.";
    else
        hint = "Heard about " + (heard.bpm > 0 ? bpmText (heard.bpm) : juce::String ("?")) + " BPM. Type your friend's number if he gave you one. "
               + "It plays as it is: never stretched or chopped.";
    vintage::lcdText (g, hint.toUpperCase(), s.reduced (10, 0), 15.0f, juce::Justification::centredLeft);
}

void FriendTrackSheet::resized()
{
    auto r = getLocalBounds().reduced (22, 16);
    auto top = r.removeFromTop (32);
    titleArea = top.removeFromLeft (260);
    infoArea = top;
    r.removeFromTop (8);
    overviewArea = r.removeFromTop (110);
    r.removeFromTop (8);
    closeUpArea = r.removeFromTop (84);
    r.removeFromTop (12);

    auto groups = r.removeFromTop (150);
    const int gw = (groups.getWidth() - 24) / 3;
    tempoArea = groups.removeFromLeft (gw);
    groups.removeFromLeft (12);
    barArea = groups.removeFromLeft (gw);
    groups.removeFromLeft (12);
    keyArea = groups;
    {
        auto t = tempoArea.withTrimmedTop (22);
        auto line = t.removeFromTop (50);
        bpmBox.setBounds (line.removeFromLeft (120));
        line.removeFromLeft (8);
        beatsBox.setBounds (line.removeFromLeft (70).withSizeKeepingCentre (70, 30));
        t.removeFromTop (8);
        auto b = t.removeFromTop (32);
        const int w = (b.getWidth() - 18) / 4;
        for (auto* c : { &tapButton, &heardButton, &halfButton, &doubleButton })
        {
            c->setBounds (b.removeFromLeft (w));
            b.removeFromLeft (6);
        }
    }
    {
        auto t = barArea.withTrimmedTop (22);
        auto a = t.removeFromTop (32);
        const int w = (a.getWidth() - 18) / 4;
        for (auto* c : { &nudgeLeft, &nudgeRight, &beatLeft, &beatRight })
        {
            c->setBounds (a.removeFromLeft (w));
            a.removeFromLeft (6);
        }
        t.removeFromTop (8);
        auto b = t.removeFromTop (36);
        findButton.setBounds (b.removeFromLeft (b.getWidth() / 2 - 3));
        b.removeFromLeft (6);
        listenButton.setBounds (b);
    }
    {
        auto t = keyArea.withTrimmedTop (22 + 34 + 8);
        keyButton.setBounds (t.removeFromTop (32).removeFromLeft (130));
        t.removeFromTop (6);
        keyToggle.setBounds (t.removeFromTop (28));
    }
    r.removeFromTop (6);
    tempoToggle.setBounds (r.removeFromTop (26));
    sectionsToggle.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (46);
    bringButton.setBounds (bottom.removeFromRight (180));
    bottom.removeFromRight (10);
    cancelButton.setBounds (bottom.removeFromRight (120));
    bottom.removeFromRight (16);
    statusArea = bottom;
}

//==============================================================================
FriendTrackStrip::FriendTrackStrip (RigEngine& e) : engine (e)
{
    bringButton.sub = "a friend's song, at its tempo";
    bringButton.setTooltip ("Bring in a whole song (a friend's mixdown) with its BPM, play your own parts over it, and send them back. "
                            "Or drop the file here.");
    bringButton.onClick = [this] { if (onBringIn) onBringIn(); };
    putBackButton.setTooltip ("Brings back the track you took out");
    putBackButton.onClick = [this] { putBack(); };
    muteButton.setTooltip ("Mutes his track (it keeps its place)");
    muteButton.onClick = [this] { toggleMute(); };
    practiceButton.setTooltip ("Plays a few bars (a section) over and over, to work out a part");
    practiceButton.onClick = [this] { showPracticeMenu(); };
    changeButton.setTooltip ("Its tempo, where bar 1 is, and its key");
    changeButton.onClick = [this] { if (onChange) onChange(); };
    sendButton.setTooltip ("Your parts as files to send back (without his track), or everything as an MP3 to listen to");
    sendButton.onClick = [this] { if (onSendBack) onSendBack(); };
    removeButton.setTooltip ("Takes his track out of the song (Put it back brings it back)");
    removeButton.onClick = [this] { takeOut(); };
    level.setSliderStyle (juce::Slider::LinearHorizontal);
    level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    level.setRange (0.0, 1.0);
    level.setTooltip ("His track's level");
    level.setValue (engine.getFriendTrack().level.load(), juce::dontSendNotification);
    level.onValueChange = [this] { engine.getFriendTrack().level = (float) level.getValue(); };
    level.setColour (juce::Slider::thumbColourId, kTrackColour);
    for (auto* b : { &bringButton, &putBackButton, &muteButton, &practiceButton, &changeButton, &sendButton, &removeButton })
    {
        b->fontSize = 15.0f;
        addChildComponent (b);
    }
    bringButton.fontSize = 16.0f;
    addChildComponent (level);
    update();
}

int FriendTrackStrip::preferredHeight() const
{
    return engine.getFriendTrack().hasAudio() ? 74 : 40;
}

juce::Rectangle<int> FriendTrackStrip::waveArea() const
{
    auto r = getLocalBounds().reduced (6, 6);
    r.removeFromLeft (230);
    r.removeFromRight (500);
    return r;
}

void FriendTrackStrip::update()
{
    const bool has = engine.getFriendTrack().hasAudio();
    bringButton.setVisible (! has);
    putBackButton.setVisible (! has && removedAudio != nullptr);
    for (juce::Component* c : { (juce::Component*) &muteButton, (juce::Component*) &practiceButton, (juce::Component*) &changeButton,
                                (juce::Component*) &sendButton, (juce::Component*) &removeButton, (juce::Component*) &level })
        c->setVisible (has);
    muteButton.lit = engine.getFriendTrack().mute.load() ? Lit::mute : Lit::none;
    practiceButton.lit = engine.isPracticing() ? Lit::current : Lit::none;
    practiceButton.text = engine.isPracticing() ? "Practice: bars " + juce::String ((int) engine.getTimeline().cycleStart.load() + 1) + "-"
                                                      + juce::String ((int) engine.getTimeline().cycleEnd.load())
                                                : juce::String ("Practice loop");
    level.setValue (engine.getFriendTrack().level.load(), juce::dontSendNotification);
    if (has != shownWithTrack)
    {
        shownWithTrack = has;
        if (onLayout)
            onLayout();
    }
    if (has && peaksId != engine.getFriendTrack().getId())
    {
        peaksId = engine.getFriendTrack().getId();
        peaks = friendtrack::peaksOf (*engine.getFriendTrack().getAudio(), 1200);
    }
    for (auto* b : { &muteButton, &practiceButton })
        b->repaint();
    resized();
    repaint();
}

void FriendTrackStrip::tick()
{
    const bool has = engine.getFriendTrack().hasAudio();
    if (has != shownWithTrack || (has && peaksId != engine.getFriendTrack().getId()))
        update();
    if (! has)
        return;
    const double bar = engine.getTransport().getPositionInBeats() / juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const bool practicing = engine.isPracticing();
    if (std::abs (bar - lastBar) > 0.02 || practicing != (practiceButton.lit == Lit::current))
    {
        lastBar = bar;
        if (practicing != (practiceButton.lit == Lit::current))
            update();
        repaint (waveArea());
    }
}

void FriendTrackStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    vintage::drawWell (g, r, 10.0f);
    if (dropHover)
    {
        g.setColour (kTrackColour.withAlpha (0.25f));
        g.fillRoundedRectangle (r, 10.0f);
    }
    const auto& ft = engine.getFriendTrack();
    if (! ft.hasAudio())
    {
        g.setColour (vintage::pal().wellInk2);
        g.setFont (vintage::barlow (14.0f));
        auto t = getLocalBounds().reduced (12, 0);
        t.removeFromLeft (bringButton.getRight() + 6);
        if (putBackButton.isVisible())
            t.removeFromLeft (putBackButton.getWidth() + 10);
        g.drawText ("Got a song from a friend? Bring it in with its BPM, play your parts over it, then Send it back. Or drop the file here.", t,
                    juce::Justification::centredLeft);
        return;
    }
    const auto info = ft.getInfo();
    auto left = getLocalBounds().reduced (12, 8).removeFromLeft (216);
    g.setColour (kTrackColour);
    g.fillRect (left.removeFromLeft (4));
    left.removeFromLeft (8);
    g.setColour (vintage::pal().wellInk);
    g.setFont (vintage::cond (17.0f));
    g.drawText ("FRIEND'S TRACK", left.removeFromTop (18), juce::Justification::centredLeft);
    g.setFont (vintage::barlow (14.0f));
    g.drawText (info.name, left.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setColour (vintage::colours::lcdAmber);
    g.setFont (vintage::lcd (18.0f));
    g.drawText (infoLine (info, engine.songLengthBarsOfTrack()), left, juce::Justification::centredLeft, true);

    const auto w = waveArea().toFloat();
    g.setColour (vintage::colours::lcdBack);
    g.fillRoundedRectangle (w, 6.0f);
    // The song from its bar 1 (the bit before it doesn't play).
    const double total = ft.getAudio()->getNumSamples() / RigFriendTrack::kRate;
    const double from = total > 0 ? info.firstBeat / total : 0.0;
    friendtrack::drawPeaks (g, peaks, w.reduced (2, 4), (ft.mute.load() ? juce::Colours::grey : kTrackColour).withAlpha (0.8f), from, 1.0);
    const double bars = juce::jmax (1.0, engine.songLengthBarsOfTrack());
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    if (engine.isPracticing())
    {
        const float a = w.getX() + (float) (engine.getTimeline().cycleStart.load() / bars) * w.getWidth();
        const float b = w.getX() + (float) (engine.getTimeline().cycleEnd.load() / bars) * w.getWidth();
        g.setColour (vintage::colours::next.withAlpha (0.22f));
        g.fillRect (juce::Rectangle<float> (a, w.getY(), juce::jmax (2.0f, b - a), w.getHeight()));
    }
    for (const auto& s : rigsong::sections (engine.getTimeline()))
    {
        const float x = w.getX() + (float) (s.start / bars) * w.getWidth();
        if (x > w.getRight())
            break;
        g.setColour (juce::Colours::white.withAlpha (0.3f));
        g.drawVerticalLine ((int) x, w.getY(), w.getBottom());
        g.setFont (vintage::cond (11.0f));
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.drawText (s.name, juce::Rectangle<float> (x + 3, w.getY() + 1, 80, 12), juce::Justification::centredLeft, true);
    }
    const double at = engine.getTransport().getPositionInBeats() / bpb;
    g.setColour (juce::Colours::white);
    g.fillRect (w.getX() + (float) juce::jlimit (0.0, 1.0, at / bars) * w.getWidth() - 1.0f, w.getY(), 2.0f, w.getHeight());
}

void FriendTrackStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 5);
    if (! engine.getFriendTrack().hasAudio())
    {
        bringButton.setBounds (r.removeFromLeft (220));
        r.removeFromLeft (8);
        if (putBackButton.isVisible())
            putBackButton.setBounds (r.removeFromLeft (120));
        return;
    }
    auto right = r.removeFromRight (494);
    auto top = right.removeFromTop (right.getHeight() / 2).reduced (0, 2);
    auto bottom = right.reduced (0, 2);
    removeButton.setBounds (top.removeFromRight (34));
    top.removeFromRight (6);
    sendButton.setBounds (top.removeFromRight (130));
    top.removeFromRight (6);
    changeButton.setBounds (top.removeFromRight (150));
    top.removeFromRight (6);
    muteButton.setBounds (top.removeFromRight (40));
    level.setBounds (top);
    practiceButton.setBounds (bottom.removeFromRight (180));
}

void FriendTrackStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! engine.getFriendTrack().hasAudio() || ! waveArea().contains (e.getPosition()))
        return;
    // Play from there.
    const auto w = waveArea();
    const double bars = juce::jmax (1.0, engine.songLengthBarsOfTrack());
    const double bar = std::floor (juce::jlimit (0.0, 1.0, (e.x - w.getX()) / (double) w.getWidth()) * bars);
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    engine.songStartBar = bar;
    if (engine.getTransport().isPlaying() && engine.getTimeline().on.load())
        engine.getTimeline().jumpTo (bar);
    else
        engine.getTransport().setPosition (bar * bpb);
    lastBar = -1;
    repaint();
    if (onSay)
        onSay ("From bar " + juce::String ((int) bar + 1) + (engine.getTransport().isPlaying() ? juce::String (" (at the next bar).") : juce::String (": press Play.")));
}

void FriendTrackStrip::toggleMute()
{
    auto& ft = engine.getFriendTrack();
    ft.mute = ! ft.mute.load();
    update();
}

void FriendTrackStrip::showPracticeMenu()
{
    juce::PopupMenu menu;
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double bar = std::floor (engine.getTransport().getPositionInBeats() / bpb + 1.0e-6);
    if (engine.isPracticing())
        menu.addItem ("Stop the practice loop (play on)", [this] { engine.practiceOff(); update(); });
    const auto secs = rigsong::sections (engine.getTimeline());
    if (! secs.empty())
    {
        menu.addSectionHeader ("A section");
        for (const auto& s : secs)
            menu.addItem (s.name + " (bars " + juce::String ((int) s.start + 1) + "-" + juce::String ((int) std::round (s.end())) + ")",
                          [this, s]
                          {
                              engine.practiceLoop (s.start, s.end());
                              if (onSay)
                                  onSay ("Practice loop: " + s.name + " over and over. Record over it as you like; Stop the practice loop to play on.");
                              update();
                          });
    }
    menu.addSectionHeader ("From bar " + juce::String ((int) bar + 1));
    for (int n : { 2, 4, 8, 16 })
        menu.addItem (juce::String (n) + " bars", [this, bar, n]
                      {
                          engine.practiceLoop (bar, bar + n);
                          if (onSay)
                              onSay ("Practice loop: bars " + juce::String ((int) bar + 1) + "-" + juce::String ((int) bar + n) + " over and over.");
                          update();
                      });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&practiceButton));
}

void FriendTrackStrip::takeOut()
{
    auto& ft = engine.getFriendTrack();
    if (! ft.hasAudio())
        return;
    removedAudio = ft.getAudio();
    removedInfo = ft.getInfo();
    engine.practiceOff();
    engine.bringInFriendTrack (nullptr, {}, false, false);
    if (onSay)
        onSay (removedInfo.name + " is out of the song. Put it back brings it back.");
    update();
}

void FriendTrackStrip::putBack()
{
    if (removedAudio == nullptr)
        return;
    const auto said = engine.bringInFriendTrack (removedAudio, removedInfo, false, false);
    removedAudio = nullptr;
    if (onSay)
        onSay (said);
    update();
}

void FriendTrackStrip::fileDragEnter (const juce::StringArray&, int, int)
{
    dropHover = true;
    repaint();
}

void FriendTrackStrip::fileDragExit (const juce::StringArray&)
{
    dropHover = false;
    repaint();
}

bool FriendTrackStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg"))
            return true;
    return false;
}

void FriendTrackStrip::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac;m4a;ogg"))
        {
            if (onBringInFile)
                onBringInFile (juce::File (f));
            return;
        }
}
