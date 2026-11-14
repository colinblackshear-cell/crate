#include "ChordsView.h"

#include "Parameters.h"
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
            const auto name = chords.padName (index);
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
        setTooltip ("Click a key to add it to the picked chord, or take it out. You hear the chord as it changes.");
    }

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
        const int low = chords.lowKey.load();

        g.setColour (juce::Colour (0xff2a2622));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

        auto shade = [&] (int note, juce::Rectangle<float> key, bool black)
        {
            const bool inChord = pad.notes.contains (note), playing = chords.isNoteSounding (note);
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

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int note = noteAt (e.position);
        if (note >= 0)
            view.clickKey (note);
    }

private:
    ChordsView& view;
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
ChordsView::ChordsView (RigEngine& e) : engine (e), chords (e.getChords())
{
    selected = juce::jlimit (0, RigChords::kPads - 1, chords.shapePad.load());

    target.addItem ("The picked track", 1);
    for (auto d : { RigRouting::Dest::vocoder, RigRouting::Dest::synth, RigRouting::Dest::bass, RigRouting::Dest::sampler,
                    RigRouting::Dest::mpc, RigRouting::Dest::drums })
        target.addItem (RigRouting::destName ((int) d), 2 + (int) d);
    target.setTooltip ("Where the chords play: the track picked on the left (it follows as you pick another), or always one instrument.");
    target.onChange = [this] { setTarget (target.getSelectedId() == 1 ? RigChords::kFollowKeys : target.getSelectedId() - 2); };

    for (int i = 0; i <= (int) RigChords::Trigger::everyKey; ++i)
        trigger.addItem (RigChords::triggerName (i), i + 1);
    trigger.setTooltip ("What plays the pads besides your clicks: the Launchkey's 16 drum pads (instead of the MPC while it's on), "
                        "its lowest 16 keys (marked blue on the keys below), or every key (each key plays the picked chord from that note).");
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
    key.setTooltip ("Fill's key");
    scale.setTooltip ("Fill's scale");
    type.setTooltip ("Fill's chord type: simple triads, sevenths, ninths, sus, power chords...");
    for (auto* c : { &key, &scale, &type })
        c->onChange = [this]
        {
            chords.fillKey = key.getSelectedId() - 1;
            chords.fillScale = scale.getSelectedId() - 1;
            chords.fillType = type.getSelectedId() - 1;
        };

    for (int i = 0; i < (int) RigChords::stepLengths().size(); ++i)
        length.addItem (RigChords::stepLengthName (i), i + 1);
    length.setTooltip ("How long each step of the progression plays.");
    length.onChange = [this] { chords.stepLength = length.getSelectedId() - 1; updateControls(); };

    for (auto* c : { &target, &trigger, &strum, &key, &scale, &type, &length })
        addAndMakeVisible (c);

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
                     &closeVoicing, &catchButton, &nameButton, &clearButton, &playButton, &loopButton, &recButton, &logicButton })
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

void ChordsView::refresh()
{
    updateControls();
    for (auto* p : padButtons)
        p->repaint();
    keys->repaint();
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
    length.setSelectedId (chords.stepLength.load() + 1, juce::dontSendNotification);
    holdButton.setToggleState (chords.hold.load(), juce::dontSendNotification);
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
        const auto text = pad < 0 ? juce::String ("-") : chords.isEmpty (pad) ? juce::String (pad + 1) : chords.padName (pad);
        if (stepButtons[i]->getButtonText() != text)
            stepButtons[i]->setButtonText (text);
        stepButtons[i]->setToggleState (i == playing, juce::dontSendNotification);
        stepButtons[i]->setAlpha (i < juce::jmax (1, chords.progressionSteps()) || pad >= 0 ? 1.0f : 0.6f);
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
    chords.trigger = juce::jlimit (0, (int) RigChords::Trigger::everyKey, t);
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
        case RigChords::Trigger::screen:
        default:
            say ("Your keyboard plays notes as usual; click the pads to play chords.");
            break;
    }
    refresh();
}

void ChordsView::pressHold()
{
    chords.stopAll();
    chords.hold = ! chords.hold.load();
    refresh();
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
    const char* labels[] = { "Plays", "From", "Fill", "", "", "" };
    for (size_t i = 0; i < labelAreas.size(); ++i)
        if (labels[i][0] != 0)
            vintage::engraved (g, labels[i], labelAreas[i], juce::Justification::centredRight, 13.0f);

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
        closeButton.setBounds (top.removeFromRight (90).reduced (0, 1));
        top.removeFromRight (8);
        setsButton.setBounds (top.removeFromRight (80).reduced (0, 1));
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
