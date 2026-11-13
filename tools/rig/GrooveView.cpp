#include "GrooveView.h"
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
            tick();
        };
        mute.setTooltip ("Mutes " + trackName (t) + ". Muted, it keeps going silently, so it comes back in time.");
        mute.onClick = [this]
        {
            auto& m = owner.engine.getStrip (stripFor (index)).mute;
            m = ! m.load();
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
            setTooltip (owner.stateOf (index));
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = vintage::pal();
        const bool selected = owner.track == index;
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        if (selected)
        {
            vintage::glow (g, r, 12.0f, vintage::colours::selected, 10.0f, 0.5f);
            g.setColour (vintage::colours::selected);
            g.fillRoundedRectangle (r.expanded (3.0f), 14.0f);
        }
        vintage::drawWell (g, r);
        {
            juce::Graphics::ScopedSaveState s (g);
            juce::Path clip;
            clip.addRoundedRectangle (r, 12.0f);
            g.reduceClipRegion (clip);
            g.setColour (colour);
            g.fillRect (r.withWidth (10.0f));
        }

        auto area = r.toNearestInt().withTrimmedLeft (22).reduced (0, 8).withTrimmedRight (12);
        auto top = area.removeFromTop (area.getHeight() / 2);
        g.setColour (p.wellInk);
        g.setFont (vintage::cond (juce::jmin (30.0f, top.getHeight() * 0.95f), Weight::extra, 0.04f));
        const auto name = trackName (index).toUpperCase();
        const int nameWidth = juce::jmin (top.getWidth() / 2 + 20, (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name) + 6);
        g.drawText (name, top.removeFromLeft (nameWidth), juce::Justification::centredLeft, false);
        g.setColour (p.wellInk2);
        g.setFont (vintage::barlow (13.0f));
        g.drawFittedText (shownDetail, top, juce::Justification::centredRight, 1, 0.8f);

        auto row = area;
        row.removeFromRight (go.getWidth() + mute.getWidth() + solo.getWidth() + 16);
        vintage::drawLamp (g, { (float) row.getX() + 8.0f, (float) row.getCentreY() }, shownStatus.second);
        vintage::engraved (g, shownStatus.first, row.withTrimmedLeft (24), juce::Justification::centredLeft, 13.0f, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4).withTrimmedRight (12).withTrimmedBottom (8);
        const int size = juce::jlimit (30, 44, r.getHeight() / 2);
        auto row = r.removeFromBottom (size);
        solo.setBounds (row.removeFromRight (size));
        row.removeFromRight (8);
        mute.setBounds (row.removeFromRight (size));
        row.removeFromRight (8);
        go.setBounds (row.removeFromRight (juce::jmax (size, 50)));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            owner.selectTrack (index);
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
        case machine: return "Machine";
        case bass:    return "Bass";
        case synth:   return "Synth";
        case vocoder: return "Vocoder";
        case sampler: return "Sampler";
        case loops:   return "Loops";
        case lineIn:  return "Line in";
        default:      return {};
    }
}

juce::Colour GrooveView::trackColour (int t) { return RigColours::forStrip (stripFor (t)); }

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

    for (auto* b : { &openButton, &micButton, &extraButton, &barButton, &clearButton, &quantizeButton, &soundButton, &soundPrev, &soundNext })
    {
        wellButton (*b);
        addChildComponent (b);
    }
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
            engine.vocoderOn = extraButton.getToggleState();
        else if (track == lineIn)
            setLineIn (extraButton.getToggleState());
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

    clearButton.onClick = [this] { clearPattern(); };
    quantizeButton.onClick = [this]
    {
        if (onQuantize)
            onQuantize (track == machine ? RigQuantizer::machine : track == bass ? RigQuantizer::bass
                        : track == synth ? RigQuantizer::synth : track == loops ? RigQuantizer::allLoops : RigQuantizer::mpc,
                        &quantizeButton);
    };

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
        default:      return false;
    }
}

juce::String GrooveView::trackButtonText (int t) const
{
    const bool going = isGoing (t);
    switch (t)
    {
        case vocoder:
        case lineIn:  return going ? "On" : "Off";
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
                say ("All eight loop layers have loops: clear one on the Looper, or start a new bank.");
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
    if (on)
    {
        engine.micOn = true;   // Line in hears the interface only while the input is on
        say ("Line in on (" + RigEngine::inputJackName (engine.inputJack.load())
             + "): plug your guitar, keytar or a mic into your interface. Its pedals and tuner are on the Effects page.");
    }
    else
        say ("Line in is off.");
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
        say ("All eight loop layers have loops: clear one on the Looper, or start a new bank.");
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

void GrooveView::selectTrack (int t)
{
    if (! juce::isPositiveAndBelow (t, (int) numTracks))
        return;
    track = t;
    // The keys play the track you're on (the loops have no notes, so the keys stay put).
    if (const int dest = destFor (t); dest >= 0)
        engine.getRouting().keysTo = dest;
    // The keyboard's drum pads play the drums you're on: the MPC's pads or the drum machine.
    if (t == drums || t == machine)
        engine.getRouting().padsTo = (int) (t == machine ? RigRouting::Dest::drums : RigRouting::Dest::mpc);
    heldCell = dragStep = -1;
    buildTrack();
    setFollowKnobs();
    for (auto* c : cards)
        c->tick();
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
            engine.getStrip (st).mute = mutesBeforeSolo[(size_t) st];
        soloTrack = -1;
    }
    else
    {
        if (soloTrack < 0)
            for (int st = 0; st < RigEngine::numStrips; ++st)
                mutesBeforeSolo[(size_t) st] = engine.getStrip (st).mute.load();
        for (int other = 0; other < numTracks; ++other)
            engine.getStrip (stripFor (other)).mute = other != t;
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
        case machine: return { RigEngine::drumsTool, machineKnobs[index].id };
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
                add (name, "Vocoder sound: " + juce::String (name) + ".");
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
                            : track == machine ? machineKnobs : fxKnobs;
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

    openButton.setButtonText ("Open " + tabFor (track) + juce::String::fromUTF8 (" \xe2\x80\xba"));
    openButton.setTooltip ("Everything " + trackName (track) + " can do, on its own page.");
    micButton.setVisible (track == vocoder && showMic);
    extraButton.setVisible (track == bass || track == vocoder || track == machine || track == lineIn);
    for (auto* b : { &soundButton, &soundPrev, &soundNext })
        b->setVisible (hasKeys());
    screenKeys.setVisible (hasKeys());
    keysState.allNotesOff (1);
    barButton.setVisible (track == drums || (track == machine && machineLength() > 16));
    clearButton.setVisible (track == drums || track == bass || track == synth || track == machine);
    quantizeButton.setVisible (track == drums || track == bass || track == synth || track == machine || track == loops);
    quantizeButton.setTooltip (track == drums ? "Quantize the MPC's beat: pull its notes onto a grid (Cmd+U does it straight away)."
                               : track == loops ? "Quantize the loops: slide each hit onto the beat (Cmd+U does it straight away)."
                               : "Quantize the pattern onto a coarser or triplet grid (Cmd+U does it straight away).");
    clearButton.setTooltip (track == drums ? "Clears the shown bar of the beat, for the four sounds here."
                            : track == machine ? "Clears the drum machine's pattern (every sound, not just these rows)."
                                           : track == bass ? "Clears the bass pattern's steps." : "Lights every arp step again, no accents.");
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
            barButton.setVisible (machineLength() > 16);
            if (machineLength() <= 16)
                machinePage = 0;
            barButton.setButtonText (machinePage == 0 ? "Steps 1-16" : "Steps 17-32");
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
            for (int i = 0; i < patternButtons.size(); ++i)
                setLit (patternButtons[i], engine.getLooper().getLayer (i).mute.load(), Lit::mute);
            break;
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
    switch (track)
    {
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
            const int h = p.hits[(size_t) machineVoice (row)][(size_t) step];
            c.lit = h > 0;
            if (h >= 2)
                c.label = juce::String::fromUTF8 ("\xc3\x97") + juce::String (h);
            else if (h == 1 && p.accent[(size_t) step])
                c.label = ">";
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
                case 0: c.lit = s.on; break;
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
            auto& h = p.hits[(size_t) voice][(size_t) step];
            h = (std::uint8_t) (h == 0 ? 1 : 0);
            if (step >= p.length && h != 0)
                p.length = step < 16 ? 16 : crate::drums::kMaxSteps;
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
            engine.getLooper().pressRecord (i);
            break;
        case lineIn:
            // Picking the jack means you want to hear it.
            engine.inputJack = i;
            if (! lineInLive())
                setLineIn (true);
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
    auto& h = p.hits[(size_t) machineVoice (row)][(size_t) step];
    h = (std::uint8_t) (h <= 1 ? 2 : h >= 4 ? 1 : h + 1);
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
            break;
        }
        case lineIn:
            if (i == 0)
                loopLineIn();
            else if (onOpen)
                onOpen (tabFor (lineIn));
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
    sig << track << "/" << playheadStep() << "/" << stateOf (track) << "/" << heldCell << "/" << drumBar << "/" << patternText()
        << (vintage::isStage() ? "d" : "l");
    for (int row = 0; row < numRows(); ++row)
    {
        sig << rowName (row) << ":";
        for (int i = 0; i < numSteps(); ++i)
        {
            const auto c = cellAt (row, i);
            sig << (c.lit ? 1 : 0) << (c.strong ? 1 : 0) << (c.dim ? 1 : 0) << (c.playing ? 1 : 0) << c.label << c.sub;
        }
    }
    if (track == loops)
        for (int i = 0; i < RigLooper::kLayers; ++i)
            sig << juce::roundToInt (engine.getLooper().getLayer (i).position.load() * 50.0);
    if (sig != shownSignature)
    {
        shownSignature = sig;
        repaint (wellArea);
    }
}

//==============================================================================
juce::Rectangle<float> GrooveView::cellRect (int row, int i) const
{
    const int n = numSteps(), rows = numRows();
    auto grid = gridArea.toFloat();
    if (! bigCells())
        grid.removeFromLeft ((float) kLabelWidth + 12.0f);
    const float gap = bigCells() ? 12.0f : 6.0f, groupGap = n == 16 ? 14.0f : 0.0f;
    const float w = (grid.getWidth() - gap * (n - 1) - groupGap * 3) / (float) n;
    const float x = grid.getX() + i * (w + gap) + (n == 16 ? (i / 4) * (groupGap) : 0.0f);
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
        if (track == drums)
            showDrumRowMenu (row);
        else if (track == machine)
            auditionMachineRow (row);
        return;
    }
    const auto [row, step] = cellAt (e.getPosition());
    if (step < 0)
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
        const juce::String title = (bigCells() ? (track == vocoder ? "Parts" : track == sampler ? "Pads" : "Layers") : "Steps")
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
            vintage::lcdText (g, patternText(), lcd.reduced (8.0f, 0.0f), 22.0f);
        }
        h.removeFromRight (12);
        const juce::String hint = [this]
        {
            switch (track)
            {
                case drums:   return juce::String ("Click a sound's name to change it");
                case machine: return juce::String ("Top row: accents. Right-click a step for a roll; click a name to hear it");
                case bass:    return juce::String ("Drag a note up or down");
                case synth:   return juce::String ("Hold a chord; it plays on the lit steps");
                case vocoder: return juce::String ("Switch a part on, then set how much of it underneath");
                case sampler: return juce::String ("Press a pad to play it; the keys play it too");
                case loops:   return juce::String ("Press a layer: records from the next bar");
                case lineIn:  return juce::String ("Pick the input your instrument is plugged into");
                default:      return juce::String();
            }
        }();
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
            vintage::drawStep (g, r, c.lit, i == now && ! bigCells(), (i / 4) % 2 == 1 && n == 16, fill, alpha);
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
            else if (c.label.isNotEmpty())
            {
                g.setColour (p.wellInk.withAlpha (c.dim ? 0.5f : 0.95f));
                g.setFont (vintage::cond (16.0f, Weight::extra));
                g.drawFittedText (c.label, text, juce::Justification::centred, 1, 0.6f);
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

    // Tracks down the left.
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
        place (clearButton, 76);
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
