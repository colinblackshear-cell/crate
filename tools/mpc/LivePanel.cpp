#include "LivePanel.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

namespace
{
    constexpr int kNewSong = 1000, kRenameSong = 1001, kDeleteSong = 1002;

    juce::String sequenceName (const Project& project, int index)
    {
        const auto& seqs = const_cast<Project&> (project).getSequences();
        if (! juce::isPositiveAndBelow (index, (int) seqs.size()))
            return "?";
        return juce::String (index + 1).paddedLeft ('0', 2) + " " + seqs[(size_t) index].name;
    }
}

//==============================================================================
SongEditor::SongEditor (MpcProcessor& p, std::function<void()> c) : processor (p), project (p.getProject()), changed (std::move (c))
{
    title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    empty.setText ("No steps yet. Add one for each part (intro, verse, chorus...), and set how many times it plays.",
                   juce::dontSendNotification);
    empty.setColour (juce::Label::textColourId, Theme::textDim);
    empty.setJustificationType (juce::Justification::centred);
    total.setColour (juce::Label::textColourId, Theme::textDim);

    add.onClick = [this]
    {
        edit ([this] (seq::Song& s)
        {
            // A new step carries on from the last one: the next sequence along, or the current one.
            const int n = (int) project.getSequences().size();
            const int seqIndex = s.steps.empty() ? project.getCurrentSequenceIndex() : juce::jmin (n - 1, s.steps.back().sequence + 1);
            s.steps.push_back ({ seqIndex, 1 });
        });
        viewport.setViewPosition (0, rowHolder.getHeight());
    };
    loop.setTooltip ("After the last step, start again from the first");
    loop.onClick = [this] { edit ([this] (seq::Song& s) { s.loop = loop.getToggleState(); }); };

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    for (auto* c : std::initializer_list<juce::Component*> { &title, &viewport, &empty, &add, &loop, &total })
        addAndMakeVisible (c);
    rebuild();
}

void SongEditor::edit (std::function<void (seq::Song&)> f)
{
    f (project.currentSong());
    project.sequenceChanged();
    if (changed)
        changed();
    rebuild();
}

void SongEditor::rebuild()
{
    const auto& song = project.currentSong();
    const auto& seqs = project.getSequences();
    title.setText (song.name, juce::dontSendNotification);
    loop.setToggleState (song.loop, juce::dontSendNotification);
    total.setText (juce::String ((int) song.steps.size()) + (song.steps.size() == 1 ? " step, " : " steps, ")
                       + juce::String (song.totalBars (seqs)) + " bars",
                   juce::dontSendNotification);
    empty.setVisible (song.steps.empty());

    while (rows.size() > song.steps.size())
        rows.pop_back();
    while (rows.size() < song.steps.size())
    {
        auto row = std::make_unique<Row>();
        const int i = (int) rows.size();
        row->number.setJustificationType (juce::Justification::centredRight);
        row->sequence.onChange = [this, i]
        {
            const int id = rows[(size_t) i]->sequence.getSelectedId();
            edit ([i, id] (seq::Song& s) { s.steps[(size_t) i].sequence = id - 1; });
        };
        row->repeats.setSliderStyle (juce::Slider::IncDecButtons);
        row->repeats.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 24);
        row->repeats.setRange (1, 99, 1);
        row->repeats.setTextValueSuffix ("x");
        row->repeats.setTooltip ("How many times this sequence plays before the next step");
        row->repeats.onValueChange = [this, i]
        {
            const int n = (int) rows[(size_t) i]->repeats.getValue();
            if (n != project.currentSong().steps[(size_t) i].repeats)
            {
                project.currentSong().steps[(size_t) i].repeats = n;
                project.sequenceChanged();
                if (changed)
                    changed();
                total.setText (juce::String ((int) project.currentSong().steps.size()) + " steps, "
                                   + juce::String (project.currentSong().totalBars (project.getSequences())) + " bars",
                               juce::dontSendNotification);
            }
        };
        row->up.setTooltip ("Move this step earlier");
        row->up.onClick = [this, i] { edit ([i] (seq::Song& s) { std::swap (s.steps[(size_t) i], s.steps[(size_t) i - 1]); }); };
        row->remove.setTooltip ("Remove this step");
        row->remove.onClick = [this, i]
        {
            // Let the button's click finish before its row goes.
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SongEditor> (this), i]
            {
                if (safe != nullptr && i < (int) safe->project.currentSong().steps.size())
                    safe->edit ([i] (seq::Song& s) { s.steps.erase (s.steps.begin() + i); });
            });
        };
        for (auto* c : std::initializer_list<juce::Component*> { &row->number, &row->sequence, &row->repeats, &row->up, &row->remove })
        {
            c->setWantsKeyboardFocus (false);
            rowHolder.addAndMakeVisible (c);
        }
        rows.push_back (std::move (row));
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& row = *rows[i];
        const auto& step = song.steps[i];
        row.number.setText (juce::String ((int) i + 1), juce::dontSendNotification);
        row.sequence.clear (juce::dontSendNotification);
        for (int s = 0; s < (int) seqs.size(); ++s)
            row.sequence.addItem (sequenceName (project, s) + "  (" + juce::String (seqs[(size_t) s].bars) + " bars)", s + 1);
        row.sequence.setSelectedId (step.sequence + 1, juce::dontSendNotification);
        row.repeats.setValue (step.repeats, juce::dontSendNotification);
        row.up.setEnabled (i > 0);
    }
    resized();
}

void SongEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::panel);
}

void SongEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    title.setBounds (r.removeFromTop (26));
    r.removeFromTop (4);
    auto bottom = r.removeFromBottom (30);
    add.setBounds (bottom.removeFromLeft (110));
    bottom.removeFromLeft (10);
    loop.setBounds (bottom.removeFromLeft (130));
    total.setBounds (bottom);
    r.removeFromBottom (8);
    viewport.setBounds (r);
    empty.setBounds (r);

    constexpr int rowHeight = 32;
    rowHolder.setSize (r.getWidth() - (rows.size() * rowHeight > (size_t) r.getHeight() ? viewport.getScrollBarThickness() : 0),
                       (int) rows.size() * rowHeight);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto line = juce::Rectangle<int> (0, (int) i * rowHeight, rowHolder.getWidth(), rowHeight).reduced (0, 3);
        auto& row = *rows[i];
        row.number.setBounds (line.removeFromLeft (24));
        line.removeFromLeft (6);
        row.remove.setBounds (line.removeFromRight (30));
        line.removeFromRight (4);
        row.up.setBounds (line.removeFromRight (40));
        line.removeFromRight (8);
        row.repeats.setBounds (line.removeFromRight (100));
        line.removeFromRight (8);
        row.sequence.setBounds (line);
    }
}

//==============================================================================
LivePanel::LivePanel (MpcProcessor& p) : processor (p), project (p.getProject())
{
    song.setTooltip ("Songs play sequences in order: intro, verse x4, chorus x2... Each saves with the project.");
    song.onChange = [this]
    {
        const int id = song.getSelectedId();
        if (id >= kNewSong)
            songMenu();
        else if (id > 0 && id - 1 != project.getCurrentSongIndex())
        {
            if (processor.isSongPlaying())
                processor.stopTransport();
            project.setCurrentSong (id - 1);
        }
        refresh();
    };
    editSteps.setTooltip ("Choose the song's sequences and how many times each plays");
    editSteps.onClick = [this] { editSong(); };
    playSong.setTooltip ("Play the song from the start; press again to stop");
    playSong.onClick = [this]
    {
        if (processor.isSongPlaying())
            return processor.stopTransport();
        if (! processor.playSong (0))
        {
            say ("This song has no steps yet. Click Steps... to add the sequences it plays.", true);
            editSong();
        }
    };

    switchAt.addItem ("Next at the end", 1);
    switchAt.addItem ("Next at the bar", 2);
    switchAt.setSelectedId (processor.switchAtBar ? 2 : 1, juce::dontSendNotification);
    switchAt.setTooltip ("Pick a sequence while one plays and it waits its turn: until the end of the one playing, or just the next bar line");
    switchAt.onChange = [this] { processor.switchAtBar = switchAt.getSelectedId() == 2; };
    nextLabel.setColour (juce::Label::textColourId, Theme::textDim);
    nextLabel.setColour (juce::Label::backgroundColourId, Theme::background);
    nextLabel.setColour (juce::Label::outlineColourId, Theme::outline);

    tapButton.setTooltip ("Tap a few times in time to set the tempo (or press T)");
    tapButton.onClick = [this] { tap(); };
    songStatus.setColour (juce::Label::textColourId, Theme::textDim);

    tracksLabel.setColour (juce::Label::textColourId, Theme::textDim);
    tracksLabel.setTooltip ("Click a track to mute it, right-click (or Shift-click) to solo it. They work while it plays.");
    for (int i = 0; i < kTrackButtons; ++i)
    {
        auto& b = tracks[(size_t) i];
        b.setTooltip ("Click to mute, right-click or Shift-click to solo");
        b.onClick = [this, i]
        {
            const auto mods = juce::ModifierKeys::getCurrentModifiers();
            trackClicked (i, mods.isShiftDown() || mods.isPopupMenu());
        };
        b.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
        addAndMakeVisible (b);
    }
    allOn.setTooltip ("Unmute and unsolo every track");
    allOn.onClick = [this]
    {
        for (auto& t : project.currentSequence().tracks)
            t.mute = t.solo = false;
        project.sequenceChanged();
    };

    launchkeyPads.setToggleState (processor.launchkeyPads.load(), juce::dontSendNotification);
    launchkeyPads.setTooltip ("A Launchkey's 16 drum pads (Drum mode: channel 10, notes 36-51) play the 16 pads on screen, "
                              "pad 1 bottom left like the MPC. Turn it off if a drum track sends General MIDI drums on channel 10.");
    launchkeyPads.onClick = [this] { processor.launchkeyPads = launchkeyPads.getToggleState(); };
    // Inside the Rig, the Rig maps the pads itself.
    launchkeyPads.setVisible (processor.wrapperType != juce::AudioProcessor::wrapperType_Undefined);
    addChildComponent (launchkeyPads);

    for (auto* b : std::initializer_list<juce::Button*> { &editSteps, &playSong, &tapButton, &allOn, &launchkeyPads })
        b->setWantsKeyboardFocus (false);
    for (auto& b : tracks)
        b.setWantsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &songLabel, &song, &editSteps, &playSong, &songStatus, &nextLabel,
                                                             &switchAt, &tapButton, &tracksLabel, &allOn })
        addAndMakeVisible (c);

    project.sequenceEdits.addChangeListener (this);
    project.addChangeListener (this);
    refresh();
    startTimerHz (15);
}

LivePanel::~LivePanel()
{
    project.sequenceEdits.removeChangeListener (this);
    project.removeChangeListener (this);
}

void LivePanel::songMenu()
{
    const int id = song.getSelectedId();
    if (id == kNewSong)
    {
        if (processor.isSongPlaying())
            processor.stopTransport();
        project.addSong();
        say ("New song. Click Steps... to choose its sequences.");
        editSong();
    }
    else if (id == kRenameSong)
    {
        auto* w = new juce::AlertWindow ("Rename song", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", project.currentSong().name);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            const auto name = w->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
            {
                project.currentSong().name = name;
                project.sequenceChanged();
            }
        }), true);
    }
    else if (id == kDeleteSong)
    {
        if (processor.isSongPlaying())
            processor.stopTransport();
        project.removeSong (project.getCurrentSongIndex());
    }
}

void LivePanel::editSong()
{
    auto editor = std::make_unique<SongEditor> (processor, nullptr);
    editor->setSize (460, juce::jlimit (200, 460, 110 + 32 * (int) project.currentSong().steps.size()));
    juce::CallOutBox::launchAsynchronously (std::move (editor), editSteps.getScreenBounds(), nullptr);
}

void LivePanel::trackClicked (int index, bool solo)
{
    auto& s = project.currentSequence();
    if (! juce::isPositiveAndBelow (index, (int) s.tracks.size()))
        return;
    auto& t = s.tracks[(size_t) index];
    if (solo)
        t.solo = ! t.solo;
    else
        t.mute = ! t.mute;
    project.sequenceChanged();
}

void LivePanel::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000.0)
        taps.clear();
    taps.push_back (now);
    if (taps.size() > 5)
        taps.erase (taps.begin());
    if (taps.size() < 2)
        return say ("Tap again in time...");

    const double beat = (taps.back() - taps.front()) / (double) (taps.size() - 1);
    const double bpm = juce::jlimit (30.0, 300.0, std::round (600000.0 / beat) / 10.0);
    if (processor.getSequencer().isFollowingHost())
        return say ("Following Logic's tempo while Logic plays.", true);
    project.currentSequence().bpm = bpm;
    project.sequenceChanged();
    say ("Tempo " + juce::String (bpm, 1) + " BPM");
}

void LivePanel::refresh()
{
    song.clear (juce::dontSendNotification);
    const auto& songs = project.getSongs();
    for (int i = 0; i < (int) songs.size(); ++i)
        song.addItem (songs[(size_t) i].name, i + 1);
    song.addSeparator();
    song.addItem ("New song", kNewSong);
    song.addItem ("Rename...", kRenameSong);
    song.addItem ("Delete this song", kDeleteSong);
    song.setItemEnabled (kDeleteSong, songs.size() > 1);
    song.setSelectedId (project.getCurrentSongIndex() + 1, juce::dontSendNotification);

    const auto& s = project.currentSequence();
    bool anySolo = false;
    for (const auto& t : s.tracks)
        anySolo |= t.solo;
    for (int i = 0; i < kTrackButtons; ++i)
    {
        auto& b = tracks[(size_t) i];
        const bool exists = i < (int) s.tracks.size();
        b.setVisible (exists);
        if (! exists)
            continue;
        const auto& t = s.tracks[(size_t) i];
        const bool audible = anySolo ? t.solo : ! t.mute;
        b.setButtonText (juce::String (i + 1) + (t.solo ? " S" : t.mute ? " M" : ""));
        b.setTooltip (t.name + (t.notes.empty() ? " (empty)" : "") + ". Click to mute, right-click or Shift-click to solo.");
        b.setColour (juce::TextButton::buttonColourId, t.solo ? juce::Colour (0xffe8c547).darker (0.2f)
                                                       : audible ? Theme::raised.brighter (t.notes.empty() ? 0.0f : 0.25f)
                                                                 : Theme::background);
        b.setColour (juce::TextButton::textColourOffId, t.solo ? Theme::background : audible ? Theme::text : Theme::textDim);
    }
    allOn.setEnabled (std::any_of (s.tracks.begin(), s.tracks.end(), [] (const seq::Track& t) { return t.mute || t.solo; }));
    lastStatus = {};
    timerCallback();
    resized();
}

void LivePanel::timerCallback()
{
    const auto& engine = processor.getSequencer();
    const auto& songNow = project.currentSong();
    const bool songOn = processor.isSongPlaying();

    juce::String status;
    if (songOn && juce::isPositiveAndBelow (processor.getSongStep(), (int) songNow.steps.size()))
    {
        const auto& step = songNow.steps[(size_t) processor.getSongStep()];
        status = "Step " + juce::String (processor.getSongStep() + 1) + " of " + juce::String ((int) songNow.steps.size())
                 + ": " + sequenceName (project, step.sequence)
                 + (step.repeats > 1 ? "  " + juce::String (processor.getSongRepeat() + 1) + "/" + juce::String (step.repeats) : juce::String());
    }
    else
    {
        status = songNow.steps.empty() ? juce::String ("No steps yet")
                                       : juce::String ((int) songNow.steps.size()) + " steps, "
                                             + juce::String (songNow.totalBars (project.getSequences())) + " bars"
                                             + (songNow.loop ? ", loops" : "");
    }

    const int queued = processor.getQueuedSequence();
    const auto next = queued >= 0 ? "Next: " + sequenceName (project, queued)
                    : engine.stopAtEnd.load() ? juce::String ("Stops at the end")
                    : engine.isPlaying() ? juce::String ("Pick a sequence to go next")
                                         : juce::String ("Next: -");
    status += "|" + next + "|" + (songOn ? "1" : "0");
    if (status == lastStatus)
        return;
    lastStatus = status;

    songStatus.setText (status.upToFirstOccurrenceOf ("|", false, false), juce::dontSendNotification);
    nextLabel.setText (next, juce::dontSendNotification);
    nextLabel.setColour (juce::Label::textColourId, queued >= 0 ? Theme::accent : Theme::textDim);
    playSong.setButtonText (songOn ? "Stop song" : "Play song");
    playSong.setColour (juce::TextButton::buttonColourId, songOn ? Theme::accent.darker (0.3f) : Theme::raised);
}

void LivePanel::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void LivePanel::resized()
{
    auto r = getLocalBounds().reduced (10, 4);
    auto row1 = r.removeFromTop (28);
    songLabel.setBounds (row1.removeFromLeft (42));
    song.setBounds (row1.removeFromLeft (150));
    row1.removeFromLeft (6);
    editSteps.setBounds (row1.removeFromLeft (70));
    row1.removeFromLeft (6);
    playSong.setBounds (row1.removeFromLeft (90));
    row1.removeFromLeft (10);

    tapButton.setBounds (row1.removeFromRight (56));
    row1.removeFromRight (12);
    switchAt.setBounds (row1.removeFromRight (150));
    row1.removeFromRight (6);
    nextLabel.setBounds (row1.removeFromRight (juce::jmin (220, row1.getWidth() / 2)));
    row1.removeFromRight (10);
    songStatus.setBounds (row1);

    r.removeFromTop (4);
    auto row2 = r.removeFromTop (24);
    tracksLabel.setBounds (row2.removeFromLeft (52));
    if (launchkeyPads.isVisible())
    {
        launchkeyPads.setBounds (row2.removeFromRight (130));
        row2.removeFromRight (8);
    }
    allOn.setBounds (row2.removeFromRight (64));
    row2.removeFromRight (10);
    const int w = juce::jmin (64, (row2.getWidth() - (kTrackButtons - 1) * 4) / kTrackButtons);
    for (auto& b : tracks)
    {
        b.setBounds (row2.removeFromLeft (w));
        row2.removeFromLeft (4);
    }
}
