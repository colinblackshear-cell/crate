#pragma once

#include "BeatsScreen.h"
#include "EffectsSheet.h"
#include "LinkScreens.h"
#include "LoopsScreen.h"
#include "PadSheet.h"
#include "PocketAudio.h"
#include "PocketHost.h"
#include "PocketLibrary.h"
#include "PocketSettings.h"
#include "PocketTheme.h"
#include "RecordScreen.h"
#include "SoundsScreen.h"
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
    void closeSendSheet();
    void linkChanged();
    juce::Rectangle<float> chipRect() const;
    bool remoteShowing() const;
    void placeOnPads (const juce::String& soundId, int firstPad, bool chop);
    void slide (juce::Component&, juce::Rectangle<int> open, bool shown);
    juce::Rectangle<int> padSheetBounds() const;
    juce::Rectangle<int> sendSheetBounds() const;
    void showTab (int);
    void showMorePage (int);   // 0 list, 1 sounds, 2 remote, 3 mac link, 4 settings
    void layoutSheet (bool shown);
    void savePng (const juce::File&);

    struct PocketLook : juce::LookAndFeel_V4 { PocketLook(); juce::Font getPopupMenuFont() override; juce::Font getTextButtonFont (juce::TextButton&, int) override; };
    PocketLook look;

    pocket::Library lib;
    pocket::AppSettings appSettings;
    pocket::LinkClient link;   // the Mac
    BeatsScreen beats;     // before the sound card, so the card stops before the engine goes
    LoopsScreen loopsScreen;   // owns the one renderer the sound card plays (Beats inside it)
    VocoderScreen vocoderScreen;   // plays inside that renderer, so it stops after the sound card does
    PocketAudio audio;
    EffectsSheet sheet;
    PadSheet padSheet;
    RecordScreen record;
    SoundsScreen sounds;
    std::unique_ptr<RemoteScreen> remote;
    std::unique_ptr<MacLinkScreen> macLink;
    std::unique_ptr<SendToMacSheet> sendSheet;
    std::unique_ptr<SettingsScreen> settings;
    std::unique_ptr<MoreScreen> more;
    std::unique_ptr<TabBar> tabs;
    std::unique_ptr<Toast> toastBox;
    std::unique_ptr<Dim> dim;
    juce::Rectangle<float> status;
    juce::BorderSize<int> insets;
    int tab = 0;
    bool sheetOpen = false, padSheetOpen = false, sendOpen = false;
    bool animate = true;
};
