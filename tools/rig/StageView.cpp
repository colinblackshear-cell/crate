#include "StageView.h"
#include "RigSong.h"
#include "RigColours.h"

using vintage::Lit;
using vintage::Weight;

namespace
{
// The vocoder's parts, as switches on the Stage.
struct VocoderPart { const char* id; const char* name; juce::uint32 colour; };
const VocoderPart vocoderParts[] = { { "voiceOn", "Voice", 0xfff1e7d1 }, { "tuneOn", "Tune", 0xffffb23e }, { "robotOn", "Robot", 0xff5fb3c4 },
                                     { "arpOn", "Arp", 0xffe0816f }, { "vintageOn", "Vintage", 0xffc9a26b }, { "fxOn", "Effects", 0xff9cc27a } };
constexpr int kVocoderParts = (int) std::size (vocoderParts);

float getParam (const juce::AudioProcessorValueTreeState& params, const juce::String& id)
{
    auto* v = params.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

void setParam (juce::AudioProcessorValueTreeState& params, const juce::String& id, float value)
{
    if (auto* p = params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

juce::String letter (int i) { return juce::String::charToString ((juce::juce_wchar) ('A' + i)); }

juce::String partName (const RigScene& s, int i)
{
    return s.name == "Part " + letter (i) ? juce::String() : s.name;
}

// The song's sections (the Song screen's), as parts: which plays now and which comes next.
struct SongNow
{
    std::vector<RigScene> list;
    int current = -1, queued = -1;
    double startBeat = 0;

    const std::vector<RigScene>& getScenes() const { return list; }
    int getCurrent() const { return current; }
    int getQueued() const { return queued; }
    double getCurrentStartBeat() const { return startBeat; }

    explicit SongNow (RigEngine& engine)
    {
        const auto& tl = engine.getTimeline();
        auto& clock = engine.getTransport();
        const int bpb = juce::jmax (1, clock.getBeatsPerBar());
        const auto sections = rigsong::sections (tl);
        for (const auto& sec : sections)
        {
            RigScene s;
            s.name = sec.name;
            s.bars = juce::roundToInt (sec.bars);
            list.push_back (s);
        }
        if (tl.on.load() && clock.isPlaying())
        {
            current = rigsong::sectionAt (tl, clock.getPositionInBeats() / bpb);
            if (current >= 0)
                startBeat = sections[(size_t) current].start * bpb;
            if (const double pending = tl.getPendingJump(); pending >= 0)
                queued = rigsong::sectionAt (tl, pending);
            if (queued == current)
                queued = -1;
        }
    }
};
}

//==============================================================================
int StageView::channelStrip (int c)
{
    const int strips[] = { RigEngine::mpcStrip, RigEngine::drumsStrip, RigEngine::bassStrip, RigEngine::synthStrip, RigEngine::vocoderStrip, RigEngine::loopsStrip };
    return strips[juce::jlimit (0, kChannels - 1, c)];
}

juce::String StageView::channelName (int c)
{
    const char* names[] = { "Drums", "Machine", "Bass", "Synth", "Vocoder", "Loops" };
    return names[juce::jlimit (0, kChannels - 1, c)];
}

juce::String StageView::channelPage (int c)
{
    const char* pages[] = { "MPC", "Drum machine", "Bass", "Synth", "Vocoder", "Looper" };
    return pages[juce::jlimit (0, kChannels - 1, c)];
}

StageView::StageView (RigEngine& e) : engine (e), pad (e.getPerformFx())
{
    setOpaque (true);
    auto& transport = engine.getTransport();

    play.icon = rigpanels::PanelButton::Icon::play;
    stop.icon = rigpanels::PanelButton::Icon::stop;
    rec.icon = rigpanels::PanelButton::Icon::rec;
    for (auto* b : { &play, &stop, &rec })
    {
        b->fontSize = 24.0f;
        b->subSize = 15.0f;
        addAndMakeVisible (b);
    }
    play.onClick = [&transport] { transport.togglePlay(); };
    stop.onClick = [this, &transport]
    {
        transport.stop();
        engine.allNotesOff();
    };
    rec.onClick = [this] { if (onRecord) onRecord(); };
    play.setTooltip ("Starts and stops everything (space bar).");
    stop.setTooltip ("Stops, goes back to the start, and silences any notes still sounding.");
    rec.setTooltip ("Records the set for Logic from the next bar: every track as its own WAV, the mix, and the MIDI.");

    for (auto* b : { &minus, &plus })
    {
        b->fontSize = 30.0f;
        b->setRepeatSpeed (400, 80);
        addAndMakeVisible (b);
    }
    tap.fontSize = 24.0f;
    addAndMakeVisible (tap);
    minus.onClick = [this] { if (onNudgeTempo) onNudgeTempo (-1.0); };
    plus.onClick = [this] { if (onNudgeTempo) onNudgeTempo (1.0); };
    tap.onClick = [this] { if (onTap) onTap(); };

    for (int c = 0; c < kChannels; ++c)
    {
        auto* m = mutes.add (new rigpanels::PanelButton ("Mute"));
        m->fontSize = 22.0f;
        m->inWell = true;
        m->setTooltip ("Mutes " + channelName (c) + ". It keeps going silently, so it comes back in time.");
        m->onClick = [this, c] { pressMute (c); };
        addAndMakeVisible (m);
    }

    for (int i = 0; i < 8; ++i)
    {
        auto* b = parts.add (new rigpanels::PanelButton (letter (i)));
        b->fontSize = 72.0f;
        b->subSize = 17.0f;
        b->onClick = [this, i] { rigpanels::Footer::pressPart (engine, i); };
        addAndMakeVisible (b);
    }

    padToggle.fontSize = 17.0f;
    padToggle.setTooltip ("Swaps the channels for the FX pad: touch it to sweep a filter, repeat a beat, stop the tape, gate or build a riser over the whole mix.");
    padToggle.onClick = [this] { setShowPad (! showPad); };
    addAndMakeVisible (padToggle);
    addChildComponent (pad);

    refresh();
    startTimerHz (30);
}

StageView::~StageView()
{
    stopTimer();
}

void StageView::refresh()
{
    shownSignature = {};
    timerCallback();
    repaint();
}

void StageView::setShowPad (bool on)
{
    showPad = on;
    pad.setVisible (on);
    if (! on)
        pad.release();
    for (auto* m : mutes)
        m->setVisible (! on);
    padToggle.text = on ? "Channels" : "FX pad";
    padToggle.lit = on ? vintage::Lit::current : vintage::Lit::none;
    padToggle.repaint();
    repaint();
}

void StageView::pressMute (int c)
{
    auto& m = engine.getStrip (channelStrip (c)).mute;
    m = ! m.load();
    timerCallback();
}

void StageView::pressLayer (int layer, bool mute)
{
    if (! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return;
    if (mute)
    {
        auto& m = engine.getLooper().getLayer (layer).mute;
        m = ! m.load();
    }
    else
        engine.getLooper().pressRecord (layer);
    repaint (loopsArea);
}

void StageView::pressVocoderPart (int part)
{
    if (! juce::isPositiveAndBelow (part, kVocoderParts))
        return;
    auto& params = engine.getVocoder().params;
    const bool on = getParam (params, vocoderParts[part].id) < 0.5f;
    setParam (params, vocoderParts[part].id, on ? 1.0f : 0.0f);
    if (on)
        engine.vocoderOn = true;
    repaint (loopsArea);
}

//==============================================================================
void StageView::timerCallback()
{
    auto& transport = engine.getTransport();
    const bool playing = transport.isPlaying();
    const auto playLit = playing ? Lit::play : Lit::none;
    if (play.lit != playLit)
    {
        play.lit = playLit;
        play.text = playing ? "Playing" : "Play";
        play.repaint();
    }
    if (recordState)
    {
        const auto [text, lit] = recordState();
        const bool ring = lit == Lit::next;
        const auto shownLit = ring ? Lit::none : lit;
        if (rec.text != text || rec.lit != shownLit || rec.redRing != ring)
        {
            rec.text = text;
            rec.lit = shownLit;
            rec.redRing = ring;
            rec.repaint();
        }
    }

    for (int c = 0; c < kChannels; ++c)
    {
        const bool muted = engine.getStrip (channelStrip (c)).mute.load();
        const auto lit = muted ? Lit::mute : Lit::none;
        if (mutes[c]->lit != lit)
        {
            mutes[c]->lit = lit;
            mutes[c]->repaint();
        }
        const float m = meterOf ? meterOf (channelStrip (c)) : 0.0f;
        const float db = juce::Decibels::gainToDecibels (m, -60.0f);
        levels[(size_t) c] = muted ? 0.0f : juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
    }
    const float master = meterOf ? meterOf (RigEngine::masterStrip) : 0.0f;
    vuLevel = vuLevel * 0.7f + juce::jlimit (0.0f, 1.04f, (juce::Decibels::gainToDecibels (master, -40.0f) + 20.0f) / 23.0f) * 0.3f;
    clipHold = master >= 0.99f ? 1.0f : juce::jmax (0.0f, clipHold - 1.0f / 60.0f);

    // Song parts.
    const SongNow scenes (engine);
    const auto& list = scenes.getScenes();
    const int count = (int) list.size();
    const bool pulse = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;
    for (int i = 0; i < parts.size(); ++i)
    {
        auto* b = parts[i];
        const bool has = i < count;
        juce::String sub;
        Lit lit = Lit::none;
        if (i == scenes.getCurrent())
            sub = juce::String::fromUTF8 ("\xe2\x96\xb6 Now"), lit = Lit::play;
        else if (i == scenes.getQueued())
            sub = "Next", lit = pulse ? Lit::next : Lit::none;
        else if (has)
            sub = partName (list[(size_t) i], i);
        else
            sub = i == count ? "+ Save" : "Empty";
        const bool empty = ! has && i != count;
        if (b->sub != sub || b->lit != lit || b->empty != empty)
        {
            b->sub = sub;
            b->lit = lit;
            b->empty = empty;
            b->repaint();
        }
    }

    // Repaint what's drawn when it changes.
    juce::String sig;
    sig << transport.getBpm() << "/" << (int) std::floor (transport.getPositionInBeats()) << "/" << scenes.getCurrent() << "/"
        << scenes.getQueued() << "/" << (clipHold > 0 ? 1 : 0) << "/" << juce::roundToInt (vuLevel * 200) << "/" << (int) engine.vocoderOn.load();
    for (float l : levels)
        sig << juce::roundToInt (l * 20) << ",";
    for (int c = 0; c < kChannels; ++c)
        sig << (int) engine.getStrip (channelStrip (c)).mute.load();
    for (int i = 0; i < RigLooper::kLayers; ++i)
        sig << (int) engine.getLooper().getState (i) << (int) engine.getLooper().getLayer (i).mute.load();
    for (const auto& p : vocoderParts)
        sig << (getParam (engine.getVocoder().params, p.id) > 0.5f ? 1 : 0);
    if (sig != shownSignature)
    {
        shownSignature = sig;
        repaint();
    }
}

//==============================================================================
juce::Rectangle<int> StageView::layerRect (int layer) const
{
    auto r = loopsArea.reduced (14).withTrimmedTop (40);
    const int size = juce::jmin (48, (r.getWidth() - 30) / 4);
    const int gap = (r.getWidth() - size * 4) / 3;
    return { r.getX() + (layer % 4) * (size + gap), r.getY() + (layer / 4) * (size + 10), size, size };
}

juce::Rectangle<int> StageView::vocoderPartRect (int part) const
{
    auto r = loopsArea.reduced (14);
    const int w = (r.getWidth() - 12) / 3;
    const int top = r.getBottom() - 2 * 32 - 6;
    return { r.getX() + (part % 3) * (w + 6), top + (part / 3) * 38, w, 32 };
}

void StageView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;
    const auto p = e.getPosition();
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (layerRect (i).contains (p))
            return pressLayer (i, e.mods.isPopupMenu() || e.mods.isShiftDown());
    for (int i = 0; i < kVocoderParts; ++i)
        if (vocoderPartRect (i).contains (p))
            return pressVocoderPart (i);
    for (int c = 0; c < kChannels; ++c)
        if (channelAreas[(size_t) c].withHeight (48).contains (p) && onOpen)
            return onOpen (channelPage (c));
}

//==============================================================================
void StageView::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    vintage::drawFace (g, getLocalBounds());
    auto& transport = engine.getTransport();

    // Tempo.
    {
        auto r = tempoArea.toFloat();
        vintage::drawLcd (g, r, 10.0f);
        auto lcd = tempoLcd.toFloat();
        const double bpm = transport.getBpm();
        const auto text = std::abs (bpm - std::round (bpm)) < 0.05 ? juce::String (juce::roundToInt (bpm)) : juce::String (bpm, 1);
        vintage::lcdText (g, text, lcd.removeFromTop (lcd.getHeight() * 0.74f), juce::jmin (150.0f, lcd.getHeight() * 1.6f));
        vintage::lcdText (g, juce::String::fromUTF8 ("BPM \xc2\xb7 ") + juce::String (transport.getBeatsPerBar()) + "/4", lcd, 26.0f);
    }

    // The part playing.
    {
        auto r = partArea.toFloat();
        vintage::drawWell (g, r, 14.0f);
        auto inner = r.reduced (22.0f, 18.0f);
        const SongNow scenes (engine);
        const auto& list = scenes.getScenes();
        const int current = scenes.getCurrent(), queued = scenes.getQueued();
        const bool has = juce::isPositiveAndBelow (current, (int) list.size());

        auto tile = inner.removeFromLeft (juce::jmin (150.0f, inner.getHeight() * 0.75f));
        inner.removeFromLeft (20.0f);
        if (has)
        {
            vintage::glow (g, tile, 12.0f, vintage::colours::play, 16.0f, 0.5f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff86f2a9), 0, tile.getY(), juce::Colour (0xff37c26a), 0, tile.getBottom(), false));
        }
        else
            g.setColour (juce::Colour (0xff2e2925));
        g.fillRoundedRectangle (tile, 12.0f);
        g.setColour (has ? juce::Colour (0xff062611) : p.wellInk2);
        g.setFont (vintage::cond (juce::jmin (140.0f, tile.getHeight() * 0.72f), Weight::extra));
        g.drawText (has ? letter (current) : juce::String ("-"), tile.withTrimmedBottom (tile.getHeight() * 0.2f), juce::Justification::centred);
        g.setFont (vintage::cond (20.0f, Weight::extra, 0.16f));
        g.drawText (has ? juce::String::fromUTF8 ("\xe2\x96\xb6 NOW") : juce::String ("NO PART"), tile.removeFromBottom (tile.getHeight() * 0.24f),
                    juce::Justification::centredTop);

        // Name, bar, progress, next.
        const float rowH = inner.getHeight() / 4.0f;
        auto nameRow = inner.removeFromTop (rowH);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (juce::jmin (54.0f, rowH * 1.05f), Weight::extra, 0.06f));
        const auto name = has ? (partName (list[(size_t) current], current).isNotEmpty() ? partName (list[(size_t) current], current)
                                                                                        : "Part " + letter (current))
                              : juce::String ("Press a part");
        g.drawFittedText (name.toUpperCase(), nameRow.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);

        const int beatsPerBar = juce::jmax (1, transport.getBeatsPerBar());
        const double beats = transport.getPositionInBeats();
        const double since = has ? juce::jmax (0.0, beats - scenes.getCurrentStartBeat()) : beats;
        const int partBars = has ? list[(size_t) current].bars : 0;
        const int barInPart = (int) std::floor (since / beatsPerBar);
        const int beat = (int) std::floor (std::fmod (juce::jmax (0.0, beats), (double) beatsPerBar)) + 1;
        const int blocks = partBars > 0 ? juce::jlimit (1, 16, partBars) : 8;
        const int lit = partBars > 0 ? juce::jmin (barInPart, blocks - 1) : barInPart % 8;

        auto barRow = inner.removeFromTop (rowH);
        const auto barText = "BAR " + juce::String (partBars > 0 ? juce::jmin (barInPart + 1, partBars) : barInPart + 1)
                             + (partBars > 0 ? "/" + juce::String (partBars) : juce::String());
        auto lcd = barRow.removeFromLeft (juce::jmin (barRow.getWidth() * 0.6f, 40.0f + barText.length() * 20.0f)).reduced (0, 4.0f);
        vintage::drawLcd (g, lcd);
        vintage::lcdText (g, barText, lcd, juce::jmin (44.0f, lcd.getHeight() * 1.1f));
        barRow.removeFromLeft (14.0f);
        g.setColour (p.wellInk2);
        g.setFont (vintage::cond (24.0f, Weight::bold));
        g.drawText ("beat " + juce::String (beat), barRow, juce::Justification::centredLeft);

        auto progress = inner.removeFromTop (rowH).withSizeKeepingCentre (inner.getWidth(), juce::jmin (22.0f, rowH * 0.6f));
        const float bw = (progress.getWidth() - 6.0f * (blocks - 1)) / blocks;
        for (int b = 0; b < blocks; ++b)
        {
            auto block = juce::Rectangle<float> (progress.getX() + b * (bw + 6.0f), progress.getY(), bw, progress.getHeight());
            const bool on = transport.isPlaying() && b <= lit;
            g.setColour (on ? vintage::colours::play : juce::Colour (0xff2e2925));
            g.fillRoundedRectangle (block, 4.0f);
            if (on && b == lit)
            {
                g.setColour (juce::Colour (0xfffff3d6));
                g.drawRoundedRectangle (block.expanded (2.0f), 5.0f, 3.0f);
            }
        }

        auto nextRow = inner.reduced (0, 4.0f);
        if (juce::isPositiveAndBelow (queued, (int) list.size()))
        {
            g.setColour (vintage::colours::next);
            g.drawRoundedRectangle (nextRow, 10.0f, 2.0f);
            vintage::drawLamp (g, { nextRow.getX() + 24.0f, nextRow.getCentreY() }, Lit::next, 22.0f);
            g.setColour (p.wellInk);
            g.setFont (vintage::cond (juce::jmin (26.0f, nextRow.getHeight() * 0.8f), Weight::extra, 0.08f));
            const auto nextName = partName (list[(size_t) queued], queued);
            g.drawFittedText ("NEXT: " + letter (queued) + (nextName.isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + nextName.toUpperCase() : juce::String()),
                              nextRow.withTrimmedLeft (48.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        }
        else
        {
            g.setColour (p.wellInk2);
            g.setFont (vintage::cond (18.0f, Weight::bold, 0.1f));
            g.drawText ("TAP A PART BELOW TO START", nextRow, juce::Justification::centredLeft);
        }
    }

    // Channels.
    for (int c = 0; c < (showPad ? 0 : kChannels); ++c)
    {
        auto r = channelAreas[(size_t) c];
        vintage::drawWell (g, r.toFloat(), 14.0f);
        auto inner = r.reduced (12);
        auto title = inner.removeFromTop (30);
        g.setColour (RigColours::forStrip (channelStrip (c)));
        g.fillRoundedRectangle (title.removeFromLeft (18).withSizeKeepingCentre (18, 18).toFloat(), 4.0f);
        title.removeFromLeft (8);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (26.0f, Weight::extra, 0.06f));
        g.drawFittedText (channelName (c).toUpperCase(), title, juce::Justification::centredLeft, 1, 0.7f);

        inner.removeFromTop (8);
        auto status = inner.removeFromTop (24);
        const int strip = channelStrip (c);
        juce::String word = "Ready";
        Lit lamp = Lit::none;
        bool recording = false;
        if (strip == RigEngine::loopsStrip)
            for (int l = 0; l < RigLooper::kLayers; ++l)
            {
                const auto st = engine.getLooper().getState (l);
                recording = recording || st == RigLooper::State::recording || st == RigLooper::State::overdubbing || st == RigLooper::State::armed;
            }
        if (engine.getStrip (strip).mute.load())
            word = "Muted", lamp = Lit::mute;
        else if (strip == RigEngine::vocoderStrip && ! engine.vocoderOn.load())
            word = "Off";
        else if (recording)
            word = "Playing + Rec", lamp = Lit::arm;
        else if (levels[(size_t) c] > 0.05f)
            word = "Playing", lamp = Lit::play;
        else if (transport.isPlaying())
            word = "Quiet";
        vintage::drawLamp (g, { (float) status.getX() + 11.0f, (float) status.getCentreY() }, lamp, 22.0f);
        vintage::engraved (g, word, status.withTrimmedLeft (30), juce::Justification::centredLeft, 15.0f, true, p.wellInk);

        // The LED meter: 20 segments, green, then amber, then red.
        auto meter = inner.withTrimmedTop (10).withTrimmedBottom (mutes[c]->getHeight() + 12);
        meter = meter.withSizeKeepingCentre (juce::jmin (56, meter.getWidth()), meter.getHeight());
        const float segH = (meter.getHeight() - 3.0f * 19) / 20.0f;
        const int litSegs = juce::roundToInt (levels[(size_t) c] * 20.0f);
        for (int k = 0; k < 20; ++k)
        {
            auto seg = juce::Rectangle<float> ((float) meter.getX(), meter.getBottom() - (k + 1) * (segH + 3.0f) + 3.0f, (float) meter.getWidth(), segH);
            const auto colour = k < 13 ? vintage::colours::play : k < 17 ? juce::Colour (0xffffc94a) : juce::Colour (0xfff0503a);
            if (k < litSegs)
            {
                g.setColour (colour.withAlpha (0.25f));
                g.fillRoundedRectangle (seg.expanded (2.0f), 4.0f);
                g.setColour (colour);
            }
            else
                g.setColour (juce::Colour (0xff2a2521));
            g.fillRoundedRectangle (seg, 3.0f);
        }
    }

    // Loops and the vocoder.
    {
        auto r = loopsArea;
        vintage::drawWell (g, r.toFloat(), 14.0f);
        auto inner = r.reduced (14);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (26.0f, Weight::extra, 0.08f));
        g.drawText ("LOOPS", inner.removeFromTop (30), juce::Justification::centredLeft);
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            const auto lr = layerRect (i).toFloat();
            const auto st = engine.getLooper().getState (i);
            Lit lit = Lit::none;
            if (st == RigLooper::State::recording || st == RigLooper::State::overdubbing || st == RigLooper::State::armed)
                lit = Lit::arm;
            else if (st == RigLooper::State::playing)
                lit = engine.getLooper().getLayer (i).mute.load() ? Lit::mute : Lit::play;
            vintage::drawLamp (g, lr.getCentre(), lit, lr.getWidth());
            g.setColour (lit == Lit::none ? p.wellInk2 : juce::Colour (0xff1e1a16));
            g.setFont (vintage::cond (24.0f, Weight::extra));
            g.drawText (juce::String (i + 1), lr, juce::Justification::centred);
        }
        auto legend = layerRect (4).withY (layerRect (4).getBottom() + 8).withHeight (16).withWidth (inner.getWidth());
        vintage::engraved (g, juce::String::fromUTF8 ("Green play \xc2\xb7 blue mute \xc2\xb7 red rec"), legend,
                           juce::Justification::centredLeft, 10.5f, true);

        const auto firstPart = vocoderPartRect (0);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (22.0f, Weight::extra, 0.08f));
        g.drawText ("VOCODER", juce::Rectangle<int> (inner.getX(), firstPart.getY() - 32, inner.getWidth(), 28), juce::Justification::centredLeft);
        for (int i = 0; i < kVocoderParts; ++i)
        {
            const auto pr = vocoderPartRect (i).toFloat();
            const bool on = engine.vocoderOn.load() && getParam (engine.getVocoder().params, vocoderParts[i].id) > 0.5f;
            const juce::Colour colour (vocoderParts[i].colour);
            g.setColour (on ? colour : juce::Colour (0xff2a2521));
            g.fillRoundedRectangle (pr, 6.0f);
            g.setColour (on ? juce::Colour (0xff1e1a16) : p.wellInk2);
            g.setFont (vintage::cond (17.0f, Weight::extra, 0.06f));
            g.drawFittedText (juce::String (vocoderParts[i].name).toUpperCase(), pr.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }

    // Master.
    {
        auto r = masterArea;
        vintage::drawWell (g, r.toFloat(), 14.0f);
        auto inner = r.reduced (14);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (26.0f, Weight::extra, 0.08f));
        g.drawText ("MASTER", inner.removeFromTop (30), juce::Justification::centredLeft);
        auto lampRow = inner.removeFromBottom (24);
        inner.removeFromBottom (8);
        const float vuH = juce::jmin ((inner.getHeight() - 10.0f) / 2.0f, inner.getWidth() * 78.0f / 132.0f);
        const float vuW = vuH * 132.0f / 78.0f;
        auto vu = inner.toFloat().withSizeKeepingCentre (vuW, vuH * 2.0f + 10.0f);
        vintage::drawVu (g, vu.removeFromTop (vuH), vuLevel, "L");
        vu.removeFromTop (10.0f);
        vintage::drawVu (g, vu, vuLevel, "R");
        vintage::drawLamp (g, { (float) lampRow.getX() + 9.0f, (float) lampRow.getCentreY() }, clipHold > 0 ? Lit::arm : Lit::none);
        vintage::engraved (g, clipHold > 0 ? "Clipping: turn down" : "No clipping", lampRow.withTrimmedLeft (26), juce::Justification::centredLeft,
                           13.0f, true);
    }

    vintage::engraved (g, juce::String::fromUTF8 ("Song parts \xc2\xb7 tap to queue \xc2\xb7 plays on the next bar"), partsLabel,
                       juce::Justification::centredLeft, 14.0f);
}

void StageView::resized()
{
    auto area = getLocalBounds().withTrimmedTop (10).withTrimmedBottom (14).reduced (22, 0);
    const float scale = juce::jlimit (0.6f, 1.0f, area.getHeight() / 690.0f);

    // Song parts along the bottom.
    auto partsRow = area.removeFromBottom (juce::roundToInt (132 * scale));
    partsLabel = area.removeFromBottom (34);
    padToggle.setBounds (partsLabel.removeFromRight (juce::roundToInt (180 * scale)).reduced (0, 1));
    area.removeFromBottom (4);
    {
        const int gap = 12;
        const int w = (partsRow.getWidth() - gap * 7) / 8;
        for (auto* b : parts)
        {
            b->fontSize = 72.0f * scale;
            b->setBounds (partsRow.removeFromLeft (w));
            partsRow.removeFromLeft (gap);
        }
    }
    area.removeFromBottom (18);

    // The top: transport, tempo, the part.
    auto top = area.removeFromTop (juce::roundToInt (240 * scale));
    area.removeFromTop (18);
    {
        auto t = top.withSizeKeepingCentre (top.getWidth(), juce::roundToInt (200 * scale));
        transportArea = t.removeFromLeft (juce::roundToInt ((150 + 130 + 130 + 28) * scale));
        auto tr = transportArea;
        play.setBounds (tr.removeFromLeft (juce::roundToInt (150 * scale)));
        tr.removeFromLeft (juce::roundToInt (14 * scale));
        stop.setBounds (tr.removeFromLeft (juce::roundToInt (130 * scale)));
        tr.removeFromLeft (juce::roundToInt (14 * scale));
        rec.setBounds (tr);
        for (auto* b : { &play, &stop, &rec })
            b->fontSize = (b == &rec ? 22.0f : 24.0f) * scale;
    }
    top.removeFromLeft (juce::roundToInt (20 * scale) + transportArea.getWidth());
    tempoArea = top.removeFromLeft (juce::roundToInt (330 * scale));
    top.removeFromLeft (20);
    partArea = top;
    {
        auto t = tempoArea.reduced (14);
        auto buttons = t.removeFromBottom (juce::roundToInt (54 * scale));
        t.removeFromBottom (8);
        tempoLcd = t;
        const int bw = juce::roundToInt (70 * scale), tw = juce::roundToInt (90 * scale);
        auto row = buttons.withSizeKeepingCentre (bw * 2 + tw + 20, buttons.getHeight());
        minus.setBounds (row.removeFromLeft (bw));
        row.removeFromLeft (10);
        tap.setBounds (row.removeFromLeft (tw));
        row.removeFromLeft (10);
        plus.setBounds (row.removeFromLeft (bw));
    }

    // The middle: channels, loops + vocoder, master.
    const int sideW = juce::jmin (250, area.getWidth() / 6);
    masterArea = area.removeFromRight (sideW);
    area.removeFromRight (20);
    loopsArea = area.removeFromRight (sideW);
    area.removeFromRight (20);
    channelsArea = area;
    pad.setBounds (channelsArea);
    const int gap = 12;
    const int cw = (area.getWidth() - gap * (kChannels - 1)) / kChannels;
    for (int c = 0; c < kChannels; ++c)
    {
        channelAreas[(size_t) c] = area.removeFromLeft (cw);
        area.removeFromLeft (gap);
        auto inner = channelAreas[(size_t) c].reduced (12);
        mutes[c]->setBounds (inner.removeFromBottom (48));
    }
}
