#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PocketHold.h"
#include "PocketLinkClient.h"
#include "PocketRoute.h"

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
    virtual void openMacLink() = 0;                        // the menu's MAC LINK
    // Press and hold: opens the hold menu card next to the finger (`at`) over a dim that keeps `highlight` bright.
    // Both are in `source`'s coordinates.
    virtual void holdMenu (const pocket::HoldSpec&, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight) = 0;
    // A note with one button on it (UNDO), for a few seconds.
    virtual void toastAction (const juce::String& text, const juce::String& button, std::function<void()> then) = 0;
    virtual void openSamplePage (int pad) = 0;             // the pad's sound on the SAMPLER (IN / OUT, trim, send to a pad)
    // EDIT THIS SLICE: the pad's own part of its sound, alone in the sound editor; leaving it saves onto the pad.
    virtual void editPadSlice (int pad) = 0;
    // Play/Stop for the whole song (the thumb bar's button): loops, else the pad pattern, else the drum machine.
    virtual void togglePlay() = 0;
    // The pad sample page: the Record tab edited the sound a pad plays and saved it. Every pad playing
    // that sound picks it up. backToPads returns to the SAMPLER with that pad's sound.
    virtual void padSoundEdited (const juce::String& soundId) = 0;
    // An edit of a factory kit's sound was saved as your own copy: this song's pads that played `oldId` play `newId`.
    virtual void padSoundSwapped (const juce::String& oldId, const juce::String& newId) = 0;
    virtual void backToPads (int pad) = 0;
    virtual void openDrumKit() = 0;     // DRUMS > KIT: your own sounds on the drum machine's sounds
    virtual void openSoundBank() = 0;   // SOUNDS on the pads: factory kits and your own sounds
    virtual void showLoops() = 0;       // the LOOPS tool (layers)
    virtual void showVocoder() = 0;     // every vocoder setting (the VOCODER tool's display opens it)
    virtual void closeVocoderSettings() = 0;   // back from every vocoder setting to the VOCODER tool
    virtual void openRecorder() = 0;    // RECORD A SOUND: the sound recorder (a sample for the pads)
    virtual void openSampler (int pad) = 0;   // the SAMPLER tool, aimed at that pad (-1: none), empty and ready for REC

    // The mic, for the whole app: off at every launch, and nothing from it reaches the speaker until the
    // headphones sheet says so. askForMic shows that sheet (if the mic is off) and runs `then` once it's on.
    virtual bool micReady() = 0;
    virtual void askForMic (std::function<void()> then) = 0;
    virtual pocket::Route micRoute() = 0;
    virtual bool hearingMyself() = 0;
    virtual void setHearMyself (bool) = 0;
    virtual void micOff() = 0;
    virtual void micUseChanged() = 0;   // a screen changed what it wants to hear (MY VOICE, VOCODER, BOTH)
    virtual double songBpm() = 0;       // the song's tempo (the TEMPO sheet's)
};
