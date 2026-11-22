#pragma once

#include "PocketChords.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// CHORDS > hold a chord pad > EDIT CHORD: the pad's own chord. ROOT (twelve notes), TYPE (the builder's fifteen), INV and
// OCT, then two octaves of keys (‹ › move them an octave) where a tap adds a note or takes it out, for any voicing. The
// chord's name up top as the Mac's chord builder names it, or CUSTOM. Every change is on the pad at once (and wherever
// the pad's chord plays: HOLD, STRUM, ARP, STABS, + BASS, the progression, a CHORDS take, the VOCODER's quick pads).
// HEAR, RESET TO SONG KEY and DONE sit at the bottom, under the thumb.
class ChordEditSheet : public pocket::DragSheet
{
public:
    explicit ChordEditSheet (pocket::ChordsTool&);

    std::function<void()> onDone;
    std::function<void()> onChanged;                     // something to save, the pads to repaint
    std::function<void (const juce::String&)> toast;
    int bottomInset = 0;                                 // the home bar: the bottom row stays above it

    void open (int pad);
    int pad() const { return padIndex; }
    int keysLow() const { return kbLow; }

    // What the buttons and keys do (also for the self-checks and screenshots)
    void pickRoot (int pitchClass);
    void pickType (int type);
    void invert (int direction);
    void shiftOctave (int direction);
    void toggleNote (int midi);
    void moveKeys (int octaves);
    void hear();
    void resetToKey();
    juce::Rectangle<float> keyRect (int midi) const;     // a key of the keyboard on screen (empty when it isn't shown)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void apply (bool audition = true);
    void refresh();
    int keyAt (juce::Point<float>) const;
    static bool isBlack (int midi);

    pocket::ChordsTool& tool;
    int padIndex = 0;
    pocket::OwnChord work;
    int kbLow = 48;
    juce::Rectangle<float> keysArea, nameArea, notesArea;
    juce::Rectangle<float> rootLabel, typeLabel, keysLabel, invLabel, octLabel;
    pocket::PButton roots[12], types[15], invDown { "-" }, invUp { "+" }, octDown { "-" }, octUp { "+" },
        keysDown { U8("\xe2\x80\xb9") }, keysUp { U8("\xe2\x80\xba") }, hearButton { "Hear", pocket::Look::green },
        resetButton { "Reset to song key" }, doneButton { "Done", pocket::Look::amber };
};
