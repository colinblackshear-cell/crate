#pragma once

#include "PocketFrame.h"
#include "PocketSynth.h"
#include "PocketTheme.h"

// The SYNTH tool: the sound up top (the display with the sound's name and < >, eight kinds of sound, four
// knobs: TONE, SHAPE, DECAY, ECHO), then KEYS / PATTERN / ARP, the song's KEY and the octave, and the keys
// low, under the thumb. KEYS plays the keys in the song's key (or CHROMATIC); PATTERN is a 16-steps-a-bar
// note pattern that plays with the song; ARP plays the keys you hold in time. The thumb bar's REC records
// what you play into the pattern and puts it on the next loop layer (PocketApp drives it).
class SynthScreen : public juce::Component, private juce::Timer
{
public:
    SynthScreen (pocket::SynthPart&, pocket::BeatsEngine&);
    ~SynthScreen() override;

    std::function<pocket::SongKey()> songKey;
    std::function<void (juce::Component&, juce::Rectangle<int>)> onKeyMenu;
    std::function<void (const juce::String&)> toast;
    std::function<void()> onChanged;       // something to save
    std::function<int()> nextLayer;        // for the hint: where REC lands (0-based, -1 = full)
    std::function<bool()> recBusy;         // REC is counting in, recording or locking

    void leave();                          // another tool opened: every key up
    void partChanged();                    // the part was replaced (a song opened): redraw, no keys held
    juce::Rectangle<int> tourSpot() const { return keysArea.toNearestInt(); }
    juce::Rectangle<int> keysBounds() const { return keysArea.toNearestInt(); }

    // Screenshot mode / self-checks: a key as a finger would press it (down true) and lift it.
    void pressKey (int key, bool down);
    void setMode (int mode);
    void selectStep (int step) { selectedStep = step; repaint(); }
    void setDemoHeld (std::vector<int> keys) { demoHeld = std::move (keys); repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void visibilityChanged() override { if (! isVisible()) leave(); }

private:
    enum Id
    {
        none = 0, prevId, nextId, nameId, kindBase = 10, knobBase = 20, modeBase = 30, keyChipId = 33, octDownId, octUpId,
        scaleId = 40, chromaticId, snapId, arpBase = 45, rateBase = 50, holdId = 52, lenId = 55, pagePrevId, pageNextId, tieId, clearId,
        stripId = 60, stepBase = 70, keyBase = 100
    };
    struct Hit { juce::Rectangle<float> r; int id; };

    void timerCallback() override;
    void layout();
    int idAt (juce::Point<float>) const;
    int keyAt (juce::Point<float>) const;
    void press (int id);
    void changed();
    void keyDown (int source, int key);
    void keyUp (int source);
    bool keyLit (int key) const;
    juce::String stepLabel (int step) const;
    juce::Rectangle<float> keyRect (int key) const;
    bool isBlack (int key) const { return part.chromatic && key >= 8; }
    void drawKnob (juce::Graphics&, int k);
    void drawKeys (juce::Graphics&);
    void drawStrip (juce::Graphics&, juce::Rectangle<float>);
    void drawSteps (juce::Graphics&);

    pocket::SynthPart& part;
    pocket::BeatsEngine& beats;

    // the layout (from the bounds)
    juce::Rectangle<float> panel, lcd, prevR, nextR, strip, keysArea, hintArea;
    std::array<juce::Rectangle<float>, pocket::kSynthKinds> kindR;
    std::array<juce::Rectangle<float>, 4> knobR;
    std::array<juce::Rectangle<float>, 16> stepR;
    std::vector<Hit> hits;

    int pressed = 0, page = 0, selectedStep = -1, dragKnob = -1;
    float dragStart = 0, dragValue = 0;
    std::array<int, 10> fingerKey;          // the key each finger holds (-1 none)
    std::array<int, 10> fingerNote;         // the note it sounds
    std::array<int, pocket::kSynthKinds> lastOfKind;
    std::vector<int> demoHeld;
    int shownStep = -2;
};
