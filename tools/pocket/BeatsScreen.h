#pragma once

#include "BeatsEngine.h"
#include "PocketHost.h"
#include "PocketKit.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"

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
    // A ready beat for a new song: a 2-bar boom-bap pattern on the Street kit's pads at 90 BPM.
    void fillStarterBeat();
    // The tour's spotlights, in this screen's coordinates: 0 = pad A13, 1 = the pattern and its controls.
    juce::Rectangle<int> tourSpot (int which) const;

    void resized() override;

private:
    class DrumView;
    class PadsView;
    void timerCallback() override;
    void saveSoon() { saveCountdown = 30; }
    void pushPads();

    PocketHost& host;
    pocket::Library& library;
    pocket::BeatsEngine beatsEngine;
    pocket::Kit padKit;
    pocket::DrumData drumData;
    std::unique_ptr<DrumView> drumView;
    std::unique_ptr<PadsView> padsView;

    int screenMode = 0, bank = 0, selected = -1, voice = 0, demoStep = -1, saveCountdown = 0;
    bool recPads = false;
    double recFrom = 0.0;
    std::vector<pocket::PadPattern> patternUndo;
    bool drumsDirty = false, kitDirty = false;
   #if JUCE_IOS || JUCE_ANDROID
    juce::ScopedMessageBox sharing;
   #endif
};
