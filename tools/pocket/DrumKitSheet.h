#pragma once

#include "PocketLibrary.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// DRUMS > KIT: your own sounds on the drum machine. For the drum sound picked on the grid (SNARE, say) it lists the sounds
// in your library, newest first: tap one and it is on that drum sound (and plays, so you hear it). BACK TO ANALOG puts the
// machine's own sound back. USE A PAD'S SOUNDS AS A KIT maps the pads whose names say kick, snare, hat, tom... onto the
// drum sounds in one go. The rows are tall, for a thumb.
class DrumKitSheet : public pocket::DragSheet
{
public:
    explicit DrumKitSheet (pocket::Library&);
    ~DrumKitSheet() override;

    std::function<void()> onDone, onBackToAnalog, onUsePads;
    std::function<void (const juce::String& soundId)> onPick;
    int bottomInset = 0;

    // `voiceName` is the drum sound ("SNARE"); `currentId` and `currentName` say what is on it now (both empty: analog).
    void open (const juce::String& voiceName, const juce::String& currentId, const juce::String& currentName);
    int rowCount() const;
    void pickRow (int row);                         // what a tap on a row does (the self-checks tap with it)
    juce::Rectangle<int> rowBounds (int row) const; // in the sheet's coordinates (empty when scrolled away)
    juce::String shownVoice() const { return voice; }
    juce::String shownSource() const { return sourceName; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class List;
    pocket::Library& library;
    juce::String voice, sourceId, sourceName;
    pocket::PButton done { "Done", pocket::Look::amber }, analog { "Back to analog" }, usePads { "Use a pad's sounds as a kit" };
    juce::Viewport viewport;
    std::unique_ptr<List> list;
    juce::Rectangle<float> sourceBox, headingBox;
};
