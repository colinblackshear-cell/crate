#include "VocoderSettings.h"

#include "VocoderScreen.h"
#include "Parameters.h"   // the vocoder's
#include "dsp/Chords.h"

using namespace vintage;

namespace
{
constexpr int minCell = 80;     // the narrowest a knob gets before a part wraps onto a second row
constexpr int knobHeight = 90;
constexpr int headerHeight = 32;
constexpr int gap = 10;

struct Setting
{
    const char* id;
    const char* label;
    juce::uint32 colour = 0;   // 0: the part's own
};

struct SectionInfo
{
    const char* name;
    const char* kind;
    juce::uint32 colour;
    std::vector<Setting> knobs;
    float extraUnits;   // room for the tuner, the chord buttons or Blend, in knobs
    int pedal;          // the pedal its switch stomps, or -1
};

const std::vector<SectionInfo>& sectionInfo()
{
    using namespace ParamIDs;
    static const std::vector<SectionInfo> info {
        { "Your voice", "the mic", 0xffd8c49a,
          { { inputGain, "Mic gain" }, { gateThreshold, "Gate" }, { voiceLevel, "Level" }, { fillGaps, "Fill gaps" } }, 0.0f, -1 },
        { "Tune", "auto-tune + harmony", 0xff4fb39a,
          { { tuneKey, "Key" }, { tuneScale, "Scale" }, { tuneSpeed, "Speed" }, { tuneAmount, "Amount" }, { singToPlay, "Harmony", 0xff8fc06a } },
          1.6f, VocoderScreen::tune },
        { "Robot", "vocoder + character", 0xff5fb3c4,
          { { bands, "Bands" }, { sharpness, "Beep" }, { attack, "Attack" }, { release, "Release" }, { sibilance, "S and T" },
            { formant, "Character", 0xff8a6fb3 }, { robotLevel, "Level" } },
          0.0f, VocoderScreen::robot },
        { "Synth", "the robot's synth", 0xffe0785a,
          { { waveform, "Wave" }, { octave, "Octave" }, { detune, "Detune" }, { pulseWidth, "Pulse" }, { pwmDepth, "Pulse move" },
            { osc2Wave, "Osc 2 wave" }, { osc2Semitones, "Osc 2 tune" }, { osc2Fine, "Osc 2 fine" }, { osc2Level, "Osc 2 vol" },
            { subLevel, "Sub" }, { noise, "Noise" }, { glide, "Glide" }, { vibratoRate, "Vib speed" }, { vibratoDepth, "Vibrato" },
            { carrierAttack, "Attack" }, { carrierRelease, "Release" }, { droneNote, "Drone note" } },
          0.0f, VocoderScreen::synth },
        { "Chords", "no keys needed", 0xffefcb45,
          { { chordKey, "Key" }, { chordScale, "Scale" }, { chordFlavour, "Type" }, { chordTiming, "Changes on" } }, 4.0f, VocoderScreen::chords },
        { "Rhythm", "arp + gate", 0xffd9534a,
          { { arpMode, "Mode" }, { arpRate, "Speed" }, { arpPattern, "Pattern" }, { arpOctaves, "Octaves" }, { arpGate, "Length" },
            { arpSwing, "Swing" }, { gatePattern, "Rhythm" } },
          0.0f, VocoderScreen::arp },
        { "Filter", "dark / thin", 0xff4f8fd0,
          { { filterMode, "Type" }, { cutoff, "Cutoff" }, { resonance, "Resonance" }, { filterDrive, "Drive" }, { voiceFollow, "Voice follow" },
            { lfoShape, "LFO shape" }, { lfoSync, "LFO speed" }, { lfoRate, "LFO rate" }, { lfoDepth, "LFO amount" } },
          0.0f, VocoderScreen::filter },
        { "Vintage", "old mics", 0xffc9a472, { { vintageEra, "Era" }, { vintageWear, "Wear" }, { vintageAmount, "Amount" } }, 0.0f,
          VocoderScreen::vintageMic },
        { "Effects", "dirt, chorus, echo, space", 0xff9b8ccf,
          { { drive, "Drive", 0xffb0563c }, { crush, "Crush", 0xffb0563c }, { chorusMix, "Chorus", 0xff63c2b7 }, { chorusRate, "Rate", 0xff63c2b7 },
            { chorusDepth, "Depth", 0xff63c2b7 }, { delayMix, "Echo", 0xff7d9be0 }, { delayTime, "Time", 0xff7d9be0 },
            { delayFeedback, "Repeats", 0xff7d9be0 }, { delayPingPong, "Ping-pong", 0xff7d9be0 }, { reverbMix, "Space" },
            { reverbSize, "Size" }, { reverbDamping, "Darkness" } },
          0.0f, -1 },
        { "Out", "level + blend", 0xfff1e7d1, { { outputGain, "Level" } }, 1.0f, -1 },
    };
    return info;
}

juce::String noteName (int note) { return juce::MidiMessage::getMidiNoteName (note, true, true, 3); }
}

//==============================================================================
// The tuner: the note you're singing and how far off it is.
class Tuner : public juce::Component,
              public juce::SettableTooltipClient
{
public:
    explicit Tuner (VocoderProcessor& p) : processor (p) { setTooltip ("The note you're singing, and how far off it is (Tune pulls it onto the line)."); }

    void update()
    {
        const float newPitch = processor.getSungPitch();
        const int newTarget = processor.getTargetNote();
        if (newPitch >= 0.0f && newTarget >= 0)
            shown += 0.4f * ((newPitch - (float) newTarget) - shown);
        if (std::abs (newPitch - pitch) > 0.01f || newTarget != target)
        {
            pitch = newPitch;
            target = newTarget;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        engraved (g, "Tuner", r.removeFromTop (15.0f).toNearestInt(), juce::Justification::centred, 13.0f, true);
        r.removeFromTop (2.0f);
        drawLcd (g, r, 6.0f);
        const bool singing = pitch >= 0.0f;
        const int note = target >= 0 ? target : juce::roundToInt (pitch);
        auto text = r.reduced (6.0f, 4.0f);
        lcdText (g, singing ? noteName (note) : juce::String ("--"), text.removeFromTop (text.getHeight() * 0.55f), 26.0f);
        // The needle: off to the left is flat, right is sharp.
        const auto scale = text.reduced (4.0f, 4.0f);
        g.setColour (colours::lcdAmber.withAlpha (0.35f));
        g.fillRect (scale.getCentreX() - 0.5f, scale.getY(), 1.0f, scale.getHeight());
        if (singing)
        {
            const float off = juce::jlimit (-0.5f, 0.5f, shown);
            const float x = scale.getCentreX() + off * scale.getWidth();
            g.setColour (std::abs (off) < 0.1f ? colours::lcdGreen : colours::lcdAmber);
            g.fillRoundedRectangle (x - 3.0f, scale.getY(), 6.0f, scale.getHeight(), 2.0f);
        }
    }

private:
    VocoderProcessor& processor;
    float pitch = -1.0f, shown = 0.0f;
    int target = -1;
};

//==============================================================================
// One part of the chain in its well: the title, its switch and Reset, every setting as a knob.
class VocoderSettings::Section : public juce::Component
{
public:
    Section (VocoderSettings& o, int i, juce::AudioProcessorValueTreeState& params) : owner (o), index (i), info (sectionInfo()[(size_t) i])
    {
        for (const auto& s : info.knobs)
        {
            auto knob = std::make_unique<Knob> (params, s.id, s.label, juce::Colour (s.colour != 0 ? s.colour : info.colour));
            knob->inWell = true;
            knob->nameSize = 13.0f;
            auto* param = params.getParameter (s.id);
            knob->slider.textFromValueFunction = [param] (double v) { return readable (param, v); };
            addAndMakeVisible (*knob);
            knobs.push_back (std::move (knob));
        }
        for (auto* b : { &power, &reset })
        {
            setInWell (*b, true);
            b->getProperties().set ("fontSize", 15.0f);
        }
        setLit (power, Lit::play);
        reset.setTooltip ("This part's settings back to how they start.");
        power.onClick = [this] { owner.pressSwitch (index); };
        reset.onClick = [this] { owner.resetSection (index); };
        if (owner.hasSwitch (index))
            addAndMakeVisible (power);
        addAndMakeVisible (reset);
    }

    // How wide it is, in knobs, with its knobs in this many rows.
    // Never so narrow that the title and its buttons don't fit.
    float units (int rows = 1) const { return juce::jmax (3.6f, (float) (((int) knobs.size() + rows - 1) / rows) + info.extraUnits); }
    float currentUnits() const { return units (knobRows); }

    int perRow (int width) const
    {
        const int inner = width - 28 - (extraWidth (width) > 0 ? extraWidth (width) + 8 : 0);
        const int n = juce::jmax (1, (int) knobs.size());
        return juce::jlimit (1, n, juce::jmin ((n + knobRows - 1) / knobRows, juce::jmax (1, inner / minCell)));
    }

    // How tall it is at this width: the knobs wrap onto more rows when they don't fit.
    int heightFor (int width) const
    {
        const int per = perRow (width);
        const int rows = juce::jmax (1, ((int) knobs.size() + per - 1) / per);
        return headerHeight + 20 + rows * knobHeight + (rows - 1) * 4;
    }

    int extraWidth (int width) const
    {
        if (info.extraUnits <= 0.0f)
            return 0;
        const float share = info.extraUnits / currentUnits();
        return juce::jlimit (juce::roundToInt (info.extraUnits * 70), juce::roundToInt (info.extraUnits * 130), juce::roundToInt ((width - 28) * share));
    }

    void paint (juce::Graphics& g) override
    {
        drawWell (g, getLocalBounds().toFloat(), 12.0f);
        auto t = title;
        g.setColour (juce::Colour (info.colour));
        g.fillRoundedRectangle (t.removeFromLeft (7).toFloat().reduced (0, 5), 3.0f);
        t.removeFromLeft (9);
        const auto& p = pal();
        const bool on = owner.hasSwitch (index) && owner.isSectionOn (index);
        if (owner.hasSwitch (index))
        {
            drawLamp (g, { (float) t.getX() + 7.0f, (float) t.getCentreY() }, on ? Lit::play : Lit::none, 12.0f);
            t.removeFromLeft (20);
        }
        g.setColour (p.wellInk);
        g.setFont (cond (21.0f, Weight::extra, 0.08f));
        const auto name = juce::String (info.name).toUpperCase();
        const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 12;
        g.drawText (name, t.removeFromLeft (w), juce::Justification::centredLeft);
        g.setColour (p.wellInk2);
        g.setFont (cond (14.0f, Weight::semi, 0.06f));
        g.drawFittedText (subtitle.isNotEmpty() ? subtitle : juce::String (info.kind).toUpperCase(), t, juce::Justification::centredLeft, 1, 0.8f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10);
        auto head = r.removeFromTop (headerHeight - 4);
        reset.setBounds (head.removeFromRight (74).reduced (0, 1));
        head.removeFromRight (6);
        if (extraButton != nullptr)
        {
            extraButton->setBounds (head.removeFromRight (104).reduced (0, 1));
            head.removeFromRight (6);
        }
        if (power.isVisible())
        {
            power.setBounds (head.removeFromRight (index == synth ? 112 : 92).reduced (0, 1));
            head.removeFromRight (6);
        }
        title = head;
        r.removeFromTop (6);

        const int ew = extraWidth (getWidth());
        if (ew > 0)
        {
            extraArea = r.removeFromRight (ew);
            r.removeFromRight (8);
        }
        const int n = (int) knobs.size();
        const int per = perRow (getWidth());
        const int kw = juce::jmin (124, r.getWidth() / per);
        for (int i = 0; i < n; ++i)
        {
            const int row = i / per, col = i % per;
            const int inRow = juce::jmin (per, n - row * per);
            const int x0 = r.getX() + (r.getWidth() - inRow * kw) / 2;
            knobs[(size_t) i]->setBounds (juce::Rectangle<int> (x0 + col * kw, r.getY() + row * (knobHeight + 4), kw, knobHeight).reduced (2, 0));
        }
        if (onLayout)
            onLayout (extraArea);
    }

    void update()
    {
        const bool on = owner.isSectionOn (index);
        power.setToggleState (on, juce::dontSendNotification);
        juce::String text = on ? "On" : "Off";
        if (index == synth)
            text = on ? "Drone on" : "Drone off";
        else if (index == chords)
            text = on ? "Stop" : "Play";
        if (power.getButtonText() != text)
            power.setButtonText (text);
        repaint (title.withX (0).withRight (getWidth()));
    }

    VocoderSettings& owner;
    const int index;
    const SectionInfo& info;
    std::vector<std::unique_ptr<Knob>> knobs;
    juce::TextButton power, reset { "Reset" };
    juce::TextButton* extraButton = nullptr;   // Raw voice, in Out
    juce::String subtitle;                      // instead of the kind (Your voice: filling in)
    int knobRows = 1;                           // how many rows its knobs take, set by the page's layout
    std::function<void (juce::Rectangle<int>)> onLayout;
    juce::Rectangle<int> title, extraArea;
};

//==============================================================================
// The vocoder's keys, in a wooden key bed. With Hold on, a click turns a note on and the next
// click turns it off, so you can sing over it hands-free.
class VocoderSettings::Keys : public analog::Keys
{
public:
    Keys (juce::MidiKeyboardState& s, juce::Colour accent) : analog::Keys (s, accent), state (s) {}
    bool latch = false;

protected:
    bool mouseDownOnKey (int note, const juce::MouseEvent& e) override
    {
        if (! latch)
            return analog::Keys::mouseDownOnKey (note, e);
        if (state.isNoteOn (getMidiChannel(), note))
            state.noteOff (getMidiChannel(), note, 0.0f);
        else
            state.noteOn (getMidiChannel(), note, 0.8f);
        return false;
    }

    bool mouseDraggedToKey (int note, const juce::MouseEvent& e) override { return latch ? false : analog::Keys::mouseDraggedToKey (note, e); }

private:
    juce::MidiKeyboardState& state;
};

//==============================================================================
namespace
{
// The page inside the scroller: paints the keys' well.
struct Page : juce::Component
{
    std::function<void (juce::Graphics&)> painter;
    void paint (juce::Graphics& g) override { if (painter) painter (g); }
};

class ChordButtons : public juce::Component
{
public:
    juce::TextButton buttons[7];
    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (r.getHeight() / 2);
        r.removeFromTop (6);
        for (int i = 0; i < 7; ++i)
        {
            auto& row = i < 4 ? top : r;
            const int count = i < 4 ? 4 : 3;
            const int w = (getWidth() - (count - 1) * 6) / count;
            buttons[i].setBounds (row.removeFromLeft (w).withTrimmedBottom (0));
            row.removeFromLeft (6);
        }
    }
};
}

VocoderSettings::VocoderSettings (RigEngine& e, VocoderScreen& s) : engine (e), screen (s)
{
    auto& params = engine.getVocoder().params;
    auto* page = new Page();
    page->painter = [this] (juce::Graphics& g)
    {
        if (keysWell.isEmpty())
            return;
        drawWell (g, keysWell.toFloat(), 12.0f);
        engraved (g, "Keys", keysWell.reduced (14, 10).removeFromTop (22), juce::Justification::centredLeft, 18.0f, true);
        engraved (g, "Play the robot from here", keysWell.reduced (14, 10).withTrimmedTop (24).removeFromTop (18), juce::Justification::centredLeft,
                  12.0f, true);
    };
    viewport.setViewedComponent (page, true);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);

    for (int i = 0; i < numSections; ++i)
        page->addAndMakeVisible (sections.add (new Section (*this, i, params)));

    // Tune: the tuner.
    {
        auto* tuner = new Tuner (engine.getVocoder());
        sections[tune]->addAndMakeVisible (tuner);
        sections[tune]->onLayout = [tuner] (juce::Rectangle<int> r) { tuner->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 92))); };
        ownedExtras.add (tuner);
    }
    // Chords: the seven chords of the key.
    {
        auto* chordButtons = new ChordButtons();
        for (int i = 0; i < 7; ++i)
        {
            auto& b = chordButtons->buttons[i];
            setInWell (b, true);
            setLit (b, Lit::play);
            b.getProperties().set ("fontSize", 18.0f);
            b.setTooltip ("Click to have the robot sing this chord. Click again to stop.");
            b.onClick = [this, i] { screen.pressChord (i); refresh(); };
            chordButtons->addAndMakeVisible (b);
        }
        sections[chords]->addAndMakeVisible (chordButtons);
        sections[chords]->onLayout = [chordButtons] (juce::Rectangle<int> r) { chordButtons->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 92))); };
        chordPad = chordButtons;
        ownedExtras.add (chordButtons);
    }
    // Out: Blend and Raw voice.
    {
        blend = std::make_unique<Knob> ("Blend", juce::Colour (0xff5fb3c4));
        blend->inWell = true;
        blend->nameSize = 13.0f;
        blend->slider.setRange (0.0, 1.0, 0.01);
        blend->slider.setDoubleClickReturnValue (true, 0.5);
        blend->slider.textFromValueFunction = [] (double v)
        {
            if (std::abs (v - 0.5) < 0.005)
                return juce::String ("Both");
            return v < 0.5 ? "Voice " + juce::String (juce::roundToInt ((0.5 - v) * 200)) : "Robot " + juce::String (juce::roundToInt ((v - 0.5) * 200));
        };
        blend->slider.setTooltip ("Your voice (left) to the robot (right). In the middle you hear both at full. Moves both Levels.");
        blend->slider.onValueChange = [this] { screen.setDryWet (blend->slider.getValue()); blend->repaint(); };
        sections[out]->addAndMakeVisible (*blend);
        auto* knob = blend.get();
        sections[out]->onLayout = [knob] (juce::Rectangle<int> r) { knob->setBounds (r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), knobHeight))); };

        setInWell (rawVoice, true);
        rawVoice.getProperties().set ("fontSize", 15.0f);
        rawVoice.setTooltip ("Back to just your raw voice: every part off and the chords stopped.");
        rawVoice.onClick = [this] { pressRawVoice(); };
        sections[out]->addAndMakeVisible (rawVoice);
        sections[out]->extraButton = &rawVoice;
    }

    // The keys.
    keys = std::make_unique<Keys> (engine.getVocoder().keyboardState, juce::Colour (0xff5fb3c4));
    keys->setAvailableRange (36, 96);
    keys->setWantsKeyboardFocus (false);
    page->addAndMakeVisible (*keys);
    for (auto* b : { &holdButton, &stopButton })
    {
        setInWell (*b, true);
        b->getProperties().set ("fontSize", 15.0f);
        page->addAndMakeVisible (*b);
    }
    setLit (holdButton, Lit::next);
    holdButton.setTooltip ("Clicked keys keep playing until you click them again.");
    holdButton.onClick = [this] { setHold (! isHolding()); };
    stopButton.setTooltip ("Stops every note and chord the vocoder is playing.");
    stopButton.onClick = [this] { pressStopAll(); };

    refresh();
    startTimerHz (15);
}

VocoderSettings::~VocoderSettings()
{
    stopTimer();
}

juce::String VocoderSettings::sectionName (int i)
{
    return sectionInfo()[(size_t) juce::jlimit (0, numSections - 1, i)].name;
}

int VocoderSettings::numKnobs (int section) const
{
    return (int) sections[juce::jlimit (0, numSections - 1, section)]->knobs.size();
}

juce::StringArray VocoderSettings::shownIds() const
{
    juce::StringArray ids;
    for (const auto& s : sectionInfo())
        for (const auto& k : s.knobs)
            ids.add (k.id);
    // The switches.
    for (auto* id : { ParamIDs::voiceOn, ParamIDs::tuneOn, ParamIDs::robotOn, ParamIDs::drone, ParamIDs::arpOn, ParamIDs::vintageOn, ParamIDs::fxOn })
        ids.add (id);
    return ids;
}

bool VocoderSettings::hasSwitch (int section) const
{
    return section != out;
}

bool VocoderSettings::isSectionOn (int section) const
{
    if (section == voice)
        return screen.isMonitoring();
    if (section == effects)
        return screen.isEffectsOn();
    const int pedal = sectionInfo()[(size_t) juce::jlimit (0, numSections - 1, section)].pedal;
    return pedal >= 0 && screen.isOn (pedal);
}

void VocoderSettings::pressSwitch (int section)
{
    if (section == voice)
        screen.pressMonitor();
    else if (section == effects)
        screen.pressEffects();
    else if (const int pedal = sectionInfo()[(size_t) juce::jlimit (0, numSections - 1, section)].pedal; pedal >= 0)
        screen.stomp (pedal);
    refresh();
}

void VocoderSettings::resetSection (int section)
{
    section = juce::jlimit (0, numSections - 1, section);
    auto& params = engine.getVocoder().params;
    const bool wasOn = hasSwitch (section) && isSectionOn (section);
    for (const auto& s : sectionInfo()[(size_t) section].knobs)
        if (auto* p = params.getParameter (s.id))
            p->setValueNotifyingHost (p->getDefaultValue());
    // Reset keeps the part on or off as it was.
    if (wasOn && ! isSectionOn (section))
        pressSwitch (section);
    if (screen.onSay)
        screen.onSay (sectionName (section) + ": back to how it starts.");
    screen.refresh();
    refresh();
}

void VocoderSettings::pressRawVoice()
{
    screen.pickSound (0);
    pressStopAll();
}

void VocoderSettings::pressStopAll()
{
    if (screen.getActiveChord() >= 0)
        screen.pressChord (screen.getActiveChord());
    engine.getVocoder().requestChord ({}, true);
    engine.getVocoder().keyboardState.allNotesOff (1);
    refresh();
}

void VocoderSettings::setHold (bool hold)
{
    keys->latch = hold;
    if (! hold)
        engine.getVocoder().keyboardState.allNotesOff (1);
    holdButton.setToggleState (hold, juce::dontSendNotification);
}

bool VocoderSettings::isHolding() const { return keys->latch; }

juce::MidiKeyboardState& VocoderSettings::getKeyState() { return engine.getVocoder().keyboardState; }

void VocoderSettings::refresh()
{
    for (auto* s : sections)
        s->update();
    // Your voice: say when it's filling in on its own (the robot has nothing to sing).
    const juce::String voiceText = engine.getVocoder().isVoiceStandingIn() && ! screen.isMonitoring() ? "FILLING IN ON ITS OWN" : "";
    if (sections[voice]->subtitle != voiceText)
    {
        sections[voice]->subtitle = voiceText;
        sections[voice]->repaint();
    }
    if (blend != nullptr)
        blend->slider.setValue (screen.getDryWet(), juce::dontSendNotification);
    const auto& p = engine.getVocoder().params;
    const int key = juce::roundToInt (p.getRawParameterValue (ParamIDs::chordKey)->load());
    const int scale = juce::roundToInt (p.getRawParameterValue (ParamIDs::chordScale)->load());
    const int type = juce::roundToInt (p.getRawParameterValue (ParamIDs::chordFlavour)->load());
    if (auto* pad = dynamic_cast<ChordButtons*> (chordPad))
        for (int i = 0; i < 7; ++i)
        {
            const juce::String name (vox::Chords::name (key, scale, i, type));
            if (pad->buttons[i].getButtonText() != name)
                pad->buttons[i].setButtonText (name);
            pad->buttons[i].setToggleState (i == screen.getActiveChord(), juce::dontSendNotification);
        }
}

void VocoderSettings::timerCallback()
{
    if (! isShowing())
        return;
    if (auto* tuner = dynamic_cast<Tuner*> (ownedExtras[0]))
        tuner->update();
    refresh();
}

void VocoderSettings::resized()
{
    viewport.setBounds (getLocalBounds());
    layOut (getWidth() - viewport.getScrollBarThickness() - 4);
}

void VocoderSettings::layOut (int width)
{
    auto* page = viewport.getViewedComponent();
    if (page == nullptr || width <= 0)
        return;

    // The parts go in the chain's order, in bands across the page. A band is one or two rows of knobs
    // tall, whichever packs more knobs into the height; each part is as wide as its knobs need, and
    // the band's spare width is shared out.
    const float maxUnits = (float) (width - 30) / (float) minCell;
    int y = 0;
    for (int first = 0; first < sections.size();)
    {
        int bestRows = 1, bestCount = 1;
        float bestScore = -1.0f;
        for (int rows : { 1, 2 })
        {
            float used = 0.0f;
            int count = 0, knobsIn = 0;
            for (int i = first; i < sections.size(); ++i)
            {
                const float w = sections[i]->units (rows);
                if (count > 0 && used + w > maxUnits)
                    break;
                used += w;
                knobsIn += (int) sections[i]->knobs.size() + juce::roundToInt (sections[i]->info.extraUnits * rows);
                ++count;
            }
            const float score = (float) knobsIn / (float) rows;
            if (score > bestScore + 0.01f)
            {
                bestScore = score;
                bestRows = rows;
                bestCount = count;
            }
        }

        std::vector<Section*> row;
        float total = 0.0f;
        for (int i = first; i < first + bestCount; ++i)
        {
            sections[i]->knobRows = bestRows;
            row.push_back (sections[i]);
            total += sections[i]->currentUnits();
        }
        first += bestCount;

        const int gaps = gap * ((int) row.size() - 1);
        std::vector<int> widths;
        int x = 0, height = 0;
        for (size_t i = 0; i < row.size(); ++i)
        {
            const int w = i + 1 == row.size() ? width - gaps - x : juce::roundToInt ((float) (width - gaps) * row[i]->currentUnits() / total);
            widths.push_back (w);
            height = juce::jmax (height, row[i]->heightFor (w));
            x += w;
        }
        x = 0;
        for (size_t i = 0; i < row.size(); ++i)
        {
            row[i]->setBounds (x, y, widths[i], height);
            x += widths[i] + gap;
        }
        y += height + gap;
    }

    keysWell = { 0, y, width, 112 };
    {
        auto k = keysWell.reduced (14, 10);
        auto left = k.removeFromLeft (190);
        left.removeFromTop (46);
        holdButton.setBounds (left.removeFromLeft (88).reduced (0, 4));
        left.removeFromLeft (8);
        stopButton.setBounds (left.reduced (0, 4));
        k.removeFromLeft (8);
        keys->setBounds (k);
        const int whiteKeys = 36;   // C2 to C7
        keys->setKeyWidth ((float) k.getWidth() / whiteKeys);
    }
    y = keysWell.getBottom();
    page->setSize (width, y);
}
