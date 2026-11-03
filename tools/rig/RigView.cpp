#include "RigView.h"

namespace Theme = crate::Theme;

namespace
{
const juce::Colour mpcColour { 0xffff8a3d }, samplerColour { 0xff8fd3c8 }, masterColour { 0xffeeeeee };

juce::Colour stripColour (int strip)
{
    return strip == RigEngine::mpcStrip ? mpcColour : strip == RigEngine::samplerStrip ? samplerColour : masterColour;
}

juce::File showsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig");
}
}

//==============================================================================
RigView::StripView::StripView (RigEngine& e, int s, int fader, int muteC)
    : engine (e), strip (s), faderControl (fader), muteControl (muteC)
{
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
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
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
    learn.setBounds (top.removeFromRight (58));
    top.removeFromRight (6);
    mute.setBounds (top.removeFromRight (52));
    area.removeFromTop (4);
    level.setBounds (area.removeFromTop (26));
}

//==============================================================================
RigView::RigView (RigEngine& e, juce::AudioDeviceManager& d)
    : engine (e), devices (d)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &stopButton, &clickButton, &learnPlay, &learnStop,
                                                             &tempoLabel, &tempo, &tapButton, &position, &midiLight,
                                                             &audioButton, &saveButton, &openButton, &tabs,
                                                             &padsLabel, &padBank, &keysLabel, &keysTo })
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
    tabs.addTab ("Sampler", Theme::panel, engine.getSampler().createEditorAndMakeActive(), true);
    tabs.getTabbedButtonBar().setTabBackgroundColour (0, mpcColour.withAlpha (0.25f));
    tabs.getTabbedButtonBar().setTabBackgroundColour (1, samplerColour.withAlpha (0.25f));

    // Routing.
    for (auto* l : { &padsLabel, &keysLabel })
        l->setColour (juce::Label::textColourId, Theme::textDim);
    padBank.addItemList ({ "A", "B", "C", "D" }, 1);
    padBank.onChange = [this] { engine.getRouting().padBank = padBank.getSelectedItemIndex(); };
    padBank.setTooltip ("Your keyboard's 16 drum pads (MIDI channel 10) play these MPC pads.");
    keysTo.addItem ("Sampler", 1 + (int) RigRouting::Dest::sampler);
    keysTo.addItem ("MPC pads", 1 + (int) RigRouting::Dest::mpc);
    keysTo.addItem ("Nothing", 1 + (int) RigRouting::Dest::off);
    keysTo.onChange = [this] { engine.getRouting().keysTo = keysTo.getSelectedId() - 1; };
    keysTo.setTooltip ("What your keyboard's keys play.");

    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::mpcStrip, RigRouting::mpcLevel, RigRouting::mpcMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::samplerStrip, RigRouting::samplerLevel, RigRouting::samplerMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::masterStrip, RigRouting::masterLevel, -1));
    const int faders[] = { RigRouting::mpcLevel, RigRouting::samplerLevel, RigRouting::masterLevel };
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
    const int faders[] = { RigRouting::mpcLevel, RigRouting::samplerLevel, RigRouting::masterLevel };
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

    auto bottom = getLocalBounds().removeFromBottom (96);
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

    openButton.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (6);
    saveButton.setBounds (bar.removeFromRight (100));
    bar.removeFromRight (6);
    audioButton.setBounds (bar.removeFromRight (120));
    bar.removeFromRight (10);
    midiLight.setBounds (bar.reduced (6, 0));

    auto bottom = area.removeFromBottom (96).reduced (10, 10);
    auto routing = bottom.removeFromLeft (300);
    auto row1 = routing.removeFromTop (34);
    padsLabel.setBounds (row1.removeFromLeft (160));
    padBank.setBounds (row1.removeFromLeft (70).reduced (0, 3));
    routing.removeFromTop (8);
    auto row2 = routing.removeFromTop (34);
    keysLabel.setBounds (row2.removeFromLeft (160));
    keysTo.setBounds (row2.removeFromLeft (130).reduced (0, 3));

    bottom.removeFromLeft (10);
    const int w = (bottom.getWidth() - 20) / 3;
    for (auto& s : stripViews)
    {
        s->setBounds (bottom.removeFromLeft (w));
        bottom.removeFromLeft (10);
    }

    tabs.setBounds (area);
}
