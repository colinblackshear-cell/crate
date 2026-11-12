#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MpcProcessor.h"
#include "core/SoundBank.h"

// Your sound bank (your sample folders and Splice), right under the pads. Search it, or pick a
// kind (kicks, snares, bass...), click a sound to hear it, and double-click it (or press
// Enter, or "Onto pad") to put it on the pad you picked. Sounds can be dragged onto any pad
// too. A loop with a tempo in its name gets its length in beats, so Keep in time fits it.
//
// In the Rig it shares the Sounds page's bank; on its own (the MPC app, or in Logic) it opens
// the same bank (~/Music/Crate) the first time it's shown.
class BankBrowser : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::Timer
{
public:
    explicit BankBrowser (MpcProcessor&);
    ~BankBrowser() override;

    // The bank to show (the Rig's). Without one it makes its own when it's first shown.
    void setSoundBank (std::shared_ptr<SoundBank>);
    SoundBank* getSoundBank() const { return bank.get(); }

    // The pad sounds go onto (0..63 in the kit).
    std::function<int()> targetPad;
    // A sound went onto a pad.
    std::function<void (int pad)> onPlaced;
    std::function<void (const juce::String&, bool warning)> onStatus;

    // Shows what matches the search and kind (the tests call these).
    void filter();
    void setSearch (const juce::String&);
    void setKind (int kind);              // -1 = every kind
    int getNumShown() const { return (int) shown.size(); }
    void selectRow (int row);
    // Puts the selected sound on the target pad; false if nothing is selected or it won't open.
    bool putSelectedOnPad();

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int) override { putSelectedOnPad(); }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void timerCallback() override;
    void addFolder();
    void say (const juce::String& t, bool warning = false) { if (onStatus) onStatus (t, warning); }

    MpcProcessor& processor;
    std::shared_ptr<SoundBank> bank;
    int lastSize = -1;
    bool lastScanning = false;
    std::vector<SoundBank::Sound> shown;
    int kind = -1;

    juce::Label title { {}, "Sound bank" };
    juce::TextEditor search;
    juce::ComboBox kinds, length;
    juce::ListBox list { "Sound bank", this };
    juce::TextButton onto { "Onto pad" }, folders { "Folders..." };
    juce::ToggleButton hear { "Hear on click" };
    juce::Label count;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BankBrowser)
};
