#include "SynthScreen.h"

using namespace pocket;

namespace
{
const juce::Colour kSynth { 0xffa877f5 }, kDarkKey { 0xff221b2d }, kPaper { 0xfff3eadb }, kKeyFace { 0xfff3eadb }, kRootDot { 0xfff59a2a };
const juce::Colour kKnobFaces[4] = { juce::Colour (0xffa877f5), juce::Colour (0xff2fc6a8), juce::Colour (0xffe4dbee), juce::Colour (0xfff59a2a) };
const char* const kKnobNames[4] = { "TONE", "SHAPE", "DECAY", "ECHO" };
const char* const kModeNames[3] = { "KEYS", "PATTERN", "ARP" };
const char* const kArpNames[3] = { "UP", "DOWN", "UP-DN" };
}

SynthScreen::SynthScreen (SynthPart& p, BeatsEngine& b) : part (p), beats (b)
{
    fingerKey.fill (-1);
    fingerNote.fill (-1);
    lastOfKind.fill (-1);
    setOpaque (false);
    startTimerHz (20);
}

SynthScreen::~SynthScreen()
{
    stopTimer();
}

void SynthScreen::leave()
{
    for (int s = 0; s < (int) fingerKey.size(); ++s)
        keyUp (s);
    if (! (part.mode == SynthPart::arpMode && part.arpHold))
        part.engine().allKeysUp();   // (a held arp keeps going while you use another tool)
}

void SynthScreen::partChanged()
{
    leave();
    selectedStep = -1;
    page = 0;
    layout();
    repaint();
}

void SynthScreen::changed()
{
    if (onChanged)
        onChanged();
    repaint();
}

void SynthScreen::setMode (int m)
{
    leave();
    part.mode = juce::jlimit (0, 2, m);
    part.apply();
    if (part.mode != SynthPart::patternMode)
        selectedStep = -1;
    layout();
    changed();
}

void SynthScreen::timerCallback()
{
    if (! isVisible())
        return;
    const int s = part.engine().patternStep();
    if (s != shownStep)
    {
        shownStep = s;
        repaint();
    }
}

//==============================================================================
void SynthScreen::resized()
{
    layout();
}

void SynthScreen::layout()
{
    hits.clear();
    const float w = (float) getWidth(), h = (float) getHeight();
    const float x0 = 16.0f, iw = w - 32.0f;
    auto addHit = [this] (juce::Rectangle<float> r, int id) { hits.push_back ({ r, id }); };

    // the sound: the display, the eight kinds, the four knobs
    const bool roomy = h > 560.0f;
    const float kindH = roomy ? 42.0f : 38.0f, knobBlock = roomy ? 80.0f : 72.0f;
    float y = 8.0f;
    panel = { x0, y, iw, 10.0f + 32.0f + 4.0f + kindH * 2.0f + 6.0f + 6.0f + knobBlock + 6.0f };
    lcd = { x0 + 10.0f, y + 8.0f, iw - 20.0f, 32.0f };
    nextR = { lcd.getRight() - 44.0f, lcd.getY() - 4.0f, 44.0f, 40.0f };
    prevR = nextR.translated (-92.0f, 0);
    addHit (prevR, prevId);
    addHit (nextR, nextId);
    addHit (lcd.withRight (prevR.getX() - 4.0f), nameId);
    const float kw = (iw - 20.0f - 3.0f * 8.0f) / 4.0f;
    for (int k = 0; k < kSynthKinds; ++k)
    {
        kindR[(size_t) k] = { x0 + 10.0f + (float) (k % 4) * (kw + 8.0f), lcd.getBottom() + 4.0f + (float) (k / 4) * (kindH + 6.0f), kw, kindH };
        addHit (kindR[(size_t) k], kindBase + k);
    }
    const float knobTop = kindR[4].getBottom() + 8.0f;
    for (int k = 0; k < 4; ++k)
    {
        knobR[(size_t) k] = { x0 + 10.0f + (float) k * (iw - 20.0f) / 4.0f, knobTop, (iw - 20.0f) / 4.0f, knobBlock };
        addHit (knobR[(size_t) k], knobBase + k);
    }
    y = panel.getBottom() + 8.0f;

    // KEYS / PATTERN / ARP, the key, the octave
    {
        const float weights[6] = { 56, 76, 48, 76, 46, 46 }, gap = 5.0f;
        float sum = 0;
        for (auto v : weights)
            sum += v;
        const float scale = (iw - gap * 5.0f) / sum;
        float x = x0;
        const int ids[6] = { modeBase, modeBase + 1, modeBase + 2, keyChipId, octDownId, octUpId };
        for (int i = 0; i < 6; ++i)
        {
            addHit ({ x, y, weights[i] * scale, 46.0f }, ids[i]);
            x += weights[i] * scale + gap;
        }
    }
    y += 46.0f + 6.0f;

    // the mode's own row
    auto row = [&] (std::initializer_list<std::pair<float, int>> cells)
    {
        float sum = 0;
        for (auto& c : cells)
            sum += c.first;
        const float gap = 5.0f, scale = (iw - gap * (float) (cells.size() - 1)) / sum;
        float x = x0;
        for (auto& c : cells)
        {
            if (c.second != 0)
                addHit ({ x, y, c.first * scale, 44.0f }, c.second);
            x += c.first * scale + gap;
        }
    };
    if (part.mode == SynthPart::keysMode)
        row ({ { 80, scaleId }, { 96, chromaticId }, { 12, 0 }, { 110, snapId } });
    else if (part.mode == SynthPart::arpMode)
        row ({ { 50, arpBase }, { 56, arpBase + 1 }, { 56, arpBase + 2 }, { 46, rateBase }, { 50, rateBase + 1 }, { 56, holdId } });
    else
        row ({ { 84, lenId }, { 38, pagePrevId }, { 62, 0 }, { 38, pageNextId }, { 52, tieId }, { 62, clearId } });
    y += 44.0f + 8.0f;

    // the pattern: a strip (KEYS, ARP) or the steps (PATTERN)
    if (part.mode == SynthPart::patternMode)
    {
        const float sh = 42.0f, gap = 6.0f, sw = (iw - 7.0f * gap) / 8.0f;
        for (int i = 0; i < 16; ++i)
        {
            stepR[(size_t) i] = { x0 + (float) (i % 8) * (sw + gap), y + (float) (i / 8) * (sh + gap), sw, sh };
            addHit (stepR[(size_t) i], stepBase + i);
        }
        strip = {};
        y += sh * 2.0f + gap + 8.0f;
    }
    else
    {
        strip = { x0, y, iw, 36.0f };
        addHit (strip, stripId);
        y += 36.0f + 8.0f;
    }

    // the keys, low; a line of help above them when there's room
    const float room = h - 8.0f - y;
    const float keysH = juce::jlimit (90.0f, 170.0f, room - (room > 170.0f ? 42.0f : 0.0f));
    keysArea = { x0, h - 8.0f - keysH, iw, keysH };
    hintArea = { x0, y, iw, juce::jmax (0.0f, keysArea.getY() - y - 6.0f) };
}

juce::Rectangle<float> SynthScreen::keyRect (int key) const
{
    auto a = keysArea;
    if (! part.chromatic)
    {
        const float kw = a.getWidth() / 8.0f;
        return { a.getX() + (float) key * kw + 2.0f, a.getY(), kw - 4.0f, a.getHeight() };
    }
    const float kw = a.getWidth() / 8.0f;
    if (key < 8)
        return { a.getX() + (float) key * kw + 2.0f, a.getY(), kw - 4.0f, a.getHeight() };
    static constexpr float after[5] = { 1, 2, 4, 5, 6 };   // the white key each black one sits after
    const float bw = kw * 0.66f;
    return { a.getX() + after[key - 8] * kw - bw * 0.5f, a.getY(), bw, a.getHeight() * 0.58f };
}

int SynthScreen::keyAt (juce::Point<float> p) const
{
    if (! keysArea.expanded (0, 6).contains (p))
        return -1;
    if (part.chromatic)
        for (int k = 8; k < 13; ++k)
            if (keyRect (k).contains (p))
                return k;
    for (int k = 0; k < 8; ++k)
        if (keyRect (k).expanded (2.0f, 6.0f).contains (p))
            return k;
    return -1;
}

int SynthScreen::idAt (juce::Point<float> p) const
{
    if (const int k = keyAt (p); k >= 0)
        return keyBase + k;
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (it->r.contains (p))
            return it->id;
    return none;
}

//==============================================================================
void SynthScreen::keyDown (int source, int key)
{
    if (! juce::isPositiveAndBelow (source, (int) fingerKey.size()))
        return;
    keyUp (source);
    const auto k = songKey ? songKey() : SongKey();
    const int note = part.keyNote (key, k);
    fingerKey[(size_t) source] = key;
    fingerNote[(size_t) source] = note;
    part.engine().keyDown (note, 0.85f);
    if (part.mode == SynthPart::patternMode && selectedStep >= 0)
    {
        // a step is picked: the key sets its note, and the next step is picked
        const int global = page * 16 + selectedStep;
        if (global < part.pattern.steps())
        {
            part.pattern.setStep (global, note);
            part.patternChanged();
            if (selectedStep < 15 && page * 16 + selectedStep + 1 < part.pattern.steps())
                ++selectedStep;
            else if ((page + 1) * 16 < part.pattern.steps())
            {
                ++page;
                selectedStep = 0;
            }
            if (onChanged)
                onChanged();
        }
    }
    repaint();
}

void SynthScreen::keyUp (int source)
{
    if (! juce::isPositiveAndBelow (source, (int) fingerKey.size()) || fingerKey[(size_t) source] < 0)
        return;
    part.engine().keyUp (fingerNote[(size_t) source]);
    fingerKey[(size_t) source] = -1;
    fingerNote[(size_t) source] = -1;
    repaint();
}

void SynthScreen::pressKey (int key, bool down)
{
    if (down)
        keyDown (9, key);
    else
        keyUp (9);
}

bool SynthScreen::keyLit (int key) const
{
    for (auto k : fingerKey)
        if (k == key)
            return true;
    for (auto k : demoHeld)
        if (k == key)
            return true;
    if (part.mode == SynthPart::patternMode && selectedStep >= 0)
    {
        const auto k = songKey ? songKey() : SongKey();
        const int note = part.keyNote (key, k);
        for (int i : part.pattern.notesIn (page * 16 + selectedStep))
            if (part.pattern.notes[(size_t) i].note == note)
                return true;
    }
    return false;
}

void SynthScreen::mouseDown (const juce::MouseEvent& e)
{
    const int src = e.source.getIndex();
    const int id = idAt (e.position);
    if (id >= keyBase)
    {
        keyDown (src, id - keyBase);
        return;
    }
    if (src != 0 && pressed != 0)
        return;   // one finger on the buttons at a time; the others play keys
    pressed = id;
    if (id >= knobBase && id < knobBase + 4)
    {
        dragKnob = id - knobBase;
        dragStart = e.position.y;
        dragValue = dragKnob == 0 ? part.knobs.tone : dragKnob == 1 ? (float) part.knobs.shape / 3.0f : dragKnob == 2 ? part.knobs.decay : part.knobs.echo;
    }
    repaint();
}

void SynthScreen::mouseDrag (const juce::MouseEvent& e)
{
    const int src = e.source.getIndex();
    if (juce::isPositiveAndBelow (src, (int) fingerKey.size()) && fingerKey[(size_t) src] >= 0)
    {
        // slide between keys
        const int k = keyAt (e.position);
        if (k >= 0 && k != fingerKey[(size_t) src] && part.mode != SynthPart::patternMode)
            keyDown (src, k);
        return;
    }
    if (dragKnob >= 0)
    {
        const float v = juce::jlimit (0.0f, 1.0f, dragValue + (dragStart - e.position.y) / 160.0f);
        switch (dragKnob)
        {
            case 0: part.knobs.tone = v; break;
            case 1: part.knobs.shape = juce::roundToInt (v * 3.0f); break;
            case 2: part.knobs.decay = v; break;
            default: part.knobs.echo = v; break;
        }
        part.apply();
        repaint();
    }
}

void SynthScreen::mouseUp (const juce::MouseEvent& e)
{
    const int src = e.source.getIndex();
    if (juce::isPositiveAndBelow (src, (int) fingerKey.size()) && fingerKey[(size_t) src] >= 0)
        return keyUp (src);
    const int was = pressed;
    pressed = 0;
    if (dragKnob >= 0)
    {
        const bool moved = std::abs (e.position.y - dragStart) > 3.0f;
        dragKnob = -1;
        if (moved)
            return changed();
    }
    if (was != 0 && idAt (e.position) == was)
        press (was);
    repaint();
}

void SynthScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    if (id < knobBase || id >= knobBase + 4)
        return;
    switch (id - knobBase)
    {
        case 0: part.knobs.tone = 0.5f; break;
        case 1: part.knobs.shape = presetShape (part.preset); break;
        case 2: part.knobs.decay = 0.5f; break;
        default: part.knobs.echo = 0.0f; break;
    }
    part.apply();
    changed();
}

void SynthScreen::press (int id)
{
    auto say = [this] (const juce::String& t) { if (toast) toast (t); };
    if (id == prevId || id == nextId)
    {
        part.stepPreset (id == prevId ? -1 : 1);
        lastOfKind[(size_t) part.kind()] = part.preset;
    }
    else if (id == nameId)
    {
        // every sound of this kind, by name
        juce::PopupMenu m;
        const auto list = synthPresetsOf (part.kind());
        for (int i : list)
            m.addItem (1 + i, synthPresets()[(size_t) i].name + U8("  \xc2\xb7  ") + synthPresets()[(size_t) i].from, true, i == part.preset);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (lcd.toNearestInt())),
                         [safe = juce::Component::SafePointer<SynthScreen> (this)] (int r)
                         {
                             if (safe == nullptr || r == 0)
                                 return;
                             safe->part.setPreset (r - 1);
                             safe->lastOfKind[(size_t) safe->part.kind()] = safe->part.preset;
                             safe->changed();
                         });
        return;
    }
    else if (id >= kindBase && id < kindBase + kSynthKinds)
    {
        const int k = id - kindBase;
        if (k == part.kind())
            part.stepPreset (1);   // the lit kind again: its next sound
        else
        {
            const auto list = synthPresetsOf (k);
            part.setPreset (lastOfKind[(size_t) k] >= 0 ? lastOfKind[(size_t) k] : list.front());
        }
        lastOfKind[(size_t) part.kind()] = part.preset;
    }
    else if (id >= modeBase && id < modeBase + 3)
        return setMode (id - modeBase);
    else if (id == keyChipId)
    {
        for (auto& hit : hits)
            if (hit.id == keyChipId && onKeyMenu)
                onKeyMenu (*this, hit.r.toNearestInt());
        return;
    }
    else if (id == octDownId || id == octUpId)
    {
        leave();
        const int was = part.octave;
        part.octave = juce::jlimit (-2, 2, part.octave + (id == octUpId ? 1 : -1));
        if (was == part.octave)
            say (part.octave > 0 ? "That's as high as it goes" : "That's as low as it goes");
        part.apply();
    }
    else if (id == scaleId || id == chromaticId)
    {
        leave();
        part.chromatic = id == chromaticId;
        say (part.chromatic ? U8("CHROMATIC: every note, C to C. The dots are the song's key notes.")
                            : U8("IN KEY: the 8 keys are the song's scale, so there are no wrong notes."));
    }
    else if (id == snapId)
    {
        part.quantize = (part.quantize + 1) % 3;
        say (part.quantize == 2 ? U8("REC keeps your timing as you played it") : U8("REC snaps what you play to ") + juce::String (part.quantize == 0 ? "1/16" : "1/8") + " notes");
    }
    else if (id >= arpBase && id < arpBase + 3)
    {
        part.arpStyle = id - arpBase;
        part.apply();
    }
    else if (id >= rateBase && id < rateBase + 2)
    {
        part.arpRate = id - rateBase;
        part.apply();
    }
    else if (id == holdId)
    {
        part.arpHold = ! part.arpHold;
        if (! part.arpHold)
            part.engine().allKeysUp();
        part.apply();
        say (part.arpHold ? "HOLD: the arp keeps going after you let go. Play new keys to change it." : "HOLD off: the arp plays while you hold keys");
    }
    else if (id == lenId)
    {
        const int bars = part.pattern.bars;
        const int next = bars == 1 ? 2 : bars == 2 ? 4 : bars == 4 ? 8 : 1;
        if (next < bars && ! part.pattern.empty())
        {
            // shorter: notes past the end would go, so ask
            auto* w = new juce::AlertWindow ("Shorten the pattern to " + juce::String (next) + (next == 1 ? " bar?" : " bars?"),
                                             "Notes after bar " + juce::String (next) + " are taken out.", juce::MessageBoxIconType::NoIcon, this);
            w->addButton ("Shorten", 1);
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SynthScreen> (this), next] (int r)
            {
                if (safe == nullptr || r != 1)
                    return;
                safe->part.pattern.setBars (next);
                safe->part.patternChanged();
                safe->page = 0;
                safe->selectedStep = -1;
                safe->changed();
            }), true);
            return;
        }
        part.pattern.setBars (next);
        part.patternChanged();
        page = juce::jmin (page, part.pattern.bars - 1);
    }
    else if (id == pagePrevId || id == pageNextId)
    {
        page = juce::jlimit (0, part.pattern.bars - 1, page + (id == pageNextId ? 1 : -1));
        selectedStep = -1;
    }
    else if (id == tieId)
    {
        if (selectedStep < 0)
            say ("Pick a step with a note first, then TIE makes it one step longer");
        else if (! part.pattern.tieStep (page * 16 + selectedStep))
            say ("That step has no note to tie. Tap a key to give it one.");
        else
            part.patternChanged();
    }
    else if (id == clearId)
    {
        if (part.pattern.empty())
            say ("The pattern is already empty");
        else
        {
            part.stash = part.pattern;
            part.pattern.notes.clear();
            part.patternChanged();
            selectedStep = -1;
            say ("Pattern cleared");
        }
    }
    else if (id == stripId)
        return setMode (SynthPart::patternMode);
    else if (id >= stepBase && id < stepBase + 16)
    {
        const int s = id - stepBase, global = page * 16 + s;
        if (global >= part.pattern.steps())
            return;
        if (selectedStep == s)
        {
            if (! part.pattern.notesIn (global).empty())
            {
                part.pattern.clearStep (global);
                part.patternChanged();
            }
            else
                selectedStep = -1;
        }
        else
        {
            selectedStep = s;
            // hear what's on it
            for (int i : part.pattern.notesIn (global))
            {
                const int note = part.pattern.notes[(size_t) i].note;
                part.engine().keyDown (note, 0.7f);
                juce::Timer::callAfterDelay (180, [safe = juce::Component::SafePointer<SynthScreen> (this), note]
                {
                    if (safe != nullptr)
                        safe->part.engine().keyUp (note);
                });
            }
        }
    }
    layout();
    changed();
}

//==============================================================================
juce::String SynthScreen::stepLabel (int global) const
{
    const auto in = part.pattern.notesIn (global);
    if (in.empty())
        return {};
    int low = 128;
    for (int i : in)
        low = juce::jmin (low, part.pattern.notes[(size_t) i].note);
    return SongKey::noteName (low) + (in.size() > 1 ? "+" + juce::String ((int) in.size() - 1) : juce::String());
}

void SynthScreen::drawKnob (juce::Graphics& g, int k)
{
    auto r = knobR[(size_t) k];
    const float d = juce::jmin (r.getWidth() - 16.0f, r.getHeight() - 32.0f);
    auto c = juce::Rectangle<float> (r.getCentreX() - d * 0.5f, r.getY() + 2.0f, d, d);
    float v = 0;
    juce::String value;
    switch (k)
    {
        case 0: v = part.knobs.tone; value = juce::String (juce::roundToInt (v * 100.0f)); break;
        case 1: v = (float) part.knobs.shape / 3.0f; value = synthShapeName (part.knobs.shape); break;
        case 2:
        {
            v = part.knobs.decay;
            const float s = synthDecaySeconds (part.preset, part.knobs);
            value = s < 1.0f ? juce::String (s, 2).trimCharactersAtEnd ("0") + "s" : juce::String (s, 1) + "s";
            break;
        }
        default: v = part.knobs.echo; value = juce::String (juce::roundToInt (v * 100.0f)); break;
    }
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.translated (0, 3));
    g.setColour (kKnobFaces[k]);
    g.fillEllipse (c);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.drawEllipse (c.reduced (1.5f), 1.2f);
    const float a = juce::degreesToRadians (-135.0f + 270.0f * v);
    const auto cc = c.getCentre();
    g.setColour (col::ink);
    g.drawLine (cc.x + std::sin (a) * d * 0.14f, cc.y - std::cos (a) * d * 0.14f, cc.x + std::sin (a) * d * 0.4f, cc.y - std::cos (a) * d * 0.4f, 3.0f);
    g.setColour (juce::Colour (0xffb7a9c2));
    g.setFont (spaced (condensedBold (12.0f), 0.12f));
    const juce::String name = k == 2 && synthKnobIsRelease (part.preset) ? "RELEASE" : kKnobNames[k];
    g.drawText (name, juce::Rectangle<float> (r.getX(), c.getBottom() + 3.0f, r.getWidth(), 14.0f), juce::Justification::centred);
    g.setColour (col::lcdAmber);
    g.setFont (lcdFont (17.0f));
    g.drawText (value, juce::Rectangle<float> (r.getX(), c.getBottom() + 16.0f, r.getWidth(), 16.0f), juce::Justification::centred);
}

void SynthScreen::drawStrip (juce::Graphics& g, juce::Rectangle<float> r)
{
    drawWell (g, r, 10.0f);
    const auto& pat = part.pattern;
    g.setFont (spaced (condensedBold (11.0f), 0.12f));
    g.setColour (juce::Colour (0xffb7a9c2));
    const juce::String bars = juce::String (pat.bars) + (pat.bars == 1 ? " BAR" : " BARS");
    const juce::String head = pat.empty() ? U8("PATTERN \xc2\xb7 EMPTY \xc2\xb7 REC PUTS WHAT YOU PLAY HERE \xc2\xb7 TAP TO EDIT")
                                          : U8("PATTERN \xc2\xb7 ") + bars + U8(" \xc2\xb7 TAP PATTERN TO EDIT");
    g.drawText (head, r.reduced (10, 0).withHeight (14.0f).translated (0, 3), juce::Justification::centredLeft);
    if (pat.empty())
        return;
    auto roll = r.reduced (10, 0).withTrimmedTop (18).withTrimmedBottom (4);
    int lo = 127, hi = 0;
    for (auto& n : pat.notes)
    {
        lo = juce::jmin (lo, n.note);
        hi = juce::jmax (hi, n.note);
    }
    const float span = (float) juce::jmax (6, hi - lo + 1);
    const double len = pat.lengthBeats();
    for (auto& n : pat.notes)
    {
        const float x = roll.getX() + (float) (n.beat / len) * roll.getWidth();
        const float wN = juce::jmax (3.0f, (float) (n.length / len) * roll.getWidth() - 1.0f);
        const float y = roll.getBottom() - ((float) (n.note - lo) + 1.0f) / span * roll.getHeight();
        g.setColour (kSynth);
        g.fillRoundedRectangle (x, y, wN, juce::jmax (3.0f, roll.getHeight() / span), 1.5f);
    }
    const int s = part.engine().patternStep();
    if (s >= 0)
    {
        const float x = roll.getX() + (float) (s * kSynthStep / len) * roll.getWidth();
        g.setColour (col::green);
        g.fillRect (x, roll.getY(), 2.0f, roll.getHeight());
    }
}

void SynthScreen::drawSteps (juce::Graphics& g)
{
    const int playing = part.engine().patternStep();
    for (int i = 0; i < 16; ++i)
    {
        auto r = stepR[(size_t) i];
        const int global = page * 16 + i;
        const bool inRange = global < part.pattern.steps();
        const auto label = inRange ? stepLabel (global) : juce::String();
        const bool tied = inRange && part.pattern.tiedInto (global);
        const bool sel = selectedStep == i;
        const bool down = pressed == stepBase + i;
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0, 3), 9.0f);
        g.setColour (label.isNotEmpty() ? kSynth : tied ? kSynth.withAlpha (0.45f) : kDarkKey);
        if (! inRange)
            g.setColour (kDarkKey.withAlpha (0.35f));
        g.fillRoundedRectangle (r.translated (0, down ? 2.0f : 0.0f), 9.0f);
        if (tied)
        {
            g.setColour (kSynth);
            g.fillRect (r.getX() - 6.0f, r.getCentreY() - 3.0f, 12.0f, 6.0f);
        }
        if (label.isNotEmpty())
        {
            g.setColour (col::onBright);
            g.setFont (condensedBold (15.0f));
            g.drawFittedText (label, r.reduced (3, 0).withTrimmedBottom (12).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        g.setColour (label.isNotEmpty() ? col::onBright.withAlpha (0.7f) : juce::Colour (0xff8e7fa0));
        g.setFont (condensedBold (11.0f));
        g.drawText (juce::String (global + 1), r.reduced (5, 3), juce::Justification::bottomRight);
        if (playing == global)
        {
            g.setColour (col::green);
            g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 2.5f);
        }
        if (sel)
        {
            g.setColour (col::amber);
            g.drawRoundedRectangle (r.expanded (1.5f), 10.0f, 3.0f);
        }
    }
}

void SynthScreen::drawKeys (juce::Graphics& g)
{
    const auto k = songKey ? songKey() : SongKey();
    auto drawOne = [&] (int key)
    {
        auto r = keyRect (key);
        const bool black = isBlack (key);
        const bool lit = keyLit (key);
        const int note = part.keyNote (key, k);
        const bool root = ((note - k.root) % 12 + 12) % 12 == 0;
        const float down = lit ? 2.0f : 0.0f;
        g.setColour (black ? juce::Colours::black : col::rule.darker (0.15f));
        g.fillRoundedRectangle (r.translated (0, 4), 8.0f);
        g.setColour (lit ? kSynth : black ? kDarkKey : kKeyFace);
        g.fillRoundedRectangle (r.translated (0, down), 8.0f);
        // the marks: the root (red), and on CHROMATIC the song's other key notes (below the black keys)
        const float markY = part.chromatic && ! black ? r.getY() + keysArea.getHeight() * 0.58f + 10.0f : r.getY() + 10.0f;
        if (root)
        {
            g.setColour (kRootDot);
            g.fillEllipse (r.getCentreX() - 4.0f, markY + down, 8.0f, 8.0f);
        }
        else if (part.chromatic && k.inScale (note))
        {
            g.setColour ((black ? kPaper : col::onBright).withAlpha (0.7f));
            g.fillEllipse (r.getCentreX() - 3.0f, markY + 1.0f + down, 6.0f, 6.0f);
        }
        g.setColour (black ? kPaper : col::onBright);
        g.setFont (condensedBold (black ? 13.0f : 16.0f));
        const auto name = SongKey::pitchName (note);
        g.drawFittedText (name, r.translated (0, down).withTrimmedTop (r.getHeight() - 30.0f).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    };
    for (int key = 0; key < 8; ++key)
        drawOne (key);
    if (part.chromatic)
        for (int key = 8; key < 13; ++key)
            drawOne (key);
}

void SynthScreen::paint (juce::Graphics& g)
{
    const auto& preset = synthPresets()[(size_t) part.preset];
    const auto k = songKey ? songKey() : SongKey();

    // the sound
    drawWell (g, panel, 16.0f);
    {
        g.setFont (spaced (lcdFont (21.0f), 0.04f));
        g.setColour (col::lcdAmber);
        g.drawFittedText ("SYNTH " + juce::String (U8("\xc2\xb7 ")) + preset.name.toUpperCase(), lcd.withRight (prevR.getX() - 4.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.7f);
        const auto list = synthPresetsOf (part.kind());
        int at = 0;
        for (int i = 0; i < (int) list.size(); ++i)
            if (list[(size_t) i] == part.preset)
                at = i;
        g.setColour (juce::Colour (0xffb7a9c2));
        g.setFont (lcdFont (19.0f));
        g.drawText (juce::String (at + 1) + " OF " + juce::String ((int) list.size()), prevR.getUnion (nextR).reduced (30, 0), juce::Justification::centred);
        for (auto [r, t] : { std::pair { prevR, U8("\xe2\x80\xb9") }, std::pair { nextR, U8("\xe2\x80\xba") } })
        {
            g.setColour (pressed == (r == prevR ? prevId : nextId) ? col::lcdAmber : juce::Colour (0xffb7a9c2));
            g.setFont (condensedBold (30.0f));
            g.drawText (t, r, juce::Justification::centred);
        }
    }
    for (int i = 0; i < kSynthKinds; ++i)
    {
        auto r = kindR[(size_t) i];
        const bool lit = part.kind() == i;
        const bool down = pressed == kindBase + i;
        const auto c = synthKindColour (i);
        g.setColour (lit ? c.darker (0.5f) : c.withAlpha (0.85f));
        g.fillRoundedRectangle (r.translated (0, 3), 9.0f);
        g.setColour (lit ? c : kDarkKey);
        g.fillRoundedRectangle (r.translated (0, down ? 2.0f : 0.0f), 9.0f);
        g.setColour (lit ? col::onBright : kPaper);
        g.setFont (spaced (condensedBold (15.0f), 0.08f));
        g.drawText (synthKindName (i), r.translated (0, down ? 2.0f : 0.0f), juce::Justification::centred);
    }
    for (int i = 0; i < 4; ++i)
        drawKnob (g, i);

    // the chips: the mode, the key, the octave, and the mode's own row
    for (auto& hit : hits)
    {
        const int id = hit.id;
        const bool down = pressed == id;
        juce::String text;
        bool lit = false;
        if (id >= modeBase && id < modeBase + 3) { text = kModeNames[id - modeBase]; lit = part.mode == id - modeBase; }
        else if (id == keyChipId) text = k.label().replace (" ", "\n");
        else if (id == octDownId) text = U8("OCT\n\xe2\x88\x92") + (part.octave < 0 ? juce::String (part.octave) : juce::String());
        else if (id == octUpId) text = "OCT\n+" + (part.octave > 0 ? juce::String (part.octave) : juce::String());
        else if (id == scaleId) { text = "IN KEY"; lit = ! part.chromatic; }
        else if (id == chromaticId) { text = "CHROMATIC"; lit = part.chromatic; }
        else if (id == snapId) text = "REC SNAP " + juce::String (part.quantize == 0 ? "1/16" : part.quantize == 1 ? "1/8" : "OFF");
        else if (id >= arpBase && id < arpBase + 3) { text = id == arpBase + 2 ? U8("UP\xc2\xb7" "DN") : juce::String (kArpNames[id - arpBase]); lit = part.arpStyle == id - arpBase; }
        else if (id >= rateBase && id < rateBase + 2) { text = id == rateBase ? "1/8" : "1/16"; lit = part.arpRate == id - rateBase; }
        else if (id == holdId) { text = "HOLD"; lit = part.arpHold; }
        else if (id == lenId) text = "LEN " + juce::String (part.pattern.bars) + (part.pattern.bars == 1 ? " BAR" : " BARS");
        else if (id == pagePrevId) text = U8("\xe2\x80\xb9");
        else if (id == pageNextId) text = U8("\xe2\x80\xba");
        else if (id == tieId) text = "TIE";
        else if (id == clearId) text = "CLEAR";
        else
            continue;
        const bool dim = (id == pagePrevId && page == 0) || (id == pageNextId && page >= part.pattern.bars - 1);
        drawChip (g, hit.r, text, lit, down, (id == pagePrevId || id == pageNextId) ? 24.0f : 14.0f, (id >= modeBase && id < modeBase + 3) ? col::amber : juce::Colour());
        if (dim)
        {
            g.setColour (col::cream.withAlpha (0.6f));
            g.fillRoundedRectangle (hit.r, 10.0f);
        }
        if (id == pagePrevId)
        {
            // BAR 1 / 2 between the arrows
            auto between = juce::Rectangle<float> (hit.r.getRight() + 5.0f, hit.r.getY(), 0, hit.r.getHeight());
            for (auto& other : hits)
                if (other.id == pageNextId)
                    between.setRight (other.r.getX() - 5.0f);
            g.setColour (col::ink);
            g.setFont (spaced (condensedBold (14.0f), 0.06f));
            g.drawFittedText ("BAR " + juce::String (page + 1) + "/" + juce::String (part.pattern.bars), between.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }

    if (part.mode == SynthPart::patternMode)
        drawSteps (g);
    else
        drawStrip (g, strip);

    // a line of help, when there's room
    if (hintArea.getHeight() >= 18.0f)
    {
        juce::String hint;
        const int next = nextLayer ? nextLayer() : -1;
        const juce::String layer = next >= 0 ? "layer " + juce::String (next + 1) : juce::String ("the next layer");
        const bool busy = recBusy && recBusy();
        if (busy)
            hint = "Recording: play now. STOP calls it off; what you played stays in the pattern.";
        else if (part.mode == SynthPart::keysMode)
            hint = part.chromatic ? U8("Every note, C to C; the dots are the song's key notes. REC: count-in, play, and it loops on ") + layer + "."
                                  : U8("Every key is in the song's key, so there are no wrong notes. Press REC: count-in, play, and it loops on ") + layer + ".";
        else if (part.mode == SynthPart::arpMode)
            hint = "Hold keys: they play in time, one after another. REC records the arp onto " + layer + ".";
        else
            hint = selectedStep >= 0 ? U8("Tap a key to put its note on the picked step (it moves on). Tap the step again to clear it.")
                                     : U8("Tap a step, then a key. The pattern plays with the song; REC puts it on ") + layer + ".";
        g.setColour (col::inkDim);
        g.setFont (small (14.0f));
        g.drawFittedText (hint, hintArea.toNearestInt(), juce::Justification::centredLeft, juce::jmax (1, (int) (hintArea.getHeight() / 17.0f)), 0.85f);
    }

    drawKeys (g);
}
