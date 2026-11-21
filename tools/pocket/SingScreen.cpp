#include "SingScreen.h"

#include "PocketRoute.h"

using namespace pocket;

namespace
{
const char* const kModes[3] = { "MY VOICE", "VOCODER", "BOTH" };
const juce::Colour kPadLit { 0xffb9a3dc }, kPadDark { 0xff4a4037 }, kPadText { 0xfff1e7d1 }, kGreenBg { 0xffe6f2e2 }, kGreenEdge { 0xff8fc06a },
                   kAmberBg { 0xfffbe9c4 };
}

SingScreen::SingScreen (PocketHost& h, LoopsScreen& l, VocoderEngine& v, ChordsTool& c, AppSettings& s)
    : host (h), loopsScreen (l), voc (v), chords (c), settings (s)
{
    startTimerHz (20);
}

SingScreen::~SingScreen()
{
    stopTimer();
}

void SingScreen::leave()
{
    if (pressed >= padBase && pressed < padBase + kChordPads)
        pressPad (pressed - padBase, false);
    pressed = 0;
}

void SingScreen::visibilityChanged()
{
    if (isVisible() && ! demo)
        applyMode();
    else
        leave();
}

juce::String SingScreen::sendingName() const
{
    const int pad = chords.soundingPad();
    if (pad >= 0 && chords.playsVocoder)
        return chords.padName (pad);
    if (! voc.chordsFed() && voc.activeChord() >= 0)
        return voc.padName (voc.activeChord());   // the vocoder's own chord (its settings' CHORDS pedal)
    return {};
}

int SingScreen::recLayer() const
{
    if (takeLayer < 0)
        return -1;
    const auto st = loopsScreen.engine().state (takeLayer);
    return st == LoopState::waiting || st == LoopState::recording ? takeLayer : -1;
}

void SingScreen::applyMode()
{
    voc.setMonitor (settings.singMode != 0);   // the vocoder in your ears for VOCODER and BOTH (once the mic is heard)
    host.micUseChanged();                      // your dry voice for MY VOICE and BOTH
}

bool SingScreen::hearShown() const
{
    return host.micReady() && host.micRoute() != Route::none && (host.micRoute() == Route::bluetooth || ! host.hearingMyself());
}

//==============================================================================
void SingScreen::timerCallback()
{
    ++ticks;
    if (! isVisible() || demo)
        return;
    shownChord = chords.soundingPad();
    float level = voc.takeMicLevel();
    if (settings.singMode != 0)
        level = juce::jmax (level, voc.outputLevel());
    levels[(size_t) levelAt] = level;
    levelAt = (levelAt + 1) % (int) levels.size();

    // a take that just finished: say where it went
    const int now = takeLayer >= 0 ? (int) loopsScreen.engine().state (takeLayer) : -1;
    if (takeLayer >= 0 && lastTakeState == (int) LoopState::recording && now == (int) LoopState::playing)
        host.toast ("Take on loop layer " + juce::String (takeLayer + 1) + ". It plays with the beat. REC again for another.");
    lastTakeState = now;
    repaint();
}

//==============================================================================
juce::Rectangle<float> SingScreen::padRect (int i) const
{
    auto w = padWell().reduced (8);
    const float cw = w.getWidth() / 4.0f, ch = w.getHeight() / 2.0f;
    return { w.getX() + (float) (i % 4) * cw + 3, w.getY() + (float) (i / 4) * ch + 3, cw - 6, ch - 6 };
}

int SingScreen::idAt (juce::Point<float> p) const
{
    if (micRect().expanded (3).contains (p)) return micId;
    if (hearShown() && hearRect().expanded (3).contains (p)) return hearId;
    for (int i = 0; i < 3; ++i)
        if (modeCell (i).contains (p))
            return modeBase + i;
    if (lcdRect().contains (p)) return lcdId;
    if (keyRect().expanded (2).contains (p)) return keyId;
    if (chordsRect().expanded (2).contains (p)) return chordsId;
    if (holdRect().expanded (2).contains (p)) return holdId;
    for (int i = 0; i < kChordPads; ++i)
        if (padRect (i).contains (p))
            return padBase + i;
    if (settingsRect().contains (p)) return settingsId;
    if (layersRect().contains (p)) return layersId;
    return 0;
}

void SingScreen::mouseDown (const juce::MouseEvent& e)
{
    pressed = idAt (e.position);
    if (pressed >= padBase && pressed < padBase + kChordPads)
        pressPad (pressed - padBase, true);   // pads sound on the touch, not the lift
    repaint();
}

void SingScreen::mouseUp (const juce::MouseEvent& e)
{
    const int id = idAt (e.position), was = pressed;
    pressed = 0;
    if (was >= padBase && was < padBase + kChordPads)
        pressPad (was - padBase, false);
    else if (id != 0 && id == was)
        press (id);
    repaint();
}

void SingScreen::pressPad (int pad, bool down)
{
    // the CHORDS tool's chords: the same HOLD, sound and vocoder notes (a picked progression box is left alone)
    if (down)
        chords.padDown (pad, false);
    else
        chords.padUp (pad);
    repaint();
}

void SingScreen::press (int id)
{
    if (id >= modeBase && id < modeBase + 3)
    {
        settings.singMode = id - modeBase;
        settings.save();
        applyMode();
    }
    else if (id == micId)
    {
        if (host.micReady())
            host.micOff();
        else
            host.askForMic ({});
    }
    else if (id == hearId)
        host.setHearMyself (! host.hearingMyself());
    else if (id == lcdId || id == settingsId)
        host.showVocoder();
    else if (id == layersId)
        host.showLoops();
    else if (id == keyId)
        showKeyMenu();
    else if (id == chordsId)
    {
        if (onOpenChords)
            onOpenChords();
    }
    else if (id == holdId)
    {
        chords.hold = ! chords.hold;
        if (! chords.hold)
            chords.releaseAll();
        if (onChanged)
            onChanged();
        host.toast (chords.hold ? "HOLD on: a chord rings until you tap it again or another one."
                                : "HOLD off: a chord plays while your finger is on it.");
    }
}

int SingScreen::recordTake (int bars)
{
    static const LoopSource sources[3] = { LoopSource::mic, LoopSource::vocoder, LoopSource::both };
    const int layer = loopsScreen.recordFrom ((int) sources[juce::jlimit (0, 2, settings.singMode)], bars);
    if (layer >= 0)
        takeLayer = layer;
    return layer;
}

void SingScreen::showKeyMenu()
{
    if (onKeyMenu)
        return onKeyMenu (*this, keyRect().toNearestInt());   // the song's one key
    juce::PopupMenu m;
    m.addItem (101, "Minor", true, voc.minor());
    m.addItem (102, "Major", true, ! voc.minor());
    m.addSeparator();
    for (int k = 0; k < 12; ++k)
        m.addItem (1 + k, VocoderEngine::keyName (k), true, k == voc.key());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (keyRect().toNearestInt())),
                     [safe = juce::Component::SafePointer<SingScreen> (this)] (int r)
                     {
                         if (safe == nullptr || r == 0)
                             return;
                         auto& v = safe->voc;
                         if (r == 101 || r == 102)
                             v.setKey (v.key(), r == 101);
                         else
                             v.setKey (r - 1, v.minor());
                         safe->repaint();
                     });
}

//==============================================================================
void SingScreen::paint (juce::Graphics& g)
{
    // what the mic is doing
    {
        auto b = bannerRect();
        const bool on = host.micReady();
        const auto route = host.micRoute();
        const bool hear = host.hearingMyself();
        const bool good = on && route == Route::wired && hear;
        g.setColour (good ? kGreenBg : kAmberBg);
        g.fillRoundedRectangle (b, 12.0f);
        g.setColour (good ? kGreenEdge : col::amber);
        g.drawRoundedRectangle (b.reduced (1.0f), 12.0f, 2.0f);
        juce::String head, tail;
        if (! on)
        {
            head = "Mic off. ";
            tail = "Turn it on to sing.";
        }
        else
        {
            head = route == Route::wired ? "Wired headphones. " : route == Route::bluetooth ? "AirPods / Bluetooth. " : "No headphones. ";
            tail = route == Route::none ? U8("Mic records \xc2\xb7 you don't hear it.") : hear ? U8("Mic on \xc2\xb7 you hear yourself.") : U8("Mic on \xc2\xb7 hear myself off.");
        }
        juce::AttributedString a;
        a.append (head, small (14.0f).boldened(), col::ink);
        a.append (tail, small (14.0f), col::ink);
        a.setWordWrap (juce::AttributedString::byWord);
        a.setJustification (juce::Justification::centredLeft);
        const float right = hearShown() ? hearRect().getX() : micRect().getX();
        a.draw (g, b.withTrimmedLeft (12).withRight (right - 6).reduced (0, 3));

        auto button = [&] (juce::Rectangle<float> r, const juce::String& text, bool lit, int id)
        {
            drawRaised (g, r, lit ? Look::amber : Look::cream, pressed == id, 10.0f);
            g.setColour (col::ink);
            g.setFont (spaced (condensedBold (13.0f), 0.08f));
            g.drawFittedText (text, r.withTrimmedBottom (3).reduced (4, 0).toNearestInt(), juce::Justification::centred, 1, 0.8f);
        };
        button (micRect(), on ? "MIC OFF" : "MIC ON", ! on, micId);
        if (hearShown())
            button (hearRect(), hear ? "HEAR: ON" : "HEAR: OFF", hear, hearId);
    }

    // MY VOICE / VOCODER / BOTH
    {
        auto all = modeCell (0).getUnion (modeCell (2));
        g.setColour (col::creamDeep);
        g.fillRoundedRectangle (all, 12.0f);
        for (int i = 0; i < 3; ++i)
        {
            auto c = modeCell (i).reduced (4);
            if (settings.singMode == i)
                drawRaised (g, c, Look::cream, pressed == modeBase + i, 9.0f);
            g.setColour (settings.singMode == i ? col::ink : col::inkDim);
            g.setFont (spaced (condensedBold (16.0f), 0.08f));
            g.drawText (kModes[i], c.withTrimmedBottom (3), juce::Justification::centred);
        }
    }

    // the display: the sound, the take, and your voice going by
    auto& loops = loopsScreen.engine();
    const int rec = recLayer();
    {
        auto r = lcdRect();
        drawWell (g, r, 12.0f);
        const auto sending = sendingName();
        const juce::String sound = settings.singMode == 0 ? juce::String ("YOUR VOICE")
                                                          : voc.presetName (voc.preset()).toUpperCase() + U8(" \xc2\xb7 ")
                                                                + (sending.isNotEmpty() ? sending : juce::String ("NO CHORD"));
        juce::String right;
        auto bar = [&] (double rel) { return juce::String ((int) std::floor (rel / 4.0) + 1); };
        if (rec >= 0 && loops.state (rec) == LoopState::recording)
        {
            const int bars = loops.recordingBars (rec);
            right = "TAKE " + juce::String (rec + 1) + U8(" \xc2\xb7 BAR ") + bar (juce::jmax (0.0, loops.beatNow() - loops.startBeat (rec)))
                    + (bars > 0 ? " OF " + juce::String (bars) : juce::String());
        }
        else if (rec >= 0)
            right = loops.countingIn() ? "COUNT-IN" : "NEXT BAR";
        else
        {
            int next = -1;
            for (int l = 0; l < kLoopLayers && next < 0; ++l)
                if (loops.state (l) == LoopState::empty)
                    next = l;
            right = next >= 0 ? "READY " + juce::String (U8("\xc2\xb7 LAYER ")) + juce::String (next + 1) : juce::String ("LAYERS FULL");
        }
        g.setFont (spaced (lcdFont (19.0f), 0.04f));
        g.setColour (col::lcdAmber);
        const float rightW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), right) + 14.0f;
        g.drawFittedText (sound, r.reduced (12, 0).withHeight (26).translated (0, 6).withTrimmedRight (rightW).toNearestInt(), juce::Justification::centredLeft, 1, 0.6f);
        g.setColour (rec >= 0 ? col::lcdText : col::lcdAmber);
        g.drawText (right, r.reduced (12, 0).withHeight (26).translated (0, 6), juce::Justification::centredRight);

        auto w = r.reduced (12, 0).withTrimmedTop (36).withTrimmedBottom (8);
        const int n = (int) levels.size();
        const float pitch = w.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float v = std::sqrt (juce::jlimit (0.0f, 1.0f, levels[(size_t) ((levelAt + i) % n)] * 1.5f));
            const float h = juce::jmax (2.0f, v * w.getHeight());
            const bool recent = i > n * 2 / 5;
            g.setColour (rec >= 0 && recent ? col::lcdText : (recent ? col::lcdText.withAlpha (0.75f) : juce::Colour (0xff5a4a3c)));
            g.fillRoundedRectangle (w.getX() + (float) i * pitch, w.getCentreY() - h * 0.5f, juce::jmax (1.5f, pitch - 1.5f), h, 0.8f);
        }
    }

    // CHORDS · Dm › (the CHORDS tool), KEY, HOLD
    auto smallButton = [&] (juce::Rectangle<float> b, const juce::String& text, bool lit, int id)
    {
        drawRaised (g, b, lit ? Look::amber : Look::cream, pressed == id, 10.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensed (14.0f), 0.08f));
        g.drawFittedText (text, b.withTrimmedBottom (3).reduced (4, 0).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    };
    {
        auto b = chordsRect();
        drawRaised (g, b, Look::cream, pressed == chordsId, 10.0f);
        auto inner = b.withTrimmedBottom (3).reduced (12, 0).translated (0, pressed == chordsId ? 2.0f : 0.0f);
        const auto sending = sendingName();
        const bool progression = chords.engine().fromProgression() && chords.soundingPad() >= 0;
        juce::AttributedString a;
        a.append ("CHORDS ", spaced (condensedBold (16.0f), 0.08f), col::ink);
        a.append (U8("\xc2\xb7 ") + (sending.isNotEmpty() ? sending : SongKey::pitchName (chords.key.root) + (chords.key.minor ? " MINOR" : " MAJOR")),
                  spaced (condensedBold (16.0f), 0.04f), sending.isNotEmpty() ? juce::Colour (0xff6a4f9a) : col::ink);
        if (progression)
            a.append (U8("  \xc2\xb7 PROGRESSION"), spaced (condensed (12.0f), 0.08f), col::inkDim);
        a.setJustification (juce::Justification::centredLeft);
        a.draw (g, inner.withTrimmedRight (16));
        g.setColour (col::ink);
        g.setFont (condensedBold (24.0f));
        g.drawText (U8("\xe2\x80\xba"), inner.removeFromRight (16), juce::Justification::centredRight);
    }
    smallButton (keyRect(), "KEY", false, keyId);
    smallButton (holdRect(), "HOLD", chords.hold, holdId);

    drawWell (g, padWell(), 12.0f);
    for (int i = 0; i < kChordPads; ++i)
    {
        auto p = padRect (i);
        const bool lit = chords.soundingPad() == i;
        const float down = pressed == padBase + i ? 2.0f : 0.0f;
        g.setColour (juce::Colour (0xff120f0c));
        g.fillRoundedRectangle (p.translated (0, 3), 10.0f);
        g.setColour (lit ? kPadLit : kPadDark);
        g.fillRoundedRectangle (p.translated (0, down), 10.0f);
        g.setColour (lit ? col::ink : kPadText);
        g.setFont (condensedBold (19.0f));
        g.drawFittedText (chords.padName (i), p.translated (0, down).withTrimmedBottom (p.getHeight() * 0.38f).toNearestInt(), juce::Justification::centredBottom, 1, 0.7f);
        g.setColour (lit ? juce::Colour (0xff3a2a55) : juce::Colour (0xffa89a86));
        g.setFont (lcdFont (16.0f));
        g.drawText (chords.padNumeral (i), p.translated (0, down).withTrimmedTop (p.getHeight() * 0.6f), juce::Justification::centredTop);
    }

    // what happens
    g.setColour (col::inkDim);
    g.setFont (small (13.5f));
    juce::String hint;
    if (rec >= 0)
        hint = loops.state (rec) == LoopState::recording ? "Recording. STOP ends it at the end of the bar."
                                                         : "Getting ready: the take starts on the next bar.";
    else
        hint = settings.singMode == 0 ? "REC records your voice on the next layer. The chords guide you."
                                      : "REC records it on the next layer. The chords set the notes it sings.";
    g.drawFittedText (hint, juce::Rectangle<int> (16, (int) settingsRect().getBottom() + 4, getWidth() - 32, (int) padWell().getY() - (int) settingsRect().getBottom() - 6),
                      juce::Justification::centredLeft, 1, 0.8f);

    smallButton (settingsRect(), "VOCODER SETTINGS", false, settingsId);
    smallButton (layersRect(), U8("LOOP LAYERS \xe2\x80\xba"), false, layersId);
}

//==============================================================================
void SingScreen::showDemo()
{
    demo = true;
    settings.singMode = 1;
    // (PocketApp holds the chord: the CHORDS tool's state is shared)
    juce::Random rng (5);
    for (size_t i = 0; i < levels.size(); ++i)
        levels[i] = (float) (0.15 + 0.6 * std::pow (std::sin ((double) i * 0.37), 2.0) * rng.nextFloat());
    levelAt = 0;
    takeLayer = 2;   // the Loops demo has layer 3 recording
    repaint();
}
