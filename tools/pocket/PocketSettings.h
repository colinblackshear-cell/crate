#pragma once

#include <juce_core/juce_core.h>

// The few app settings the user can change, kept in Documents/settings.json next to the sounds.
namespace pocket
{
class AppSettings
{
public:
    explicit AppSettings (juce::File rootFolder) : root (std::move (rootFolder))
    {
        auto v = juce::JSON::parse (root.getChildFile ("settings.json"));
        if (auto* o = v.getDynamicObject())
        {
            countIn = (bool) o->getProperties().getWithDefault ("countIn", true);
            click = (bool) o->getProperties().getWithDefault ("click", false);
        }
    }

    bool countIn = true;   // Loops: a silent one-bar count-in when a recording starts the clock
    bool click = false;    // a click on every beat while the song plays (Tempo sheet)

    void save() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("countIn", countIn);
        o->setProperty ("click", click);
        root.createDirectory();
        root.getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    }

private:
    juce::File root;
};
}
