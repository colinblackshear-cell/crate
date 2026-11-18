#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// What the screens ask of the app around them.
struct PocketHost
{
    virtual ~PocketHost() = default;
    virtual void toast (const juce::String&) = 0;      // a short message at the bottom
    virtual void openSheet() = 0;                      // the Effects + Chop sheet
    virtual void closeSheet() = 0;
    virtual void editSound (const juce::String& id) = 0;   // opens a saved sound in Record
    virtual void sendToPad (const juce::String& soundId) = 0;   // TO PAD: onto the next empty pad of the current bank
    virtual void openPadSheet (int pad) = 0;               // the pad sheet (volume, pan, tune, chop, trim)
    virtual void closePadSheet() = 0;
};
