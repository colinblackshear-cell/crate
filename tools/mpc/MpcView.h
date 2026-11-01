#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "MpcProcessor.h"
#include "PadEditor.h"
#include "PadGrid.h"
#include "core/ui/LibraryList.h"
#include "core/ui/Theme.h"

// The MPC tool's window: kits, the shared sound library, the pad grid, pad settings, tempo,
// and export to the MPC's card.
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
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MpcView)
};
