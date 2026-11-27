#include "VocoderScreen.h"

#include "PocketRoute.h"
#include "core/AudioFiles.h"

using namespace pocket;

namespace
{
constexpr float kPedalPitch = 85.6f;
const juce::Colour kLedRed { 0xffec4e8a }, kLedOff { 0xff221b2d }, kLcdOrange { 0xfff59a2a }, kWellText { 0xffe4dbee }, kWellDim { 0xff8e7fa0 };

void drawLed (juce::Graphics& g, juce::Point<float> c, bool on, juce::Colour colour = kLedRed)
{
    if (on)
    {
        g.setColour (colour.withAlpha (0.28f));
        g.fillEllipse (c.x - 9, c.y - 9, 18, 18);
    }
    g.setColour (on ? colour : kLedOff);
    g.fillEllipse (c.x - 5, c.y - 5, 10, 10);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawEllipse (c.x - 5, c.y - 5, 10, 10, 1.0f);
    if (on)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillEllipse (c.x - 2.5f, c.y - 3.2f, 3.0f, 3.0f);
    }
}

void drawButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, Look look, bool down, float font = 16.0f)
{
    drawRaised (g, r, look, down, 12.0f);
    g.setColour (look == Look::red ? juce::Colours::white : (look == Look::cream || look == Look::dark || look == Look::pad || look == Look::ghost) ? col::ink : col::onBright);
    g.setFont (spaced (condensed (font), 0.08f));
    g.drawText (text, r.withTrimmedBottom (3).translated (0, down ? 2.0f : 0.0f), juce::Justification::centred);
}

void drawChevron (juce::Graphics& g, juce::Rectangle<float> r, bool right, juce::Colour c)
{
    const auto m = r.getCentre();
    juce::Path p;
    const float s = right ? 1.0f : -1.0f;
    p.startNewSubPath (m.x - 3.5f * s, m.y - 8);
    p.lineTo (m.x + 3.5f * s, m.y);
    p.lineTo (m.x - 3.5f * s, m.y + 8);
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}
}

//==============================================================================
VocoderScreen::VocoderScreen (PocketHost& h, Library& l, PocketAudio& a, LoopsScreen& loops, BeatsEngine& b)
    : host (h), library (l), audio (a), loopsScreen (loops), voc (b)
{
    if (stateFile().existsAsFile())
        voc.load (stateFile());
    else
        voc.setPreset (1);   // Classic Robot, singing the key's own chord: it works with no keys the first time
    voc.consumeChanged();
    startTimerHz (30);
}

VocoderScreen::~VocoderScreen()
{
    stopTimer();
    save();
}

void VocoderScreen::save()
{
    if (! demo)
        voc.save (stateFile());
}

void VocoderScreen::visibilityChanged()
{
    if (! isVisible() && voc.taking())
        finishTake();
}

//==============================================================================
void VocoderScreen::timerCallback()
{
    ++ticks;
    if ((ticks % 20) == 0 && voc.consumeChanged())
        save();
    if (! isVisible())
        return;

    if (! demo)
    {
        micLevel = juce::jmax (micLevel * 0.85f, voc.takeMicLevel());
        outLevel = juce::jmax (outLevel * 0.85f, voc.outputLevel());
    }

    if (pressedPedal >= 0 && ! dragged && ! held && juce::Time::getMillisecondCounter() - pressTime > 450)
    {
        held = true;
        openPedal (pressedPedal);
    }
    repaint();
}

void VocoderScreen::openPedal (int pedal)
{
    shown = juce::jlimit (0, kPedals - 1, pedal);
    // bring it into view
    const float x = 18.0f + (float) shown * kPedalPitch;
    if (x - scrollX < 8.0f)
        scrollX = juce::jmax (0.0f, x - 18.0f);
    else if (x + 76.0f - scrollX > (float) getWidth() - 8.0f)
        scrollX = juce::jmin (maxScroll(), x + 76.0f - (float) getWidth() + 18.0f);
}

//==============================================================================
juce::Rectangle<float> VocoderScreen::knobRect (int k, int count) const
{
    const float cx = (float) getWidth() * 0.5f + ((float) k - (float) (count - 1) * 0.5f) * 95.0f;
    return { cx - 30.0f, 335.0f, 60.0f, 60.0f };
}

juce::Rectangle<float> VocoderScreen::keyLabelRect() const
{
    const auto head = U8("CHORDS \xc2\xb7 ");
    auto f = spaced (condensedBold (12.0f), 0.14f);
    const float x = 16.0f + juce::GlyphArrangement::getStringWidth (f, head);
    return { x - 4.0f, 436, juce::GlyphArrangement::getStringWidth (f, voc.keyLabel()) + 8.0f, 28 };
}

int VocoderScreen::idAt (juce::Point<float> p) const
{
    if (backRect().expanded (2).contains (p)) return backId;
    if (hearRect().expanded (4).contains (p)) return hearMe;
    if (prevRect().expanded (4).contains (p)) return prev;
    if (nextRect().expanded (4).contains (p)) return next;
    if (pedalRow().contains (p))
    {
        for (int i = 0; i < kPedals; ++i)
            if (pedalRect (i).expanded (4.8f, 0).contains (p))
                return pedalBase + i;
        return pedalBase - 1;   // the row between pedals: still scrolls
    }
    const int count = voc.knobCount (shown);
    for (int k = 0; k < count; ++k)
    {
        auto kr = knobRect (k, count);
        if (juce::Rectangle<float> (kr.getCentreX() - 45, 315, 90, 108).contains (p))
            return knobBase + k;
    }
    if (keyLabelRect().contains (p)) return keyLabel;
    if (droneRect().expanded (4).contains (p)) return drone;
    for (int i = 0; i < 7; ++i)
        if (chordRect (i).expanded (3, 3).contains (p))
            return chordBase + i;
    if (recRect().contains (p)) return recTake;
    if (loopRect().contains (p)) return toLoop;
    if (macRect().contains (p)) return toMac;
    return 0;
}

void VocoderScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    dragged = held = false;
    pressedPedal = -1;
    knobDrag = -1;
    if (pressed >= pedalBase - 1 && pressed < pedalBase + kPedals)
    {
        pressedPedal = pressed >= pedalBase ? pressed - pedalBase : -1;
        pressTime = juce::Time::getMillisecondCounter();
        scrollStart = scrollX;
    }
    else if (pressed >= knobBase && pressed < knobBase + 4)
    {
        knobDrag = pressed - knobBase;
        knobStart = voc.knob01 (shown, knobDrag);
    }
    repaint();
}

void VocoderScreen::mouseDrag (const juce::MouseEvent& e)
{
    if (pressed >= pedalBase - 1 && pressed < pedalBase + kPedals)
    {
        if (! dragged && std::abs (e.getDistanceFromDragStartX()) > 8)
            dragged = true;
        if (dragged)
            scrollX = juce::jlimit (0.0f, maxScroll(), scrollStart - (float) e.getDistanceFromDragStartX());
    }
    else if (knobDrag >= 0)
        voc.setKnob01 (shown, knobDrag, knobStart - (float) e.getDistanceFromDragStartY() / 160.0f);
    repaint();
}

void VocoderScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    const int was = pressed;
    const bool wasPedal = was >= pedalBase && was < pedalBase + kPedals;
    pressed = 0;
    if (wasPedal)
    {
        if (! dragged && ! held && id == was)
            voc.stomp (was - pedalBase);
    }
    else if (knobDrag >= 0)
    {
        // a tap on the knob's name or number does nothing; the drag already moved it
    }
    else if (id != 0 && id == was)
        press (id);
    pressedPedal = -1;
    knobDrag = -1;
    dragged = held = false;
    repaint();
}

void VocoderScreen::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int id = idAt (e.position);
    if (id >= knobBase && id < knobBase + 4)
        voc.resetKnob (shown, id - knobBase);
}

void VocoderScreen::press (int id)
{
    if (id == backId)
        host.closeVocoderSettings();
    else if (id == hearMe)
    {
        // HEAR ME: the vocoder in your ears. It needs the mic on (the headphones sheet), and it only
        // reaches the speaker while you hear yourself.
        if (! host.micReady())
            return host.askForMic ([this] { voc.setMonitor (true); });
        voc.setMonitor (! voc.monitoring());
        if (voc.monitoring() && ! host.hearingMyself())
            host.toast (host.micRoute() == Route::none ? "No headphones: the mic is recorded but not heard, so the speaker can't squeal."
                                                       : "Hear myself is off. Turn it on in the VOCODER tool's banner.");
    }
    else if (id == prev)
        voc.stepPreset (-1);
    else if (id == next)
        voc.stepPreset (1);
    else if (id >= chordBase && id < chordBase + 7)
        voc.pressChord (id - chordBase);
    else if (id == drone)
        voc.toggleDrone();
    else if (id == keyLabel)
        showKeyMenu();
    else if (id == recTake)
        toggleTake();
    else if (id == toLoop)
    {
        if (loopsScreen.vocoderLayer() >= 0)
            loopsScreen.stopVocoder();
        else
            host.askForMic ([this]
            {
                if (const int layer = loopsScreen.recordVocoder(); layer >= 0)
                    host.toast ("Recording the vocoder onto layer " + juce::String (layer + 1) + " from the next bar");
            });
    }
    else if (id == toMac)
    {
        // The newest vocoder take (REC A TAKE saves them to Sounds).
        for (auto& item : library.items())   // newest first
            if (item.name.startsWith ("Vocoder take "))
                return host.sendSoundToMac (item.id);
        host.toast ("Record a take first with REC A TAKE, then send it");
    }
}

void VocoderScreen::showKeyMenu()
{
    juce::PopupMenu m;
    m.addItem (101, "Minor", true, voc.minor());
    m.addItem (102, "Major", true, ! voc.minor());
    m.addSeparator();
    for (int k = 0; k < 12; ++k)
        m.addItem (1 + k, VocoderEngine::keyName (k), true, k == voc.key());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (keyLabelRect().toNearestInt())),
                     [safe = juce::Component::SafePointer<VocoderScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;
                         auto& v = safe->voc;
                         if (r == 101 || r == 102)
                             v.setKey (v.key(), r == 101);
                         else
                             v.setKey (r - 1, v.minor());
                     });
}

//==============================================================================
// REC A TAKE
juce::String VocoderScreen::nextTakeName() const
{
    int highest = 0;
    for (auto& item : library.items())
        if (item.name.startsWith ("Vocoder take "))
            highest = juce::jmax (highest, item.name.fromFirstOccurrenceOf ("Vocoder take ", false, false).getIntValue());
    return "Vocoder take " + juce::String (highest + 1);
}

void VocoderScreen::toggleTake()
{
    if (voc.taking())
        return finishTake();
    if (! audio.isRunning())
        return host.toast ("The microphone isn't ready. Check Settings > Kaleidobox Pocket.");
    takeFile = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-vocoder-take.wav");
    if (! voc.startTake (takeFile))
        host.toast ("Couldn't start the take");
}

void VocoderScreen::finishTake()
{
    voc.stopTake();
    juce::String error;
    auto loaded = crate::AudioFiles::load (takeFile, library.formats(), error);
    takeFile.deleteFile();
    if (loaded == nullptr || loaded->getNumSamples() < 100)
        return host.toast ("That take was empty");
    const auto name = nextTakeName();
    const auto id = library.add (name, kTagRecorded, *loaded, {}, {});
    host.toast (id.isEmpty() ? juce::String ("Couldn't save that take") : "Saved \"" + name + "\" to Sounds");
}

//==============================================================================
void VocoderScreen::showDemo()
{
    demo = true;
    voc.setPreset (1);
    voc.setKey (0, true);
    voc.pressChord (5);                       // A flat
    if (! voc.pedalOn ((int) Pedal::tune))
        voc.stomp ((int) Pedal::tune);
    shown = (int) Pedal::robot;
    scrollX = 0.0f;
    micLevel = 0.6f;
    outLevel = 0.8f;
    repaint();
}

//==============================================================================
void VocoderScreen::paint (juce::Graphics& g)
{
    const bool micOn = demo || (audio.isRunning() && host.micReady());

    // back to the VOCODER tool, what the mic is doing, HEAR ME
    {
        drawButton (g, backRect(), U8("\xe2\x80\xb9 VOCODER"), Look::cream, pressed == backId, 16.0f);
        drawLed (g, { backRect().getRight() + 14, 28 }, micOn, col::green);
        const float x = backRect().getRight() + 26;
        const auto route = host.micRoute();
        g.setFont (spaced (condensedBold (14.0f), 0.08f));
        g.setColour (col::ink);
        g.drawText (micOn ? "MIC ON" : "MIC OFF", juce::Rectangle<float> (x, 6, hearRect().getX() - x - 4, 22), juce::Justification::bottomLeft);
        g.setFont (small (12.5f));
        g.setColour (! micOn || route != Route::wired ? col::amberDark : col::inkDim);
        const juce::String note = ! micOn ? juce::String ("tap HEAR ME to turn it on")
                                : host.hearingMyself() ? juce::String (route == Route::bluetooth ? "Bluetooth: you hear it late" : "you hear yourself")
                                                       : juce::String (route == Route::none ? "no headphones: not heard" : "hear myself is off");
        g.drawFittedText (note, juce::Rectangle<float> (x, 28, hearRect().getX() - x - 4, 20).toNearestInt(), juce::Justification::topLeft, 1, 0.8f);

        drawButton (g, hearRect(), "HEAR ME", voc.monitoring() && host.hearingMyself() ? Look::amber : Look::cream, pressed == hearMe, 16.0f);
    }

    // the preset LCD
    {
        auto r = lcdRect();
        drawWell (g, r, 14.0f);
        drawRaised (g, prevRect(), Look::dark, pressed == prev, 9.0f);
        drawRaised (g, nextRect(), Look::dark, pressed == next, 9.0f);
        drawChevron (g, prevRect().withTrimmedBottom (3), false, juce::Colour (0xfff3eadb));
        drawChevron (g, nextRect().withTrimmedBottom (3), true, juce::Colour (0xfff3eadb));

        const int idx = voc.preset();
        g.setColour (kLcdOrange);
        g.setFont (spaced (lcdFont (18.0f), 0.1f));
        g.drawText (voc.presetCategory (idx).toUpperCase() + U8(" \xc2\xb7 ") + juce::String (idx + 1) + " OF " + juce::String (voc.presetCount()),
                    juce::Rectangle<float> (68, 62, (float) getWidth() - 170, 24), juce::Justification::centredLeft);

        const auto name = voc.presetName (idx).toUpperCase();
        auto f = spaced (lcdFont (35.0f), 0.02f);
        const float room = (float) getWidth() - 184.0f;
        if (juce::GlyphArrangement::getStringWidth (f, name) > room)
            f = f.withHeight (35.0f * room / juce::GlyphArrangement::getStringWidth (f, name));
        g.setColour (col::lcdAmber);
        g.setFont (f);
        g.drawText (name, juce::Rectangle<float> (68, 84, room, 36), juce::Justification::centredLeft);

        // the level
        auto m = meterRect();
        const float heights[3] = { 21.0f, 31.0f, 41.0f }, from[3] = { 0.04f, 0.18f, 0.45f };
        for (int i = 0; i < 3; ++i)
        {
            const bool lit = outLevel > from[i];
            g.setColour (lit ? (i == 2 ? col::lcdAmber : juce::Colour (0xff2fc6a8)) : juce::Colour (0xff3a2f4f));
            g.fillRoundedRectangle (m.getX() + (float) i * 12.0f, m.getBottom() - heights[i], 7.5f, heights[i], 1.5f);
        }
    }

    // the pedals
    {
        g.setColour (col::inkDim);
        g.setFont (spaced (condensedBold (12.0f), 0.14f));
        g.drawText (U8("PEDALS \xc2\xb7 TAP TO STOMP, HOLD TO OPEN"), juce::Rectangle<float> (16, 132, (float) getWidth() - 150, 20), juce::Justification::centredLeft);
        g.setFont (small (14.0f));
        g.drawText (juce::String (voc.pedalsOn()) + U8(" on \xc2\xb7 ") + juce::String (kPedals) + U8(" \xe2\x80\xba"),
                    juce::Rectangle<float> ((float) getWidth() - 160, 132, 144, 20), juce::Justification::centredRight);

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (pedalRow().toNearestInt());
        for (int i = 0; i < kPedals; ++i)
            drawPedal (g, i);
    }

    drawPanel (g);

    // the chords
    {
        const auto key = voc.keyLabel();
        auto f = spaced (condensedBold (12.0f), 0.14f);
        g.setFont (f);
        g.setColour (col::inkDim);
        const auto head = U8("CHORDS \xc2\xb7 ");
        g.drawText (head, juce::Rectangle<float> (16, 436, 200, 28), juce::Justification::centredLeft);
        const float kx = 16.0f + juce::GlyphArrangement::getStringWidth (f, head);
        g.drawText (key, juce::Rectangle<float> (kx, 436, 200, 28), juce::Justification::centredLeft);
        const float tx = kx + juce::GlyphArrangement::getStringWidth (f, key);
        g.drawText (U8(" \xc2\xb7 NO KEYS NEEDED"), juce::Rectangle<float> (tx, 436, 220, 28), juce::Justification::centredLeft);

        drawButton (g, droneRect(), "DRONE", voc.drone() ? Look::amber : Look::cream, pressed == drone, 15.0f);
        for (int i = 0; i < 7; ++i)
            drawButton (g, chordRect (i), voc.chordName (i), voc.activeChord() == i ? Look::amber : Look::cream, pressed == chordBase + i, 18.0f);
    }

    // the bottom row
    {
        auto r = recRect();
        const bool taking = voc.taking();
        drawRaised (g, r, Look::red, pressed == recTake, 12.0f);
        auto inner = r.withTrimmedBottom (3).translated (0, pressed == recTake ? 2.0f : 0.0f);
        g.setColour (juce::Colours::white);
        const auto text = taking ? "STOP " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + juce::String (juce::roundToInt (voc.takeSeconds())) + "s" : juce::String ("REC A TAKE");
        g.setFont (spaced (condensed (17.0f), 0.08f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text);
        const float x0 = inner.getCentreX() - (tw + 24.0f) * 0.5f;
        if (taking)
            g.fillRoundedRectangle (x0, inner.getCentreY() - 6, 12, 12, 2.0f);
        else
            g.fillEllipse (x0, inner.getCentreY() - 6, 12, 12);
        g.drawText (text, juce::Rectangle<float> (x0 + 24.0f, inner.getY(), tw + 4.0f, inner.getHeight()), juce::Justification::centredLeft);

        juce::String loopText = "TO LOOP";
        const int layer = loopsScreen.vocoderLayer();
        if (layer >= 0)
            loopText = loopsScreen.engine().state (layer) == pocket::LoopState::recording ? "STOP" : "WAIT";
        drawButton (g, loopRect(), loopText, layer >= 0 ? Look::amber : Look::cream, pressed == toLoop, 16.0f);
        drawButton (g, macRect(), "TO MAC", Look::cream, pressed == toMac, 16.0f);
    }
}

void VocoderScreen::drawPedal (juce::Graphics& g, int i)
{
    auto r = pedalRect (i);
    if (r.getRight() < -4.0f || r.getX() > (float) getWidth() + 4.0f)
        return;
    const auto& d = pedalDef (i);
    const juce::Colour c (d.colour);
    const bool down = pressedPedal == i && ! dragged;
    auto body = r.withTrimmedBottom (4.0f).translated (0, down ? 2.0f : 0.0f);

    g.setColour (c.darker (0.55f));
    g.fillRoundedRectangle (body.translated (0, down ? 1.0f : 4.0f), 10.0f);
    g.setGradientFill (juce::ColourGradient (c.brighter (0.12f), 0, body.getY(), c.darker (0.08f), 0, body.getBottom(), false));
    g.fillRoundedRectangle (body, 10.0f);
    g.setColour (juce::Colours::white.withAlpha (0.3f));
    g.drawRoundedRectangle (body.reduced (0.7f), 10.0f, 1.0f);

    drawLed (g, { body.getCentreX(), body.getY() + 14.0f }, voc.pedalOn (i));

    auto f = spaced (condensed (16.0f), 0.04f);
    const juce::String name (d.name);
    const float room = body.getWidth() - 6.0f;
    if (juce::GlyphArrangement::getStringWidth (f, name) > room)
        f = f.withHeight (16.0f * room / juce::GlyphArrangement::getStringWidth (f, name));
    g.setColour (col::ink);
    g.setFont (f);
    g.drawText (name, body.withTrimmedTop (30).withHeight (28), juce::Justification::centred);

    auto sw = juce::Rectangle<float> (body.getCentreX() - 19.0f, body.getY() + 69.0f, 38.0f, 22.0f);
    g.setColour (juce::Colour (0xff8e7fa0).withAlpha (0.8f));
    g.fillRoundedRectangle (sw.translated (0, 2), 8.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff3eadb), 0, sw.getY(), juce::Colour (0xffc9bcd6), 0, sw.getBottom(), false));
    g.fillRoundedRectangle (sw, 8.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawRoundedRectangle (sw.reduced (0.6f), 8.0f, 1.0f);

    if (i == shown)
    {
        g.setColour (col::amber);
        g.drawRoundedRectangle (r.withTrimmedBottom (2.0f).expanded (3.5f, 3.5f), 12.0f, 2.5f);
    }
}

void VocoderScreen::drawPanel (juce::Graphics& g)
{
    auto r = panelRect();
    drawWell (g, r, 16.0f);
    const auto& d = pedalDef (shown);
    const juce::Colour c (d.colour);

    auto tf = spaced (condensed (21.0f), 0.08f);
    g.setFont (tf);
    g.setColour (c);
    const juce::String title (d.name);
    g.drawText (title, juce::Rectangle<float> (r.getX() + 14, r.getY() + 8, 200, 24), juce::Justification::centredLeft);
    g.setColour (kWellText);
    g.setFont (small (14.0f));
    g.drawText (d.about, juce::Rectangle<float> (r.getX() + 14 + juce::GlyphArrangement::getStringWidth (tf, title) + 12, r.getY() + 8, r.getWidth() - 40 - juce::GlyphArrangement::getStringWidth (tf, title), 24),
                juce::Justification::centredLeft);

    const int count = voc.knobCount (shown);
    for (int k = 0; k < count; ++k)
        drawKnobCell (g, k);
}

void VocoderScreen::drawKnobCell (juce::Graphics& g, int k)
{
    const auto& d = pedalDef (shown);
    const int count = voc.knobCount (shown);
    auto kr = knobRect (k, count);
    g.setColour (kWellText);
    g.setFont (spaced (condensed (13.0f), 0.12f));
    g.drawText (d.knob[k].label, juce::Rectangle<float> (kr.getCentreX() - 46, 321, 92, 18), juce::Justification::centred);
    drawKnob (g, kr, voc.knob01 (shown, k), juce::Colour (d.colour), true, true);
    g.setColour (kWellDim);
    g.setFont (lcdFont (21.0f));
    g.drawFittedText (voc.knobText (shown, k), juce::Rectangle<int> ((int) kr.getCentreX() - 44, 396, 88, 22), juce::Justification::centred, 1, 0.7f);
}
