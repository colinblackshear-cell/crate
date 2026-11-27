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
// Under that, Live key: twelve key buttons (a little keyboard), Oct - / Oct +, Back to the
// builder's key, when a change lands (now, next beat, next bar) and Learn (put them on keyboard
// buttons), so everything the chords play moves key on the fly while you perform.
// Under them, the key map: Map keys sets every key of your keyboard playing a chord of the key
// (each scale note's chord, the white keys as I to vii, or your own pads), with how keys outside
// the scale play, where the chords sit (follow the keys, one octave, smooth voice leading), Bass
// and Octave. The keys on screen show each key's chord, and a click plays it.
// The sixteen pads: click one to hear it (and to edit it below); right-click for more. Under
// them, the picked pad: its notes, voicing (Lower, Higher, Oct, Spread, Tight), Catch (hold a
// chord on your keyboard), Name and Clear; and the keys, where a click adds or takes out a note.
// On the right, the progression: eight steps on the clock, its length, Play, To loop, Rec MIDI and
// To Logic, with the file to drag into Logic.
class ChordsView : public juce::Component,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit ChordsView (RigEngine&);
    ~ChordsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onSay;
    std::function<void()> onOpenSampler;   // Sound > Edit it in the Sampler

    // Your own sound for the chords: a sample file into the Sampler, tuned (its root note from the
    // file's name, or heard), and the chords sent to it. Returns what happened.
    juce::String loadSound (const juce::File&);
    // The note a sound plays as recorded: a note in the file's name ("Pad_Cmin_120.wav": C), the
    // pitch heard (its octave), or C3. `how` says which.
    static int guessRoot (const juce::AudioBuffer<float>&, const juce::String& fileName, juce::String& how);
    static int noteInName (const juce::String& fileName);   // pitch class 0-11, or -1

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    // The keys' right-click menu (Your keys: put a pad's chord here, catch one, clear).
    void showKeyMenu (int note);
    void putPadOnKey (int pad, int note);   // as that menu does
    void setMapLayout (int layout);

    // As clicks do (the tests use these).
    void selectPad (int pad);
    int getSelectedPad() const { return selected; }
    void pressPad (int pad);       // a click on a pad: picks it and plays it
    void releasePad (int pad);
    void clickKey (int note);      // the keys: adds the note to the picked pad (or takes it out)
    void setTarget (int target);   // RigChords::kFollowKeys or a RigRouting::Dest
    void setTrigger (int trigger);
    void pressHold();
    void pressAllOff();            // All off: every chord stops now (latched, held, the progression)
    void pressChordsOn();          // Chords on / off: chords, or plain single notes
    void showChordsOnMenu();       // its right-click: put it on a keyboard button (Learn), or forget it
    void pressFill();
    void pressMapKeys();           // Map keys: the key map on, or back to what played the pads before
    void setKey (int key, int scale, int type);
    void pickStep (int step, int pad);   // -1: a rest
    void pressPlayProgression();
    juce::String pressToLoop();
    void pressRecMidi();
    juce::File pressToLogic();     // the progression as a MIDI file
    juce::File getLastFile() const { return lastFile; }
    void pressCatch();
    // Live key, as its buttons do.
    void pressLiveKey (int key);       // 0-11
    void pressLiveOctave (int delta);
    void pressLiveHome();
    void setLiveChange (int change);   // RigChords::LiveChange
    void refresh();

    static constexpr int kPreferredHeight = 486;

    class PadButton;
    class KeyBed;
    class DragChip;
    class LiveKeys;

private:
    void timerCallback() override;
    void updateControls();
    void showPadMenu (int pad);
    void showSetsMenu();
    void showStepMenu (int step);
    void showLiveLearnMenu();
    void learnNextLiveControl();
    void rename (int pad);
    void say (const juce::String&);
    void wakeVocoder();   // chords for the vocoder: its robot comes on to sing them
    int targetDest() const;

    RigEngine& engine;
    RigChords& chords;
    int selected = 0;
    juce::String status;

    juce::ComboBox target, trigger, strum, key, scale, type, length, mapLayout, mapOffScale, mapVoicing, mapOctave;
    // Chords on / off (a right-click puts it on a keyboard button).
    struct OnOffButton : juce::TextButton
    {
        std::function<void()> onMenu;
        using juce::TextButton::clicked;
        void clicked (const juce::ModifierKeys& m) override
        {
            if (m.isPopupMenu() && onMenu != nullptr)
                onMenu();
            else
                juce::TextButton::clicked (m);
        }
    };
    OnOffButton chordsOnButton;
    bool shownChordsOn = true;
    juce::TextButton allOffButton { "All off" };
    juce::TextButton holdButton { "Hold" }, fillButton { "Fill pads 1-8" }, setsButton { "Sets" }, closeButton { "Close" },
        lowerButton { "Lower" }, higherButton { "Higher" }, octDown { "Oct -" }, octUp { "Oct +" }, spreadButton { "Spread" },
        closeVoicing { "Tight" }, catchButton { "Catch" }, nameButton { "Name" }, clearButton { "Clear" },
        playButton { "Play it" }, loopButton { "To loop" }, recButton { "Rec MIDI" }, logicButton { "To Logic" },
        mapButton { "Map keys" }, bassButton { "+ Bass" }, soundButton { "Sound" };
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false;
    juce::String shownSamplerName;
    void showSoundMenu();
    juce::OwnedArray<PadButton> padButtons;
    juce::OwnedArray<juce::TextButton> stepButtons;
    std::unique_ptr<KeyBed> keys;
    std::unique_ptr<DragChip> chip;
    std::unique_ptr<LiveKeys> liveKeys;
    juce::ComboBox liveChange;
    juce::TextButton liveOctDown { "Oct -" }, liveOctUp { "Oct +" }, liveHome { "Back" }, liveLearn { "Learn" };
    juce::Rectangle<int> liveWell, liveTitle, liveLcd;
    // Learn all 12 keys in a row: the next control to learn (a RigRouting::Control), or -1.
    int learningLive = -1;
    juce::File lastFile;
    int loopLayer = -1;
    bool loopHadProgression = false;   // the progression was on when To loop was pressed
    int shownKeysTo = -2;
    int shownMapKey = -1;
    int triggerBeforeMap = (int) RigChords::Trigger::screen;
    juce::String mapSummary() const;

    juce::Rectangle<int> titleArea, padsWell, editLcd, progressionWell, progressionTitle, statusArea;
    std::array<juce::Rectangle<int>, 6> labelAreas;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChordsView)
};
