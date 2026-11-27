#pragma once

#include "BeatsEngine.h"
#include "PocketHost.h"
#include "PocketKey.h"
#include "PocketKit.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"
#include "RepeatHold.h"
#include "StepTools.h"

// Two tools in one: DRUMS (the Mac's 808 engine, mode 0) and PADS (the MPC pads, mode 1). The tool row
// picks the mode. The thumb bar's REC records either onto the next loop layer (PocketApp drives it).
class BeatsScreen : public juce::Component, private juce::Timer
{
public:
    BeatsScreen (PocketHost&, pocket::Library&);
    ~BeatsScreen() override;

    pocket::BeatsEngine& engine() { return beatsEngine; }
    pocket::Kit& kit() { return padKit; }
    pocket::DrumData& drums() { return drumData; }
    const pocket::DrumData& drums() const { return drumData; }

    void showMode (int mode);   // 0 drum machine, 1 MPC pads
    int mode() const { return screenMode; }
    int currentBank() const { return bank; }
    void setBank (int);
    int selectedPad() const { return selected; }
    void selectPad (int pad);   // switches to its bank and highlights it

    void padChanged (int pad);  // a pad's settings changed: send them to the audio and save
    void padsChanged();         // pads were put on, cleared or replaced
    void drumsChanged();        // the drum machine's patterns or knobs changed
    void setBpm (double);       // the one tempo of the app
    void leave();               // the tab was left: stop the drums and the pads
    void refreshPads();         // redraw the pad buttons' states (after LOCK BEAT)
    void flush();               // save anything waiting to be saved, now (before switching ideas)
    void reloadIdea (const juce::String& ideaName);   // the library points at another idea: load its pads and beat
    juce::String summary() const;   // "92 BPM · 4 pads · beat recorded", for the Ideas list

    void exportForMpc();
    void renameKit();

    // Press and hold a pad (PADS): SOUND PAGE, SWAP SOUND, RECORD A NEW SOUND, VOLUME · PAN · TUNE, 16 LEVELS,
    // COPY TO ANOTHER PAD, REMOVE SOUND. A touch still sounds the pad at once; the menu comes after the hold.
    pocket::HoldSpec padMenuSpec (int pad);
    pocket::HoldSpec swapMenuSpec (int pad);
    void swapSound (int pad, const juce::String& soundId);   // SWAP SOUND: the pad keeps its volume, pan, tune and mode
    void removePad (int pad);                                // REMOVE SOUND: UNDO (the note's button) puts it back
    void undoRemovePad();
    void addFactoryItem (pocket::HoldSpec&, int pad);              // FACTORY SOUND BACK, on a factory kit's bank
    bool canUndoRemovePad() const { return haveRemoved; }
    // 16 LEVELS: the bank's 16 pads play one pad's sound from quiet (the first pad) to loud (the last).
    void startLevels (int pad);
    int levelsPad() const { return levelsOf; }
    // COPY TO ANOTHER PAD: tap the pad it goes to (the pad there is kept for UNDO).
    void startCopy (int pad);
    void copyPadTo (int target);
    void undoCopy();
    int copySource() const { return copyFrom; }
    void endPadMode();                                       // leaves 16 LEVELS or COPY
    void holdPad (int pad);                                  // what a long press on a pad does (also for the self-checks)
    // PLAY / EDIT on the pads. In PLAY a pad only plays: holding it keeps it sounding and nothing pops up. In EDIT a
    // tap opens the pad's sheet (pitch first), and a hold opens the full pad menu.
    void setPadEdit (bool on);
    bool padEditing() const { return padEdit; }
    // PLAY AS KEYS: the pad's sound on all 16 pads of its bank, each a step of the song's key higher (the original in
    // the middle, A08 and A16). UNDO (the note's button) puts the bank back.
    void playAsKeys (int pad);
    std::function<pocket::SongKey()> songKey;
    // The self-checks: a finger lands on a pad of the bank, the hold time passes, the finger lifts.
    void testTouchPad (int pad);
    void testHoldElapsed();
    void testLiftPad();
    void testTouchPadSource (int pad, int source);   // a second finger (multi-touch)
    void testLiftSource (int source);
    juce::Rectangle<float> testPadBounds (int pad) const;   // a pad of the shown bank, in this screen's coordinates (POCKET_STRESS taps it)

    // The pad pattern. While recording, pads played from `fromBeat` on go into the pattern, snapped to the
    // grid (REC on PADS turns it on, with the pattern playing; PocketApp then locks it onto a layer).
    void setRecordingPads (bool on, double fromBeat = 0.0);
    bool recordingPads() const { return recPads; }
    std::function<bool()> clickWanted;   // the app's CLICK, for when the pattern stops
    void togglePatternPlay();
    void setPatternBars (int bars);
    void cycleQuantize();
    void undoPatternHit();
    void clearPattern();
    void patternChanged();
    double patternPosition() const;   // beats into the pattern, -1 when it isn't playing
    void recordHit (int pad, float velocity);

    // NOTE REPEAT (PADS): hold the red REPEAT strip and every pad you hold plays again at the rate; double-tap the strip to
    // latch it on, press once to switch it off. The rate chips above the pads set how fast (noterepeat::kRates).
    void repeatPress (double nowMs = juce::Time::getMillisecondCounterHiRes());
    void repeatRelease();
    bool repeatIsOn() const { return repeatHold.isOn(); }
    bool repeatIsLatched() const { return repeatHold.isLatched(); }
    void setRepeatRate (int index);
    int repeatRate() const { return drumData.repeatRate; }

    // The drum machine's two ways in: STEPS (the 16 big boxes) and PLAY LIVE (finger drumming on 12 pads).
    int drumMode() const { return drumModeValue; }   // 0 STEPS, 1 PLAY LIVE
    void setDrumMode (int mode);
    std::function<void()> onDrumModeChanged;
    // A PLAY LIVE hit: the voice sounds at once; while the clock runs (the song plays, or REC counts and records)
    // the hit is also written into the edited drum pattern on the nearest step. Returns that step, or -1 when
    // nothing was written (stopped, or still counting in). Message thread: the sound goes through the engine's
    // lock-free queue and the pattern is published like any other edit.
    int hitLive (int voice);
    void undoLiveHit();
    // The step tools on the STEPS grid (HIT, ROLL xN, BUILDS, FLAM, ACCENT, SOFT/LOUD, ERASE), the bar it shows (0-3 of up to 64 steps)
    // and the length. Every change to the grid is remembered, so UNDO puts it back.
    void setDrumTool (crate::drums::steptools::Tool);
    crate::drums::steptools::Tool getDrumTool() const { return drumTool; }
    void setDrumRoll (int hits);   // 2 to 8
    int getDrumRoll() const { return drumRoll; }
    void setDrumPage (int bar);
    int getDrumPage() const { return drumPage; }
    void cycleDrumLength();        // 8, 12, 16, 24, 32, 48, 64 steps, then round again
    void rememberDrums();
    void clearDrumPattern();
    bool canUndoLiveHit() const { return ! liveUndo.empty(); }
    std::function<bool()> inCountIn;   // the app's count-in is running: hits sound but aren't written
    // REC on PLAY LIVE (PocketApp): hits are written from `fromBeat` on.
    void setRecordingDrums (bool on, double fromBeat = 0.0);
    bool recordingDrums() const { return recDrumsOn; }
    // The Make a beat lesson: a compact layout that leaves `reservePx` free at the bottom for the coach card,
    // and amber hints on steps of one voice (only drawn, never written).
    void setLessonLayout (bool on, int reservePx = 0);
    void setGuide (int voice, juce::uint32 stepMask);
    // STEPS shows six sounds at once: a tap on row (0-5) box (0-7) of sound set 0/1, half bar 0/1 (the self-checks).
    void tapEverySoundBox (int set, int half, int row, int box);
    bool everySoundGridShown() const;
    void tapStep (int step);                    // what a tap on a step box does (the self-checks tap with it)
    juce::Rectangle<int> stepsBounds() const;   // the 16 big step buttons, in this screen's coordinates
    juce::String selfCheckLive();               // PLAY LIVE writes to the nearest step, the lesson hints write nothing, no double hits
    juce::String liveReport;                    // what selfCheckLive measured for the early hit

    // Your own kit on the drum machine: a sound from your library on any of the twelve drum sounds (it plays instead of
    // the analog sound, and the idea saves which sound it is). The KIT button opens the sheet for the picked drum sound.
    int drumVoice() const { return voice; }
    bool setDrumSound (int drumVoice, const juce::String& soundId, bool hear = true);   // false: the sound couldn't be opened
    void clearDrumSound (int drumVoice);
    int useBankAsKit();                                    // the pads' names (kick, snare, hat...) onto the drum sounds; how many
    juce::String drumSoundId (int drumVoice) const { return drumData.sampleIds[(size_t) drumVoice]; }
    juce::String drumSoundName (int drumVoice) const;     // "" = analog
    bool drumHasSound (int drumVoice) const { return beatsEngine.getDrumSample (drumVoice) != nullptr; }
    std::function<void()> onDrumKitChanged;                // the sheet shows what is on the picked sound

    // Screenshot mode
    void setDemoStep (int step) { demoStep = step; repaint(); }
    void fillDemoKit();
    void selectVoice (int voice);
    // Writes the kit as an MPC export, reads it back, and returns what was wrong (empty = round trip fine).
    juce::String selfCheckExport (juce::File work);
    // Plays the drum machine and a pad through the engine with no sound card; returns what was wrong.
    juce::String selfCheckSound();
    juce::String selfCheckPattern();   // a recorded hit plays on its beat, and LOCK BEAT's timing holds
    void fillDemoPattern();
    void fillDemoDrums();   // screenshots only: a boom-bap beat in the drum grid (a new song's grid is empty)
    // The tour's spotlights, in this screen's coordinates: 0 = pad A13, 1 = the pattern and its controls.
    juce::Rectangle<int> tourSpot (int which) const;

    void resized() override;

private:
    class DrumView;
    class PadsView;
    void timerCallback() override;
    void saveSoon() { saveCountdown = 30; }
    void pushPads();
    void applyDrumSounds();   // every drum sound's own sound from the idea's saved ids

    PocketHost& host;
    pocket::Library& library;
    pocket::BeatsEngine beatsEngine;
    pocket::Kit padKit;
    pocket::DrumData drumData;
    std::unique_ptr<DrumView> drumView;
    std::unique_ptr<PadsView> padsView;

    int screenMode = 0, bank = 0, selected = -1, voice = 0, demoStep = -1, saveCountdown = 0;
    bool recPads = false, recDrumsOn = false;
    double recDrumsFrom = 0.0;
    int drumModeValue = 0, lessonReserve = 0, guideVoice = -1;
    int drumPage = 0, drumRoll = 3;
    crate::drums::steptools::Tool drumTool = crate::drums::steptools::Tool::hit;
    bool lessonLayout = false;
    juce::uint32 guideMask = 0;
    struct LiveUndo { int slot = 0; bool b = false; crate::drums::Pattern pattern; };
    std::vector<LiveUndo> liveUndo;
    double recFrom = 0.0;
    noterepeat::Hold repeatHold;
    void applyRepeatRate();
    void takeRepeats();   // REC PADS: the repeats the engine played go into the pattern
    void recordHitAt (int pad, float velocity, double now, bool commit);
    std::vector<pocket::PadPattern> patternUndo;
    bool drumsDirty = false, kitDirty = false;
    int levelsOf = -1, copyFrom = -1, removedPad = -1, copiedTo = -1;
    pocket::KitPad removed, copiedWas;
    bool haveRemoved = false, haveCopied = false;
    bool padEdit = false;              // PLAY / EDIT on the pads
   #if JUCE_IOS || JUCE_ANDROID
    juce::ScopedMessageBox sharing;
   #endif
};
