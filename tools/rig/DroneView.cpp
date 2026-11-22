#include "DroneView.h"

#include "RigColours.h"

using vintage::Lit;
using vintage::Weight;

namespace
{
const char* const kCaption = "Sing or play into it and four strings tuned like the drone ring along. Works with the drone off too. "
                             "With the mic, wear headphones.";

juce::String dbText (float gain)
{
    if (gain <= 0.0001f)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e dB");
    const float db = juce::Decibels::gainToDecibels (gain);
    return (db > 0.05f ? "+" : "") + (std::abs (db) < 9.95f ? juce::String (db, 1) : juce::String (juce::roundToInt (db))) + " dB";
}

float getParam (const juce::AudioProcessorValueTreeState& params, const char* id)
{
    auto* v = params.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

// A small button on a dark well, lit amber when it's the one picked.
void wellButton (juce::Button& b, Lit lit = Lit::current, float fontSize = 15.0f)
{
    vintage::setInWell (b, true);
    vintage::setLit (b, lit);
    b.getProperties().set ("fontSize", fontSize);
}

struct KnobSpec { const char* id; const char* name; const char* tip; };
const KnobSpec knobSpecs[] = {
    { "droneFine", "Fine tune", "Tunes the whole drone up or down a little (in cents), to sit with a voice or an instrument that isn't at A440." },
    { "dronePace", "Pace", "The time from one pluck to the next, while it keeps its own time." },
    { "droneJawari", "Jawari", "The tanpura's curved bridge: plain strings at 0; up, each pluck blooms and buzzes as it rings." },
    { "droneDecay", "Ring", "How long each string rings after its pluck." },
    { "droneWidth", "Width", "How wide the strings sit across the speakers." },
    { "droneStrings", "Strings", "How loud the plucked strings are. Down to 0 for the reeds alone (a shruti box)." },
    { "droneReeds", "Reeds", "A held reed tone under the strings (a harmonium or shruti box), on the drone's notes." },
    { "droneResonate", "Resonate", "How much the four strings ring to what's fed into them: a voice or an instrument makes the drone sing along." },
    { "droneSwing", "Swing", "Pushes every other step late, for a swung, rolling pattern (with Steps on)." },
    { "droneHuman", "Human", "A player's hand: each pluck a little early or late and a little softer or harder. 0 is exact." },
    { "droneBright", "Bright", "Darker (down) or brighter (up): how much of the high harmonics you hear." },
    { "droneBody", "Body", "How much of the strings' low fundamental you hear: thinner down, fuller up." },
    { "droneDetune", "Detune", "Each string a few cents apart from the next, for a wide, chorused, slightly out-of-tune bed." },
    { "droneSpace", "Space", "A big room around the strings: up for a long, washed-out reverb." },
};

const char* const orderTips[] = { "The classic tanpura order: the first string, Sa, Sa, then low Sa, round and round.",
                                  "The other way round: low Sa first, back up to the first string.",
                                  "A different string each time (never the same one twice), Sa most often: it never quite repeats.",
                                  "Two at once: the first string with low Sa, then the two Sa strings together.",
                                  "All four strings strummed together, one after another, quickly." };
const char* const voiceTips[] = { "Tanpura: plucked strings with the jawari's buzz (the classic drone).",
                                  "Bowed: each string swells in slowly and holds, like a bowed string section.",
                                  "Swarmandal: a bright, harp-like zither, an octave up, quick and sparkling.",
                                  "Electric: a held, filtered synth tone that slowly opens and closes.",
                                  "Bell: glassy, ringing bells (not in tune with each other's harmonics: a metallic shimmer)." };
const char* const stepCountNames[] = { "Off", "8", "16" };
const char* const stepRateNames[] = { "1/4", "1/8", "1/16" };

constexpr size_t kFirstPatternKnob = 8;   // knobSpecs from Swing on are Pattern's and Tone's

const char* const firstNames[] = { "Pa", "Ma", "Ni", "Sa" };
const char* const firstTips[] = { "The first string plays Pa, the fifth over Sa: the classic drone.",
                                  "The first string plays Ma, the fourth: for ragas without Pa (evening ragas).",
                                  "The first string plays Ni, the seventh: a yearning, open drone.",
                                  "The first string plays Sa too: just the note, in octaves." };
const char* const pluckNames[] = { "Own time", "Every beat", "Every 2 beats", "Every bar" };
}

DroneView::DroneView (RigEngine& e) : engine (e)
{
    setOpaque (true);
    auto& params = engine.getDrone().params;
    const auto ring = RigColours::drone;

    onButton.inWell = true;
    onButton.fontSize = 24.0f;
    onButton.setTooltip ("Turns the drone on or off. It doesn't wait for Play: on, it sounds straight away.");
    onButton.onClick = [this]
    {
        const bool on = ! engine.getDrone().isOn();
        setChoice ("droneOn", on ? 1 : 0);
        say (on ? juce::String ("The drone is on. It doesn't wait for Play.") : juce::String ("The drone is off (its strings ring out)."));
        update();
    };
    addAndMakeVisible (onButton);

    soundButton.inWell = true;
    soundButton.fontSize = 18.0f;
    soundButton.sub = "ready-made drones";
    soundButton.setTooltip ("Ready-made drones: Classic Pa, Evening Ma, Deep and slow, Shruti box... They set the strings, the jawari, "
                            "the pace and the reeds; the note and On stay as they are.");
    soundButton.onClick = [this] { showSoundMenu(); };
    addAndMakeVisible (soundButton);

    const auto notes = DroneProcessor::rootNames();
    for (int i = 0; i < notes.size(); ++i)
    {
        auto* b = noteKeys.add (new juce::TextButton (notes[i]));
        wellButton (*b, Lit::current, 15.0f);
        b->setTooltip ("The drone's note (Sa): " + notes[i] + ".");
        b->onClick = [this, i] { setChoice ("droneRoot", i); update(); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        const char* names[] = { "Low", "Middle", "High" };
        auto* b = octaveButtons.add (new juce::TextButton (names[i]));
        wellButton (*b);
        b->setTooltip (juce::String ("The drone's octave: ") + names[i] + ".");
        b->onClick = [this, i] { setChoice ("droneOctave", i); update(); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 4; ++i)
    {
        auto* b = firstButtons.add (new juce::TextButton (firstNames[i]));
        wellButton (*b);
        b->setTooltip (firstTips[i]);
        b->onClick = [this, i] { setChoice ("droneFirst", i); update(); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 4; ++i)
    {
        auto* b = pluckButtons.add (new juce::TextButton (pluckNames[i]));
        wellButton (*b, Lit::current, 14.0f);
        b->setTooltip (i == 0 ? juce::String ("The strings are plucked one after another at their own pace (the Pace knob), whether or not Kaleidobox plays.")
                              : "A pluck " + juce::String (pluckNames[i]).toLowerCase() + ", in time with Kaleidobox while it plays "
                                "(stopped, it keeps that pace on its own).");
        b->onClick = [this, i] { setChoice ("droneSync", i); update(); };
        addAndMakeVisible (b);
    }

    wellButton (followButton, Lit::current, 15.0f);
    followButton.setClickingTogglesState (true);
    followButton.setTooltip ("Follow keys: the last note you play becomes the drone's note.");
    followAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (params, "droneFollow", followButton);
    addAndMakeVisible (followButton);

    wellButton (chordsKeyButton, Lit::none, 14.0f);
    chordsKeyButton.onClick = [this]
    {
        const int key = juce::jlimit (0, 11, engine.getChords().fillKey);
        setChoice ("droneRoot", key);
        say ("The drone is on " + DroneProcessor::rootNames()[key] + ", the chord builder's key.");
        update();
    };
    addAndMakeVisible (chordsKeyButton);

    wellButton (feedButton, Lit::none, 15.0f);
    feedButton.setTooltip ("What the strings ring to: a track as you hear it (after its effects and fader), the mic or line in, or everything.");
    feedButton.onClick = [this] { showFeedMenu(); };
    addAndMakeVisible (feedButton);

    // Volume: the drone's Mixer fader, so turning it down here is turning it down there.
    levelKnob.inWell = true;
    levelKnob.sideways = true;
    levelKnob.nameSize = 13.0f;
    levelKnob.slider.setRange (0.0, 1.0, 0.0);
    levelKnob.slider.setDoubleClickReturnValue (true, 0.8);
    levelKnob.slider.textFromValueFunction = [] (double v) { return dbText (RigEngine::faderGain ((float) v)); };
    levelKnob.slider.setValue (engine.getStrip (RigEngine::droneStrip).level.load(), juce::dontSendNotification);
    levelKnob.slider.setTooltip ("The drone's volume (the same as its fader on the Mixer). Drag up or down; double-click for 0 dB.");
    levelKnob.slider.onValueChange = [this]
    {
        engine.getStrip (RigEngine::droneStrip).level = (float) levelKnob.slider.getValue();
        levelKnob.repaint();
    };
    addAndMakeVisible (levelKnob);

    wellButton (micButton, Lit::arm, 14.0f);
    micButton.setToggleState (true, juce::dontSendNotification);   // (lit, so it's seen)
    micButton.setTooltip ("The strings hear your mic or line in only while the mic is on (it's off when Kaleidobox starts, so the speakers "
                          "can't feed back). Turns it on; wear headphones.");
    micButton.onClick = [this] { turnMicOn(); };
    addChildComponent (micButton);

    for (int i = 0; i < crate::drone::numOrders; ++i)
    {
        auto* b = orderButtons.add (new juce::TextButton (crate::drone::orderName (i)));
        wellButton (*b, Lit::current, 14.0f);
        b->setTooltip (orderTips[i]);
        b->onClick = [this, i] { setChoice ("droneOrder", i); say (juce::String ("The strings go ") + crate::drone::orderName (i) + "."); update(); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto* b = stepCountButtons.add (new juce::TextButton (stepCountNames[i]));
        wellButton (*b, Lit::current, 14.0f);
        b->setTooltip (i == 0 ? juce::String ("Steps off: the strings go round in the order above, at the Pluck setting.")
                              : juce::String (i == 1 ? "8" : "16") + " steps you draw: click a square to pluck that string on that step "
                                "(a column of several plucks them together). In time with Kaleidobox while it plays.");
        b->onClick = [this, i]
        {
            engine.getDrone().setStepCount (i);
            say (i == 0 ? juce::String ("Steps off: the strings go round in their order.")
                        : juce::String ("Steps on: click the squares to draw which strings pluck on which step."));
            update();
            repaint (patternWell);
        };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto* b = stepRateButtons.add (new juce::TextButton (stepRateNames[i]));
        wellButton (*b, Lit::current, 14.0f);
        b->setTooltip (juce::String ("Each step is ") + (i == 0 ? "a beat" : i == 1 ? "half a beat" : "a quarter of a beat") + " long.");
        b->onClick = [this, i] { setChoice ("droneStepRate", i); update(); };
        addAndMakeVisible (b);
    }
    wellButton (clearStepsButton, Lit::none, 13.0f);
    clearStepsButton.setTooltip ("Clears every step (all rests).");
    clearStepsButton.onClick = [this]
    {
        engine.getDrone().clearSteps();
        say ("The steps are cleared: click the squares to draw a new pattern.");
        repaint (patternWell);
    };
    addAndMakeVisible (clearStepsButton);
    for (int i = 0; i < crate::drone::numVoices; ++i)
    {
        auto* b = voiceButtons.add (new juce::TextButton (crate::drone::voiceName (i)));
        wellButton (*b, Lit::current, 14.0f);
        b->setTooltip (voiceTips[i]);
        b->onClick = [this, i] { setChoice ("droneVoice", i); say (juce::String ("The strings are a ") + juce::String (crate::drone::voiceName (i)).toLowerCase() + " now."); update(); };
        addAndMakeVisible (b);
    }

    for (auto* tab : { &stringsTab, &patternTab })
    {
        wellButton (*tab, Lit::current, 14.0f);
        tab->onClick = [this, tab] { showPage (tab == &patternTab ? 1 : 0); };
        addChildComponent (tab);
    }
    stringsTab.setTooltip ("The strings, their tuning, how they play, and singing into them.");
    patternTab.setTooltip ("Pattern (the order the strings go in, or your own steps) and Tone (the instrument they are).");

    for (const auto& spec : knobSpecs)
    {
        auto knob = std::make_unique<vintage::Knob> (params, spec.id, spec.name, ring);
        knob->inWell = true;
        knob->nameSize = 13.0f;
        knob->slider.setTooltip (juce::String (spec.tip) + " Drag up or down; double-click for the default.");
        addAndMakeVisible (*knob);
        knobs.push_back (std::move (knob));
    }

    update();
    startTimerHz (30);
}

DroneView::~DroneView()
{
    stopTimer();
}

void DroneView::say (const juce::String& text)
{
    if (onSay)
        onSay (text);
}

vintage::Knob* DroneView::knobFor (const juce::String& paramId) const
{
    for (size_t i = 0; i < knobs.size(); ++i)
        if (paramId == knobSpecs[i].id)
            return knobs[i].get();
    return nullptr;
}

int DroneView::choice (const char* id) const
{
    return juce::roundToInt (getParam (engine.getDrone().params, id));
}

// A setting changed from a button, as a touch: knob Learn, macros and automation see it.
void DroneView::setChoice (const char* id, int index)
{
    if (auto* p = engine.getDrone().params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
}

void DroneView::showSoundMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Ready-made drones");
    const auto names = DroneProcessor::presetNames();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundButton),
                        [safe = juce::Component::SafePointer<DroneView> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            auto& d = safe->engine.getDrone();
                            d.loadPreset (result - 1);
                            safe->say ("The drone is " + DroneProcessor::presetNames()[result - 1] + " now (its note and On stay as they were)."
                                       + (d.isOn() ? juce::String() : juce::String (" Press On to hear it.")));
                            safe->update();
                        });
}

void DroneView::showFeedMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("The strings ring to");
    const auto names = DroneProcessor::feedNames();
    const int now = engine.getDrone().getFeed();
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i], true, i == now);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (feedButton),
                        [safe = juce::Component::SafePointer<DroneView> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            safe->setChoice ("droneFeed", result - 1);
                            // Picking what they ring to means you want to hear it: Resonate comes up if it was at 0,
                            // and the mic comes on for the mic (as Monitor does on the Vocoder).
                            juce::String extra;
                            if (getParam (safe->engine.getDrone().params, "droneResonate") <= 0.001f)
                            {
                                if (auto* p = safe->engine.getDrone().params.getParameter ("droneResonate"))
                                {
                                    p->beginChangeGesture();
                                    p->setValueNotifyingHost (p->convertTo0to1 (0.6f));
                                    p->endChangeGesture();
                                }
                                extra << " Resonate is up to 60%.";
                            }
                            if (result - 1 == DroneProcessor::feedInput && ! safe->engine.micOn.load())
                            {
                                safe->engine.micOn = true;
                                extra << " The mic is on now: wear headphones so the speakers don't ring them too.";
                            }
                            safe->say ("The strings ring to " + DroneProcessor::feedNames()[result - 1] + "." + extra);
                            safe->update();
                        });
}

void DroneView::turnMicOn()
{
    engine.micOn = true;
    say ("The mic is on: sing and the strings ring along. Wear headphones so the speakers don't ring them too.");
    update();
}

juce::Rectangle<int> DroneView::stringArea (int string) const
{
    if (! juce::isPositiveAndBelow (string, crate::drone::kStrings))
        return {};
    const int h = stringsArea.getHeight() / crate::drone::kStrings;
    return { stringsArea.getX(), stringsArea.getY() + string * h, stringsArea.getWidth(), h };
}

void DroneView::pressString (int string)
{
    if (! juce::isPositiveAndBelow (string, crate::drone::kStrings))
        return;
    engine.pluckDrone (string);
    flash[(size_t) string] = 1.0f;
    if (! engine.getDrone().isOn())
        say ("Plucked. The drone is off, so the string rings once: press On for the drone.");
    repaint (stringsWell);
}

void DroneView::showPage (int which)
{
    page = juce::jlimit (0, 1, which);
    resized();
    update();
    repaint();
}

std::vector<juce::Component*> DroneView::mainControls()
{
    std::vector<juce::Component*> list { &onButton, &soundButton, &followButton, &chordsKeyButton, &feedButton, &levelKnob, &micButton };
    for (auto* group : { &noteKeys, &octaveButtons, &firstButtons, &pluckButtons })
        for (auto* b : *group)
            list.push_back (b);
    for (size_t i = 0; i < knobs.size(); ++i)
        if (i < kFirstPatternKnob)
            list.push_back (knobs[i].get());
    return list;
}

std::vector<juce::Component*> DroneView::moreControls()
{
    std::vector<juce::Component*> list { &clearStepsButton };
    for (auto* group : { &orderButtons, &stepCountButtons, &stepRateButtons, &voiceButtons })
        for (auto* b : *group)
            list.push_back (b);
    for (size_t i = kFirstPatternKnob; i < knobs.size(); ++i)
        list.push_back (knobs[i].get());
    return list;
}

juce::Rectangle<int> DroneView::stepCell (int step, int string) const
{
    if (gridArea.isEmpty() || ! juce::isPositiveAndBelow (step, 16) || ! juce::isPositiveAndBelow (string, crate::drone::kStrings))
        return {};
    const auto cells = gridArea.withTrimmedLeft (gridLabelW);
    const int w = cells.getWidth() / 16, h = cells.getHeight() / crate::drone::kStrings;
    return { cells.getX() + step * w, cells.getY() + string * h, w, h };
}

void DroneView::pressStep (int step, int string)
{
    auto& d = engine.getDrone();
    if (choice ("droneStepCount") == 0)
    {
        // Drawing a step turns the steps on (as many as reach it).
        d.setStepCount (step < 8 ? 1 : 2);
        say ("Steps on: the strings pluck on the squares you light.");
    }
    else if (step >= 8 && choice ("droneStepCount") == 1)
        d.setStepCount (2);
    d.setStep (step, d.getStep (step) ^ (1 << string));
    update();
    repaint (patternWell);
}

void DroneView::mouseDown (const juce::MouseEvent& e)
{
    for (int s = 0; s < crate::drone::kStrings; ++s)
        if (stringArea (s).contains (e.getPosition()))
            return pressString (s);
    for (int step = 0; step < 16; ++step)
        for (int s = 0; s < crate::drone::kStrings; ++s)
            if (stepCell (step, s).contains (e.getPosition()))
                return pressStep (step, s);
}

//==============================================================================
// Lights the buttons to match the settings (they can change from knobs, macros, automation).
void DroneView::update()
{
    auto& d = engine.getDrone();
    auto light = [] (juce::Button& b, bool on)
    {
        if (b.getToggleState() != on)
            b.setToggleState (on, juce::dontSendNotification);
    };
    const bool on = d.isOn();
    const auto lit = on ? Lit::play : Lit::none;
    if (onButton.lit != lit)
    {
        onButton.lit = lit;
        onButton.text = on ? "On" : "Off";
        onButton.repaint();
    }
    const int root = choice ("droneRoot"), octave = choice ("droneOctave"), first = choice ("droneFirst"), sync = choice ("droneSync");
    for (int i = 0; i < noteKeys.size(); ++i)
        light (*noteKeys[i], i == root);
    for (int i = 0; i < octaveButtons.size(); ++i)
        light (*octaveButtons[i], i == octave);
    for (int i = 0; i < firstButtons.size(); ++i)
        light (*firstButtons[i], i == first);
    for (int i = 0; i < pluckButtons.size(); ++i)
        light (*pluckButtons[i], i == sync);
    const int order = choice ("droneOrder"), stepCount = choice ("droneStepCount"), stepRate = choice ("droneStepRate"), voice = choice ("droneVoice");
    for (int i = 0; i < orderButtons.size(); ++i)
    {
        light (*orderButtons[i], i == order);
        orderButtons[i]->setAlpha (stepCount == 0 ? 1.0f : 0.45f);   // (the steps play instead)
    }
    for (int i = 0; i < stepCountButtons.size(); ++i)
        light (*stepCountButtons[i], i == stepCount);
    for (int i = 0; i < stepRateButtons.size(); ++i)
    {
        light (*stepRateButtons[i], i == stepRate);
        stepRateButtons[i]->setAlpha (stepCount > 0 ? 1.0f : 0.45f);
    }
    for (int i = 0; i < voiceButtons.size(); ++i)
        light (*voiceButtons[i], i == voice);
    for (auto* b : pluckButtons)
        b->setAlpha (stepCount == 0 ? 1.0f : 0.45f);
    if (auto* swing = knobFor ("droneSwing"))
        swing->setAlpha (stepCount > 0 ? 1.0f : 0.45f);

    // Pace only counts while it keeps its own time.
    if (auto* pace = knobFor ("dronePace"))
    {
        const bool used = sync == 0 && choice ("droneStepCount") == 0;
        pace->setAlpha (used ? 1.0f : 0.4f);
        pace->slider.setTooltip (used ? juce::String (knobSpecs[1].tip) + " Drag up or down; double-click for the default."
                                      : choice ("droneStepCount") > 0
                                            ? juce::String ("Pace is the time between plucks in the strings' order. The steps are on, so they play instead "
                                                            "(at their step length): turn Steps off to use Pace.")
                                            : "Pace is the time between plucks while the drone keeps its own time. Pluck is set to "
                                                  + juce::String (pluckNames[juce::jlimit (0, 3, sync)]).toLowerCase()
                                                  + ", so it plucks with the beat instead: pick Own time to use Pace.");
    }

    const int key = juce::jlimit (0, 11, engine.getChords().fillKey);
    chordsKeyButton.setTooltip ("Sets the drone's note to the chord builder's key (" + DroneProcessor::rootNames()[key] + " now).");
    // The volume follows the Mixer's fader (and a learned knob) unless you're turning it here.
    if (! levelKnob.slider.isMouseButtonDown())
    {
        const float level = engine.getStrip (RigEngine::droneStrip).level.load();
        if (std::abs ((float) levelKnob.slider.getValue() - level) > 1.0e-4f)
            levelKnob.slider.setValue (level, juce::dontSendNotification);
    }
    const bool deaf = engine.whyDroneDeaf().isNotEmpty() && (! compact || page == 0);
    if (micButton.isVisible() != deaf)
        micButton.setVisible (deaf);

    const auto feedText = DroneProcessor::feedNames()[d.getFeed()] + juce::String::fromUTF8 (" \xe2\x96\xbe");
    if (feedButton.getButtonText() != feedText)
        feedButton.setButtonText (feedText);
}

void DroneView::timerCallback()
{
    if (isShowing())
        tick();
}

void DroneView::tick()
{
    auto& d = engine.getDrone();
    phase += 0.55;
    for (int s = 0; s < crate::drone::kStrings; ++s)
    {
        // A new pluck flashes its string.
        if (const int count = d.getPluckCount (s); count != seenPlucks[(size_t) s])
        {
            seenPlucks[(size_t) s] = count;
            flash[(size_t) s] = 1.0f;
        }
        flash[(size_t) s] *= 0.86f;
        const float target = juce::jlimit (0.0f, 1.0f, d.getRinging (s));
        shown[(size_t) s] += (target - shown[(size_t) s]) * 0.5f;
    }
    meter = juce::jmax (juce::jlimit (0.0f, 1.0f, d.getResonating()), meter * 0.85f);
    update();
    repaint (stringsWell);
    repaint (meterArea.expanded (2));
    if (const int step = d.getCurrentStep(); step != shownStep)
    {
        shownStep = step;
        repaint (gridArea.expanded (3));
    }
}

//==============================================================================
void DroneView::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    const auto colour = RigColours::drone;
    vintage::drawFace (g, getLocalBounds());

    for (auto well : { stringsWell, tuningWell, playingWell, resonateWell, patternWell, toneWell })
        if (! well.isEmpty())
            vintage::drawWell (g, well.toFloat());

    auto title = [&] (juce::Rectangle<int> well, const juce::String& text, const juce::String& sub)
    {
        if (well.isEmpty())
            return;
        auto r = well.reduced (16, 12).removeFromTop (24);
        g.setColour (p.wellInk);
        const auto upper = text.toUpperCase();
        // A long title on a narrow well goes on two lines.
        if (juce::GlyphArrangement::getStringWidth (vintage::cond (21.0f, Weight::extra, 0.1f), upper) > (float) r.getWidth())
        {
            g.setFont (vintage::cond (juce::jmin (21.0f, (float) r.getWidth() / 6.0f), Weight::extra, 0.08f));
            g.drawFittedText (upper, r.withHeight (titleTwoLines), juce::Justification::topLeft, 2, 0.8f);
            return;
        }
        g.setFont (vintage::cond (21.0f, Weight::extra, 0.1f));
        const int w = juce::jmin (r.getWidth(), (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), upper) + 8);
        g.drawText (upper, r.removeFromLeft (w), juce::Justification::centredLeft, false);
        if (sub.isNotEmpty() && r.getWidth() > 60)
            vintage::engraved (g, sub, r.withTrimmedLeft (8), juce::Justification::centredLeft, 12.0f, true);
    };

    // The strings: the drone's colour swatch, its name, and four strings ringing.
    if (! stringsWell.isEmpty())
    {
        auto r = stringsWell.reduced (16, 12).removeFromTop (24);
        auto swatch = r.removeFromLeft (18).withSizeKeepingCentre (18, 18).toFloat();
        vintage::glow (g, swatch, 4.0f, colour, 5.0f, 0.5f);
        g.setColour (colour);
        g.fillRoundedRectangle (swatch, 4.0f);
        title (stringsWell.withTrimmedLeft (30), juce::String::fromUTF8 ("Strings \xc2\xb7 Drone"), "click a string to pluck it");
    }
    auto& d = engine.getDrone();
    const bool on = d.isOn();
    const auto notes = d.stringNotes();
    const int first = choice ("droneFirst");
    for (int s = 0; s < crate::drone::kStrings; ++s)
    {
        const auto lane = stringArea (s);
        if (lane.isEmpty())
            continue;
        const float f = flash[(size_t) s], ringing = shown[(size_t) s];
        if (f > 0.02f)
        {
            g.setColour (colour.withAlpha (0.22f * f));
            g.fillRoundedRectangle (lane.toFloat().reduced (2.0f, 3.0f), 8.0f);
        }
        // Its name and its note.
        auto label = lane.withWidth (juce::jmin (118, juce::jmax (84, lane.getWidth() / 4)));
        const juce::String name = s == 0 ? juce::String (firstNames[juce::jlimit (0, 3, first)]) : s == 3 ? juce::String ("Low Sa") : juce::String ("Sa");
        g.setColour (p.wellInk.withAlpha (on || ringing > 0.05f ? 1.0f : 0.55f));
        // As big as fits ("LOW SA" in a narrow window too).
        float nameSize = juce::jmin (28.0f, lane.getHeight() * 0.36f);
        while (nameSize > 13.0f && juce::GlyphArrangement::getStringWidth (vintage::cond (nameSize, Weight::extra, 0.06f), name.toUpperCase())
                                       > (float) label.getWidth() - 10.0f)
            nameSize -= 1.0f;
        g.setFont (vintage::cond (nameSize, Weight::extra, 0.06f));
        g.drawFittedText (name.toUpperCase(), label.withTrimmedLeft (6).removeFromTop (label.getHeight() * 11 / 20), juce::Justification::bottomLeft, 1, 0.7f);
        auto noteBox = label.withTrimmedLeft (6).withTrimmedTop (label.getHeight() * 11 / 20 + 3).withHeight (juce::jmin (26, label.getHeight() / 3)).withWidth (60);
        vintage::drawLcd (g, noteBox.toFloat(), 4.0f);
        vintage::lcdText (g, notes[s], noteBox.toFloat(), juce::jmin (20.0f, noteBox.getHeight() * 0.9f));

        // The string, from the bridge (left) to its peg, vibrating as it rings.
        const float x0 = (float) label.getRight() + 14.0f, x1 = (float) lane.getRight() - 18.0f, cy = (float) lane.getCentreY();
        g.setColour (juce::Colour (0xffc9b48a).withAlpha (0.8f));
        g.fillRoundedRectangle (x0 - 8.0f, cy - lane.getHeight() * 0.32f, 6.0f, lane.getHeight() * 0.64f, 2.0f);   // the bridge
        g.fillEllipse (x1 + 2.0f, cy - 5.0f, 10.0f, 10.0f);                                                      // the peg
        const float amplitude = juce::jmin (lane.getHeight() * 0.34f, 30.0f) * std::sqrt (ringing);   // (a quiet string still shows)
        const double speed = 1.0 + 0.23 * s;
        juce::Path path;
        const int steps = 96;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float) i / steps;
            // The string's own shape (its first two modes) and the jawari's quick buzz near the bridge.
            const float shape = std::sin (juce::MathConstants<float>::pi * t) * (float) std::sin (phase * speed + s)
                                + 0.35f * std::sin (juce::MathConstants<float>::twoPi * t) * (float) std::sin (phase * speed * 2.1 + s * 1.7)
                                + 0.12f * (1.0f - t) * (float) std::sin (phase * 9.0 + t * 40.0);
            const float x = x0 + (x1 - x0) * t, y = cy + amplitude * shape * 0.8f;
            if (i == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
        const auto ink = (on || ringing > 0.05f) ? juce::Colour (0xfff1e7d1).interpolatedWith (colour, juce::jlimit (0.0f, 1.0f, ringing * 1.4f + f * 0.5f))
                                                 : p.wellInk2.withAlpha (0.6f);
        if (ringing > 0.05f)
        {
            g.setColour (colour.withAlpha (0.25f * ringing));
            g.strokePath (path, juce::PathStrokeType (s == 3 ? 9.0f : 7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (ink);
        g.strokePath (path, juce::PathStrokeType (s == 3 ? 3.5f : 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Tuning, Playing, Resonate: their titles and the names over their rows.
    title (tuningWell, "Tuning", {});
    title (playingWell, "Playing", {});
    title (resonateWell, "Sing into the strings", {});
    title (patternWell, "Pattern", "the order the strings go in, or your own steps");
    title (toneWell, "Tone", "what the strings are");
    const std::pair<juce::Rectangle<int>, const char*> labels[] = { { noteLabel, "Note (Sa)" }, { octaveLabel, "Octave" }, { firstLabel, "First string" },
                                                                    { pluckLabel, "Pluck" }, { feedLabel, "Rings to" }, { orderLabel, "Order" },
                                                                    { stepsLabel, "Steps" }, { voiceLabel, "Instrument" } };
    for (const auto& [area, text] : labels)
        if (! area.isEmpty())
            vintage::engraved (g, text, area, juce::Justification::bottomLeft, 12.0f, true);

    // The steps: a row for each string, a square for each step (lit: it plucks there), the step playing outlined.
    if (! gridArea.isEmpty())
    {
        const int count = choice ("droneStepCount") == 2 ? 16 : choice ("droneStepCount") == 1 ? 8 : 0;
        const int now = count > 0 ? d.getCurrentStep() : -1;
        for (int string = 0; string < crate::drone::kStrings; ++string)
        {
            const auto row = stepCell (0, string);
            const juce::String name = string == 0 ? juce::String (firstNames[juce::jlimit (0, 3, first)]) : string == 3 ? juce::String ("Low") : juce::String ("Sa");
            g.setColour (p.wellInk.withAlpha (count > 0 ? 0.9f : 0.5f));
            g.setFont (vintage::cond (13.0f, Weight::extra, 0.06f));
            g.drawText (name.toUpperCase(), juce::Rectangle<int> (gridArea.getX(), row.getY(), gridLabelW - 6, row.getHeight()), juce::Justification::centredLeft, false);
            for (int step = 0; step < 16; ++step)
            {
                const auto cell = stepCell (step, string).toFloat().reduced (2.0f);
                const bool lit = (engine.getDrone().getStep (step) >> string) & 1;
                auto fill = lit ? colour.withAlpha (count > 0 && step < count ? 1.0f : 0.35f)
                                : juce::Colour ((step / 4) % 2 == 0 ? 0xff3a332d : 0xff2e2925);
                if (count > 0 && step >= count)
                    fill = fill.withMultipliedAlpha (0.35f);
                g.setColour (fill);
                g.fillRoundedRectangle (cell, 3.0f);
            }
        }
        if (juce::isPositiveAndBelow (now, count))
        {
            auto column = stepCell (now, 0).getUnion (stepCell (now, crate::drone::kStrings - 1)).toFloat().reduced (0.5f);
            g.setColour (juce::Colour (0xfff1e7d1).withAlpha (0.8f));
            g.drawRoundedRectangle (column, 4.0f, 1.5f);
        }
        if (! gridHint.isEmpty())
            vintage::engraved (g, count == 0 ? "Steps off: click to draw" : "Click squares to draw",
                               gridHint.withTrimmedLeft (gridLabelW), juce::Justification::centredLeft, 11.0f, true);
    }

    // How much the strings are ringing to what's fed in.
    if (! meterArea.isEmpty())
    {
        auto m = meterArea.toFloat();
        const int count = 16;
        const float gap = 3.0f, w = (m.getWidth() - gap * (count - 1)) / count;
        const int lit = juce::roundToInt (meter * count);
        for (int i = 0; i < count; ++i)
        {
            g.setColour (i < lit ? colour.brighter (0.1f + 0.4f * (float) i / count) : juce::Colour (0xff2e2925));
            g.fillRoundedRectangle (m.getX() + i * (w + gap), m.getY(), w, m.getHeight(), 2.0f);
        }
    }
    if (! captionArea.isEmpty())
    {
        g.setColour (p.wellInk2);
        juce::GlyphArrangement words;   // laid out as resized() measured it
        words.addJustifiedText (vintage::barlow (13.0f), kCaption, (float) captionArea.getX(), (float) captionArea.getY() + 11.0f,
                                (float) captionArea.getWidth(), juce::Justification::topLeft);
        words.draw (g);
    }
}

void DroneView::resized()
{
    auto area = getLocalBounds().withTrimmedTop (6).withTrimmedBottom (14).reduced (22, 0);
    const int gap = 14;
    const int full = area.getWidth();
    // A narrow window gives the strings a little less, and the settings smaller type.
    const bool narrow = full < 900;
    // A short window shows the strings or Pattern and Tone, a tab for each; a tall one shows them all, Pattern
    // and Tone along the bottom.
    compact = getHeight() < 640;
    stringsTab.setVisible (compact);
    patternTab.setVisible (compact);
    if (compact)
    {
        auto tabs = area.removeFromTop (32);
        stringsTab.setBounds (tabs.removeFromLeft (200));
        tabs.removeFromLeft (6);
        patternTab.setBounds (tabs.removeFromLeft (200));
        area.removeFromTop (10);
        stringsTab.setToggleState (page == 0, juce::dontSendNotification);
        patternTab.setToggleState (page == 1, juce::dontSendNotification);
    }
    const bool showMain = ! compact || page == 0, showMore = ! compact || page == 1;
    for (auto* c : mainControls())
        c->setVisible (showMain && c != &micButton);
    for (auto* c : moreControls())
        c->setVisible (showMore);
    if (auto* fine = knobFor ("droneFine"))
    {
        fine->setVisible (compact ? showMore : showMain);
        if (! compact && fine->sideways)
        {
            fine->sideways = false;
            fine->nameSize = narrow ? 11.0f : 13.0f;
        }
    }
    stringsWell = tuningWell = playingWell = resonateWell = stringsArea = meterArea = captionArea = {};
    patternWell = toneWell = gridArea = gridHint = {};
    noteLabel = octaveLabel = firstLabel = pluckLabel = feedLabel = orderLabel = stepsLabel = voiceLabel = {};
    if (showMore)
    {
        auto band = compact ? area : area.removeFromBottom (juce::jlimit (212, 240, getHeight() * 30 / 100));
        area.removeFromBottom (gap);
        if (compact && band.getWidth() < 1000)
        {
            patternWell = band.removeFromTop (juce::jmax (band.getHeight() / 2, band.getHeight() - gap - 128));   // (Tone: its buttons and a row of knobs)
            band.removeFromTop (gap);
        }
        else
        {
            patternWell = band.removeFromLeft ((band.getWidth() - gap) * 60 / 100);
            band.removeFromLeft (gap);
        }
        toneWell = band;
    }
    if (showMain)
    {
        stringsWell = area.removeFromLeft (full * (narrow ? 30 : 40) / 100);
        area.removeFromLeft (gap);
        const int rest = area.getWidth() - gap * 2;
        tuningWell = area.removeFromLeft (rest * (narrow ? 36 : 34) / 100);
        area.removeFromLeft (gap);
        playingWell = area.removeFromLeft (rest * (narrow ? 35 : 36) / 100);
        area.removeFromLeft (gap);
        resonateWell = area;
    }

    const int row = juce::jlimit (28, 40, getHeight() / 20);
    for (auto* list : { &noteKeys, &octaveButtons, &firstButtons })
        for (auto* b : *list)
            b->getProperties().set ("fontSize", narrow ? 12.0f : 15.0f);
    for (auto* b : pluckButtons)
        b->getProperties().set ("fontSize", narrow ? 11.0f : 14.0f);
    for (auto* b : { &followButton, &chordsKeyButton, &feedButton })
        b->getProperties().set ("fontSize", narrow ? 12.0f : (b == &chordsKeyButton ? 14.0f : 15.0f));
    for (auto& k : knobs)
        k->nameSize = narrow ? 11.0f : 13.0f;
    octaveButtons[1]->setButtonText (narrow ? "Mid" : "Middle");
    // "Sing into the strings" on two lines when its well is narrow.
    titleTwoLines = juce::GlyphArrangement::getStringWidth (vintage::cond (21.0f, Weight::extra, 0.1f), "SING INTO THE STRINGS")
                            > (float) resonateWell.reduced (16, 12).getWidth() ? 46 : 24;
    // The strings: On and the sounds over them, then the four strings.
    if (showMain)
    {
        auto r = stringsWell.reduced (16, 12);
        r.removeFromTop (24 + 10);
        auto top = r.removeFromTop (juce::jlimit (44, 60, r.getHeight() / 8));
        levelKnob.nameSize = narrow ? 11.0f : 13.0f;
        levelKnob.setBounds (top.removeFromRight (juce::jmin (170, top.getWidth() * 38 / 100)));
        top.removeFromRight (10);
        onButton.setBounds (top.removeFromLeft (juce::jmin (140, (top.getWidth() - 10) / 3)));
        top.removeFromLeft (10);
        soundButton.setBounds (top.removeFromLeft (juce::jmin (220, top.getWidth())));
        r.removeFromTop (12);
        stringsArea = r;
    }

    // Tuning.
    if (showMain)
    {
        auto r = tuningWell.reduced (14, 12);
        r.removeFromTop (24 + 6);
        noteLabel = r.removeFromTop (18);
        r.removeFromTop (4);
        const int perRow = r.getWidth() >= 300 ? 12 : r.getWidth() >= 150 ? 6 : 4;
        const int keyW = (r.getWidth() - 4 * (perRow - 1)) / perRow;
        for (int line = 0; line < 12 / perRow; ++line)
        {
            auto keys = r.removeFromTop (row);
            for (int k = 0; k < perRow; ++k)
            {
                noteKeys[line * perRow + k]->setBounds (keys.removeFromLeft (keyW));
                keys.removeFromLeft (4);
            }
            r.removeFromTop (4);
        }
        r.removeFromTop (6);
        auto choiceRow = [&] (juce::Rectangle<int>& label, juce::OwnedArray<juce::TextButton>& buttons)
        {
            label = r.removeFromTop (18);
            r.removeFromTop (4);
            auto line = r.removeFromTop (row);
            const int n = buttons.size();
            const int w = (line.getWidth() - 4 * (n - 1)) / n;
            for (auto* b : buttons)
            {
                b->setBounds (line.removeFromLeft (w));
                line.removeFromLeft (4);
            }
            r.removeFromTop (10);
        };
        choiceRow (octaveLabel, octaveButtons);
        choiceRow (firstLabel, firstButtons);
        auto bottom = r.removeFromBottom (row * 2 + 6);
        chordsKeyButton.setBounds (bottom.removeFromBottom (row));
        bottom.removeFromBottom (6);
        followButton.setBounds (bottom);
        r.removeFromBottom (8);
        if (auto* fine = knobFor ("droneFine"); fine != nullptr && ! compact)
            fine->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 160), juce::jmin (r.getHeight(), 170)));
    }

    // Playing.
    if (showMain)
    {
        auto r = playingWell.reduced (14, 12);
        r.removeFromTop (24 + 6);
        pluckLabel = r.removeFromTop (18);
        r.removeFromTop (4);
        for (int line = 0; line < 2; ++line)
        {
            auto buttons = r.removeFromTop (row);
            const int w = (buttons.getWidth() - 4) / 2;
            for (int k = 0; k < 2; ++k)
            {
                pluckButtons[line * 2 + k]->setBounds (buttons.removeFromLeft (w));
                buttons.removeFromLeft (4);
            }
            r.removeFromTop (4);
        }
        r.removeFromTop (8);
        const char* ids[] = { "dronePace", "droneJawari", "droneDecay", "droneWidth", "droneStrings", "droneReeds" };
        const int h = r.getHeight() / 3, w = r.getWidth() / 2;
        for (int i = 0; i < 6; ++i)
            if (auto* k = knobFor (ids[i]))
                k->setBounds (juce::Rectangle<int> (r.getX() + (i % 2) * w, r.getY() + (i / 2) * h, w, h).reduced (4, 2));
    }

    // Pattern: Order and Steps on the left, the grid in the middle, Swing and Human on the right; in a narrow
    // well, Order, then Steps with Swing and Human beside them, then the grid across the whole well.
    if (showMore)
    {
        const int small = juce::jlimit (26, 32, row);
        for (auto* list : { &orderButtons, &stepCountButtons, &stepRateButtons, &voiceButtons })
            for (auto* b : *list)
                b->getProperties().set ("fontSize", narrow ? 11.0f : 14.0f);
        auto r = patternWell.reduced (14, 12);
        r.removeFromTop (24 + 6);
        auto rowOf = [] (juce::Rectangle<int> line, juce::OwnedArray<juce::TextButton>& buttons, int count, int from = 0)
        {
            const int w = (line.getWidth() - 4 * (count - 1)) / count;
            for (int i = from; i < from + count && i < buttons.size(); ++i)
            {
                buttons[i]->setBounds (line.removeFromLeft (w));
                line.removeFromLeft (4);
            }
        };
        auto stepButtons = [&] (juce::Rectangle<int> line)
        {
            // Off, 8, 16, then how long each step is.
            const int w = (line.getWidth() - 4 * 4 - 10) / 6;
            for (auto* list : { &stepCountButtons, &stepRateButtons })
            {
                for (auto* b : *list)
                {
                    b->setBounds (line.removeFromLeft (w));
                    line.removeFromLeft (4);
                }
                line.removeFromLeft (6);
            }
        };
        auto* swing = knobFor ("droneSwing");
        auto* human = knobFor ("droneHuman");
        const bool stacked = r.getWidth() < 680;
        if (stacked)
        {
            // (A short well: the buttons say what they are, without the label over them.)
            if (patternWell.getHeight() >= 260)
            {
                orderLabel = r.removeFromTop (16);
                r.removeFromTop (3);
            }
            rowOf (r.removeFromTop (small), orderButtons, orderButtons.size());
            r.removeFromTop (8);
            auto line = r.removeFromTop (juce::jmax (small, 40));
            stepsLabel = line.withHeight (0);   // (the buttons say what they are)
            for (auto* k : { swing, human })
                if (k != nullptr && ! k->sideways)
                {
                    k->sideways = true;
                    k->nameSize = 11.0f;
                }
            const int kw = juce::jmin (120, line.getWidth() / 5);
            if (human != nullptr)
                human->setBounds (line.removeFromRight (kw));
            if (swing != nullptr)
                swing->setBounds (line.removeFromRight (kw));
            line.removeFromRight (8);
            auto buttons = line.withSizeKeepingCentre (line.getWidth(), small);
            clearStepsButton.setBounds (buttons.removeFromRight (juce::jmin (70, buttons.getWidth() / 6)));
            buttons.removeFromRight (6);
            stepButtons (buttons);
            r.removeFromTop (6);
        }
        else
        {
            auto left = r.removeFromLeft (juce::jlimit (220, 330, r.getWidth() * 37 / 100));
            r.removeFromLeft (16);
            auto knobsArea = r.removeFromRight (juce::jlimit (130, 200, r.getWidth() * 28 / 100));
            r.removeFromRight (12);
            orderLabel = left.removeFromTop (18);
            left.removeFromTop (4);
            // Five in a row, or three and two when that's too narrow for their names.
            if (left.getWidth() < 300)
            {
                rowOf (left.removeFromTop (small), orderButtons, 3);
                left.removeFromTop (4);
                rowOf (left.removeFromTop (small).withTrimmedRight ((left.getWidth() + 4) / 3), orderButtons, 2, 3);
            }
            else
                rowOf (left.removeFromTop (small), orderButtons, orderButtons.size());
            left.removeFromTop (8);
            stepsLabel = left.removeFromTop (18);
            left.removeFromTop (4);
            stepButtons (left.removeFromTop (small));
            for (auto* k : { swing, human })
                if (k != nullptr && k->sideways)
                {
                    k->sideways = false;
                    k->nameSize = narrow ? 11.0f : 13.0f;
                }
            const int kw = knobsArea.getWidth() / 2;
            if (swing != nullptr)
                swing->setBounds (knobsArea.removeFromLeft (kw).reduced (2, 0));
            if (human != nullptr)
                human->setBounds (knobsArea.reduced (2, 0));
        }
        for (auto* k : { swing, human })
            if (k != nullptr)
                k->resized();
        if (! stacked)
        {
            // Over the grid: what it says, and Clear.
            auto top = r.removeFromTop (small);
            clearStepsButton.setBounds (top.removeFromRight (juce::jmin (80, top.getWidth() / 4)));
            gridHint = top.withTrimmedRight (8);
            r.removeFromTop (4);
        }
        // The grid: as tall as it can be, squares no wider than they're tall.
        gridLabelW = narrow ? 34 : 44;
        const int cellH = juce::jmin (r.getHeight() / crate::drone::kStrings, 34);
        const int cellW = juce::jmin ((r.getWidth() - gridLabelW) / 16, cellH + 10);
        gridArea = juce::Rectangle<int> (r.getX(), r.getY(), gridLabelW + cellW * 16, cellH * crate::drone::kStrings);
    }

    // Tone: the instrument, then its knobs (a short well: side by side, their names beside them). On a short
    // window's Pattern and tone page, Fine tune comes here too.
    if (showMore)
    {
        auto r = toneWell.reduced (14, 12);
        r.removeFromTop (24 + 6);
        if (! compact)
        {
            voiceLabel = r.removeFromTop (16);
            r.removeFromTop (4);
        }
        const int small = juce::jlimit (26, 32, row);
        auto line = r.removeFromTop (small);
        const int w = (line.getWidth() - 4 * 4) / 5;
        for (auto* b : voiceButtons)
        {
            b->setBounds (line.removeFromLeft (w));
            line.removeFromLeft (4);
        }
        r.removeFromTop (8);
        std::vector<vintage::Knob*> list;
        for (auto* id : { "droneBright", "droneBody", "droneDetune", "droneSpace" })
            list.push_back (knobFor (id));
        if (compact)
            list.push_back (knobFor ("droneFine"));
        const bool flat = r.getHeight() < 96;
        const int kw = r.getWidth() / (int) list.size();
        for (size_t i = 0; i < list.size(); ++i)
            if (auto* k = list[i])
            {
                k->sideways = flat;
                k->nameSize = flat || narrow ? 11.0f : 13.0f;
                k->setBounds (juce::Rectangle<int> (r.getX() + (int) i * kw, r.getY(), kw, flat ? juce::jmin (r.getHeight(), 48) : r.getHeight()).reduced (3, 0));
                k->resized();
            }
    }

    // Resonate: what it does (under its title), its knob, what it rings to, how much it rings.
    if (showMain)
    {
        auto r = resonateWell.reduced (14, 12);
        r.removeFromTop (titleTwoLines + 6);
        {
            // As tall as the words need at this width.
            juce::GlyphArrangement words;
            words.addJustifiedText (vintage::barlow (13.0f), kCaption, 0.0f, 0.0f, (float) r.getWidth(), juce::Justification::topLeft);
            const int tall = words.getNumGlyphs() > 0 ? juce::roundToInt (words.getBoundingBox (0, -1, true).getHeight()) : 0;
            captionArea = r.removeFromTop (juce::jlimit (32, 124, tall + 10));
        }
        r.removeFromTop (6);
        meterArea = r.removeFromBottom (14);
        r.removeFromBottom (12);
        micButton.setBounds (r.removeFromBottom (row));
        r.removeFromBottom (8);
        auto feed = r.removeFromBottom (row);
        feedButton.setBounds (feed);
        feedLabel = r.removeFromBottom (22);
        r.removeFromBottom (10);
        if (auto* res = knobFor ("droneResonate"))
            res->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 170), juce::jmin (r.getHeight(), 190)));
    }
}
