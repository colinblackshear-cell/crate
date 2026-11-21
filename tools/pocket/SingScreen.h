#pragma once

#include "BeatsScreen.h"
#include "LoopsScreen.h"
#include "PocketChordPlayer.h"
#include "PocketHost.h"
#include "PocketSettings.h"
#include "PocketTheme.h"
#include "PocketVocoder.h"

// The VOCODER tool: the beat plays in your headphones while you sing over it. Pick MY VOICE, VOCODER or BOTH,
// play chords on eight pads (they drive the vocoder, and can play a pad's sound too), and the thumb bar's
// REC lays the take onto the next empty loop layer, in time (recordTake). A banner says what the mic is
// doing. The display shows the vocoder sound and the take; tap it for every vocoder setting.
class SingScreen : public juce::Component, private juce::Timer
{
public:
    SingScreen (PocketHost&, LoopsScreen&, pocket::VocoderEngine&, BeatsScreen&, pocket::AppSettings&);
    ~SingScreen() override;

    pocket::ChordPlayer& chordPlayer() { return chords; }
    std::function<void (juce::Component&, juce::Rectangle<int>)> onKeyMenu;   // the song's KEY menu (PocketApp)
    void leave();   // another screen opened: let go of a momentary chord
    juce::Rectangle<int> tourSpot() const { return padWell().toNearestInt(); }
    juce::Rectangle<int> bannerSpot() const { return bannerRect().toNearestInt(); }
    // REC: the take (voice, vocoder or both) onto the next empty layer for `bars` bars (0 = FREE). Returns
    // the layer, or -1 (it said why).
    int recordTake (int bars);
    int recLayer() const;   // the layer a take waits or records on, -1 = none

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Screenshot mode: vocoder picked, Dm held, take 3 recording at bar 2 of 8.
    void showDemo();
    void setDemo (bool on) { demo = on; repaint(); }

private:
    enum Id { modeBase = 1, lcdId = 10, keyId = 11, soundId = 12, holdId = 13, padBase = 20, micId = 30, hearId = 31,
              settingsId = 33, layersId = 34 };

    void timerCallback() override;
    int idAt (juce::Point<float>) const;
    void press (int id);
    void pressPad (int pad, bool down);
    void applyMode();
    void showKeyMenu();
    void pickPadSound();
    bool hearShown() const;

    // Settings up top, the chord pads low (under the thumb).
    juce::Rectangle<float> bannerRect() const { return { 16, 8, (float) getWidth() - 32, 46 }; }
    juce::Rectangle<float> micRect() const { auto b = bannerRect(); return { b.getRight() - 84, b.getY() + 5, 78, 36 }; }
    juce::Rectangle<float> hearRect() const { auto m = micRect(); return { m.getX() - 92, m.getY(), 86, 36 }; }
    juce::Rectangle<float> modeCell (int i) const { const float w = ((float) getWidth() - 32) / 3.0f; return { 16 + (float) i * w, 60, w, 46 }; }
    juce::Rectangle<float> lcdRect() const { return { 16, 114, (float) getWidth() - 32, 62 }; }
    juce::Rectangle<float> holdRect() const { return { (float) getWidth() - 80, 184, 64, 44 }; }
    juce::Rectangle<float> soundRect() const { return { (float) getWidth() - 222, 184, 136, 44 }; }
    juce::Rectangle<float> keyRect() const { return { (float) getWidth() - 288, 184, 60, 44 }; }
    juce::Rectangle<float> settingsRect() const { return { 16, 236, (float) getWidth() * 0.5f - 20, 44 }; }
    juce::Rectangle<float> layersRect() const { return { (float) getWidth() * 0.5f + 4, 236, (float) getWidth() * 0.5f - 20, 44 }; }
    juce::Rectangle<float> padWell() const { const float top = juce::jmax (314.0f, (float) getHeight() - 268.0f); return { 16, top, (float) getWidth() - 32, (float) getHeight() - 8 - top }; }
    juce::Rectangle<float> padRect (int i) const;

    PocketHost& host;
    LoopsScreen& loopsScreen;
    pocket::VocoderEngine& voc;
    BeatsScreen& beats;
    pocket::AppSettings& settings;
    pocket::ChordPlayer chords;

    int pressed = 0, heldPad = -1, takeLayer = -1, soundPad = -1;
    bool demo = false;
    std::array<float, 64> levels {};
    int levelAt = 0, ticks = 0, lastTakeState = -1;
};
