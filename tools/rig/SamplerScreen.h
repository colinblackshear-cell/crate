#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Vintage.h"
#include "BankBrowser.h"
#include "NoteRepeatControls.h"

class EditorView;
class MpcView;

// The Sampler · MPC screen (F4): the MPC and the Sampler as one unit. The MPC's 16 pads are always
// on the right; the left is one of three sides: Chop · Pads (below), Sequencer · Kits (the MPC's
// sequencer, songs, kits and each pad's settings, which was the separate MPC page), and Edit ·
// Effects · Keys (the full sample editor). Across the top: the MPC's Beat on/off (like every
// instrument's pattern on the Rig's one clock) and Loop pads, which plays the pads into the looper.
//
// On the left, the Sampler's sound in a dark well: its name and length, how to chop it
// (Transients finds the hits, Even 16 cuts it in sixteen, By hand lets you click the ruler to
// add markers and drag them), Snap to zero, and the waveform with the slices numbered along the
// top. Click a slice to hear it and pick it. Below, the picked slice's pad: Pitch, Level, Pan,
// Mode and Choke, as the MPC has them. Next to it, Send to MPC2000XL writes the kits to the card.
//
// On the right, the 16 pads of the bank (A-D), laid out like the MPC (pad 1 bottom-left). They
// light when they play, from a click, the Launchkey's pads or the sequencer. "To pads" puts the
// slices on the pads, slice 1 on A01. The full sample editor (effects, fades, recording) is a
// click away.
class SamplerScreen : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      private juce::Timer
{
public:
    SamplerScreen (RigEngine&, EditorView& fullEditor);
    ~SamplerScreen() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    // Re-reads everything (after a show is loaded).
    void refresh();

    // The full sample editor (record, trim, fades, effects, keys, library) lives on this screen
    // too, a click away: Edit shows it, Chop shows the chop and the pads. Takes ownership.
    void ownEditor (juce::Component* fullEditor);
    // The MPC's own window (kits, sequencer, songs, pad settings), shown beside the pads on the
    // Sequencer · Kits side. Takes ownership.
    void ownMpc (MpcView*);
    MpcView* getMpcView() const { return mpcView; }

    enum Side { chopSide, mpcSide, editSide };
    void setSide (int);
    int getSide() const { return mode; }
    void setEditMode (bool on) { setSide (on ? editSide : chopSide); }
    bool isEditMode() const { return mode == editSide; }
    // Called when the side changes (so the Rig's Back and Forward know where you are).
    std::function<void()> onSideChanged;

    // Across the top, as a click does: the MPC's beat on or off; Loop pads (into the looper,
    // for `loopBars` bars, 0 until pressed again); and the picked pad's sound editor.
    void pressBeat();
    void pressLoopPads();
    int loopBars = 0;
    // Edit (and right-click > Open in the Sampler, or a triple-click): the picked pad's sound opens
    // alone in the Sampler's full editor (Edit · Effects · Keys), aimed at that pad. A bar across the
    // top says which pad, steps to the pad before or after, Back to pads, As a new sound, and Save to
    // A04. Leaving by any of those saves an edit onto the pad first, so nothing is lost.
    void pressEditPad();
    void openPadInSampler (int pad);   // 0..63 in the current kit
    bool isEditingPad() const { return editPad >= 0; }
    int getEditPad() const { return editPad; }
    bool padEditChanged() const;       // the Sampler holds an edit not on the pad yet
    void pressSaveToPad();
    void pressBackToPads();
    void pressStepPad (int delta);
    void pressPadAsNewSound();
    // The MPC's own small pad editor (trim, fades, effects), as Edit opened before.
    void pressQuickEditPad();
    // Sample a tool: records `source` (a RigCapture::Source) into the Sampler, from the next bar
    // (now when stopped), for `bars` (0: until pressed again). Pressed again while it waits or
    // records, it stops. The menu picks the tool and the length.
    void pressSampleTool (int source, int bars);
    void showSampleToolMenu();
    // Sets (Kits menu): bank 0..3 (-1: the whole kit) saved as a set in setsFolder; a set loaded onto
    // the bank on screen (a whole-kit set goes on a new kit), with Undo for a bank.
    juce::File setsFolder = SoundSet::macFolder();
    juce::File saveSet (int bank, const juce::String& name);
    bool loadSetFile (const juce::File& set);
    bool canUndoLoadSet() const;
    void pressUndoLoadSet();
    void askSaveSet (int bank);
    // The steps across the top: 1 Get, 2 Shape, 3 Chop, 4 Pads, 5 Save set. The lit one is where you are.
    int currentStep() const;
    void pressStep (int step);
    bool isSamplingTool() const;
    // Remove: takes the sound off the picked pad (its name, settings and effects with it). Until
    // something goes on that pad again, the button says Undo and puts it all back.
    void pressRemovePad();
    bool canUndoRemovePad() const;
    void pressUndoRemovePad();
    // Clear: empties the Sampler (the pads keep their sounds). Until something else goes in, the
    // button says Undo clear and brings the sound back (the editor's Undo does too).
    void pressClear();
    bool canUndoClear() const;
    // Kits (under the pads): the factory kits, your kits, and putting a factory kit's sounds back.
    void showKitsMenu();
    void pressRestoreFactoryPad();

    std::function<void (const juce::String&)> onSay;
    // Opens a page of the Rig ("Sampler editor", "MPC").
    std::function<void (const juce::String&)> onOpen;
    // Send to MPC: write every kit to the card (the MPC tool asks where).
    std::function<void()> onSendToMpc;

    // As a click does (the tests use these).
    enum ChopMode { transients, even16, byHand };
    void pressChop (int mode);
    void pressSnap();
    void pressToPads();
    void selectSlice (int slice);
    void pressPad (int padInBank);
    void pressBank (int bank);
    void addMarker (int sample);
    void removeMarker (int index);
    void moveMarker (int index, int sample);
    int getSelectedSlice() const { return selectedSlice; }
    int getSelectedPad() const;   // 0..63 in the kit
    int numSlices() const;
    juce::Range<int> sliceRange (int slice) const;

    // Capacity of the medium the kits go to, in bytes (0 = whatever the card holds).
    static size_t capacityOf (int medium);
    static juce::String mediumName (int medium);
    int medium = 0;

    std::vector<std::unique_ptr<vintage::Knob>> knobs;   // Pitch, Level, Pan, Mode, Choke
    // The pad's live sound, on the same row when FX is on: Filter, Reso, Decay, Delay, Reverb.
    std::vector<std::unique_ptr<vintage::Knob>> fxKnobs;
    void showFxKnobs (bool);
    bool isShowingFxKnobs() const { return fxShown; }

    // The pad tools under the pads: 16 Levels, Note repeat, and your sound bank (which shows
    // over the wave, for the picked pad).
    void setSixteenLevels (int type);
    void showSounds (bool);
    bool isShowingSounds() const { return soundsShown; }
    void setSoundBank (std::shared_ptr<SoundBank> b) { browser.setSoundBank (std::move (b)); }
    BankBrowser& getBrowser() { return browser; }

private:
    void timerCallback() override;
    void updateButtons();
    void updateKnobs();
    void setPadValue (int knob, double value);
    void setFxValue (int knob, double value);
    void updatePadTools();
    void rebuildPeaks();
    void play (juce::Range<int>);

    // Where things are drawn.
    juce::Rectangle<float> padRect (int padInBank) const;
    float xForSample (int sample) const;
    int sampleForX (float x) const;
    int markerAt (juce::Point<float>) const;
    int padAt (juce::Point<float>) const;

    RigEngine& engine;
    EditorView& editor;

    int selectedSlice = 0, chopMode = transients;
    juce::Range<double> view { 0.0, 1.0 };   // the part of the sound in the wave, as shares
    std::vector<float> peaks;                 // per pixel column of the whole sound, 0..1
    const void* peaksOf = nullptr;
    int peaksWidth = 0;
    std::array<juce::uint32, 16> flashUntil {};
    int dragMarker = -1, heldPad = -1;
    bool draggingScroll = false;
    double scrollGrab = 0;
    juce::String shownSignature;
    std::unique_ptr<juce::Component> ownedEditor, ownedMpc;
    MpcView* mpcView = nullptr;
    int mode = chopSide;
    juce::TextButton chopModeButton { juce::String::fromUTF8 ("Chop \xc2\xb7 Pads") },
        mpcModeButton { juce::String::fromUTF8 ("Sequencer \xc2\xb7 Kits") },
        editModeButton { juce::String::fromUTF8 ("Edit \xc2\xb7 Effects \xc2\xb7 Keys") },
        recordButton { "Rec sample" }, beatButton { "Beat on" }, loopPadsButton { "Loop pads" }, loopBarsButton { "Free" },
        editPadButton { "Edit" }, removePadButton { "Remove A01" }, sampleToolButton { juce::String::fromUTF8 ("Sample a tool \xe2\x96\xbe") };
    void updateSampleToolButton();
    Pad removedPad;
    int removedPadIndex = -1, removedPadKit = -1;
    void updateRemoveButton();
    void showPadMenu (int padInBank);
    void updateTopButtons();
    juce::Rectangle<int> modeRow;
    juce::TextButton stepButtons[5] { juce::TextButton ("1  Get"), juce::TextButton ("2  Shape"), juce::TextButton ("3  Chop"),
                                      juce::TextButton ("4  Pads"), juce::TextButton ("5  Save set") };
    juce::Rectangle<int> stepRow;
    void updateSteps();

    juce::TextButton chopButtons[3] { juce::TextButton ("Transients"), juce::TextButton ("Even 16"), juce::TextButton ("By hand") };
    juce::TextButton snapButton { "Snap to zero" }, toPadsButton { "To pads" }, openButton { "Open" }, editorButton { "Editor" },
        zoomOut { juce::String::fromUTF8 ("\xe2\x88\x92") }, zoomIn { "+" }, fullLevel { "Full level" }, sendButton { "Send to MPC" },
        clearButton { "Clear" };
    juce::TextButton levelsButton { "16 Levels" }, soundsButton { "Sounds" }, fxButton { "FX" }, kitsButton { "Kits" };
    // NOTE REPEAT: the red hold button (double-tap to latch) and its rates, shared with every pad screen.
    noterepeatui::HoldButton repeatButton { engine.getMpc().getSequencer() };
    noterepeatui::RateButtons rateButtons { engine.getMpc().getSequencer() };
    void showRepeatLearnMenu (juce::Point<int> screenPosition);
    void updateClearButton();
    std::array<Pad, kPadsPerBank> setUndoPads;
    int setUndoKit = -1, setUndoBank = -1;
    juce::String setUndoName;
    // The pad open in the Sampler (-1: none), its kit, and what the Sampler held when it opened.
    int editPad = -1, editKit = -1;
    const void* editOpenedAudio = nullptr;
    juce::String editName;
    juce::Rectangle<int> padBar;
    juce::TextButton padPrevButton { "<" }, padNextButton { ">" }, padBackButton { "Back to pads" },
        padNewButton { "As a new sound" }, padSaveButton { "Save to A01" };
    bool saveEditIfChanged();
    void updatePadBar();
    juce::String editLabel() const;
    BankBrowser browser { engine.getMpc() };
    bool fxShown = false, soundsShown = false;
    juce::TextButton bankButtons[4] { juce::TextButton ("A"), juce::TextButton ("B"), juce::TextButton ("C"), juce::TextButton ("D") };
    juce::TextButton mediumButtons[3] { juce::TextButton (juce::String::fromUTF8 ("USB \xe2\x86\x92 CF card")), juce::TextButton ("Zip disk"),
                                        juce::TextButton ("Floppy") };

    juce::Rectangle<int> leftArea, waveWell, titleRow, waveArea, rulerArea, zoomRow, scrollArea, sliceBox, sliceTitle, sendWell,
        sendTitle, sendInfo, padsHeader, padsWell, padsNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerScreen)
};
