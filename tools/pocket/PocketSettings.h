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
            const auto& p = o->getProperties();
            // Older versions kept a count-in switch (1 bar or off) and Sing's own length: they become the
            // one Rec setup that every tool uses.
            countInBars = juce::jlimit (0, 2, p.contains ("countInBars") ? (int) p["countInBars"] : ((bool) p.getWithDefault ("countIn", true) ? 1 : 0));
            recLength = juce::jlimit (0, 4, p.contains ("recLength") ? (int) p["recLength"] : (int) p.getWithDefault ("singLength", 2));
            click = (bool) p.getWithDefault ("click", false);
            singMode = juce::jlimit (0, 2, (int) p.getWithDefault ("singMode", 1));
            singHold = (bool) p.getWithDefault ("singHold", true);
            singPadSound = (bool) p.getWithDefault ("singPadSound", false);
        }
    }

    // Rec setup, the same for every tool: a count-in of 0, 1 or 2 bars when REC starts the song (the click
    // plays through it even with CLICK off), and how long a take is: 0..4 = 1, 2, 4, 8 bars, FREE.
    int countInBars = 1;
    int recLength = 2;
    bool click = false;    // a click on every beat while the song plays (the thumb bar's CLICK)
    // The Vocoder tool: 0 = my voice, 1 = vocoder, 2 = both; chord pads latch; chord pads also play the
    // picked pad's sound
    int singMode = 1;
    bool singHold = true, singPadSound = false;

    // The take's length in bars, 0 = FREE (until stopped, or the first layer's length).
    int recBars() const { static const int bars[5] = { 1, 2, 4, 8, 0 }; return bars[juce::jlimit (0, 4, recLength)]; }
    static juce::String lengthText (int choice)
    {
        static const char* names[5] = { "1 BAR", "2 BARS", "4 BARS", "8 BARS", "FREE" };
        return names[juce::jlimit (0, 4, choice)];
    }
    static juce::String countInText (int bars) { return bars == 0 ? juce::String ("OFF") : bars == 1 ? juce::String ("1 BAR") : juce::String ("2 BARS"); }

    void save() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("countInBars", countInBars);
        o->setProperty ("recLength", recLength);
        o->setProperty ("click", click);
        o->setProperty ("singMode", singMode);
        o->setProperty ("singHold", singHold);
        o->setProperty ("singPadSound", singPadSound);
        root.createDirectory();
        root.getChildFile ("settings.json").replaceWithText (juce::JSON::toString (juce::var (o)));
    }

private:
    juce::File root;
};
}
