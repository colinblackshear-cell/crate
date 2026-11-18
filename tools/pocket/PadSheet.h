#pragma once

#include "PocketKit.h"
#include "PocketTheme.h"

// The bottom sheet for one MPC pad: volume, pan and tune, how it plays, and chop, trim and clear.
class PadSheet : public juce::Component
{
public:
    explicit PadSheet (pocket::Kit&);

    void show (int pad);
    int pad() const { return padIndex; }

    std::function<void (int)> onChanged;       // volume, pan, tune or mode changed
    std::function<void (int)> onChop, onTrim;
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

    juce::OwnedArray<pocket::KnobView> knobs;   // volume, pan, tune
    pocket::PButton modes[3], chop { "Chop across pads" }, trim { "Trim" }, clear { "Clear", pocket::Look::ghost },
                    done { "Done", pocket::Look::ghost };
};
