#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "core/ui/Theme.h"

// The Rig's window: one transport across the top, the tools in tabs (MPC, Vocoder, Sampler),
// and a mixer with the keyboard routing along the bottom.
class RigView : public juce::Component,
                private juce::Timer,
                private juce::AudioProcessorListener
{
public:
    RigView (RigEngine&, juce::AudioDeviceManager&);
    ~RigView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Shows a file chooser and saves or opens a show.
    void saveShowAs();
    void openShow();
    // Opens newly plugged-in keyboards. Runs every couple of seconds.
    void openMidiInputs();

    // Re-reads everything from the engine (after loading a show).
    void refresh();

private:
    // One mixer channel: name, level fader, mute, meter, and Learn for its fader.
    class StripView : public juce::Component
    {
    public:
        StripView (RigEngine&, int strip, int faderControl, int muteControl, bool hasOnSwitch = false);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void tick();

        juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::TextButton mute { "Mute" }, learn { "Learn" }, on { "On" };

    private:
        RigEngine& engine;
        int strip, faderControl, muteControl;
        bool hasOn;
        float meter = 0;
    };

    void timerCallback() override;
    void showAudioSettings();
    void startLearning (int control, juce::Button&);
    void updateLearnButtons();
    void toggleSampling();
    void showKnobMenu();
    void finishKnobLearn();
    void say (const juce::String&);

    // Knob Learn watches for a setting being touched on screen.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex) override;

    RigEngine& engine;
    juce::AudioDeviceManager& devices;
    crate::Theme::LookAndFeel lookAndFeel;

    // Transport.
    juce::TextButton playButton { "Play" }, stopButton { "Stop" }, clickButton { "Click" };
    juce::TextButton learnPlay { "Learn" }, learnStop { "Learn" };
    juce::Label tempoLabel { {}, "Tempo" };
    juce::Slider tempo { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::TextButton tapButton { "Tap" }, sampleButton { "Sample" }, knobButton { "Knobs" };
    juce::Label position, midiLight;
    juce::TextButton audioButton { "Audio + MIDI" }, saveButton { "Save show" }, openButton { "Open show" };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // Routing.
    juce::Label padsLabel { {}, "Pads play MPC bank" }, keysLabel { {}, "Keys play" }, sourceLabel { {}, "Sampler records" },
                splitLabel { {}, "Split, below" };
    juce::ComboBox padBank, keysTo, sampleSource, splitNote, lowerKeysTo;

    // Knob Learn: waiting for a setting to be touched and a knob to be turned.
    bool learningKnob = false;
    int touchedTool = -1, touchedParameter = -1;
    std::pair<int, int> caughtKnob { 0, -1 };
    juce::uint32 sayUntil = 0;

    std::vector<std::unique_ptr<StripView>> stripViews;

    std::vector<double> taps;
    juce::uint32 lastTap = 0;
    juce::Array<juce::MidiDeviceInfo> knownInputs;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigView)
};
