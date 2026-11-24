#include "PocketApp.h"

#include "core/AudioFiles.h"
#include "PocketFactory.h"

using namespace pocket;

//==============================================================================
PocketApp::PocketLook::PocketLook()
{
    setColour (juce::PopupMenu::backgroundColourId, col::creamLight);
    setColour (juce::PopupMenu::textColourId, col::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::amber);
    setColour (juce::PopupMenu::highlightedTextColourId, col::ink);
    setColour (juce::AlertWindow::backgroundColourId, col::cream);
    setColour (juce::AlertWindow::textColourId, col::ink);
    setColour (juce::AlertWindow::outlineColourId, col::rule);
    setColour (juce::TextEditor::backgroundColourId, col::creamLight);
    setColour (juce::TextEditor::textColourId, col::ink);
    setColour (juce::TextEditor::outlineColourId, col::rule);
    setColour (juce::TextButton::buttonColourId, col::creamDeep);
    setColour (juce::TextButton::textColourOffId, col::ink);
    setColour (juce::ScrollBar::thumbColourId, col::inkFaint);
}

juce::Font PocketApp::PocketLook::getPopupMenuFont() { return small (18.0f); }
juce::Font PocketApp::PocketLook::getTextButtonFont (juce::TextButton&, int) { return condensed (17.0f); }

// Settings: the Rec setup (the same sheet as the header's display) and the click.
class PocketApp::SettingsScreen : public juce::Component
{
public:
    SettingsScreen (AppSettings& s, std::function<void()> back, std::function<void()> recSetup, std::function<void()> clickChanged,
                    std::function<juce::String()> phoneSummary)
        : settings (s), onRecSetup (std::move (recSetup)), onClick (std::move (clickChanged)), summary (std::move (phoneSummary))
    {
        backButton.setFontHeight (16.0f);
        backButton.onClick = std::move (back);
        addAndMakeVisible (backButton);
    }

    void resized() override { backButton.setBounds (16, 6, 96, 44); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (col::ink);
        g.setFont (fraunces (30.0f));
        g.drawText ("Settings", getLocalBounds().withTrimmedTop (56).withHeight (44).withTrimmedLeft (20), juce::Justification::centredLeft);

        auto row = [&] (juce::Rectangle<float> r, const juce::String& title, const juce::String& blurb)
        {
            drawRaised (g, r, Look::cream, false, 16.0f);
            g.setColour (col::ink);
            g.setFont (spaced (condensed (21.0f), 0.08f));
            g.drawText (title, juce::Rectangle<float> (r.getX() + 18, r.getY() + 10, r.getWidth() - 110, 26), juce::Justification::centredLeft);
            g.setColour (col::inkDim);
            g.setFont (small (14.0f));
            g.drawFittedText (blurb, juce::Rectangle<int> ((int) r.getX() + 18, (int) r.getY() + 38, (int) r.getWidth() - 110, 40), juce::Justification::topLeft, 2);
        };
        row (recRow(), "REC SETUP", "Count-in " + AppSettings::countInText (settings.countInBars).toLowerCase() + U8(" · takes ")
                                        + AppSettings::lengthText (settings.recLength).toLowerCase() + U8(" · the same on every tool"));
        g.setColour (col::ink);
        g.setFont (condensed (22.0f));
        g.drawText (">", juce::Rectangle<float> (recRow().getRight() - 40, recRow().getY(), 24, recRow().getHeight() - 3), juce::Justification::centred);

        row (clickRow(), "CLICK", "A click on every beat while the song plays (the thumb bar's CLICK)");
        auto sw = switchBounds();
        drawWell (g, sw, 15.0f);
        g.setColour (settings.click ? col::amber : col::inkDim);
        g.fillEllipse (settings.click ? sw.getRight() - 29 : sw.getX() + 3, sw.getY() + 3, 24, 24);

        // what's on the phone
        auto box = juce::Rectangle<float> (16, (float) getHeight() - 88, (float) getWidth() - 32, 80);
        g.setColour (col::well);
        g.fillRoundedRectangle (box, 16.0f);
        g.setColour (col::inkFaint);
        g.setFont (spaced (condensedBold (12.0f), 0.14f));
        g.drawText ("ON THIS IPHONE", box.withTrimmedLeft (16).withHeight (28), juce::Justification::bottomLeft);
        g.setColour (juce::Colour (0xfff1e7d6));
        g.setFont (small (16.0f));
        g.drawFittedText (summary ? summary() : juce::String(), box.reduced (16, 0).withTrimmedTop (30).toNearestInt(), juce::Justification::topLeft, 2);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (recRow().contains (e.position) && onRecSetup)
            onRecSetup();
        else if (clickRow().contains (e.position))
        {
            settings.click = ! settings.click;
            settings.save();
            if (onClick)
                onClick();
            repaint();
        }
    }

private:
    juce::Rectangle<float> recRow() const { return { 16, 108, (float) getWidth() - 32, 86 }; }
    juce::Rectangle<float> clickRow() const { return { 16, 204, (float) getWidth() - 32, 86 }; }
    juce::Rectangle<float> switchBounds() const { auto r = clickRow(); return { r.getRight() - 78, r.getCentreY() - 17, 58, 30 }; }

    AppSettings& settings;
    std::function<void()> onRecSetup, onClick;
    std::function<juce::String()> summary;
    PButton backButton { "< Back" };
};

class PocketApp::Dim : public juce::Component
{
public:
    std::function<void()> onTap;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black.withAlpha (0.38f)); }
    void mouseUp (const juce::MouseEvent&) override { if (onTap) onTap(); }
};

class PocketApp::Toast : public juce::Component, private juce::Timer
{
public:
    Toast() { setInterceptsMouseClicks (false, false); }

    void show (const juce::String& t)
    {
        text = t;
        action = nullptr;
        actionLabel = {};
        setInterceptsMouseClicks (false, false);
        setVisible (true);
        toFront (false);
        repaint();
        startTimer (2600);
    }

    // A note with one button on it (UNDO): it stays a little longer, and the button runs `then` once.
    void showAction (const juce::String& t, const juce::String& label, std::function<void()> then)
    {
        text = t;
        action = std::move (then);
        actionLabel = label;
        setInterceptsMouseClicks (true, false);
        setVisible (true);
        toFront (false);
        repaint();
        startTimer (6500);
    }

    bool hasAction() const { return isVisible() && action != nullptr; }
    void pressAction()   // what a tap on the button does (also for the self-checks)
    {
        if (auto f = std::move (action))
        {
            action = nullptr;
            stopTimer();
            setVisible (false);
            f();
        }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1);
        g.setColour (juce::Colour (0xf0201c18));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (juce::Colour (0xfff1e7d6));
        g.setFont (small (15.0f));
        if (action == nullptr)
            return g.drawFittedText (text, r.reduced (14, 4).toNearestInt(), juce::Justification::centred, 2);
        g.drawFittedText (text, r.withTrimmedRight (92).reduced (14, 4).toNearestInt(), juce::Justification::centredLeft, 2);
        auto b = buttonRect();
        g.setColour (col::amber);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensedBold (16.0f), 0.08f));
        g.drawText (actionLabel, b, juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (action != nullptr && buttonRect().expanded (6).contains (e.position))
            pressAction();
    }

private:
    juce::Rectangle<float> buttonRect() const { return { (float) getWidth() - 88.0f, 8.0f, 78.0f, (float) getHeight() - 16.0f }; }
    void timerCallback() override { stopTimer(); setVisible (false); action = nullptr; }
    juce::String text, actionLabel;
    std::function<void()> action;
};

//==============================================================================
PocketApp::PocketApp (Options o)
    : lib (o.libraryRoot != juce::File() ? o.libraryRoot : Library::defaultRoot()),
      ideas (lib),
      appSettings (lib.getRoot()),
      link (lib.getRoot(), o.audio),
      beats (static_cast<PocketHost&> (*this), lib),
      loopsScreen (static_cast<PocketHost&> (*this), lib, beats.engine(), appSettings),
      vocoderScreen (static_cast<PocketHost&> (*this), lib, audio, loopsScreen, beats.engine()),
      chordsTool (beats.engine()),
      singScreen (static_cast<PocketHost&> (*this), loopsScreen, vocoderScreen.engine(), chordsTool, appSettings),
      synthPart (beats.engine()),
      synthScreen (synthPart, beats.engine()),
      chordsScreen (chordsTool),
      droneTool (beats.engine()),
      droneScreen (droneTool),
      padSheet (beats.kit()),
      record (static_cast<PocketHost&> (*this), audio, lib, sheet),
      sampler (static_cast<PocketHost&> (*this), audio, lib, beats.kit()),
      padPicker (beats.kit()),
      sounds (static_cast<PocketHost&> (*this), audio, lib)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    juce::Desktop::getInstance().addGlobalMouseListener (&keyboardDismisser);
    juce::Desktop::getInstance().addGlobalMouseListener (&edgeWatcher);

    remote = std::make_unique<RemoteScreen> (link);
    remote->onLink = [this] { openMacLink(); };
    remote->onSay = [this] (const juce::String& t) { toast (t); };
    macLink = std::make_unique<MacLinkScreen> (link);
    macLink->onClose = [this] { goBack(); };
    macLink->onRemote = [this] { showPage (Page::more, 2); };
    macLink->onSay = [this] (const juce::String& t) { toast (t); };
    sendSheet = std::make_unique<SendToMacSheet> (link);
    sendSheet->onClose = [this] { closeSendSheet(); };
    sendSheet->onLink = [this] { closeSendSheet(); openMacLink(); };
    sendSheet->onSay = [this] (const juce::String& t) { toast (t); };
    link.onChange = [this] { linkChanged(); };
    link.onSay = [this] (const juce::String& t) { toast (t); };
    settings = std::make_unique<SettingsScreen> (appSettings, [this] { goBack(); }, [this] { openRecSetup(); }, [this] { applyClick(); thumb.repaint(); },
                                                 [this] { return phoneSummary(); });
    ideasScreen = std::make_unique<IdeasScreen> (ideas);
    ideasScreen->onOpen = [this] (const juce::String& id) { openIdea (id); };
    ideasScreen->onNew = [this] { newIdea(); };
    ideasScreen->onNewBeat = [this] { makeABeat(); };
    ideasScreen->onCatch = [this] { catchIdea(); };
    ideasScreen->onSpark = [this] (const crate::spark::Spark& sp) { startSpark (sp); };
    ideasScreen->onStartWith = [this] (Tool t, bool voice) { startWithTool (t, voice); };
    ideasScreen->onTour = [this] { startTour(); };
    ideasScreen->onPlay = [this] (const juce::String& id) { playIdea (id); };
    ideasScreen->isPlaying = [this] (const juce::String& id) { return id == ideas.current().id && beats.engine().clockRunning(); };
    ideasScreen->onHoldSong = [this] (const juce::String& id, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight)
    {
        holdMenu (songHoldSpec (id), source, at, highlight);
    };
    // The song drawer
    drawer.onNew = [this] { closeDrawer(); newIdea(); };
    drawer.onPick = [this] (const juce::String& id) { pickSong (id); };
    drawer.onPlay = [this] (const juce::String& id) { playFromDrawer (id); };
    drawer.isPlaying = [this] (const juce::String& id) { return id == ideas.current().id && beats.engine().clockRunning(); };
    drawer.onHoldSong = [this] (const juce::String& id, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight)
    {
        holdMenu (songHoldSpec (id), source, at, highlight);
    };
    drawer.onTile = [this] (int tile) { drawerTile (tile); };
    drawer.currentLayers = [this] { return loopsScreen.layerCount(); };
    drawer.currentBpm = [this] { return beats.drums().bpm; };
    holdCard.onClosed = [this] { repaint(); };
    title = std::make_unique<TitleScreen>();
    title->micLevel = [this] { return audio.takeLevel(); };   // a meter only: the mic is never heard here
    title->onStart = [this] { showTitle (false); };
    title->onTour = [this] { showTitle (false); startTour(); };
    title->onContinue = [this]
    {
        showTitle (false);
        openIdea (ideas.current().id);
    };
    tour = std::make_unique<TourOverlay>();
    tour->show = [this] (int stop) { return tourStop (stop); };
    tour->onFinish = [this] { endTour(); };
    toastBox = std::make_unique<Toast>();
    dim = std::make_unique<Dim>();
    dim->onTap = [this] { closeAllSheets(); };

    // The frame: the thumb bar, the tool row (stage 1: the tools that exist) and the count-in panel.
    toolRow.tools = { Tool::loops, Tool::song, Tool::drums, Tool::pads, Tool::sampler, Tool::synth, Tool::chords, Tool::vocoder, Tool::drone, Tool::effects };
    // EFFECTS from a tool opens on that tool's board
    toolRow.onSelect = [this] (Tool t) { if (t == Tool::effects) openEffects (page == Page::tool ? boardForTool (tool) : -1); else showTool (t); };
    thumb.state = [this]
    {
        ThumbBar::State s;
        s.bpm = beats.drums().bpm;
        s.click = appSettings.click;
        s.recBusy = recBusy();
        s.playing = beats.engine().clockRunning();
        return s;
    };
    thumb.onPress = [this] (int b) { thumbPressed (b); };
    countPanel.setInterceptsMouseClicks (false, false);

    recSetup.getCountIn = [this] { return appSettings.countInBars; };
    recSetup.getLength = [this] { return appSettings.recLength; };
    recSetup.setCountIn = [this] (int bars)
    {
        appSettings.countInBars = bars;
        appSettings.save();
        loopsScreen.engine().setCountInBars (bars);
        repaint();
    };
    recSetup.setLength = [this] (int choice)
    {
        appSettings.recLength = choice;
        appSettings.save();
        repaint();
    };
    recSetup.getPadsTo = [this] { return appSettings.padsRecTo; };
    recSetup.setPadsTo = [this] (int to)
    {
        appSettings.padsRecTo = juce::jlimit (0, 1, to);
        appSettings.save();
        repaint();
    };
    recSetup.onDone = [this] { closeRecSetup(); };
    headphones.route = [this] { return route; };
    headphones.onHeadphones = [this] { chooseHeadphones (true); };
    headphones.onNoHeadphones = [this] { chooseHeadphones (false); };

    soundBank.onLoad = [this] (int k) { loadKit (k); };
    soundBank.onDone = [this] { closeSoundBank(); };
    soundBank.onSets = [this] { showSetsMenu(); };
    soundBank.onOpenSounds = [this] { closeSoundBank(); showPage (Page::more, 1); };
    soundBank.loadedKitName = [this] { return beats.kit().name; };
    soundBank.onImport = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose sounds to import", juce::File(), "*.wav;*.aif;*.aiff;*.mp3");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
        {
            int ok = 0;
            juce::String error;
            for (auto& f : fc.getResults())
            {
                juce::String e;
                if (lib.importFile (f, e).isNotEmpty())
                    ++ok;
                else
                    error = e;
            }
            if (ok > 0)
                toast ("Imported " + juce::String (ok) + (ok == 1 ? " sound" : " sounds") + U8(". They're in the menu › Sounds: TO PAD puts one on a pad."));
            else if (error.isNotEmpty())
                toast (error);
        });
    };
    tempoSheet.getBpm = [this] { return beats.drums().bpm; };
    tempoSheet.setBpm = [this] (double b) { beats.setBpm (b); repaint(); thumb.repaint(); };
    tempoSheet.getClick = [this] { return appSettings.click; };
    tempoSheet.setClick = [this] (bool on)
    {
        appSettings.click = on;
        appSettings.save();
        applyClick();
        thumb.repaint();
    };
    tempoSheet.getCountIn = [this] { return appSettings.countInBars; };
    tempoSheet.setCountIn = [this] (int bars)
    {
        appSettings.countInBars = bars;
        appSettings.save();
        loopsScreen.engine().setCountInBars (bars);
    };
    tempoSheet.onDone = [this] { closeTempo(); };
    beats.clickWanted = [this] { return appSettings.click; };
    beats.inCountIn = [this] { return recView().kind == RecView::counting; };
    beats.onDrumModeChanged = [this] { if (lessonOn && beats.drumMode() != 0) endLesson(); };
    lesson = std::make_unique<BeatLesson>();
    lesson->onPhase = [this] (int phase) { applyLessonPhase (phase); };
    lesson->onEnd = [this] { endLesson(); };
    lesson->onAddTool = [this] { endLesson(); toolRow.flash(); toast ("Pick another tool, then press REC: it lands on the next layer."); };
    lesson->onPressRec = [this] { recPressed(); };
    lesson->recSpot = [this] { return getLocalArea (&thumb, thumb.buttonRect (ThumbBar::rec).toNearestInt()); };
    lesson->nextLayerNumber = [this] { return loopsScreen.nextLayer() + 1; };
    loopsScreen.onSourceChanged = [this] { applyMic(); };
    loopsScreen.onOpenTool = [this] (Tool t) { showTool (t); };
    loopsScreen.onFullBoard = [this] (int layer) { openEffects (pocket::layerBoardId (layer)); };
    loopsScreen.kitSounds = [this]
    {
        std::vector<PadSetup> kit;
        for (int p = 0; p < kPads; ++p)
            kit.push_back (beats.kit().pads[(size_t) p].setup());
        return kit;
    };
    loopsScreen.drumSampleSounds = [this]
    {
        std::array<crate::drums::SharedSample, crate::drums::kVoices> sounds;
        for (int v = 0; v < crate::drums::kVoices; ++v)
            sounds[(size_t) v] = beats.engine().getDrumSample (v);
        return sounds;
    };
    synthScreen.songKey = [this] { return songKey; };
    synthScreen.onKeyMenu = [this] (juce::Component& c, juce::Rectangle<int> r) { openKeyMenu (c, r); };
    synthScreen.toast = [this] (const juce::String& t) { toast (t); };
    synthScreen.onChanged = [this] { saveSong(); };
    synthScreen.nextLayer = [this] { return loopsScreen.nextLayer(); };
    synthScreen.recBusy = [this] { return synthLock() && ! lockArmed; };
    singScreen.onKeyMenu = [this] (juce::Component& c, juce::Rectangle<int> r) { openKeyMenu (c, r); };
    singScreen.onOpenChords = [this] { showTool (Tool::chords); };
    singScreen.onChanged = [this] { saveSong(); };
    singScreen.onEffects = [this] { openEffects (boardForTool (Tool::vocoder)); };
    singScreen.fxActive = [this] { const int b = boardForTool (Tool::vocoder); return b >= 0 && loopsScreen.engine().board (b).audible(); };
    chordsScreen.onKeyMenu = [this] (juce::Component& c, juce::Rectangle<int> r) { openKeyMenu (c, r); };
    chordsScreen.toast = [this] (const juce::String& t) { toast (t); };
    chordsScreen.showHold = [this] (const HoldSpec& spec, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight) { holdMenu (spec, source, at, highlight); };
    chordsScreen.onChanged = [this] { saveSong(); singScreen.repaint(); };
    chordsScreen.onEditChord = [this] (int pad) { openChordEdit (pad); };
    drumKit.onDone = [this] { closeDrumKit(); };
    drumKit.onDismiss = [this] { closeDrumKit(); };
    drumKit.onPick = [this] (const juce::String& id)
    {
        if (! beats.setDrumSound (beats.drumVoice(), id))
            return toast ("That sound couldn't be opened");
        refreshDrumKit();
    };
    drumKit.onBackToAnalog = [this]
    {
        beats.clearDrumSound (beats.drumVoice());
        refreshDrumKit();
    };
    drumKit.onUsePads = [this]
    {
        const int n = beats.useBankAsKit();
        if (n == 0)
            return toast ("No pad is named kick, snare, hat or tom. Name the pads (or load a factory kit) first.");
        refreshDrumKit();
        toast (juce::String (n) + (n == 1 ? " sound" : " sounds") + " from the pads are on the drum sounds");
    };
    chordEdit.onDone = [this] { closeChordEdit(); };
    chordEdit.onDismiss = [this] { closeChordEdit(); };
    chordEdit.onChanged = [this] { saveSong(); chordsScreen.repaint(); singScreen.repaint(); };
    chordEdit.toast = [this] (const juce::String& t) { toast (t); };
    chordsScreen.padSounds = [this]
    {
        juce::StringArray names;
        for (int p = 0; p < kPads; ++p)
            if (! beats.kit().pads[(size_t) p].empty())
                names.add ("PAD " + Kit::padLabel (p) + U8(" \xc2\xb7 ") + beats.kit().pads[(size_t) p].name);
        return names;
    };
    chordsScreen.padSoundPads = [this]
    {
        juce::Array<int> pads;
        for (int p = 0; p < kPads; ++p)
            if (! beats.kit().pads[(size_t) p].empty())
                pads.add (p);
        return pads;
    };
    chordsScreen.onPadSound = [this] (int pad)
    {
        if (! juce::isPositiveAndBelow (pad, kPads) || beats.kit().pads[(size_t) pad].empty())
            return false;
        chordsTool.padSoundPad = pad;
        chordsTool.padSoundId = Kit::padLabel (pad);
        applyChordsPadSound();
        toast ("The chords play pad " + Kit::padLabel (pad) + " (" + beats.kit().pads[(size_t) pad].name + ").");
        return true;
    };
    chordsScreen.lockedText = [this]
    {
        return chordsUndoLayer >= 0 && loopsScreen.engine().state (chordsUndoLayer) != pocket::LoopState::empty
                   ? "Chords on layer " + juce::String (chordsUndoLayer + 1) : juce::String();
    };
    chordsTool.onPlay = [this]
    {
        if (chordsTool.playsVocoder)
            vocoderScreen.engine().feedTakesOver();   // the robot sings what CHORDS plays
        singScreen.repaint();
    };
    chordsTool.engine().setVocoder (&vocoderScreen.engine().processor());
    loopsScreen.engine().setInstrument (0, &synthPart.engine());
    loopsScreen.engine().setInstrument (1, &chordsTool.engine());
    loopsScreen.engine().setDrone (&droneTool);
    setupSong();
    droneScreen.toast = [this] (const juce::String& t) { toast (t); };
    droneScreen.onChanged = [this] { saveSong(); };
    droneScreen.micOn = [this] { return micOn; };
    droneScreen.askMic = [this] { askForMic ([this] { droneScreen.repaint(); }); };
    droneScreen.songRoot = [this] { return songKey.root; };
    droneScreen.songKeyName = [this] { return songKey.label(); };
    setupEffects();
    setupSampler();
    loadSong();
    loopsScreen.engine().setCountInBars (appSettings.countInBars);
    headerTick.startTimerHz (15);
    songTick.startTimerHz (30);

    // Every bottom sheet closes with a downward flick or drag as well as its DONE button and a tap on the dim
    sheet.onDismiss = [this] { closeSheet(); };
    padSheet.onDismiss = [this] { closePadSheet(); };
    tempoSheet.onDismiss = [this] { closeTempo(); };
    recSetup.onDismiss = [this] { closeRecSetup(); };
    headphones.onDismiss = [this] { closeHeadphones(); };   // closed without a choice: the mic stays off
    soundBank.onDismiss = [this] { closeSoundBank(); };
    sendSheet->onDismiss = [this] { closeSendSheet(); };
    padSheet.onChanged = [this] (int pad) { beats.padChanged (pad); };
    padSheet.onDone = [this] { closePadSheet(); };
    padSheet.onClear = [this] (int) { beats.padsChanged(); };
    padSheet.onUndoClear = [this] (int) { beats.padsChanged(); };
    padSheet.onTrim = [this] (int pad) { openSamplePage (pad); };
    padSheet.onChop = [this] (int pad)
    {
        const auto id = beats.kit().pads[(size_t) pad].soundId;
        auto* item = lib.find (id);
        if (item == nullptr)
            return toast ("That sound is gone from Sounds");
        if (item->markers.empty())
            return toast ("No chop markers on this sound. Add some in the sound recorder, under Chop.");
        const int n = beats.kit().chop (pad, id, false);
        beats.padsChanged();
        closePadSheet();
        toast ("Chopped across " + juce::String (n) + (n == 1 ? " pad" : " pads"));
    };

    addChildComponent (record);
    addAndMakeVisible (*ideasScreen);
    addChildComponent (sounds);
    for (auto* c : std::initializer_list<juce::Component*> { &beats, &loopsScreen, &vocoderScreen, &singScreen, &synthScreen, &chordsScreen, &droneScreen, songScreen.get(), &sampler, effects.get(), remote.get(), macLink.get(), settings.get() })
        addChildComponent (c);
    addChildComponent (countPanel);
    addChildComponent (thumb);
    addChildComponent (toolRow);
    addChildComponent (*lesson);
    addChildComponent (*dim);
    addChildComponent (drawer);
    addChildComponent (sheet);
    addChildComponent (padSheet);
    addChildComponent (tempoSheet);
    addChildComponent (recSetup);
    addChildComponent (headphones);
    addChildComponent (soundBank);
    addChildComponent (*sendSheet);
    addChildComponent (padPicker);
    addChildComponent (chordEdit);
    addChildComponent (drumKit);
    addChildComponent (holdCard);
    addChildComponent (*toastBox);
    addChildComponent (*tour);
    addChildComponent (*title);

    setSize (390, 844);
    const bool phone = o.audio || o.phoneUi;
    if (phone)
        fillEmptyIdea();   // the first launch: the pads start with the Street kit
    if (phone)
        showTitle (true);   // the opening title, each time the app opens
    sounds.selectFirst();
    animate = phone;
    applyAnimate();

    // The mic starts off and unheard, every launch.
    route = pocket::currentRoute();
    pocket::watchRoute ([safe = juce::Component::SafePointer<PocketApp> (this)] { if (safe != nullptr) safe->routeChanged(); });
    loopsScreen.engine().setVocoderSource (&vocoderScreen.engine(), &vocoderScreen.engine().monitorFlag());
    applyMic();
    showPage (Page::songs);
    if (o.audio)
    {
        audio.setRenderer (&loopsScreen.engine());
        audio.start ([this] (juce::String error)
        {
            if (error.isNotEmpty())
                toast (error);
        });
    }
}

PocketApp::~PocketApp()
{
    pocket::watchRoute ({});
    juce::Desktop::getInstance().removeGlobalMouseListener (&keyboardDismisser);
    juce::Desktop::getInstance().removeGlobalMouseListener (&edgeWatcher);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PocketApp::toast (const juce::String& t)
{
    toastBox->show (t);
}

void PocketApp::toastAction (const juce::String& text, const juce::String& button, std::function<void()> then)
{
    toastBox->showAction (text, button, std::move (then));
}

void PocketApp::applyAnimate()
{
    drawer.animated = animate;
    for (auto* s : std::initializer_list<pocket::DragSheet*> { &sheet, &padSheet, &tempoSheet, &soundBank, &recSetup, &headphones, sendSheet.get(), &padPicker, &chordEdit, &drumKit })
        s->animated = animate;
    loopsScreen.editorSheet().animated = animate;
}

//==============================================================================
// The song drawer and the hold menus
void PocketApp::openDrawer()
{
    if (title->isVisible() || tour->isVisible() || drawer.isVisible())
        return;
    closeAllSheets();
    drawer.insets = insets;
    drawer.animateTo (true);
}

void PocketApp::closeDrawer()
{
    if (drawer.isVisible())
        drawer.animateTo (false);
}

// A song in the drawer: switch to it and stay on the tool you are on (from anywhere else it opens like Home's tap does).
void PocketApp::pickSong (const juce::String& id)
{
    closeDrawer();
    const bool onTool = page == Page::tool;
    if (id == ideas.current().id)
    {
        if (! onTool)
            openIdea (id);
        return;
    }
    if (lessonOn)
        endLesson();
    if (onTool)
    {
        switchIdea (id);
        showTool (tool, false);
    }
    else
        openIdea (id);
}

void PocketApp::playFromDrawer (const juce::String& id)
{
    const bool switching = id != ideas.current().id;
    if (switching && lessonOn)
        endLesson();
    playIdea (id);
    if (switching && page == Page::tool)
        showTool (tool, false);
    drawer.refresh();
    drawer.repaint();
}

void PocketApp::drawerTile (int t)
{
    switch (t)
    {
        case SongDrawer::sounds: closeDrawer(); showPage (Page::more, 1); break;
        case SongDrawer::record: closeDrawer(); openRecorder(); break;
        case SongDrawer::macLink: closeDrawer(); showPage (Page::more, 3); break;
        case SongDrawer::remote: closeDrawer(); showPage (Page::more, 2); break;
        case SongDrawer::settings: closeDrawer(); showPage (Page::more, 4); break;
        case SongDrawer::tourLesson:
        {
            HoldSpec s;
            s.title = U8("TOUR \xc2\xb7 LESSON");
            s.spotlight = false;
            s.items.push_back ({ "TAKE THE TOUR", "7 stops", false, true, [this] { closeDrawer(); startTour(); } });
            s.items.push_back ({ "SHOW ME: MAKE A BEAT", {}, false, true, [this] { closeDrawer(); lessonFromMenu(); } });
            holdMenu (s, *this, {}, drawer.tileBounds (t).toFloat());
            break;
        }
        default: break;
    }
}

HoldSpec PocketApp::songHoldSpec (const juce::String& id)
{
    HoldSpec s;
    const auto* info = ideas.find (id);
    s.title = info != nullptr ? info->name.toUpperCase() : juce::String ("SONG");
    const bool starred = info != nullptr && info->starred;
    s.items.push_back ({ starred ? "UNSTAR" : "STAR", starred ? juce::String() : juce::String ("on My ideas' STARRED"), false, true, [this, id, starred]
    {
        ideas.setStarred (id, ! starred);
        ideasScreen->refresh();
        drawer.refresh();
    } });
    s.items.push_back ({ "RENAME", {}, false, true, [this, id] { renameIdea (id); } });
    s.items.push_back ({ "DELETE", "asks first", true, true, [this, id] { deleteIdea (id); } });
    return s;
}

void PocketApp::holdMenu (const HoldSpec& spec, juce::Component& source, juce::Point<float> at, juce::Rectangle<float> highlight)
{
    auto target = juce::Rectangle<int>();
    if (! highlight.isEmpty())
        target = (&source == this ? highlight.toNearestInt() : getLocalArea (&source, highlight.toNearestInt())).getIntersection (getLocalBounds());
    if (target.isEmpty())
    {
        const auto p = (&source == this ? at : getLocalPoint (&source, at)).toInt();
        target = juce::Rectangle<int> (p.x, p.y, 1, 1).expanded (14);
    }
    lastHoldTarget = target.toFloat();
    holdCard.setBounds (getLocalBounds());
    holdCard.open (spec, target, insets.getTop() + 6, insets.getBottom() + 6);
}

// SOUND PAGE (a pad's hold menu, the pad sheet): the SAMPLER, on that pad's sound and aimed at the pad
void PocketApp::openSamplePage (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads))
        return;
    auto id = beats.kit().pads[(size_t) pad].soundId;
    if (! beats.kit().pads[(size_t) pad].empty() && lib.find (id) == nullptr)
        return toast ("That sound is gone from Sounds");
    closePadSheet();
    showTool (Tool::sampler, false);
    sampler.editForPad (pad);
}

// EDIT THIS SLICE (a pad's hold menu): the pad's part of its sound, alone in the sound editor and aimed at the pad.
// A part of a longer sound, a sound other pads play too, or a factory sound becomes the pad's own copy first, so the
// edit changes this pad only (UNDO puts the pad back on the sound it played).
void PocketApp::editPadSlice (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kPads))
        return;
    auto& kit = beats.kit();
    const auto old = kit.pads[(size_t) pad];
    const auto* item = lib.find (old.soundId);
    auto whole = item != nullptr ? lib.load (old.soundId) : nullptr;
    if (old.empty() || whole == nullptr)
        return toast (old.empty() ? "Pad " + Kit::padLabel (pad) + " is empty" : juce::String ("That sound is gone from Sounds"));
    const int n = whole->getNumSamples();
    const int from = juce::jlimit (0, juce::jmax (0, n - 1), old.from);
    const int to = old.to < 0 ? n : juce::jlimit (from + 1, n, old.to);
    int sharing = 0;
    for (int i = 0; i < kPads; ++i)
        sharing += (i != pad && ! kit.pads[(size_t) i].empty() && kit.pads[(size_t) i].soundId == old.soundId) ? 1 : 0;
    auto id = old.soundId;
    if (from > 0 || to < n || sharing > 0 || item->tag == pocket::kTagFactory)
    {
        juce::AudioBuffer<float> part (whole->getNumChannels(), to - from);
        for (int c = 0; c < part.getNumChannels(); ++c)
            part.copyFrom (c, 0, *whole, c, from, to - from);
        const auto name = old.name.isNotEmpty() ? old.name : item->name + " " + Kit::padLabel (pad);
        const auto fx = item->fx;
        id = lib.add (name, pocket::kTagRecorded, part, fx, {});
        const auto* copy = lib.find (id);
        auto copied = copy != nullptr ? lib.load (id) : nullptr;
        if (copied == nullptr)
            return toast ("The slice couldn't be saved");
        kit.assign (pad, *copy, copied, 0, -1, old.name);
        beats.padsChanged();
    }
    closePadSheet();
    showPage (Page::recorder);
    record.editForPad (pad, id);
    if (id != old.soundId)
        toastAction ("Pad " + Kit::padLabel (pad) + U8(" has its slice as its own sound now: edit it, then SAVE TO PAD"), "UNDO",
                     [this, pad, old]
                     {
                         beats.kit().pads[(size_t) pad] = old;
                         beats.padsChanged();
                         record.endPadMode();
                     });
}

// RECORD A NEW SOUND (a pad's hold menu, PADS): the SAMPLER, empty and aimed at the pad, for REC
void PocketApp::openSampler (int pad)
{
    closePadSheet();
    showTool (Tool::sampler, false);
    sampler.clear();
    sampler.setTarget (pad);
    toast (pad >= 0 ? "Press REC to sample a sound for pad " + Kit::padLabel (pad) + ", then SEND TO PAD."
                    : juce::String ("Press REC to sample a sound from the mic, then SEND TO A PAD."));
}

//==============================================================================
// A finger on the very left edge, pulling right: the drawer follows it
void PocketApp::EdgeWatcher::mouseDown (const juce::MouseEvent& e)
{
    begin (app.getLocalPoint (nullptr, e.getScreenPosition()).toFloat(), e.source.getIndex());
}

void PocketApp::EdgeWatcher::mouseDrag (const juce::MouseEvent& e)
{
    move (app.getLocalPoint (nullptr, e.getScreenPosition()).toFloat(), e.source.getIndex());
}

void PocketApp::EdgeWatcher::mouseUp (const juce::MouseEvent& e)
{
    end (e.source.getIndex());
}

void PocketApp::EdgeWatcher::begin (juce::Point<float> p, int sourceIndex)
{
    if (tracking)
        return;
    dragging = false;
    if (! app.edgeSwipeAllowed (p))
        return;
    tracking = true;
    source = sourceIndex;
    start = p;
    lastX = prevX = p.x;
    lastT = prevT = juce::Time::getMillisecondCounter();
}

void PocketApp::EdgeWatcher::move (juce::Point<float> p, int sourceIndex)
{
    if (! tracking || sourceIndex != source)
        return;
    const float dx = p.x - start.x, dy = p.y - start.y;
    if (! dragging)
    {
        if (dx > 14.0f && dx > std::abs (dy) * 1.4f)
        {
            dragging = true;
            app.closeAllSheets();
            app.drawer.insets = app.insets;
        }
        else
        {
            if (std::abs (dy) > 18.0f || dx < -8.0f)
                tracking = false;   // it is a vertical drag (a scroll, a knob): not ours
            return;
        }
    }
    prevX = lastX;
    prevT = lastT;
    lastX = p.x;
    lastT = juce::Time::getMillisecondCounter();
    app.drawer.setProgress (dx / (float) app.drawer.panelWidth());
}

void PocketApp::EdgeWatcher::end (int sourceIndex)
{
    if (! tracking || sourceIndex != source)
        return;
    tracking = false;
    if (! dragging)
        return;
    dragging = false;
    const float v = juce::Time::getMillisecondCounter() - lastT > 90 ? 0.0f : (lastX - prevX) / (float) juce::jmax<juce::uint32> (1, lastT - prevT);
    app.drawer.animateTo (app.drawer.progress() > 0.35f || v > 0.5f);
}

// Where a swipe from the left edge may start: within 22 px of it, on the tool or page itself. Not on the thumb bar or the
// tool row (they have their own touches), not on the SYNTH keys or the loop editor, and not while a sheet, a menu, the
// title or the tour is up.
bool PocketApp::edgeSwipeAllowed (juce::Point<float> p) const
{
    if (p.x > 22.0f || p.x < 0.0f || p.y < (float) insets.getTop())
        return false;
    if (title->isVisible() || tour->isVisible() || drawer.isVisible() || holdCard.isOpen())
        return false;
    if (sheetOpen || padSheetOpen || sendOpen || tempoOpen || bankOpen || recSetupOpen || phonesOpen || padPickOpen || chordEditOpen || drumKitOpen)
        return false;
    if (page == Page::recorder)
        return false;   // (the sound's wave has handles to drag)
    const int bottom = thumbShown() ? thumb.getY() : toolRowShown() ? toolRow.getY() : getHeight();
    if (p.y >= (float) bottom)
        return false;
    const auto pi = p.toInt();
    if (page == Page::tool && tool == Tool::synth && getLocalArea (&synthScreen, synthScreen.keysBounds()).expanded (0, 6).contains (pi))
        return false;
    if (page == Page::tool && tool == Tool::sampler && getLocalArea (&sampler, sampler.waveBounds()).contains (pi))
        return false;   // (its IN and OUT handles)
    if (page == Page::tool && tool == Tool::loops && loopsScreen.editorSheet().isVisible()
        && getLocalArea (&loopsScreen, loopsScreen.editorSheet().getBounds()).contains (pi))
        return false;
    return true;
}

//==============================================================================
// Pages and tools
void PocketApp::showPage (Page p, int morePage)
{
    if (page == Page::recorder && p != Page::recorder)
        record.savePadEdits();   // a pad's sound edited on the sample page: leaving by any way keeps the edit on the pad
    if (lessonOn && ! (p == Page::tool && tool == Tool::drums))
        endLesson();   // leaving DRUMS ends the lesson
    // Sounds, Remote, Mac link, Settings and the recorder remember where they were opened from
    if ((p == Page::more || p == Page::recorder) && page != Page::more && page != Page::recorder)
    {
        backPage = page;
        backTool = tool;
    }
    holdCard.close();
    if (drawer.isVisible())
        drawer.animateTo (false);
    if (sheetOpen)
        closeSheet();
    closePadSheet();
    closeSendSheet();
    closeHeadphones();
    closePadPicker();
    closeChordEdit();
    closeDrumKit();
    audio.stopPlaying();
    if (p == Page::songs || p == Page::recorder)
        stopEverything();   // the tools share the clock; your songs and the sound recorder don't

    page = p;
    const bool onTool = p == Page::tool;
    record.setVisible (p == Page::recorder);
    ideasScreen->setVisible (p == Page::songs);
    if (p == Page::songs)
    {
        ideas.setSummary (beats.summary() + (loopsScreen.layerCount() > 0 ? U8(" · ") + juce::String (loopsScreen.layerCount()) + (loopsScreen.layerCount() == 1 ? " loop" : " loops") : juce::String()));
        ideasScreen->refresh();
    }
    if (onTool && (tool == Tool::drums || tool == Tool::pads))
        beats.showMode (tool == Tool::drums ? 0 : 1);
    beats.setVisible (onTool && (tool == Tool::drums || tool == Tool::pads));
    loopsScreen.setVisible (onTool && tool == Tool::loops);
    singScreen.setVisible (onTool && tool == Tool::vocoder);
    synthScreen.setVisible (onTool && tool == Tool::synth);
    chordsScreen.setVisible (onTool && tool == Tool::chords);
    droneScreen.setVisible (onTool && tool == Tool::drone);
    songScreen->setVisible (onTool && tool == Tool::song);
    if (! (onTool && tool == Tool::sampler))
        sampler.leave();   // a take still running is kept
    sampler.setVisible (onTool && tool == Tool::sampler);
    effects->setVisible (onTool && tool == Tool::effects);
    vocoderScreen.setVisible (p == Page::vocoderSettings);
    showMorePage (p == Page::more ? juce::jlimit (1, 4, morePage) : -1);

    thumb.setVisible (thumbShown());
    toolRow.setVisible (toolRowShown());
    refreshToolRow();
    resized();
    applyMic();
    noteStep();
    headerTickNow();
    repaint();
}

void PocketApp::showTool (Tool t, bool askMic)
{
    tool = t;
    showPage (Page::tool);
    if (t == Tool::vocoder && askMic && ! micOn)
        askForMic ({});
}

void PocketApp::openRecorder()
{
    showPage (Page::recorder);
}

// The way back from Sounds, Remote, Mac link, Settings and the recorder: the tool (or Home) you opened them from.
void PocketApp::hideToast() { toastBox->setVisible (false); }
bool PocketApp::toastHasAction() const { return toastBox->hasAction(); }
void PocketApp::pressToastAction() { toastBox->pressAction(); }

void PocketApp::goBack()
{
    if (page == Page::recorder)
    {
        record.savePadEdits();
        record.endPadMode();
        if (backPage == Page::tool && backTool == Tool::sampler && record.soundId().isNotEmpty() && lib.find (record.soundId()) != nullptr)
        {
            const int pad = sampler.target();
            showTool (Tool::sampler, false);
            sampler.loadSound (record.soundId());   // MORE EDITS: the sampler has the edited sound
            sampler.setTarget (pad);
            return;
        }
    }
    if (backPage == Page::tool)
        showTool (backTool, false);
    else if (backPage == Page::vocoderSettings)
        showPage (Page::vocoderSettings);
    else
        showPage (Page::songs);
}

juce::String PocketApp::phoneSummary() const
{
    juce::int64 bytes = 0;
    for (auto* folder : { "Sounds", "Ideas" })   // every sound, and every idea's loops
        for (auto& f : lib.getRoot().getChildFile (folder).findChildFiles (juce::File::findFiles, true))
            bytes += f.getSize();
    const int n = (int) lib.items().size(), layers = loopsScreen.layerCount();
    return juce::String (n) + (n == 1 ? " sound" : " sounds") + U8(" Â· ") + juce::String (layers) + (layers == 1 ? " loop layer" : " loop layers")
           + U8(" in this song Â· ") + juce::String (juce::roundToInt ((double) bytes / 1048576.0)) + " MB";
}

void PocketApp::refreshToolRow()
{
    toolRow.selected = -1;
    if (page == Page::tool || page == Page::vocoderSettings)
        for (int i = 0; i < (int) toolRow.tools.size(); ++i)
            if (toolRow.tools[(size_t) i] == (page == Page::vocoderSettings ? Tool::vocoder : tool))
                toolRow.selected = i;
    toolRow.repaint();
}

// page: -1 = not on one of them. 1 Sounds, 2 Remote, 3 Mac link, 4 Settings
void PocketApp::showMorePage (int p)
{
    morePageShown = p;
    sounds.setVisible (p == 1);
    remote->setVisible (p == 2);
    macLink->setVisible (p == 3);
    settings->setVisible (p == 4);
    if (p == 1)
        sounds.selectFirst();
    if (p == 3)
        macLink->shown();
    repaint();   // the header is dark on the Remote
}

void PocketApp::noteStep()
{
    // Your songs draws how far a song has got with the old seven steps; the tools map onto them.
    int s = -1;
    if (page == Page::recorder)
        s = sheetOpen ? stepShape : stepRecord;
    else if (page == Page::tool)
        s = tool == Tool::loops || tool == Tool::synth || tool == Tool::chords || tool == Tool::drone || tool == Tool::effects || tool == Tool::song ? stepSong : tool == Tool::vocoder ? stepSing : tool == Tool::drums ? stepBeat
          : beats.drums().padPattern.hits.empty() ? stepPads : stepBeat;
    if (page == Page::tool && tool == Tool::sampler)
        s = stepShape;
    if (s >= 0)
        ideas.reachedStep (s);
}

void PocketApp::stopEverything()
{
    lockWatch.stopTimer();
    lockKind = LockKind::none;
    finishSynthTake();
    synthScreen.leave();
    chordsScreen.leave();
    chordsTool.releaseAll();
    beats.leave();
    loopsScreen.leave();
}

juce::Rectangle<int> PocketApp::contentBounds() const
{
    const int w = getWidth(), h = getHeight();
    const int top = insets.getTop() + 46;   // the header and its rule
    int bottom = toolRowShown() ? h - 64 - insets.getBottom() : h - insets.getBottom();
    if (thumbShown())
        bottom -= 74;
    return { 0, top, w, bottom - top };
}

void PocketApp::editSound (const juce::String& id)
{
    showPage (Page::recorder);
    record.loadSound (id);
}

void PocketApp::closeAllSheets()
{
    holdCard.close();
    closeSheet();
    closePadSheet();
    closeSendSheet();
    closeTempo();
    closeSoundBank();
    closeRecSetup();
    closeHeadphones();
    closePadPicker();
    closeChordEdit();
    closeDrumKit();
}

void PocketApp::slide (juce::Component& c, juce::Rectangle<int> open, bool shown)
{
    auto closed = open.withY (getHeight());
    if (! animate)
        c.setBounds (shown ? open : closed);
    else
        juce::Desktop::getInstance().getAnimator().animateComponent (&c, shown ? open : closed, 1.0f, 220, false, 1.0, 1.0);
}

void PocketApp::layoutSheet (bool shown)
{
    const int top = insets.getTop() + 84;
    slide (sheet, juce::Rectangle<int> (0, top, getWidth(), getHeight() - top), shown);
}

juce::Rectangle<int> PocketApp::padSheetBounds() const
{
    const int h = 418;
    return { 0, getHeight() - h, getWidth(), h };
}

void PocketApp::openPadSheet (int pad)
{
    if (padSheetOpen || sheetOpen || pad < 0)
        return;
    padSheetOpen = true;
    padSheet.show (pad);
    dim->setVisible (true);
    padSheet.setBounds (padSheetBounds().withY (getHeight()));
    padSheet.setVisible (true);
    padSheet.toFront (false);
    toastBox->toFront (false);
    slide (padSheet, padSheetBounds(), true);
}

void PocketApp::closePadSheet()
{
    if (! padSheetOpen)
        return;
    padSheetOpen = false;
    dim->setVisible (false);
    slide (padSheet, padSheetBounds(), false);
    if (! animate)
        padSheet.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->padSheetOpen)
                safe->padSheet.setVisible (false);
        });
}

void PocketApp::showDragSheet (pocket::DragSheet& sheetToShow, juce::Rectangle<int> bounds, bool& openFlag, bool shown)
{
    if (shown == openFlag)
        return;
    openFlag = shown;
    if (shown)
    {
        holdCard.close();
        dim->setVisible (true);
        dim->toFront (false);
        sheetToShow.setBounds (bounds.withY (getHeight()));
        sheetToShow.setVisible (true);
        sheetToShow.toFront (false);
        toastBox->toFront (false);
        slide (sheetToShow, bounds, true);
        return;
    }
    dim->setVisible (false);
    slide (sheetToShow, bounds, false);
    if (! animate)
        sheetToShow.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this), s = juce::Component::SafePointer<juce::Component> (&sheetToShow), &openFlag]
        {
            if (safe != nullptr && s != nullptr && ! openFlag)
                s->setVisible (false);
        });
}

void PocketApp::openPadPicker()
{
    if (padPickOpen)
        return;
    padPicker.show (sampler.target(), beats.currentBank(), sampler.soundName());
    showDragSheet (padPicker, padPickerBounds(), padPickOpen, true);
}

void PocketApp::closePadPicker()
{
    showDragSheet (padPicker, padPickerBounds(), padPickOpen, false);
}

juce::Rectangle<int> PocketApp::chordEditBounds() const
{
    const int top = insets.getTop() + 24;
    return { 0, top, getWidth(), getHeight() - top };
}

juce::Rectangle<int> PocketApp::drumKitBounds() const
{
    const int top = insets.getTop() + 24;
    return { 0, top, getWidth(), getHeight() - top };
}

// What the sheet shows: the picked drum sound and what is on it.
void PocketApp::refreshDrumKit()
{
    const int v = beats.drumVoice();
    drumKit.bottomInset = insets.getBottom();
    drumKit.open (juce::String (crate::drums::voiceName (juce::jlimit (0, crate::drums::kVoices - 1, v))).toUpperCase(), beats.drumSoundId (v),
                  beats.drumSoundName (v));
}

void PocketApp::openDrumKit()
{
    if (drumKitOpen)
        return;
    closeSheet();
    closePadSheet();
    closeSendSheet();
    closeTempo();
    refreshDrumKit();
    showDragSheet (drumKit, drumKitBounds(), drumKitOpen, true);
}

void PocketApp::closeDrumKit()
{
    showDragSheet (drumKit, drumKitBounds(), drumKitOpen, false);
}

void PocketApp::openChordEdit (int pad)
{
    if (chordEditOpen || ! juce::isPositiveAndBelow (pad, kChordPads))
        return;
    chordEdit.bottomInset = insets.getBottom();
    chordEdit.open (pad);
    showDragSheet (chordEdit, chordEditBounds(), chordEditOpen, true);
}

void PocketApp::closeChordEdit()
{
    showDragSheet (chordEdit, chordEditBounds(), chordEditOpen, false);
}

// TO PAD: the sound goes on the next empty pad of the current bank, and the pads open with it picked.
void PocketApp::sendToPad (const juce::String& soundId)
{
    auto* item = lib.find (soundId);
    if (item == nullptr)
        return toast ("Pick a sound first");
    const int first = beats.kit().nextEmpty (beats.currentBank());
    if (first < 0)
        return toast ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + beats.currentBank())) + " is full. Pick another bank in PADS.");

    if (item->markers.empty())
        return placeOnPads (soundId, first, false);

    auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Send to pad")
                       .withMessage ("\"" + item->name + "\" has " + juce::String ((int) item->markers.size()) + " chop markers.")
                       .withButton ("One pad").withButton ("Chop across pads").withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [this, soundId, first] (int r)
    {
        if (r == 1 || r == 2)
            placeOnPads (soundId, first, r == 2);
    });
}

void PocketApp::placeOnPads (const juce::String& soundId, int first, bool chop)
{
    auto* item = lib.find (soundId);
    if (item == nullptr)
        return;
    const auto name = item->name;
    int used = 1;
    if (chop)
        used = beats.kit().chop (first, soundId, true);
    else if (! beats.kit().assign (first, soundId))
        used = 0;
    if (used == 0)
        return toast ("That sound couldn't be opened");

    beats.padsChanged();
    showTool (Tool::pads);
    beats.selectPad (first);
    toast (chop ? "Chopped \"" + name + "\" across " + juce::String (used) + " pads from " + Kit::padLabel (first)
                : "Put \"" + name + "\" on pad " + Kit::padLabel (first));
}

void PocketApp::openSheet()
{
    if (sheetOpen)
        return;
    sheetOpen = true;
    record.sheetOpened();
    dim->setVisible (true);
    sheet.setBounds (sheet.getBounds().withY (getHeight()).withHeight (getHeight() - insets.getTop() - 84).withWidth (getWidth()));
    sheet.setVisible (true);
    sheet.toFront (false);
    toastBox->toFront (false);
    layoutSheet (true);
}

void PocketApp::closeSheet()
{
    if (! sheetOpen)
        return;
    sheetOpen = false;
    record.sheetClosed();
    dim->setVisible (false);
    layoutSheet (false);
    if (! animate)
        sheet.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->sheetOpen)
                safe->sheet.setVisible (false);
        });
}

//==============================================================================
// The Mac: the Send to Mac sheet, Link to your Mac, and the header's link chip.
juce::Rectangle<int> PocketApp::sendSheetBounds() const
{
    const int h = juce::jmin (getHeight() - insets.getTop() - 40, 640);
    return { 0, getHeight() - h, getWidth(), h };
}

void PocketApp::sendSoundToMac (const juce::String& soundId, const juce::String& dest)
{
    auto* item = lib.find (soundId);
    if (item == nullptr)
        return toast ("Pick a sound first");
    auto audio = lib.load (soundId);
    if (audio == nullptr)
        return toast ("That sound couldn't be opened");
    if (item->fx.anyOn())   // what you hear with its effects on is what goes
        audio = crate::share (crate::SoundFx::apply (*audio, item->fx.toSettings()));
    MacSend send;
    send.items.push_back ({ item->name, audio, 0.0 });
    send.title = item->name;
    send.detail = Kit::lengthText ((double) audio->getNumSamples() / crate::kSampleRate) + U8(" · WAV");
    send.dests = { "sounds", "pad", "sampler", "loop" };
    send.dest = dest;
    sendToMac (std::move (send));
}

void PocketApp::sendToMac (MacSend send)
{
    closeSheet();
    closePadSheet();
    sendSheet->show (std::move (send));
    sendOpen = true;
    dim->setVisible (true);
    dim->toFront (false);
    sendSheet->setBounds (sendSheetBounds().withY (getHeight()));
    sendSheet->setVisible (true);
    sendSheet->toFront (false);
    toastBox->toFront (false);
    slide (*sendSheet, sendSheetBounds(), true);
}

void PocketApp::closeSendSheet()
{
    if (! sendOpen)
        return;
    sendOpen = false;
    dim->setVisible (false);
    slide (*sendSheet, sendSheetBounds(), false);
    if (! animate)
        sendSheet->setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->sendOpen)
                safe->sendSheet->setVisible (false);
        });
}

void PocketApp::openMacLink()
{
    closeSheet();
    closePadSheet();
    closeSendSheet();
    showPage (Page::more, 3);
}

//==============================================================================
// SOUNDS: the factory kits and your own sounds
void PocketApp::openSoundBank()
{
    if (bankOpen)
        return;
    if (const int n = lib.importDropFolder(); n > 0)
        toast (juce::String (n) + (n == 1 ? " sound" : " sounds") + U8(" came in from Drop Sounds Here. They're in the menu › Sounds."));
    closeSheet();
    closePadSheet();
    closeSendSheet();
    closeTempo();
    bankOpen = true;
    auto open = tempoBounds().withTop (getHeight() - 560 - insets.getBottom());
    dim->setVisible (true);
    soundBank.setBounds (open.withY (getHeight()));
    dim->toFront (false);
    soundBank.setVisible (true);
    soundBank.toFront (false);
    toastBox->toFront (false);
    slide (soundBank, open, true);
}

void PocketApp::closeSoundBank()
{
    if (! bankOpen)
        return;
    bankOpen = false;
    dim->setVisible (false);
    slide (soundBank, tempoBounds().withTop (getHeight() - 560 - insets.getBottom()), false);
    if (! animate)
        soundBank.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->bankOpen)
                safe->soundBank.setVisible (false);
        });
}

void PocketApp::loadKit (int index)
{
    auto doLoad = [this, index]
    {
        const int bank = beats.currentBank();
        const int n = loadFactoryKit (lib, beats.kit(), index, bank);
        beats.padsChanged();
        soundBank.repaint();
        closeSoundBank();
        toast (juce::String (factoryKits()[(size_t) index].name) + " is on bank " + juce::String::charToString ((juce::juce_wchar) ('A' + bank))
               + U8(" (") + juce::String (n) + U8(" pads). Hold a pad for its sample page."));
    };
    bool used = false;
    for (int i = 0; i < 16; ++i)
        used = used || ! beats.kit().pads[(size_t) (beats.currentBank() * 16 + i)].empty();
    if (! used)
        return doLoad();
    auto* w = new juce::AlertWindow ("Replace bank " + juce::String::charToString ((juce::juce_wchar) ('A' + beats.currentBank())) + "'s pads?",
                                     "The kit goes on its 16 pads. The sounds on them now stay in Sounds.", juce::MessageBoxIconType::NoIcon, this);
    w->addButton ("Replace", 1);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([doLoad] (int r) { if (r == 1) doLoad(); }), true);
}

void PocketApp::fillEmptyIdea()
{
    if (beats.kit().usedPads() > 0)
        return;
    loadFactoryKit (lib, beats.kit(), 0, 0);
    beats.padsChanged();
}

//==============================================================================
// The pad sample page
void PocketApp::padSoundEdited (const juce::String& soundId)
{
    int n = 0;
    for (auto& p : beats.kit().pads)
        n += (! p.empty() && p.soundId == soundId) ? 1 : 0;
    repadSound (soundId, soundId, 0);
    if (n > 0)
        toast (n == 1 ? juce::String ("The pad has the new sound") : juce::String (n) + " pads have the new sound");
}

void PocketApp::padSoundSwapped (const juce::String& oldId, const juce::String& newId)
{
    repadSound (oldId, newId, 0);
}

void PocketApp::backToPads (int pad)
{
    const auto id = record.soundId();
    record.endPadMode();
    showTool (Tool::sampler, false);
    if (id.isNotEmpty() && lib.find (id) != nullptr)
        sampler.loadSound (id);
    sampler.setTarget (pad);
    beats.selectPad (pad);
}

//==============================================================================
// Your songs
void PocketApp::switchIdea (const juce::String& id)
{
    if (id == ideas.current().id)
        return;
    lockWatch.stopTimer();
    lockKind = LockKind::none;
    finishSynthTake();
    synthScreen.leave();
    chordsScreen.leave();
    chordsTool.releaseAll();
    if (sheetOpen)
        closeSheet();
    closePadSheet();
    closeSendSheet();
    audio.stopPlaying();
    record.savePadEdits();   // (into the song being left: its pads)
    beats.leave();
    loopsScreen.leave();
    if (song->isLive())
        song->stopAndKeep();   // (a run going on is kept with the song being left)
    song->stopPlaying();
    beats.flush();
    loopsScreen.flush();
    saveSong();
    ideas.setSummary (beats.summary() + (loopsScreen.layerCount() > 0 ? U8(" · ") + juce::String (loopsScreen.layerCount()) + (loopsScreen.layerCount() == 1 ? " loop" : " loops") : juce::String()));
    record.endPadMode();
    ideas.open (id);
    beats.reloadIdea (ideas.current().name);
    loopsScreen.reloadIdea();
    loadSong();
    fillEmptyIdea();
}

void PocketApp::openIdea (const juce::String& id)
{
    switchIdea (id);
    // A song with layers opens on them; a fresh one on the pads (a kit is loaded, so they play at once).
    showTool (loopsScreen.layerCount() > 0 ? Tool::loops : Tool::pads);
}

// Home's + NEW SONG: an empty song (the Street kit on the pads, nothing programmed), open on LOOPS with its empty
// layers, and the tool row lit to say where to start.
void PocketApp::newIdea()
{
    switchIdea (ideas.create (ideas.nextName()));
    showTool (Tool::loops, false);
    if (animate)
        toolRow.flash();   // (an outline for a moment: only in the running app)
    toast ("Pick a tool down here, then press REC: it lands on layer 1, in time.");
}

// CATCH AN IDEA: like a voice memo. A new song, LOOPS on the mic, and REC at once: free length (stop when you're done),
// no count-in and no click, so a hum is caught the moment it comes. It lands on layer 1, and the song grows from there.
void PocketApp::catchIdea()
{
    switchIdea (ideas.create (ideas.nextName()));
    showTool (Tool::loops, false);
    loopsScreen.setSource ((int) LoopSource::mic);
    auto go = [this]
    {
        if (page != Page::tool || tool != Tool::loops || recBusy())
            return;
        if (loopsScreen.recordFrom ((int) LoopSource::mic, 0) >= 0)
            toast ("Catching it: sing, hum or play. Tap REC when you're done and it loops on layer 1.");
        applyClick();
        headerTickNow();
    };
    if (! micOn)
        return askForMic (go);
    go();
}

// START THIS SPARK: a new song named for the spark, at its tempo and in its key, with its kit on the pads, opened on
// the first step's tool. The three steps are said once, so you know where it's going.
void PocketApp::startSpark (const crate::spark::Spark& spark)
{
    using namespace crate::spark;
    juce::String name = spark.title.upToFirstOccurrenceOf (",", false, false).trim();
    switchIdea (ideas.create (name.isNotEmpty() ? name : ideas.nextName()));
    beats.setBpm ((double) spark.bpm);
    setSongKey ({ spark.root, spark.minor });
    if (juce::isPositiveAndBelow (spark.kit, (int) factoryKits().size()))
    {
        loadFactoryKit (lib, beats.kit(), spark.kit, 0);
        beats.padsChanged();
    }
    saveSong();
    const auto first = spark.steps[0].tool;
    const bool voice = first == crate::spark::voice;
    static const Tool tools[8] = { Tool::drums, Tool::pads, Tool::sampler, Tool::synth, Tool::chords, Tool::vocoder, Tool::loops, Tool::drone };
    showTool (tools[juce::jlimit (0, 7, first)], false);
    if (voice)
        loopsScreen.setSource ((int) LoopSource::mic);
    thumb.repaint();
    toast ("1 " + spark.steps[0].text + U8(" \xc2\xb7 2 ") + spark.steps[1].text + U8(" \xc2\xb7 3 ") + spark.steps[2].text);
}

// START A SONG FROM ANY TOOL: a new song, opened on that tool (MY VOICE: LOOPS, set to record the mic).
void PocketApp::startWithTool (Tool t, bool voice)
{
    switchIdea (ideas.create (ideas.nextName()));
    showTool (t, false);
    if (voice)
        loopsScreen.setSource ((int) LoopSource::mic);
    toast (voice ? "Press REC and sing, hum or play: it lands on layer 1, in time."
                 : "Play it, then press REC: it lands on layer 1, in time. Then pick the next tool.");
}

void PocketApp::renameIdea (const juce::String& id)
{
    auto* info = ideas.find (id);
    if (info == nullptr)
        return;
    auto* w = new juce::AlertWindow ("Name this song", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", info->name);
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int result)
    {
        if (result != 1)
            return;
        ideas.rename (id, w->getTextEditorContents ("name").substring (0, 60));
        ideasScreen->refresh();
        drawer.refresh();
        repaint();
    }), true);
}

void PocketApp::deleteIdea (const juce::String& id)
{
    auto* info = ideas.find (id);
    if (info == nullptr)
        return;
    // The confirm is the app's own card, where the song's menu was (a native alert sits apart from the app on the phone)
    const bool open = id == ideas.current().id;
    juce::String next;
    for (auto& i : ideas.list())
        if (i.id != id && next.isEmpty())
            next = i.name;
    HoldSpec s;
    s.title = "DELETE " + info->name.toUpperCase() + "?";
    const juce::String after = ! open ? U8("pads, beat, loops \xc2\xb7 no undo")
                             : next.isNotEmpty() ? "then opens " + next : juce::String ("then a new song opens");
    s.items.push_back ({ "DELETE SONG", after, true, true, [this, id] { removeIdea (id); } });
    s.items.push_back ({ "KEEP IT", {}, false, true, [] {} });
    auto where = lastHoldTarget;
    if (where.isEmpty() || ! holdCard.getBounds().toFloat().intersects (where))
        where = juce::Rectangle<float> ((float) getWidth() * 0.5f - 40.0f, (float) getHeight() * 0.4f, 80.0f, 2.0f);
    holdMenu (s, *this, where.getCentre(), where);
}

void PocketApp::removeIdea (const juce::String& id)
{
    auto* info = ideas.find (id);
    if (info == nullptr)
        return;
    const auto name = info->name;
    if (id != ideas.current().id)
    {
        if (! ideas.remove (id))
            return toast ("Couldn't delete " + name);
        ideasScreen->refresh();
        drawer.refresh();
        return toast ("Deleted " + name + ". The sounds stay in Sounds.");
    }
    // The open song: your most recent other song opens in its place (a new empty one when it was the last)
    juce::String next;
    for (auto& i : ideas.list())   // (newest first)
        if (i.id != id && next.isEmpty())
            next = i.id;
    const bool fresh = next.isEmpty();
    if (fresh)
        next = ideas.create (ideas.nextName());
    if (lessonOn)
        endLesson();
    const auto wasPage = page;
    switchIdea (next);
    if (! ideas.remove (id))
        return toast ("Couldn't delete " + name);
    if (wasPage == Page::tool)
        showTool (tool, false);
    else if (wasPage == Page::songs)
        showIdeasList();
    ideasScreen->refresh();
    drawer.refresh();
    repaint();
    toast ("Deleted " + name + ". " + (fresh ? juce::String ("Here's a new empty song.") : "You're on " + ideas.current().name + " now."));
}

// MAKE A BEAT: a new song with an empty grid, DRUMS open, and the lesson walking through it.
void PocketApp::makeABeat()
{
    switchIdea (ideas.create (ideas.nextName()));
    startLesson();
}

// The drawer's TOUR · LESSON > Show me: make a beat. A song that is still empty is used as it is; a song with a beat or
// layers stays safe, and the lesson starts a new one.
void PocketApp::lessonFromMenu()
{
    auto& data = beats.drums();
    bool hasDrums = false;
    for (auto& pair : data.patterns)
        for (auto& p : pair)
            hasDrums = hasDrums || ! p.isEmpty();
    if (hasDrums || loopsScreen.layerCount() > 0 || ! data.padPattern.hits.empty())
        switchIdea (ideas.create (ideas.nextName()));
    startLesson();
}


void PocketApp::playIdea (const juce::String& id)
{
    if (id == ideas.current().id && beats.engine().clockRunning())
    {
        togglePlay();
        ideasScreen->refresh();
        return;
    }
    switchIdea (id);
    togglePlay();
    ideasScreen->refresh();
}

//==============================================================================
// PLAY: the loop layers if there are any, otherwise what the tool on screen plays (the drum machine on
// DRUMS, the pad pattern if it has hits), otherwise the drum machine. STOP: everything.
void PocketApp::stopClock()
{
    auto& b = beats.engine();
    auto& l = loopsScreen.engine();
    lockWatch.stopTimer();
    lockKind = LockKind::none;
    finishSynthTake();
    beats.setRecordingPads (false);
    beats.setRecordingDrums (false);
    l.setTransport (false);
    b.setPlaying (false);
    b.setPatternPlaying (false);
    b.stopPads();
}

void PocketApp::togglePlay()
{
    auto& b = beats.engine();
    auto& l = loopsScreen.engine();
    // SONG: the thumb's PLAY plays the song from its playhead; STOP stops it (and keeps a REC LIVE run)
    if (song->isLive())
    {
        songScreen->pressRecLive();
        applyClick();
        return thumb.repaint();
    }
    if (song->isOn() || (! b.clockRunning() && page == Page::tool && tool == Tool::song && ! song->blocks.empty()))
    {
        songScreen->pressPlay();
        applyClick();
        beats.refreshPads();
        repaint();
        return thumb.repaint();
    }
    if (b.clockRunning())
    {
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        finishSynthTake();
        beats.setRecordingPads (false);
        beats.setRecordingDrums (false);
        l.setTransport (false);
        b.setPlaying (false);
        b.setPatternPlaying (false);
        b.stopPads();
    }
    else
    {
        if (loopsScreen.layerCount() > 0)
            l.setTransport (true);
        else if (page == Page::tool && tool == Tool::drums)
            b.setPlaying (true);
        else if (! beats.drums().padPattern.hits.empty())
            b.setPatternPlaying (true);
        else
            b.setPlaying (true);
    }
    applyClick();
    beats.refreshPads();
    repaint();
    thumb.repaint();
}

void PocketApp::applyClick()
{
    beats.engine().setClick (appSettings.click || beats.recordingPads() || beats.recordingDrums() || lessonClick || (synthLock() && ! lockArmed));
}

//==============================================================================
// The sheets that slide up: Tempo, Rec setup, Headphones on first
juce::Rectangle<int> PocketApp::tempoBounds() const
{
    const int h = 410 + insets.getBottom();
    return { 0, getHeight() - h, getWidth(), h };
}

void PocketApp::openTempo()
{
    if (tempoOpen)
        return;
    closeAllSheets();
    tempoOpen = true;
    tempoSheet.refresh();
    dim->setVisible (true);
    tempoSheet.setBounds (tempoBounds().withY (getHeight()));
    tempoSheet.setVisible (true);
    tempoSheet.toFront (false);
    toastBox->toFront (false);
    slide (tempoSheet, tempoBounds(), true);
}

void PocketApp::closeTempo()
{
    if (! tempoOpen)
        return;
    tempoOpen = false;
    dim->setVisible (false);
    slide (tempoSheet, tempoBounds(), false);
    if (! animate)
        tempoSheet.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->tempoOpen)
                safe->tempoSheet.setVisible (false);
        });
    repaint();
}

juce::Rectangle<int> PocketApp::recSetupBounds() const
{
    const int h = 482 + insets.getBottom();
    return { 0, getHeight() - h, getWidth(), h };
}

void PocketApp::openRecSetup()
{
    if (recSetupOpen)
        return;
    closeAllSheets();
    recSetupOpen = true;
    recSetup.refresh();
    dim->setVisible (true);
    recSetup.setBounds (recSetupBounds().withY (getHeight()));
    recSetup.setVisible (true);
    recSetup.toFront (false);
    toastBox->toFront (false);
    slide (recSetup, recSetupBounds(), true);
}

void PocketApp::closeRecSetup()
{
    if (! recSetupOpen)
        return;
    recSetupOpen = false;
    dim->setVisible (false);
    slide (recSetup, recSetupBounds(), false);
    if (! animate)
        recSetup.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->recSetupOpen)
                safe->recSetup.setVisible (false);
        });
    repaint();
}

// Above the thumb bar, like the design: the thumb bar stays where it was.
juce::Rectangle<int> PocketApp::headphonesBounds() const
{
    const int bottom = thumbShown() ? thumb.getY() : getHeight() - insets.getBottom();
    const int h = juce::jmin (430, bottom - insets.getTop() - 20);
    return { 0, bottom - h, getWidth(), h };
}

void PocketApp::askForMic (std::function<void()> then)
{
    if (micOn)
    {
        if (then)
            then();
        return;
    }
    openHeadphones (std::move (then));
}

void PocketApp::openHeadphones (std::function<void()> then)
{
    afterMic = std::move (then);
    route = pocket::currentRoute();
    if (phonesOpen)
        return headphones.refresh();
    closeSheet();
    closePadSheet();
    closeSendSheet();
    closeTempo();
    closeSoundBank();
    closeRecSetup();
    phonesOpen = true;
    headphones.refresh();
    dim->setVisible (true);
    headphones.setBounds (headphonesBounds().withY (getHeight()));
    headphones.setVisible (true);
    headphones.toFront (false);
    toastBox->toFront (false);
    slide (headphones, headphonesBounds(), true);
}

void PocketApp::closeHeadphones()
{
    if (! phonesOpen)
        return;
    phonesOpen = false;
    afterMic = nullptr;   // closed without a choice: nothing waits for the mic
    dim->setVisible (false);
    slide (headphones, headphonesBounds(), false);
    if (! animate)
        headphones.setVisible (false);
    else
        juce::Timer::callAfterDelay (260, [safe = juce::Component::SafePointer<PocketApp> (this)]
        {
            if (safe != nullptr && ! safe->phonesOpen)
                safe->headphones.setVisible (false);
        });
}

void PocketApp::chooseHeadphones (bool wearing)
{
    auto then = std::move (afterMic);
    afterMic = nullptr;
    route = pocket::currentRoute();
    micOn = true;
    // Hear myself: on with wired headphones; off on Bluetooth (it comes back late) and with no headphones.
    // If the phone can't see any headphones, it stays off even when you say you're wearing some: a
    // speaker that hears itself squeals. Plug them in and turn HEAR on in the banner.
    micHear = wearing && route == pocket::Route::wired;
    closeHeadphones();
    applyMic();
    if (wearing && route == pocket::Route::none)
        toast ("The phone can't see any headphones, so you won't hear yourself yet. Plug them in, then HEAR in the banner.");
    else if (wearing && route == pocket::Route::bluetooth)
        toast ("Mic on. Bluetooth comes back late, so you don't hear yourself. HEAR in the banner turns it on.");
    else if (wearing)
        toast ("Mic on. You hear yourself in your headphones.");
    else
        toast ("Mic on, but nothing from it goes to the speaker. Your takes still record it.");
    singScreen.repaint();
    vocoderScreen.repaint();
    if (then)
        then();
}

void PocketApp::setHearMyself (bool on)
{
    if (! micOn)
        return;
    route = pocket::currentRoute();
    if (on && route == pocket::Route::none)
        return toast ("No headphones found. Plug some in first: the speaker would squeal.");
    micHear = on;
    applyMic();
    if (on && route == pocket::Route::bluetooth)
        toast ("You hear yourself a little late on Bluetooth. Takes are still lined up in time.");
    singScreen.repaint();
}

void PocketApp::micOff()
{
    micOn = micHear = false;
    applyMic();
    toast ("Mic off. Nothing from it is recorded or heard.");
    singScreen.repaint();
}

void PocketApp::applyMic()
{
    auto& l = loopsScreen.engine();
    l.setMic (micOn, micHear);
    // Your own voice, dry, while you hear yourself: on MY VOICE, and on LOOPS with the mic picked. (The
    // vocoder carries your voice itself when its VOICE pedal is on, so VOCODER and BOTH don't add it.)
    const bool dry = page == Page::tool && ((tool == Tool::vocoder && appSettings.singMode == 0)
                                            || (tool == Tool::loops && loopsScreen.source() == (int) LoopSource::mic)
                                            || (tool == Tool::effects && effects != nullptr && effects->source() == pocket::boardVoice));
    l.setDryMonitor (dry);
}

void PocketApp::routeChanged()
{
    const auto was = route;
    route = pocket::currentRoute();
    if (route != was && micHear && route != pocket::Route::wired)
    {
        micHear = false;   // headphones out (or Bluetooth in): stop hearing yourself at once
        toast (route == pocket::Route::none ? "Headphones out: you don't hear yourself now, so the speaker can't squeal."
                                            : "Bluetooth: hear myself is off (it comes back late). HEAR in the banner turns it on.");
    }
    // Bluetooth's delay goes into the takes' latency compensation.
    audio.updateLatency (pocket::routeLatencySamples (audio.deviceRate()));
    applyMic();
    if (phonesOpen)
        headphones.refresh();
    singScreen.repaint();
    vocoderScreen.repaint();
}

//==============================================================================
// One way to record: the thumb bar's REC records the tool on screen onto the next empty layer.
void PocketApp::thumbPressed (int button)
{
    switch (button)
    {
        case ThumbBar::bpm: openTempo(); break;
        case ThumbBar::click:
            appSettings.click = ! appSettings.click;
            appSettings.save();
            applyClick();   // at once, even while it plays
            break;
        case ThumbBar::rec: recPressed(); break;
        case ThumbBar::play: togglePlay(); break;
        case ThumbBar::undo:
        {
            // the last take; a SYNTH part comes back into the pattern with its layer taken off
            const int top = loopsScreen.lastTakeUndoLayer();
            loopsScreen.undo();
            if (top >= 0 && top == synthUndoLayer && synthPart.hasStash())
            {
                synthPart.restoreStash();
                synthUndoLayer = -1;
                synthScreen.partChanged();
                saveSong();
                toast ("Layer " + juce::String (top + 1) + U8(" is off. The synth part is back in its pattern: REC puts it on again."));
            }
            else if (top >= 0 && top == chordsUndoLayer)
            {
                chordsUndoLayer = -1;
                if (chordsTool.engine().progressionMuted())
                {
                    chordsTool.progressionEdited();
                    toast ("Layer " + juce::String (top + 1) + U8(" is off. The progression plays from CHORDS again."));
                }
                chordsScreen.repaint();
            }
            break;
        }
        default: break;
    }
    thumb.repaint();
    headerTickNow();
}

bool PocketApp::recBusy() const
{
    return song->isLive() || playingIn() || loopsScreen.busyLayer() >= 0 || beats.recordingPads() || sampler.isRecording();
}

void PocketApp::recPressed()
{
    if (recBusy())
        return stopRec();
    if (page != Page::tool)
        return;
    const bool needsMic = recTool() == Tool::vocoder
                       || (tool == Tool::drone && droneTool.feed.load() == (int) pocket::DroneFeed::voice && droneTool.resonate.load() > 0.0f)
                       || (tool == Tool::loops && (loopsScreen.source() == (int) LoopSource::mic || loopsScreen.source() == (int) LoopSource::vocoder))
                       || (tool == Tool::effects && effects->source() == pocket::boardVoice);
    if (needsMic && ! micOn)
        return askForMic ([this] { recPressed(); });
    switch (tool)
    {
        case Tool::loops: recLoops(); break;
        case Tool::drums: recDrums(); break;
        case Tool::pads: recPads(); break;
        case Tool::sampler: sampler.startRecording(); break;   // a one-shot from the mic, at once (no count-in, no layer)
        case Tool::vocoder: recVocoder(); break;
        case Tool::synth: recSynth(); break;
        case Tool::chords: recChords(); break;
        case Tool::drone: recDrone(); break;
        case Tool::effects: recEffects(); break;
        case Tool::song: songScreen->pressRecLive(); break;
        default: break;
    }
    applyClick();
    headerTickNow();
}

void PocketApp::stopRec()
{
    if (song->isLive())
        return songScreen->pressRecLive();   // STOP + KEEP
    if (sampler.isRecording())
        return sampler.stopRecording();
    if (playingIn())
    {
        // Still playing hits into the pattern: stop there. What's in the pattern stays.
        const bool synth = synthLock();
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        finishSynthTake();
        if (synth)
        {
            applyClick();
            synthScreen.partChanged();
            saveSong();
            toast ("Stopped. What you played stays in the SYNTH pattern; REC in PATTERN puts it on a layer.");
            return;
        }
        beats.setRecordingPads (false);
        beats.setRecordingDrums (false);
        applyClick();
        toast ("Stopped. The hits you played stay in the pattern; REC again puts it on a layer.");
        return;
    }
    if (const int l = loopsScreen.busyLayer(); l >= 0)
        loopsScreen.stopTake (l);   // recording: ends at the bar line; waiting: called off
}

void PocketApp::recLoops()
{
    // The source picked on the LOOPS screen. A take that starts the song counts in first.
    loopsScreen.recordTake (appSettings.recBars());
}

void PocketApp::recVocoder()
{
    const bool wasRunning = beats.engine().clockRunning();
    if (singScreen.recordTake (appSettings.recBars()) >= 0 && ! wasRunning)
        togglePlay();   // the beat starts; the take starts after the count-in (or at once without one)
}

void PocketApp::recDrums()
{
    auto& b = beats.engine();
    const int layer = loopsScreen.nextLayer();
    if (layer < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    auto& data = beats.drums();
    const bool live = beats.drumMode() == 1;

    if (! live)
    {
        // STEPS: what the drum machine plays, from a bar line (every bar is the drum pattern's start). Once the
        // layer plays, the live drum machine stops so the two don't double.
        if (data.patterns[(size_t) data.slot][0].isEmpty() && data.patterns[(size_t) data.slot][1].isEmpty())
            return toast ("The drum grid is empty. Tap some boxes, or switch to PLAY LIVE and press REC to drum it in.");
        if (! loopsScreen.armLayer (layer, (int) LoopSource::drums, appSettings.recBars()))
            return;
        if (! b.isPlaying())
            b.setPlaying (true);
        lockKind = LockKind::drums;
        lockLayer = layer;
        lockArmed = true;
        lockWatch.startTimer (20);
        return;
    }

    // PLAY LIVE: like the pads. The drum machine loops its pattern; pads you hit sound at once and go into the
    // pattern on the nearest step. After a pass the pattern is locked onto the next layer from its start (the
    // layer records the drum machine, so it holds exactly what STEPS shows), and the live machine then stops.
    const double stepLen = crate::drums::stepBeats (data.settings.stepRate);
    lockLen = juce::jmax (1.0, data.edited().length * stepLen);
    const int patBars = juce::jmax (1, (int) std::ceil (lockLen / 4.0 - 1.0e-6));
    lockBars = appSettings.recBars() > 0 ? juce::jmax (appSettings.recBars(), patBars) : patBars;
    const bool running = b.clockRunning();
    const double now = running ? b.clockBeat() : 0.0;
    padsFrom = running ? now : appSettings.countInBars * 4.0;   // the clock starts at 0: the count-in comes first
    // a whole pass to play into, then the arming beat (the one before the layer's start)
    lockStart = std::ceil ((padsFrom + lockLen + 1.0) / lockLen - 1.0e-9) * lockLen;
    if (! b.isPlaying())
        b.setPlaying (true);
    beats.setRecordingDrums (true, padsFrom);
    lockKind = LockKind::drumsLive;
    lockArmed = false;
    lockLayer = -1;
    lockWatch.startTimer (20);
    toast ("Hit the pads " + juce::String (running ? "now" : "after the count-in") + ": they go in the pattern. Then it goes on layer "
           + juce::String (layer + 1) + " and keeps looping.");
}

void PocketApp::recPads()
{
    // Pads: hits you play go into the pattern (snapped), and when it has gone round it is locked onto the
    // next layer from its start, so it keeps looping. A pattern that's already there just locks.
    auto& b = beats.engine();
    const bool patternOnly = appSettings.padsRecTo == 1;   // Rec setup > PADS RECORD INTO > THE PAD PATTERN
    if (! patternOnly && loopsScreen.nextLayer() < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    auto& p = beats.drums().padPattern;
    const bool empty = p.hits.empty();
    // The pattern grows to the take's length up to 8 bars; a longer take (16, 32, 64 bars) plays the pattern
    // round on the layer for all of it, like PLAY LIVE on the drum machine.
    const int want = appSettings.recBars();
    if (const int bars = juce::jmin (8, want); empty && bars > 0 && bars != p.bars)
        beats.setPatternBars (bars);
    lockLen = p.lengthBeats();
    lockBars = empty && want > p.bars ? want : p.bars;
    const bool running = b.clockRunning();
    const double now = running ? b.clockBeat() : 0.0;
    padsFrom = running ? now : appSettings.countInBars * 4.0;   // the clock starts at 0: the count-in comes first
    if (empty)
        lockStart = std::ceil ((padsFrom + lockLen) / lockLen - 1.0e-9) * lockLen;   // a whole pass to play into
    else
        lockStart = std::ceil ((now + 0.3) / lockLen - 1.0e-9) * lockLen;            // its next start
    beats.setRecordingPads (true, padsFrom);
    lockKind = LockKind::pads;
    lockArmed = false;
    lockLayer = -1;
    lockWatch.startTimer (20);
    if (patternOnly)
    {
        lockStart = 1.0e15;   // never locked onto a layer: the pattern loops, taking every pass, until REC stops it
        toast ("Play the pads " + juce::String (running ? "now" : "after the count-in")
               + U8(": the hits go into the pad pattern and it keeps looping, so each pass adds. REC again stops. (Rec setup \xe2\x80\xba PADS RECORD INTO)"));
        return;
    }
    if (empty)
        toast ("Play the pads " + juce::String (running ? "now" : "after the count-in") + ": " + lengthLabel (lockBars).toLowerCase()
               + ". Then it goes on layer " + juce::String (loopsScreen.nextLayer() + 1) + " and keeps looping.");
    else
        toast ("The pattern goes on layer " + juce::String (loopsScreen.nextLayer() + 1) + U8(" from its next start. Hits you play before then go in too."));
}

void PocketApp::startClockForRec()
{
    if (beats.engine().clockRunning())
        return;
    if (loopsScreen.layerCount() > 0)
        loopsScreen.engine().setTransport (true);
    else
        beats.engine().setPlaying (true);
}

void PocketApp::finishSynthTake()
{
    if (synthPart.taking())
        synthPart.endTake();
    synthPart.engine().setPlayFrom (-1.0e9);
}

// SYNTH: in KEYS or ARP, a count-in (from stopped), then what you play goes into the synth's pattern for the
// take's length, snapped by REC SNAP; it loops, and from its next start it is locked onto the next layer. In
// PATTERN, the pattern is locked as it is from its next start. Once the layer plays the pattern is cleared.
void PocketApp::recSynth()
{
    auto& b = beats.engine();
    const int layer = loopsScreen.nextLayer();
    if (layer < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    const bool running = b.clockRunning();
    const double now = running ? b.clockBeat() : 0.0;
    padsFrom = running ? now : appSettings.countInBars * 4.0;   // the clock starts at 0: the count-in comes first
    synthScreen.leave();

    if (synthPart.mode == SynthPart::patternMode)
    {
        if (synthPart.pattern.empty())
            return toast ("The pattern is empty. Tap a step, then a key. Or switch to KEYS and press REC to play it in.");
        lockLen = synthPart.pattern.lengthBeats();
        const int patBars = synthPart.pattern.bars, want = appSettings.recBars();
        lockBars = want > patBars ? (int) std::ceil ((double) want / patBars - 1.0e-9) * patBars : patBars;
        lockStart = std::ceil (juce::jmax (now + 0.3, padsFrom) / lockLen - 1.0e-9) * lockLen;
        synthPart.engine().setPlayFrom (-1.0e9);
        lockKind = LockKind::synthPattern;
        toast ("The pattern goes on layer " + juce::String (layer + 1) + U8(" from its next start (") + lengthLabel (lockBars).toLowerCase() + ").");
    }
    else
    {
        int bars = appSettings.recBars();
        if (bars <= 0)
            bars = synthPart.pattern.bars;   // FREE: the pattern's own length
        bars = bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : 8;
        lockLen = bars * 4.0;
        lockBars = bars;
        lockStart = std::ceil ((padsFrom + lockLen) / lockLen - 1.0e-9) * lockLen;
        synthPart.startTake (bars, padsFrom, padsFrom + lockLen);
        lockKind = LockKind::synthLive;
        toast ("Play " + juce::String (running ? "now" : "after the count-in") + ": " + lengthLabel (bars).toLowerCase()
               + U8(". It loops, then goes on layer ") + juce::String (layer + 1) + ".");
    }
    startClockForRec();
    lockArmed = false;
    lockLayer = -1;
    lockWatch.startTimer (20);
    synthScreen.partChanged();
}

// CHORDS: the chords source goes onto the next layer for the take's length, in time (a count-in first from a
// stopped song). With the progression on, the take starts the progression from its first box and lasts whole
// passes of it. Once the layer plays, the progression's sound is muted here (it still drives the vocoder) and a
// held chord lets go, so nothing doubles. UNDO takes the layer off and the progression sounds again.
int PocketApp::chordsRecBars() const
{
    int bars = appSettings.recBars();
    const int pass = chordsTool.progressionOn ? chordsTool.usedBoxes() : 0;
    if (pass > 0)
        bars = bars <= 0 ? pass : (int) std::ceil ((double) bars / pass - 1.0e-9) * pass;
    return bars;
}

void PocketApp::recChords()
{
    auto& b = beats.engine();
    if (loopsScreen.nextLayer() < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    if (! chordsTool.playsSynth && ! chordsTool.playsPad)
        return toast ("The chords only play the vocoder's notes: pick SYNTH or MY PAD SOUND to record them.");
    const bool running = b.clockRunning();
    const bool prog = chordsTool.progressionOn && chordsTool.usedBoxes() > 0;
    const int bars = chordsRecBars();
    chordsScreen.leave();
    const int layer = loopsScreen.recordFrom ((int) pocket::LoopSource::chords, bars);
    if (layer < 0)
        return;
    auto& eng = chordsTool.engine();
    eng.setProgressionMuted (false);
    if (prog)   // the take starts where it will (corrected once it has): the progression from its top there
        eng.setOrigin (running ? std::ceil (b.clockBeat() / 4.0 - 1.0e-9) * 4.0 : appSettings.countInBars * 4.0);
    chordsLockProg = prog;
    lockKind = LockKind::chords;
    lockLayer = layer;
    lockArmed = true;
    lockWatch.startTimer (20);
    if (! running)
        togglePlay();   // the song starts; the take starts after the count-in (or at once without one)
    toast (prog ? "The progression goes on layer " + juce::String (layer + 1) + U8(" from its first chord (") + lengthLabel (bars).toLowerCase() + ")."
                : "Play the chords " + juce::String (running ? "from the next bar" : "after the count-in") + ": they go on layer " + juce::String (layer + 1) + ".");
}

// DRONE: REC records the drone as you hear it (with your voice ringing its strings, if they ring to it) onto the
// next layer, from the next bar (or after the count-in). Once the layer plays, the drone is switched off here so it
// doesn't double; ON plays it live again.
void PocketApp::recDrone()
{
    auto& b = beats.engine();
    if (loopsScreen.nextLayer() < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    if (! droneTool.on.load() && droneTool.resonate.load() <= 0.0f)
        return toast ("The drone is off. Tap ON (or turn RESONATE up), then REC.");
    const bool running = b.clockRunning();
    int bars = appSettings.recBars();
    if (bars <= 0)
        bars = 4;   // FREE: four bars (a drone has no end of its own)
    const int layer = loopsScreen.recordFrom ((int) pocket::LoopSource::drone, bars);
    if (layer < 0)
        return;
    lockKind = LockKind::drone;
    lockLayer = layer;
    lockArmed = true;
    lockWatch.startTimer (20);
    if (! running)
        togglePlay();   // the song starts; the take starts after the count-in (or at once without one)
    toast ("The drone goes on layer " + juce::String (layer + 1) + " " + juce::String (running ? "from the next bar" : "after the count-in")
           + " (" + lengthLabel (bars).toLowerCase() + ").");
}

void PocketApp::applyChordsPadSound()
{
    const int pad = chordsTool.padSoundPad;
    if (juce::isPositiveAndBelow (pad, kPads) && ! beats.kit().pads[(size_t) pad].empty())
    {
        const auto& kp = beats.kit().pads[(size_t) pad];
        chordsTool.engine().setPadSound (kp.audio, kp.tune / 10.0);
    }
    else
    {
        chordsTool.engine().setPadSound (nullptr, 0);
        chordsTool.playsPad = false;
    }
    chordsTool.apply();
}

void PocketApp::lockWatchTick()
{
    auto& b = beats.engine();
    auto& loops = loopsScreen.engine();
    auto cancel = [this] (const juce::String& why)
    {
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        finishSynthTake();
        beats.setRecordingPads (false);
        beats.setRecordingDrums (false);
        applyClick();
        if (why.isNotEmpty())
            toast (why);
    };
    if (lockKind == LockKind::none)
        return cancel ({});

    // SYNTH: what's played goes into the pattern until the pass ends (also after the layer is armed)
    if (synthPart.taking())
    {
        synthPart.serviceTake();
        if (b.clockRunning() && b.clockBeat() >= padsFrom + lockLen)
            finishSynthTake();
        synthScreen.repaint();
    }
    if (! lockArmed && synthLock())
    {
        if (! b.clockRunning())
            return;
        const double beat = b.clockBeat();
        const bool live = lockKind == LockKind::synthLive;
        if (beat >= lockStart - 0.3)
            lockStart += lockLen;   // too close to arm in time: the start after
        else if (live ? beat >= lockStart - 1.0 : beat > lockStart - 3.95)
        {
            // the beat (or bar) before the pattern's start: arm, so the layer starts on the pattern's start
            if (synthPart.pattern.empty())
            {
                if (live && synthPart.taking() && beat < padsFrom + lockLen - 1.0e-3)
                    return;   // still in the pass: wait for the start after
                return cancel (live ? "No notes went in. Press REC and play the SYNTH keys after the count-in." : "The pattern is empty.");
            }
            const int layer = loopsScreen.nextLayer();
            if (layer < 0)
                return cancel ("All 8 layers are used. Clear one in LOOPS to record.");
            if (! loopsScreen.armLayer (layer, (int) LoopSource::synth, lockBars))
                return cancel ("Couldn't arm layer " + juce::String (layer + 1));
            loops.keepOwnTop (layer);   // printed from a pattern: keeps the pattern's top
            auto part = std::make_shared<pocket::NotePart>();
            part->kind = pocket::NotePart::synth;
            part->notes = synthPart.pattern;
            part->preset = synthPart.preset;
            part->knobs = synthPart.knobs;
            part->octave = synthPart.octave;
            part->board = loopsScreen.engine().board (pocket::boardSynth).settings();   // recorded through it: QUANTIZE keeps it
            part->lengthBeats = lockBars * 4.0;
            part->startBeat = lockStart;
            lockPart = part;
            lockLayer = layer;
            lockArmed = true;
            applyClick();
        }
        return;
    }

    if (! lockArmed && lockKind == LockKind::drumsLive)
    {
        if (! b.isPlaying())
            return cancel ("Recording called off");
        if (! b.clockRunning())
            return;
        const double beat = b.clockBeat();
        if (beat >= lockStart - 0.3)
            lockStart += lockLen;   // too close to arm in time: the start after
        else if (beat >= lockStart - 1.0)
        {
            // the beat before the pattern's start: arm, so the layer starts on that bar line
            auto& data = beats.drums();
            if (data.patterns[(size_t) data.slot][0].isEmpty() && data.patterns[(size_t) data.slot][1].isEmpty())
                return cancel ("No drum hits went in. Press REC and hit the pads after the count-in.");
            const int layer = loopsScreen.nextLayer();
            if (layer < 0)
                return cancel ("All 8 layers are used. Clear one in LOOPS to record.");
            if (! loopsScreen.armLayer (layer, (int) LoopSource::drums, lockBars))
                return cancel ("Couldn't arm layer " + juce::String (layer + 1));
            loops.keepOwnTop (layer);   // printed from a pattern: keeps the pattern's top
            lockLayer = layer;
            lockArmed = true;
            beats.setRecordingDrums (false);
            applyClick();
        }
        return;
    }
    if (! lockArmed)
    {
        if (! b.patternPlaying())
            return cancel ("Recording called off");
        if (! b.clockRunning())
            return;
        const double beat = b.clockBeat();
        if (beat >= lockStart - 0.3)
            lockStart += lockLen;   // too close to arm in time: the start after
        else if (beat > lockStart - 3.95)
        {
            // the pattern's last bar: arm, so the layer starts on the next bar line, the pattern's start
            if (beats.drums().padPattern.hits.empty())
                return cancel ("No pad hits went in. Press REC and play the pads after the count-in.");
            const int layer = loopsScreen.nextLayer();
            if (layer < 0)
                return cancel ("All 8 layers are used. Clear one in LOOPS to record.");
            if (! loopsScreen.armLayer (layer, (int) LoopSource::drums, lockBars, (int) pocket::LayerTool::pads))
                return cancel ("Couldn't arm layer " + juce::String (layer + 1));
            loops.keepOwnTop (layer);   // printed from a pattern: keeps the pattern's top
            // the notes it's printed from, for QUANTIZE: the pattern, the drum machine if it plays along, the kit
            auto part = std::make_shared<pocket::NotePart>();
            part->kind = pocket::NotePart::pads;
            part->pattern = beats.drums().padPattern;
            part->drumsOn = b.isPlaying();
            if (part->drumsOn)
                part->drums = beats.drums();
            for (int p = 0; p < kPads; ++p)
                part->kit.push_back (beats.kit().pads[(size_t) p].setup());
            for (int v = 0; v < crate::drums::kVoices; ++v)
                part->drumSamples[(size_t) v] = b.getDrumSample (v);
            part->board = loopsScreen.engine().board (pocket::boardDrums).settings();
            part->padsBoard = loopsScreen.engine().board (pocket::boardPads).settings();
            part->lengthBeats = lockBars * 4.0;
            part->startBeat = lockStart;
            lockPart = part;
            lockLayer = layer;
            lockArmed = true;
            beats.setRecordingPads (false);
            applyClick();
        }
        return;
    }
    if (lockLayer < 0 || loops.settling (lockLayer))
        return;
    const auto st = loops.state (lockLayer);
    if (lockKind == LockKind::chords && st == pocket::LoopState::recording && chordsLockProg)
    {
        // the take has started: the progression's top on its first beat, wherever the looper put it
        const double start = loops.startBeat (lockLayer);
        if (std::abs (chordsTool.engine().getOrigin() - start) > 1.0e-6)
            chordsTool.engine().setOrigin (start);
    }
    if (lockKind == LockKind::chords && st == pocket::LoopState::playing)
    {
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        loops.setTransport (true);
        if (chordsLockProg)
            chordsTool.engine().setProgressionMuted (true);   // still drives the vocoder
        chordsTool.releaseAll();
        chordsUndoLayer = lockLayer;
        loops.setKnob (lockLayer, fxDelay, echoToDelay (chordsTool.engine().echoSend()));
        loops.setKnob (lockLayer, fxReverb, echoToReverb (chordsTool.engine().echoSend()));
        chordsScreen.repaint();
        singScreen.repaint();
        toast ("Chords on layer " + juce::String (lockLayer + 1) + (chordsLockProg ? U8(". The progression is muted here so it doesn't double; it still sets the vocoder's notes.")
                                                                                  : U8(". It keeps looping. UNDO takes it off.")));
        return;
    }
    if (lockKind == LockKind::drone)
    {
        if (st == pocket::LoopState::playing)
        {
            lockWatch.stopTimer();
            lockKind = LockKind::none;
            loops.setTransport (true);
            const bool wasOn = droneTool.on.exchange (false);
            droneScreen.repaint();
            saveSong();
            toast ("The drone is on layer " + juce::String (lockLayer + 1) + " and keeps looping."
                   + (wasOn ? juce::String (" It's off here so it doesn't double: tap ON to play it live again.") : juce::String()));
        }
        else if (st == pocket::LoopState::empty)
        {
            lockWatch.stopTimer();
            lockKind = LockKind::none;
        }
        return;
    }
    if (lockKind == LockKind::chords)
    {
        if (st == pocket::LoopState::empty)
        {
            lockWatch.stopTimer();
            lockKind = LockKind::none;
        }
        return;
    }
    if (st == pocket::LoopState::playing && synthLock())
    {
        // The SYNTH part is on its layer: the pattern is cleared (kept for UNDO) so nothing plays twice, and the
        // layer gets the synth's echo as its own delay and reverb.
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        finishSynthTake();
        loops.setTransport (true);
        loopsScreen.attachPart (lockLayer, lockPart);
        lockPart = nullptr;
        synthPart.clearKeepingUndo();
        synthUndoLayer = lockLayer;
        if (synthPart.knobs.echo > 0.0f)
        {
            loops.setKnob (lockLayer, fxDelay, echoToDelay (synthPart.knobs.echo));
            loops.setKnob (lockLayer, fxReverb, echoToReverb (synthPart.knobs.echo));
        }
        applyClick();
        synthScreen.partChanged();
        saveSong();
        toast ("Synth on layer " + juce::String (lockLayer + 1) + U8(". It keeps looping; the synth is free for a new part. UNDO gives this one back."));
        return;
    }
    if (st == pocket::LoopState::playing)
    {
        lockWatch.stopTimer();
        const bool pads = lockKind == LockKind::pads;
        const bool drumTake = lockKind == LockKind::drums || lockKind == LockKind::drumsLive;
        if (pads && lockPart != nullptr)
            loopsScreen.attachPart (lockLayer, lockPart);
        lockPart = nullptr;
        lockKind = LockKind::none;
        loops.setTransport (true);     // the loop keeps the clock going once the pattern stops
        b.setPatternPlaying (false);
        b.setPlaying (false);
        applyClick();
        beats.refreshPads();
        if (lessonOn && drumTake)
            lesson->showLanded (lockLayer + 1);
        toast ((pads ? "Beat locked on layer " : "Drums on layer ") + juce::String (lockLayer + 1) + ". It keeps looping. Next: another tool, then REC.");
    }
    else if (st == pocket::LoopState::empty)
    {
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        toast ("Recording called off");
    }
}

juce::String PocketApp::lengthLabel (int bars) const
{
    return bars <= 0 ? juce::String ("FREE") : bars == 1 ? juce::String ("1 BAR") : juce::String (bars) + " BARS";
}

PocketApp::RecView PocketApp::recView() const
{
    RecView v;
    auto& loops = const_cast<LoopsScreen&> (loopsScreen).engine();
    const auto& b = const_cast<BeatsScreen&> (beats).engine();
    const bool running = b.clockRunning();
    const double beat = b.clockBeat();
    auto barText = [] (double rel) { rel = juce::jmax (0.0, rel); return juce::String ((int) std::floor (rel / 4.0) + 1) + "." + juce::String ((int) std::fmod (std::floor (rel), 4.0) + 1); };

    const int busy = loopsScreen.busyLayer();
    const bool padsPass = playingIn();
    const int layer = busy >= 0 ? busy : (padsPass ? loopsScreen.nextLayer() : loopsScreen.nextLayer());
    int bars = appSettings.recBars();
    if (busy >= 0)
        bars = loops.recordingBars (busy);
    else if (page == Page::tool && recTool() == Tool::pads)
        bars = beats.drums().padPattern.hits.empty() && appSettings.recBars() > 0 ? appSettings.recBars() : beats.drums().padPattern.bars;
    else if (synthLock())
        bars = lockBars;
    else if (page == Page::tool && recTool() == Tool::chords)
        bars = chordsRecBars();
    else if (page == Page::tool && recTool() == Tool::synth)
    {
        const int patBars = synthPart.pattern.bars, want = appSettings.recBars();
        if (synthPart.mode == SynthPart::patternMode)
            bars = want > patBars ? (int) std::ceil ((double) want / patBars - 1.0e-9) * patBars : patBars;
        else
            bars = want <= 0 ? patBars : want <= 1 ? 1 : want <= 2 ? 2 : want <= 4 ? 4 : 8;
    }
    v.right = layer >= 0 ? "LAYER " + juce::String (layer + 1) + U8(" \xc2\xb7 ") + lengthLabel (bars) : juce::String ("LAYERS FULL");

    const double engineCount = loops.countInEnd();
    const double countEnd = engineCount > 0 ? engineCount : (padsPass && running && beat < padsFrom - 0.02 ? padsFrom : -1.0);
    if (running && countEnd > beat)
    {
        v.kind = RecView::counting;
        v.countBeat = (int) std::fmod (std::floor (beat), 4.0);
        v.countBar = (int) std::floor (beat / 4.0);
        v.countBars = juce::jmax (1, (int) std::ceil (countEnd / 4.0 - 1.0e-6));
        v.left = "COUNT " + juce::String (v.countBeat + 1);
        if (padsPass && lockKind == LockKind::synthPattern)
            v.after = U8("THEN THE PATTERN \xc2\xb7 ") + lengthLabel (lockBars) + U8(" ON LAYER ") + juce::String (layer + 1);
        else if (padsPass && lockKind == LockKind::synthLive)
            v.after = U8("THEN PLAY THE SYNTH \xc2\xb7 ") + lengthLabel (lockBars) + U8(" \xc2\xb7 THEN LAYER ") + juce::String (layer + 1);
        else if (padsPass)
            v.after = (lockKind == LockKind::drumsLive ? U8("THEN HIT THE PADS \xc2\xb7 ") : U8("THEN PLAY THE PADS \xc2\xb7 ")) + lengthLabel (lockBars) + U8(" \xc2\xb7 THEN LAYER ") + juce::String (layer + 1);
        else
            v.after = bars > 0 ? "THEN " + lengthLabel (bars) + " ON LAYER " + juce::String (layer + 1) + U8(" \xc2\xb7 STOPS BY ITSELF")
                               : "THEN LAYER " + juce::String (layer + 1) + U8(" \xc2\xb7 STOP ENDS IT");
    }
    else if (busy >= 0 && loops.state (busy) == pocket::LoopState::recording)
    {
        v.kind = RecView::recording;
        v.left = "REC " + barText (beat - loops.startBeat (busy));
    }
    else if (padsPass)
    {
        v.kind = RecView::padsPass;
        v.left = (lockKind == LockKind::drumsLive ? juce::String ("DRUMS ") : synthLock() ? juce::String ("SYNTH ") : juce::String ("PADS "))
                 + barText (std::fmod (juce::jmax (0.0, beat), lockLen));
    }
    else if (busy >= 0)
    {
        v.kind = RecView::waiting;
        v.left = "WAIT " + barText (beat);
    }
    else
        v.left = "BAR " + (running ? barText (beat) : juce::String ("1.1"));
    // SAMPLER: REC samples a sound for the pads, not a layer
    if (page == Page::tool && tool == Tool::sampler && busy < 0 && ! padsPass && v.kind != RecView::counting)
    {
        const bool rec = sampler.isRecording();
        v.kind = rec ? RecView::recording : RecView::idle;
        v.left = rec ? "REC " + juce::String (sampler.recordedSeconds(), 1) + "S" : juce::String ("SAMPLE");
        v.right = sampler.target() >= 0 ? "FOR PAD " + Kit::padLabel (sampler.target()) : juce::String ("FROM THE MIC");
    }
    return v;
}

void PocketApp::setSongKey (SongKey k)
{
    const bool changed = k != songKey;
    if (k.root != songKey.root)
        chordsTool.resetLiveKey();   // a new song key: the LIVE KEY starts from it
    songKey = k;
    auto& voc = vocoderScreen.engine();
    if (voc.key() != k.root || voc.minor() != k.minor)
        voc.setKey (k.root, k.minor);
    chordsTool.setKey (k);
    droneTool.setSongKey (k.root);
    droneScreen.repaint();
    saveSong();
    synthScreen.leave();
    synthScreen.repaint();
    chordsScreen.repaint();
    singScreen.repaint();
    if (changed)
        toast ("The song is in " + k.label() + ". SYNTH, CHORDS and the vocoder follow it.");
}

void PocketApp::openKeyMenu (juce::Component& c, juce::Rectangle<int> r)
{
    showSongKeyMenu (c, r, songKey, [safe = juce::Component::SafePointer<PocketApp> (this)] (SongKey k)
    {
        if (safe != nullptr)
            safe->setSongKey (k);
    });
}

void PocketApp::loadSong()
{
    auto v = juce::JSON::parse (lib.ideaRoot().getChildFile ("song.json"));
    auto& voc = vocoderScreen.engine();
    songKey = SongKey::fromVar (v.getProperty ("key", {}), SongKey { voc.key(), voc.minor() });   // older songs: the vocoder's key
    synthPart.fromVar (v.getProperty ("synth", {}));
    synthUndoLayer = -1;
    chordsTool.fromVar (v.getProperty ("chords", {}));
    chordsTool.setKey (songKey);
    applyChordsPadSound();
    chordsUndoLayer = -1;
    chordsScreen.toolChanged();
    droneTool.fromVar (v.getProperty ("drone", {}), songKey.root);
    droneScreen.toolChanged();
    song->fromVar (v.getProperty ("arrangement", {}));   // older songs: none
    savedSongEdits = song->edits();
    songScreen->selectBlock (-1);
    songScreen->setPlayhead (0);
    if (voc.key() != songKey.root || voc.minor() != songKey.minor)
        voc.setKey (songKey.root, songKey.minor);
    synthScreen.partChanged();
    boardsFromVar (v.getProperty ("boards", {}));   // older songs: none, every board neutral
}

void PocketApp::saveSong()
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("key", songKey.toVar());
    o->setProperty ("synth", synthPart.toVar());
    o->setProperty ("chords", chordsTool.toVar());
    o->setProperty ("drone", droneTool.toVar());
    if (! song->blocks.empty() || ! song->takes().empty())
        o->setProperty ("arrangement", song->toVar());   // the SONG tool
    savedSongEdits = song->edits();
    if (const auto b = boardsToVar(); ! b.isVoid())
        o->setProperty ("boards", b);
    auto file = lib.ideaRoot().getChildFile ("song.json");
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (o)));
}

void PocketApp::headerTickNow()
{
    // the vocoder's own settings can change the key too: the song follows
    {
        auto& voc = vocoderScreen.engine();
        if (voc.key() != songKey.root || voc.minor() != songKey.minor)
        {
            songKey = { voc.key(), voc.minor() };
            chordsTool.setKey (songKey);
            droneTool.setSongKey (songKey.root);
            saveSong();
            synthScreen.repaint();
            chordsScreen.repaint();
        }
    }

    const auto v = recView();
    const auto text = v.left + "|" + v.right + "|" + juce::String ((int) v.kind);
    if (text != shownHeader)
    {
        shownHeader = text;
        repaint (lcdRect().expanded (2).toNearestInt());
    }
    if (lessonOn)
        lesson->setRecBusy (recBusy());
    const bool busy = recBusy(), playing = beats.engine().clockRunning();
    if (busy != shownBusy || playing != shownPlaying)
    {
        shownBusy = busy;
        shownPlaying = playing;
        thumb.repaint();
    }
    const bool counting = v.kind == RecView::counting && page == Page::tool;
    if (counting)
        countPanel.set (v.countBeat, v.countBar, v.countBars, v.after);
    if (counting != countPanel.isVisible())
    {
        countPanel.setVisible (counting);
        if (counting)
            countPanel.toFront (false);
    }
}

//==============================================================================
void PocketApp::linkChanged()
{
    repaint();
    drawer.repaint();
    macLink->refresh();
    remote->refresh();
    sendSheet->refresh();
}

bool PocketApp::remoteShowing() const
{
    return remote != nullptr && remote->isVisible();
}

// The header, 44 high: the menu icon and the song · the display (bar › layer · length) · the Mac link dot.
// On Sounds, Remote, Mac link, Settings and the recorder the left side is a back arrow and the page's name.
juce::Rectangle<float> PocketApp::menuRect() const
{
    return { 6.0f, (float) insets.getTop() + 1.0f, 44.0f, 42.0f };
}

juce::Rectangle<float> PocketApp::chipRect() const
{
    if (page == Page::songs)
        return { (float) getWidth() - 146.0f, (float) insets.getTop() + 4.0f, 140.0f, 36.0f };   // with the Mac's name
    return { (float) getWidth() - 50.0f, (float) insets.getTop() + 1.0f, 44.0f, 42.0f };         // just the dot
}

juce::Rectangle<float> PocketApp::lcdRect() const
{
    const float w = 176.0f;
    return { chipRect().getX() - 4.0f - w, (float) insets.getTop() + 6.0f, w, 32.0f };
}

juce::Rectangle<float> PocketApp::backRect() const
{
    const float right = thumbShown() ? lcdRect().getX() : chipRect().getX();
    return { 6.0f, (float) insets.getTop() + 1.0f, right - 12.0f, 42.0f };
}

void PocketApp::mouseDown (const juce::MouseEvent& e)
{
    headerDown = e.position.y <= (float) insets.getTop() + 46.0f;
    headerSwiped = false;
    headerStart = e.position;
}

// A slide along the header bar goes to the previous or next tool (not from the left edge: that pulls the drawer out).
void PocketApp::mouseDrag (const juce::MouseEvent& e)
{
    if (! headerDown || headerSwiped || page != Page::tool || headerStart.x <= 24.0f)
        return;
    const auto d = e.position - headerStart;
    if (std::abs (d.x) > 48.0f && std::abs (d.x) > std::abs (d.y) * 2.0f)
    {
        headerSwiped = true;
        swipeTool (d.x < 0 ? 1 : -1);
    }
}

void PocketApp::swipeTool (int direction)
{
    if (page != Page::tool)
        return;
    int at = -1;
    for (int i = 0; i < (int) toolRow.tools.size(); ++i)
        if (toolRow.tools[(size_t) i] == tool)
            at = i;
    const int to = at + direction;
    if (at < 0 || to < 0 || to >= (int) toolRow.tools.size() || ! toolRow.onSelect)
        return;
    toolRow.onSelect (toolRow.tools[(size_t) to]);
}

void PocketApp::mouseUp (const juce::MouseEvent& e)
{
    const bool wasHeader = headerDown, swiped = headerSwiped;
    headerDown = headerSwiped = false;
    if (! wasHeader || swiped || e.getDistanceFromDragStart() > 12 || e.position.y > (float) insets.getTop() + 46.0f)
        return;
    if (chipRect().expanded (2).contains (e.position))
        return openMacLink();
    if (page == Page::songs)
    {
        if (backRect().contains (e.position))
            openDrawer();
        return;
    }
    if (thumbShown() && lcdRect().expanded (2).contains (e.position))
        return openRecSetup();
    if (backRect().contains (e.position))
    {
        if (page == Page::more || page == Page::recorder)
            goBack();
        else
            openDrawer();   // the menu icon, or the song's name
    }
}

void PocketApp::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);

    const int top = insets.getTop();
    const bool dark = remoteShowing();
    const auto paper = juce::Colour (0xfff1e7d6);
    if (dark)
        g.fillAll (juce::Colour (0xff1f1b17));
    const bool subPage = page == Page::more || page == Page::recorder;
    {
        auto back = backRect();
        g.setColour (dark ? paper : col::ink);
        if (subPage)
        {
            // ‹ and the page's name: tap for the way back to where you were
            juce::Path arrow;
            arrow.startNewSubPath (back.getX() + 15, back.getCentreY() - 8);
            arrow.lineTo (back.getX() + 8, back.getCentreY());
            arrow.lineTo (back.getX() + 15, back.getCentreY() + 8);
            g.strokePath (arrow, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            static const char* titles[] = { "", "Sounds", "Remote", "Mac link", "Settings" };
            g.setFont (condensedBold (22.0f));
            g.drawFittedText (page == Page::recorder ? juce::String ("Record a sound") : juce::String (titles[juce::jlimit (0, 4, morePageShown)]),
                              back.withTrimmedLeft (26).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
        }
        else
        {
            // the menu icon (the song drawer), then the song's name (Home: the wordmark)
            auto icon = menuRect();
            for (int k = -1; k <= 1; ++k)
                g.fillRoundedRectangle (icon.getCentreX() - 11.0f, icon.getCentreY() + (float) k * 7.0f - 1.5f, 22.0f, 3.0f, 1.5f);
            if (page == Page::songs)
            {
                g.setFont (fraunces (30.0f));
                g.drawText ("Kaleidobox", 54, top + 2, 200, 40, juce::Justification::centredLeft);
            }
            else
            {
                g.setFont (condensedBold (22.0f));
                g.drawFittedText (ideas.current().name, back.withTrimmedLeft (48).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
            }
        }
    }

    // the display: where the song is, then the layer the next take lands on and how long it is
    if (thumbShown())
    {
        auto r = lcdRect();
        drawWell (g, r, 9.0f);
        const auto v = recView();
        const bool red = v.kind == RecView::counting || v.kind == RecView::recording || v.kind == RecView::padsPass;
        const auto leftColour = red ? col::lcdText : col::lcdAmber;
        const juce::String sep = U8(" \xe2\x80\xba ");
        auto f = spaced (lcdFont (19.0f), 0.04f);
        const float room = r.getWidth() - 16.0f;
        const float total = juce::GlyphArrangement::getStringWidth (f, v.left + sep + v.right);
        if (total > room)
            f = f.withHeight (19.0f * room / total);
        g.setFont (f);
        float x = r.getX() + 8.0f + juce::jmax (0.0f, (room - juce::GlyphArrangement::getStringWidth (f, v.left + sep + v.right)) * 0.5f);
        auto part = [&] (const juce::String& t, juce::Colour c)
        {
            const float w = juce::GlyphArrangement::getStringWidth (f, t);
            g.setColour (c);
            g.drawText (t, juce::Rectangle<float> (x, r.getY(), w + 2.0f, r.getHeight()), juce::Justification::centredLeft);
            x += w;
        };
        part (v.left, leftColour);
        part (sep, juce::Colour (0xff8a7d6a));
        part (v.right, juce::Colour (0xffcdbfa5));
    }

    // the link to the Mac: green when linked; tap it for Link to your Mac
    auto chip = chipRect();
    const auto status = link.getStatus();
    const auto dotColour = status == LinkClient::Status::linked ? col::green : status == LinkClient::Status::connecting ? col::amber : col::grey;
    const bool dotOnly = page != Page::songs;
    if (dark && ! dotOnly)
    {
        g.setColour (juce::Colour (0xff332c26));
        g.fillRoundedRectangle (chip, 9.0f);
    }
    auto dot = dotOnly ? juce::Rectangle<float> (chip.getCentreX() - 5.5f, chip.getCentreY() - 5.5f, 11, 11)
                       : juce::Rectangle<float> (chip.getX() + 12, chip.getCentreY() - 5.5f, 11, 11);
    if (status == LinkClient::Status::linked)
    {
        g.setColour (dotColour.withAlpha (0.3f));
        g.fillEllipse (dot.expanded (3.5f));
    }
    g.setColour (dotColour);
    g.fillEllipse (dot);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (dot, 1.0f);
    if (! dotOnly)
    {
        g.setColour (dark ? paper : status == LinkClient::Status::linked ? col::ink : col::inkDim);
        g.setFont (spaced (condensed (15.0f), 0.1f));
        const auto text = status == LinkClient::Status::linked ? link.getMacName().toUpperCase()
                        : status == LinkClient::Status::connecting ? U8("LINKING…") : juce::String ("NOT LINKED");
        g.drawFittedText (text, chip.withTrimmedLeft (30).withTrimmedRight (4).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }

    g.setColour (dark ? juce::Colour (0xff3a332c) : col::rule);
    g.fillRect (0, top + 44, getWidth(), 2);
}

juce::BorderSize<int> PocketApp::safeInsets()
{
   #if JUCE_IOS
    auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    auto b = display != nullptr ? display->safeAreaInsets : juce::BorderSize<int>();
    // Never under the clock, even before the phone has said where it is.
    return { juce::jmax (b.getTop(), 59), b.getLeft(), juce::jmax (b.getBottom(), 0), b.getRight() };
   #else
    // pretend to be an iPhone with a notch (POCKET_INSETS=top,bottom pretends to be another one, for POCKET_STRESS)
    const auto env = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("POCKET_INSETS", {}), ",", {});
    if (env.size() == 2)
        return { env[0].getIntValue(), 0, env[1].getIntValue(), 0 };
    return { 47, 0, 34, 0 };
   #endif
}

void PocketApp::resized()
{
    if (toastBox == nullptr || dim == nullptr)
        return;   // still being built
    insets = safeInsets();
    insetWatch.startTimer (500);

    const int w = getWidth(), h = getHeight();
    const int toolH = 64 + insets.getBottom();
    toolRow.bottomInset = insets.getBottom();
    toolRow.setBounds (0, h - toolH, w, toolH);
    toolRow.setVisible (toolRowShown());
    thumb.setBounds (0, h - toolH - 74, w, 74);
    thumb.setVisible (thumbShown());

    const auto content = contentBounds();
    for (auto* c : std::initializer_list<juce::Component*> { &record, ideasScreen.get(), &sounds, &beats, &loopsScreen, &vocoderScreen, &singScreen, &synthScreen, &chordsScreen, &droneScreen, songScreen.get(), &sampler, effects.get(), remote.get(), macLink.get(), settings.get() })
        c->setBounds (content);
    padPicker.setBounds (padPickOpen ? padPickerBounds() : padPickerBounds().withY (h));
    chordEdit.bottomInset = insets.getBottom();
    chordEdit.setBounds (chordEditOpen ? chordEditBounds() : chordEditBounds().withY (h));
    drumKit.bottomInset = insets.getBottom();
    drumKit.setBounds (drumKitOpen ? drumKitBounds() : drumKitBounds().withY (h));
    countPanel.setBounds (16, content.getY() + 8, w - 32, 136);
    sendSheet->setBounds (sendOpen ? sendSheetBounds() : sendSheetBounds().withY (h));

    dim->setBounds (0, 0, w, h);
    drawer.insets = insets;
    drawer.setBounds (0, 0, w, h);
    holdCard.setBounds (0, 0, w, h);
    layoutSheet (sheetOpen);
    padSheet.setBounds (padSheetOpen ? padSheetBounds() : padSheetBounds().withY (h));
    tempoSheet.setBounds (tempoOpen ? tempoBounds() : tempoBounds().withY (h));
    recSetup.setBounds (recSetupOpen ? recSetupBounds() : recSetupBounds().withY (h));
    headphones.setBounds (phonesOpen ? headphonesBounds() : headphonesBounds().withY (h));
    soundBank.setBounds (bankOpen ? tempoBounds().withTop (h - 560 - insets.getBottom()) : tempoBounds().withTop (h - 560 - insets.getBottom()).withY (h));
    toastBox->setBounds (24, content.getBottom() - 60, w - 48, 52);
    title->setBounds (0, 0, w, h);
    tour->setBounds (0, 0, w, h);
    lesson->setBounds (0, 0, w, h);
    layoutLesson();
}

//==============================================================================
// The Make a beat lesson
void PocketApp::startLesson()
{
    showTitle (false);
    closeAllSheets();
    tour->setVisible (false);
    showTool (Tool::drums, false);
    beats.setDrumMode (0);          // STEPS: the lesson talks about the big step boxes
    lessonOn = true;
    beats.setLessonLayout (true, BeatLesson::kCardHeight + 6);
    layoutLesson();
    lesson->setVisible (true);
    lesson->toFront (false);
    lesson->begin();
}

void PocketApp::endLesson()
{
    if (! lessonOn)
        return;
    lessonOn = false;
    lessonClick = false;
    lesson->setVisible (false);
    beats.setGuide (-1, 0);
    beats.setLessonLayout (false);
    applyClick();
    headerTickNow();
}

void PocketApp::layoutLesson()
{
    const int h = BeatLesson::kCardHeight;
    lesson->setCardBounds ({ 12, thumb.getY() - 6 - h, getWidth() - 24, h });
    if (lessonOn)
        beats.setLessonLayout (true, h + 6);
}

// What each step of the lesson sets up on the real screen. Only glows: nothing is ever written for the player.
void PocketApp::applyLessonPhase (int phase)
{
    auto& b = beats.engine();
    lessonClick = phase != BeatLesson::landed;
    if (phase == BeatLesson::kick || phase == BeatLesson::snare || phase == BeatLesson::hats)
    {
        // the pattern loops with the click, so every box tapped is heard in the loop
        if (! b.isPlaying())
            b.setPlaying (true);
        const int voice = phase == BeatLesson::kick ? 0 : phase == BeatLesson::snare ? 1 : 9;
        const juce::uint32 mask = phase == BeatLesson::kick ? ((1u << 0) | (1u << 10))
                                : phase == BeatLesson::snare ? ((1u << 4) | (1u << 12)) : 0x5555u;
        beats.selectVoice (voice);
        beats.setGuide (voice, mask);
    }
    else
        beats.setGuide (-1, 0);
    applyClick();
    thumb.repaint();
    headerTickNow();
}

//==============================================================================
// The opening title and the tour
void PocketApp::showTitle (bool shown)
{
    if (shown)
    {
        // PICK UP WHERE YOU LEFT OFF: the open song, when it has layers
        const auto f = IdeasScreen::factsOf (ideas.folderFor (ideas.current().id));
        std::vector<juce::Colour> colours;
        for (int t : f.tools)
            colours.push_back (layerToolColour (t));
        juce::String meta;
        if (f.bpm > 0.0)
            meta << juce::String (f.bpm, 0) << U8(" BPM \xc2\xb7 ");
        meta << f.layers << (f.layers == 1 ? " layer" : " layers");
        title->setContinue (f.layers > 0 ? ideas.current().name : juce::String(), meta, colours);
    }
    title->setVisible (shown);
    if (shown)
        title->toFront (false);
}

void PocketApp::startTour()
{
    endLesson();
    showTitle (false);
    closeAllSheets();
    tour->setBounds (getLocalBounds());
    tour->setVisible (true);
    tour->begin();
}

void PocketApp::endTour()
{
    tour->setVisible (false);
    showIdeasList();
    toast ("That's the tour. + NEW SONG starts one: pick a tool, press REC.");
}

juce::Rectangle<int> PocketApp::tourStop (int stop)
{
    switch (stop)
    {
        case 0:
            showTool (Tool::loops, false);
            return toolRow.getBounds().withTrimmedBottom (insets.getBottom()).reduced (4, 0);
        case 1:
            showTool (Tool::pads, false);
            return getLocalArea (&thumb, thumb.buttonRect (ThumbBar::rec).toNearestInt()).expanded (2);
        case 2:
            showTool (Tool::chords, false);
            return getLocalArea (&chordsScreen, chordsScreen.tourSpot());
        case 3:
            showTool (Tool::pads, false);
            return getLocalArea (&beats, beats.tourSpot (0));
        case 4:
            showTool (Tool::loops, false);
            return getLocalArea (&loopsScreen, loopsScreen.tourSpot (0));
        case 5:
            showTool (Tool::vocoder, false);
            return getLocalArea (&singScreen, singScreen.bannerSpot());
        case 6:
            showTool (Tool::loops, false);
            return { 4, insets.getTop() + 1, 150, 42 };   // the menu icon and the song's name: the drawer
        default:
            return {};
    }
}

//==============================================================================
// Screenshot mode
static crate::SharedAudio demoSound (double seconds, int kind)
{
    const int n = (int) (seconds * crate::kSampleRate);
    juce::AudioBuffer<float> b (1, n);
    juce::Random rng (kind * 77 + 3);
    auto* d = b.getWritePointer (0);
    float lp = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / crate::kSampleRate;
        float env;
        float s;
        switch (kind)
        {
            case 0:   // rain: noise with a slow swell
                env = (float) (0.45 + 0.35 * std::sin (t * 0.9) * std::sin (t * 0.9) + 0.2 * std::sin (t * 3.1 + 1.0));
                lp += 0.35f * (rng.nextFloat() * 2 - 1 - lp);
                s = lp * 1.6f;
                break;
            case 1:   // a hit
                env = (float) std::exp (-t * 14.0);
                s = (float) std::sin (2 * juce::MathConstants<double>::pi * (60 + 120 * std::exp (-t * 30)) * t);
                break;
            default:  // a tone with a little movement
                env = (float) (0.5 + 0.5 * std::sin (t * 2.0)) * (float) std::exp (-t * 0.4);
                s = (float) std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t) * 0.6f + 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 330.0 * t);
                break;
        }
        d[i] = s * env * 0.8f;
    }
    return crate::share (std::move (b));
}

void PocketApp::fillDemoLibrary (Library& lib)
{
    auto now = juce::Time::getCurrentTime();
    auto day = [&] (int month, int dayOfMonth) { return juce::Time (now.getYear(), month, dayOfMonth, 12, 0); };
    struct Demo { const char* name; const char* tag; juce::Time when; double secs; int kind; };
    const Demo demos[] = {
        { "Thumb piano", kTagRecorded, day (10, 9), 5.2, 2 },
        { "Street noise loop", kTagRecorded, day (10, 10), 16.0, 0 },
        { "Rhodes stab Cm", kTagFromMac, day (10, 12), 2.1, 2 },
        { "Tape snare", kTagImported, day (10, 14), 0.8, 1 },
        { "Dusty kick", kTagImported, day (10, 14), 0.9, 1 },
        { "Kids choir \"ooh\"", kTagRecorded, now - juce::RelativeTime::days (1), 8.3, 2 },
        { "Kitchen pots hit", kTagRecorded, now, 3.2, 1 },
        { "Hand drums loop 92 Dmin", kTagImported, now - juce::RelativeTime::days (1), 480.0 / 92.0, 0 },
        { "Shaker groove 92 BPM", kTagFromMac, now, 240.0 / 92.0, 1 },
        { "Porch rain", kTagRecorded, now, 12.4, 0 },
    };
    for (auto& d : demos)
        lib.add (d.name, d.tag, *demoSound (d.secs, d.kind), {}, {}, d.when);
}

void PocketApp::savePng (const juce::File& file)
{
    headerTickNow();
    toastBox->setVisible (false);   // a passing note would cover the screen it's on
    auto image = createComponentSnapshot (getLocalBounds(), true, 2.0f);
    file.deleteFile();
    if (auto out = file.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (image, *out);
    }
}

void PocketApp::writeSnapshots (const juce::File& folder)
{
    folder.createDirectory();
    animate = false;
    applyAnimate();

    // Linked to a Mac (made up), so the header shows it as in the design.
    crate::link::Message state ("state");
    {
        juce::Array<juce::var> sections, mutes, layers;
        for (auto* n : { "Intro", "Verse", "Chorus", "Verse 2", "Bridge", "Outro" })
            sections.add (n);
        for (int i = 0; i < 8; ++i)
            mutes.add (i == 3 || i == 7);
        for (int i = 0; i < 8; ++i)
            layers.add (i < 3 ? 3 : 0);
        auto* rec = new juce::DynamicObject();
        rec->setProperty ("set", 0);
        rec->setProperty ("take", 0);
        rec->setProperty ("moves", 0);
        state.set ("bpm", 97.0).set ("playing", true).set ("bpb", 4).set ("bar", 12.3).set ("sections", sections).set ("section", 1)
             .set ("song", true).set ("barIn", 5.3).set ("sectionBars", 8).set ("next", 2).set ("mutes", mutes).set ("layers", layers)
             .set ("remoteLayer", -1).set ("rec", juce::var (rec)).set ("show", "Friday Set");
    }
    const std::vector<LinkClient::MacRow> demoMacs { { "Colin's MacBook", "Friday Set", true, true }, { "Studio iMac", {}, false, true } };
    link.setDemo (demoMacs, "Colin's MacBook", state);
    pocket::setDemoRoute ((int) pocket::Route::wired);
    route = pocket::currentRoute();

    auto take = lib.load (lib.items().front().id);

    // Home: how it works, + NEW SONG, the lesson and the tour, then the songs with their layers' tools
    {
        const auto now = juce::Time::getCurrentTime();
        const auto day = juce::RelativeTime::days (1);
        auto layersOf = [this] (const juce::String& id, double bpm, std::initializer_list<pocket::LayerTool> tools)
        {
            auto dir = ideas.folderFor (id).getChildFile ("Loops");
            dir.createDirectory();
            auto* root = new juce::DynamicObject();
            juce::Array<juce::var> list;
            int l = 0;
            for (auto t : tools)
            {
                auto* o = new juce::DynamicObject();
                // a short sound of the right shape, so My ideas draws its lanes: hits, notes or a voice
                const int sr = 22050, beats = (l % 2 == 0) ? 16 : 8;
                const int len = bpm > 0.0 ? (int) (60.0 / bpm * beats * sr) : sr * 12;
                juce::AudioBuffer<float> sound (1, len);
                juce::Random rng (l * 31 + (int) t * 7 + 3);
                for (int i = 0; i < len; ++i)
                {
                    const double secs = (double) i / sr, beat = secs * juce::jmax (60.0, bpm) / 60.0;
                    const double ph = beat - std::floor (beat);
                    float v = 0.0f;
                    if (t == pocket::LayerTool::drums || t == pocket::LayerTool::pads)
                        v = (float) (std::exp (-ph * 14.0) * (((int) beat % 4 == 2) ? 0.6 : 1.0)) * (rng.nextFloat() * 2.0f - 1.0f);
                    else if (t == pocket::LayerTool::synth || t == pocket::LayerTool::chords)
                        v = (float) ((ph < 0.6 ? 0.8 : 0.1) * (0.5 + 0.5 * std::sin ((int) beat * 1.7)) * std::sin (secs * 2200.0));
                    else
                        v = (float) ((0.35 + 0.65 * std::abs (std::sin (secs * 2.3) * std::sin (secs * 0.7 + l))) * std::sin (secs * 1400.0 + std::sin (secs * 9.0)));
                    sound.setSample (0, i, v * 0.7f);
                }
                const auto wav = dir.getChildFile ("demo-layer-" + juce::String (l + 1) + ".wav");
                wav.deleteFile();
                if (auto out = std::unique_ptr<juce::FileOutputStream> (wav.createOutputStream()))
                {
                    juce::WavAudioFormat format;
                    if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (format.createWriterFor (out.get(), sr, 1, 16, {}, 0)))
                    {
                        out.release();
                        writer->writeFromAudioSampleBuffer (sound, 0, len);
                    }
                }
                o->setProperty ("file", wav.getFileName());
                o->setProperty ("beats", (double) beats);
                o->setProperty ("layer", l++);
                o->setProperty ("tool", (int) t);
                list.add (juce::var (o));
            }
            root->setProperty ("bpm", bpm);
            root->setProperty ("layers", list);
            dir.getChildFile ("loops.json").replaceWithText (juce::JSON::toString (juce::var (root)));
        };
        using LT = pocket::LayerTool;
        ideas.rename (ideas.current().id, "Train horn song");
        ideas.setDemo (ideas.current().id, stepSing, false, {}, now - juce::RelativeTime::minutes (20));
        layersOf (ideas.current().id, 92.0, { LT::drums, LT::synth, LT::vocoder });
        auto porch = ideas.create ("Porch rain groove");
        ideas.setDemo (porch, stepBeat, false, {}, now - day);
        ideas.setStarred (porch, true);
        layersOf (porch, 84.0, { LT::pads, LT::drums, LT::chords, LT::mic });
        auto hum = ideas.create ("Humming in the car");
        ideas.setDemo (hum, stepRecord, false, {}, now - juce::RelativeTime::days (3));
        layersOf (hum, 0.0, { LT::mic });
        ideas.setDemo (ideas.create ("Late night vocoder"), stepSend, true, {}, now - juce::RelativeTime::days (4));
    }
    showIdeasList();
    savePng (folder.getChildFile ("00-songs.png"));
    // further down My ideas: tonight's spark and the tools, then your songs
    ideasScreen->scrollTo (ideasScreen->getHeight() - 140);
    savePng (folder.getChildFile ("00-songs-2.png"));
    ideasScreen->scrollTo (100000);
    savePng (folder.getChildFile ("00-songs-3.png"));
    ideasScreen->scrollTo (0);

    // The sound recorder (from PADS > RECORD A SOUND, or the drawer's RECORD A SOUND) and its Effects + Chop sheet
    showPage (Page::recorder);
    record.showDemoTake (take, "Porch rain", false);
    savePng (folder.getChildFile ("01-recorder.png"));
    record.showDemoTake (take, "Porch rain", true);
    savePng (folder.getChildFile ("01b-recorder-recording.png"));
    record.showDemoTake (take, "Porch rain", false);
    auto& k = record.knobs();
    k.v[FxKnobs::pitch] = -2;
    k.v[FxKnobs::lowCut] = 80;
    k.v[FxKnobs::drive] = 0.24;
    k.v[FxKnobs::bits] = 12;
    k.v[FxKnobs::reverb] = 0.3;
    openSheet();
    sheet.setKnobs (k);
    resized();
    savePng (folder.getChildFile ("02-effects-chop.png"));
    sheet.chopWave.setMarkers ({ take->getNumSamples() / 5, take->getNumSamples() * 2 / 5, take->getNumSamples() * 3 / 5, take->getNumSamples() * 4 / 5 });
    sheet.chopWave.onMarkersChange();
    savePng (folder.getChildFile ("02b-chop-markers.png"));
    closeSheet();

    // LOOPS: drums, a synth part (picked: the panel), a vocoder take, a pads pattern, a muted mic take, the next row
    beats.setBpm (92.0);
    loopsScreen.showDemo();
    showTool (Tool::loops);
    savePng (folder.getChildFile ("03-tool-loops.png"));

    // The song drawer (over LOOPS and over Home), a hold on a layer, a hold on a song, and a slide along the tool row
    openDrawer();
    savePng (folder.getChildFile ("20-drawer-on-tool.png"));
    drawer.holdRow (1);
    savePng (folder.getChildFile ("24b-hold-song-in-drawer.png"));
    holdCard.close();
    closeDrawer();
    loopsScreen.holdLayer (1);
    savePng (folder.getChildFile ("21-hold-layer.png"));
    holdCard.close();
    {
        auto cellX = [this] (Tool t)
        {
            for (int i = 0; i < (int) toolRow.tools.size(); ++i)
                if (toolRow.tools[(size_t) i] == t)
                    return toolRow.cellRect (i).getCentreX();
            return 0.0f;
        };
        toolRow.slideStart (cellX (Tool::loops));
        toolRow.slideTo (cellX (Tool::chords));
        savePng (folder.getChildFile ("25-toolrow-slide.png"));
        toolRow.slideEnd (cellX (Tool::chords), -200.0f);   // (called off: it stays on LOOPS)
    }
    showIdeasList();
    openDrawer();
    savePng (folder.getChildFile ("20b-drawer-on-home.png"));
    closeDrawer();
    ideasScreen->holdRow (1);
    savePng (folder.getChildFile ("24-hold-song-on-home.png"));
    holdCard.close();
    ideasScreen->holdRow (0);   // the open song: DELETE asks on the app's own card, then opens your last other song
    holdCard.choose (2);
    savePng (folder.getChildFile ("24c-delete-open-song-confirm.png"));
    holdCard.close();
    showTool (Tool::loops);
    loopsScreen.selectLayer (2);
    savePng (folder.getChildFile ("03a-loops-audio-picked.png"));
    openTempo();
    savePng (folder.getChildFile ("03b-tempo.png"));
    closeTempo();
    openRecSetup();
    savePng (folder.getChildFile ("03c-rec-setup.png"));
    closeRecSetup();
    loopsScreen.showEditorDemo (0);
    savePng (folder.getChildFile ("03d-loop-editor-shape-audio.png"));
    loopsScreen.closeEditor();
    loopsScreen.showEditorDemo (1);
    savePng (folder.getChildFile ("03e-loop-editor-shape-notes.png"));
    loopsScreen.showEditorDemo (2);
    savePng (folder.getChildFile ("03f-loop-editor-effects.png"));
    loopsScreen.leave();

    // DRUMS: the drum machine with a beat in it (a new song's grid is empty: this one is filled for the picture)
    beats.fillDemoDrums();
    beats.drums().variation = 2;
    beats.drums().bpm = 97.0;
    beats.drums().settings.swing = 0.16f;
    beats.drums().settings.voices[0].level = 0.8f;
    beats.drums().settings.voices[0].decay = 0.62f;
    beats.drums().settings.voices[0].tone = 0.45f;
    beats.setBpm (97.0);
    beats.drumsChanged();
    showTool (Tool::drums);
    beats.selectVoice (0);
    beats.setDemoStep (4);
    savePng (folder.getChildFile ("04-tool-drums.png"));
    beats.selectVoice (1);
    beats.setDemoStep (8);
    savePng (folder.getChildFile ("04a-drums-steps-snare.png"));   // another row picked from the overview
    beats.setDemoStep (-1);
    beats.setDrumMode (1);
    savePng (folder.getChildFile ("04b-drums-play-live.png"));
    beats.setDrumMode (0);
    beats.selectVoice (0);

    // DRUMS > KIT: a few of your own sounds on drum sounds (a blue dot on each), and the KIT sheet for the snare
    {
        const auto& own = lib.items();
        int put = 0;
        for (int v : { 0, 1, 9 })
            if (put < (int) own.size() && beats.setDrumSound (v, own[(size_t) put++].id, false))
                continue;
        beats.selectVoice (1);
        savePng (folder.getChildFile ("04c-drums-your-sounds.png"));
        openDrumKit();
        savePng (folder.getChildFile ("04d-drums-kit-sheet.png"));
        closeDrumKit();
        for (int v = 0; v < 12; ++v)
            beats.clearDrumSound (v);
        beats.selectVoice (0);
    }

    // PADS: a demo kit, then a pattern, the pad sheet, the sound bank and the pad's sample page
    beats.fillDemoKit();
    showTool (Tool::pads);
    savePng (folder.getChildFile ("05-tool-pads.png"));
    beats.holdPad (9);
    savePng (folder.getChildFile ("22-hold-pad.png"));
    holdCard.close();
    beats.startLevels (9);
    toastBox->setVisible (false);
    savePng (folder.getChildFile ("22b-pads-16-levels.png"));
    beats.endPadMode();
    beats.startCopy (9);
    toastBox->setVisible (false);
    savePng (folder.getChildFile ("22c-pads-copy.png"));
    beats.endPadMode();
    beats.selectPad (-1);
    beats.fillDemoPattern();
    savePng (folder.getChildFile ("05b-pads-pattern.png"));
    openPadSheet (4);
    resized();
    savePng (folder.getChildFile ("05c-pad-sheet.png"));
    closePadSheet();
    openSoundBank();
    savePng (folder.getChildFile ("05d-sound-bank.png"));
    closeSoundBank();
    padSheet.onTrim (4);   // SAMPLE PAGE: the pad's sound on the SAMPLER
    savePng (folder.getChildFile ("05e-pad-sample-page.png"));
    record.endPadMode();

    // SAMPLER: empty (REC samples a sound from the mic), a sound with IN / OUT, and SEND TO PAD > PICK PAD (the 4 x 4 grid)
    showTool (Tool::sampler, false);
    sampler.clear();
    sampler.setTarget (-1);
    savePng (folder.getChildFile ("30-sampler-empty.png"));
    sampler.setTarget (6);
    savePng (folder.getChildFile ("30a-sampler-empty-for-pad.png"));
    {
        const int n = take->getNumSamples();
        sampler.showDemo (take, "Porch rain", { n * 22 / 100, n * 61 / 100 }, 6);
    }
    savePng (folder.getChildFile ("30b-sampler-sound.png"));
    openPadPicker();
    savePng (folder.getChildFile ("30c-sampler-pick-pad.png"));
    closePadPicker();
    sampler.clear();

    // SYNTH: KEYS (Rubber Funk with C held, as in the design), PATTERN (a line, step 5 picked), ARP, CHROMATIC
    {
        setSongKey ({ 9, true });
        synthPart.reset();
        synthPart.setPreset (synthPresetIndex ("Rubber Funk"));
        synthPart.knobs.tone = 0.62f;
        synthPart.knobs.echo = 0.15f;
        synthPart.apply();
        synthPart.pattern.setBars (1);
        const int line[8][2] = { { 0, 57 }, { 3, 57 }, { 4, 60 }, { 6, 64 }, { 8, 62 }, { 10, 60 }, { 12, 55 }, { 14, 57 } };
        for (auto& n : line)
            synthPart.pattern.setStep (n[0], n[1]);
        synthPart.pattern.tieStep (12);
        synthPart.patternChanged();
        showTool (Tool::synth);
        synthScreen.setDemoHeld ({ 2 });
        savePng (folder.getChildFile ("14-tool-synth-keys.png"));
        synthScreen.setDemoHeld ({});
        synthScreen.setMode (SynthPart::patternMode);
        synthScreen.selectStep (4);
        savePng (folder.getChildFile ("14b-synth-pattern.png"));
        synthScreen.selectStep (-1);
        synthPart.setPreset (synthPresetIndex ("Bell Pluck"));
        synthPart.arpHold = true;
        synthScreen.setMode (SynthPart::arpMode);
        synthScreen.setDemoHeld ({ 0, 2, 4 });
        savePng (folder.getChildFile ("14c-synth-arp.png"));
        synthScreen.setDemoHeld ({ 4, 9 });
        synthPart.chromatic = true;
        synthScreen.setMode (SynthPart::keysMode);
        savePng (folder.getChildFile ("14d-synth-chromatic.png"));
        synthScreen.setDemoHeld ({});
        synthPart.reset();
        synthScreen.partChanged();
    }

    // CHORDS in A minor: a chord held, then the progression playing its second bar, then 8 boxes
    auto chordsRender = [this] (bool running, double beat)
    {
        std::vector<float> l (256), r (256);
        chordsTool.engine().renderAt (l.data(), r.data(), 256, running, beat, 120.0);   // (no sound card here)
    };
    {
        setSongKey ({ 9, true });
        chordsTool.reset();
        chordsTool.setKey (songKey);
        chordsScreen.toolChanged();
        showTool (Tool::chords);
        chordsTool.padDown (3, false);
        chordsRender (false, 0.0);
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("15-tool-chords.png"));
        chordsScreen.holdPad (3);
        savePng (folder.getChildFile ("23-hold-chord.png"));
        holdCard.close();
        chordsTool.releaseAll();
        chordsTool.progression = { 0, 3, 5, 6, -1, -1, -1, -1 };
        chordsTool.progressionOn = true;
        chordsTool.style = pocket::styleStabs;
        chordsTool.apply();
        chordsRender (true, 4.5);
        chordsScreen.repaint();
        savePng (folder.getChildFile ("15b-chords-progression.png"));
        chordsTool.progLength = 8;
        chordsTool.progression = { 0, 3, 5, 6, 0, 3, 4, 7 };
        chordsTool.colour = (int) pocket::ChordColour::plain;
        chordsTool.style = pocket::styleChord;
        chordsTool.bass = true;
        chordsTool.editingBox = 6;
        chordsTool.apply();
        chordsScreen.toolChanged();
        chordsRender (true, 4.0 * 6 + 0.5);
        savePng (folder.getChildFile ("15c-chords-8-boxes.png"));
        // LIVE KEY open, playing in B (the song in A), a chord held
        chordsTool.reset();
        chordsTool.setKey (songKey);
        chordsTool.liveOpen = true;
        chordsTool.liveOnBar = false;
        chordsTool.setLiveKey (11);
        chordsScreen.toolChanged();
        chordsTool.padDown (3, false);
        chordsRender (false, 0.0);
        chordsScreen.repaint();
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("15d-chords-live-key.png"));
        chordsTool.releaseAll();
        // CHORDS OFF: each pad plays one note (its chord's lowest), a pad held
        chordsTool.reset();
        chordsTool.setKey (songKey);
        chordsTool.setChordsOn (false);
        chordsScreen.toolChanged();
        chordsTool.padDown (3, false);
        chordsRender (false, 0.0);
        chordsScreen.repaint();
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("15e-chords-off.png"));
        chordsTool.releaseAll();
        chordsTool.reset();
        chordsTool.setKey (songKey);
        chordsScreen.toolChanged();
    }

    // DRONE: on, the strings ringing (a few seconds rendered so they show), then singing into them with the mic off
    {
        droneTool.reset();
        droneTool.setSongKey (songKey.root);
        droneTool.on = true;
        droneTool.prepare (44100.0);
        std::vector<float> l (512), r (512);
        for (int i = 0; i < 44100 * 3 / 512; ++i)
            droneTool.renderAt (l.data(), r.data(), 512, false, 0.0, 97.0);
        micOn = micHear = false;
        applyMic();
        showTool (Tool::drone, false);
        for (int i = 0; i < 12; ++i)
            droneScreen.tickForDemo();
        savePng (folder.getChildFile ("17-tool-drone.png"));
        droneTool.resonate = 0.6f;
        droneScreen.repaint();
        savePng (folder.getChildFile ("17b-drone-sing-mic-off.png"));
        // its second page: PATTERN and TONE, with 16 steps drawn and the swarmandal picked
        droneTool.resonate = 0.0f;
        droneTool.voice = 2;
        droneTool.setStepCount (2);
        droneTool.toggleStep (3, 1);
        droneTool.toggleStep (3, 2);
        droneTool.toggleStep (9, 3);
        droneTool.swing = 0.3f;
        droneTool.space = 0.4f;
        droneScreen.showPage (1);
        savePng (folder.getChildFile ("17c-drone-pattern.png"));
        droneScreen.showPage (0);
        droneTool.reset();
        droneTool.setSongKey (songKey.root);
        droneScreen.toolChanged();
    }

    // VOCODER: the mic off, the headphones sheet (wired, Bluetooth, none), then the banner. The CHORDS tool holds
    // Dm7, so the vocoder shows the chord CHORDS is sending.
    chordsTool.padDown (3, false);
    chordsRender (false, 0.0);
    singScreen.showDemo();
    micOn = micHear = false;
    applyMic();
    showTool (Tool::vocoder, false);
    singScreen.repaint();
    savePng (folder.getChildFile ("06-tool-vocoder-mic-off.png"));
    openHeadphones ({});
    savePng (folder.getChildFile ("06b-headphones-wired.png"));
    pocket::setDemoRoute ((int) pocket::Route::bluetooth);
    openHeadphones ({});
    savePng (folder.getChildFile ("06c-headphones-bluetooth.png"));
    pocket::setDemoRoute ((int) pocket::Route::none);
    openHeadphones ({});
    savePng (folder.getChildFile ("06d-headphones-none.png"));
    pocket::setDemoRoute ((int) pocket::Route::wired);
    chooseHeadphones (true);
    toastBox->setVisible (false);
    savePng (folder.getChildFile ("06e-tool-vocoder-banner.png"));
    pocket::setDemoRoute ((int) pocket::Route::bluetooth);
    routeChanged();
    toastBox->setVisible (false);
    savePng (folder.getChildFile ("06f-tool-vocoder-bluetooth.png"));
    // the progression running: the VOCODER tool shows which chord CHORDS is sending, and CHORDS · ... › opens it
    chordsTool.releaseAll();
    chordsTool.progression = { 0, 3, 5, 6, -1, -1, -1, -1 };
    chordsTool.progressionOn = true;
    chordsTool.apply();
    chordsRender (true, 8.5);
    toastBox->setVisible (false);
    savePng (folder.getChildFile ("16-vocoder-linked-chord.png"));
    chordsTool.reset();
    chordsTool.setKey (songKey);
    chordsRender (false, 0.0);
    pocket::setDemoRoute ((int) pocket::Route::wired);
    routeChanged();
    micHear = true;
    applyMic();
    vocoderScreen.showDemo();
    showPage (Page::vocoderSettings);
    savePng (folder.getChildFile ("06g-vocoder-settings.png"));

    // The pages the drawer opens (it replaced the old More list)
    showPage (Page::more, 4);
    savePng (folder.getChildFile ("10-settings.png"));
    showPage (Page::more, 1);
    savePng (folder.getChildFile ("09-sounds.png"));
    sounds.setTempo (crate::Tempo::Filter::exactly (92.0));
    savePng (folder.getChildFile ("09b-sounds-at-92-bpm.png"));
    sounds.setTempo ({});
    sendSoundToMac (lib.items().front().id, "sounds");
    resized();
    savePng (folder.getChildFile ("13-send-to-mac.png"));
    closeSendSheet();
    showPage (Page::more, 2);
    savePng (folder.getChildFile ("12-remote.png"));
    link.setDemo ({ { "Colin's MacBook", "Friday Set", true, false }, { "Studio iMac", {}, false, true } }, {}, {});
    showPage (Page::more, 3);
    macLink->setDemoCode ("472");
    savePng (folder.getChildFile ("11-link-mac.png"));
    showPage (Page::more, 2);
    savePng (folder.getChildFile ("12b-remote-not-linked.png"));
    link.setDemo (demoMacs, "Colin's MacBook", state);

    // The opening title and every stop of the tour
    title->setDemoAngle (0.3f);
    showTitle (true);
    savePng (folder.getChildFile ("00a-title.png"));
    showTitle (false);
    startTour();
    for (int s = 0; s < (int) TourOverlay::stops().size(); ++s)
    {
        tour->goTo (s);
        savePng (folder.getChildFile ("00b-tour-" + juce::String (s + 1) + ".png"));
    }
    tour->setVisible (false);
    toastBox->setVisible (false);

    // Checks for the things a picture can't show: TO PAD puts a sound on the next empty pad of the bank
    // and shows it, and an export for the MPC reads back through the MPC file code.
    juce::StringArray problems;
    showPage (Page::more, 1);
    const auto* firstSound = &lib.items().front();
    sendToPad (firstSound->id);
    if (beats.selectedPad() != 2 || beats.kit().pads[2].empty() || page != Page::tool || tool != Tool::pads || beats.mode() != 1)
        problems.add ("TO PAD did not land on A03 in PADS");
    savePng (folder.getChildFile ("07-to-pad.png"));
    if (const auto why = beats.selfCheckExport (folder.getChildFile ("export-check")); why.isNotEmpty())
        problems.add ("MPC export: " + why);

    if (const auto why = beats.selfCheckSound(); why.isNotEmpty())
        problems.add ("Sound: " + why);
    if (const auto why = beats.selfCheckPattern(); why.isNotEmpty())
        problems.add ("Pad pattern: " + why);
    if (const auto why = beats.selfCheckLive(); why.isNotEmpty())
        problems.add ("Drums: " + why);
    juce::String synthReport;
    if (const auto why = synthSelfCheck (synthReport); why.isNotEmpty())
        problems.add ("Synth: " + why);
    juce::String chordsReport;
    if (const auto why = chordsSelfCheck (chordsReport); why.isNotEmpty())
        problems.add ("Chords: " + why);

    // Ideas: each keeps its own pads and beat
    {
        const auto first = ideas.current().id;
        const int pads = beats.kit().usedPads();
        const auto hits = beats.drums().padPattern.hits.size();
        const auto check = ideas.create ("Self-check idea");
        switchIdea (check);
        const int emptyPads = beats.kit().usedPads();
        const auto emptyHits = beats.drums().padPattern.hits.size();
        bool emptyDrums = true;
        for (auto& pair : beats.drums().patterns)
            for (auto& p : pair)
                emptyDrums = emptyDrums && p.isEmpty();
        switchIdea (first);
        if (! emptyDrums)
            problems.add ("Ideas: a new idea's drum machine was not empty (no pre-made beat)");
        if (emptyPads != 16 || emptyHits != 0)
            problems.add ("Ideas: a new idea didn't start with the Street kit (" + juce::String (emptyPads) + " of 16 pads) and an empty beat");
        if (beats.kit().usedPads() != pads || beats.drums().padPattern.hits.size() != hits)
            problems.add ("Ideas: going back didn't bring the pads (" + juce::String (beats.kit().usedPads()) + " of " + juce::String (pads)
                          + ") and beat (" + juce::String ((int) beats.drums().padPattern.hits.size()) + " of " + juce::String ((int) hits) + ") back");
        if (! ideas.remove (check))
            problems.add ("Ideas: couldn't delete an idea");
    }

    // The factory kits: 16 sounds each, none silent, none clipped, none overlong
    for (int kit = 0; kit < (int) factoryKits().size(); ++kit)
    {
        auto made = makeFactorySounds (kit);
        if (made.size() != 16)
            problems.add (juce::String ("Factory kit ") + factoryKits()[(size_t) kit].name + " has " + juce::String ((int) made.size()) + " sounds");
        auto dir = folder.getChildFile ("factory").getChildFile (factoryKits()[(size_t) kit].name);
        dir.createDirectory();
        for (auto& [name, b] : made)
        {
            juce::WavAudioFormat wav;
            if (auto stream = std::unique_ptr<juce::OutputStream> (dir.getChildFile (name + ".wav").createOutputStream()))
                if (std::unique_ptr<juce::AudioFormatWriter> w { wav.createWriterFor (stream.get(), 44100.0, 1, 16, {}, 0) })
                {
                    stream.release();
                    w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
                }
            const float peak = b.getNumSamples() > 0 ? b.getMagnitude (0, b.getNumSamples()) : 0.0f;
            const double secs = b.getNumSamples() / 44100.0;
            if (peak < 0.5f || peak > 0.95f || secs < 0.02 || secs > 4.5 || std::isnan (peak))
                problems.add ("Factory sound " + name + ": peak " + juce::String (peak, 2) + ", " + juce::String (secs, 2) + " s");
        }
    }

    juce::String loopsReport;
    if (const auto why = LoopsScreen::selfCheck (loopsReport); why.isNotEmpty())
        problems.add ("Loops: " + why);

    juce::String toolsReport;
    if (const auto why = loopToolsSelfCheck (toolsReport); why.isNotEmpty())
        problems.add ("Loop editor: " + why);

    juce::String vocoderReport;
    if (const auto why = VocoderEngine::selfCheck (vocoderReport); why.isNotEmpty())
        problems.add ("Vocoder: " + why);

    // One REC on every tool, the count-in and the mic gate, through the app itself
    juce::String recReport;
    if (const auto why = runRecChecks (recReport); why.isNotEmpty())
        problems.add (why);
    juce::String editReport;
    if (const auto why = runLoopEditChecks (editReport); why.isNotEmpty())
        problems.add ("Loop editor in the app: " + why);

    // EFFECTS: the pedals themselves, then through the app (bit-identical when untouched, REC through a board, ...)
    juce::String boardsReport;
    if (const auto why = pocket::boardsSelfCheck (boardsReport); why.isNotEmpty())
        problems.add ("Pedals: " + why);
    juce::String effectsReport;
    if (const auto why = runEffectsChecks (effectsReport); why.isNotEmpty())
        problems.add (why);
    writeEffectsSnapshots (folder);

    // The drawer, the hold menus, sliding between tools and the sheets' drag, through the app
    juce::String navReport;
    if (const auto why = runNavigationChecks (navReport, folder); why.isNotEmpty())
        problems.add ("Navigation: " + why);

    // Deleting songs: any song, the open one too, and the last one
    juce::String deleteReport;
    if (const auto why = runSongDeleteChecks (deleteReport, folder); why.isNotEmpty())
        problems.add ("Delete songs: " + why);

    // Own chords on the CHORDS pads: EDIT CHORD, the notes the pad plays, saved, and a key change
    juce::String ownChordReport;
    if (const auto why = runOwnChordChecks (ownChordReport, folder); why.isNotEmpty())
        problems.add ("Own chords: " + why);

    // SONG: REC LIVE through the app (in and out on the bar, MARK, STOP + KEEP), played back, edited, punched, saved
    juce::String songReport;
    if (const auto why = runSongChecks (songReport, folder); why.isNotEmpty())
        problems.add ("Song: " + why);

    // The sampler: a trimmed sound is what its pad plays, at once and after the song is opened again
    juce::String samplerReport;
    if (const auto why = runSamplerChecks (samplerReport); why.isNotEmpty())
        problems.add ("Sampler: " + why);

    // The count-in as it looks: REC on VOCODER with the song stopped, at beat 3 of the count
    {
        switchIdea (ideas.create ("Train horn song"));
        {
            std::vector<float> l (512), r (512);
            for (int i = 0; i < 40; ++i)
                loopsScreen.engine().render (l.data(), r.data(), 512);   // the empty layers reach the engine and settle
        }
        appSettings.countInBars = 1;
        appSettings.recLength = 2;
        loopsScreen.engine().setCountInBars (1);
        micOn = micHear = true;
        applyMic();
        showTool (Tool::vocoder, false);
        recPressed();
        std::vector<float> l (512), r (512), in (512, 0.0f);
        const float* ptr = in.data();
        for (int guard = 0; guard < 400 && ! (beats.engine().clockRunning() && beats.engine().clockBeat() >= 2.3); ++guard)
        {
            loopsScreen.engine().setInput (&ptr, 1);
            loopsScreen.engine().render (l.data(), r.data(), 512);
        }
        savePng (folder.getChildFile ("06h-count-in.png"));
        togglePlay();
    }

    // MAKE A BEAT: a new song with an empty grid, and the lesson on DRUMS (steps 1, 2, 4 and the last card)
    {
        showIdeasList();
        switchIdea (ideas.create (ideas.nextName()));   // (what MAKE A BEAT does first)
        {
            std::vector<float> l (512), r (512);
            for (int i = 0; i < 40; ++i)
                loopsScreen.engine().render (l.data(), r.data(), 512);   // the empty layers reach the engine and settle
        }
        startLesson();
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("04c-lesson-1-kick.png"));
        beats.tapStep (0);
        beats.tapStep (10);
        lesson->goTo (BeatLesson::snare);
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("04d-lesson-2-snare.png"));
        beats.tapStep (4);
        beats.tapStep (12);
        lesson->goTo (BeatLesson::hats);
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("04e-lesson-3-hats.png"));
        lesson->goTo (BeatLesson::rec);
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("04f-lesson-4-rec.png"));
        lesson->showLanded (1);
        toastBox->setVisible (false);
        savePng (folder.getChildFile ("04g-lesson-done.png"));
        endLesson();
        stopEverything();
    }

    auto note = folder.getChildFile ("selfcheck.txt");
    note.replaceWithText ((problems.isEmpty() ? juce::String ("OK: + NEW SONG makes an empty song open on LOOPS, SET IN / SET OUT at the playhead and TRIM give a loop of the snapped length that plays gap-free in time (a 7-beat trim pads to 2 bars), FIT TO n BARS is exact and keeps the pitch, QUANTIZE moves loosely played pad hits and synth notes onto the grid in the re-rendered layer and UNDO brings the original back, effects and moves survive trim, fit and quantize, a BPM change keeps every layer's pitch, each CHORDS colour gives the right notes, the progression changes chord on every bar line and each chord reaches the vocoder, REC on CHORDS (progression and held chord) lands a take of the right length that isn't silent and the live chords then stop sounding (the progression still drives the vocoder), a PLAY LIVE hit just before its step sounds once (not again from the grid), a SYNTH key sounds at its pitch in the song's key, SYNTH pattern notes start on their steps, the arp plays in time, REC on SYNTH (KEYS and PATTERN) lands a take of the right length that isn't silent and the synth part then stops (UNDO brings it back), a new song starts with an empty drum grid and empty pad pattern (no pre-made beat), PLAY LIVE hits land on the nearest step while playing and not in the count-in or when stopped, the lesson's suggested steps are never written unless tapped, ideas keep their own pads and beat, a new idea gets the Street kit, TO PAD, the MPC export round trip, the pad pattern, the drum machine and the pads make sound, loops record, line up and play with no gap, the vocoder makes sound that follows the voice, REC on LOOPS, DRUMS (through the lesson), PLAY LIVE, PADS and VOCODER lands a take of the right length on the next layer, the count-in click is heard with CLICK off, the mic is silent in the speaker until it's allowed, an untouched song renders bit for bit as before the effect boards, each pedal changes the sound when on and is bit-transparent when off, a SYNTH board changes only the synth, REC on EFFECTS records through the board with the layer's own board left empty, QUANTIZE keeps the board, boards come back with the song, and the tempo-synced delay follows the BPM, the song drawer opens from the menu icon, the song name and the left edge (not over the SYNTH keys, the thumb bar or the tool row) and closes on a swipe left or a tap on the dim, picking a song there switches to it on the same tool with its own layers, the drawer reaches Sounds, the sound recorder, Mac link, Remote, Settings, the tour and the lesson, a hold on a layer, a pad, a chord pad or a song opens its menu and each row runs its action (CLEAR, REMOVE SOUND and COPY undo), a touch on a pad or chord sounds before and during the hold, sliding along the tool row or swiping the header changes tool, every bottom sheet closes on a downward drag or flick, and the tool row's amber outline can't stay on\n")
                                              : "FAILED\n" + problems.joinIntoString ("\n") + "\n")
                          + "\nPLAY LIVE early hit: " + beats.liveReport + "\n"
                          + "\nSynth self-check (120 BPM, 44.1 kHz):\n" + synthReport
                          + "\nChords self-check (120 BPM, 44.1 kHz):\n" + chordsReport
                          + "\nLoops self-check (97 BPM, 44.1 kHz):\n" + loopsReport
                          + "\nLoop editor (offline):\n" + toolsReport
                          + "\nLoop editor in the app (96 BPM, 44.1 kHz):\n" + editReport
                          + "\nVocoder self-check (44.1 kHz):\n" + vocoderReport
                          + "\nREC, count-in and mic checks (97 BPM, 44.1 kHz):\n" + recReport
                          + "\nEffect pedals (44.1 kHz):\n" + boardsReport
                          + "\nEffects in the app (97 BPM, 44.1 kHz):\n" + effectsReport
                          + "\nNavigation (drawer, hold menus, tool slide, sheets):\n" + navReport
                          + "\nDeleting songs (the open one, the last one):\n" + deleteReport
                          + "\nSampler (trim onto a pad, save, reopen):\n" + samplerReport
                          + "\nOwn chords (EDIT CHORD, save, reopen, key change):\n" + ownChordReport
                          + "\nSong (REC LIVE, play, edit, punch, save; 120 BPM):\n" + songReport);
    for (auto& line : problems)
        std::fprintf (stderr, "SELF-CHECK FAILED: %s\n", line.toRawUTF8());
}

// Drives the app's own engine by hand (no sound card in screenshot mode): presses REC on each tool and
// checks where the take went and how long it is; the count-in click with CLICK off; the mic gate.
juce::String PocketApp::runRecChecks (juce::String& report)
{
    juce::StringArray problems;
    auto& L = loopsScreen.engine();
    auto& b = beats.engine();
    const double sr = 44100.0, bpm = 97.0;
    const int barSamples = (int) (4.0 * 60.0 / bpm * sr);

    switchIdea (ideas.create ("Rec check"));
    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    appSettings.countInBars = 0;
    appSettings.recLength = 1;   // 2 bars
    appSettings.click = false;
    L.setCountInBars (0);
    micOn = true;
    micHear = false;
    applyMic();

    std::vector<float> in (512), outL (512), outR (512);
    juce::int64 counter = 0;
    bool silentInput = false;
    float outPeak = 0.0f;
    auto pump = [&] (int samples)
    {
        for (int done = 0; done < samples; done += 512)
        {
            for (auto& s : in)
                s = silentInput ? 0.0f : 0.3f * (float) std::sin ((double) counter++ * 0.0627);
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
            for (auto v : outL)
                outPeak = juce::jmax (outPeak, std::abs (v));
            if (lockKind != LockKind::none)
                lockWatchTick();
        }
    };
    auto waitPlaying = [&] (int layer, int maxBars)
    {
        for (int g = 0; g < maxBars * barSamples && L.state (layer) != pocket::LoopState::playing; g += 512)
            pump (512);
        pump (2048);   // and let the watcher see it
        return L.state (layer) == pocket::LoopState::playing;
    };
    auto takePeak = [&] (int layer)
    {
        float p = 0.0f;
        if (auto t = L.takeOf (layer))
            for (int i = 0; i < t->length.load(); ++i)
                p = juce::jmax (p, std::abs (t->data[(size_t) i]));
        return p;
    };
    auto checkTake = [&] (const juce::String& name, int layer, double wantBeats, double alignBeats)
    {
        const int expect = (int) (wantBeats * 60.0 / bpm * sr);
        auto t = L.takeOf (layer);
        const double got = t != nullptr ? t->lengthBeats.load() : 0.0;
        const int samples = t != nullptr ? t->length.load() : 0;
        const double start = L.startBeat (layer);
        report << name << ": layer " << (layer + 1) << ", " << juce::String (got, 2) << " beats, " << samples << " samples (expected "
               << expect << "), started on beat " << juce::String (start, 2) << "\n";
        if (std::abs (got - wantBeats) > 1.0e-6 || std::abs (samples - expect) > 2)
            problems.add ("REC on " + name + ": the take is " + juce::String (got, 2) + " beats (" + juce::String (samples) + " samples), expected " + juce::String (wantBeats, 0) + " beats");
        if (alignBeats > 0 && std::abs (std::fmod (start + 1.0e-6, alignBeats)) > 1.0e-3)
            problems.add ("REC on " + name + ": the take started on beat " + juce::String (start, 3) + ", not on a " + juce::String (alignBeats, 0) + "-beat line");
    };

    pump (2048);   // the new idea's empty layers reach the engine

    // LOOPS, from the mic
    showTool (Tool::loops, false);
    int want = loopsScreen.nextLayer();
    recPressed();
    if (want != 0 || ! waitPlaying (0, 6))
        problems.add ("REC on LOOPS: no take landed on layer 1");
    else
    {
        checkTake ("LOOPS (mic)", 0, 8.0, 4.0);
        if (takePeak (0) < 0.1f)
            problems.add ("REC on LOOPS: the mic take is silent");
    }

    // DRUMS, through the Make a beat lesson: the glowing hints write nothing, the player's own taps do, and
    // REC puts what the drum machine plays on layer 2. Afterwards the live drum machine stops (no doubling).
    {
        auto drumHits = [this] (int voice)
        {
            juce::String steps;
            for (int st = 0; st < 16; ++st)
                if (beats.drums().patterns[(size_t) beats.drums().slot][0].hits[(size_t) voice][(size_t) st] > 0)
                    steps << (steps.isEmpty() ? "" : ",") << (st + 1);
            return steps;
        };
        auto gridEmpty = [this]
        {
            for (auto& pair : beats.drums().patterns)
                for (auto& p : pair)
                    if (! p.isEmpty())
                        return false;
            return true;
        };
        startLesson();
        bool hintWrote = ! gridEmpty();
        for (int ph = 0; ph < 4; ++ph)
        {
            lesson->goTo (ph);
            hintWrote = hintWrote || ! gridEmpty();
        }
        report << "lesson: the hints alone wrote " << (hintWrote ? "steps (wrong)" : "nothing") << "\n";
        if (hintWrote)
            problems.add ("Lesson: suggested steps were written into the grid without a tap");
        lesson->goTo (BeatLesson::kick);
        beats.tapStep (0);
        beats.tapStep (10);
        lesson->goTo (BeatLesson::snare);
        beats.tapStep (4);
        beats.tapStep (12);
        lesson->goTo (BeatLesson::rec);
        const auto kickSteps = drumHits (0), snareSteps = drumHits (1), hatSteps = drumHits (9);
        report << "lesson: tapped kick " << kickSteps << ", snare " << snareSteps << ", hats [" << hatSteps << "]\n";
        if (kickSteps != "1,11" || snareSteps != "5,13" || hatSteps.isNotEmpty())
            problems.add ("Lesson: the grid has kick " + kickSteps + ", snare " + snareSteps + ", hats " + hatSteps + " (only the 4 tapped boxes belong)");
        want = loopsScreen.nextLayer();
        recPressed();
        if (want != 1 || ! waitPlaying (1, 6))
            problems.add ("REC on DRUMS: no take landed on layer 2");
        else
        {
            checkTake ("DRUMS", 1, 8.0, 4.0);
            if (b.isPlaying())
                problems.add ("REC on DRUMS: the live drum machine kept playing over its own layer");
            if (lesson->phase() != BeatLesson::landed || lesson->landedLayer() != 2)
                problems.add ("Lesson: after the take landed the card did not say layer 2 (phase " + juce::String (lesson->phase()) + ")");
            if (takePeak (1) < 0.05f)
                problems.add ("REC on DRUMS: the beat made by hand is silent on its layer");
        }
        endLesson();
    }

    // PADS with a pattern: it locks from the pattern's start
    showTool (Tool::pads, false);
    beats.fillDemoPattern();
    want = loopsScreen.nextLayer();
    recPressed();
    if (want != 2 || ! waitPlaying (2, 10))
        problems.add ("REC on PADS (a pattern): no take landed on layer 3");
    else
    {
        checkTake ("PADS (lock a pattern)", 2, 8.0, 8.0);
        if (b.patternPlaying())
            problems.add ("REC on PADS: the live pattern kept playing over its own layer");
    }

    // PADS from nothing: hits played go into the pattern, then it locks
    beats.clearPattern();
    want = loopsScreen.nextLayer();
    recPressed();
    const bool recording = beats.recordingPads();
    pump (barSamples / 2);
    beats.recordHit (12, 1.0f);
    pump (barSamples / 2);
    beats.recordHit (13, 1.0f);
    if (! recording || want != 3 || ! waitPlaying (3, 12))
        problems.add ("REC on PADS (new hits): no take landed on layer 4 (recording pads: " + juce::String (recording ? "yes" : "no") + ")");
    else
    {
        checkTake ("PADS (play, then lock)", 3, 8.0, 8.0);
        if (beats.drums().padPattern.hits.size() != 2)
            problems.add ("REC on PADS: the pattern has " + juce::String ((int) beats.drums().padPattern.hits.size()) + " hits, not the 2 played");
    }

    // VOCODER: the take (vocoder picked)
    showTool (Tool::vocoder, false);
    want = loopsScreen.nextLayer();
    recPressed();
    if (want != 4 || ! waitPlaying (4, 6))
        problems.add ("REC on VOCODER: no take landed on layer 5");
    else
        checkTake ("VOCODER", 4, 8.0, 4.0);

    // UNDO takes the last take off
    loopsScreen.undo();
    pump (2048);
    if (L.state (4) != pocket::LoopState::empty)
        problems.add ("UNDO did not take the last take off");
    else
        report << "UNDO: took layer 5 off\n";
    togglePlay();   // stop
    pump (2048);

    // PLAY LIVE: REC from a stopped song counts in, hits go in the pattern on the nearest step (not during the
    // count-in), then the pattern lands on layer 1 and the live machine stops.
    switchIdea (ideas.create ("Live check"));
    appSettings.countInBars = 1;
    appSettings.recLength = 0;   // 1 bar
    L.setCountInBars (1);
    appSettings.click = false;
    applyClick();
    silentInput = true;
    pump (2048);
    showTool (Tool::drums, false);
    beats.setDrumMode (1);
    {
        const bool emptyAtStart = beats.drums().edited().isEmpty();
        recPressed();
        pump (barSamples / 2);
        const bool counting = recView().kind == RecView::counting;
        const int duringCount = beats.hitLive (2);   // a hit in the count-in sounds but isn't written
        for (int g = 0; g < 400 && ! (b.clockRunning() && b.clockBeat() >= 4.6); ++g)
            pump (512);
        const double beat1 = b.clockBeat();
        const int want1 = (int) ((long long) std::floor (beat1 * 4.0 + 0.5) % 16);
        const int got1 = beats.hitLive (0);
        for (int g = 0; g < 400 && ! (b.clockBeat() >= 6.6); ++g)
            pump (512);
        const double beat2 = b.clockBeat();
        const int want2 = (int) ((long long) std::floor (beat2 * 4.0 + 0.5) % 16);
        const int got2 = beats.hitLive (1);
        const auto& pat = beats.drums().edited();
        const bool done = waitPlaying (0, 12);
        report << "play live: count-in " << (counting ? "yes" : "no") << ", hit in the count-in wrote step " << duringCount << ", kick at beat " << juce::String (beat1, 2)
               << " went to step " << (got1 + 1) << " (nearest " << (want1 + 1) << "), snare at beat " << juce::String (beat2, 2) << " to step " << (got2 + 1)
               << " (nearest " << (want2 + 1) << "), layer 1 " << (done ? "playing" : "NOT playing") << "\n";
        if (! emptyAtStart)
            problems.add ("PLAY LIVE: the new song's drum grid was not empty");
        if (! counting)
            problems.add ("PLAY LIVE: REC from a stopped song did not count in");
        if (duringCount != -1 || pat.hits[2][(size_t) 0] != 0)
            problems.add ("PLAY LIVE: a hit during the count-in was written into the pattern");
        if (got1 != want1 || pat.hits[0][(size_t) want1] == 0 || got2 != want2 || pat.hits[1][(size_t) want2] == 0)
            problems.add ("PLAY LIVE: hits did not land on the nearest steps");
        if (! done)
            problems.add ("PLAY LIVE: REC did not put the pattern on layer 1");
        else
        {
            checkTake ("PLAY LIVE", 0, 4.0, 4.0);
            if (b.isPlaying())
                problems.add ("PLAY LIVE: the live drum machine kept playing over its own layer");
            if (takePeak (0) < 0.05f)
                problems.add ("PLAY LIVE: the layer is silent (peak " + juce::String (takePeak (0), 4) + ")");
        }
    }
    beats.setDrumMode (0);
    togglePlay();
    pump (2048);

    // SYNTH in KEYS: REC from a stopped song counts in, what's played goes into the pattern (snapped to 1/16), it
    // lands on layer 1 for the take's length, and the live synth then plays nothing (no doubling). UNDO takes the
    // layer off and gives the part back. Then PATTERN: the pattern is locked as it is.
    switchIdea (ideas.create ("Synth check"));
    appSettings.countInBars = 1;
    appSettings.recLength = 0;   // 1 bar
    L.setCountInBars (1);
    appSettings.click = false;
    applyClick();
    silentInput = true;
    pump (2048);
    showTool (Tool::synth, false);
    {
        synthPart.reset();
        synthPart.setPreset (synthPresetIndex ("Pluck"));
        synthPart.mode = SynthPart::keysMode;
        synthPart.quantize = 0;
        synthPart.apply();
        recPressed();
        pump (barSamples / 2);
        const bool counting = recView().kind == RecView::counting;
        auto waitBeat = [&] (double beat) { for (int g = 0; g < 2000 && ! (b.clockRunning() && b.clockBeat() >= beat); ++g) pump (512); };
        auto playKey = [&] (double at, int key, double hold)
        {
            waitBeat (at);
            synthScreen.pressKey (key, true);
            waitBeat (at + hold);
            synthScreen.pressKey (key, false);
        };
        playKey (3.97, 0, 0.4);    // just before the pass: snaps onto its first step
        playKey (5.02, 2, 0.3);
        playKey (6.45, 4, 0.5);    // snaps to beat 2.5 of the bar
        const bool landed = waitPlaying (0, 12);
        juce::String steps;
        const auto& kept = synthPart.stash;
        for (auto& n : kept.notes)
            steps << (steps.isEmpty() ? "" : ", ") << SongKey::noteName (n.note) << "@" << juce::String (n.beat, 2);
        report << "synth keys: count-in " << (counting ? "yes" : "no") << ", the take's notes " << steps << ", layer 1 " << (landed ? "playing" : "NOT playing") << "\n";
        if (! counting)
            problems.add ("REC on SYNTH: no count-in from a stopped song");
        if (! landed)
            problems.add ("REC on SYNTH: no take landed on layer 1");
        else
        {
            checkTake ("SYNTH (keys)", 0, 4.0, 4.0);
            if (takePeak (0) < 0.02f)
                problems.add ("REC on SYNTH: the layer is silent (peak " + juce::String (takePeak (0), 4) + ")");
            bool where = kept.notes.size() == 3;
            const double wantBeats[3] = { 0.0, 1.0, 2.5 };
            for (size_t i = 0; where && i < 3; ++i)
                where = std::abs (kept.notes[i].beat - wantBeats[i]) < 1.0e-6;
            if (! where)
                problems.add ("REC on SYNTH: the pattern got " + steps + " (A, C, E on beats 0, 1 and 2.5 expected)");
            if (! synthPart.pattern.empty())
                problems.add ("REC on SYNTH: the pattern wasn't cleared after the lock (it would double the layer)");
            pump (barSamples);
            synthPart.engine().takePeak();
            pump (barSamples * 2);
            const float livePeak = synthPart.engine().takePeak();
            report << "synth after the lock: live synth peak " << juce::String (livePeak, 6) << ", layer peak " << juce::String (takePeak (0), 3) << "\n";
            if (livePeak > 1.0e-4f)
                problems.add ("REC on SYNTH: the live synth kept playing over its layer (peak " + juce::String (livePeak, 5) + ")");
            thumbPressed (ThumbBar::undo);
            pump (2048);
            if (L.state (0) != pocket::LoopState::empty || synthPart.pattern.notes.size() != 3)
                problems.add ("UNDO after a SYNTH take: the layer is " + juce::String (L.state (0) == pocket::LoopState::empty ? "off" : "still on")
                              + " and the pattern has " + juce::String ((int) synthPart.pattern.notes.size()) + " notes (3 expected back)");
            else
                report << "synth UNDO: layer 1 off, the 3 notes back in the pattern\n";
        }

        // PATTERN: locked as it is (1 bar of pattern, 2-bar take), from its next start
        appSettings.recLength = 1;   // 2 bars
        synthPart.mode = SynthPart::patternMode;
        synthPart.apply();
        want = loopsScreen.nextLayer();
        recPressed();
        const bool done = waitPlaying (want, 12);
        if (! done)
            problems.add ("REC on SYNTH (PATTERN): no take landed on layer " + juce::String (want + 1));
        else
        {
            checkTake ("SYNTH (pattern)", want, 8.0, 4.0);
            if (takePeak (want) < 0.02f)
                problems.add ("REC on SYNTH (PATTERN): the layer is silent");
            if (! synthPart.pattern.empty())
                problems.add ("REC on SYNTH (PATTERN): the pattern wasn't cleared after the lock");
        }
    }
    togglePlay();
    pump (2048);

    // CHORDS with the progression on (Am Dm F G, a bar each): REC from a stopped song counts in, the take is whole
    // passes of the progression (1 bar asked: 4 bars), starts the progression from its top, isn't silent, and once
    // it plays the progression is muted here (no doubling) while it still drives the vocoder. UNDO un-mutes it.
    // Then a held chord: REC takes it from the next bar for 1 bar, and it lets go once its layer plays.
    switchIdea (ideas.create ("Chords check"));
    appSettings.countInBars = 1;
    appSettings.recLength = 0;   // 1 bar
    L.setCountInBars (1);
    appSettings.click = false;
    applyClick();
    silentInput = true;
    pump (2048);
    showTool (Tool::chords, false);
    {
        auto& ce = chordsTool.engine();
        chordsTool.reset();
        chordsTool.setKey ({ 9, true });
        chordsTool.progression = { 0, 3, 5, 6, -1, -1, -1, -1 };
        chordsTool.progressionOn = true;
        chordsTool.apply();
        recPressed();
        pump (barSamples / 2);
        const bool counting = recView().kind == RecView::counting;
        const bool landed = waitPlaying (0, 12);
        report << "chords progression: count-in " << (counting ? "yes" : "no") << ", layer 1 " << (landed ? "playing" : "NOT playing")
               << ", progression from beat " << juce::String (ce.getOrigin(), 2) << "\n";
        if (! counting)
            problems.add ("REC on CHORDS: no count-in from a stopped song");
        if (! landed)
            problems.add ("REC on CHORDS: no take landed on layer 1");
        else
        {
            checkTake ("CHORDS (progression)", 0, 16.0, 4.0);
            if (takePeak (0) < 0.02f)
                problems.add ("REC on CHORDS: the layer is silent (peak " + juce::String (takePeak (0), 4) + ")");
            if (std::abs (ce.getOrigin() - L.startBeat (0)) > 1.0e-6)
                problems.add ("REC on CHORDS: the progression didn't start from its first box with the take");
            if (! ce.progressionMuted())
                problems.add ("REC on CHORDS: the progression kept sounding over its layer");
            pump (barSamples);
            ce.takePeak();
            const int sendsBefore = ce.vocoderSends();
            pump (barSamples * 2);
            const float livePeak = ce.takePeak();
            const int sent = ce.vocoderSends() - sendsBefore;
            report << "chords after the lock: live peak " << juce::String (livePeak, 6) << ", layer peak " << juce::String (takePeak (0), 3)
                   << ", chords sent to the vocoder in 2 bars " << sent << "\n";
            if (livePeak > 1.0e-4f)
                problems.add ("REC on CHORDS: the live chords kept playing over their layer (peak " + juce::String (livePeak, 5) + ")");
            if (sent < 2)
                problems.add ("REC on CHORDS: the muted progression stopped driving the vocoder (" + juce::String (sent) + " chords in 2 bars)");
            thumbPressed (ThumbBar::undo);
            pump (2048);
            if (L.state (0) != pocket::LoopState::empty || ce.progressionMuted())
                problems.add ("UNDO after a CHORDS take: the layer should be off and the progression sounding again");
        }

        // a held chord, the song playing: from the next bar, 1 bar
        chordsTool.progressionOn = false;
        chordsTool.progressionEdited();
        chordsTool.hold = true;
        chordsTool.padDown (3, false);
        pump (barSamples / 4);
        const int layer = loopsScreen.nextLayer();
        recPressed();
        const bool held = waitPlaying (layer, 6);
        if (! held)
            problems.add ("REC on CHORDS (held chord): no take landed on layer " + juce::String (layer + 1));
        else
        {
            checkTake ("CHORDS (held chord)", layer, 4.0, 4.0);
            if (takePeak (layer) < 0.02f)
                problems.add ("REC on CHORDS (held chord): the layer is silent");
            if (chordsTool.heldPad() >= 0)
                problems.add ("REC on CHORDS (held chord): the chord kept ringing over its layer");
        }
        chordsTool.reset();
    }
    togglePlay();
    pump (2048);

    // The count-in: REC with the song stopped and CLICK off. The click sounds through the count-in bar,
    // then stops; the take starts on beat 4; the click is in no take.
    switchIdea (ideas.create ("Count check"));
    appSettings.countInBars = 1;
    L.setCountInBars (1);
    appSettings.click = false;
    applyClick();
    silentInput = true;
    pump (2048);
    showTool (Tool::loops, false);
    recPressed();
    outPeak = 0.0f;
    pump (barSamples * 2 / 3);
    const bool counting = recView().kind == RecView::counting;
    pump (barSamples / 3 - 2048);
    const float countPeak = outPeak;
    pump (4096);
    outPeak = 0.0f;
    pump (barSamples / 2);
    const float afterPeak = outPeak;
    const bool done = waitPlaying (0, 6);
    report << "count-in: header counting " << (counting ? "yes" : "no") << ", click peak in the count-in " << juce::String (countPeak, 3)
           << ", after it (CLICK off) " << juce::String (afterPeak, 5) << ", take started on beat " << juce::String (L.startBeat (0), 2) << "\n";
    if (! counting)
        problems.add ("Count-in: the header didn't show the count");
    if (countPeak < 0.1f)
        problems.add ("Count-in: no click in the count-in with CLICK off (peak " + juce::String (countPeak, 4) + ")");
    if (afterPeak > 0.01f)
        problems.add ("Count-in: the click went on after the count-in with CLICK off (peak " + juce::String (afterPeak, 4) + ")");
    if (! done || std::abs (L.startBeat (0) - 4.0) > 1.0e-6)
        problems.add ("Count-in: the take didn't start after one bar (beat " + juce::String (L.startBeat (0), 3) + ")");
    // with CLICK on, a DRUMS-source take records the drums, not the click
    appSettings.click = true;
    applyClick();
    L.arm (1, pocket::LoopSource::drums, 1);
    const bool clickRecorded = ! waitPlaying (1, 4) || takePeak (1) > 1.0e-4f;
    report << "click in a drums take: " << juce::String (takePeak (1), 6) << "\n";
    if (clickRecorded)
        problems.add ("The click went into a DRUMS take (peak " + juce::String (takePeak (1), 5) + ")");
    appSettings.click = false;
    applyClick();
    togglePlay();
    pump (2048);

    // The mic gate: off, nothing of the mic reaches the speaker even with the vocoder's monitor and your
    // dry voice asked for; on without hearing, it records but stays out of the speaker; heard, it's heard.
    silentInput = false;
    auto& voc = vocoderScreen.engine();
    const bool wasMonitoring = voc.monitoring();
    voc.setMonitor (true);
    micOn = micHear = false;
    applyMic();
    L.setDryMonitor (true);
    pump (8192);
    outPeak = 0.0f;
    pump (8192);
    const float offPeak = outPeak;
    micOn = true;
    applyMic();
    L.setDryMonitor (true);
    L.arm (2, pocket::LoopSource::mic, 1);
    float unheardPeak = 0.0f;
    for (int g = 0; g < 4 * barSamples && L.state (2) != pocket::LoopState::playing; g += 512)
    {
        const bool wasRecording = L.state (2) == pocket::LoopState::recording;
        outPeak = 0.0f;
        pump (512);
        if (wasRecording && L.state (2) == pocket::LoopState::recording)
            unheardPeak = juce::jmax (unheardPeak, outPeak);   // while it records (not once the take plays back)
    }
    const bool took = waitPlaying (2, 1);
    const float recorded = takePeak (2);
    togglePlay();
    pump (4096);
    micHear = true;
    applyMic();
    L.setDryMonitor (true);
    pump (4096);
    outPeak = 0.0f;
    pump (8192);
    const float heardPeak = outPeak;
    report << "mic gate: speaker peak with the mic off " << juce::String (offPeak, 6) << ", on but not heard " << juce::String (unheardPeak, 6)
           << " (take peak " << juce::String (recorded, 3) << "), heard " << juce::String (heardPeak, 3) << "\n";
    if (offPeak > 1.0e-4f)
        problems.add ("Mic gate: the mic reached the speaker before it was allowed (peak " + juce::String (offPeak, 5) + ")");
    if (! took || recorded < 0.1f)
        problems.add ("Mic gate: with the mic on and not heard, the take didn't record it");
    if (unheardPeak > 1.0e-4f)
        problems.add ("Mic gate: NO HEADPHONES still put the mic in the speaker (peak " + juce::String (unheardPeak, 5) + ")");
    if (heardPeak < 0.05f)
        problems.add ("Mic gate: hearing yourself didn't put the mic in the speaker (peak " + juce::String (heardPeak, 4) + ")");
    voc.setMonitor (wasMonitoring);
    micOn = micHear = false;
    applyMic();
    stopEverything();
    pump (2048);

    // DRONE: singing into its strings (RESONATE, ringing to your voice) is silent while the mic is off and rings once
    // it's on, even with the mic not in the speaker; REC lands the drone on the next layer, and the live drone then
    // switches off so it doesn't double; its settings come back with the song.
    {
        switchIdea (ideas.create ("Drone check"));
        appSettings.countInBars = 0;
        appSettings.recLength = 0;   // 1 bar
        L.setCountInBars (0);
        appSettings.click = false;
        applyClick();
        L.setDryMonitor (false);
        silentInput = false;
        pump (2048);
        showTool (Tool::drone, false);
        droneTool.reset();
        droneTool.followKey = false;
        droneTool.root = 9;   // A: the test tone (A4) is its Sa an octave up
        droneTool.feed = (int) pocket::DroneFeed::voice;
        droneTool.resonate = 0.8f;
        micOn = micHear = false;
        applyMic();
        pump (8192);
        outPeak = 0.0f;
        pump (barSamples);
        const float deafPeak = outPeak;
        const bool warned = droneScreen.micWarningShown();
        micOn = true;
        applyMic();
        pump (8192);
        outPeak = 0.0f;
        pump (barSamples);
        const float singPeak = outPeak;
        report << "drone, singing A into the strings: mic off " << juce::String (deafPeak, 6) << " (warning " << (warned ? "shown" : "NOT shown")
               << "), mic on (not in the speaker) " << juce::String (singPeak, 4) << "\n";
        if (deafPeak > 1.0e-4f)
            problems.add ("DRONE: the strings rang to the mic while it was off (peak " + juce::String (deafPeak, 5) + ")");
        if (! warned)
            problems.add ("DRONE: the screen didn't say the mic is off");
        if (singPeak < 0.01f)
            problems.add ("DRONE: singing into the strings with the mic on didn't ring them (peak " + juce::String (singPeak, 5) + ")");

        droneTool.resonate = 0.0f;
        droneTool.on = true;
        silentInput = true;
        pump (8192);
        recPressed();
        const bool landed = waitPlaying (0, 6);
        if (! landed)
            problems.add ("REC on DRONE: no take landed on layer 1");
        else
        {
            checkTake ("DRONE", 0, 4.0, 4.0);
            if (takePeak (0) < 0.01f)
                problems.add ("REC on DRONE: the layer is silent (peak " + juce::String (takePeak (0), 4) + ")");
            if (droneTool.on.load())
                problems.add ("REC on DRONE: the live drone kept playing over its layer");
            if (auto t = L.takeOf (0); t == nullptr || t->tool != (int) pocket::LayerTool::drone)
                problems.add ("REC on DRONE: the layer isn't marked as the drone's");
        }
        togglePlay();
        pump (4096);

        droneTool.level = 0.5f;
        droneTool.first = 1;
        droneTool.voice = 3;
        droneTool.order = 2;
        droneTool.space = 0.35f;
        droneTool.setStepCount (1);
        droneTool.toggleStep (1, 2);
        saveSong();
        droneTool.reset();
        loadSong();
        if (std::abs (droneTool.level.load() - 0.5f) > 1.0e-4f || droneTool.first.load() != 1 || droneTool.root.load() != 9 || droneTool.followKey)
            problems.add ("DRONE: its settings didn't come back with the song");
        if (droneTool.voice.load() != 3 || droneTool.order.load() != 2 || std::abs (droneTool.space.load() - 0.35f) > 1.0e-4f
            || droneTool.stepCount.load() != 1 || droneTool.steps[0].load() != 0x1 || droneTool.steps[1].load() != 0x4)
            problems.add ("DRONE: its pattern and tone didn't come back with the song");

        // STEPS: drawn squares pluck their strings on their steps, in time with the clock (8 steps of an 1/8: 4 beats round)
        {
            droneTool.reset();
            droneTool.on = true;
            droneTool.clearSteps();
            droneTool.toggleStep (0, 0);
            droneTool.toggleStep (2, 1);
            droneTool.toggleStep (2, 2);
            droneTool.toggleStep (5, 3);
            droneTool.prepare (44100.0);
            int before[4];
            for (int st = 0; st < 4; ++st)
                before[st] = droneTool.pluckCount (st);
            std::vector<float> l (441), r (441);
            for (int b = 0; b < 800; ++b)   // 8 s at 120: 16 beats, four times round
                droneTool.renderAt (l.data(), r.data(), 441, true, b * 441 * 2.0 / 44100.0, 120.0);
            juce::String got;
            bool right = true;
            for (int st = 0; st < 4; ++st)
            {
                const int n = droneTool.pluckCount (st) - before[st];
                got << n << " ";
                right = right && n == 4;
            }
            report << "drone steps (4 times round): plucks per string " << got.trim() << "\n";
            if (! right)
                problems.add ("DRONE STEPS: the strings didn't pluck on their steps (" + got.trim() + ")");
        }
        droneTool.reset();
        droneTool.setSongKey (songKey.root);
        micOn = micHear = false;
        applyMic();
        silentInput = false;
        stopEverything();
        pump (2048);
    }
    return problems.joinIntoString ("\n");
}

//==============================================================================
// The loop editor through the app: + NEW SONG is empty; SET IN / SET OUT at the playhead and TRIM give a loop of the
// snapped length that plays with no gap, in time; FIT TO n BARS is exact and keeps the pitch; QUANTIZE puts loosely
// played notes on the grid and UNDO brings them back; effects and moves survive all of it; a tempo change keeps the
// layers' pitch.
juce::String PocketApp::runLoopEditChecks (juce::String& report)
{
    juce::StringArray problems;
    auto& L = loopsScreen.engine();
    const double sr = 44100.0, bpm = 96.0, spb = 60.0 / bpm * sr;

    // + NEW SONG
    showIdeasList();
    newIdea();
    std::vector<float> in (512, 0.0f), outL (512), outR (512);
    std::vector<double> blockBeat;
    std::vector<float> out;
    bool keep = false;
    auto pump = [&] (int samples)
    {
        for (int done = 0; done < samples; done += 512)
        {
            const float* ptr = in.data();
            L.setInput (&ptr, 1);
            std::fill (outL.begin(), outL.end(), 0.0f);
            std::fill (outR.begin(), outR.end(), 0.0f);
            L.render (outL.data(), outR.data(), 512);
            if (keep)
            {
                blockBeat.push_back (L.beatNow() - 256.0 / spb);   // the clock says where its last 256-sample chunk began
                out.insert (out.end(), outL.begin(), outL.end());
            }
        }
    };
    pump (40 * 512);
    {
        bool emptyDrums = true;
        for (auto& pair : beats.drums().patterns)
            for (auto& p : pair)
                emptyDrums = emptyDrums && p.isEmpty();
        const bool onLoops = page == Page::tool && tool == Tool::loops;
        report << "new song: " << ideas.current().name << ", " << loopsScreen.layerCount() << " layers, drum grid " << (emptyDrums ? "empty" : "NOT empty")
               << ", pad pattern " << (int) beats.drums().padPattern.hits.size() << " hits, " << beats.kit().usedPads() << " pads with sounds, on "
               << (onLoops ? "LOOPS" : "another page") << "\n";
        if (loopsScreen.layerCount() != 0 || ! emptyDrums || ! beats.drums().padPattern.hits.empty() || ! onLoops || beats.kit().usedPads() == 0)
            problems.add ("+ NEW SONG: the song wasn't empty with a kit loaded, open on LOOPS");
    }

    beats.setBpm (bpm);
    L.prepare (sr);
    L.setLatency (0);
    loopsScreen.setSyncJobs (true);
    micOn = false;
    applyMic();

    // layer 1: a ramp 4 bars long, so any gap or repeat shows
    const double d = 1.0 / 300000.0;
    const int n16 = (int) std::llround (16.0 * spb);
    {
        auto take = LoopsEngine::makeTake (n16, sr);
        for (int i = 0; i < n16; ++i)
            take->data[(size_t) i] = (float) ((double) i * d);
        take->length = n16;
        take->lengthBeats = 16.0;
        L.setLayer (0, take, false);
    }
    // layer 2 (muted): a hum 4.2 bars long in a 5-bar FREE take, with effects and moves
    const int sound = (int) std::llround (4.2 * 4.0 * spb), n20 = (int) std::llround (20.0 * spb);
    LayerFxState fx;
    fx.knob[fxReverb] = 0.3f;
    fx.moves[fxFilter].resize ((size_t) kMoveBins);
    for (int i = 0; i < kMoveBins; ++i)
        fx.moves[fxFilter][(size_t) i] = 0.2f + 0.6f * (float) i / kMoveBins;
    {
        auto take = LoopsEngine::makeTake (n20, sr);
        for (int i = 0; i < sound; ++i)
            take->data[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / sr);
        take->length = n20;
        take->lengthBeats = 20.0;
        take->content = sound;
        take->tool = (int) LayerTool::mic;
        L.setLayer (1, take, true);
    }
    // layer 3 (muted): a synth part played loosely (REC SNAP off), printed as it was played
    auto part = std::make_shared<NotePart>();
    part->kind = NotePart::synth;
    part->preset = juce::jmax (0, synthPresetIndex ("Rubber Funk"));
    part->notes.bars = 2;
    part->notes.notes = { { 0.09, 0.4, 45 }, { 4.11, 0.4, 45 } };
    part->lengthBeats = 8.0;
    SharedTake looseTake;
    {
        const int n8 = (int) std::llround (8.0 * spb);
        looseTake = LoopsEngine::makeTake (n8, sr);
        auto audio = renderPart (*part, {}, bpm, sr, n8);
        std::copy (audio.begin(), audio.end(), looseTake->data.begin());
        looseTake->length = n8;
        looseTake->lengthBeats = 8.0;
        looseTake->tool = (int) LayerTool::synth;
        looseTake->source = (int) LoopSource::synth;
        looseTake->part = part;
        L.setLayer (2, looseTake, true);
    }
    pump (2048);
    L.setFx (1, fx);
    L.setFx (2, fx);
    L.setTransport (true);
    pump (4096);
    auto fxSurvived = [&] (int layer, bool remapped)
    {
        const auto now = L.fxOf (layer);
        if (std::abs (now.knob[fxReverb] - 0.3f) > 1.0e-4f || now.moves[fxFilter].size() != (size_t) kMoveBins)
            return false;
        return remapped || std::abs (now.moves[fxFilter][100] - fx.moves[fxFilter][100]) < 1.0e-4f;
    };

    // SET IN while it plays bar 2, SET OUT while it plays bar 4 (SNAP BAR), then TRIM
    loopsScreen.openEditor (0);
    auto& ed = loopsScreen.editorSheet();
    ed.setSnap (0);
    auto playheadBeat = [&] { return L.position (0) * 16.0; };
    for (int g = 0; g < 2 * n16 && ! (playheadBeat() >= 5.0 && playheadBeat() < 6.0); g += 512)
        pump (512);
    ed.setAtPlayhead (true);
    for (int g = 0; g < 2 * n16 && ! (playheadBeat() >= 12.3 && playheadBeat() < 13.5); g += 512)
        pump (512);
    ed.setAtPlayhead (false);
    report << "SET IN at beat " << juce::String (ed.markerIn(), 2) << ", SET OUT at beat " << juce::String (ed.markerOut(), 2) << " (SNAP BAR)\n";
    if (std::abs (ed.markerIn() - 4.0) > 1.0e-9 || std::abs (ed.markerOut() - 12.0) > 1.0e-9)
        problems.add ("SET IN / SET OUT at the playhead didn't snap to bars 2 and 4 (" + juce::String (ed.markerIn(), 2) + ", " + juce::String (ed.markerOut(), 2) + ")");
    ed.pressTrim();
    pump (2048);
    {
        auto t = L.takeOf (0);
        const int len = t != nullptr ? t->length.load() : 0;
        const int want = (int) std::llround (8.0 * n16 / 16.0);
        const double startVal = t != nullptr ? t->data[0] / d : -1.0;
        // three passes: the loop point every 8 beats, on the beat it started (IN's place in the song), nothing else jumps
        keep = true;
        out.clear();
        blockBeat.clear();
        pump (len * 3 + 1024);
        keep = false;
        int wraps = 0, bad = 0;
        long lastWrap = -1, minSpacing = 1L << 40, maxSpacing = 0;
        double worstPhase = 0.0;
        const double expectedDiff = 0.8 * d;
        for (size_t k = 2000; k + 1 < out.size(); ++k)
        {
            const double diff = (double) out[k + 1] - (double) out[k];
            if (diff < -0.01)
            {
                if (lastWrap >= 0 && (long) k - lastWrap < 4)
                    continue;
                ++wraps;
                if (lastWrap >= 0)
                {
                    minSpacing = juce::jmin (minSpacing, (long) k - lastWrap);
                    maxSpacing = juce::jmax (maxSpacing, (long) k - lastWrap);
                }
                lastWrap = (long) k;
                const double beatThere = blockBeat[k / 512] + (double) ((k + 1) % 512) / spb;
                double ph = std::fmod (beatThere - L.startBeat (0), 8.0);
                ph = juce::jmin (ph, 8.0 - ph);
                worstPhase = juce::jmax (worstPhase, ph);
            }
            else if (std::abs (diff - expectedDiff) > expectedDiff * 0.5)
                ++bad;
        }
        report << "TRIM: " << (t != nullptr ? t->lengthBeats.load() : 0.0) << " beats, " << len << " samples (expected " << want << "), starts at sample "
               << juce::String (startVal, 0) << " of the old take (IN = " << juce::String (4.0 * spb, 0) << "); plays " << wraps << " loop points " << minSpacing << "-"
               << maxSpacing << " apart, " << bad << " gaps, loop point " << juce::String (worstPhase * 1000.0, 2) << " thousandths of a beat off the bar\n";
        if (t == nullptr || std::abs (t->lengthBeats.load() - 8.0) > 1.0e-9 || std::abs (len - want) > 1 || std::abs (startVal - 4.0 * spb) > 2.0)
            problems.add ("SET IN / SET OUT + TRIM did not give the 2 bars between them");
        if (wraps < 2 || bad != 0 || minSpacing < len - 1 || maxSpacing > len + 1 || worstPhase > 2.0 / spb)
            problems.add ("the trimmed loop doesn't play gap-free in time (" + juce::String (wraps) + " loop points, " + juce::String (bad) + " gaps)");
    }
    loopsScreen.undo();
    pump (2048);
    if (auto t = L.takeOf (0); t == nullptr || std::abs (t->lengthBeats.load() - 16.0) > 1.0e-9)
        problems.add ("UNDO after TRIM didn't bring the 4 bars back");
    // IN-OUT of 7 beats (SNAP BEAT): padded to 2 bars so it comes round on the bar
    loopsScreen.openEditor (0);
    ed.setSnap (1);
    ed.setMarkers (4.0, 11.0);
    ed.pressTrim();
    pump (2048);
    if (auto t = L.takeOf (0))
    {
        report << "TRIM of 7 beats: " << t->lengthBeats.load() << " beats long with " << juce::String ((double) t->contentSamples() / spb, 2) << " beats of sound\n";
        if (std::abs (t->lengthBeats.load() - 8.0) > 1.0e-9 || std::abs ((double) t->contentSamples() / spb - 7.0) > 0.01)
            problems.add ("a 7-beat TRIM didn't loop on the bar (padded to 2 bars)");
    }
    loopsScreen.undo();
    pump (2048);
    loopsScreen.closeEditor();

    // BEAT MATCH: FIT TO 4 BARS on the 4.2-bar hum
    loopsScreen.fitLayer (1, 4, 0.0, 20.0 * sound / n20);
    pump (2048);
    {
        auto t = L.takeOf (1);
        const int want = (int) std::llround (16.0 * spb);
        double hz = 0.0;
        if (t != nullptr)
        {
            int first = -1, last = -1, count = 0;
            const int len = t->length.load();
            for (int i = len / 4; i < len * 3 / 4; ++i)
                if (t->data[(size_t) i] <= 0.0f && t->data[(size_t) i + 1] > 0.0f)
                {
                    if (first < 0) first = i;
                    last = i;
                    ++count;
                }
            hz = count > 1 ? (count - 1) * sr / (double) (last - first) : 0.0;
        }
        report << "FIT TO 4 BARS: " << (t != nullptr ? t->length.load() : 0) << " samples (expected " << want << "), " << juce::String (hz, 2)
               << " Hz (was 440), effects and moves " << (fxSurvived (1, true) ? "kept" : "LOST") << "\n";
        if (t == nullptr || t->length.load() != want || std::abs (t->lengthBeats.load() - 16.0) > 1.0e-9)
            problems.add ("FIT TO 4 BARS did not give exactly 4 bars");
        if (std::abs (hz - 440.0) > 4.4)
            problems.add ("FIT TO 4 BARS changed the pitch (" + juce::String (hz, 1) + " Hz)");
        if (! fxSurvived (1, true))
            problems.add ("FIT lost the layer's effects or moves");
    }

    // QUANTIZE 1/4 on the loosely played synth part, then UNDO
    {
        LoopQuantize q;
        q.grid = 1;
        loopsScreen.quantizeLayer (2, q);
        pump (2048);
        auto t = L.takeOf (2);
        int first = -1, second = -1;
        if (t != nullptr)
        {
            for (int i = 0; i < t->length.load() && first < 0; ++i)
                if (std::abs (t->data[(size_t) i]) > 0.01f) first = i;
            for (int i = (int) (3.0 * spb); i < t->length.load() && second < 0; ++i)
                if (std::abs (t->data[(size_t) i]) > 0.01f) second = i;
        }
        int looseFirst = -1;
        for (int i = 0; i < looseTake->length.load() && looseFirst < 0; ++i)
            if (std::abs (looseTake->data[(size_t) i]) > 0.01f) looseFirst = i;
        report << "QUANTIZE 1/4: notes played at 0.09 and 4.11 now sound at " << juce::String (first / spb, 3) << " and " << juce::String (second / spb, 3)
               << " (as played: " << juce::String (looseFirst / spb, 3) << "), effects and moves " << (fxSurvived (2, false) ? "kept" : "LOST") << "\n";
        if (t == nullptr || t->quant.grid != 1 || first < 0 || first > 400 || std::abs (second - 4.0 * spb) > 400 || looseFirst < 0.05 * spb)
            problems.add ("QUANTIZE didn't move the loosely played notes onto the beats in the layer");
        if (! fxSurvived (2, false))
            problems.add ("QUANTIZE lost the layer's effects or moves");
        loopsScreen.undo();
        pump (2048);
        const bool back = L.takeOf (2) == looseTake;
        report << "UNDO after QUANTIZE: " << (back ? "the layer as played again" : "NOT the original") << "\n";
        if (! back)
            problems.add ("UNDO after QUANTIZE didn't bring back the layer as played");
    }

    // a tempo change: every layer follows, keeping its pitch
    {
        beats.setBpm (120.0);
        for (int g = 0; g < 12; ++g)
        {
            loopsScreen.followTempo();
            pump (1024);
        }
        auto t = L.takeOf (1);
        bool allThere = true;
        for (int l = 0; l < 3; ++l)
            if (auto x = L.takeOf (l))
                allThere = allThere && std::abs (nativeBpm (*x) - 120.0) < 0.3;
        double hz = 0.0;
        if (t != nullptr)
        {
            int first = -1, last = -1, count = 0;
            const int len = t->length.load();
            for (int i = len / 4; i < len * 3 / 4; ++i)
                if (t->data[(size_t) i] <= 0.0f && t->data[(size_t) i + 1] > 0.0f)
                {
                    if (first < 0) first = i;
                    last = i;
                    ++count;
                }
            hz = count > 1 ? (count - 1) * sr / (double) (last - first) : 0.0;
        }
        report << "96 -> 120 BPM: every layer at 120 " << (allThere ? "yes" : "NO") << ", the hum " << (t != nullptr ? t->lengthBeats.load() : 0.0) << " beats at "
               << juce::String (hz, 2) << " Hz\n";
        if (! allThere || t == nullptr || std::abs (t->lengthBeats.load() - 16.0) > 1.0e-9 || std::abs (hz - 440.0) > 4.4)
            problems.add ("after a BPM change the layers didn't follow at their own pitch");
        beats.setBpm (bpm);
    }

    loopsScreen.setSyncJobs (false);
    loopsScreen.closeEditor();
    togglePlay();
    stopEverything();
    for (int l = 0; l < kLoopLayers; ++l)
        L.setLayer (l, nullptr, false);
    pump (2048);
    return problems.joinIntoString ("\n");
}
