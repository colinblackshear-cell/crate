#include "SynthScreens.h"
#include "RigEngine.h"
#include "SynthProcessor.h"
#include "BassProcessor.h"

using namespace analog;

namespace
{
// The section colours: warm, like the coloured strips on old panels.
const juce::Colour oscColour { 0xffefcb45 }, mixColour { 0xffd8c49a }, filterColour { 0xfff39b33 }, envColour { 0xffe85a3a },
    lfoColour { 0xff5fb3c4 }, voiceColour { 0xffc58fd6 }, outColour { 0xffefe6d2 }, arpColour { 0xffe0816f }, seqColour { 0xff9cc27a };

constexpr int kWood = 18, kSectionGap = 10;

juce::String noteName (double v) { return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3); }
}

AnalogScreen::AnalogScreen (juce::AudioProcessorValueTreeState& p, PluginSlot& s, juce::MidiKeyboardState& keyState,
                            const std::vector<RigSounds::Sound>& list, const juce::String& m, const juce::String& t, const juce::String& track,
                            juce::Colour a)
    : params (p), slot (s), sounds (list), model (m), title (t), trackName (track), accent (a), keys (keyState, a), shell (s, track, a)
{
    keys.setOctaveForMiddleC (3);
    addAndMakeVisible (keys);
    addChildComponent (shell);
    shell.onSay = [this] (const juce::String& text) { say (text); };
    shell.onChanged = [this]
    {
        // Loading a plugin shows it; taking it out goes back to the built-in sound.
        if (slot.isLoaded() && ! slot.bypassed.load() && ! shellShown)
            showEngine (true);
        updateEngineButtons();
    };

    for (auto* b : { &builtIn, &plugin, &soundPrev, &soundNext, &soundMenu })
        addAndMakeVisible (b);
    builtIn.setTooltip ("The " + trackName + "'s own analog sound. A plugin you loaded stays in the shell, switched off, to come back to.");
    plugin.setTooltip ("The instrument shell: your Arturia synths (or any instrument plugin) play the " + trackName + "'s keys, steps and arp.");
    builtIn.onClick = [this] { showEngine (false); };
    plugin.onClick = [this] { showEngine (true); };
    soundPrev.setTooltip ("The previous built-in sound.");
    soundNext.setTooltip ("The next built-in sound.");
    soundMenu.setTooltip ("All the built-in sounds, by kind.");
    soundPrev.onClick = [this] { stepSound (-1); };
    soundNext.onClick = [this] { stepSound (1); };
    soundMenu.onClick = [this] { showSoundMenu(); };

    shellShown = slot.isLoaded() && ! slot.bypassed.load();
    startTimerHz (20);
}

AnalogScreen::~AnalogScreen() { stopTimer(); }

void AnalogScreen::say (const juce::String& text)
{
    if (onSay)
        onSay (text);
}

AnalogScreen::Section& AnalogScreen::section (const juce::String& t, juce::Colour c, bool sound)
{
    auto s = std::make_unique<Section>();
    s->title = t;
    s->colour = c;
    s->sound = sound;
    sections.push_back (std::move (s));
    return *sections.back();
}

void AnalogScreen::add (Section& s, juce::Component& c, int width)
{
    s.items.push_back ({ &c, width });
    addAndMakeVisible (c);
}

Knob& AnalogScreen::knob (Section& s, const juce::String& id, const juce::String& name, int width, const juce::String& low, const juce::String& high)
{
    auto k = std::make_unique<Knob> (params, id, name, s.colour, low, high);
    auto& ref = *k;
    owned.push_back (std::move (k));
    add (s, ref, width);
    return ref;
}

Selector& AnalogScreen::selector (Section& s, const juce::String& id, const juce::String& name, juce::StringArray labels, int columns)
{
    auto k = std::make_unique<Selector> (params, id, name, s.colour, std::move (labels), columns);
    auto& ref = *k;
    owned.push_back (std::move (k));
    add (s, ref, ref.preferredWidth());
    return ref;
}

Switch& AnalogScreen::toggle (Section& s, const juce::String& id, const juce::String& name, bool invert)
{
    auto k = std::make_unique<Switch> (params, id, name, s.colour, invert);
    auto& ref = *k;
    owned.push_back (std::move (k));
    add (s, ref, 58);
    return ref;
}

void AnalogScreen::layoutRow (const std::vector<Section*>& row, juce::Rectangle<int> area)
{
    int wanted = 0;
    for (auto* s : row)
    {
        wanted += 24;
        for (auto& [c, w] : s->items)
            wanted += w;
    }
    wanted += kSectionGap * ((int) row.size() - 1);
    const float scale = (float) area.getWidth() / (float) juce::jmax (1, wanted);
    for (size_t i = 0; i < row.size(); ++i)
    {
        auto* s = row[i];
        int width = 24;
        for (auto& [c, w] : s->items)
            width += w;
        const int sectionWidth = i + 1 == row.size() ? area.getWidth() : juce::roundToInt ((float) width * scale);
        s->bounds = area.removeFromLeft (sectionWidth);
        area.removeFromLeft (juce::roundToInt (kSectionGap * scale));

        auto inner = s->bounds.reduced (12, 0).withTrimmedTop (24).withTrimmedBottom (8);
        const float innerScale = (float) inner.getWidth() / (float) juce::jmax (1, width - 24);
        for (auto& [c, w] : s->items)
        {
            auto cell = inner.removeFromLeft (juce::roundToInt ((float) w * innerScale));
            if (dynamic_cast<Switch*> (c) != nullptr)
                c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), 64)));
            else if (dynamic_cast<Knob*> (c) != nullptr)
                c->setBounds (cell.withSizeKeepingCentre (cell.getWidth(), juce::jmin (cell.getHeight(), cell.getWidth() + 30)));
            else
                c->setBounds (cell);
        }
    }
}

void AnalogScreen::showEngine (bool showPlugin)
{
    shellShown = showPlugin;
    if (slot.isLoaded())
        slot.bypassed = ! showPlugin;
    if (showPlugin)
        say (slot.isLoaded() ? slot.getName() + " plays the " + trackName + "."
                             : "The instrument shell: Load plugin picks one of your Arturia synths (or any instrument plugin).");
    else if (slot.isLoaded())
        say ("The " + trackName + "'s own sound plays. " + slot.getName() + " waits in the shell: press Plugin to bring it back.");
    updateEngineButtons();
    resized();
    repaint();
}

void AnalogScreen::updateEngineButtons()
{
    builtIn.lit = ! shellShown;
    plugin.lit = shellShown;
    builtIn.repaint();
    plugin.repaint();
    for (auto* b : { &soundPrev, &soundNext, &soundMenu })
        b->setVisible (! shellShown);
}

void AnalogScreen::stepSound (int delta)
{
    const int n = (int) sounds.size();
    const int i = (RigSounds::indexOf (params, sounds) + delta + n) % n;
    RigSounds::apply (params, sounds[(size_t) i]);
    repaint (soundLcd);
}

void AnalogScreen::showSoundMenu()
{
    juce::PopupMenu menu;
    juce::StringArray kinds;
    for (const auto& s : sounds)
        kinds.addIfNotAlreadyThere (s.kind);
    const auto current = RigSounds::nameOf (params, sounds);
    for (const auto& kind : kinds)
    {
        juce::PopupMenu sub;
        bool ticked = false;
        for (int i = 0; i < (int) sounds.size(); ++i)
            if (kind == sounds[(size_t) i].kind)
            {
                const bool here = current == sounds[(size_t) i].name;
                ticked = ticked || here;
                sub.addItem (1 + i, sounds[(size_t) i].name, true, here);
            }
        menu.addSubMenu (kind, sub, true, nullptr, ticked);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundMenu),
                        [safe = juce::Component::SafePointer<AnalogScreen> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            RigSounds::apply (safe->params, safe->sounds[(size_t) (result - 1)]);
                            safe->repaint();
                        });
}

void AnalogScreen::timerCallback()
{
    const auto name = RigSounds::nameOf (params, sounds);
    if (name != shownSound)
    {
        shownSound = name;
        repaint (soundLcd);
    }
    // The Groove screen (or a show) can load or switch the plugin too.
    const bool playing = slot.isLoaded() && ! slot.bypassed.load();
    if (playing && ! shellShown)
        showEngine (true);
    tick();
}

void AnalogScreen::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    // The instrument: walnut ends and the black panel.
    vintage::drawCheek (g, r.removeFromLeft (kWood));
    vintage::drawCheek (g, r.removeFromRight (kWood));
    drawPanel (g, r);
    drawNameplate (g, nameplate, model, title, accent);

    // The sound's name, on a small LCD in the nameplate.
    if (! soundLcd.isEmpty())
    {
        vintage::drawLcd (g, soundLcd.toFloat(), 5.0f);
        const auto text = shellShown ? (slot.isLoaded() ? "SHELL: " + slot.getName().toUpperCase() : juce::String ("SHELL: EMPTY"))
                                     : RigSounds::nameOf (params, sounds).toUpperCase();
        vintage::lcdText (g, text, soundLcd.toFloat().reduced (10, 0), 24.0f, juce::Justification::centredLeft);
    }
    g.setColour (juce::Colour (0xff3a3632));
    g.setFont (legendFont (11.0f, vintage::Weight::extra));
    g.drawText ("ENGINE", builtIn.getX() - 52, builtIn.getY(), 48, builtIn.getHeight(), juce::Justification::centredRight);

    for (auto& s : sections)
        if (! (s->sound && shellShown) && ! s->bounds.isEmpty())
            drawSection (g, s->bounds, s->title, s->colour);

    // The key bed's wooden lip.
    if (! keyBed.isEmpty())
    {
        vintage::drawCheek (g, keyBed.withHeight (6));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (keyBed.withTrimmedTop (6).withHeight (2));
    }
}

void AnalogScreen::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (kWood);
    r.removeFromRight (kWood);
    panelArea = r;
    auto area = r.reduced (16, 10);

    // The nameplate: model, the sound, and the engine buttons.
    nameplate = area.removeFromTop (juce::jlimit (36, 50, getHeight() / 16));
    auto plate = nameplate.reduced (8, 6);
    plate.removeFromRight (128);   // the stripes
    plugin.setBounds (plate.removeFromRight (84));
    plate.removeFromRight (4);
    builtIn.setBounds (plate.removeFromRight (84));
    plate.removeFromRight (64);
    soundMenu.setBounds (plate.removeFromRight (78));
    plate.removeFromRight (4);
    soundNext.setBounds (plate.removeFromRight (30));
    soundLcd = plate.removeFromRight (juce::jlimit (160, 280, plate.getWidth() / 3));
    plate.removeFromRight (4);
    soundPrev.setBounds (plate.removeFromRight (30));
    soundPrev.setBounds (soundPrev.getBounds().withX (soundLcd.getX() - 34));
    area.removeFromTop (10);

    // The keys at the bottom, in their bed.
    keyBed = area.removeFromBottom (juce::jlimit (52, 96, area.getHeight() / 9));
    keys.setBounds (keyBed.withTrimmedTop (8));
    const int whiteKeys = 36;
    keys.setKeyWidth ((float) keys.getWidth() / (float) whiteKeys);
    area.removeFromBottom (8);

    auto play = area.removeFromBottom (playHeight (area.getHeight()));
    area.removeFromBottom (6);
    layoutPlay (play);

    for (auto& s : sections)
        for (auto& [c, w] : s->items)
            c->setVisible (! (s->sound && shellShown));
    shell.setVisible (shellShown);
    if (shellShown)
    {
        shell.setBounds (area);
        for (auto& s : sections)
            if (s->sound)
                s->bounds = {};
    }
    else
    {
        layoutSound (area);
    }
    updateEngineButtons();
    tick();
}

//==============================================================================
SynthScreen::SynthScreen (RigEngine& engine)
    : AnalogScreen (engine.getSynth().params, engine.getSynthPlugin(), engine.getSynth().keyboardState, RigSounds::synth(), "PS-8",
                    "Polyphonic analog synthesizer", "Synth", vintage::colours::synth),
      synth (engine.getSynth()), lane (engine.getSynth(), arpColour)
{
    keys.setAvailableRange (36, 96);
    keys.setLowestVisibleKey (36);

    osc1 = &section ("Oscillator 1", oscColour);
    selector (*osc1, "wave", "Wave", { "Saw", "Square", "Triangle", "Super" });
    knob (*osc1, "octave", "Octave");
    knob (*osc1, "detune", "Detune");
    knob (*osc1, "pulseWidth", "Pulse W");

    osc2 = &section ("Oscillator 2", oscColour);
    selector (*osc2, "osc2Wave", "Wave", { "Off", "Saw", "Square", "Triangle", "Sine" });
    knob (*osc2, "osc2Semi", "Tune");
    knob (*osc2, "osc2Fine", "Fine");
    knob (*osc2, "osc2Level", "Level");

    mixer = &section ("Mixer", mixColour);
    knob (*mixer, "subLevel", "Sub");
    knob (*mixer, "noiseLevel", "Noise");

    filter = &section ("Filter", filterColour);
    selector (*filter, "filterType", "Type", { "Low 24", "Low 12", "Band", "High" });
    knob (*filter, "cutoff", "Cutoff", 72);
    knob (*filter, "resonance", "Reso");
    knob (*filter, "envMod", "Env amt");
    knob (*filter, "keyTrack", "Key trk");
    knob (*filter, "velFilter", "Velocity");

    filterEnv = &section ("Filter envelope", envColour);
    knob (*filterEnv, "filterAttack", "Attack");
    knob (*filterEnv, "filterDecay", "Decay");
    knob (*filterEnv, "filterSustain", "Sustain");

    ampEnv = &section ("Amp envelope", envColour);
    knob (*ampEnv, "attack", "Attack");
    knob (*ampEnv, "decay", "Decay");
    knob (*ampEnv, "sustain", "Sustain");
    knob (*ampEnv, "release", "Release");

    lfo = &section ("LFO", lfoColour);
    selector (*lfo, "lfoShape", "Shape", { "Sine", "Triangle", "Saw", "Square", "Random" });
    knob (*lfo, "lfoRate", "Rate").slider.setTooltip ("The LFO's speed, in time with the tempo.");
    knob (*lfo, "lfoDepth", "Depth");
    selector (*lfo, "lfoDest", "To", { "Filter", "Pitch", "Volume", "Pulse W" });

    voice = &section ("Voices", voiceColour);
    selector (*voice, "voiceMode", "Mode", { "Poly", "Mono", "Legato" });
    knob (*voice, "glide", "Glide");

    output = &section ("Output", outColour);
    knob (*output, "drive", "Drive");
    knob (*output, "width", "Width");
    knob (*output, "volume", "Volume");

    // The LFO's rate list runs 4 bars (slow) to 1/16 T: the knob's left end is slow.
    arp = &section ("Arpeggiator", arpColour, false);
    toggle (*arp, "arpOn", "Arp").setTooltip ("Hold a chord and the arp plays its notes in time, on the steps lit in the rhythm.");
    toggle (*arp, "latch", "Hold").setTooltip ("Keeps the arp playing after you let go. Play a new chord to change it.");
    selector (*arp, "arpMode", "Mode", { "Up", "Down", "Up+down", "Random", "Played", "Chord", "Converge", "Pinky" }, 2)
        .setTooltip ("The order it plays the chord's notes. Chord plays them all at once; Converge goes outside in; Pinky keeps going back to the top note.");
    knob (*arp, "arpSpeed", "Speed", 76, "SLOW", "FAST").setTooltip ("How fast it goes, in time with the tempo: left is slower (down to a note every 8 bars), "
                                                                    "right is faster. D = dotted, T = triplets.");
    toggle (*arp, "arpSync", "Free", true).setTooltip ("On: the arp ignores the tempo and runs at the Free rate (in Hz). Off: in time.");
    knob (*arp, "arpHz", "Free rate").setTooltip ("The arp's speed when Free is on, in notes a second.");
    knob (*arp, "arpOctaves", "Octaves");
    knob (*arp, "arpGate", "Gate").setTooltip ("How long each note holds: short and plucky, or long and smooth.");
    knob (*arp, "arpSwing", "Swing").setTooltip ("Pushes every second step later, for a shuffle. About two thirds is a triplet swing.");
    knob (*arp, "arpRepeats", "Repeats").setTooltip ("Plays each note this many times before the next.");
    knob (*arp, "arpChance", "Chance").setTooltip ("The chance each lit step plays: turn it down for a looser, random pattern.");

    rhythm = &section ("Rhythm", arpColour, false);
    rhythm->items.push_back ({ &lane, 400 });
    addAndMakeVisible (lane);
    for (auto* b : { &shorter, &longer, &allOn })
        addAndMakeVisible (b);
    addAndMakeVisible (lengthLabel);
    lengthLabel.setJustificationType (juce::Justification::centred);
    lengthLabel.setColour (juce::Label::textColourId, colours::legend);
    lengthLabel.setFont (legendFont (14.0f));
    shorter.setTooltip ("A shorter rhythm (fewer steps before it goes round).");
    longer.setTooltip ("A longer rhythm.");
    allOn.setTooltip ("Lights every step again, no accents.");
    auto editLength = [this] (int delta)
    {
        auto r = synth.getRhythm();
        r.length = juce::jlimit (1, crate::synthtool::kRhythmSteps, r.length + delta);
        synth.setRhythm (r);
    };
    shorter.onClick = [editLength] { editLength (-1); };
    longer.onClick = [editLength] { editLength (1); };
    allOn.onClick = [this] { synth.setRhythm (crate::synthtool::Rhythm()); lane.repaint(); };

    updateEngineButtons();
}

int SynthScreen::playHeight (int available) const { return juce::jlimit (190, 270, available * 42 / 100); }

void SynthScreen::layoutSound (juce::Rectangle<int> area)
{
    const int rowH = (area.getHeight() - 8) / 2;
    layoutRow ({ osc1, osc2, mixer, filter }, area.removeFromTop (rowH));
    area.removeFromTop (8);
    layoutRow ({ filterEnv, ampEnv, lfo, voice, output }, area.removeFromTop (rowH));
}

void SynthScreen::layoutPlay (juce::Rectangle<int> area)
{
    const int rhythmH = juce::jlimit (78, 104, area.getHeight() * 38 / 100);
    auto lanes = area.removeFromBottom (rhythmH);
    area.removeFromBottom (6);
    layoutRow ({ arp }, area);

    rhythm->bounds = lanes;
    auto inner = lanes.reduced (12, 0).withTrimmedTop (22).withTrimmedBottom (6);
    auto side = inner.removeFromRight (190);
    auto top = side.removeFromTop (side.getHeight() / 2).reduced (0, 1);
    shorter.setBounds (top.removeFromLeft (36));
    longer.setBounds (top.removeFromRight (36));
    lengthLabel.setBounds (top);
    allOn.setBounds (side.reduced (36, 1));
    inner.removeFromRight (10);
    lane.setBounds (inner);
}

void SynthScreen::tick()
{
    lengthLabel.setText (juce::String (synth.getRhythm().length) + " STEPS", juce::dontSendNotification);
    lane.repaint();
}

juce::Rectangle<float> SynthScreen::RhythmLane::cell (int step, int row) const
{
    const float labelW = 64.0f;
    const float w = ((float) getWidth() - labelW) / crate::synthtool::kRhythmSteps;
    const float h = (float) getHeight() * (row == 0 ? 0.6f : 0.4f);
    return { labelW + (float) step * w, row == 0 ? 0.0f : (float) getHeight() * 0.6f, w, h };
}

void SynthScreen::RhythmLane::paint (juce::Graphics& g)
{
    const auto r = processor.getRhythm();
    const int now = processor.getArpStep();
    g.setFont (legendFont (12.0f));
    g.setColour (colours::legendDim);
    g.drawText ("PLAY", juce::Rectangle<float> (0, 0, 58, cell (0, 0).getHeight()), juce::Justification::centredRight);
    g.drawText ("ACCENT", juce::Rectangle<float> (0, cell (0, 1).getY(), 58, cell (0, 1).getHeight()), juce::Justification::centredRight);
    for (int i = 0; i < crate::synthtool::kRhythmSteps; ++i)
        for (int row = 0; row < 2; ++row)
        {
            auto c = cell (i, row).reduced (2.5f, 2.0f);
            const bool lit = row == 0 ? r.on[(size_t) i] : r.accent[(size_t) i];
            vintage::drawStep (g, c, lit, i == now, (i / 4) % 2 == 1, row == 0 ? colour : juce::Colour (0xffffb23e),
                               i < r.length ? 1.0f : 0.35f);
        }
}

void SynthScreen::RhythmLane::click (juce::Point<float> at, bool drag)
{
    for (int i = 0; i < crate::synthtool::kRhythmSteps; ++i)
        for (int row = 0; row < 2; ++row)
            if (cell (i, row).contains (at))
            {
                if (drag && (i == lastStep || row != dragRow))
                    return;
                lastStep = i;
                auto r = processor.getRhythm();
                bool& flag = row == 0 ? r.on[(size_t) i] : r.accent[(size_t) i];
                if (! drag)
                {
                    dragValue = ! flag;
                    dragRow = row;
                }
                flag = dragValue;
                processor.setRhythm (r);
                repaint();
                return;
            }
}

void SynthScreen::RhythmLane::mouseDown (const juce::MouseEvent& e)
{
    lastStep = -1;
    click (e.position, false);
}

void SynthScreen::RhythmLane::mouseDrag (const juce::MouseEvent& e) { click (e.position, true); }

//==============================================================================
BassScreen::BassScreen (RigEngine& engine)
    : AnalogScreen (engine.getBass().params, engine.getBassPlugin(), engine.getBass().keyboardState, RigSounds::bass(), "BX-1",
                    "Bass line synthesizer", "Bass", vintage::colours::bass),
      bass (engine.getBass()), grid (engine.getBass())
{
    keys.setAvailableRange (24, 84);
    keys.setLowestVisibleKey (24);

    osc = &section ("Oscillator", oscColour);
    selector (*osc, "wave", "Wave", { "Saw", "Square", "Triangle" });
    knob (*osc, "pulseWidth", "Pulse W");
    knob (*osc, "fat", "Fat").setTooltip ("A second oscillator, a little out of tune: a thick, moving bass.");
    knob (*osc, "sub", "Sub");

    filter = &section ("Filter", filterColour);
    selector (*filter, "filterType", "Type", { "Low 24", "Low 12", "Band", "High" });
    knob (*filter, "cutoff", "Cutoff", 72);
    knob (*filter, "resonance", "Reso");
    knob (*filter, "envMod", "Env amt");
    knob (*filter, "decay", "Decay");
    knob (*filter, "keyTrack", "Key trk");

    accentGlide = &section ("Accent and glide", envColour);
    knob (*accentGlide, "accent", "Accent");
    knob (*accentGlide, "glide", "Glide");

    amp = &section ("Amp", envColour);
    knob (*amp, "attack", "Attack");
    knob (*amp, "release", "Release");

    lfo = &section ("LFO", lfoColour);
    selector (*lfo, "lfoShape", "Shape", { "Sine", "Triangle", "Saw", "Square", "Random" });
    knob (*lfo, "lfoRate", "Rate");
    knob (*lfo, "lfoDepth", "Depth");
    selector (*lfo, "lfoDest", "To", { "Filter", "Pitch", "Volume" });

    output = &section ("Output", outColour);
    knob (*output, "drive", "Drive");
    knob (*output, "volume", "Volume");

    sequencer = &section ("Sequencer", seqColour, false);
    toggle (*sequencer, "sequencer", "Run").setTooltip ("On: the pattern plays while the Rig (or Logic) plays. Off: the keys play the bass.");
    toggle (*sequencer, "keysTranspose", "Keys move").setTooltip ("On: while the pattern plays, a key moves the whole pattern to that key.");
    knob (*sequencer, "stepRate", "Steps", 92, "SLOW", "FAST").setTooltip ("How long each step is, in time with the tempo.");
    knob (*sequencer, "gate", "Gate");
    auto& root = knob (*sequencer, "root", "Key");
    root.slider.textFromValueFunction = noteName;
    root.slider.updateText();

    patterns = &section ("Patterns", seqColour, false);
    for (int i = 0; i < crate::bass::kPatterns; ++i)
    {
        auto b = std::make_unique<Button> (juce::String (i + 1), seqColour);
        b->setTooltip ("Pattern " + juce::String (i + 1) + ". While playing, it switches at the next bar.");
        b->onClick = [this, i] { bass.selectPattern (i); };
        addAndMakeVisible (*b);
        patternButtons.push_back (std::move (b));
    }
    for (auto* b : { &shorter, &longer, &randomButton, &clearButton, &copyButton })
        addAndMakeVisible (b);
    addAndMakeVisible (lengthLabel);
    addAndMakeVisible (grid);
    lengthLabel.setJustificationType (juce::Justification::centred);
    lengthLabel.setColour (juce::Label::textColourId, colours::legend);
    lengthLabel.setFont (legendFont (14.0f));
    shorter.onClick = [this] { editPattern ([] (crate::bass::Pattern& p) { p.length = juce::jmax (1, p.length - 1); }); };
    longer.onClick = [this] { editPattern ([] (crate::bass::Pattern& p) { p.length = juce::jmin (crate::bass::kSteps, p.length + 1); }); };
    randomButton.setTooltip ("A new random bass line on this pattern.");
    randomButton.onClick = [this] { editPattern ([this] (crate::bass::Pattern& p) { p = BassProcessor::randomPattern (random); }); };
    clearButton.onClick = [this] { editPattern ([] (crate::bass::Pattern& p) { p = crate::bass::Pattern(); }); };
    copyButton.setTooltip ("Copies this pattern to the next one, to make a variation.");
    copyButton.onClick = [this]
    {
        const int from = bass.getSelectedPattern();
        const int to = (from + 1) % crate::bass::kPatterns;
        bass.setPattern (to, bass.getPattern (from));
        bass.selectPattern (to);
    };

    updateEngineButtons();
}

void BassScreen::editPattern (const std::function<void (crate::bass::Pattern&)>& change)
{
    const int index = bass.getSelectedPattern();
    auto p = bass.getPattern (index);
    change (p);
    bass.setPattern (index, p);
    grid.repaint();
}

int BassScreen::playHeight (int available) const { return juce::jlimit (230, 420, available * 52 / 100); }

void BassScreen::layoutSound (juce::Rectangle<int> area)
{
    const int rowH = (area.getHeight() - 8) / 2;
    layoutRow ({ osc, filter, accentGlide }, area.removeFromTop (rowH));
    area.removeFromTop (8);
    layoutRow ({ amp, lfo, output }, area.removeFromTop (rowH));
}

void BassScreen::layoutPlay (juce::Rectangle<int> area)
{
    // The sequencer's settings on the left, the patterns and step grid beside them.
    auto left = area.removeFromLeft (juce::jlimit (300, 380, area.getWidth() / 4));
    layoutRow ({ sequencer }, left.removeFromTop (juce::jmin (left.getHeight(), 128)));
    area.removeFromLeft (kSectionGap);

    patterns->bounds = area;
    auto inner = area.reduced (12, 0).withTrimmedTop (24).withTrimmedBottom (8);
    auto top = inner.removeFromTop (28);
    for (auto& b : patternButtons)
    {
        b->setBounds (top.removeFromLeft (38).reduced (2, 0));
    }
    top.removeFromLeft (12);
    shorter.setBounds (top.removeFromLeft (30));
    lengthLabel.setBounds (top.removeFromLeft (84));
    longer.setBounds (top.removeFromLeft (30));
    copyButton.setBounds (top.removeFromRight (110).reduced (2, 0));
    clearButton.setBounds (top.removeFromRight (70).reduced (2, 0));
    randomButton.setBounds (top.removeFromRight (84).reduced (2, 0));
    inner.removeFromTop (6);
    grid.setBounds (inner);
}

void BassScreen::tick()
{
    const int selected = bass.getSelectedPattern();
    const int playing = bass.getPlayingPattern();
    for (int i = 0; i < (int) patternButtons.size(); ++i)
    {
        auto& b = *patternButtons[(size_t) i];
        const bool lit = i == selected;
        const auto text = juce::String (i + 1) + (i == playing && i != selected ? ">" : "");
        if (b.lit != lit || b.getButtonText() != text)
        {
            b.lit = lit;
            b.setButtonText (text);
            b.repaint();
        }
    }
    lengthLabel.setText (juce::String (bass.getPattern (selected).length) + " STEPS", juce::dontSendNotification);
    grid.repaint();
}
