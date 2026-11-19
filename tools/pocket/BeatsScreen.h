#pragma once

#include "BeatsEngine.h"
#include "PocketHost.h"
#include "PocketKit.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"

// The Beats tab: a switch at the top between the drum machine (the Mac's 808 engine) and the MPC pads.
class BeatsScreen : public juce::Component, private juce::Timer
{
public:
    BeatsScreen (PocketHost&, pocket::Library&);
    ~BeatsScreen() override;

    pocket::BeatsEngine& engine() { return beatsEngine; }
    pocket::Kit& kit() { return padKit; }
    pocket::DrumData& drums() { return drumData; }

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

    void exportForMpc();
    void renameKit();

    // The pad pattern (REC PADS). Recording runs the clock with a click; pads played go into the
    // pattern, snapped to the grid. LOCK BEAT records the beat onto the next empty loop layer.
    void toggleRecordPads();
    bool recordingPads() const { return recPads; }
    void togglePatternPlay();
    void setPatternBars (int bars);
    void cycleQuantize();
    void undoPatternHit();
    void clearPattern();
    void lockBeat();
    void patternChanged();
    double patternPosition() const;   // beats into the pattern, -1 when it isn't playing
    void recordHit (int pad, float velocity);

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

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    class DrumView;
    class PadsView;
    void timerCallback() override;
    void saveSoon() { saveCountdown = 30; }
    void pushPads();
    juce::Rectangle<float> segment (int i) const;

    PocketHost& host;
    pocket::Library& library;
    pocket::BeatsEngine beatsEngine;
    pocket::Kit padKit;
    pocket::DrumData drumData;
    std::unique_ptr<DrumView> drumView;
    std::unique_ptr<PadsView> padsView;

    int screenMode = 0, bank = 0, selected = -1, voice = 0, demoStep = -1, saveCountdown = 0;
    bool recPads = false;
    std::vector<pocket::PadPattern> patternUndo;
    bool drumsDirty = false, kitDirty = false;
   #if JUCE_IOS || JUCE_ANDROID
    juce::ScopedMessageBox sharing;
   #endif
};
