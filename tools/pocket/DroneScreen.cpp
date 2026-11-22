#include "DroneScreen.h"

using namespace pocket;

namespace
{
const juce::Colour kDrone { 0xffd8578e }, kString { 0xfff1e7d1 }, kBridge { 0xffc9b48a };
const char* const kFirstNames[4] = { "PA", "MA", "NI", "SA" };
const char* const kSyncNames[4] = { "OWN TIME", "BEAT", "2 BEATS", "BAR" };
const char* const kOctNames[3] = { "LOW", "MID", "HIGH" };
const char* const kKnobNames[DroneScreen::numKnobs] = { "PACE", "JAWARI", "RING", "REEDS", "VOLUME", "RESONATE" };
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

    // ON, Drone · C, SOUND
    float y = 6.0f;
    {
        const float onW = 64.0f, soundW = juce::jlimit (120.0f, 170.0f, iw * 0.42f);
        addHit ({ x0, y, onW, rowH }, onId);
        addHit ({ x0 + iw - soundW, y, soundW, rowH }, soundId);
        titleR = { x0 + onW + 10.0f, y, iw - onW - soundW - 20.0f, rowH };
    }
    y += rowH + gap;

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
        default: return juce::String (juce::roundToInt (knobValue (k) * 100.0f));
    }
}

//==============================================================================
void DroneScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    if (pressed >= stringBase && pressed < stringBase + crate::drone::kStrings)
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

void DroneScreen::paint (juce::Graphics& g)
{
    const bool on = drone.on.load();

    // Drone · C
    g.setColour (col::ink);
    g.setFont (spaced (condensedBold (19.0f), 0.02f));
    g.drawFittedText ("Drone " + juce::String (U8("\xc2\xb7 ")) + DroneTool::noteName (drone.root.load()), titleR.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
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
        g.drawFittedText (drone.stringNote (s), label.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

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
