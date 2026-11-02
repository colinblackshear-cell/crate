#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "MpcProcessor.h"
#include "core/Edits.h"
#include "core/ui/EffectsPanel.h"
#include "core/ui/WaveView.h"

// Edits one pad's sound in place: trim it, fade it, put effects on it. It's shown over the pad
// grid while you work on a pad. Every change goes straight to the pad (so the pad, MIDI and
// export all use it), and can be undone until you close the editor.
class PadSoundEditor : public juce::Component,
                       private juce::Timer
{
public:
    explicit PadSoundEditor (MpcProcessor&);
    ~PadSoundEditor() override;

    void open (int pad);
    void close();
    // Call when the project changed: if the pad's sound was replaced from elsewhere (cleared,
    // reloaded, another kit picked), the editor shows the new sound and forgets its undo.
    void sync();
    int getPad() const { return padIndex; }

    std::function<void()> onClose;
    std::function<void (const juce::String&, bool warning)> onStatus;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int kMaxUndo = 50;

private:
    using Buffer = juce::AudioBuffer<float>;
    using Edit = std::function<crate::Edits::Result (const Buffer&, juce::Range<int>)>;

    void timerCallback() override;
    Pad* pad();
    void apply (const juce::String& what, Edit, bool needsSelection = false);
    void setPadAudio (SharedAudio, juce::Range<int> selection);
    void undo();
    void redo();
    void togglePlay();
    void applyEffects();
    void refresh();
    void say (const juce::String& text, bool warning = false) { if (onStatus) onStatus (text, warning); }

    MpcProcessor& processor;
    Project& project;
    int padIndex = -1;

    std::vector<SharedAudio> undoStack, redoStack;
    SharedAudio shown;     // the pad's sound as this editor last set or saw it
    SharedAudio playing;   // what the preview plays: the pad's sound, or it with the effects on

    juce::Label title;
    juce::TextButton doneButton { "Done" };
    crate::WaveView wave;
    juce::Label info;

    juce::TextButton playButton { "Play" }, trimButton { "Trim" }, deleteButton { "Delete" },
                     silenceButton { "Trim silence" }, fadeInButton { "Fade in" }, fadeOutButton { "Fade out" },
                     normalizeButton { "Normalize" }, reverseButton { "Reverse" },
                     undoButton { "Undo" }, redoButton { "Redo" };
    juce::ComboBox fadeCurve;

    crate::EffectsPanel effects;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadSoundEditor)
};
