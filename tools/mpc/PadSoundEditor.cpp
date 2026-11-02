#include "PadSoundEditor.h"

#include "core/SoundFx.h"
#include "core/ui/Theme.h"

namespace Edits = crate::Edits;
namespace Theme = crate::Theme;

PadSoundEditor::PadSoundEditor (MpcProcessor& p) : processor (p), project (p.getProject())
{
    setWantsKeyboardFocus (true);

    title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    info.setColour (juce::Label::textColourId, Theme::textDim);
    info.setFont (juce::FontOptions (13.0f));

    doneButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    doneButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    doneButton.setTooltip ("Back to the pads (Esc)");
    doneButton.onClick = [this] { if (onClose) onClose(); };

    wave.setEmptyText ("This pad is empty");
    wave.onSelectionChange = [this] { refresh(); };

    playButton.setTooltip ("Play the selection, or the whole sound (Space). With Hear effects on, you hear the effects too.");
    playButton.onClick = [this] { togglePlay(); };

    trimButton.setTooltip ("Keep only the selection: drag across the waveform, or drag a selection's edges, first (T)");
    trimButton.onClick = [this] { apply ("Trimmed to the selection", [] (const Buffer& b, auto r) { return Edits::crop (b, r); }, true); };
    deleteButton.setTooltip ("Cut the selection out (Delete)");
    deleteButton.onClick = [this] { apply ("Deleted the selection", [] (const Buffer& b, auto r) { return Edits::remove (b, r); }, true); };
    silenceButton.setTooltip ("Cut the quiet bits off both ends");
    silenceButton.onClick = [this] { apply ("Trimmed the quiet ends", [] (const Buffer& b, auto) { return Edits::trimSilence (b); }); };

    fadeCurve.addItem ("Smooth", 1 + (int) Edits::FadeCurve::smooth);
    fadeCurve.addItem ("Linear", 1 + (int) Edits::FadeCurve::linear);
    fadeCurve.addItem ("Fast", 1 + (int) Edits::FadeCurve::fast);
    fadeCurve.addItem ("Slow", 1 + (int) Edits::FadeCurve::slow);
    fadeCurve.setSelectedId (1 + (int) Edits::FadeCurve::smooth, juce::dontSendNotification);
    fadeCurve.setTooltip ("The shape of fades");

    // Without a selection, fades cover a sensible bit of the sound: a short fade in to take the
    // click off the start, and the last quarter for a fade out.
    auto fadeRange = [] (const Buffer& b, juce::Range<int> r, bool fadeIn)
    {
        if (! r.isEmpty())
            return r;
        const int n = b.getNumSamples();
        return fadeIn ? juce::Range<int> (0, juce::jmin (n, (int) (0.01 * crate::kSampleRate)))
                      : juce::Range<int> (n - n / 4, n);
    };
    fadeInButton.setTooltip ("Fade in across the selection (or take the click off the start)");
    fadeInButton.onClick = [this, fadeRange]
    {
        const auto curve = (Edits::FadeCurve) (fadeCurve.getSelectedId() - 1);
        apply ("Faded in", [fadeRange, curve] (const Buffer& b, auto r) { return Edits::fade (b, fadeRange (b, r, true), true, curve); });
    };
    fadeOutButton.setTooltip ("Fade out across the selection (or the last quarter)");
    fadeOutButton.onClick = [this, fadeRange]
    {
        const auto curve = (Edits::FadeCurve) (fadeCurve.getSelectedId() - 1);
        apply ("Faded out", [fadeRange, curve] (const Buffer& b, auto r) { return Edits::fade (b, fadeRange (b, r, false), false, curve); });
    };
    normalizeButton.onClick = [this] { apply ("Normalized", [] (const Buffer& b, auto r) { return Edits::normalize (b, r); }); };
    reverseButton.onClick = [this] { apply ("Reversed", [] (const Buffer& b, auto r) { return Edits::reverse (b, r); }); };
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };

    effects.onApply = [this] { applyEffects(); };
    effects.onChange = [this]
    {
        // Hearing the effects while they play: start again with the new settings.
        if (processor.getPreviewPosition() >= 0 && effects.isPreviewOn())
            togglePlay(), togglePlay();
    };
    effects.onPreviewChanged = [this] (bool on)
    {
        say (on ? "Effects preview is on: press Play to hear the pad with the effects, then Apply to keep them."
                : "Effects preview is off.");
    };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &doneButton, &wave, &info, &playButton, &trimButton,
                                                             &deleteButton, &silenceButton, &fadeInButton, &fadeOutButton,
                                                             &fadeCurve, &normalizeButton, &reverseButton, &undoButton,
                                                             &redoButton, &effects })
        addAndMakeVisible (c);
}

PadSoundEditor::~PadSoundEditor()
{
    processor.stopPreview();
}

Pad* PadSoundEditor::pad()
{
    if (! juce::isPositiveAndBelow (padIndex, mpc::kPadCount))
        return nullptr;
    return &project.currentBank().pads[(size_t) padIndex];
}

void PadSoundEditor::open (int index)
{
    processor.stopPreview();
    padIndex = index;
    undoStack.clear();
    redoStack.clear();
    effects.reset();

    auto* p = pad();
    shown = p != nullptr ? p->audio : nullptr;
    wave.setAudio (shown);
    wave.selectAll();
    refresh();
    startTimerHz (30);
    grabKeyboardFocus();
}

void PadSoundEditor::close()
{
    processor.stopPreview();
    stopTimer();
    playing = nullptr;
    undoStack.clear();
    redoStack.clear();
}

void PadSoundEditor::sync()
{
    auto* p = pad();
    if (p == nullptr || p->audio != shown)
        open (padIndex);
    else
        refresh();
}

void PadSoundEditor::setPadAudio (SharedAudio audio, juce::Range<int> selection)
{
    auto* p = pad();
    if (p == nullptr)
        return;

    processor.stopPreview();
    p->audio = std::move (audio);
    shown = p->audio;
    playing = nullptr;
    project.changed();
    wave.setAudio (p->audio, true);
    wave.setSelection (selection);
    refresh();
}

void PadSoundEditor::apply (const juce::String& what, Edit edit, bool needsSelection)
{
    auto* p = pad();
    if (p == nullptr || p->isEmpty())
        return;
    if (needsSelection && ! wave.hasSelection())
        return say ("Drag across the waveform to select first.", true);

    // Keeping the whole sound, or deleting all of it, changes nothing useful.
    const auto range = wave.getSelection();
    if (needsSelection && range.getLength() >= p->audio->getNumSamples() && what.startsWith ("Deleted"))
        return say ("That would delete the whole sound. Clear the pad instead.", true);

    auto result = edit (*p->audio, range);
    if (result.audio.getNumSamples() == 0)
        return say ("Nothing left after that.", true);

    undoStack.push_back (p->audio);
    if ((int) undoStack.size() > kMaxUndo)
        undoStack.erase (undoStack.begin());
    redoStack.clear();

    setPadAudio (crate::share (std::move (result.audio)), result.selection);
    say (what + ". Undo with Cmd+Z.");
}

void PadSoundEditor::undo()
{
    auto* p = pad();
    if (p == nullptr || undoStack.empty())
        return;
    redoStack.push_back (p->audio);
    auto previous = undoStack.back();
    undoStack.pop_back();
    setPadAudio (previous, {});
}

void PadSoundEditor::redo()
{
    auto* p = pad();
    if (p == nullptr || redoStack.empty())
        return;
    undoStack.push_back (p->audio);
    auto next = redoStack.back();
    redoStack.pop_back();
    setPadAudio (next, {});
}

void PadSoundEditor::togglePlay()
{
    auto* p = pad();
    if (processor.getPreviewPosition() >= 0)
    {
        processor.stopPreview();
        playButton.setButtonText ("Play");
        return;
    }
    if (p == nullptr || p->isEmpty())
        return;

    const auto range = Edits::orWhole (*p->audio, wave.getSelection());
    if (effects.isPreviewOn() && effects.hasEffects())
    {
        // The effects can make the sound longer (delay and reverb tails), so play the processed
        // selection from the start.
        playing = crate::share (crate::SoundFx::apply (Edits::copy (*p->audio, range), effects.getSettings()));
        processor.preview (playing, {});
    }
    else
    {
        playing = p->audio;
        processor.preview (playing, range);
    }
    playButton.setButtonText ("Stop");
}

void PadSoundEditor::applyEffects()
{
    if (! effects.hasEffects())
        return say ("Switch on an effect first, or pick a preset.", true);

    const auto settings = effects.getSettings();
    const bool whole = ! wave.hasSelection() || wave.getSelection().getLength() >= pad()->audio->getNumSamples();
    apply (whole ? "Effects applied" : "Effects applied to the selection", [settings, whole] (const Buffer& b, juce::Range<int> r)
    {
        if (whole)
            return Edits::Result { crate::SoundFx::apply (b, settings), {} };
        return Edits::paste (b, r, crate::SoundFx::apply (Edits::copy (b, r), settings));
    });
    effects.reset();
}

void PadSoundEditor::timerCallback()
{
    const int position = processor.getPreviewPosition();
    // The playhead only makes sense on the pad's own sound, not a processed copy.
    wave.setPlayhead (position >= 0 && pad() != nullptr && playing == pad()->audio ? position : -1);
    if (position < 0 && playButton.getButtonText() == "Stop")
        playButton.setButtonText ("Play");
}

void PadSoundEditor::refresh()
{
    auto* p = pad();
    const bool has = p != nullptr && ! p->isEmpty();
    title.setText ("Edit pad " + padLabel (juce::jmax (0, padIndex)) + (p != nullptr && p->name.isNotEmpty() ? ": " + p->name : juce::String()),
                   juce::dontSendNotification);

    if (has)
    {
        const auto sel = wave.getSelection();
        const bool partial = wave.hasSelection() && sel.getLength() < p->audio->getNumSamples();
        info.setText (crate::formatTime (p->audio->getNumSamples()) + (p->audio->getNumChannels() > 1 ? ", stereo" : ", mono")
                          + (partial ? ". Selected " + crate::formatTime (sel.getLength()) + "." : ". Drag across the waveform, or drag the selection's edges, to trim."),
                      juce::dontSendNotification);
        effects.setApplyText (partial ? "Apply to selection" : "Apply to the pad");
    }
    else
    {
        info.setText ({}, juce::dontSendNotification);
    }

    for (auto* b : { &playButton, &trimButton, &deleteButton, &silenceButton, &fadeInButton, &fadeOutButton, &normalizeButton, &reverseButton })
        b->setEnabled (has);
    undoButton.setEnabled (! undoStack.empty());
    redoButton.setEnabled (! redoStack.empty());
    effects.setEnabled (has);
}

bool PadSoundEditor::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    if (key == juce::KeyPress::spaceKey) { togglePlay(); return true; }
    if (key == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
    if (key == juce::KeyPress ('z', cmd, 0)) { undo(); return true; }
    if (key == juce::KeyPress ('z', cmd | juce::ModifierKeys::shiftModifier, 0)) { redo(); return true; }
    if (key == juce::KeyPress ('t')) { trimButton.triggerClick(); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteButton.triggerClick(); return true; }
    return false;
}

void PadSoundEditor::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void PadSoundEditor::resized()
{
    auto r = getLocalBounds().reduced (14);

    auto top = r.removeFromTop (32);
    doneButton.setBounds (top.removeFromRight (90));
    title.setBounds (top);
    r.removeFromTop (8);

    effects.setBounds (r.removeFromRight (juce::jmin (330, r.getWidth() / 2)));
    r.removeFromRight (12);

    auto row = [&r] (std::initializer_list<juce::Component*> items)
    {
        auto line = r.removeFromBottom (30);
        const int w = (line.getWidth() - 6 * ((int) items.size() - 1)) / (int) items.size();
        for (auto* c : items)
        {
            c->setBounds (line.removeFromLeft (w));
            line.removeFromLeft (6);
        }
        r.removeFromBottom (6);
    };

    row ({ &fadeInButton, &fadeOutButton, &fadeCurve, &normalizeButton, &reverseButton });
    row ({ &playButton, &trimButton, &deleteButton, &silenceButton, &undoButton, &redoButton });
    info.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    wave.setBounds (r);
}
