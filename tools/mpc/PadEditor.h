#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "BankModel.h"

// Settings for the selected pad, on two pages. Pad: name, volume, pan, tune, how it plays,
// and how it's saved. Sound + FX: its live sound (filter, attack, decay, and how much goes to
// the kit's delay and reverb), which you can turn while it plays, and the kit's delay and reverb.
class PadEditor : public juce::Component
{
public:
    explicit PadEditor (Project&);

    void setPad (int pad);
    void refresh();

    std::function<void()> onClear;
    std::function<void()> onLoadFile;
    // Cuts the pad's sound onto this pad and the ones after it: at its hits (0) or in equal slices.
    std::function<void (int slices)> onChop;
    // Opens the sound editor for the pad: trim, fades and effects.
    std::function<void()> onEditSound;
    // Plays the pad (the Sound + FX page's Play button).
    std::function<void()> onPlay;

    // Which page shows: false the pad's settings, true its sound and effects.
    void showSoundPage (bool);
    bool isSoundPage() const { return soundPage; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Pad& pad() { return project.currentBank().pads[(size_t) padIndex]; }
    void edited();

    Project& project;
    int padIndex = 0;

    juce::Label title;
    juce::Label info;
    juce::Label nameLabel { {}, "Name" };
    juce::TextEditor name;
    juce::Label volumeLabel { {}, "Volume" }, panLabel { {}, "Pan" }, tuneLabel { {}, "Tune" }, modeLabel { {}, "Plays" };
    juce::Slider volume, pan, tune;
    juce::ComboBox mode;
    juce::Label groupLabel { {}, "Cuts" };
    juce::ComboBox group;
    juce::Label timingLabel { {}, "Length" };
    juce::ComboBox timing;
    juce::Label timingInfo;

    static constexpr int kTimingAuto = 1, kTimingNormal = 2, kTimingBeats = 10;
    static constexpr std::array<double, 9> kBeatChoices { 0.25, 0.5, 1, 2, 4, 8, 16, 32, 64 };
    static juce::String beatsText (double beats);
    juce::ToggleButton mono { "Save as mono (half the memory)" };
    juce::ToggleButton reverse { "Reverse" };
    juce::ToggleButton normalize { "Normalize" };
    juce::TextButton load { "Load sound..." };
    juce::TextButton clear { "Clear pad" };
    juce::TextButton chop { "Chop across pads..." };
    juce::TextButton editSound { "Trim and effects..." };

    // Pages.
    bool soundPage = false;
    juce::TextButton padPageButton { "Pad" }, soundPageButton { "Sound + FX" };
    std::vector<juce::Component*> padPage() ;
    std::vector<juce::Component*> soundPageControls();

    // Sound + FX.
    juce::Label cutoffLabel { {}, "Filter" }, resonanceLabel { {}, "Reso" }, attackLabel { {}, "Attack" }, decayLabel { {}, "Decay" },
                delayLabel { {}, "Delay" }, reverbLabel { {}, "Reverb" }, soundInfo, kitFxTitle { {}, "The kit's delay and reverb" },
                delayTimeLabel { {}, "Echo" }, feedbackLabel { {}, "Repeats" }, roomLabel { {}, "Room" };
    juce::Slider cutoff, resonance, attack, decay, delaySend, reverbSend, feedback, room;
    juce::ComboBox delayTime;
    juce::TextButton playButton { "Play" }, resetSound { "Reset" };
    static constexpr std::array<double, 7> kDelayChoices { 0.25, 1.0 / 3.0, 0.5, 0.75, 1.0, 1.5, 2.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadEditor)
};
