#pragma once

#include "EffectsSheet.h"
#include "PocketAudio.h"
#include "PocketHost.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"
#include "PocketWave.h"
#include "core/Edits.h"

class RecordScreen : public juce::Component, private juce::Timer
{
public:
    RecordScreen (PocketHost&, PocketAudio&, pocket::Library&, EffectsSheet&);
    ~RecordScreen() override;

    void loadSound (const juce::String& id);
    void startRecording() { if (! audio.isRecording()) toggleRecord(); }   // NEW IDEA
    // Demo for screenshots: puts a take on the screen as if it had just been recorded.
    void showDemoTake (crate::SharedAudio, const juce::String& name, bool recordingLook);
    void sheetOpened();
    void sheetClosed();
    pocket::FxKnobs& knobs() { return fx; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct State
    {
        crate::SharedAudio audio;
        juce::Range<int> selection;
        std::vector<int> markers;
    };

    void timerCallback() override;
    void toggleRecord();
    void finishRecording();
    void togglePlay();
    void play (bool withFx);
    void edit (const juce::String& what, std::function<crate::Edits::Result (const juce::AudioBuffer<float>&, juce::Range<int>)>,
               bool keepMarkers = false);
    void commit (crate::SharedAudio, juce::Range<int>, std::vector<int> markers);
    void undo();
    void redo();
    State current() const;
    void restore (const State&);
    bool needSound();
    void saveToSounds();
    void wireSheet();
    void updateLabels();
    void previewSoon();
    juce::String lengthText (double seconds) const;

    PocketHost& host;
    PocketAudio& audio;
    pocket::Library& library;
    EffectsSheet& sheet;

    PocketWave wave;
    juce::TextEditor nameField;
    pocket::PButton recButton, play_ { "Play", pocket::Look::green }, selectAll { "Select all" }, undoButton, redoButton,
        keep { "Keep" }, del { "Delete" }, trim { "Trim silence" }, fadeIn { "Fade in" }, fadeOut { "Fade out" },
        normal { "Normal." }, reverse { "Reverse" }, effectsRow, chopRow, saveButton { "Save to sounds", pocket::Look::amber },
        toPad { "To pad" }, toMac { "To mac" };

    std::vector<State> undoStack, redoStack;
    juce::String currentId;
    pocket::FxKnobs fx;
    juce::Rectangle<float> lcdArea, nameBox;
    float level = 0.0f;
    bool recordingLook = false;
    bool sheetShown = false;
    int previewCountdown = 0;
    int playOffset = 0;
    bool playingWithFx = false;
    double lastRecordSeconds = 0;
    juce::File takeFile;
};
