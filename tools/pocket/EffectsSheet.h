#pragma once

#include "PocketLibrary.h"
#include "PocketSheet.h"
#include "PocketTheme.h"
#include "PocketWave.h"

// The bottom sheet: nine effect knobs on top, chopping below.
class EffectsSheet : public pocket::DragSheet
{
public:
    EffectsSheet();

    void setKnobs (const pocket::FxKnobs&);
    pocket::FxKnobs getKnobs() const;
    void resetKnobs();
    bool hearEffects() const { return hear.isToggled(); }
    void setHearEffects (bool on) { hear.setToggled (on); }
    float sensitivity() const { return (float) sens / 100.0f; }

    PocketWave chopWave;

    std::function<void()> onKnobsChange, onApply, onHearChanged, onFindHits, onClear, onDone;
    std::function<void (int)> onEven;
    std::function<void (juce::String)> onMessage;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Slider01 : juce::Component
    {
        int value = 55;
        std::function<void()> onChange;
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent& e) override { set (e); }
        void mouseDrag (const juce::MouseEvent& e) override { set (e); }
        void set (const juce::MouseEvent&);
    };

    void updateCount();

    juce::OwnedArray<pocket::KnobView> knobs;
    pocket::PButton hear { "Hear effects", pocket::Look::amber }, reset { "Reset" }, apply { "Apply", pocket::Look::green },
                    findHits { "Find hits", pocket::Look::amber }, even { "Even" }, clear { "Clear", pocket::Look::ghost },
                    done { "Done", pocket::Look::ghost };
    Slider01 slider;
    int sens = 55;
    int onCount = 0;
};
