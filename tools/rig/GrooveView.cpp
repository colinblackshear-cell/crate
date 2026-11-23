#include "GrooveView.h"
#include "StepMarks.h"
#include "core/ui/ToolIcons.h"
#include "RigColours.h"
#include "RigPanels.h"
#include "RigSounds.h"
#include "PluginSlot.h"

using vintage::Lit;
using vintage::Weight;

namespace
{
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

// A button on the dark well: lit amber when it's the one on, or another status colour.
void wellButton (juce::Button& b, Lit lit = Lit::current, float fontSize = 15.0f)
{
    vintage::setInWell (b, true);
    vintage::setLit (b, lit);
    b.getProperties().set ("fontSize", fontSize);
}

// The vocoder sounds on the Groove screen's pattern row: one of each kind.
const char* const vocoderSounds[] = { "Raw Voice", "Classic Robot", "French Touch", "Choir Pad",
                                      "Talk Box Funk", "Hard Tune", "Space Echo", "Monster" };

// The vocoder's module switches, as its chain bar has them.
const std::pair<const char*, const char*> vocoderSwitches[] = { { "robotOn", "Robot" }, { "voiceOn", "Voice" },
                                                                { "fxOn", "Effects" }, { "arpOn", "Arp" },
                                                                { "vintageOn", "Vintage" } };

// Under each part, its own knobs: how much of it first (its level, or how wet), then what
// shapes its sound. In the same order as vocoderSwitches.
struct PartKnob { const char* id; const char* name; const char* tip; };
const std::vector<PartKnob> vocoderPartKnobs[] = {
    { { "robotLevel", "Level", "How loud the robot is. With your voice on too, this is the robot's side of the dry/wet." },
      { "sharpness", "Beep", "Low is smooth and breathy; high rings like a machine." },
      { "bands", "Bands", "More bands make the words clearer; fewer sound older and rougher." },
      { "formant", "Character", "Down for giants and monsters, up for cartoons and chipmunks." } },
    { { "voiceLevel", "Level", "How loud your own voice is (dry). Robot level and this are the dry/wet." },
      { "tuneAmount", "Tune", "How far auto-tune pulls your voice onto the notes (when Tune is on)." },
      { "tuneSpeed", "Snap", "How fast auto-tune snaps: fast is the hard robotic step, slow sounds natural." },
      { "gateThreshold", "Gate", "Mutes the mic below this level, so the room's hum stays out between phrases." } },
    { { "reverbMix", "Reverb", "How much room: dry at 0, all reverb at the top." },
      { "delayMix", "Echo", "How loud the echoes are, in time with Kaleidobox." },
      { "chorusMix", "Chorus", "How much shimmer: a group of voices instead of one." },
      { "drive", "Drive", "Warms it up, then distorts it." } },
    { { "arpRate", "Speed", "How fast the notes (or the chops) go, in time with Kaleidobox." },
      { "arpGate", "Length", "Short and choppy, or long and smooth." },
      { "arpSwing", "Swing", "Pushes every other step late, for a shuffle." },
      { "arpOctaves", "Octaves", "How many octaves the note arp climbs." } },
    { { "vintageAmount", "Wet/dry", "How much of the old mic: your clean sound at 0, all old record at the top." },
      { "vintageEra", "Era", "Which old mic: a 1900s phonograph up to a 90s cassette, a phone or a megaphone." },
      { "vintageWear", "Wear", "The crackle and hiss of a worn record or tape." } },
};
// Each part's colour, as on the vocoder pedalboard.
const juce::uint32 vocoderPartColours[] = { 0xff5fb3c4, 0xfff1e7d1, 0xff9cc27a, 0xffe0816f, 0xffc9a26b };

const char* const arpModes[] = { "Up", "Down", "Up + down", "Random", "As played" };

struct KnobSpec { const char* id; const char* name; };
const KnobSpec bassKnobs[] = { { "cutoff", "Cutoff" }, { "resonance", "Reso" }, { "envMod", "Env" }, { "decay", "Decay" },
                               { "accent", "Accent" }, { "glide", "Glide" }, { "drive", "Drive" }, { "volume", "Level" } };
const KnobSpec synthKnobs[] = { { "cutoff", "Cutoff" }, { "resonance", "Reso" }, { "envMod", "Env" }, { "attack", "Attack" },
                                { "release", "Release" }, { "detune", "Detune" }, { "lfoDepth", "LFO" }, { "volume", "Level" } };
const KnobSpec machineKnobs[] = { { "kickTune", "Kick tune" }, { "kickDecay", "Boom" }, { "snareTone", "Snap" }, { "closedHatDecay", "Hat decay" },
                                  { "accent", "Accent" }, { "swing", "Swing" }, { "drive", "Drive" }, { "volume", "Level" } };
const KnobSpec vocoderKnobs[] = { { "robotLevel", "Robot" }, { "voiceLevel", "Voice" }, { "formant", "Formant" }, { "cutoff", "Cutoff" },
                                  { "resonance", "Reso" }, { "chorusMix", "Chorus" }, { "delayMix", "Delay" }, { "reverbMix", "Reverb" } };
const KnobSpec droneKnobs[] = { { "droneJawari", "Jawari" }, { "droneDecay", "Ring" }, { "dronePace", "Pace" }, { "droneWidth", "Width" },
                                 { "droneStrings", "Strings" }, { "droneReeds", "Reeds" }, { "droneResonate", "Resonate" }, { "droneFine", "Fine" } };
// The tracks without their own knobs get their mixer strip's effects.
const KnobSpec fxKnobs[] = { { "filter", "Filter" }, { "resonance", "Reso" }, { "drive", "Drive" }, { "crush", "Crush" },
                             { "delayMix", "Delay" }, { "feedback", "Feedback" }, { "reverbMix", "Reverb" }, { "reverbSize", "Room" } };

int destFor (int track)
{
    switch (track)
    {
        case GrooveView::drums:   return (int) RigRouting::Dest::mpc;
        case GrooveView::machine: return (int) RigRouting::Dest::drums;
        case GrooveView::bass:    return (int) RigRouting::Dest::bass;
        case GrooveView::synth:   return (int) RigRouting::Dest::synth;
        case GrooveView::vocoder: return (int) RigRouting::Dest::vocoder;
        case GrooveView::sampler: return (int) RigRouting::Dest::sampler;
        default:                  return -1;
    }
}

constexpr int kDrumStepTicks = seq::kTicksPerBeat / 4;   // 1/16
constexpr int kLabelWidth = 96, kTrackWidth = 292;

// A short name for a drum sound: "Kick" from "808 Kick 03.wav", else its pad.
juce::String shortSound (const juce::String& name, int pad)
{
    if (name.isEmpty())
        return padLabel (pad);
    for (auto* word : { "kick", "snare", "clap", "hat", "rim", "tom", "crash", "ride", "perc", "shaker", "cowbell", "conga", "snap", "808" })
        if (name.containsIgnoreCase (word))
            return juce::String (word);
    return name.upToFirstOccurrenceOf (".", false, false).substring (0, 9);
}
}

//==============================================================================
// A track down the left: a dark card with its colour stripe, its name and what it's playing,
// a lamp and a word for what it's doing, its own Play / Stop (On / Off, Rec), a mute and a solo.
class GrooveView::TrackCard : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    TrackCard (GrooveView& o, int t) : owner (o), index (t), colour (trackColour (t))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        for (auto* b : { &go, &mute, &solo })
        {
            wellButton (*b, b == &mute ? Lit::mute : b == &go ? (t == loops ? Lit::arm : Lit::play) : Lit::current, 20.0f);
            b->setClickingTogglesState (false);
            addAndMakeVisible (b);
        }
        go.getProperties().set ("fontSize", 15.0f);
        go.onClick = [this]
        {
            owner.pressTrackButton (index);
            owner.songHint();
            tick();
        };
        mute.setTooltip ("Mutes " + trackName (t) + ". Muted, it keeps going silently, so it comes back in time.");
        mute.onClick = [this]
        {
            auto& m = owner.engine.getStrip (stripFor (index)).mute;
            m = ! m.load();
            owner.engine.getDirty().authoredField (RigDirty::stripMute, stripFor (index));
            owner.songHint();
            tick();
        };
        solo.setTooltip ("Solo: hear only " + trackName (t) + ". Press again to bring the others back.");
        solo.onClick = [this] { owner.pressSolo (index); };
    }

    // What the lamp says.
    std::pair<juce::String, Lit> status() const
    {
        auto& engine = owner.engine;
        if (engine.getStrip (stripFor (index)).mute.load())
            return { "Muted", Lit::mute };
        if (index == vocoder && ! engine.vocoderOn.load())
            return { "Off", Lit::none };
        if (index == lineIn && ! owner.lineInLive())
            return { "Off", Lit::none };
        if (index == drone && ! owner.droneOn())
            return heard > 0.05f ? std::pair<juce::String, Lit> { "Ringing", Lit::play } : std::pair<juce::String, Lit> { "Off", Lit::none };
        if (index == loops)
            for (int l = 0; l < RigLooper::kLayers; ++l)
            {
                const auto st = engine.getLooper().getState (l);
                if (st == RigLooper::State::recording || st == RigLooper::State::overdubbing || st == RigLooper::State::armed)
                    return { "Rec", Lit::arm };
            }
        if (heard > 0.05f)
            return { "Playing", Lit::play };
        return { engine.getTransport().isPlaying() ? "Quiet" : "Ready", Lit::none };
    }

    void tick()
    {
        const float m = owner.meterOf ? owner.meterOf (stripFor (index)) : 0.0f;
        heard = m > 0.003f ? 1.0f : heard * 0.97f;
        const bool muted = owner.engine.getStrip (stripFor (index)).mute.load();
        mute.setToggleState (muted, juce::dontSendNotification);
        solo.setToggleState (owner.soloTrack == index, juce::dontSendNotification);
        go.setToggleState (owner.isGoing (index), juce::dontSendNotification);
        if (const auto text = owner.trackButtonText (index); go.getButtonText() != text)
        {
            go.setButtonText (text);
            go.setTooltip (owner.trackButtonTip (index));
        }
        const auto now = status();
        const auto detail = owner.detailOf (index);
        const bool selected = owner.track == index;
        if (now != shownStatus || detail != shownDetail || selected != shownSelected)
        {
            shownStatus = now;
            shownDetail = detail;
            shownSelected = selected;
            setTooltip ((detail.isNotEmpty() ? detail + ". " : juce::String()) + owner.stateOf (index));
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        // As on the phone's tool row: the picture on its tile, the name, a dot and a word.
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        vintage::drawToolCard (g, r, colour, railIcon (index), railName (index), shownStatus.first, shownStatus.second,
                               owner.track == index, isMouseOver (true), getWidth() - 4 - go.getX(),
                               getHeight() >= 74 ? shownDetail : juce::String());
    }

    // Too short for the name over the buttons: it goes on one line.
    bool compact() const { return getHeight() < 56; }

    void resized() override
    {
        // The card's own Play (On, Rec), M and S in a row on its right.
        auto r = getLocalBounds().reduced (4).withTrimmedRight (10);
        const int size = juce::jlimit (18, 28, r.getHeight() - 18);
        const float font = juce::jlimit (12.0f, 16.0f, size * 0.5f);
        for (auto* b : { &mute, &solo })
            b->getProperties().set ("fontSize", font);
        go.getProperties().set ("fontSize", juce::jmin (13.0f, font));
        auto row = r.withSizeKeepingCentre (r.getWidth(), size);
        solo.setBounds (row.removeFromRight (size));
        row.removeFromRight (5);
        mute.setBounds (row.removeFromRight (size));
        row.removeFromRight (5);
        go.setBounds (row.removeFromRight (juce::jmax (size + 6, 36)));
    }

    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked())
        {
            owner.selectTrack (index);
            if (e.getNumberOfClicks() >= 2 && owner.onTrackOpened)
                owner.onTrackOpened (index);   // the whole tool, in Edit
            else if (owner.onTrackPicked)
                owner.onTrackPicked (index);
        }
    }

    juce::TextButton go, mute { "M" }, solo { "S" };

private:
    GrooveView& owner;
    const int index;
    const juce::Colour colour;
    float heard = 0;
    std::pair<juce::String, Lit> shownStatus;
    juce::String shownDetail;
    bool shownSelected = false;
};

//==============================================================================
juce::String GrooveView::trackName (int t)
{
    switch (t)
    {
        case drums:   return "MPC";
        case machine: return "Drums";
        case bass:    return "Bass";
        case synth:   return "Synth";
        case vocoder: return "Vocoder";
        case sampler: return "Sampler";
        case loops:   return "Loops";
        case lineIn:  return "Line in";
        case drone:   return "Drone";
        default:      return {};
    }
}

juce::Colour GrooveView::trackColour (int t) { return RigColours::forStrip (stripFor (t)); }

juce::String GrooveView::railName (int t)
{
    return t == drums ? juce::String::fromUTF8 ("Pads \xc2\xb7 MPC") : trackName (t);
}

int GrooveView::railIcon (int t)
{
    using crate::toolicons::Tool;
    switch (t)
    {
        case drums:   return (int) Tool::pads;
        case machine: return (int) Tool::drums;
        case bass:    return (int) Tool::bass;
        case synth:   return (int) Tool::synth;
        case vocoder: return (int) Tool::vocoder;
        case sampler: return (int) Tool::sampler;
        case lineIn:  return (int) Tool::lineIn;
        case drone:   return (int) Tool::drone;
        default:      return (int) Tool::loops;
    }
}

int GrooveView::stripFor (int t)
{
    switch (t)
    {
        case drums:   return RigEngine::mpcStrip;
        case machine: return RigEngine::drumsStrip;
        case bass:    return RigEngine::bassStrip;
        case synth:   return RigEngine::synthStrip;
        case vocoder: return RigEngine::vocoderStrip;
        case sampler: return RigEngine::samplerStrip;
        case lineIn:  return RigEngine::inputStrip;
        case drone:   return RigEngine::droneStrip;
        default:      return RigEngine::loopsStrip;
    }
}

juce::String GrooveView::tabFor (int t)
{
    switch (t)
    {
        case drums:   return "MPC";
        case machine: return "Drum machine";
        case loops:   return "Looper";
        case lineIn:  return "Effects";   // its pedalboard, tuner and input
        default:    return trackName (t);
    }
}

//==============================================================================
GrooveView::GrooveView (RigEngine& e) : engine (e)
{
    setOpaque (true);
    for (int t = 0; t < numTracks; ++t)
        addAndMakeVisible (cards.add (new TrackCard (*this, t)));

    for (auto* b : { &openButton, &micButton, &extraButton, &barButton, &clearButton, &quantizeButton, &soundButton, &soundPrev, &soundNext,
                     &overdubButton, &undoButton, &redoButton })
    {
        wellButton (*b);
        addChildComponent (b);
    }
    // The picked loop layer's buttons.
    overdubButton.onClick = [this]
    {
        auto& looper = engine.getLooper();
        const auto st = looper.getState (pickedLayer);
        if (st == RigLooper::State::playing || st == RigLooper::State::overdubbing)
            looper.pressRecord (pickedLayer);
        say (st == RigLooper::State::overdubbing ? "Stopped recording on top of layer " + juce::String (pickedLayer + 1) + ". Undo takes it back."
                                                 : "Recording on top of layer " + juce::String (pickedLayer + 1) + " as it goes round. Press Overdub again to stop.");
        updateButtons();
    };
    undoButton.onClick = [this]
    {
        engine.getLooper().undo (pickedLayer);
        say ("Took back the last recording on top of layer " + juce::String (pickedLayer + 1) + ".");
        updateButtons();
    };
    redoButton.onClick = [this]
    {
        // Clear it, then record it again from the next bar (the clear goes first).
        auto& looper = engine.getLooper();
        looper.clear (pickedLayer);
        looper.pressRecord (pickedLayer);
        say ("Layer " + juce::String (pickedLayer + 1) + " records again from the next bar. Press the layer (or Redo's layer) to close the loop.");
        updateButtons();
    };
    overdubButton.setTooltip ("Records on top of the picked layer as it goes round, until you press it again. Undo takes it back.");
    undoButton.setTooltip ("Takes back the last recording on top of the picked layer.");
    redoButton.setTooltip ("Throws the picked layer away and records it again from the next bar: for trying until it's right.");
    soundButton.setTooltip ("The sound: click for every sound, by kind.");
    soundPrev.setTooltip ("The sound before.");
    soundNext.setTooltip ("The next sound.");
    soundButton.onClick = [this] { showSoundMenu(); };
    soundPrev.onClick = [this] { stepSound (-1); };
    soundNext.onClick = [this] { stepSound (1); };

    // Keys on the screen: click them, or click them once and play the computer's keys (A W S E D...).
    screenKeys.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour (0xfff1e7d1));
    screenKeys.setColour (juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour (0xff1e1a16));
    screenKeys.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, juce::Colour (0xff8a7f6c));
    screenKeys.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha (0.3f));
    screenKeys.setColour (juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, juce::Colour (0xff2a2622));
    screenKeys.setColour (juce::MidiKeyboardComponent::upDownButtonArrowColourId, juce::Colour (0xfff1e7d1));
    screenKeys.setAvailableRange (24, 96);
    screenKeys.setOctaveForMiddleC (4);
    screenKeys.setKeyPressBaseOctave (4);
    addChildComponent (screenKeys);
    keysState.addListener (this);
    openButton.setVisible (true);
    openButton.onClick = [this] { if (onOpen) onOpen (tabFor (track)); };

    micButton.setClickingTogglesState (true);
    vintage::setLit (micButton, Lit::arm);
    micButton.setTooltip ("The mic, for the vocoder. Use headphones with it on, or the speakers feed back into the mic.");
    micButton.onClick = [this] { engine.micOn = micButton.getToggleState(); };

    extraButton.setClickingTogglesState (true);
    vintage::setLit (extraButton, Lit::play);
    extraButton.onClick = [this]
    {
        if (track == bass || track == machine)
            pressPlay();
        else if (track == vocoder)
        {
            engine.vocoderOn = extraButton.getToggleState();
            engine.getDirty().authoredField (RigDirty::vocoderOn, 0);
        }
        else if (track == lineIn)
            setLineIn (extraButton.getToggleState());
        else if (track == drone)
            setDroneOn (extraButton.getToggleState());
        updateButtons();
    };

    barButton.setTooltip ("Which bar of the beat the steps show. Click for the next one.");
    barButton.onClick = [this]
    {
        if (track == machine)
        {
            machinePage = machinePage == 0 ? 1 : 0;
            updateButtons();
            repaint();
            return;
        }
        const auto& s = engine.getMpc().getProject().currentSequence();
        drumBar = (drumBar + 1) % juce::jmax (1, s.bars);
        updateButtons();
        repaint();
    };

    clearButton.onClick = [this]
    {
        if (canUndoClear())
            undoClear();
        else
            clearPattern();
    };
    quantizeButton.onClick = [this]
    {
        if (onQuantize)
            onQuantize (track == machine ? RigQuantizer::machine : track == bass ? RigQuantizer::bass
                        : track == synth ? RigQuantizer::synth : track == loops ? RigQuantizer::allLoops : RigQuantizer::mpc,
                        &quantizeButton);
    };

    // Pattern life: what a click on a lit step changes.
    {
        const char* names[] = { "Hits", "Chance", "Condition", "Nudge", "Pitch", "Decay", "Level" };
        const char* tips[] = { "A click lights or clears a step (as always).",
                               "A click on a lit step sets how often it plays: 100%, 75%, 50%, 25%, 10% of the times round (Shift goes back). "
                               "The beat breathes: never quite the same twice.",
                               "A click on a lit step sets which times round it plays: 1:2 the first of every two, 3:4 the third of every "
                               "four, Fill only in a fill, First only the first time after Play (Shift goes back).",
                               "A click on a lit step pushes it a little early or drags it a little late (part of a step), for a "
                               "human, swung or rushing feel (Shift goes back).",
                               "A click on a lit step gives that one hit its own pitch, in semitones over the sound's tune: one hand drum "
                               "plays a melody of tones (Shift goes back).",
                               "A click on a lit step makes that one hit ring longer (open) or shorter (muted) than the sound's Decay "
                               "(Shift goes back).",
                               "A click on a lit step sets how hard that one hit plays: ghost notes and strong strokes (Shift goes back)." };
        for (int m = 0; m < numLifeModes; ++m)
        {
            auto* b = lifeButtons.add (new juce::TextButton (names[m]));
            wellButton (*b, Lit::current, 14.0f);
            b->setClickingTogglesState (false);
            b->setTooltip (tips[m]);
            b->onClick = [this, m] { setLifeMode (m); };
            addChildComponent (b);
        }
        wellButton (mutateButton, Lit::current, 14.0f);
        mutateButton.setTooltip ("Changes the pattern a little (or a lot): moves, adds and drops a few hits, so it grows. Undo puts it back.");
        mutateButton.onClick = [this] { showMutateMenu(); };
        addChildComponent (mutateButton);
        wellButton (feelButton, Lit::current, 14.0f);
        feelButton.setTooltip ("A ready-made feel for the drum machine: 12/8 and 6/8 (three to a beat), triplets, shuffle, "
                               "or straight 16ths. Sets the step size, the pattern's length and the swing.");
        feelButton.onClick = [this] { showFeelMenu(); };
        addChildComponent (feelButton);
    }

    // The drum machine's bars (Bar 1 to 4 of up to 64 steps) and its step tools.
    for (int i = 0; i < 4; ++i)
    {
        auto& b = machineBars[(size_t) i];
        wellButton (b, Lit::current, 14.0f);
        b.setTooltip ("Show bar " + juce::String (i + 1) + " of the drum machine's steps (steps " + juce::String (i * 16 + 1) + " to " + juce::String (i * 16 + 16)
                      + "). A bar past the end of the pattern makes the pattern longer.");
        b.onClick = [this, i] { setMachinePage (i); };
        addChildComponent (b);
    }
    {
        static const StepTool tools[] = { StepTool::roll, StepTool::builds, StepTool::flam, StepTool::level };
        static const char* tips[] = { "ROLL: pick it, then click a step for a stutter roll of N quick hits (click the chip again to change N: 2, 3, 4, 6, 8). "
                                      "Click the same step again to take it back to one hit. Pick it again to put the tool down.",
                                      "BUILDS: a roll that gets louder with every hit (it uses the ROLL size). Pick it again to put it down.",
                                      "FLAM: a hit with a soft grace hit just before it. Pick it again to put it down.",
                                      "SOFT/LOUD: click a lit step to step its level: full, soft, very soft. Pick it again to put it down." };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = toolChips[(size_t) i];
            wellButton (b, Lit::current, 14.0f);
            b.setTooltip (tips[i]);
            b.onClick = [this, i]
            {
                if (tools[i] == StepTool::roll && machineToolOn (StepTool::roll))
                    setMachineRoll (crate::drums::steptools::nextRollChoice (machineRoll));
                else
                    setMachineTool (tools[i], ! machineToolOn (tools[i]));
            };
            addChildComponent (b);
        }
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
    keysState.removeListener (this);
}

// The screen's keys play the picked track, as a keyboard's keys do (the routing sends them there).
void GrooveView::handleNoteOn (juce::MidiKeyboardState*, int, int note, float velocity)
{
    engine.addMidi (juce::MidiMessage::noteOn (1, note, velocity));
}

void GrooveView::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    engine.addMidi (juce::MidiMessage::noteOff (1, note));
}

//==============================================================================
namespace
{
const std::vector<RigSounds::Sound>* soundsFor (int track)
{
    return track == GrooveView::bass ? &RigSounds::bass() : track == GrooveView::synth ? &RigSounds::synth() : nullptr;
}
}

PluginSlot* GrooveView::slotFor (int t) const
{
    return t == bass ? &engine.getBassPlugin() : t == synth ? &engine.getSynthPlugin() : nullptr;
}

int GrooveView::numSounds() const
{
    if (auto* slot = slotFor (track); slot != nullptr && slot->isPlaying())
        return slot->numPrograms();
    const auto* sounds = soundsFor (track);
    return sounds != nullptr ? (int) sounds->size() : 0;
}

juce::String GrooveView::soundName() const
{
    if (auto* slot = slotFor (track); slot != nullptr && slot->isPlaying())
    {
        const auto program = slot->numPrograms() > 1 ? slot->programName (slot->currentProgram()).trim() : juce::String();
        return slot->getName() + (program.isNotEmpty() ? ": " + program : juce::String());
    }
    if (track == bass)
        return RigSounds::nameOf (engine.getBass().params, RigSounds::bass());
    if (track == synth)
        return RigSounds::nameOf (engine.getSynth().params, RigSounds::synth());
    return {};
}

void GrooveView::pickSound (int index)
{
    const auto* sounds = soundsFor (track);
    if (sounds == nullptr)
        return;
    // A built-in sound puts the track's own synth back.
    if (auto* slot = slotFor (track); slot != nullptr && slot->isPlaying())
        slot->clear();
    auto& params = track == bass ? engine.getBass().params : engine.getSynth().params;
    const auto& sound = (*sounds)[(size_t) juce::jlimit (0, (int) sounds->size() - 1, index)];
    RigSounds::apply (params, sound);
    updateButtons();
    repaint();
}

void GrooveView::stepSound (int delta)
{
    if (auto* slot = slotFor (track); slot != nullptr && slot->isPlaying())
    {
        // A plugin: its own sounds.
        const int n = slot->numPrograms();
        if (n > 1)
            slot->setProgram ((slot->currentProgram() + delta + n) % n);
        updateButtons();
        return;
    }
    const auto* sounds = soundsFor (track);
    if (sounds == nullptr)
        return;
    const auto& params = track == bass ? engine.getBass().params : engine.getSynth().params;
    const int n = (int) sounds->size();
    pickSound ((RigSounds::indexOf (params, *sounds) + delta + n) % n);
}

void GrooveView::showSoundMenu()
{
    const auto* sounds = soundsFor (track);
    auto* slot = slotFor (track);
    if (sounds == nullptr || slot == nullptr)
        return;
    juce::SharedResourcePointer<PluginHost> host;
    const auto plugins = host->instruments();
    const bool external = slot->isPlaying();

    juce::PopupMenu menu;
    // The plugin's own sounds and window first, when there's one.
    if (external)
    {
        menu.addSectionHeader (slot->getName());
        menu.addItem (2001, "Open " + slot->getName() + "'s window");
        const int n = slot->numPrograms();
        if (n > 1)
        {
            juce::PopupMenu programs;
            for (int i = 0; i < juce::jmin (n, 512); ++i)
                programs.addItem (3000 + i, slot->programName (i).isNotEmpty() ? slot->programName (i) : "Sound " + juce::String (i + 1), true,
                                  i == slot->currentProgram());
            menu.addSubMenu (slot->getName() + "'s sounds", programs);
        }
        menu.addSeparator();
    }

    // The track's own sounds, by kind.
    menu.addSectionHeader ("Built-in " + trackName (track));
    juce::StringArray kinds;
    for (const auto& s : *sounds)
        kinds.addIfNotAlreadyThere (s.kind);
    const auto current = external ? juce::String() : soundName();
    for (const auto& kind : kinds)
    {
        juce::PopupMenu sub;
        bool ticked = false;
        for (int i = 0; i < (int) sounds->size(); ++i)
            if (kind == (*sounds)[(size_t) i].kind)
            {
                const bool here = current == (*sounds)[(size_t) i].name;
                ticked = ticked || here;
                sub.addItem (1 + i, (*sounds)[(size_t) i].name, true, here);
            }
        menu.addSubMenu (kind, sub, true, nullptr, ticked);
    }

    // Your plugins.
    menu.addSeparator();
    menu.addSectionHeader ("Your instrument plugins");
    if (plugins.empty())
        menu.addItem (-1, host->hasScanned() ? "No instrument plugins found" : "Arturia and the rest show here after a scan", false, false);
    else
        PluginMenu::addPlugins (menu, plugins, 5000);
    menu.addItem (2002, host->hasScanned() ? "Scan for plugins again" : "Scan for plugins (Arturia, Quake...)");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundButton),
                        [safe = juce::Component::SafePointer<GrooveView> (this), plugins, t = track] (int result)
                        {
                            if (safe == nullptr || result == 0 || safe->track != t)
                                return;
                            auto* s = safe->slotFor (t);
                            if (result == 2001)
                                s->showWindow();
                            else if (result == 2002)
                                PluginMenu::scan ([safe] (const juce::String& text) { if (safe != nullptr) safe->say (text); },
                                                  [safe] { if (safe != nullptr) safe->showSoundMenu(); });
                            else if (result >= 5000)
                            {
                                const auto d = plugins[(size_t) (result - 5000)];
                                safe->say ("Loading " + d.name + "...");
                                s->load (d, [safe, s, name = d.name] (const juce::String& error)
                                         {
                                             if (safe == nullptr)
                                                 return;
                                             safe->say (error.isEmpty() ? name + " plays the " + trackName (safe->track)
                                                                              + " track now: its steps, arp and keys. Its window is open."
                                                                        : name + " didn't load: " + error);
                                             if (error.isEmpty())
                                             {
                                                 s->bypassed = false;   // it may have been switched off on the Synth or Bass page
                                                 s->showWindow();
                                             }
                                             safe->updateButtons();
                                         });
                            }
                            else if (result >= 3000)
                            {
                                s->setProgram (result - 3000);
                                safe->updateButtons();
                            }
                            else
                                safe->pickSound (result - 1);
                        });
}

void GrooveView::say (const juce::String& text)
{
    if (onSay)
        onSay (text);
}

void GrooveView::songHint()
{
    // With the song in charge, what you switch on may not play: say why, and how to take over.
    if (engine.songInCharge())
        say ("The song is playing and decides which tracks play. Press Jam (top, in the song's window) to take over.");
}

void GrooveView::pressPlay()
{
    auto& params = track == machine ? engine.getDrums().params : engine.getBass().params;
    const bool on = getParam (params, "sequencer") > 0.5f;
    setParam (params, "sequencer", on ? 0.0f : 1.0f);
    // Play means you hear it: start the Rig if it's stopped.
    if (! on && ! engine.getTransport().isPlaying())
        engine.getTransport().play();
    updateButtons();
}

//==============================================================================
namespace
{
// The MPC's beat on or off, apart from Kaleidobox's Play (RigEngine::setMpcBeatOn). Until the
// engine has it, the MPC plays whenever Kaleidobox does, so its card's button is the clock's.
template <typename E, typename = void>
struct HasMpcBeat : std::false_type {};
template <typename E>
struct HasMpcBeat<E, std::void_t<decltype (std::declval<E&>().setMpcBeatOn (true)), decltype (std::declval<const E&>().isMpcBeatOn())>>
    : std::true_type {};

template <typename E>
bool mpcBeatOn (E& e)
{
    if constexpr (HasMpcBeat<E>::value)
        return e.isMpcBeatOn();
    else
        return true;
}

template <typename E>
void setMpcBeat (E& e, bool on)
{
    if constexpr (HasMpcBeat<E>::value)
        e.setMpcBeatOn (on);
    else if (! on)
        e.getTransport().stop();
}

int firstLayerIn (RigLooper& looper, std::initializer_list<RigLooper::State> states)
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        for (auto st : states)
            if (looper.getState (i) == st)
                return i;
    return -1;
}
}

bool GrooveView::isGoing (int t) const
{
    const bool clock = engine.getTransport().isPlaying();
    switch (t)
    {
        case drums:   return clock && mpcBeatOn (engine) && ! engine.getMpc().getProject().currentSequence().isEmpty();
        case machine: return clock && getParam (engine.getDrums().params, "sequencer") > 0.5f;
        case bass:    return clock && getParam (engine.getBass().params, "sequencer") > 0.5f;
        case synth:   return getParam (engine.getSynth().params, "arpOn") > 0.5f;
        case vocoder: return engine.vocoderOn.load();
        case sampler: return engine.getSampler().isPlaying();
        case loops:
            return firstLayerIn (engine.getLooper(),
                                 { RigLooper::State::armed, RigLooper::State::recording, RigLooper::State::overdubbing }) >= 0;
        case lineIn:  return lineInLive();
        case drone:   return droneOn();
        default:      return false;
    }
}

juce::String GrooveView::trackButtonText (int t) const
{
    const bool going = isGoing (t);
    switch (t)
    {
        case vocoder:
        case lineIn:
        case drone:   return going ? "On" : "Off";
        case loops:   return juce::String::fromUTF8 (going ? "\xe2\x96\xa0" : "\xe2\x97\x8f") + juce::String (" Rec");
        default:      return juce::String::fromUTF8 (going ? "\xe2\x96\xa0" : "\xe2\x96\xb6");
    }
}

juce::String GrooveView::trackButtonTip (int t) const
{
    switch (t)
    {
        case drums:   return "Plays or stops the MPC's beat (Kaleidobox starts if it's stopped). Its pads still play live while it's stopped.";
        case machine: return "Plays or stops the drum machine's pattern (Kaleidobox starts if it's stopped). The pads still play it live.";
        case bass:    return "Plays or stops the bass pattern (Kaleidobox starts if it's stopped). Stopped, it plays what you play on the keys.";
        case synth:   return "Turns the synth's arp on or off. On, it plays the chord you hold (Hold keeps it going).";
        case vocoder: return "Switches the vocoder on or off.";
        case sampler: return "Plays the Sampler's whole sound, or stops it.";
        case loops:   return "Records the next empty loop layer from the next bar (from its source); press again to close the loop.";
        case lineIn:  return "Switches Line in on or off: your guitar, keytar or second mic on your interface, through its pedals.";
        case drone:   return "Switches the drone on or off. It doesn't wait for Play: on, it sounds straight away.";
        default:      return {};
    }
}

void GrooveView::pressTrackButton (int t)
{
    if (! juce::isPositiveAndBelow (t, (int) numTracks))
        return;
    auto& transport = engine.getTransport();
    const bool going = isGoing (t);
    auto startClock = [&transport] { if (! transport.isPlaying()) transport.play(); };
    switch (t)
    {
        case drums:
            if (going)
            {
                setMpcBeat (engine, false);
                say ("The MPC's beat is stopped. Its pads still play live.");
            }
            else if (engine.getMpc().getProject().currentSequence().isEmpty())
                say ("The MPC has no beat yet: light some steps (pick MPC), or record one on the MPC page.");
            else
            {
                setMpcBeat (engine, true);
                startClock();
            }
            break;
        case machine:
        case bass:
        {
            auto& params = t == machine ? engine.getDrums().params : engine.getBass().params;
            setParam (params, "sequencer", going ? 0.0f : 1.0f);
            if (! going)
                startClock();
            break;
        }
        case synth:
            setParam (engine.getSynth().params, "arpOn", going ? 0.0f : 1.0f);
            if (! going)
                say ("The arp is on: hold a chord on the keys (Hold keeps it going after you let go).");
            break;
        case vocoder:
            engine.vocoderOn = ! going;
            engine.getDirty().authoredField (RigDirty::vocoderOn, 0);
            break;
        case sampler:
        {
            auto& s = engine.getSampler();
            if (going)
                s.stop();
            else if (auto audio = s.getDocument().getAudio(); audio != nullptr && audio->getNumSamples() > 0)
                s.play (audio, { 0, audio->getNumSamples() }, false);
            else
                say ("The Sampler is empty: press Sample (pick Sampler) or send it a sound from Sounds.");
            break;
        }
        case loops:
        {
            auto& looper = engine.getLooper();
            int layer = firstLayerIn (looper, { RigLooper::State::armed, RigLooper::State::recording, RigLooper::State::overdubbing });
            if (layer < 0)
                layer = firstLayerIn (looper, { RigLooper::State::empty });
            if (layer < 0)
            {
                say ("All sixteen loop layers have loops: clear one on the Looper, or start a new bank.");
                break;
            }
            looper.pressRecord (layer);
            if (! going)
            {
                startClock();
                say ("Layer " + juce::String (layer + 1) + " records from the next bar ("
                     + RigLooper::sourceName (looper.getLayer (layer).source.load()) + "). Press Rec again to close the loop.");
            }
            break;
        }
        case lineIn:
            setLineIn (! going);
            break;
        case drone:
            setDroneOn (! going);
            break;
        default: break;
    }
    updateButtons();
    repaint();
}

bool GrooveView::lineInLive() const
{
    return engine.inputOn.load() && engine.micOn.load();
}

void GrooveView::setLineIn (bool on)
{
    engine.inputOn = on;
    engine.getDirty().authoredField (RigDirty::inputOn, 0);
    if (on)
    {
        engine.micOn = true;   // Line in hears the interface only while the input is on
        say ("Line in on (" + RigEngine::inputJackName (engine.inputJack.load())
             + "): plug your guitar, keytar or a mic into your interface. Its pedals and tuner are on the Effects page.");
    }
    else
        say ("Line in is off.");
}

bool GrooveView::droneOn() const
{
    return engine.getDrone().isOn();
}

void GrooveView::setDroneOn (bool on)
{
    setParam (engine.getDrone().params, "droneOn", on ? 1.0f : 0.0f);
    say (on ? "The drone is on, on " + detailOf (drone).upToFirstOccurrenceOf (juce::String::fromUTF8 (" \xc2\xb7"), false, false)
                  + ". It doesn't wait for Play. Its Edit screen has its tuning and its strings that ring to a track."
            : juce::String ("The drone is off (its strings let go and ring out)."));
}

juce::String GrooveView::droneStringName (int string) const
{
    if (string == 0)
        return crate::drone::firstName (juce::roundToInt (getParam (engine.getDrone().params, "droneFirst")));
    return string == 3 ? "Low Sa" : "Sa";
}

// Records Line in onto the next empty loop layer, from the next bar.
void GrooveView::loopLineIn()
{
    auto& looper = engine.getLooper();
    if (const int going = firstLayerIn (looper, { RigLooper::State::armed, RigLooper::State::recording, RigLooper::State::overdubbing });
        going >= 0 && looper.getLayer (going).source.load() == RigLooper::line)
    {
        looper.pressRecord (going);   // closes the loop
        return;
    }
    const int layer = firstLayerIn (looper, { RigLooper::State::empty });
    if (layer < 0)
    {
        say ("All sixteen loop layers have loops: clear one on the Looper, or start a new bank.");
        return;
    }
    if (! lineInLive())
        setLineIn (true);
    looper.getLayer (layer).source = (int) RigLooper::line;
    looper.pressRecord (layer);
    if (! engine.getTransport().isPlaying())
        engine.getTransport().play();
    say ("Line in records onto loop layer " + juce::String (layer + 1) + " from the next bar. Press Loop it again to close the loop.");
}

void GrooveView::refresh()
{
    shownSignature = {};
    buildTrack();
    setFollowKnobs();
    for (auto* c : cards)
        c->tick();
    repaint();
}

juce::Array<juce::Component*> GrooveView::takeTrackCards()
{
    cardsElsewhere = true;
    juce::Array<juce::Component*> list;
    for (auto* c : cards)
    {
        removeChildComponent (c);
        list.add (c);
    }
    resized();
    repaint();
    return list;
}

juce::String GrooveView::trackHint() const
{
    switch (track)
    {
        case drums:   return juce::String ("Click a sound's name to change it");
        case machine: return juce::String ("Top row: accents. Pick the ROLL chip, then a step, for a roll; click a name to hear it");
        case bass:    return juce::String ("Drag a note up or down");
        case synth:   return juce::String ("Hold a chord; it plays on the lit steps");
        case vocoder: return juce::String ("Switch a part on, then set how much of it underneath");
        case sampler: return juce::String ("Press a pad to play it; the keys play it too");
        case loops:   return juce::String ("Empty layer: press to record. With sound: press to pick it, then use the buttons");
        case lineIn:  return juce::String ("Pick the input your instrument is plugged into");
        case drone:   return juce::String ("Click a string to pluck it; the sounds below are ready-made drones");
        default:      return juce::String();
    }
}

void GrooveView::selectTrack (int t)
{
    if (! juce::isPositiveAndBelow (t, (int) numTracks))
        return;
    track = t;
    // The keys play the track you're on (the loops have no notes, so the keys stay put).
    // (Following the screen: playback-like, so the dirty model keeps what you set.)
    auto& routing = engine.getRouting();
    if (const int dest = destFor (t); dest >= 0)
    {
        engine.getDirty().runtimeField (RigDirty::keysTo, 0, (float) routing.keysTo.load(), (float) dest);
        routing.keysTo = dest;
    }
    // The keyboard's drum pads play the drums you're on: the MPC's pads or the drum machine.
    if (t == drums || t == machine)
    {
        const int to = (int) (t == machine ? RigRouting::Dest::drums : RigRouting::Dest::mpc);
        engine.getDirty().runtimeField (RigDirty::padsTo, 0, (float) routing.padsTo.load(), (float) to);
        routing.padsTo = to;
    }
    heldCell = dragStep = -1;
    buildTrack();
    setFollowKnobs();
    for (auto* c : cards)
        c->tick();
    if (isShowing())   // (on Play; Edit shows the instrument itself)
        say (trackName (t) + ": " + trackHint() + ".");
    repaint();
}

void GrooveView::pressSolo (int t)
{
    if (! juce::isPositiveAndBelow (t, (int) numTracks))
        return;
    if (soloTrack == t)
    {
        // Bring everything back as it was.
        for (int st = 0; st < RigEngine::numStrips; ++st)
        {
            engine.getStrip (st).mute = mutesBeforeSolo[(size_t) st];
            engine.getDirty().authoredField (RigDirty::stripMute, st);
        }
        soloTrack = -1;
    }
    else
    {
        if (soloTrack < 0)
            for (int st = 0; st < RigEngine::numStrips; ++st)
                mutesBeforeSolo[(size_t) st] = engine.getStrip (st).mute.load();
        for (int other = 0; other < numTracks; ++other)
        {
            engine.getStrip (stripFor (other)).mute = other != t;
            engine.getDirty().authoredField (RigDirty::stripMute, stripFor (other));
        }
        soloTrack = t;
    }
    for (auto* c : cards)
        c->tick();
}

//==============================================================================
juce::AudioProcessorValueTreeState* GrooveView::paramsOf (int tool) const
{
    switch (tool)
    {
        case RigEngine::bassTool:    return &engine.getBass().params;
        case RigEngine::synthTool_:  return &engine.getSynth().params;
        case RigEngine::vocoderTool: return &engine.getVocoder().params;
        case RigEngine::drumsTool:   return &engine.getDrums().params;
        case RigEngine::droneTool:   return &engine.getDrone().params;
        default:
            if (RigEngine::isFxTool (tool))
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
        case machine: return { RigEngine::drumsTool, machineKnobs[index].id };
        case drone:   return { RigEngine::droneTool, droneKnobs[index].id };
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
    auto add = [this] (const juce::String& text, const juce::String& tip)
    {
        auto* b = patternButtons.add (new juce::TextButton (text));
        wellButton (*b, Lit::current, 15.0f);
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
        case machine:
            // The same 16 patterns, A / B / A+B and fill as the drum machine's own page.
            for (int i = 0; i < crate::drums::kPatterns; ++i)
                add (juce::String (i + 1), "Drum machine pattern " + juce::String (i + 1) + ". Playing, it changes at the next bar.");
            add ("A", "Play (and edit) the A variation, as on the drum machine.");
            add ("B", "Play (and edit) the B variation.");
            add ("A+B", "Play A then B, back and forth: a two-bar beat. Press again to switch which one the steps edit.");
            add ("Fill", "Plays the fill once, the next time the beat comes round (pick the fill pattern on the drum machine's page).");
            vintage::setLit (*patternButtons.getLast(), Lit::next);
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
            add ("Slower", "The arp goes slower (down to a note every 8 bars). More arp settings on the Synth page.");
            add ("Faster", "The arp goes faster (up to 1/32 triplets).");
            break;
        case vocoder:
            for (auto* name : vocoderSounds)
                add (name, "Vocoder sound: " + juce::String (name) + ". Changed its knobs and want it back? Press it again: it comes back as it was.");
            break;
        case sampler:
            add ("Sample", "Records the mic, a tool or the mix (as set on the Setup page) into the Sampler, from the next bar.");
            vintage::setLit (*patternButtons.getLast(), Lit::arm);
            add ("Stop sound", "Stops the Sampler's sound.");
            break;
        case loops:
            for (int i = 0; i < RigLooper::kLayers; ++i)
                add ("Mute " + juce::String (i + 1), "Mutes loop layer " + juce::String (i + 1) + ". It keeps going round, so it comes back in time.");
            for (auto* b : patternButtons)
                vintage::setLit (*b, Lit::mute);
            break;
        case lineIn:
            add ("Loop it", "Records Line in onto the next empty loop layer, from the next bar. Press again to close the loop.");
            vintage::setLit (*patternButtons.getLast(), Lit::arm);
            add ("Pedals", "Line in's pedalboard and tuner, on the Effects page.");
            break;
        case drone:
        {
            // Its ready-made drones (the note, On and what the strings ring to stay as they are).
            const auto names = DroneProcessor::presetNames();
            for (const auto& name : names)
                add (name, "A ready-made drone: " + name + ". It sets the strings, the jawari, the pace and the reeds; "
                           "the note and On stay as they are.");
            break;
        }
        default: break;
    }

    knobs.clear();
    for (int i = 0; i < 8; ++i)
    {
        const auto [tool, id] = knobFor (i);
        auto* params = paramsOf (tool);
        if (params == nullptr || params->getParameter (id) == nullptr)
            continue;
        const auto* specs = track == bass ? bassKnobs : track == synth ? synthKnobs : track == vocoder ? vocoderKnobs
                            : track == machine ? machineKnobs : track == drone ? droneKnobs : fxKnobs;
        auto knob = std::make_unique<vintage::Knob> (*params, id, specs[i].name, colour);
        knob->slider.setTooltip (juce::String (specs[i].name) + ". Knob " + juce::String (i + 1)
                                 + " on your keyboard turns it too. Drag up or down; double-click for the default.");
        if (track == vocoder)
        {
            // The vocoder's settings have no units of their own: keep the numbers short.
            knob->slider.textFromValueFunction = [] (double v) { return juce::String (v, std::abs (v) >= 100.0 ? 0 : 2); };
            knob->slider.updateText();
        }
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    partKnobs.clear();
    partOfKnob.clear();
    if (track == vocoder)
    {
        auto& params = engine.getVocoder().params;
        for (int part = 0; part < (int) std::size (vocoderPartKnobs); ++part)
            for (const auto& spec : vocoderPartKnobs[part])
            {
                auto* param = params.getParameter (spec.id);
                if (param == nullptr)
                    continue;
                auto knob = std::make_unique<vintage::Knob> (params, spec.id, spec.name, juce::Colour (vocoderPartColours[part]));
                knob->inWell = true;
                knob->nameSize = 13.0f;
                // An era's name is long ("1940s Ribbon"): its year fits the box.
                const bool era = juce::String (spec.id) == "vintageEra";
                knob->slider.textFromValueFunction = [param, era] (double v)
                {
                    const auto text = vintage::readable (param, v);
                    return era ? text.upToFirstOccurrenceOf (" ", false, false) : text;
                };
                knob->slider.updateText();
                knob->slider.setTooltip (juce::String (vocoderSwitches[part].second) + ": " + spec.tip
                                         + " Drag up or down; double-click for the default.");
                // Reaching for Effects or Vintage while it's off switches it on, so you hear the knob.
                knob->slider.onDragStart = [this, part]
                {
                    auto& p = engine.getVocoder().params;
                    const char* id = vocoderSwitches[part].first;
                    if ((juce::String (id) == "fxOn" || juce::String (id) == "vintageOn") && getParam (p, id) < 0.5f)
                    {
                        setParam (p, id, 1.0f);
                        engine.vocoderOn = true;
                        say (juce::String (vocoderSwitches[part].second) + " is on, so you hear it.");
                    }
                };
                addAndMakeVisible (*knob);
                partKnobs.push_back (std::move (knob));
                partOfKnob.push_back (part);
            }
    }

    openButton.setButtonText ("Edit " + trackName (track) + juce::String::fromUTF8 (" \xe2\x80\xba"));
    openButton.setTooltip ("Everything " + trackName (track) + " can do, in full (Edit, F2).");
    micButton.setVisible (track == vocoder && showMic);
    extraButton.setVisible (track == bass || track == vocoder || track == machine || track == lineIn || track == drone);
    for (auto* b : { &soundButton, &soundPrev, &soundNext })
        b->setVisible (hasKeys());
    screenKeys.setVisible (hasKeys());
    keysState.allNotesOff (1);
    barButton.setVisible (track == drums);
    clearButton.setVisible (track == drums || track == bass || track == synth || track == machine || track == loops);
    for (auto* b : { &overdubButton, &undoButton, &redoButton })
        b->setVisible (track == loops);
    quantizeButton.setVisible (track == drums || track == bass || track == synth || track == machine || track == loops);
    quantizeButton.setTooltip (track == drums ? "Quantize the MPC's beat: pull its notes onto a grid (Cmd+U does it straight away)."
                               : track == loops ? "Quantize the loops: slide each hit onto the beat (Cmd+U does it straight away)."
                               : "Quantize the pattern onto a coarser or triplet grid (Cmd+U does it straight away).");
    clearButton.setTooltip (track == drums ? "Clears the shown bar of the beat, for the four sounds here."
                            : track == machine ? "Clears the drum machine's pattern (every sound, not just these rows)."
                                           : track == bass ? "Clears the bass pattern's steps."
                                           : track == loops ? "Clears the picked loop layer (it goes quiet and empty, ready to record again)."
                                                            : "Lights every arp step again, no accents.");
    drumBar = 0;
    updateButtons();
    resized();
}

// Lights the buttons to match what's on.
void GrooveView::updateButtons()
{
    auto setLit = [] (juce::Button* b, bool on, Lit lit)
    {
        const bool changed = (int) b->getProperties()["lit"] != (int) lit || b->getToggleState() != on;
        vintage::setLit (*b, lit);
        if (b->getToggleState() != on)
            b->setToggleState (on, juce::dontSendNotification);
        if (changed)
            b->repaint();
    };
    const bool pulse = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;
    for (int m = 0; m < lifeButtons.size(); ++m)
    {
        const bool shown = (track == machine) || (track == bass && m != lifeNudge && m < lifePitch);
        lifeButtons[m]->setVisible (shown);
        setLit (lifeButtons[m], lifeMode == m, Lit::current);
    }
    mutateButton.setVisible (hasLife());
    feelButton.setVisible (track == machine);
    if (track != machine)
        for (int i = 0; i < 4; ++i)
        {
            machineBars[(size_t) i].setVisible (false);
            toolChips[(size_t) i].setVisible (false);
        }
    if (track != loops)
    {
        const auto text = canUndoClear() ? juce::String ("Undo clear") : juce::String ("Clear");
        if (clearButton.getButtonText() != text)
        {
            clearButton.setButtonText (text);
            clearButton.setEnabled (true);
        }
    }
    if (hasKeys())
    {
        const auto text = soundName() + juce::String::fromUTF8 (" \xe2\x96\xbe");
        if (soundButton.getButtonText() != text)
            soundButton.setButtonText (text);
    }
    switch (track)
    {
        case drums:
        {
            auto& mpc = engine.getMpc();
            const int now = mpc.getProject().getCurrentSequenceIndex(), queued = mpc.getQueuedSequence();
            for (int i = 0; i < patternButtons.size(); ++i)
                setLit (patternButtons[i], i == now || (i == queued && pulse), i == now ? Lit::current : Lit::next);
            const auto& s = mpc.getProject().currentSequence();
            drumBar = juce::jlimit (0, juce::jmax (0, s.bars - 1), drumBar);
            barButton.setButtonText ("Bar " + juce::String (drumBar + 1) + " of " + juce::String (s.bars));
            break;
        }
        case machine:
        {
            auto& d = engine.getDrums();
            const int selected = d.getSelectedPattern(), playing = d.getPlayingPattern();
            for (int i = 0; i < juce::jmin (crate::drums::kPatterns, patternButtons.size()); ++i)
                setLit (patternButtons[i], i == selected || (i == playing && pulse), i == selected ? Lit::current : Lit::next);
            for (int v = 0; v < 3; ++v)
                if (auto* b = patternButtons[crate::drums::kPatterns + v])
                    setLit (b, d.getVariation() == v, Lit::current);
            if (auto* fill = patternButtons.getLast())
                setLit (fill, d.isPlayingFill(), Lit::next);
            const bool on = getParam (d.params, "sequencer") > 0.5f;
            extraButton.setToggleState (on, juce::dontSendNotification);
            extraButton.setButtonText (juce::String::fromUTF8 (on ? "\xe2\x96\xa0 Stop" : "\xe2\x96\xb6 Play"));
            extraButton.setTooltip ("Play: the drum machine plays its pattern (and Kaleidobox starts if it's stopped). Stop: only the pads and keys play it.");
            const int length = machineLength();
            machinePage = juce::jlimit (0, 3, machinePage);
            for (int i = 0; i < 4; ++i)
            {
                machineBars[(size_t) i].setVisible (true);
                setLit (&machineBars[(size_t) i], i == machinePage, Lit::current);
                machineBars[(size_t) i].setAlpha (i * 16 < length ? 1.0f : 0.55f);
            }
            for (int i = 0; i < 4; ++i)
            {
                static const StepTool tools[] = { StepTool::roll, StepTool::builds, StepTool::flam, StepTool::level };
                toolChips[(size_t) i].setVisible (lifeMode == lifeHits);
                setLit (&toolChips[(size_t) i], machineToolOn (tools[i]), Lit::current);
            }
            toolChips[0].setButtonText (juce::String ("Roll ") + juce::String::fromUTF8 ("\xc3\x97") + juce::String (machineRoll));
            break;
        }
        case bass:
        {
            const int selected = engine.getBass().getSelectedPattern();
            for (int i = 0; i < patternButtons.size(); ++i)
                setLit (patternButtons[i], i == selected, Lit::current);
            const bool on = getParam (engine.getBass().params, "sequencer") > 0.5f;
            extraButton.setToggleState (on, juce::dontSendNotification);
            extraButton.setButtonText (juce::String::fromUTF8 (on ? "\xe2\x96\xa0 Stop" : "\xe2\x96\xb6 Play"));
            extraButton.setTooltip ("Play: the bass plays its pattern (and Kaleidobox starts if it's stopped). Stop: it only plays what you play on the keys.");
            break;
        }
        case synth:
        {
            auto& params = engine.getSynth().params;
            const bool arp = getParam (params, "arpOn") > 0.5f;
            const int mode = (int) getParam (params, "arpMode");
            const int modes = 1 + (int) std::size (arpModes);
            for (int i = 0; i < juce::jmin (modes, patternButtons.size()); ++i)
                setLit (patternButtons[i], i == 0 ? ! arp : arp && mode == i - 1, Lit::current);
            if (auto* hold = patternButtons[modes])
                setLit (hold, getParam (params, "latch") > 0.5f, Lit::play);
            for (int i = modes + 1; i < patternButtons.size(); ++i)
                setLit (patternButtons[i], false, Lit::current);
            break;
        }
        case vocoder:
        {
            auto& v = engine.getVocoder();
            const auto current = v.getProgramName (v.getCurrentProgram());
            for (int i = 0; i < patternButtons.size(); ++i)
                setLit (patternButtons[i], current == vocoderSounds[i], Lit::current);
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
                setLit (b, recording, Lit::arm);
                b->setButtonText (recording ? "Stop sampling" : "Sample");
            }
            break;
        }
        case loops:
        {
            auto& looper = engine.getLooper();
            for (int i = 0; i < patternButtons.size(); ++i)
                setLit (patternButtons[i], looper.getLayer (i).mute.load(), Lit::mute);
            const auto st = looper.getState (pickedLayer);
            const bool hasSound = st == RigLooper::State::playing || st == RigLooper::State::overdubbing;
            const auto n = juce::String (pickedLayer + 1);
            auto label = [] (juce::TextButton& b, const juce::String& text)
            {
                if (b.getButtonText() != text)
                    b.setButtonText (text);
            };
            label (overdubButton, (st == RigLooper::State::overdubbing ? "Stop " : "Overdub ") + n);
            setLit (&overdubButton, st == RigLooper::State::overdubbing, Lit::arm);
            overdubButton.setEnabled (hasSound);
            label (undoButton, "Undo " + n);
            undoButton.setEnabled (looper.canUndo (pickedLayer) && st == RigLooper::State::playing);
            label (clearButton, "Clear " + n);
            clearButton.setEnabled (st != RigLooper::State::empty);
            label (redoButton, "Redo " + n);
            redoButton.setEnabled (hasSound);
            break;
        }
        case lineIn:
        {
            const bool on = lineInLive();
            extraButton.setToggleState (on, juce::dontSendNotification);
            extraButton.setButtonText (on ? "Line in on" : "Line in off");
            extraButton.setTooltip ("Turns Line in on or off: your guitar, keytar or a mic on your interface. Use headphones with a mic.");
            auto& looper = engine.getLooper();
            const int rec = firstLayerIn (looper, { RigLooper::State::armed, RigLooper::State::recording, RigLooper::State::overdubbing });
            if (auto* b = patternButtons[0])
            {
                const bool looping = rec >= 0 && looper.getLayer (rec).source.load() == RigLooper::line;
                setLit (b, looping, Lit::arm);
                b->setButtonText (looping ? "Close the loop" : "Loop it");
            }
            break;
        }
        case drone:
        {
            const bool on = droneOn();
            extraButton.setToggleState (on, juce::dontSendNotification);
            extraButton.setButtonText (on ? "Drone on" : "Drone off");
            extraButton.setTooltip ("Turns the drone on or off. It doesn't wait for Play: on, it sounds straight away.");
            break;
        }
        default: break;
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
        case machine:
        {
            auto& d = engine.getDrums();
            const auto name = "Pattern " + juce::String (d.getSelectedPattern() + 1) + (d.editVariation == 1 ? "B" : "A");
            if (getParam (d.params, "sequencer") < 0.5f)
                return "Pattern off: press Play, or play the pads";
            if (machinePattern().isEmpty())
                return name + " is empty: light some steps";
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
        case lineIn:
            if (! lineInLive())
                return "Off: press On, then play your guitar or keytar";
            return "Listening on " + RigEngine::inputJackName (engine.inputJack.load()) + ": play, or Loop it";
        case drone:
        {
            auto& d = engine.getDrone();
            const auto note = DroneProcessor::rootNames()[juce::jlimit (0, 11, juce::roundToInt (getParam (d.params, "droneRoot")))];
            if (! d.isOn())
                return getParam (d.params, "droneResonate") > 0.001f ? "Off; its strings ring to " + DroneProcessor::feedNames()[d.getFeed()]
                                                                    : juce::String ("Off: press On for a drone on ") + note;
            return "Droning on " + note + (getParam (d.params, "droneSync") > 0.5f ? ", plucked in time" : juce::String());
        }
        default: return {};
    }
}

// The few words on a track's card: its kit, pattern, mode or sound.
juce::String GrooveView::detailOf (int t) const
{
    switch (t)
    {
        case drums:
        {
            auto& project = engine.getMpc().getProject();
            const auto kit = project.currentBank().name;
            return kit.isNotEmpty() ? kit : "Beat " + juce::String (project.getCurrentSequenceIndex() + 1);
        }
        case machine:
            return getParam (engine.getDrums().params, "sequencer") > 0.5f
                       ? "Pattern " + juce::String (engine.getDrums().getSelectedPattern() + 1) + (engine.getDrums().editVariation == 1 ? "B" : "A")
                       : juce::String ("Pads");
        case bass:
            return getParam (engine.getBass().params, "sequencer") > 0.5f
                       ? "Pattern " + juce::String (engine.getBass().getSelectedPattern() + 1) : juce::String ("Keys");
        case synth:
        {
            auto& params = engine.getSynth().params;
            if (getParam (params, "arpOn") < 0.5f)
                return "Keys";
            // The mode (as the parameter names it, so the newer modes read right) and the speed.
            juce::String mode = "Up";
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (params.getParameter ("arpMode")))
                mode = choice->getCurrentChoiceName();
            const auto speed = getParam (params, "arpSync") > 0.5f ? crate::synth::ArpRates::names()[juce::roundToInt (getParam (params, "arpSpeed"))]
                                                                   : juce::String (getParam (params, "arpHz"), 1) + " Hz";
            return "Arp " + mode + " " + speed;
        }
        case vocoder:
        {
            auto& v = engine.getVocoder();
            return v.getProgramName (v.getCurrentProgram());
        }
        case sampler:
        {
            auto& doc = engine.getSampler().getDocument();
            return doc.hasAudio() ? (doc.getName().isNotEmpty() ? doc.getName() : juce::String ("A sound")) : juce::String ("Empty");
        }
        case loops:
        {
            int used = 0;
            for (int i = 0; i < RigLooper::kLayers; ++i)
                used += engine.getLooper().getState (i) != RigLooper::State::empty ? 1 : 0;
            return juce::String (used) + " of " + juce::String (RigLooper::kLayers) + " layers";
        }
        case lineIn:
            return lineInLive() ? RigEngine::inputJackName (engine.inputJack.load()) : juce::String ("Off");
        case drone:
        {
            auto& d = engine.getDrone();
            if (! d.isOn())
                return "Off";
            const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
            return DroneProcessor::rootNames()[juce::jlimit (0, 11, juce::roundToInt (getParam (d.params, "droneRoot")))] + dot
                   + droneStringName (0) + dot + "On";
        }
        default: return {};
    }
}

juce::String GrooveView::patternText() const
{
    switch (track)
    {
        case drums:
        {
            auto& project = engine.getMpc().getProject();
            return "BEAT " + juce::String (project.getCurrentSequenceIndex() + 1) + "/" + juce::String ((int) project.getSequences().size());
        }
        case machine:
            return engine.getDrums().isPlayingFill() ? juce::String ("FILL")
                                                     : "PATTERN " + juce::String (engine.getDrums().getSelectedPattern() + 1)
                                                           + (engine.getDrums().editVariation == 1 ? "B" : "A") + "/" + juce::String (crate::drums::kPatterns);
        case bass:
            return "PATTERN " + juce::String (engine.getBass().getSelectedPattern() + 1) + "/" + juce::String (crate::bass::kPatterns);
        case synth:
            return getParam (engine.getSynth().params, "arpOn") > 0.5f ? detailOf (synth).toUpperCase() : juce::String ("ARP OFF");
        case vocoder:
            return detailOf (vocoder).toUpperCase();
        case sampler:
            return engine.getSampler().getDocument().hasAudio() ? juce::String ("LOADED") : juce::String ("EMPTY");
        case loops:
            return detailOf (loops).toUpperCase();
        case lineIn:
            return detailOf (lineIn).toUpperCase();
        case drone:
        {
            const auto notes = engine.getDrone().stringNotes();
            return (droneOn() ? "SA " + notes[1] : juce::String ("OFF")).toUpperCase();
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
        case lineIn:  return 3;   // both inputs, input 1, input 2
        case drone:   return crate::drone::kStrings;
        default:      return 16;
    }
}

int GrooveView::numRows() const
{
    switch (track)
    {
        case drums: return kDrumRows;
        case machine: return kMachineRows;
        case bass:  return 4;
        case synth: return 2;
        default:    return 1;
    }
}

int GrooveView::drumPad (int row) const
{
    const int first = engine.getRouting().padBank.load() * kPadsPerBank;
    if (juce::isPositiveAndBelow (row, kDrumRows) && drumRows[(size_t) row] >= 0)
        return first + drumRows[(size_t) row];

    // Not picked: the kit's sounds in pad order, then the empty pads.
    const auto& pads = engine.getMpc().getProject().currentBank().pads;
    std::vector<int> order;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < kPadsPerBank; ++i)
            if (pads[(size_t) (first + i)].isEmpty() == (pass == 1))
                order.push_back (i);
    // Leave out the ones other rows picked.
    int n = 0;
    for (int i : order)
    {
        if (std::find (drumRows.begin(), drumRows.end(), i) != drumRows.end())
            continue;
        int autoRow = 0;
        for (int r = 0; r < row; ++r)
            autoRow += drumRows[(size_t) r] < 0 ? 1 : 0;
        if (n == autoRow)
            return first + i;
        ++n;
    }
    return first + juce::jlimit (0, kPadsPerBank - 1, row);
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

void GrooveView::clearPattern()
{
    // Keep what's there first, so the Clear can be taken back.
    clearUndo = {};
    switch (track)
    {
        case drums:
            clearUndo.index = engine.getMpc().getProject().getCurrentSequenceIndex();
            clearUndo.mpcTracks = engine.getMpc().getProject().currentSequence().tracks;
            break;
        case machine:
            clearUndo.index = engine.getDrums().getSelectedPattern();
            clearUndo.variation = engine.getDrums().editVariation;
            clearUndo.machine = machinePattern();
            break;
        case bass:
            clearUndo.index = engine.getBass().getSelectedPattern();
            clearUndo.bass = engine.getBass().getPattern (clearUndo.index);
            break;
        case synth:
            clearUndo.synth = engine.getSynth().getRhythm();
            break;
        default: break;
    }
    if (track == drums || track == machine || track == bass || track == synth)
    {
        clearUndo.track = track;
        say ("Cleared. Changed your mind? Undo clear (the same button), or Cmd+Z.");
    }
    switch (track)
    {
        case loops:
            engine.getLooper().clear (pickedLayer);
            say ("Cleared loop layer " + juce::String (pickedLayer + 1) + ". Press it to record it again from the next bar.");
            break;
        case drums:
            for (int row = 0; row < kDrumRows; ++row)
                for (int step = 0; step < numSteps(); ++step)
                    if (drumHit (drumPad (row), step))
                        setDrumHit (drumPad (row), step, false);
            break;
        case machine:
        {
            auto p = machinePattern();
            p.hits = {};
            p.accent = {};
            setMachinePattern (p);
            break;
        }
        case bass:
        {
            auto& b = engine.getBass();
            auto p = b.getPattern (b.getSelectedPattern());
            for (auto& st : p.steps)
                st = {};
            b.setPattern (b.getSelectedPattern(), p);
            break;
        }
        case synth:
            engine.getSynth().setRhythm ({});
            break;
        default: break;
    }
    repaint();
}

bool GrooveView::undoClear()
{
    if (! canUndoClear())
        return false;
    const auto u = std::exchange (clearUndo, {});
    switch (u.track)
    {
        case drums:
        {
            auto& mpc = engine.getMpc();
            auto& seqs = mpc.getProject().getSequences();
            if (juce::isPositiveAndBelow (u.index, (int) seqs.size()))
            {
                mpc.beginEdit();
                seqs[(size_t) u.index].tracks = u.mpcTracks;
                mpc.getProject().sequenceChanged();
            }
            break;
        }
        case machine:
            engine.getDrums().setPattern (u.index, u.machine, u.variation);
            break;
        case bass:
            engine.getBass().setPattern (u.index, u.bass);
            break;
        case synth:
            engine.getSynth().setRhythm (u.synth);
            break;
        default: return false;
    }
    say ("Put back what Clear took away.");
    updateButtons();
    repaint();
    return true;
}

juce::String GrooveView::rowName (int row) const
{
    switch (track)
    {
        case drums:
        {
            const int pad = drumPad (row);
            const auto& p = engine.getMpc().getProject().currentBank().pads[(size_t) pad];
            return shortSound (p.isEmpty() ? juce::String() : p.name, pad);
        }
        case machine: return row == 0 ? juce::String ("Accent") : juce::String (crate::drums::voiceName (machineVoice (juce::jlimit (1, kMachineRows - 1, row))));
        case bass:  { const char* names[] = { "Steps", "Note", "Accent", "Slide" }; return names[juce::jlimit (0, 3, row)]; }
        case synth: return row == 0 ? "Arp" : "Accent";
        default:    return {};
    }
}

GrooveView::Cell GrooveView::cellAt (int row, int i) const
{
    Cell c;
    switch (track)
    {
        case drums:
        {
            const int pad = drumPad (row);
            c.lit = drumHit (pad, i);
            c.dim = engine.getMpc().getProject().currentBank().pads[(size_t) pad].isEmpty();
            break;
        }
        case machine:
        {
            const auto p = machinePattern();
            const int step = i + machinePage * 16;
            c.dim = step >= p.length;
            if (row == 0)
            {
                c.lit = p.accent[(size_t) step];
                break;
            }
            const int voice = machineVoice (row);
            const int h = p.hits[(size_t) voice][(size_t) step];
            c.lit = h > 0;
            c.hits = h;
            c.shape = p.shape[(size_t) voice][(size_t) step];
            if (h >= 2)
                c.label = juce::String::fromUTF8 ("\xc3\x97") + juce::String (h) + (c.shape == crate::drums::builds ? "^" : "");
            else if (h == 1 && c.shape == crate::drums::flam)
                c.label = "f";
            else if (h == 1 && p.accent[(size_t) step])
                c.label = ">";
            // Its life, where it has one (and always in that mode).
            if (h > 0)
            {
                const int chance = p.chance[(size_t) voice][(size_t) step], cond = p.cond[(size_t) voice][(size_t) step];
                const int nudge = p.nudge[(size_t) voice][(size_t) step];
                if (lifeMode == lifeChance || (lifeMode == lifeHits && chance < 100))
                    c.sub = juce::String (chance) + "%";
                else if (lifeMode == lifeCond || (lifeMode == lifeHits && cond != 0))
                    c.sub = crate::life::condName (cond);
                else if (lifeMode == lifeNudge || (lifeMode == lifeHits && nudge != 0))
                    c.sub = (nudge > 0 ? "+" : "") + juce::String (nudge);
                else if (const int pitch = p.pitch[(size_t) voice][(size_t) step]; lifeMode == lifePitch || (lifeMode == lifeHits && pitch != 0))
                    c.sub = pitch == 0 ? juce::String ("0 st") : (pitch > 0 ? "+" : "") + juce::String (pitch) + " st";
                else if (const int decay = p.decayLock[(size_t) voice][(size_t) step]; lifeMode == lifeDecay || (lifeMode == lifeHits && decay != 0))
                    c.sub = decay == 0 ? juce::String ("decay") : decay > 0 ? "open" : "mute";
                else if (const int level = p.velocity[(size_t) voice][(size_t) step]; lifeMode == lifeLevel || (lifeMode == lifeHits && level != 100))
                    c.sub = juce::String (level) + "%";
                c.dim = c.dim || chance < 100 || cond != 0;
            }
            // A row of its own length: past its end the steps are greyed.
            if (const int own = p.rowLength[(size_t) voice]; own > 0 && step >= own)
                c.dim = true;
            break;
        }
        case bass:
        {
            auto& b = engine.getBass();
            const auto p = b.getPattern (b.getSelectedPattern());
            const auto& s = p.steps[(size_t) i];
            c.dim = i >= p.length;
            switch (row)
            {
                case 0:
                    c.lit = s.on;
                    if (s.on && (lifeMode == lifeChance || (lifeMode == lifeHits && s.chance < 100)))
                        c.sub = juce::String ((int) s.chance) + "%";
                    else if (s.on && (lifeMode == lifeCond || (lifeMode == lifeHits && s.cond != 0)))
                        c.sub = crate::life::condName (s.cond);
                    break;
                case 1:
                    c.label = juce::MidiMessage::getMidiNoteName ((int) getParam (b.params, "root") + s.note, true, true, 3);
                    c.lit = s.on;
                    c.playing = dragStep == i;
                    break;
                case 2: c.lit = s.accent; break;
                default: c.lit = s.slide; break;
            }
            break;
        }
        case synth:
        {
            const auto r = engine.getSynth().getRhythm();
            c.lit = row == 0 ? r.on[(size_t) i] : r.accent[(size_t) i];
            c.dim = i >= r.length;
            break;
        }
        case vocoder:
            c.label = vocoderSwitches[i].second;
            c.lit = getParam (engine.getVocoder().params, vocoderSwitches[i].first) > 0.5f;
            c.sub = c.lit ? "On" : "Off";
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
                case RigLooper::State::armed:       c.sub = "Next bar"; break;
                case RigLooper::State::recording:   c.sub = "Recording"; break;
                case RigLooper::State::playing:     c.sub = c.dim ? "Muted" : "Playing"; break;
                case RigLooper::State::overdubbing: c.sub = "Overdub"; break;
            }
            break;
        }
        case lineIn:
        {
            const bool live = lineInLive(), picked = engine.inputJack.load() == i;
            c.label = RigEngine::inputJackName (i);
            c.lit = picked;
            c.dim = ! live;
            c.playing = picked && live && engine.micPeak.load() > 0.01f;
            c.sub = picked ? (live ? "Listening" : "Off") : juce::String();
            break;
        }
        case drone:
        {
            // A string: its name and note; lit while the drone's on, bright as it rings.
            auto& d = engine.getDrone();
            c.label = droneStringName (i);
            c.sub = d.stringNotes()[i];
            c.lit = d.isOn();
            c.dim = ! d.isOn() && d.getRinging (i) < 0.05f;
            c.playing = d.getRinging (i) > 0.35f;
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
        case machine:
        {
            auto& d = engine.getDrums();
            if (getParam (d.params, "sequencer") < 0.5f || d.getSelectedPattern() != d.getPlayingPattern() || d.isPlayingFill())
                return -1;
            const int step = d.getCurrentStep() - machinePage * 16;
            return juce::isPositiveAndBelow (step, 16) ? step : -1;
        }
        case bass:
            return getParam (engine.getBass().params, "sequencer") > 0.5f ? engine.getBass().getCurrentStep() : -1;
        case synth:
            return getParam (engine.getSynth().params, "arpOn") > 0.5f ? engine.getSynth().getArpStep() : -1;
        default:
            return -1;
    }
}

//==============================================================================
void GrooveView::pressStep (int i) { pressCell (0, i); }

void GrooveView::pressCell (int row, int i)
{
    if (! juce::isPositiveAndBelow (i, numSteps()) || ! juce::isPositiveAndBelow (row, numRows()))
        return;
    if (lifeMode != lifeHits && pressLife (row, i, false))
        return;
    if (canUndoClear())
        clearUndo = {};   // a new step after a Clear: that's the new pattern now
    switch (track)
    {
        case drums:
        {
            const int pad = drumPad (row);
            const bool add = ! drumHit (pad, i);
            setDrumHit (pad, i, add);
            if (add && ! engine.getTransport().isPlaying())
                engine.getMpc().playPad (pad, 100 / 127.0f);
            break;
        }
        case machine:
        {
            auto& d = engine.getDrums();
            auto p = machinePattern();
            const int step = i + machinePage * 16;
            if (row == 0)
            {
                p.accent[(size_t) step] = ! p.accent[(size_t) step];
                setMachinePattern (p);
                break;
            }
            const int voice = machineVoice (row);
            // The picked chip (Roll, Builds, Flam, Soft/Loud) or, with none, a plain hit that lights or clears.
            const auto what = machineToolPicked && lifeMode == lifeHits ? machineTool : StepTool::hit;
            crate::drums::steptools::apply (p, what, voice, step, machineRoll);
            const auto h = p.hits[(size_t) voice][(size_t) step];
            if (h != 0)
                crate::drums::steptools::extendTo (p, step);
            setMachinePattern (p);
            // A lit step means you want to hear the pattern; stopped, you hear the sound.
            if (h != 0)
            {
                setParam (d.params, "sequencer", 1.0f);
                if (! engine.getTransport().isPlaying())
                    d.playVoice (voice, 0.8f);
            }
            break;
        }
        case bass:
        {
            auto& b = engine.getBass();
            const int index = b.getSelectedPattern();
            auto p = b.getPattern (index);
            auto& s = p.steps[(size_t) i];
            if (row == 2)
                s.accent = ! s.accent;
            else if (row == 3)
                s.slide = ! s.slide;
            else if (row == 1 && s.on)
                break;   // the note row: drag a lit note up or down
            else
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
            if (row == 1)
            {
                r.accent[(size_t) i] = ! r.accent[(size_t) i];
                synthTool.setRhythm (r);
                break;
            }
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
        {
            // An empty layer records from the next bar; a recording one closes its loop. One
            // with sound is just picked: recording on top is the Overdub button, never a tap.
            auto& looper = engine.getLooper();
            const auto st = looper.getState (i);
            pickedLayer = i;
            if (st == RigLooper::State::empty || st == RigLooper::State::armed || st == RigLooper::State::recording)
                looper.pressRecord (i);
            else if (st == RigLooper::State::overdubbing)
                looper.pressRecord (i);   // stops the overdub
            break;
        }
        case lineIn:
            // Picking the jack means you want to hear it.
            engine.inputJack = i;
            if (! lineInLive())
                setLineIn (true);
            break;
        case drone:
            engine.pluckDrone (i);
            break;
        default: break;
    }
    updateButtons();
    repaint();
}

void GrooveView::pressRoll (int row, int i)
{
    if (track != machine || ! juce::isPositiveAndBelow (i, numSteps()) || ! juce::isPositiveAndBelow (row, numRows()))
        return;
    auto& d = engine.getDrums();
    auto p = machinePattern();
    const int step = i + machinePage * 16;
    if (step >= p.length || row == 0)
        return;
    crate::drums::steptools::cycleRoll (p, machineVoice (row), step);   // none or one -> 2 -> 3 -> 4 -> 6 -> 8 -> one
    setMachinePattern (p);
    setParam (d.params, "sequencer", 1.0f);
    repaint();
}

int GrooveView::machineLength() const
{
    return machinePattern().length;
}

// The pattern the drum machine's own page is editing (its A or B), so both screens show one beat.
crate::drums::Pattern GrooveView::machinePattern() const
{
    auto& d = engine.getDrums();
    return d.getPattern (d.getSelectedPattern(), d.editVariation);
}

void GrooveView::setMachinePattern (const crate::drums::Pattern& p)
{
    auto& d = engine.getDrums();
    d.setPattern (d.getSelectedPattern(), p, d.editVariation);
}

void GrooveView::setMachineTool (StepTool t, bool on)
{
    machineToolPicked = on;
    machineTool = t;
    if (on)
        say (t == StepTool::roll ? "ROLL: click a step for a roll of " + juce::String (machineRoll) + " hits (click the chip again to change it)."
             : t == StepTool::builds ? "BUILDS: click a step for a roll of " + juce::String (machineRoll) + " hits that gets louder."
             : t == StepTool::flam ? "FLAM: click a step for a hit with a grace hit before it."
                                   : "SOFT/LOUD: click a lit step to step its level.");
    updateButtons();
    repaint();
}

void GrooveView::setMachineRoll (int n)
{
    machineRoll = juce::jlimit (2, crate::drums::kMaxRoll, n);
    say ("Rolls are " + juce::String (machineRoll) + " hits now.");
    updateButtons();
}

void GrooveView::setMachinePage (int bar)
{
    machinePage = juce::jlimit (0, 3, bar);
    if (track == machine && machinePage * 16 >= machineLength())
    {
        auto p = machinePattern();
        crate::drums::steptools::extendTo (p, machinePage * 16);
        setMachinePattern (p);
    }
    updateButtons();
    repaint();
}

void GrooveView::releaseCell()
{
    if (track == sampler && heldCell >= 0)
    {
        auto& s = engine.getSampler();
        const bool slices = s.getKeys().getMode() == crate::KeyboardSampler::Mode::slices;
        const int note = slices ? crate::KeyboardSampler::kFirstSliceNote + heldCell : s.getKeys().getRootNote() + heldCell;
        s.keyboardState.noteOff (1, juce::jlimit (0, 127, note), 0.0f);
    }
    heldCell = -1;
    repaint();
}

void GrooveView::toggleAccent (int i)
{
    if (track == bass)
        pressCell (2, i);
    else if (track == synth)
        pressCell (1, i);
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
        case machine:
        {
            auto& d = engine.getDrums();
            if (i >= crate::drums::kPatterns + 3)
            {
                d.fillNow();
                say ("The fill plays the next time the beat comes round.");
                break;
            }
            if (i >= crate::drums::kPatterns)
            {
                // A, B or A+B, as the drum machine's own buttons do.
                const int v = i - crate::drums::kPatterns;
                if (v == 2 && d.getVariation() == 2)
                    d.editVariation = 1 - d.editVariation;
                d.setVariation (v);
                say (v == 2 ? juce::String ("A then B, back and forth. The steps edit ") + (d.editVariation == 1 ? "B." : "A.")
                            : juce::String ("Variation ") + (v == 1 ? "B" : "A") + ".");
                break;
            }
            d.selectPattern (i);
            setParam (d.params, "sequencer", 1.0f);
            machinePage = 0;
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
            else if (i == (int) std::size (arpModes) + 1)
                setParam (params, "latch", getParam (params, "latch") > 0.5f ? 0.0f : 1.0f);
            else
            {
                // Slower / Faster: one speed along (the speeds run slowest first).
                const int speed = juce::jlimit (0, crate::synth::ArpRates::count - 1,
                                                juce::roundToInt (getParam (params, "arpSpeed")) + (i == (int) std::size (arpModes) + 2 ? -1 : 1));
                setParam (params, "arpSpeed", (float) speed);
                setParam (params, "arpSync", 1.0f);
                say ("Arp speed: " + crate::synth::ArpRates::names()[speed] + ".");
            }
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
            engine.getDirty().authoredField (RigDirty::loopMute, i);
            break;
        }
        case lineIn:
            if (i == 0)
                loopLineIn();
            else if (onOpen)
                onOpen (tabFor (lineIn));
            break;
        case drone:
            engine.getDrone().loadPreset (i);
            say ("The drone is " + DroneProcessor::presetNames()[i] + " now (its note and On stay as they were)."
                 + (droneOn() ? juce::String() : juce::String (" Press On to hear it.")));
            break;
        default: break;
    }
    updateButtons();
    repaint();
}

void GrooveView::pressPart (int i)
{
    rigpanels::Footer::pressPart (engine, i);
}

void GrooveView::showDrumRowMenu (int row)
{
    const int first = engine.getRouting().padBank.load() * kPadsPerBank;
    const auto& pads = engine.getMpc().getProject().currentBank().pads;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Sound for this row");
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        const auto& p = pads[(size_t) (first + i)];
        menu.addItem (i + 1, padLabel (first + i) + "   " + (p.isEmpty() ? juce::String ("(empty)") : p.name), true, drumPad (row) == first + i);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellRect (row, 0).toNearestInt().withX (gridArea.getX()))),
                        [safe = juce::Component::SafePointer<GrooveView> (this), row] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            safe->drumRows[(size_t) row] = result - 1;
                            safe->engine.getMpc().playPad (safe->drumPad (row), 100 / 127.0f);
                            safe->repaint();
                        });
}

// A click on a sound's name plays it, as its pad on the drum machine does.
void GrooveView::auditionMachineRow (int row)
{
    if (row >= 1 && row < kMachineRows)
        engine.getDrums().playVoice (machineVoice (row), 0.8f);
}

//==============================================================================
void GrooveView::timerCallback()
{
    if (! isShowing())
        return;
    for (auto* c : cards)
        c->tick();
    updateButtons();

    // Follow the keys if they're sent somewhere else (the Mixer page, a loaded show).
    const int keys = engine.getRouting().keysTo.load();
    if (destFor (track) >= 0 && keys != destFor (track))
        for (int t = 0; t < numTracks; ++t)
            if (destFor (t) == keys)
            {
                selectTrack (t);
                break;
            }

    for (size_t k = 0; k < partKnobs.size(); ++k)
    {
        const bool on = engine.vocoderOn.load() && getParam (engine.getVocoder().params, vocoderSwitches[partOfKnob[k]].first) > 0.5f;
        partKnobs[k]->setAlpha (on ? 1.0f : 0.5f);
    }

    juce::String sig;
    sig << track << "/" << playheadStep() << "/" << stateOf (track) << "/" << heldCell << "/" << pickedLayer << "/" << drumBar << "/" << patternText()
        << (vintage::isStage() ? "d" : "l");
    for (int row = 0; row < numRows(); ++row)
    {
        sig << rowName (row) << ":";
        for (int i = 0; i < numSteps(); ++i)
        {
            const auto c = cellAt (row, i);
            sig << (c.lit ? 1 : 0) << (c.strong ? 1 : 0) << (c.dim ? 1 : 0) << (c.playing ? 1 : 0) << c.label << c.sub << c.shape;
        }
    }
    if (track == loops)
        for (int i = 0; i < RigLooper::kLayers; ++i)
            sig << juce::roundToInt (engine.getLooper().getLayer (i).position.load() * 50.0);
    if (sig != shownSignature || guideRow >= 0)   // (the lesson's glow pulses)
    {
        shownSignature = sig;
        repaint (wellArea);
    }
}

//==============================================================================
int GrooveView::beatGroup() const
{
    // The drum machine in threes (12/8, 6/8, triplets): the grid is grouped by beat, three a beat.
    if (track == machine)
        if (auto* p = engine.getDrums().params.getParameter ("stepRate"))
            if (const int rate = juce::roundToInt (p->convertFrom0to1 (p->getValue())); rate == 2 || rate == 4)
                return 3;
    return 4;
}

juce::Rectangle<float> GrooveView::cellRect (int row, int i) const
{
    const int n = numSteps(), rows = numRows();
    auto grid = gridArea.toFloat();
    if (! bigCells())
        grid.removeFromLeft ((float) kLabelWidth + 12.0f);
    const int group = beatGroup();
    const float gap = bigCells() ? 12.0f : 6.0f, groupGap = n == 16 ? 14.0f : 0.0f;
    const float w = (grid.getWidth() - gap * (n - 1) - groupGap * (float) ((n - 1) / group)) / (float) n;
    const float x = grid.getX() + i * (w + gap) + (n == 16 ? (float) (i / group) * groupGap : 0.0f);
    const float rowGap = rows > 6 ? 3.0f : 10.0f;   // the drum machine's 13 rows sit closer
    float h = (grid.getHeight() - rowGap * (rows - 1)) / (float) rows;
    if (! bigCells())
        h = juce::jmin (h, 58.0f);
    return { x, grid.getY() + row * (h + rowGap), w, h };
}

std::pair<int, int> GrooveView::cellAt (juce::Point<int> p) const
{
    for (int row = 0; row < numRows(); ++row)
        for (int i = 0; i < numSteps(); ++i)
            if (cellRect (row, i).expanded (2.0f).contains (p.toFloat()))
                return { row, i };
    return { -1, -1 };
}

int GrooveView::rowLabelAt (juce::Point<int> p) const
{
    if (bigCells())
        return -1;
    for (int row = 0; row < numRows(); ++row)
    {
        const auto r = cellRect (row, 0);
        if (juce::Rectangle<float> ((float) gridArea.getX(), r.getY(), (float) kLabelWidth, r.getHeight()).contains (p.toFloat()))
            return row;
    }
    return -1;
}

void GrooveView::mouseDown (const juce::MouseEvent& e)
{
    if (const int row = rowLabelAt (e.getPosition()); row >= 0)
    {
        if (e.mods.isPopupMenu() && hasLife())
        {
            showRowMenu (row);
            return;
        }
        if (track == drums)
            showDrumRowMenu (row);
        else if (track == machine)
            auditionMachineRow (row);
        return;
    }
    const auto [row, step] = cellAt (e.getPosition());
    if (step < 0)
        return;
    if (lifeMode != lifeHits && e.mods.isShiftDown() && pressLife (row, step, true))
        return;
    if ((e.mods.isPopupMenu() || e.mods.isAltDown()) && track == machine)
    {
        pressRoll (row, step);
        return;
    }
    if ((e.mods.isPopupMenu() || e.mods.isShiftDown()) && (track == bass || track == synth))
    {
        toggleAccent (step);
        return;
    }
    if (track == bass && row == 1)
    {
        // Drag a note up or down (a click on an unlit one lights it).
        const auto p = engine.getBass().getPattern (engine.getBass().getSelectedPattern());
        if (! p.steps[(size_t) step].on)
            pressCell (0, step);
        dragStep = step;
        dragStartNote = p.steps[(size_t) step].note;
        dragStartY = e.y;
        repaint();
        return;
    }
    pressCell (row, step);
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
        releaseCell();
    dragStep = -1;
    repaint();
}

void GrooveView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (track != bass)
        return;
    const auto [row, step] = cellAt (e.getPosition());
    if (step >= 0 && std::abs (w.deltaY) > 0.0f)
        nudgeNote (step, w.deltaY > 0 ? 1 : -1);
}

//==============================================================================
void GrooveView::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    const auto colour = trackColour (track);
    vintage::drawFace (g, getLocalBounds());

    if (! cardsElsewhere)
        vintage::engraved (g, juce::String::fromUTF8 ("Tracks \xc2\xb7 tap to select"), tracksArea.withHeight (18));

    // The steps' well.
    vintage::drawWell (g, wellArea.toFloat());
    {
        auto h = headerArea;
        auto swatch = h.removeFromLeft (18).withSizeKeepingCentre (18, 18).toFloat();
        vintage::glow (g, swatch, 4.0f, colour, 5.0f, 0.5f);
        g.setColour (colour);
        g.fillRoundedRectangle (swatch, 4.0f);
        h.removeFromLeft (12);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (24.0f, Weight::extra, 0.1f));
        const juce::String title = (bigCells() ? (track == vocoder ? "Parts" : track == sampler ? "Pads" : track == drone ? "Strings" : "Layers") : "Steps")
                                   + juce::String::fromUTF8 (" \xc2\xb7 ") + trackName (track);
        const int titleWidth = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title.toUpperCase()) + 8;
        g.drawText (title.toUpperCase(), h.removeFromLeft (titleWidth), juce::Justification::centredLeft, false);
        h.removeFromLeft (12);

        // The pattern on the LCD, at the right of the free space.
        const int lcdWidth = juce::jmin (180, juce::jmax (0, h.getWidth() - 40));
        auto lcd = h.removeFromRight (lcdWidth).withSizeKeepingCentre (lcdWidth, 30).toFloat();
        if (lcdWidth > 60)
        {
            vintage::drawLcd (g, lcd);
            // Smaller type rather than a cut-off word ("0 of 8 laye").
            const auto text = patternText();
            const auto box = lcd.reduced (8.0f, 0.0f);
            float size = 22.0f;
            while (size > 14.0f && juce::GlyphArrangement::getStringWidth (vintage::lcd (size), text) > box.getWidth())
                size -= 1.0f;
            vintage::lcdText (g, text, box, size);
        }
        h.removeFromRight (12);
        const auto hint = trackHint();
        // Only when it fits: a cut-off hint ("D...") says nothing. Picking the track says it below too.
        if (juce::GlyphArrangement::getStringWidth (vintage::cond (13.0f, Weight::bold, 0.14f), hint.toUpperCase()) <= (float) h.getWidth())
            vintage::engraved (g, hint, h, juce::Justification::centredLeft, 13.0f, true);
    }

    // Step numbers, with the play head.
    const int n = numSteps(), now = playheadStep();
    if (! numbersArea.isEmpty())
        for (int i = 0; i < n; ++i)
        {
            auto r = cellRect (0, i).withY ((float) numbersArea.getY()).withHeight ((float) numbersArea.getHeight());
            const bool here = i == now;
            g.setColour (here ? vintage::colours::selected : p.wellInk2);
            g.setFont (vintage::cond (15.0f, Weight::bold, 0.06f));
            g.drawText (here ? juce::String::fromUTF8 ("\xe2\x96\xbc ") + juce::String (i + 1) : juce::String (i + 1), r,
                        juce::Justification::centred);
        }

    // The rows.
    for (int row = 0; row < numRows(); ++row)
    {
        if (! bigCells())
        {
            const auto first = cellRect (row, 0);
            auto label = juce::Rectangle<float> ((float) gridArea.getX(), first.getY(), (float) kLabelWidth, first.getHeight()).toNearestInt();
            g.setColour (p.wellInk);
            g.setFont (vintage::cond (juce::jmin (20.0f, first.getHeight() * 0.9f), Weight::extra, 0.08f));
            g.drawFittedText (rowName (row).toUpperCase(), label, juce::Justification::centredLeft, 1, 0.6f);
            if (track == drums)
            {
                g.setColour (p.wellInk2);
                g.setFont (vintage::barlow (11.0f));
                g.drawText (juce::String::fromUTF8 ("\xe2\x96\xbe"), label, juce::Justification::centredRight);
            }
        }
        for (int i = 0; i < n; ++i)
        {
            const auto c = cellAt (row, i);
            auto r = cellRect (row, i);
            const auto fill = track == loops && c.strong ? vintage::colours::arm : colour;
            float alpha = c.dim ? 0.35f : 1.0f;
            if (track == bass && row == 1)
                alpha *= 0.45f;
            // (On Loops, the ring marks the layer you've picked.)
            vintage::drawStep (g, r, c.lit, track == loops ? i == pickedLayer : i == now && ! bigCells(), (i / beatGroup()) % 2 == 1 && n == 16, fill, alpha);
            if (c.dim && ! c.lit)
            {
                g.setColour (p.well.withAlpha (0.45f));
                g.fillRoundedRectangle (r.withTrimmedBottom (3.0f), 8.0f);
            }
            if (track == loops && c.lit && engine.getLooper().getState (i) != RigLooper::State::empty)
            {
                // How far round the loop it is.
                const float pos = juce::jlimit (0.0f, 1.0f, (float) engine.getLooper().getLayer (i).position.load());
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                auto face = r.withTrimmedBottom (3.0f);
                g.fillRoundedRectangle (face.withTop (face.getY() + face.getHeight() * pos), 8.0f);
            }
            if (c.playing)
            {
                g.setColour (juce::Colours::white.withAlpha (0.3f));
                g.fillRoundedRectangle (r.withTrimmedBottom (3.0f), 8.0f);
            }
            // The Make a beat lesson's suggested steps: a pulsing amber ring until you click them.
            if (row == guideRow && ! c.lit && i < 32 && (guideMask >> i) & 1u)
            {
                const float pulse = 0.55f + 0.45f * std::sin ((float) juce::Time::getMillisecondCounter() * 0.006f);
                g.setColour (vintage::colours::lcdAmber.withAlpha (0.25f * pulse));
                g.fillRoundedRectangle (r.withTrimmedBottom (3.0f), 8.0f);
                g.setColour (vintage::colours::lcdAmber.withAlpha (0.6f + 0.4f * pulse));
                g.drawRoundedRectangle (r.withTrimmedBottom (3.0f).reduced (1.5f), 8.0f, 3.0f);
            }

            auto text = r.withTrimmedBottom (3.0f).reduced (6.0f, 8.0f).toNearestInt();
            const auto ink = c.lit && alpha > 0.5f ? juce::Colour (0xff1e1a16) : p.wellInk.withAlpha (c.dim ? 0.5f : 0.9f);
            g.setColour (ink);
            if (bigCells() && r.getHeight() < 72.0f)
            {
                // A short switch: the name, and On or Off beside it.
                auto line = text.withSizeKeepingCentre (text.getWidth(), juce::jmin (text.getHeight(), 30));
                g.setFont (vintage::cond (14.0f, Weight::bold, 0.12f));
                g.drawText (c.sub.toUpperCase(), line.removeFromRight (34), juce::Justification::centredRight, false);
                g.setFont (vintage::cond (24.0f, Weight::extra, 0.06f));
                g.drawFittedText (c.label.toUpperCase(), line.withTrimmedLeft (34), juce::Justification::centred, 1, 0.6f);
            }
            else if (bigCells())
            {
                g.setFont (vintage::cond (track == sampler ? 24.0f : 26.0f, Weight::extra, 0.06f));
                g.drawFittedText (c.label.toUpperCase(), text, juce::Justification::centredTop, 1, 0.6f);
                g.setFont (vintage::cond (14.0f, Weight::bold, 0.12f));
                g.drawFittedText (c.sub.toUpperCase(), text, juce::Justification::centredBottom, 2, 0.7f);
            }
            else
            {
                const bool marked = track == machine && c.lit && (c.hits >= 2 || c.shape == crate::drums::flam);
                if (marked)
                {
                    // A roll's ticks and count, a roll that builds rising, a flam's grace tick.
                    g.setColour (ink);
                    auto marks = r.withTrimmedBottom (3.0f).reduced (5.0f, c.sub.isNotEmpty() ? 4.0f : 6.0f).toFloat();
                    if (c.sub.isNotEmpty())
                        marks = marks.withTrimmedBottom (marks.getHeight() * 0.38f);
                    crate::drums::steptools::drawHitMarks (g, marks, c.hits, c.shape, ink, true, vintage::cond (14.0f, Weight::extra));
                }
                else if (c.label.isNotEmpty())
                {
                    g.setColour (p.wellInk.withAlpha (c.dim ? 0.5f : 0.95f));
                    g.setFont (vintage::cond (16.0f, Weight::extra));
                    g.drawFittedText (c.label, c.sub.isNotEmpty() ? text.withTrimmedBottom (text.getHeight() / 2) : text,
                                      juce::Justification::centred, 1, 0.6f);
                }
                // Its life (chance, condition, nudge) small along its bottom.
                if (c.sub.isNotEmpty())
                {
                    const auto face = r.withTrimmedBottom (3.0f).toNearestInt();
                    // (On a dimmed step, a light ink, so 50% still reads on the darker red.)
                    g.setColour (c.lit && ! c.dim ? juce::Colour (0xff1e1a16) : p.wellInk);
                    g.setFont (vintage::cond (juce::jlimit (9.0f, 13.0f, face.getHeight() * 0.42f), Weight::extra));
                    g.drawFittedText (c.sub, face.reduced (1, 1), c.label.isNotEmpty() ? juce::Justification::centredBottom : juce::Justification::centred, 1, 0.5f);
                }
            }
        }
    }

    // Under each of the vocoder's parts, a panel for its knobs, lit in its colour while it's on.
    if (! partsArea.isEmpty())
        for (int part = 0; part < (int) std::size (vocoderPartKnobs); ++part)
        {
            const auto cell = cellRect (0, part);
            const auto panel = juce::Rectangle<float> (cell.getX(), (float) partsArea.getY(), cell.getWidth(), (float) partsArea.getHeight());
            const bool on = cellAt (0, part).lit && engine.vocoderOn.load();
            const juce::Colour tint (vocoderPartColours[part]);
            g.setColour (tint.withAlpha (on ? 0.13f : 0.04f));
            g.fillRoundedRectangle (panel, 10.0f);
            g.setColour (tint.withAlpha (on ? 0.55f : 0.18f));
            g.drawRoundedRectangle (panel.reduced (0.75f), 10.0f, 1.5f);
            // A short cable from the switch down to its knobs.
            g.fillRect (juce::Rectangle<float> (cell.getCentreX() - 2.0f, cell.getBottom() - 2.0f, 4.0f, panel.getY() - cell.getBottom() + 4.0f));
        }

    // The knobs' box.
    g.setColour (p.line);
    g.drawRoundedRectangle (knobBox.toFloat().reduced (1.0f), 12.0f, 2.0f);
    {
        auto h = knobHeader;
        g.setColour (p.ink);
        g.setFont (vintage::cond (22.0f, Weight::extra, 0.1f));
        const auto title = (juce::String ("Knobs") + juce::String::fromUTF8 (" \xc2\xb7 ") + trackName (track)).toUpperCase();
        const int w = (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title) + 8;
        g.drawText (title, h.removeFromLeft (w), juce::Justification::centredLeft, false);
        h.removeFromLeft (12);
        auto linked = h.removeFromRight (90);
        vintage::drawLamp (g, { (float) linked.getX() + 8.0f, (float) linked.getCentreY() }, Lit::play);
        vintage::engraved (g, "Linked", linked.withTrimmedLeft (24), juce::Justification::centredLeft, 13.0f, false, p.ink);
        const bool fx = track == drums || track == sampler || track == loops || track == lineIn;
        vintage::engraved (g, juce::String::fromUTF8 ("Follow Launchkey knobs 1\xe2\x80\x93" "8") + (fx ? juce::String::fromUTF8 (" \xc2\xb7 this track's effects") : juce::String()),
                           h, juce::Justification::centredLeft);
    }
}

void GrooveView::resized()
{
    auto area = getLocalBounds().withTrimmedTop (6).withTrimmedBottom (14).reduced (22, 0);

    // Tracks down the left (unless they're on the window's track rail).
    if (cardsElsewhere)
        tracksArea = {};
    else
    {
    tracksArea = area.removeFromLeft (juce::jmin (kTrackWidth, area.getWidth() / 4 + 20));
    {
        auto list = tracksArea.withTrimmedTop (24);
        const int gap = 4;
        const int h = (list.getHeight() - gap * (numTracks - 1)) / numTracks;
        for (auto* c : cards)
        {
            c->setBounds (list.removeFromTop (h).expanded (4, 0));
            list.removeFromTop (gap);
        }
    }
    area.removeFromLeft (16);
    }

    // The knobs below, the steps above.
    // The vocoder's parts have knobs of their own up top, so its eight knobs give them a little room.
    knobBox = area.removeFromBottom (track == vocoder ? juce::jlimit (144, 214, area.getHeight() * 27 / 100)
                                                      : juce::jlimit (150, 214, area.getHeight() * 36 / 100));
    area.removeFromBottom (14);
    wellArea = area;
    {
        auto k = knobBox.reduced (18, 12);
        knobHeader = k.removeFromTop (26);
        k.removeFromTop (6);
        const int w = k.getWidth() / 8;
        for (auto& knob : knobs)
            knob->setBounds (k.removeFromLeft (w).reduced (4, 0));
    }

    auto c = wellArea.reduced (16, 12);
    headerArea = c.removeFromTop (44);
    {
        auto right = headerArea;
        auto place = [&right] (juce::Component& b, int w)
        {
            if (! b.isVisible())
                return;
            b.setBounds (right.removeFromRight (w).withSizeKeepingCentre (w, 44));
            right.removeFromRight (8);
        };
        place (openButton, 132);
        place (redoButton, 92);
        place (clearButton, track == loops ? 92 : 76);
        place (undoButton, 92);
        place (overdubButton, 112);
        place (quantizeButton, 100);
        place (extraButton, 110);
        if (soundButton.isVisible())
        {
            right.removeFromRight (4);
            soundNext.setBounds (right.removeFromRight (36).withSizeKeepingCentre (36, 44));
            soundButton.setBounds (right.removeFromRight (190).withSizeKeepingCentre (190, 44));
            soundPrev.setBounds (right.removeFromRight (36).withSizeKeepingCentre (36, 44));
            right.removeFromRight (8);
        }
        place (barButton, 112);
        place (micButton, 92);
        headerArea = headerArea.withRight (right.getRight());
    }
    c.removeFromTop (8);
    patternArea = c.removeFromBottom (44);
    c.removeFromBottom (12);
    keysArea = {};
    if (hasKeys())
    {
        keysArea = c.removeFromBottom (juce::jlimit (48, 72, c.getHeight() / 3));
        screenKeys.setBounds (keysArea);
        screenKeys.setKeyWidth (juce::jmax (12.0f, keysArea.getWidth() / 36.0f));
        screenKeys.setLowestVisibleKey (track == bass ? 24 : 48);
        c.removeFromBottom (10);
    }
    {
        auto row = patternArea;
        const int count = juce::jmax (1, patternButtons.size());
        const int gap = count > 12 ? 4 : 8;
        const int w = juce::jmin (150, (row.getWidth() - gap * (count - 1)) / count);
        for (auto* b : patternButtons)
        {
            b->setBounds (row.removeFromLeft (w));
            row.removeFromLeft (gap);
        }
    }
    lifeArea = {};
    if (hasLife())
    {
        lifeArea = c.removeFromTop (30);
        c.removeFromTop (6);
        auto row = lifeArea.withTrimmedLeft (kLabelWidth);
        for (auto* b : lifeButtons)
            if (b->isVisible())
            {
                b->setBounds (row.removeFromLeft (b->getButtonText() == "Condition" ? 100 : 74));
                row.removeFromLeft (6);
            }
        mutateButton.setBounds (lifeArea.removeFromRight (100));
        if (track == machine)
        {
            lifeArea.removeFromRight (6);
            feelButton.setBounds (lifeArea.removeFromRight (90));
        }
    }
    toolsArea = {};
    if (track == machine)
    {
        toolsArea = c.removeFromTop (30);
        c.removeFromTop (6);
        auto row = toolsArea.withTrimmedLeft (kLabelWidth);
        for (auto& b : machineBars)
        {
            b.setBounds (row.removeFromLeft (66));
            row.removeFromLeft (6);
        }
        row.removeFromLeft (24);
        for (auto& b : toolChips)
        {
            const int w = b.getButtonText() == "Soft/Loud" ? 100 : b.getButtonText().startsWith ("Roll") ? 92 : 78;
            b.setBounds (row.removeFromLeft (w));
            row.removeFromLeft (6);
        }
    }
    numbersArea = bigCells() ? juce::Rectangle<int>() : c.removeFromTop (20);
    if (! bigCells())
        c.removeFromTop (4);
    partsArea = {};
    if (track == vocoder && ! partKnobs.empty())
    {
        // The switches on top, each part's knobs under its switch.
        gridArea = c.removeFromTop (juce::jlimit (46, 84, c.getHeight() / 4));
        c.removeFromTop (8);
        partsArea = c;
        layoutPartKnobs();
        return;
    }
    gridArea = c;
}

void GrooveView::layoutPartKnobs()
{
    const int parts = (int) std::size (vocoderPartKnobs);
    for (int part = 0; part < parts; ++part)
    {
        const auto cell = cellRect (0, part);
        auto column = juce::Rectangle<int> ((int) cell.getX(), partsArea.getY(), (int) cell.getWidth(), partsArea.getHeight()).reduced (6, 8);
        std::vector<vintage::Knob*> mine;
        for (size_t k = 0; k < partKnobs.size(); ++k)
            if (partOfKnob[k] == part)
                mine.push_back (partKnobs[k].get());
        const int n = (int) mine.size();
        if (n == 0)
            continue;
        // Two by two: each knob with its name above and its value below. The same rows in every
        // part, so the knobs line up across.
        const int perRow = 2, rows = 2;
        const int kh = juce::jmin (130, column.getHeight() / rows);
        const int kw = column.getWidth() / perRow;
        for (int k = 0; k < n; ++k)
        {
            const int row = k / perRow, col = k % perRow;
            const int inRow = juce::jmin (perRow, n - row * perRow);
            const int x0 = column.getX() + (column.getWidth() - inRow * kw) / 2;
            mine[(size_t) k]->setBounds (juce::Rectangle<int> (x0 + col * kw, column.getY() + row * kh, kw, kh).reduced (2, 1));
        }
    }
}

int GrooveView::numPartKnobs (int part) const
{
    int n = 0;
    for (int p : partOfKnob)
        n += p == part ? 1 : 0;
    return n;
}

vintage::Knob* GrooveView::partKnob (int part, int index) const
{
    for (size_t k = 0; k < partKnobs.size(); ++k)
        if (partOfKnob[k] == part && index-- == 0)
            return partKnobs[k].get();
    return nullptr;
}

//==============================================================================
// Pattern life.
void GrooveView::setLifeMode (int m)
{
    lifeMode = juce::jlimit (0, (int) numLifeModes - 1, m);
    if (track == bass && (lifeMode == lifeNudge || lifeMode >= lifePitch))
        lifeMode = lifeHits;
    static const char* says[] = { "Hits: a click lights or clears a step.",
                                  "Chance: click a lit step for how often it plays (100, 75, 50, 25, 10%). Shift+click goes back.",
                                  "Condition: click a lit step for which times round it plays (1:2, 3:4, Fill, First...). Shift+click goes back.",
                                  "Nudge: click a lit step to push it early or drag it late. Shift+click goes back.",
                                  "Pitch: click a lit step to give that hit its own pitch (+2, +3, +5, +7, +12, -2...). Shift+click goes back.",
                                  "Decay: click a lit step to let that hit ring open or mute it short. Shift+click goes back.",
                                  "Level: click a lit step for how hard it hits (ghost notes at 30%, 50%, 70%). Shift+click goes back." };
    say (says[lifeMode]);
    updateButtons();
    repaint();
}

bool GrooveView::pressLife (int row, int i, bool back)
{
    namespace life = crate::life;
    if (track == machine)
    {
        if (row == 0)
            return false;   // (the accents are just on or off)
        auto p = machinePattern();
        const int step = i + machinePage * 16;
        const int voice = machineVoice (row);
        if (step >= crate::drums::kMaxSteps || p.hits[(size_t) voice][(size_t) step] == 0)
            return false;   // an unlit step lights as usual
        auto& chance = p.chance[(size_t) voice][(size_t) step];
        auto& cond = p.cond[(size_t) voice][(size_t) step];
        auto& nudge = p.nudge[(size_t) voice][(size_t) step];
        juce::String text;
        if (lifeMode == lifeChance)
        {
            chance = life::nextIn (life::chanceSteps(), chance, back);
            text = juce::String ((int) chance) + "% of the times round";
        }
        else if (lifeMode == lifeCond)
        {
            cond = (std::uint8_t) ((cond + (back ? life::numConds - 1 : 1)) % life::numConds);
            text = juce::String ("plays ") + life::condName (cond);
        }
        else if (lifeMode == lifeNudge)
        {
            nudge = life::nextIn (life::nudgeSteps(), nudge, back);
            text = nudge == 0 ? juce::String ("right on the step")
                              : juce::String (std::abs ((int) nudge)) + "% of a step " + (nudge < 0 ? "early" : "late");
        }
        else if (lifeMode == lifePitch)
        {
            auto& pitch = p.pitch[(size_t) voice][(size_t) step];
            pitch = life::nextIn (life::pitchSteps(), pitch, back);
            text = pitch == 0 ? juce::String ("its own tune") : (pitch > 0 ? "+" : "") + juce::String ((int) pitch) + " semitones";
        }
        else if (lifeMode == lifeDecay)
        {
            auto& decay = p.decayLock[(size_t) voice][(size_t) step];
            decay = life::nextIn (life::decaySteps(), decay, back);
            text = decay == 0 ? juce::String ("its own decay") : decay > 0 ? "rings longer (open)" : "shorter (muted)";
        }
        else if (lifeMode == lifeLevel)
        {
            auto& level = p.velocity[(size_t) voice][(size_t) step];
            level = life::nextIn (life::levelSteps(), level, back);
            text = juce::String ((int) level) + "% as hard";
        }
        setMachinePattern (p);
        say (rowName (row) + ", step " + juce::String (step + 1) + ": " + text + ".");
        repaint();
        return true;
    }
    if (track == bass && row == 0)
    {
        auto& b = engine.getBass();
        const int index = b.getSelectedPattern();
        auto p = b.getPattern (index);
        auto& st = p.steps[(size_t) i];
        if (! st.on)
            return false;
        juce::String text;
        if (lifeMode == lifeChance)
        {
            st.chance = life::nextIn (life::chanceSteps(), st.chance, back);
            text = juce::String ((int) st.chance) + "% of the times round";
        }
        else if (lifeMode == lifeCond)
        {
            st.cond = (std::uint8_t) ((st.cond + (back ? life::numConds - 1 : 1)) % life::numConds);
            text = juce::String ("plays ") + life::condName (st.cond);
        }
        else
            return false;
        b.setPattern (index, p);
        say ("Bass step " + juce::String (i + 1) + ": " + text + ".");
        repaint();
        return true;
    }
    return false;
}

juce::StringArray GrooveView::feelNames()
{
    return { "Straight 16ths", "12/8 (three to a beat, 12 steps)", "6/8 (three to a beat, 6 steps)", "16th triplets (24 steps)",
             "Shuffle (swung 16ths)", "Heavy shuffle", "Half time (8ths)" };
}

void GrooveView::applyFeel (int feel)
{
    // Step rates: 0 1/8, 1 1/16, 2 1/16 triplets, 3 1/32, 4 1/8 triplets.
    struct Feel { int rate, length; float swing; };
    static const Feel feels[] = { { 1, 16, 0.0f }, { 4, 12, 0.0f }, { 4, 6, 0.0f }, { 2, 24, 0.0f }, { 1, 16, 0.33f }, { 1, 16, 0.66f }, { 0, 16, 0.0f } };
    feel = juce::jlimit (0, (int) std::size (feels) - 1, feel);
    auto& drumsProc = engine.getDrums();
    auto set = [&drumsProc] (const char* id, float value)
    {
        if (auto* p = drumsProc.params.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    set ("stepRate", (float) feels[feel].rate);
    set ("swing", feels[feel].swing);
    auto p = machinePattern();
    p.length = feels[feel].length;
    setMachinePattern (p);
    say ("Feel: " + feelNames()[feel] + ". The hits stay where they are on the grid: try Euclid (right-click a sound) for a ritual pattern to start.");
    buildTrack();
    resized();
    repaint();
}

void GrooveView::showFeelMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Feel: the drum machine's grid and swing");
    const auto names = feelNames();
    for (int f = 0; f < names.size(); ++f)
        menu.addItem (names[f], [this, f] { applyFeel (f); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&feelButton));
}

void GrooveView::showMutateMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Mutate a little (a hit or two)", [this] { mutate (0.25f); });
    menu.addItem ("Mutate more", [this] { mutate (0.55f); });
    menu.addItem ("Mutate a lot", [this] { mutate (1.0f); });
    menu.addSeparator();
    menu.addItem ("Undo the last mutate", lifeUndo.track == track, false, [this] { undoMutate(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (mutateButton));
}

void GrooveView::mutate (float amount)
{
    amount = juce::jlimit (0.0f, 1.0f, amount);
    auto& r = lifeRandom;
    lifeUndo = {};
    lifeUndo.track = track;
    int changes = 0;
    if (track == machine)
    {
        auto& d = engine.getDrums();
        lifeUndo.index = d.getSelectedPattern();
        lifeUndo.variation = d.editVariation;
        lifeUndo.machine = machinePattern();
        auto p = machinePattern();
        const int len = juce::jlimit (1, crate::drums::kMaxSteps, p.length);
        const int moves = 1 + (int) std::round (amount * 6);
        for (int k = 0; k < moves; ++k)
        {
            // A sound that plays: move one of its hits, add a ghost, or let one breathe.
            std::vector<int> voices;
            for (int v = 0; v < crate::drums::kVoices; ++v)
                for (int st = 0; st < len; ++st)
                    if (p.hits[(size_t) v][(size_t) st] > 0)
                    {
                        voices.push_back (v);
                        break;
                    }
            if (voices.empty())
                break;
            const int v = voices[(size_t) r.nextInt ((int) voices.size())];
            std::vector<int> lit, unlit;
            for (int st = 0; st < len; ++st)
                (p.hits[(size_t) v][(size_t) st] > 0 ? lit : unlit).push_back (st);
            const int what = r.nextInt (4);
            if (what == 0 && ! lit.empty())
            {
                // Move a hit a step (not the downbeat of the kick: it holds the floor).
                const int from = lit[(size_t) r.nextInt ((int) lit.size())];
                if (v == crate::drums::kick && from % 4 == 0)
                    continue;
                const int to = (from + (r.nextBool() ? 1 : len - 1)) % len;
                if (p.hits[(size_t) v][(size_t) to] == 0)
                {
                    std::swap (p.hits[(size_t) v][(size_t) from], p.hits[(size_t) v][(size_t) to]);
                    std::swap (p.chance[(size_t) v][(size_t) from], p.chance[(size_t) v][(size_t) to]);
                    std::swap (p.shape[(size_t) v][(size_t) from], p.shape[(size_t) v][(size_t) to]);
                    ++changes;
                }
            }
            else if (what == 1 && ! unlit.empty())
            {
                // A ghost hit that plays some of the time.
                const int at = unlit[(size_t) r.nextInt ((int) unlit.size())];
                p.hits[(size_t) v][(size_t) at] = 1;
                p.chance[(size_t) v][(size_t) at] = (std::uint8_t) (r.nextBool() ? 50 : 25);
                ++changes;
            }
            else if (what == 2 && lit.size() > 1)
            {
                // One that breathes: plays only some times round.
                const int at = lit[(size_t) r.nextInt ((int) lit.size())];
                if (! (v == crate::drums::kick && at % 4 == 0))
                {
                    p.chance[(size_t) v][(size_t) at] = (std::uint8_t) (r.nextBool() ? 75 : 50);
                    ++changes;
                }
            }
            else if (! lit.empty())
            {
                // A roll on a hat or a tom, or a nudge for feel.
                const int at = lit[(size_t) r.nextInt ((int) lit.size())];
                if (v == crate::drums::closedHat || v == crate::drums::lowTom || v == crate::drums::midTom || v == crate::drums::highTom || v == crate::drums::rim)
                    p.hits[(size_t) v][(size_t) at] = (std::uint8_t) (2 + r.nextInt (2));
                else
                    p.nudge[(size_t) v][(size_t) at] = (std::int8_t) (r.nextBool() ? 10 : -10);
                ++changes;
            }
        }
        setMachinePattern (p);
        setParam (d.params, "sequencer", 1.0f);
    }
    else if (track == bass)
    {
        auto& b = engine.getBass();
        lifeUndo.index = b.getSelectedPattern();
        lifeUndo.bass = b.getPattern (lifeUndo.index);
        auto p = lifeUndo.bass;
        const int len = juce::jlimit (1, crate::bass::kSteps, p.length);
        static const int scale[] = { 0, 3, 5, 7, 10, 12, -2, -5 };   // minor pentatonic-ish moves
        const int moves = 1 + (int) std::round (amount * 5);
        for (int k = 0; k < moves; ++k)
        {
            auto& st = p.steps[(size_t) r.nextInt (len)];
            switch (r.nextInt (4))
            {
                case 0: if (st.on) st.note = juce::jlimit (-12, 24, scale[r.nextInt (8)]); break;
                case 1: st.on = ! st.on; break;
                case 2: if (st.on) st.slide = ! st.slide; break;
                default: if (st.on) st.chance = (std::uint8_t) (r.nextBool() ? 75 : 50); break;
            }
            ++changes;
        }
        b.setPattern (lifeUndo.index, p);
        setParam (b.params, "sequencer", 1.0f);
    }
    else if (track == drums)
    {
        auto& mpc = engine.getMpc();
        lifeUndo.index = mpc.getProject().getCurrentSequenceIndex();
        lifeUndo.mpcTracks = mpc.getProject().currentSequence().tracks;
        const int steps = numSteps();
        const int moves = 1 + (int) std::round (amount * 5);
        for (int k = 0; k < moves; ++k)
        {
            const int row = r.nextInt (kDrumRows);
            const int pad = drumPad (row);
            std::vector<int> lit, unlit;
            for (int st = 0; st < steps; ++st)
                (drumHit (pad, st) ? lit : unlit).push_back (st);
            if (lit.empty())
                continue;
            if (r.nextBool() && ! unlit.empty())
                setDrumHit (pad, unlit[(size_t) r.nextInt ((int) unlit.size())], true);
            else if (lit.size() > 1)
            {
                const int from = lit[(size_t) r.nextInt ((int) lit.size())];
                const int to = (from + (r.nextBool() ? 1 : steps - 1)) % steps;
                if (! drumHit (pad, to))
                {
                    setDrumHit (pad, from, false);
                    setDrumHit (pad, to, true);
                }
            }
            ++changes;
        }
    }
    else
    {
        lifeUndo = {};
        return;
    }
    say (changes > 0 ? "Mutated: " + juce::String (changes) + (changes == 1 ? " change" : " changes") + ". Mutate again to grow it, or Undo the last mutate."
                     : juce::String ("Nothing to mutate yet: light a few steps first."));
    updateButtons();
    repaint();
}

bool GrooveView::undoMutate()
{
    if (lifeUndo.track != track)
        return false;
    const auto u = std::exchange (lifeUndo, {});
    switch (u.track)
    {
        case machine: engine.getDrums().setPattern (u.index, u.machine, u.variation); break;
        case bass:    engine.getBass().setPattern (u.index, u.bass); break;
        case drums:
        {
            auto& mpc = engine.getMpc();
            auto& seqs = mpc.getProject().getSequences();
            if (juce::isPositiveAndBelow (u.index, (int) seqs.size()))
            {
                mpc.beginEdit();
                seqs[(size_t) u.index].tracks = u.mpcTracks;
                mpc.getProject().sequenceChanged();
            }
            break;
        }
        default: return false;
    }
    say ("Put back what Mutate changed.");
    repaint();
    return true;
}

void GrooveView::euclidRow (int row, int hits, int steps, int rotate)
{
    const auto spread = crate::life::euclid (hits, steps, rotate);
    if (track == machine && row >= 1)
    {
        auto p = machinePattern();
        const int voice = machineVoice (row);
        auto& d = engine.getDrums();
        // Fewer steps than the pattern: the row gets its own length, so it comes round against it.
        p.rowLength[(size_t) voice] = (std::uint8_t) (steps == p.length ? 0 : juce::jlimit (1, crate::drums::kMaxSteps, steps));
        if (steps > p.length)
            p.length = juce::jlimit (1, crate::drums::kMaxSteps, steps);
        for (int st = 0; st < crate::drums::kMaxSteps; ++st)
        {
            const bool on = st < steps && spread[(size_t) st];
            p.hits[(size_t) voice][(size_t) st] = (std::uint8_t) (on ? juce::jmax<std::uint8_t> (1, p.hits[(size_t) voice][(size_t) st]) : 0);
            if (! on)
            {
                p.chance[(size_t) voice][(size_t) st] = 100;
                p.cond[(size_t) voice][(size_t) st] = 0;
                p.nudge[(size_t) voice][(size_t) st] = 0;
            }
        }
        setMachinePattern (p);
        setParam (d.params, "sequencer", 1.0f);
    }
    else if (track == bass && row == 0)
    {
        auto& b = engine.getBass();
        auto p = b.getPattern (b.getSelectedPattern());
        p.length = juce::jlimit (1, crate::bass::kSteps, steps);
        for (int st = 0; st < crate::bass::kSteps; ++st)
            p.steps[(size_t) st].on = st < steps && spread[(size_t) st];
        b.setPattern (b.getSelectedPattern(), p);
        setParam (b.params, "sequencer", 1.0f);
    }
    else if (track == drums)
    {
        const int pad = drumPad (row);
        const int n = numSteps();
        for (int st = 0; st < n; ++st)
        {
            const bool on = spread[(size_t) (st % juce::jmax (1, steps))];
            if (drumHit (pad, st) != on)
                setDrumHit (pad, st, on);
        }
    }
    say ("Euclid: " + juce::String (hits) + " hits spread over " + juce::String (steps) + " steps on " + rowName (row) + ".");
    repaint();
}

void GrooveView::turnRow (int row, int by)
{
    if (track == machine && row >= 1)
    {
        auto p = machinePattern();
        const int voice = machineVoice (row);
        const int len = p.rowLengthOf (voice);
        auto turn = [&] (auto& a)
        {
            auto copy = a;
            for (int st = 0; st < len; ++st)
                a[(size_t) (((st + by) % len + len) % len)] = copy[(size_t) st];
        };
        turn (p.hits[(size_t) voice]);
        turn (p.chance[(size_t) voice]);
        turn (p.cond[(size_t) voice]);
        turn (p.nudge[(size_t) voice]);
        turn (p.pitch[(size_t) voice]);
        turn (p.decayLock[(size_t) voice]);
        turn (p.velocity[(size_t) voice]);
        turn (p.shape[(size_t) voice]);
        setMachinePattern (p);
    }
    else if (track == bass)
    {
        auto& b = engine.getBass();
        auto p = b.getPattern (b.getSelectedPattern());
        const int len = juce::jlimit (1, crate::bass::kSteps, p.length);
        auto copy = p.steps;
        for (int st = 0; st < len; ++st)
            p.steps[(size_t) (((st + by) % len + len) % len)] = copy[(size_t) st];
        b.setPattern (b.getSelectedPattern(), p);
    }
    else if (track == drums)
    {
        const int pad = drumPad (row);
        const int n = numSteps();
        std::vector<bool> was ((size_t) n);
        for (int st = 0; st < n; ++st)
            was[(size_t) st] = drumHit (pad, st);
        for (int st = 0; st < n; ++st)
        {
            const bool on = was[(size_t) (((st - by) % n + n) % n)];
            if (drumHit (pad, st) != on)
                setDrumHit (pad, st, on);
        }
    }
    repaint();
}

void GrooveView::setRowLength (int row, int steps)
{
    if (track != machine || row < 1)
        return;
    auto p = machinePattern();
    p.rowLength[(size_t) machineVoice (row)] = (std::uint8_t) (steps == p.length ? 0 : juce::jlimit (0, crate::drums::kMaxSteps, steps));
    setMachinePattern (p);
    say (steps > 0 && steps != p.length
             ? rowName (row) + " goes round every " + juce::String (steps) + " steps now, against the beat's " + juce::String (p.length) + ": it lands somewhere new each bar."
             : rowName (row) + " goes round with the beat again.");
    repaint();
}

void GrooveView::showRowMenu (int row)
{
    if (! hasLife() || (track == machine && row == 0) || (track == bass && row != 0))
    {
        say (track == bass ? "Right-click the Steps row's name for Euclid and Turn." : "Right-click a sound's name for its rhythm tools.");
        return;
    }
    juce::PopupMenu menu, euclid, length;
    menu.addSectionHeader (rowName (row) + ": rhythm");
    const int pattern = track == machine ? machinePattern().length : track == bass ? engine.getBass().getPattern (engine.getBass().getSelectedPattern()).length : numSteps();
    for (const auto& preset : crate::life::euclidPresets())
        if (track != bass || preset.steps <= crate::bass::kSteps)
            euclid.addItem (preset.name, [this, row, preset] { euclidRow (row, preset.hits, preset.steps); });
    euclid.addSeparator();
    for (int hits = 2; hits <= 9; ++hits)
        euclid.addItem (juce::String (hits) + " in " + juce::String (pattern), [this, row, hits, pattern] { euclidRow (row, hits, pattern); });
    menu.addSubMenu ("Euclid: spread hits evenly", euclid);
    menu.addItem ("Turn left (a step earlier)", [this, row] { turnRow (row, -1); });
    menu.addItem ("Turn right (a step later)", [this, row] { turnRow (row, 1); });
    if (track == machine)
    {
        const int own = machinePattern().rowLength[(size_t) machineVoice (row)];
        length.addItem ("With the beat (" + juce::String (pattern) + " steps)", true, own == 0, [this, row] { setRowLength (row, 0); });
        for (int n : { 3, 5, 6, 7, 9, 10, 11, 12, 13, 15, 24 })
            length.addItem (juce::String (n) + " steps", true, own == n, [this, row, n] { setRowLength (row, n); });
        menu.addSubMenu ("Its own length (polymeter)", length);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (cellRect (row, 0).toNearestInt().withX (gridArea.getX()))));
}
