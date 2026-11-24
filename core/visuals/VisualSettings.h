#pragma once

#include <juce_core/juce_core.h>

#include "VisualLooks.h"

// Kaleidobox Visuals' picks, kept between launches in a small JSON file the app chooses
// (the phone keeps it with its sounds, the Mac with its Rig folder).
namespace crate::visuals
{
inline juce::var toVar (VisualState& s)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("look", s.look.load());
    o->setProperty ("drift", s.drift.load());
    o->setProperty ("colours", s.colours.load());
    o->setProperty ("wild", s.wild.load());
    o->setProperty ("trails", s.trails.load());
    juce::Array<juce::var> yours;
    {
        const juce::SpinLock::ScopedLockType l (s.yoursLock);
        for (auto c : s.yours)
            yours.add (c.toDisplayString (false));
    }
    o->setProperty ("yours", yours);
    return juce::var (o);
}

inline void fromVar (VisualState& s, const juce::var& v)
{
    if (! v.isObject())
        return;
    s.look = juce::jlimit (0, kLooks - 1, (int) v.getProperty ("look", 0));
    s.drift = (bool) v.getProperty ("drift", false);
    s.colours = juce::jlimit (0, (int) Colours::count - 1, (int) v.getProperty ("colours", 0));
    s.wild = juce::jlimit (1, 4, (int) v.getProperty ("wild", 3));
    s.trails = juce::jlimit (1, 4, (int) v.getProperty ("trails", 3));
    if (const auto* yours = v.getProperty ("yours", {}).getArray(); yours != nullptr && yours->size() == 12)
        for (int pc = 0; pc < 12; ++pc)
            s.setYours (pc, juce::Colour::fromString ("ff" + (*yours)[pc].toString()));
}

// Where the app keeps them: the app's own support folder (Mac: ~/Library/Application Support/Kaleidobox;
// the phone: inside the app, not in Files).
inline juce::File defaultFile()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("Kaleidobox").getChildFile ("Visuals.json");
}

inline void load (VisualState& s, const juce::File& file)
{
    if (file.existsAsFile())
        fromVar (s, juce::JSON::parse (file));
}

inline void save (VisualState& s, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (toVar (s)));
}
} // namespace crate::visuals
