#include "MixerView.h"
#include "RigColours.h"

using vintage::Lit;
using vintage::Weight;

namespace
{
// The fader: a slot with a scale and a cream cap with the strip's colour across it.
class ConsoleLook : public vintage::LookAndFeel
{
public:
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle style,
                           juce::Slider& s) override
    {
        if (style != juce::Slider::LinearVertical)
            return vintage::LookAndFeel::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
        const auto& p = vintage::pal();
        const auto area = juce::Rectangle<int> (x, y, w, h).toFloat();
        const float capH = juce::jlimit (16.0f, 30.0f, area.getHeight() * 0.11f);
        const float top = area.getY() + capH * 0.5f, bottom = area.getBottom() - capH * 0.5f;

        // The scale: 0 dB (where it starts), and every 6 dB down.
        g.setFont (vintage::cond (10.0f, Weight::bold));
        for (float level : { 1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f })
        {
            const float ty = bottom - (bottom - top) * level;
            g.setColour (p.wellInk2.withAlpha (level == 0.8f ? 0.9f : 0.45f));
            g.fillRect (area.getX(), ty - 0.5f, area.getWidth(), level == 0.8f ? 1.5f : 1.0f);
        }

        // The slot.
        const auto slot = juce::Rectangle<float> (area.getCentreX() - 3.0f, top, 6.0f, bottom - top);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (slot, 3.0f);
        const auto ring = s.findColour (juce::Slider::trackColourId);
        g.setColour (ring.withAlpha (0.55f));
        g.fillRoundedRectangle (slot.withTop (pos), 3.0f);

        // The cap.
        const float capW = juce::jmin (area.getWidth() - 2.0f, 44.0f);
        auto cap = juce::Rectangle<float> (capW, capH).withCentre ({ area.getCentreX(), pos });
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRoundedRectangle (cap.translated (0, 2.0f), 4.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4ead6), 0, cap.getY(), juce::Colour (0xffc9bda6), 0, cap.getBottom(), false));
        g.fillRoundedRectangle (cap, 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        for (float dy : { -0.3f, 0.3f })
            g.fillRect (cap.getX() + 4.0f, cap.getCentreY() + cap.getHeight() * dy, cap.getWidth() - 8.0f, 1.0f);
        g.setColour (ring);
        g.fillRect (cap.getX() + 3.0f, cap.getCentreY() - 1.5f, cap.getWidth() - 6.0f, 3.0f);
    }
};

juce::String dbText (float gain)
{
    if (gain <= 0.0001f)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e");
    const float db = juce::Decibels::gainToDecibels (gain);
    return (db > 0.05f ? "+" : "") + (std::abs (db) < 9.95f ? juce::String (db, 1) : juce::String (juce::roundToInt (db)));
}

juce::String panText (float pan)
{
    const int amount = juce::roundToInt (std::abs (pan) * 100.0f);
    return amount == 0 ? juce::String ("C") : (pan < 0 ? "L" : "R") + juce::String (amount);
}

void setUpKnob (juce::Slider& s, juce::Colour ring, double low, double high, double reset, const juce::String& tip)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setRange (low, high);
    s.setDoubleClickReturnValue (true, reset);
    s.setColour (juce::Slider::rotarySliderFillColourId, ring);
    s.setTooltip (tip);
}

void setUpButton (juce::TextButton& b, Lit lit, const juce::String& tip)
{
    b.setClickingTogglesState (false);
    vintage::setLit (b, lit);
    vintage::setInWell (b, true);
    b.getProperties().set ("fontSize", 14.0f);
    b.setTooltip (tip);
}

float meterFill (float peak)
{
    const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
    return juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f);
}
}

//==============================================================================
const std::vector<int>& MixerView::toolStrips()
{
    static const std::vector<int> strips { RigEngine::mpcStrip, RigEngine::drumsStrip, RigEngine::bassStrip, RigEngine::synthStrip,
                                           RigEngine::vocoderStrip, RigEngine::samplerStrip, RigEngine::inputStrip, RigEngine::loopsStrip };
    return strips;
}

int MixerView::faderControl (int strip)
{
    switch (strip)
    {
        case RigEngine::mpcStrip:     return RigRouting::mpcLevel;
        case RigEngine::drumsStrip:   return RigRouting::drumsLevel;
        case RigEngine::bassStrip:    return RigRouting::bassLevel;
        case RigEngine::synthStrip:   return RigRouting::synthLevel;
        case RigEngine::vocoderStrip: return RigRouting::vocoderLevel;
        case RigEngine::samplerStrip: return RigRouting::samplerLevel;
        case RigEngine::inputStrip:   return RigRouting::inputLevel;
        case RigEngine::loopsStrip:   return RigRouting::loopsLevel;
        case RigEngine::masterStrip:  return RigRouting::masterLevel;
        default:                      return -1;
    }
}

juce::String MixerView::pageFor (int strip)
{
    switch (strip)
    {
        case RigEngine::mpcStrip:     return "MPC";
        case RigEngine::drumsStrip:   return "Drum machine";
        case RigEngine::bassStrip:    return "Bass";
        case RigEngine::synthStrip:   return "Synth";
        case RigEngine::vocoderStrip: return "Vocoder";
        case RigEngine::samplerStrip: return "Sampler";
        case RigEngine::loopsStrip:   return "Looper";
        case RigEngine::inputStrip:   return "Setup";
        default:                      return {};
    }
}

const juce::StringArray& MixerView::hubPages()
{
    static const juce::StringArray pages { "Groove", "MPC", "Drum machine", "Bass", "Synth", "Vocoder", "Sampler", "Looper",
                                           "Song", "Sounds", "Setup" };
    return pages;
}

//==============================================================================
MixerView::Channel::Channel (MixerView& d, Kind k, int i) : kind (k), index (i), desk (d)
{
    const auto c = colour();
    fader.setRange (0.0, 1.0);
    fader.setDoubleClickReturnValue (true, 0.8);
    fader.setColour (juce::Slider::trackColourId, c);
    fader.setTooltip ("The level. Double-click puts it back to 0 dB.");
    addAndMakeVisible (fader);

    setUpButton (mute, Lit::mute, "Mute: silent, but it keeps playing in time, so it comes back on the beat.");
    addAndMakeVisible (mute);

    auto& e = engine();
    if (kind == Kind::tool)
    {
        auto& strip = e.getStrip (index);
        fader.onValueChange = [this, &strip] { strip.level = (float) fader.getValue(); };
        mute.onClick = [this, &strip] { strip.mute = ! strip.mute.load(); refresh(); };
        setUpButton (solo, Lit::next, "Solo: hear only this (and anything else soloed). The sends still reach the Reverb and Echo.");
        solo.onClick = [this, &strip] { strip.solo = ! strip.solo.load(); desk.refresh(); };
        addAndMakeVisible (solo);
        setUpKnob (sendA, juce::Colour (0xff8fb8ff), 0.0, 1.0, 0.0, "Send A: how much of this goes to the shared Reverb (after its fader).");
        setUpKnob (sendB, RigColours::song, 0.0, 1.0, 0.0, "Send B: how much of this goes to the shared Echo, in time with the tempo (after its fader).");
        setUpKnob (pan, c, -1.0, 1.0, 0.0, "Pan: left or right. Double-click for the middle.");
        sendA.onValueChange = [this, &strip] { strip.sendA = (float) sendA.getValue(); };
        sendB.onValueChange = [this, &strip] { strip.sendB = (float) sendB.getValue(); };
        pan.onValueChange = [this, &strip] { strip.pan = (float) pan.getValue(); repaint(); };
        for (auto* s : { &sendA, &sendB, &pan })
            addAndMakeVisible (s);
    }
    else if (kind == Kind::master)
    {
        auto& strip = e.getStrip (RigEngine::masterStrip);
        fader.onValueChange = [this, &strip] { strip.level = (float) fader.getValue(); };
        mute.onClick = [this, &strip] { strip.mute = ! strip.mute.load(); refresh(); };
    }
    else if (kind == Kind::layer)
    {
        auto& layer = e.getLooper().getLayer (index);
        fader.onValueChange = [this, &layer] { layer.level = (float) fader.getValue(); };
        mute.onClick = [this, &layer] { layer.mute = ! layer.mute.load(); refresh(); };
        setUpButton (rec, Lit::arm, "Records this layer from the next bar; press again to stop and loop it, again to overdub.");
        rec.onClick = [this] { engine().getLooper().pressRecord (index); };
        addAndMakeVisible (rec);
    }
    else
    {
        auto& ret = e.getReturn (kind == Kind::reverb ? RigEngine::reverbReturn : RigEngine::echoReturn);
        fader.onValueChange = [this, &ret] { ret.level = (float) fader.getValue(); };
        mute.onClick = [this, &ret] { ret.mute = ! ret.mute.load(); refresh(); };
        if (kind == Kind::reverb)
        {
            setUpKnob (amount, c, 0.0, 1.0, 0.5, "The room: small and tight, to a big hall.");
            setUpKnob (tone, c, 0.0, 1.0, 0.5, "Brightness: dark and soft, to bright.");
            tone.onValueChange = [this, &ret] { ret.tone = (float) tone.getValue(); };
            addAndMakeVisible (tone);
        }
        else
        {
            setUpKnob (amount, c, 0.0, 0.9, 0.5, "Feedback: how many times it repeats.");
            const auto& times = RigEngine::echoTimes();
            for (int t = 0; t < (int) times.size(); ++t)
                time.addItem (times[(size_t) t].first, t + 1);
            time.onChange = [this, &ret] { ret.time = time.getSelectedId() - 1; };
            time.setTooltip ("How far apart the repeats are, in time with the tempo.");
            addAndMakeVisible (time);
        }
        amount.onValueChange = [this, &ret] { ret.amount = (float) amount.getValue(); };
        addAndMakeVisible (amount);
    }

    if (kind == Kind::tool || kind == Kind::master || kind == Kind::layer)
    {
        setUpButton (fx, Lit::current, kind == Kind::layer ? "This loop's own effects: filter, drive, crush, delay, reverb and beat repeat."
                                                           : "This strip's effects: filter sweep, drive, crush, delay, reverb and beat repeat. Lit when any are on.");
        fx.onClick = [this]
        {
            if (kind == Kind::layer)
            {
                if (desk.onLayerFx)
                    desk.onLayerFx (index);
            }
            else if (desk.onFx)
                desk.onFx (index);
        };
        addAndMakeVisible (fx);
    }

    if (kind == Kind::tool || kind == Kind::master)
    {
        setUpButton (learn, Lit::none, "Press, then move a fader or knob on your keyboard to control this level with it.");
        learn.getProperties().set ("fontSize", 12.0f);
        learn.onClick = [this] { if (desk.onLearn) desk.onLearn (faderControl (index), learn); };
        addAndMakeVisible (learn);
    }

    std::atomic<bool>* onFlag = kind != Kind::tool ? nullptr
                                : index == RigEngine::vocoderStrip ? &e.vocoderOn
                                : index == RigEngine::inputStrip   ? &e.inputOn
                                                                   : nullptr;
    if (onFlag != nullptr)
    {
        setUpButton (on, Lit::play, index == RigEngine::inputStrip
                                        ? "Plays your audio input (a guitar, a bass, another mic) through this strip and its effects. "
                                          "Off until you turn it on, so a mic doesn't feed back."
                                        : "Turns the vocoder on or off. Off, it's silent and uses no processor time.");
        on.onClick = [this, onFlag] { *onFlag = ! onFlag->load(); refresh(); };
        addAndMakeVisible (on);
    }
    if (kind == Kind::tool && index == RigEngine::inputStrip)
    {
        for (int i = 0; i < 3; ++i)
            jack.addItem (RigEngine::inputJackName (i), i + 1);
        jack.onChange = [this] { engine().inputJack = jack.getSelectedId() - 1; };
        jack.setTooltip ("Which input of your audio interface this strip plays: both, or just input 1 or 2 "
                         "(e.g. the mic on 1 for the vocoder and a guitar on 2).");
        addAndMakeVisible (jack);
    }

    if (pageFor (index).isNotEmpty() && kind == Kind::tool)
        setMouseCursor (juce::MouseCursor::NormalCursor);
    refresh();
}

RigEngine& MixerView::Channel::engine() const { return desk.engine; }

juce::String MixerView::Channel::title() const
{
    switch (kind)
    {
        case Kind::tool:   return index == RigEngine::drumsStrip ? juce::String ("Machine") : RigEngine::stripName (index);
        case Kind::layer:  return "Loop " + juce::String (index + 1);
        case Kind::reverb: return "A " + RigEngine::returnName (RigEngine::reverbReturn);
        case Kind::echo:   return "B " + RigEngine::returnName (RigEngine::echoReturn);
        case Kind::master: return "Master";
    }
    return {};
}

juce::Colour MixerView::Channel::colour() const
{
    switch (kind)
    {
        case Kind::tool:   return RigColours::forStrip (index);
        case Kind::layer:  return RigColours::loops;
        case Kind::reverb: return juce::Colour (0xff8fb8ff);
        case Kind::echo:   return RigColours::song;
        case Kind::master: return RigColours::master;
    }
    return RigColours::master;
}

std::pair<juce::String, Lit> MixerView::Channel::status() const
{
    auto& e = engine();
    const bool playing = e.getTransport().isPlaying();
    const bool sounding = meter > 0.05f;
    if (kind == Kind::layer)
    {
        const auto& layer = e.getLooper().getLayer (index);
        switch (e.getLooper().getState (index))
        {
            case RigLooper::State::armed:       return { "Next bar", Lit::arm };
            case RigLooper::State::recording:   return { "Recording", Lit::arm };
            case RigLooper::State::overdubbing: return { "Overdub", Lit::arm };
            case RigLooper::State::playing:     return layer.mute.load() ? std::pair<juce::String, Lit> { "Muted", Lit::mute }
                                                                         : std::pair<juce::String, Lit> { "Playing", Lit::play };
            case RigLooper::State::empty:       break;
        }
        return { "Empty", Lit::none };
    }
    if (kind == Kind::reverb || kind == Kind::echo)
    {
        const auto& ret = e.getReturn (kind == Kind::reverb ? RigEngine::reverbReturn : RigEngine::echoReturn);
        if (ret.mute.load())
            return { "Muted", Lit::mute };
        return sounding ? std::pair<juce::String, Lit> { "Playing", Lit::play } : std::pair<juce::String, Lit> { "Send to it", Lit::none };
    }
    if (kind == Kind::master)
    {
        if (e.getStrip (RigEngine::masterStrip).mute.load())
            return { "Muted", Lit::mute };
        if (meter >= 0.99f)
            return { "Too loud", Lit::arm };
        return sounding ? std::pair<juce::String, Lit> { "Playing", Lit::play } : std::pair<juce::String, Lit> { playing ? "Quiet" : "Ready", Lit::none };
    }
    const auto& strip = e.getStrip (index);
    if (strip.mute.load())
        return { "Muted", Lit::mute };
    if (index == RigEngine::vocoderStrip && ! e.vocoderOn.load())
        return { "Off", Lit::none };
    if (index == RigEngine::inputStrip && ! e.inputOn.load())
        return { "Off", Lit::none };
    if (e.isSilenced (index))
        return { "Silenced", Lit::none };
    if (strip.solo.load())
        return { "Solo", Lit::next };
    if (sounding)
        return { "Playing", Lit::play };
    return { playing ? "Quiet" : "Ready", Lit::none };
}

void MixerView::Channel::refresh()
{
    auto& e = engine();
    auto set = [] (juce::Slider& s, float v)
    {
        if (! s.isMouseButtonDown() && std::abs (s.getValue() - v) > 1.0e-5)
            s.setValue (v, juce::dontSendNotification);
    };
    auto light = [] (juce::Button& b, bool lit)
    {
        if (b.getToggleState() != lit)
            b.setToggleState (lit, juce::dontSendNotification);
    };
    if (kind == Kind::tool || kind == Kind::master)
    {
        const auto& strip = e.getStrip (index);
        set (fader, strip.level.load());
        light (mute, strip.mute.load());
        light (fx, e.getFx (index).isActive());
        if (kind == Kind::tool)
        {
            set (sendA, strip.sendA.load());
            set (sendB, strip.sendB.load());
            set (pan, strip.pan.load());
            light (solo, strip.solo.load());
        }
        if (on.isVisible())
        {
            const bool isOn = index == RigEngine::vocoderStrip ? e.vocoderOn.load() : e.inputOn.load();
            light (on, isOn);
            on.setButtonText (isOn ? "On" : "Off");
        }
        if (jack.isVisible())
            jack.setSelectedId (e.inputJack.load() + 1, juce::dontSendNotification);
    }
    else if (kind == Kind::layer)
    {
        const auto& layer = e.getLooper().getLayer (index);
        set (fader, layer.level.load());
        light (mute, layer.mute.load());
        light (fx, e.getLoopFx (index).isActive());
        const auto st = e.getLooper().getState (index);
        light (rec, st == RigLooper::State::armed || st == RigLooper::State::recording || st == RigLooper::State::overdubbing);
        rec.setButtonText (st == RigLooper::State::empty ? "Rec" : st == RigLooper::State::playing ? "Dub" : "Stop");
    }
    else
    {
        const auto& ret = e.getReturn (kind == Kind::reverb ? RigEngine::reverbReturn : RigEngine::echoReturn);
        set (fader, ret.level.load());
        light (mute, ret.mute.load());
        set (amount, ret.amount.load());
        if (kind == Kind::reverb)
            set (tone, ret.tone.load());
        else
            time.setSelectedId (ret.time.load() + 1, juce::dontSendNotification);
    }
}

void MixerView::Channel::tick()
{
    auto& e = engine();
    float peak = 0;
    if (kind == Kind::layer)
        peak = e.getLooper().getLayer (index).peak.exchange (0.0f);
    else if (kind == Kind::reverb || kind == Kind::echo)
        peak = e.getReturn (kind == Kind::reverb ? RigEngine::reverbReturn : RigEngine::echoReturn).peak.exchange (0.0f);
    else if (desk.meterOf)
        peak = desk.meterOf (index);
    meter = kind == Kind::layer || kind == Kind::reverb || kind == Kind::echo ? juce::jmax (peak, meter * 0.85f) : peak;
    refresh();

    const auto [word, lit] = status();
    juce::String sig;
    sig << word << (int) lit << juce::roundToInt (meterFill (meter) * 24.0f) << juce::roundToInt (fader.getValue() * 200.0);
    if (sig != shown)
    {
        shown = sig;
        repaint();
    }
}

void MixerView::Channel::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked() || ! nameArea.expanded (0, 4).contains (e.getPosition()))
        return;
    const auto page = kind == Kind::layer ? juce::String ("Looper") : kind == Kind::tool ? pageFor (index) : juce::String();
    if (page.isNotEmpty() && desk.onOpen)
        desk.onOpen (page);
}

void MixerView::Channel::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    const auto c = colour();
    auto r = getLocalBounds().toFloat();
    vintage::drawWell (g, r, 10.0f);
    g.setColour (c);
    g.fillRoundedRectangle (r.reduced (8.0f, 0).removeFromTop (5.0f).translated (0, 5.0f), 2.0f);

    // The name (a click goes into the tool) and what it's doing.
    g.setColour (p.wellInk);
    g.setFont (vintage::cond (juce::jmin (18.0f, nameArea.getHeight() * 0.95f), Weight::extra, 0.05f));
    const bool opens = kind == Kind::layer || (kind == Kind::tool && pageFor (index).isNotEmpty());
    g.drawFittedText (title().toUpperCase() + (opens ? juce::String::fromUTF8 (" \xe2\x96\xb8") : juce::String()), nameArea,
                      juce::Justification::centred, 1, 0.6f);
    const auto [word, lit] = status();
    auto st = statusArea;
    const auto lampAt = juce::Point<float> ((float) st.getX() + 7.0f, (float) st.getCentreY());
    vintage::drawLamp (g, lampAt, lit, 11.0f);
    vintage::engraved (g, word, st.withTrimmedLeft (16), juce::Justification::centredLeft, 10.5f, true, p.wellInk2);

    // The knobs' names and values.
    auto label = [&] (const juce::Slider& s, const juce::String& name, const juce::String& value)
    {
        if (! s.isVisible())
            return;
        auto under = juce::Rectangle<int> (s.getX() - 6, s.getBottom(), s.getWidth() + 12, 13);
        vintage::engraved (g, value.isNotEmpty() ? name + " " + value : name, under, juce::Justification::centred, 9.5f, true, p.wellInk2);
    };
    label (sendA, "Rev", {});
    label (sendB, "Echo", {});
    label (pan, "Pan", panText ((float) pan.getValue()));
    label (amount, kind == Kind::reverb ? "Size" : "Repeats", {});
    label (tone, "Bright", {});

    // The meter: green, amber, red.
    if (! meterArea.isEmpty())
    {
        const int segments = 24;
        const float gap = 2.0f;
        const auto m = meterArea.toFloat();
        const float segH = (m.getHeight() - gap * (segments - 1)) / segments;
        const int litSegs = juce::roundToInt (meterFill (meter) * segments);
        for (int k = 0; k < segments; ++k)
        {
            auto seg = juce::Rectangle<float> (m.getX(), m.getBottom() - (k + 1) * (segH + gap) + gap, m.getWidth(), segH);
            const auto col = k < segments * 0.65f ? vintage::colours::play : k < segments * 0.87f ? juce::Colour (0xffffc94a) : juce::Colour (0xfff0503a);
            g.setColour (k < litSegs ? col : juce::Colour (0xff2a2521));
            g.fillRoundedRectangle (seg, 1.5f);
        }
    }

    // The level, in dB.
    if (! dbArea.isEmpty())
    {
        const float level = (float) fader.getValue();
        const float gain = kind == Kind::layer ? level : RigEngine::faderGain (level);
        vintage::drawLcd (g, dbArea.toFloat(), 4.0f);
        vintage::lcdText (g, dbText (gain) + " dB", dbArea.toFloat(), juce::jmin (17.0f, dbArea.getHeight() * 1.0f));
    }
}

void MixerView::Channel::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    r.removeFromTop (8);
    nameArea = r.removeFromTop (20);
    statusArea = r.removeFromTop (15);
    r.removeFromTop (4);
    auto row1 = r.removeFromTop (22);   // on (+ jack), rec, or the echo's time
    r.removeFromTop (4);
    auto row2 = r.removeFromTop (22);   // FX
    r.removeFromTop (4);

    // The knobs get what's left after a fader of at least 90.
    const int spare = r.getHeight() - (24 + 6 + 22 + 4 + 18 + 4 + 90 + 8);
    const int k = juce::jlimit (22, 42, juce::jmin ((r.getWidth() - 4) / 2, spare / 2 - 14));
    knobRowA = r.removeFromTop (k + 14);
    knobRowB = r.removeFromTop (k + 14);
    r.removeFromTop (4);
    auto row3 = r.removeFromTop (24);   // mute, solo
    r.removeFromTop (6);
    auto learnRow = r.removeFromBottom (22);
    r.removeFromBottom (4);
    dbArea = r.removeFromBottom (18);
    r.removeFromBottom (6);
    meterArea = r.removeFromRight (juce::jlimit (6, 10, r.getWidth() / 9));
    r.removeFromRight (4);
    faderArea = r;
    fader.setBounds (faderArea);

    if (on.isVisible())
        on.setBounds (row1);
    if (jack.isVisible())
    {
        // The Input's jack shares the FX row, so its name fits.
        jack.setBounds (row2.removeFromRight (row2.getWidth() / 2 - 1));
        row2.removeFromRight (2);
    }
    if (rec.isVisible())
        rec.setBounds (row1);
    if (time.isVisible())
        time.setBounds (row1);
    if (fx.isVisible())
        fx.setBounds (row2);

    auto knobIn = [k] (juce::Rectangle<int> area) { return area.withTrimmedBottom (14).withSizeKeepingCentre (k, k); };
    if (kind == Kind::tool)
    {
        auto a = knobRowA;
        sendA.setBounds (knobIn (a.removeFromLeft (a.getWidth() / 2)));
        sendB.setBounds (knobIn (a));
        pan.setBounds (knobIn (knobRowB));
        mute.setBounds (row3.removeFromLeft (row3.getWidth() / 2 - 1));
        row3.removeFromLeft (2);
        solo.setBounds (row3);
    }
    else
    {
        if (kind == Kind::reverb)
        {
            auto a = knobRowA;
            amount.setBounds (knobIn (a.removeFromLeft (a.getWidth() / 2)));
            tone.setBounds (knobIn (a));
        }
        else if (kind == Kind::echo)
            amount.setBounds (knobIn (knobRowA));
        mute.setBounds (row3);
    }
    if (learn.isVisible())
        learn.setBounds (learnRow);
}

//==============================================================================
MixerView::MixerView (RigEngine& e) : engine (e), look (std::make_unique<ConsoleLook>()), pad (e.getPerformFx())
{
    setOpaque (true);
    setLookAndFeel (look.get());

    const juce::StringArray labels { "Groove", "MPC", "Drum machine", "Bass", "Synth", "Vocoder", "Sampler", "Looper", "Song", "Sounds", "Setup" };
    const juce::StringArray tips { "The Groove: every track's steps and patterns on one screen.",
                                   "The MPC: pads, programs, sequences and songs.",
                                   "The drum machine: analog sounds and patterns.",
                                   "The Bass: its sound, patterns and your plugin.",
                                   "The Synth: its sound, the arp and your plugin.",
                                   "The Vocoder: your voice through the pedals.",
                                   "The Sampler and MPC pads, with the full sample editor.",
                                   "The Looper: eight layers in time.",
                                   "The Song builder: parts and the timeline.",
                                   "Your sounds and Splice folder.",
                                   "Set-up: what your keyboard's keys and pads play, the split, sampling between tools, knob Learn, the click and the mic." };
    for (int i = 0; i < hubPages().size(); ++i)
    {
        auto* b = hub.add (new rigpanels::PanelButton (labels[i]));
        b->fontSize = 15.0f;
        b->setTooltip ("Opens " + tips[i]);
        b->onClick = [this, i] { if (onOpen) onOpen (hubPages()[i]); };
        addAndMakeVisible (b);
    }

    for (auto* b : { &toolsBank, &layersBank, &padBank, &clearSolo })
    {
        b->fontSize = 14.0f;
        addAndMakeVisible (b);
    }
    toolsBank.setTooltip ("A strip for each tool: the MPC, drum machine, bass, synth, vocoder, sampler, your audio input and the loops.");
    layersBank.setTooltip ("A strip for each of the looper's eight layers: record, effects, mute and level, into the Loops strip.");
    clearSolo.setTooltip ("Turns every Solo off, so you hear everything again.");
    padBank.setTooltip ("The FX pad on the whole mix: filter sweeps, beat repeat, tape stop, gate and riser. Touch to play it, Hold keeps it.");
    padBank.onClick = [this] { showPad (! padShown); };
    addChildComponent (pad);
    toolsBank.onClick = [this] { showLayers (false); };
    layersBank.onClick = [this] { showLayers (true); };
    clearSolo.setEnabled (false);
    clearSolo.onClick = [this]
    {
        for (int s = 0; s < RigEngine::numStrips; ++s)
            engine.getStrip (s).solo = false;
        refresh();
    };

    for (int s : toolStrips())
        addAndMakeVisible (tools.add (new Channel (*this, Channel::Kind::tool, s)));
    for (int l = 0; l < RigLooper::kLayers; ++l)
        addChildComponent (layers.add (new Channel (*this, Channel::Kind::layer, l)));
    addAndMakeVisible (returnsAndMaster.add (new Channel (*this, Channel::Kind::reverb, RigEngine::reverbReturn)));
    addAndMakeVisible (returnsAndMaster.add (new Channel (*this, Channel::Kind::echo, RigEngine::echoReturn)));
    addAndMakeVisible (returnsAndMaster.add (new Channel (*this, Channel::Kind::master, RigEngine::masterStrip)));

    showLayers (false);
    startTimerHz (30);
}

MixerView::~MixerView()
{
    stopTimer();
    if (panel != nullptr)
        removeChildComponent (panel.getComponent());
    setLookAndFeel (nullptr);
}

void MixerView::refresh()
{
    for (auto* list : { &tools, &layers, &returnsAndMaster })
        for (auto* c : *list)
            c->refresh();
    repaint();
}

void MixerView::setPanel (juce::Component* p)
{
    if (panel != nullptr && panel.getComponent() != p)
        removeChildComponent (panel.getComponent());
    panel = p;
    if (panel != nullptr)
        addAndMakeVisible (panel);
    resized();
}

void MixerView::showLayers (bool show)
{
    layersShown = show;
    if (padShown)
    {
        // Leaving the pad lets go of it (unless Hold is on), as the Big stage's did.
        pad.release();
    }
    padShown = false;
    pad.setVisible (false);
    for (auto* c : tools)
        c->setVisible (! show || c->index == RigEngine::loopsStrip);
    for (auto* c : layers)
        c->setVisible (show);
    toolsBank.lit = show ? Lit::none : Lit::current;
    layersBank.lit = show ? Lit::current : Lit::none;
    padBank.lit = Lit::none;
    for (auto* b : { &toolsBank, &layersBank, &padBank })
        b->repaint();
    resized();
    repaint();
}

void MixerView::showPad (bool show)
{
    if (! show)
        return showLayers (layersShown);
    padShown = true;
    for (auto* c : tools)
        c->setVisible (false);
    for (auto* c : layers)
        c->setVisible (false);
    pad.setVisible (true);
    toolsBank.lit = layersBank.lit = Lit::none;
    padBank.lit = Lit::current;
    for (auto* b : { &toolsBank, &layersBank, &padBank })
        b->repaint();
    resized();
    repaint();
}

MixerView::Channel* MixerView::channelFor (int strip) const
{
    for (auto* c : tools)
        if (c->index == strip)
            return c;
    return strip == RigEngine::masterStrip ? returnsAndMaster[2] : nullptr;
}

MixerView::Channel* MixerView::returnChannel (int returnId) const
{
    return juce::isPositiveAndBelow (returnId, (int) RigEngine::numReturns) ? returnsAndMaster[returnId] : nullptr;
}

MixerView::Channel* MixerView::layerChannel (int layer) const
{
    return juce::isPositiveAndBelow (layer, layers.size()) ? layers[layer] : nullptr;
}

std::vector<std::pair<juce::Button*, int>> MixerView::learnButtons() const
{
    std::vector<std::pair<juce::Button*, int>> list;
    for (auto* c : tools)
        list.emplace_back (&c->learn, faderControl (c->index));
    list.emplace_back (&returnsAndMaster[2]->learn, faderControl (RigEngine::masterStrip));
    return list;
}

void MixerView::timerCallback()
{
    for (auto* list : { &tools, &layers, &returnsAndMaster })
        for (auto* c : *list)
            if (c->isVisible())
                c->tick();

    // The hub's lamps: green while that tool is sounding.
    juce::String sig;
    for (int i = 0; i < hub.size(); ++i)
    {
        int strip = -1;
        const auto page = hubPages()[i];
        for (int s : toolStrips())
            if (pageFor (s) == page && s != RigEngine::inputStrip)
                strip = s;
        const bool sounding = strip >= 0 && meterOf && meterOf (strip) > 0.05f && ! engine.isSilenced (strip);
        const auto lit = sounding ? Lit::play : Lit::none;
        if (hub[i]->lit != lit)
        {
            hub[i]->lit = lit;
            hub[i]->repaint();
        }
    }
    const bool solos = engine.anySolo();
    const auto soloLit = solos ? Lit::next : Lit::none;
    if (clearSolo.lit != soloLit)
    {
        clearSolo.lit = soloLit;
        clearSolo.setEnabled (solos);
        clearSolo.repaint();
    }
}

void MixerView::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    vintage::engraved (g, padShown ? juce::String::fromUTF8 ("The FX pad plays on the whole mix \xc2\xb7 touch it, let go to stop \xc2\xb7 Hold keeps it") : layersShown ? juce::String::fromUTF8 ("The looper's layers \xc2\xb7 into the Loops strip \xc2\xb7 sends A and B on every tool")
                                      : juce::String::fromUTF8 ("Click a name to go into the tool \xc2\xb7 A: reverb \xc2\xb7 B: echo \xc2\xb7 M mute \xc2\xb7 S solo"),
                       legendArea, juce::Justification::centredLeft, 12.0f);

    // The returns and master stand apart, behind a line.
    if (auto* first = returnsAndMaster[0]; first != nullptr && first->isVisible())
    {
        const int x = first->getX() - 9;
        g.setColour (vintage::pal().engrave.withAlpha (0.5f));
        g.fillRect (x, first->getY() + 10, 2, first->getHeight() - 20);
    }
}

void MixerView::resized()
{
    auto area = getLocalBounds().reduced (14, 8);

    // The hub: every tool.
    hubArea = area.removeFromTop (44);
    {
        auto row = hubArea;
        const int gap = 6;
        const int w = (row.getWidth() - gap * (hub.size() - 1)) / juce::jmax (1, hub.size());
        for (auto* b : hub)
        {
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    }
    area.removeFromTop (8);

    auto bankRow = area.removeFromTop (30);
    toolsBank.setBounds (bankRow.removeFromLeft (96));
    bankRow.removeFromLeft (6);
    layersBank.setBounds (bankRow.removeFromLeft (124));
    bankRow.removeFromLeft (6);
    padBank.setBounds (bankRow.removeFromLeft (96));
    bankRow.removeFromLeft (14);
    clearSolo.setBounds (bankRow.removeFromRight (120));
    legendArea = bankRow;
    area.removeFromTop (8);

    if (panel != nullptr)
    {
        panel->setBounds (area.removeFromBottom (juce::jmin (150, area.getHeight() / 3)));
        area.removeFromBottom (8);
    }
    deskArea = area;
    layoutStrips (area);
}

void MixerView::layoutStrips (juce::Rectangle<int> area)
{
    if (padShown)
    {
        // The pad where the strips were; the returns and master keep their place on the right.
        const int gap = 6, groupGap = 18;
        const int w = juce::jlimit (70, 110, area.getWidth() / 12);
        auto right = area.removeFromRight (w * returnsAndMaster.size() + gap + groupGap);
        pad.setBounds (area.withTrimmedRight (groupGap));
        right.removeFromLeft (groupGap - gap);
        for (int i = 0; i < returnsAndMaster.size(); ++i)
        {
            returnsAndMaster[i]->setBounds (right.removeFromLeft (w));
            right.removeFromLeft (i == 1 ? groupGap : gap);
        }
        return;
    }

    std::vector<Channel*> left;
    for (auto* c : tools)
        if (c->isVisible() && c->index != RigEngine::loopsStrip)
            left.push_back (c);
    for (auto* c : layers)
        if (c->isVisible())
            left.push_back (c);
    left.push_back (tools[(int) toolStrips().size() - 1]);   // the Loops strip, last of the tools

    const int gap = 6, groupGap = 18;
    const int count = (int) left.size() + returnsAndMaster.size();
    const int w = (area.getWidth() - gap * (count - 2) - groupGap * 2) / juce::jmax (1, count);
    for (auto* c : left)
    {
        c->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (gap);
    }
    area.removeFromLeft (groupGap - gap);
    for (int i = 0; i < returnsAndMaster.size(); ++i)
    {
        returnsAndMaster[i]->setBounds (area.removeFromLeft (w));
        area.removeFromLeft (i == 1 ? groupGap : gap);
    }
}
