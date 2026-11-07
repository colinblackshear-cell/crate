#include "GrooveView.h"
#include "RigColours.h"

namespace Theme = crate::Theme;

namespace
{
constexpr int kTrackWidth = 230, kGap = 10, kPartsHeight = 58;

void setParam (juce::AudioProcessorValueTreeState& params, const juce::String& id, float value)
{
    if (auto* p = params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

float getParam (const juce::AudioProcessorValueTreeState& params, const juce::String& id)
{
    auto* v = params.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

void styleButton (juce::TextButton& b, juce::Colour colour)
{
    b.setColour (juce::TextButton::buttonOnColourId, colour);
    b.setColour (juce::TextButton::textColourOnId, Theme::background);
}

// The vocoder sounds on the Main screen's pattern row: one of each kind.
const char* const vocoderSounds[] = { "Raw Voice", "Classic Robot", "French Touch", "Choir Pad",
                                      "Talk Box Funk", "Hard Tune", "Space Echo", "Monster" };

// The vocoder's module switches, as its chain bar has them.
const std::pair<const char*, const char*> vocoderSwitches[] = { { "robotOn", "Robot" }, { "voiceOn", "Voice" },
                                                                { "fxOn", "Effects" }, { "arpOn", "Arp" },
                                                                { "vintageOn", "Vintage" } };

const char* const arpModes[] = { "Up", "Down", "Up + down", "Random", "As played" };

struct KnobSpec { const char* id; const char* name; };
const KnobSpec bassKnobs[] = { { "cutoff", "Cutoff" }, { "resonance", "Resonance" }, { "envMod", "Env mod" }, { "decay", "Decay" },
                               { "accent", "Accent" }, { "glide", "Glide" }, { "drive", "Drive" }, { "volume", "Volume" } };
const KnobSpec synthKnobs[] = { { "cutoff", "Cutoff" }, { "resonance", "Resonance" }, { "envMod", "Env mod" }, { "attack", "Attack" },
                                { "release", "Release" }, { "detune", "Detune" }, { "lfoDepth", "LFO depth" }, { "volume", "Volume" } };
const KnobSpec vocoderKnobs[] = { { "robotLevel", "Robot" }, { "voiceLevel", "Voice" }, { "formant", "Formant" }, { "cutoff", "Cutoff" },
                                  { "resonance", "Resonance" }, { "chorusMix", "Chorus" }, { "delayMix", "Delay" }, { "reverbMix", "Reverb" } };
// The tracks without their own knobs get their mixer strip's effects.
const KnobSpec fxKnobs[] = { { "filter", "Filter" }, { "resonance", "Resonance" }, { "drive", "Drive" }, { "crush", "Crush" },
                             { "delayMix", "Delay" }, { "feedback", "Feedback" }, { "reverbMix", "Reverb" }, { "reverbSize", "Room" } };

int destFor (int track)
{
    switch (track)
    {
        case GrooveView::drums:   return (int) RigRouting::Dest::mpc;
        case GrooveView::bass:    return (int) RigRouting::Dest::bass;
        case GrooveView::synth:   return (int) RigRouting::Dest::synth;
        case GrooveView::vocoder: return (int) RigRouting::Dest::vocoder;
        case GrooveView::sampler: return (int) RigRouting::Dest::sampler;
        default:                  return -1;
    }
}

constexpr int kDrumStepTicks = seq::kTicksPerBeat / 4;   // 1/16
}

//==============================================================================
// A track down the left: its name, what it's doing (or why it's quiet), a meter and a mute.
class GrooveView::TrackButton : public juce::Component
{
public:
    TrackButton (GrooveView& o, int t) : owner (o), index (t), colour (trackColour (t))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setRepaintsOnMouseActivity (true);
        addAndMakeVisible (mute);
        mute.setClickingTogglesState (true);
        mute.setColour (juce::TextButton::buttonOnColourId, Theme::record);
        mute.setTooltip ("Mutes " + trackName (t) + ". Muted, it keeps going silently, so it comes back in time.");
        mute.onClick = [this] { owner.engine.getStrip (stripFor (index)).mute = mute.getToggleState(); owner.repaint(); };
    }

    void tick()
    {
        const bool muted = owner.engine.getStrip (stripFor (index)).mute.load();
        mute.setToggleState (muted, juce::dontSendNotification);
        const auto text = owner.stateOf (index);
        const float m = owner.meterOf ? owner.meterOf (stripFor (index)) : 0.0f;
        const bool keys = owner.engine.getRouting().keysTo.load() == destFor (index);
        if (text != state || std::abs (m - meter) > 0.003f || keys != hasKeys)
        {
            state = text;
            meter = m;
            hasKeys = keys;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const bool selected = owner.track == index;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (selected ? Theme::raised.brighter (0.1f) : isMouseOver (true) ? Theme::raised.brighter (0.04f) : Theme::raised);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (selected ? colour : colour.withAlpha (0.25f));
        g.drawRoundedRectangle (r, 8.0f, selected ? 2.0f : 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.withWidth (5.0f).reduced (0, 10), 2.0f);

        auto area = getLocalBounds().reduced (16, 8);
        area.removeFromRight (mute.getWidth() + 6);
        auto top = area.removeFromTop (24);
        g.setColour (selected ? colour : Theme::text);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText (trackName (index), top, juce::Justification::centredLeft);
        if (hasKeys)
        {
            g.setColour (colour.withAlpha (0.85f));
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            g.drawText ("KEYS", top, juce::Justification::centredRight);
        }

        const bool quiet = owner.engine.getStrip (stripFor (index)).mute.load();
        g.setColour (quiet ? Theme::record.withAlpha (0.9f) : Theme::textDim);
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (state, area.removeFromTop (32), juce::Justification::topLeft, 2);

        auto bar = area.removeFromBottom (5).toFloat();
        g.setColour (Theme::background);
        g.fillRoundedRectangle (bar, 2.5f);
        const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
        g.setColour (colour.withAlpha (quiet ? 0.3f : 0.9f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f)), 2.5f);
    }

    void resized() override
    {
        mute.setBounds (getLocalBounds().reduced (10).removeFromRight (34).withSizeKeepingCentre (34, 30));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            owner.selectTrack (index);
    }

    juce::TextButton mute { "M" };

private:
    GrooveView& owner;
    const int index;
    const juce::Colour colour;
    juce::String state;
    float meter = -1;
    bool hasKeys = false;
};

//==============================================================================
juce::String GrooveView::trackName (int t)
{
    switch (t)
    {
        case drums:   return "Drums";
        case bass:    return "Bass";
        case synth:   return "Synth";
        case vocoder: return "Vocoder";
        case sampler: return "Sampler";
        case loops:   return "Loops";
        default:      return {};
    }
}

juce::Colour GrooveView::trackColour (int t) { return RigColours::forStrip (stripFor (t)); }

int GrooveView::stripFor (int t)
{
    switch (t)
    {
        case drums:   return RigEngine::mpcStrip;
        case bass:    return RigEngine::bassStrip;
        case synth:   return RigEngine::synthStrip;
        case vocoder: return RigEngine::vocoderStrip;
        case sampler: return RigEngine::samplerStrip;
        default:      return RigEngine::loopsStrip;
    }
}

juce::String GrooveView::tabFor (int t)
{
    switch (t)
    {
        case drums: return "MPC";
        case loops: return "Looper";
        default:    return trackName (t);
    }
}

//==============================================================================
GrooveView::GrooveView (RigEngine& e) : engine (e)
{
    for (int t = 0; t < numTracks; ++t)
        addAndMakeVisible (trackButtons.add (new TrackButton (*this, t)));

    addAndMakeVisible (openButton);
    openButton.onClick = [this] { if (onOpen) onOpen (tabFor (track)); };

    addChildComponent (micButton);
    micButton.setClickingTogglesState (true);
    micButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
    micButton.setTooltip ("The mic, for the vocoder. Use headphones with it on, or the speakers feed back into the mic.");
    micButton.onClick = [this] { engine.micOn = micButton.getToggleState(); };

    addChildComponent (extraButton);
    extraButton.setClickingTogglesState (true);
    extraButton.onClick = [this]
    {
        if (track == bass)
            setParam (engine.getBass().params, "sequencer", extraButton.getToggleState() ? 1.0f : 0.0f);
        else if (track == vocoder)
            engine.vocoderOn = extraButton.getToggleState();
        updateButtons();
    };

    addChildComponent (barButton);
    barButton.setTooltip ("Which bar of the beat the steps show. Click for the next one.");
    barButton.onClick = [this]
    {
        const auto& s = engine.getMpc().getProject().currentSequence();
        drumBar = (drumBar + 1) % juce::jmax (1, s.bars);
        updateButtons();
        repaint();
    };

    for (int i = 0; i < 8; ++i)
    {
        auto* b = partButtons.add (new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + i))));
        styleButton (*b, RigColours::song);
        b->onClick = [this, i]
        {
            if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
                return;
            pressPart (i);
        };
        addAndMakeVisible (b);
    }

    // Start on the track the keys already play, so nothing changes until you pick one.
    for (int t = 0; t < numTracks; ++t)
        if (destFor (t) == engine.getRouting().keysTo.load())
            track = t;
    buildTrack();
    setFollowKnobs();
    startTimerHz (30);
}

GrooveView::~GrooveView()
{
    stopTimer();
}

void GrooveView::refresh()
{
    shownSignature = {};
    buildTrack();
    setFollowKnobs();
    for (auto* t : trackButtons)
        t->tick();
    repaint();
}

void GrooveView::selectTrack (int t)
{
    if (! juce::isPositiveAndBelow (t, (int) numTracks))
        return;
    track = t;
    // The keys play the track you're on (the loops have no notes, so the keys stay put).
    if (const int dest = destFor (t); dest >= 0)
        engine.getRouting().keysTo = dest;
    heldCell = dragStep = -1;
    buildTrack();
    setFollowKnobs();
    for (auto* b : trackButtons)
        b->tick();
    repaint();
}

//==============================================================================
juce::AudioProcessorValueTreeState* GrooveView::paramsOf (int tool) const
{
    switch (tool)
    {
        case RigEngine::bassTool:    return &engine.getBass().params;
        case RigEngine::synthTool_:  return &engine.getSynth().params;
        case RigEngine::vocoderTool: return &engine.getVocoder().params;
        default:
            if (tool >= RigEngine::fxTool && tool < RigEngine::numTools)
                return &engine.getFx (tool - RigEngine::fxTool).params;
            return nullptr;
    }
}

std::pair<int, juce::String> GrooveView::knobFor (int index) const
{
    if (! juce::isPositiveAndBelow (index, 8))
        return { -1, {} };
    switch (track)
    {
        case bass:    return { RigEngine::bassTool, bassKnobs[index].id };
        case synth:   return { RigEngine::synthTool_, synthKnobs[index].id };
        case vocoder: return { RigEngine::vocoderTool, vocoderKnobs[index].id };
        default:      return { RigEngine::fxTool + stripFor (track), fxKnobs[index].id };
    }
}

void GrooveView::setFollowKnobs()
{
    auto& routing = engine.getRouting();
    for (int i = 0; i < RigRouting::kFollowKnobs; ++i)
    {
        const auto [tool, id] = knobFor (i);
        int parameter = -1;
        if (tool >= 0)
        {
            const auto& list = engine.getTool (tool).getParameters();
            for (int p = 0; p < list.size(); ++p)
                if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (list[p]); withId != nullptr && withId->paramID == id)
                    parameter = p;
        }
        routing.setFollowKnob (i, tool, parameter);
    }
}

//==============================================================================
// The middle, rebuilt for the track picked: its pattern row and its knobs.
void GrooveView::buildTrack()
{
    const auto colour = trackColour (track);
    patternButtons.clear();
    auto add = [this, colour] (const juce::String& text, const juce::String& tip)
    {
        auto* b = patternButtons.add (new juce::TextButton (text));
        styleButton (*b, colour);
        b->setTooltip (tip);
        const int i = patternButtons.size() - 1;
        b->onClick = [this, i] { pressPattern (i); };
        addAndMakeVisible (b);
    };

    switch (track)
    {
        case drums:
            for (int i = 0; i < 8; ++i)
                add ("Beat " + juce::String (i + 1), "Plays the MPC's sequence " + juce::String (i + 1)
                                                     + ". Playing, it comes in at the next bar. The steps edit the beat that's on.");
            break;
        case bass:
            for (int i = 0; i < crate::bass::kPatterns; ++i)
                add ("Pattern " + juce::String (i + 1), "Bass pattern " + juce::String (i + 1) + ". Playing, it changes at the next bar.");
            break;
        case synth:
            add ("Arp off", "The synth plays what you play on the keys.");
            for (auto* m : arpModes)
                add (m, "The arp plays the chord you hold, in this order, on the steps that are lit.");
            add ("Hold", "On, the arp keeps playing after you let go of the keys. Press a new chord to change it.");
            patternButtons.getLast()->setClickingTogglesState (false);
            break;
        case vocoder:
            for (auto* name : vocoderSounds)
                add (name, "Vocoder sound: " + juce::String (name) + ".");
            break;
        case sampler:
            add ("Sample", "Records what's picked under Sample at the bottom left into the Sampler (or an MPC pad).");
            patternButtons.getLast()->setColour (juce::TextButton::buttonOnColourId, Theme::record);
            add ("Stop sound", "Stops the Sampler's sound.");
            break;
        case loops:
            for (int i = 0; i < RigLooper::kLayers; ++i)
                add ("Mute " + juce::String (i + 1), "Mutes loop layer " + juce::String (i + 1) + ". It keeps going round, so it comes back in time.");
            break;
        default: break;
    }

    knobs.clear();
    for (int i = 0; i < 8; ++i)
    {
        const auto [tool, id] = knobFor (i);
        auto* params = paramsOf (tool);
        if (params == nullptr || params->getParameter (id) == nullptr)
            continue;
        const auto* specs = track == bass ? bassKnobs : track == synth ? synthKnobs : track == vocoder ? vocoderKnobs : fxKnobs;
        auto knob = std::make_unique<crate::ParamKnob> (*params, id, specs[i].name, colour);
        knob->slider.setTooltip (juce::String (specs[i].name) + ". Knob " + juce::String (i + 1)
                                 + " on your keyboard turns it too. Double-click for the default.");
        if (track == vocoder)
        {
            // The vocoder's settings have no units of their own: keep the numbers short.
            knob->slider.textFromValueFunction = [] (double v) { return juce::String (v, std::abs (v) >= 100.0 ? 0 : 2); };
            knob->slider.updateText();
        }
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    openButton.setButtonText ("Open " + tabFor (track) + "  >");
    openButton.setTooltip ("Everything " + trackName (track) + " can do, in its own tab.");
    micButton.setVisible (track == vocoder && showMic);
    extraButton.setVisible (track == bass || track == vocoder);
    styleButton (extraButton, colour);
    barButton.setVisible (track == drums);
    drumBar = 0;
    updateButtons();
    resized();
}

// Lights the buttons to match what's on.
void GrooveView::updateButtons()
{
    switch (track)
    {
        case drums:
        {
            auto& mpc = engine.getMpc();
            const int now = mpc.getProject().getCurrentSequenceIndex(), queued = mpc.getQueuedSequence();
            for (int i = 0; i < patternButtons.size(); ++i)
            {
                patternButtons[i]->setToggleState (i == now, juce::dontSendNotification);
                patternButtons[i]->setColour (juce::TextButton::buttonColourId, i == queued ? trackColour (track).withAlpha (0.35f) : Theme::panel);
            }
            const auto& s = mpc.getProject().currentSequence();
            drumBar = juce::jlimit (0, juce::jmax (0, s.bars - 1), drumBar);
            barButton.setButtonText ("Bar " + juce::String (drumBar + 1) + " of " + juce::String (s.bars));
            break;
        }
        case bass:
        {
            const int selected = engine.getBass().getSelectedPattern();
            for (int i = 0; i < patternButtons.size(); ++i)
                patternButtons[i]->setToggleState (i == selected, juce::dontSendNotification);
            const bool on = getParam (engine.getBass().params, "sequencer") > 0.5f;
            extraButton.setToggleState (on, juce::dontSendNotification);
            extraButton.setButtonText (on ? "Pattern on" : "Pattern off");
            extraButton.setTooltip ("On, the bass plays its pattern while the Rig plays. Off, it only plays what you play on the keys.");
            break;
        }
        case synth:
        {
            auto& params = engine.getSynth().params;
            const bool arp = getParam (params, "arpOn") > 0.5f;
            const int mode = (int) getParam (params, "arpMode");
            for (int i = 0; i < patternButtons.size() - 1; ++i)
                patternButtons[i]->setToggleState (i == 0 ? ! arp : arp && mode == i - 1, juce::dontSendNotification);
            if (auto* hold = patternButtons.getLast())
                hold->setToggleState (getParam (params, "latch") > 0.5f, juce::dontSendNotification);
            break;
        }
        case vocoder:
        {
            auto& v = engine.getVocoder();
            const auto current = v.getProgramName (v.getCurrentProgram());
            for (int i = 0; i < patternButtons.size(); ++i)
                patternButtons[i]->setToggleState (current == vocoderSounds[i], juce::dontSendNotification);
            const bool on = engine.vocoderOn.load();
            extraButton.setToggleState (on, juce::dontSendNotification);
            extraButton.setButtonText (on ? "Vocoder on" : "Vocoder off");
            extraButton.setTooltip ("Turns the vocoder on or off.");
            micButton.setToggleState (engine.micOn.load(), juce::dontSendNotification);
            micButton.setButtonText (engine.micOn.load() ? "Mic on" : "Mic off");
            break;
        }
        case sampler:
        {
            const bool recording = isSampling && isSampling();
            if (auto* b = patternButtons[0])
            {
                b->setToggleState (recording, juce::dontSendNotification);
                b->setButtonText (recording ? "Stop sampling" : "Sample");
            }
            break;
        }
        case loops:
            for (int i = 0; i < patternButtons.size(); ++i)
                patternButtons[i]->setToggleState (engine.getLooper().getLayer (i).mute.load(), juce::dontSendNotification);
            break;
        default: break;
    }

    // Song parts: the one playing lit, the one coming next half lit, the first empty one says
    // it saves.
    auto& scenes = engine.getScenes();
    const int count = (int) scenes.getScenes().size();
    for (int i = 0; i < partButtons.size(); ++i)
    {
        auto* b = partButtons[i];
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
        const bool has = i < count;
        const auto name = has ? scenes.getScenes()[(size_t) i].name : juce::String();
        b->setButtonText (has ? (name == "Part " + letter ? letter : letter + "  " + name) : i == count ? letter + "  + Save" : letter);
        b->setToggleState (i == scenes.getCurrent(), juce::dontSendNotification);
        b->setColour (juce::TextButton::buttonColourId, i == scenes.getQueued() ? RigColours::song.withAlpha (0.35f) : Theme::panel);
        b->setAlpha (i <= count ? 1.0f : 0.4f);
        b->setTooltip (has ? "Plays this part from the next bar (straight away when stopped)."
                           : "Saves what's playing now (beats, patterns, mutes) as a song part. Then press it to come back to it.");
    }
}

//==============================================================================
juce::String GrooveView::stateOf (int t) const
{
    if (engine.getStrip (stripFor (t)).mute.load())
        return "Muted";
    const bool playing = engine.getTransport().isPlaying();
    switch (t)
    {
        case drums:
        {
            auto& mpc = engine.getMpc();
            auto& project = mpc.getProject();
            if (project.currentBank().usedPads() == 0)
                return "No sounds on the pads yet: load a kit in MPC or Sounds";
            if (project.currentSequence().isEmpty())
                return "No beat yet: light some steps";
            const auto name = "Beat " + juce::String (project.getCurrentSequenceIndex() + 1);
            return playing ? "Playing " + name : name + " ready: press Play";
        }
        case bass:
        {
            auto& b = engine.getBass();
            if (getParam (b.params, "sequencer") < 0.5f)
                return "Pattern off: plays from the keys";
            const auto p = b.getPattern (b.getSelectedPattern());
            const bool any = std::any_of (p.steps.begin(), p.steps.begin() + p.length, [] (const crate::bass::Step& s) { return s.on; });
            const auto name = "Pattern " + juce::String (b.getSelectedPattern() + 1);
            if (! any)
                return name + " has no notes: light some steps";
            return playing ? "Playing " + name : name + " ready: press Play";
        }
        case synth:
        {
            auto& params = engine.getSynth().params;
            if (getParam (params, "arpOn") < 0.5f)
                return "Plays what you play on the keys";
            return getParam (params, "latch") > 0.5f ? "Arp, holding: play a chord" : "Arp: hold a chord on the keys";
        }
        case vocoder:
            if (! engine.vocoderOn.load())
                return "Switched off";
            if (! engine.micOn.load())
                return "Mic is off: turn on Mic to sing";
            return "Sing into the mic and play the keys";
        case sampler:
        {
            auto& doc = engine.getSampler().getDocument();
            if (! doc.hasAudio())
                return "Empty: press Sample to record something";
            return "Has " + (doc.getName().isNotEmpty() ? doc.getName() : juce::String ("a sound")) + ": play the pads";
        }
        case loops:
        {
            auto& looper = engine.getLooper();
            int going = 0, recording = -1;
            for (int i = 0; i < RigLooper::kLayers; ++i)
            {
                const auto s = looper.getState (i);
                if (s == RigLooper::State::playing || s == RigLooper::State::overdubbing)
                    ++going;
                if (s == RigLooper::State::recording || s == RigLooper::State::armed)
                    recording = i;
            }
            if (recording >= 0)
                return "Recording layer " + juce::String (recording + 1);
            if (going == 0)
                return "Empty: press a layer to record";
            return juce::String (going) + (going == 1 ? " layer playing" : " layers playing");
        }
        default: return {};
    }
}

//==============================================================================
int GrooveView::numSteps() const
{
    switch (track)
    {
        case drums:   return juce::jlimit (1, 16, engine.getMpc().getProject().currentSequence().beatsPerBar * 4);
        case vocoder: return (int) std::size (vocoderSwitches);
        case loops:   return RigLooper::kLayers;
        default:      return 16;
    }
}

int GrooveView::drumPad() const
{
    return engine.getRouting().padBank.load() * kPadsPerBank + drumPadIndex;
}

bool GrooveView::drumHit (int pad, int step) const
{
    const auto& s = engine.getMpc().getProject().currentSequence();
    const int at = drumBar * s.ticksPerBar() + step * kDrumStepTicks;
    for (const auto& t : s.tracks)
        for (const auto& n : t.notes)
            if (n.pad == pad && n.tick >= at - kDrumStepTicks / 2 && n.tick < at + kDrumStepTicks / 2)
                return true;
    return false;
}

void GrooveView::setDrumHit (int pad, int step, bool on)
{
    auto& mpc = engine.getMpc();
    auto& s = mpc.getProject().currentSequence();
    const int at = drumBar * s.ticksPerBar() + step * kDrumStepTicks;
    if (at >= s.lengthTicks() || s.tracks.empty())
        return;
    mpc.beginEdit();
    for (auto& t : s.tracks)
        t.remove (pad, at - kDrumStepTicks / 2, at + kDrumStepTicks / 2);
    if (on)
        s.tracks[(size_t) juce::jlimit (0, (int) s.tracks.size() - 1, mpc.recordTrack)].add ({ at, pad, 100, kDrumStepTicks });
    mpc.getProject().sequenceChanged();
}

GrooveView::Cell GrooveView::cellAt (int i) const
{
    Cell c;
    switch (track)
    {
        case drums:
            c.lit = drumHit (drumPad(), i);
            break;
        case bass:
        {
            auto& b = engine.getBass();
            const auto p = b.getPattern (b.getSelectedPattern());
            const auto& s = p.steps[(size_t) i];
            c.lit = s.on;
            c.strong = s.accent;
            c.dim = i >= p.length;
            c.label = s.on ? juce::MidiMessage::getMidiNoteName ((int) getParam (b.params, "root") + s.note, true, true, 3) : juce::String();
            c.sub = s.on && s.slide ? "slide" : juce::String();
            break;
        }
        case synth:
        {
            const auto r = engine.getSynth().getRhythm();
            c.lit = r.on[(size_t) i];
            c.strong = r.accent[(size_t) i];
            c.dim = i >= r.length;
            break;
        }
        case vocoder:
            c.label = vocoderSwitches[i].second;
            c.lit = getParam (engine.getVocoder().params, vocoderSwitches[i].first) > 0.5f;
            c.sub = c.lit ? "on" : "off";
            c.dim = ! engine.vocoderOn.load();
            break;
        case sampler:
        {
            auto& s = engine.getSampler();
            const bool slices = s.getKeys().getMode() == crate::KeyboardSampler::Mode::slices;
            const int count = slices ? (int) s.getChops().size() + 1 : 16;
            c.label = juce::String (i + 1);
            c.lit = s.getDocument().hasAudio() && i < count;
            c.dim = ! c.lit;
            c.playing = heldCell == i;
            c.sub = slices ? "slice" : juce::String();
            break;
        }
        case loops:
        {
            auto& looper = engine.getLooper();
            const auto st = looper.getState (i);
            c.label = juce::String (i + 1);
            c.lit = st != RigLooper::State::empty;
            c.strong = st == RigLooper::State::recording || st == RigLooper::State::overdubbing || st == RigLooper::State::armed;
            c.dim = looper.getLayer (i).mute.load();
            switch (st)
            {
                case RigLooper::State::empty:       c.sub = "Rec"; break;
                case RigLooper::State::armed:       c.sub = "Waiting for the bar"; break;
                case RigLooper::State::recording:   c.sub = "Recording"; break;
                case RigLooper::State::playing:     c.sub = c.dim ? "Muted" : "Playing"; break;
                case RigLooper::State::overdubbing: c.sub = "Overdub"; break;
            }
            break;
        }
        default: break;
    }
    return c;
}

int GrooveView::playheadStep() const
{
    if (! engine.getTransport().isPlaying())
        return -1;
    switch (track)
    {
        case drums:
        {
            auto& mpc = engine.getMpc();
            if (! mpc.getSequencer().isPlaying())
                return -1;
            const auto& s = mpc.getProject().currentSequence();
            const double pos = mpc.getSequencer().getPosition() - drumBar * s.ticksPerBar();
            return pos >= 0 && pos < s.ticksPerBar() ? (int) (pos / kDrumStepTicks) : -1;
        }
        case bass:
            return getParam (engine.getBass().params, "sequencer") > 0.5f ? engine.getBass().getCurrentStep() : -1;
        case synth:
            return getParam (engine.getSynth().params, "arpOn") > 0.5f ? engine.getSynth().getArpStep() : -1;
        default:
            return -1;
    }
}

int GrooveView::laneCount() const
{
    return track == drums || track == bass || track == synth ? 16 : 0;
}

GrooveView::Cell GrooveView::laneCell (int i) const
{
    Cell c;
    switch (track)
    {
        case drums:
        {
            const int pad = engine.getRouting().padBank.load() * kPadsPerBank + i;
            const auto& p = engine.getMpc().getProject().currentBank().pads[(size_t) pad];
            c.label = padLabel (pad);
            c.sub = p.isEmpty() ? juce::String ("empty") : p.name;
            c.lit = i == drumPadIndex;
            c.dim = p.isEmpty();
            break;
        }
        case bass:
        {
            const auto cell = cellAt (i);
            c.label = cell.label.isNotEmpty() ? cell.label : juce::String ("-");
            c.lit = cell.lit;
            c.dim = cell.dim || ! cell.lit;
            c.playing = dragStep == i;
            break;
        }
        case synth:
        {
            const auto r = engine.getSynth().getRhythm();
            c.label = r.accent[(size_t) i] ? "Accent" : "";
            c.lit = r.accent[(size_t) i];
            c.dim = i >= r.length;
            break;
        }
        default: break;
    }
    return c;
}

//==============================================================================
void GrooveView::pressStep (int i)
{
    if (! juce::isPositiveAndBelow (i, numSteps()))
        return;
    switch (track)
    {
        case drums:
        {
            const int pad = drumPad();
            const bool add = ! drumHit (pad, i);
            setDrumHit (pad, i, add);
            if (add && ! engine.getTransport().isPlaying())
                engine.getMpc().playPad (pad, 100 / 127.0f);
            break;
        }
        case bass:
        {
            auto& b = engine.getBass();
            const int index = b.getSelectedPattern();
            auto p = b.getPattern (index);
            auto& s = p.steps[(size_t) i];
            s.on = ! s.on;
            if (i >= p.length && s.on)
                p.length = i + 1;
            b.setPattern (index, p);
            // A lit step means you want to hear the pattern.
            if (s.on)
                setParam (b.params, "sequencer", 1.0f);
            break;
        }
        case synth:
        {
            auto& synthTool = engine.getSynth();
            auto r = synthTool.getRhythm();
            r.on[(size_t) i] = ! r.on[(size_t) i];
            if (i >= r.length && r.on[(size_t) i])
                r.length = i + 1;
            synthTool.setRhythm (r);
            if (r.on[(size_t) i])
                setParam (synthTool.params, "arpOn", 1.0f);
            break;
        }
        case vocoder:
        {
            auto& params = engine.getVocoder().params;
            const bool on = getParam (params, vocoderSwitches[i].first) < 0.5f;
            setParam (params, vocoderSwitches[i].first, on ? 1.0f : 0.0f);
            if (on)
                engine.vocoderOn = true;
            break;
        }
        case sampler:
        {
            auto& s = engine.getSampler();
            const bool slices = s.getKeys().getMode() == crate::KeyboardSampler::Mode::slices;
            const int note = slices ? crate::KeyboardSampler::kFirstSliceNote + i : s.getKeys().getRootNote() + i;
            s.keyboardState.noteOn (1, juce::jlimit (0, 127, note), 0.9f);
            heldCell = i;
            break;
        }
        case loops:
            engine.getLooper().pressRecord (i);
            break;
        default: break;
    }
    updateButtons();
    repaint();
}

void GrooveView::releaseCell (int i)
{
    if (track == sampler && heldCell == i)
    {
        auto& s = engine.getSampler();
        const bool slices = s.getKeys().getMode() == crate::KeyboardSampler::Mode::slices;
        const int note = slices ? crate::KeyboardSampler::kFirstSliceNote + i : s.getKeys().getRootNote() + i;
        s.keyboardState.noteOff (1, juce::jlimit (0, 127, note), 0.0f);
    }
    heldCell = -1;
    repaint();
}

void GrooveView::pressLane (int i)
{
    if (track == drums)
    {
        drumPadIndex = juce::jlimit (0, kPadsPerBank - 1, i);
        engine.getMpc().playPad (drumPad(), 100 / 127.0f);
    }
    else if (track == synth)
        toggleAccent (i);
    repaint();
}

void GrooveView::toggleAccent (int i)
{
    if (track == bass)
    {
        auto& b = engine.getBass();
        auto p = b.getPattern (b.getSelectedPattern());
        p.steps[(size_t) i].accent = ! p.steps[(size_t) i].accent;
        b.setPattern (b.getSelectedPattern(), p);
    }
    else if (track == synth)
    {
        auto r = engine.getSynth().getRhythm();
        r.accent[(size_t) i] = ! r.accent[(size_t) i];
        engine.getSynth().setRhythm (r);
    }
    repaint();
}

void GrooveView::nudgeNote (int i, int semitones)
{
    if (track != bass || semitones == 0)
        return;
    auto& b = engine.getBass();
    auto p = b.getPattern (b.getSelectedPattern());
    auto& s = p.steps[(size_t) i];
    s.note = juce::jlimit (-12, 24, s.note + semitones);
    s.on = true;
    b.setPattern (b.getSelectedPattern(), p);
    setParam (b.params, "sequencer", 1.0f);
    repaint();
}

void GrooveView::pressPattern (int i)
{
    if (! juce::isPositiveAndBelow (i, patternButtons.size()))
        return;
    switch (track)
    {
        case drums:
        {
            auto& project = engine.getMpc().getProject();
            while ((int) project.getSequences().size() <= i)
                project.addSequence();
            engine.playMpcSequence (i);
            drumBar = 0;
            break;
        }
        case bass:
            engine.getBass().selectPattern (i);
            setParam (engine.getBass().params, "sequencer", 1.0f);
            break;
        case synth:
        {
            auto& params = engine.getSynth().params;
            if (i == 0)
                setParam (params, "arpOn", 0.0f);
            else if (i <= (int) std::size (arpModes))
            {
                setParam (params, "arpMode", (float) (i - 1));
                setParam (params, "arpOn", 1.0f);
            }
            else
                setParam (params, "latch", getParam (params, "latch") > 0.5f ? 0.0f : 1.0f);
            break;
        }
        case vocoder:
        {
            auto& v = engine.getVocoder();
            for (int p = 0; p < v.getNumPrograms(); ++p)
                if (v.getProgramName (p) == vocoderSounds[i])
                    v.setCurrentProgram (p);
            engine.vocoderOn = true;
            break;
        }
        case sampler:
            if (i == 0 && onSample)
                onSample();
            else if (i == 1)
                engine.getSampler().stop();
            break;
        case loops:
        {
            auto& layer = engine.getLooper().getLayer (i);
            layer.mute = ! layer.mute.load();
            break;
        }
        default: break;
    }
    updateButtons();
    repaint();
}

void GrooveView::pressPart (int i)
{
    auto& scenes = engine.getScenes();
    auto& list = scenes.getScenes();
    if (juce::isPositiveAndBelow (i, (int) list.size()))
        scenes.launch (i);
    else if (i >= 0 && (int) list.size() < RigScenes::kMaxScenes)
    {
        // An empty part saves what's playing now, in the next free part.
        RigScene scene;
        scene.name = "Part " + juce::String::charToString ((juce::juce_wchar) ('A' + (int) list.size()));
        scene.state = engine.captureScene();
        list.push_back (scene);
    }
    updateButtons();
}

//==============================================================================
void GrooveView::timerCallback()
{
    if (! isShowing())
        return;
    for (auto* t : trackButtons)
        t->tick();
    updateButtons();

    // Follow the keys if they're sent somewhere else (the Keys play menu, a loaded show).
    const int keys = engine.getRouting().keysTo.load();
    if (destFor (track) >= 0 && keys != destFor (track))
        for (int t = 0; t < numTracks; ++t)
            if (destFor (t) == keys)
            {
                selectTrack (t);
                break;
            }

    juce::String sig;
    sig << track << "/" << playheadStep() << "/" << stateOf (track) << "/" << heldCell << "/" << drumPadIndex << "/" << drumBar;
    for (int i = 0; i < numSteps(); ++i)
    {
        const auto c = cellAt (i);
        sig << (c.lit ? 1 : 0) << (c.strong ? 1 : 0) << (c.dim ? 1 : 0) << c.label << c.sub;
    }
    for (int i = 0; i < laneCount(); ++i)
    {
        const auto c = laneCell (i);
        sig << (c.lit ? 1 : 0) << (c.dim ? 1 : 0) << c.label << c.sub;
    }
    if (track == loops)
        for (int i = 0; i < RigLooper::kLayers; ++i)
            sig << juce::roundToInt (engine.getLooper().getLayer (i).position.load() * 50.0);
    if (sig != shownSignature)
    {
        shownSignature = sig;
        repaint (centreArea);
    }
}

//==============================================================================
juce::Rectangle<float> GrooveView::stepRect (int i) const
{
    const int n = numSteps();
    const float gap = 6.0f, groupGap = n == 16 ? 10.0f : 0.0f;
    const float w = (stepArea.getWidth() - gap * (n - 1) - groupGap * 3) / (float) n;
    const float x = stepArea.getX() + i * (w + gap) + (n == 16 ? (i / 4) * groupGap : 0.0f);
    return { x, (float) stepArea.getY(), w, (float) stepArea.getHeight() };
}

juce::Rectangle<float> GrooveView::laneRect (int i) const
{
    auto r = stepRect (i);
    return { r.getX(), (float) laneArea.getY(), r.getWidth(), (float) laneArea.getHeight() };
}

int GrooveView::stepAt (juce::Point<int> p) const
{
    for (int i = 0; i < numSteps(); ++i)
        if (stepRect (i).contains (p.toFloat()))
            return i;
    return -1;
}

int GrooveView::laneAt (juce::Point<int> p) const
{
    for (int i = 0; i < laneCount(); ++i)
        if (laneRect (i).contains (p.toFloat()))
            return i;
    return -1;
}

void GrooveView::mouseDown (const juce::MouseEvent& e)
{
    if (const int lane = laneAt (e.getPosition()); lane >= 0)
    {
        if (track == bass)
        {
            // Drag a note up or down.
            const auto p = engine.getBass().getPattern (engine.getBass().getSelectedPattern());
            dragStep = lane;
            dragStartNote = p.steps[(size_t) lane].note;
            dragStartY = e.y;
            repaint();
        }
        else
            pressLane (lane);
        return;
    }
    if (const int step = stepAt (e.getPosition()); step >= 0)
    {
        if ((e.mods.isPopupMenu() || e.mods.isShiftDown()) && (track == bass || track == synth))
            toggleAccent (step);
        else
            pressStep (step);
    }
}

void GrooveView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStep < 0 || track != bass)
        return;
    const int wanted = juce::jlimit (-12, 24, dragStartNote + (dragStartY - e.y) / 8);
    const auto p = engine.getBass().getPattern (engine.getBass().getSelectedPattern());
    if (wanted != p.steps[(size_t) dragStep].note || ! p.steps[(size_t) dragStep].on)
        nudgeNote (dragStep, wanted - p.steps[(size_t) dragStep].note);
}

void GrooveView::mouseUp (const juce::MouseEvent&)
{
    if (heldCell >= 0)
        releaseCell (heldCell);
    dragStep = -1;
    repaint();
}

void GrooveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (track != bass)
        return;
    int i = laneAt (e.getPosition());
    if (i < 0)
        i = stepAt (e.getPosition());
    if (i >= 0 && std::abs (w.deltaY) > 0.0f)
        nudgeNote (i, w.deltaY > 0 ? 1 : -1);
}

//==============================================================================
void GrooveView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
    const auto colour = trackColour (track);

    // The middle's panel.
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (centreArea.toFloat(), 10.0f);
    g.setColour (colour.withAlpha (0.35f));
    g.drawRoundedRectangle (centreArea.toFloat().reduced (0.5f), 10.0f, 1.0f);

    // Header: the track, what it's doing, where the keys and knobs go.
    auto header = headerArea;
    header.removeFromRight (openButton.getWidth() + (micButton.isVisible() ? micButton.getWidth() + 8 : 0) + 12);
    g.setColour (colour);
    g.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    g.drawText (trackName (track), header.removeFromLeft (150), juce::Justification::centredLeft);
    auto lines = header;
    g.setColour (engine.getStrip (stripFor (track)).mute.load() ? Theme::record : Theme::text);
    g.setFont (juce::FontOptions (16.0f));
    g.drawFittedText (stateOf (track), lines.removeFromTop (lines.getHeight() / 2 + 4), juce::Justification::bottomLeft, 1);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.5f));
    const juce::String hint = [this]
    {
        switch (track)
        {
            case drums:   return juce::String ("Pick a sound above, then light the steps it plays on. The keys and pads play the drums.");
            case bass:    return juce::String ("Light steps to play. Drag a note up or down to change it. Right-click a step for an accent.");
            case synth:   return juce::String ("Light the steps the arp plays on. Pick how it plays below, then hold a chord.");
            case vocoder: return juce::String ("Switch the parts on and off. Pick a sound below, sing, and play the keys.");
            case sampler: return juce::String ("Press a pad to play your sound (or its slices). The keys play it too.");
            case loops:   return juce::String ("Press a layer to record from the next bar, again to stop. It plays in time with everything.");
            default:      return juce::String();
        }
    }();
    g.drawFittedText (hint, lines, juce::Justification::topLeft, 1);

    // The lane: the drum sounds, the bass notes, the synth accents.
    if (const int lanes = laneCount(); lanes > 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (track == drums ? "SOUND" : track == bass ? "NOTES  (drag)" : "ACCENTS",
                    laneArea.withY (laneArea.getY() - 16).withHeight (14), juce::Justification::centredLeft);
        for (int i = 0; i < lanes; ++i)
        {
            const auto c = laneCell (i);
            auto r = laneRect (i);
            g.setColour (c.lit ? colour.withAlpha (track == bass ? 0.3f : 0.9f) : Theme::background);
            g.fillRoundedRectangle (r, 5.0f);
            if (c.playing || (track == drums && c.lit))
            {
                g.setColour (Theme::text);
                g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.5f);
            }
            g.setColour (c.lit && track != bass ? Theme::background : c.dim ? Theme::textDim.withAlpha (0.6f) : Theme::text);
            if (track == drums)
            {
                auto t = r.reduced (4.0f, 3.0f);
                g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                g.drawText (c.label, t.removeFromTop (14.0f), juce::Justification::centredLeft);
                g.setFont (juce::FontOptions (10.5f));
                g.drawFittedText (c.sub, t.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
            }
            else
            {
                g.setFont (juce::FontOptions (track == bass ? 14.0f : 11.0f, juce::Font::bold));
                g.drawText (c.label, r, juce::Justification::centred);
            }
        }
    }
    else
    {
        // No lane: a word on what this track's cells are.
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (track == vocoder ? "PARTS" : track == sampler ? "PADS" : "LAYERS",
                    stepArea.withY (stepArea.getY() - 16).withHeight (14), juce::Justification::centredLeft);
    }

    // The steps.
    const int n = numSteps(), now = playheadStep();
    if (laneCount() > 0)
    {
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (track == drums ? "STEPS: " + padLabel (drumPad()) : juce::String ("STEPS"),
                    stepArea.withY (stepArea.getY() - 16).withHeight (14), juce::Justification::centredLeft);
    }
    for (int i = 0; i < n; ++i)
    {
        const auto c = cellAt (i);
        auto r = stepRect (i);
        const auto fill = track == loops && c.strong ? Theme::record : colour;
        g.setColour (Theme::background.withAlpha (c.dim && ! c.lit ? 0.5f : 1.0f));
        g.fillRoundedRectangle (r, 7.0f);
        if (c.lit)
        {
            float alpha = c.strong ? 1.0f : 0.72f;
            if (c.dim)
                alpha = 0.3f;
            g.setColour (fill.withAlpha (alpha));
            if (track == loops && engine.getLooper().getState (i) != RigLooper::State::empty)
            {
                // How far round the loop it is.
                const float pos = juce::jlimit (0.0f, 1.0f, (float) engine.getLooper().getLayer (i).position.load());
                g.setColour (fill.withAlpha (alpha * 0.35f));
                g.fillRoundedRectangle (r, 7.0f);
                g.setColour (fill.withAlpha (alpha));
                g.fillRoundedRectangle (r.withTop (r.getBottom() - juce::jmax (6.0f, r.getHeight() * pos)), 7.0f);
            }
            else
                g.fillRoundedRectangle (r, 7.0f);
        }
        if (c.playing)
        {
            g.setColour (Theme::text.withAlpha (0.3f));
            g.fillRoundedRectangle (r, 7.0f);
        }
        if (i == now)
        {
            g.setColour (Theme::text);
            g.drawRoundedRectangle (r.reduced (1.0f), 7.0f, 3.0f);
        }
        else
        {
            g.setColour (c.lit ? fill : Theme::outline);
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        }

        g.setColour (c.lit && ! c.dim ? Theme::background : Theme::textDim);
        const bool big = track == vocoder || track == loops;
        g.setFont (juce::FontOptions (big ? 20.0f : 14.0f, juce::Font::bold));
        auto text = r.reduced (6.0f, 8.0f).toNearestInt();
        g.drawText (c.label, text, big ? juce::Justification::centredTop : juce::Justification::centred);
        if (c.sub.isNotEmpty())
        {
            g.setFont (juce::FontOptions (big ? 13.0f : 10.0f));
            g.drawFittedText (c.sub, text, juce::Justification::centredBottom, 2);
        }
        if (c.strong && track != loops)
        {
            g.setColour (Theme::background);
            g.fillEllipse (r.getRight() - 12.0f, r.getY() + 6.0f, 6.0f, 6.0f);
        }
    }
    if (n == 16)
    {
        // Beat numbers under each group of four.
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (11.0f));
        for (int b = 0; b < 4; ++b)
        {
            auto r = stepRect (b * 4);
            g.drawText (juce::String (b + 1), r.withY (r.getBottom() + 2).withHeight (14).toNearestInt(), juce::Justification::centredLeft);
        }
    }

    // Knobs.
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (juce::String ("KNOBS 1-8") + (track == drums || track == sampler || track == loops ? "  (effects on this track)" : "")
                    + "   your keyboard's knobs turn these",
                knobArea.withHeight (14), juce::Justification::centredLeft);

    // Song parts.
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("SONG PARTS", partsArea.withWidth (90), juce::Justification::centredLeft);
}

void GrooveView::resized()
{
    auto area = getLocalBounds().reduced (kGap);

    partsArea = area.removeFromBottom (kPartsHeight - 14);
    {
        auto parts = partsArea;
        parts.removeFromLeft (90);
        const int w = (parts.getWidth() - 6 * 7) / 8;
        for (auto* b : partButtons)
        {
            b->setBounds (parts.removeFromLeft (w));
            parts.removeFromLeft (6);
        }
    }
    area.removeFromBottom (kGap);

    auto left = area.removeFromLeft (kTrackWidth);
    const int h = (left.getHeight() - 8 * (numTracks - 1)) / numTracks;
    for (auto* t : trackButtons)
    {
        t->setBounds (left.removeFromTop (h));
        left.removeFromTop (8);
    }
    area.removeFromLeft (kGap);

    centreArea = area;
    auto c = area.reduced (18, 14);
    headerArea = c.removeFromTop (56);
    {
        auto right = headerArea;
        openButton.setBounds (right.removeFromRight (150).withSizeKeepingCentre (150, 34));
        right.removeFromRight (8);
        if (micButton.isVisible())
            micButton.setBounds (right.removeFromRight (100).withSizeKeepingCentre (100, 34));
    }
    c.removeFromTop (12);

    // Knobs at the bottom, the pattern row above them, then the steps and the lane fill the rest.
    knobArea = c.removeFromBottom (juce::jlimit (96, 130, c.getHeight() / 4));
    {
        auto k = knobArea.withTrimmedTop (16);
        const int w = k.getWidth() / 8;
        for (auto& knob : knobs)
            knob->setBounds (k.removeFromLeft (w).reduced (6, 0));
    }
    c.removeFromBottom (12);
    auto patternRow = c.removeFromBottom (40);
    {
        auto row = patternRow;
        if (extraButton.isVisible())
        {
            extraButton.setBounds (row.removeFromRight (140));
            row.removeFromRight (16);
        }
        if (barButton.isVisible())
        {
            barButton.setBounds (row.removeFromRight (120));
            row.removeFromRight (16);
        }
        const int count = juce::jmax (1, patternButtons.size());
        const int w = juce::jmin (150, (row.getWidth() - 6 * (count - 1)) / count);
        for (auto* b : patternButtons)
        {
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
        }
    }
    c.removeFromBottom (24);

    const bool lane = track == drums || track == bass || track == synth;
    if (lane)
    {
        c.removeFromTop (16);
        laneArea = c.removeFromTop (track == drums ? 54 : track == bass ? 40 : 30);
        c.removeFromTop (14);
    }
    else
        laneArea = {};
    c.removeFromTop (16);
    stepArea = c.withHeight (juce::jmin (c.getHeight(), 240));
}
