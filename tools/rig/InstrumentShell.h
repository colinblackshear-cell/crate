#pragma once

#include "Analog.h"
#include "PluginSlot.h"

// The instrument shell: a home for your own instrument plugins (Arturia's V Collection and
// Pigments, Cymatics, anything that's an Audio Unit or VST3) inside the Bass or the Synth.
// Whatever's loaded plays that track's notes: its keys, its steps or arp, its song parts, and it
// goes through the track's strip, effects, recorder and stems like the built-in sound.
//
// On its panel: what's loaded and its preset (step through them, or pick from the list), its
// own window, a level, and 8 macro knobs, mapped by themselves to the plugin's most useful
// settings (Arturia's Brightness, Timbre, Time and Movement first). Click a knob's name to put
// another setting on it.
class InstrumentShell : public juce::Component,
                        private juce::Timer
{
public:
    InstrumentShell (PluginSlot&, const juce::String& trackName, juce::Colour accent);
    ~InstrumentShell() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String&)> onSay;
    // Something loaded, cleared or switched (the screen updates its engine switch).
    std::function<void()> onChanged;

    void showLoadMenu();
    void showPresetMenu();
    void showMacroMenu (int knob);
    void stepPreset (int delta);
    // Puts the knobs back on the plugin's settings (tests use this after a fake loads).
    void refresh();

private:
    void timerCallback() override;
    void say (const juce::String&);
    juce::String signature() const;

    PluginSlot& slot;
    juce::String trackName;
    juce::Colour accent;

    analog::Button load { "Load plugin" }, window { "Open window" }, remove { "Remove" }, prev { "<" }, next { ">" },
        presets { "Presets" }, autoMap { "Auto-map" };
    std::vector<std::unique_ptr<analog::Knob>> macros;
    analog::Knob level { "Level", analog::colours::legend };
    juce::Rectangle<int> lcdArea, hintArea, macroArea;
    juce::String shown;
    std::array<bool, PluginSlot::kMacros> dragging {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentShell)
};
