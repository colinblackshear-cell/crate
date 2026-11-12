#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "core/SoundBank.h"
#include "core/ui/Theme.h"

// The Sounds tab: every sample in your folders and Splice, sorted into kinds (kicks, snares,
// hats, bass, vocals, loops...) and searchable. Click a sound to hear it; send it onto the next
// empty MPC pad, into the Sampler, or onto a loop layer (a loop is stretched to the Rig's tempo).
class SoundsView : public juce::Component,
                   private juce::ListBoxModel,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    SoundsView (RigEngine&, juce::File bankHome = SoundBank::defaultHome());
    ~SoundsView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    std::function<void (const juce::String&)> onSay;
    SoundBank& getBank() { return bank; }
    // The same bank, for the MPC's sound bank under its pads.
    std::shared_ptr<SoundBank> getSharedBank() const { return bankPtr; }

    // Sends the selected sound (public for tests).
    void sendToMpc();
    void sendToSampler();
    void sendToLoop();
    void selectRow (int row);
    int getNumShown() const { return (int) shown.size(); }
    void filter();

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { filter(); }
    void timerCallback() override;
    void showFolders();
    void setKind (int kind);
    std::shared_ptr<const juce::AudioBuffer<float>> loadSelected (const SoundBank::Sound*& sound);
    void say (const juce::String& s) { if (onSay) onSay (s); }

    RigEngine& engine;
    std::shared_ptr<SoundBank> bankPtr;
    SoundBank& bank;
    juce::AudioFormatManager formats;
    std::vector<SoundBank::Sound> shown;
    int kind = -1;

    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> kindButtons;   // All, then each kind
    juce::ComboBox length;
    juce::TextButton nearTempo { "Near tempo" }, folders { "Folders..." }, hearButton { "Hear on click" };
    juce::ListBox list { "Sounds", this };
    juce::TextButton play { "Play" }, stop { "Stop" }, toMpc { "Onto MPC pad" }, toSampler { "Into Sampler" }, toLoop { "Onto a loop layer" };
    juce::Label status;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoundsView)
};
