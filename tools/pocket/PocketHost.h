#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PocketLinkClient.h"

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
    // TO MAC: the Send to Mac sheet, for one saved sound (with its effects), or for anything else.
    virtual void sendSoundToMac (const juce::String& soundId, const juce::String& dest = "sounds") = 0;
    virtual void sendToMac (pocket::MacSend) = 0;
    virtual void openMacLink() = 0;                        // More > Mac link
    // LOCK BEAT: records what the Beats tab plays (the pad pattern and the drum machine) onto the next
    // empty loop layer, `bars` long, from the pattern's start.
    virtual void lockBeat (int bars) = 0;
    // Play/Stop for the whole song (the header's button): loops, else the pad pattern, else the drum machine.
    virtual void togglePlay() = 0;
    // The pad sample page: the Record tab edited the sound a pad plays and saved it. Every pad playing
    // that sound picks it up. backToPads returns to Beats > MPC Pads with the pad picked.
    virtual void padSoundEdited (const juce::String& soundId) = 0;
    virtual void backToPads (int pad) = 0;
    virtual void openSoundBank() = 0;   // SOUNDS on the pads: factory kits and your own sounds
};
