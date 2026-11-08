#include "VocoderScreen.h"

#include "Parameters.h"   // the vocoder's

using namespace vintage;

namespace
{
// Chords' moods, happy to sad, and the scale each one sings in.
struct Mood { const char* name; int scale; };
const Mood moods[] = { { "Dreamy", 8 }, { "Happy", 1 }, { "Sunny", 7 }, { "Cool", 6 }, { "Sad", 2 }, { "Spooky", 10 }, { "Dark", 9 } };
constexpr int numMoods = (int) std::size (moods);
constexpr int kNewestEra = 8;   // eras 0-8 are 1900s-1990s (then phone and megaphone)

juce::String choiceText (juce::AudioProcessorValueTreeState& params, const char* id, int index)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (params.getParameter (id)))
        return c->choices[juce::jlimit (0, c->choices.size() - 1, index)];
    return {};
}

void drawLeds (juce::Graphics& g, juce::Rectangle<float> r, float level, int count, bool vertical)
{
    const float gap = 3.0f;
    const float size = ((vertical ? r.getHeight() : r.getWidth()) - gap * (count - 1)) / count;
    const int lit = juce::roundToInt (juce::jlimit (0.0f, 1.0f, level) * count);
    for (int i = 0; i < count; ++i)
    {
        const auto seg = vertical ? juce::Rectangle<float> (r.getX(), r.getBottom() - (i + 1) * size - i * gap, r.getWidth(), size)
                                  : juce::Rectangle<float> (r.getX() + i * (size + gap), r.getY(), size, r.getHeight());
        const float share = (float) i / count;
        const auto colour = share > 0.85f ? colours::arm : share > 0.65f ? colours::next : colours::play;
        g.setColour (i < lit ? colour : juce::Colour (0xff2e2925));
        g.fillRoundedRectangle (seg, 2.0f);
        if (i < lit)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.drawRoundedRectangle (seg.expanded (1.0f), 2.5f, 1.5f);
        }
    }
}
}

//==============================================================================
// A stomp box: a coloured enclosure with a lamp, a name plate, one big knob and a footswitch.
class VocoderScreen::Pedal : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    Pedal (const juce::String& n, const juce::String& d, const juce::String& k, juce::Colour c)
        : name (n), description (d), knobName (k), colour (c)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob.setMouseDragSensitivity (220);
        knob.setColour (juce::Slider::rotarySliderFillColourId,
                        c.getPerceivedBrightness() > 0.5f ? juce::Colour (0xff1e1a16) : juce::Colour (0xfff1e7d1));
        addAndMakeVisible (knob);
    }

    void paint (juce::Graphics& g) override
    {
        const bool lit = on != nullptr && on();
        auto body = getLocalBounds().toFloat().reduced (2.0f);
        const auto base = lit ? colour : colour.darker (0.5f).withMultipliedSaturation (0.7f);
        glow (g, body, 14.0f, juce::Colours::black, 10.0f, 0.25f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), 0, body.getY(), base.darker (0.15f), 0, body.getBottom(), false));
        g.fillRoundedRectangle (body, 14.0f);
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.drawRoundedRectangle (body.reduced (0.5f), 14.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (body.getX() + 14, body.getY() + 1.5f, body.getRight() - 14, body.getY() + 1.5f, 1.0f);

        const bool light = base.getPerceivedBrightness() > 0.5f;
        const auto ink = light ? juce::Colour (0xff1e1a16) : juce::Colour (0xfff1e7d1);

        // Lamp and ON/OFF.
        drawLamp (g, { (float) topRow.getX() + 9, (float) topRow.getCentreY() }, lit ? Lit::play : Lit::none, 16.0f);
        g.setColour (ink);
        g.setFont (cond (19.0f, Weight::extra, 0.08f));
        g.drawText (lit ? "ON" : "OFF", topRow.withTrimmedLeft (24), juce::Justification::centredLeft);

        // The name plate.
        const auto plateArea = plate.toFloat();
        g.setColour (juce::Colour (0xff1e1a16));
        g.fillRoundedRectangle (plateArea, 6.0f);
        auto text = plate.reduced (6, 4);
        g.setColour (juce::Colour (0xfff1e7d1));
        g.setFont (cond (juce::jmin (22.0f, plate.getHeight() * 0.42f), Weight::extra, 0.03f));
        g.drawFittedText (name, text.removeFromTop (text.getHeight() * 6 / 10), juce::Justification::centredBottom, 2, 0.8f);
        g.setColour (juce::Colour (0xffbdaf97));
        g.setFont (barlow (12.0f));
        g.drawFittedText (description, text, juce::Justification::centredTop, 1, 0.8f);

        // The knob's name and value.
        g.setColour (ink);
        g.setFont (cond (17.0f, Weight::extra, 0.08f));
        g.drawText (knobName.toUpperCase(), knobLabel, juce::Justification::centred);
        if (valueText != nullptr)
        {
            const auto box = valueBox.toFloat();
            drawLcd (g, box, 5.0f);
            lcdText (g, valueText(), box, 18.0f);
        }

        // The footswitch.
        const auto sw = footswitch.toFloat();
        glow (g, sw, sw.getWidth() / 2, juce::Colours::black, 8.0f, 0.35f);
        g.setColour (juce::Colour (0xff2a2622));
        g.fillEllipse (sw);
        const auto cap = sw.reduced (sw.getWidth() * 0.08f).translated (0, pressed ? 2.0f : 0.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f1ea), cap.getX(), cap.getY(), juce::Colour (0xff9a958c), cap.getRight(),
                                                 cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.drawEllipse (cap.reduced (cap.getWidth() * 0.12f), 2.0f);
        g.setColour (juce::Colour (0xff2a2622));
        g.setFont (cond (juce::jmin (17.0f, cap.getHeight() * 0.28f), Weight::extra, 0.06f));
        g.drawText (lit ? "ON" : "OFF", cap, juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 10);
        topRow = r.removeFromTop (24);
        r.removeFromTop (6);
        plate = r.removeFromTop (juce::jlimit (44, 58, getHeight() / 8));
        const int switchSize = juce::jlimit (54, 76, juce::jmin (r.getWidth() - 20, getHeight() / 5));
        footswitch = r.removeFromBottom (switchSize).withSizeKeepingCentre (switchSize, switchSize);
        r.removeFromBottom (10);
        valueBox = r.removeFromBottom (26).withSizeKeepingCentre (juce::jmin (r.getWidth(), 110), 26);
        r.removeFromBottom (4);
        knobLabel = r.removeFromBottom (22);
        r.removeFromBottom (4);
        const int side = juce::jmin (r.getWidth() - 10, r.getHeight() - 6, 132);
        knob.setBounds (r.withSizeKeepingCentre (side, side));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (footswitch.contains (e.getPosition()) || topRow.contains (e.getPosition()))
        {
            pressed = true;
            repaint();
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (pressed)
        {
            pressed = false;
            if ((footswitch.contains (e.getPosition()) || topRow.contains (e.getPosition())) && onStomp != nullptr)
                onStomp();
            repaint();
        }
    }

    juce::String name, description, knobName;
    juce::Colour colour;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::function<bool()> on;
    std::function<void()> onStomp;
    std::function<juce::String()> valueText;

private:
    juce::Rectangle<int> topRow, plate, knobLabel, valueBox, footswitch;
    bool pressed = false;
};

//==============================================================================
VocoderScreen::VocoderScreen (RigEngine& e, juce::Component* studioView, bool isStandalone)
    : engine (e), standalone (isStandalone), studio (studioView)
{
    setOpaque (true);
    auto& params = engine.getVocoder().params;

    for (auto* b : { &easyButton, &studioButton })
    {
        setLit (*b, Lit::current);
        b->getProperties().set ("fontSize", 20.0f);
        addAndMakeVisible (*b);
    }
    easyButton.onClick = [this] { setStudio (false); };
    studioButton.onClick = [this] { setStudio (true); };
    easyButton.setTooltip ("Easy: one big knob per pedal. Kid-safe.");
    studioButton.setTooltip ("Studio: the vocoder's full window, every setting.");

    bypassButton.getProperties().set ("fontSize", 17.0f);
    setLit (bypassButton, Lit::mute);
    bypassButton.setTooltip ("Switches every pedal off, to hear your voice dry. Press again to put them back.");
    bypassButton.onClick = [this] { pressBypassAll(); };
    addAndMakeVisible (bypassButton);

    setInWell (notesButton, true);
    notesButton.getProperties().set ("fontSize", 17.0f);
    notesButton.setTooltip ("Which keys play the robot's notes. Click to send the keys to the vocoder.");
    notesButton.onClick = [this] { pressNotesFrom(); };
    addAndMakeVisible (notesButton);

    // The pedals.
    const std::array<juce::Colour, numPedals> pedalColours { juce::Colour (0xff5fb3c4), juce::Colour (0xff6e5e8c), juce::Colour (0xffc9a472),
                                                             juce::Colour (0xffa0594d), juce::Colour (0xffefcb45) };
    const char* info[numPedals][3] = { { "Robot", "Classic vocoder", "Beep-ness" },
                                       { "Monster", "Low & huge", "Growl" },
                                       { "Vintage Mic", "Old radio ribbon", "Old-timey" },
                                       { "Arp", "Chops the notes", "Speed" },
                                       { "Chords", "One note \xe2\x86\x92 a chord", "Happy \xe2\x86\x94 sad" } };
    for (int i = 0; i < numPedals; ++i)
    {
        auto* p = pedals.add (new Pedal (juce::String::fromUTF8 (info[i][0]), juce::String::fromUTF8 (info[i][1]), juce::String::fromUTF8 (info[i][2]),
                                         pedalColours[(size_t) i]));
        p->on = [this, i] { return isOn (i); };
        p->onStomp = [this, i] { stomp (i); };
        addAndMakeVisible (p);
    }

    auto& robotPedal = *pedals[robot];
    robotPedal.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, ParamIDs::sharpness, robotPedal.knob);
    robotPedal.valueText = [this] { return juce::String (param (ParamIDs::sharpness), 1) + "x"; };
    robotPedal.setTooltip ("The robot: your voice plays the notes you hold on the keys. Beep-ness makes it ring like a machine.");

    auto& monsterPedal = *pedals[monster];
    monsterPedal.knob.setRange (1.0, 12.0, 0.5);
    monsterPedal.knob.setValue (monsterGrowl, juce::dontSendNotification);
    monsterPedal.knob.onValueChange = [this]
    {
        monsterGrowl = (float) pedals[monster]->knob.getValue();
        if (isOn (monster))
            setParam (ParamIDs::formant, -monsterGrowl);
        pedals[monster]->repaint();
    };
    monsterPedal.valueText = [this] { return "-" + juce::String (monsterGrowl, 1) + " st"; };
    monsterPedal.setTooltip ("Monster: moves your voice's character down, bigger and deeper. Growl is how far.");

    auto& vintagePedal = *pedals[vintageMic];
    vintagePedal.knob.setRange (0.0, (double) kNewestEra, 1.0);
    vintagePedal.knob.setMouseDragSensitivity (140);
    vintagePedal.knob.onValueChange = [this]
    {
        setParam (ParamIDs::vintageEra, (float) (kNewestEra - juce::roundToInt (pedals[vintageMic]->knob.getValue())));
        pedals[vintageMic]->repaint();
    };
    vintagePedal.valueText = [this, &params]
    {
        return choiceText (params, ParamIDs::vintageEra, juce::roundToInt (param (ParamIDs::vintageEra)));
    };
    vintagePedal.setTooltip ("Vintage mic: sing into a microphone from another time. Turn up for older: the 1990s down to the 1900s.");

    auto& arpPedal = *pedals[arp];
    arpPedal.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, ParamIDs::arpRate, arpPedal.knob);
    arpPedal.knob.setMouseDragSensitivity (120);
    arpPedal.valueText = [this, &params] { return choiceText (params, ParamIDs::arpRate, juce::roundToInt (param (ParamIDs::arpRate))); };
    arpPedal.setTooltip ("Arp: plays the robot's chord note by note, or chops your voice in time. Speed is the note length.");

    auto& chordsPedal = *pedals[chords];
    chordsPedal.knob.setRange (0.0, (double) numMoods - 1, 1.0);
    chordsPedal.knob.setValue (1.0, juce::dontSendNotification);   // Happy
    chordsPedal.knob.setMouseDragSensitivity (120);
    chordsPedal.knob.onValueChange = [this]
    {
        setParam (ParamIDs::tuneScale, (float) moods[juce::roundToInt (pedals[chords]->knob.getValue())].scale);
        pedals[chords]->repaint();
    };
    chordsPedal.valueText = [this] { return juce::String (moods[juce::jlimit (0, numMoods - 1, juce::roundToInt (pedals[chords]->knob.getValue()))].name); };
    chordsPedal.setTooltip ("Chords: sing one note and hear a whole chord. Turn from happy to sad for the mood.");

    // Raw voice and Out.
    micGain = std::make_unique<Knob> (params, ParamIDs::inputGain, "Mic gain", colours::vocoder);
    micGain->inWell = true;
    micGain->slider.textFromValueFunction = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 0) + " dB"; };
    addAndMakeVisible (*micGain);

    for (auto* b : { &monitorButton, &recVoiceButton, &toLooperButton })
    {
        setInWell (*b, true);
        b->getProperties().set ("fontSize", 16.0f);
        addAndMakeVisible (*b);
    }
    setLit (monitorButton, Lit::play);
    setLit (recVoiceButton, Lit::arm);
    setLit (toLooperButton, Lit::arm);
    monitorButton.setTooltip ("Monitor: hear your own voice (as well as the pedals).");
    recVoiceButton.setTooltip ("Records the mic into the Sampler, from the next bar while playing. Press again to stop.");
    toLooperButton.setTooltip ("Records the vocoder onto the next free loop layer. Press again to stop it and loop it.");
    monitorButton.onClick = [this] { pressMonitor(); };
    recVoiceButton.onClick = [this]
    {
        engine.captureSource = RigCapture::mic;
        engine.captureDest = RigCapture::toSampler;
        if (onRecVoice)
            onRecVoice();
        updateButtons();
    };
    toLooperButton.onClick = [this] { pressToLooper(); };

    dryWet.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    dryWet.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dryWet.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    dryWet.setColour (juce::Slider::rotarySliderFillColourId, colours::vocoder);
    dryWet.setRange (0.0, 1.0, 0.01);
    dryWet.setDoubleClickReturnValue (true, 0.5);
    dryWet.setTooltip ("Dry (your voice) to wet (the robot). In the middle you hear both at full.");
    dryWet.onValueChange = [this] { setDryWet (dryWet.getValue()); };
    addAndMakeVisible (dryWet);

    if (studio != nullptr)
        addChildComponent (*studio);

    inputHistory.assign (120, 0.0f);
    refresh();
    startTimerHz (30);
}

VocoderScreen::~VocoderScreen()
{
    stopTimer();
}

juce::String VocoderScreen::pedalName (int i)
{
    static const char* names[] = { "Robot", "Monster", "Vintage Mic", "Arp", "Chords" };
    return names[juce::jlimit (0, numPedals - 1, i)];
}

float VocoderScreen::param (const char* id) const
{
    if (auto* v = engine.getVocoder().params.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void VocoderScreen::setParam (const char* id, float value)
{
    if (auto* p = engine.getVocoder().params.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

//==============================================================================
bool VocoderScreen::isOn (int pedal) const
{
    switch (pedal)
    {
        case robot: return param (ParamIDs::robotOn) > 0.5f;
        case monster: return param (ParamIDs::formant) <= -0.5f;
        case vintageMic: return param (ParamIDs::vintageOn) > 0.5f;
        case arp: return param (ParamIDs::arpOn) > 0.5f && juce::roundToInt (param (ParamIDs::arpMode)) != 0;
        case chords: return param (ParamIDs::tuneOn) > 0.5f && juce::roundToInt (param (ParamIDs::singToPlay)) == 3;
        default: return false;
    }
}

void VocoderScreen::stomp (int pedal)
{
    const bool wasOn = isOn (pedal);
    switch (pedal)
    {
        case robot: setParam (ParamIDs::robotOn, wasOn ? 0.0f : 1.0f); break;
        case monster: setParam (ParamIDs::formant, wasOn ? 0.0f : -monsterGrowl); break;
        case vintageMic: setParam (ParamIDs::vintageOn, wasOn ? 0.0f : 1.0f); break;
        case arp:
            if (wasOn)
                setParam (ParamIDs::arpOn, 0.0f);
            else
            {
                // A note arp needs the robot's chord; without the robot, it chops your voice in time.
                if (juce::roundToInt (param (ParamIDs::arpMode)) == 0)
                    setParam (ParamIDs::arpMode, param (ParamIDs::robotOn) > 0.5f ? 1.0f : 2.0f);
                setParam (ParamIDs::arpOn, 1.0f);
            }
            break;
        case chords:
            if (wasOn)
            {
                setParam (ParamIDs::singToPlay, 0.0f);
                setParam (ParamIDs::tuneOn, 0.0f);
            }
            else
            {
                setParam (ParamIDs::tuneScale, (float) moods[juce::roundToInt (pedals[chords]->knob.getValue())].scale);
                setParam (ParamIDs::tuneOn, 1.0f);
                setParam (ParamIDs::singToPlay, 3.0f);
            }
            break;
        default: break;
    }
    bypassed = false;
    if (! engine.vocoderOn.load() && ! wasOn && onSay)
        onSay ("The vocoder is switched off in the Mixer, so you won't hear the pedals until it's on.");
    updateButtons();
    repaint();
}

void VocoderScreen::turnKnob (int pedal, double value)
{
    pedals[juce::jlimit (0, numPedals - 1, pedal)]->knob.setValue (value, juce::sendNotificationSync);
}

void VocoderScreen::pressBypassAll()
{
    if (! bypassed)
    {
        bool any = false;
        for (int i = 0; i < numPedals; ++i)
        {
            beforeBypass[(size_t) i] = isOn (i);
            if (beforeBypass[(size_t) i])
            {
                stomp (i);
                any = true;
            }
        }
        bypassed = any;
        if (onSay)
            onSay (any ? "Every pedal is off: your voice, dry. Press Bypass all again to put them back." : "The pedals are all off already.");
    }
    else
    {
        for (int i = 0; i < numPedals; ++i)
            if (beforeBypass[(size_t) i] && ! isOn (i))
                stomp (i);
        bypassed = false;
    }
    updateButtons();
    repaint();
}

void VocoderScreen::pressMonitor()
{
    setParam (ParamIDs::voiceOn, param (ParamIDs::voiceOn) > 0.5f ? 0.0f : 1.0f);
    updateButtons();
}

void VocoderScreen::pressToLooper()
{
    auto& looper = engine.getLooper();
    if (looperLayer >= 0)
    {
        const auto state = looper.getState (looperLayer);
        if (state == RigLooper::State::armed || state == RigLooper::State::recording || state == RigLooper::State::empty)
        {
            looper.pressRecord (looperLayer);
            if (onSay)
                onSay ("Looping it on layer " + juce::String (looperLayer + 1) + ".");
            looperLayer = -1;
            updateButtons();
            return;
        }
    }
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) == RigLooper::State::empty)
        {
            looper.getLayer (i).source = RigLooper::vocoder;
            looper.pressRecord (i);
            looperLayer = i;
            if (onSay)
                onSay ("Recording the vocoder onto loop layer " + juce::String (i + 1)
                       + (engine.getTransport().isPlaying() ? " from the next bar." : ". Press Play to start.") + " Press To looper again to loop it.");
            updateButtons();
            return;
        }
    if (onSay)
        onSay ("Every loop layer is in use. Clear one on the Looper screen first.");
}

void VocoderScreen::pressNotesFrom()
{
    engine.getRouting().keysTo = (int) RigRouting::Dest::vocoder;
    updateButtons();
}

void VocoderScreen::setDryWet (double wet)
{
    wet = juce::jlimit (0.0, 1.0, wet);
    setParam (ParamIDs::robotLevel, (float) juce::jmin (1.0, wet * 2.0));
    setParam (ParamIDs::voiceLevel, (float) (0.9 * juce::jmin (1.0, (1.0 - wet) * 2.0)));
    if (std::abs (dryWet.getValue() - wet) > 0.001)
        dryWet.setValue (wet, juce::dontSendNotification);
}

double VocoderScreen::getDryWet() const
{
    const float robotLevel = param (ParamIDs::robotLevel), voiceLevel = param (ParamIDs::voiceLevel) / 0.9f;
    if (robotLevel < 0.999f)
        return robotLevel / 2.0;
    return juce::jlimit (0.5, 1.0, 1.0 - voiceLevel / 2.0);
}

void VocoderScreen::setStudio (bool show)
{
    studioShown = show && studio != nullptr;
    if (studio != nullptr)
        studio->setVisible (studioShown);
    for (auto* p : pedals)
        p->setVisible (! studioShown);
    for (juce::Component* c : { (juce::Component*) micGain.get(), (juce::Component*) &monitorButton, (juce::Component*) &recVoiceButton,
                                (juce::Component*) &toLooperButton, (juce::Component*) &dryWet })
        c->setVisible (! studioShown);
    updateButtons();
    resized();
    repaint();
}

//==============================================================================
void VocoderScreen::refresh()
{
    // The knobs that aren't parameters follow the vocoder's settings.
    if (param (ParamIDs::formant) <= -0.5f)
        monsterGrowl = juce::jlimit (1.0f, 12.0f, -param (ParamIDs::formant));
    pedals[monster]->knob.setValue (monsterGrowl, juce::dontSendNotification);
    const int era = juce::roundToInt (param (ParamIDs::vintageEra));
    pedals[vintageMic]->knob.setValue (kNewestEra - juce::jlimit (0, kNewestEra, era), juce::dontSendNotification);
    const int scale = juce::roundToInt (param (ParamIDs::tuneScale));
    for (int i = 0; i < numMoods; ++i)
        if (moods[i].scale == scale)
            pedals[chords]->knob.setValue (i, juce::dontSendNotification);
    dryWet.setValue (getDryWet(), juce::dontSendNotification);
    updateButtons();
    repaint();
}

void VocoderScreen::updateButtons()
{
    easyButton.setToggleState (! studioShown, juce::dontSendNotification);
    studioButton.setToggleState (studioShown, juce::dontSendNotification);
    bypassButton.setToggleState (bypassed, juce::dontSendNotification);
    monitorButton.setToggleState (param (ParamIDs::voiceOn) > 0.5f, juce::dontSendNotification);
    monitorButton.setButtonText (monitorButton.getToggleState() ? "Monitor on" : "Monitor off");
    const bool sampling = isSampling != nullptr && isSampling() && engine.getCapture().getSource() == RigCapture::mic;
    recVoiceButton.setToggleState (sampling, juce::dontSendNotification);
    recVoiceButton.setButtonText (sampling ? "Stop rec" : "Rec voice");
    bool looping = false;
    if (looperLayer >= 0)
    {
        const auto state = engine.getLooper().getState (looperLayer);
        looping = state == RigLooper::State::armed || state == RigLooper::State::recording;
        // Empty: the press isn't handled yet. Playing: it's looping, so the next press starts a new layer.
        looping = looping || state == RigLooper::State::empty;
        if (state == RigLooper::State::playing || state == RigLooper::State::overdubbing)
            looperLayer = -1;
    }
    toLooperButton.setToggleState (looping, juce::dontSendNotification);
    toLooperButton.setButtonText (looping ? "Loop it" : "To looper");

    const int keys = engine.getRouting().keysTo.load();
    juce::String text;
    if (keys == (int) RigRouting::Dest::vocoder)
        text = standalone ? "Launchkey keys" : "Logic MIDI";
    else
        text = "Keys play " + RigRouting::destName (keys);
    if (notesButton.getButtonText() != text)
        notesButton.setButtonText (text);
    notesButton.setToggleState (keys == (int) RigRouting::Dest::vocoder, juce::dontSendNotification);
    setLit (notesButton, keys == (int) RigRouting::Dest::vocoder ? Lit::current : Lit::none);
}

void VocoderScreen::timerCallback()
{
    const float in = meterOf != nullptr ? meterOf (RigEngine::inputStrip) : 0.0f;
    const float out = meterOf != nullptr ? meterOf (RigEngine::vocoderStrip) : 0.0f;
    inputLevel = in;
    outLevel = out;
    std::rotate (inputHistory.begin(), inputHistory.begin() + 1, inputHistory.end());
    inputHistory.back() = in;

    if (! isShowing())
        return;

    juce::String sig;
    for (int i = 0; i < numPedals; ++i)
        sig << (int) isOn (i);
    sig << "/" << param (ParamIDs::sharpness) << "/" << param (ParamIDs::formant) << "/" << param (ParamIDs::vintageEra) << "/"
        << param (ParamIDs::arpRate) << "/" << param (ParamIDs::tuneScale) << "/" << param (ParamIDs::robotLevel) << "/"
        << param (ParamIDs::voiceLevel) << "/" << (int) engine.micOn.load() << "/" << (int) engine.vocoderOn.load();
    if (sig != shownSignature)
    {
        shownSignature = sig;
        refresh();
    }
    else
        updateButtons();

    if (! studioShown)
    {
        repaint (scopeArea);
        repaint (inputLeds);
        repaint (outLeds);
    }
}

//==============================================================================
void VocoderScreen::paint (juce::Graphics& g)
{
    drawFace (g, getLocalBounds());
    const auto& p = pal();

    engraved (g, studioShown ? "Studio: every setting of the vocoder." : "Easy: one big knob per pedal. Kid-safe.", captionArea,
              juce::Justification::centredLeft, 13.0f);
    engraved (g, "Notes from", notesLabel, juce::Justification::centredRight, 13.0f);

    if (studioShown)
        return;

    // Raw voice.
    drawWell (g, rawWell.toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (cond (22.0f, Weight::extra, 0.08f));
    g.drawText ("RAW VOICE", rawTitle, juce::Justification::centredLeft);
    const bool micLive = standalone ? engine.micOn.load() : true;
    drawLamp (g, micLamp.getCentre().toFloat(), micLive ? Lit::arm : Lit::none, 16.0f);
    engraved (g, juce::String::fromUTF8 (standalone ? (micLive ? "Mic in \xc2\xb7 live" : "Mic off \xc2\xb7 click the lamp") : "Side chain from Logic"), rawSub,
              juce::Justification::centredLeft, 12.0f, true);

    // The mic's wave.
    {
        const auto s = scopeArea.toFloat();
        g.setColour (juce::Colour (0xff0e0c0a));
        g.fillRoundedRectangle (s, 8.0f);
        const int n = (int) inputHistory.size();
        const float w = (s.getWidth() - 12) / n;
        for (int i = 0; i < n; ++i)
        {
            const float level = std::sqrt (juce::jlimit (0.0f, 1.0f, inputHistory[(size_t) i]));
            const float h = juce::jmax (1.5f, level * (s.getHeight() - 12));
            g.setColour ((i > n - 8 && level > 0.05f ? colours::arm : colours::vocoder).withAlpha (0.85f));
            g.fillRect (s.getX() + 6 + i * w, s.getCentreY() - h / 2, juce::jmax (1.0f, w - 1.0f), h);
        }
    }
    engraved (g, "Input level", inputLeds.withHeight (16).translated (0, -18), juce::Justification::centredLeft, 12.0f, true);
    drawLeds (g, inputLeds.toFloat(), std::sqrt (inputLevel), 20, false);

    // The chain.
    engraved (g, juce::String::fromUTF8 ("Effects chain \xc2\xb7 stomp to switch"), chainLabel, juce::Justification::centredLeft, 13.0f);
    for (auto a : { arrow1, arrow2 })
    {
        juce::Path arrow;
        const auto c = a.getCentre().toFloat();
        arrow.startNewSubPath (c.x - 8, c.y);
        arrow.lineTo (c.x + 8, c.y);
        arrow.startNewSubPath (c.x + 2, c.y - 6);
        arrow.lineTo (c.x + 8, c.y);
        arrow.lineTo (c.x + 2, c.y + 6);
        g.setColour (p.ink2);
        g.strokePath (arrow, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Out.
    drawWell (g, outWell.toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (cond (22.0f, Weight::extra, 0.08f));
    g.drawText ("OUT", outTitle, juce::Justification::centred);
    drawLeds (g, outLeds.toFloat(), std::sqrt (outLevel), 18, true);
    engraved (g, juce::String::fromUTF8 ("Dry \xe2\x86\x94 wet"), dryWetLabel, juce::Justification::centred, 12.0f, true);
}

void VocoderScreen::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (22).withTrimmedRight (22).withTrimmedTop (6).withTrimmedBottom (14);
    modeRow = r.removeFromTop (48);
    {
        auto m = modeRow;
        easyButton.setBounds (m.removeFromLeft (110));
        studioButton.setBounds (m.removeFromLeft (110));
        m.removeFromLeft (16);
        bypassButton.setBounds (m.removeFromRight (140));
        m.removeFromRight (12);
        notesButton.setBounds (m.removeFromRight (180).reduced (0, 4));
        notesLabel = m.removeFromRight (110).withTrimmedRight (10);
        captionArea = m;
    }
    r.removeFromTop (14);

    if (studio != nullptr)
        studio->setBounds (r);

    rawWell = r.removeFromLeft (juce::jlimit (170, 210, r.getWidth() / 6));
    outWell = r.removeFromRight (juce::jlimit (96, 120, r.getWidth() / 10));
    arrow1 = r.removeFromLeft (30);
    arrow2 = r.removeFromRight (30);
    chainLabel = r.removeFromTop (20);
    r.removeFromTop (6);
    chainArea = r;
    const int gap = 12;
    const int w = (chainArea.getWidth() - gap * (numPedals - 1)) / numPedals;
    auto row = chainArea;
    for (auto* pedal : pedals)
    {
        pedal->setBounds (row.removeFromLeft (w));
        row.removeFromLeft (gap);
    }
    arrow1 = arrow1.withY (chainArea.getCentreY() - 10).withHeight (20);
    arrow2 = arrow2.withY (chainArea.getCentreY() - 10).withHeight (20);
    rawWell = rawWell.withTop (chainArea.getY() - 26);
    outWell = outWell.withTop (chainArea.getY() - 26);

    {
        auto w2 = rawWell.reduced (14, 14);
        auto title = w2.removeFromTop (28);
        micLamp = title.removeFromRight (20);
        rawTitle = title;
        rawSub = w2.removeFromTop (18);
        w2.removeFromTop (8);
        const int buttons = 44;
        auto bottom = w2.removeFromBottom (buttons);
        const int bw = (bottom.getWidth() - 8) / 2;
        monitorButton.setBounds (bottom.removeFromLeft (bw));
        bottom.removeFromLeft (8);
        recVoiceButton.setBounds (bottom);
        w2.removeFromBottom (14);
        inputLeds = w2.removeFromBottom (12);
        w2.removeFromBottom (24);
        const int gainHeight = juce::jmin (150, w2.getHeight() * 6 / 10);
        micGain->setBounds (w2.removeFromBottom (gainHeight).withSizeKeepingCentre (juce::jmin (w2.getWidth(), 130), gainHeight));
        w2.removeFromBottom (10);
        scopeArea = w2;
    }
    {
        auto o = outWell.reduced (12, 14);
        outTitle = o.removeFromTop (28);
        o.removeFromTop (8);
        toLooperButton.setBounds (o.removeFromBottom (44));
        o.removeFromBottom (10);
        dryWetLabel = o.removeFromBottom (18);
        dryWet.setBounds (o.removeFromBottom (64).withSizeKeepingCentre (64, 64));
        o.removeFromBottom (10);
        outLeds = o.withSizeKeepingCentre (juce::jmin (o.getWidth(), 30), o.getHeight());
    }
}

void VocoderScreen::mouseUp (const juce::MouseEvent& e)
{
    if (! studioShown && standalone && (micLamp.expanded (8).contains (e.getPosition()) || rawSub.contains (e.getPosition())) && onMic)
    {
        onMic();
        repaint (rawWell);
    }
}
