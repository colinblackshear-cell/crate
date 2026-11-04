#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "core/ui/Theme.h"
#include "core/ui/ParamControls.h"

// The Rig's window: one transport across the top, the tools in tabs (the Stage, with every tool
// on one screen, then MPC, Vocoder, Bass...), and a mixer with the keyboard routing along the bottom.
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

    // A tab in its own window (for a second screen), and back again; the windows a loaded
    // show had (in the engine's screens) are put back by restoreWindows.
    void popOutTab (const juce::String& tab);
    void bringBack (const juce::String& tab);
    bool isInOwnWindow (const juce::String& tab) const;
    void restoreWindows();

private:
    // One mixer channel: name, level fader, mute, meter, and Learn for its fader.
    class StripView : public juce::Component
    {
    public:
        // `onSwitch`, if given, is an on/off switch on the strip (the vocoder's, the input's).
        StripView (RigEngine&, int strip, int faderControl, int muteControl, std::atomic<bool>* onSwitch = nullptr);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void tick (float meterLevel);

        juce::Slider level { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
        juce::TextButton mute { "Mute" }, learn { "Learn" }, on { "On" }, fxButton { "FX" };
        juce::ComboBox jack;   // the Input strip's: which input it listens to
        const int strip;

    private:
        RigEngine& engine;
        int faderControl, muteControl;
        std::atomic<bool>* onFlag;
        float meter = 0;
    };

    // One strip's effects, in a row above the mixer: knobs for the filter, drive, crush,
    // delay and reverb, and the beat repeat's buttons.
    class FxPanel : public juce::Component
    {
    public:
        FxPanel (RigEngine&, int strip);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();

        const int strip;
        juce::TextButton resetButton { "Reset" }, closeButton { "Close" };

    private:
        void setRepeat (int choice);

        RigEngine& engine;
        std::vector<std::unique_ptr<crate::ParamKnob>> knobs;
        std::unique_ptr<crate::ParamChoice> delayTime;
        juce::OwnedArray<juce::TextButton> repeatButtons;
    };
    void showFx (int strip);

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
    juce::TextButton tapButton { "Tap" }, sampleButton { "Sample" }, knobButton { "Knobs" }, recordButton { "Record set" };
    void toggleRecordSet();
    void updateRecordButton();
    juce::Label position, midiLight;
    juce::TextButton audioButton { "Audio + MIDI" }, saveButton { "Save show" }, openButton { "Open show" };

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // Routing.
    juce::Label padsLabel { {}, "Pads play MPC bank" }, keysLabel { {}, "Keys play" }, sourceLabel { {}, "Sample" },
                splitLabel { {}, "Split, below" };
    juce::ComboBox padBank, keysTo, sampleSource, splitNote, lowerKeysTo, sampleDest, sampleBars;
    void updateSampleButton();

    // Knob Learn: waiting for a setting to be touched and a knob to be turned.
    bool learningKnob = false;
    int touchedTool = -1, touchedParameter = -1;
    std::pair<int, int> caughtKnob { 0, -1 };
    juce::uint32 sayUntil = 0;

    std::vector<std::unique_ptr<StripView>> stripViews;
    std::unique_ptr<FxPanel> fxPanel;
    class LooperView* looper = nullptr;   // owned by the tabs
    class SongView* song = nullptr;
    class StageView* stage = nullptr;
    void showTab (const juce::String& name);

    // Each tab's page. A page can pop out into its own window (to put it on a second screen);
    // its tab then holds a note saying where it went, and closing the window brings it back.
    class ToolWindow;
    class Placeholder;
    struct Page
    {
        juce::String name;
        juce::Colour colour;
        std::unique_ptr<juce::Component> content;
        std::unique_ptr<Placeholder> placeholder;
        std::unique_ptr<ToolWindow> window;
    };
    std::vector<Page> pages;
    void addPage (const juce::String& name, juce::Colour, juce::Component*);
    int pageIndex (const juce::String& name) const;
    void popOut (int page, juce::Rectangle<int> bounds = {});
    void dockBack (int page);
    // Keeps the engine's list of popped-out windows up to date (it's saved with the show), and
    // puts the windows back as a loaded show had them.
    void storeWindows();
    juce::TextButton popOutButton { "Pop out" };

    // Each strip's meter (0..1, falling slowly), shared by the mixer and the Stage.
    std::array<float, RigEngine::numStrips> meters {};

    std::vector<double> taps;
    juce::uint32 lastTap = 0;
    juce::Array<juce::MidiDeviceInfo> knownInputs;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigView)
};
