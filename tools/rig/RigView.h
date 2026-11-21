#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "StripFxPanel.h"
#include "core/ui/Theme.h"
#include "core/ui/ParamControls.h"
#include "RigPanels.h"
#include "RigNavigation.h"
#include "ChordsView.h"

// The Rig's window, built like a piece of 70s/80s studio gear: the top bar (your show, transport,
// tempo, where the song is, VU meters), the view buttons (Play, Edit, Song, Mix; F1-F4) with
// Sounds (a drawer over any view), Settings and the guide, the track rail down the left (every
// instrument; pick one and the keys, knobs and Edit follow it), the screen, and the song's
// sections along the bottom.
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
    // Opens newly plugged-in keyboards (and closes the ones set to Don't listen). Runs every
    // couple of seconds. A controller seen for the first time (not a Launchkey) opens MIDI
    // controllers on it, once.
    void openMidiInputs();
    // MIDI controllers (Settings > Audio & MIDI, or the keyboard lamp at the top right): what each
    // controller is. `newDevice` puts a just-plugged-in one first and says so.
    void showControllers (const juce::String& newDevice = {});
    class ControllersPanel* getControllersPanel() const;

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

    // The views along the top: "Play", "Edit" (the picked track in full), "Song", "Mix". getView
    // is the one lit ("" on the Guide or Settings).
    void showView (const juce::String& view);
    juce::String getView() const;
    // The track picked on the rail (a GrooveView::Track), and picking one: the keys and knobs
    // play it, and on Edit its instrument shows.
    int getTrack() const;
    void pickTrack (int track);
    // A Song box, double-clicked: its track (the lane's), on the pattern the box plays (clip, -1
    // leaves it), in Edit or Play. A loop layer opens its start/end editor in Edit.
    void openSongBox (int lane, int clip, bool edit);
    // Settings > Your plugins: your Arturia synths (and any instrument) onto the Bass or Synth,
    // effects (Quake...) onto any track's effects, and the scan.
    void showPluginsMenu();
    // Loads an instrument plugin onto the Bass or Synth and opens its own screen in Edit.
    void loadInstrumentPlugin (int track, const juce::PluginDescription&);
    static int trackForLane (int lane);
    // The Song lane a track plays on (Loops: the layer picked on Play).
    int laneForTrack (int track) const;
    // A click on a track on the rail: picks it, and the view shows it (Play its steps, Edit its
    // tool, Song its row, Mix its effects; Settings and the like go to Play). A double-click
    // opens its whole tool in Edit, from any view.
    void railClicked (int track);
    void railDoubleClicked (int track);

    // Capture (under the tracks). Rec take: the picked track, as you play it, onto the next empty
    // loop from the next bar; again stops it at the end of that bar. Returns the loop (-1: none).
    int pressRecTake();
    int getTakeLayer() const { return takeLayer; }
    // The last take of a track (-1: none yet).
    int lastTakeOf (int track) const;
    // To Song: the menu (into a section, every section, a new one; the take too). toSong does one:
    // `section` -1 for a new section at the end, -2 for every section; `take` puts the track's
    // last take (its loop) there instead of what the track plays. Returns the section (-1: none).
    void showToSongMenu();
    int toSong (int section, bool take = false);
    // Rec into (the song strip along the bottom, and Rec take once there's a song): records the
    // picked track into a section of the song, from its top, onto a loop of its own; with Cycle
    // on the section plays round and every time round is a new take (RigTakes). Again stops it.
    // Returns the loop it records onto (-1: none, or it stopped).
    int pressRecInto();
    // Where the next take goes: the section (the one picked on the strip or the Song screen, or
    // the one playing, or the first; -1 with no song) and the loop (the takes this track already
    // has there, or the first free loop; -1 when none is free).
    int recTargetSection() const;
    int recTargetLayer() const;
    void pickRecSection (int section);
    // `layer` -1: a new layer (or the track's takes there).
    void setRecTarget (int section, int layer);
    void showRecTargetMenu (juce::Component& from);
    void showTakesMenu (juce::Component& from);
    // Cycle (the strip): the target section plays round, and every pass records a new take.
    void toggleCycle();
    bool isCycleOn() const { return recCycle; }
    // The Song screen's Add a layer with: `track` opens in Edit, ready to record into the picked
    // section, with the song playing round it.
    void addLayerWith (int track);
    // Rec moves (the rail): records the knobs you turn from the next bar, or stops at the end of
    // the bar. showAutomation opens the lanes (Cmd+Shift+A).
    void pressRecMoves();
    void showAutomation();
    // The walkthrough's steps (the Guide): going to the place a step shows.
    void showTourPlace (const juce::String& place);
    // The page that shows a track in full ("MPC", "Drum machine", "Bass"...).
    static juce::String editPageFor (int track);
    // The Sounds drawer, over the right of whichever view is showing (F5).
    void showSounds (bool show);
    bool isSoundsOpen() const { return soundsOpen; }
    // The chord builder, over the bottom of whichever view is showing (F7).
    void showChords (bool show);
    bool isChordsOpen() const { return chordsOpen; }
    ChordsView& getChordsView() { return *chordsDrawer; }
    const rigpanels::TrackRail& getRail() const { return rail; }
    // The show menu (the show's name in the top bar): New song, Open, Save, Save as, Export, Guide.
    void showShowMenu();
    juce::Component* getCurrentContent() const { return tabs.getCurrentContentComponent(); }
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

    // A one-tool plugin (Kaleidobox Vocoder, MPC, Drums, Bass, Synth on their own Logic tracks):
    // only its track on the rail, Edit and Mix, and the menu at the top gets its sound from a
    // Kaleidobox in the song, a saved show or a tool sound file, or sends it back.
    bool toolMode() const;
    void showToolMenu();
    // Tool sounds (RigEngine::saveToolSound) as files, in the whole box too (the show menu).
    // `strip` is the tool's StripId. The ones returning a String say what happened.
    void keepToolSound (int strip);
    void openToolSound();
    juce::String loadToolSoundFile (const juce::File&);
    void getToolFromShowFile();
    juce::String getToolFromShow (const juce::File& showFile, int strip);
    // The tool the show menu's "Keep ... as a tool sound" keeps: this plugin's, or the picked track's (-1: none).
    int toolSoundStrip() const;

    // Song > Import a song from Logic: asks about saving this song, picks the folder of stems
    // (and MIDI file), and puts it on as a new song (rigimport). importFromLogic does the folder.
    void askImportFromLogic();

    // A one-tool plugin's Rec (the top bar): records the tool, as you hear it and the notes it
    // plays, from the next bar until you press again or Logic stops, into To Logic. Then the
    // footer's two chips drag it into Logic: the MIDI onto the tool's own track (a region that
    // plays through it), the audio anywhere (an audio track). Both are named for the bar they
    // start on. printTake tidies a finished take into those two files (tests call it).
    void printTake();
    juce::File getPrintedMidi() const { return printedMidi; }
    juce::File getPrintedAudio() const { return printedAudio; }
    juce::String importFromLogic (const juce::File& folder);

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
    int takeLayer = -1, takeTrack = -1;
    bool takeStarted = false;   // the take's layer has been seen armed or recording
    bool takeKeptSaid = false;  // told you the song left the take alone
    int tourStop = 0;   // the walkthrough's stop you were on
    std::array<int, 9> lastTake { -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // by track
    void updateCaptureButtons();
    int recSection = -1, recLayer = -1;   // the Rec into target as picked (-1: follow the song)
    bool recCycle = true, recFree = false, recAsSound = false;
    void openNoteEditor (int strip, double startBar);
    juce::Component::SafePointer<juce::DialogWindow> noteEditorWindow;
    int takesSeen = 0;
    void updateRecInto();
    juce::Component::SafePointer<juce::Component> automationPanel;
    std::unique_ptr<juce::FileChooser> toolChooser;
    juce::File printedMidi, printedAudio;
    std::unique_ptr<juce::Component> dragMidi, dragAudio;
    void startToolMode();
    juce::Component::SafePointer<juce::Component> quantizePanel;
    std::unique_ptr<class HelpPanel> helpPanel;
    // Hover over a button for a moment and it says what it does.
    juce::SharedResourcePointer<juce::TooltipWindow> tooltips;
public:
    class MixerView* getMixer() const { return mixer; }
    // Kaleidobox Pocket on the iPhone (Settings > iPhone), in the app only (null in Logic).
    class RigPhoneLink* getPhoneLink() const { return phoneLink.get(); }
    // Takes the effects rack off the Mixer, if one is showing.
    void closeMixerPanel();
    // Opens (or closes) the how-to guide, on the current screen's guide.
    void showHelp (bool show);
    // Cmd+Z: the last Clear on Play, or the last overdub on a loop. False if nothing to undo.
    bool undoLast();
    HelpPanel* getHelp() const { return helpPanel.get(); }
private:
    bool writeShowTo (const juce::File&);

    // Knob Learn watches for a setting being touched on screen.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int parameterIndex) override;
    // (and Macros' Assign, for where a setting was let go of)
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int parameterIndex) override;

    RigEngine& engine;
    juce::AudioDeviceManager* devices;
    float micMeter = 0.0f;
    // First, before any tool is made: the tools inside take the panel's warm wells.
    struct WarmWells { WarmWells() { crate::Theme::useWarmWells(); } } warmWells;
    crate::Theme::LookAndFeel lookAndFeel;

    // The frame.
    vintage::LookAndFeel vintageLook;
    rigpanels::Header header;
    rigpanels::ScreenBar screenBar;
    rigpanels::Footer footer;
    rigpanels::TrackRail rail;
    class Drawer;
    std::unique_ptr<Drawer> soundsDrawer;
    bool soundsOpen = false;
    std::unique_ptr<ChordsView> chordsDrawer;
    bool chordsOpen = false;
    juce::String viewOfPlace (const juce::String& place) const;
    int trackOfPlace (const juce::String& place) const;
    bool railShown() const;
    void showMoreMenu();
    void showPopOutMenu();
    class MoreView* moreView = nullptr;   // owned by its page
    void setDarkPanel (bool);
    juce::String currentPage() const;
    // What's recording now, and the screen it's on ({} when nothing is).
    std::pair<juce::String, juce::String> whatsRecording() const;
    void nudgeTempo (double);
public:
    // A click on the tempo: Ramp to... (a glide over some bars while it plays), or stop one.
    void showTempoMenu();
    void startTempoRamp (double target, int bars);
private:
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
    juce::TextButton clickButton { "Click" }, micButton { "Mic off" }, timingButton { "Timing" };
    void showTimingMenu();
    void updateTimingButton();
    juce::TextButton learnPlay { "Learn" }, learnStop { "Learn" };
    juce::Label learnPlayLabel { {}, "Play key" }, learnStopLabel { {}, "Stop key" };   // on Settings
    juce::TextButton sampleButton { "Sample" }, knobButton { "Knobs" };
    void toggleRecordSet();
    void updateRecordButton();
    juce::TextButton controllersButton { "MIDI controllers..." };
    juce::Component::SafePointer<juce::DialogWindow> controllersWindow;
    juce::TextButton audioButton { "Audio + MIDI" }, saveButton { "Save show" }, openButton { "Open show" };
    juce::TextButton darkButton { "Dark panel" }, popOutButton { "Second screen..." }, pluginsButton { "Your plugins..." };
    juce::Label pluginsLabel { {}, "Plugins" }, pluginsHint;
    // Settings > iPhone: the four numbers Kaleidobox Pocket types to link, and Unlink.
    std::unique_ptr<class RigPhoneLink> phoneLink;
    juce::Label phoneCode;
    juce::TextButton phoneUnlink { "Unlink" };
    juce::String phoneHint;
    void updatePhoneCard();
    int phoneRecState (const juce::String& target) const;
    juce::String keyboardName;
    bool keyboardFound = false, keyboardActive = false;
    float vuLevel = 0;

    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };

    // Routing.
    juce::Label padsLabel { {}, "Pads play" }, keysLabel { {}, "Keys play" }, sourceLabel { {}, "From" },
                splitLabel { {}, "Split below" };
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
    juce::Component* pageContent (const juce::String& name) const
    {
        const int i = pageIndex (name);
        return i >= 0 ? pages[(size_t) i].content.get() : nullptr;
    }
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
    juce::StringArray listenedInputs;
    int ticks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RigView)
};
