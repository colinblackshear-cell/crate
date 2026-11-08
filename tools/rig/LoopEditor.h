#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"

// Edits where a looper layer's loop starts and ends. It shows the whole loop as you recorded
// it, with a green handle where it starts and a red one where it ends: drag them, or drag the
// middle to slide the part that plays. They snap to bars, beats or 16th notes, or go anywhere
// (Free). The loop changes as you let go, so you hear it right away while the Rig plays, and
// it always stays a whole number of beats long so it keeps in time. "Whole loop" puts it back.
class LoopEditor : public juce::Component
{
public:
    LoopEditor (RigEngine&, int layer);
    ~LoopEditor() override;

    int getLayer() const { return layer; }
    // False when the layer has no loop (it was cleared): the editor should close.
    bool isValid() const { return valid; }

    void tick();   // ~30 Hz from the looper window: the playhead and the Play button

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

    // The snapping, as the menu lists it: bars, beats, 16ths or free.
    enum Snap { snapBars, snapBeats, snapSixteenths, snapFree };
    static double snapUnit (Snap, int beatsPerBar);   // in beats; 0 for free
    // How many beats the part between two beats plays for: whole beats, at least one, so the
    // loop stays on the beat (a free trim plays a touch faster or slower to fit).
    static double playBeatsFor (double start, double end);
    // "bar 2 beat 3" (and the 16th, when it isn't on a beat).
    static juce::String placeText (double beat, int beatsPerBar);

    // For the tests: move the handles to these beats, as if dragged there and let go.
    void setRange (double start, double end);

private:
    class Wave;
    void apply();
    void nudge (bool startHandle, int direction);
    void setLength (double beatsLong);
    void updateText();

    RigEngine& engine;
    const int layer;
    bool valid = false;
    double start = 0, end = 0;   // in beats of the whole loop

    std::unique_ptr<Wave> wave;
    juce::Label title, info, hint;
    juce::ComboBox snap, length;
    juce::TextButton whole { "Whole loop" }, play { "Play" }, done { "Done" };
    juce::TextButton startLeft { "<" }, startRight { ">" }, endLeft { "<" }, endRight { ">" };
    juce::Label startLabel, endLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopEditor)
};
