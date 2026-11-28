#pragma once

#include "RigEngine.h"
#include "RigHistory.h"

#include <array>

// The looper's part of Undo and Redo (RigHistory). It watches the layers from the window's timer:
// a layer seen recording that then plays is a take (however it was recorded: Rec, Rec next
// layer, a card's Rec, the pads, To looper, Catch an idea); one seen overdubbing that then plays
// is an overdub. A loop that comes in by a loop bank or a song part never records, so it's no
// step. Clears by hand (RigEngine::clearLoop) and Clear all are steps too.
//
// Each layer is a RigHistorySite. A layer's sound changing in a way no step made (a loop bank,
// a song part, a trim, the Loop Lab, an import) gives it a new token, so no step puts a loop back
// over it. A take undone keeps the loop first, so Redo puts the same sound back.
//
// Message thread only.
class RigLoopHistory
{
public:
    RigLoopHistory (RigEngine&, RigHistory&);

    // The window's timer (the tests call it): pushes the takes and overdubs that have landed.
    // `changesAreSteps`: any other change to a layer's sound since is a step's doing (a quantize
    // about to push its own), so it leaves the steps under it good.
    void tick (bool changesAreSteps = false);
    // A layer's sound just changed by a step (or a change that is one): what it holds now is known.
    void sync (int layer);
    void syncAll();
    // A new show: nothing from before is a step any more.
    void reset();

    // A layer another step owns as it lands (To looper's own step): the watcher leaves it.
    std::function<bool (int layer)> isClaimed;
    // A take was undone (its loop is empty): the window forgets it as the last take.
    std::function<void (int layer)> onTakeUndone;

    // A take landed on `layer` (it plays now): its step. `undoToo` and `redoToo` run after the
    // loop is cleared or put back (To looper switches its instrument on and off with them).
    void pushTake (int layer, const juce::String& what, std::function<void()> undoToo = {}, std::function<void()> redoToo = {});
    // An overdub on `layer` ended: Undo takes it back with the looper's own undo (no step if there
    // is none), Redo puts the overdubbed loop back.
    void pushOverdub (int layer);
    // A Clear by hand took `kept` off `layer` (RigEngine::onLoopCleared).
    void pushCleared (int layer, std::shared_ptr<const RigEngine::KeptLoop> kept);
    // Clear all (RigEngine::onAllLoopsCleared): Undo brings it all back, Redo clears it again.
    void pushClearedAll();
    // A change to the layer's sound that another step takes back (the Loop Lab's fit, a
    // quantize): `ticks` timer ticks during which its sound changing is that step's doing.
    void expectChange (int layer, int ticks = 60);

    RigHistorySite& site (int layer) { return sites[(size_t) layer]; }
    static juce::String siteName (int layer) { return "loop " + juce::String (layer + 1); }

private:
    RigEngine& engine;
    RigHistory& history;
    std::array<RigHistorySite, RigLooper::kLayers> sites;
    std::array<int, RigLooper::kLayers> seenVersion {}, seenState {}, expectUntil {};
    std::array<bool, RigLooper::kLayers> seenShift {};
    int ticks = 0;
    bool explained (int layer, int version) const;
};
