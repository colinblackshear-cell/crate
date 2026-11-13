#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "Analog.h"
#include "RigEngine.h"
#include "Vintage.h"

class VocoderScreen;

// The Vocoder screen's Full window: every setting the vocoder has, on one page, in the order the
// sound goes through them. It takes the place of the vocoder's own Easy, Studio and Advanced views
// (Advanced was every setting as a plain list, Studio the main ones as knobs): here each setting
// is a knob, as on the pedalboard's rack, in a well of its own colour.
//
// Each part has its switch (the same as stomping its pedal) and Reset. Your voice (with Mic gain,
// Gate and the level), Tune with its tuner, Robot, the robot's Synth, Chords with the seven chord
// buttons, Rhythm, Filter, Vintage, Effects, and Out (the level, Blend, and Raw voice: back to just
// your voice). Along the bottom, the vocoder's keys, with Hold and Stop all. It scrolls when the
// window is too small to show it all.
class VocoderSettings : public juce::Component,
                        private juce::Timer
{
public:
    VocoderSettings (RigEngine&, VocoderScreen&);
    ~VocoderSettings() override;

    enum SectionId { voice, tune, robot, synth, chords, rhythm, filter, vintageMic, effects, out, numSections };
    static juce::String sectionName (int);

    void resized() override;
    void refresh();

    // As a click does (the tests use these).
    int numKnobs (int section) const;
    juce::StringArray shownIds() const;   // every setting on the page
    bool isSectionOn (int section) const;
    bool hasSwitch (int section) const;
    void pressSwitch (int section);
    void resetSection (int section);
    void pressRawVoice();
    void pressStopAll();
    void setHold (bool);
    bool isHolding() const;
    juce::MidiKeyboardState& getKeyState();

    class Section;
    class Keys;

private:
    void timerCallback() override;
    void layOut (int width);

    RigEngine& engine;
    VocoderScreen& screen;
    juce::Viewport viewport;
    juce::OwnedArray<Section> sections;
    std::unique_ptr<Keys> keys;
    juce::TextButton holdButton { "Hold" }, stopButton { "Stop all" }, rawVoice { "Raw voice" };
    std::unique_ptr<vintage::Knob> blend;
    juce::OwnedArray<juce::Component> ownedExtras;   // the tuner, the chord buttons
    juce::Component* chordPad = nullptr;
    juce::Rectangle<int> keysWell;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocoderSettings)
};
