#include "RigMacros.h"

RigMacros::RigMacros()
{
    for (auto& v : value)
        v = 0.0f;
    applied.fill (-1.0f);
}

juce::String RigMacros::getName (int m) const
{
    if (! juce::isPositiveAndBelow (m, kMacros))
        return {};
    const juce::SpinLock::ScopedLockType sl (lock);
    return macros[(size_t) m].name.isNotEmpty() ? macros[(size_t) m].name : "Macro " + juce::String (m + 1);
}

void RigMacros::setName (int m, const juce::String& name)
{
    if (! juce::isPositiveAndBelow (m, kMacros))
        return;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        macros[(size_t) m].name = name.trim().substring (0, 24);
    }
    ++version;
}

std::vector<RigMacros::Target> RigMacros::getTargets (int m) const
{
    if (! juce::isPositiveAndBelow (m, kMacros))
        return {};
    const juce::SpinLock::ScopedLockType sl (lock);
    return macros[(size_t) m].targets;
}

int RigMacros::getNumTargets (int m) const { return (int) getTargets (m).size(); }

juce::String RigMacros::targetName (const Target& t) const
{
    juce::String name = toolName != nullptr ? toolName (t.tool) : juce::String();
    if (auto* p = getParameter != nullptr ? getParameter (t.tool, t.parameter) : nullptr)
        name << " " << p->getName (40);
    return name.trim();
}

void RigMacros::resolve (Macro& m)
{
    m.params.clear();
    for (const auto& t : m.targets)
        m.params.push_back (getParameter != nullptr ? getParameter (t.tool, t.parameter) : nullptr);
}

bool RigMacros::setTarget (int m, Target t)
{
    if (! juce::isPositiveAndBelow (m, kMacros))
        return false;
    if (t.paramId.isEmpty())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (getParameter != nullptr ? getParameter (t.tool, t.parameter) : nullptr))
            t.paramId = withId->paramID;
    {
        auto macro = macros[(size_t) m];   // (built off the audio thread, swapped in under the lock)
        bool found = false;
        for (auto& existing : macro.targets)
            if (existing.tool == t.tool && existing.parameter == t.parameter)
            {
                existing = t;
                found = true;
            }
        if (! found)
        {
            if ((int) macro.targets.size() >= kTargets)
                return false;
            macro.targets.push_back (t);
        }
        resolve (macro);
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (macros[(size_t) m], macro);
    }
    ++version;
    return true;
}

void RigMacros::removeTarget (int m, int index)
{
    if (! juce::isPositiveAndBelow (m, kMacros))
        return;
    auto macro = getTargets (m);
    if (! juce::isPositiveAndBelow (index, (int) macro.size()))
        return;
    Macro next;
    next.name = macros[(size_t) m].name;
    next.targets = macro;
    next.targets.erase (next.targets.begin() + index);
    resolve (next);
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (macros[(size_t) m], next);
    }
    ++version;
}

void RigMacros::clear (int m)
{
    if (! juce::isPositiveAndBelow (m, kMacros))
        return;
    Macro next;
    next.name = macros[(size_t) m].name;
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        std::swap (macros[(size_t) m], next);
    }
    value[(size_t) m] = 0.0f;
    ++version;
}

void RigMacros::clearAll()
{
    for (int m = 0; m < kMacros; ++m)
    {
        Macro none;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            std::swap (macros[(size_t) m], none);
        }
        value[(size_t) m] = 0.0f;
    }
    assigning = -1;
    touches.clear();
    ++version;
}

void RigMacros::startAssigning (int m)
{
    assigning = juce::isPositiveAndBelow (m, kMacros) ? m : -1;
    touches.clear();
    ++version;
}

void RigMacros::stopAssigning()
{
    assigning = -1;
    touches.clear();
    ++version;
}

void RigMacros::touchBegan (int tool, int parameter, float valueNow)
{
    if (assigning < 0)
        return;
    for (auto& t : touches)
        if (t.tool == tool && t.parameter == parameter)
            return;   // (still the start of its first touch)
    touches.push_back ({ tool, parameter, valueNow });
}

bool RigMacros::touchEnded (int tool, int parameter, float valueNow)
{
    if (assigning < 0)
        return false;
    for (const auto& t : touches)
        if (t.tool == tool && t.parameter == parameter)
        {
            if (std::abs (valueNow - t.began) < 0.005f)
                return false;   // touched, not moved
            Target target;
            target.tool = tool;
            target.parameter = parameter;
            target.from = t.began;
            target.to = valueNow;
            if (! setTarget (assigning, target))
                return false;
            // The macro is where its settings are now: all the way up.
            value[(size_t) assigning] = 1.0f;
            applied[(size_t) assigning] = 1.0f;
            return true;
        }
    return false;
}

juce::ValueTree RigMacros::toTree() const
{
    juce::ValueTree tree (kTreeType);
    for (int m = 0; m < kMacros; ++m)
    {
        const auto targets = getTargets (m);
        const auto name = macros[(size_t) m].name;
        if (targets.empty() && name.isEmpty())
            continue;
        juce::ValueTree mt ("Macro");
        mt.setProperty ("index", m, nullptr);
        mt.setProperty ("name", name, nullptr);
        mt.setProperty ("value", value[(size_t) m].load(), nullptr);
        for (const auto& t : targets)
        {
            juce::ValueTree tt ("Target");
            tt.setProperty ("tool", t.tool, nullptr);
            tt.setProperty ("parameter", t.parameter, nullptr);
            tt.setProperty ("id", t.paramId, nullptr);
            tt.setProperty ("from", t.from, nullptr);
            tt.setProperty ("to", t.to, nullptr);
            mt.appendChild (tt, nullptr);
        }
        tree.appendChild (mt, nullptr);
    }
    return tree;
}

void RigMacros::fromTree (const juce::ValueTree& tree)
{
    clearAll();
    if (! tree.isValid())
        return;
    for (const auto& mt : tree)
    {
        const int m = mt.getProperty ("index", -1);
        if (! juce::isPositiveAndBelow (m, kMacros))
            continue;
        Macro macro;
        macro.name = mt.getProperty ("name").toString();
        for (const auto& tt : mt)
        {
            Target t;
            t.tool = tt.getProperty ("tool", 0);
            t.parameter = tt.getProperty ("parameter", 0);
            t.paramId = tt.getProperty ("id").toString();
            t.from = tt.getProperty ("from", 0.0f);
            t.to = tt.getProperty ("to", 1.0f);
            // A setting that moved in its tool's list is found again by its ID.
            if (t.paramId.isNotEmpty() && getParameter != nullptr && numParameters != nullptr)
            {
                auto* at = dynamic_cast<juce::AudioProcessorParameterWithID*> (getParameter (t.tool, t.parameter));
                if (at == nullptr || at->paramID != t.paramId)
                    for (int p = 0; p < numParameters (t.tool); ++p)
                        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (getParameter (t.tool, p)); withId != nullptr && withId->paramID == t.paramId)
                        {
                            t.parameter = p;
                            break;
                        }
            }
            if ((int) macro.targets.size() < kTargets)
                macro.targets.push_back (t);
        }
        resolve (macro);
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            std::swap (macros[(size_t) m], macro);
        }
        value[(size_t) m] = (float) mt.getProperty ("value", 0.0f);
        applied[(size_t) m] = value[(size_t) m].load();   // the settings were saved where they were
    }
    ++version;
}

void RigMacros::process()
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;
    for (int m = 0; m < kMacros; ++m)
    {
        const float v = juce::jlimit (0.0f, 1.0f, value[(size_t) m].load (std::memory_order_relaxed));
        if (std::abs (v - applied[(size_t) m]) < 1.0e-5f)
            continue;
        applied[(size_t) m] = v;
        const auto& macro = macros[(size_t) m];
        for (size_t i = 0; i < macro.targets.size() && i < macro.params.size(); ++i)
            if (auto* p = macro.params[i])
            {
                const auto& t = macro.targets[i];
                const float to = juce::jlimit (0.0f, 1.0f, t.from + (t.to - t.from) * v);
                if (std::abs (p->getValue() - to) > 1.0e-6f)
                    p->setValueNotifyingHost (to);
            }
    }
}
