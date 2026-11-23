#pragma once

#include "PocketAudio.h"
#include "PocketHost.h"
#include "PocketKit.h"
#include "PocketSheet.h"
#include "PocketTheme.h"
#include "PocketWave.h"

// The SAMPLER tool: one sound for the pads, on a full page. The sound and where it goes up top (SOUNDS opens another
// one), the waveform with its IN and OUT handles, then ALL, UNDO and MORE EDITS (the sound recorder: fades, effects,
// chop, name), HEAR and TRIM, and low under the thumb SEND TO PAD (the pad you came from) with PICK PAD (a 4 x 4 grid).
// The thumb bar's REC samples a new sound from the mic, at once (a one-shot: no count-in, no click, and the mic is
// never heard in the speaker, as in the sound recorder). HEAR plays IN to OUT, which is what SEND TO PAD puts on the
// pad (as the pad's part of the sound); TRIM cuts the sound itself to IN..OUT and every pad playing it plays the cut.
class SamplerScreen : public juce::Component, private juce::Timer
{
public:
    SamplerScreen (PocketHost&, PocketAudio&, pocket::Library&, pocket::Kit&);
    ~SamplerScreen() override;

    // What the app does for it
    std::function<void (int pad)> onPadSet;                 // a pad got a sound (or got its old one back): push the pads
    // A sound was saved after TRIM or UNDO: the pads playing `oldId` play `newId` (the same id, or your copy of a factory
    // sound), their parts moved by `shift` samples.
    std::function<void (const juce::String& oldId, const juce::String& newId, int shift)> onRepad;
    std::function<void()> onPickPad;                        // PICK PAD: the 4 x 4 grid
    std::function<void (const juce::String& soundId, int pad)> onMoreEdits;   // the sound recorder, on this sound
    std::function<int()> currentBank;
    std::function<void()> onSampleTool, onShowPads, onSaveSet;   // A TOOL; the steps' 4 PADS and 5 SAVE SET                     // A TOOL: a loop layer (any tool's take) or the loops mixed, onto the page

    void editForPad (int pad);                 // the pad's sound (its part as IN / OUT), aimed at the pad
    void loadSound (const juce::String& id);   // another sound (the target stays)
    void setTarget (int pad);
    int target() const { return targetPad; }
    bool hasSound() const { return wave.getAudio() != nullptr && wave.getAudio()->getNumSamples() > 0; }
    const juce::String& soundId() const { return currentId; }
    const crate::SharedAudio& sound() const { return wave.getAudio(); }
    juce::Range<int> selection() const { return wave.getSelection(); }
    void setSelection (juce::Range<int> r) { wave.setSelection (r); updateLabels(); }
    juce::String soundName() const { return name; }
    juce::Rectangle<int> waveBounds() const { return wave.getBounds(); }

    // The thumb bar's REC
    bool isRecording() const { return recording; }
    void startRecording();
    void stopRecording();
    double recordedSeconds() const;
    void useTake (crate::SharedAudio, const juce::String& takeName = {});   // a finished take onto the page (REC's stop, A TOOL, the self-checks)

    // The buttons (also for the self-checks)
    void hear();
    void trim();
    void undo();
    void selectAll();
    bool sendTo (int pad);                     // IN..OUT onto that pad; false when there's no sound
    void showSounds();                         // SOUNDS: your sounds in a card
    // CLEAR: an empty page for a new sound (UNDO brings it back; Sounds and the pads keep theirs)
    void clearSound();
    // CHOP TO PADS: cuts the sound at its hits (slices <= 0) or into `slices` equal parts and puts them on the bank's
    // pads from its pad 1 (the target pad's bank, or the bank on screen), one slice a pad. Each pad keeps its slice as
    // its part of the one sound, so its sample page opens that slice. Returns how many pads it filled.
    int chopToPads (int slices);
    void showChopMenu();                       // CHOP TO PADS: hits, or 4, 8 or 16 equal slices
    void leave();                              // another tool or page: stops a take and the playback
    // The steps along the top: 1 GET, 2 SHAPE, 3 CHOP, 4 PADS, 5 SAVE SET. The lit one is where you are; each runs its step.
    int currentStep() const { return ! hasSound() ? 0 : chopped ? 3 : 1; }
    void pressStep (int step);

    // Screenshots: a sound on screen as if it had just been recorded.
    void showDemo (crate::SharedAudio, const juce::String& soundName, juce::Range<int> sel, int pad);
    void clear();                               // nothing loaded

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct State { crate::SharedAudio audio; juce::Range<int> sel; juce::String id; int trimStart = 0; bool saved = false; juce::String name; };

    void timerCallback() override;
    bool needSound();
    bool ensureSaved();                         // a new take goes into Sounds (TRIM and SEND TO PAD need it there)
    void updateLabels();
    juce::String clockText (double seconds) const;

    PocketHost& host;
    PocketAudio& audio;
    pocket::Library& library;
    pocket::Kit& kit;

    PocketWave wave;
    pocket::PButton soundsButton { "Sounds" }, allButton { "All" }, undoButton { "Undo" }, moreButton { U8("More edits \xe2\x80\xba") },
        hearButton { "Hear", pocket::Look::green }, trimButton { "Trim" }, sendButton { "Send to pad", pocket::Look::amber },
        pickButton, clearButton { "Clear" }, chopButton { U8("Chop to pads\xe2\x80\xa6") }, toolButton { "A tool" }, steps[5];
    bool chopped = false;   // the sound on the page went onto the pads (CHOP TO PADS)
    std::vector<State> undoStack;
    juce::String currentId, name;
    int targetPad = -1;
    bool recording = false;
    juce::File takeFile;
    float level = 0.0f;
    juce::Rectangle<float> titleArea, infoArea;
};

//==============================================================================
// SEND TO PAD > PICK PAD: the four banks and their 4 x 4 pads, each with its sound; the pad you came from lit.
class SamplerPadPicker : public pocket::DragSheet
{
public:
    explicit SamplerPadPicker (pocket::Kit&);

    std::function<void (int pad)> onPick;
    std::function<void()> onCancel;
    void show (int targetPad, int bank, const juce::String& soundName);
    int bank() const { return shownBank; }
    juce::Rectangle<float> padRect (int padInBank) const;
    int wantedHeight() const { return 486; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int padAt (juce::Point<float>) const;
    pocket::Kit& kit;
    int target = -1, shownBank = 0, pressed = -1;
    juce::String what;
    pocket::PButton banks[4], cancel { "Cancel" };
};
