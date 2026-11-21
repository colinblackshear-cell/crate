#include "RigPanels.h"
#include "RigSong.h"
#include "RigColours.h"

namespace rigpanels
{
using namespace vintage;

//==============================================================================
PanelButton::PanelButton (const juce::String& t) : juce::Button (t), text (t)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PanelButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    const auto& p = pal();
    auto r = getLocalBounds().toFloat();
    juce::Colour ink;
    if (empty)
    {
        g.setColour (p.line);
        g.drawRoundedRectangle (r.reduced (1.0f).withTrimmedBottom (3.0f), 9.0f, 2.0f);
        ink = p.ink2;
    }
    else
    {
        if (redRing)
        {
            const auto face = r.withTrimmedBottom (3.0f).translated (0, down ? 2.0f : 0.0f);
            g.setColour (colours::arm);
            g.fillRoundedRectangle (face.expanded (3.0f), 12.0f);
        }
        drawButton (g, r, lit, down, over && isEnabled(), inWell);
        switch (lit)
        {
            case Lit::play:    ink = juce::Colour (0xff062611); break;
            case Lit::arm:     ink = juce::Colour (0xff2a0602); break;
            case Lit::next:
            case Lit::current: ink = juce::Colour (0xff2a1a05); break;
            case Lit::mute:    ink = juce::Colour (0xff06233a); break;
            default:           ink = inWell ? juce::Colour (0xfff1e7d1) : p.btnInk; break;
        }
    }
    if (! isEnabled())
        ink = ink.withMultipliedAlpha (0.45f);

    auto area = getLocalBounds().withTrimmedBottom (3).translated (0, down && ! empty ? 2 : 0).reduced (4, 2);
    const auto upper = text.toUpperCase();
    const auto font = cond (fontSize, Weight::extra, 0.08f);
    float textWidth = juce::GlyphArrangement::getStringWidth (font, upper);

    // Icon above the word.
    if (icon != Icon::none)
    {
        auto iconArea = area.removeFromTop (area.getHeight() * 11 / 20).toFloat();
        const float s = juce::jmin (fontSize * 2.6f, iconArea.getHeight() - 4.0f);
        auto ic = juce::Rectangle<float> (s, s).withCentre ({ iconArea.getCentreX(), iconArea.getBottom() - s * 0.5f - 1.0f });
        juce::Path path;
        if (icon == Icon::back || icon == Icon::forward)
        {
            // A chevron, pointing the way it goes.
            const float x0 = ic.getX() + s * (icon == Icon::back ? 0.62f : 0.38f), x1 = ic.getX() + s * (icon == Icon::back ? 0.32f : 0.68f);
            path.startNewSubPath (x0, ic.getY() + s * 0.18f);
            path.lineTo (x1, ic.getCentreY());
            path.lineTo (x0, ic.getBottom() - s * 0.18f);
            g.setColour (ink);
            g.strokePath (path, juce::PathStrokeType (s * 0.15f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            path.clear();
        }
        else if (icon == Icon::play)
            path.addTriangle (ic.getX() + s * 0.2f, ic.getY() + s * 0.12f, ic.getRight() - s * 0.08f, ic.getCentreY(), ic.getX() + s * 0.2f,
                              ic.getBottom() - s * 0.12f);
        else if (icon == Icon::stop)
            path.addRoundedRectangle (ic.reduced (s * 0.2f), 1.5f);
        else
            path.addEllipse (ic.reduced (s * 0.16f));
        g.setColour (icon == Icon::rec && lit == Lit::none ? colours::arm : ink);
        g.fillPath (path);
    }

    g.setColour (ink);
    g.setFont (font);
    if (sub.isNotEmpty())
    {
        auto top = area.removeFromTop (area.getHeight() * 3 / 5);
        g.drawFittedText (upper, top, juce::Justification::centredBottom, 1, 0.7f);
        g.setFont (cond (subSize, Weight::bold, 0.12f));
        g.drawFittedText (sub.toUpperCase(), area, juce::Justification::centredTop, 1, 0.7f);
    }
    else if (key.isNotEmpty())
    {
        // The word with its key hint beside it, like "GROOVE [F1]".
        const auto keyFont = barlow (12.0f);
        const float keyWidth = juce::GlyphArrangement::getStringWidth (keyFont, key) + 12.0f;
        textWidth = juce::jmin (textWidth, (float) area.getWidth() - keyWidth - 12.0f);
        const float total = textWidth + 12.0f + keyWidth;
        auto row = area.toFloat().withSizeKeepingCentre (juce::jmin (total, (float) area.getWidth()), (float) area.getHeight());
        g.drawFittedText (upper, row.removeFromLeft (textWidth).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        row.removeFromLeft (12.0f);
        if (row.getWidth() >= keyWidth - 1.0f)
        {
            auto chip = row.removeFromLeft (keyWidth).withSizeKeepingCentre (keyWidth, 18.0f);
            g.setColour (ink.withAlpha (0.8f));
            g.drawRoundedRectangle (chip.reduced (0.75f), 4.0f, 1.5f);
            g.setFont (keyFont);
            g.drawText (key, chip, juce::Justification::centred);
        }
    }
    else
        g.drawFittedText (upper, area, juce::Justification::centred, 2, 0.7f);
}

//==============================================================================
bool Header::Shown::operator== (const Shown& o) const
{
    return std::abs (bpm - o.bpm) < 0.01 && rampTo == o.rampTo && partLetter == o.partLetter && partName == o.partName && barBeat == o.barBeat
        && partQueued == o.partQueued && std::abs (vuLeft - o.vuLeft) < 0.004f && std::abs (vuRight - o.vuRight) < 0.004f
        && keyboard == o.keyboard && keyboardFound == o.keyboardFound && keyboardActive == o.keyboardActive && macKeys == o.macKeys
        && micOn == o.micOn && recording == o.recording && songOn == o.songOn;
}

Header::Header (bool isStandalone) : standalone (isStandalone)
{
    for (auto* b : { &play, &stop, &rec })
    {
        b->fontSize = 13.0f;
        addAndMakeVisible (b);
    }
    play.icon = PanelButton::Icon::play;
    stop.icon = PanelButton::Icon::stop;
    rec.icon = PanelButton::Icon::rec;
    for (auto* b : { &up, &down })
    {
        b->fontSize = 20.0f;
        addAndMakeVisible (b);
    }
    tap.fontSize = 16.0f;
    addAndMakeVisible (tap);
    quantize.fontSize = 13.0f;
    addAndMakeVisible (quantize);
    jam.fontSize = 12.0f;
    jam.setTooltip ("The song is playing and decides which tracks play, so a track you switch on can stay quiet. "
                    "Jam hands control back to you: everything you switch on plays, and the song waits on the Song screen.");
    addChildComponent (jam);
    show.fontSize = 15.0f;
    show.subSize = 11.0f;
    show.sub = juce::String::fromUTF8 ("Not saved yet \xc2\xb7 menu");
    show.setTooltip ("Your show: New song, Open, Save (Cmd+S), Save as, Export for Logic, and the Guide.");
    addAndMakeVisible (show);
    play.setTooltip ("Starts and stops every tool together (space bar).");
    stop.setTooltip ("Stops, goes back to the start, and silences any notes still sounding.");
    rec.setTooltip ("Rec set: records the whole set for Logic, from the next bar: every tool as its own WAV, the mix, and what you play as MIDI. "
                    "Press again to stop. Saved in Music > Crate > Rig > Recordings.");
    up.setTooltip ("Tempo up (hold Shift for a tenth).");
    down.setTooltip ("Tempo down (hold Shift for a tenth).");
    tap.setTooltip ("Tap a few times in time to set the tempo.");
    quantize.setTooltip ("Quantize: pulls what you've played onto the beat (the MPC, drum machine, Bass, Synth or loops), "
                         "on the grid you pick. Cmd+U quantizes what's on screen straight away.");
}

void Header::update (const Shown& s)
{
    if (s == shown)
        return;
    shown = s;
    jam.setVisible (s.songOn && s.recording.isEmpty());
    repaint();
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    const int h = 66;
    // Room for everything? Narrow the VU meters, then drop them, then the wordmark, on a small
    // screen.
    constexpr int fixed = 276 + 128 + 8 + 46 + 8 + 84 + 16 + 176 + 12 + 150 + 12 + 118;
    constexpr int wordmark = 168 + 16, vuMin = 16 + 2 * 80 + 8, vuMax = 16 + 2 * 116 + 8;
    showVu = r.getWidth() >= wordmark + fixed + vuMin;
    showWordmark = r.getWidth() >= wordmark + fixed || showVu;

    if (showWordmark)
    {
        wordmarkArea = r.removeFromLeft (168);
        r.removeFromLeft (16);
    }
    else
        wordmarkArea = {};

    auto centreY = [&] (juce::Rectangle<int> b, int height) { return b.withSizeKeepingCentre (b.getWidth(), height); };
    for (auto* b : { &play, &stop, &rec })
    {
        b->setBounds (centreY (r.removeFromLeft (80), h));
        r.removeFromLeft (10);
    }
    r.removeFromLeft (6);
    tempoArea = centreY (r.removeFromLeft (128), h);
    r.removeFromLeft (8);
    {
        auto col = centreY (r.removeFromLeft (46), h);
        up.setBounds (col.removeFromTop (32));
        col.removeFromTop (2);
        down.setBounds (col);
    }
    r.removeFromLeft (8);
    {
        // Tap over Quantize.
        auto col = centreY (r.removeFromLeft (84), h + 6);
        tap.setBounds (col.removeFromTop (col.getHeight() / 2 - 1));
        col.removeFromTop (2);
        quantize.setBounds (col);
    }
    r.removeFromLeft (16);
    partArea = centreY (r.removeFromLeft (176), h);
    jam.setBounds (partArea.getRight() - 62, partArea.getBottom() - 30, 54, 24);
    r.removeFromLeft (12);
    // Your show, beside the song's place: its name and its menu.
    show.setBounds (centreY (r.removeFromLeft (150), h));
    r.removeFromLeft (12);

    lampsArea = centreY (r.removeFromRight (118), 72);
    vuArea = {};
    if (showVu)
    {
        const int width = juce::jmin (vuMax, r.getWidth()) - 16;
        r.removeFromRight (16);
        vuArea = centreY (r.removeFromRight (width), 68);
    }
    micLampArea = lampsArea.withTrimmedTop (48);
    keyboardLampArea = lampsArea.withHeight (24);
}

void Header::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.setColour (p.line);
    g.fillRect (getLocalBounds().removeFromBottom (2));

    if (! wordmarkArea.isEmpty())
    {
        auto w = wordmarkArea.withSizeKeepingCentre (wordmarkArea.getWidth(), 56);
        g.setColour (p.ink);
        auto mark = wordmark (36.0f);
        const auto markWidth = juce::GlyphArrangement::getStringWidth (mark, "Kaleidobox");
        if (markWidth > (float) w.getWidth())
            mark = wordmark (36.0f * (float) w.getWidth() / markWidth);
        g.setFont (mark);
        g.drawText ("Kaleidobox", w.removeFromTop (40), juce::Justification::bottomLeft, false);
        engraved (g, juce::String::fromUTF8 ("Model KB-1 \xc2\xb7 Groove Station"), w.withTrimmedTop (2), juce::Justification::topLeft, 11.0f);
    }

    // Tempo.
    {
        auto r = tempoArea.toFloat();
        drawLcd (g, r);
        lcdText (g, juce::String (shown.bpm, 1), r.withTrimmedBottom (20).withTrimmedTop (4), 50.0f);
        lcdText (g, shown.rampTo > 0 ? juce::String::fromUTF8 ("\xe2\x86\x92 ") + juce::String (shown.rampTo, 0) + " BPM" : juce::String ("BPM"),
                 r.withTrimmedTop (r.getHeight() - 22).withTrimmedBottom (4), 16.0f, juce::Justification::centred,
                 shown.rampTo > 0 ? colours::lcdGreen : colours::lcdAmber);
    }

    // Where the song is: the part playing, the bar and beat.
    {
        auto r = partArea.toFloat();
        drawLcd (g, r);
        auto inner = r.reduced (14.0f, 2.0f);
        auto letter = inner.removeFromLeft (36.0f);
        lcdText (g, shown.partLetter.isNotEmpty() ? shown.partLetter : "-", letter, 58.0f, juce::Justification::centred,
                 shown.partLetter.isNotEmpty() ? colours::lcdGreen : colours::lcdAmber.withAlpha (0.5f));
        inner.removeFromLeft (12.0f);
        lcdText (g, shown.barBeat, inner.removeFromTop (inner.getHeight() * 0.58f).withTrimmedTop (4), 34.0f, juce::Justification::bottomLeft);
        if (shown.recording.isNotEmpty())
        {
            // Recording: a red line instead of the section's name, and a red edge.
            g.setColour (colours::arm);
            g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);
            const bool on = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
            lcdText (g, juce::String::fromUTF8 (on ? "\xe2\x97\x8f " : "  ") + shown.recording.toUpperCase(), inner, 15.0f,
                     juce::Justification::topLeft, colours::arm.brighter (0.3f));
        }
        else
        {
            if (shown.songOn)
            {
                // The song is in charge: a green edge, and room for Jam.
                g.setColour (colours::lcdGreen.withAlpha (0.8f));
                g.drawRoundedRectangle (r.reduced (1.0f), 8.0f, 2.0f);
                inner.removeFromRight ((float) jam.getWidth() + 2.0f);
            }
            lcdText (g, shown.partName.toUpperCase(), inner, 15.0f, juce::Justification::topLeft,
                     shown.partQueued ? colours::next : colours::lcdAmber.withAlpha (0.85f));
        }
    }

    if (! vuArea.isEmpty())
    {
        auto r = vuArea.toFloat();
        const float each = (r.getWidth() - 8.0f) / 2.0f;
        drawVu (g, r.removeFromLeft (each), shown.vuLeft, juce::String::fromUTF8 ("VU \xc2\xb7 L"));
        r.removeFromLeft (8.0f);
        drawVu (g, r.removeFromLeft (each), shown.vuRight, juce::String::fromUTF8 ("VU \xc2\xb7 R"));
    }

    // What's plugged in.
    {
        auto r = lampsArea;
        auto row = [&] (Lit lit, const juce::String& text)
        {
            auto line = r.removeFromTop (24);
            drawLamp (g, { (float) line.getX() + 8.0f, (float) line.getCentreY() }, lit);
            engraved (g, text, line.withTrimmedLeft (24), juce::Justification::centredLeft, 12.0f, false, p.ink);
        };
        row (shown.keyboardFound ? (shown.keyboardActive ? Lit::next : Lit::play) : Lit::none,
             shown.keyboard.isNotEmpty() ? shown.keyboard : juce::String ("No keyboard"));
        row (shown.macKeys ? Lit::play : Lit::none, standalone ? "Mac keys" : "Logic MIDI");
        row (shown.micOn ? Lit::arm : Lit::none, standalone ? (shown.micOn ? "Mic in" : "Mic off") : "Side chain");
    }
}

void Header::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && wordmarkArea.contains (e.getPosition()) && onWordmark)
        return onWordmark();
    if (e.mouseWasClicked() && tempoArea.contains (e.getPosition()) && onTempo)
        return onTempo();
    if (e.mouseWasClicked() && partArea.contains (e.getPosition()) && shown.recording.isNotEmpty() && onRecording)
        return onRecording();
    if (standalone && e.mouseWasClicked() && micLampArea.contains (e.getPosition()) && onMic)
        return onMic();
    if (standalone && e.mouseWasClicked() && keyboardLampArea.contains (e.getPosition()) && onKeyboard)
        onKeyboard();
}

//==============================================================================
const juce::StringArray& ScreenBar::views()
{
    static const juce::StringArray v { "Play", "Edit", "Song", "Mix" };
    return v;
}

ScreenBar::ScreenBar()
{
    const char* tips[] = { "Play (F1): the picked track's steps, patterns and eight knobs. Every track works the same way here.",
                           "Edit (F2): the picked track's instrument in full: the MPC and Sampler, the drum machine, Bass, Synth, "
                           "the Vocoder's pedals, the Loops, or Line in's amp.",
                           "Song (F3): the song's sections in order, and what every track plays in each. Build it, play it, record it.",
                           "Mix (F4): a fader, pan, sends and effects for every track and loop layer, and the FX pad." };
    for (int i = 0; i < views().size(); ++i)
    {
        auto* b = tabs.add (new PanelButton (views()[i]));
        b->key = "F" + juce::String (i + 1);
        b->fontSize = 20.0f;
        b->setTooltip (tips[i]);
        b->onClick = [this, i] { if (onView) onView (views()[i]); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &back, &forward })
    {
        b->fontSize = 12.0f;
        b->icon = b == &back ? PanelButton::Icon::back : PanelButton::Icon::forward;
        b->setEnabled (false);
        addAndMakeVisible (b);
    }
    back.setTooltip ("Back to the screen you were on (Cmd+[).");
    forward.setTooltip ("Forward again (Cmd+]).");
    sounds.fontSize = 16.0f;
    sounds.key = "F5";
    sounds.setTooltip ("Your samples and Splice folder, in a drawer over any view: find a sound by kind, tempo or key, hear it, "
                       "and send it onto an MPC pad, into the Sampler or onto a loop layer (F5).");
    addAndMakeVisible (sounds);
    chords.fontSize = 16.0f;
    chords.key = "F7";
    chords.setTooltip ("The chord builder, in a drawer over any view: your own chords on 16 pads, played on the picked track "
                       "(or any instrument) from a click, your Launchkey's pads or keys; a progression on the clock, to a loop or to Logic (F7).");
    addAndMakeVisible (chords);
    settings.fontSize = 16.0f;
    settings.key = "F6";
    settings.setTooltip ("Settings: what the keys and pads play, the split, sampling one tool into another, knob Learn, the click, "
                         "audio and MIDI, the dark panel and a second screen (F6).");
    addAndMakeVisible (settings);
    help.fontSize = 17.0f;
    help.setTooltip ("How to: step-by-step guides for this screen, recording loops, building a song, saving and exporting, "
                     "singing live and plugging in a guitar.");
    addAndMakeVisible (help);
}

void ScreenBar::setCurrent (const juce::String& view, bool soundsOpen, bool settingsOpen, bool chordsOpen)
{
    auto set = [] (PanelButton& b, bool on)
    {
        const auto lit = on ? Lit::current : Lit::none;
        if (b.lit != lit)
        {
            b.lit = lit;
            b.repaint();
        }
    };
    for (int i = 0; i < tabs.size(); ++i)
        set (*tabs[i], views()[i] == view);
    set (sounds, soundsOpen);
    set (chords, chordsOpen);
    set (settings, settingsOpen);
}

void ScreenBar::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    r = r.withSizeKeepingCentre (r.getWidth(), 49);
    back.setBounds (r.removeFromLeft (60));
    r.removeFromLeft (6);
    forward.setBounds (r.removeFromLeft (60));
    r.removeFromLeft (16);
    help.setBounds (r.removeFromRight (84));
    r.removeFromRight (10);
    const int util = juce::jlimit (110, 150, r.getWidth() / 9);
    settings.setBounds (r.removeFromRight (util));
    r.removeFromRight (10);
    sounds.setBounds (r.removeFromRight (util));
    r.removeFromRight (10);
    chords.setBounds (r.removeFromRight (util));
    r.removeFromRight (28);
    int n = 0;
    for (auto* b : tabs)
        n += b->isVisible() ? 1 : 0;
    n = juce::jmax (1, n);
    const int w = juce::jmin (220, (r.getWidth() - 12 * (n - 1)) / n);
    for (auto* b : tabs)
    {
        if (! b->isVisible())
            continue;
        b->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (12);
    }
}

//==============================================================================
TrackRail::TrackRail()
{
    newPattern.fontSize = 14.0f;
    newPattern.setTooltip ("Starts the next thing in the song on an empty pattern: a beat, a bass line, keys, the vocoder, a loop "
                           "or a sample. Also keeps what's playing as the next section, or starts over with a new, empty song.");
    addAndMakeVisible (newPattern);
    for (auto* b : { &recTake, &toSong, &recMoves, &moves })
    {
        b->fontSize = 15.0f;
        b->subSize = 10.5f;
        addAndMakeVisible (b);
    }
    recTake.icon = PanelButton::Icon::rec;
    recMoves.icon = PanelButton::Icon::rec;

}

void TrackRail::setCards (const juce::Array<juce::Component*>& list)
{
    cards.clear();
    for (auto* c : list)
    {
        cards.add (c);
        addAndMakeVisible (c);
    }
    resized();
}

void TrackRail::paint (juce::Graphics&)
{
}

void TrackRail::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (40);
    newPattern.setBounds (top.withTrimmedRight (4).withHeight (36));
    r.removeFromTop (6);
    {
        auto capture = r.removeFromBottom (54).withTrimmedRight (4);
        r.removeFromBottom (6);
        const int w = (capture.getWidth() - 6) / 2;
        recTake.setBounds (capture.removeFromLeft (w));
        capture.removeFromLeft (6);
        toSong.setBounds (capture);
    }
    if (recMoves.isVisible() || moves.isVisible())
    {
        auto automation = r.removeFromBottom (54).withTrimmedRight (4);
        r.removeFromBottom (6);
        const int w = (automation.getWidth() - 6) / 2;
        recMoves.setBounds (automation.removeFromLeft (w));
        automation.removeFromLeft (6);
        moves.setBounds (automation);
    }
    const int n = cards.size();
    if (n == 0)
        return;
    const int gap = 2;
    // (No taller than this: a taller card squeezes its words, "Rea...".)
    const int h = juce::jmin (79, (r.getHeight() - gap * (n - 1)) / n);
    for (auto& c : cards)
    {
        if (c != nullptr)
            c->setBounds (r.removeFromTop (h).expanded (4, 0).withTrimmedLeft (4));
        r.removeFromTop (gap);
    }
}

//==============================================================================
Footer::Footer (RigEngine& e) : engine (e)
{
    for (int i = 0; i < 8; ++i)
    {
        auto* b = parts.add (new PanelButton (juce::String::charToString ((juce::juce_wchar) ('A' + i))));
        b->fontSize = 30.0f;
        b->subSize = 12.0f;
        b->onClick = [this, i]
        {
            if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
                showPartMenu (i);
            else
            {
                const bool has = i < sectionCount;
                pressPart (engine, i);
                if (has && onPickSection)
                    onPickSection (i);
            }
        };
        addAndMakeVisible (b);
    }
    for (auto* b : { &recButton, &changeButton, &cycleButton, &takesButton, &songButton })
    {
        b->fontSize = 13.0f;
        addChildComponent (b);
    }
    recButton.icon = PanelButton::Icon::none;
    recButton.redRing = false;
    recButton.onClick = [this] { if (onRecInto) onRecInto(); };
    changeButton.onClick = [this] { if (onChange) onChange (changeButton); };
    cycleButton.onClick = [this] { if (onCycle) onCycle(); };
    takesButton.onClick = [this] { if (onTakes) onTakes (takesButton); };
    songButton.onClick = [this] { if (onSong) onSong(); };
    recButton.setTooltip ("Records the picked track into the song, where the red box on the strip shows: from the top of that section to its end. "
                          "With Cycle on, the section plays over and over and every time round is a new take, so keep going until it's right. "
                          "Press again to stop (a take cut short is kept too).");
    changeButton.setTooltip ("Where the next take goes: another section, a new layer or one you already have. Or a free take, the old way.");
    cycleButton.setTooltip ("The section plays over and over: practise over it, and while recording every time round is a new take.");
    takesButton.setTooltip ("Every take on this layer: pick the one that plays, or delete the ones you don't want.");
    songButton.setTooltip ("The Song screen (F3), to see the whole song and add the next layer. Back (Cmd+[) brings you here again.");
    message.setJustificationType (juce::Justification::centred);
    message.setFont (barlow (14.0f));
    message.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (message);
}

void Footer::pressPart (RigEngine& engine, int i)
{
    // The keys are the song's sections: a full one plays from the next bar (with the song on);
    // an empty one keeps what's playing as the next new section.
    auto& tl = engine.getTimeline();
    const int count = (int) rigsong::sections (tl).size();
    if (juce::isPositiveAndBelow (i, count))
        engine.launchSection (i);
    else if (i >= count)
        rigsong::addSectionFromWhatsPlaying (engine, rigsong::suggestName (tl), count == 0 ? 8.0 : rigsong::sections (tl).back().bars);
}

void Footer::showPartMenu (int i)
{
    const int count = (int) rigsong::sections (engine.getTimeline()).size();
    if (! juce::isPositiveAndBelow (i, count))
        return;
    juce::PopupMenu menu;
    menu.addItem (1, juce::String::fromUTF8 ("\xe2\x96\xb6 Play from here"));
    menu.addItem (2, "Rename, lengths and what plays (Song screen)...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (parts[i]),
                        [safe = juce::Component::SafePointer<Footer> (this), i] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            if (result == 1)
                                safe->engine.launchSection (i);
                            else if (result == 2 && safe->onEditParts)
                                safe->onEditParts();
                        });
}

void Footer::tick()
{
    const auto& tl = engine.getTimeline();
    const auto list = rigsong::sections (tl);
    const int count = (int) list.size();
    if ((count == 0) != (sectionCount == 0))
    {
        sectionCount = count;
        resized();
        repaint();
        if (onSlimChanged)
            onSlimChanged();
    }
    sectionCount = count;
    {
        int hash = count;
        for (auto& sec : list)
            hash = hash * 31 + (int) (sec.start * 64.0) * 7 + (int) (sec.bars * 64.0);
        if (hash != sectionsHash)
        {
            sectionsHash = hash;
            resized();
        }
    }
    if (! lanesArea.isEmpty())
        repaint (lanesArea.expanded (2));
    auto& clock = engine.getTransport();
    const bool songOn = tl.on.load() && clock.isPlaying();
    const int bpb = juce::jmax (1, clock.getBeatsPerBar());
    const int current = songOn ? rigsong::sectionAt (tl, clock.getPositionInBeats() / bpb) : -1;
    const double pending = tl.getPendingJump();
    const int queued = songOn && pending >= 0 ? rigsong::sectionAt (tl, pending) : -1;
    const bool pulse = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;
    for (int i = 0; i < parts.size(); ++i)
    {
        auto* b = parts[i];
        const auto letter = rigsong::letterFor (i);
        const bool has = i < count;
        juce::String sub;
        Lit lit = Lit::none;
        if (i == queued && i != current)
            sub = juce::String::fromUTF8 ("Next \xc2\xb7 ") + (has ? list[(size_t) i].name : juce::String()), lit = pulse ? Lit::next : Lit::none;
        else if (i == current)
            sub = juce::String::fromUTF8 ("\xe2\x96\xb6 ") + list[(size_t) i].name, lit = Lit::play;
        else if (has)
            sub = list[(size_t) i].name;
        else
            sub = i == count ? (count == 0 ? juce::String() : juce::String ("Save")) : juce::String ("Empty");
        // No song yet: A is one plain button that says what it does. After the sections, + keeps
        // what's playing as the next one.
        const auto text = count == 0 && i == 0 ? juce::String ("+ Keep what's playing as A") : i == count ? juce::String ("+") : letter;
        if (b->text != text)
        {
            b->text = text;
            b->repaint();
        }
        const bool empty = ! has && i != count;
        if (b->sub != sub || b->lit != lit || b->empty != empty)
        {
            b->sub = sub;
            b->lit = lit;
            b->empty = empty;
            b->setTooltip (has ? "Plays section " + letter + " (" + list[(size_t) i].name + ") from the next bar, with the song on "
                                 "(straight away when stopped). Right-click for the Song screen. Key " + juce::String (i + 1) + "."
                               : "Keeps what's playing now (beats, patterns, loops) as the song's next section. Then press it to come back to it.");
            b->repaint();
        }
    }
}

void Footer::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    auto content = r.withSizeKeepingCentre (r.getWidth(), 64 + 22);
    for (auto* b : parts)
        b->setVisible (partsShown);
    if (! partsShown)
    {
        // A one-tool plugin has no song sections: the footer is just for messages.
        partsLabel = partsArea = {};
        if (onLayout)
            content = onLayout (content);
        message.setBounds (content.withTrimmedBottom (4));
        message.setJustificationType (juce::Justification::centred);
        message.setFont (barlow (15.0f));
        return;
    }

    if (isSlim())
    {
        // No song yet: just A (keep what's playing as the first section), then the messages.
        content = r.withSizeKeepingCentre (r.getWidth(), 22 + 44);
        partsLabel = content.removeFromTop (22);
        for (int i = 1; i < parts.size(); ++i)
            parts[i]->setVisible (false);
        partsArea = content.withWidth (260);
        parts[0]->fontSize = 15.0f;
        parts[0]->setBounds (partsArea.withHeight (44));
        content.removeFromLeft (partsArea.getWidth() + 16);
        message.setBounds (content.withTrimmedBottom (2));
        message.setJustificationType (juce::Justification::centred);
        message.setFont (barlow (14.0f));
        return;
    }
    // The song strip: the Rec into bar, the sections as keys as wide as their bars, and the
    // layers under them. Messages on the right when there's room.
    for (auto* b : { &recButton, &changeButton, &cycleButton, &takesButton, &songButton })
        b->setVisible (true);
    partsLabel = {};
    content = r.reduced (0, 6);
    juce::Rectangle<int> messages;
    if (content.getWidth() >= 1040)
    {
        messages = content.removeFromRight (juce::jlimit (240, 360, content.getWidth() / 4));
        content.removeFromRight (14);
    }
    recRow = content.removeFromTop (28);
    content.removeFromTop (4);
    keysRow = content.removeFromTop (juce::jlimit (32, 42, content.getHeight() * 55 / 100));
    content.removeFromTop (3);
    lanesArea = content;
    {
        auto row = recRow;
        recButton.setBounds (row.removeFromLeft (116));
        row.removeFromLeft (10);
        songButton.setBounds (row.removeFromRight (78));
        row.removeFromRight (6);
        takesButton.setBounds (row.removeFromRight (92));
        row.removeFromRight (6);
        cycleButton.setBounds (row.removeFromRight (70));
        row.removeFromRight (6);
        changeButton.setBounds (row.removeFromRight (96));
        row.removeFromRight (10);
        recText = row;
    }

    // The keys: each as wide as its section's bars (at least kMin), + after them.
    const auto list = rigsong::sections (engine.getTimeline());
    const int count = (int) list.size();
    const int gap = 4, kMin = 46, plusW = count < parts.size() ? 50 : 0;
    auto keys = keysRow.withTrimmedRight (plusW > 0 ? plusW + gap : 0);
    sectionBoxes.clear();
    sectionBars.clear();
    double total = 0;
    for (auto& sec : list)
        total += juce::jmax (0.25, sec.bars);
    const int usable = keys.getWidth() - gap * juce::jmax (0, count - 1);
    // Widths by bars, with the short ones held at kMin and the rest sharing what's left.
    std::vector<int> widths ((size_t) count, kMin);
    if (count > 0)
    {
        std::vector<bool> fixed ((size_t) count, false);
        for (int pass = 0; pass < 4; ++pass)
        {
            double free = usable, bars = 0;
            for (int i = 0; i < count; ++i)
                if (fixed[(size_t) i])
                    free -= kMin;
                else
                    bars += juce::jmax (0.25, list[(size_t) i].bars);
            bool changed = false;
            for (int i = 0; i < count; ++i)
                if (! fixed[(size_t) i])
                {
                    const int w = bars > 0 ? (int) (free * juce::jmax (0.25, list[(size_t) i].bars) / bars) : kMin;
                    if (w < kMin)
                    {
                        fixed[(size_t) i] = true;
                        changed = true;
                    }
                    widths[(size_t) i] = juce::jmax (kMin, w);
                }
            if (! changed)
                break;
        }
    }
    int x = keys.getX();
    for (int i = 0; i < count; ++i)
    {
        const int w = i == count - 1 ? juce::jmax (kMin, keys.getRight() - x) : widths[(size_t) i];
        sectionBoxes.push_back ({ x, keys.getY(), w, keys.getHeight() });
        sectionBars.push_back ({ list[(size_t) i].start, list[(size_t) i].bars });
        x += w + gap;
    }
    partsArea = keysRow;
    for (int i = 0; i < parts.size(); ++i)
    {
        auto* b = parts[i];
        b->fontSize = 17.0f;
        b->subSize = 11.0f;
        if (i < count)
            b->setBounds (sectionBoxes[(size_t) i]);
        else if (i == count)
            b->setBounds (keysRow.removeFromRight (plusW));
        b->setVisible (i <= count);
    }
    if (! messages.isEmpty())
    {
        message.setBounds (messages);
        message.setJustificationType (juce::Justification::centred);
        message.setFont (barlow (14.0f));
    }
    else
    {
        message.setBounds (recText);
        message.setJustificationType (juce::Justification::centredLeft);
        message.setFont (barlow (13.0f));
    }
    juce::ignoreUnused (total);
}

float Footer::xForBar (double bar) const
{
    if (sectionBoxes.empty())
        return (float) lanesArea.getX();
    for (size_t i = 0; i < sectionBoxes.size(); ++i)
    {
        const auto [start, bars] = sectionBars[i];
        const auto& box = sectionBoxes[i];
        if (bar < start + bars || i + 1 == sectionBoxes.size())
        {
            const double t = juce::jlimit (0.0, 1.0, (bar - start) / juce::jmax (1.0e-6, bars));
            return (float) (box.getX() + t * box.getWidth());
        }
    }
    return (float) sectionBoxes.back().getRight();
}

int Footer::sectionAtX (int x) const
{
    for (size_t i = 0; i < sectionBoxes.size(); ++i)
        if (x >= sectionBoxes[i].getX() - 2 && x < sectionBoxes[i].getRight() + 2)
            return (int) i;
    return -1;
}

bool Footer::RecInto::operator== (const RecInto& o) const
{
    return text == o.text && section == o.section && layer == o.layer && colourStrip == o.colourStrip && waiting == o.waiting
           && recording == o.recording && cycle == o.cycle && std::abs (progress - o.progress) < 0.002 && takes == o.takes;
}

void Footer::setRecInto (const RecInto& r)
{
    if (r == recInto)
        return;
    recInto = r;
    const auto lit = r.recording || r.waiting ? Lit::arm : Lit::none;
    const juce::String text = r.recording || r.waiting ? "Stop take" : "Rec into";
    if (recButton.text != text || recButton.lit != lit)
    {
        recButton.text = text;
        recButton.lit = lit;
        recButton.repaint();
    }
    const auto cycleLit = r.cycle ? Lit::current : Lit::none;
    if (cycleButton.lit != cycleLit)
    {
        cycleButton.lit = cycleLit;
        cycleButton.repaint();
    }
    const auto takesText = r.takes > 0 ? "Takes " + juce::String (r.takes) + juce::String::fromUTF8 (" \xe2\x96\xbe")
                                       : juce::String::fromUTF8 ("Takes \xe2\x96\xbe");
    if (takesButton.text != takesText)
    {
        takesButton.text = takesText;
        takesButton.repaint();
    }
    takesButton.setEnabled (r.takes > 0);
    repaint (recText);
    repaint (lanesArea.expanded (4));
}

void Footer::mouseUp (const juce::MouseEvent& e)
{
    if (! lanesArea.contains (e.getPosition()) || e.mods.isPopupMenu())
        return;
    if (const int i = sectionAtX (e.x); i >= 0 && onPickSection)
        onPickSection (i);
}

void Footer::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (lanesArea.contains (e.getPosition()) && onEditParts)
        onEditParts();
}

void Footer::paintStrip (juce::Graphics& g)
{
    const auto& p = pal();
    auto& tl = engine.getTimeline();
    auto& looper = engine.getLooper();

    // Rec into: where the next take goes.
    {
        auto t = recText;
        const bool live = recInto.recording || recInto.waiting;
        if (message.getText().isEmpty() || ! message.getBounds().intersects (recText))
        {
            g.setColour (live ? colours::arm : p.ink2);
            g.setFont (cond (14.0f, Weight::bold, 0.03f));
            g.drawFittedText (recInto.text.isEmpty() ? juce::String ("Rec into: pick a section on the strip") : recInto.text,
                              t, juce::Justification::centredLeft, 1, 0.75f);
        }
    }

    if (lanesArea.isEmpty())
        return;
    // The well the lanes sit in.
    auto well = lanesArea.toFloat();
    g.setColour (p.well);
    g.fillRoundedRectangle (well, 5.0f);

    // The lanes used by the song (and the one being recorded onto).
    std::vector<int> lanes;
    const int targetLane = RigTakes::isNotesLayer (recInto.layer) ? RigTimeline::laneForStrip (RigTakes::stripOfNotesLayer (recInto.layer))
                         : recInto.layer >= 0 ? RigTimeline::laneForLoop (recInto.layer) : -1;
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
        if (tl.laneUsed (lane) || lane == targetLane)
            lanes.push_back (lane);
    const auto inner = lanesArea.reduced (0, 3);
    const int n = juce::jmax (1, (int) lanes.size());
    const float laneH = juce::jlimit (2.0f, 9.0f, (inner.getHeight() - (n - 1)) / (float) n);
    const auto colourOf = [&] (int lane) -> juce::Colour
    {
        const auto& info = RigTimeline::lanes()[(size_t) lane];
        if (! info.isLoop)
            return RigColours::forStrip (info.index);
        if (lane == targetLane && recInto.colourStrip >= 0)
            return RigColours::forStrip (recInto.colourStrip);
        const int strip = RigEngine::stripForSource (looper.getLayer (info.index).source.load());
        return strip >= 0 ? RigColours::forStrip (strip) : RigColours::loops;
    };
    // (A few lanes sit in the middle of the well.)
    float y = (float) inner.getY() + juce::jmax (0.0f, (inner.getHeight() - n * (laneH + 1.0f)) * 0.5f);
    float targetY = -1;
    for (int lane : lanes)
    {
        const auto c = colourOf (lane);
        for (auto& b : tl.getBlocks())
            if (b.lane == lane)
            {
                const float x0 = xForBar (b.start), x1 = xForBar (b.end());
                g.setColour (c.withAlpha (0.9f));
                g.fillRoundedRectangle ({ x0, y, juce::jmax (2.0f, x1 - x0 - 1.0f), laneH }, juce::jmin (2.0f, laneH * 0.5f));
            }
        if (lane == targetLane)
            targetY = y;
        y += laneH + 1.0f;
    }

    // The red box: where the next take goes.
    if (juce::isPositiveAndBelow (recInto.section, (int) sectionBoxes.size()))
    {
        const auto box = sectionBoxes[(size_t) recInto.section];
        const float bandY = targetY >= 0 ? targetY - 1.0f : (float) inner.getY();
        const float bandH = targetY >= 0 ? laneH + 2.0f : (float) inner.getHeight();
        juce::Rectangle<float> r ((float) box.getX(), bandY, (float) box.getWidth(), bandH);
        if (recInto.recording)
        {
            g.setColour (colours::arm.withAlpha (0.35f));
            g.fillRect (r.withWidth ((float) (r.getWidth() * recInto.progress)));
        }
        g.setColour (colours::arm.withAlpha (recInto.recording || recInto.waiting ? 1.0f : 0.8f));
        g.drawRoundedRectangle (r.expanded (1.0f, 1.0f), 2.0f, 1.6f);
        // The section's whole height, faintly, so it shows even with many lanes.
        g.setColour (colours::arm.withAlpha (0.18f));
        g.drawRect (juce::Rectangle<float> ((float) box.getX(), (float) lanesArea.getY(), (float) box.getWidth(), (float) lanesArea.getHeight()), 1.0f);
    }

    // The play head.
    auto& clock = engine.getTransport();
    if (clock.isPlaying() && ! sectionBoxes.empty())
    {
        const double bar = clock.getPositionInBeats() / juce::jmax (1, clock.getBeatsPerBar());
        const auto last = sectionBars.back();
        if (bar >= sectionBars.front().first && bar <= last.first + last.second)
        {
            const float x = xForBar (bar);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (x - 1.0f, (float) lanesArea.getY(), 2.0f, (float) lanesArea.getHeight()));
        }
    }
}

void Footer::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.setColour (juce::Colours::black.withAlpha (0.05f));
    g.fillRect (getLocalBounds());
    g.setColour (p.line);
    g.fillRect (getLocalBounds().removeFromTop (2));
    message.setColour (juce::Label::textColourId, p.ink2);
    if (partsShown && isSlim())
        engraved (g, juce::String ("Song sections: none yet (optional)"), partsLabel.withWidth (500));
    else if (partsShown)
        paintStrip (g);
}
}
