#pragma once

#include "PocketFactory.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// SOUNDS on Beats > MPC Pads: the factory kits (LOAD puts one on the bank), and where your own sounds
// come from (the Files app's Drop Sounds Here folder, Import from Files, or the Mac).
class SoundBankSheet : public pocket::DragSheet
{
public:
    SoundBankSheet();

    std::function<void (int kit)> onLoad;
    std::function<void()> onImport, onOpenSounds, onDone, onSets;
    std::function<juce::String()> loadedKitName;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    pocket::PButton loads[5], import { "Import from Files", pocket::Look::amber }, openSounds { "All sounds" }, done { "Done" }, sets { U8("Sets \xe2\x80\xba"), pocket::Look::amber };
    juce::Rectangle<float> row (int i) const { return { 14.0f, 62.0f + (float) i * 58.0f, (float) getWidth() - 28.0f, 52.0f }; }
};
