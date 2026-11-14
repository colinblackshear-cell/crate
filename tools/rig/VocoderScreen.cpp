#include "VocoderScreen.h"
#include "VocoderSettings.h"

#include "Parameters.h"   // the vocoder's
#include "Presets.h"
#include "dsp/Chords.h"

using namespace vintage;

namespace
{
struct Setting { const char* id; const char* label; };

struct PedalInfo
{
    const char* name;
    const char* kind;          // under the name
    const char* description;   // in the rack
    juce::uint32 colour;
    Setting knob;              // the pedal's own knob
    const char* switchText;    // on the footswitch (null: ON / OFF)
    std::vector<Setting> rack;
};

const std::vector<PedalInfo>& pedalInfo()
{
    using namespace ParamIDs;
    static const std::vector<PedalInfo> info {
        { "Tune", "auto-tune",
          "Pulls your voice onto the notes of a key. Fast speed gives the hard robotic snap, slow sounds natural. Amount is how far it pulls.",
          0xff4fb39a, { tuneSpeed, "Speed" }, nullptr,
          { { tuneKey, "Key" }, { tuneScale, "Scale" }, { tuneSpeed, "Speed" }, { tuneAmount, "Amount" } } },
        { "Harmony", "sing to play",
          "Sing one note and the synth sings with you: just your melody, your melody plus a third, or a whole chord, in Tune's key and scale.",
          0xff8fc06a, { singToPlay, "Plays" }, nullptr,
          { { singToPlay, "Plays" }, { tuneKey, "Key" }, { tuneScale, "Scale" }, { tuneSpeed, "Speed" } } },
        { "Robot", "vocoder",
          "The classic vocoder: your voice shapes the synth. Hold notes on the keys (or use Chords or Harmony) to give it something to sing. "
          "More bands make the words clearer; Beep makes it ring like a machine.",
          0xff5fb3c4, { sharpness, "Beep" }, nullptr,
          { { bands, "Bands" }, { sharpness, "Beep" }, { attack, "Attack" }, { release, "Release" }, { sibilance, "S and T" },
            { gateThreshold, "Gate" }, { robotLevel, "Level" }, { fillGaps, "Fill gaps" } } },
        { "Character", "voice shape",
          "Moves the character of your voice: down for giants and monsters, up for cartoons and chipmunks. Breath and wobble add life.",
          0xff8a6fb3, { formant, "Character" }, nullptr,
          { { formant, "Character" }, { sibilance, "Air" }, { noise, "Breath" }, { vibratoDepth, "Wobble" }, { vibratoRate, "Wobble rate" } } },
        { "Synth", "robot's synth",
          "The sound the robot sings with: the wave, a second oscillator, a sub below, noise, glide between notes and vibrato. "
          "The footswitch is Drone: it holds a note, so you don't need the keys.",
          0xffe0785a, { waveform, "Wave" }, "DRONE",
          { { waveform, "Wave" }, { octave, "Octave" }, { detune, "Detune" }, { pulseWidth, "Pulse" }, { pwmDepth, "Pulse move" },
            { osc2Wave, "Osc 2 wave" }, { osc2Semitones, "Osc 2 tune" }, { osc2Fine, "Osc 2 fine" }, { osc2Level, "Osc 2 vol" },
            { subLevel, "Sub" }, { noise, "Noise" }, { glide, "Glide" }, { vibratoRate, "Vib speed" }, { vibratoDepth, "Vibrato" },
            { carrierAttack, "Attack" }, { carrierRelease, "Release" }, { droneNote, "Drone note" }, { drone, "Drone" } } },
        { "Chords", "no keys needed",
          "Click a chord and the robot sings it, no keys needed. The chords follow the key and scale; Type makes them richer. "
          "They change on the next beat or bar, so they stay in time.",
          0xffefcb45, { chordFlavour, "Type" }, nullptr,
          { { chordKey, "Key" }, { chordScale, "Scale" }, { chordFlavour, "Type" }, { chordTiming, "Changes on" } } },
        { "Arp", "notes in rhythm",
          "Plays the robot's chord one note at a time (Note arp), or chops your voice in rhythm (Rhythm gate), in time with Kaleidobox.",
          0xffd9534a, { arpRate, "Speed" }, nullptr,
          { { arpMode, "Mode" }, { arpRate, "Speed" }, { arpPattern, "Pattern" }, { arpOctaves, "Octaves" }, { arpGate, "Length" },
            { arpSwing, "Swing" }, { gatePattern, "Rhythm" } } },
        { "Filter", "dark / thin",
          "Sweeps the sound darker or thinner. Voice follow opens it when you sing louder, and the LFO wobbles it in time.",
          0xff4f8fd0, { cutoff, "Cutoff" }, nullptr,
          { { filterMode, "Type" }, { cutoff, "Cutoff" }, { resonance, "Resonance" }, { filterDrive, "Drive" }, { voiceFollow, "Voice follow" },
            { lfoShape, "LFO shape" }, { lfoSync, "LFO speed" }, { lfoRate, "LFO rate" }, { lfoDepth, "LFO amount" } } },
        { "Vintage", "old mics",
          "Sing through a microphone from another time: a 1900s phonograph up to a 90s cassette, or a phone or megaphone. Wear adds the crackle and hiss.",
          0xffc9a472, { vintageEra, "Era" }, nullptr,
          { { vintageEra, "Era" }, { vintageWear, "Wear" }, { vintageAmount, "Amount" } } },
        { "Dirt", "drive + crush",
          "Drive warms it up and then distorts it. Crush makes it crunchy and digital, like an old games console.",
          0xffb0563c, { drive, "Drive" }, nullptr,
          { { drive, "Drive" }, { crush, "Crush" } } },
        { "Chorus", "shimmer",
          "Thickens the sound into a shimmering group of voices.",
          0xff63c2b7, { chorusMix, "Mix" }, nullptr,
          { { chorusMix, "Mix" }, { chorusRate, "Rate" }, { chorusDepth, "Depth" } } },
        { "Echo", "delay in time",
          "Repeats you in time with Kaleidobox. Repeats is how many; Ping-pong bounces them left and right.",
          0xff7d9be0, { delayMix, "Mix" }, nullptr,
          { { delayMix, "Mix" }, { delayTime, "Time" }, { delayFeedback, "Repeats" }, { delayPingPong, "Ping-pong" } } },
        { "Space", "reverb",
          "Puts you in a room, a hall or a cathedral. Size is how big, Darkness softens the echoes.",
          0xff9b8ccf, { reverbMix, "Mix" }, nullptr,
          { { reverbMix, "Mix" }, { reverbSize, "Size" }, { reverbDamping, "Darkness" } } },
    };
    return info;
}

juce::String characterName (float semitones)
{
    if (semitones <= -9.0f) return "Giant";
    if (semitones <= -5.0f) return "Monster";
    if (semitones <= -1.5f) return "Deep";
    if (semitones < 1.5f) return "Natural";
    if (semitones < 5.0f) return "Bright";
    if (semitones < 9.0f) return "Chipmunk";
    return "Mouse";
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
    }
}
}

//==============================================================================
// A small stomp box: a lamp and the name, one knob with its value, a footswitch. Clicking the
// pedal (not the footswitch) opens it in the rack.
class VocoderScreen::Pedal : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    Pedal (const PedalInfo& i) : info (i), colour (juce::Colour (i.colour))
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        knob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        knob.setMouseDragSensitivity (200);
        knob.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xfff1e7d1));
        knob.onDragStart = [this] { if (onSelect) onSelect(); };
        knob.onValueChange = [this] { repaint(); if (onTurn) onTurn(); };
        addAndMakeVisible (knob);
        setTooltip (juce::String (info.description));
    }

    void paint (juce::Graphics& g) override
    {
        const bool lit = on != nullptr && on();
        auto body = getLocalBounds().toFloat().reduced (3.0f);
        if (picked)
        {
            glow (g, body, 12.0f, colours::selected, 8.0f, 0.6f);
            g.setColour (colours::selected);
            g.drawRoundedRectangle (body.expanded (1.5f), 13.0f, 3.0f);
        }
        const auto base = lit ? colour : colour.darker (0.55f).withMultipliedSaturation (0.65f);
        glow (g, body, 12.0f, juce::Colours::black, 8.0f, 0.25f);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.15f), 0, body.getY(), base.darker (0.2f), 0, body.getBottom(), false));
        g.fillRoundedRectangle (body, 12.0f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawRoundedRectangle (body.reduced (0.5f), 12.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (body.getX() + 12, body.getY() + 1.5f, body.getRight() - 12, body.getY() + 1.5f, 1.0f);

        // Lamp, name and what it is, on a dark plate.
        g.setColour (juce::Colour (0xe01e1a16));
        g.fillRoundedRectangle (plate.toFloat(), 6.0f);
        auto p = plate.reduced (4, 3);
        g.setColour (juce::Colour (0xfff1e7d1));
        g.setFont (cond (juce::jmin (19.0f, getWidth() * 0.2f), Weight::extra, 0.02f));
        g.drawFittedText (juce::String (info.name).toUpperCase(), p.removeFromTop (22), juce::Justification::centred, 1, 0.7f);
        g.setColour (juce::Colour (0xffbdaf97));
        g.setFont (barlow (11.5f));
        g.drawFittedText (info.kind, p, juce::Justification::centred, 1, 0.8f);

        drawLamp (g, { body.getX() + 12.0f, (float) plate.getBottom() + 12.0f }, lit ? Lit::play : Lit::none, 12.0f);

        // The knob's value.
        const auto box = valueBox.toFloat();
        drawLcd (g, box, 4.0f);
        lcdText (g, valueText != nullptr ? valueText() : juce::String(), box, juce::jmin (18.0f, box.getHeight() * 0.9f));

        // The footswitch.
        const auto sw = footswitch.toFloat();
        glow (g, sw, sw.getWidth() / 2, juce::Colours::black, 6.0f, 0.35f);
        g.setColour (juce::Colour (0xff2a2622));
        g.fillEllipse (sw);
        const auto cap = sw.reduced (sw.getWidth() * 0.09f).translated (0, pressed ? 2.0f : 0.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff4f1ea), cap.getX(), cap.getY(), juce::Colour (0xff9a958c), cap.getRight(),
                                                 cap.getBottom(), false));
        g.fillEllipse (cap);
        g.setColour (juce::Colour (0xff2a2622));
        g.setFont (cond (juce::jmin (13.0f, cap.getHeight() * 0.3f), Weight::extra, 0.04f));
        g.drawText (info.switchText != nullptr ? juce::String (info.switchText) : juce::String (lit ? "ON" : "OFF"), cap,
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (9, 9);
        plate = r.removeFromTop (40);
        r.removeFromTop (6);
        const int switchSize = juce::jlimit (34, 50, juce::jmin (r.getWidth() - 24, getHeight() / 5));
        footswitch = r.removeFromBottom (switchSize).withSizeKeepingCentre (switchSize, switchSize);
        r.removeFromBottom (8);
        valueBox = r.removeFromBottom (22);
        r.removeFromBottom (4);
        const int side = juce::jmin (r.getWidth() - 6, r.getHeight(), 84);
        knob.setBounds (r.withSizeKeepingCentre (side, side));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (footswitch.expanded (4).contains (e.getPosition()))
        {
            pressed = true;
            repaint();
        }
        else if (onSelect)
            onSelect();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (pressed)
        {
            pressed = false;
            if (footswitch.expanded (4).contains (e.getPosition()) && onStomp != nullptr)
                onStomp();
            repaint();
        }
    }

    const PedalInfo& info;
    juce::Colour colour;
    juce::Slider knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    std::function<bool()> on;
    std::function<void()> onStomp, onSelect, onTurn;
    std::function<juce::String()> valueText;
    bool picked = false;

private:
    juce::Rectangle<int> plate, valueBox, footswitch;
    bool pressed = false;
};

//==============================================================================
VocoderScreen::VocoderScreen (RigEngine& e, bool isStandalone)
    : engine (e), standalone (isStandalone)
{
    setOpaque (true);
    auto& params = engine.getVocoder().params;

    for (auto* b : { &boardButton, &studioButton })
    {
        setLit (*b, Lit::current);
        b->getProperties().set ("fontSize", 18.0f);
        addAndMakeVisible (*b);
    }
    boardButton.onClick = [this] { setStudio (false); };
    studioButton.onClick = [this] { setStudio (true); };
    boardButton.setTooltip ("The pedalboard: stomp the pedals, click one to open every setting it has.");
    studioButton.setTooltip ("Every setting the vocoder has, on one page: each part with its switch, every knob, the tuner, the chords and the keys.");

    for (auto* b : { &prevSound, &nextSound, &soundsButton, &surpriseButton })
    {
        b->getProperties().set ("fontSize", 17.0f);
        addAndMakeVisible (*b);
    }
    prevSound.setTooltip ("The sound before.");
    nextSound.setTooltip ("The next sound.");
    soundsButton.setTooltip ("Every sound, by kind, and the ones you saved. Save this sound from here too.");
    surpriseButton.setTooltip ("Picks a sound at random. Then open the pedals and make it yours.");
    prevSound.onClick = [this] { stepSound (-1); };
    nextSound.onClick = [this] { stepSound (1); };
    soundsButton.onClick = [this] { showSoundMenu(); };
    surpriseButton.onClick = [this]
    {
        const int n = (int) Presets::all().size();
        int pick = engine.getVocoder().getCurrentProgram();
        while (n > 2 && pick == engine.getVocoder().getCurrentProgram())
            pick = 1 + juce::Random::getSystemRandom().nextInt (n - 1);
        pickSound (pick);
    };

    bypassButton.getProperties().set ("fontSize", 17.0f);
    setLit (bypassButton, Lit::mute);
    bypassButton.setTooltip ("Switches every pedal off, to hear your voice dry. Press again to put them back.");
    bypassButton.onClick = [this] { pressBypassAll(); };
    addAndMakeVisible (bypassButton);

    notesButton.getProperties().set ("fontSize", 17.0f);
    notesButton.setTooltip ("Which keys play the robot's notes. Click to send the keys to the vocoder.");
    notesButton.onClick = [this] { pressNotesFrom(); };
    addAndMakeVisible (notesButton);

    // The pedals and their racks.
    const auto& all = pedalInfo();
    for (int i = 0; i < numPedals; ++i)
    {
        const auto& info = all[(size_t) i];
        auto* p = pedals.add (new Pedal (info));
        p->on = [this, i] { return isOn (i); };
        p->onStomp = [this, i] { stomp (i); };
        p->onSelect = [this, i] { selectPedal (i); };
        p->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (params, info.knob.id, p->knob);
        if (auto* param = params.getParameter (info.knob.id))
            p->knob.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
        auto* knobParam = params.getParameter (info.knob.id);
        p->knob.textFromValueFunction = [knobParam] (double v) { return readable (knobParam, v); };
        p->valueText = [p] { return p->knob.getTextFromValue (p->knob.getValue()); };
        addAndMakeVisible (p);

        for (const auto& s : info.rack)
        {
            auto knob = std::make_unique<Knob> (params, s.id, s.label, juce::Colour (info.colour));
            knob->inWell = true;
            knob->nameSize = 13.5f;
            auto* rackParam = params.getParameter (s.id);
            knob->slider.textFromValueFunction = [rackParam] (double v) { return readable (rackParam, v); };
            addChildComponent (*knob);
            rackKnobs[(size_t) i].push_back (std::move (knob));
        }
    }
    pedals[character]->valueText = [this] { return characterName (param (ParamIDs::formant)); };
    pedals[vintageMic]->valueText = [this] { auto t = pedals[vintageMic]->knob.getTextFromValue (pedals[vintageMic]->knob.getValue()); return t.upToFirstOccurrenceOf (" ", false, false); };
    // Turning a pedal's knob up is enough to hear it.
    pedals[harmony]->onTurn = [this]
    {
        if (param (ParamIDs::singToPlay) > 0.5f && param (ParamIDs::tuneOn) < 0.5f)
            setParam (ParamIDs::tuneOn, 1.0f);
    };
    for (int i : { (int) dirt, (int) chorus, (int) echo, (int) space })
        pedals[i]->onTurn = [this] { if (param (ParamIDs::fxOn) < 0.5f) setParam (ParamIDs::fxOn, 1.0f); };

    setLit (rackPower, Lit::play);
    rackPower.getProperties().set ("fontSize", 16.0f);
    rackReset.getProperties().set ("fontSize", 16.0f);
    setInWell (rackPower, true);
    setInWell (rackReset, true);
    rackPower.onClick = [this] { stomp (selected); };
    rackReset.onClick = [this] { resetPedal (selected); };
    rackReset.setTooltip ("This pedal's settings back to how they start.");
    addAndMakeVisible (rackPower);
    addAndMakeVisible (rackReset);

    for (int i = 0; i < 7; ++i)
    {
        auto& b = chordButtons[i];
        setInWell (b, true);
        setLit (b, Lit::play);
        b.getProperties().set ("fontSize", 20.0f);
        b.setTooltip ("Click to have the robot sing this chord. Click again to stop.");
        b.onClick = [this, i] { pressChord (i); };
        addChildComponent (b);
    }

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
    monitorButton.setTooltip ("Monitor: hear your own voice (as well as the pedals). Switches the mic on too if it's off. Use headphones, "
                              "so the speakers don't feed back.");
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
    dryWet.setTooltip ("Blend: your voice (left) to the robot (right). In the middle you hear both at full. It moves both faders.");
    dryWet.onValueChange = [this] { setDryWet (dryWet.getValue()); };
    addAndMakeVisible (dryWet);

    // Mix: Your voice and the Robot on faders, so how much of you against the effect is one move.
    {
        using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
        voiceAttachment = std::make_unique<Attachment> (params, ParamIDs::voiceLevel, voiceFader);
        robotAttachment = std::make_unique<Attachment> (params, ParamIDs::robotLevel, robotFader);
        levelAttachment = std::make_unique<Attachment> (params, ParamIDs::outputGain, levelFader);
        const std::pair<juce::Slider*, const char*> faders[] = {
            { &voiceFader, "Your voice: how much of your own voice you hear, under the pedals. Double-click for where it starts." },
            { &robotFader, "Robot: how loud the robot (the vocoder) is against your voice. Double-click for where it starts." },
            { &levelFader, "Out: the vocoder's level. Double-click for 0 dB." } };
        const juce::Colour colourOf[] = { juce::Colour (0xffd8c49a), colours::vocoder, colours::master };
        for (int i = 0; i < 3; ++i)
        {
            auto* f = faders[i].first;
            f->setTooltip (faders[i].second);
            f->setColour (juce::Slider::trackColourId, colourOf[i]);
            f->setMouseDragSensitivity (160);
            f->onValueChange = [this] { dryWet.setValue (getDryWet(), juce::dontSendNotification); repaint (outWell); };
            addAndMakeVisible (*f);
        }
        voiceFader.setDoubleClickReturnValue (true, 0.9);
        robotFader.setDoubleClickReturnValue (true, 1.0);
        levelFader.setDoubleClickReturnValue (true, 0.0);

        for (auto* b : { &voiceSwitch, &robotSwitch, &fxSwitch })
        {
            setInWell (*b, true);
            setLit (*b, Lit::play);
            b->getProperties().set ("fontSize", 15.0f);
            addAndMakeVisible (*b);
        }
        voiceSwitch.setTooltip ("Your voice on or off (the same as Monitor).");
        robotSwitch.setTooltip ("The robot on or off (the same as its pedal).");
        fxSwitch.setTooltip ("Effects on or off: Dirt, Chorus, Echo and Space all at once, keeping their settings.");
        voiceSwitch.onClick = [this] { pressMonitor(); };
        robotSwitch.onClick = [this] { pressRobot(); };
        fxSwitch.onClick = [this] { pressEffects(); };
    }

    settings = std::make_unique<VocoderSettings> (engine, *this);
    addChildComponent (*settings);
    robotWasOn = param (ParamIDs::robotOn) > 0.5f;
    soundsSeen = engine.getVocoder().getSoundChanges();

    inputHistory.assign (120, 0.0f);
    shownSound = engine.getVocoder().getProgramName (engine.getVocoder().getCurrentProgram());
    selectPedal (robot);
    refresh();
    startTimerHz (30);
}

VocoderScreen::~VocoderScreen()
{
    stopTimer();
}

void VocoderScreen::watchRobot()
{
    using namespace ParamIDs;
    auto& vocoder = engine.getVocoder();
    const bool robotNow = param (robotOn) > 0.5f;
    const bool picked = vocoder.getSoundChanges() != soundsSeen;
    soundsSeen = vocoder.getSoundChanges();
    // Sing to play and Drone give it notes already.
    if (robotNow && (! robotWasOn || picked) && param (singToPlay) < 0.5f && param (drone) < 0.5f && activeChord < 0)
    {
        bool held = false;
        for (int note = 0; note < 128 && ! held; ++note)
            held = vocoder.keyboardState.isNoteOnForChannels (0xffff, note);
        if (! held)
            pressChord (0);
    }
    robotWasOn = robotNow;
}

bool VocoderScreen::isEffectsOn() const
{
    return param (ParamIDs::fxOn) > 0.5f;
}

void VocoderScreen::pressEffects()
{
    using namespace ParamIDs;
    if (isEffectsOn())
        setParam (fxOn, 0.0f);
    else
    {
        setParam (fxOn, 1.0f);
        // On should always be heard: with every effect down, Space comes up.
        bool any = false;
        for (auto* id : { drive, crush, chorusMix, delayMix, reverbMix })
            any = any || param (id) > 0.001f;
        if (! any)
            setParam (reverbMix, keptSpace > 0.005f ? keptSpace : 0.3f);
    }
    bypassed = false;
    updateButtons();
    repaint();
}

juce::String VocoderScreen::pedalName (int i)
{
    return pedalInfo()[(size_t) juce::jlimit (0, numPedals - 1, i)].name;
}

int VocoderScreen::numRackKnobs (int pedal) const
{
    return (int) rackKnobs[(size_t) juce::jlimit (0, numPedals - 1, pedal)].size();
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
    using namespace ParamIDs;
    switch (pedal)
    {
        case tune: return param (tuneOn) > 0.5f;
        case harmony: return param (tuneOn) > 0.5f && juce::roundToInt (param (singToPlay)) != 0;
        case robot: return param (robotOn) > 0.5f;
        case character: return std::abs (param (formant)) >= 0.5f;
        case synth: return param (drone) > 0.5f;
        case chords: return activeChord >= 0;
        case arp: return param (arpOn) > 0.5f && juce::roundToInt (param (arpMode)) != 0;
        case filter:
        {
            const int mode = juce::roundToInt (param (filterMode));
            const bool wideOpen = mode == 1 && param (cutoff) >= 19900.0f && param (lfoDepth) < 0.01f && param (filterDrive) < 0.01f
                               && std::abs (param (voiceFollow)) < 0.01f;
            return mode != 0 && ! wideOpen;
        }
        case vintageMic: return param (vintageOn) > 0.5f;
        case dirt: return param (drive) > 0.005f || param (crush) > 0.005f;
        case chorus: return param (chorusMix) > 0.005f;
        case echo: return param (delayMix) > 0.005f;
        case space: return param (reverbMix) > 0.005f;
        default: return false;
    }
}

void VocoderScreen::stomp (int pedal)
{
    using namespace ParamIDs;
    const bool wasOn = isOn (pedal);
    auto mix = [&] (const char* id, float& kept)
    {
        if (wasOn)
        {
            kept = param (id);
            setParam (id, 0.0f);
        }
        else
        {
            setParam (fxOn, 1.0f);
            setParam (id, kept);
        }
    };
    switch (pedal)
    {
        case tune: setParam (tuneOn, wasOn ? 0.0f : 1.0f); break;
        case harmony:
            if (wasOn)
            {
                keptHarmony = (float) juce::roundToInt (param (singToPlay));
                setParam (singToPlay, 0.0f);
            }
            else
            {
                setParam (tuneOn, 1.0f);
                setParam (singToPlay, juce::jmax (1.0f, keptHarmony));
            }
            break;
        case robot: setParam (robotOn, wasOn ? 0.0f : 1.0f); break;
        case character:
            if (wasOn)
            {
                keptFormant = param (formant);
                setParam (formant, 0.0f);
            }
            else
                setParam (formant, keptFormant);
            break;
        case synth:
            setParam (drone, wasOn ? 0.0f : 1.0f);
            if (! wasOn)
                setParam (robotOn, 1.0f);   // a drone is the robot singing one note
            break;
        case chords: pressChord (wasOn ? activeChord : 0); return;
        case arp:
            if (wasOn)
                setParam (arpOn, 0.0f);
            else
            {
                // A note arp needs the robot's chord; without the robot, it chops your voice in time.
                if (juce::roundToInt (param (arpMode)) == 0)
                    setParam (arpMode, param (robotOn) > 0.5f ? 1.0f : 2.0f);
                setParam (arpOn, 1.0f);
            }
            break;
        case filter:
            if (wasOn)
            {
                keptFilterMode = (float) juce::roundToInt (param (filterMode));
                keptCutoff = param (cutoff);
                setParam (filterMode, 0.0f);
            }
            else
            {
                setParam (filterMode, juce::jmax (1.0f, keptFilterMode));
                if (param (cutoff) >= 19900.0f || keptCutoff < 19900.0f)
                    setParam (cutoff, keptCutoff < 19900.0f ? keptCutoff : 1500.0f);
            }
            break;
        case vintageMic: setParam (vintageOn, wasOn ? 0.0f : 1.0f); break;
        case dirt:
            if (wasOn)
            {
                keptDrive = param (drive);
                keptCrush = param (crush);
                setParam (drive, 0.0f);
                setParam (crush, 0.0f);
            }
            else
            {
                setParam (fxOn, 1.0f);
                setParam (drive, keptDrive > 0.005f || keptCrush > 0.005f ? keptDrive : 0.35f);
                setParam (crush, keptCrush);
            }
            break;
        case chorus: mix (chorusMix, keptChorus); break;
        case echo: mix (delayMix, keptEcho); break;
        case space: mix (reverbMix, keptSpace); break;
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

void VocoderScreen::selectPedal (int pedal)
{
    selected = juce::jlimit (0, numPedals - 1, pedal);
    for (int i = 0; i < numPedals; ++i)
    {
        pedals[i]->picked = i == selected;
        pedals[i]->repaint();
        for (auto& k : rackKnobs[(size_t) i])
            k->setVisible (! studioShown && i == selected);
    }
    for (auto& b : chordButtons)
        b.setVisible (! studioShown && selected == chords);
    layoutRack();
    updateButtons();
    repaint (rackWell);
}

void VocoderScreen::resetPedal (int pedal)
{
    auto& params = engine.getVocoder().params;
    const bool wasOn = isOn (pedal);
    for (const auto& s : pedalInfo()[(size_t) juce::jlimit (0, numPedals - 1, pedal)].rack)
        if (auto* p = params.getParameter (s.id))
            p->setValueNotifyingHost (p->getDefaultValue());
    // Reset keeps the pedal on or off as it was (a mix at 0 would switch it off).
    if (wasOn && ! isOn (pedal))
        stomp (pedal);
    if (onSay)
        onSay (pedalName (pedal) + ": back to how it starts.");
    refresh();
}

juce::Array<int> VocoderScreen::chordNotes (int degree) const
{
    using namespace ParamIDs;
    juce::Array<int> notes;
    for (int n : vox::Chords::build (juce::roundToInt (param (chordKey)), juce::roundToInt (param (chordScale)), degree,
                                     juce::roundToInt (param (chordFlavour))))
        notes.add (n);
    return notes;
}

void VocoderScreen::pressChord (int degree)
{
    auto& vocoder = engine.getVocoder();
    if (degree == activeChord || degree < 0)
    {
        activeChord = -1;
        vocoder.requestChord ({}, true);
    }
    else
    {
        activeChord = juce::jlimit (0, 6, degree);
        // Chords are sung by the robot, so it comes on to sing them.
        if (param (ParamIDs::robotOn) < 0.5f)
            setParam (ParamIDs::robotOn, 1.0f);
        vocoder.requestChord (chordNotes (activeChord));
    }
    updateButtons();
    repaint();
}

//==============================================================================
void VocoderScreen::pickSound (int index)
{
    auto& vocoder = engine.getVocoder();
    index = juce::jlimit (0, vocoder.getNumPrograms() - 1, index);
    vocoder.setCurrentProgram (index);
    shownSound = vocoder.getProgramName (index);
    bypassed = false;
    if (onSay)
        onSay (juce::String (Presets::all()[(size_t) index].category) + ": " + shownSound
               + (index == 0 ? ". Every pedal off, just your voice." : ". Click a lit pedal to see how it's made."));
    refresh();
}

void VocoderScreen::stepSound (int delta)
{
    const int n = engine.getVocoder().getNumPrograms();
    pickSound ((engine.getVocoder().getCurrentProgram() + delta + n) % n);
}

juce::File VocoderScreen::userSoundsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Vocoder Sounds");
}

bool VocoderScreen::saveSound (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty())
        return false;
    auto folder = userSoundsFolder();
    folder.createDirectory();
    auto state = engine.getVocoder().params.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr || ! xml->writeTo (folder.getChildFile (clean + ".xml")))
        return false;
    shownSound = clean;
    if (onSay)
        onSay ("Saved \"" + clean + "\". It's in Sounds, under My sounds.");
    repaint (modeRow);
    return true;
}

bool VocoderScreen::loadSound (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    auto& params = engine.getVocoder().params;
    if (xml == nullptr || ! xml->hasTagName (params.state.getType()))
        return false;
    params.replaceState (juce::ValueTree::fromXml (*xml));
    shownSound = file.getFileNameWithoutExtension();
    bypassed = false;
    if (onSay)
        onSay ("Your sound \"" + shownSound + "\".");
    refresh();
    return true;
}

void VocoderScreen::showSoundMenu()
{
    juce::PopupMenu menu;
    const auto& presets = Presets::all();
    juce::StringArray categories;
    for (const auto& p : presets)
        categories.addIfNotAlreadyThere (p.category);
    const int current = engine.getVocoder().getCurrentProgram();
    for (const auto& category : categories)
    {
        juce::PopupMenu sub;
        for (int i = 0; i < (int) presets.size(); ++i)
            if (category == presets[(size_t) i].category)
                sub.addItem (1 + i, presets[(size_t) i].name, true, i == current && shownSound == presets[(size_t) i].name);
        menu.addSubMenu (category, sub);
    }
    menu.addSeparator();
    auto files = userSoundsFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();
    juce::PopupMenu mine;
    for (int i = 0; i < files.size(); ++i)
        mine.addItem (1000 + i, files[i].getFileNameWithoutExtension(), true, shownSound == files[i].getFileNameWithoutExtension());
    menu.addSubMenu ("My sounds", mine, ! files.isEmpty());
    menu.addItem (999, "Save this sound...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundsButton),
                        [safe = juce::Component::SafePointer<VocoderScreen> (this), files] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 999)
                                safe->askToSave();
                            else if (result >= 1000)
                                safe->loadSound (files[result - 1000]);
                            else
                                safe->pickSound (result - 1);
                        });
}

void VocoderScreen::askToSave()
{
    auto* window = new juce::AlertWindow ("Save this sound", "Give it a name. It goes in Sounds, under My sounds.", juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", shownSound.endsWith ("(mine)") ? shownSound : shownSound + " (mine)");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<VocoderScreen> (this), window] (int result)
                                                                        {
                                                                            if (safe != nullptr && result == 1)
                                                                                safe->saveSound (window->getTextEditorContents ("name"));
                                                                        }),
                             true);
}

//==============================================================================
void VocoderScreen::pressBypassAll()
{
    if (! bypassed)
    {
        bool any = false;
        for (int i = 0; i < numPedals; ++i)
            beforeBypass[(size_t) i] = isOn (i);
        // Harmony before Tune (Harmony sits on Tune), and the rest in order.
        for (int i : { (int) harmony, (int) tune, (int) robot, (int) character, (int) synth, (int) chords, (int) arp, (int) filter,
                       (int) vintageMic, (int) dirt, (int) chorus, (int) echo, (int) space })
            if (beforeBypass[(size_t) i] && isOn (i))
            {
                stomp (i);
                any = true;
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

bool VocoderScreen::isMonitoring() const
{
    // You only hear yourself with the mic live too: with it off the vocoder gets silence,
    // even though the wave above still shows what's coming in.
    return param (ParamIDs::voiceOn) > 0.5f && (! standalone || engine.micOn.load());
}

void VocoderScreen::pressMonitor()
{
    if (isMonitoring())
    {
        setParam (ParamIDs::voiceOn, 0.0f);
    }
    else
    {
        // Monitor on means you hear your voice, whatever else was in the way.
        if (standalone && ! engine.micOn.load())
        {
            if (onMic)
                onMic();   // also checks a mic is picked, and says so if not
            else
                engine.micOn = true;
        }
        setParam (ParamIDs::voiceOn, 1.0f);
        engine.vocoderOn = true;
        engine.getStrip (RigEngine::vocoderStrip).mute = false;
    }
    updateButtons();
    repaint (rawWell);
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
    studioShown = show;
    // The sound row stays along the top; the page takes the place of the pedalboard.
    const juce::Component* topRow[] = { &boardButton, &studioButton, &prevSound, &nextSound, &soundsButton, &surpriseButton, &notesButton, &bypassButton };
    for (auto* c : getChildren())
        if (c != settings.get() && std::find (std::begin (topRow), std::end (topRow), c) == std::end (topRow))
            c->setVisible (! studioShown);
    settings->setVisible (studioShown);
    if (studioShown)
        settings->refresh();
    selectPedal (selected);   // the rack's knobs and chords
    updateButtons();
    resized();
    repaint();
}

//==============================================================================
void VocoderScreen::refresh()
{
    dryWet.setValue (getDryWet(), juce::dontSendNotification);
    for (auto* p : pedals)
        p->repaint();
    updateButtons();
    repaint();
}

void VocoderScreen::updateButtons()
{
    boardButton.setToggleState (! studioShown, juce::dontSendNotification);
    studioButton.setToggleState (studioShown, juce::dontSendNotification);
    bypassButton.setToggleState (bypassed, juce::dontSendNotification);
    monitorButton.setToggleState (isMonitoring(), juce::dontSendNotification);
    monitorButton.setButtonText (isMonitoring() ? "Monitor on" : "Monitor off");
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

    auto onOff = [] (juce::TextButton& b, bool on, const juce::String& name)
    {
        b.setToggleState (on, juce::dontSendNotification);
        const auto text = (name.isEmpty() ? juce::String() : name + " ") + (on ? "on" : "off");
        if (b.getButtonText() != text)
            b.setButtonText (text);
    };
    onOff (voiceSwitch, isMonitoring(), {});   // the fader's name is above it
    onOff (robotSwitch, isOn (robot), {});
    onOff (fxSwitch, isEffectsOn(), "FX");
    voiceFader.setAlpha (isMonitoring() || engine.getVocoder().isVoiceStandingIn() ? 1.0f : 0.45f);
    robotFader.setAlpha (isOn (robot) ? 1.0f : 0.45f);
    toLooperButton.setButtonText (looping ? "Loop it" : "To looper");

    const bool on = isOn (selected);
    rackPower.setToggleState (on, juce::dontSendNotification);
    const juce::String powerText = selected == synth ? (on ? "Drone on" : "Drone off") : selected == chords ? (on ? "Stop" : "Play") : (on ? "On" : "Off");
    if (rackPower.getButtonText() != powerText)
        rackPower.setButtonText (powerText);

    using namespace ParamIDs;
    const int key = juce::roundToInt (param (chordKey)), scale = juce::roundToInt (param (chordScale)), type = juce::roundToInt (param (chordFlavour));
    for (int i = 0; i < 7; ++i)
    {
        const juce::String name (vox::Chords::name (key, scale, i, type));
        if (chordButtons[i].getButtonText() != name)
            chordButtons[i].setButtonText (name);
        chordButtons[i].setToggleState (i == activeChord, juce::dontSendNotification);
    }

    const int keys = engine.getRouting().keysTo.load();
    juce::String text;
    if (keys == (int) RigRouting::Dest::vocoder)
        text = standalone ? "Keys play Vocoder" : "Logic MIDI plays it";
    else
        text = "Keys play " + RigRouting::destName (keys);
    if (notesButton.getButtonText() != text)
        notesButton.setButtonText (text);
    notesButton.setToggleState (keys == (int) RigRouting::Dest::vocoder, juce::dontSendNotification);
    setLit (notesButton, keys == (int) RigRouting::Dest::vocoder ? Lit::current : Lit::none);
}

void VocoderScreen::timerCallback()
{
    const float in = micLevel != nullptr ? micLevel() : 0.0f;
    const float out = meterOf != nullptr ? meterOf (RigEngine::vocoderStrip) : 0.0f;
    inputLevel = in;
    outLevel = out;
    std::rotate (inputHistory.begin(), inputHistory.begin() + 1, inputHistory.end());
    inputHistory.back() = in;

    // A chord that's playing follows the key, scale and type at once.
    using namespace ParamIDs;
    const juce::String chordSig = juce::String (param (chordKey)) + "/" + juce::String (param (chordScale)) + "/" + juce::String (param (chordFlavour));
    if (chordSig != chordSignature)
    {
        if (activeChord >= 0 && chordSignature.isNotEmpty())
            engine.getVocoder().requestChord (chordNotes (activeChord));
        chordSignature = chordSig;
    }

    watchRobot();

    if (! isShowing())
        return;

    juce::String sig;
    for (int i = 0; i < numPedals; ++i)
        sig << (int) isOn (i);
    for (auto* p : pedals)
        sig << "/" << p->knob.getValue();
    sig << "/" << param (robotLevel) << "/" << param (voiceLevel) << "/" << (int) engine.micOn.load() << "/" << (int) engine.vocoderOn.load()
        << "/" << engine.getVocoder().getCurrentProgram() << "/" << param (fxOn) << "/" << param (outputGain) << "/" << activeChord;
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

    // The sound's name.
    {
        const auto box = soundLcd.toFloat();
        drawLcd (g, box, 7.0f);
        const int program = engine.getVocoder().getCurrentProgram();
        const auto& preset = Presets::all()[(size_t) juce::jlimit (0, (int) Presets::all().size() - 1, program)];
        const bool factory = shownSound == preset.name;
        auto text = box.reduced (12, 2);
        lcdText (g, factory ? juce::String (preset.category).toUpperCase() : juce::String ("MY SOUND"), text.removeFromLeft (text.getWidth() * 0.36f), 18.0f,
                 juce::Justification::centredLeft, colours::lcdGreen);
        lcdText (g, shownSound, text, 26.0f, juce::Justification::centredLeft);
    }

    if (studioShown)
        return;

    // The patch cable along the board.
    if (! pedals.isEmpty())
    {
        const float y = (float) pedals[0]->getY() + 26.0f;
        g.setColour (juce::Colour (0xff1e1a16));
        g.fillRoundedRectangle ((float) boardArea.getX() - 14.0f, y - 3.0f, (float) boardArea.getWidth() + 28.0f, 6.0f, 3.0f);
        engraved (g, "Voice in", { boardArea.getX(), pedals[0]->getY() - 20, 120, 16 }, juce::Justification::centredLeft, 12.0f);
        engraved (g, juce::String::fromUTF8 ("Out \xe2\x86\x92"), { boardArea.getRight() - 120, pedals[0]->getY() - 20, 120, 16 },
                  juce::Justification::centredRight, 12.0f);
        engraved (g, juce::String::fromUTF8 ("Stomp to switch \xc2\xb7 click a pedal to open all its settings"),
                  { boardArea.getX() + 130, pedals[0]->getY() - 20, boardArea.getWidth() - 260, 16 }, juce::Justification::centred, 12.0f);
    }

    // Raw voice.
    drawWell (g, rawWell.toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (cond (21.0f, Weight::extra, 0.08f));
    g.drawText ("RAW VOICE", rawTitle, juce::Justification::centredLeft);
    const bool micLive = standalone ? engine.micOn.load() : true;
    drawLamp (g, micLamp.getCentre().toFloat(), micLive ? Lit::arm : Lit::none, 14.0f);
    engraved (g, juce::String::fromUTF8 (standalone ? (micLive ? "Mic in \xc2\xb7 live" : "Mic off \xc2\xb7 click the lamp") : "Side chain from Logic"), rawSub,
              juce::Justification::centredLeft, 12.0f, true);
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
        if (! micLive)
        {
            // The wave still shows what's coming in, but none of it is heard yet.
            g.setColour (juce::Colour (0xff0e0c0a).withAlpha (0.72f));
            g.fillRoundedRectangle (s, 8.0f);
            g.setColour (colours::arm);
            g.setFont (cond (17.0f, Weight::extra, 0.06f));
            g.drawFittedText (juce::String::fromUTF8 ("MIC OFF \xc2\xb7 NOT HEARD"), s.reduced (8).toNearestInt().removeFromTop ((int) s.getHeight() / 2 + 4),
                              juce::Justification::centredBottom, 1);
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.setFont (barlow (13.0f));
            g.drawFittedText ("Press Monitor or click the lamp", s.reduced (8).toNearestInt().removeFromBottom ((int) s.getHeight() / 2 - 4),
                              juce::Justification::centredTop, 1);
        }
    }
    drawLeds (g, inputLeds.toFloat(), std::sqrt (inputLevel), 18, false);

    // The rack: the picked pedal, every setting.
    drawWell (g, rackWell.toFloat(), 12.0f);
    {
        const auto& info = pedalInfo()[(size_t) selected];
        auto t = rackTitle;
        g.setColour (juce::Colour (info.colour));
        g.fillRoundedRectangle (t.removeFromLeft (8).toFloat().reduced (0, 4), 3.0f);
        t.removeFromLeft (10);
        g.setColour (p.wellInk);
        g.setFont (cond (24.0f, Weight::extra, 0.08f));
        const auto title = juce::String (info.name).toUpperCase();
        const int titleWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 14;
        g.drawText (title, t.removeFromLeft (titleWidth), juce::Justification::centredLeft);
        g.setColour (p.wellInk2);
        g.setFont (cond (15.0f, Weight::semi, 0.06f));
        g.drawText (juce::String (info.kind).toUpperCase(), t, juce::Justification::centredLeft);
        g.setFont (barlow (13.5f));
        g.drawFittedText (info.description, rackText, juce::Justification::topLeft, 2, 0.9f);
    }

    // Mix.
    drawWell (g, outWell.toFloat(), 12.0f);
    g.setColour (p.wellInk);
    g.setFont (cond (21.0f, Weight::extra, 0.08f));
    g.drawText ("MIX", outTitle, juce::Justification::centredLeft);
    engraved (g, juce::String::fromUTF8 ("you \xc2\xb7 robot \xc2\xb7 out"), outTitle.withTrimmedLeft (52), juce::Justification::centredLeft, 12.0f, true);
    drawLeds (g, outLeds.toFloat(), std::sqrt (outLevel), 14, true);
    engraved (g, "Blend", dryWetLabel, juce::Justification::centred, 12.0f, true);
    {
        const char* names[] = { "Voice", "Robot", "Out" };
        juce::Slider* faders[] = { &voiceFader, &robotFader, &levelFader };
        auto& params = engine.getVocoder().params;
        const char* ids[] = { ParamIDs::voiceLevel, ParamIDs::robotLevel, ParamIDs::outputGain };
        for (int i = 0; i < 3; ++i)
        {
            engraved (g, names[i], faderNames[(size_t) i], juce::Justification::centred, 13.0f, true);
            const auto box = faderValues[(size_t) i].toFloat();
            drawLcd (g, box, 4.0f);
            auto value = readable (params.getParameter (ids[i]), faders[i]->getValue());
            if (i == 2 && faders[i]->getValue() > 0.05)
                value = "+" + value;
            lcdText (g, value, box, juce::jmin (17.0f, box.getHeight() * 0.9f));
        }
    }
}

void VocoderScreen::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (22).withTrimmedRight (22).withTrimmedTop (6).withTrimmedBottom (14);
    modeRow = r.removeFromTop (44);
    {
        auto m = modeRow;
        // Beside the track rail the row is tighter: the buttons give the sound's name more room.
        const bool tight = m.getWidth() < 1100;
        boardButton.setBounds (m.removeFromLeft (tight ? 120 : 130));
        studioButton.setBounds (m.removeFromLeft (tight ? 120 : 130));
        m.removeFromLeft (tight ? 10 : 18);
        bypassButton.setBounds (m.removeFromRight (tight ? 110 : 124));
        m.removeFromRight (8);
        notesButton.setBounds (m.removeFromRight (tight ? 156 : 180));
        m.removeFromRight (tight ? 10 : 18);
        surpriseButton.setBounds (m.removeFromRight (tight ? 106 : 120));
        m.removeFromRight (6);
        soundsButton.setBounds (m.removeFromRight (96));
        m.removeFromRight (6);
        nextSound.setBounds (m.removeFromRight (44));
        prevSound.setBounds (m.removeFromLeft (44));
        m.removeFromLeft (6);
        m.removeFromRight (6);
        soundLcd = m;
    }
    r.removeFromTop (12);

    settings->setBounds (r);

    // The pedalboard.
    r.removeFromTop (22);   // its caption
    const int boardHeight = juce::jlimit (170, 236, r.getHeight() * 2 / 5);
    boardArea = r.removeFromTop (boardHeight);
    {
        const int gap = 7;
        const int w = (boardArea.getWidth() - gap * (numPedals - 1)) / numPedals;
        auto row = boardArea;
        for (auto* pedal : pedals)
        {
            pedal->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    }
    r.removeFromTop (14);

    // Raw voice | the rack | Out.
    rawWell = r.removeFromLeft (juce::jlimit (200, 250, r.getWidth() / 6));
    r.removeFromLeft (12);
    outWell = r.removeFromRight (juce::jlimit (320, 360, r.getWidth() / 4));
    r.removeFromRight (12);
    rackWell = r;
    {
        auto w = rawWell.reduced (14, 12);
        auto title = w.removeFromTop (24);
        micLamp = title.removeFromRight (18);
        rawTitle = title;
        rawSub = w.removeFromTop (16);
        w.removeFromTop (6);
        auto bottom = w.removeFromBottom (juce::jmin (110, w.getHeight() / 2));
        micGain->setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 4));
        bottom.removeFromLeft (8);
        const int bh = juce::jmin (40, (bottom.getHeight() - 8) / 2);
        monitorButton.setBounds (bottom.removeFromTop (bh));
        bottom.removeFromTop (8);
        recVoiceButton.setBounds (bottom.removeFromTop (bh));
        w.removeFromBottom (8);
        inputLeds = w.removeFromBottom (10);
        w.removeFromBottom (8);
        scopeArea = w;
    }
    {
        // Voice | Robot | Out faders, each with its value and switch; the meter; Blend and To looper.
        auto o = outWell.reduced (12, 10);
        outTitle = o.removeFromTop (24);
        o.removeFromTop (4);
        auto side = o.removeFromRight (juce::jmin (92, o.getWidth() / 4));
        o.removeFromRight (4);
        outLeds = o.removeFromRight (14).withTrimmedTop (18).withTrimmedBottom (34);
        o.removeFromRight (4);
        const int cw = o.getWidth() / 3;
        juce::Slider* faders[] = { &voiceFader, &robotFader, &levelFader };
        juce::TextButton* switches[] = { &voiceSwitch, &robotSwitch };
        for (int i = 0; i < 3; ++i)
        {
            auto c = (i == 2 ? o : o.removeFromLeft (cw)).reduced (2, 0);
            faderNames[(size_t) i] = c.removeFromTop (16);
            if (i < 2)
                switches[i]->setBounds (c.removeFromBottom (30));
            else
                c.removeFromBottom (30);
            c.removeFromBottom (4);
            faderValues[(size_t) i] = c.removeFromBottom (20);
            c.removeFromBottom (2);
            faders[i]->setBounds (c.withSizeKeepingCentre (juce::jmin (c.getWidth(), 48), c.getHeight()));
        }
        toLooperButton.setBounds (side.removeFromBottom (30));
        side.removeFromBottom (6);
        fxSwitch.setBounds (side.removeFromBottom (30));
        side.removeFromBottom (6);
        dryWetLabel = side.removeFromBottom (16);
        dryWet.setBounds (side.withSizeKeepingCentre (juce::jmin (side.getWidth(), 66), juce::jmin (side.getHeight(), 66)));
    }
    layoutRack();
}

void VocoderScreen::layoutRack()
{
    if (rackWell.isEmpty())
        return;
    auto w = rackWell.reduced (16, 12);
    auto head = w.removeFromTop (34);
    rackReset.setBounds (head.removeFromRight (84).reduced (0, 2));
    head.removeFromRight (8);
    rackPower.setBounds (head.removeFromRight (110).reduced (0, 2));
    head.removeFromRight (12);
    rackTitle = head;
    rackText = w.removeFromTop (36);
    w.removeFromTop (6);

    if (selected == chords)
    {
        auto buttons = w.removeFromBottom (juce::jmin (54, w.getHeight() / 2));
        const int bw = (buttons.getWidth() - 6 * 8) / 7;
        for (auto& b : chordButtons)
        {
            b.setBounds (buttons.removeFromLeft (bw));
            buttons.removeFromLeft (8);
        }
        w.removeFromBottom (8);
    }
    rackKnobArea = w;

    auto& knobs = rackKnobs[(size_t) selected];
    const int n = (int) knobs.size();
    if (n == 0)
        return;
    // One row while they fit at a good size, then two.
    const int perRow = n <= 9 && w.getWidth() / n >= 96 ? n : (n + 1) / 2;
    const int rows = (n + perRow - 1) / perRow;
    const int kw = juce::jmin (130, w.getWidth() / perRow);
    const int kh = juce::jmin (150, w.getHeight() / rows);
    for (int i = 0; i < n; ++i)
    {
        const int row = i / perRow, col = i % perRow;
        const int inRow = juce::jmin (perRow, n - row * perRow);
        const int x0 = w.getX() + (w.getWidth() - inRow * kw) / 2;
        knobs[(size_t) i]->setBounds (juce::Rectangle<int> (x0 + col * kw, w.getY() + row * kh, kw, kh).reduced (4, 2));
    }
}

void VocoderScreen::mouseUp (const juce::MouseEvent& e)
{
    if (! studioShown && standalone && (micLamp.expanded (8).contains (e.getPosition()) || rawSub.contains (e.getPosition())
                                        || (! engine.micOn.load() && scopeArea.contains (e.getPosition()))) && onMic)
    {
        onMic();
        repaint (rawWell);
    }
}
