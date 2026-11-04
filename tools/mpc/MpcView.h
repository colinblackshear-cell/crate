#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MpcProcessor.h"
#include "PadEditor.h"
#include "PadGrid.h"
#include "PadSoundEditor.h"
#include "LivePanel.h"
#include "SequencerView.h"
#include "core/ui/LibraryList.h"
#include "core/ui/Theme.h"

// The MPC tool's window: kits, the shared sound library, the pad grid, pad settings, tempo,
// the sequencer, and export to the MPC's card.
class MpcView : public juce::AudioProcessorEditor,
                public juce::DragAndDropContainer,
                private juce::ChangeListener,
                private juce::ListBoxModel,
                private juce::Timer
{
public:
    explicit MpcView (MpcProcessor&);
    ~MpcView() override;

    // Opens an MPC program and the sounds next to it as a kit.
    void openKitFile (const juce::File& pgm);
    // Puts sounds on pads in order, starting at the selected pad.
    void placeSounds (const std::vector<std::pair<juce::String, SharedAudio>>&);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;

    // Shows the pads or the sequencer's step grid in the middle of the window.
    void showSequencer (bool);

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void selectedRowsChanged (int row) override;

    void selectPad (int pad);
    void showPadBank (int padBank);
    void loadFilesIntoPads (int firstPad, const juce::Array<juce::File>& files);
    void chooseFileForPad();
    void chopSelectedPad (int slices);
    void openSoundEditor (bool open);
    void openKit();
    void exportToCard();
    void writeBanks (const juce::File& destination);
    void refresh();
    void setStatus (const juce::String& text, bool warning = false);

    MpcProcessor& processor;
    Project& project;
    juce::AudioFormatManager& formats;

    crate::Theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    juce::Label kitLabel { {}, "Kit" };
    juce::TextEditor kitName;
    juce::Label memory;
    juce::ComboBox ram;
    juce::TextButton openButton { "Open kit..." };
    juce::Label bpmLabel { {}, "BPM" };
    juce::Slider bpm;
    juce::ToggleButton keepInTime { "Keep in time" };
    juce::TextButton exportButton { "Export to card..." };

    juce::ListBox kitList { "Kits", this };
    juce::TextButton newKit { "New kit" };
    juce::TextButton removeKit { "Remove" };
    crate::LibraryList libraryList;

    std::array<juce::TextButton, 4> bankTabs;
    PadGrid grid { project };
    PadEditor editor { project };
    PadSoundEditor soundEditor { processor };
    TransportBar transport { processor };
    LivePanel live { processor };
    SequencerView sequencer { processor };
    juce::TextButton padsTab { "Pads" }, sequenceTab { "Steps" };
    // Computer keys play the 16 pads on screen, laid out like the MPC's pads (bottom row first).
    static constexpr const char* kPadKeys = "zxcvasdfqwer1234";
    std::array<bool, 16> keyDown {};
    std::array<int, 16> keyPad {};
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpcView)
};
