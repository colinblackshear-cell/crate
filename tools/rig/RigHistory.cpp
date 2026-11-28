#include "RigHistory.h"

void RigHistory::push (Step step)
{
    if (busy)
        return;
    past.push_back (std::move (step));
    if ((int) past.size() > kMaxSteps)
        past.erase (past.begin());
    future.clear();
    changed();
}

bool RigHistory::prune (std::vector<Step>& steps)
{
    bool dropped = false;
    while (! steps.empty() && steps.back().stillValid != nullptr && ! steps.back().stillValid())
    {
        steps.pop_back();
        dropped = true;
    }
    return dropped;
}

juce::String RigHistory::take (std::vector<Step>& from, std::vector<Step>& to, bool undoing)
{
    if (busy)
        return {};
    const bool dropped = prune (from);
    if (from.empty())
    {
        if (dropped)
            changed();
        return {};
    }
    auto step = std::move (from.back());
    from.pop_back();
    busy = true;
    if (undoing ? step.undo != nullptr : step.redo != nullptr)
        (undoing ? step.undo : step.redo)();
    busy = false;
    auto what = step.what;
    to.push_back (std::move (step));
    changed();
    return what.isNotEmpty() ? what : juce::String ("the last change");
}

juce::String RigHistory::undo() { return take (past, future, true); }
juce::String RigHistory::redo() { return take (future, past, false); }

bool RigHistory::canUndo()
{
    if (prune (past))
        changed();
    return ! past.empty();
}

bool RigHistory::canRedo()
{
    if (prune (future))
        changed();
    return ! future.empty();
}

juce::String RigHistory::nextUndo() { return canUndo() ? past.back().what : juce::String(); }
juce::String RigHistory::nextRedo() { return canRedo() ? future.back().what : juce::String(); }

bool RigHistory::newestIs (const juce::String& site)
{
    return canUndo() && past.back().site == site;
}

void RigHistory::clear()
{
    if (past.empty() && future.empty())
        return;
    past.clear();
    future.clear();
    changed();
}

void RigHistory::changed()
{
    if (onChange)
        onChange();
}

RigHistory::Step RigHistory::siteStep (RigHistorySite& site, const juce::String& siteName, const juce::String& what, int before,
                                       std::function<void()> undo, std::function<void()> redo,
                                       std::function<bool (bool)> alsoValid)
{
    struct State
    {
        int before = 0, after = 0;
        bool undone = false;
    };
    auto st = std::make_shared<State>();
    st->before = before;
    st->after = site.token;
    Step step;
    step.what = what;
    step.site = siteName;
    step.stillValid = [&site, st, alsoValid]
    {
        if (site.token != (st->undone ? st->before : st->after))
            return false;
        return alsoValid == nullptr || alsoValid (! st->undone);
    };
    step.undo = [&site, st, undo]
    {
        if (undo != nullptr)
            undo();
        site.token = st->before;
        st->undone = true;
    };
    step.redo = [&site, st, redo]
    {
        if (redo != nullptr)
            redo();
        site.token = st->after;
        st->undone = false;
    };
    return step;
}

juce::String RigHistory::undidText (const juce::String& what, bool more)
{
    return "Undid: " + what + (more ? juce::String (". Undo again goes back another step.") : juce::String (". That's as far back as it goes."));
}

juce::String RigHistory::redidText (const juce::String& what)
{
    return "Redid: " + what + ".";
}
