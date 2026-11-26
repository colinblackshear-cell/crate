#pragma once

#include "PocketKit.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// The bottom sheet for one pad (a tap on a pad in EDIT): PITCH first and big (-12..+12 semitones, with - and +),
// then volume and pan, how it plays, PLAY AS KEYS, the sound page, chop, MORE (the pad's full menu) and clear.
class PadSheet : public pocket::DragSheet
{
public:
    explicit PadSheet (pocket::Kit&);

    void show (int pad);
    int pad() const { return padIndex; }

    std::function<void (int)> onChanged;       // volume, pan, tune or mode changed
    std::function<void (int)> onChop, onTrim, onPlayAsKeys, onMore;
    std::function<void (int)> onClear;         // the pad was emptied (the sheet keeps it for UNDO)
    std::function<void (int)> onUndoClear;
    std::function<void()> onDone;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void refresh();
    void setMode (pocket::PadMode);

    pocket::Kit& kit;
    int padIndex = 0;
    pocket::KitPad cleared;
    bool hasCleared = false;

    void setPitch (int semitones);
    pocket::PitchStrip pitch;
    juce::OwnedArray<pocket::KnobView> knobs;   // volume, pan
    pocket::PButton modes[3], chop { "Chop across pads" }, trim { "Sample page" }, clear { "Clear", pocket::Look::ghost },
                    done { "Done", pocket::Look::ghost }, keys { "Play as keys", pocket::Look::amber }, more { U8("More\xe2\x80\xa6") },
                    down { U8("\xe2\x88\x92") }, up { "+" };
};
