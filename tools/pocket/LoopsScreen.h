#pragma once

#include "PocketHost.h"
#include "PocketLibrary.h"
#include "PocketLooper.h"
#include "PocketSettings.h"
#include "PocketTheme.h"

// The Loops tab: a big REC button and display, where the loop comes from (mic, drums, vocoder, mix),
// how long it is, and eight layers you record, dub, mute and clear. Loops are saved to Documents/Loops.
class LoopsScreen : public juce::Component, private juce::Timer
{
public:
    LoopsScreen (PocketHost&, pocket::Library&, pocket::BeatsEngine&, pocket::AppSettings&);
    ~LoopsScreen() override;

    pocket::LoopsEngine& engine() { return loops; }
    int layerCount() const;                 // layers that hold a loop
    void leave();                           // another tab (not Beats) was opened: stop the clock and the loops
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Screenshot mode: layers 1-2 playing, 3 recording, 4 waiting for the next bar, 5 muted, the rest empty.
    void showDemo();
    // Records a made-up source into a 2-bar layer at 97 BPM and checks its length, its latency
    // compensation and that it loops with no gap. Returns what was wrong (empty = fine).
    static juce::String selfCheck (juce::String& report);

private:
    struct Undo { struct One { int layer; pocket::SharedTake before; bool mute; }; std::vector<One> items; };
    struct Row { pocket::LoopState st = pocket::LoopState::empty; bool muted = false; int version = -1; std::vector<float> peaks; };
    enum Id { rec = 1, srcBase = 10, lenBase = 20, lcd = 30, undoId = 31, clearId = 32, clearAllId = 33, toMacId = 34,
              rowSelect = 100, rowAct = 200, rowMute = 300 };

    void timerCallback() override;
    void refresh();
    int idAt (juce::Point<float>) const;
    void press (int id);
    void startRecording (int layer);
    void pushUndo (Undo);
    void undo();
    void clearLayer (int layer, bool withUndo);
    void clearAll();
    void saveIfChanged();
    void save();
    void restore();
    int barsForNext() const;
    juce::File folder() const { return library.getRoot().getChildFile ("Loops"); }
    juce::Rectangle<float> rowRect (int l) const;
    juce::Rectangle<float> actRect (int l) const;
    juce::Rectangle<float> muteRect (int l) const;
    juce::Rectangle<float> sourceCell (int i) const;
    juce::Rectangle<float> lengthCell (int i) const;
    juce::Rectangle<float> lcdRect() const { return { 102, 12, (float) getWidth() - 117, 76 }; }
    juce::Rectangle<float> recRect() const { return { 16, 12, 76, 76 }; }
    juce::Rectangle<float> undoRect() const { return { 16, 584, 59, 43 }; }
    juce::Rectangle<float> clearRect() const { return { 84, 584, 67, 43 }; }
    juce::Rectangle<float> clearAllRect() const { return { 160, 584, 96, 43 }; }
    juce::Rectangle<float> toMacRect() const { return { (float) getWidth() - 90, 584, 74, 43 }; }
    void drawLcd (juce::Graphics&);
    void drawRow (juce::Graphics&, int l);
    void drawChoice (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool on, bool dim = false);

    PocketHost& host;
    pocket::Library& library;
    pocket::BeatsEngine& beats;
    pocket::AppSettings& settings;
    pocket::LoopsEngine loops;

    std::array<Row, pocket::kLoopLayers> rows;
    std::vector<Undo> undoStack;
    int selected = -1, lastLayer = -1;
    int sourceChoice = 0;   // pocket::LoopSource
    int lengthChoice = 2;   // 0..3 = 1, 2, 4, 8 bars, 4 = FREE
    int pressed = 0;
    int ticks = 0;
    bool demo = false;

    struct Saved { const pocket::LoopTake* take = nullptr; int version = -1; bool present = false, mute = false; };
    std::array<Saved, pocket::kLoopLayers> saved;
    bool settingsDirty = false;
    float inputLevel = 0.0f;
};
