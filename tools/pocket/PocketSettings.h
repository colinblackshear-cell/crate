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
            singMode = juce::jlimit (0, 2, (int) o->getProperties().getWithDefault ("singMode", 1));
            singLength = juce::jlimit (0, 4, (int) o->getProperties().getWithDefault ("singLength", 2));
            singHold = (bool) o->getProperties().getWithDefault ("singHold", true);
            singPadSound = (bool) o->getProperties().getWithDefault ("singPadSound", false);
        }
    }

    bool countIn = true;   // Loops: a silent one-bar count-in when a recording starts the clock
    bool click = false;    // a click on every beat while the song plays (Tempo sheet)
    // Sing: 0 = my voice, 1 = vocoder, 2 = both; length 0..4 = 1, 2, 4, 8 bars, FREE; chord pads latch;
    // chord pads also play the picked Beats pad's sound
    int singMode = 1, singLength = 2;
    bool singHold = true, singPadSound = false;

    void save() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("countIn", countIn);
        o->setProperty ("click", click);
        o->setProperty ("singMode", singMode);
        o->setProperty ("singLength", singLength);
        o->setProperty ("singHold", singHold);
        o->setProperty ("singPadSound", singPadSound);
        root.createDirectory();
        root.getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    }

private:
    juce::File root;
};
}
