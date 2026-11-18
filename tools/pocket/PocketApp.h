#pragma once

#include "BeatsScreen.h"
#include "EffectsSheet.h"
#include "PadSheet.h"
#include "PocketAudio.h"
#include "PocketHost.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"
#include "RecordScreen.h"
#include "SoundsScreen.h"

// The whole phone app: a header, four tabs (Record, Sounds, Pads, Remote) and the Effects + Chop sheet.
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

    // Screenshot mode: fills a demo library, then writes one PNG per screen into `folder`.
    static void fillDemoLibrary (pocket::Library&);
    void writeSnapshots (const juce::File& folder);
    pocket::Library& library() { return lib; }

private:
    class TabBar;
    class Panel;
    class MoreScreen;
    class Toast;
    class Dim;

    void toast (const juce::String&) override;
    void openSheet() override;
    void closeSheet() override;
    void editSound (const juce::String&) override;
    void sendToPad (const juce::String&) override;
    void openPadSheet (int pad) override;
    void closePadSheet() override;
    void placeOnPads (const juce::String& soundId, int firstPad, bool chop);
    void slide (juce::Component&, juce::Rectangle<int> open, bool shown);
    juce::Rectangle<int> padSheetBounds() const;
    void showTab (int);
    void showMorePage (int);   // 0 list, 1 sounds, 2 remote, 3 mac link, 4 settings
    void layoutSheet (bool shown);
    void savePng (const juce::File&);

    struct PocketLook : juce::LookAndFeel_V4 { PocketLook(); juce::Font getPopupMenuFont() override; juce::Font getTextButtonFont (juce::TextButton&, int) override; };
    PocketLook look;

    pocket::Library lib;
    BeatsScreen beats;     // before the sound card, so the card stops before the engine goes
    PocketAudio audio;
    EffectsSheet sheet;
    PadSheet padSheet;
    RecordScreen record;
    SoundsScreen sounds;
    std::unique_ptr<Panel> loops, vocoder, remote, macLink, settings;
    std::unique_ptr<MoreScreen> more;
    std::unique_ptr<TabBar> tabs;
    std::unique_ptr<Toast> toastBox;
    std::unique_ptr<Dim> dim;
    juce::Rectangle<float> status;
    juce::BorderSize<int> insets;
    int tab = 0;
    bool sheetOpen = false, padSheetOpen = false;
    bool animate = true;
};
