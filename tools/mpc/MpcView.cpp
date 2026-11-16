#include "MpcView.h"

#include "SampleTools.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace Theme = crate::Theme;

namespace fs = std::filesystem;

MpcView::MpcView (MpcProcessor& p)
    : AudioProcessorEditor (p), processor (p), project (p.getProject()), formats (p.getFormats()), libraryList (p.getLibrary())
{
    setLookAndFeel (&lookAndFeel);

   #if JucePlugin_Build_Standalone
    // Turn on every connected MIDI keyboard in the app so it just works. (In Logic, the
    // track's MIDI comes in on its own.)
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            for (const auto& input : juce::MidiInput::getAvailableDevices())
                holder->deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
   #endif

    memory.setJustificationType (juce::Justification::centredRight);
    status.setColour (juce::Label::textColourId, Theme::textDim);

    for (auto* c : std::initializer_list<juce::Component*> { &kitLabel, &kitName, &memory, &ram, &openButton,
                                                             &bpmLabel, &bpm, &keepInTime, &exportButton, &kitList,
                                                             &newKit, &removeKit, &libraryList,
                                                             &grid, &editorScroll, &status })
        addAndMakeVisible (c);

    kitName.setInputRestrictions (16);
    kitName.setJustification (juce::Justification::centredLeft);

    // One tempo per kit. With Keep in time on, every pad is stretched to it (except pads set to
    // play normal), so loops recorded at different speeds play together.
    bpm.setSliderStyle (juce::Slider::IncDecButtons);
    bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 30);
    bpm.setRange (40.0, 240.0, 0.1);
    bpm.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    bpm.onValueChange = [this] { project.currentBank().bpm = bpm.getValue(); project.changed(); };
    keepInTime.onClick = [this]
    {
        project.currentBank().keepInTime = keepInTime.getToggleState();
        processor.stopAll();
        project.changed();
    };

    libraryList.onOpen = [this] (const SoundLibrary::Entry& entry)
    {
        loadFilesIntoPads (grid.getSelectedPad(), { entry.file });
    };
    kitName.onTextChange = [this]
    {
        project.currentBank().name = kitName.getText();
        project.changed();
    };

    ram.addItem ("MPC has 2 MB", 2);
    ram.addItem ("MPC has 8 MB", 8);
    ram.addItem ("MPC has 16 MB", 16);
    ram.addItem ("MPC has 32 MB", 32);
    ram.setSelectedId (project.ramMegabytes, juce::dontSendNotification);
    ram.onChange = [this] { project.ramMegabytes = ram.getSelectedId(); project.changed(); };

    openButton.onClick = [this] { openKit(); };
    exportButton.setColour (juce::TextButton::buttonColourId, Theme::accent);
    exportButton.setColour (juce::TextButton::textColourOffId, Theme::background);
    exportButton.onClick = [this] { exportToCard(); };

    kitList.setRowHeight (44);
    kitList.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    newKit.onClick = [this] { project.addBank(); showPadBank (0); selectPad (0); };
    removeKit.onClick = [this]
    {
        const auto& bank = project.currentBank();
        if (bank.usedPads() == 0)
        {
            project.removeBank (project.getCurrentIndex());
            return;
        }

        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon,
                                                                    "Remove " + bank.name + "?",
                                                                    "Its " + juce::String (bank.usedPads())
                                                                        + " pads will be cleared. Kits already exported to the card stay there.",
                                                                    "Remove", "Cancel", this);
        juce::AlertWindow::showAsync (options, [this] (int result)
        {
            if (result == 1)
                project.removeBank (project.getCurrentIndex());
        });
    };

    for (int i = 0; i < 4; ++i)
    {
        auto& tab = bankTabs[(size_t) i];
        tab.setButtonText (juce::String::charToString ((juce::juce_wchar) ('A' + i)));
        tab.setClickingTogglesState (true);
        tab.setRadioGroupId (1);
        tab.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        tab.setColour (juce::TextButton::textColourOnId, Theme::background);
        tab.onClick = [this, i] { showPadBank (i); selectPad (i * kPadsPerBank); };
        addAndMakeVisible (tab);
    }
    bankTabs[0].setToggleState (true, juce::dontSendNotification);

    grid.onPadPressed = [this] (int pad)
    {
        // With 16 Levels on the pads all play the one sound, which stays picked.
        if (processor.levelsType.load() == levels::off)
            selectPad (pad);
        processor.playPad (pad);
    };
    grid.levelsText = [this] (int slot) { return levelText (slot); };

    // Pad tools.
    for (int t = 0; t < levels::numTypes; ++t)
        levelsChoice.addItem (levels::typeName (t), t + 1);
    levelsChoice.setTooltip ("16 Levels: the picked pad's sound on all 16 pads, a step apart. Tune plays it as a scale "
                             "(a semitone a pad), Filter opens up pad by pad, Velocity goes soft to loud, Decay short to long.");
    levelsChoice.onChange = [this] { setSixteenLevels (levelsChoice.getSelectedId() - 1); };
    noteRepeatButton.setClickingTogglesState (true);
    noteRepeatButton.setTooltip ("Hold a pad and it plays again on every step (1/16 and so on), in time with the sequence "
                                 "and its metronome, while it plays, records, or is stopped.");
    noteRepeatButton.onClick = [this] { setNoteRepeat (noteRepeatButton.getToggleState(), repeatRate.getSelectedId() - 2); };
    repeatRate.addItem ("Timing", 1);
    for (auto g : { seq::Grid::eighth, seq::Grid::eighthTriplet, seq::Grid::sixteenth, seq::Grid::sixteenthTriplet,
                    seq::Grid::thirtySecond, seq::Grid::thirtySecondTriplet })
        repeatRate.addItem (seq::gridName (g), (int) g + 2);
    repeatRate.setTooltip ("How fast held pads repeat (Timing: the sequencer's timing correct)");
    repeatRate.onChange = [this] { setNoteRepeat (noteRepeatButton.getToggleState(), repeatRate.getSelectedId() - 2); };
    fullLevelButton.setClickingTogglesState (true);
    fullLevelButton.setTooltip ("Full level: every hit plays at full velocity, however hard you hit the pad");
    fullLevelButton.onClick = [this] { processor.fullLevel = fullLevelButton.getToggleState(); };
    for (auto* b : { &noteRepeatButton, &fullLevelButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        b->setColour (juce::TextButton::textColourOnId, Theme::background);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &levelsLabel, &levelsChoice, &noteRepeatButton, &repeatLabel, &repeatRate,
                                                             &fullLevelButton, &browser })
        addAndMakeVisible (c);
    levelsLabel.setJustificationType (juce::Justification::centredRight);
    repeatLabel.setJustificationType (juce::Justification::centred);
    browser.targetPad = [this] { return grid.getSelectedPad(); };
    browser.onStatus = [this] (const juce::String& text, bool warning) { setStatus (text, warning); };
    browser.onPlaced = [this] (int pad) { showPadBank (pad / kPadsPerBank); selectPad (pad); grid.repaint(); };
    editor.onPlay = [this]
    {
        processor.playPad (grid.getSelectedPad());
        juce::Timer::callAfterDelay (400, [safe = juce::Component::SafePointer<MpcView> (this), pad = grid.getSelectedPad()]
                                     { if (safe != nullptr) safe->processor.releasePad (pad); });
    };
    updatePadTools();
    grid.onPadReleased = [this] (int pad) { processor.releasePad (pad); };
    grid.onFilesDropped = [this] (int pad, const juce::StringArray& paths)
    {
        loadFilesIntoPads (pad, SampleTools::expandDroppedFiles (paths, formats));
    };
    grid.onClearPad = [this] (int pad)
    {
        project.currentBank().pads[(size_t) pad] = {};
        project.changed();
    };

    editorScroll.setViewedComponent (&editor, false);
    editorScroll.setScrollBarsShown (true, false);
    editorScroll.setScrollBarThickness (8);
    editor.onClear = [this] { grid.onClearPad (grid.getSelectedPad()); };
    editor.onLoadFile = [this] { chooseFileForPad(); };

    editor.onChop = [this] (int slices) { chopSelectedPad (slices); };
    editor.onEditSound = [this] { openSoundEditor (true); };
    // A double-click on a pad is just two hits (finger drumming): the pad's Trim and effects...
    // button, or its right-click menu, opens the sound editor.
    grid.onEditPad = [this] (int pad) { selectPad (pad); openSoundEditor (true); };

    addChildComponent (soundEditor);
    soundEditor.onClose = [this] { openSoundEditor (false); };
    soundEditor.onStatus = [this] (const juce::String& text, bool warning) { setStatus (text, warning); };

    project.addChangeListener (this);

    addAndMakeVisible (transport);
    addAndMakeVisible (live);
    addChildComponent (sequencer);
    transport.onStatus = sequencer.onStatus = live.onStatus = [this] (const juce::String& text, bool warning) { setStatus (text, warning); };
    for (auto* tab : { &padsTab, &sequenceTab })
    {
        tab->setClickingTogglesState (true);
        tab->setRadioGroupId (2);
        tab->setWantsKeyboardFocus (false);
        tab->setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        tab->setColour (juce::TextButton::textColourOnId, Theme::background);
        addAndMakeVisible (tab);
    }
    padsTab.setToggleState (true, juce::dontSendNotification);
    padsTab.setTooltip ("The pads");
    sequenceTab.setTooltip ("The sequence as steps for the 16 pads on screen: click to add or remove hits");
    padsTab.onClick = [this] { showSequencer (false); };
    sequenceTab.onClick = [this] { showSequencer (true); };
    for (auto& tab : bankTabs)
        tab.setWantsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
    keyPad.fill (-1);

    setStatus ("Drop samples, a folder, or sounds from the library on the pads. Click a pad or play your MIDI keyboard to hear it.");
    refresh();
    startTimerHz (30);

    setResizeLimits (1100, 780, 2400, 1600);
    setResizable (true, true);
    setSize (1280, 860);
}

MpcView::~MpcView()
{
    project.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void MpcView::timerCallback()
{
    // The pad bank on screen can be changed from elsewhere (the Rig's Sampler screen).
    if (const int bank = juce::jlimit (0, 3, processor.padBankOnScreen.load()); bank != grid.getPadBank())
        showPadBank (bank);
    if (noteRepeatButton.getToggleState() != processor.getSequencer().noteRepeat.load()
        || fullLevelButton.getToggleState() != processor.fullLevel.load()
        || levelsChoice.getSelectedId() != processor.levelsType.load() + 1)
    {
        updatePadTools();
        grid.repaint();
    }

    // Light the pads that played, from MIDI or clicks.
    const auto played = processor.takePlayedPads();
    for (int pad = 0; pad < mpc::kPadCount; ++pad)
        if ((played >> pad) & 1u)
            if (pad / kPadsPerBank == grid.getPadBank())
                grid.flash (pad);
}

void MpcView::showPadBank (int padBank)
{
    bankTabs[(size_t) padBank].setToggleState (true, juce::dontSendNotification);
    grid.setPadBank (padBank);
    sequencer.setPadBank (padBank);
    processor.padBankOnScreen = padBank;
    if (onPadBankChanged)
        onPadBankChanged (padBank);
}

void MpcView::setSixteenLevels (int type)
{
    type = juce::jlimit (0, levels::numTypes - 1, type);
    const int pad = grid.getSelectedPad();
    if (type != levels::off && project.currentBank().pads[(size_t) pad].isEmpty())
    {
        processor.levelsType = levels::off;
        updatePadTools();
        return setStatus ("Pick a pad with a sound on it first, then 16 Levels spreads it over the 16 pads.", true);
    }
    processor.levelsPad = pad;
    processor.levelsType = type;
    updatePadTools();
    grid.repaint();
    if (type == levels::off)
        setStatus ("16 Levels is off: each pad plays its own sound again.");
    else
        setStatus ("16 Levels (" + levels::typeName (type) + "): the 16 pads all play " + project.currentBank().pads[(size_t) pad].name
                   + ", pad 1 lowest to pad 16 highest. Hits you record keep their level. Pick Off to go back.");
}

void MpcView::setNoteRepeat (bool on, int gridChoice)
{
    auto& engine = processor.getSequencer();
    engine.repeatGrid = gridChoice;
    engine.noteRepeat = on;
    updatePadTools();
    if (on)
        setStatus ("Note repeat: hold a pad and it repeats every " + seq::gridName (engine.getRepeatGrid())
                   + ". Press Rec with the metronome on to record the repeats.");
}

void MpcView::updatePadTools()
{
    const auto& engine = processor.getSequencer();
    const int type = processor.levelsType.load();
    levelsChoice.setSelectedId (type + 1, juce::dontSendNotification);
    noteRepeatButton.setToggleState (engine.noteRepeat.load(), juce::dontSendNotification);
    const int rate = engine.repeatGrid.load();
    repeatRate.setSelectedId (rate > 0 ? rate + 2 : 1, juce::dontSendNotification);
    fullLevelButton.setToggleState (processor.fullLevel.load(), juce::dontSendNotification);
    levelsLabel.setText (type == levels::off ? juce::String ("16 Levels")
                                             : "16 Levels of " + padLabel (juce::jlimit (0, mpc::kPadCount - 1, processor.levelsPad.load())),
                         juce::dontSendNotification);
}

juce::String MpcView::levelText (int slot) const
{
    return levels::stepText (processor.levelsType.load(), slot);
}

void MpcView::setHosted (juce::LookAndFeel* look, std::function<void (juce::Graphics&)> paintBackground)
{
    hosted = true;
    hostPaint = std::move (paintBackground);
    setLookAndFeel (look);
    // The host's screen has these.
    for (auto* c : std::initializer_list<juce::Component*> { &grid, &browser, &levelsLabel, &levelsChoice, &noteRepeatButton,
                                                             &repeatLabel, &repeatRate, &fullLevelButton })
        c->setVisible (false);
    for (auto& tab : bankTabs)
        tab.setVisible (false);
    padsTab.setButtonText ("Pad");
    padsTab.setTooltip ("The picked pad's settings: name, level, tune, how it plays, and its sound and effects");
    sequenceTab.setButtonText ("Steps");
    for (auto* c : std::initializer_list<juce::Component*> { &kitList, &libraryList })
        c->setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1b1814));
    status.setColour (juce::Label::textColourId, juce::Colour (0xffc9bfa8));
    memory.setTooltip ("How much of the MPC's memory the kit uses");
    refresh();
    showSequencer (sequencer.isVisible());
}

void MpcView::showSequencer (bool show)
{
    sequenceTab.setToggleState (show, juce::dontSendNotification);
    padsTab.setToggleState (! show, juce::dontSendNotification);
    sequencer.setVisible (show);
    grid.setVisible (! show && ! hosted);
    browser.setVisible (! show && ! hosted);
    if (hosted)
    {
        editorScroll.setVisible (! show);
        if (soundEditor.isVisible())
            openSoundEditor (false);
    }
    resized();
    if (show)
        setStatus ("Click steps to add hits, or press Rec and play pads (keys Z-V, A-F, Q-R, 1-4 play the 16 pads). Space plays and stops.");
}

bool MpcView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        auto& engine = processor.getSequencer();
        engine.isPlaying() ? processor.pressStop() : processor.pressPlay (false);
        return true;
    }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T')
    {
        live.tap();
        return true;
    }
    // Pad keys are handled as they go down and up, in keyStateChanged.
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    return c != 0 && juce::String (kPadKeys).containsChar (c) && ! key.getModifiers().isCommandDown();
}

bool MpcView::keyStateChanged (bool)
{
    if (juce::ModifierKeys::currentModifiers.isCommandDown())
        return false;

    bool used = false;
    for (int i = 0; i < 16; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (kPadKeys[i]);
        if (down == keyDown[(size_t) i])
            continue;
        keyDown[(size_t) i] = down;
        used = true;
        if (down)
        {
            keyPad[(size_t) i] = grid.getPadBank() * kPadsPerBank + i;
            processor.playPad (keyPad[(size_t) i], 100 / 127.0f);
        }
        else if (keyPad[(size_t) i] >= 0)
        {
            processor.releasePad (keyPad[(size_t) i]);
            keyPad[(size_t) i] = -1;
        }
    }
    return used;
}

void MpcView::paint (juce::Graphics& g)
{
    if (hostPaint != nullptr)
        return hostPaint (g);
    g.fillAll (Theme::background);
}

void MpcView::layoutHosted (juce::Rectangle<int> r)
{
    // The kit: name and tempo on the left, the card on the right.
    auto header = r.removeFromTop (32);
    exportButton.setBounds (header.removeFromRight (128));
    header.removeFromRight (6);
    openButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (6);
    ram.setBounds (header.removeFromRight (132));
    kitLabel.setBounds (header.removeFromLeft (34));
    kitName.setBounds (header.removeFromLeft (130));
    header.removeFromLeft (8);
    bpmLabel.setBounds (header.removeFromLeft (36));
    bpm.setBounds (header.removeFromLeft (96));
    header.removeFromLeft (6);
    keepInTime.setBounds (header.removeFromLeft (112));
    memory.setBounds (header.reduced (4, 0));

    r.removeFromTop (8);
    transport.setBounds (r.removeFromTop (78));
    r.removeFromTop (6);
    live.setBounds (r.removeFromTop (64));
    r.removeFromTop (8);
    status.setBounds (r.removeFromBottom (22));
    r.removeFromBottom (4);

    auto left = r.removeFromLeft (juce::jlimit (170, 220, r.getWidth() / 5));
    auto kits = left.removeFromTop (juce::jmax (120, left.getHeight() / 2));
    auto leftButtons = kits.removeFromBottom (30);
    newKit.setBounds (leftButtons.removeFromLeft (leftButtons.getWidth() / 2 - 3));
    leftButtons.removeFromLeft (6);
    removeKit.setBounds (leftButtons);
    kits.removeFromBottom (6);
    kitList.setBounds (kits);
    left.removeFromTop (10);
    libraryList.setBounds (left);
    r.removeFromLeft (12);

    // The middle: the picked pad's settings, or the steps.
    auto top = r.removeFromTop (30);
    sequenceTab.setBounds (top.removeFromRight (80));
    top.removeFromRight (6);
    padsTab.setBounds (top.removeFromRight (80));
    r.removeFromTop (6);
    sequencer.setBounds (r);
    soundEditor.setBounds (r);
    editorScroll.setBounds (r);
    const int needed = 610;
    const bool scrolls = r.getHeight() < needed;
    editor.setBounds (0, 0, r.getWidth() - (scrolls ? editorScroll.getScrollBarThickness() : 0), juce::jmax (r.getHeight(), needed));
}

void MpcView::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    if (hosted)
        return layoutHosted (r);

    auto header = r.removeFromTop (34);
    exportButton.setBounds (header.removeFromRight (150));
    header.removeFromRight (8);
    openButton.setBounds (header.removeFromRight (110));
    header.removeFromRight (16);
    ram.setBounds (header.removeFromRight (140));
    header.removeFromRight (8);
    memory.setBounds (header.removeFromRight (190));
    kitLabel.setBounds (header.removeFromLeft (70));
    kitName.setBounds (header.removeFromLeft (200));
    header.removeFromLeft (20);
    bpmLabel.setBounds (header.removeFromLeft (36));
    bpm.setBounds (header.removeFromLeft (110));
    header.removeFromLeft (10);
    keepInTime.setBounds (header.removeFromLeft (150));

    r.removeFromTop (10);
    transport.setBounds (r.removeFromTop (42));
    r.removeFromTop (6);
    live.setBounds (r.removeFromTop (64));
    r.removeFromTop (10);
    status.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (8);

    auto left = r.removeFromLeft (220);
    auto kits = left.removeFromTop (juce::jmax (150, left.getHeight() * 2 / 5));
    auto leftButtons = kits.removeFromBottom (30);
    newKit.setBounds (leftButtons.removeFromLeft (leftButtons.getWidth() / 2 - 4));
    leftButtons.removeFromLeft (8);
    removeKit.setBounds (leftButtons);
    kits.removeFromBottom (8);
    kitList.setBounds (kits);
    left.removeFromTop (14);
    libraryList.setBounds (left);

    r.removeFromLeft (16);
    const auto editorArea = r;
    {
        const auto area = r.removeFromRight (340);
        editorScroll.setBounds (area);
        const int needed = 610;   // both pages fit in this
        const bool scrolls = area.getHeight() < needed;
        editor.setBounds (0, 0, area.getWidth() - (scrolls ? editorScroll.getScrollBarThickness() : 0), juce::jmax (area.getHeight(), needed));
    }
    r.removeFromRight (16);

    auto top = r.removeFromTop (30);
    for (auto& tab : bankTabs)
    {
        tab.setBounds (top.removeFromLeft (40));
        top.removeFromLeft (4);
    }
    sequenceTab.setBounds (top.removeFromRight (70));
    top.removeFromRight (4);
    padsTab.setBounds (top.removeFromRight (70));

    r.removeFromTop (8);
    sequencer.setBounds (r);
    {
        auto tools = r.removeFromTop (28);
        // Narrow windows squeeze the row rather than lose any of it.
        const float scale = juce::jmin (1.0f, (float) tools.getWidth() / 620.0f);
        auto take = [&tools, scale] (int width) { return tools.removeFromLeft (juce::roundToInt (width * scale)); };
        levelsLabel.setBounds (take (120));
        take (6);
        levelsChoice.setBounds (take (130));
        take (16);
        noteRepeatButton.setBounds (take (104));
        repeatLabel.setBounds (take (24));
        repeatRate.setBounds (take (110));
        fullLevelButton.setBounds (tools.removeFromRight (juce::roundToInt (92 * scale)));
    }
    r.removeFromTop (8);
    if (browser.isVisible())
    {
        browser.setBounds (r.removeFromBottom (juce::jlimit (130, 260, r.getHeight() * 32 / 100)));
        r.removeFromBottom (8);
    }
    grid.setBounds (r);
    soundEditor.setBounds (editorArea);
}

void MpcView::openSoundEditor (bool open)
{
    if (open)
    {
        const int pad = grid.getSelectedPad();
        if (project.currentBank().pads[(size_t) pad].isEmpty())
            return setStatus ("Pad " + padLabel (pad) + " is empty. Put a sound on it first.", true);
        soundEditor.open (pad);
        soundEditor.setVisible (true);
        soundEditor.toFront (true);
        setStatus ("Drag across the waveform to pick a part, then trim, fade, or put effects on it. Space plays, Esc goes back.");
    }
    else
    {
        soundEditor.close();
        soundEditor.setVisible (false);
        grid.repaint();
    }
}

void MpcView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void MpcView::refresh()
{
    auto& bank = project.currentBank();
    kitName.setText (bank.name, juce::dontSendNotification);
    bpm.setValue (bank.bpm, juce::dontSendNotification);
    keepInTime.setToggleState (bank.keepInTime, juce::dontSendNotification);

    const auto used = bank.memoryBytes();
    const auto available = (size_t) project.ramMegabytes * 1024 * 1024;
    memory.setText ((hosted ? juce::String() : juce::String ("Kit uses ")) + SampleTools::formatMegabytes (used) + " of " + juce::String (project.ramMegabytes) + " MB",
                    juce::dontSendNotification);
    memory.setColour (juce::Label::textColourId, used > available ? Theme::record : Theme::text);

    kitList.updateContent();
    kitList.selectRow (project.getCurrentIndex(), true, true);
    kitList.repaint();
    removeKit.setEnabled (project.getBanks().size() > 1 || bank.usedPads() > 0);

    editor.refresh();
    if (soundEditor.isVisible())
        soundEditor.sync();
    grid.repaint();
}

int MpcView::getNumRows()
{
    return (int) project.getBanks().size();
}

void MpcView::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= getNumRows())
        return;

    const auto& bank = project.getBanks()[(size_t) row];

    if (selected)
    {
        g.setColour (juce::Colour (0xff55575e));
        g.fillRect (0, 0, width, height);
    }

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (bank.name, 10, 4, width - 20, 20, juce::Justification::centredLeft);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (juce::String (bank.usedPads()) + " pads, " + SampleTools::formatMegabytes (bank.memoryBytes()),
                10, 22, width - 20, 18, juce::Justification::centredLeft);
}

void MpcView::selectedRowsChanged (int row)
{
    if (row >= 0 && row != project.getCurrentIndex())
    {
        processor.stopAll();
        project.setCurrent (row);
    }
}

void MpcView::selectPad (int pad)
{
    grid.setSelectedPad (pad);
    editor.setPad (pad);
}

void MpcView::loadFilesIntoPads (int firstPad, const juce::Array<juce::File>& files)
{
    if (files.isEmpty())
    {
        setStatus ("Those aren't audio files the app can read (WAV, AIFF, MP3, FLAC or .SND).", true);
        return;
    }

    juce::MouseCursor::showWaitCursor();

    auto& bank = project.currentBank();
    juce::StringArray problems;
    int pad = firstPad;
    int loaded = 0;

    for (const auto& file : files)
    {
        if (pad >= mpc::kPadCount)
        {
            problems.add (juce::String (files.size() - loaded) + " files didn't fit; the kit has 64 pads");
            break;
        }

        juce::String error;
        auto audio = SampleTools::load (file, formats, error);

        if (audio == nullptr)
        {
            problems.add (error);
            continue;
        }

        auto& p = bank.pads[(size_t) pad];
        p = {};
        p.audio = audio;
        p.name = file.getFileNameWithoutExtension().substring (0, 16);
        ++pad;
        ++loaded;
    }

    juce::MouseCursor::hideWaitCursor();

    showPadBank (firstPad / kPadsPerBank);
    selectPad (firstPad);
    project.changed();

    if (problems.isEmpty())
        setStatus ("Loaded " + juce::String (loaded) + (loaded == 1 ? " sound" : " sounds") + " from pad " + padLabel (firstPad) + ".");
    else
        setStatus ("Loaded " + juce::String (loaded) + ". " + problems.joinIntoString (". "), true);
}

void MpcView::chooseFileForPad()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sound onto pad " + padLabel (grid.getSelectedPad()),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   SampleTools::audioFileWildcard (formats));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (! fc.getResults().isEmpty())
                                  loadFilesIntoPads (grid.getSelectedPad(), fc.getResults());
                          });
}

void MpcView::openKit()
{
    chooser = std::make_unique<juce::FileChooser> ("Open an MPC program (.PGM)", juce::File ("/Volumes"), "*.pgm;*.PGM");

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult() != juce::File())
                                  openKitFile (fc.getResult());
                          });
}

void MpcView::openKitFile (const juce::File& file)
{
    juce::StringArray missing;
    juce::String error;
    auto bank = SampleTools::importProgram (file, missing, error);

    if (! bank)
    {
        setStatus (error, true);
        return;
    }

    // Opening into the untouched starting kit replaces it rather than adding a second one.
    const bool replaceFirst = project.getBanks().size() == 1 && project.currentBank().usedPads() == 0;
    const int index = replaceFirst ? 0 : project.addBank (bank->name);
    project.getBanks()[(size_t) index] = std::move (*bank);
    project.setCurrent (index);
    showPadBank (0);
    selectPad (0);
    project.changed();

    if (missing.isEmpty())
        setStatus ("Opened " + file.getFileName() + ".");
    else
        setStatus ("Opened " + file.getFileName() + ", but these sounds weren't next to it: "
                       + missing.joinIntoString (", "), true);
}

void MpcView::exportToCard()
{
    int used = 0;
    for (const auto& bank : project.getBanks())
        used += bank.usedPads();

    if (used == 0)
    {
        setStatus ("Nothing to export yet. Drop some samples on the pads first.", true);
        return;
    }

    auto start = juce::File ("/Volumes");
    chooser = std::make_unique<juce::FileChooser> ("Choose your MPC's SD card (or any folder)",
                                                   start.isDirectory() ? start : juce::File::getSpecialLocation (juce::File::userDesktopDirectory));

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir == juce::File() || ! dir.isDirectory())
                                  return;

                              // Warn before replacing kit folders that are already on the card.
                              std::vector<std::string> taken;
                              juce::StringArray existing;
                              for (const auto& bank : project.getBanks())
                              {
                                  if (bank.usedPads() == 0)
                                      continue;
                                  const auto folder = mpc::makeUniqueName (bank.name.toStdString(), taken);
                                  const auto onCard = dir.getChildFile (folder);
                                  if (onCard.isDirectory() && ! onCard.findChildFiles (juce::File::findFiles, false, "*.SND;*.snd;*.PGM;*.pgm").isEmpty())
                                      existing.add (folder);
                              }

                              if (existing.isEmpty())
                              {
                                  writeBanks (dir);
                                  return;
                              }

                              auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
                                  juce::MessageBoxIconType::WarningIcon, "Replace kits on the card?",
                                  "These folders already have sounds in them: " + existing.joinIntoString (", ")
                                      + ".\n\nTheir .SND and .PGM files will be replaced with this export.",
                                  "Replace", "Cancel", this);

                              juce::AlertWindow::showAsync (options, [this, dir] (int result)
                              {
                                  if (result == 1)
                                      writeBanks (dir);
                              });
                          });
}

void MpcView::writeBanks (const juce::File& destination)
{
    juce::MouseCursor::showWaitCursor();

    const fs::path root (destination.getFullPathName().toStdString());
    const auto available = (size_t) project.ramMegabytes * 1024 * 1024;
    std::vector<std::string> taken;
    juce::StringArray written, tooBig, errors;

    for (const auto& bank : project.getBanks())
    {
        if (bank.usedPads() == 0)
            continue;

        auto files = mpc::buildBank (SampleTools::toMpcBank (bank), taken);
        auto result = mpc::writeBankFolder (root, files);

        if (! result.ok)
        {
            errors.add (juce::String (result.error));
            continue;
        }

        written.add (juce::String (files.folderName) + "/" + juce::String (files.folderName) + ".PGM ("
                     + juce::String ((int) files.sounds.size()) + " sounds, " + SampleTools::formatMegabytes (result.memoryBytes) + ")");

        if (result.memoryBytes > available)
            tooBig.add (juce::String (files.folderName));
    }

    // Finder leaves "._" files on cards; the MPC shows them as junk.
    mpc::removeMacMetadataFiles (root);

    juce::MouseCursor::hideWaitCursor();

    juce::String message;
    if (! written.isEmpty())
        message << "Exported to " << destination.getFullPathName() << ":\n\n" << written.joinIntoString ("\n")
                << "\n\nEject the card, put it in the MPC, then LOAD the .PGM inside each folder.";
    if (! tooBig.isEmpty())
        message << "\n\nToo big for " << project.ramMegabytes << " MB of MPC memory: " << tooBig.joinIntoString (", ")
                << ". Try saving pads as mono or trimming them.";
    if (! errors.isEmpty())
        message << "\n\nProblems:\n" << errors.joinIntoString ("\n");

    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (errors.isEmpty() ? juce::MessageBoxIconType::InfoIcon : juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (errors.isEmpty() ? "Export done" : "Export had problems")
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  nullptr);

    setStatus (errors.isEmpty() ? "Exported " + juce::String (written.size()) + " kits to " + destination.getFileName() + "."
                                : errors[0],
               ! errors.isEmpty());
}

void MpcView::placeSounds (const std::vector<std::pair<juce::String, SharedAudio>>& sounds)
{
    auto& bank = project.currentBank();
    const int first = grid.getSelectedPad();
    int pad = first;

    for (const auto& [soundName, audio] : sounds)
    {
        if (pad >= mpc::kPadCount)
            break;
        auto& p = bank.pads[(size_t) pad++];
        p = {};
        p.audio = audio;
        p.name = soundName.substring (0, 16);
    }

    showPadBank (first / kPadsPerBank);
    selectPad (first);
    project.changed();

    const int placed = pad - first;
    setStatus ("Placed " + juce::String (placed) + (placed == 1 ? " sound" : " sounds") + " from pad " + padLabel (first)
               + (placed < (int) sounds.size() ? ". The rest didn't fit; the kit has 64 pads." : "."),
               placed < (int) sounds.size());
}

void MpcView::chopSelectedPad (int slices)
{
    const int pad = grid.getSelectedPad();
    const auto& source = project.currentBank().pads[(size_t) pad];
    if (source.isEmpty())
        return;

    const auto pieces = SampleTools::chop (source.audio, slices);
    if (pieces.size() < 2)
    {
        setStatus ("Couldn't find separate hits in " + source.name + ". Try even slices instead.", true);
        return;
    }

    // The name gets a number per slice, kept short for the MPC's 16-character names.
    const auto base = source.name.substring (0, 13);
    std::vector<std::pair<juce::String, SharedAudio>> named;
    for (size_t i = 0; i < pieces.size(); ++i)
        named.emplace_back (base + " " + juce::String ((int) i + 1), pieces[i]);
    placeSounds (named);
}

void MpcView::setStatus (const juce::String& text, bool warning)
{
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, warning ? Theme::record : Theme::textDim);
}
