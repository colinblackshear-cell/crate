#pragma once

#include "PocketLooper.h"
#include "PocketTheme.h"

// The loop editor (EDIT on a loop layer): the layer's wave with trim handles, TRIM / HALF / DOUBLE /
// REVERSE, five effect knobs (volume, filter, delay, reverb, drive) and REC MOVES, which records the
// knobs turning as the loop plays and plays the moves back every pass. It slides up over the layer list;
// the header's Play/Stop and BPM stay in reach.
class LoopEditSheet : public juce::Component, private juce::Timer
{
public:
    enum Edit { trim, half, twice, reverse };

    explicit LoopEditSheet (pocket::LoopsEngine&);

    void open (int layer);
    int layer() const { return current; }
    void layerChanged();   // the loop was edited: new wave, handles back to the ends

    std::function<void (int edit, double fromBeat, double toBeat)> onEdit;
    std::function<void()> onUndo, onDone, onPlay, onFxChanged;
    std::function<void (const juce::String&)> onToast;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<float> waveBox() const { return { 16.0f, 52.0f, (float) getWidth() - 32.0f, 118.0f }; }
    juce::Rectangle<float> waveArea() const { auto b = waveBox(); return { b.getX() + 10, b.getY() + 30, b.getWidth() - 20, 48 }; }
    juce::Rectangle<float> laneArea() const { auto b = waveBox(); return { b.getX() + 8, b.getBottom() - 34, b.getWidth() - 16, 28 }; }
    juce::Rectangle<float> knobPanel() const { return { 16.0f, 224.0f, (float) getWidth() - 32.0f, 112.0f }; }
    juce::Rectangle<float> knobCell (int k) const;
    int knobAt (juce::Point<float>) const;
    float beatX (double beat) const;
    double xBeat (float x) const;
    double totalBeats() const;
    void startRecMoves();

    pocket::LoopsEngine& loops;
    int current = -1, version = -1;
    std::vector<float> peaks;
    double trimFrom = 0, trimTo = 0;
    int dragging = 0;          // 1 = start handle, 2 = end handle, 10 + k = knob k
    float dragStartValue = 0;
    int laneKnob = pocket::fxFilter;

    pocket::PButton done { "Done", pocket::Look::amber }, trimButton { "Trim" }, halfButton { "Half" }, doubleButton { "Double" },
        reverseButton { "Reverse" }, recMoves { U8("\xe2\x97\x8f Rec moves"), pocket::Look::red }, clearMoves { "Clear moves" }, undo { "Undo" };
};
