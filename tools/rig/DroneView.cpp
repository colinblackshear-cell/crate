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
};

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

void DroneView::mouseDown (const juce::MouseEvent& e)
{
    for (int s = 0; s < crate::drone::kStrings; ++s)
        if (stringArea (s).contains (e.getPosition()))
            return pressString (s);
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

    // Pace only counts while it keeps its own time.
    if (auto* pace = knobFor ("dronePace"))
    {
        const bool used = sync == 0;
        pace->setAlpha (used ? 1.0f : 0.4f);
        pace->slider.setTooltip (used ? juce::String (knobSpecs[1].tip) + " Drag up or down; double-click for the default."
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
    const bool deaf = engine.whyDroneDeaf().isNotEmpty();
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
}

//==============================================================================
void DroneView::paint (juce::Graphics& g)
{
    const auto& p = vintage::pal();
    const auto colour = RigColours::drone;
    vintage::drawFace (g, getLocalBounds());

    for (auto well : { stringsWell, tuningWell, playingWell, resonateWell })
        vintage::drawWell (g, well.toFloat());

    auto title = [&] (juce::Rectangle<int> well, const juce::String& text, const juce::String& sub)
    {
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
    const std::pair<juce::Rectangle<int>, const char*> labels[] = { { noteLabel, "Note (Sa)" }, { octaveLabel, "Octave" }, { firstLabel, "First string" },
                                                                    { pluckLabel, "Pluck" }, { feedLabel, "Rings to" } };
    for (const auto& [area, text] : labels)
        if (! area.isEmpty())
            vintage::engraved (g, text, area, juce::Justification::bottomLeft, 12.0f, true);

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
    stringsWell = area.removeFromLeft (full * (narrow ? 30 : 40) / 100);
    area.removeFromLeft (gap);
    const int rest = area.getWidth() - gap * 2;
    tuningWell = area.removeFromLeft (rest * (narrow ? 36 : 34) / 100);
    area.removeFromLeft (gap);
    playingWell = area.removeFromLeft (rest * (narrow ? 35 : 36) / 100);
    area.removeFromLeft (gap);
    resonateWell = area;

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
        if (auto* fine = knobFor ("droneFine"))
            fine->setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 160), juce::jmin (r.getHeight(), 170)));
    }

    // Playing.
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

    // Resonate: what it does (under its title), its knob, what it rings to, how much it rings.
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
