#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "Vintage.h"

class EditorView;

// The Sampler · MPC screen (F4): chop a sound and play it from the MPC's pads, on one screen.
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

private:
    void timerCallback() override;
    void updateButtons();
    void updateKnobs();
    void setPadValue (int knob, double value);
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

    juce::TextButton chopButtons[3] { juce::TextButton ("Transients"), juce::TextButton ("Even 16"), juce::TextButton ("By hand") };
    juce::TextButton snapButton { "Snap to zero" }, toPadsButton { "To pads" }, openButton { "Open" }, editorButton { "Editor" },
        zoomOut { juce::String::fromUTF8 ("\xe2\x88\x92") }, zoomIn { "+" }, fullLevel { "Full level" }, sendButton { "Send to MPC" };
    juce::TextButton bankButtons[4] { juce::TextButton ("A"), juce::TextButton ("B"), juce::TextButton ("C"), juce::TextButton ("D") };
    juce::TextButton mediumButtons[3] { juce::TextButton (juce::String::fromUTF8 ("USB \xe2\x86\x92 CF card")), juce::TextButton ("Zip disk"),
                                        juce::TextButton ("Floppy") };

    juce::Rectangle<int> leftArea, waveWell, titleRow, waveArea, rulerArea, zoomRow, scrollArea, sliceBox, sliceTitle, sendWell,
        sendTitle, sendInfo, padsHeader, padsWell, padsNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplerScreen)
};
