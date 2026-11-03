#include "RigView.h"

namespace Theme = crate::Theme;

namespace
{
constexpr int kBottomHeight = 132;
const juce::Colour mpcColour { 0xffff8a3d }, samplerColour { 0xff8fd3c8 }, vocoderColour { 0xffb48cff }, masterColour { 0xffeeeeee },
                   bassColour { 0xff4fc3f7 }, synthColour { 0xfff06292 };

juce::Colour stripColour (int strip)
{
    switch (strip)
    {
        case RigEngine::mpcStrip:     return mpcColour;
        case RigEngine::samplerStrip: return samplerColour;
        case RigEngine::vocoderStrip: return vocoderColour;
        case RigEngine::bassStrip:    return bassColour;
        case RigEngine::synthStrip:   return synthColour;
        default:                      return masterColour;
    }
}

juce::File showsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig");
}
}

//==============================================================================
RigView::StripView::StripView (RigEngine& e, int s, int fader, int muteC, bool hasOnSwitch)
    : engine (e), strip (s), faderControl (fader), muteControl (muteC), hasOn (hasOnSwitch)
{
    if (hasOn)
    {
        addAndMakeVisible (on);
        on.setClickingTogglesState (true);
        on.setColour (juce::TextButton::buttonOnColourId, stripColour (strip));
        on.setTooltip ("Turns the vocoder on or off. Off, it's silent and uses no processor time.");
        on.onClick = [this] { engine.vocoderOn = on.getToggleState(); };
    }
    addAndMakeVisible (level);
    addAndMakeVisible (mute);
    addAndMakeVisible (learn);
    level.setRange (0.0, 1.0);
    level.setDoubleClickReturnValue (true, 0.8);
    level.setColour (juce::Slider::thumbColourId, stripColour (strip));
    level.setColour (juce::Slider::trackColourId, stripColour (strip).withAlpha (0.6f));
    level.onValueChange = [this] { engine.getStrip (strip).level = (float) level.getValue(); };
    mute.setClickingTogglesState (true);
    mute.setColour (juce::TextButton::buttonOnColourId, Theme::record);
    mute.onClick = [this] { engine.getStrip (strip).mute = mute.getToggleState(); };
    mute.setVisible (muteControl >= 0 || strip == RigEngine::masterStrip);
    learn.setTooltip ("Press, then move a fader or knob on your keyboard to control this level with it.");
    refresh();
}

void RigView::StripView::refresh()
{
    level.setValue (engine.getStrip (strip).level.load(), juce::dontSendNotification);
    mute.setToggleState (engine.getStrip (strip).mute.load(), juce::dontSendNotification);
    if (hasOn)
    {
        on.setToggleState (engine.vocoderOn.load(), juce::dontSendNotification);
        on.setButtonText (engine.vocoderOn.load() ? "On" : "Off");
    }
}

void RigView::StripView::tick()
{
    const float peak = engine.getStrip (strip).peak.exchange (0.0f);
    meter = juce::jmax (peak, meter * 0.85f);
    repaint();
}

void RigView::StripView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (area, 6.0f);

    auto top = getLocalBounds().reduced (10, 6).removeFromTop (20);
    g.setColour (stripColour (strip));
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText (RigEngine::stripName (strip), top, juce::Justification::centredLeft);

    // The meter, under the fader.
    auto meterArea = getLocalBounds().reduced (10, 0).withTop (level.getBottom()).withHeight (6).toFloat();
    g.setColour (Theme::background);
    g.fillRoundedRectangle (meterArea, 3.0f);
    const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
    const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (meter > 0.98f ? Theme::record : stripColour (strip).withAlpha (0.9f));
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * fill), 3.0f);
}

void RigView::StripView::resized()
{
    auto area = getLocalBounds().reduced (10, 6);
    auto top = area.removeFromTop (20);
    learn.setBounds (top.removeFromRight (48));
    top.removeFromRight (4);
    mute.setBounds (top.removeFromRight (42));
    area.removeFromTop (4);
    auto row = area.removeFromTop (26);
    if (hasOn)
        on.setBounds (row.removeFromLeft (36).reduced (0, 2));
    level.setBounds (row);
}

//==============================================================================
RigView::RigView (RigEngine& e, juce::AudioDeviceManager& d)
    : engine (e), devices (d)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &clickButton, &learnPlay, &learnStop,
                                                             &tempoLabel, &tempo, &tapButton, &sampleButton, &knobButton, &position, &midiLight,
                                                             &audioButton, &saveButton, &openButton, &tabs,
                                                             &padsLabel, &padBank, &keysLabel, &keysTo,
                                                             &sourceLabel, &sampleSource, &splitLabel, &splitNote, &lowerKeysTo })
        addAndMakeVisible (c);

    auto& transport = engine.getTransport();

    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4caf50));
    playButton.setClickingTogglesState (false);
    playButton.onClick = [&transport] { transport.togglePlay(); };
    playButton.setTooltip ("Starts and stops every tool together (space bar).");
    stopButton.onClick = [&transport] { transport.stop(); };
    stopButton.setTooltip ("Stops and goes back to the start.");
    clickButton.setClickingTogglesState (true);
    clickButton.onClick = [this, &transport] { transport.metronome = clickButton.getToggleState(); };
    clickButton.setTooltip ("A metronome click on every beat while playing.");
    learnPlay.setTooltip ("Press, then press a button on your keyboard to make it start and stop the Rig.");
    learnStop.setTooltip ("Press, then press a button on your keyboard to make it stop the Rig.");
    learnPlay.onClick = [this] { startLearning (RigRouting::play, learnPlay); };
    learnStop.onClick = [this] { startLearning (RigRouting::stop, learnStop); };

    tempo.setRange (RigTransport::kMinBpm, RigTransport::kMaxBpm, 0.5);
    tempo.setTextValueSuffix (" BPM");
    tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 28);
    tempo.onValueChange = [this, &transport] { transport.setBpm (tempo.getValue()); };
    tapButton.setTooltip ("Tap a few times in time to set the tempo.");
    tapButton.onClick = [this]
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        if (! taps.empty() && now - taps.back() > 2000.0)
            taps.clear();
        taps.push_back (now);
        if (taps.size() > 5)
            taps.erase (taps.begin());
        if (taps.size() >= 2)
        {
            const double beat = (taps.back() - taps.front()) / (double) (taps.size() - 1);
            tempo.setValue (std::round (60000.0 / beat * 2.0) / 2.0);
        }
    };

    sampleButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
    sampleButton.setTooltip ("Records what the Sampler hears (pick it in Sampler records: the mic, the vocoder, or the Rig mix) "
                             "straight into the Sampler. Press again to stop; then play it from the keys or edit it in the Sampler tab.");
    sampleButton.onClick = [this] { toggleSampling(); };

    knobButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    knobButton.setTooltip ("Put a knob of your keyboard on any setting: choose Learn a knob, touch the setting on screen "
                           "(the bass's Cutoff, the synth's LFO depth...), then turn the knob.");
    knobButton.onClick = [this] { showKnobMenu(); };

    position.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    position.setJustificationType (juce::Justification::centred);
    position.setColour (juce::Label::backgroundColourId, Theme::background);
    midiLight.setJustificationType (juce::Justification::centredLeft);
    midiLight.setColour (juce::Label::textColourId, Theme::textDim);

    audioButton.onClick = [this] { showAudioSettings(); };
    saveButton.onClick = [this] { saveShowAs(); };
    openButton.onClick = [this] { openShow(); };

    // The tools, as their own windows show them.
    tabs.setTabBarDepth (34);
    tabs.setOutline (0);
    tabs.addTab ("MPC", Theme::panel, engine.getMpc().createEditorAndMakeActive(), true);
    tabs.addTab ("Vocoder", Theme::panel, engine.getVocoder().createEditorAndMakeActive(), true);
    tabs.addTab ("Bass", Theme::panel, engine.getBass().createEditorAndMakeActive(), true);
    tabs.addTab ("Synth", Theme::panel, engine.getSynth().createEditorAndMakeActive(), true);
    tabs.addTab ("Sampler", Theme::panel, engine.getSampler().createEditorAndMakeActive(), true);
    const juce::Colour tabColours[] = { mpcColour, vocoderColour, bassColour, synthColour, samplerColour };
    for (int i = 0; i < 5; ++i)
        tabs.getTabbedButtonBar().setTabBackgroundColour (i, tabColours[i].withAlpha (0.25f));

    for (int t = 0; t < RigEngine::numTools; ++t)
        engine.getTool (t).addListener (this);

    // Routing.
    for (auto* l : { &padsLabel, &keysLabel, &sourceLabel, &splitLabel })
        l->setColour (juce::Label::textColourId, Theme::textDim);
    padBank.addItemList ({ "A", "B", "C", "D" }, 1);
    padBank.onChange = [this] { engine.getRouting().padBank = padBank.getSelectedItemIndex(); };
    padBank.setTooltip ("Your keyboard's 16 drum pads (MIDI channel 10) play these MPC pads.");
    for (auto d : { RigRouting::Dest::vocoder, RigRouting::Dest::synth, RigRouting::Dest::bass, RigRouting::Dest::sampler,
                    RigRouting::Dest::mpc, RigRouting::Dest::off })
    {
        keysTo.addItem (RigRouting::destName ((int) d), 1 + (int) d);
        lowerKeysTo.addItem (RigRouting::destName ((int) d), 1 + (int) d);
    }
    keysTo.onChange = [this] { engine.getRouting().keysTo = keysTo.getSelectedId() - 1; };
    lowerKeysTo.onChange = [this] { engine.getRouting().lowerKeysTo = lowerKeysTo.getSelectedId() - 1; };
    lowerKeysTo.setTooltip ("What the keys below the split play, e.g. the bass with your left hand.");
    splitNote.addItem ("No split", 1);
    for (int note = 36; note <= 72; note += 12)
        for (int n : { note, note + 5, note + 7 })
            if (n <= 72)
                splitNote.addItem (juce::MidiMessage::getMidiNoteName (n, true, true, 3), n + 1);
    splitNote.onChange = [this]
    {
        engine.getRouting().splitNote = splitNote.getSelectedId() <= 1 ? 0 : splitNote.getSelectedId() - 1;
        lowerKeysTo.setEnabled (engine.getRouting().splitNote.load() > 0);
    };
    splitNote.setTooltip ("Split the keyboard: keys below this note play a second instrument.");
    keysTo.setTooltip ("What your keyboard's keys play.");
    for (int i = 0; i < 3; ++i)
        sampleSource.addItem (RigEngine::sampleSourceName (i), i + 1);
    sampleSource.onChange = [this] { engine.sampleSource = sampleSource.getSelectedId() - 1; };
    sampleSource.setTooltip ("What the Sampler records: your mic, the vocoder, or the Rig mix (the MPC and the vocoder as you hear them).");

    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::mpcStrip, RigRouting::mpcLevel, RigRouting::mpcMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::vocoderStrip, RigRouting::vocoderLevel, RigRouting::vocoderMute, true));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::bassStrip, RigRouting::bassLevel, RigRouting::bassMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::synthStrip, RigRouting::synthLevel, RigRouting::synthMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::samplerStrip, RigRouting::samplerLevel, RigRouting::samplerMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::masterStrip, RigRouting::masterLevel, -1));
    const int faders[] = { RigRouting::mpcLevel, RigRouting::vocoderLevel, RigRouting::bassLevel, RigRouting::synthLevel,
                           RigRouting::samplerLevel, RigRouting::masterLevel };
    for (size_t i = 0; i < stripViews.size(); ++i)
    {
        auto* s = stripViews[i].get();
        addAndMakeVisible (s);
        const int control = faders[i];
        s->learn.onClick = [this, s, control] { startLearning (control, s->learn); };
    }

    openMidiInputs();
    refresh();
    startTimerHz (30);
}

RigView::~RigView()
{
    for (int t = 0; t < RigEngine::numTools; ++t)
        engine.getTool (t).removeListener (this);
    tabs.clearTabs();
    setLookAndFeel (nullptr);
}

void RigView::refresh()
{
    auto& transport = engine.getTransport();
    tempo.setValue (transport.getBpm(), juce::dontSendNotification);
    clickButton.setToggleState (transport.metronome.load(), juce::dontSendNotification);
    padBank.setSelectedItemIndex (engine.getRouting().padBank.load(), juce::dontSendNotification);
    keysTo.setSelectedId (engine.getRouting().keysTo.load() + 1, juce::dontSendNotification);
    sampleSource.setSelectedId (engine.sampleSource.load() + 1, juce::dontSendNotification);
    lowerKeysTo.setSelectedId (engine.getRouting().lowerKeysTo.load() + 1, juce::dontSendNotification);
    const int split = engine.getRouting().splitNote.load();
    splitNote.setSelectedId (split > 0 ? split + 1 : 1, juce::dontSendNotification);
    lowerKeysTo.setEnabled (split > 0);
    for (auto& s : stripViews)
        s->refresh();
    updateLearnButtons();
}

void RigView::startLearning (int control, juce::Button& button)
{
    auto& routing = engine.getRouting();
    if (routing.getLearning() == control)
    {
        // Pressing Learn again while it's waiting forgets the control.
        routing.cancelLearn();
        routing.forget (control);
    }
    else
    {
        routing.learn (control);
    }
    juce::ignoreUnused (button);
    updateLearnButtons();
}

void RigView::updateLearnButtons()
{
    auto& routing = engine.getRouting();
    const int learning = routing.getLearning();

    auto show = [&] (juce::Button& b, int control)
    {
        const auto [channel, cc] = routing.getAssignment (control);
        if (learning == control)
        {
            b.setButtonText ("Move it...");
            b.setColour (juce::TextButton::buttonColourId, Theme::accent.withAlpha (0.6f));
        }
        else
        {
            b.setButtonText (cc >= 0 ? "CC " + juce::String (cc) : "Learn");
            b.setColour (juce::TextButton::buttonColourId, cc >= 0 ? Theme::outline : Theme::raised);
        }
        b.setTooltip (cc >= 0 ? RigRouting::controlName (control) + " is on CC " + juce::String (cc) + ", channel "
                                    + juce::String (channel) + ". Press Learn, then press it again to forget it."
                              : "Press, then move a control on your keyboard to use it for " + RigRouting::controlName (control) + ".");
    };
    show (learnPlay, RigRouting::play);
    show (learnStop, RigRouting::stop);
    const int faders[] = { RigRouting::mpcLevel, RigRouting::vocoderLevel, RigRouting::bassLevel, RigRouting::synthLevel,
                           RigRouting::samplerLevel, RigRouting::masterLevel };
    for (size_t i = 0; i < stripViews.size(); ++i)
        show (stripViews[i]->learn, faders[i]);
}

void RigView::timerCallback()
{
    auto& transport = engine.getTransport();
    const bool playing = transport.isPlaying();
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setButtonText (playing ? "Playing" : "Play");

    const int beatsPerBar = transport.getBeatsPerBar();
    const double beats = transport.getPositionInBeats();
    const int bar = (int) std::floor (beats / beatsPerBar) + 1;
    const int beat = (int) std::floor (std::fmod (beats, (double) beatsPerBar)) + 1;
    position.setText (juce::String (bar) + " . " + juce::String (beat), juce::dontSendNotification);

    auto& sampler = engine.getSampler();
    if (sampler.isRecording())
    {
        sampler.collectRecording();
        const int seconds = (int) sampler.getRecordedSeconds();
        sampleButton.setButtonText ("Stop " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2));
    }
    else if (sampleButton.getToggleState())
    {
        toggleSampling();   // the Sampler hit its length limit, or stopped in its own tab
    }

    if (learningKnob)
    {
        if (const auto knob = engine.getRouting().takeCaughtKnob(); knob.second >= 0)
            caughtKnob = knob;
        finishKnobLearn();
    }
    knobButton.setToggleState (learningKnob, juce::dontSendNotification);
    if (sayUntil != 0 && juce::Time::getMillisecondCounter() > sayUntil)
    {
        sayUntil = 0;
        knownInputs.clear();
        midiLight.setText ({}, juce::dontSendNotification);
        openMidiInputs();
    }

    if (engine.controlsMoved.exchange (false))
        refresh();
    else
        updateLearnButtons();

    for (auto& s : stripViews)
        s->tick();

    const bool recent = juce::Time::getMillisecondCounter() - engine.lastMidiTime.load() < 150;
    midiLight.setColour (juce::Label::textColourId, recent ? Theme::accent : Theme::textDim);

    if (++ticks % 60 == 0)
        openMidiInputs();
}

void RigView::toggleSampling()
{
    auto& sampler = engine.getSampler();
    if (! sampleButton.getToggleState())
    {
        sampler.stop();
        sampler.startRecording();
        sampleButton.setToggleState (true, juce::dontSendNotification);
        sampleButton.setButtonText ("Stop 0:00");
        return;
    }

    sampleButton.setToggleState (false, juce::dontSendNotification);
    sampleButton.setButtonText ("Sample");
    if (! sampler.isRecording())
        return;   // already stopped (and loaded) in the Sampler tab

    auto take = sampler.stopRecording();
    if (take.getNumSamples() < crate::samples (0.05))
        return;

    // Into the Sampler, ready to play from the keys or edit (and undoable there).
    sampler.getDocument().load (crate::share (std::move (take)),
                                engine.sampleSourceName (engine.sampleSource.load()) + " "
                                    + juce::Time::getCurrentTime().formatted ("%H.%M.%S"),
                                "Sample");
}

void RigView::say (const juce::String& text)
{
    midiLight.setText (text, juce::dontSendNotification);
    sayUntil = juce::Time::getMillisecondCounter() + 5000;
}

void RigView::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor* tool, int parameterIndex)
{
    // Called when a control is touched on screen (on the message thread for the Rig's own windows).
    if (! learningKnob)
        return;
    for (int t = 0; t < RigEngine::numTools; ++t)
        if (&engine.getTool (t) == tool)
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this), t, parameterIndex]
            {
                if (safe != nullptr && safe->learningKnob)
                {
                    safe->touchedTool = t;
                    safe->touchedParameter = parameterIndex;
                }
            });
        }
}

void RigView::finishKnobLearn()
{
    if (touchedTool < 0 || caughtKnob.second < 0)
    {
        if (! learningKnob)
            return;
        if (touchedTool >= 0)
            midiLight.setText ("Now turn a knob on your keyboard...", juce::dontSendNotification);
        else if (caughtKnob.second >= 0)
            midiLight.setText ("Got CC " + juce::String (caughtKnob.second) + ". Now touch a setting on screen...", juce::dontSendNotification);
        return;
    }

    auto& routing = engine.getRouting();
    routing.addParameterKnob (caughtKnob.first, caughtKnob.second, touchedTool, touchedParameter);
    const auto& list = engine.getTool (touchedTool).getParameters();
    const auto name = juce::isPositiveAndBelow (touchedParameter, list.size()) ? list[touchedParameter]->getName (40) : juce::String();
    say ("CC " + juce::String (caughtKnob.second) + " now controls " + RigEngine::toolName (touchedTool) + " " + name + ".");

    learningKnob = false;
    routing.stopCatching();
    touchedTool = touchedParameter = -1;
    caughtKnob = { 0, -1 };
}

void RigView::showKnobMenu()
{
    auto& routing = engine.getRouting();
    if (learningKnob)
    {
        learningKnob = false;
        routing.stopCatching();
        say ("Knob Learn cancelled.");
        return;
    }

    juce::PopupMenu menu;
    menu.addItem (1, "Learn a knob...");
    const auto knobs = routing.getParameterKnobs();
    if (! knobs.empty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("Click one to forget it");
        for (size_t i = 0; i < knobs.size(); ++i)
        {
            const auto& k = knobs[i];
            const auto& list = engine.getTool (k.tool).getParameters();
            const auto name = juce::isPositiveAndBelow (k.parameter, list.size()) ? list[k.parameter]->getName (40) : juce::String ("?");
            menu.addItem (100 + (int) i, "CC " + juce::String (k.cc) + "  >  " + RigEngine::toolName (k.tool) + " " + name);
        }
        menu.addSeparator();
        menu.addItem (2, "Forget all knobs");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (knobButton),
                        [safe = juce::Component::SafePointer<RigView> (this)] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& r = safe->engine.getRouting();
                            if (result == 1)
                            {
                                safe->learningKnob = true;
                                safe->touchedTool = safe->touchedParameter = -1;
                                safe->caughtKnob = { 0, -1 };
                                r.catchNextKnob();
                                safe->midiLight.setText ("Touch a setting on screen, then turn a knob...", juce::dontSendNotification);
                                safe->sayUntil = juce::Time::getMillisecondCounter() + 60000;
                            }
                            else if (result == 2)
                            {
                                r.clearParameterKnobs();
                                safe->say ("Forgot all learned knobs.");
                            }
                            else
                            {
                                r.removeParameterKnob (result - 100);
                                safe->say ("Forgot that knob.");
                            }
                        });
}

void RigView::openMidiInputs()
{
    const auto inputs = juce::MidiInput::getAvailableDevices();
    juce::StringArray names;
    for (const auto& input : inputs)
    {
        if (! devices.isMidiInputDeviceEnabled (input.identifier))
            devices.setMidiInputDeviceEnabled (input.identifier, true);
        names.add (input.name);
    }
    if (inputs != knownInputs || midiLight.getText().isEmpty())
    {
        knownInputs = inputs;
        midiLight.setText (names.isEmpty() ? "No MIDI keyboard found" : "MIDI: " + names.joinIntoString (", "), juce::dontSendNotification);
        midiLight.setTooltip (midiLight.getText());
    }
}

void RigView::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (devices, 0, 2, 1, 2, true, false, true, false);
    selector->setSize (520, 480);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector.release());
    options.dialogTitle = "Audio and MIDI";
    options.dialogBackgroundColour = Theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void RigView::saveShowAs()
{
    showsFolder().createDirectory();
    auto chooser = std::make_shared<juce::FileChooser> ("Save the show", showsFolder().getChildFile ("My show.crateshow"), "*.crateshow");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              file = file.withFileExtension ("crateshow");
                              if (auto xml = engine.saveShow().createXml())
                                  xml->writeTo (file);
                          });
}

void RigView::openShow()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open a show", showsFolder(), "*.crateshow");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              if (auto xml = juce::parseXML (file))
                              {
                                  engine.loadShow (juce::ValueTree::fromXml (*xml));
                                  refresh();
                              }
                          });
}

bool RigView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::spaceKey)
    {
        engine.getTransport().togglePlay();
        return true;
    }
    return false;
}

void RigView::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);
    auto bar = getLocalBounds().removeFromTop (56);
    g.setColour (Theme::panel);
    g.fillRect (bar);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("Rig", bar.reduced (14, 0).removeFromLeft (60), juce::Justification::centredLeft);

    auto bottom = getLocalBounds().removeFromBottom (kBottomHeight);
    g.setColour (Theme::panel);
    g.fillRect (bottom);
}

void RigView::resized()
{
    auto area = getLocalBounds();

    auto bar = area.removeFromTop (56).reduced (10, 10);
    bar.removeFromLeft (60);
    playButton.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (4);
    learnPlay.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (10);
    stopButton.setBounds (bar.removeFromLeft (70));
    bar.removeFromLeft (4);
    learnStop.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (16);
    position.setBounds (bar.removeFromLeft (100));
    bar.removeFromLeft (16);
    tempoLabel.setBounds (bar.removeFromLeft (52));
    tempo.setBounds (bar.removeFromLeft (170));
    bar.removeFromLeft (6);
    tapButton.setBounds (bar.removeFromLeft (52));
    bar.removeFromLeft (6);
    clickButton.setBounds (bar.removeFromLeft (64));
    bar.removeFromLeft (6);
    sampleButton.setBounds (bar.removeFromLeft (96));
    bar.removeFromLeft (6);
    knobButton.setBounds (bar.removeFromLeft (70));

    openButton.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (6);
    saveButton.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (6);
    audioButton.setBounds (bar.removeFromRight (120));

    auto bottom = area.removeFromBottom (kBottomHeight).reduced (10, 8);
    auto routing = bottom.removeFromLeft (320);
    const std::pair<juce::Label*, juce::ComboBox*> rows[] = { { &padsLabel, &padBank }, { &keysLabel, &keysTo },
                                                              { &splitLabel, &splitNote }, { &sourceLabel, &sampleSource } };
    for (auto [label, box] : rows)
    {
        auto row = routing.removeFromTop (28);
        label->setBounds (row.removeFromLeft (124));
        box->setBounds (row.removeFromLeft (box == &padBank ? 70 : box == &splitNote ? 82 : 120).reduced (0, 2));
        if (box == &splitNote)
            lowerKeysTo.setBounds (row.removeFromLeft (100).reduced (4, 2));
        routing.removeFromTop (1);
    }

    bottom.removeFromLeft (10);
    bottom = bottom.withSizeKeepingCentre (bottom.getWidth(), 70);
    const int count = (int) stripViews.size();
    const int w = (bottom.getWidth() - 10 * (count - 1)) / count;
    for (auto& s : stripViews)
    {
        s->setBounds (bottom.removeFromLeft (w));
        bottom.removeFromLeft (10);
    }

    tabs.setBounds (area);
    // Messages and the MIDI keyboards found, on the right of the tab bar.
    midiLight.setBounds (area.removeFromTop (34).removeFromRight (juce::jmax (200, area.getWidth() - 420)).reduced (8, 0));
    midiLight.setJustificationType (juce::Justification::centredRight);
    midiLight.toFront (false);
}
