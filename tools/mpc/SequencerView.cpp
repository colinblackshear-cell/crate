#include "SequencerView.h"

#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

namespace
{
    constexpr int kNewSequence = 1000, kRenameSequence = 1001, kDeleteSequence = 1002;
    constexpr int kAddTrack = 1000, kRenameTrack = 1001;

    void askForName (juce::Component* parent, const juce::String& title, const juce::String& current,
                     std::function<void (const juce::String&)> done)
    {
        auto* w = new juce::AlertWindow (title, {}, juce::MessageBoxIconType::NoIcon, parent);
        w->addTextEditor ("name", current);
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int result)
        {
            const auto name = w->getTextEditorContents ("name").trim();
            if (result == 1 && name.isNotEmpty())
                done (name);
        }), true);
    }

    void style (juce::Slider& s, double min, double max, double step)
    {
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 28);
        s.setRange (min, max, step);
        s.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    }
}

//==============================================================================
TransportBar::TransportBar (MpcProcessor& p) : processor (p), project (p.getProject())
{
    auto& engine = processor.getSequencer();

    sequence.setTooltip ("The sequence that plays. Each one has its own tempo, length and tracks.");
    sequence.onChange = [this]
    {
        const int id = sequence.getSelectedId();
        if (id == kNewSequence)
        {
            project.addSequence();
            say ("New sequence. Press Rec and play the pads, or click steps in the grid.");
        }
        else if (id == kRenameSequence)
        {
            askForName (this, "Rename sequence", project.currentSequence().name, [this] (const juce::String& name)
            {
                project.currentSequence().name = name;
                project.sequenceChanged();
            });
        }
        else if (id == kDeleteSequence)
        {
            project.removeSequence (project.getCurrentSequenceIndex());
        }
        else if (id > 0)
        {
            project.setCurrentSequence (id - 1);
        }
        refresh();
    };

    playStart.setTooltip ("Play from the start");
    playStart.onClick = [&engine] { engine.play (true); };
    play.setTooltip ("Play from where it stopped (Space starts and stops)");
    play.onClick = [&engine] { engine.isPlaying() ? engine.stop() : engine.play (false); };
    stop.onClick = [&engine] { engine.stop(); };
    rec.setTooltip ("Record what you play onto the selected track. From stop it counts in a bar first; while playing it punches in and out.");
    rec.onClick = [this] { processor.toggleRecord(); };

    position.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::bold));
    position.setJustificationType (juce::Justification::centred);
    position.setColour (juce::Label::backgroundColourId, Theme::background);
    position.setColour (juce::Label::outlineColourId, Theme::outline);
    position.setTooltip ("Bar . beat . tick (96 ticks a beat, like the MPC2000XL)");

    style (tempo, 30.0, 300.0, 0.1);
    tempo.onValueChange = [this]
    {
        project.currentSequence().bpm = tempo.getValue();
        project.sequenceChanged();
    };
    style (bars, 1.0, seq::kMaxBars, 1.0);
    bars.setTooltip ("How long the sequence is. Making it shorter drops the notes past the end (Undo brings them back).");
    bars.onValueChange = [this]
    {
        const int n = (int) bars.getValue();
        if (n == project.currentSequence().bars)
            return;
        processor.beginEdit();
        project.currentSequence().setBars (n);
        project.sequenceChanged();
    };

    loop.onClick = [this]
    {
        project.currentSequence().loop = loop.getToggleState();
        project.sequenceChanged();
    };
    countIn.setToggleState (engine.countIn.load(), juce::dontSendNotification);
    countIn.setTooltip ("Count in a bar before recording from stop");
    countIn.onClick = [this, &engine] { engine.countIn = countIn.getToggleState(); };

    click.addItem ("Click off", 1 + (int) SequencerEngine::ClickMode::off);
    click.addItem ("Click on rec", 1 + (int) SequencerEngine::ClickMode::recording);
    click.addItem ("Click always", 1 + (int) SequencerEngine::ClickMode::always);
    click.setSelectedId (1 + engine.clickMode.load(), juce::dontSendNotification);
    click.onChange = [this, &engine] { engine.clickMode = click.getSelectedId() - 1; };

    for (auto* b : { &playStart, &play, &stop, &rec })
        b->setWantsKeyboardFocus (false);
    followHost.setToggleState (engine.followHost.load(), juce::dontSendNotification);
    followHost.setTooltip ("When Logic plays, the sequence plays along at Logic's tempo and position");
    followHost.onClick = [this, &engine] { engine.followHost = followHost.getToggleState(); };
    followHost.setVisible (processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone);
    addChildComponent (followHost);

    for (auto* b : { &loop, &countIn, &followHost })
        b->setWantsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &sequence, &playStart, &play, &stop, &rec, &position, &tempoLabel,
                                                             &tempo, &barsLabel, &bars, &loop, &countIn, &click })
        addAndMakeVisible (c);

    project.sequenceEdits.addChangeListener (this);
    project.addChangeListener (this);
    refresh();
    timerCallback();
    startTimerHz (20);
}

TransportBar::~TransportBar()
{
    project.sequenceEdits.removeChangeListener (this);
    project.removeChangeListener (this);
}

void TransportBar::refresh()
{
    sequence.clear (juce::dontSendNotification);
    const auto& seqs = project.getSequences();
    for (int i = 0; i < (int) seqs.size(); ++i)
        sequence.addItem (juce::String (i + 1).paddedLeft ('0', 2) + "  " + seqs[(size_t) i].name, i + 1);
    sequence.addSeparator();
    sequence.addItem ("New sequence", kNewSequence);
    sequence.addItem ("Rename...", kRenameSequence);
    sequence.addItem ("Delete this sequence", kDeleteSequence);
    sequence.setItemEnabled (kDeleteSequence, seqs.size() > 1);
    sequence.setSelectedId (project.getCurrentSequenceIndex() + 1, juce::dontSendNotification);

    const auto& s = project.currentSequence();
    tempo.setValue (s.bpm, juce::dontSendNotification);
    bars.setValue (s.bars, juce::dontSendNotification);
    loop.setToggleState (s.loop, juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    const auto& engine = processor.getSequencer();
    const auto& s = project.currentSequence();
    const double t = engine.getPosition();
    position.setText (t < 0 ? "Count " + juce::String ((int) std::floor (t / seq::kTicksPerBeat) + s.beatsPerBar + 1)
                            : seq::formatPosition (t, s.beatsPerBar),
                      juce::dontSendNotification);
    position.setColour (juce::Label::textColourId, engine.isRecording() ? Theme::record : engine.isPlaying() ? Theme::accent : Theme::text);

    play.setButtonText (engine.isPlaying() ? "Pause" : "Play");
    rec.setColour (juce::TextButton::buttonColourId, engine.isRecording() ? Theme::record : Theme::raised);
    play.setColour (juce::TextButton::buttonColourId, engine.isPlaying() && ! engine.isRecording() ? Theme::accent.darker (0.3f) : Theme::raised);

    const bool following = engine.isFollowingHost();
    tempo.setEnabled (! following);
    tempo.setTooltip (following ? "Following Logic's tempo while Logic plays" : "The sequence's tempo");
}

void TransportBar::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    sequence.setBounds (r.removeFromLeft (160));
    r.removeFromLeft (12);
    for (auto* b : { &playStart, &play, &stop, &rec })
    {
        b->setBounds (r.removeFromLeft (b == &playStart ? 40 : 62));
        r.removeFromLeft (4);
    }
    r.removeFromLeft (8);
    position.setBounds (r.removeFromLeft (120));
    r.removeFromLeft (14);
    tempoLabel.setBounds (r.removeFromLeft (44));
    tempo.setBounds (r.removeFromLeft (108));
    r.removeFromLeft (10);
    barsLabel.setBounds (r.removeFromLeft (38));
    bars.setBounds (r.removeFromLeft (96));
    r.removeFromLeft (10);
    loop.setBounds (r.removeFromLeft (60));
    countIn.setBounds (r.removeFromLeft (86));
    if (followHost.isVisible())
        followHost.setBounds (r.removeFromRight (juce::jmin (134, r.getWidth() / 2)));
    click.setBounds (r.removeFromLeft (juce::jmin (r.getWidth(), 140)));
}

//==============================================================================
StepGrid::StepGrid (MpcProcessor& p) : processor (p), project (p.getProject()) {}

int StepGrid::stepsPerBar() const
{
    return project.currentSequence().ticksPerBar() / seq::gridTicks (grid);
}

int StepGrid::stepStart (int step) const
{
    return bar * project.currentSequence().ticksPerBar() + seq::stepTick (grid, processor.getSequencer().swing.load(), step);
}

juce::Rectangle<int> StepGrid::cell (int row, int step) const
{
    const auto area = getLocalBounds().withTrimmedLeft (kLabelWidth);
    const float w = area.getWidth() / (float) stepsPerBar();
    const float h = getHeight() / (float) kPadsPerBank;
    return juce::Rectangle<float> (area.getX() + step * w, row * h, w, h).toNearestInt();
}

int StepGrid::rowAt (int y) const
{
    return juce::jlimit (0, kPadsPerBank - 1, (int) (y / (getHeight() / (float) kPadsPerBank)));
}

int StepGrid::stepAt (int x) const
{
    if (x < kLabelWidth)
        return -1;
    const float w = (getWidth() - kLabelWidth) / (float) stepsPerBar();
    return juce::jlimit (0, stepsPerBar() - 1, (int) ((x - kLabelWidth) / w));
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto& s = project.currentSequence();
    const auto& bank = project.currentBank();
    const int trackIndex = juce::jlimit (0, (int) s.tracks.size() - 1, processor.recordTrack);
    const int steps = stepsPerBar();
    const int perBeat = juce::jmax (1, seq::kTicksPerBeat / seq::gridTicks (grid));
    const int swingNow = processor.getSequencer().swing.load();

    g.fillAll (Theme::panel);

    // Each cell covers the ticks closest to its step, so swung and played-in notes show in it.
    auto cellRange = [&] (int step)
    {
        const int base = bar * s.ticksPerBar();
        const int here = seq::stepTick (grid, swingNow, step);
        const int prev = seq::stepTick (grid, swingNow, step - 1), next = seq::stepTick (grid, swingNow, step + 1);
        return juce::Range<int> (base + (prev + here + 1) / 2, base + (here + next + 1) / 2);
    };

    for (int row = 0; row < kPadsPerBank; ++row)
    {
        const int pad = padForRow (row);
        const auto& p = bank.pads[(size_t) pad];
        auto label = cell (row, 0).withX (0).withWidth (kLabelWidth - 4);
        if (row % 2 == 1)
        {
            g.setColour (Theme::background.withAlpha (0.35f));
            g.fillRect (label.withRight (getWidth()));
        }
        g.setColour (pad == heldPad ? Theme::accent : p.isEmpty() ? Theme::textDim.withAlpha (0.6f) : Theme::text);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText (padLabel (pad), label.removeFromLeft (34).withTrimmedLeft (6), juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (Theme::textDim);
        g.drawText (p.name, label, juce::Justification::centredLeft, true);

        for (int step = 0; step < steps; ++step)
        {
            const auto r = cell (row, step).reduced (1);
            const auto range = cellRange (step);
            g.setColour ((step / perBeat) % 2 == 0 ? Theme::raised : Theme::raised.darker (0.25f));
            g.fillRect (r);

            // Other tracks' hits faintly, the selected track's brightly, louder ones fuller.
            int own = 0, others = 0;
            for (int t = 0; t < (int) s.tracks.size(); ++t)
                for (const auto& n : s.tracks[(size_t) t].notes)
                    if (n.pad == pad && range.contains (n.tick))
                    {
                        if (t == trackIndex) own = juce::jmax (own, n.velocity);
                        else others = juce::jmax (others, n.velocity);
                    }
            if (others > 0)
            {
                g.setColour (Theme::textDim.withAlpha (0.35f));
                g.fillRect (r.reduced (r.getWidth() / 3, r.getHeight() / 3));
            }
            if (own > 0)
            {
                const auto& track = s.tracks[(size_t) trackIndex];
                g.setColour ((track.mute ? Theme::textDim : Theme::accent).withAlpha (0.35f + 0.65f * own / 127.0f));
                const int h = juce::jmax (4, (int) (r.getHeight() * (0.4f + 0.6f * own / 127.0f)));
                g.fillRect (r.withTrimmedTop (r.getHeight() - h));
            }
        }
    }

    // Beat and bar lines.
    for (int step = 0; step <= steps; step += perBeat)
    {
        const int x = step < steps ? cell (0, step).getX() : getWidth() - 1;
        g.setColour (step % (perBeat * s.beatsPerBar) == 0 ? Theme::outline.brighter (0.4f) : Theme::outline);
        g.drawVerticalLine (x, 0.0f, (float) getHeight());
    }

    // The playhead.
    const double t = processor.getSequencer().getPosition();
    if (t >= bar * s.ticksPerBar() && t < (bar + 1) * s.ticksPerBar())
    {
        const float x = kLabelWidth + (float) ((t - bar * s.ticksPerBar()) / s.ticksPerBar()) * (getWidth() - kLabelWidth);
        g.setColour (processor.getSequencer().isRecording() ? Theme::record : Theme::text);
        g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) getHeight());
    }
}

void StepGrid::setCell (int row, int step, bool on, int velocity)
{
    auto& s = project.currentSequence();
    auto& track = s.tracks[(size_t) juce::jlimit (0, (int) s.tracks.size() - 1, processor.recordTrack)];
    const int pad = padForRow (row);
    const int swingNow = processor.getSequencer().swing.load();
    const int base = bar * s.ticksPerBar();
    const int here = seq::stepTick (grid, swingNow, step);
    const int from = base + (seq::stepTick (grid, swingNow, step - 1) + here + 1) / 2;
    const int to = base + (here + seq::stepTick (grid, swingNow, step + 1) + 1) / 2;

    track.remove (pad, from, to);
    if (on && base + here < s.lengthTicks())
        track.add ({ base + here, pad, velocity, seq::gridTicks (grid) });
    project.sequenceChanged();
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    const int step = stepAt (e.x);
    const int pad = padForRow (row);

    if (step < 0)
    {
        // The pad names play the pad, so you can record from here too.
        heldPad = pad;
        processor.playPad (pad);
        repaint();
        return;
    }

    auto& s = project.currentSequence();
    const auto& track = s.tracks[(size_t) juce::jlimit (0, (int) s.tracks.size() - 1, processor.recordTrack)];
    const int swingNow = processor.getSequencer().swing.load();
    const int base = bar * s.ticksPerBar();
    const int here = seq::stepTick (grid, swingNow, step);
    const int from = base + (seq::stepTick (grid, swingNow, step - 1) + here + 1) / 2;
    const int to = base + (here + seq::stepTick (grid, swingNow, step + 1) + 1) / 2;
    const bool has = std::any_of (track.notes.begin(), track.notes.end(),
                                  [&] (const seq::Note& n) { return n.pad == pad && n.tick >= from && n.tick < to; });

    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu menu;
        for (auto [name, v] : { std::pair { "Soft", 40 }, { "Medium", 80 }, { "Hard", 110 }, { "Full", 127 } })
            menu.addItem (juce::String (name) + " hit", [this, row, step, v = v] { processor.beginEdit(); setCell (row, step, true, v); });
        if (has)
            menu.addItem ("Remove", [this, row, step] { processor.beginEdit(); setCell (row, step, false, 0); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        return;
    }

    processor.beginEdit();
    dragRow = row;
    dragAdds = ! has;
    dragDone = { step };
    setCell (row, step, dragAdds, 100);
    if (dragAdds)
        processor.playPad (pad, 100 / 127.0f);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    const int step = stepAt (e.x);
    if (dragRow < 0 || step < 0 || std::find (dragDone.begin(), dragDone.end(), step) != dragDone.end())
        return;
    dragDone.push_back (step);
    setCell (dragRow, step, dragAdds, 100);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (heldPad >= 0)
        processor.releasePad (heldPad);
    heldPad = dragRow = -1;
    repaint();
}

//==============================================================================
SequencerView::SequencerView (MpcProcessor& p) : processor (p), project (p.getProject()), steps (p)
{
    auto& engine = processor.getSequencer();

    track.setTooltip ("The track you record onto and edit. All tracks play together.");
    track.onChange = [this]
    {
        const int id = track.getSelectedId();
        auto& s = project.currentSequence();
        if (id == kAddTrack)
        {
            if ((int) s.tracks.size() < seq::kMaxTracks)
            {
                processor.beginEdit();
                s.tracks.push_back ({ "Track " + juce::String ((int) s.tracks.size() + 1), false, {} });
                processor.recordTrack = (int) s.tracks.size() - 1;
                project.sequenceChanged();
            }
        }
        else if (id == kRenameTrack)
        {
            askForName (this, "Rename track", s.tracks[(size_t) processor.recordTrack].name, [this] (const juce::String& name)
            {
                project.currentSequence().tracks[(size_t) processor.recordTrack].name = name;
                project.sequenceChanged();
            });
        }
        else if (id > 0)
        {
            processor.recordTrack = id - 1;
        }
        refresh();
    };

    mute.setTooltip ("Silence this track");
    mute.onClick = [this]
    {
        processor.beginEdit();
        project.currentSequence().tracks[(size_t) processor.recordTrack].mute = mute.getToggleState();
        project.sequenceChanged();
    };

    for (int g = 0; g <= (int) seq::Grid::thirtySecondTriplet; ++g)
        gridChoice.addItem (seq::gridName ((seq::Grid) g), g + 1);
    gridChoice.setSelectedId (engine.grid.load() + 1, juce::dontSendNotification);
    gridChoice.setTooltip ("Timing correct: what you record snaps to this grid. The step grid shows it too.");
    gridChoice.onChange = [this, &engine]
    {
        const auto g = (seq::Grid) (gridChoice.getSelectedId() - 1);
        engine.grid = (int) g;
        steps.grid = g == seq::Grid::off ? seq::Grid::sixteenth : g;
        swing.setEnabled (seq::gridSwings (g));
        steps.repaint();
    };

    swing.setSliderStyle (juce::Slider::LinearBar);
    swing.setRange (50, 75, 1);
    swing.setTextValueSuffix ("%");
    swing.setValue (engine.swing.load(), juce::dontSendNotification);
    swing.setTooltip ("Swing pushes every other 1/8 or 1/16 late. 50% is straight; 66% is a triplet feel.");
    swing.onValueChange = [this, &engine] { engine.swing = (int) swing.getValue(); steps.repaint(); };

    noteRepeat.setTooltip ("Hold a pad while it plays to repeat it on every step of the timing grid");
    noteRepeat.onClick = [this, &engine] { engine.noteRepeat = noteRepeat.getToggleState(); };

    fix.setTooltip ("Snap this track's notes to the timing grid (with swing)");
    fix.onClick = [this, &engine]
    {
        const auto g = (seq::Grid) engine.grid.load();
        if (g == seq::Grid::off)
            return say ("Pick a timing grid first.", true);
        processor.beginEdit();
        auto& s = project.currentSequence();
        seq::correct (s.tracks[(size_t) processor.recordTrack], g, engine.swing.load(), s.lengthTicks());
        project.sequenceChanged();
        say ("Snapped " + s.tracks[(size_t) processor.recordTrack].name + " to " + seq::gridName (g) + ".");
    };
    clearTrack.setTooltip ("Remove every note from this track");
    clearTrack.onClick = [this]
    {
        processor.beginEdit();
        project.currentSequence().tracks[(size_t) processor.recordTrack].notes.clear();
        project.sequenceChanged();
        say ("Cleared the track. Undo brings it back.");
    };
    undo.onClick = [this] { processor.undoEdit(); };
    redo.onClick = [this] { processor.redoEdit(); };

    prevBar.onClick = [this] { steps.bar = juce::jmax (0, steps.bar - 1); refresh(); };
    nextBar.onClick = [this] { steps.bar = juce::jmin (project.currentSequence().bars - 1, steps.bar + 1); refresh(); };
    barLabel.setJustificationType (juce::Justification::centred);

    exportButton.setTooltip ("Save the sequence as a MIDI file. The MPC2000XL loads it into a sequence (LOAD, then pick the .MID).");
    exportButton.onClick = [this] { exportMidi(); };
    importButton.setTooltip ("Open a MIDI file as a new sequence. Notes 35 to 98 land on pads A01 to D16, like on the MPC.");
    importButton.onClick = [this] { importMidi(); };

    for (auto* b : std::initializer_list<juce::Button*> { &mute, &noteRepeat, &fix, &clearTrack, &undo, &redo, &prevBar,
                                                          &nextBar, &exportButton, &importButton })
        b->setWantsKeyboardFocus (false);

    for (auto* c : std::initializer_list<juce::Component*> { &track, &mute, &gridLabel, &gridChoice, &swingLabel, &swing,
                                                             &noteRepeat, &fix, &clearTrack, &undo, &redo, &prevBar,
                                                             &barLabel, &nextBar, &exportButton, &importButton, &steps })
        addAndMakeVisible (c);

    project.sequenceEdits.addChangeListener (this);
    project.addChangeListener (this);
    refresh();
    startTimerHz (30);
}

SequencerView::~SequencerView()
{
    project.sequenceEdits.removeChangeListener (this);
    project.removeChangeListener (this);
}

void SequencerView::refresh()
{
    auto& s = project.currentSequence();
    processor.recordTrack = juce::jlimit (0, (int) s.tracks.size() - 1, processor.recordTrack);
    steps.bar = juce::jlimit (0, s.bars - 1, steps.bar);

    track.clear (juce::dontSendNotification);
    for (int i = 0; i < (int) s.tracks.size(); ++i)
    {
        const auto& t = s.tracks[(size_t) i];
        track.addItem (t.name + (t.notes.empty() ? juce::String() : "  (" + juce::String ((int) t.notes.size()) + ")")
                           + (t.mute ? "  muted" : ""), i + 1);
    }
    track.addSeparator();
    track.addItem ("Add a track", kAddTrack);
    track.addItem ("Rename track...", kRenameTrack);
    track.setItemEnabled (kAddTrack, (int) s.tracks.size() < seq::kMaxTracks);
    track.setSelectedId (processor.recordTrack + 1, juce::dontSendNotification);
    mute.setToggleState (s.tracks[(size_t) processor.recordTrack].mute, juce::dontSendNotification);

    barLabel.setText ("Bar " + juce::String (steps.bar + 1) + " of " + juce::String (s.bars), juce::dontSendNotification);
    prevBar.setEnabled (steps.bar > 0);
    nextBar.setEnabled (steps.bar < s.bars - 1);
    undo.setEnabled (processor.canUndo());
    redo.setEnabled (processor.canRedo());
    steps.repaint();
}

void SequencerView::timerCallback()
{
    // While playing, the grid follows the bar that's playing.
    const auto& engine = processor.getSequencer();
    const auto& s = project.currentSequence();
    if (engine.isPlaying() && engine.getPosition() >= 0)
    {
        const int playingBar = juce::jlimit (0, s.bars - 1, (int) (engine.getPosition() / s.ticksPerBar()));
        if (playingBar != lastFollowBar)
        {
            lastFollowBar = playingBar;
            steps.bar = playingBar;
            refresh();
        }
    }
    else
    {
        lastFollowBar = -1;
    }
    if (undo.isEnabled() != processor.canUndo() || redo.isEnabled() != processor.canRedo())
        refresh();
    steps.repaint();
}

void SequencerView::exportMidi()
{
    const auto& s = project.currentSequence();
    if (s.isEmpty())
        return say ("This sequence has no notes yet.", true);

    const auto name = juce::File::createLegalFileName (s.name.isNotEmpty() ? s.name : "Sequence");
    chooser = std::make_unique<juce::FileChooser> ("Save the sequence as a MIDI file",
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (name + ".mid"),
                                                   "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("mid");
                              const auto data = seq::toMidiFile (project.currentSequence());
                              if (file.replaceWithData (data.getData(), data.getSize()))
                                  say ("Saved " + file.getFileName() + ". On the MPC: LOAD, pick the .MID, and it loads into a sequence.");
                              else
                                  say ("Couldn't write " + file.getFullPathName() + ".", true);
                          });
}

void SequencerView::importMidi()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a MIDI file", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              juce::MemoryBlock data;
                              file.loadFileAsData (data);
                              seq::Sequence s;
                              s.name = file.getFileNameWithoutExtension();
                              juce::String error;
                              if (! seq::fromMidiFile (data.getData(), data.getSize(), s, error))
                                  return say (error, true);
                              project.getSequences().push_back (std::move (s));
                              project.setCurrentSequence ((int) project.getSequences().size() - 1);
                              processor.recordTrack = 0;
                              say ("Opened " + file.getFileName() + " as a new sequence." + (error.isNotEmpty() ? " " + error : juce::String()),
                                   error.isNotEmpty());
                          });
}

void SequencerView::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void SequencerView::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto row1 = r.removeFromTop (28);
    track.setBounds (row1.removeFromLeft (juce::jmin (170, row1.getWidth() / 4)));
    row1.removeFromLeft (6);
    mute.setBounds (row1.removeFromLeft (64));
    gridLabel.setBounds (row1.removeFromLeft (50));
    gridChoice.setBounds (row1.removeFromLeft (110));
    row1.removeFromLeft (8);
    swingLabel.setBounds (row1.removeFromLeft (46));
    swing.setBounds (row1.removeFromLeft (juce::jmax (60, row1.getWidth() - 120)));
    row1.removeFromLeft (8);
    noteRepeat.setBounds (row1);
    r.removeFromTop (6);

    auto row2 = r.removeFromTop (28);
    prevBar.setBounds (row2.removeFromLeft (28));
    barLabel.setBounds (row2.removeFromLeft (90));
    nextBar.setBounds (row2.removeFromLeft (28));
    row2.removeFromLeft (10);
    // Wider buttons for the longer names.
    const int unit = (row2.getWidth() - 5 * 4) / 15;
    for (auto [b, units] : { std::pair<juce::Button*, int> { &fix, 3 }, { &clearTrack, 2 }, { &undo, 2 }, { &redo, 2 },
                             { &importButton, 3 }, { &exportButton, 3 } })
    {
        b->setBounds (row2.removeFromLeft (unit * units));
        row2.removeFromLeft (4);
    }
    r.removeFromTop (8);
    steps.setBounds (r);
}
