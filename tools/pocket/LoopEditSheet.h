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
//  WARP: the take's timing reshaped on Loop Lab's warp markers: drag a marker onto a beat, tap a hit to pin it, SNAP
//    pulls every hit toward the grid, FIT n BARS, a stretch style, pitch and formant, ORIGINAL, PRINT, RESET.
//  EFFECTS: five knobs (volume, filter, delay, reverb, drive) and REC MOVES, which records the knobs turning as
//    the loop plays and plays the moves back every pass.
class LoopEditSheet : public pocket::DragSheet, private juce::Timer
{
public:
    enum Edit { trim, half, twice, reverse };
    enum Page { shapePage, effectsPage, warpPage };
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
    std::function<void (const crate::Warp::Plan&)> onWarp;   // the plan to render the layer by
    std::function<void (bool)> onOriginal;                   // ORIGINAL: the recording as it was (true) or the warp (false)
    std::function<bool()> isOriginal;
    std::function<int()> onPrint;                            // PRINT: the layer it landed on, -1 when no layer is empty
    std::function<bool()> isBusy;   // a FIT or QUANTIZE is being worked out
    std::function<void()> onHeightChanged;
    int preferredHeight() const { return pageShown == warpPage ? 504 : pageShown == effectsPage ? 452 : (hasPart() ? 530 : 440); }

    // For the self-checks and screenshots: the markers, SNAP, and SET IN / SET OUT at the playhead.
    void setMarkers (double fromBeat, double toBeat) { trimFrom = fromBeat; trimTo = toBeat; updateRegion(); repaint(); }
    double markerIn() const { return trimFrom; }
    double markerOut() const { return trimTo; }
    void setSnap (int s);
    void setAtPlayhead (bool in);
    void pressTrim();
    void setHearing (bool on);

    // The WARP page, for the self-checks: each does what its button does.
    void pressWarpSnap();
    void pressStyle (crate::Warp::Style);
    void pressFitBars (int bars);
    void stepPitch (int semis);
    void stepFormant (int semis);
    void dragWarpMarker (int index, double targetBeat);
    void pressOriginal();
    int pressPrint();
    const crate::Warp::Plan& warpPlan() const { return plan; }
    int hitCount() const { return (int) hits.size(); }

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
    // WARP
    void readWarp();                    // the plan, hits and what it sounds like, from the take
    void commitWarp();                  // the plan changed: tell the screen, redraw
    void resnap();                      // SNAP's markers again (hits, grid, strength or length changed)
    void stepFine (int cents);
    void stepHits (int dir);
    void setGrid (int i);
    void setStrength (int i);
    void resetWarp();
    bool warpHere (juce::Rectangle<float>) const;
    int markerNear (float x, const crate::Warp::Plan&) const;
    int hitNear (float x) const;
    double gridSnapped (double beat) const;
    juce::String infoText() const;
    juce::String pitchText() const;
    void paintWarp (juce::Graphics&, juce::Rectangle<float> wave);

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

    // WARP page state (read again from the take whenever it changes)
    pocket::WarpSource warpSrc;
    crate::Warp::Plan plan, dragPlan;
    std::vector<double> hits;
    double gridBeats = 0.25, soundsLike = 0.0;
    float strength = 0.75f, sensitivity = 0.6f;
    bool snapOn = false;
    int warpDrag = -1, tapHit = -1;
    juce::Rectangle<float> infoRect, hitsRect, fitLabel, stretchLabel, pitchRect, formantRect, hintRect;

    pocket::PButton done { "Done", pocket::Look::amber }, tabShape { "Shape" }, tabFx { "Effects" };
    pocket::PButton snaps[4] { pocket::PButton ("Bar"), pocket::PButton ("Beat"), pocket::PButton ("1/16"), pocket::PButton ("Off") };
    pocket::PButton setIn { "Set in" }, setOut { "Set out" }, hear { U8("\xe2\x96\xb6 Hear in-out") }, trimButton { "Trim" };
    pocket::PButton fitButtons[3] { pocket::PButton ({}, pocket::Look::amber), pocket::PButton(), pocket::PButton() };
    pocket::PButton grids[3] { pocket::PButton ("1/4"), pocket::PButton ("1/8"), pocket::PButton ("1/16") };
    pocket::PButton swingButton { "Swing" }, half50 { "50%" }, full100 { "100%" };
    pocket::PButton halfButton { "Half" }, doubleButton { "Double" }, reverseButton { "Reverse" }, undo { "Undo" };
    pocket::PButton recMoves { U8("\xe2\x97\x8f Rec moves"), pocket::Look::red }, clearMoves { "Clear moves" }, undoFx { "Undo" };
    pocket::PButton fullBoard { U8("Full board \xe2\x80\xba") };

    pocket::PButton tabWarp { "Warp" }, warpSnap { "Snap" }, warpUndo { "Undo" }, hitsMinus { U8("\xe2\x88\x92") }, hitsPlus { "+" };
    pocket::PButton gridChips[4] { pocket::PButton ("Beat"), pocket::PButton ("1/8"), pocket::PButton ("1/16"), pocket::PButton ("1/32") };
    pocket::PButton strengthChips[3] { pocket::PButton ("50%"), pocket::PButton ("75%"), pocket::PButton ("100%") };
    pocket::PButton fitBars[4] { pocket::PButton ("1"), pocket::PButton ("2"), pocket::PButton ("4"), pocket::PButton ("8") };
    pocket::PButton styles[4] { pocket::PButton ("Voice"), pocket::PButton ("Beats"), pocket::PButton ("Texture"), pocket::PButton ("Tape") };
    pocket::PButton pitchMinus { U8("\xe2\x88\x92 st") }, pitchPlus { "+ st" }, fineMinus { U8("\xe2\x88\x92 ct") }, finePlus { "+ ct" };
    pocket::PButton formantMinus { U8("\xe2\x88\x92 st") }, formantPlus { "+ st" };
    pocket::PButton originalButton { "Original" }, printButton { "Print" }, resetButton { "Reset" };
};
