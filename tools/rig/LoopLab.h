#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "LoopEditor.h"
#include "RigEngine.h"

#include <optional>

// Loop Lab: a looper layer opened to reshape it (double-click its wave, or its ... menu).
// Pages along the top:
//   WARP      the hits it finds, warp markers you drag onto the beat lines, SNAP TO BEAT with a
//             strength, FIT TO n bars, FOLLOW SONG TEMPO, how it stretches (Voice, Beats,
//             Texture, Tape), transpose, fine tune and formant (RigLoopLab does the work).
//   IN / OUT  where it starts and ends, slip, nudge, level, fades and reverse (LoopEditor).
// And for every page: ORIGINAL (hear the recording as it was), UNDO, PRINT (the result on the
// next empty layer) and DONE.
class LoopLab : public juce::Component
{
public:
    enum Page { warpPage, inOutPage };

    LoopLab (RigEngine&, int layer, Page = warpPage);
    ~LoopLab() override;

    int getLayer() const { return layer; }
    // False when the layer has no loop any more (it was cleared): the lab should close.
    bool isValid() const;
    Page getPage() const { return page; }
    void showPage (Page);
    LoopEditor* getInOut() const { return inOut.get(); }
    static Page lastPageUsed();

    void tick();   // ~30 Hz from the looper window

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void (const juce::String&)> onSay;

    // For the tests: the controls, as if clicked.
    void pressStyle (crate::Warp::Style);
    void pressSnap();
    void pressFollow();
    void pressOriginal();
    void pressUndo();
    int pressPrint();
    void pressFit (double beats);
    void setTranspose (float semitones, float cents);
    void setFormant (float semitones);
    // A marker dragged on the wave, as if dragged and let go (index, to a target beat).
    void dragMarker (int index, double targetBeat);
    void addMarkerAt (double targetBeat);
    static juce::String gridName (double beats);

private:
    class Wave;
    void refresh();
    void refreshLabels();
    void commitPitch();
    void say (const juce::String&);
    RigLoopLab& lab();
    void fillFitMenu();

    RigEngine& engine;
    const int layer;
    Page page = warpPage;
    bool opened = false;
    juce::String lastStamp;

    juce::Label title, info, hint;
    juce::TextButton warpTab { "Warp" }, inOutTab { "In / Out" };
    juce::TextButton original { "Original" }, undoButton { "Undo" }, printButton { "Print" }, done { "Done" };

    // WARP
    std::unique_ptr<Wave> wave;
    juce::Label findLabel, strengthLabel, transposeLabel, fineLabel, formantLabel, styleLabel;
    juce::Slider sensitivity, strength, transpose, fine, formant;
    juce::TextButton snapButton { "Snap to beat" }, followButton { "Follow song tempo" }, resetButton { "Reset warp" };
    juce::ComboBox gridBox, fitBox;
    std::array<juce::TextButton, 4> styleButtons;

    // IN / OUT
    std::unique_ptr<LoopEditor> inOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopLab)
};
