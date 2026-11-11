#include "RigPanels.h"
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
        if (icon == Icon::play)
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
    return std::abs (bpm - o.bpm) < 0.01 && partLetter == o.partLetter && partName == o.partName && barBeat == o.barBeat
        && partQueued == o.partQueued && std::abs (vuLeft - o.vuLeft) < 0.004f && std::abs (vuRight - o.vuRight) < 0.004f
        && keyboard == o.keyboard && keyboardFound == o.keyboardFound && keyboardActive == o.keyboardActive && macKeys == o.macKeys
        && micOn == o.micOn;
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
    tap.fontSize = 18.0f;
    addAndMakeVisible (tap);
    play.setTooltip ("Starts and stops every tool together (space bar).");
    stop.setTooltip ("Stops, goes back to the start, and silences any notes still sounding.");
    rec.setTooltip ("Records the set for Logic, from the next bar: every tool as its own WAV, the mix, and what you play as MIDI. "
                    "Press again to stop. Saved in Music > Crate > Rig > Recordings.");
    up.setTooltip ("Tempo up (hold Shift for a tenth).");
    down.setTooltip ("Tempo down (hold Shift for a tenth).");
    tap.setTooltip ("Tap a few times in time to set the tempo.");
}

void Header::update (const Shown& s)
{
    if (s == shown)
        return;
    shown = s;
    repaint();
}

void Header::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    const int h = 66;
    // Room for everything? Drop the VU meters first, then the wordmark, on a small screen.
    const int needed = 168 + 16 + 260 + 16 + 128 + 8 + 46 + 8 + 58 + 16 + 176 + 16 + 240 + 16 + 118;
    showVu = r.getWidth() >= needed;
    showWordmark = r.getWidth() >= needed - 256 || showVu;

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
    tap.setBounds (centreY (r.removeFromLeft (58), h));
    r.removeFromLeft (16);
    partArea = centreY (r.removeFromLeft (176), h);
    r.removeFromLeft (16);

    lampsArea = centreY (r.removeFromRight (118), 72);
    r.removeFromRight (16);
    vuArea = showVu ? centreY (r.removeFromRight (240), 68) : juce::Rectangle<int>();
    micLampArea = lampsArea.withTrimmedTop (48);
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
        g.setFont (wordmark (36.0f));
        g.drawText ("Crate Rig", w.removeFromTop (40), juce::Justification::bottomLeft, false);
        engraved (g, juce::String::fromUTF8 ("Model CR-1 \xc2\xb7 Groove Station"), w.withTrimmedTop (2), juce::Justification::topLeft, 11.0f);
    }

    // Tempo.
    {
        auto r = tempoArea.toFloat();
        drawLcd (g, r);
        lcdText (g, juce::String (shown.bpm, 1), r.withTrimmedBottom (20).withTrimmedTop (4), 50.0f);
        lcdText (g, "BPM", r.withTrimmedTop (r.getHeight() - 22).withTrimmedBottom (4), 16.0f);
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
        lcdText (g, shown.partName.toUpperCase(), inner, 15.0f, juce::Justification::topLeft,
                 shown.partQueued ? colours::next : colours::lcdAmber.withAlpha (0.85f));
    }

    if (! vuArea.isEmpty())
    {
        auto r = vuArea.toFloat();
        drawVu (g, r.removeFromLeft (116.0f), shown.vuLeft, juce::String::fromUTF8 ("VU \xc2\xb7 L"));
        r.removeFromLeft (8.0f);
        drawVu (g, r.removeFromLeft (116.0f), shown.vuRight, juce::String::fromUTF8 ("VU \xc2\xb7 R"));
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
    if (standalone && e.mouseWasClicked() && micLampArea.contains (e.getPosition()) && onMic)
        onMic();
}

//==============================================================================
const juce::StringArray& ScreenBar::screens()
{
    static const juce::StringArray s { "Groove", "Vocoder", "Looper", "Sampler", "Stage" };
    return s;
}

juce::String ScreenBar::label (int i)
{
    return i == 3 ? juce::String::fromUTF8 ("Sampler \xc2\xb7 MPC") : screens()[i];
}

ScreenBar::ScreenBar()
{
    for (int i = 0; i < screens().size(); ++i)
    {
        auto* b = tabs.add (new PanelButton (label (i)));
        b->key = "F" + juce::String (i + 1);
        b->fontSize = 21.0f;
        b->setTooltip ("Shows the " + label (i) + " screen (F" + juce::String (i + 1) + ").");
        b->onClick = [this, i] { if (onScreen) onScreen (screens()[i]); };
        addAndMakeVisible (b);
    }
    more.fontSize = 18.0f;
    more.setTooltip ("Everything else: the MPC, Bass and Synth in full, the song list, the sound bank, the mixer and keyboard "
                     "set-up, saving and opening shows, the dark Stage panel.");
    addAndMakeVisible (more);
    help.fontSize = 20.0f;
    help.setTooltip ("How to: step-by-step guides for this screen, recording loops, building a song, saving and exporting, "
                     "singing live and plugging in a guitar.");
    addAndMakeVisible (help);
}

void ScreenBar::setCurrent (const juce::String& page)
{
    for (int i = 0; i < tabs.size(); ++i)
    {
        const auto lit = screens()[i] == page ? Lit::current : Lit::none;
        if (tabs[i]->lit != lit)
        {
            tabs[i]->lit = lit;
            tabs[i]->repaint();
        }
    }
    const auto moreLit = screens().contains (page) || page == "Guide" ? Lit::none : Lit::current;
    if (more.lit != moreLit)
    {
        more.lit = moreLit;
        more.repaint();
    }
}

void ScreenBar::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    r = r.withSizeKeepingCentre (r.getWidth(), 49);
    help.setBounds (r.removeFromRight (56));
    r.removeFromRight (12);
    more.setBounds (r.removeFromRight (juce::jmin (120, r.getWidth() / 8)));
    r.removeFromRight (12);
    const int n = tabs.size();
    const int w = (r.getWidth() - 12 * (n - 1)) / n;
    for (auto* b : tabs)
    {
        b->setBounds (r.removeFromLeft (w));
        r.removeFromLeft (12);
    }
}

//==============================================================================
int Footer::chipStrip (int chip)
{
    const int strips[] = { RigEngine::mpcStrip, RigEngine::drumsStrip, RigEngine::bassStrip, RigEngine::synthStrip, RigEngine::vocoderStrip, RigEngine::loopsStrip };
    return strips[juce::jlimit (0, kChips - 1, chip)];
}

juce::String Footer::chipName (int chip)
{
    const char* names[] = { "Drums", "Machine", "Bass", "Synth", "Vocoder", "Loops" };
    return names[juce::jlimit (0, kChips - 1, chip)];
}

Footer::Footer (RigEngine& e) : engine (e)
{
    for (int i = 0; i < 8; ++i)
    {
        auto* b = parts.add (new PanelButton (juce::String::charToString ((juce::juce_wchar) ('A' + i))));
        b->fontSize = 38.0f;
        b->subSize = 12.0f;
        b->onClick = [this, i]
        {
            if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
                showPartMenu (i);
            else
                pressPart (engine, i);
        };
        addAndMakeVisible (b);
    }
    message.setJustificationType (juce::Justification::centred);
    message.setFont (barlow (14.0f));
    message.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (message);
}

void Footer::pressPart (RigEngine& engine, int i)
{
    auto& scenes = engine.getScenes();
    auto& list = scenes.getScenes();
    if (juce::isPositiveAndBelow (i, (int) list.size()))
        scenes.launch (i);
    else if (i >= 0 && (int) list.size() < RigScenes::kMaxScenes)
    {
        // An empty part saves what's playing now, in the next free part.
        RigScene scene;
        scene.name = "Part " + juce::String::charToString ((juce::juce_wchar) ('A' + (int) list.size()));
        scene.state = engine.captureScene();
        list.push_back (scene);
    }
}

void Footer::showPartMenu (int i)
{
    auto& list = engine.getScenes().getScenes();
    if (! juce::isPositiveAndBelow (i, (int) list.size()))
        return;
    juce::PopupMenu menu;
    menu.addItem (1, "Save what's playing now into this part");
    menu.addItem (2, "Rename, reorder and set lengths (Song page)...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (parts[i]),
                        [safe = juce::Component::SafePointer<Footer> (this), i] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            auto& scenes = safe->engine.getScenes().getScenes();
                            if (result == 1 && juce::isPositiveAndBelow (i, (int) scenes.size()))
                                scenes[(size_t) i].state = safe->engine.captureScene();
                            else if (result == 2 && safe->onEditParts)
                                safe->onEditParts();
                        });
}

void Footer::tick (const std::function<float (int)>& meterOf)
{
    auto& scenes = engine.getScenes();
    const auto& list = scenes.getScenes();
    const int count = (int) list.size();
    const bool pulse = (juce::Time::getMillisecondCounter() / 350) % 2 == 0;
    for (int i = 0; i < parts.size(); ++i)
    {
        auto* b = parts[i];
        const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
        const bool has = i < count;
        juce::String sub;
        Lit lit = Lit::none;
        if (i == scenes.getCurrent())
            sub = juce::String::fromUTF8 ("\xe2\x96\xb6 Now"), lit = Lit::play;
        else if (i == scenes.getQueued())
            sub = "Next", lit = pulse ? Lit::next : Lit::none;
        else if (has)
            sub = list[(size_t) i].name == "Part " + letter ? juce::String() : list[(size_t) i].name;
        else
            sub = i == count ? "+ Save" : "Empty";
        const bool empty = ! has && i != count;
        if (b->sub != sub || b->lit != lit || b->empty != empty)
        {
            b->sub = sub;
            b->lit = lit;
            b->empty = empty;
            b->setTooltip (has ? "Plays part " + letter + " from the next bar (straight away when stopped). Right-click to save over it "
                                 "or rename it. Key " + juce::String (i + 1) + "."
                               : "Saves what's playing now (beats, patterns, mutes) as a song part. Then press it to come back to it.");
            b->repaint();
        }
    }

    // Live now: what each track is doing.
    auto& transport = engine.getTransport();
    bool changed = false;
    for (int c = 0; c < kChips; ++c)
    {
        const int strip = chipStrip (c);
        const float m = meterOf ? meterOf (strip) : 0.0f;
        heard[(size_t) c] = m > 0.003f ? 1.0f : heard[(size_t) c] * 0.97f;
        Chip chip;
        bool recording = false;
        if (strip == RigEngine::loopsStrip)
            for (int l = 0; l < RigLooper::kLayers; ++l)
            {
                const auto st = engine.getLooper().getState (l);
                recording = recording || st == RigLooper::State::recording || st == RigLooper::State::overdubbing || st == RigLooper::State::armed;
            }
        if (engine.getStrip (strip).mute.load())
            chip = { "Muted", Lit::mute };
        else if (strip == RigEngine::vocoderStrip && ! engine.vocoderOn.load())
            chip = { "Off", Lit::none };
        else if (recording)
            chip = { "Rec", Lit::arm };
        else if (heard[(size_t) c] > 0.05f)
            chip = { "Playing", Lit::play };
        else
            chip = { transport.isPlaying() ? "Quiet" : "Ready", Lit::none };
        if (chip.text != chips[(size_t) c].text || chip.lit != chips[(size_t) c].lit)
        {
            chips[(size_t) c] = chip;
            changed = true;
        }
    }
    if (changed)
        repaint (chipsArea.expanded (4));
}

void Footer::resized()
{
    auto r = getLocalBounds().reduced (22, 0);
    auto content = r.withSizeKeepingCentre (r.getWidth(), 76 + 22);

    // Parts on the left, Live now on the right; on a small screen the chips go first.
    const int chipsWidth = kChips * kChipWidth + (kChips - 1) * 8;
    showChips = content.getWidth() >= 8 * 64 + 7 * 8 + 22 + chipsWidth;
    if (showChips)
    {
        chipsArea = content.removeFromRight (chipsWidth);
        chipsLabel = chipsArea.removeFromTop (22);
        content.removeFromRight (22);
    }
    else
        chipsArea = chipsLabel = {};

    partsLabel = content.removeFromTop (22);
    const int gap = 10;
    const int w = juce::jlimit (56, 84, (content.getWidth() - 7 * gap) / 8);
    partsArea = content.withWidth (8 * w + 7 * gap);
    auto row = partsArea;
    for (auto* b : parts)
    {
        b->setBounds (row.removeFromLeft (w).withHeight (76));
        row.removeFromLeft (gap);
    }
    content.removeFromLeft (partsArea.getWidth() + 16);
    // Messages go beside the parts; when there's no room there, on the line above them.
    if (content.getWidth() >= 200)
    {
        message.setBounds (content.withTrimmedBottom (4));
        message.setJustificationType (juce::Justification::centred);
        message.setFont (barlow (14.0f));
    }
    else
    {
        const int right = showChips ? chipsArea.getX() - 12 : partsArea.getRight();
        message.setBounds (juce::Rectangle<int>::leftTopRightBottom (partsLabel.getX() + 350, partsLabel.getY(), right, partsLabel.getBottom()));
        message.setJustificationType (juce::Justification::centredLeft);
        message.setFont (barlow (13.0f));
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

    engraved (g, juce::String::fromUTF8 ("Song parts \xc2\xb7 keys 1\xe2\x80\x93" "8 \xc2\xb7 switch on next bar"), partsLabel.withWidth (500));
    if (! showChips)
        return;
    engraved (g, "Live now", chipsLabel);
    auto r = chipsArea.withHeight (76);
    for (int c = 0; c < kChips; ++c)
    {
        auto chip = r.removeFromLeft (kChipWidth).toFloat();
        r.removeFromLeft (8);
        drawWell (g, chip);
        auto inner = chip.reduced (10.0f, 8.0f);
        auto top = inner.removeFromTop (24.0f);
        g.setColour (RigColours::forStrip (chipStrip (c)));
        g.fillRoundedRectangle (top.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 12.0f), 3.0f);
        top.removeFromLeft (7.0f);
        g.setColour (p.wellInk);
        g.setFont (cond (18.0f, Weight::extra, 0.06f));
        g.drawFittedText (chipName (c).toUpperCase(), top.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
        auto bottom = inner.removeFromBottom (20.0f);
        drawLamp (g, { bottom.getX() + 8.0f, bottom.getCentreY() }, chips[(size_t) c].lit);
        engraved (g, chips[(size_t) c].text, bottom.withTrimmedLeft (22.0f).toNearestInt(), juce::Justification::centredLeft, 12.0f, true);
    }
}

void Footer::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked() || ! showChips || ! chipsArea.contains (e.getPosition()))
        return;
    const int c = (e.x - chipsArea.getX()) / (kChipWidth + 8);
    if (juce::isPositiveAndBelow (c, kChips) && onTrack)
        onTrack (chipStrip (c));
}
}
