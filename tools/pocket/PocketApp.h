#pragma once

#include "BeatsScreen.h"
#include "EffectsSheet.h"
#include "IdeasScreen.h"
#include "LinkScreens.h"
#include "LoopsScreen.h"
#include "PadSheet.h"
#include "PocketAudio.h"
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

// The whole phone app: a header, five tabs (Record, Beats, Loops, Vocoder, More) and the Effects + Chop sheet.
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

    class TabBar;
    class MoreScreen;
    class SettingsScreen;
    class Toast;
    class Dim;

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
    void lockBeat (int bars) override;
    void closeSendSheet();
    void linkChanged();
    juce::Rectangle<float> chipRect() const;
    bool remoteShowing() const;
    void placeOnPads (const juce::String& soundId, int firstPad, bool chop);
    void slide (juce::Component&, juce::Rectangle<int> open, bool shown);
    juce::Rectangle<int> padSheetBounds() const;
    juce::Rectangle<int> sendSheetBounds() const;
    void showTab (int);
    // Ideas and the seven steps (Record, Shape, Pads, Beat, Sing, Song, Send)
    void showIdeasList (bool shown);
    void openIdea (const juce::String& id);
    void newIdea();
    void switchIdea (const juce::String& id);
    void renameIdea (const juce::String& id);
    void deleteIdea (const juce::String& id);
    int currentStep() const;     // -1 = not on a step (the Ideas list, More)
    void goStep (int step, bool explain);
    void noteStep();
    bool stripShown() const;
    juce::Rectangle<float> stepRect (int step) const;
    juce::Rectangle<float> nextRect() const;
    juce::Rectangle<float> backRect() const;
    // The transport in the header: BPM (tap for Tempo), the bar, Play/Stop for the whole song.
    void togglePlay() override;
    void padSoundEdited (const juce::String& soundId) override;
    void backToPads (int pad) override;
    void openSoundBank() override;
    void closeSoundBank();
    void loadKit (int index);
    void fillEmptyIdea();   // a new idea (or one with no pads) starts with the Street kit
    juce::Rectangle<float> transportRect() const;
    juce::Rectangle<float> playRect() const;
    void openTempo();
    void closeTempo();
    juce::Rectangle<int> tempoBounds() const;
    void showMorePage (int);   // 0 list, 1 sounds, 2 remote, 3 mac link, 4 settings
    void layoutSheet (bool shown);
    void savePng (const juce::File&);

    struct PocketLook : juce::LookAndFeel_V4 { PocketLook(); juce::Font getPopupMenuFont() override; juce::Font getTextButtonFont (juce::TextButton&, int) override; };
    PocketLook look;

    pocket::Library lib;
    pocket::Ideas ideas;       // before the screens: it points the library at the current idea's folder
    pocket::AppSettings appSettings;
    pocket::LinkClient link;   // the Mac
    BeatsScreen beats;     // before the sound card, so the card stops before the engine goes
    LoopsScreen loopsScreen;   // owns the one renderer the sound card plays (Beats inside it)
    VocoderScreen vocoderScreen;   // plays inside that renderer, so it stops after the sound card does
    PocketAudio audio;
    EffectsSheet sheet;
    PadSheet padSheet;
    TempoSheet tempoSheet;
    SoundBankSheet soundBank;
    std::unique_ptr<juce::FileChooser> chooser;
    RecordScreen record;
    SoundsScreen sounds;
    std::unique_ptr<RemoteScreen> remote;
    std::unique_ptr<MacLinkScreen> macLink;
    std::unique_ptr<SendToMacSheet> sendSheet;
    std::unique_ptr<SettingsScreen> settings;
    std::unique_ptr<MoreScreen> more;
    std::unique_ptr<IdeasScreen> ideasScreen;
    bool ideasShown = true;   // the Record tab shows the Ideas list (true) or the recorder
    std::unique_ptr<TabBar> tabs;
    std::unique_ptr<Toast> toastBox;
    std::unique_ptr<Dim> dim;
    juce::Rectangle<float> status;
    juce::BorderSize<int> insets;
    static juce::BorderSize<int> safeInsets();
    // The phone only knows its safe area (the clock, the Dynamic Island, a Maps or call pill, the home bar)
    // once the window is up, and it changes when the status bar does: look again now and then.
    juce::TimedCallback insetWatch { [this] { if (safeInsets() != insets) { resized(); repaint(); } } };
    // LOCK BEAT: waits for the pattern's last bar, arms the layer, then waits for it to finish.
    void lockWatchTick();
    juce::TimedCallback lockWatch { [this] { lockWatchTick(); } };
    int lockLayer = -1, lockBars = 0;
    bool lockArmed = false;
    int tab = 0;
    bool sheetOpen = false, padSheetOpen = false, sendOpen = false, tempoOpen = false, bankOpen = false;
    bool shownRunning = false;
    juce::String shownBar;
    juce::TimedCallback headerTick { [this] {
        const bool running = beats.engine().clockRunning();
        const double b = beats.engine().clockBeat();
        const auto bar = running ? juce::String ((int) (b / 4.0) + 1) + "." + juce::String ((int) std::fmod (b, 4.0) + 1) : juce::String ("1.1");
        if (running != shownRunning || bar != shownBar) { shownRunning = running; shownBar = bar; repaint (transportRect().expanded (2).toNearestInt()); }
    } };
    bool animate = true;
};
