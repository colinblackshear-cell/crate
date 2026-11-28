#pragma once

#include "PocketKey.h"
#include "PocketSheet.h"
#include "PocketSynth.h"
#include "PocketTheme.h"

// FIX NOTES: the notes a SYNTH or BASS layer was recorded from, on a grid you can fix with a thumb, while the layer
// keeps looping. One bar at a time (16 steps; the bar chips pick another), a row for every semitone with the song's
// key notes lit. Tap a note to pick it, drag it to move it (sideways: steps, up and down: semitones), tap an empty
// spot to add one, drag up or down on an empty spot to see higher or lower. UP / DOWN move the picked note to the
// next note in the key, EARLIER / LATER a 1/16, SHORTER / LONGER change its length, DELETE takes it out, UNDO takes
// back the last change. Every change goes to the app (onChange), which renders the layer again from the notes, so
// you hear it on the next pass; each is a step in the app's UNDO history too.
class FixNotesSheet : public pocket::DragSheet, private juce::Timer
{
public:
    FixNotesSheet();
    ~FixNotesSheet() override;

    // Opens on `layer`'s notes ("BASS" / "SYNTH"), in the song's key.
    void show (int layer, const juce::String& what, const pocket::SynthPattern&, pocket::SongKey);
    int layer() const { return shownLayer; }
    const pocket::SynthPattern& pattern() const { return pat; }

    std::function<void (const pocket::SynthPattern&)> onChange;   // the notes changed: render the layer again
    std::function<void()> onDone;
    std::function<double()> playBeat;   // where the layer plays now, in its own beats (< 0: stopped)

    enum Id { none = 0, upId, downId, earlierId, laterId, shorterId, longerId, deleteId, undoId, doneId, barBase = 100 };
    // What the buttons and a tap do (the self-checks use these too).
    void press (int id);
    void pick (int index);                 // -1: none
    int picked() const { return pickedNote; }
    void tapCell (int stepInPattern, int note);   // an empty spot: adds a note there; on a note: picks it
    void showBar (int bar);
    int shownBar() const { return bar; }
    int lowestRow() const { return lowNote; }

    int preferredHeight() const { return 612; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    static constexpr int kRows = 15;
    void timerCallback() override;
    void changed (const pocket::SynthPattern& before);   // a step for UNDO, then onChange
    void refresh();
    void fitRows();                         // the rows hold the notes (on open)
    void keepInView (int note);
    int noteAt (juce::Point<float>) const;  // the note under the finger in the bar shown, or -1
    float stepX (double stepInBar) const;
    float rowY (int note) const;
    int rowNote (float y) const;
    double stepAt (float x) const;          // the step in the bar (0..16) under x
    juce::String pickedText() const;
    int stepInKey (int note, int dir) const;

    pocket::SynthPattern pat;
    pocket::SongKey key;
    std::vector<pocket::SynthPattern> undos;
    int shownLayer = -1, pickedNote = -1, bar = 0, lowNote = 36;
    juce::String what;

    juce::Rectangle<float> grid, labels, barRow, infoRect;
    // a finger on the grid
    enum class Drag { none, note, scroll, tap } drag = Drag::none;
    juce::Point<float> downAt;
    pocket::SynthPattern dragBefore;
    int dragSteps = 0, dragSemis = 0, scrollFrom = 0;
    double playShown = -1.0;

    pocket::PButton done { "Done", pocket::Look::amber };
    pocket::PButton up { U8("\xe2\x96\xb2 Up") }, down { U8("\xe2\x96\xbc Down") }, earlier { U8("\xe2\x97\x80 Earlier") }, later { U8("Later \xe2\x96\xb6") };
    pocket::PButton shorter { "Shorter" }, longer { "Longer" }, remove { "Delete", pocket::Look::red }, undo { "Undo" };
    std::vector<std::unique_ptr<pocket::PButton>> bars;
};
