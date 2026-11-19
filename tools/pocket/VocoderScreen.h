#pragma once

#include "LoopsScreen.h"
#include "PocketAudio.h"
#include "PocketHost.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"
#include "PocketVocoder.h"

// The Vocoder tab: your voice from the phone's mic through the Kaleidobox vocoder, with the sounds as a
// preset LCD, the Mac's thirteen pedals as a row of stomp boxes (tap to stomp, hold to open), the open
// pedal's knobs, and chord buttons so no keys are needed. REC A TAKE saves the vocoder to Sounds, TO LOOP
// records it into the next empty Loops layer. Its engine plays inside the Loops engine, on the shared clock.
class VocoderScreen : public juce::Component, private juce::Timer
{
public:
    VocoderScreen (PocketHost&, pocket::Library&, PocketAudio&, LoopsScreen&, pocket::BeatsEngine&);
    ~VocoderScreen() override;

    pocket::VocoderEngine& engine() { return voc; }

    void paint (juce::Graphics&) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void visibilityChanged() override;

    // Screenshot mode: the design's state. The Robot pedal open, A-flat picked, the mic on.
    void showDemo();
    void setDemoHeadphones (bool on) { headphones = on; }

private:
    enum Id { hearMe = 1, prev, next, pedalBase = 10, knobBase = 30, chordBase = 40, drone = 50, keyLabel = 51, recTake = 52,
              toLoop = 53, toMac = 54, pedalsHeader = 55 };

    void timerCallback() override;
    int idAt (juce::Point<float>) const;
    void press (int id);
    void openPedal (int pedal);
    void showKeyMenu();
    void toggleTake();
    void finishTake();
    void save();
    juce::File stateFile() const { return library.getRoot().getChildFile ("Vocoder").getChildFile ("vocoder.xml"); }
    juce::String nextTakeName() const;

    juce::Rectangle<float> micRect() const { return { 16, 6, (float) getWidth() - 110, 46 }; }
    juce::Rectangle<float> hearRect() const { return { (float) getWidth() - 87, 12, 71, 36 }; }
    juce::Rectangle<float> lcdRect() const { return { 16, 58, (float) getWidth() - 32, 66 }; }
    juce::Rectangle<float> prevRect() const { return { 24, 68, 35, 46 }; }
    juce::Rectangle<float> nextRect() const { return { (float) getWidth() - 59, 68, 35, 46 }; }
    juce::Rectangle<float> meterRect() const { return { (float) getWidth() - 98, 72, 31, 41 }; }
    juce::Rectangle<float> pedalRect (int i) const { return { 18.0f + (float) i * 85.6f - scrollX, 162, 76, 107 }; }
    juce::Rectangle<float> pedalRow() const { return { 0, 154, (float) getWidth(), 122 }; }
    juce::Rectangle<float> panelRect() const { return { 16, 283, (float) getWidth() - 32, 139 }; }
    juce::Rectangle<float> knobRect (int k, int count) const;
    juce::Rectangle<float> chordRect (int i) const { return { 16.0f + (float) i * ((float) getWidth() - 32.0f - 46.0f) / 6.0f, 473, 46, 48 }; }
    juce::Rectangle<float> droneRect() const { return { (float) getWidth() - 74, 436, 58, 28 }; }
    juce::Rectangle<float> recRect() const { return { 16, 532, (float) getWidth() - 203, 44 }; }
    juce::Rectangle<float> loopRect() const { return { (float) getWidth() - 177, 532, 79, 44 }; }
    juce::Rectangle<float> macRect() const { return { (float) getWidth() - 88, 532, 72, 44 }; }
    juce::Rectangle<float> chordsLabel() const { return { 16, 436, (float) getWidth() - 100, 28 }; }
    juce::Rectangle<float> keyLabelRect() const;
    float maxScroll() const { return juce::jmax (0.0f, 18.0f + (float) pocket::kPedals * 85.6f - 9.6f + 16.0f - (float) getWidth()); }

    void drawPedal (juce::Graphics&, int i);
    void drawPanel (juce::Graphics&);
    void drawKnobCell (juce::Graphics&, int k);

    PocketHost& host;
    pocket::Library& library;
    PocketAudio& audio;
    LoopsScreen& loopsScreen;
    pocket::VocoderEngine voc;

    int shown = 3;                 // the pedal whose panel is open (ROBOT)
    int pressed = 0, pressedPedal = -1;
    float scrollX = 0.0f, scrollStart = 0.0f;
    bool dragged = false, held = false;
    juce::uint32 pressTime = 0;
    int knobDrag = -1;
    float knobStart = 0.0f;
    float micLevel = 0.0f, outLevel = 0.0f;
    bool headphones = true, demo = false;
    int ticks = 0;
    juce::File takeFile;
};
