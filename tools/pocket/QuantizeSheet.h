#pragma once

#include "PocketLoopTools.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// QUANTIZE on an instrument (SYNTH, BASS, PADS): a bottom sheet over the part's notes or hits that are already in it.
// GRID (1/4 to 1/32, and the triplets 1/8T and 1/16T), STRENGTH (halfway, or all the way) and SWING, then APPLY moves
// them onto the grid. The choices are kept for the next time. The app (PocketApp) shows it and does the work.
class QuantizeSheet : public pocket::DragSheet
{
public:
    QuantizeSheet();

    pocket::QuantizeSpec spec;
    std::function<void (const pocket::QuantizeSpec&)> onApply;   // the app moves the notes, says so, and closes the sheet
    std::function<void()> onCancel;

    enum Id { none = 0, gridBase = 1, half = 10, full, swingId, applyId, cancelId };
    // What the buttons do (the self-checks use these too).
    void press (int id);
    juce::Component* chip (int id);
    // Opens on `what` ("BASS") with `count` notes or hits in it (the word for them: "note" / "hit").
    void show (const juce::String& what, const juce::String& noun, int count);
    void refresh();
    int preferredHeight() const { return 392; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::String title, nounText;
    int itemCount = 0;
    pocket::PButton grids[pocket::kQuantizeGrids];
    pocket::PButton halfB { "50%" }, fullB { "100%" }, swingB { "SWING" };
    pocket::PButton applyB { "APPLY", pocket::Look::amber }, cancelB { "CANCEL", pocket::Look::ghost };
    juce::Rectangle<int> gridLabel, strengthLabel, hintArea;
};
