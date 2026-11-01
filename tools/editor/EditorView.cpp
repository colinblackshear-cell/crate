#include "EditorView.h"

#include "core/AudioFiles.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using crate::Theme::LookAndFeel;
namespace Edits = crate::Edits;

//==============================================================================
void LevelMeter::setLevel (float peak)
{
    level = juce::jmax (peak, level * 0.85f);
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (crate::Theme::raised);
    g.fillRoundedRectangle (r, 3.0f);

    // -60 dB .. 0 dB
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float amount = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto bar = r.reduced (2).withWidth ((r.getWidth() - 4) * amount);
    g.setColour (level > 0.98f ? crate::Theme::record : db > -12.0f ? crate::Theme::accent : crate::Theme::waveform);
    g.fillRoundedRectangle (bar, 2.0f);
}

//==============================================================================
EditorView::EditorView (EditorProcessor& p)
    : AudioProcessorEditor (p), processor (p), document (p.getDocument()), libraryList (p.getLibrary())
{
    setLookAndFeel (&lookAndFeel);

   #if JucePlugin_Build_Standalone
    // The app mutes the input by default to avoid feedback. We never send the input to the
    // speakers, so unmute it: otherwise recordings come out silent.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->getMuteInputValue().setValue (false);
   #endif

    // top bar
    recordButton.setColour (juce::TextButton::buttonOnColourId, crate::Theme::record);
    recordButton.setTooltip ("Record from your audio input (R). Press again to stop.");
    recordButton.onClick = [this] { toggleRecording(); };
    recordTime.setColour (juce::Label::textColourId, crate::Theme::textDim);
    openButton.setTooltip ("Open a sound file: WAV, AIFF, MP3, FLAC (Cmd+O). You can also drag files onto the window.");
    openButton.onClick = [this] { chooseFile(); };
    saveButton.setTooltip ("Save this sound to the shared library, where every Crate tool can use it (Cmd+S)");
    saveButton.onClick = [this] { saveToLibrary(); };
    exportButton.setTooltip ("Save a copy anywhere as WAV or AIFF");
    exportButton.onClick = [this] { exportFile(); };
    nameLabel.setColour (juce::Label::textColourId, crate::Theme::textDim);
    nameEditor.setTextToShowWhenEmpty ("Untitled", crate::Theme::textDim);
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (8, 0);
    nameEditor.onReturnKey = [this] { grabKeyboardFocus(); };
    nameEditor.onFocusLost = [this]
    {
        if (document.hasAudio() && nameEditor.getText() != document.getName())
            document.setName (nameEditor.getText().trim());
    };

    libraryList.onOpen = [this] (const SoundLibrary::Entry& e) { openFile (e.file); };

    wave.onSelectionChange = [this]
    {
        grabKeyboardFocus();
        refresh();
    };

    // transport
    playButton.setTooltip ("Play the selection, or from the cursor (Space)");
    playButton.onClick = [this] { togglePlay(); };
    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Loop playback (L)");
    loopButton.onClick = [this] { processor.setLooping (loopButton.getToggleState()); };
    zoomOutButton.onClick = [this] { wave.zoomOut(); };
    zoomInButton.onClick = [this] { wave.zoomIn(); };
    fitButton.setTooltip ("Show the whole sound");
    fitButton.onClick = [this] { wave.zoomToFit(); };
    zoomSelButton.onClick = [this] { wave.zoomToSelection(); };
    infoLabel.setJustificationType (juce::Justification::centredRight);
    infoLabel.setColour (juce::Label::textColourId, crate::Theme::textDim);

    // edits
    for (auto* l : { &editHeading, &fadeHeading, &levelHeading })
    {
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, crate::Theme::textDim);
    }

    const std::initializer_list<std::pair<juce::TextButton*, const char*>> actions {
        { &undoButton, "undo" }, { &redoButton, "redo" },
        { &cutButton, "cut" }, { &copyButton, "copy" }, { &pasteButton, "paste" },
        { &deleteButton, "delete" }, { &cropButton, "crop" }, { &silenceButton, "silence" },
        { &trimSilenceButton, "trimSilence" },
        { &fadeInButton, "fadeIn" }, { &fadeOutButton, "fadeOut" },
        { &reverseButton, "reverse" }, { &normalizeButton, "normalize" }, { &gainButton, "gain" } };
    for (auto& [button, action] : actions)
    {
        const juce::String name (action);
        button->onClick = [this, name] { performAction (name); };
    }

    undoButton.setTooltip ("Undo (Cmd+Z)");
    redoButton.setTooltip ("Redo (Cmd+Shift+Z)");
    cutButton.setTooltip ("Cut the selection (Cmd+X)");
    copyButton.setTooltip ("Copy the selection, or the whole sound (Cmd+C)");
    pasteButton.setTooltip ("Paste at the cursor, or over the selection (Cmd+V)");
    deleteButton.setTooltip ("Delete the selection (Delete)");
    cropButton.setTooltip ("Keep only the selection (T)");
    silenceButton.setTooltip ("Make the selection quiet without changing the length");
    trimSilenceButton.setTooltip ("Cut the quiet bits off the start and end");
    snapToggle.setTooltip ("Move cut points to where the wave crosses zero, so cuts don't click");
    snapToggle.setToggleState (true, juce::dontSendNotification);
    fadeInButton.setTooltip ("Fade in across the selection, or over Length at the start if nothing is selected (I)");
    fadeOutButton.setTooltip ("Fade out across the selection, or over Length at the end if nothing is selected (O)");
    for (auto* l : { &fadeLengthLabel, &fadeAmountLabel })
        l->setColour (juce::Label::textColourId, crate::Theme::textDim);
    fadeLength.setRange (5.0, 10000.0, 1.0);
    fadeLength.setSkewFactorFromMidPoint (500.0);
    fadeLength.setValue (300.0, juce::dontSendNotification);
    fadeLength.setTextValueSuffix (" ms");
    fadeLength.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    fadeLength.setTooltip ("How long fades are when nothing is selected. With a selection, the fade covers the selection.");
    fadeAmount.setRange (5.0, 100.0, 1.0);
    fadeAmount.setValue (100.0, juce::dontSendNotification);
    fadeAmount.setTextValueSuffix (" %");
    fadeAmount.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    fadeAmount.setDoubleClickReturnValue (true, 100.0);
    fadeAmount.setTooltip ("How far the level drops: 100% fades to silence, 50% only to half volume");
    curveBox.addItemList ({ "Straight", "Smooth", "Fast", "Slow" }, 1);
    curveBox.setSelectedId (2, juce::dontSendNotification);
    curveBox.setTooltip ("The shape of fades: Smooth is gentle at both ends, Fast changes early, Slow changes late");
    reverseButton.setTooltip ("Play the selection, or the whole sound, backwards");
    normalizeButton.setTooltip ("Make the selection, or the whole sound, as loud as it can go without clipping");
    gainSlider.setRange (-24.0, 24.0, 0.5);
    gainSlider.setValue (3.0, juce::dontSendNotification);
    gainSlider.setTextValueSuffix (" dB");
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 22);
    gainButton.setTooltip ("Turn the selection, or the whole sound, up or down by this much");

    message.setColour (juce::Label::textColourId, crate::Theme::textDim);

    effects.onChange = [this]
    {
        if (playingPreview && processor.isPlaying())
            previewDueAt = juce::Time::getMillisecondCounterHiRes() + 150.0;   // re-render once the control settles
        refresh();
    };
    effects.onPreviewChanged = [this] (bool on)
    {
        if (processor.isPlaying())
            processor.stop();
        playingPreview = false;
        say (on ? "Effects preview is on: press Play (Space) to hear the selection, or the whole sound, with the effects."
                : "Effects preview is off.");
    };
    effects.onApply = [this] { applyEffects(); };

    for (auto* c : std::initializer_list<juce::Component*> {
             &recordButton, &meter, &recordTime, &openButton, &saveButton, &exportButton, &nameLabel, &nameEditor,
             &libraryList, &wave, &playButton, &loopButton, &zoomOutButton, &zoomInButton, &fitButton, &zoomSelButton,
             &infoLabel, &editHeading, &undoButton, &redoButton, &cutButton, &copyButton, &pasteButton,
             &deleteButton, &cropButton, &silenceButton, &trimSilenceButton, &snapToggle, &fadeInButton,
             &fadeOutButton, &curveBox, &fadeHeading, &levelHeading, &fadeLength, &fadeAmount,
             &fadeLengthLabel, &fadeAmountLabel, &effects, &reverseButton, &normalizeButton, &gainSlider, &gainButton, &message })
        addAndMakeVisible (c);

    document.addChangeListener (this);
    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1100, 680, 2600, 1700);
    setSize (1440, 800);

    changeListenerCallback (&document);   // show the sound that's already open (window reopened)
    say (processor.hasInput() ? "Press Record, open a file, or pick a sound from the library."
                              : "Open a file or pick a sound from the library. (No audio input is connected, so recording is off.)");
    refresh();
    startTimerHz (30);
}

EditorView::~EditorView()
{
    renderPool.removeAllJobs (true, 5000);
    document.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void EditorView::paint (juce::Graphics& g)
{
    g.fillAll (crate::Theme::background);

    // the tools panel under the waveform
    auto tools = getLocalBounds().reduced (12);
    tools.removeFromLeft (252);
    tools.removeFromRight (kEffectsWidth + 12);
    tools = tools.removeFromBottom (kToolsHeight + 26).withTrimmedBottom (26);
    g.setColour (crate::Theme::panel);
    g.fillRoundedRectangle (tools.toFloat(), 6.0f);
}

void EditorView::resized()
{
    auto r = getLocalBounds().reduced (12);

    // top bar
    auto top = r.removeFromTop (34);
    recordButton.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (8);
    meter.setBounds (top.removeFromLeft (120).reduced (0, 10));
    top.removeFromLeft (6);
    recordTime.setBounds (top.removeFromLeft (80));
    exportButton.setBounds (top.removeFromRight (92));
    top.removeFromRight (6);
    saveButton.setBounds (top.removeFromRight (128));
    top.removeFromRight (6);
    openButton.setBounds (top.removeFromRight (82));
    top.removeFromRight (16);
    nameLabel.setBounds (top.removeFromLeft (46));
    nameEditor.setBounds (top.withSizeKeepingCentre (top.getWidth(), 28));

    r.removeFromTop (12);
    libraryList.setBounds (r.removeFromLeft (240));
    r.removeFromLeft (12);
    effects.setBounds (r.removeFromRight (kEffectsWidth));
    r.removeFromRight (12);

    // tools and message at the bottom
    message.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);
    auto tools = r.removeFromBottom (kToolsHeight).reduced (10, 6);
    r.removeFromBottom (10);

    auto row1 = tools.removeFromTop (38).reduced (0, 4);
    editHeading.setBounds (row1.removeFromLeft (52));
    for (auto* b : { &undoButton, &redoButton })
        b->setBounds (row1.removeFromLeft (58)), row1.removeFromLeft (4);
    row1.removeFromLeft (10);
    for (auto* b : { &cutButton, &copyButton, &pasteButton, &deleteButton })
        b->setBounds (row1.removeFromLeft (60)), row1.removeFromLeft (4);
    row1.removeFromLeft (10);
    cropButton.setBounds (row1.removeFromLeft (124));
    row1.removeFromLeft (4);
    silenceButton.setBounds (row1.removeFromLeft (68));
    row1.removeFromLeft (4);
    trimSilenceButton.setBounds (row1.removeFromLeft (118));

    auto row2 = tools.removeFromTop (38).reduced (0, 4);
    fadeHeading.setBounds (row2.removeFromLeft (52));
    fadeInButton.setBounds (row2.removeFromLeft (72));
    row2.removeFromLeft (4);
    fadeOutButton.setBounds (row2.removeFromLeft (76));
    row2.removeFromLeft (4);
    curveBox.setBounds (row2.removeFromLeft (96));
    row2.removeFromLeft (14);
    fadeLengthLabel.setBounds (row2.removeFromLeft (52));
    fadeLength.setBounds (row2.removeFromLeft (200));
    row2.removeFromLeft (12);
    fadeAmountLabel.setBounds (row2.removeFromLeft (60));
    fadeAmount.setBounds (row2.removeFromLeft (juce::jmin (180, row2.getWidth())));

    auto row3 = tools.removeFromTop (38).reduced (0, 4);
    levelHeading.setBounds (row3.removeFromLeft (52));
    reverseButton.setBounds (row3.removeFromLeft (76));
    row3.removeFromLeft (4);
    normalizeButton.setBounds (row3.removeFromLeft (88));
    row3.removeFromLeft (14);
    gainButton.setBounds (row3.removeFromLeft (92));
    row3.removeFromLeft (4);
    gainSlider.setBounds (row3.removeFromLeft (juce::jmin (220, row3.getWidth())));
    row3.removeFromLeft (16);
    snapToggle.setBounds (row3.removeFromLeft (juce::jmin (130, row3.getWidth())));

    // transport under the waveform
    auto transport = r.removeFromBottom (30);
    playButton.setBounds (transport.removeFromLeft (80));
    transport.removeFromLeft (4);
    loopButton.setBounds (transport.removeFromLeft (64));
    transport.removeFromLeft (16);
    zoomOutButton.setBounds (transport.removeFromLeft (30));
    transport.removeFromLeft (4);
    zoomInButton.setBounds (transport.removeFromLeft (30));
    transport.removeFromLeft (4);
    fitButton.setBounds (transport.removeFromLeft (44));
    transport.removeFromLeft (4);
    zoomSelButton.setBounds (transport.removeFromLeft (132));
    infoLabel.setBounds (transport);
    r.removeFromBottom (8);

    wave.setBounds (r);
}

//==============================================================================
void EditorView::say (const juce::String& text, bool problem)
{
    message.setText (text, juce::dontSendNotification);
    message.setColour (juce::Label::textColourId, problem ? crate::Theme::record : crate::Theme::textDim);
}

void EditorView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // the sound changed (edit, undo, redo, load): stop playing the old version
    if (document.getAudio().get() != shownAudio)
    {
        processor.stop();
        shownAudio = document.getAudio().get();
        wave.setAudio (document.getAudio(), ! resetViewOnNextChange);
        wave.setSelection (document.getSelection());
        resetViewOnNextChange = false;
    }

    if (! nameEditor.hasKeyboardFocus (true))
        nameEditor.setText (document.getName(), juce::dontSendNotification);
    refresh();
}

void EditorView::refresh()
{
    const bool has = document.hasAudio();
    const bool selected = has && wave.hasSelection();
    const bool recording = processor.isRecording();

    recordButton.setButtonText (recording ? "Stop" : "Record");
    recordButton.setToggleState (recording, juce::dontSendNotification);
    recordButton.setEnabled (processor.hasInput());

    undoButton.setEnabled (document.canUndo() && ! recording);
    undoButton.setTooltip (document.canUndo() ? "Undo " + document.getUndoName() + " (Cmd+Z)" : "Nothing to undo");
    redoButton.setEnabled (document.canRedo() && ! recording);
    redoButton.setTooltip (document.canRedo() ? "Redo " + document.getRedoName() + " (Cmd+Shift+Z)" : "Nothing to redo");

    for (auto* b : { &copyButton, &reverseButton, &normalizeButton, &gainButton, &trimSilenceButton,
                     &saveButton, &exportButton, &playButton, &fadeInButton, &fadeOutButton })
        b->setEnabled (has && ! recording);
    for (auto* b : { &cutButton, &deleteButton, &cropButton, &silenceButton })
        b->setEnabled (selected && ! recording);
    pasteButton.setEnabled (processor.clipboard.getNumSamples() > 0 && ! recording);
    openButton.setEnabled (! recording);
    effects.setApplyText (selected ? "Apply to selection" : "Apply to whole sound");

    playButton.setButtonText (processor.isPlaying() ? "Stop" : "Play");
    saveButton.setButtonText (document.hasAudio() && document.hasUnsavedChanges() ? "Save to library *" : "Save to library");

    if (! has)
    {
        infoLabel.setText ({}, juce::dontSendNotification);
        return;
    }

    const auto& audio = *document.getAudio();
    const auto sel = wave.getSelection();
    juce::String text = crate::formatTime (audio.getNumSamples()) + (audio.getNumChannels() > 1 ? ", stereo" : ", mono");
    if (sel.isEmpty())
        text = "Cursor " + crate::formatTime (sel.getStart()) + "   |   " + text;
    else
        text = "Selected " + crate::formatTime (sel.getStart()) + " to " + crate::formatTime (sel.getEnd())
             + " (" + crate::formatTime (sel.getLength()) + ")   |   " + text;
    infoLabel.setText (text, juce::dontSendNotification);
}

void EditorView::timerCallback()
{
    meter.setLevel (processor.takeInputLevel());

    if (processor.isRecording())
    {
        processor.collectRecording();
        recordTime.setText (crate::formatTime (crate::samples (processor.getRecordedSeconds())), juce::dontSendNotification);
        if (! processor.isRecording())   // hit the length limit
            toggleRecording();
    }

    if (playingPreview && processor.isPlaying())
    {
        const double f = processor.getPlayFraction();
        wave.setPlayhead (f < 0 ? -1 : previewRange.getStart() + juce::jmin ((int) (f * previewRange.getLength()), previewRange.getLength()));
    }
    else
    {
        wave.setPlayhead (processor.getPlayPosition());
    }

    if (previewDueAt > 0 && juce::Time::getMillisecondCounterHiRes() >= previewDueAt)
        requestPreview (false);
    const bool playing = processor.isPlaying();
    if (playButton.getButtonText() != (playing ? "Stop" : "Play"))
        playButton.setButtonText (playing ? "Stop" : "Play");
}

//==============================================================================
void EditorView::edit (const juce::String& action, EditFn fn, bool needsSelection, bool snap)
{
    if (! document.hasAudio())
        return say ("There's no sound yet. Record one, open a file, or pick one from the library.", true);

    auto selection = wave.getSelection();
    if (needsSelection && selection.isEmpty())
        return say ("Select part of the sound first: drag across the waveform.", true);

    if (snap && snapToggle.getToggleState())
        selection = Edits::snapToZeroCrossings (*document.getAudio(), selection);

    processor.stop();
    auto result = fn (*document.getAudio(), selection);
    document.apply (std::move (result), action, wave.getSelection());
    say (action + " done. Undo with Cmd+Z.");
}

void EditorView::performAction (const juce::String& action)
{
    const auto curve = (Edits::FadeCurve) juce::jmax (0, curveBox.getSelectedItemIndex());

    if (action == "undo")
    {
        if (document.canUndo())
        {
            const auto name = document.getUndoName();
            document.undo();
            say ("Undid " + name + ".");
        }
    }
    else if (action == "redo")
    {
        if (document.canRedo())
        {
            const auto name = document.getRedoName();
            document.redo();
            say ("Redid " + name + ".");
        }
    }
    else if (action == "copy")
    {
        if (document.hasAudio())
        {
            processor.clipboard = Edits::copy (*document.getAudio(), wave.getSelection());
            say ("Copied " + crate::formatTime (processor.clipboard.getNumSamples()) + ".");
            refresh();
        }
    }
    else if (action == "cut")
    {
        if (document.hasAudio() && wave.hasSelection())
            processor.clipboard = Edits::copy (*document.getAudio(), snapToggle.getToggleState()
                                                   ? Edits::snapToZeroCrossings (*document.getAudio(), wave.getSelection())
                                                   : wave.getSelection());
        edit ("Cut", [] (const Buffer& b, juce::Range<int> r) { return Edits::remove (b, r); }, true, true);
    }
    else if (action == "paste")
    {
        if (processor.clipboard.getNumSamples() == 0)
            return say ("Nothing to paste yet. Copy part of a sound first.", true);
        if (! document.hasAudio())
        {
            document.load (crate::share (processor.clipboard), "Pasted sound", "Paste");
            return;
        }
        const auto clip = processor.clipboard;
        edit ("Paste", [clip] (const Buffer& b, juce::Range<int> r) { return Edits::paste (b, r, clip); }, false, true);
    }
    else if (action == "delete")
        edit ("Delete", [] (const Buffer& b, juce::Range<int> r) { return Edits::remove (b, r); }, true, true);
    else if (action == "crop")
        edit ("Trim to selection", [] (const Buffer& b, juce::Range<int> r) { return Edits::crop (b, r); }, true, true);
    else if (action == "silence")
        edit ("Silence", [] (const Buffer& b, juce::Range<int> r) { return Edits::silence (b, r); }, true, true);
    else if (action == "trimSilence")
        edit ("Trim quiet ends", [] (const Buffer& b, juce::Range<int>) { return Edits::trimSilence (b); }, false, false);
    else if (action == "fadeIn" || action == "fadeOut")
    {
        const bool in = action == "fadeIn";
        const int length = crate::samples (fadeLength.getValue() / 1000.0);
        const float amount = (float) fadeAmount.getValue() / 100.0f;
        edit (in ? "Fade in" : "Fade out", [=] (const Buffer& b, juce::Range<int> r)
        {
            // nothing selected: fade the start (or end) of the sound over the set length
            const int n = b.getNumSamples();
            if (r.isEmpty())
                r = in ? juce::Range<int> (0, juce::jmin (n, length)) : juce::Range<int> (juce::jmax (0, n - length), n);
            auto result = Edits::fade (b, r, in, curve, amount);
            result.selection = {};
            return result;
        }, false, false);
    }
    else if (action == "reverse")
        edit ("Reverse", [] (const Buffer& b, juce::Range<int> r) { return Edits::reverse (b, r); }, false, true);
    else if (action == "normalize")
        edit ("Normalize", [] (const Buffer& b, juce::Range<int> r) { return Edits::normalize (b, r); }, false, false);
    else if (action == "applyEffects")
        applyEffects();
    else if (action == "gain")
    {
        const float db = (float) gainSlider.getValue();
        edit ("Gain " + juce::String (db, 1) + " dB",
              [db] (const Buffer& b, juce::Range<int> r) { return Edits::gain (b, r, db); }, false, false);
    }
}

bool EditorView::keyPressed (const juce::KeyPress& key)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    if (key == juce::KeyPress::spaceKey)                                  togglePlay();
    else if (key == juce::KeyPress ('z', cmd, 0))                         performAction ("undo");
    else if (key == juce::KeyPress ('z', cmd | shift, 0)
             || key == juce::KeyPress ('y', cmd, 0))                      performAction ("redo");
    else if (key == juce::KeyPress ('x', cmd, 0))                         performAction ("cut");
    else if (key == juce::KeyPress ('c', cmd, 0))                         performAction ("copy");
    else if (key == juce::KeyPress ('v', cmd, 0))                         performAction ("paste");
    else if (key == juce::KeyPress ('a', cmd, 0))                         wave.selectAll();
    else if (key == juce::KeyPress ('o', cmd, 0))                         chooseFile();
    else if (key == juce::KeyPress ('s', cmd, 0))                         saveToLibrary();
    else if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) performAction ("delete");
    else if (key == juce::KeyPress ('t'))                                 performAction ("crop");
    else if (key == juce::KeyPress ('i'))                                 performAction ("fadeIn");
    else if (key == juce::KeyPress ('o'))                                 performAction ("fadeOut");
    else if (key == juce::KeyPress ('r'))                                 toggleRecording();
    else if (key == juce::KeyPress ('l'))                                 loopButton.triggerClick();
    else if (key == juce::KeyPress ('=') || key == juce::KeyPress ('+'))  wave.zoomIn();
    else if (key == juce::KeyPress ('-'))                                 wave.zoomOut();
    else if (key == juce::KeyPress ('f'))                                 wave.zoomToFit();
    else if (key == juce::KeyPress::escapeKey)                            wave.setSelection ({ 0, 0 }), refresh();
    else
        return false;
    return true;
}

//==============================================================================
void EditorView::toggleRecording()
{
    if (! processor.isRecording())
    {
        if (! processor.hasInput())
            return say ("No audio input. In the app, open Options > Audio/MIDI Settings and pick an input.", true);

        processor.stop();
        processor.startRecording();
        recordTime.setText (crate::formatTime (0), juce::dontSendNotification);
        say ("Recording... press Stop (or R) when you're done.");
        refresh();
        return;
    }

    auto take = processor.stopRecording();
    refresh();

    if (take.getNumSamples() < crate::samples (0.05))
        return say ("That recording was too short to keep.", true);

    if (take.getMagnitude (0, take.getNumSamples()) < 1.0e-4f)
        say ("The recording is silent. Check the input in Options > Audio/MIDI Settings, and that the app has microphone access in System Settings > Privacy & Security.", true);
    else
        say ("Recorded " + crate::formatTime (take.getNumSamples()) + ". Trim it, shape it, then Save to library.");

    resetViewOnNextChange = true;
    document.load (crate::share (std::move (take)),
                   "Recording " + juce::Time::getCurrentTime().formatted ("%H.%M.%S"), "Record");
}

juce::Range<int> EditorView::playRange() const
{
    const int n = document.getAudio()->getNumSamples();
    auto sel = wave.getSelection();
    if (sel.isEmpty())
        sel = { sel.getStart() >= n - 1 ? 0 : sel.getStart(), n };
    return sel;
}

void EditorView::togglePlay()
{
    if (processor.isPlaying())
    {
        processor.stop();
        playingPreview = false;
        return;
    }
    if (! document.hasAudio())
        return;

    if (effects.isPreviewOn() && effects.hasEffects())
        return requestPreview (true);

    playingPreview = false;
    processor.play (document.getAudio(), playRange(), loopButton.getToggleState());
    playButton.setButtonText ("Stop");
}

void EditorView::requestPreview (bool startPlaying)
{
    previewDueAt = 0;
    if (! document.hasAudio())
        return;

    const auto source = document.getAudio();
    const auto range = playRange();
    const auto settings = effects.getSettings();
    const int generation = ++previewGeneration;
    previewRange = range;

    if (startPlaying)
        say ("Getting the effects ready...");

    juce::Component::SafePointer<EditorView> safe (this);
    renderPool.addJob ([safe, source, range, settings, generation, startPlaying, this]
    {
        // a newer request will replace this one: don't bother
        if (generation != previewGeneration.load())
            return;
        auto part = Edits::crop (*source, range).audio;
        auto processed = crate::share (crate::SoundFx::apply (part, settings));
        juce::MessageManager::callAsync ([safe, processed, generation, startPlaying]
        {
            if (safe != nullptr)
                safe->previewReady (processed, generation, startPlaying);
        });
    });
}

void EditorView::previewReady (crate::SharedAudio processed, int generation, bool startPlaying)
{
    if (generation != previewGeneration.load() || processed == nullptr)
        return;

    if (startPlaying)
    {
        processor.play (processed, { 0, processed->getNumSamples() }, loopButton.getToggleState());
        playingPreview = true;
        playButton.setButtonText ("Stop");
        say ("Playing with effects. Change the controls while it plays, then Apply when it sounds right.");
    }
    else if (playingPreview && processor.isPlaying())
    {
        processor.swapAudio (processed);
    }
}

void EditorView::applyEffects()
{
    if (! effects.hasEffects())
        return say ("Switch on an effect first, or pick a preset.", true);

    const auto settings = effects.getSettings();
    const bool whole = ! wave.hasSelection();
    edit ("Effects", [settings, whole] (const Buffer& b, juce::Range<int> r)
    {
        if (whole)
            return Edits::Result { crate::SoundFx::apply (b, settings), {} };
        auto processed = crate::SoundFx::apply (Edits::crop (b, r).audio, settings);
        return Edits::paste (b, r, processed);
    }, false, false);

    playingPreview = false;
    effects.reset();
    say ("Effects applied" + juce::String (whole ? "" : " to the selection") + ". Undo with Cmd+Z.");
}

void EditorView::openFile (const juce::File& file)
{
    juce::String error;
    auto audio = crate::AudioFiles::load (file, processor.getFormats(), error);
    if (audio == nullptr)
        return say (error, true);

    resetViewOnNextChange = true;
    const bool inLibrary = file.isAChildOf (processor.getLibrary().getFolder());
    document.load (std::move (audio), file.getFileNameWithoutExtension(), "Open " + file.getFileName(), file, inLibrary);
    say ("Opened " + file.getFileName() + ".");
}

void EditorView::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a sound", juce::File(),
                                                   crate::AudioFiles::wildcard (processor.getFormats()));
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c)
                          {
                              if (c.getResult().existsAsFile())
                                  openFile (c.getResult());
                          });
}

void EditorView::saveToLibrary()
{
    if (! document.hasAudio())
        return;

    auto* window = new juce::AlertWindow ("Save to library",
                                          "Saves a WAV in your Crate sound library, where every Crate tool can find it.",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor ("name", document.getName().isNotEmpty() ? document.getName() : "Untitled", "Name");
    window->addTextEditor ("tags", {}, "Tags (optional, comma separated)");
    window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    window->enterModalState (true, juce::ModalCallbackFunction::create ([this, window] (int result)
    {
        if (result != 1 || ! document.hasAudio())
            return;

        SoundLibrary::Info info;
        info.name = window->getTextEditorContents ("name").trim();
        info.tags.addTokens (window->getTextEditorContents ("tags"), ",", {});
        info.tags.trim();
        info.tags.removeEmptyStrings();
        info.tool = "Sample Editor";

        const auto file = processor.getLibrary().save (*document.getAudio(), info);
        if (file == juce::File())
            return say ("Couldn't save to " + processor.getLibrary().getFolder().getFullPathName() + ".", true);

        document.setName (file.getFileNameWithoutExtension());
        document.markSaved();
        say ("Saved to the library as " + file.getFileName() + ".");
    }), true);
}

void EditorView::exportFile()
{
    if (! document.hasAudio())
        return;

    const auto name = document.getName().isNotEmpty() ? document.getName() : juce::String ("Sound");
    auto start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                     .getChildFile (juce::File::createLegalFileName (name) + ".wav");
    chooser = std::make_unique<juce::FileChooser> ("Export sound", start, "*.wav;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& c)
                          {
                              auto file = c.getResult();
                              if (file == juce::File() || ! document.hasAudio())
                                  return;
                              if (! file.hasFileExtension ("wav;aif;aiff"))
                                  file = file.withFileExtension ("wav");

                              SoundLibrary::Info info;
                              info.name = file.getFileNameWithoutExtension();
                              info.tool = "Sample Editor";
                              if (crate::AudioFiles::write (file, *document.getAudio(), SoundLibrary::toMetadata (info)))
                                  say ("Exported " + file.getFullPathName() + ".");
                              else
                                  say ("Couldn't write " + file.getFullPathName() + ".", true);
                          });
}

//==============================================================================
bool EditorView::isInterestedInFileDrag (const juce::StringArray&)
{
    return true;
}

void EditorView::filesDropped (const juce::StringArray& paths, int, int)
{
    const auto files = crate::AudioFiles::expandDropped (paths, processor.getFormats());
    if (files.isEmpty())
        return say ("That isn't a sound file I can open. Try WAV, AIFF, MP3 or FLAC.", true);
    openFile (files.getFirst());
    if (files.size() > 1)
        say ("Opened " + files.getFirst().getFileName() + " (one sound at a time; the others were skipped).");
}

bool EditorView::isInterestedInDragSource (const SourceDetails& details)
{
    return details.description.toString().startsWith (SoundLibrary::dragPrefix);
}

void EditorView::itemDropped (const SourceDetails& details)
{
    openFile (juce::File (details.description.toString().fromFirstOccurrenceOf (SoundLibrary::dragPrefix, false, false)));
}
