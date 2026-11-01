#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "EditorProcessor.h"
#include "core/ui/LibraryList.h"
#include "core/ui/Theme.h"
#include "core/ui/WaveView.h"

// A small level meter for the audio input.
class LevelMeter : public juce::Component
{
public:
    void setLevel (float peak);
    void paint (juce::Graphics&) override;

private:
    float level = 0;
};

// The Sample Editor window: library on the left, the waveform in the middle, recording and
// files along the top, and the editing tools underneath.
class EditorView : public juce::AudioProcessorEditor,
                   public juce::FileDragAndDropTarget,
                   public juce::DragAndDropContainer,
                   public juce::DragAndDropTarget,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    explicit EditorView (EditorProcessor&);
    ~EditorView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    // Used by the tests to drive the window without a mouse.
    crate::WaveView& getWaveView() { return wave; }
    void runEdit (const juce::String& action) { performAction (action); }

private:
    using Buffer = juce::AudioBuffer<float>;
    using EditFn = std::function<crate::Edits::Result (const Buffer&, juce::Range<int>)>;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh();
    void say (const juce::String& message, bool problem = false);
    void edit (const juce::String& action, EditFn, bool needsSelection, bool snap);
    void performAction (const juce::String& action);

    void toggleRecording();
    void togglePlay();
    void openFile (const juce::File&);
    void chooseFile();
    void saveToLibrary();
    void exportFile();

    EditorProcessor& processor;
    EditDocument& document;
    crate::Theme::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    // top bar
    juce::TextButton recordButton { "Record" };
    LevelMeter meter;
    juce::Label recordTime;
    juce::TextButton openButton { "Open..." }, saveButton { "Save to library" }, exportButton { "Export..." };
    juce::Label nameLabel { {}, "Name" };
    juce::TextEditor nameEditor;

    crate::LibraryList libraryList;
    crate::WaveView wave;

    // transport
    juce::TextButton playButton { "Play" }, loopButton { "Loop" };
    juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Fit" }, zoomSelButton { "Zoom to selection" };
    juce::Label infoLabel;

    // edits
    juce::Label editHeading { {}, "EDIT" }, shapeHeading { "", "SHAPE" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton cutButton { "Cut" }, copyButton { "Copy" }, pasteButton { "Paste" };
    juce::TextButton deleteButton { "Delete" }, cropButton { "Trim to selection" }, silenceButton { "Silence" };
    juce::TextButton trimSilenceButton { "Trim quiet ends" };
    juce::ToggleButton snapToggle { "Snap to zero" };

    juce::TextButton fadeInButton { "Fade in" }, fadeOutButton { "Fade out" };
    juce::ComboBox curveBox;
    juce::TextButton reverseButton { "Reverse" }, normalizeButton { "Normalize" };
    juce::Slider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton gainButton { "Apply gain" };

    juce::Label message;

    std::unique_ptr<juce::FileChooser> chooser;
    bool resetViewOnNextChange = true;
    const void* shownAudio = nullptr;
    bool playingSelectionOnly = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorView)
};
