#pragma once

#include "PocketChords.h"
#include "PocketFrame.h"
#include "PocketHold.h"
#include "PocketTheme.h"

// The CHORDS tool: the song's KEY and the SOUND up top, then COLOUR (PLAIN, 7TH, 9TH, SUS, POWER), PLAY (HOLD,
// STRUM, ARP, STABS, + BASS), the PROGRESSION (4 or 8 boxes, one chord a bar while the song plays), what the
// chords PLAY (the synth, a pad's sound, the vocoder's notes), and the eight chord pads low, under the thumb.
// The thumb bar's REC records the chords onto the next loop layer (PocketApp drives it).
class ChordsScreen : public juce::Component, private juce::Timer
{
public:
    explicit ChordsScreen (pocket::ChordsTool&);
    ~ChordsScreen() override;

    std::function<void (juce::Component&, juce::Rectangle<int>)> onKeyMenu;
    std::function<void (const juce::String&)> toast;
    std::function<void()> onChanged;                       // something to save
    // MY PAD SOUND: the PADS pads that have a sound ("A1 · Rhodes Chord"), and picking one.
    std::function<juce::StringArray()> padSounds;
    std::function<juce::Array<int>()> padSoundPads;
    std::function<bool (int pad)> onPadSound;              // false: that pad has no sound
    std::function<juce::String()> lockedText;              // "PROGRESSION ON LAYER 3": what's on a layer, or empty
    pocket::HoldShow showHold;                             // press and hold a chord pad: EDIT CHORD, COPY CHORD TO, PUT IN PROGRESSION BOX 1..n
    std::function<void (int pad)> onEditChord;             // EDIT CHORD: the app opens the chord editor
    pocket::HoldSpec copyMenuSpec (int pad);               // COPY CHORD TO: the other seven pads
    pocket::HoldSpec padMenuSpec (int pad);
    void putInBox (int pad, int box);                      // what that menu row does
    void holdPad (int pad);                                // a long press on a chord pad (also for the self-checks)

    void leave();                                          // another tool opened (a held chord keeps ringing)
    void toolChanged();                                    // the settings were replaced (a song opened)
    juce::Rectangle<int> tourSpot() const { return padWell.toNearestInt(); }

    // Screenshot mode: what the pads and the progression show as sounding.
    void setDemo (int soundingPad, int progressionStep) { demoPad = soundingPad; demoStep = progressionStep; repaint(); }
    void pressPad (int pad, bool down);

    void paint (juce::Graphics&) override;
    void resized() override { layout(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    enum Id
    {
        none = 0, keyId = 1, soundId, colourBase = 10, styleBase = 20, holdId = 19, bassId = 25, progOnId = 30, progLenId, progClearId,
        boxBase = 40, playsBase = 50, padBase = 60
    };
    struct Hit { juce::Rectangle<float> r; int id; };

    void timerCallback() override;
    void layout();
    int idAt (juce::Point<float>) const;
    void press (int id);
    void changed();
    void showSoundMenu();
    juce::String soundLabel() const;
    juce::String statusText() const;
    int sounding() const;
    int step() const;

    pocket::ChordsTool& tool;
    std::vector<Hit> hits;
    juce::Rectangle<float> titleR, colourLabel, playLabel, progWell, progTitle, statusR, padWell;
    std::array<juce::Rectangle<float>, pocket::kChordPads> padR;
    std::array<juce::Rectangle<float>, pocket::kProgressionMax> boxR;
    int pressed = 0, shownPad = -2, shownStep = -2, demoPad = -2, demoStep = -2;
    pocket::HoldDetector hold { [this] { if (pressed >= padBase && pressed < padBase + pocket::kChordPads) holdPad (pressed - padBase); } };
    bool shownMuted = false;
};
