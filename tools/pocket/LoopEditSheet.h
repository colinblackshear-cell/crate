#pragma once

#include "PocketLoopTools.h"
#include "PocketLooper.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// The loop editor (a layer's IN / OUT, QUANTIZE or FIT, EFFECTS): two pages under the layer's wave, which stays
// in view on both.
//  SHAPE: the IN and OUT markers (drag them, or SET IN / SET OUT at the playhead while it plays), snapped by
//    SNAP (BAR, BEAT, 1/16, OFF = sample accurate); HEAR IN-OUT loops just that part before TRIM keeps it.
//    BEAT MATCH says how long the sound (or IN-OUT) really is at the song's tempo and FIT TO n BARS stretches it
//    to exactly n bars at the same pitch. A layer made from notes (PADS, SYNTH) shows QUANTIZE: 1/4, 1/8, 1/16,
//    50% / 100% and SWING re-render it from its notes. HALF, DOUBLE, REVERSE and UNDO.
//  EFFECTS: five knobs (volume, filter, delay, reverb, drive) and REC MOVES, which records the knobs turning as
//    the loop plays and plays the moves back every pass.
class LoopEditSheet : public pocket::DragSheet, private juce::Timer
{
public:
    enum Edit { trim, half, twice, reverse };
    enum Page { shapePage, effectsPage };
    enum Focus { noFocus, fitFocus, quantizeFocus };

    explicit LoopEditSheet (pocket::LoopsEngine&);

    void open (int layer, int page = shapePage, int focus = noFocus);
    void close();                 // lets go of HEAR IN-OUT and REC MOVES
    int layer() const { return current; }
    int page() const { return pageShown; }
    void showPage (int);
    void layerChanged();          // the loop was edited: new wave, markers back to the ends of its sound

    std::function<void (int edit, double fromBeat, double toBeat)> onEdit;
    std::function<void (int bars, double fromBeat, double toBeat)> onFit;
    std::function<void (const pocket::LoopQuantize&)> onQuantize;
    std::function<void()> onUndo, onDone, onPlay, onFxChanged;
    std::function<void()> onFullBoard;   // FULL BOARD: the EFFECTS tool on this layer's pedalboard
    std::function<void (const juce::String&)> onToast;
    std::function<bool()> isBusy;   // a FIT or QUANTIZE is being worked out
    std::function<void()> onHeightChanged;
    int preferredHeight() const { return pageShown == effectsPage ? 452 : (hasPart() ? 530 : 440); }

    // For the self-checks and screenshots: the markers, SNAP, and SET IN / SET OUT at the playhead.
    void setMarkers (double fromBeat, double toBeat) { trimFrom = fromBeat; trimTo = toBeat; updateRegion(); repaint(); }
    double markerIn() const { return trimFrom; }
    double markerOut() const { return trimTo; }
    void setSnap (int s);
    void setAtPlayhead (bool in);
    void pressTrim();
    void setHearing (bool on);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void layout();
    void refreshButtons();
    bool busy() const { return isBusy && isBusy(); }
    void updateRegion();
    juce::Rectangle<float> waveBox() const { return { 16.0f, 54.0f, (float) getWidth() - 32.0f, 116.0f }; }
    juce::Rectangle<float> waveArea() const;
    juce::Rectangle<float> laneArea() const { auto b = waveBox(); return { b.getX() + 8, b.getBottom() - 32, b.getWidth() - 16, 26 }; }
    juce::Rectangle<float> knobPanel() const { return { 16.0f, 178.0f, (float) getWidth() - 32.0f, 112.0f }; }
    juce::Rectangle<float> knobCell (int k) const;
    int knobAt (juce::Point<float>) const;
    float beatX (double beat) const;
    double xBeat (float x) const;
    double totalBeats() const;
    double contentBeats() const;        // the sound, without the silence a trim padded it with
    double regionBars() const;          // what BEAT MATCH talks about, in bars at the song's tempo
    bool markersMoved() const;
    bool hasPart() const;
    juce::String placeText (double beat) const;
    void startRecMoves();
    void fit (int which);               // 0 nearest, 1 smaller, 2 larger
    void quantize (int grid, int strength, int swing);   // -1 = as it is

    pocket::LoopsEngine& loops;
    int current = -1, version = -1, pageShown = shapePage, snap = 0, flash = 0, shownHeight = 0;
    std::vector<float> peaks;
    double trimFrom = 0, trimTo = 0;
    bool hearing = false;
    int dragging = 0;          // 1 = IN, 2 = OUT, 10 + k = knob k
    float dragStartValue = 0;
    int laneKnob = pocket::fxFilter;
    pocket::FitChoices choices;
    juce::Rectangle<float> fitBox, quantBox;

    pocket::PButton done { "Done", pocket::Look::amber }, tabShape { "Shape" }, tabFx { "Effects" };
    pocket::PButton snaps[4] { pocket::PButton ("Bar"), pocket::PButton ("Beat"), pocket::PButton ("1/16"), pocket::PButton ("Off") };
    pocket::PButton setIn { "Set in" }, setOut { "Set out" }, hear { U8("\xe2\x96\xb6 Hear in-out") }, trimButton { "Trim" };
    pocket::PButton fitButtons[3] { pocket::PButton ({}, pocket::Look::amber), pocket::PButton(), pocket::PButton() };
    pocket::PButton grids[3] { pocket::PButton ("1/4"), pocket::PButton ("1/8"), pocket::PButton ("1/16") };
    pocket::PButton swingButton { "Swing" }, half50 { "50%" }, full100 { "100%" };
    pocket::PButton halfButton { "Half" }, doubleButton { "Double" }, reverseButton { "Reverse" }, undo { "Undo" };
    pocket::PButton recMoves { U8("\xe2\x97\x8f Rec moves"), pocket::Look::red }, clearMoves { "Clear moves" }, undoFx { "Undo" };
    pocket::PButton fullBoard { U8("Full board \xe2\x80\xba") };
};
