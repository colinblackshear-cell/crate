#pragma once

#include "PocketFactory.h"
#include "PocketTheme.h"

// SOUNDS on Beats > MPC Pads: the factory kits (LOAD puts one on the bank), and where your own sounds
// come from (the Files app's Drop Sounds Here folder, Import from Files, or the Mac).
class SoundBankSheet : public juce::Component
{
public:
    SoundBankSheet();

    std::function<void (int kit)> onLoad;
    std::function<void()> onImport, onOpenSounds, onDone;
    std::function<juce::String()> loadedKitName;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    pocket::PButton loads[5], import { "Import from Files", pocket::Look::amber }, openSounds { "All sounds" }, done { "Done" };
    juce::Rectangle<float> row (int i) const { return { 14.0f, 62.0f + (float) i * 58.0f, (float) getWidth() - 28.0f, 52.0f }; }
};
