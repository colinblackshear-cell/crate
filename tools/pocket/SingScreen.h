#pragma once

#include "BeatsScreen.h"
#include "LoopsScreen.h"
#include "PocketChordPlayer.h"
#include "PocketHost.h"
#include "PocketSettings.h"
#include "PocketTheme.h"
#include "PocketVocoder.h"

// The Sing step: the beat plays in your headphones while you sing over it. Pick MY VOICE, VOCODER or BOTH,
// play chords on eight pads (they drive the vocoder, and can play a Beats pad's sound too), and REC lays
// the take onto the next empty loop layer, in time, from the next bar (after a count-in when it starts the
// song). The display shows the vocoder sound and the take; tap it for every vocoder setting.
class SingScreen : public juce::Component, private juce::Timer
{
public:
    SingScreen (PocketHost&, LoopsScreen&, pocket::VocoderEngine&, BeatsScreen&, pocket::AppSettings&);
    ~SingScreen() override;

    pocket::ChordPlayer& chordPlayer() { return chords; }
    void leave();   // another screen opened: let go of a momentary chord
    juce::Rectangle<int> tourSpot() const { return padWell().toNearestInt(); }

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Screenshot mode: vocoder picked, Dm held, take 3 recording at bar 2 of 8.
    void showDemo();
    void setDemoHeadphones (bool on) { headphones = on; repaint(); }

private:
    enum Id { modeBase = 1, lcdId = 10, keyId = 11, soundId = 12, holdId = 13, padBase = 20, countId = 30, recId = 31,
              lengthId = 32, settingsId = 33, layersId = 34 };

    void timerCallback() override;
    int idAt (juce::Point<float>) const;
    void press (int id);
    void pressPad (int pad, bool down);
    void applyMode();
    void toggleRec();
    void showKeyMenu();
    void pickPadSound();
    int recLayer() const;   // the layer a take waits or records on, -1 = none

    juce::Rectangle<float> bannerRect() const { return { 16, 8, (float) getWidth() - 32, 50 }; }
    juce::Rectangle<float> modeCell (int i) const { const float w = ((float) getWidth() - 32) / 3.0f; return { 16 + (float) i * w, 66, w, 46 }; }
    juce::Rectangle<float> lcdRect() const { return { 16, 120, (float) getWidth() - 32, 80 }; }
    juce::Rectangle<float> holdRect() const { return { (float) getWidth() - 80, 210, 64, 34 }; }
    juce::Rectangle<float> soundRect() const { return { (float) getWidth() - 214, 210, 128, 34 }; }
    juce::Rectangle<float> keyRect() const { return { (float) getWidth() - 278, 210, 58, 34 }; }
    juce::Rectangle<float> padWell() const { return { 16, 252, (float) getWidth() - 32, 128 }; }
    juce::Rectangle<float> padRect (int i) const;
    juce::Rectangle<float> recRect() const { return { (float) getWidth() * 0.5f - 42, 394, 84, 84 }; }
    juce::Rectangle<float> countRect() const { return { 16, 408, (float) getWidth() * 0.5f - 72, 56 }; }
    juce::Rectangle<float> lengthRect() const { return { (float) getWidth() * 0.5f + 56, 408, (float) getWidth() * 0.5f - 72, 56 }; }
    juce::Rectangle<float> settingsRect() const { return { 16, 540, (float) getWidth() * 0.5f - 22, 44 }; }
    juce::Rectangle<float> layersRect() const { return { (float) getWidth() * 0.5f + 6, 540, (float) getWidth() * 0.5f - 22, 44 }; }

    PocketHost& host;
    LoopsScreen& loopsScreen;
    pocket::VocoderEngine& voc;
    BeatsScreen& beats;
    pocket::AppSettings& settings;
    pocket::ChordPlayer chords;

    int pressed = 0, heldPad = -1, takeLayer = -1, soundPad = -1;
    bool headphones = true, demo = false;
    std::array<float, 64> levels {};
    int levelAt = 0, ticks = 0, lastTakeState = -1;
};
