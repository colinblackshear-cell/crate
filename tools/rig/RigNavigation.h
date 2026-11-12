#pragma once

#include <juce_core/juce_core.h>

// Back and forward through the screens you've been on, like a web browser: each new screen
// goes on the end, Back steps along it, Forward steps back again, and going somewhere new
// from the middle drops what was ahead. A place is a page's name ("Groove", "MPC"...), or a
// page and the part of it showing ("Sampler editor", "Looper / Timeline").
class RigNavigation
{
public:
    static constexpr int kMaxPlaces = 64;

    // You're now at `place`. The same place again changes nothing.
    void visit (const juce::String& place)
    {
        if (place.isEmpty() || place == current())
            return;
        places.removeRange (index + 1, places.size());
        places.add (place);
        if (places.size() > kMaxPlaces)
            places.removeRange (0, places.size() - kMaxPlaces);
        index = places.size() - 1;
    }

    bool canGoBack() const { return index > 0; }
    bool canGoForward() const { return index < places.size() - 1; }

    // Steps back (or forward) and says where to; empty when there's nowhere to go.
    juce::String back() { return canGoBack() ? places[--index] : juce::String(); }
    juce::String forward() { return canGoForward() ? places[++index] : juce::String(); }

    juce::String current() const { return places[index]; }
    juce::String previous() const { return canGoBack() ? places[index - 1] : juce::String(); }
    juce::String next() const { return canGoForward() ? places[index + 1] : juce::String(); }

private:
    juce::StringArray places;
    int index = -1;
};
