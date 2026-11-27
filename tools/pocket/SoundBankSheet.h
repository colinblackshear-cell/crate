#pragma once

#include "PocketFactory.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// SOUNDS on Beats > MPC Pads (and the kit name on the pads' screen): the factory kits (LOAD puts one on the bank; the list
// scrolls), and where your own sounds come from (the Files app's Drop Sounds Here folder, Import from Files, or the Mac).
class SoundBankSheet : public pocket::DragSheet
{
public:
    SoundBankSheet();
    ~SoundBankSheet() override;

    static constexpr int kHeight = 600;   // how tall the sheet stands, from the bottom edge up

    std::function<void (int kit)> onLoad;
    std::function<void()> onImport, onOpenSounds, onDone, onSets, onRename;
    std::function<juce::String()> loadedKitName;

    void paint (juce::Graphics&) override;
    void resized() override;
    int kitCount() const { return (int) pocket::factoryKits().size(); }
    void scrollKitsTo (int kit);   // (the self-checks and screenshots)

private:
    class KitList;
    std::unique_ptr<KitList> list;
    juce::Viewport view;
    pocket::PButton import { "Import from Files", pocket::Look::amber }, openSounds { "All sounds" }, done { "Done" },
                    sets { U8("Sets \xe2\x80\xba"), pocket::Look::amber }, rename { "Rename kit" };
    juce::Rectangle<int> listBounds() const { return { 8, 56, getWidth() - 16, getHeight() - 56 - 206 }; }
};
