#include "RigLoopHistory.h"

using State = RigLooper::State;

RigLoopHistory::RigLoopHistory (RigEngine& e, RigHistory& h) : engine (e), history (h)
{
    syncAll();
}

void RigLoopHistory::sync (int i)
{
    if (! juce::isPositiveAndBelow (i, RigLooper::kLayers))
        return;
    const auto& looper = engine.getLooper();
    seenVersion[(size_t) i] = looper.getLayer (i).version.load();
    seenState[(size_t) i] = (int) looper.getState (i);
    seenShift[(size_t) i] = looper.isSyncShiftPending (i);
}

void RigLoopHistory::syncAll()
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
        sync (i);
}

void RigLoopHistory::reset()
{
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        sites[(size_t) i].touch();
        expectUntil[(size_t) i] = 0;
        sync (i);
    }
}

void RigLoopHistory::expectChange (int i, int forTicks)
{
    if (juce::isPositiveAndBelow (i, RigLooper::kLayers))
        expectUntil[(size_t) i] = ticks + forTicks;
}

bool RigLoopHistory::explained (int i, int version) const
{
    if (ticks < expectUntil[(size_t) i])
        return true;
    // A new loop turned round onto the shared bars: the same sound.
    if (seenShift[(size_t) i] && ! engine.getLooper().isSyncShiftPending (i))
        return true;
    // The Loop Lab playing its own render of the loop it holds (its steps are its own).
    auto& lab = engine.getLoopLab();
    return lab.isOpen (i) && lab.get (i).playingVersion == version;
}

void RigLoopHistory::tick (bool changesAreSteps)
{
    ++ticks;
    auto& looper = engine.getLooper();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto st = looper.getState (i);
        const int version = looper.getLayer (i).version.load();
        const auto was = (State) seenState[(size_t) i];
        if (st == State::armed || st == State::recording)
        {
            // In flight: it's a step once it lands.
        }
        else if (was == State::recording && (st == State::playing || st == State::overdubbing))
        {
            if (isClaimed != nullptr && isClaimed (i))
                sync (i);
            else
                pushTake (i, "take on loop " + juce::String (i + 1));
            continue;
        }
        else if (was == State::overdubbing && st == State::playing)
        {
            pushOverdub (i);
            continue;
        }
        else if (st == State::overdubbing)
        {
            // In flight too.
        }
        else if (version != seenVersion[(size_t) i] && ! changesAreSteps && ! explained (i, version))
        {
            sites[(size_t) i].touch();   // changed by something that isn't a step
        }
        sync (i);
    }
}

void RigLoopHistory::pushTake (int i, const juce::String& what, std::function<void()> undoToo, std::function<void()> redoToo)
{
    if (! juce::isPositiveAndBelow (i, RigLooper::kLayers))
        return;
    if (history.isBusy())
        return sync (i);
    auto& site = sites[(size_t) i];
    const int before = site.token;
    site.touch();
    sync (i);
    // The loop, kept as the take is undone (so Redo puts the same sound back).
    auto kept = std::make_shared<std::shared_ptr<const RigEngine::KeptLoop>>();
    history.push (RigHistory::siteStep (site, siteName (i), what, before,
        [this, i, kept, undoToo]
        {
            if (*kept == nullptr)
                *kept = engine.keepLoop (i);
            engine.emptyLoop (i);
            sync (i);
            if (onTakeUndone != nullptr)
                onTakeUndone (i);
            if (undoToo != nullptr)
                undoToo();
        },
        [this, i, kept, redoToo]
        {
            engine.putLoopBack (i, **kept);
            sync (i);
            if (redoToo != nullptr)
                redoToo();
        },
        [this, i, kept] (bool undoing)
        {
            auto& looper = engine.getLooper();
            if (looper.isSwapPending (i))
                return false;
            const auto st = looper.getState (i);
            return undoing ? st == State::playing
                           : st == State::empty && *kept != nullptr && (*kept)->hasSound();
        }));
}

void RigLoopHistory::pushOverdub (int i)
{
    if (! juce::isPositiveAndBelow (i, RigLooper::kLayers))
        return;
    auto& site = sites[(size_t) i];
    const int before = site.token;
    site.touch();
    sync (i);
    // With no undo for it (too long, squeezed), it's no step, and what's under it can't come
    // back over it either (the touch above).
    if (history.isBusy() || ! engine.getLooper().canUndo (i))
        return;
    struct Kept
    {
        std::shared_ptr<const RigEngine::KeptLoop> dubbed, plain;   // with the overdub, and without
    };
    auto kept = std::make_shared<Kept>();
    history.push (RigHistory::siteStep (site, siteName (i), "overdub on loop " + juce::String (i + 1), before,
        [this, i, kept]
        {
            if (kept->dubbed == nullptr)
                kept->dubbed = engine.keepLoop (i);
            if (kept->plain != nullptr)
                engine.putLoopBack (i, *kept->plain);
            else
            {
                engine.getLooper().undo (i);   // the audio thread puts it back
                expectChange (i);
            }
            sync (i);
        },
        [this, i, kept]
        {
            if (kept->plain == nullptr)
                kept->plain = engine.keepLoop (i);
            engine.putLoopBack (i, *kept->dubbed);
            sync (i);
        },
        [this, i, kept] (bool undoing)
        {
            auto& looper = engine.getLooper();
            if (looper.isSwapPending (i) || looper.getState (i) != State::playing)
                return false;
            if (undoing)
                return kept->plain != nullptr ? kept->plain->hasSound() : looper.canUndo (i);
            return kept->dubbed != nullptr && kept->dubbed->hasSound();
        }));
}

void RigLoopHistory::pushCleared (int i, std::shared_ptr<const RigEngine::KeptLoop> kept)
{
    if (! juce::isPositiveAndBelow (i, RigLooper::kLayers) || kept == nullptr)
        return;
    if (history.isBusy())
        return sync (i);
    auto& site = sites[(size_t) i];
    const int before = site.token;
    site.touch();
    sync (i);
    history.push (RigHistory::siteStep (site, siteName (i), "clear loop " + juce::String (i + 1), before,
        [this, i, kept]
        {
            engine.putLoopBack (i, *kept);
            sync (i);
        },
        [this, i]
        {
            engine.emptyLoop (i);
            sync (i);
        },
        [this, i, kept] (bool undoing)
        {
            auto& looper = engine.getLooper();
            if (looper.isSwapPending (i))
                return false;
            // Back only onto the empty layer it left; cleared again only while it's that loop.
            return undoing ? looper.getState (i) == State::empty && kept->hasSound()
                           : looper.getState (i) == State::playing;
        }));
}

void RigLoopHistory::pushClearedAll()
{
    if (history.isBusy())
        return syncAll();
    struct Tokens
    {
        std::array<int, RigLooper::kLayers> before {}, after {};
        bool undone = false;
    };
    auto t = std::make_shared<Tokens>();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        t->before[(size_t) i] = sites[(size_t) i].token;
        t->after[(size_t) i] = sites[(size_t) i].touch();
    }
    syncAll();
    RigHistory::Step step;
    step.what = "clear all loops";
    step.site = "loops";
    step.stillValid = [this, t]
    {
        for (int i = 0; i < RigLooper::kLayers; ++i)
            if (sites[(size_t) i].token != (t->undone ? t->before : t->after)[(size_t) i])
                return false;
        // Back only while the layers are still empty (as Clear all's own Undo).
        return t->undone || engine.canUndoClearAllLoops();
    };
    step.undo = [this, t]
    {
        engine.undoClearAllLoops();
        syncAll();
        for (int i = 0; i < RigLooper::kLayers; ++i)
            sites[(size_t) i].token = t->before[(size_t) i];
        t->undone = true;
    };
    step.redo = [this, t]
    {
        engine.clearAllLoops();
        syncAll();
        for (int i = 0; i < RigLooper::kLayers; ++i)
            sites[(size_t) i].token = t->after[(size_t) i];
        t->undone = false;
    };
    history.push (std::move (step));
}
