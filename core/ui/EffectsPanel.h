#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../SoundFx.h"

namespace crate
{

// A panel of effects, each in a section you can switch on and off, plus presets. It edits a
// SoundFx::Settings; moving any control switches its section on. Sections that are off add
// nothing to the sound.
class EffectsPanel : public juce::Component
{
public:
    EffectsPanel();
    ~EffectsPanel() override;

    // What the panel is set to, with switched-off sections left neutral.
    SoundFx::Settings getSettings() const;
    bool hasEffects() const { return ! getSettings().isNeutral(); }

    // Switches every section off and puts the controls back to their starting values.
    void reset();
    // Picks a preset by name (the same names as the presets menu). Returns false if unknown.
    bool loadPreset (const juce::String& name);
    static juce::StringArray presetNames();

    void setApplyText (const juce::String& text) { applyButton.setButtonText (text); }
    bool isPreviewOn() const { return previewButton.getToggleState(); }
    void setPreviewOn (bool on) { previewButton.setToggleState (on, juce::sendNotification); }

    std::function<void()> onChange;          // any control or section switch changed
    std::function<void()> onApply;           // the Apply button
    std::function<void (bool)> onPreviewChanged;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Param
    {
        juce::String name, suffix;
        double min, max, initial, step, skewCentre;
        std::function<void (SoundFx::Settings&, float)> write;
    };

    struct Section : juce::Component
    {
        juce::ToggleButton toggle;
        std::vector<Param> params;
        juce::OwnedArray<juce::Slider> sliders;
        juce::OwnedArray<juce::Label> labels;
        void resized() override;
        int preferredHeight() const { return 30 + 28 * params.size(); }
    };

    Section& addSection (const juce::String& name, std::vector<Param>);
    void set (const juce::String& section, int param, double value);
    void changed();

    juce::Label title { {}, "Effects" };
    juce::ComboBox presets;
    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<Section> sections;
    juce::TextButton previewButton { "Hear effects" }, applyButton { "Apply" }, resetButton { "Reset" };
    bool loading = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EffectsPanel)
};

} // namespace crate
