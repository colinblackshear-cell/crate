#include "RigView.h"
#include "LooperView.h"
#include "SongView.h"
#include "StageView.h"
#include "GrooveView.h"
#include "SoundsView.h"
#include "RigColours.h"
#include "EditorView.h"
#include "MpcView.h"
#include "SamplerScreen.h"
#include "VocoderScreen.h"

namespace Theme = crate::Theme;

namespace
{
constexpr int kCheek = 26;   // the walnut cheeks either side
const juce::Colour mpcColour = RigColours::mpc, samplerColour = RigColours::sampler, vocoderColour = RigColours::vocoder,
                   bassColour = RigColours::bass, synthColour = RigColours::synth, loopsColour = RigColours::loops;

juce::Colour stripColour (int strip) { return RigColours::forStrip (strip); }

juce::File showsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig");
}
}

//==============================================================================
// A tool in its own window. Closing it puts the tool back in its tab.
class RigView::ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (RigView& v, int p, juce::Component& content)
        : DocumentWindow ("Crate Rig - " + v.pages[(size_t) p].name, Theme::panel, DocumentWindow::closeButton | DocumentWindow::maximiseButton),
          view (v), page (p)
    {
        setUsingNativeTitleBar (true);
        setContentNonOwned (&content, false);
        setResizable (true, false);
        setResizeLimits (400, 300, 6000, 4000);
    }

    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (&view), p = page]
                                         { if (safe != nullptr) safe->dockBack (p); });
    }

    // The space bar and the looper's keys work in every window.
    bool keyPressed (const juce::KeyPress& key) override { return view.keyPressed (key); }

    void moved() override { DocumentWindow::moved(); view.storeWindows(); }
    void resized() override { DocumentWindow::resized(); view.storeWindows(); }

private:
    RigView& view;
    const int page;
};

// What a tab shows while its tool is in its own window.
class RigView::Placeholder : public juce::Component
{
public:
    Placeholder (RigView& v, int p) : view (v), page (p)
    {
        for (auto* b : { &show, &back })
            addAndMakeVisible (b);
        show.onClick = [this] { if (auto& w = view.pages[(size_t) page].window) w->toFront (true); };
        back.onClick = [this] { view.dockBack (page); };
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (view.pages[(size_t) page].name + " is in its own window.",
                    getLocalBounds().withSizeKeepingCentre (600, 30).translated (0, -30), juce::Justification::centred);
    }

    void resized() override
    {
        auto row = getLocalBounds().withSizeKeepingCentre (280, 32).translated (0, 16);
        show.setBounds (row.removeFromLeft (136));
        row.removeFromLeft (8);
        back.setBounds (row);
    }

private:
    RigView& view;
    const int page;
    juce::TextButton show { "Show it" }, back { "Bring it back here" };
};

// The Mixer page: lays its parts out through the view, which owns them.
class RigView::MixerPage : public juce::Component
{
public:
    explicit MixerPage (RigView& v) : view (v) {}
    void paint (juce::Graphics& g) override
    {
        g.fillAll (Theme::panel);
        g.setColour (Theme::textDim);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("TOOLS", getLocalBounds().reduced (14, 0).removeFromTop (22), juce::Justification::bottomLeft);
    }
    void resized() override { view.layoutMixer (getLocalBounds()); }

private:
    RigView& view;
};

//==============================================================================
RigView::StripView::StripView (RigEngine& e, int s, int fader, int muteC, std::atomic<bool>* onSwitch)
    : strip (s), engine (e), faderControl (fader), muteControl (muteC), onFlag (onSwitch)
{
    addAndMakeVisible (fxButton);
    fxButton.setColour (juce::TextButton::buttonOnColourId, stripColour (strip).withAlpha (0.8f));
    fxButton.setTooltip ("Effects on this strip: filter sweep, drive, crush, delay, reverb and beat repeat. Lit when any are on.");
    if (onFlag != nullptr)
    {
        addAndMakeVisible (on);
        on.setClickingTogglesState (true);
        on.setColour (juce::TextButton::buttonOnColourId, stripColour (strip));
        on.setTooltip (strip == RigEngine::inputStrip
                           ? "Plays your audio input (a guitar, another mic) through this strip and its effects. "
                             "Off until you turn it on, so a mic doesn't feed back."
                           : "Turns the vocoder on or off. Off, it's silent and uses no processor time.");
        on.onClick = [this] { *onFlag = on.getToggleState(); refresh(); };
    }
    if (strip == RigEngine::inputStrip)
    {
        addAndMakeVisible (jack);
        for (int i = 0; i < 3; ++i)
            jack.addItem (RigEngine::inputJackName (i), i + 1);
        jack.onChange = [this] { engine.inputJack = jack.getSelectedId() - 1; };
        jack.setTooltip ("Which input of your audio interface this strip plays: both, or just input 1 or 2 "
                         "(e.g. the mic on 1 for the vocoder and a guitar on 2).");
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
    if (onFlag != nullptr)
    {
        on.setToggleState (onFlag->load(), juce::dontSendNotification);
        on.setButtonText (onFlag->load() ? "On" : "Off");
    }
    if (strip == RigEngine::inputStrip)
        jack.setSelectedId (engine.inputJack.load() + 1, juce::dontSendNotification);
}

void RigView::StripView::tick (float meterLevel)
{
    fxButton.setToggleState (engine.getFx (strip).isActive(), juce::dontSendNotification);
    meter = meterLevel;
    repaint();
}

void RigView::StripView::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (area, 6.0f);

    auto top = getLocalBounds().reduced (6, 6).removeFromTop (20);
    g.setColour (stripColour (strip));
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (RigEngine::stripName (strip), top, juce::Justification::centredLeft);

    // The meter, under the fader.
    auto meterArea = getLocalBounds().reduced (6, 0).withTop (level.getBottom()).withHeight (6).toFloat();
    g.setColour (Theme::background);
    g.fillRoundedRectangle (meterArea, 3.0f);
    const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
    const float fill = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (meter > 0.98f ? Theme::record : stripColour (strip).withAlpha (0.9f));
    g.fillRoundedRectangle (meterArea.withWidth (meterArea.getWidth() * fill), 3.0f);
}

void RigView::StripView::resized()
{
    // Name, On and the input's jack / Mute, Learn, FX / the fader and meter.
    auto area = getLocalBounds().reduced (6, 6);
    auto top = area.removeFromTop (20);
    if (jack.isVisible())
        jack.setBounds (top.removeFromRight (48));
    if (on.isVisible())
    {
        top.removeFromRight (2);
        on.setBounds (top.removeFromRight (32));
    }
    area.removeFromTop (4);
    auto buttons = area.removeFromTop (22);
    std::vector<juce::Button*> row;
    for (auto* b : std::initializer_list<juce::Button*> { &mute, &learn, &fxButton })
        if (b->isVisible())
            row.push_back (b);
    const int w = (buttons.getWidth() - 3 * ((int) row.size() - 1)) / (int) row.size();
    for (auto* b : row)
    {
        b->setBounds (buttons.removeFromLeft (w));
        buttons.removeFromLeft (3);
    }
    area.removeFromTop (2);
    level.setBounds (area.removeFromTop (24));
}

//==============================================================================
RigView::FxPanel::FxPanel (RigEngine& engine, int s)
    : StripFxPanel (engine.getFx (s), RigEngine::stripName (s) + " effects", stripColour (s)), strip (s)
{
}

//==============================================================================
void RigView::showFx (int strip)
{
    if (fxPanel != nullptr && fxPanel->strip == strip)
        fxPanel.reset();
    else
    {
        fxPanel = std::make_unique<FxPanel> (engine, strip);
        fxPanel->closeButton.onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
                                                                                 { if (safe != nullptr) { safe->fxPanel.reset(); safe->mixerPage->resized(); } }); };
        mixerPage->addAndMakeVisible (*fxPanel);
    }
    mixerPage->resized();
}

//==============================================================================
//==============================================================================
RigView::RigView (RigEngine& e, juce::AudioDeviceManager* d)
    : engine (e), devices (d), header (d != nullptr), footer (e)
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    vintage::setStage ((bool) engine.screens.getProperty ("dark", false));

    for (auto* c : std::initializer_list<juce::Component*> { &header, &screenBar, &footer })
    {
        c->setLookAndFeel (&vintageLook);
        addAndMakeVisible (c);
    }
    addAndMakeVisible (tabs);

    auto& transport = engine.getTransport();

    // The top bar.
    header.play.onClick = [&transport] { transport.togglePlay(); };
    header.stop.onClick = [this, &transport]
    {
        transport.stop();
        engine.allNotesOff();   // Stop always means quiet: no hanging notes or latched arps
    };
    header.rec.onClick = [this] { toggleRecordSet(); };
    header.up.onClick = [this] { nudgeTempo (juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? 0.1 : 1.0); };
    header.down.onClick = [this] { nudgeTempo (juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? -0.1 : -1.0); };
    header.up.setRepeatSpeed (400, 80);
    header.down.setRepeatSpeed (400, 80);
    header.onMic = [this]
    {
        engine.micOn = ! engine.micOn.load();
        refresh();
        if (engine.micOn.load())
            say ("Mic on: use headphones, or your speakers can feed back into the mic.");
    };
    header.tap.onClick = [this]
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
            engine.getTransport().setBpm (std::round (60000.0 / beat * 2.0) / 2.0);
        }
    };

    // The screens, and More.
    screenBar.onScreen = [this] (const juce::String& page) { showTab (page); };
    screenBar.more.onClick = [this] { showMoreMenu(); };

    // The song parts and Live now.
    footer.onTrack = [this] (int strip)
    {
        showTab ("Groove");
        if (groove != nullptr)
            for (int t = 0; t < GrooveView::numTracks; ++t)
                if (GrooveView::stripFor (t) == strip)
                    groove->selectTrack (t);
    };
    footer.onEditParts = [this] { showTab ("Song"); };

    // The Mixer page's parts.
    mixerPage = new MixerPage (*this);
    for (auto* c : std::initializer_list<juce::Component*> { &clickButton, &micButton, &learnPlay, &learnStop, &learnPlayLabel, &learnStopLabel,
                                                             &sampleButton, &knobButton, &audioButton, &saveButton, &openButton,
                                                             &padsLabel, &padBank, &keysLabel, &keysTo,
                                                             &sourceLabel, &sampleSource, &sampleDest, &sampleBars, &splitLabel, &splitNote, &lowerKeysTo })
        mixerPage->addAndMakeVisible (c);

    clickButton.setClickingTogglesState (true);
    clickButton.onClick = [this, &transport] { transport.metronome = clickButton.getToggleState(); };
    clickButton.setTooltip ("A metronome click on every beat while playing.");
    micButton.setClickingTogglesState (true);
    micButton.onClick = [this]
    {
        engine.micOn = micButton.getToggleState();
        refresh();
        if (engine.micOn.load())
            say ("Mic on: use headphones, or your speakers can feed back into the mic.");
    };
    micButton.setTooltip ("Hear your mic (the vocoder and the Input strip). Use headphones with it on, so the speakers don't feed back. "
                          "Sampling and looping from the mic work either way. The Mic lamp at the top right switches it too.");
    learnPlay.setTooltip ("Press, then press a button on your keyboard to make it start and stop the Rig.");
    learnStop.setTooltip ("Press, then press a button on your keyboard to make it stop the Rig.");
    learnPlay.onClick = [this] { startLearning (RigRouting::play, learnPlay); };
    learnStop.onClick = [this] { startLearning (RigRouting::stop, learnStop); };
    for (auto* l : { &learnPlayLabel, &learnStopLabel })
        l->setColour (juce::Label::textColourId, Theme::textDim);

    sampleButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
    sampleButton.setTooltip ("Records one tool into another, as picked beside it: the mic, a tool or the "
                             "Rig mix, into the Sampler or onto the next empty MPC pad. It starts at the next bar (now, if the "
                             "Rig is stopped) and stops after the bars picked, or when you press again.");
    sampleButton.onClick = [this] { toggleSampling(); };

    knobButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    knobButton.setTooltip ("Put a knob of your keyboard on any setting: choose Learn a knob, touch the setting on screen "
                           "(the bass's Cutoff, the synth's LFO depth...), then turn the knob.");
    knobButton.onClick = [this] { showKnobMenu(); };

    audioButton.onClick = [this] { showAudioSettings(); };
    saveButton.onClick = [this] { saveShowAs(); };
    openButton.onClick = [this] { openShow(); };

    // The five screens first (F1-F5), then what More shows. The tab bar is hidden: the screen
    // buttons and More pick the page.
    tabs.setTabBarDepth (0);
    tabs.setOutline (0);
    auto* grooveView = new GrooveView (engine);
    grooveView->setLookAndFeel (&vintageLook);
    grooveView->onOpen = [this] (const juce::String& name) { showTab (name); };
    grooveView->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
    grooveView->onSample = [this] { toggleSampling(); };
    grooveView->isSampling = [this] { return engine.getCapture().getState() != RigCapture::State::idle; };
    grooveView->showMic = devices != nullptr;
    grooveView->refresh();
    groove = grooveView;
    addPage ("Groove", RigColours::song, grooveView);
    {
        // The pedalboard (Easy), over the vocoder's own window (Studio).
        auto* screen = new VocoderScreen (engine, engine.getVocoder().createEditorAndMakeActive(), devices != nullptr);
        screen->setLookAndFeel (&vintageLook);
        screen->onSay = [this] (const juce::String& text) { say (text); };
        screen->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
        screen->onRecVoice = [this] { toggleSampling(); };
        screen->isSampling = [this] { return engine.getCapture().getState() != RigCapture::State::idle; };
        screen->onMic = [this] { if (header.onMic) header.onMic(); };
        vocoderScreen = screen;
        addPage ("Vocoder", vocoderColour, screen);
    }
    auto* looperView = new LooperView (engine);
    looperView->setLookAndFeel (&vintageLook);
    looperView->onSay = [this] (const juce::String& text) { say (text); };
    looper = looperView;
    addPage ("Looper", loopsColour, looperView);
    // The Sampler · MPC screen, over the full sample editor (a page of its own, in More).
    auto* samplerView = engine.getSampler().createEditorAndMakeActive();
    auto* samplerEditor = dynamic_cast<EditorView*> (samplerView);
    // Smart chop's "To MPC pads": the slices go straight onto the MPC's pads
    if (samplerEditor != nullptr)
        samplerEditor->setSendSlicesToPads ([this] (std::vector<crate::SharedAudio> slices, const juce::String& name)
                                            { return engine.sendSlicesToMpc (std::move (slices), name); });
    if (samplerEditor != nullptr)
    {
        auto* screen = new SamplerScreen (engine, *samplerEditor);
        screen->setLookAndFeel (&vintageLook);
        screen->onSay = [this] (const juce::String& text) { say (text); };
        screen->onOpen = [this] (const juce::String& name) { showTab (name); };
        screen->onSendToMpc = [this]
        {
            if (auto* mpcView = dynamic_cast<MpcView*> (pages[(size_t) pageIndex ("MPC")].content.get()))
                mpcView->exportToCard();
        };
        samplerScreen = screen;
        addPage ("Sampler", samplerColour, screen);
    }
    auto* stageView = new StageView (engine);
    stageView->onOpen = [this] (const juce::String& name) { showTab (name); };
    stageView->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
    stageView->onRecord = [this] { toggleRecordSet(); };
    stageView->onTap = [this] { header.tap.onClick(); };
    stageView->onNudgeTempo = [this] (double amount) { nudgeTempo (amount); };
    stageView->recordState = [this]
    {
        // The Stage shows "armed" as a red ring, which it asks for as Lit::next.
        const auto state = engine.getRecorder().getState();
        return std::pair<juce::String, vintage::Lit> (header.rec.text, state == RigRecorder::State::armed ? vintage::Lit::next
                                                                      : state == RigRecorder::State::recording ? vintage::Lit::arm
                                                                                                               : vintage::Lit::none);
    };
    stage = stageView;
    addPage ("Stage", Theme::text, stageView);
    addPage ("MPC", mpcColour, engine.getMpc().createEditorAndMakeActive());
    addPage (samplerEditor != nullptr ? "Sampler editor" : "Sampler", samplerColour, samplerView);
    addPage ("Bass", bassColour, engine.getBass().createEditorAndMakeActive());
    addPage ("Synth", synthColour, engine.getSynth().createEditorAndMakeActive());
    auto* songView = new SongView (engine);
    songView->onSay = [this] (const juce::String& text) { say (text); };
    song = songView;
    addPage ("Song", RigColours::song, songView);
    auto* soundsView = new SoundsView (engine);
    soundsView->onSay = [this] (const juce::String& text) { say (text); };
    addPage ("Sounds", juce::Colour (0xffa1887f), soundsView);
    addPage ("Mixer", Theme::text, mixerPage);
    screenBar.setCurrent ("Groove");

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
    for (int i = 0; i < RigCapture::numSources; ++i)
        sampleSource.addItem (RigCapture::sourceName (i), i + 1);
    sampleSource.onChange = [this]
    {
        engine.captureSource = sampleSource.getSelectedId() - 1;
        // The Sampler's own Record button (in its tab) hears the same, where it can.
        switch (engine.captureSource)
        {
            case RigCapture::mic:     engine.sampleSource = (int) RigEngine::SampleSource::mic; break;
            case RigCapture::vocoder: engine.sampleSource = (int) RigEngine::SampleSource::vocoder; break;
            case RigCapture::mix:     engine.sampleSource = (int) RigEngine::SampleSource::mix; break;
            default: break;
        }
    };
    sampleSource.setTooltip ("What Sample records: the mic, any tool as you hear it (after its effects and level), or the whole Rig mix.");
    sampleDest.addItem ("into Sampler", RigCapture::toSampler + 1);
    sampleDest.addItem ("onto MPC pad", RigCapture::toMpcPad + 1);
    sampleDest.onChange = [this] { engine.captureDest = sampleDest.getSelectedId() - 1; };
    sampleDest.setTooltip ("Where it goes: the Sampler (play it from the keys, chop it), or the next empty pad of the MPC "
                           "(in the bank your keyboard's pads play).");
    for (int bars : RigCapture::barChoices())
        sampleBars.addItem (bars == 0 ? "Free" : juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars + 1);
    sampleBars.onChange = [this] { engine.captureBars = sampleBars.getSelectedId() - 1; };
    sampleBars.setTooltip ("How long: a number of bars (it stops by itself, in time, ready to loop), or Free (until you press Sample again).");

    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::mpcStrip, RigRouting::mpcLevel, RigRouting::mpcMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::vocoderStrip, RigRouting::vocoderLevel, RigRouting::vocoderMute, &engine.vocoderOn));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::bassStrip, RigRouting::bassLevel, RigRouting::bassMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::synthStrip, RigRouting::synthLevel, RigRouting::synthMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::inputStrip, RigRouting::inputLevel, RigRouting::inputMute, &engine.inputOn));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::samplerStrip, RigRouting::samplerLevel, RigRouting::samplerMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::loopsStrip, RigRouting::loopsLevel, RigRouting::loopsMute));
    stripViews.push_back (std::make_unique<StripView> (engine, RigEngine::masterStrip, RigRouting::masterLevel, -1));
    const int faders[] = { RigRouting::mpcLevel, RigRouting::vocoderLevel, RigRouting::bassLevel, RigRouting::synthLevel,
                           RigRouting::inputLevel, RigRouting::samplerLevel, RigRouting::loopsLevel, RigRouting::masterLevel };
    for (size_t i = 0; i < stripViews.size(); ++i)
    {
        auto* s = stripViews[i].get();
        mixerPage->addAndMakeVisible (s);
        const int control = faders[i];
        s->learn.onClick = [this, s, control] { startLearning (control, s->learn); };
        s->fxButton.onClick = [this, s] { showFx (s->strip); };
    }

    openMidiInputs();
    refresh();
    startTimerHz (30);
    // Put popped-out tools back on their screens, once this window is up.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
                                     { if (safe != nullptr) safe->restoreWindows(); });
}

RigView::~RigView()
{
    for (int t = 0; t < RigEngine::numTools; ++t)
        engine.getTool (t).removeListener (this);
    fxPanel.reset();
    for (auto& p : pages)
        p.window.reset();   // they only show the pages
    tabs.clearTabs();
    pages.clear();
    setLookAndFeel (nullptr);
}

void RigView::refresh()
{
    if ((bool) engine.screens.getProperty ("dark", false) != vintage::isStage())
        setDarkPanel ((bool) engine.screens.getProperty ("dark", false));
    auto& transport = engine.getTransport();
    clickButton.setToggleState (transport.metronome.load(), juce::dontSendNotification);
    micButton.setToggleState (engine.micOn.load(), juce::dontSendNotification);
    micButton.setButtonText (engine.micOn.load() ? "Mic on" : "Mic off");
    micButton.setVisible (devices != nullptr);
    padBank.setSelectedItemIndex (engine.getRouting().padBank.load(), juce::dontSendNotification);
    keysTo.setSelectedId (engine.getRouting().keysTo.load() + 1, juce::dontSendNotification);
    sampleSource.setSelectedId (engine.captureSource + 1, juce::dontSendNotification);
    sampleDest.setSelectedId (engine.captureDest + 1, juce::dontSendNotification);
    sampleBars.setSelectedId (engine.captureBars + 1, juce::dontSendNotification);
    lowerKeysTo.setSelectedId (engine.getRouting().lowerKeysTo.load() + 1, juce::dontSendNotification);
    const int split = engine.getRouting().splitNote.load();
    splitNote.setSelectedId (split > 0 ? split + 1 : 1, juce::dontSendNotification);
    lowerKeysTo.setEnabled (split > 0);
    for (auto& s : stripViews)
        s->refresh();
    if (looper != nullptr)
        looper->refresh();
    if (song != nullptr)
        song->refresh();
    if (stage != nullptr)
        stage->refresh();
    if (samplerScreen != nullptr)
        samplerScreen->refresh();
    if (vocoderScreen != nullptr)
        vocoderScreen->refresh();
    if (groove != nullptr)
        groove->refresh();
    updateLearnButtons();
}

void RigView::showTab (const juce::String& name)
{
    const int i = pageIndex (name);
    if (i < 0)
        return;
    if (pages[(size_t) i].window != nullptr)
        pages[(size_t) i].window->toFront (true);
    else
        tabs.setCurrentTabIndex (i);
    pageChanged();
}

// The Stage fills the window on a black panel: no top bar or footer (it has its own), and the
// screen buttons go dark too. Leaving it puts the panel back as it was.
void RigView::pageChanged()
{
    const bool onStage = currentPage() == "Stage";
    if (onStage == stageShown)
        return;
    stageShown = onStage;
    vintage::setStage (onStage || (bool) engine.screens.getProperty ("dark", false));
    header.setVisible (! onStage);
    footer.setVisible (! onStage);
    resized();
    repaint();
    for (auto* c : std::initializer_list<juce::Component*> { &header, &screenBar, &footer })
        c->repaint();
    if (groove != nullptr)
        groove->repaint();
    if (stage != nullptr)
        stage->repaint();
}

void RigView::popOutTab (const juce::String& tab)
{
    if (const int i = pageIndex (tab); i >= 0)
        popOut (i);
}

void RigView::bringBack (const juce::String& tab)
{
    if (const int i = pageIndex (tab); i >= 0)
        dockBack (i);
}

bool RigView::isInOwnWindow (const juce::String& tab) const
{
    const int i = pageIndex (tab);
    return i >= 0 && pages[(size_t) i].window != nullptr;
}

void RigView::addPage (const juce::String& name, juce::Colour colour, juce::Component* content)
{
    Page p;
    p.name = name;
    p.colour = colour;
    p.content.reset (content);
    pages.push_back (std::move (p));
    const int i = (int) pages.size() - 1;
    tabs.addTab (name, Theme::panel, content, false);
    tabs.getTabbedButtonBar().setTabBackgroundColour (i, colour.withAlpha (0.25f));
}

int RigView::pageIndex (const juce::String& name) const
{
    for (size_t i = 0; i < pages.size(); ++i)
        if (pages[i].name == name)
            return (int) i;
    return -1;
}

void RigView::popOut (int index, juce::Rectangle<int> bounds)
{
    auto& page = pages[(size_t) index];
    if (page.window != nullptr)
        return;

    // The tab keeps its place, showing where the tool went.
    const int current = tabs.getCurrentTabIndex();
    const auto size = tabs.getLocalBounds().withTrimmedTop (tabs.getTabBarDepth());
    page.placeholder = std::make_unique<Placeholder> (*this, index);
    tabs.removeTab (index);
    tabs.addTab (page.name, Theme::panel, page.placeholder.get(), false, index);
    tabs.getTabbedButtonBar().setTabBackgroundColour (index, page.colour.withAlpha (0.25f));
    tabs.setCurrentTabIndex (current);

    page.window = std::make_unique<ToolWindow> (*this, index, *page.content);
    page.content->setVisible (true);

    // Where it goes: where it was last time, else onto a second screen if there is one, else
    // over this window.
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    auto onScreen = [&displays] (juce::Rectangle<int> r)
    {
        const auto* d = displays.getDisplayForRect (r);
        return d != nullptr && d->userBounds.toNearestInt().getIntersection (r).getWidth() >= 100;
    };
    if (bounds.isEmpty() || ! onScreen (bounds))
    {
        bounds = {};
        const auto here = getScreenBounds();
        for (const auto& d : displays.displays)
            if (! d.userBounds.toNearestInt().intersects (here))
            {
                bounds = d.userBounds.toNearestInt().reduced (20);
                break;
            }
        if (bounds.isEmpty())
        {
            bounds = juce::Rectangle<int> (juce::jmax (800, size.getWidth() - 120), juce::jmax (560, size.getHeight()))
                         .withPosition (here.getPosition() + juce::Point<int> (60, 60));
            if (const auto* d = displays.getDisplayForRect (here))
                bounds = bounds.constrainedWithin (d->userBounds.toNearestInt());
        }
    }
    page.window->setBounds (bounds);
    page.window->setVisible (true);
    resized();
    storeWindows();
}

void RigView::dockBack (int index)
{
    auto& page = pages[(size_t) index];
    if (page.window == nullptr)
        return;
    page.window->clearContentComponent();
    page.window.reset();

    const int current = tabs.getCurrentTabIndex();
    tabs.removeTab (index);
    tabs.addTab (page.name, Theme::panel, page.content.get(), false, index);
    tabs.getTabbedButtonBar().setTabBackgroundColour (index, page.colour.withAlpha (0.25f));
    tabs.setCurrentTabIndex (current);
    page.placeholder.reset();
    resized();
    storeWindows();
}

void RigView::storeWindows()
{
    auto& screens = engine.screens;
    screens.removeAllChildren (nullptr);
    for (const auto& p : pages)
        if (p.window != nullptr)
        {
            juce::ValueTree w ("Window");
            w.setProperty ("tab", p.name, nullptr);
            w.setProperty ("bounds", p.window->getBounds().toString(), nullptr);
            screens.appendChild (w, nullptr);
        }
}

void RigView::restoreWindows()
{
    const auto wanted = engine.screens.createCopy();
    for (int i = 0; i < (int) pages.size(); ++i)
        if (! wanted.getChildWithProperty ("tab", pages[(size_t) i].name).isValid())
            dockBack (i);
    for (const auto& w : wanted)
        if (const int i = pageIndex (w.getProperty ("tab").toString()); i >= 0)
        {
            const auto bounds = juce::Rectangle<int>::fromString (w.getProperty ("bounds").toString());
            if (pages[(size_t) i].window != nullptr)
                pages[(size_t) i].window->setBounds (bounds);
            else
                popOut (i, bounds);
        }
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
                           RigRouting::inputLevel, RigRouting::samplerLevel, RigRouting::loopsLevel, RigRouting::masterLevel };
    for (size_t i = 0; i < stripViews.size(); ++i)
        show (stripViews[i]->learn, faders[i]);
}

void RigView::timerCallback()
{
    auto& transport = engine.getTransport();
    const bool playing = transport.isPlaying();
    if (header.play.lit != (playing ? vintage::Lit::play : vintage::Lit::none))
    {
        header.play.lit = playing ? vintage::Lit::play : vintage::Lit::none;
        header.play.text = playing ? "Playing" : "Play";
        header.play.repaint();
    }
    screenBar.setCurrent (currentPage());
    pageChanged();

    // The Main screen sends the keys to the track you pick.
    if (keysTo.getSelectedId() != engine.getRouting().keysTo.load() + 1)
        keysTo.setSelectedId (engine.getRouting().keysTo.load() + 1, juce::dontSendNotification);

    if (const auto done = engine.serviceCapture(); done.isNotEmpty())
        say (done);
    updateSampleButton();

    if (fxPanel != nullptr)
        fxPanel->tick();
    engine.tickScenes();
    if (engine.serviceRecorder())
    {
        const auto& files = engine.getRecorder().getSavedFiles();
        say ("Saved the set: " + juce::String (juce::jmax (0, files.size() - 1)) + " files in Recordings/"
             + engine.getRecorder().getFolder().getFileName() + ". Open in Logic.txt there says how.");
    }
    updateRecordButton();

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
        footer.message.setText ({}, juce::dontSendNotification);
        openMidiInputs();
    }

    if (engine.controlsMoved.exchange (false))
        refresh();
    else
        updateLearnButtons();

    for (int st = 0; st < RigEngine::numStrips; ++st)
    {
        const float peak = engine.getStrip (st).peak.exchange (0.0f);
        meters[(size_t) st] = juce::jmax (peak, meters[(size_t) st] * 0.85f);
    }
    for (auto& s : stripViews)
        s->tick (meters[(size_t) s->strip]);

    const bool recent = juce::Time::getMillisecondCounter() - engine.lastMidiTime.load() < 150;
    keyboardActive = recent;

    // The top bar: tempo, where the song is, the master meters, what's plugged in.
    {
        rigpanels::Header::Shown shown;
        shown.bpm = transport.getBpm();
        const int beatsPerBar = transport.getBeatsPerBar();
        const double beats = transport.getPositionInBeats();
        const int bar = (int) std::floor (beats / beatsPerBar) + 1;
        const int beat = (int) std::floor (std::fmod (beats, (double) beatsPerBar)) + 1;
        shown.barBeat = juce::String (bar).paddedLeft ('0', 3) + "." + juce::String (beat);
        auto& scenes = engine.getScenes();
        const auto& list = scenes.getScenes();
        const int current = scenes.getCurrent(), queued = scenes.getQueued();
        auto nameOf = [&list] (int i)
        {
            const auto letter = juce::String::charToString ((juce::juce_wchar) ('A' + i));
            const auto& n = list[(size_t) i].name;
            return n == "Part " + letter ? juce::String ("Part ") + letter : n;
        };
        if (juce::isPositiveAndBelow (current, (int) list.size()) && current < 26)
        {
            shown.partLetter = juce::String::charToString ((juce::juce_wchar) ('A' + current));
            shown.partName = nameOf (current);
        }
        if (juce::isPositiveAndBelow (queued, (int) list.size()) && queued < 26)
        {
            shown.partQueued = true;
            shown.partName = "Next: " + juce::String::charToString ((juce::juce_wchar) ('A' + queued)) + " " + nameOf (queued);
        }
        else
            shown.partName = (shown.partName.isNotEmpty() ? shown.partName + juce::String::fromUTF8 (" \xc2\xb7 ") : juce::String())
                           + "Bar.Beat";
        // VU: -20 dB at the left end, 0 at three quarters, +3 at the right.
        auto vu = [] (float gain)
        {
            const float db = juce::Decibels::gainToDecibels (gain, -40.0f);
            return juce::jlimit (0.0f, 1.04f, (db + 20.0f) / 23.0f);
        };
        vuLevel = vuLevel * 0.7f + vu (meters[(size_t) RigEngine::masterStrip]) * 0.3f;
        shown.vuLeft = shown.vuRight = vuLevel;
        shown.keyboard = keyboardName;
        shown.keyboardFound = keyboardFound;
        shown.keyboardActive = keyboardActive;
        shown.macKeys = devices != nullptr || recent;
        shown.micOn = devices != nullptr ? engine.micOn.load() : true;
        header.update (shown);
    }
    footer.tick ([this] (int strip) { return meters[(size_t) strip]; });

    if (++ticks % 60 == 0)
        openMidiInputs();
}

void RigView::toggleSampling()
{
    auto& capture = engine.getCapture();
    if (capture.getState() == RigCapture::State::idle)
    {
        if (engine.startCapture (engine.captureSource, engine.captureBars) && engine.getTransport().isPlaying())
            say ("Sampling " + RigCapture::sourceName (engine.captureSource) + " from the next bar...");
    }
    else
    {
        engine.stopCapture();
    }
    updateSampleButton();
}

void RigView::updateSampleButton()
{
    const auto& capture = engine.getCapture();
    const auto state = capture.getState();
    juce::String text = "Sample";
    if (state == RigCapture::State::armed)
        text = "Next bar...";
    else if (state == RigCapture::State::recording)
    {
        const int seconds = (int) capture.getRecordedSeconds();
        text = "Stop " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2);
    }
    sampleButton.setToggleState (state == RigCapture::State::recording
                                     || (state == RigCapture::State::armed && (juce::Time::getMillisecondCounter() / 400) % 2 == 0),
                                 juce::dontSendNotification);
    if (sampleButton.getButtonText() != text)
        sampleButton.setButtonText (text);
}

void RigView::toggleRecordSet()
{
    auto& recorder = engine.getRecorder();
    if (recorder.getState() == RigRecorder::State::idle)
    {
        const auto folder = RigEngine::recordingsFolder().getChildFile (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"));
        if (! engine.startRecording (folder))
            say ("Couldn't start recording in " + folder.getFullPathName() + ".");
        else
            say (engine.getTransport().isPlaying() ? "Recording the set from the next bar..." : "Recording the set when you press Play...");
    }
    else
    {
        engine.stopRecording();
    }
    updateRecordButton();
}

void RigView::updateRecordButton()
{
    auto& recorder = engine.getRecorder();
    const auto state = recorder.getState();
    juce::String text = "Rec";
    if (state == RigRecorder::State::armed)
        text = juce::String::fromUTF8 ("Rec \xc2\xb7 Armed");
    else if (state == RigRecorder::State::recording)
    {
        const int seconds = (int) recorder.getRecordedSeconds();
        text = "Stop " + juce::String (seconds / 60) + ":" + juce::String (seconds % 60).paddedLeft ('0', 2);
    }
    else if (state == RigRecorder::State::finishing)
        text = "Saving...";
    // Armed: a red ring (it records from the next bar). Recording: lit red.
    const auto lit = state == RigRecorder::State::recording ? vintage::Lit::arm : vintage::Lit::none;
    const bool ring = state == RigRecorder::State::armed;
    if (header.rec.text != text || header.rec.lit != lit || header.rec.redRing != ring)
    {
        header.rec.text = text;
        header.rec.lit = lit;
        header.rec.redRing = ring;
        header.rec.repaint();
    }
}

void RigView::say (const juce::String& text)
{
    footer.message.setText (text, juce::dontSendNotification);
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
            footer.message.setText ("Now turn a knob on your keyboard...", juce::dontSendNotification);
        else if (caughtKnob.second >= 0)
            footer.message.setText ("Got CC " + juce::String (caughtKnob.second) + ". Now touch a setting on screen...", juce::dontSendNotification);
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
                                safe->footer.message.setText ("Touch a setting on screen, then turn a knob...", juce::dontSendNotification);
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
    if (devices == nullptr)
    {
        // In Logic, the keys come from the track.
        keyboardName = "Logic track";
        keyboardFound = true;
        return;
    }
    const auto inputs = juce::MidiInput::getAvailableDevices();
    juce::StringArray names;
    for (const auto& input : inputs)
    {
        if (! devices->isMidiInputDeviceEnabled (input.identifier))
            devices->setMidiInputDeviceEnabled (input.identifier, true);
        names.add (input.name);
    }
    if (inputs != knownInputs || keyboardName.isEmpty())
    {
        knownInputs = inputs;
        // The top bar names the keyboard: a Launchkey if there is one, else the first found.
        juce::String shown;
        for (const auto& n : names)
            if (n.containsIgnoreCase ("launchkey"))
                shown = n.containsIgnoreCase ("61") ? "Launchkey 61" : "Launchkey";
        if (shown.isEmpty() && ! names.isEmpty())
            shown = names[0].upToFirstOccurrenceOf (" MIDI", false, true).substring (0, 16);
        keyboardFound = ! names.isEmpty();
        keyboardName = keyboardFound ? shown : "No keyboard";
        header.setTooltip (names.isEmpty() ? "No MIDI keyboard found" : "MIDI: " + names.joinIntoString (", "));
    }
}

void RigView::showAudioSettings()
{
    if (devices == nullptr)
        return;
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent> (*devices, 0, 2, 1, 2, true, false, true, false);
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
                                  if (! xml->writeTo (file) || ! engine.writeLoopAudio (file))
                                      say ("Couldn't save all of " + file.getFileName() + ".");
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
                                  engine.readLoopAudio (file);
                                  refresh();
                                  restoreWindows();
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
    // Esc leaves the Stage.
    if (key == juce::KeyPress::escapeKey && stageShown)
    {
        showTab ("Groove");
        return true;
    }
    // F1-F5: the screens.
    for (int i = 0; i < rigpanels::ScreenBar::screens().size(); ++i)
        if (key.getKeyCode() == juce::KeyPress::F1Key + i)
        {
            showTab (rigpanels::ScreenBar::screens()[i]);
            return true;
        }
    // 1-8: the song parts; on the Looper screen, its layers (record, stop, overdub).
    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '8' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        if (currentPage() == "Looper")
            engine.getLooper().pressRecord ((int) (c - '1'));
        else
            rigpanels::Footer::pressPart (engine, (int) (c - '1'));
        return true;
    }
    return false;
}

juce::String RigView::currentPage() const
{
    const int i = tabs.getCurrentTabIndex();
    return juce::isPositiveAndBelow (i, (int) pages.size()) ? pages[(size_t) i].name : juce::String();
}

void RigView::nudgeTempo (double amount)
{
    auto& transport = engine.getTransport();
    if (transport.isHosted())
    {
        say ("In Logic, Logic sets the tempo.");
        return;
    }
    transport.setBpm (std::round ((transport.getBpm() + amount) * 10.0) / 10.0);
}

void RigView::setDarkPanel (bool dark)
{
    vintage::setStage (dark || stageShown);
    engine.screens.setProperty ("dark", dark, nullptr);
    repaint();
    for (auto* c : std::initializer_list<juce::Component*> { &header, &screenBar, &footer })
        c->repaint();
    if (groove != nullptr)
        groove->repaint();
}

void RigView::showMoreMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Tools in full");
    const char* tools[][2] = { { "MPC", "MPC: pads, programs, sequences" }, { "Sampler editor", "Sampler: record, trim, effects" }, { "Bass", "Bass: sound and patterns" },
                               { "Synth", "Synth: sound and arp" }, { "Song", "Song: parts in order, lengths, names" },
                               { "Sounds", "Sounds: your samples and Splice" }, { "Mixer", "Mixer, effects and keyboard set-up" } };
    const auto here = currentPage();
    for (int i = 0; i < (int) std::size (tools); ++i)
        menu.addItem (10 + i, tools[i][1], true, here == tools[i][0]);
    menu.addSeparator();
    menu.addItem (2, "Pop this screen out (for a second screen)", here.isNotEmpty());
    menu.addItem (3, "Dark panel (for dark rooms)", true, vintage::isStage());
    menu.addItem (4, "Click (metronome)", true, engine.getTransport().metronome.load());
    menu.addSeparator();
    menu.addItem (5, "Save show...");
    menu.addItem (6, "Open show...");
    if (devices != nullptr)
        menu.addItem (7, "Audio and MIDI set-up...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (screenBar.more),
                        [safe = juce::Component::SafePointer<RigView> (this), names = std::vector<juce::String> { "MPC", "Sampler editor", "Bass", "Synth", "Song", "Sounds", "Mixer" }] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result >= 10)
                                safe->showTab (names[(size_t) (result - 10)]);
                            else if (result == 2)
                            {
                                const int i = safe->tabs.getCurrentTabIndex();
                                if (juce::isPositiveAndBelow (i, (int) safe->pages.size()))
                                {
                                    if (safe->pages[(size_t) i].window != nullptr)
                                        safe->pages[(size_t) i].window->toFront (true);
                                    else
                                        safe->popOut (i);
                                }
                            }
                            else if (result == 3)
                                safe->setDarkPanel (! vintage::isStage());
                            else if (result == 4)
                            {
                                auto& t = safe->engine.getTransport();
                                t.metronome = ! t.metronome.load();
                                safe->refresh();
                            }
                            else if (result == 5)
                                safe->saveShowAs();
                            else if (result == 6)
                                safe->openShow();
                            else if (result == 7)
                                safe->showAudioSettings();
                        });
}

void RigView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    vintage::drawCheek (g, r.removeFromLeft (kCheek));
    vintage::drawCheek (g, r.removeFromRight (kCheek));
    vintage::drawFace (g, r);
}

void RigView::resized()
{
    auto area = getLocalBounds();
    area.removeFromLeft (kCheek);
    area.removeFromRight (kCheek);

    // Shorter windows squeeze the top bar and the footer a little.
    const bool compact = getHeight() < 820;
    if (! stageShown)
        header.setBounds (area.removeFromTop (compact ? 88 : 100));
    screenBar.setBounds (area.removeFromTop (compact ? 56 : 62));
    if (! stageShown)
        footer.setBounds (area.removeFromBottom (compact ? 108 : 118));
    tabs.setBounds (area.reduced (0, 4));
}

void RigView::layoutMixer (juce::Rectangle<int> page)
{
    auto area = page.reduced (14, 10);

    // Tools across the top.
    area.removeFromTop (14);
    auto tools = area.removeFromTop (30);
    auto place = [&tools] (juce::Component& c, int w) { c.setBounds (tools.removeFromLeft (w)); tools.removeFromLeft (8); };
    place (clickButton, 64);
    if (devices != nullptr)
        place (micButton, 76);
    place (knobButton, 72);
    place (learnPlayLabel, 60);
    place (learnPlay, 64);
    place (learnStopLabel, 60);
    place (learnStop, 64);
    openButton.setBounds (tools.removeFromRight (100));
    tools.removeFromRight (8);
    saveButton.setBounds (tools.removeFromRight (100));
    tools.removeFromRight (8);
    audioButton.setVisible (devices != nullptr);
    if (devices != nullptr)
        audioButton.setBounds (tools.removeFromRight (116));
    area.removeFromTop (14);

    // The strips at the bottom, the effects of one above them.
    auto strips = area.removeFromBottom (96);
    const int count = (int) stripViews.size();
    const int w = (strips.getWidth() - 6 * (count - 1)) / juce::jmax (1, count);
    for (auto& s : stripViews)
    {
        s->setBounds (strips.removeFromLeft (w));
        strips.removeFromLeft (6);
    }
    area.removeFromBottom (10);
    if (fxPanel != nullptr)
    {
        fxPanel->setBounds (area.removeFromBottom (130));
        area.removeFromBottom (10);
    }

    // The keyboard routing and Sample.
    auto routing = area.removeFromLeft (560);
    const std::pair<juce::Label*, juce::ComboBox*> rows[] = { { &padsLabel, &padBank }, { &keysLabel, &keysTo },
                                                              { &splitLabel, &splitNote }, { &sourceLabel, &sampleSource } };
    for (auto [label, box] : rows)
    {
        auto row = routing.removeFromTop (30);
        if (box == &sampleSource)
        {
            sampleButton.setBounds (row.removeFromLeft (110).reduced (0, 1));
            row.removeFromLeft (8);
        }
        label->setBounds (row.removeFromLeft (box == &sampleSource ? 54 : 124));
        box->setBounds (row.removeFromLeft (box == &padBank ? 70 : box == &splitNote ? 82 : box == &sampleSource ? 120 : 140).reduced (0, 2));
        if (box == &splitNote)
            lowerKeysTo.setBounds (row.removeFromLeft (120).reduced (4, 2));
        if (box == &sampleSource)
        {
            sampleDest.setBounds (row.removeFromLeft (130).reduced (4, 2));
            sampleBars.setBounds (row.removeFromLeft (80).reduced (0, 2));
        }
        routing.removeFromTop (4);
    }
}
