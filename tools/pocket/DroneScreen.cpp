#include "DroneScreen.h"

using namespace pocket;

namespace
{
const juce::Colour kDrone { 0xffd8578e }, kString { 0xfff1e7d1 }, kBridge { 0xffc9b48a };
const char* const kFirstNames[4] = { "PA", "MA", "NI", "SA" };
const char* const kSyncNames[4] = { "OWN TIME", "BEAT", "2 BEATS", "BAR" };
const char* const kOctNames[3] = { "LOW", "MID", "HIGH" };
const char* const kKnobNames[DroneScreen::numKnobs] = { "PACE", "JAWARI", "RING", "REEDS", "VOLUME", "RESONATE", "SWING", "HUMAN", "BRIGHT", "BODY", "DETUNE", "SPACE",
                                                        "LEVEL", "SWELL" };
const char* const kPageNames[3] = { "STRINGS", "PATTERN", "MIRACLE" };
const char* const kFitNames[2] = { "OVER THE DRONE", "TUNE DRONE TO IT" };
const char* const kBeatNames[4] = { "OFF", "2 Hz", "6 Hz", "10 Hz" };
const char* const kOrderNames[crate::drone::numOrders] = { "CLASSIC", "REVERSE", "WANDER", "PAIRS", "ROLL" };
const char* const kVoiceNames[crate::drone::numVoices] = { "TANPURA", "BOWED", "SWARMANDAL", "ELECTRIC", "BELL" };
const char* const kStepCountNames[3] = { "OFF", "8", "16" };
const char* const kStepRateNames[3] = { "1/4", "1/8", "1/16" };
}

DroneScreen::DroneScreen (DroneTool& d) : drone (d)
{
    setOpaque (false);
    startTimerHz (30);
}

DroneScreen::~DroneScreen()
{
    stopTimer();
}

void DroneScreen::changed()
{
    if (onChanged)
        onChanged();
    repaint();
}

bool DroneScreen::micWarningShown() const
{
    return drone.feed.load() == (int) DroneFeed::voice && drone.resonate.load() > 0.0f && ! (micOn && micOn());
}

//==============================================================================
void DroneScreen::layout()
{
    hits.clear();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = 16.0f, iw = w - 32.0f;
    auto addHit = [this] (juce::Rectangle<float> r, int id) { hits.push_back ({ r, id }); };
    const bool roomy = h > 560.0f, compact = h < 480.0f;   // compact: a small phone (an SE), everything a little shorter
    const float rowH = roomy ? 42.0f : compact ? 34.0f : 38.0f, gap = roomy ? 8.0f : compact ? 4.0f : 5.0f, labelW = 58.0f;

    // ON and SOUND, then the pages: STRINGS, PATTERN, MIRACLE
    float y = 6.0f;
    {
        const float onW = 64.0f;
        addHit ({ x0, y, onW, rowH }, onId);
        addHit ({ x0 + onW + 8.0f, y, iw - onW - 8.0f, rowH }, soundId);
        y += rowH + gap;
        const float pw = (iw - 10.0f) / 3.0f;
        for (int i = 0; i < 3; ++i)
            addHit ({ x0 + (float) i * (pw + 5.0f), y, pw, rowH - 4.0f }, pageBase + i);
        titleR = { x0, y, iw, rowH - 4.0f };
    }
    y += rowH - 4.0f + gap;
    if (page == 1)
        return layoutPattern (y);
    if (page == 2)
        return layoutMiracle (y);

    // the strings: the knobs and the sing well take what they need, the strings the rest
    const float knobH = roomy ? 78.0f : compact ? 56.0f : 66.0f, singH = roomy ? 112.0f : compact ? 80.0f : 96.0f;
    const float fixedBelow = (rowH + gap) * 3.0f + knobH + gap + singH + 8.0f;
    const float stringsH = juce::jlimit (compact ? 64.0f : 104.0f, 180.0f, h - y - fixedBelow - gap);
    stringsWell = { x0, y, iw, stringsH };
    {
        auto r = stringsWell.reduced (10.0f, 8.0f);
        const float lane = r.getHeight() / (float) crate::drone::kStrings;
        for (int s = 0; s < crate::drone::kStrings; ++s)
        {
            stringR[(size_t) s] = { r.getX(), r.getY() + lane * (float) s, r.getWidth(), lane };
            addHit (stringR[(size_t) s], stringBase + s);
        }
    }
    y = stringsWell.getBottom() + gap;

    // a label, then chips sharing the rest of the row by weight
    auto row = [&] (std::initializer_list<std::pair<float, int>> cells, float left, float width)
    {
        float sum = 0;
        for (auto& c : cells)
            sum += c.first;
        const float g = 5.0f, scale = (width - g * (float) (cells.size() - 1)) / sum;
        float x = left;
        for (auto& c : cells)
        {
            if (c.second != 0)
                addHit ({ x, y, c.first * scale, rowH }, c.second);
            x += c.first * scale + g;
        }
    };
    noteLabel = { x0, y, labelW, rowH };
    row ({ { 54, keyId }, { 46, noteId }, { 6, 0 }, { 44, octBase }, { 44, octBase + 1 }, { 48, octBase + 2 } }, x0 + labelW, iw - labelW);
    y += rowH + gap;
    firstLabel = { x0, y, labelW, rowH };
    row ({ { 1, firstBase }, { 1, firstBase + 1 }, { 1, firstBase + 2 }, { 1, firstBase + 3 } }, x0 + labelW, iw - labelW);
    y += rowH + gap;
    pluckLabel = { x0, y, labelW, rowH };
    row ({ { 72, syncBase }, { 50, syncBase + 1 }, { 64, syncBase + 2 }, { 44, syncBase + 3 } }, x0 + labelW, iw - labelW);
    y += rowH + gap;

    // PACE JAWARI RING REEDS VOLUME
    for (int k = 0; k < 5; ++k)
    {
        knobR[(size_t) k] = { x0 + (float) k * iw / 5.0f, y, iw / 5.0f, knobH };
        addHit (knobR[(size_t) k], knobBase + k);
    }
    y += knobH + gap;

    // SING INTO THE STRINGS: RESONATE, what they ring to, the mic, how much they ring
    singWell = { x0, y, iw, juce::jmax (80.0f, h - 8.0f - y) };
    {
        auto r = singWell.reduced (10.0f, 8.0f);
        const float kw = juce::jmin (86.0f, r.getHeight());
        knobR[(size_t) resonateKnob] = r.removeFromLeft (kw);
        addHit (knobR[(size_t) resonateKnob], knobBase + resonateKnob);
        r.removeFromLeft (8.0f);
        singTitle = r.removeFromTop (compact ? 16.0f : 20.0f);
        r.removeFromTop (compact ? 2.0f : 4.0f);
        auto chips = r.removeFromTop (rowH - 4.0f);
        const float feedW = chips.getWidth() * 0.5f;
        addHit (chips.removeFromLeft (feedW), feedId);
        chips.removeFromLeft (6.0f);
        addHit (chips, micId);
        r.removeFromTop (compact ? 4.0f : 6.0f);
        meterR = r.removeFromTop (compact ? 8.0f : 10.0f);
        statusR = r.getHeight() >= 14.0f ? r : juce::Rectangle<float>();   // (no room on a small phone: the MIC chip says it)
    }
}

// PATTERN: ORDER, STEPS (and CLEAR), the grid (a row for each string, a square for each step), then TONE: the
// instrument, and a row of knobs, SWING HUMAN BRIGHT BODY DETUNE SPACE.
void DroneScreen::layoutPattern (float y)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = 16.0f, iw = w - 32.0f;
    const bool roomy = h > 560.0f, compact = h < 480.0f;
    const float rowH = roomy ? 42.0f : compact ? 34.0f : 38.0f, gap = roomy ? 8.0f : compact ? 4.0f : 6.0f, labelW = 54.0f;
    auto addHit = [this] (juce::Rectangle<float> r, int id) { hits.push_back ({ r, id }); };
    auto chips = [&] (int base, int count, float left, float width, float g = 4.0f)
    {
        const float cw = (width - g * (float) (count - 1)) / (float) count;
        for (int i = 0; i < count; ++i)
            addHit ({ left + (float) i * (cw + g), y, cw, rowH }, base + i);
    };
    orderLabel = { x0, y, labelW, rowH };
    chips (orderBase, crate::drone::numOrders, x0 + labelW, iw - labelW);
    y += rowH + gap;
    stepsLabel = { x0, y, labelW, rowH };
    {
        const float left = x0 + labelW, width = iw - labelW, clearW = 58.0f;
        const float part = (width - clearW - 16.0f) / 2.0f;
        chips (stepCountBase, 3, left, part);
        chips (stepRateBase, 3, left + part + 8.0f, part);
        addHit ({ left + width - clearW, y, clearW, rowH }, clearId);
    }
    y += rowH + gap;

    // the grid takes what the rest leaves (squares up to 46 tall)
    const float knobH = roomy ? 84.0f : compact ? 64.0f : 78.0f;
    const float below = gap + rowH + gap + knobH + 8.0f;
    const float gridH = juce::jlimit (4.0f * 22.0f, 4.0f * (roomy ? 46.0f : 36.0f), h - y - below);
    gridR = { x0, y, iw, gridH };
    {
        const float cw = (iw - 40.0f) / 16.0f, ch = gridH / (float) crate::drone::kStrings;
        for (int step = 0; step < 16; ++step)
            for (int string = 0; string < crate::drone::kStrings; ++string)
                addHit ({ x0 + 40.0f + (float) step * cw, y + (float) string * ch, cw, ch }, cellBase + step * 4 + string);
    }
    y += gridH + gap;
    toneLabel = { x0, y, labelW, rowH };
    chips (voiceBase, crate::drone::numVoices, x0 + labelW, iw - labelW);
    y += rowH + gap;
    for (int k = swingKnob; k <= spaceKnob; ++k)
    {
        const float kw = iw / 6.0f;
        knobR[(size_t) k] = { x0 + (float) (k - swingKnob) * kw, y, kw, knobH };
        addHit (knobR[(size_t) k], knobBase + k);
    }
}

// MIRACLE TONES: the tiles (4 across), the tone (its Hz, name, meaning, the nearest note), FIT, SOUND and BINAURAL,
// then LEVEL, SWELL and TONE ONLY, and the note that the meanings are tradition.
void DroneScreen::layoutMiracle (float y)
{
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = 16.0f, iw = w - 32.0f;
    const bool roomy = h > 560.0f, compact = h < 480.0f;
    const float rowH = roomy ? 40.0f : compact ? 32.0f : 36.0f, gap = roomy ? 6.0f : compact ? 3.0f : 5.0f, labelW = 66.0f;
    auto addHit = [this] (juce::Rectangle<float> r, int id) { hits.push_back ({ r, id }); };
    const float readoutH = compact ? 50.0f : 64.0f, knobH = roomy ? 70.0f : compact ? 52.0f : 62.0f, noteH = compact ? 24.0f : 30.0f;
    const float fixedBelow = gap + readoutH + gap + (rowH + gap) * 3.0f + knobH + gap + noteH;
    const int cols = 4, rows = (crate::drone::kNumTones + cols - 1) / cols;
    const float tileH = juce::jlimit (34.0f, 58.0f, (h - y - fixedBelow - 4.0f - 5.0f * (float) (rows - 1)) / (float) rows);
    const float tw = (iw - 5.0f * (float) (cols - 1)) / (float) cols;
    for (int i = 0; i < crate::drone::kNumTones; ++i)
        addHit ({ x0 + (float) (i % cols) * (tw + 5.0f), y + (float) (i / cols) * (tileH + 5.0f), tw, tileH }, toneBase + i);
    y += (float) rows * (tileH + 5.0f) - 5.0f + gap;
    readoutR = { x0, y, iw, readoutH };
    y += readoutH + gap + 2.0f;
    auto chips = [&] (juce::Rectangle<float>& label, int base, int count)
    {
        label = { x0, y, labelW, rowH };
        const float g = 5.0f, cw = (iw - labelW - g * (float) (count - 1)) / (float) count;
        for (int i = 0; i < count; ++i)
            addHit ({ x0 + labelW + (float) i * (cw + g), y, cw, rowH }, base + i);
        y += rowH + gap;
    };
    chips (fitLabelR, fitBase, 2);
    chips (soundLabelR, toneSoundBase, 3);
    chips (beatLabelR, beatBase, crate::drone::kNumToneBeats);
    {
        const float kw = iw * 0.27f;
        knobR[(size_t) toneLevelKnob] = { x0, y, kw, knobH };
        knobR[(size_t) toneSwellKnob] = { x0 + kw, y, kw, knobH };
        addHit (knobR[(size_t) toneLevelKnob], knobBase + toneLevelKnob);
        addHit (knobR[(size_t) toneSwellKnob], knobBase + toneSwellKnob);
        const float ow = juce::jmin (150.0f, iw - kw * 2.0f - 12.0f);
        addHit ({ x0 + iw - ow, y + (knobH - juce::jmin (knobH, 52.0f)) * 0.5f, ow, juce::jmin (knobH, 52.0f) }, toneOnlyId);
    }
    y += knobH + gap;
    toneNoteR = { x0, y, iw, juce::jmax (0.0f, juce::jmin (noteH, h - y - 2.0f)) };
}

void DroneScreen::showPage (int which)
{
    page = juce::jlimit (0, 2, which);
    pressed = 0;
    dragKnob = -1;
    layout();
    repaint();
}

int DroneScreen::idAt (juce::Point<float> p) const
{
    for (auto& hit : hits)
        if (hit.r.expanded (2.0f).contains (p))
            return hit.id;
    return 0;
}

juce::Rectangle<float> DroneScreen::hitRect (int id) const
{
    for (auto& hit : hits)
        if (hit.id == id)
            return hit.r;
    return {};
}

//==============================================================================
float DroneScreen::knobValue (int k) const
{
    switch (k)
    {
        case paceKnob: return juce::jmap (std::log (drone.pace.load() / 0.3f) / std::log (4.0f / 0.3f), 0.0f, 1.0f);
        case jawariKnob: return drone.jawari.load();
        case ringKnob: return drone.ring.load();
        case reedsKnob: return drone.reeds.load();
        case volumeKnob: return drone.level.load();
        case resonateKnob: return drone.resonate.load();
        case swingKnob: return drone.swing.load();
        case humanKnob: return drone.human.load();
        case brightKnob: return drone.bright.load();
        case bodyKnob: return drone.body.load();
        case detuneKnob: return drone.detune.load();
        case spaceKnob: return drone.space.load();
        case toneLevelKnob: return drone.toneLevel.load();
        case toneSwellKnob: return drone.toneSwell.load();
        default: return 0.0f;
    }
}

void DroneScreen::setKnob (int k, float v)
{
    v = juce::jlimit (0.0f, 1.0f, v);
    switch (k)
    {
        case paceKnob: drone.pace = 0.3f * std::pow (4.0f / 0.3f, v); break;
        case jawariKnob: drone.jawari = v; break;
        case ringKnob: drone.ring = v; break;
        case reedsKnob: drone.reeds = v; break;
        case volumeKnob: drone.level = v; break;
        case resonateKnob: drone.resonate = v; break;
        case swingKnob: drone.swing = v; break;
        case humanKnob: drone.human = v; break;
        case brightKnob: drone.bright = v; break;
        case bodyKnob: drone.body = v; break;
        case detuneKnob: drone.detune = v; break;
        case spaceKnob: drone.space = v; break;
        case toneLevelKnob: drone.toneLevel = v; break;
        case toneSwellKnob: drone.toneSwell = v; break;
        default: break;
    }
    repaint();
}

juce::String DroneScreen::knobText (int k) const
{
    switch (k)
    {
        case paceKnob: return juce::String (drone.pace.load(), 1) + "s";
        case volumeKnob: return DroneTool::volumeText (drone.level.load());
        case brightKnob: return juce::String (juce::roundToInt ((knobValue (k) - 0.5f) * 200.0f));   // (0 in the middle)
        default: return juce::String (juce::roundToInt (knobValue (k) * 100.0f));
    }
}

//==============================================================================
void DroneScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    if ((pressed >= stringBase && pressed < stringBase + crate::drone::kStrings) || pressed >= cellBase)
    {
        press (pressed);   // a string sounds on the touch
        pressed = 0;
        return;
    }
    if (pressed >= knobBase && pressed < knobBase + numKnobs)
    {
        dragKnob = pressed - knobBase;
        dragStart = e.position.y;
        dragValue = knobValue (dragKnob);
    }
    repaint();
}

void DroneScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob >= 0)
        setKnob (dragKnob, dragValue + (dragStart - e.position.y) / 160.0f);
}

void DroneScreen::mouseUp (const juce::MouseEvent& e)
{
    const int was = pressed;
    pressed = 0;
    if (dragKnob >= 0)
    {
        const bool moved = std::abs (e.position.y - dragStart) > 3.0f;
        const int k = dragKnob;
        dragKnob = -1;
        if (moved)
        {
            if (k == resonateKnob && micWarningShown() && toast)
                toast ("The mic is off, so the strings can't hear you. Tap MIC OFF to turn it on (wear headphones).");
            return changed();
        }
    }
    if (was != 0 && idAt (e.position) == was && ! (was >= knobBase && was < knobBase + numKnobs))
        press (was);
    repaint();
}

void DroneScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    if (id < knobBase || id >= knobBase + numKnobs)
        return;
    switch (id - knobBase)
    {
        case paceKnob: drone.pace = 1.1f; break;
        case jawariKnob: drone.jawari = 0.6f; break;
        case ringKnob: drone.ring = 0.5f; break;
        case reedsKnob: drone.reeds = 0.0f; break;
        case volumeKnob: drone.level = 0.8f; break;
        case resonateKnob: drone.resonate = 0.0f; break;
        case swingKnob: drone.swing = 0.0f; break;
        case humanKnob: drone.human = 0.0f; break;
        case brightKnob: drone.bright = 0.5f; break;
        case bodyKnob: drone.body = 0.5f; break;
        case detuneKnob: drone.detune = 0.0f; break;
        case spaceKnob: drone.space = 0.0f; break;
        case toneLevelKnob: drone.toneLevel = 0.6f; break;
        case toneSwellKnob: drone.toneSwell = 0.3f; break;
        default: break;
    }
    changed();
}

void DroneScreen::press (int id)
{
    auto say = [this] (const juce::String& t) { if (toast) toast (t); };
    if (id >= stringBase && id < stringBase + crate::drone::kStrings)
    {
        const int s = id - stringBase;
        drone.pluck (s);
        flash[(size_t) s] = 1.0f;
        if (! drone.on.load())
            say ("Plucked. The drone is off, so the string rings once: tap ON for the drone.");
        return repaint();
    }
    if (id >= cellBase && id < cellBase + 64)
    {
        const bool was = drone.stepCount.load() > 0;
        drone.toggleStep ((id - cellBase) / 4, (id - cellBase) % 4);
        if (! was)
            say ("STEPS on: the strings pluck on the squares you light.");
        return changed();
    }
    if (id >= pageBase && id < pageBase + 3)
        return showPage (id - pageBase);
    if (id >= toneBase && id < toneBase + crate::drone::kNumTones)
    {
        const int index = id - toneBase;
        if (index == crate::drone::kOwnTone && ! (drone.toneOn.load() && drone.tone.load() == index))
            return askOwnHz();
        if (drone.pickTone (index))
            say (crate::drone::toneHzText (drone.toneHz()) + " Hz is playing. Tap it again to stop.");
        else
            say ("The tone is off.");
        return changed();
    }
    if (id >= fitBase && id < fitBase + 2)
    {
        drone.toneFit = id - fitBase;
        say (id == fitBase ? juce::String ("The tone plays over the drone; the drone stays on its note.")
                           : "The drone is tuned to the tone: " + crate::drone::nearestNoteText (crate::drone::toneDroneSa (drone.toneHz(), drone.octave.load())) + ".");
        return changed();
    }
    if (id >= toneSoundBase && id < toneSoundBase + 3)
    {
        drone.toneSound = id - toneSoundBase;
        return changed();
    }
    if (id >= beatBase && id < beatBase + crate::drone::kNumToneBeats)
    {
        drone.toneBeat = id - beatBase;
        if (id != beatBase)
            say (juce::String ("BINAURAL ") + kBeatNames[id - beatBase] + ": " + crate::drone::toneBeatName (id - beatBase)
                 + " Headphones only. A recorded layer is mono, so it keeps a soft pulse instead.");
        return changed();
    }
    if (id == toneOnlyId)
    {
        drone.toneOnly = ! drone.toneOnly.load();
        say (drone.toneOnly.load() ? "TONE ONLY: the strings rest. REC records just the tone." : "The strings play with the tone again.");
        return changed();
    }
    if (id >= orderBase && id < orderBase + crate::drone::numOrders)
    {
        drone.order = id - orderBase;
        if (drone.stepCount.load() > 0)
            say ("The STEPS are on, so they play instead: tap OFF to hear " + juce::String (kOrderNames[id - orderBase]) + ".");
        return changed();
    }
    if (id >= stepCountBase && id < stepCountBase + 3)
    {
        drone.setStepCount (id - stepCountBase);
        say (id == stepCountBase ? juce::String ("STEPS off: the strings go round in their ORDER.")
                                 : juce::String ("STEPS on: tap the squares to draw which strings pluck on which step."));
        return changed();
    }
    if (id >= stepRateBase && id < stepRateBase + 3)
    {
        drone.stepRate = id - stepRateBase;
        return changed();
    }
    if (id == clearId)
    {
        drone.clearSteps();
        say ("The steps are cleared: tap the squares to draw a new pattern.");
        return changed();
    }
    if (id >= voiceBase && id < voiceBase + crate::drone::numVoices)
    {
        drone.voice = id - voiceBase;
        return changed();
    }
    if (id == onId)
    {
        drone.on = ! drone.on.load();
        say (drone.on.load() ? "The drone is on. It doesn't wait for PLAY." : "The drone is off (its strings ring out).");
        return changed();
    }
    if (id == soundId)
        return showSoundMenu();
    if (id == keyId)
    {
        drone.followKey = true;
        drone.root = songRoot ? juce::jlimit (0, 11, songRoot()) : drone.root.load();
        say ("The drone follows the song's KEY: " + DroneTool::noteName (drone.root.load()) + ".");
        return changed();
    }
    if (id == noteId)
        return showNoteMenu();
    if (id >= octBase && id < octBase + 3)
    {
        drone.octave = id - octBase - 1;
        return changed();
    }
    if (id >= firstBase && id < firstBase + 4)
    {
        drone.first = id - firstBase;
        return changed();
    }
    if (id >= syncBase && id < syncBase + 4)
    {
        drone.sync = id - syncBase;
        if (id != syncBase)
            say ("A pluck every " + juce::String (kSyncNames[id - syncBase]).toLowerCase() + " while the song plays (stopped, it keeps that pace).");
        return changed();
    }
    if (id == feedId)
        return showFeedMenu();
    if (id == micId)
    {
        if (micOn && micOn())
            return say ("The mic is on: sing the drone's note and the strings ring along. Wear headphones.");
        if (askMic)
            askMic();
        return repaint();
    }
}

void DroneScreen::askOwnHz()
{
    auto* w = new juce::AlertWindow ("Your own tone", "A frequency from 20 to 2000 Hz.", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("hz", crate::drone::toneHzText (drone.ownHz.load()));
    if (auto* editor = w->getTextEditor ("hz"))
    {
        editor->setInputRestrictions (7, "0123456789.");
        editor->setKeyboardType (juce::TextInputTarget::decimalKeyboard);
    }
    w->addButton ("Play it", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<DroneScreen> (this), w] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const double hz = w->getTextEditorContents ("hz").getDoubleValue();
        if (hz < crate::drone::kOwnToneMin || hz > crate::drone::kOwnToneMax)
        {
            if (safe->toast)
                safe->toast ("Type a frequency from 20 to 2000 Hz.");
            return;
        }
        safe->drone.setOwnToneHz (hz);
        if (safe->toast)
            safe->toast (crate::drone::toneHzText (hz) + " Hz is playing (your own). Tap it again to stop.");
        safe->changed();
    }), true);
}

void DroneScreen::showSoundMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("READY-MADE DRONES");
    const auto& list = crate::drone::presets();
    const int now = drone.presetIndex();
    for (int i = 0; i < (int) list.size(); ++i)
        m.addItem (i + 1, list[(size_t) i].name, true, i == now);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (hitRect (soundId).toNearestInt())),
                     [safe = juce::Component::SafePointer<DroneScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         safe->drone.loadPreset (r - 1);
                         if (safe->toast)
                             safe->toast ("The drone is " + juce::String (crate::drone::presets()[(size_t) (r - 1)].name)
                                          + (safe->drone.on.load() ? juce::String (".") : juce::String (". Tap ON to hear it.")));
                         safe->changed();
                     });
}

void DroneScreen::showNoteMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("THE DRONE'S NOTE (SA)");
    for (int i = 0; i < 12; ++i)
        m.addItem (i + 1, DroneTool::noteName (i), true, ! drone.followKey && i == drone.root.load());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (hitRect (noteId).toNearestInt())),
                     [safe = juce::Component::SafePointer<DroneScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         safe->drone.followKey = false;
                         safe->drone.root = r - 1;
                         safe->changed();
                     });
}

void DroneScreen::showFeedMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("THE STRINGS RING TO");
    for (int i = 0; i < (int) DroneFeed::count; ++i)
        m.addItem (i + 1, droneFeedName ((DroneFeed) i), true, i == drone.feed.load());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (hitRect (feedId).toNearestInt())),
                     [safe = juce::Component::SafePointer<DroneScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r <= 0)
                             return;
                         auto& d = safe->drone;
                         d.feed = r - 1;
                         // picking what they ring to means you want to hear it: RESONATE up if it was at 0, and the mic on for your voice
                         juce::String extra;
                         if (d.resonate.load() <= 0.001f)
                         {
                             d.resonate = 0.6f;
                             extra << " RESONATE is up to 60.";
                         }
                         if (r - 1 == (int) DroneFeed::voice && ! (safe->micOn && safe->micOn()) && safe->askMic)
                             safe->askMic();
                         if (safe->toast)
                             safe->toast (juce::String ("The strings ring to ") + juce::String (droneFeedName ((DroneFeed) (r - 1))).toLowerCase() + "." + extra);
                         safe->changed();
                     });
}

//==============================================================================
void DroneScreen::timerCallback()
{
    if (! isShowing() && ! isVisible())
        return;
    phase += 0.55;
    for (int s = 0; s < crate::drone::kStrings; ++s)
    {
        if (const int c = drone.pluckCount (s); c != seenPlucks[(size_t) s])
        {
            seenPlucks[(size_t) s] = c;
            flash[(size_t) s] = 1.0f;
        }
        flash[(size_t) s] *= 0.86f;
        shown[(size_t) s] += (juce::jlimit (0.0f, 1.0f, drone.ringing (s)) - shown[(size_t) s]) * 0.5f;
    }
    meter = juce::jmax (juce::jlimit (0.0f, 1.0f, drone.resonating()), meter * 0.85f);
    if (page == 2)
        return repaint (readoutR.toNearestInt());
    if (page == 1)
    {
        if (const int step = drone.currentStep(); step != shownStep)
        {
            shownStep = step;
            repaint (gridR.expanded (3.0f).toNearestInt());
        }
        return;
    }
    repaint (stringsWell.expanded (2).toNearestInt());
    repaint (singWell.toNearestInt());
}

juce::String DroneScreen::statusText() const
{
    if (micWarningShown())
        return "The mic is off: the strings can't hear you yet.";
    if (drone.resonate.load() <= 0.0f)
        return "Turn RESONATE up and the strings ring along with what you sing or play.";
    if (drone.feed.load() == (int) DroneFeed::voice)
        return "Sing the drone's note: the strings sing back. Wear headphones.";
    return juce::String ("The strings ring to the ") + juce::String (droneFeedName ((DroneFeed) drone.feed.load())).toLowerCase() + ".";
}

void DroneScreen::drawKnobAt (juce::Graphics& g, int k)
{
    auto r = knobR[(size_t) k];
    const bool inWell = k == resonateKnob;
    auto label = r.removeFromTop (16.0f), value = r.removeFromBottom (16.0f);
    const float d = juce::jmin (r.getWidth() - 10.0f, r.getHeight());
    drawKnob (g, juce::Rectangle<float> (d, d).withCentre (r.getCentre()), knobValue (k), kDrone, true, inWell);
    g.setColour (inWell ? juce::Colour (0xffcdbfa5) : col::inkDim);
    g.setFont (spaced (condensedBold (12.5f), 0.08f));
    g.drawFittedText (kKnobNames[k], label.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    g.setColour (inWell ? col::lcdAmber : col::ink);
    g.setFont (inWell ? lcdFont (15.0f) : condensedBold (13.5f));
    g.drawFittedText (knobText (k), value.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

void DroneScreen::paintPattern (juce::Graphics& g)
{
    for (auto [r, t] : { std::pair { orderLabel, "ORDER" }, std::pair { stepsLabel, "STEPS" }, std::pair { toneLabel, "TONE" } })
    {
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.1f));
        g.drawText (t, r, juce::Justification::centredLeft);
    }
    const int order = drone.order.load(), count = drone.stepCount.load(), rateChoice = drone.stepRate.load(), voice = drone.voice.load();
    const int steps = count == 2 ? 16 : count == 1 ? 8 : 0;
    for (auto& hit : hits)
    {
        const int id = hit.id;
        juce::String text;
        bool lit = false;
        float alpha = 1.0f;
        if (id >= orderBase && id < orderBase + crate::drone::numOrders) { text = kOrderNames[id - orderBase]; lit = order == id - orderBase; alpha = steps > 0 ? 0.45f : 1.0f; }
        else if (id >= stepCountBase && id < stepCountBase + 3) { text = kStepCountNames[id - stepCountBase]; lit = count == id - stepCountBase; }
        else if (id >= stepRateBase && id < stepRateBase + 3) { text = kStepRateNames[id - stepRateBase]; lit = rateChoice == id - stepRateBase; alpha = steps > 0 ? 1.0f : 0.45f; }
        else if (id == clearId) text = "CLEAR";
        else if (id >= voiceBase && id < voiceBase + crate::drone::numVoices) { text = kVoiceNames[id - voiceBase]; lit = voice == id - voiceBase; }
        else
            continue;
        g.beginTransparencyLayer (alpha);
        drawChip (g, hit.r, text, lit, pressed == id, (id >= orderBase && id < orderBase + crate::drone::numOrders) || id >= voiceBase ? 11.5f : 13.0f);
        g.endTransparencyLayer();
    }

    // the grid: a row for each string, a square for each step (lit: it plucks there), the step playing outlined
    drawWell (g, gridR.expanded (4.0f), 10.0f);
    const int now = steps > 0 ? drone.currentStep() : -1;
    const int first = drone.first.load();
    for (int string = 0; string < crate::drone::kStrings; ++string)
    {
        const auto row = hitRect (cellBase + string);
        g.setColour (kString.withAlpha (steps > 0 ? 0.9f : 0.5f));
        g.setFont (condensedBold (juce::jmin (14.0f, row.getHeight() * 0.6f)));
        const juce::String name = string == 0 ? juce::String (kFirstNames[juce::jlimit (0, 3, first)]) : string == 3 ? juce::String ("LOW") : juce::String ("SA");
        g.drawText (name, juce::Rectangle<float> (gridR.getX() + 4.0f, row.getY(), 36.0f, row.getHeight()), juce::Justification::centredLeft);
        for (int step = 0; step < 16; ++step)
        {
            const auto cell = hitRect (cellBase + step * 4 + string).reduced (1.5f);
            const bool lit = (drone.steps[(size_t) step].load() >> string) & 1;
            auto fill = lit ? kDrone : juce::Colour ((step / 4) % 2 == 0 ? 0xff3a332d : 0xff2e2925);
            if (steps == 0 || step >= steps)
                fill = fill.withMultipliedAlpha (lit ? 0.4f : 0.6f);
            g.setColour (fill);
            g.fillRoundedRectangle (cell, 3.0f);
        }
    }
    if (juce::isPositiveAndBelow (now, steps))
    {
        auto column = hitRect (cellBase + now * 4).getUnion (hitRect (cellBase + now * 4 + 3));
        g.setColour (kString.withAlpha (0.85f));
        g.drawRoundedRectangle (column.reduced (0.5f), 4.0f, 1.5f);
    }
    for (int k = swingKnob; k <= spaceKnob; ++k)
    {
        g.beginTransparencyLayer (k == swingKnob && steps == 0 ? 0.45f : 1.0f);
        drawKnobAt (g, k);
        g.endTransparencyLayer();
    }
}

void DroneScreen::paintMiracle (juce::Graphics& g)
{
    const bool toneOn = drone.toneOn.load();
    const int tone = drone.tone.load();
    for (int i = 0; i < crate::drone::kNumTones; ++i)
    {
        auto r = hitRect (toneBase + i);
        const bool lit = toneOn && i == tone, down = pressed == toneBase + i;
        g.setColour (lit ? kDrone : juce::Colour (0xfff7f2e8));
        g.fillRoundedRectangle (r.translated (0.0f, down ? 2.0f : 0.0f), 10.0f);
        g.setColour (lit ? kDrone.darker (0.4f) : juce::Colour (0xffcfc5b2));
        g.fillRoundedRectangle (r.withTop (r.getBottom() - 3.0f).translated (0.0f, down ? 0.0f : 0.0f), 2.0f);
        const bool own = i == crate::drone::kOwnTone;
        const juce::String hz = own ? crate::drone::toneHzText (drone.ownHz.load()) : crate::drone::toneHzText (crate::drone::tones()[(size_t) i].hz);
        const juce::String name = own ? juce::String ("YOUR OWN") : juce::String (crate::drone::tones()[(size_t) i].name);
        auto text = r.reduced (3.0f, 2.0f);
        g.setColour (lit ? juce::Colours::white : col::ink);
        g.setFont (lcdFont (juce::jmin (22.0f, text.getHeight() * 0.5f)));
        g.drawFittedText (hz, text.removeFromTop (text.getHeight() * 0.55f).toNearestInt(), juce::Justification::centredBottom, 1, 0.8f);
        g.setFont (condensedBold (juce::jmin (12.0f, text.getHeight() * 0.8f)));
        g.drawFittedText (name, text.toNearestInt(), juce::Justification::centredTop, 1, 0.6f);
    }

    // the tone: its Hz and name, what it's said to mean (or how to start), the nearest note and the fit (or binaural)
    drawWell (g, readoutR, 12.0f);
    {
        auto r = readoutR.reduced (12.0f, 6.0f);
        const double hz = drone.toneHz();
        const float lineH = r.getHeight() / 3.0f;
        auto top = r.removeFromTop (lineH + 4.0f);
        const auto hzText = crate::drone::toneHzText (hz) + " Hz";
        g.setFont (lcdFont (juce::jmin (24.0f, top.getHeight())));
        g.setColour (juce::Colour (0xfff08bb4).withAlpha (toneOn ? 1.0f : 0.5f));
        const float hzW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hzText) + 10.0f;
        g.drawText (hzText, top.removeFromLeft (hzW), juce::Justification::centredLeft);
        g.setColour (kString.withAlpha (toneOn ? 1.0f : 0.55f));
        g.setFont (condensedBold (15.0f));
        const juce::String name = tone < (int) crate::drone::tones().size() ? juce::String (crate::drone::tones()[(size_t) tone].name) : juce::String ("YOUR OWN");
        g.drawFittedText (name, top.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        const juce::String meaning = ! toneOn ? juce::String ("Off. Tap a tone to play it.")
                                     : tone < (int) crate::drone::tones().size() ? juce::String (crate::drone::tones()[(size_t) tone].meaning)
                                                                                 : juce::String ("Your own frequency.");
        g.setColour (juce::Colour (0xffcdbfa5));
        g.setFont (small (12.5f));
        g.drawFittedText (meaning, r.removeFromTop (lineH).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        const int beat = drone.toneBeat.load();
        juce::String info;
        if (beat > 0)
            info << "L " << crate::drone::toneHzText (hz) << " / R " << crate::drone::toneHzText (hz + crate::drone::toneBeatHz (beat)) << " Hz: "
                 << crate::drone::toneBeatName (beat);
        else
            info << "NEAREST " << crate::drone::nearestNoteText (hz).toUpperCase() << U8("  \xc2\xb7  ")
                 << (drone.toneFit.load() == 0 ? "DRONE STAYS ON " + DroneTool::noteName (drone.root.load())
                                                : "DRONE ON " + crate::drone::nearestNoteText (crate::drone::toneDroneSa (hz, drone.octave.load())).toUpperCase());
        g.setColour (col::lcdAmber);
        g.setFont (spaced (lcdFont (13.0f), 0.03f));
        g.drawFittedText (info, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
    }

    for (auto [r, t] : { std::pair { fitLabelR, "FIT" }, std::pair { soundLabelR, "SOUND" }, std::pair { beatLabelR, "BINAURAL" } })
    {
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.08f));
        g.drawFittedText (t, r.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
    const int fit = drone.toneFit.load(), sound = drone.toneSound.load(), beat = drone.toneBeat.load();
    for (int i = 0; i < 2; ++i)
        drawChip (g, hitRect (fitBase + i), kFitNames[i], fit == i, pressed == fitBase + i, 12.0f);
    for (int i = 0; i < 3; ++i)
        drawChip (g, hitRect (toneSoundBase + i), crate::drone::toneSoundName (i), sound == i, pressed == toneSoundBase + i, 13.0f);
    for (int i = 0; i < crate::drone::kNumToneBeats; ++i)
        drawChip (g, hitRect (beatBase + i), kBeatNames[i], beat == i, pressed == beatBase + i, 13.0f);
    drawKnobAt (g, toneLevelKnob);
    drawKnobAt (g, toneSwellKnob);
    drawChip (g, hitRect (toneOnlyId), drone.toneOnly.load() ? "TONE ONLY: ON" : "TONE ONLY", drone.toneOnly.load(), pressed == toneOnlyId, 13.0f, kDrone);
    if (! toneNoteR.isEmpty())
    {
        g.setColour (col::inkDim);
        g.setFont (small (11.5f));
        g.drawFittedText ("Names and meanings come from Solfeggio and sound-healing tradition: beliefs, not proven science. The tones are exact.",
                          toneNoteR.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
    }
}

void DroneScreen::paint (juce::Graphics& g)
{
    const bool on = drone.on.load();

    // the pages (MIRACLE with a dot while a tone plays), and ON and SOUND over every page
    for (int i = 0; i < 3; ++i)
        drawChip (g, hitRect (pageBase + i), i == 2 && drone.toneOn.load() ? U8("MIRACLE \xc2\xb7 ON") : juce::String (kPageNames[i]), page == i,
                  pressed == pageBase + i, 13.0f);
    if (page != 0)
    {
        for (auto& hit : hits)
            if (hit.id == onId || hit.id == soundId)
                drawChip (g, hit.r, hit.id == onId ? (on ? "ON" : "OFF") : "SOUND: " + drone.presetName().toUpperCase(), hit.id == onId && on,
                          pressed == hit.id, hit.id == soundId ? 13.0f : 14.0f, hit.id == onId ? col::green : juce::Colour());
        return page == 1 ? paintPattern (g) : paintMiracle (g);
    }
    for (auto [r, t] : { std::pair { noteLabel, "NOTE" }, std::pair { firstLabel, "FIRST" }, std::pair { pluckLabel, "PLUCK" } })
    {
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (13.0f), 0.1f));
        g.drawText (t, r, juce::Justification::centredLeft);
    }

    // the strings, ringing as you hear them
    drawWell (g, stringsWell, 12.0f);
    for (int s = 0; s < crate::drone::kStrings; ++s)
    {
        const auto lane = stringR[(size_t) s];
        const float f = flash[(size_t) s], ringing = shown[(size_t) s];
        if (f > 0.02f)
        {
            g.setColour (kDrone.withAlpha (0.22f * f));
            g.fillRoundedRectangle (lane.reduced (0.0f, 2.0f), 8.0f);
        }
        auto label = lane.withWidth (78.0f);
        const juce::String name = s == 0 ? juce::String (kFirstNames[juce::jlimit (0, 3, drone.first.load())]) : s == 3 ? juce::String ("LOW SA") : juce::String ("SA");
        g.setColour (kString.withAlpha (on || ringing > 0.05f ? 1.0f : 0.55f));
        g.setFont (condensedBold (juce::jmin (17.0f, lane.getHeight() * 0.6f)));
        g.drawFittedText (name, label.removeFromLeft (44.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (col::lcdAmber.withAlpha (on || ringing > 0.05f ? 1.0f : 0.6f));
        g.setFont (lcdFont (juce::jmin (17.0f, lane.getHeight() * 0.62f)));
        g.drawFittedText (drone.stringNoteNow (s), label.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

        const float x0 = lane.getX() + 86.0f, x1 = lane.getRight() - 10.0f, cy = lane.getCentreY();
        g.setColour (kBridge.withAlpha (0.8f));
        g.fillRoundedRectangle (x0 - 6.0f, cy - lane.getHeight() * 0.3f, 4.0f, lane.getHeight() * 0.6f, 1.5f);
        g.fillEllipse (x1 + 1.0f, cy - 3.5f, 7.0f, 7.0f);
        const float amp = juce::jmin (lane.getHeight() * 0.32f, 12.0f) * std::sqrt (ringing);
        juce::Path path;
        for (int i = 0; i <= 64; ++i)
        {
            const float t = (float) i / 64.0f;
            const float shape = std::sin (juce::MathConstants<float>::pi * t) * (float) std::sin (phase * (1.0 + 0.23 * s) + s)
                              + 0.35f * std::sin (juce::MathConstants<float>::twoPi * t) * (float) std::sin (phase * (1.0 + 0.23 * s) * 2.1 + s * 1.7);
            const float x = x0 + (x1 - x0) * t, yy = cy + amp * shape * 0.8f;
            if (i == 0)
                path.startNewSubPath (x, yy);
            else
                path.lineTo (x, yy);
        }
        if (ringing > 0.05f)
        {
            g.setColour (kDrone.withAlpha (0.3f * ringing));
            g.strokePath (path, juce::PathStrokeType (s == 3 ? 7.0f : 5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour ((on || ringing > 0.05f) ? kString.interpolatedWith (kDrone, juce::jlimit (0.0f, 1.0f, ringing * 1.4f + f * 0.5f))
                                             : juce::Colour (0xff6a5c4e));
        g.strokePath (path, juce::PathStrokeType (s == 3 ? 3.0f : 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // the chips
    const int oct = drone.octave.load() + 1, first = drone.first.load(), sync = drone.sync.load();
    for (auto& hit : hits)
    {
        const int id = hit.id;
        if (id >= stringBase || (id >= knobBase && id < knobBase + numKnobs) || id == feedId || id == micId)
            continue;
        juce::String text;
        bool lit = false;
        juce::Colour litColour;
        if (id == onId) { text = on ? "ON" : "OFF"; lit = on; litColour = col::green; }
        else if (id == soundId) text = "SOUND: " + drone.presetName().toUpperCase();
        else if (id == keyId) { text = "KEY"; lit = drone.followKey; }
        else if (id == noteId) { text = DroneTool::noteName (drone.root.load()); lit = ! drone.followKey; }
        else if (id >= octBase && id < octBase + 3) { text = kOctNames[id - octBase]; lit = oct == id - octBase; }
        else if (id >= firstBase && id < firstBase + 4) { text = kFirstNames[id - firstBase]; lit = first == id - firstBase; }
        else if (id >= syncBase && id < syncBase + 4) { text = kSyncNames[id - syncBase]; lit = sync == id - syncBase; }
        else
            continue;
        drawChip (g, hit.r, text, lit, pressed == id, id == soundId ? 13.0f : 14.0f, litColour);
    }
    for (int k = 0; k < 5; ++k)
        drawKnobAt (g, k);

    // SING INTO THE STRINGS
    drawWell (g, singWell, 12.0f);
    drawKnobAt (g, resonateKnob);
    g.setColour (juce::Colour (0xffcdbfa5));
    g.setFont (spaced (lcdFont (15.0f), 0.04f));
    g.drawFittedText ("SING INTO THE STRINGS", singTitle.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    drawChip (g, hitRect (feedId), U8("TO: ") + droneFeedName ((DroneFeed) drone.feed.load()), false, pressed == feedId, 13.0f);
    const bool mic = micOn && micOn();
    drawChip (g, hitRect (micId), mic ? "MIC ON" : "MIC OFF", mic || micWarningShown(), pressed == micId, 13.0f,
              mic ? col::green : col::red);
    {
        const int count = 14;
        const float gp = 3.0f, bw = (meterR.getWidth() - gp * (count - 1)) / count;
        const int lit = juce::roundToInt (meter * count);
        for (int i = 0; i < count; ++i)
        {
            g.setColour (i < lit ? kDrone.brighter (0.1f + 0.4f * (float) i / count) : juce::Colour (0xff2e2925));
            g.fillRoundedRectangle (meterR.getX() + (float) i * (bw + gp), meterR.getY(), bw, meterR.getHeight(), 2.0f);
        }
    }
    g.setColour (micWarningShown() ? col::lcdText : juce::Colour (0xffa89a86));
    g.setFont (small (12.5f));
    g.drawFittedText (statusText(), statusR.toNearestInt(), juce::Justification::topLeft, 2, 0.8f);
}
