#pragma once

#include "PocketHost.h"
#include "PocketLibrary.h"
#include "LoopEditSheet.h"
#include "PocketLooper.h"
#include "PocketSettings.h"
#include "PocketTheme.h"

// The LOOPS tool: where a take comes from (mic, drums, vocoder, mix) and eight layers you dub, mute, edit
// and clear. The thumb bar's REC records the next take (recordTake); its length and count-in are the Rec
// setup's. Loops are saved to the idea's Loops folder.
class LoopsScreen : public juce::Component, private juce::Timer
{
public:
    LoopsScreen (PocketHost&, pocket::Library&, pocket::BeatsEngine&, pocket::AppSettings&);
    ~LoopsScreen() override;

    pocket::LoopsEngine& engine() { return loops; }
    int layerCount() const;                 // layers that hold a loop
    // The Vocoder tab's TO LOOP: records the vocoder on the next empty layer, from the next bar, for the chosen
    // LENGTH. Returns the layer, or -1 when all eight are used (it says so itself).
    int recordVocoder();
    // REC: records `source` (a pocket::LoopSource) on the next empty layer from the next bar, for
    // `bars` bars (0 = FREE: until stopped, or the first layer's length). -1 when it can't (it says why).
    int recordFrom (int source, int bars);
    // REC on the LOOPS tool: the source picked here, for `bars` bars.
    int recordTake (int bars) { return recordFrom (sourceChoice, bars); }
    int source() const { return sourceChoice; }   // pocket::LoopSource
    // Arms `layer` with an undo step (LOCK BEAT and REC on DRUMS use it). False when it couldn't.
    bool armLayer (int layer, int source, int bars) { return startRecording (layer, source, bars); }
    // The layer the next take lands on: the picked empty layer, else the first empty one. -1 = all used.
    int nextLayer() const;
    // A layer that is waiting for its bar or recording (-1 = none). stopTake ends it (at the bar) or calls it off.
    int busyLayer() const;
    void stopTake (int layer);
    // UNDO: the last take or edit. Says "Nothing to undo" when there's none.
    void undo();
    // The layer UNDO would take off when the last step is a new take on it, else -1.
    int lastTakeUndoLayer() const
    {
        if (undoStack.empty() || undoStack.back().items.size() != 1 || undoStack.back().items[0].before != nullptr)
            return -1;
        return undoStack.back().items[0].layer;
    }
    std::function<void()> onSourceChanged;
    // The layer a vocoder take is waiting or recording on (-1 = none). stopVocoder() ends it at the bar.
    int vocoderLayer() const;
    void stopVocoder();
    void leave();                           // another tab (not Beats) was opened: stop the clock and the loops
    void flush() { save(); }                // before switching ideas
    void reloadIdea();
    void sendToMac (bool onlyPicked);
    // The tour's spotlights: 0 = the first three layers, 1 = TO MAC.
    juce::Rectangle<int> tourSpot (int which) const { return (which == 0 ? rowRect (0).getUnion (rowRect (2)) : toMacRect()).toNearestInt(); }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Screenshot mode: layers 1-2 playing, 3 recording, 4 waiting for the next bar, 5 muted, the rest empty.
    void showDemo();
    // Screenshot mode: the loop editor open on layer 2, with filter moves recorded and some reverb.
    void showEditorDemo();
    // Records a made-up source into a 2-bar layer at 97 BPM and checks its length, its latency
    // compensation and that it loops with no gap. Returns what was wrong (empty = fine).
    static juce::String selfCheck (juce::String& report);

private:
    struct Undo { struct One { int layer; pocket::SharedTake before; bool mute; bool withFx = false; pocket::LayerFxState fx {}; }; std::vector<One> items; };
    struct Row { pocket::LoopState st = pocket::LoopState::empty; bool muted = false; int version = -1; std::vector<float> peaks; };
    enum Id { srcBase = 10, clearId = 32, clearAllId = 33, toMacId = 34, rowSelect = 100, rowAct = 200, rowMute = 300, rowEdit = 400 };

    void timerCallback() override;
    void refresh();
    int idAt (juce::Point<float>) const;
    void press (int id);
    bool startRecording (int layer, int source = -1, int bars = -1);
    void pushUndo (Undo);
    void clearLayer (int layer, bool withUndo);
    void clearAll();
    void openEditor (int layer);
    void closeEditor();
    void editLayer (int layer, int edit, double fromBeat, double toBeat);   // LoopEditSheet::Edit
    static juce::uint32 fxHash (const pocket::LayerFxState&);
    void saveIfChanged();
    void save();
    void restore();
    juce::File folder() const { return library.ideaRoot().getChildFile ("Loops"); }
    juce::Rectangle<float> rowRect (int l) const;
    juce::Rectangle<float> actRect (int l) const;
    juce::Rectangle<float> muteRect (int l) const;
    juce::Rectangle<float> editRect (int l) const;
    juce::Rectangle<float> sourceCell (int i) const;
    float buttonsY() const { return (float) getHeight() - 52.0f; }
    juce::Rectangle<float> clearRect() const { return { 16, buttonsY(), 96, 44 }; }
    juce::Rectangle<float> clearAllRect() const { return { 120, buttonsY(), 116, 44 }; }
    juce::Rectangle<float> toMacRect() const { return { (float) getWidth() - 112, buttonsY(), 96, 44 }; }
    int editorTop() const { return juce::jmax (60, getHeight() - 452); }
    void drawRow (juce::Graphics&, int l);
    void drawChoice (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool on, bool dim = false);

    PocketHost& host;
    pocket::Library& library;
    pocket::BeatsEngine& beats;
    pocket::AppSettings& settings;
    pocket::LoopsEngine loops;

    LoopEditSheet editor { loops };
    std::array<Row, pocket::kLoopLayers> rows;
    std::vector<Undo> undoStack;
    int selected = -1, lastLayer = -1, vocLayer = -1;
    int sourceChoice = 0;   // pocket::LoopSource
    int pressed = 0;
    int ticks = 0;
    bool demo = false;

    struct Saved { const pocket::LoopTake* take = nullptr; int version = -1; bool present = false, mute = false; juce::uint32 fx = 0; };
    std::array<Saved, pocket::kLoopLayers> saved;
    bool settingsDirty = false;
    float inputLevel = 0.0f;
};
