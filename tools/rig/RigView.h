#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "StripFxPanel.h"
#include "core/ui/Theme.h"
#include "core/ui/ParamControls.h"
#include "RigPanels.h"
#include "RigNavigation.h"

// The Rig's window, built like a piece of 70s/80s studio gear: the top bar (transport, tempo,
// where the song is, VU meters), five big screen buttons (Groove, Vocoder, Looper, Sampler + MPC,
// Mixer; F1-F5), the screen, and the song parts with "Live now" along the bottom. Everything else
// (the MPC, Bass and Synth in full, the song list, the sound bank and the
// keyboard set-up) is under More, and the Mixer reaches every one of them too.
class RigView : public juce::Component,
                private juce::Timer,
                private juce::AudioProcessorListener
{
public:
    // `devices` is the app's audio and MIDI setup; null when the Rig is a plugin in Logic
    // (Logic picks the audio and MIDI then).
    RigView (RigEngine&, juce::AudioDeviceManager* devices);
    ~RigView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // Shows a file chooser and saves or opens a show.
    void saveShowAs();
    // Saves to the show you opened or last saved (asks for a name the first time): the show
    // and its loops, together.
    void saveShow();
    void exportForLogic();
    juce::File getCurrentShow() const { return currentShow; }
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
    // The page showing ("Guide", "Groove", "MPC"...), and going to one.
    juce::String getPage() const { return currentPage(); }
    void goTo (const juce::String& page) { showTab (page); }
    class GuideView* getGuide() const { return guide; }

    // Back and forward through the screens you've been on (the arrows by the screen buttons,
    // Cmd+[ and Cmd+]). A place is a page, or the Sampler's editor, or the Looper's timeline
    // or song grid.
    void goBack();
    void goForward();
    juce::String getPlace() const { return currentPlace(); }
    const RigNavigation& getNavigation() const { return navigation; }

    // New song (the top bar): asks whether to save the song you're on, then clears the Rig to a
    // blank one and asks what to start with. `newSong` does it without asking: saving first
    // (as the show you opened, or a new one in the shows folder) when `saveFirst`, and then
    // opening New pattern's menu when `askWhatNext`.
    void askNewSong();
    void newSong (bool saveFirst, bool askWhatNext = true);
    // New pattern (the top bar): a menu of what to start (songstart::Start), and keeping
    // what's playing as the next song part. startPattern does one, as the menu does.
    void showPatternMenu();
    void startPattern (int start);
    void keepAsSongPart();

    // Quantize (the top bar's button, Cmd+U, and the tools' own Quantize buttons). What it works
    // on follows the screen: the Groove's picked track, the Looper's picked loop (or all of
    // them), the MPC, drum machine, Bass or Synth page. showQuantize opens the panel on a target
    // (by `from`); quantizeNow does it straight away and says what it did.
    int quantizeTarget() const;
    void showQuantize (int target, juce::Component* from = nullptr);
    juce::String quantizeNow();
    class QuantizePanel* getQuantizePanel() const;

private:
    // One strip's effects, along the bottom of the Mixer.
    class FxPanel : public StripFxPanel
    {
    public:
        FxPanel (RigEngine&, int strip);
        const int strip;
    };
    void showFx (int strip);
    // A loop layer's effects, on the Mixer the same way.
    void showLayerFx (int layer);

    void timerCallback() override;
    void showAudioSettings();
    // After the mic is switched on: a reminder about headphones, or why nothing comes in.
    void sayMicOn();
    void startLearning (int control, juce::Button&);
    void updateLearnButtons();
    void toggleSampling();
    void showKnobMenu();
    void finishKnobLearn();
    void say (const juce::String&);
    juce::File currentShow;
    juce::Component::SafePointer<juce::Component> quantizePanel;
    std::unique_ptr<class HelpPanel> helpPanel;
    // Hover over a button for a moment and it says what it does.
    juce::SharedResourcePointer<juce::TooltipWindow> tooltips;
public:
    class MixerView* getMixer() const { return mixer; }
    // Takes the effects rack off the Mixer, if one is showing.
    void closeMixerPanel();
    // Opens (or closes) the how-to guide, on the current screen's guide.
    void showHelp (bool show);
    HelpPanel* getHelp() const { return helpPanel.get(); }
private:
    bool writeShowTo (const juce::File&);

    // Knob Learn watches for a setting being touched on screen.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex) override;

    RigEngine& engine;
    juce::AudioDeviceManager* devices;
    float micMeter = 0.0f;
    crate::Theme::LookAndFeel lookAndFeel;

    // The frame.
    vintage::LookAndFeel vintageLook;
    rigpanels::Header header;
    rigpanels::ScreenBar screenBar;
    rigpanels::Footer footer;
    void showMoreMenu();
    void showPopOutMenu();
    class MoreView* moreView = nullptr;   // owned by its page
    void setDarkPanel (bool);
    juce::String currentPage() const;
    void nudgeTempo (double);
    void pageChanged();
    bool stageShown = false;
    RigNavigation navigation;
    juce::String currentPlace() const;
    void goToPlace (const juce::String&);
    void updateNavButtons();

    // The Setup page (under More, once called the Mixer): the keyboard routing, Sample, knob
    // Learn, the click, the mic, and the show's files. The levels are on the Mixer (F5).
    class SetupPage;
    SetupPage* setupPage = nullptr;   // owned by its page
    void layoutSetup (juce::Rectangle<int>);
    juce::TextButton clickButton { "Click" }, micButton { "Mic off" };
    juce::TextButton learnPlay { "Learn" }, learnStop { "Learn" };
    juce::Label learnPlayLabel { {}, "Play key" }, learnStopLabel { {}, "Stop key" };
    juce::TextButton sampleButton { "Sample" }, knobButton { "Knobs" };
    void toggleRecordSet();
    void updateRecordButton();
    juce::TextButton audioButton { "Audio + MIDI" }, saveButton { "Save show" }, openButton { "Open show" };
    juce::String keyboardName;
    bool keyboardFound = false, keyboardActive = false;
    float vuLevel = 0;

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

    std::unique_ptr<FxPanel> fxPanel;
    std::unique_ptr<StripFxPanel> layerFxPanel;
    int layerFxShown = -1;
    class MixerView* mixer = nullptr;   // the Mixer (F5), owned by its page
    class LooperView* looper = nullptr;   // owned by the tabs
    class SongView* song = nullptr;
    class EffectsView* effects = nullptr;
    // The Effects screen, on one sound's pedals.
    void showEffectsFor (int strip, int loop = -1);
    class SamplerScreen* samplerScreen = nullptr;
    class VocoderScreen* vocoderScreen = nullptr;   // owned by the tabs
    class GrooveView* groove = nullptr;
    class GuideView* guide = nullptr;   // the front page
    void showTab (const juce::String& name);
    void openTool (const juce::String& name);

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

    // Each strip's meter (0..1, falling slowly), shared by the mixer and the Groove cards.
    std::array<float, RigEngine::numStrips> meters {};

    std::vector<double> taps;
    juce::uint32 lastTap = 0;
    juce::Array<juce::MidiDeviceInfo> knownInputs;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigView)
};
