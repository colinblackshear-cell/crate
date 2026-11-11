#pragma once

#include "Analog.h"
#include "InstrumentShell.h"
#include "RigSounds.h"
#include "BassView.h"

class RigEngine;
class SynthProcessor;
class BassProcessor;

// The Rig's Bass and Synth pages, as old analog synthesizers (see Analog.h): every setting of
// the tool on one panel, in boxed sections, with its arp or step sequencer and its keys. The
// Engine buttons on the nameplate flip between the tool's own sound and the instrument shell
// (your Arturia synths), which then plays the same keys, steps and arp.
class AnalogScreen : public juce::Component,
                     private juce::Timer
{
public:
    AnalogScreen (juce::AudioProcessorValueTreeState&, PluginSlot&, juce::MidiKeyboardState&, const std::vector<RigSounds::Sound>&,
                  const juce::String& model, const juce::String& title, const juce::String& trackName, juce::Colour accent);
    ~AnalogScreen() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const juce::String&)> onSay;

    // The engine: the tool's own sound, or the shell's plugin.
    void showEngine (bool plugin);
    bool isShowingShell() const { return shellShown; }
    InstrumentShell& getShell() { return shell; }

protected:
    struct Section
    {
        juce::String title;
        juce::Colour colour;
        std::vector<std::pair<juce::Component*, int>> items;   // and each one's width
        juce::Rectangle<int> bounds;
        bool sound = true;   // hidden when the shell shows
    };

    Section& section (const juce::String& title, juce::Colour, bool sound = true);
    analog::Knob& knob (Section&, const juce::String& id, const juce::String& name, int width = 64, const juce::String& low = {},
                        const juce::String& high = {});
    analog::Selector& selector (Section&, const juce::String& id, const juce::String& name, juce::StringArray labels = {}, int columns = 1);
    analog::Switch& toggle (Section&, const juce::String& id, const juce::String& name, bool invert = false);
    void add (Section&, juce::Component&, int width);

    // Lays the sections out in a row across `area`, sharing out what's spare.
    void layoutRow (const std::vector<Section*>&, juce::Rectangle<int> area);
    // The rows of the tool's sound, and what plays it (arp or sequencer), for the screen to place.
    virtual void layoutSound (juce::Rectangle<int>) = 0;
    virtual void layoutPlay (juce::Rectangle<int>) = 0;
    virtual int playHeight (int available) const = 0;
    virtual void tick() {}
    void say (const juce::String&);
    void updateEngineButtons();
    void showSoundMenu();
    void stepSound (int delta);

    juce::AudioProcessorValueTreeState& params;
    PluginSlot& slot;
    const std::vector<RigSounds::Sound>& sounds;
    juce::String model, title, trackName;
    juce::Colour accent;
    std::vector<std::unique_ptr<Section>> sections;
    std::vector<std::unique_ptr<juce::Component>> owned;
    analog::Keys keys;
    InstrumentShell shell;
    analog::Button builtIn { "Built-in" }, plugin { "Plugin" }, soundPrev { "<" }, soundNext { ">" }, soundMenu { "Sounds" };
    juce::Rectangle<int> panelArea, nameplate, soundLcd, keyBed;
    bool shellShown = false;

private:
    void timerCallback() override;
    juce::String shownSound;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalogScreen)
};

//==============================================================================
// The Synth: oscillators, mixer, filter, two envelopes, LFO, voices, output, and the arp with
// its 16-step rhythm.
class SynthScreen : public AnalogScreen
{
public:
    SynthScreen (RigEngine&);

    // The arp's rhythm: 16 steps (lit ones play) and accents under them.
    class RhythmLane : public juce::Component
    {
    public:
        explicit RhythmLane (SynthProcessor& p, juce::Colour c) : processor (p), colour (c) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        juce::Rectangle<float> cell (int step, int row) const;

    private:
        void click (juce::Point<float>, bool drag);
        SynthProcessor& processor;
        juce::Colour colour;
        bool dragValue = true;
        int dragRow = 0, lastStep = -1;
    };

private:
    void layoutSound (juce::Rectangle<int>) override;
    void layoutPlay (juce::Rectangle<int>) override;
    int playHeight (int available) const override;
    void tick() override;

    SynthProcessor& synth;
    Section *osc1 = nullptr, *osc2 = nullptr, *mixer = nullptr, *filter = nullptr, *filterEnv = nullptr, *ampEnv = nullptr, *lfo = nullptr,
            *voice = nullptr, *output = nullptr, *arp = nullptr, *rhythm = nullptr;
    RhythmLane lane;
    analog::Button shorter { "-" }, longer { "+" }, allOn { "All on" };
    juce::Label lengthLabel;
};

//==============================================================================
// The Bass: oscillator, filter, accent and glide, amp, LFO, output, the sequencer's settings,
// its 8 patterns and the step grid (notes, octaves, accents, slides).
class BassScreen : public AnalogScreen
{
public:
    BassScreen (RigEngine&);

private:
    void layoutSound (juce::Rectangle<int>) override;
    void layoutPlay (juce::Rectangle<int>) override;
    int playHeight (int available) const override;
    void tick() override;
    void editPattern (const std::function<void (crate::bass::Pattern&)>&);

    BassProcessor& bass;
    Section *osc = nullptr, *filter = nullptr, *accentGlide = nullptr, *amp = nullptr, *lfo = nullptr, *output = nullptr, *sequencer = nullptr,
            *patterns = nullptr;
    std::vector<std::unique_ptr<analog::Button>> patternButtons;
    analog::Button shorter { "-" }, longer { "+" }, randomButton { "Random" }, clearButton { "Clear" }, copyButton { "Copy to next" };
    juce::Label lengthLabel;
    BassView::StepGrid grid;
    juce::Random random;
};
