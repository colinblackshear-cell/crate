#include "ChordsView.h"

#include "Parameters.h"
#include "Pedalboard.h"   // the tuner's pitch detector
#include "core/AudioFiles.h"
#include "dsp/Chords.h"

using vintage::Lit;

namespace
{
constexpr int kFirstKey = 36, kLastKey = 96;   // the keys on screen: C1 to C6, as a Launchkey 61

bool isBlack (int note)
{
    const int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

int looperSourceFor (int dest)
{
    switch ((RigRouting::Dest) dest)
    {
        case RigRouting::Dest::vocoder: return RigLooper::vocoder;
        case RigRouting::Dest::bass:    return RigLooper::bass;
        case RigRouting::Dest::synth:   return RigLooper::synth;
        case RigRouting::Dest::mpc:     return RigLooper::mpc;
        case RigRouting::Dest::sampler: return RigLooper::sampler;
        case RigRouting::Dest::drums:   return RigLooper::drums;
        case RigRouting::Dest::off:
        default:                        return -1;
    }
}

juce::Colour destColour (int dest)
{
    using namespace vintage::colours;
    switch ((RigRouting::Dest) dest)
    {
        case RigRouting::Dest::vocoder: return vocoder;
        case RigRouting::Dest::bass:    return bass;
        case RigRouting::Dest::synth:   return synth;
        case RigRouting::Dest::mpc:     return drums;
        case RigRouting::Dest::sampler: return sampler;
        case RigRouting::Dest::drums:   return drums;
        case RigRouting::Dest::off:
        default:                        return selected;
    }
}
}

//==============================================================================
class ChordsView::PadButton : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    PadButton (ChordsView& v, int i) : view (v), index (i)
    {
        setTooltip ("Click to play this chord (and edit it below). Right-click: copy, catch from your keyboard, name, clear.");
        setRepaintsOnMouseActivity (true);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const auto& chords = view.chords;
        const bool empty = chords.isEmpty (index), isSelected = view.selected == index;
        const bool playing = chords.getSounding() == index && chords.anySounding();
        const bool down = chords.isPadDown (index);
        const auto colour = destColour (view.targetDest());

        g.setColour (juce::Colour (0xff1b1712));
        g.fillRoundedRectangle (r, 7.0f);
        if (playing || down)
        {
            vintage::glow (g, r, 7.0f, vintage::colours::play, 8.0f, 0.45f);
            g.setColour (vintage::colours::play.withAlpha (0.22f));
            g.fillRoundedRectangle (r, 7.0f);
        }
        else if (isMouseOver())
        {
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (r, 7.0f);
        }
        g.setColour (isSelected ? vintage::colours::selected : juce::Colour (0xff3a332a));
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, isSelected ? 2.0f : 1.0f);

        // The pad's number, and its colour stripe (where it plays).
        auto top = r.reduced (8.0f, 4.0f).removeFromTop (14.0f);
        g.setColour (juce::Colour (0xffb7a98d));
        g.setFont (vintage::cond (12.0f, vintage::Weight::bold, 0.06f));
        g.drawText (juce::String (index + 1), top, juce::Justification::centredLeft);
        if (! empty)
        {
            g.setColour (colour.withAlpha (0.85f));
            g.fillRoundedRectangle (top.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 4.0f), 2.0f);
        }
        auto text = r.withTrimmedTop (12.0f);
        if (empty)
        {
            g.setColour (juce::Colour (0xff6b6253));
            g.setFont (vintage::cond (14.0f, vintage::Weight::semi));
            g.drawText ("empty", text, juce::Justification::centred);
        }
        else
        {
            auto name = chords.livePadName (index);
            if (! chords.areChordsOn())
            {
                // Chords off: the one note it plays, and the chord it's from.
                const int note = chords.getPad (index).notes[0] + chords.getPlayingShift();
                name = RigChords::noteName (juce::jlimit (0, 127, note)) + " (" + name + ")";
            }
            vintage::lcdText (g, name, text, name.length() > 7 ? 19.0f : 24.0f, juce::Justification::centred,
                              playing || down ? vintage::colours::lcdGreen : vintage::colours::lcdAmber);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            view.selectPad (index);
            view.showPadMenu (index);
            return;
        }
        pressed = true;
        view.pressPad (index);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (pressed)
            view.releasePad (index);
        pressed = false;
    }

private:
    ChordsView& view;
    const int index;
    bool pressed = false;
};

//==============================================================================
// The keys: the picked pad's notes lit amber, what the chords are playing lit green, a click adds
// or takes out a note. The lowest 16 keys are marked when they play the pads.
class ChordsView::KeyBed : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    explicit KeyBed (ChordsView& v) : view (v)
    {
        setTooltip ("Click a key to add it to the picked chord, or take it out. You hear the chord as it changes. "
                    "With Map keys on, each key shows its chord: a click plays it (Option-click still edits the pad). "
                    "Right-click a key to put a pad's chord on it, or catch one (Your keys).");
    }

    bool mapOn() const { return view.chords.trigger.load() == (int) RigChords::Trigger::keyMap; }

    int numWhite() const
    {
        int n = 0;
        for (int k = kFirstKey; k < kLastKey; ++k)
            n += isBlack (k) ? 0 : 1;
        return n + 1;   // the top C
    }
    juce::Rectangle<float> keyBounds (int note) const
    {
        const float w = (float) getWidth() / (float) numWhite();
        int white = 0;
        for (int k = kFirstKey; k < note; ++k)
            white += isBlack (k) ? 0 : 1;
        if (! isBlack (note))
            return { white * w, 0.0f, w, (float) getHeight() };
        return { white * w - w * 0.32f, 0.0f, w * 0.64f, getHeight() * 0.6f };
    }
    int noteAt (juce::Point<float> p) const
    {
        for (int k = kFirstKey; k <= kLastKey; ++k)
            if (isBlack (k) && keyBounds (k).contains (p))
                return k;
        for (int k = kFirstKey; k <= kLastKey; ++k)
            if (! isBlack (k) && keyBounds (k).contains (p))
                return k;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& chords = view.chords;
        const auto& pad = chords.getPad (view.selected);
        const bool lowZone = chords.trigger.load() == (int) RigChords::Trigger::lowKeys;
        const bool mapped = mapOn();
        const int split = view.engine.getRouting().splitNote.load();
        auto onMap = [&] (int k)
        {
            const auto kind = chords.getMapEntry (k).kind;
            return mapped && chords.areChordsOn() && (split <= 0 || k >= split) && kind != RigChords::MapEntry::pass && kind != RigChords::MapEntry::silent
                   && chords.mapChordName (k).isNotEmpty();
        };
        const int low = chords.lowKey.load();

        g.setColour (juce::Colour (0xff2a2622));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        auto shade = [&] (int note, juce::Rectangle<float> key, bool black)
        {
            const bool inChord = ! mapped && pad.notes.contains (note), playing = chords.isNoteSounding (note);
            if (playing)
                g.setColour (vintage::colours::play.withAlpha (black ? 0.9f : 0.75f));
            else if (inChord)
                g.setColour (vintage::colours::selected.withAlpha (black ? 0.95f : 0.8f));
            else
                return;
            g.fillRoundedRectangle (key, 2.0f);
        };

        for (int k = kFirstKey; k <= kLastKey; ++k)
        {
            if (isBlack (k))
                continue;
            auto key = keyBounds (k).reduced (0.6f, 0).withTrimmedBottom (1.0f);
            juce::ColourGradient ivory (juce::Colour (0xfff6efdf), key.getX(), key.getY(), juce::Colour (0xffd8ccb2), key.getX(), key.getBottom(), false);
            g.setGradientFill (ivory);
            g.fillRoundedRectangle (key, 2.0f);
            shade (k, key, false);
            if (lowZone && k >= low && k < low + RigChords::kPads)
            {
                g.setColour (vintage::colours::mute);
                g.fillRect (key.removeFromBottom (4.0f));
            }
            g.setColour (juce::Colours::black.withAlpha (0.18f));
            g.fillRect (keyBounds (k).removeFromBottom (2.0f));
            if (mapped && ! onMap (k))
            {
                // Plays a note (or nothing) on the map: dimmed.
                g.setColour (juce::Colour (0xff2a2622).withAlpha (chords.getMapEntry (k).kind == RigChords::MapEntry::silent ? 0.45f : 0.22f));
                g.fillRoundedRectangle (key, 2.0f);
            }
            if (onMap (k))
                drawMapLabel (g, k, key.withTrimmedTop (key.getHeight() * 0.62f).withTrimmedBottom (k % 12 == 0 ? 16.0f : 4.0f), false);
            if (k % 12 == 0)
            {
                g.setColour (juce::Colour (0xff6b6253));
                g.setFont (vintage::cond (11.0f, vintage::Weight::semi));
                g.drawText ("C" + juce::String (k / 12 - 2), keyBounds (k).removeFromBottom (18.0f).translated (0, -4.0f), juce::Justification::centred);
            }
        }
        for (int k = kFirstKey; k <= kLastKey; ++k)
        {
            if (! isBlack (k))
                continue;
            auto key = keyBounds (k);
            g.setColour (juce::Colours::black);
            g.fillRoundedRectangle (key, 2.0f);
            auto top = key.reduced (key.getWidth() * 0.12f, 0).withTrimmedBottom (key.getHeight() * 0.1f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3934), top.getX(), top.getY(), juce::Colour (0xff141210), top.getX(),
                                                     top.getBottom(), false));
            g.fillRoundedRectangle (top, 1.5f);
            shade (k, top, true);
            if (mapped && ! onMap (k))
            {
                g.setColour (juce::Colours::black.withAlpha (0.5f));
                g.fillRoundedRectangle (top, 1.5f);
            }
            if (onMap (k))
                drawMapLabel (g, k, top.withTrimmedTop (top.getHeight() * 0.35f), true);
            if (lowZone && k >= low && k < low + RigChords::kPads)
            {
                g.setColour (vintage::colours::mute);
                g.fillRect (top.removeFromBottom (3.0f));
            }
        }
        // The pad numbers on the low keys.
        if (lowZone)
            for (int i = 0; i < RigChords::kPads; ++i)
            {
                const int k = low + i;
                if (k < kFirstKey || k > kLastKey)
                    continue;
                auto key = keyBounds (k);
                g.setColour (isBlack (k) ? juce::Colour (0xffd8ccb2) : juce::Colour (0xff2a5c80));
                g.setFont (vintage::cond (10.0f, vintage::Weight::bold));
                g.drawText (juce::String (i + 1), key.withTrimmedTop (isBlack (k) ? key.getHeight() * 0.45f : key.getHeight() * 0.62f).removeFromTop (14.0f),
                            juce::Justification::centred);
            }
    }

    // A key's chord on the map: its name, and its numeral in the key under it when there's room.
    void drawMapLabel (juce::Graphics& g, int k, juce::Rectangle<float> area, bool black)
    {
        const auto& chords = view.chords;
        const bool playing = chords.getLastMapKey() == k && chords.anySounding();
        const auto name = chords.liveMapChordName (k), degree = chords.mapDegreeName (k);
        // A chord's name standing up the key, so the longest fit ("F#m7b5").
        g.saveState();
        const auto c = area.getCentre();
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, c.x, c.y));
        auto rotated = juce::Rectangle<float> (area.getHeight(), area.getWidth()).withCentre (c);
        const bool showDegree = degree.isNotEmpty() && rotated.getHeight() >= 30.0f;
        auto nameArea = showDegree ? rotated.removeFromTop (rotated.getHeight() * 0.55f) : rotated;
        g.setColour (playing ? vintage::colours::play.darker (0.25f) : black ? juce::Colour (0xffeedfb8) : juce::Colour (0xff2a5c80));
        g.setFont (vintage::cond (juce::jmin (15.0f, nameArea.getHeight() * 0.8f), vintage::Weight::bold));
        g.drawFittedText (name, nameArea.toNearestInt(), juce::Justification::centred, 1, 0.7f);
        if (showDegree)
        {
            g.setColour (black ? juce::Colour (0xffb7a98d) : juce::Colour (0xff8a7a5c));
            g.setFont (vintage::cond (juce::jmin (11.0f, rotated.getHeight() * 0.9f), vintage::Weight::semi));
            g.drawFittedText (degree, rotated.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
        g.restoreState();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int note = noteAt (e.position);
        if (note < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            view.showKeyMenu (note);
            return;
        }
        if (mapOn() && ! e.mods.isAltDown() && view.chords.mapChordName (note).isNotEmpty())
        {
            playingKey = note;
            view.wakeVocoder();
            view.chords.pressMapKey (note);
            return;
        }
        view.clickKey (note);
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (playingKey >= 0)
            view.chords.releaseMapKey (playingKey);
        playingKey = -1;
    }

private:
    ChordsView& view;
    int playingKey = -1;
};

//==============================================================================
// A MIDI file just written: drag it onto a Logic track (or anywhere); a click shows it in Finder.
class ChordsView::DragChip : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit DragChip (ChordsView& v) : view (v)
    {
        setTooltip ("Drag this onto a software instrument track in Logic (or the desktop). Click to show the file.");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff1b1712));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (vintage::colours::lcdGreen.withAlpha (0.8f));
        g.drawRoundedRectangle (r, 6.0f, 1.2f);
        const auto file = view.lastFile;
        vintage::lcdText (g, file.existsAsFile() ? "DRAG: " + file.getFileName().toUpperCase() : juce::String ("NO MIDI FILE YET"),
                          r.reduced (10.0f, 0), 16.0f, juce::Justification::centredLeft, vintage::colours::lcdGreen);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! view.lastFile.existsAsFile())
            return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ view.lastFile.getFullPathName() }, false, this,
                                                                    [this] { dragging = false; });
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging && e.getDistanceFromDragStart() < 6 && view.lastFile.existsAsFile())
            view.lastFile.revealToUser();
        dragging = false;
    }

private:
    ChordsView& view;
    bool dragging = false;
};

//==============================================================================
// Live key: the twelve keys as a little keyboard. Amber: the key the chords play in now; blinking:
// the key a change is waiting to land on (the next beat or bar); the lilac line: the builder's own
// key (home). A click moves the chords to that key.
class ChordsView::LiveKeys : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    explicit LiveKeys (ChordsView& v) : view (v)
    {
        setTooltip ("Live key: click a key and everything the chords play moves to it (the pads, every key of the map, "
                    "your own keys, the progression), the nearest way. Same fingers, same shapes, a new key. "
                    "Oct - / Oct + move it all by octaves; Back returns to the key you wrote them in.");
        setRepaintsOnMouseActivity (true);
    }

    static bool isBlack (int k) { return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    juce::Rectangle<float> keyBounds (int k) const
    {
        static const int whiteIndex[12] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        const float w = (float) getWidth() / 7.0f, h = (float) getHeight();
        if (! isBlack (k))
            return { whiteIndex[k] * w, 0.0f, w, h };
        return { (whiteIndex[k] + 1) * w - w * 0.34f, 0.0f, w * 0.68f, h * 0.56f };
    }
    int keyAt (juce::Point<float> p) const
    {
        for (int k = 0; k < 12; ++k)
            if (isBlack (k) && keyBounds (k).contains (p))
                return k;
        for (int k = 0; k < 12; ++k)
            if (! isBlack (k) && keyBounds (k).contains (p))
                return k;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        const auto& chords = view.chords;
        const int playing = chords.getPlayingKey(), asked = chords.getLiveKey(), home = chords.fillKey;
        const bool waiting = chords.isLiveKeyWaiting() && asked != playing;
        const bool blinkOn = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;
        const auto over = isMouseOver() ? keyAt (getMouseXYRelative().toFloat()) : -1;
        auto drawKey = [&] (int k)
        {
            const bool black = isBlack (k);
            auto r = keyBounds (k).reduced (black ? 0.0f : 1.5f, 0.0f);
            juce::Colour top = black ? juce::Colour (0xff3d3934) : juce::Colour (0xfffbf6ea);
            juce::Colour bottom = black ? juce::Colour (0xff141210) : juce::Colour (0xffe0d6c1);
            if (k == playing)
                top = vintage::colours::selected.brighter (0.2f), bottom = vintage::colours::selected;
            else if (waiting && k == asked && blinkOn)
                top = vintage::colours::next.brighter (0.3f), bottom = vintage::colours::next.darker (0.2f);
            g.setGradientFill (juce::ColourGradient (top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false));
            g.fillRoundedRectangle (r, 3.0f);
            if (k == over && k != playing)
            {
                g.setColour ((black ? juce::Colours::white : juce::Colours::black).withAlpha (0.08f));
                g.fillRoundedRectangle (r, 3.0f);
            }
            g.setColour (black ? juce::Colours::black : juce::Colour (0xffa99c84));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
            if (k == home)
            {
                g.setColour (vintage::colours::sampler);
                g.fillRoundedRectangle (r.removeFromBottom (4.0f).reduced (3.0f, 0.0f).translated (0.0f, -1.5f), 1.5f);
            }
            const bool lit = k == playing || (waiting && k == asked && blinkOn);
            g.setColour (lit ? juce::Colour (0xff2a241e) : black ? juce::Colour (0xffe8dcc4) : juce::Colour (0xff2a241e));
            g.setFont (vintage::cond (juce::jmin (black ? 13.0f : 15.0f, r.getWidth() * 0.55f), vintage::Weight::bold));
            g.drawText (names[k], r.withTrimmedBottom (3.0f).removeFromBottom (juce::jmin (18.0f, r.getHeight() * 0.5f)),
                        juce::Justification::centred);
        };
        for (int k = 0; k < 12; ++k)
            if (! isBlack (k))
                drawKey (k);
        for (int k = 0; k < 12; ++k)
            if (isBlack (k))
                drawKey (k);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (const int k = keyAt (e.position); k >= 0)
            view.pressLiveKey (k);
    }

private:
    ChordsView& view;
};

//==============================================================================
ChordsView::ChordsView (RigEngine& e) : engine (e), chords (e.getChords())
{
    selected = juce::jlimit (0, RigChords::kPads - 1, chords.shapePad.load());

    target.addItem ("The picked track", 1);
    for (auto d : { RigRouting::Dest::vocoder, RigRouting::Dest::synth, RigRouting::Dest::bass, RigRouting::Dest::sampler,
                    RigRouting::Dest::mpc, RigRouting::Dest::drums })
        target.addItem (RigRouting::destName ((int) d), 2 + (int) d);
    target.setTooltip ("Where the chords play: the track picked on the left (it follows as you pick another), or always one instrument.");
    target.onChange = [this] { setTarget (target.getSelectedId() == 1 ? RigChords::kFollowKeys : target.getSelectedId() - 2); };

    for (int i = 0; i <= (int) RigChords::Trigger::keyMap; ++i)
        trigger.addItem (RigChords::triggerName (i), i + 1);
    trigger.setTooltip ("What plays the pads besides your clicks: the Launchkey's 16 drum pads (instead of the MPC while it's on), "
                        "its lowest 16 keys (marked blue on the keys below), every key with one shape (each key plays the picked chord "
                        "from that note), or every key with the chords of the key (Map, below).");
    trigger.onChange = [this] { setTrigger (trigger.getSelectedId() - 1); };

    for (int i = 0; i < (int) RigChords::strumTimes().size(); ++i)
        strum.addItem (RigChords::strumName (i), i + 1);
    strum.setTooltip ("Strum: the chord's notes one after another, low to high, like a guitar.");
    strum.onChange = [this] { chords.strum = strum.getSelectedId() - 1; };

    const char* keyNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    for (int i = 0; i < 12; ++i)
        key.addItem (keyNames[i], i + 1);
    for (int i = 0; i < ChordNames::scales.size(); ++i)
        scale.addItem (ChordNames::scales[i], i + 1);
    for (int i = 0; i < ChordNames::flavours.size(); ++i)
        type.addItem (ChordNames::flavours[i], i + 1);
    key.setTooltip ("The key: what Fill puts on the pads, and what every key plays with Map keys on.");
    scale.setTooltip ("The scale (or mode) of the key, for Fill and the key map.");
    type.setTooltip ("The chord type for Fill and the key map: triads, sevenths, ninths, sus, add9, sixths, power chords, big.");
    for (auto* c : { &key, &scale, &type })
        c->onChange = [this] { setKey (key.getSelectedId() - 1, scale.getSelectedId() - 1, type.getSelectedId() - 1); };

    for (int i = 0; i < RigChords::kNumMapLayouts; ++i)
        mapLayout.addItem (RigChords::mapLayoutName (i), i + 1);
    for (int i = 0; i < 3; ++i)
    {
        mapOffScale.addItem (RigChords::offScaleName (i), i + 1);
        mapVoicing.addItem (RigChords::mapVoicingName (i), i + 1);
    }
    for (int o = -2; o <= 2; ++o)
        mapOctave.addItem (o == 0 ? juce::String ("Oct 0") : "Oct " + juce::String (o > 0 ? "+" : "") + juce::String (o), o + 3);
    mapLayout.setTooltip ("How the chords lie on the keys. Each scale note's chord: D plays Dm in C major, so the chord's root is the key "
                          "you press. White keys: C to B play the key's chords I to vii in any key. Your pads: C to B play pads 1-7, "
                          "the black keys pads 8-12, in every octave. Full keyboard: a different chord on every key, each octave its "
                          "own kind (triads, sevenths, add9s, ninths, sus4s, sixths) with borrowed chords on the keys outside the scale. "
                          "Your keys: your own chord on each key (right-click a key); it starts as a copy of the map you had.");
    mapOffScale.setTooltip ("Keys outside the scale (or the black keys, with White keys): play the chord of the scale note just below, "
                            "play as single notes (for a melody line), or play nothing.");
    mapVoicing.setTooltip ("Where the chords sit. Follows the keys: low keys play low chords. Same octave: every chord around C3, "
                           "wherever you play. Smooth voicing: each chord takes the inversion nearest the last one, as a "
                           "keyboard player would, so changes glide instead of jumping.");
    mapOctave.setTooltip ("Moves every chord on the map up or down by octaves.");
    mapLayout.onChange = [this] { setMapLayout (mapLayout.getSelectedId() - 1); };
    mapOffScale.onChange = [this] { chords.stopAll(); chords.mapOffScale = mapOffScale.getSelectedId() - 1; chords.rebuildMap(); refresh(); };
    mapVoicing.onChange = [this] { chords.stopAll(); chords.mapVoicing = mapVoicing.getSelectedId() - 1; refresh(); };
    mapOctave.onChange = [this] { chords.stopAll(); chords.mapOctave = mapOctave.getSelectedId() - 3; refresh(); };
    for (auto* c : { &mapLayout, &mapOffScale, &mapVoicing, &mapOctave })
        addAndMakeVisible (c);
    mapButton.setTooltip ("Map keys: every key on your keyboard plays a chord of the key (the keys below show which). "
                          "Press again to go back to what played the chords before.");
    mapButton.onClick = [this] { pressMapKeys(); };
    bassButton.setTooltip ("+ Bass: adds each chord's root an octave under it on the map.");
    bassButton.onClick = [this] { chords.stopAll(); chords.mapBass = ! chords.mapBass.load(); refresh(); };
    soundButton.setTooltip ("Your own sound for the chords: load a sample (or drop one from Splice or Finder onto the chords) and "
                            "the chords play it, tuned to its note. Or pick the Synth or Bass in Plays to use your plugins.");
    soundButton.onClick = [this] { showSoundMenu(); };
    vintage::setLit (mapButton, Lit::play);
    vintage::setLit (bassButton, Lit::next);

    for (int i = 0; i < (int) RigChords::stepLengths().size(); ++i)
        length.addItem (RigChords::stepLengthName (i), i + 1);
    length.setTooltip ("How long each step of the progression plays.");
    length.onChange = [this] { chords.stepLength = length.getSelectedId() - 1; updateControls(); };

    for (auto* c : { &target, &trigger, &strum, &key, &scale, &type, &length })
        addAndMakeVisible (c);

    chordsOnButton.setTooltip ("Chords on / off. Off, your keys play plain single notes on the same instrument (a pad plays its "
                               "chord's lowest note); the progression keeps its chords. Right-click to put it on a button of your keyboard.");
    chordsOnButton.onClick = [this] { pressChordsOn(); };
    chordsOnButton.onMenu = [this] { showChordsOnMenu(); };
    chordsOnButton.getProperties().set ("fontSize", 15.0f);
    vintage::setLit (chordsOnButton, Lit::play);
    addAndMakeVisible (chordsOnButton);

    holdButton.setClickingTogglesState (false);
    holdButton.setTooltip ("Hold: a chord keeps playing after you let go, until you play the next (or the same one again).");
    holdButton.onClick = [this] { pressHold(); };
    fillButton.setTooltip ("Puts the seven chords of the key on pads 1-7 and the key's chord an octave up on pad 8 (your pads 9-16 stay).");
    fillButton.onClick = [this] { pressFill(); };
    setsButton.setTooltip ("Starter sets, and your own chord sets: save these pads and progression, or open one (Music/Crate/Chords).");
    setsButton.onClick = [this] { showSetsMenu(); };
    closeButton.setTooltip ("Closes the chords (F7). They keep playing from your keyboard.");
    closeButton.onClick = [this] { if (onClose) onClose(); };
    closeVoicing.setButtonText ("Tight");

    lowerButton.setTooltip ("Inversion down: the top note goes under the others.");
    higherButton.setTooltip ("Inversion up: the bottom note goes over the others.");
    octDown.setTooltip ("The whole chord an octave down.");
    octUp.setTooltip ("The whole chord an octave up.");
    spreadButton.setTooltip ("Opens the chord up: every other note goes up an octave, for a wide, open sound.");
    closeVoicing.setTooltip ("Tight (close voicing): packs the chord into one octave above its lowest note.");
    catchButton.setTooltip ("Catch: hold a chord on your keyboard's keys and let go; it becomes this pad.");
    nameButton.setTooltip ("Give this pad your own name (empty: the chord's name).");
    clearButton.setTooltip ("Empties this pad.");
    lowerButton.onClick = [this] { chords.invert (selected, -1); chords.audition (selected); refresh(); };
    higherButton.onClick = [this] { chords.invert (selected, 1); chords.audition (selected); refresh(); };
    octDown.onClick = [this] { chords.octave (selected, -1); chords.audition (selected); refresh(); };
    octUp.onClick = [this] { chords.octave (selected, 1); chords.audition (selected); refresh(); };
    spreadButton.onClick = [this] { chords.spread (selected); chords.audition (selected); refresh(); };
    closeVoicing.onClick = [this] { chords.close (selected); chords.audition (selected); refresh(); };
    catchButton.onClick = [this] { pressCatch(); };
    nameButton.onClick = [this] { rename (selected); };
    clearButton.onClick = [this] { chords.clearPad (selected); refresh(); };

    playButton.setTooltip ("Plays the progression on Kaleidobox's clock, from the bar it's on (press Play at the top if it's stopped).");
    playButton.onClick = [this] { pressPlayProgression(); };
    loopButton.setTooltip ("Records the instrument the chords play onto the next free loop layer, for the progression's length "
                           "(or until you press again). Press again to stop early.");
    loopButton.onClick = [this] { say (pressToLoop()); };
    recButton.setTooltip ("Rec MIDI: records the chords you play from now (clicks, pads, keys, the progression) until you press again, "
                          "then saves them as a MIDI file to drag into Logic.");
    recButton.onClick = [this] { pressRecMidi(); };
    logicButton.setTooltip ("Saves the progression as a MIDI file (Music/Crate/Rig/To Logic) to drag onto a Logic track.");
    logicButton.onClick = [this] { pressToLogic(); };
    vintage::setLit (holdButton, Lit::next);
    vintage::setLit (catchButton, Lit::arm);
    vintage::setLit (playButton, Lit::play);
    vintage::setLit (loopButton, Lit::arm);
    vintage::setLit (recButton, Lit::arm);
    for (auto* b : { &holdButton, &fillButton, &setsButton, &closeButton, &lowerButton, &higherButton, &octDown, &octUp, &spreadButton,
                     &closeVoicing, &catchButton, &nameButton, &clearButton, &playButton, &loopButton, &recButton, &logicButton,
                     &mapButton, &bassButton, &soundButton })
    {
        b->getProperties().set ("fontSize", 15.0f);
        addAndMakeVisible (b);
    }

    for (int i = 0; i < RigChords::kPads; ++i)
        addAndMakeVisible (padButtons.add (new PadButton (*this, i)));
    for (int i = 0; i < RigChords::kSteps; ++i)
    {
        auto* b = stepButtons.add (new juce::TextButton());
        b->setTooltip ("Step " + juce::String (i + 1) + ": click to pick its chord (or a rest).");
        b->onClick = [this, i] { showStepMenu (i); };
        b->getProperties().set ("fontSize", 16.0f);
        vintage::setLit (*b, Lit::play);
        addAndMakeVisible (b);
    }
    keys = std::make_unique<KeyBed> (*this);
    addAndMakeVisible (*keys);
    chip = std::make_unique<DragChip> (*this);
    addAndMakeVisible (*chip);

    liveKeys = std::make_unique<LiveKeys> (*this);
    addAndMakeVisible (*liveKeys);
    for (int i = 0; i < 3; ++i)
        liveChange.addItem (RigChords::liveChangeName (i), i + 1);
    liveChange.setTooltip ("When a new key lands while the song plays: at once, on the next beat, or on the next bar (the key blinks "
                           "until then). With the song stopped it changes at once.");
    liveChange.onChange = [this] { setLiveChange (liveChange.getSelectedId() - 1); };
    addAndMakeVisible (liveChange);
    liveOctDown.setTooltip ("Everything the chords play an octave down (to -2).");
    liveOctUp.setTooltip ("Everything the chords play an octave up (to +2).");
    liveHome.setTooltip ("Back to the key you wrote the chords in, octave 0.");
    liveLearn.setTooltip ("Put the live key on your keyboard's buttons or pads (ones that send CC): teach all twelve keys in a row, "
                          "or one button at a time, Oct - / Oct + and Back too.");
    liveOctDown.onClick = [this] { pressLiveOctave (-1); };
    liveOctUp.onClick = [this] { pressLiveOctave (1); };
    liveHome.onClick = [this] { pressLiveHome(); };
    liveLearn.onClick = [this] { showLiveLearnMenu(); };
    vintage::setLit (liveLearn, Lit::arm);
    for (auto* b : { &liveOctDown, &liveOctUp, &liveHome, &liveLearn })
    {
        b->getProperties().set ("fontSize", 15.0f);
        vintage::setInWell (*b, true);
        addAndMakeVisible (b);
    }

    refresh();
    startTimerHz (20);
}

ChordsView::~ChordsView()
{
    stopTimer();
    if (chords.isTaking())
        chords.stopTake();
}

int ChordsView::targetDest() const
{
    return chords.resolveTarget (engine.getRouting().keysTo.load());
}

void ChordsView::say (const juce::String& text)
{
    status = text;
    repaint (statusArea);
    if (onSay)
        onSay (text);
}

//==============================================================================
void ChordsView::pressLiveKey (int k)
{
    static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    chords.setLiveKey (k);
    const bool waits = engine.getTransport().isPlaying() && chords.liveChange.load() != (int) RigChords::LiveChange::now;
    const int shift = chords.getLiveShift();
    say (juce::String (waits ? "Next: " : "Playing in ") + names[k]
         + (shift == 0 ? juce::String (" (home)") : " (" + juce::String (shift > 0 ? "+" : "") + juce::String (shift) + ")")
         + (waits ? (chords.liveChange.load() == (int) RigChords::LiveChange::beat ? " on the next beat." : " on the next bar.") : "."));
    refresh();
}

void ChordsView::pressLiveOctave (int delta)
{
    chords.nudgeLiveOctave (delta);
    const int o = chords.getLiveOctave();
    say ("Live key octave " + juce::String (o > 0 ? "+" : "") + juce::String (o) + ".");
    refresh();
}

void ChordsView::pressLiveHome()
{
    chords.liveKeyHome();
    say ("Back to the chords' own key.");
    refresh();
}

void ChordsView::setLiveChange (int change)
{
    chords.liveChange = juce::jlimit (0, 2, change);
    refresh();
}

void ChordsView::showLiveLearnMenu()
{
    static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    auto& routing = engine.getRouting();
    juce::PopupMenu m;
    m.addItem ("Teach all 12 keys in a row (C first)...", [this]
               {
                   learningLive = RigRouting::liveKeyC;
                   learnNextLiveControl();
               });
    juce::PopupMenu one;
    auto addControl = [&] (int control, const juce::String& label)
    {
        const auto [channel, cc] = routing.getAssignment (control);
        juce::ignoreUnused (channel);
        one.addItem (cc >= 0 ? label + " (CC " + juce::String (cc) + "): forget it" : label + "...", [this, control, cc, label]
                     {
                         if (cc >= 0)
                         {
                             engine.getRouting().forget (control);
                             say (label + " forgotten.");
                         }
                         else
                         {
                             learningLive = -1;
                             engine.getRouting().learn (control);
                             say ("Press the button on your keyboard for " + label + ".");
                         }
                         refresh();
                     });
    };
    for (int k = 0; k < 12; ++k)
        addControl (RigRouting::liveKeyC + k, juce::String ("Key ") + names[k]);
    one.addSeparator();
    addControl (RigRouting::liveOctaveDown, "Oct -");
    addControl (RigRouting::liveOctaveUp, "Oct +");
    addControl (RigRouting::liveKeyHome, "Back");
    m.addSubMenu ("One button", one);
    bool any = false;
    for (int c = RigRouting::liveKeyC; c <= RigRouting::liveKeyHome; ++c)
        any = any || routing.getAssignment (c).second >= 0;
    m.addSeparator();
    m.addItem ("Forget them all", any, false, [this]
               {
                   for (int c = RigRouting::liveKeyC; c <= RigRouting::liveKeyHome; ++c)
                       engine.getRouting().forget (c);
                   learningLive = -1;
                   say ("The live key's buttons are forgotten.");
                   refresh();
               });
    if (engine.getRouting().getLearning() >= 0)
        m.addItem ("Stop learning", [this]
                   {
                       engine.getRouting().cancelLearn();
                       learningLive = -1;
                       say ("Stopped learning.");
                       refresh();
                   });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&liveLearn));
}

void ChordsView::learnNextLiveControl()
{
    if (learningLive < RigRouting::liveKeyC || learningLive > RigRouting::liveKeyB)
    {
        learningLive = -1;
        return;
    }
    engine.getRouting().learn (learningLive);
    say ("Press the button for " + RigRouting::controlName (learningLive) + " (" + juce::String (learningLive - RigRouting::liveKeyC + 1)
         + " of 12).");
}

void ChordsView::refresh()
{
    updateControls();
    for (auto* p : padButtons)
        p->repaint();
    keys->repaint();
    liveKeys->repaint();
    repaint();
}

void ChordsView::updateControls()
{
    const int t = chords.target.load();
    target.setSelectedId (t == RigChords::kFollowKeys ? 1 : t + 2, juce::dontSendNotification);
    const int keysTo = engine.getRouting().keysTo.load();
    if (keysTo != shownKeysTo)
    {
        shownKeysTo = keysTo;
        target.changeItemText (1, "The picked track (" + RigRouting::destName (keysTo) + ")");
        if (t == RigChords::kFollowKeys)
            target.setText ("The picked track (" + RigRouting::destName (keysTo) + ")", juce::dontSendNotification);
        for (auto* p : padButtons)
            p->repaint();
    }
    trigger.setSelectedId (chords.trigger.load() + 1, juce::dontSendNotification);
    strum.setSelectedId (chords.strum.load() + 1, juce::dontSendNotification);
    key.setSelectedId (chords.fillKey + 1, juce::dontSendNotification);
    scale.setSelectedId (chords.fillScale + 1, juce::dontSendNotification);
    type.setSelectedId (chords.fillType + 1, juce::dontSendNotification);
    mapLayout.setSelectedId (chords.mapLayout.load() + 1, juce::dontSendNotification);
    mapOffScale.setSelectedId (chords.mapOffScale.load() + 1, juce::dontSendNotification);
    mapVoicing.setSelectedId (chords.mapVoicing.load() + 1, juce::dontSendNotification);
    mapOctave.setSelectedId (chords.mapOctave.load() + 3, juce::dontSendNotification);
    const bool mapOn = chords.trigger.load() == (int) RigChords::Trigger::keyMap;
    mapButton.setToggleState (mapOn, juce::dontSendNotification);
    bassButton.setToggleState (chords.mapBass.load(), juce::dontSendNotification);
    // Your pads have no key, and no keys outside it.
    mapOffScale.setEnabled (chords.mapLayout.load() != (int) RigChords::MapLayout::yourPads
                            && chords.mapLayout.load() != (int) RigChords::MapLayout::fullKeyboard);
    {
        // The Sampler, by its sound's name.
        auto& doc = engine.getSampler().getDocument();
        const auto name = doc.hasAudio() ? doc.getName() : juce::String();
        if (name != shownSamplerName)
        {
            shownSamplerName = name;
            target.changeItemText (2 + (int) RigRouting::Dest::sampler,
                                   name.isEmpty() ? RigRouting::destName ((int) RigRouting::Dest::sampler) : "Sampler: " + name);
            if (t == (int) RigRouting::Dest::sampler)
                target.setText (target.getItemText (target.indexOfItemId (2 + t)), juce::dontSendNotification);
        }
    }
    length.setSelectedId (chords.stepLength.load() + 1, juce::dontSendNotification);
    holdButton.setToggleState (chords.hold.load(), juce::dontSendNotification);
    {
        const bool on = chords.areChordsOn();
        const bool teaching = engine.getRouting().getLearning() == RigRouting::chordsOnOff;
        chordsOnButton.setToggleState (on, juce::dontSendNotification);
        const auto text = teaching ? juce::String ("Press it...") : on ? juce::String ("Chords on") : juce::String ("Chords off");
        if (chordsOnButton.getButtonText() != text)
            chordsOnButton.setButtonText (text);
        if (on != shownChordsOn)
        {
            // Flipped from the keyboard's button: say so, and the pads show what they play.
            shownChordsOn = on;
            status = on ? "Chords on: your keys and pads play chords." : "Chords off: your keys play plain single notes (pads: the chord's lowest note).";
            for (auto* p : padButtons)
                p->repaint();
            repaint (statusArea);
        }
    }
    catchButton.setToggleState (chords.isCatching(), juce::dontSendNotification);
    catchButton.setButtonText (chords.isCatching() ? "Play it..." : "Catch");
    playButton.setToggleState (chords.progressionOn.load(), juce::dontSendNotification);
    recButton.setToggleState (chords.isTaking(), juce::dontSendNotification);
    recButton.setButtonText (chords.isTaking() ? "Stop MIDI" : "Rec MIDI");
    const bool looping = loopLayer >= 0;
    loopButton.setToggleState (looping, juce::dontSendNotification);
    loopButton.setButtonText (looping ? "Loop " + juce::String (loopLayer + 1) : "To loop");

    const int playing = chords.getPlayingStep();
    for (int i = 0; i < stepButtons.size(); ++i)
    {
        const int pad = chords.steps[(size_t) i].load();
        const auto text = pad < 0 ? juce::String ("-") : chords.isEmpty (pad) ? juce::String (pad + 1) : chords.livePadName (pad);
        if (stepButtons[i]->getButtonText() != text)
            stepButtons[i]->setButtonText (text);
        stepButtons[i]->setToggleState (i == playing, juce::dontSendNotification);
        stepButtons[i]->setAlpha (i < juce::jmax (1, chords.progressionSteps()) || pad >= 0 ? 1.0f : 0.6f);
    }
    {
        static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        liveChange.setSelectedId (chords.liveChange.load() + 1, juce::dontSendNotification);
        const auto homeText = juce::String ("Back to ") + names[chords.fillKey];
        if (liveHome.getButtonText() != homeText)
            liveHome.setButtonText (homeText);
        liveHome.setEnabled (! chords.isLiveKeyHome());
        liveOctDown.setEnabled (chords.getLiveOctave() > -RigChords::kLiveOctaves);
        liveOctUp.setEnabled (chords.getLiveOctave() < RigChords::kLiveOctaves);
        bool learned = false;
        for (int c = RigRouting::liveKeyC; c <= RigRouting::liveKeyHome; ++c)
            learned = learned || engine.getRouting().getAssignment (c).second >= 0;
        const int teaching = engine.getRouting().getLearning();
        const bool isTeaching = teaching >= RigRouting::liveKeyC && teaching <= RigRouting::liveKeyHome;
        liveLearn.setToggleState (isTeaching, juce::dontSendNotification);
        liveLearn.setButtonText (isTeaching ? "Press it..." : learned ? "Learned" : "Learn");
    }
    const bool any = ! chords.isEmpty (selected);
    for (auto* b : { &lowerButton, &higherButton, &octDown, &octUp, &spreadButton, &closeVoicing, &clearButton })
        b->setEnabled (any);
}

void ChordsView::timerCallback()
{
    if (chords.serviceCatch())
    {
        const auto& pad = chords.getPad (selected);
        say ("Caught " + chords.padName (selected) + " on pad " + juce::String (selected + 1) + " ("
             + juce::String (pad.notes.size()) + " notes).");
        refresh();
    }
    chords.serviceTake();
    // Teaching the twelve keys in a row: once one is caught, the next.
    if (learningLive >= 0 && engine.getRouting().getLearning() != learningLive)
    {
        if (engine.getRouting().getLearning() < 0 && engine.getRouting().getAssignment (learningLive).second >= 0)
        {
            if (learningLive == RigRouting::liveKeyB)
            {
                learningLive = -1;
                say ("All twelve keys are on your keyboard's buttons.");
            }
            else
            {
                ++learningLive;
                learnNextLiveControl();
            }
        }
        else
            learningLive = -1;   // something else is being taught (or it was stopped)
    }
    if (const int k = chords.getLastMapKey(); k != shownMapKey && k >= 0)
    {
        shownMapKey = k;
        const auto notes = chords.mapNotesFor (k);
        juce::StringArray text;
        for (int n : notes)
            text.add (RigChords::noteName (n));
        const auto degree = chords.mapDegreeName (k);
        say ("Key " + RigChords::noteName (k) + ": " + chords.liveMapChordName (k) + (degree.isNotEmpty() ? " (" + degree + ")" : juce::String())
             + (chords.mapVoicing.load() == (int) RigChords::MapVoicing::smooth ? juce::String() : "  " + text.joinIntoString (" ")));
    }

    // The lights: what's sounding, the progression's step, a loop recording.
    static_assert (RigChords::kPads <= 16);
    if (loopLayer >= 0)
    {
        const auto state = engine.getLooper().getState (loopLayer);
        if (state != RigLooper::State::armed && state != RigLooper::State::recording)
            loopLayer = -1;
    }
    updateControls();
    for (auto* p : padButtons)
        p->repaint();
    keys->repaint();
    liveKeys->repaint();
    repaint (liveWell);
}

//==============================================================================
void ChordsView::selectPad (int pad)
{
    selected = juce::jlimit (0, RigChords::kPads - 1, pad);
    chords.shapePad = selected;   // Every key plays the pad you're on
    refresh();
}

void ChordsView::wakeVocoder()
{
    if (targetDest() != (int) RigRouting::Dest::vocoder)
        return;
    auto& params = engine.getVocoder().params;
    if (auto* v = params.getRawParameterValue (ParamIDs::robotOn); v != nullptr && v->load() < 0.5f)
        if (auto* p = params.getParameter (ParamIDs::robotOn))
            p->setValueNotifyingHost (1.0f);
    if (! engine.vocoderOn.load())
        engine.vocoderOn = true;
}

void ChordsView::pressPad (int pad)
{
    if (selected != pad)
        selectPad (pad);
    if (chords.isEmpty (pad))
    {
        say ("Pad " + juce::String (pad + 1) + " is empty: click keys below to build a chord, or Catch one from your keyboard.");
        return;
    }
    if (targetDest() == (int) RigRouting::Dest::off)
        say ("The keys play nothing right now: pick a track on the left, or pick where the chords play.");
    wakeVocoder();
    chords.press (pad);
}

void ChordsView::releasePad (int pad)
{
    chords.release (pad);
}

void ChordsView::clickKey (int note)
{
    const bool had = chords.getPad (selected).notes.contains (note);
    if (! had && chords.getPad (selected).notes.size() >= RigChords::kMaxNotes)
    {
        say ("A chord holds up to " + juce::String (RigChords::kMaxNotes) + " notes. Take one out first.");
        return;
    }
    chords.toggleNote (selected, note);
    wakeVocoder();
    if (! chords.isEmpty (selected))
        chords.audition (selected);
    say ((had ? "Took out " : "Added ") + RigChords::noteName (note) + ": pad " + juce::String (selected + 1) + " is "
         + (chords.isEmpty (selected) ? juce::String ("empty") : chords.padName (selected)) + ".");
    refresh();
}

void ChordsView::setTarget (int t)
{
    chords.stopAll();
    chords.target = t;
    shownKeysTo = -2;
    refresh();
}

void ChordsView::setTrigger (int t)
{
    chords.stopAll();
    if (t != (int) RigChords::Trigger::keyMap)
        triggerBeforeMap = juce::jlimit (0, (int) RigChords::Trigger::everyKey, t);
    chords.trigger = juce::jlimit (0, (int) RigChords::Trigger::keyMap, t);
    switch ((RigChords::Trigger) chords.trigger.load())
    {
        case RigChords::Trigger::pads:
            say ("Your Launchkey's drum pads play chord pads 1-16 now (not the MPC). Set it back to Clicks only for the MPC.");
            break;
        case RigChords::Trigger::lowKeys:
            say ("The lowest 16 keys (from " + RigChords::noteName (chords.lowKey.load()) + ") play chord pads 1-16; the rest play as usual.");
            break;
        case RigChords::Trigger::everyKey:
            say ("Every key plays pad " + juce::String (selected + 1) + "'s chord (" + chords.padName (selected)
                 + ") from that note. Pick another pad to change the shape.");
            break;
        case RigChords::Trigger::keyMap:
            say (mapSummary());
            break;
        case RigChords::Trigger::screen:
        default:
            say ("Your keyboard plays notes as usual; click the pads to play chords.");
            break;
    }
    refresh();
}

void ChordsView::pressChordsOn()
{
    chords.toggleChords();
    say (chords.areChordsOn() ? "Chords on: your keys and pads play chords."
                              : "Chords off: your keys play plain single notes (pads: the chord's lowest note).");
    refresh();
}

void ChordsView::showChordsOnMenu()
{
    auto& routing = engine.getRouting();
    const int cc = routing.getAssignment (RigRouting::chordsOnOff).second;
    juce::PopupMenu m;
    if (routing.getLearning() == RigRouting::chordsOnOff)
        m.addItem ("Stop learning", [this] { engine.getRouting().cancelLearn(); say ("Stopped learning."); refresh(); });
    else
        m.addItem ("Learn: put Chords on/off on a keyboard button...", [this]
                   {
                       learningLive = -1;
                       engine.getRouting().learn (RigRouting::chordsOnOff);
                       say ("Press the button on your keyboard for Chords on/off.");
                       refresh();
                   });
    m.addItem ("Forget its button" + (cc >= 0 ? " (CC " + juce::String (cc) + ")" : juce::String()), cc >= 0, false, [this]
               {
                   engine.getRouting().forget (RigRouting::chordsOnOff);
                   say ("Chords on/off's button forgotten.");
                   refresh();
               });
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&chordsOnButton));
}

void ChordsView::pressHold()
{
    chords.stopAll();
    chords.hold = ! chords.hold.load();
    refresh();
}

juce::String ChordsView::mapSummary() const
{
    switch ((RigChords::MapLayout) chords.mapLayout.load())
    {
        case RigChords::MapLayout::whiteKeys:
            return key.getText() + " " + scale.getText() + " on the white keys: C to B play I to vii.";
        case RigChords::MapLayout::yourPads:
            return "Your pads in every octave: C to B pads 1-7, black keys 8-12.";
        case RigChords::MapLayout::fullKeyboard:
            return key.getText() + " " + scale.getText() + ": a different chord on every key. C1 triads, C2 7ths, C3 add9, C4 9ths, C5 sus4, C6 6ths.";
        case RigChords::MapLayout::yourKeys:
            return "Your keys: right-click a key to put a pad's chord on it or catch one.";
        case RigChords::MapLayout::scaleNotes:
        default:
            return key.getText() + " " + scale.getText() + " (" + type.getText().toLowerCase() + ") on every key.";
    }
}

void ChordsView::setKey (int k, int s, int t)
{
    chords.stopAll();
    chords.setKey (k, s, t);
    updateControls();
    if (chords.trigger.load() == (int) RigChords::Trigger::keyMap)
        say (mapSummary());
    refresh();
}

void ChordsView::pressMapKeys()
{
    if (chords.trigger.load() == (int) RigChords::Trigger::keyMap)
        setTrigger (triggerBeforeMap == (int) RigChords::Trigger::keyMap ? (int) RigChords::Trigger::screen : triggerBeforeMap);
    else
        setTrigger ((int) RigChords::Trigger::keyMap);
}

void ChordsView::setMapLayout (int layout)
{
    chords.stopAll();
    layout = juce::jlimit (0, RigChords::kNumMapLayouts - 1, layout);
    if (layout == (int) RigChords::MapLayout::yourKeys && ! chords.hasKeyChords())
    {
        // Your keys start as the map you had, to change key by key.
        chords.copyMapToYourKeys();
        say ("Your keys: every key keeps the chord it had. Right-click a key to put a pad's chord on it, or catch one.");
    }
    else
    {
        chords.mapLayout = layout;
        chords.rebuildMap();
        say (mapSummary());
    }
    if (chords.trigger.load() != (int) RigChords::Trigger::keyMap)
        setTrigger ((int) RigChords::Trigger::keyMap);
    refresh();
}

void ChordsView::putPadOnKey (int pad, int note)
{
    if (chords.isEmpty (pad))
        return;
    if (chords.mapLayout.load() != (int) RigChords::MapLayout::yourKeys)
        setMapLayout ((int) RigChords::MapLayout::yourKeys);
    // The pad's chord, moved to the octave of the key, so the key's chord sits where it's played.
    auto notes = chords.getPad (pad).notes;
    const int shift = 12 * (int) std::round ((note - notes.getFirst()) / 12.0);
    for (auto& n : notes)
        n += shift;
    chords.setKeyChord (note, notes);
    if (chords.trigger.load() != (int) RigChords::Trigger::keyMap)
        setTrigger ((int) RigChords::Trigger::keyMap);
    say ("Key " + RigChords::noteName (note) + " plays " + chords.mapChordName (note) + " (pad " + juce::String (pad + 1) + ").");
    refresh();
}

void ChordsView::showKeyMenu (int note)
{
    juce::PopupMenu fromPads;
    for (int i = 0; i < RigChords::kPads; ++i)
        if (! chords.isEmpty (i))
            fromPads.addItem (100 + i, "Pad " + juce::String (i + 1) + ": " + chords.padName (i));
    const auto now = chords.mapChordName (note);
    const bool mine = chords.mapLayout.load() == (int) RigChords::MapLayout::yourKeys;
    juce::PopupMenu m;
    m.addSectionHeader ("Key " + RigChords::noteName (note) + (now.isNotEmpty() && chords.trigger.load() == (int) RigChords::Trigger::keyMap
                                                                ? ": " + now : juce::String()));
    m.addSubMenu ("Put a pad's chord on this key", fromPads, fromPads.getNumItems() > 0);
    m.addItem (1, "Catch a chord for this key (hold it, let go)");
    m.addItem (2, "Clear this key (it plays its note)", mine && ! chords.getKeyChord (note).isEmpty());
    m.addSeparator();
    m.addItem (3, "Make every key mine (copy this map to Your keys)", ! mine);
    m.addItem (4, "Clear all of Your keys", chords.hasKeyChords());
    juce::Component::SafePointer<ChordsView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (keys.get()), [safe, note] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& c = safe->chords;
        if (r >= 100)
            safe->putPadOnKey (r - 100, note);
        else if (r == 1)
        {
            if (c.mapLayout.load() != (int) RigChords::MapLayout::yourKeys)
                safe->setMapLayout ((int) RigChords::MapLayout::yourKeys);
            c.catchForKey (note);
            safe->say ("Hold a chord on your keyboard and let go: it becomes key " + RigChords::noteName (note) + "'s chord.");
        }
        else if (r == 2)
        {
            c.setKeyChord (note, {});
            safe->say ("Key " + RigChords::noteName (note) + " plays its own note now.");
        }
        else if (r == 3)
            safe->setMapLayout ((int) RigChords::MapLayout::yourKeys);
        else if (r == 4)
        {
            c.stopAll();
            c.clearKeyChords();
            safe->say ("Your keys are empty: every key plays its note until you put chords on them.");
        }
        safe->refresh();
    });
}

int ChordsView::noteInName (const juce::String& fileName)
{
    juce::StringArray tokens;
    tokens.addTokens (fileName, " _-.()[],", "");
    int found = -1;
    for (const auto& t : tokens)
    {
        if (t.isEmpty() || t[0] < 'A' || t[0] > 'G')
            continue;
        static const int letters[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        int pc = letters[t[0] - 'A'];
        auto rest = t.substring (1);
        if (rest.startsWith ("#"))
        {
            ++pc;
            rest = rest.substring (1);
        }
        else if (rest.startsWith ("b"))
        {
            --pc;
            rest = rest.substring (1);
        }
        const auto q = rest.toLowerCase();
        if (q.isEmpty() || q == "m" || q == "min" || q == "maj" || q == "minor" || q == "major" || rest == "M")
            found = (pc + 12) % 12;   // the last one wins ("Kick_A_..._Cmin": C)
    }
    return found;
}

int ChordsView::guessRoot (const juce::AudioBuffer<float>& audio, const juce::String& fileName, juce::String& how)
{
    const int named = noteInName (fileName);
    // The pitch heard, from a few stretches of its first seconds (after the attack).
    float heard = 0;
    {
        const int len = juce::jmin (audio.getNumSamples(), 44100 * 3), window = 4096;
        std::vector<float> mono ((size_t) window);
        juce::Array<float> found;
        for (const double at : { 0.1, 0.25, 0.4, 0.55 })
        {
            const int start = (int) (len * at);
            if (start + window > audio.getNumSamples())
                break;
            for (int i = 0; i < window; ++i)
            {
                float v = 0;
                for (int ch = 0; ch < audio.getNumChannels(); ++ch)
                    v += audio.getSample (ch, start + i);
                mono[(size_t) i] = v / (float) juce::jmax (1, audio.getNumChannels());
            }
            if (const float f = pedalboard::detectPitch (mono.data(), window, 44100.0); f > 0)
                found.add (f);
        }
        if (! found.isEmpty())
        {
            std::sort (found.begin(), found.end());
            heard = found[found.size() / 2];
        }
    }
    const int heardNote = heard > 0 ? juce::roundToInt (69.0 + 12.0 * std::log2 (heard / 440.0)) : -1;
    if (named >= 0 && heardNote >= 0)
    {
        // The name's note, in the octave heard.
        int best = heardNote - ((heardNote - named) % 12 + 12) % 12;
        if (heardNote - best > 6)
            best += 12;
        how = "its name and pitch";
        return juce::jlimit (0, 127, best);
    }
    if (named >= 0)
    {
        how = "its name";
        return 60 + named;
    }
    if (heardNote >= 0 && heardNote < 128)
    {
        how = "its pitch";
        return heardNote;
    }
    how = "no note found, so C3: set its note in Sound";
    return 60;
}

juce::String ChordsView::loadSound (const juce::File& file)
{
    auto& sampler = engine.getSampler();
    juce::String error;
    auto audio = crate::AudioFiles::load (file, sampler.getFormats(), error);
    if (audio == nullptr)
        return error.isNotEmpty() ? error : "Couldn't open " + file.getFileName() + ".";
    juce::String how;
    const int root = guessRoot (*audio, file.getFileNameWithoutExtension(), how);
    chords.stopAll();
    engine.putInSampler (juce::AudioBuffer<float> (*audio), file.getFileNameWithoutExtension());
    auto& sampleKeys = sampler.getKeys();
    sampleKeys.setRootNote (root);
    if (sampleKeys.getMode() == crate::KeyboardSampler::Mode::slices || sampleKeys.getMode() == crate::KeyboardSampler::Mode::oneShot)
        sampleKeys.setMode (crate::KeyboardSampler::Mode::hold);
    setTarget ((int) RigRouting::Dest::sampler);
    const auto text = "The chords play " + file.getFileNameWithoutExtension() + " now, its note " + RigChords::noteName (root)
                      + " (from " + how + ").";
    say (text);
    return text;
}

bool ChordsView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (crate::AudioFiles::isAudioFile (juce::File (f), engine.getSampler().getFormats()))
            return true;
    return false;
}

void ChordsView::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint();
}

void ChordsView::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint();
}

void ChordsView::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (const auto& f : files)
        if (crate::AudioFiles::isAudioFile (juce::File (f), engine.getSampler().getFormats()))
        {
            loadSound (juce::File (f));
            break;
        }
    refresh();
}

void ChordsView::showSoundMenu()
{
    auto& sampler = engine.getSampler();
    auto& doc = sampler.getDocument();
    auto& sampleKeys = sampler.getKeys();
    const bool has = doc.hasAudio();
    juce::PopupMenu m;
    m.addItem (1, "Your own sample... (or drop one on the chords)");
    if (has)
    {
        m.addSeparator();
        m.addSectionHeader ("Sampler: " + doc.getName() + ", its note " + RigChords::noteName (sampleKeys.getRootNote()));
        m.addItem (2, "Play the chords on it", true, targetDest() == (int) RigRouting::Dest::sampler);
        juce::PopupMenu roots;
        for (int octave = 0; octave <= 6; ++octave)
        {
            juce::PopupMenu notes;
            for (int i = 0; i < 12; ++i)
            {
                const int n = 24 + octave * 12 + i;
                notes.addItem (1000 + n, RigChords::noteName (n), true, sampleKeys.getRootNote() == n);
            }
            roots.addSubMenu ("C" + juce::String (octave) + " to B" + juce::String (octave), notes);
        }
        m.addSubMenu ("Its note (what it plays as recorded)", roots);
        m.addItem (10, "Plays while held", true, sampleKeys.getMode() == crate::KeyboardSampler::Mode::hold);
        m.addItem (11, "Loops while held", true, sampleKeys.getMode() == crate::KeyboardSampler::Mode::loop);
        m.addItem (12, "Plays to the end", true, sampleKeys.getMode() == crate::KeyboardSampler::Mode::oneShot);
        m.addItem (3, "Edit it in the Sampler (trim, loop, effects)", onOpenSampler != nullptr);
    }
    m.addSeparator();
    m.addItem (4, "Your plugins (Arturia...): play on the Synth or Bass", true, false);
    juce::Component::SafePointer<ChordsView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& k = safe->engine.getSampler().getKeys();
        if (r == 1)
        {
            safe->chooser = std::make_unique<juce::FileChooser> ("A sound for the chords",
                                                                 juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Splice"),
                                                                 crate::AudioFiles::wildcard (safe->engine.getSampler().getFormats()));
            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
            {
                if (safe != nullptr && fc.getResult().existsAsFile())
                    safe->loadSound (fc.getResult());
            });
        }
        else if (r == 2)
            safe->setTarget ((int) RigRouting::Dest::sampler);
        else if (r == 3 && safe->onOpenSampler)
            safe->onOpenSampler();
        else if (r == 4)
        {
            safe->setTarget ((int) RigRouting::Dest::synth);
            safe->say ("The chords play the Synth: load your plugin (Arturia, Quake...) on its screen with Plugin, and the chords play it.");
        }
        else if (r >= 10 && r <= 12)
            k.setMode (r == 10 ? crate::KeyboardSampler::Mode::hold : r == 11 ? crate::KeyboardSampler::Mode::loop : crate::KeyboardSampler::Mode::oneShot);
        else if (r >= 1000)
        {
            k.setRootNote (r - 1000);
            safe->say ("The sample's note is " + RigChords::noteName (r - 1000) + ": that key plays it as recorded.");
        }
        safe->refresh();
    });
}

void ChordsView::pressFill()
{
    chords.fillFromKey (key.getSelectedId() - 1, scale.getSelectedId() - 1, type.getSelectedId() - 1);
    say ("Pads 1-8: the chords of " + key.getText() + " " + scale.getText() + " (" + type.getText() + ").");
    refresh();
}

void ChordsView::pickStep (int step, int pad)
{
    if (step < 0 || step >= RigChords::kSteps)
        return;
    chords.steps[(size_t) step] = juce::jlimit (-1, RigChords::kPads - 1, pad);
    refresh();
}

void ChordsView::pressPlayProgression()
{
    if (chords.progressionSteps() == 0)
    {
        say ("The progression is empty: click a step to put a chord pad on it.");
        return;
    }
    chords.progressionOn = ! chords.progressionOn.load();
    if (chords.progressionOn.load())
    {
        wakeVocoder();
        say (engine.getTransport().isPlaying() ? "The progression plays on the clock." : "Press Play at the top to hear the progression.");
    }
    refresh();
}

juce::String ChordsView::pressToLoop()
{
    auto& looper = engine.getLooper();
    if (loopLayer >= 0)
    {
        const auto state = looper.getState (loopLayer);
        if (state == RigLooper::State::armed || state == RigLooper::State::recording)
            looper.pressRecord (loopLayer);
        const int layer = loopLayer;
        loopLayer = -1;
        refresh();
        return "Looping it on layer " + juce::String (layer + 1) + ".";
    }
    const int dest = targetDest(), source = looperSourceFor (dest);
    if (source < 0)
        return "Pick where the chords play first.";
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (looper.getState (i) == RigLooper::State::empty)
        {
            auto& layer = looper.getLayer (i);
            layer.source = source;
            const int beatsPerBar = juce::jmax (1, engine.getTransport().getBeatsPerBar());
            const double beats = chords.progressionOn.load() ? chords.progressionBeats() : 0.0;
            layer.bars = beats > 0 ? juce::jmax (1, (int) std::ceil (beats / beatsPerBar - 1.0e-6)) : 0;
            looper.pressRecord (i);
            loopLayer = i;
            refresh();
            juce::String text = "Recording the " + RigRouting::destName (dest) + " onto loop layer " + juce::String (i + 1);
            text << (engine.getTransport().isPlaying() ? " from the next bar" : " when you press Play");
            text << (layer.bars.load() > 0 ? ", for " + juce::String (layer.bars.load()) + " bars (the progression)." : ". Press To loop again to loop it.");
            return text;
        }
    return "Every loop layer is in use. Clear one on Loops first.";
}

void ChordsView::pressRecMidi()
{
    if (! chords.isTaking())
    {
        chords.startTake();
        say ("Recording what the chords play as MIDI. Press Stop MIDI when you're done.");
    }
    else
    {
        chords.stopTake();
        if (! chords.hasTake())
            say ("Nothing was played, so there's no MIDI to save.");
        else
        {
            auto file = RigChords::toLogicFolder().getNonexistentChildFile ("Chords take", ".mid");
            if (chords.writeTake (file, engine.getTransport().getBpm()))
            {
                lastFile = file;
                say ("Saved " + file.getFileName() + ": drag it from the chip onto a Logic track.");
            }
            else
                say ("Couldn't save the MIDI file in " + RigChords::toLogicFolder().getFullPathName() + ".");
        }
    }
    chip->repaint();
    refresh();
}

juce::File ChordsView::pressToLogic()
{
    if (chords.progressionSteps() == 0)
    {
        say ("The progression is empty: click a step to put a chord on it, or use Rec MIDI to record what you play.");
        return {};
    }
    auto file = RigChords::toLogicFolder().getNonexistentChildFile ("Chord progression", ".mid");
    if (! chords.writeProgression (file, engine.getTransport().getBpm(), engine.getTransport().getBeatsPerBar()))
    {
        say ("Couldn't save the MIDI file in " + RigChords::toLogicFolder().getFullPathName() + ".");
        return {};
    }
    lastFile = file;
    say ("Saved " + file.getFileName() + ": drag it from the chip onto a Logic track.");
    chip->repaint();
    return file;
}

void ChordsView::pressCatch()
{
    if (chords.isCatching())
    {
        chords.cancelCatch();
        say ("Catch cancelled.");
    }
    else
    {
        chords.catchFromKeys (selected);
        say ("Hold a chord on your keyboard and let go: it becomes pad " + juce::String (selected + 1) + ".");
    }
    refresh();
}

void ChordsView::rename (int pad)
{
    auto* w = new juce::AlertWindow ("Name pad " + juce::String (pad + 1), "Your own name for this chord (empty: its chord name).",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", chords.getPad (pad).name);
    w->addButton ("Name it", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ChordsView> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, pad] (int result)
    {
        if (safe != nullptr && result == 1)
        {
            const auto& p = safe->chords.getPad (pad);
            safe->chords.setPad (pad, p.notes, w->getTextEditorContents ("name").trim().substring (0, 12));
            safe->refresh();
        }
    }), true);
}

void ChordsView::showPadMenu (int pad)
{
    juce::PopupMenu copyTo;
    for (int i = 0; i < RigChords::kPads; ++i)
        if (i != pad)
            copyTo.addItem (100 + i, "Pad " + juce::String (i + 1) + (chords.isEmpty (i) ? juce::String (" (empty)") : " (" + chords.padName (i) + ")"));
    juce::PopupMenu steps;
    for (int i = 0; i < RigChords::kSteps; ++i)
        steps.addItem (200 + i, "Step " + juce::String (i + 1));
    juce::PopupMenu m;
    m.addSectionHeader ("Pad " + juce::String (pad + 1) + (chords.isEmpty (pad) ? juce::String() : ": " + chords.padName (pad)));
    m.addItem (1, "Catch from your keyboard");
    m.addSubMenu ("Copy to", copyTo, ! chords.isEmpty (pad));
    m.addSubMenu ("Put in the progression", steps, ! chords.isEmpty (pad));
    m.addItem (2, "Name...");
    m.addItem (3, "Clear", ! chords.isEmpty (pad));
    juce::Component::SafePointer<ChordsView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (padButtons[pad]), [safe, pad] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& c = safe->chords;
        if (r == 1)
        {
            safe->selectPad (pad);
            safe->pressCatch();
        }
        else if (r == 2)
            safe->rename (pad);
        else if (r == 3)
            c.clearPad (pad);
        else if (r >= 200)
            safe->pickStep (r - 200, pad);
        else if (r >= 100)
            c.copyPad (pad, r - 100);
        safe->refresh();
    });
}

void ChordsView::showStepMenu (int step)
{
    juce::PopupMenu m;
    m.addSectionHeader ("Step " + juce::String (step + 1));
    const int now = chords.steps[(size_t) step].load();
    for (int i = 0; i < RigChords::kPads; ++i)
        if (! chords.isEmpty (i))
            m.addItem (1 + i, "Pad " + juce::String (i + 1) + ": " + chords.padName (i), true, now == i);
    m.addSeparator();
    m.addItem (100, "Rest (no chord)", true, now < 0);
    m.addItem (101, "Clear the progression");
    juce::Component::SafePointer<ChordsView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (stepButtons[step]), [safe, step] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        if (r == 101)
        {
            for (int i = 0; i < RigChords::kSteps; ++i)
                safe->pickStep (i, -1);
            safe->chords.progressionOn = false;
        }
        else
            safe->pickStep (step, r == 100 ? -1 : r - 1);
        safe->refresh();
    });
}

void ChordsView::showSetsMenu()
{
    juce::PopupMenu starters;
    const auto names = RigChords::starterNames();
    for (int i = 0; i < names.size(); ++i)
        starters.addItem (1 + i, names[i]);
    juce::PopupMenu yours;
    const auto files = RigChords::setsFolder().findChildFiles (juce::File::findFiles, false, "*.chords");
    for (int i = 0; i < files.size() && i < 200; ++i)
        yours.addItem (100 + i, files[i].getFileNameWithoutExtension());
    juce::PopupMenu m;
    m.addSubMenu ("Starters", starters);
    m.addSubMenu ("Your sets", yours, ! files.isEmpty());
    m.addSeparator();
    m.addItem (50, "Save these chords as a set...");
    m.addItem (51, "Show the sets folder");
    juce::Component::SafePointer<ChordsView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (setsButton), [safe, files, names] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& c = safe->chords;
        if (r >= 100)
        {
            const auto file = files[r - 100];
            safe->say (c.loadSet (file) ? "Opened your set " + file.getFileNameWithoutExtension() + "." : "Couldn't open " + file.getFileName() + ".");
        }
        else if (r == 50)
        {
            auto* w = new juce::AlertWindow ("Save chord set", "A name for these 16 pads and the progression.", juce::MessageBoxIconType::NoIcon, safe.getComponent());
            w->addTextEditor ("name", "My chords");
            w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
            {
                if (safe == nullptr || result != 1)
                    return;
                const auto name = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
                if (name.isEmpty())
                    return;
                const auto file = RigChords::setsFolder().getChildFile (name + ".chords");
                safe->say (safe->chords.saveSet (file) ? "Saved your set " + name + " (Sets > Your sets)." : "Couldn't save the set.");
            }), true);
        }
        else if (r == 51)
        {
            RigChords::setsFolder().createDirectory();
            RigChords::setsFolder().revealToUser();
        }
        else if (r >= 1 && r <= names.size())
        {
            c.stopAll();
            c.loadStarter (r - 1);
            safe->say ("Starter: " + names[r - 1] + ". Make the pads your own, then Sets > Save.");
        }
        safe->refresh();
    });
}

//==============================================================================
void ChordsView::paint (juce::Graphics& g)
{
    vintage::drawFace (g, getLocalBounds());
    g.setColour (vintage::colours::selected);
    g.fillRect (getLocalBounds().removeFromTop (4));

    vintage::engraved (g, "Chords", titleArea, juce::Justification::centredLeft, 22.0f);
    const char* labels[] = { "Plays", "From", "Key", "Map", "", "" };
    for (size_t i = 0; i < labelAreas.size(); ++i)
        if (labels[i][0] != 0)
            vintage::engraved (g, labels[i], labelAreas[i], juce::Justification::centredRight, 13.0f);

    // Live key: its well, title and what it plays in.
    {
        static const char* const names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
        vintage::drawWell (g, liveWell.toFloat(), 10.0f);
        auto title = liveTitle;
        vintage::engraved (g, "Live key", title.removeFromTop (title.getHeight() / 2 + 2), juce::Justification::bottomLeft, 15.0f, true);
        vintage::engraved (g, juce::String ("Written in ") + names[chords.fillKey], title, juce::Justification::topLeft, 11.0f, true,
                           juce::Colour (0xff9c8f7e));
        auto box = liveLcd.toFloat();
        vintage::drawLcd (g, box);
        const int playingShift = chords.getPlayingShift(), octave = chords.getLiveOctave();
        juce::String text;
        if (chords.isLiveKeyWaiting() && chords.getLiveKey() != chords.getPlayingKey())
            text << "NEXT: " << names[chords.getLiveKey()];
        else
            text << "IN " << names[chords.getPlayingKey()];
        if (octave != 0)
            text << "  OCT " << (octave > 0 ? "+" : "") << octave;
        if (playingShift != 0 && ! chords.isLiveKeyWaiting())
            text << "  (" << (playingShift > 0 ? "+" : "") << playingShift << ")";
        vintage::lcdText (g, text, box.reduced (8.0f, 0), 18.0f, juce::Justification::centredLeft,
                          chords.isLiveKeyWaiting() ? vintage::colours::next : vintage::colours::lcdAmber);
    }

    vintage::drawWell (g, padsWell.toFloat(), 12.0f);

    // The picked pad: its number, name and notes.
    {
        auto box = editLcd.toFloat();
        vintage::drawLcd (g, box);
        const auto& pad = chords.getPad (selected);
        juce::StringArray noteText;
        for (int n : pad.notes)
            noteText.add (RigChords::noteName (n));
        juce::String text = "PAD " + juce::String (selected + 1) + "  ";
        text << (pad.notes.isEmpty() ? juce::String ("EMPTY: CLICK KEYS TO BUILD") : chords.padName (selected) + "   " + noteText.joinIntoString (" "));
        vintage::lcdText (g, text, box.reduced (10.0f, 0), 19.0f, juce::Justification::centredLeft);
    }

    vintage::drawWell (g, progressionWell.toFloat(), 12.0f);
    vintage::engraved (g, "Progression", progressionTitle, juce::Justification::centredLeft, 15.0f, true);
    {
        const double beats = chords.progressionBeats();
        const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
        const auto len = beats <= 0 ? juce::String ("empty")
                                    : std::fmod (beats, (double) bpb) < 0.001 ? juce::String ((int) (beats / bpb)) + ((int) (beats / bpb) == 1 ? " bar" : " bars")
                                                                              : juce::String ((int) beats) + " beats";
        vintage::engraved (g, len, progressionTitle, juce::Justification::centredRight, 12.0f, true);
    }

    if (dragOver)
    {
        g.setColour (vintage::colours::play.withAlpha (0.18f));
        g.fillRect (getLocalBounds());
        g.setColour (vintage::colours::play);
        g.drawRect (getLocalBounds(), 3);
        vintage::engraved (g, "Drop it: the chords play your sound", getLocalBounds().removeFromTop (60), juce::Justification::centred, 22.0f);
    }

    auto statusBox = statusArea.toFloat();
    vintage::drawLcd (g, statusBox);
    vintage::lcdText (g, status.isEmpty() ? juce::String ("CLICK A PAD TO PLAY IT. CLICK KEYS TO CHANGE IT.") : status.toUpperCase(),
                      statusBox.reduced (10.0f, 0), 15.0f, juce::Justification::centredLeft);
}

void ChordsView::resized()
{
    auto r = getLocalBounds().reduced (16, 10).withTrimmedTop (4);
    // The progression down the right.
    auto right = r.removeFromRight (juce::jlimit (250, 320, r.getWidth() / 4));
    r.removeFromRight (14);

    {
        auto top = right.removeFromTop (34);
        closeButton.setBounds (top.removeFromRight (80).reduced (0, 1));
        top.removeFromRight (8);
        setsButton.setBounds (top.removeFromRight (72).reduced (0, 1));
        top.removeFromRight (8);
        soundButton.setBounds (top.removeFromRight (78).reduced (0, 1));
    }
    right.removeFromTop (8);
    progressionWell = right;
    {
        auto w = right.reduced (10, 8);
        progressionTitle = w.removeFromTop (22);
        w.removeFromTop (4);
        auto grid = w.removeFromTop (88);
        const int cw = grid.getWidth() / 4;
        for (int i = 0; i < stepButtons.size(); ++i)
        {
            auto row = grid.withHeight (grid.getHeight() / 2).translated (0, (i / 4) * (grid.getHeight() / 2));
            stepButtons[i]->setBounds (row.withX (row.getX() + (i % 4) * cw).withWidth (cw).reduced (2));
        }
        w.removeFromTop (6);
        auto row = w.removeFromTop (34);
        length.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
        playButton.setBounds (row.reduced (2));
        w.removeFromTop (4);
        row = w.removeFromTop (34);
        loopButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2));
        recButton.setBounds (row.reduced (2));
        w.removeFromTop (4);
        row = w.removeFromTop (34);
        logicButton.setBounds (row.removeFromLeft (row.getWidth() * 2 / 5).reduced (2));
        chip->setBounds (row.reduced (2));
    }

    // The top row: title, where they play, what plays them, Hold, Strum.
    {
        auto top = r.removeFromTop (34);
        titleArea = top.removeFromLeft (104);
        chordsOnButton.setBounds (top.removeFromLeft (118).reduced (0, 1));
        top.removeFromLeft (12);
        // What's left shared out: where they play gets the most room.
        const int spare = juce::jmax (0, top.getWidth() - (48 + 6 + 8 + 44 + 6 + 10 + 64 + 8));
        labelAreas[0] = top.removeFromLeft (48);
        top.removeFromLeft (6);
        target.setBounds (top.removeFromLeft (spare * 36 / 100).reduced (0, 2));
        top.removeFromLeft (8);
        labelAreas[1] = top.removeFromLeft (44);
        top.removeFromLeft (6);
        trigger.setBounds (top.removeFromLeft (spare * 34 / 100).reduced (0, 2));
        top.removeFromLeft (10);
        holdButton.setBounds (top.removeFromLeft (64).reduced (0, 1));
        top.removeFromLeft (8);
        strum.setBounds (top.reduced (0, 2));
    }
    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (30);
        labelAreas[2] = row.removeFromLeft (40);
        row.removeFromLeft (6);
        key.setBounds (row.removeFromLeft (64).reduced (0, 1));
        row.removeFromLeft (4);
        scale.setBounds (row.removeFromLeft (150).reduced (0, 1));
        row.removeFromLeft (4);
        type.setBounds (row.removeFromLeft (110).reduced (0, 1));
        row.removeFromLeft (8);
        fillButton.setBounds (row.removeFromLeft (128));
        row.removeFromLeft (12);
        statusArea = row;
    }
    r.removeFromTop (8);
    // Live key: its title, the twelve keys, octave and Back, when it lands, Learn and what it plays in.
    liveWell = r.removeFromTop (48);
    {
        auto w = liveWell.reduced (10, 5);
        liveTitle = w.removeFromLeft (92);
        w.removeFromLeft (6);
        liveKeys->setBounds (w.removeFromLeft (juce::jlimit (230, 340, w.getWidth() * 34 / 100)));
        w.removeFromLeft (12);
        auto half = [] (juce::Rectangle<int> a) { return a.withSizeKeepingCentre (a.getWidth(), juce::jmin (32, a.getHeight())); };
        liveOctDown.setBounds (half (w.removeFromLeft (62)).reduced (2, 2));
        liveOctUp.setBounds (half (w.removeFromLeft (62)).reduced (2, 2));
        liveHome.setBounds (half (w.removeFromLeft (96)).reduced (2, 2));
        w.removeFromLeft (8);
        liveChange.setBounds (half (w.removeFromLeft (112)).reduced (0, 3));
        w.removeFromLeft (8);
        liveLearn.setBounds (half (w.removeFromLeft (84)).reduced (2, 2));
        w.removeFromLeft (8);
        liveLcd = half (w).reduced (0, 3);
    }
    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (30);
        labelAreas[3] = row.removeFromLeft (40);
        row.removeFromLeft (6);
        mapButton.setBounds (row.removeFromLeft (100));
        row.removeFromLeft (8);
        const int spare = juce::jmax (0, row.getWidth() - (80 + 90 + 3 * 6));
        mapLayout.setBounds (row.removeFromLeft (spare * 36 / 100).reduced (0, 1));
        row.removeFromLeft (6);
        mapOffScale.setBounds (row.removeFromLeft (spare * 32 / 100).reduced (0, 1));
        row.removeFromLeft (6);
        mapVoicing.setBounds (row.removeFromLeft (spare * 32 / 100).reduced (0, 1));
        row.removeFromLeft (6);
        mapOctave.setBounds (row.removeFromLeft (80).reduced (0, 1));
        row.removeFromLeft (6);
        bassButton.setBounds (row.removeFromLeft (90));
    }
    r.removeFromTop (8);
    padsWell = r.removeFromTop (106);
    {
        auto w = padsWell.reduced (6, 5);
        const int pw = w.getWidth() / 8, ph = w.getHeight() / 2;
        for (int i = 0; i < padButtons.size(); ++i)
            padButtons[i]->setBounds (w.getX() + (i % 8) * pw, w.getY() + (i / 8) * ph, pw, ph);
    }
    r.removeFromTop (8);
    {
        auto row = r.removeFromTop (32);
        // Edit buttons on the right, the pad's LCD on the left.
        juce::TextButton* edits[] = { &lowerButton, &higherButton, &octDown, &octUp, &spreadButton, &closeVoicing, &catchButton, &nameButton, &clearButton };
        const int bw = juce::jlimit (58, 84, (row.getWidth() * 62 / 100) / 9);
        for (int i = 8; i >= 0; --i)
        {
            edits[i]->setBounds (row.removeFromRight (bw).reduced (2, 0));
            if (i == 6 || i == 4)
                row.removeFromRight (6);
        }
        row.removeFromRight (8);
        editLcd = row;
    }
    r.removeFromTop (8);
    keys->setBounds (r);
}
