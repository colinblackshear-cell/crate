#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Vintage.h"

// The Chords drawer (F7, or Chords on the screen bar, or Build your own on the vocoder's Chords
// pedal): the chord builder (RigChords), over the bottom of whichever view is showing, so it
// plays whatever you're working on.
//
// Along the top: where the chords go (the picked track, or any instrument), what plays them
// (clicks, the Launchkey's pads, its lowest 16 keys, every key), Hold and Strum. Then Fill
// (the chords of a key, as the vocoder's chord buttons have them) and Sets (starters, your own).
// The sixteen pads: click one to hear it (and to edit it below); right-click for more. Under
// them, the picked pad: its notes, voicing (Lower, Higher, Oct, Spread, Tight), Catch (hold a
// chord on your keyboard), Name and Clear; and the keys, where a click adds or takes out a note.
// On the right, the progression: eight steps on the clock, its length, Play, To loop, Rec MIDI and
// To Logic, with the file to drag into Logic.
class ChordsView : public juce::Component,
                   private juce::Timer
{
public:
    explicit ChordsView (RigEngine&);
    ~ChordsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onSay;

    // As clicks do (the tests use these).
    void selectPad (int pad);
    int getSelectedPad() const { return selected; }
    void pressPad (int pad);       // a click on a pad: picks it and plays it
    void releasePad (int pad);
    void clickKey (int note);      // the keys: adds the note to the picked pad (or takes it out)
    void setTarget (int target);   // RigChords::kFollowKeys or a RigRouting::Dest
    void setTrigger (int trigger);
    void pressHold();
    void pressFill();
    void pickStep (int step, int pad);   // -1: a rest
    void pressPlayProgression();
    juce::String pressToLoop();
    void pressRecMidi();
    juce::File pressToLogic();     // the progression as a MIDI file
    juce::File getLastFile() const { return lastFile; }
    void pressCatch();
    void refresh();

    static constexpr int kPreferredHeight = 390;

    class PadButton;
    class KeyBed;
    class DragChip;

private:
    void timerCallback() override;
    void updateControls();
    void showPadMenu (int pad);
    void showSetsMenu();
    void showStepMenu (int step);
    void rename (int pad);
    void say (const juce::String&);
    void wakeVocoder();   // chords for the vocoder: its robot comes on to sing them
    int targetDest() const;

    RigEngine& engine;
    RigChords& chords;
    int selected = 0;
    juce::String status;

    juce::ComboBox target, trigger, strum, key, scale, type, length;
    juce::TextButton holdButton { "Hold" }, fillButton { "Fill pads 1-8" }, setsButton { "Sets" }, closeButton { "Close" },
        lowerButton { "Lower" }, higherButton { "Higher" }, octDown { "Oct -" }, octUp { "Oct +" }, spreadButton { "Spread" },
        closeVoicing { "Tight" }, catchButton { "Catch" }, nameButton { "Name" }, clearButton { "Clear" },
        playButton { "Play it" }, loopButton { "To loop" }, recButton { "Rec MIDI" }, logicButton { "To Logic" };
    juce::OwnedArray<PadButton> padButtons;
    juce::OwnedArray<juce::TextButton> stepButtons;
    std::unique_ptr<KeyBed> keys;
    std::unique_ptr<DragChip> chip;
    juce::File lastFile;
    int loopLayer = -1;
    int shownKeysTo = -2;

    juce::Rectangle<int> titleArea, padsWell, editLcd, progressionWell, progressionTitle, statusArea;
    std::array<juce::Rectangle<int>, 6> labelAreas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordsView)
};
