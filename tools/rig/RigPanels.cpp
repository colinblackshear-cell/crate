#include "RigPanels.h"
#include "RigSong.h"
#include "RigColours.h"
#include "core/ui/ToolIcons.h"

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
        else if (icon == Icon::menu)
        {
            // Three lines, as on the phone.
            for (int k = 0; k < 3; ++k)
                path.addRoundedRectangle (ic.getX() + s * 0.12f, ic.getY() + s * (0.2f + 0.27f * (float) k), s * 0.76f, s * 0.11f, s * 0.05f);
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
    return partLetter == o.partLetter && partName == o.partName && barBeat == o.barBeat && partQueued == o.partQueued && target == o.target
        && countIn == o.countIn && std::abs (vuLeft - o.vuLeft) < 0.004f && std::abs (vuRight - o.vuRight) < 0.004f
        && keyboard == o.keyboard && keyboardFound == o.keyboardFound && keyboardActive == o.keyboardActive && macKeys == o.macKeys
        && micOn == o.micOn && recording == o.recording && songOn == o.songOn;
}

Header::Header (bool isStandalone) : standalone (isStandalone)
{
    menu.icon = PanelButton::Icon::menu;
    menu.text = {};
    menu.fontSize = 14.0f;
    menu.setTooltip ("Your song and everything else: New song, Open, Save, Settings (F6), the Guide, New pattern.");
    addAndMakeVisible (menu);
    jam.fontSize = 12.0f;
    jam.setTooltip ("The song is playing and decides which tracks play, so a track you switch on can stay quiet. "
                    "Jam hands control back to you: everything you switch on plays, and the song waits on the Song screen.");
    addChildComponent (jam);
    show.fontSize = 17.0f;
    show.subSize = 11.0f;
    show.sub = juce::String::fromUTF8 ("Not saved yet \xc2\xb7 menu");
    show.setTooltip ("Your show: New song, Open, Save (Cmd+S), Save as, Export for Logic, and the Guide.");
    addAndMakeVisible (show);
}

void Header::update (const Shown& s)
{
    if (s == shown)
        return;
    shown = s;
    jam.setVisible (s.songOn && s.recording.isEmpty() && s.countIn.isEmpty());
    repaint();
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    const int h = juce::jmin (60, getHeight() - 16);
    auto centreY = [&] (juce::Rectangle<int> b, int height) { return b.withSizeKeepingCentre (b.getWidth(), height); };

    menu.setBounds (centreY (r.removeFromLeft (h), h));
    r.removeFromLeft (14);
    lampsArea = centreY (r.removeFromRight (128), 72);
    r.removeFromRight (12);
    // Room for everything? Drop the VU meters, then the wordmark, on a small screen.
    constexpr int showWidth = 220, lcdMin = 430, wordmark = 168 + 16, vu = 16 + 2 * 96;
    showWordmark = r.getWidth() >= wordmark + showWidth + 12 + lcdMin;
    showVu = r.getWidth() >= wordmark + showWidth + 12 + lcdMin + vu;
    if (showWordmark)
    {
        wordmarkArea = r.removeFromLeft (168);
        r.removeFromLeft (16);
    }
    else
        wordmarkArea = {};
    // Your song, then the LCD.
    show.setBounds (centreY (r.removeFromLeft (showWidth), h));
    r.removeFromLeft (12);
    vuArea = {};
    if (showVu)
    {
        vuArea = centreY (r.removeFromRight (vu - 16), h);
        r.removeFromRight (16);
    }
    partArea = centreY (r.removeFromLeft (juce::jmin (r.getWidth(), 640)), h);
    jam.setBounds (partArea.getRight() - 62, partArea.getBottom() - 30, 54, 24);
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

    // The LCD: the section, where the song is, and where the next take goes.
    {
        auto r = partArea.toFloat();
        drawLcd (g, r);
        auto inner = r.reduced (14.0f, 3.0f);
        if (shown.partLetter.isNotEmpty())
        {
            lcdText (g, shown.partLetter, inner.removeFromLeft (34.0f), 52.0f, juce::Justification::centred, colours::lcdGreen);
            inner.removeFromLeft (10.0f);
        }
        auto top = inner.removeFromTop (inner.getHeight() * 0.6f);
        const auto where = shown.countIn.isNotEmpty() ? shown.countIn : shown.barBeat;
        const auto whereFont = lcd (32.0f);
        const float whereWidth = juce::GlyphArrangement::getStringWidth (whereFont, where) + 20.0f;
        lcdText (g, where, top.removeFromLeft (juce::jmin (whereWidth, top.getWidth())), 32.0f, juce::Justification::bottomLeft,
                 shown.countIn.isNotEmpty() ? colours::arm.brighter (0.4f) : colours::lcdAmber);
        if (shown.target.isNotEmpty())
            lcdText (g, juce::String::fromUTF8 ("\xe2\x80\xba ") + shown.target, top, 26.0f, juce::Justification::bottomLeft,
                     colours::lcdAmber.withAlpha (0.55f));
        if (shown.recording.isNotEmpty())
        {
            // Recording: a red line under it, and a red edge.
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
    if (e.mouseWasClicked() && partArea.contains (e.getPosition()) && shown.recording.isNotEmpty() && onRecording)
        return onRecording();
    if (standalone && e.mouseWasClicked() && micLampArea.contains (e.getPosition()) && onMic)
        return onMic();
    if (standalone && e.mouseWasClicked() && keyboardLampArea.contains (e.getPosition()) && onKeyboard)
        onKeyboard();
}

//==============================================================================
void Dock::TempoBox::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    drawLcd (g, r);
    lcdText (g, juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1), r.withTrimmedBottom (r.getHeight() * 0.36f).withTrimmedTop (2),
             juce::jmin (40.0f, r.getHeight() * 0.62f));
    lcdText (g, rampTo > 0 ? juce::String::fromUTF8 ("\xe2\x86\x92 ") + juce::String (rampTo, 0) + " BPM" : juce::String::fromUTF8 ("BPM \xc2\xb7 TAP"),
             r.withTrimmedTop (r.getHeight() * 0.64f).withTrimmedBottom (3), 15.0f, juce::Justification::centred,
             rampTo > 0 ? colours::lcdGreen : colours::lcdAmber.withAlpha (0.75f));
}

void Dock::TempoBox::mouseDown (const juce::MouseEvent& e)
{
    dragStartY = e.getPosition().y;
    dragSteps = 0;
    dragged = false;
}

void Dock::TempoBox::mouseDrag (const juce::MouseEvent& e)
{
    // Up for faster, down for slower: a BPM for every 6 pixels (a tenth with Shift).
    const int steps = (dragStartY - e.getPosition().y) / 6;
    if (std::abs (dragStartY - e.getPosition().y) > 3)
        dragged = true;
    if (steps != dragSteps && onNudge)
        onNudge ((steps - dragSteps) * (e.mods.isShiftDown() ? 0.1 : 1.0));
    dragSteps = steps;
}

void Dock::TempoBox::mouseUp (const juce::MouseEvent& e)
{
    if (dragged || ! e.mouseWasClicked())
        return;
    if (e.mods.isPopupMenu())
    {
        if (onMenu)
            onMenu();
    }
    else if (onTap)
        onTap();
}

void Dock::TempoBox::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (onNudge && w.deltaY != 0.0f)
        onNudge ((w.deltaY > 0 ? 1.0 : -1.0) * (e.mods.isShiftDown() ? 0.1 : 1.0));
}

juce::Rectangle<int> Dock::RecButton::arrowArea() const
{
    return getLocalBounds().removeFromRight (juce::jmin (44, getWidth() / 4));
}

void Dock::RecButton::mouseDown (const juce::MouseEvent& e)
{
    arrowHit = arrowArea().contains (e.getPosition());
    juce::Button::mouseDown (e);
}

void Dock::RecButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().withTrimmedBottom (5.0f);
    const float radius = r.getHeight() * 0.5f;
    const bool on = state != 0;
    // A red pill (dark while it records, so the red dot inside says it), sitting on its edge.
    const auto face = on && state == 2 ? juce::Colour (0xff2a231d) : colours::arm;
    const auto edge = on && state == 2 ? juce::Colour (0xff0d0a08) : juce::Colour (0xff8d2a1c);
    if (state == 1)
        glow (g, r, radius, colours::arm, 12.0f, 0.6f);
    g.setColour (edge);
    g.fillRoundedRectangle (r.translated (0, 5.0f), radius);
    auto top = r.translated (0, down ? 3.0f : 0.0f);
    g.setColour (over ? face.brighter (0.08f) : face);
    g.fillRoundedRectangle (top, radius);
    if (state == 1)
    {
        g.setColour (juce::Colours::white.withAlpha ((juce::Time::getMillisecondCounter() / 300) % 2 == 0 ? 0.9f : 0.35f));
        g.drawRoundedRectangle (top.reduced (2.0f), radius - 2.0f, 2.5f);
    }

    auto arrow = arrowArea().toFloat().withY (top.getY()).withHeight (top.getHeight());
    auto main = top.withRight (arrow.getX());
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.fillRect (arrow.getX(), top.getCentreY() - top.getHeight() * 0.28f, 1.5f, top.getHeight() * 0.56f);
    {
        juce::Path tri;
        const auto c = arrow.getCentre().translated (-3.0f, 0.0f);
        tri.addTriangle (c.x - 6.0f, c.y - 3.0f, c.x + 6.0f, c.y - 3.0f, c.x, c.y + 4.0f);
        g.setColour (juce::Colours::white);
        g.fillPath (tri);
    }

    // The dot (a square while it records), the word, and where it goes.
    const float d = juce::jmin (26.0f, top.getHeight() * 0.38f);
    auto dot = juce::Rectangle<float> (d, d).withCentre ({ main.getX() + radius + 4.0f, main.getCentreY() });
    g.setColour (state == 2 ? colours::arm : juce::Colours::white);
    if (state == 2)
        g.fillRoundedRectangle (dot.reduced (2.0f), 3.0f);
    else
        g.fillEllipse (dot);
    auto words = main.withTrimmedLeft (dot.getRight() - main.getX() + 10.0f).withTrimmedRight (6.0f).toNearestInt();
    g.setColour (juce::Colours::white);
    g.setFont (cond (juce::jmin (28.0f, top.getHeight() * 0.42f), Weight::extra, 0.06f));
    auto upper = words.removeFromTop (words.getHeight() * 3 / 5);
    g.drawFittedText (text.toUpperCase(), upper, juce::Justification::bottomLeft, 1, 0.7f);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (cond (12.0f, Weight::bold, 0.1f));
    g.drawFittedText (sub.toUpperCase(), words, juce::Justification::topLeft, 1, 0.7f);
}

Dock::Dock()
{
    tempo.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    tempo.setTooltip ("The tempo. Click a few times in time to tap it; drag up or down (or scroll) to change it, with Shift for a tenth; "
                      "right-click for Ramp to... (a glide over some bars).");
    addAndMakeVisible (tempo);
    for (auto* b : { &up, &down })
    {
        b->fontSize = 18.0f;
        b->setRepeatSpeed (400, 80);
        addAndMakeVisible (b);
    }
    up.setTooltip ("Tempo up (hold Shift for a tenth).");
    down.setTooltip ("Tempo down (hold Shift for a tenth).");
    for (auto* b : { &click, &countIn, &length, &undo, &recMoves, &moves, &quantize })
    {
        b->fontSize = 17.0f;
        b->subSize = 10.5f;
        addAndMakeVisible (b);
    }
    click.setTooltip ("The click (metronome), on every beat while it plays. The count-in clicks either way.");
    countIn.setTooltip ("Count-in: from stop, Rec counts this many bars with the click before it records. While it plays, "
                        "it starts at the next bar (2 bars: a bar after that).");
    length.setTooltip ("How long the next take is: it stops by itself after these bars and loops. Free: until you press Rec again.");
    rec.setTooltip ("Rec: records the picked tool onto the next empty loop layer, in time, after the count-in. Press again to stop "
                    "(or it stops at its length). The arrow: where Rec lands (a layer, a take on the song, the Sampler, the whole set) "
                    "and the rest of the record options.");
    addAndMakeVisible (rec);
    play.icon = PanelButton::Icon::play;
    play.fontSize = 13.0f;
    play.setTooltip ("Starts every tool together (space bar). Pressed while it plays: stops, goes back to the start, and silences "
                     "any notes still sounding.");
    addAndMakeVisible (play);
    undo.text = "Undo";
    undo.sub = "last take";
    undo.setTooltip ("Takes back the last take: clears the loop it recorded (or calls off one that hasn't started). With no take to undo, "
                     "the last Clear or overdub (Cmd+Z).");
    recMoves.icon = PanelButton::Icon::rec;
    recMoves.setTooltip ("Records the knobs you turn while it plays, from the next bar: the bass's Cutoff sweeping, an effect's Mix, "
                         "any setting on any tool or its effects (on screen or with a learned knob). Press again to stop at the end of the bar. "
                         "Each setting gets a lane that plays back on the beat.");
    moves.setTooltip ("The automation lanes: what each one moves, Loop or Once, its length; draw on a lane to change it, "
                      "clear one or all (with Undo). Cmd+Shift+A.");
    quantize.sub = "on the grid";
    quantize.setTooltip ("Quantize: pulls what you've played onto the beat (the MPC, drum machine, Bass, Synth or loops), "
                         "on the grid you pick. Cmd+U quantizes what's on screen straight away.");
}

void Dock::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.setColour (p.line);
    g.fillRect (getLocalBounds().removeFromTop (2));
    if (moves.isVisible())
    {
        // A line between the take's buttons and the rest.
        const int x = undo.getRight() + 9;
        g.fillRect (x, 18, 2, getHeight() - 32);
    }
}

void Dock::resized()
{
    auto r = getLocalBounds().reduced (22, 0).withTrimmedTop (2);
    const int h = juce::jmin (62, r.getHeight() - 20);
    auto centreY = [&] (juce::Rectangle<int> b, int height) { return b.withSizeKeepingCentre (b.getWidth(), height); };
    // Narrow windows drop Automation, then shrink the boxes.
    const bool roomy = r.getWidth() >= 1180;
    const int box = roomy ? 104 : 88;
    tempo.setBounds (centreY (r.removeFromLeft (roomy ? 118 : 100), h));
    r.removeFromLeft (6);
    {
        auto col = centreY (r.removeFromLeft (38), h);
        up.setBounds (col.removeFromTop (h / 2 - 1));
        col.removeFromTop (2);
        down.setBounds (col);
    }
    r.removeFromLeft (10);
    click.setBounds (centreY (r.removeFromLeft (box - 10), h));
    for (auto* b : { &countIn, &length })
        if (b->isVisible())
        {
            r.removeFromLeft (8);
            b->setBounds (centreY (r.removeFromLeft (box), h));
        }
    r.removeFromLeft (16);
    rec.setBounds (centreY (r.removeFromLeft (roomy ? 260 : 220), h + 12));
    r.removeFromLeft (16);
    play.setBounds (centreY (r.removeFromLeft (box - 14), h));
    r.removeFromLeft (8);
    undo.setBounds (centreY (r.removeFromLeft (box), h));
    r.removeFromLeft (20);
    moves.setVisible (roomy && recMoves.isVisible());
    recMoves.setBounds (centreY (r.removeFromLeft (box + 8), h));
    r.removeFromLeft (8);
    if (moves.isVisible())
    {
        moves.setBounds (centreY (r.removeFromLeft (box + 16), h));
        r.removeFromLeft (8);
    }
    quantize.setBounds (centreY (r.removeFromLeft (juce::jmin (box, r.getWidth())), h));
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
    addChildComponent (chords);   // (Chords is a tool on the rail now)
    settings.fontSize = 16.0f;
    settings.key = "F6";
    settings.setTooltip ("Settings: what the keys and pads play, the split, sampling one tool into another, knob Learn, the click, "
                         "audio and MIDI, the dark panel and a second screen (F6).");
    addChildComponent (settings);   // (in the menu at the top left, and F6)
    help.fontSize = 17.0f;
    help.setTooltip ("How to: step-by-step guides for this screen, recording loops, building a song, saving and exporting, "
                     "singing live and plugging in a guitar.");
    addAndMakeVisible (help);   // (the Guide stays at the top, always)
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
    const int util = juce::jlimit (110, 150, r.getWidth() / 9);
    // (Settings, the Guide and Chords live elsewhere now, but keep a place for when they're shown.)
    for (auto* b : { &help, &settings, &chords })
        if (b->isVisible())
        {
            b->setBounds (r.removeFromRight (b == &help ? 84 : util));
            r.removeFromRight (10);
        }
    sounds.setBounds (r.removeFromRight (util));
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
TrackRail::ToolButton::ToolButton (const juce::String& name, int i, juce::Colour c) : PanelButton (name), icon (i), colour (c) {}

void TrackRail::ToolButton::paintButton (juce::Graphics& g, bool over, bool)
{
    // Drawn like the instruments' cards: a well with the tool's colour down its left edge.
    const auto& p = pal();
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    if (selected)
    {
        glow (g, r, 12.0f, colours::selected, 10.0f, 0.5f);
        g.setColour (colours::selected);
        g.fillRoundedRectangle (r.expanded (3.0f), 14.0f);
    }
    drawWell (g, r);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r, 12.0f);
        g.reduceClipRegion (clip);
        g.setColour (colour);
        g.fillRect (r.withWidth (10.0f));
        if (over)
        {
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRect (r);
        }
    }
    auto area = r.toNearestInt().withTrimmedLeft (18).withTrimmedRight (10);
    const int iconSize = juce::jmin (34, area.getHeight() - 8);
    crate::toolicons::draw (g, (crate::toolicons::Tool) icon, area.removeFromLeft (iconSize).withSizeKeepingCentre (iconSize, iconSize).toFloat().reduced (3.0f),
                            colour);
    area.removeFromLeft (8);
    const bool compact = getHeight() < 56;
    g.setColour (p.wellInk);
    if (compact)
    {
        g.setFont (cond (juce::jmin (18.0f, area.getHeight() * 0.55f), Weight::extra, 0.04f));
        g.drawFittedText (text.toUpperCase(), area, juce::Justification::centredLeft, 1, 0.75f);
        return;
    }
    auto top = area.removeFromTop (area.getHeight() / 2);
    g.setFont (cond (juce::jmin (26.0f, top.getHeight() * 0.95f), Weight::extra, 0.04f));
    g.drawText (text.toUpperCase(), top, juce::Justification::bottomLeft, false);
    drawLamp (g, { (float) area.getX() + 7.0f, (float) area.getY() + 11.0f }, selected ? Lit::play : Lit::none, 11.0f);
    engraved (g, status, area.withTrimmedLeft (18).withHeight (22), juce::Justification::centredLeft, 12.0f, true);
}

TrackRail::TrackRail()
    : chords ("Chords", (int) crate::toolicons::Tool::chords, juce::Colour (0xffb9a3dc)),
      effects ("Effects", (int) crate::toolicons::Tool::effects, juce::Colour (0xffe8c27a))
{
    chords.setTooltip ("Chords: your own chords on 16 pads, played on the picked track (or any instrument), with the song's key; "
                       "a progression on the clock, to a loop or to Logic (F7).");
    effects.setTooltip ("Effects: the pedals of any tool, loop layer or the master, as a pedalboard.");
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
    juce::Array<juce::Component*> shown;
    for (auto& c : cards)
        if (c != nullptr && c->isVisible())
            shown.add (c);
    const int n = shown.size();
    if (n == 0)
        return;
    const int gap = 2;
    // (No taller than this: a taller card squeezes its words, "Rea...".)
    const int h = juce::jmin (79, (r.getHeight() - gap * (n - 1)) / n);
    for (auto* c : shown)
    {
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
