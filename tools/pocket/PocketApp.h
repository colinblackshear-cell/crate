#pragma once

#include "BeatsScreen.h"
#include "EffectsSheet.h"
#include "IdeasScreen.h"
#include "LinkScreens.h"
#include "LoopsScreen.h"
#include "PadSheet.h"
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
#include "SynthScreen.h"
#include "ChordsScreen.h"
#include "PocketKey.h"
#include "PocketIntro.h"
#include "PocketLesson.h"

// The whole phone app, tool first: a slim header (the song, the bar and where the next take goes, the Mac
// link, ···), the tool on screen, the thumb bar (BPM, CLICK, REC, PLAY, UNDO) and the tool row (LOOPS,
// DRUMS, PADS, SYNTH, CHORDS, VOCODER). REC records whatever tool is on screen onto the next empty loop layer, in time.
// Your songs, the sound recorder, every vocoder setting and the More pages are pages without the thumb bar.
class PocketApp : public juce::Component, private PocketHost
{
public:
    struct Options
    {
        juce::File libraryRoot;       // empty = the app's Documents folder
        bool audio = true;            // false = no sound card (screenshots)
    };

    PocketApp() : PocketApp (Options()) {}
    explicit PocketApp (Options);
    ~PocketApp() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // Screenshot mode: fills a demo library, then writes one PNG per screen into `folder`.
    static void fillDemoLibrary (pocket::Library&);
    void writeSnapshots (const juce::File& folder);
    pocket::Library& library() { return lib; }

private:
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

    class MoreScreen;
    class SettingsScreen;
    class Toast;
    class Dim;

    // What's on screen: a tool (with the thumb bar), or one of the pages around them.
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
    void togglePlay() override;
    void padSoundEdited (const juce::String& soundId) override;
    void backToPads (int pad) override;
    void openSoundBank() override;
    void showLoops() override { showTool (Tool::loops); }
    void showVocoder() override { showPage (Page::vocoderSettings); }
    void closeVocoderSettings() override { showTool (Tool::vocoder); }
    void openRecorder() override;
    bool micReady() override { return micOn; }
    void askForMic (std::function<void()> then) override;
    pocket::Route micRoute() override { return route; }
    bool hearingMyself() override { return micOn && micHear; }
    void setHearMyself (bool) override;
    void micOff() override;
    void micUseChanged() override { applyMic(); }

    //==========================================================================
    // Pages and tools
    void showPage (Page, int morePage = 0);   // morePage: 0 list, 1 sounds, 2 remote, 3 mac link, 4 settings
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
    void switchIdea (const juce::String& id);
    void renameIdea (const juce::String& id);
    void deleteIdea (const juce::String& id);
    void makeABeat();        // Your songs' MAKE A BEAT: a new song with an empty grid, and the lesson
    void lessonFromMore();   // More > Show me: make a beat
    void playIdea (const juce::String& id);

    //==========================================================================
    // Recording: one REC for every tool
    void thumbPressed (int button);
    void recPressed();
    bool recBusy() const;
    void stopRec();
    void recLoops();
    void recDrums();
    void recPads();
    void recVocoder();
    void recSynth();
    void recChords();
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
    enum class LockKind { none, pads, drums, drumsLive, synthLive, synthPattern, chords };
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
    juce::Rectangle<float> moreRect() const;
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
    void layoutSheet (bool shown);
    void closeAllSheets();
    void savePng (const juce::File&);
    juce::String runRecChecks (juce::String& report);
    juce::String runLoopEditChecks (juce::String& report);

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
    PocketAudio audio;
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
    SoundsScreen sounds;
    std::unique_ptr<RemoteScreen> remote;
    std::unique_ptr<MacLinkScreen> macLink;
    std::unique_ptr<SendToMacSheet> sendSheet;
    std::unique_ptr<SettingsScreen> settings;
    std::unique_ptr<MoreScreen> more;
    std::unique_ptr<IdeasScreen> ideasScreen;
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

    Page page = Page::songs;
    Tool tool = Tool::pads;
    int morePageShown = -1;
    bool sheetOpen = false, padSheetOpen = false, sendOpen = false, tempoOpen = false, bankOpen = false, recSetupOpen = false, phonesOpen = false;
    juce::String shownHeader;
    bool shownBusy = false, shownPlaying = false;
    void headerTickNow();
    juce::TimedCallback headerTick { [this] { headerTickNow(); } };
    bool animate = true;
};
