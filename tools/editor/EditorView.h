#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "EditorProcessor.h"
#include "core/ui/EffectsPanel.h"
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

// The on-screen keys, with the root note (where the sound plays as recorded) marked, or, when
// the keys play chopped slices, the keys that have a slice tinted.
class KeysView : public juce::MidiKeyboardComponent
{
public:
    explicit KeysView (juce::MidiKeyboardState& s) : MidiKeyboardComponent (s, horizontalKeyboard) {}
    void setRootNote (int note) { if (note != rootNote) { rootNote = note; repaint(); } }
    void setSliceKeys (int first, int count) { if (first != sliceFirst || count != sliceCount) { sliceFirst = first; sliceCount = count; repaint(); } }

    void drawWhiteNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour lineColour, juce::Colour textColour) override;
    void drawBlackNote (int note, juce::Graphics&, juce::Rectangle<float> area, bool isDown, bool isOver,
                        juce::Colour fillColour) override;

private:
    void mark (int note, juce::Graphics&, juce::Rectangle<float> area);
    int rootNote = 60;
    int sliceFirst = 0, sliceCount = 0;
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
    bool keyStateChanged (bool isKeyDown) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;
    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    // Used by the tests to drive the window without a mouse.
    crate::WaveView& getWaveView() { return wave; }
    crate::EffectsPanel& getEffectsPanel() { return effects; }
    juce::Slider& getFadeLengthSlider() { return fadeLength; }
    juce::Slider& getFadeAmountSlider() { return fadeAmount; }
    void runEdit (const juce::String& action) { performAction (action); }
    juce::ComboBox& getRootBox() { return rootBox; }
    juce::ComboBox& getKeyModeBox() { return keyModeBox; }
    juce::ToggleButton& getComputerKeysToggle() { return computerKeys; }
    juce::Slider& getSensitivitySlider() { return sensitivitySlider; }
    // A host that also has MPC pads (the Rig) sets this to show a "To MPC pads" button: it gets
    // the slices and a name, and returns what to tell the user.
    void setSendSlicesToPads (std::function<juce::String (std::vector<crate::SharedAudio>, const juce::String&)>);
    void sendSlicesToPads();

    // "findHits", "split<N>" or "clear"
    void runChop (const juce::String& action)
    {
        if (action == "findHits")          findHits (true);
        else if (action.startsWith ("split")) splitEvenly (action.substring (5).getIntValue());
        else if (action == "clear")        clearChopsButton.triggerClick();
    }
    KeysView& getKeysView() { return keyboard; }

private:
    static constexpr int kEffectsWidth = 330;
    static constexpr int kToolsHeight = 164;
    static constexpr int kKeysHeight = 104;
    static constexpr int kLowestKey = 24, kHighestKey = 96;   // C0 to C6, as Logic names them (middle C is C3)

    using Buffer = juce::AudioBuffer<float>;
    using EditFn = std::function<crate::Edits::Result (const Buffer&, juce::Range<int>)>;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void refresh();
    void say (const juce::String& message, bool problem = false);
    void edit (const juce::String& action, EditFn, bool needsSelection, bool snap);
    void performAction (const juce::String& action);

    void applyEffects();
    // Effects preview: renders the sound with the panel's effects in the background, then plays it.
    void requestPreview (bool startPlaying);
    void previewReady (crate::SharedAudio, int generation, bool startPlaying);
    juce::Range<int> playRange() const;

    void toggleRecording();
    void togglePlay();
    void openFile (const juce::File&);
    void chooseFile();
    void saveToLibrary();
    void exportFile();
    void setRootNote (int note);
    // chopping
    void findHits (bool announce);
    void splitEvenly (int slices);
    void showChops();          // markers, keys and the slice count, from the processor's chops
    void saveSlices();
    void openMidiInputs();

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
    crate::EffectsPanel effects;

    // transport
    juce::TextButton playButton { "Play" }, loopButton { "Loop" };
    juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, fitButton { "Fit" }, zoomSelButton { "Zoom to selection" };
    juce::Label infoLabel;

    // edits
    juce::Label editHeading { {}, "EDIT" }, fadeHeading { {}, "FADE" }, levelHeading { {}, "LEVEL" };
    juce::TextButton undoButton { "Undo" }, redoButton { "Redo" };
    juce::TextButton cutButton { "Cut" }, copyButton { "Copy" }, pasteButton { "Paste" };
    juce::TextButton deleteButton { "Delete" }, cropButton { "Trim to selection" }, silenceButton { "Silence" };
    juce::TextButton trimSilenceButton { "Trim quiet ends" };
    juce::ToggleButton snapToggle { "Snap to zero" };

    juce::TextButton fadeInButton { "Fade in" }, fadeOutButton { "Fade out" };
    juce::ComboBox curveBox;
    juce::Label fadeLengthLabel { {}, "Length" }, fadeAmountLabel { {}, "Amount" };
    juce::Slider fadeLength { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider fadeAmount { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton reverseButton { "Reverse" }, normalizeButton { "Normalize" };
    juce::Slider gainSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::TextButton gainButton { "Apply gain" };

    juce::Label chopHeading { {}, "CHOP" }, sensitivityLabel { {}, "Sensitivity" }, chopInfo;
    juce::TextButton findHitsButton { "Find hits" }, clearChopsButton { "Clear" }, saveSlicesButton { "Save slices..." };
    juce::TextButton sendPadsButton { "To MPC pads" };
    std::function<juce::String (std::vector<crate::SharedAudio>, const juce::String&)> sendSlices;
    juce::Slider sensitivitySlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ComboBox splitBox;
    bool chopsFromHits = false;   // moving Sensitivity re-finds them
    // keys
    juce::Label keysHeading { {}, "KEYS" }, rootLabel { {}, "Root" }, keyModeLabel { {}, "Plays" };
    juce::Label attackLabel { {}, "Attack" }, releaseLabel { {}, "Release" };
    juce::ComboBox rootBox, keyModeBox;
    juce::Slider attackSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider releaseSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::ToggleButton computerKeys { "Play with computer keys" };
    KeysView keyboard { processor.keyboardState };
    juce::Rectangle<int> toolsArea, keysArea;
    double nextMidiScan = 0;
    int keysOctave = 5;

    juce::Label message;

    std::unique_ptr<juce::FileChooser> chooser;
    bool resetViewOnNextChange = true;
    const void* shownAudio = nullptr;

    // effects preview
    juce::ThreadPool renderPool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };
    std::atomic<int> previewGeneration { 0 };
    bool playingPreview = false;
    juce::Range<int> previewRange;        // the part of the sound the preview was made from
    double previewDueAt = 0;              // when to render after the controls stop moving (ms)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EditorView)
};
