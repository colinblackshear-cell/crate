#pragma once

#include "PocketFrame.h"
#include "PocketHost.h"
#include "PocketLibrary.h"
#include "LoopEditSheet.h"
#include "PocketLoopTools.h"
#include "PocketLooper.h"
#include "PocketSettings.h"
#include "PocketTheme.h"

// The LOOPS tool: where a take comes from (mic, drums, vocoder, mix), a bar ruler with the playhead, and eight
// layers, each showing the tool that made it (its icon and colour), its wave (or its notes) and M. Tap a layer and
// the panel under the rows works on it: IN / OUT, QUANTIZE (layers made from notes) or FIT (audio), EFFECTS, DUB,
// OPEN <TOOL>, CLEAR, TO MAC, CLEAR ALL. The thumb bar's REC records the next take (recordTake); its length and
// count-in are the Rec setup's. Loops (and the notes of the ones made from notes) are saved to the song's Loops folder.
//
// Tempo: a layer plays locked to the song's beat. When the BPM changes, each layer is stretched to the new tempo
// a moment later, keeping its pitch (pocket::tempoTake); until that lands it plays faster or slower.
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
    int recordFrom (int source, int bars, int tool = -1, int tap = 0);   // tap: the layer a LoopSource::layerTap take records
    // REC on the LOOPS tool: the source picked here, for `bars` bars.
    int recordTake (int bars) { return recordFrom (sourceChoice, bars); }
    int source() const { return sourceChoice; }   // pocket::LoopSource
    void setSource (int s) { if (juce::isPositiveAndBelow (s, 4) && s != sourceChoice) { sourceChoice = s; settingsDirty = true; if (onSourceChanged) onSourceChanged(); repaint(); } }
    // Arms `layer` with an undo step (LOCK BEAT and REC on DRUMS use it). False when it couldn't. tool: a LayerTool.
    bool armLayer (int layer, int source, int bars, int tool = -1) { return startRecording (layer, source, bars, tool); }
    // The layer picked on the panel (-1 = none), so EFFECTS opens on its board.
    int selectedLayer() const { return selected >= 0 && hasLayer (selected) ? selected : -1; }
    bool hasLayer (int l) const;
    // EFFECTS' FULL BOARD on the loop editor's effects page.
    std::function<void (int layer)> onFullBoard;
    // A layer's board changed (EFFECTS): saved with the layers.
    void boardsChanged() { saveIfChanged(); }
    // The notes a layer was printed from (PADS, SYNTH), so QUANTIZE can render it again. Kept with the layer.
    void attachPart (int layer, std::shared_ptr<const pocket::NotePart>);
    // The pads' sounds, for rendering a PADS layer's notes again after the song is opened.
    std::function<std::vector<pocket::PadSetup>()> kitSounds;
    // OPEN <TOOL> on the panel.
    std::function<void (pocket::Tool)> onOpenTool;
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
    // A layer's loop as a sound at 44.1 kHz, with its effects and board when it has any (nullptr: no loop), its name
    // ("Pocket loop 3 Synth") and length in beats. The whole layers mixed: layer -1.
    crate::SharedAudio layerSound (int layer, juce::String* name = nullptr, double* beats = nullptr) const;
    int layersWithLoops() const;
    // The tour's spotlights: 0 = the first six layers, 1 = TO MAC.
    juce::Rectangle<int> tourSpot (int which) const;

    // The editor's work, also for the self-checks. FIT and QUANTIZE run on a worker thread (at once with
    // setSyncJobs, for the checks and screenshots); each is one UNDO step and keeps the effects and moves.
    void selectLayer (int l) { selected = lastLayer = l; layoutPanel(); repaint(); }
    void openEditor (int layer, int page = LoopEditSheet::shapePage, int focus = LoopEditSheet::noFocus);
    void closeEditor();
    LoopEditSheet& editorSheet() { return editor; }
    const LoopEditSheet& editorSheet() const { return editor; }
    void trimLayer (int layer, double fromBeat, double toBeat) { editLayer (layer, LoopEditSheet::trim, fromBeat, toBeat); }
    void fitLayer (int layer, int bars, double fromBeat, double toBeat);
    void quantizeLayer (int layer, const pocket::LoopQuantize&);
    void setSyncJobs (bool s) { syncJobs = s; }
    bool working() const { return jobLayer >= 0; }
    void followTempo();                     // stretches layers that aren't at the song's tempo (the timer does this)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Press and hold a filled layer: the hold menu (the same actions as the panel's buttons, one press away). Picks the
    // layer first, so the panel shows it too.
    void openLayerMenu (int layer);
    pocket::HoldSpec layerMenuSpec (int layer);
    void holdLayer (int layer) { openLayerMenu (layer); }   // the self-checks

    // Screenshot mode: a drums layer, a synth part (selected), a vocoder take 4.2 bars long, a pads pattern,
    // a muted mic take, then the row the next take lands on.
    void showDemo();
    bool demoMode() const { return demo; }
    void setDemoMode (bool d) { demo = d; }   // the checks: saving on again after the screenshots' demo
    // Screenshot mode: the loop editor on the vocoder take (SHAPE), the synth part (SHAPE, quantized), or EFFECTS.
    void showEditorDemo (int which);
    // Records a made-up source into a 2-bar layer at 97 BPM and checks its length, its latency
    // compensation and that it loops with no gap. Returns what was wrong (empty = fine).
    static juce::String selfCheck (juce::String& report);

private:
    struct Undo { struct One { int layer; pocket::SharedTake before; bool mute; bool withFx = false; pocket::LayerFxState fx {}; double start = 0.0; juce::var board {}; }; std::vector<One> items; };
    struct Row { pocket::LoopState st = pocket::LoopState::empty; bool muted = false; int version = -1; std::vector<float> peaks; };
    enum Id { srcBase = 10, rowSelect = 100, rowMute = 300 };
    enum PanelButton { pQuant, pInOut, pFx, pDub, pTool, pClear, pToMac, pClearAll, kPanelButtons };

    void timerCallback() override;
    void refresh();
    int idAt (juce::Point<float>) const;
    void press (int id);
    void pressPanel (int which);
    bool startRecording (int layer, int source = -1, int bars = -1, int tool = -1, int tap = 0);
    void pushUndo (Undo);
    void clearLayer (int layer, bool withUndo);
    void clearAll();
    void editLayer (int layer, int edit, double fromBeat, double toBeat);   // LoopEditSheet::Edit
    void applyEdited (int layer, const pocket::LoopTake* was, pocket::Edited);
    // FIT, QUANTIZE and the tempo follower: `work` makes the new take off the message thread, `apply` puts it on.
    void runJob (int layer, std::function<pocket::Edited()> work, bool withUndo);
    void jobDone (int id, pocket::Edited);
    static juce::uint32 fxHash (const pocket::LayerFxState&);
    void saveIfChanged();
    void save();
    void restore();
    juce::File folder() const { return library.ideaRoot().getChildFile ("Loops"); }
    void layoutPanel();
    juce::Rectangle<float> panelRect() const { return { 16.0f, (float) getHeight() - 140.0f, (float) getWidth() - 32.0f, 134.0f }; }
    float rowsTop() const { return 66.0f; }
    juce::Rectangle<float> rowRect (int l) const;
    juce::Rectangle<float> muteRect (int l) const;
    juce::Rectangle<float> waveRect (int l) const;
    juce::Rectangle<float> rulerRect() const;
    juce::Rectangle<float> sourceCell (int i) const;
    double rulerBeats() const;              // the longest layer (at least a bar)
    double rulerStart() const;              // the clock beat the ruler's left edge is on
    int editorTop() const { return juce::jmax (4, getHeight() - editor.preferredHeight()); }
    void drawRow (juce::Graphics&, int l);
    void drawChoice (juce::Graphics&, juce::Rectangle<float>, const juce::String&, bool on, bool dim = false);

    PocketHost& host;
    pocket::Library& library;
    pocket::BeatsEngine& beats;
    pocket::AppSettings& settings;
    pocket::LoopsEngine loops;

    LoopEditSheet editor { loops };
    std::array<pocket::PButton, kPanelButtons> panel;
    std::array<Row, pocket::kLoopLayers> rows;
    std::vector<Undo> undoStack;
    int selected = -1, lastLayer = -1, vocLayer = -1, holdRow = -1;
    pocket::HoldDetector hold { [this] { if (holdRow >= 0) { const int l = holdRow; pressed = 0; openLayerMenu (l); } } };
    int sourceChoice = 0;   // pocket::LoopSource
    int pressed = 0;
    int ticks = 0;
    bool demo = false;

    // the worker
    bool syncJobs = false;
    int jobLayer = -1, jobId = 0;
    bool jobUndo = false;
    const pocket::LoopTake* jobWas = nullptr;
    double shownBpm = 0.0;
    juce::uint32 bpmChangedAt = 0;

    struct Saved { const pocket::LoopTake* take = nullptr; int version = -1; bool present = false, mute = false; juce::uint32 fx = 0, board = 0; };
    std::array<Saved, pocket::kLoopLayers> saved;
    bool settingsDirty = false;
    float inputLevel = 0.0f;
};
