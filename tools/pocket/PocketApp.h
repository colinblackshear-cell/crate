#pragma once

#include "BeatsScreen.h"
#include "EffectsSheet.h"
#include "EffectsScreen.h"
#include "IdeasScreen.h"
#include "LinkScreens.h"
#include "LoopsScreen.h"
#include "PadSheet.h"
#include "PocketDrawer.h"
#include "PocketHold.h"
#include "PocketAudio.h"
#include "PocketFrame.h"
#include "PocketHost.h"
#include "PocketIdeas.h"
#include "PocketLibrary.h"
#include "PocketSettings.h"
#include "PocketTheme.h"
#include "RecordScreen.h"
#include "SoundsScreen.h"
#include "TempoSheet.h"
#include "SoundBankSheet.h"
#include "VocoderScreen.h"
#include "SingScreen.h"
#include "SamplerScreen.h"
#include "SynthScreen.h"
#include "ChordsScreen.h"
#include "DroneScreen.h"
#include "SongScreen.h"
#include "ChordEditSheet.h"
#include "DrumKitSheet.h"
#include "PocketKey.h"
#include "PocketIntro.h"
#include "PocketLesson.h"
#include "core/visuals/VisualsView.h"

// The whole phone app, tool first: a slim header (the menu icon and the song, the bar and where the next take goes,
// the Mac link), the tool on screen, the thumb bar (BPM, CLICK, REC, PLAY, UNDO) and the tool row (LOOPS,
// DRUMS, PADS, SAMPLER, SYNTH, CHORDS, VOCODER, EFFECTS). REC records whatever tool is on screen onto the next empty loop
// layer, in time (on SAMPLER it samples a sound from the mic for the pads).
// Your songs, the sound recorder, every vocoder setting and the pages the drawer opens (Sounds, Remote, Mac link,
// Settings) are pages without the thumb bar. The song drawer slides in from the left (menu icon, the song's name, or a
// swipe from the left edge). A long press on a loop layer, a pad, a chord pad or a song opens a hold menu.
class PocketApp : public juce::Component, private PocketHost
{
public:
    struct Options
    {
        juce::File libraryRoot;       // empty = the app's Documents folder
        bool audio = true;            // false = no sound card (screenshots)
        bool phoneUi = false;         // with audio false: still the phone's title, first kit and animations (POCKET_STRESS)
    };

    PocketApp() : PocketApp (Options()) {}
    explicit PocketApp (Options);
    ~PocketApp() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    // Screenshot mode: fills a demo library, then writes one PNG per screen into `folder`.
    static void fillDemoLibrary (pocket::Library&);
    void writeSnapshots (const juce::File& folder);
    pocket::Library& library() { return lib; }

private:
    friend class PocketStress;   // POCKET_STRESS (PocketStress.cpp): drives the real handlers with an audio thread running

    // The phone's keyboard goes away with a swipe down anywhere, or a tap outside the text box
    // (the number pad has no Return key).
    struct KeyboardDismisser : juce::MouseListener
    {
        static juce::TextEditor* focusedEditor()
        {
            return dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent());
        }
        static bool inside (juce::Component* c, juce::Component* editor)
        {
            return c != nullptr && (c == editor || editor->isParentOf (c));
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            swiped = false;
            if (auto* ed = focusedEditor(); ed != nullptr && ! inside (e.originalComponent, ed))
                ed->giveAwayKeyboardFocus();
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (swiped || e.getDistanceFromDragStartY() < 40 || std::abs (e.getDistanceFromDragStartX()) > e.getDistanceFromDragStartY())
                return;
            if (auto* ed = focusedEditor())
            {
                swiped = true;
                ed->giveAwayKeyboardFocus();
            }
        }
        bool swiped = false;
    };
    KeyboardDismisser keyboardDismisser;

    class SettingsScreen;
    class Toast;
    void hideToast();
    bool toastHasAction() const;
    void pressToastAction();
    class Dim;

    // What's on screen: a tool (with the thumb bar), or one of the pages around them.
    // (more: Sounds, Remote, Mac link or Settings, whichever morePageShown says)
    enum class Page { songs, tool, recorder, vocoderSettings, more };
    using Tool = pocket::Tool;

    //==========================================================================
    // PocketHost
    void toast (const juce::String&) override;
    void openSheet() override;
    void closeSheet() override;
    void editSound (const juce::String&) override;
    void sendToPad (const juce::String&) override;
    void openPadSheet (int pad) override;
    void closePadSheet() override;
    void sendSoundToMac (const juce::String& soundId, const juce::String& dest) override;
    void sendToMac (pocket::MacSend) override;
    void openMacLink() override;
    void holdMenu (const pocket::HoldSpec&, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight) override;
    void toastAction (const juce::String& text, const juce::String& button, std::function<void()> then) override;
    void openSamplePage (int pad) override;
    void editPadSlice (int pad) override;
    void togglePlay() override;
    void padSoundEdited (const juce::String& soundId) override;
    void padSoundSwapped (const juce::String& oldId, const juce::String& newId) override;
    void backToPads (int pad) override;
    void openSoundBank() override;
    void showLoops() override { showTool (Tool::loops); }
    void showVocoder() override { showPage (Page::vocoderSettings); }
    void closeVocoderSettings() override { showTool (Tool::vocoder); }
    void openRecorder() override;
    void openSampler (int pad) override;
    bool micReady() override { return micOn; }
    void askForMic (std::function<void()> then) override;
    pocket::Route micRoute() override { return route; }
    bool hearingMyself() override { return micOn && micHear; }
    void setHearMyself (bool) override;
    void micOff() override;
    void micUseChanged() override { applyMic(); }
    double songBpm() override { return beats.drums().bpm; }

    //==========================================================================
    // Pages and tools
    void showPage (Page, int morePage = 0);   // morePage: 1 sounds, 2 remote, 3 mac link, 4 settings
    void goBack();                            // from Sounds, Remote, Mac link, Settings or the recorder: where you were
    void showTool (Tool, bool askMic = true);
    void showIdeasList() { showPage (Page::songs); }
    bool thumbShown() const { return page == Page::tool; }
    bool toolRowShown() const { return page != Page::songs; }
    juce::Rectangle<int> contentBounds() const;
    void refreshToolRow();
    void noteStep();   // keeps Your songs' progress picture (pocket::Ideas steps) up to date
    void stopEverything();

    // Ideas (Your songs)
    void openIdea (const juce::String& id);
    void newIdea();          // + NEW SONG
    void catchIdea();        // My ideas' CATCH AN IDEA: a new song, LOOPS on the mic, recording (free length) at once
    void startSpark (const crate::spark::Spark&);   // START THIS SPARK: a new song with its tempo, key and kit, on its first tool
    void startWithTool (Tool, bool voice);          // START A SONG FROM ANY TOOL (MY VOICE: LOOPS on the mic)
    void switchIdea (const juce::String& id);
    void renameIdea (const juce::String& id);
    void deleteIdea (const juce::String& id);    // asks first (a card where the song's menu was), then removeIdea
    void removeIdea (const juce::String& id);    // any song, the open one too: that one makes way for your last other song (or a new one)
    void makeABeat();        // Your songs' MAKE A BEAT: a new song with an empty grid, and the lesson
    void lessonFromMenu();   // the drawer's TOUR · LESSON > Show me: make a beat
    juce::String phoneSummary() const;   // "8 sounds · 3 loop layers in this song · 12 MB", on Settings
    juce::Rectangle<float> menuRect() const;
    void openDrawer();
    void closeDrawer();
    void pickSong (const juce::String& id);          // a song in the drawer: switch to it and stay on the tool
    void playFromDrawer (const juce::String& id);
    void drawerTile (int tile);
    pocket::HoldSpec songHoldSpec (const juce::String& id);
    void swipeTool (int direction);                   // the header bar: the previous (-1) or next (+1) tool
    bool edgeSwipeAllowed (juce::Point<float> inApp) const;
    void playIdea (const juce::String& id);

    //==========================================================================
    // Recording: one REC for every tool
    void thumbPressed (int button);
    // Kaleidobox Visuals: the light show over the screen (the thumb bar's VISUALS), above the thumb bar.
    void showVisuals (bool);
    bool visualsShown() const { return visualsView != nullptr; }
    void recPressed();
    bool recBusy() const;
    void stopRec();
    void recLoops();
    void recDrums();
    void recPads();
    void recVocoder();
    void recSynth();
    void recChords();
    void recDrone();
    // SAMPLER (PocketAppSampler.cpp): the pads that play a sound after it was trimmed or the trim undone (`shift`: how far
    // their parts move), and the 4 x 4 pad picker.
    void repadSound (const juce::String& oldId, const juce::String& newId, int shift);
    void setupSampler();
    // The SAMPLER's A TOOL: every tool's REC lands on a loop layer, so a layer (or the loops mixed) comes onto the page.
    void showSampleToolMenu();
    void sampleLayer (int layer);   // -1: the loops mixed
    // Sets (PADS > Sounds > SETS): the bank on screen saved as a set (core/SoundSet.h) in Documents/Sets, the same
    // folder layout as the Mac's ~/Music/Crate/Sets; a set loaded onto the bank (a whole-kit set onto all 64 pads), UNDO.
    juce::File setsFolder() const { return lib.getRoot().getChildFile ("Sets"); }
    void showSetsMenu (juce::Component* anchor = nullptr);   // the menu over `anchor` (the Sounds sheet when null)
    juce::File saveBankAsSet (const juce::String& name, bool wholeKit);
    bool loadSetFolder (const juce::File&);
    // A bottom sheet over the dim, sliding up (shown) or down: the pad picker, the chord editor.
    void showDragSheet (pocket::DragSheet&, juce::Rectangle<int> bounds, bool& openFlag, bool shown);
    void openPadPicker();
    void closePadPicker();
    juce::Rectangle<int> padPickerBounds() const;
    // EFFECTS (PocketAppEffects.cpp): a pedalboard for every sound. REC on EFFECTS records the picked sound through its
    // board the way that sound's own tool does (VOICE: the mic source); LAYER, ALL and MASTER are resampled.
    void setupEffects();
    void openEffects (int boardId);       // -1 = the sound it showed last
    int boardForTool (Tool) const;        // the board a tool's sound goes through (-1 = none: LOOPS with no layer picked)
    Tool recTool() const;                 // the tool REC works as (EFFECTS on SYNTH records like SYNTH)
    void recEffects();
    juce::var boardsToVar() const;        // the tools' boards, ALL and MASTER (song.json; the layers' are in loops.json)
    void boardsFromVar (const juce::var&);
    juce::String runEffectsChecks (juce::String& report);
    void writeEffectsSnapshots (const juce::File& folder);
    int chordsRecBars() const;   // a CHORDS take's bars: REC LENGTH, stretched to whole passes of the progression
    void applyChordsPadSound();  // MY PAD SOUND: the picked PADS pad's sound to the chords
    void finishSynthTake();   // a SYNTH REC pass is over or called off: what was played stays in the pattern
    void startClockForRec();  // the song starts (the loops if there are any, else the drum machine)
    void applyClick();   // the click follows CLICK (and is always on while pads are recorded)
    // LOCK: waits for the pad pattern's start, arms a layer with the drums source, then waits for it to
    // play; the live pattern (or drum machine) then stops, so the layer doesn't double it.
    void lockWatchTick();
    juce::TimedCallback lockWatch { [this] { lockWatchTick(); } };
    // pads / drumsLive: hits you play go into the pattern for a pass, then the pattern is locked onto a layer
    // (the live machine stops once the layer plays). drums: STEPS, the drum machine's pattern is recorded as it is.
    // synthLive: what you play on the SYNTH keys (or the arp) goes into its pattern for a pass, then the pattern
    // is locked onto a layer; synthPattern: SYNTH's pattern is locked as it is. Either way, once the layer plays
    // the pattern is cleared (UNDO brings it back with the layer off), so the synth is free for a new part.
    // chords: a CHORDS take (the looper records the chords source); once its layer plays, the progression's sound
    // is muted (it still drives the vocoder) and a held chord lets go, so nothing plays twice.
    enum class LockKind { none, pads, drums, drumsLive, synthLive, synthPattern, chords, drone };
    LockKind lockKind = LockKind::none;
    bool playingIn() const
    {
        return (lockKind == LockKind::pads || lockKind == LockKind::drumsLive || lockKind == LockKind::synthLive || lockKind == LockKind::synthPattern) && ! lockArmed;
    }
    bool synthLock() const { return lockKind == LockKind::synthLive || lockKind == LockKind::synthPattern; }
    int synthUndoLayer = -1;
    int chordsUndoLayer = -1;      // the layer the last CHORDS take went on (UNDO un-mutes the progression)
    bool chordsLockProg = false;   // the CHORDS take records the progression (it starts from its first box)   // the layer the last SYNTH part went on (UNDO takes it off and gives the part back)
    int lockLayer = -1, lockBars = 0;
    std::shared_ptr<pocket::NotePart> lockPart;   // the notes of the take being locked (PADS, SYNTH), for QUANTIZE
    double lockStart = 0, lockLen = 8, padsFrom = 0;
    bool lockArmed = false;
    // What the header and the count-in panel show.
    struct RecView
    {
        enum Kind { idle, counting, waiting, recording, padsPass } kind = idle;
        juce::String left, right;   // "BAR 2.3", "LAYER 4 · 4 BARS"
        int countBeat = 0, countBar = 0, countBars = 1;
        juce::String after;         // the count-in panel's last line
    };
    RecView recView() const;
    juce::String lengthLabel (int bars) const;

    //==========================================================================
    // The mic and the headphones
    void applyMic();
    void routeChanged();
    void checkMic (int generation, int step);   // after a route change: is the mic still alive?
    int micCheckGeneration = 0;
    void openHeadphones (std::function<void()> then);
    void closeHeadphones();
    void chooseHeadphones (bool wearing);
    bool micOn = false, micHear = false;   // off at every launch
    pocket::Route route = pocket::Route::wired;
    std::function<void()> afterMic;

    //==========================================================================
    void closeSendSheet();
    void linkChanged();
    juce::Rectangle<float> chipRect() const;
    juce::Rectangle<float> lcdRect() const;
    juce::Rectangle<float> backRect() const;
    bool remoteShowing() const;
    void placeOnPads (const juce::String& soundId, int firstPad, bool chop);
    void slide (juce::Component&, juce::Rectangle<int> open, bool shown);
    juce::Rectangle<int> padSheetBounds() const;
    juce::Rectangle<int> sendSheetBounds() const;
    void closeSoundBank();
    void loadKit (int index);
    void fillEmptyIdea();   // a new idea (or one with no pads) starts with the Street kit
    void openTempo();
    void closeTempo();
    juce::Rectangle<int> tempoBounds() const;
    void openRecSetup();
    void closeRecSetup();
    juce::Rectangle<int> recSetupBounds() const;
    juce::Rectangle<int> headphonesBounds() const;
    void showMorePage (int);
    void applyAnimate();
    void layoutSheet (bool shown);
    void closeAllSheets();
    void savePng (const juce::File&);
    juce::String runRecChecks (juce::String& report);
    juce::String runLoopEditChecks (juce::String& report);
    juce::String runNavigationChecks (juce::String& report, const juce::File& folder);
    juce::String runSamplerChecks (juce::String& report);
    juce::String runSongDeleteChecks (juce::String& report, const juce::File& folder);   // PocketAppNav.cpp
    juce::String runSongChecks (juce::String& report, const juce::File& folder);        // PocketAppNav.cpp: the SONG tool
    juce::String runOwnChordChecks (juce::String& report, const juce::File& folder);     // PocketAppNav.cpp: EDIT CHORD in the app   // PocketAppSampler.cpp: a trimmed sound is what the pad plays

    struct PocketLook : juce::LookAndFeel_V4 { PocketLook(); juce::Font getPopupMenuFont() override; juce::Font getTextButtonFont (juce::TextButton&, int) override; };
    PocketLook look;

    pocket::Library lib;
    pocket::Ideas ideas;       // before the screens: it points the library at the current idea's folder
    pocket::AppSettings appSettings;
    pocket::LinkClient link;   // the Mac
    BeatsScreen beats;     // before the sound card, so the card stops before the engine goes
    LoopsScreen loopsScreen;   // owns the one renderer the sound card plays (Beats inside it)
    VocoderScreen vocoderScreen;   // plays inside that renderer, so it stops after the sound card does
    pocket::ChordsTool chordsTool; // the one chord state (CHORDS and the VOCODER's quick pads), inside the renderer
    SingScreen singScreen;
    pocket::SynthPart synthPart;   // the SYNTH tool's sound and pattern, also inside the renderer
    SynthScreen synthScreen;
    ChordsScreen chordsScreen;
    pocket::DroneTool droneTool;   // the DRONE tool, inside the renderer
    DroneScreen droneScreen;
    std::unique_ptr<pocket::PocketSong> song;   // the SONG tool's arrangement of the layers (inside the renderer)
    std::unique_ptr<SongScreen> songScreen;
    void setupSong();
    void songTickNow();
    void stopClock();
    juce::Array<juce::File> bounceSong (const SongScreen::Bounce&, juce::String& said);   // PocketAppSong.cpp
    void shareBounce (const juce::Array<juce::File>&);
    void bounceToMac();
    juce::Array<juce::File> lastBounce;
    juce::ScopedMessageBox bounceSharing;
    juce::TimedCallback songTick { [this] { songTickNow(); } };
    int savedSongEdits = -1;
    juce::uint32 songSavedAt = 0;
    std::unique_ptr<EffectsScreen> effects;
    PocketAudio audio;
    crate::visuals::VisualState visualState;
    std::unique_ptr<crate::visuals::VisualsView> visualsView;   // after the sound card: it listens to its tap
    EffectsSheet sheet;
    PadSheet padSheet;
    TempoSheet tempoSheet;
    SoundBankSheet soundBank;
    pocket::RecSetupSheet recSetup;
    pocket::HeadphonesSheet headphones;
    pocket::ThumbBar thumb;
    pocket::ToolRow toolRow;
    pocket::CountInPanel countPanel;
    std::unique_ptr<juce::FileChooser> chooser;
    RecordScreen record;
    SamplerScreen sampler;
    SamplerPadPicker padPicker;
    ChordEditSheet chordEdit { chordsTool };   // CHORDS > hold a pad > EDIT CHORD
    DrumKitSheet drumKit { lib };   // DRUMS > KIT
    void openDrumKit() override;
    void closeDrumKit();
    void refreshDrumKit();
    juce::Rectangle<int> drumKitBounds() const;
    void openChordEdit (int pad);
    void closeChordEdit();
    juce::Rectangle<int> chordEditBounds() const;
    SoundsScreen sounds;
    std::unique_ptr<RemoteScreen> remote;
    std::unique_ptr<MacLinkScreen> macLink;
    std::unique_ptr<SendToMacSheet> sendSheet;
    std::unique_ptr<SettingsScreen> settings;
    std::unique_ptr<IdeasScreen> ideasScreen;
    SongDrawer drawer { ideas, link };
    pocket::HoldMenu holdCard;
    juce::Rectangle<float> lastHoldTarget;   // what the last hold menu kept bright (in the app's coordinates): a confirm goes there too
    std::unique_ptr<TitleScreen> title;
    std::unique_ptr<TourOverlay> tour;
    // The Make a beat lesson: a coach card over DRUMS (see PocketLesson.h)
    std::unique_ptr<BeatLesson> lesson;
    bool lessonOn = false, lessonClick = false;
    void startLesson();
    void endLesson();
    void applyLessonPhase (int phase);
    void layoutLesson();
    void showTitle (bool shown);
    void startTour();
    void endTour();
    juce::Rectangle<int> tourStop (int stop);   // opens the stop's screen, returns its spotlight
    std::unique_ptr<Toast> toastBox;
    std::unique_ptr<Dim> dim;
    juce::BorderSize<int> insets;
    static juce::BorderSize<int> safeInsets();
    // The phone only knows its safe area (the clock, the Dynamic Island, a Maps or call pill, the home bar)
    // once the window is up, and it changes when the status bar does: look again now and then. The route
    // is looked at too, in case a change notice was missed.
    juce::TimedCallback insetWatch { [this]
    {
        if (safeInsets() != insets) { resized(); repaint(); }
        if (pocket::currentRoute() != route) routeChanged();
    } };

    // The song: its one key (SYNTH, CHORDS and the vocoder follow it), the SYNTH part and the CHORDS settings, in the
    // idea's song.json.
    pocket::SongKey songKey;
    void setSongKey (pocket::SongKey);
    void openKeyMenu (juce::Component&, juce::Rectangle<int>);
    void loadSong();
    void saveSong();

    // A finger that starts within 22 px of the left edge and pulls right drags the drawer out (a watcher on every touch,
    // so nothing underneath loses its drag; the SYNTH keys and the sheets are left alone).
    struct EdgeWatcher : juce::MouseListener
    {
        explicit EdgeWatcher (PocketApp& a) : app (a) {}
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void begin (juce::Point<float> inApp, int sourceIndex);   // (what the three above do, and the self-checks)
        void move (juce::Point<float> inApp, int sourceIndex);
        void end (int sourceIndex);
        PocketApp& app;
        bool tracking = false, dragging = false;
        int source = -1;
        juce::Point<float> start;
        float lastX = 0.0f, prevX = 0.0f;
        juce::uint32 lastT = 0, prevT = 0;
    };
    EdgeWatcher edgeWatcher { *this };
    bool headerDown = false, headerSwiped = false;
    juce::Point<float> headerStart;

    Page page = Page::songs;
    Page backPage = Page::songs;   // where Sounds, Remote, Mac link, Settings and the recorder go back to
    Tool backTool = Tool::pads;
    Tool tool = Tool::pads;
    int morePageShown = -1;
    bool sheetOpen = false, padSheetOpen = false, sendOpen = false, tempoOpen = false, bankOpen = false, recSetupOpen = false, phonesOpen = false,
         padPickOpen = false, chordEditOpen = false, drumKitOpen = false;
    juce::String shownHeader;
    bool shownBusy = false, shownPlaying = false;
    void headerTickNow();
    juce::TimedCallback headerTick { [this] { headerTickNow(); } };
    bool animate = true;
};
