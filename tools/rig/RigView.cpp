#include "RigView.h"
#include "NoteEditor.h"
#include "AutomationPanel.h"
#include "RigExport.h"
#include "RigLogicImport.h"
#include "HelpPanel.h"
#include "RigSong.h"
#include "LooperView.h"
#include "SongView.h"
#include "MoreView.h"
#include "MixerView.h"
#include "GrooveView.h"
#include "SoundsView.h"
#include "RigPhoneLink.h"
#include "RigColours.h"
#include "EditorView.h"
#include "MpcView.h"
#include "FactoryKits.h"
#include "RigLesson.h"
#include "SamplerScreen.h"
#include "VocoderScreen.h"
#include "SynthScreens.h"
#include "GuideView.h"
#include "RigIdeas.h"
#include "RigTitle.h"
#include "core/Spark.h"
#include "core/ui/LanePeaks.h"
#include "EffectsView.h"
#include "DroneView.h"
#include "RigSongStart.h"
#include "QuantizePanel.h"
#include "ControllersPanel.h"
#include "core/visuals/VisualSettings.h"

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
namespace
{
// A file you drag out of Kaleidobox into Logic (a one-tool plugin's take: its MIDI or its audio).
// Double-click shows it in Finder.
class DragFileChip : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    DragFileChip (juce::String what, std::function<juce::File()> fileToDrag) : label (std::move (what)), file (std::move (fileToDrag)) {}

    void paint (juce::Graphics& g) override
    {
        const auto& p = vintage::pal();
        const auto f = file();
        const bool has = f.existsAsFile();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (p.ink.withAlpha (has ? 0.12f : 0.04f));
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (p.ink.withAlpha (has ? 0.6f : 0.2f));
        g.drawRoundedRectangle (r, 6.0f, has ? 1.5f : 1.0f);
        g.setColour (p.ink.withAlpha (has ? 0.95f : 0.4f));
        g.setFont (vintage::barlow (14.0f));
        g.drawFittedText (juce::String::fromUTF8 ("\xe2\x87\xa2 ") + label + (has ? ": " + f.getFileNameWithoutExtension() : juce::String (": press Rec first")),
                          getLocalBounds().reduced (10, 2), juce::Justification::centredLeft, 2, 0.8f);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto f = file();
        if (dragging || ! f.existsAsFile() || e.getDistanceFromDragStart() < 4)
            return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                    [safe = juce::Component::SafePointer<DragFileChip> (this)]
                                                                    { if (safe != nullptr) safe->dragging = false; });
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (const auto f = file(); f.existsAsFile())
            f.revealToUser();
        else
            RigEngine::logicFolder().revealToUser();
    }

private:
    juce::String label;
    std::function<juce::File()> file;
    bool dragging = false;
};
}

//==============================================================================
// A tool in its own window. Closing it puts the tool back in its tab.
class RigView::ToolWindow : public juce::DocumentWindow
{
public:
    ToolWindow (RigView& v, int p, juce::Component& content)
        : DocumentWindow ("Kaleidobox - " + v.pages[(size_t) p].name, Theme::panel, DocumentWindow::closeButton | DocumentWindow::maximiseButton),
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

// The Settings page (F6): boxed sections on the panel, each a dark well with its controls,
// like a back panel of the gear: Keys & pads, Sample one tool into another, Knobs & Learn, Click &
// mic, Audio & MIDI, Display, Your show, Plugins. The view owns the controls and lays them out.
class RigView::SetupPage : public juce::Component
{
public:
    struct Card
    {
        juce::String title, hint;
        juce::Colour colour;
        juce::Rectangle<int> box, hintArea;
    };
    std::vector<Card> cards;
    juce::Label intoLabel { {}, "Into" }, lengthLabel { {}, "Length" };
    juce::Rectangle<int> titleArea;

    explicit SetupPage (RigView& v) : view (v)
    {
        for (auto* l : { &intoLabel, &lengthLabel })
            addAndMakeVisible (l);
    }
    void paint (juce::Graphics& g) override
    {
        vintage::drawFace (g, getLocalBounds());
        const auto& p = vintage::pal();
        g.setColour (p.ink);
        g.setFont (vintage::cond (30.0f, vintage::Weight::extra, 0.12f));
        g.drawText ("SETTINGS", titleArea, juce::Justification::centredLeft);
        g.setColour (p.ink2);
        g.setFont (vintage::barlow (15.0f));
        g.drawText ("For the whole show. Saved with it, except Audio + MIDI and the panel.", titleArea.withTrimmedLeft (170),
                    juce::Justification::centredLeft);
        for (const auto& c : cards)
        {
            vintage::drawWell (g, c.box.toFloat(), 10.0f);
            auto head = c.box.reduced (16, 0).withTrimmedTop (12).withHeight (24);
            g.setColour (c.colour);
            g.fillRoundedRectangle (head.removeFromLeft (12).withSizeKeepingCentre (12, 12).toFloat(), 2.0f);
            head.removeFromLeft (10);
            g.setColour (p.wellInk);
            g.setFont (vintage::cond (19.0f, vintage::Weight::extra, 0.1f));
            g.drawFittedText (c.title.toUpperCase(), head, juce::Justification::centredLeft, 1, 0.8f);
            if (c.hint.isNotEmpty() && ! c.hintArea.isEmpty())
            {
                g.setColour (p.wellInk2);
                g.setFont (vintage::barlow (13.5f));
                g.drawFittedText (c.hint, c.hintArea, juce::Justification::topLeft, 4, 0.9f);
            }
        }
    }
    void resized() override { view.layoutSetup (getLocalBounds()); }

private:
    RigView& view;
};

// The Sounds drawer: the sound library slid over the right of the view, with a close button.
class RigView::Drawer : public juce::Component
{
public:
    explicit Drawer (RigView& v) : view (v)
    {
        close.fontSize = 14.0f;
        close.setTooltip ("Closes the drawer (F5 or Esc).");
        close.onClick = [this] { view.showSounds (false); };
        addAndMakeVisible (close);
    }
    void setContent (juce::Component* c)
    {
        content = c;
        if (c != nullptr)
            addAndMakeVisible (c);
        resized();
    }
    juce::Component* getContent() const { return content.getComponent(); }
    void paint (juce::Graphics& g) override
    {
        const auto& p = vintage::pal();
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (r.removeFromLeft (8.0f));
        g.setColour (p.face);
        g.fillRect (r);
        g.setColour (vintage::colours::selected);
        g.fillRect (r.removeFromLeft (4.0f));
        vintage::engraved (g, "Sounds", getLocalBounds().withTrimmedLeft (28).removeFromTop (44), juce::Justification::centredLeft, 18.0f);
        vintage::engraved (g, "Hear a sound, then send it to a pad, the Sampler or a loop",
                           getLocalBounds().withTrimmedLeft (110).withTrimmedRight (110).removeFromTop (44), juce::Justification::centredLeft, 12.0f);
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (12);
        auto top = r.removeFromTop (44);
        close.setBounds (top.removeFromRight (90).reduced (6, 6));
        if (content != nullptr)
            content->setBounds (r.reduced (8, 4));
    }

private:
    RigView& view;
    juce::Component::SafePointer<juce::Component> content;
    rigpanels::PanelButton close { "Close" };
};

//==============================================================================
RigView::FxPanel::FxPanel (RigEngine& engine, int s)
    : StripFxPanel (engine.getFx (s), RigEngine::stripLabel (s) + " effects", stripColour (s)), strip (s)
{
}

//==============================================================================
void RigView::showEffectsFor (int strip, int loop)
{
    if (effects == nullptr)
        return;
    if (loop >= 0)
        effects->showLoop (loop);
    else
        effects->showStrip (strip);
    showTab ("Effects");
}

void RigView::closeMixerPanel()
{
    if (mixer != nullptr)
        mixer->setPanel (nullptr);
    fxPanel.reset();
    layerFxPanel.reset();
    layerFxShown = -1;
}

void RigView::showFx (int strip)
{
    const bool same = fxPanel != nullptr && fxPanel->strip == strip;
    closeMixerPanel();
    if (same || mixer == nullptr)
        return;
    fxPanel = std::make_unique<FxPanel> (engine, strip);
    fxPanel->onSay = [this] (const juce::String& text) { say (text); };
    fxPanel->onPedals = [this, strip] { showEffectsFor (strip); };
    fxPanel->closeButton.onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
                                                                             { if (safe != nullptr) safe->closeMixerPanel(); }); };
    mixer->setPanel (fxPanel.get());
}

void RigView::showLayerFx (int layer)
{
    const bool same = layerFxPanel != nullptr && layerFxShown == layer;
    closeMixerPanel();
    if (same || mixer == nullptr || ! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return;
    layerFxPanel = std::make_unique<StripFxPanel> (engine.getLoopFx (layer), "Loop " + juce::String (layer + 1) + " effects", RigColours::loops);
    layerFxShown = layer;
    layerFxPanel->onSay = [this] (const juce::String& text) { say (text); };
    layerFxPanel->onPedals = [this, layer] { showEffectsFor (-1, layer); };
    layerFxPanel->closeButton.onClick = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
                                                                                  { if (safe != nullptr) safe->closeMixerPanel(); }); };
    mixer->setPanel (layerFxPanel.get());
}

//==============================================================================
//==============================================================================
RigView::RigView (RigEngine& e, juce::AudioDeviceManager* d)
    : engine (e), devices (d), header (d != nullptr), footer (e)
{
    // Every part of the window in the vintage look, panels and pop-ups included (the tools that
    // bring their own look still use the panel's warm wells, see WarmWells).
    setLookAndFeel (&vintageLook);
    setWantsKeyboardFocus (true);
    setOpaque (true);
    if (engine.getDirty().isEnabled())
    {
        activity = std::make_unique<Activity> (engine);
        juce::Desktop::getInstance().addGlobalMouseListener (activity.get());
        juce::Desktop::getInstance().addFocusChangeListener (activity.get());
        addKeyListener (activity.get());
    }
    vintage::setStage ((bool) engine.screens.getProperty ("dark", false));

    for (auto* c : std::initializer_list<juce::Component*> { &header, &screenBar, &footer, &dock, &rail })
    {
        c->setLookAndFeel (&vintageLook);
        addAndMakeVisible (c);
    }
    addAndMakeVisible (tabs);
    soundsDrawer = std::make_unique<Drawer> (*this);
    soundsDrawer->setLookAndFeel (&vintageLook);
    addChildComponent (*soundsDrawer);
    chordsDrawer = std::make_unique<ChordsView> (engine);
    chordsDrawer->setLookAndFeel (&vintageLook);
    chordsDrawer->onClose = [this] { showChords (false); };
    chordsDrawer->onSay = [this] (const juce::String& text) { say (text); };
    chordsDrawer->onOpenSampler = [this] { pickTrack (GrooveView::sampler); showView ("Edit"); };
    addChildComponent (*chordsDrawer);

    auto& transport = engine.getTransport();

    // The bottom bar (as the phone's thumb bar): tempo, click, count-in, length, one Rec, Play,
    // Undo last take, Rec moves, Automation, Quantize.
    dock.play.onClick = [this, &transport]
    {
        if (transport.isPlaying())
        {
            transport.stop();
            engine.allNotesOff();   // Stop always means quiet: no hanging notes or latched arps
        }
        else
            engine.playOrStop();
    };
    dock.rec.onClick = [this]
    {
        if (dock.rec.arrowHit)
            showRecMenu();
        else
            pressRec();
    };
    dock.up.onClick = [this] { nudgeTempo (juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? 0.1 : 1.0); };
    dock.down.onClick = [this] { nudgeTempo (juce::ModifierKeys::getCurrentModifiers().isShiftDown() ? -0.1 : -1.0); };
    dock.tempo.onNudge = [this] (double delta) { nudgeTempo (delta); };
    dock.tempo.onMenu = [this] { showTempoMenu(); };
    dock.tempo.onTap = [this]
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
    dock.click.onClick = [this, &transport]
    {
        transport.metronome = ! transport.metronome.load();
        updateDock();
    };
    dock.countIn.onClick = [this]
    {
        recCountIn = (recCountIn + 1) % 3;
        saveRecSetup();
        updateDock();
    };
    dock.length.onClick = [this]
    {
        static const int lengths[] = { 1, 2, 4, 8, 16, 32, 64, 0 };
        constexpr int count = (int) std::size (lengths);
        int i = 0;
        while (i < count && lengths[i] != recLength)
            ++i;
        recLength = lengths[(i + 1) % count];
        saveRecSetup();
        updateDock();
    };
    dock.undo.onClick = [this] { undoLastTake(); };
    dock.recMoves.onClick = [this] { pressRecMoves(); };
    dock.moves.onClick = [this] { showAutomation(); };
    dock.quantize.onClick = [this] { showQuantize (quantizeTarget(), &dock.quantize); };
    loadRecSetup();

    // The top bar.
    header.onMic = [this]
    {
        engine.micOn = ! engine.micOn.load();
        refresh();
        if (engine.micOn.load())
            sayMicOn();
    };
    header.onKeyboard = [this] { showControllers(); };

    // The views, the Sounds drawer and Settings.
    screenBar.onView = [this] (const juce::String& view) { showView (view); };
    screenBar.sounds.onClick = [this] { showSounds (! soundsOpen); };
    screenBar.chords.onClick = [this] { showChords (! chordsOpen); };
    // Settings (right-click: the quick menu of every tool, as More had).
    screenBar.settings.onClick = [this]
    {
        if (juce::ModifierKeys::getCurrentModifiers().isPopupMenu())
            showMoreMenu();
        else
            showTab ("Setup");
    };
    screenBar.help.onClick = [this] { showHelp (helpPanel == nullptr); };
    screenBar.help.setTooltip ("The Guide: a walkthrough of the whole of Kaleidobox, step by step on the real screens, and a how-to for every screen.");
    screenBar.back.onClick = [this] { goBack(); };
    screenBar.forward.onClick = [this] { goForward(); };

    // Chords and Effects, as tools on the rail.
    rail.chords.onClick = [this] { showChords (! chordsOpen); };
    rail.effects.onClick = [this]
    {
        if (currentPage() == "Effects")
            goBack();
        else
            showTab ("Effects");
    };

    // Your show, and starting a song from nothing.
    header.show.onClick = [this] { if (toolMode()) showToolMenu(); else showShowMenu(); };
    header.menu.onClick = [this] { if (toolMode()) showToolMenu(); else showShowMenu(); };
    crate::visuals::load (visualState, crate::visuals::defaultFile());
    header.visuals.onClick = [this] { showVisualsMenu(); };
    header.jam.onClick = [this]
    {
        engine.jam();
        say ("Jam: you're in charge. Every track you switch on plays; the song waits on the Song screen (Song mode there plays it again).");
    };

    // The song's sections.
    footer.onEditParts = [this] { showTab ("Song"); };
    footer.onRecInto = [this] { pressRecInto(); };
    footer.onChange = [this] (juce::Component& from) { showRecTargetMenu (from); };
    footer.onCycle = [this] { toggleCycle(); };
    footer.onTakes = [this] (juce::Component& from) { showTakesMenu (from); };
    footer.onSong = [this] { showView ("Song"); };
    footer.onPickSection = [this] (int section) { pickRecSection (section); };
    footer.onSlimChanged = [this] { resized(); };
    // The song's sections on the bottom bar (keys 1-8), as on the phone.
    dock.onSection = [this] (int i) { rigpanels::Footer::pressPart (engine, i); };
    dock.onSectionMenu = [this] (int i, juce::Component& chip) { footer.showPartMenu (i, &chip); };
    dock.onAddSection = [this] { keepAsSongPart(); };

    // The Setup page's parts.
    setupPage = new SetupPage (*this);
    for (auto* c : std::initializer_list<juce::Component*> { &clickButton, &micButton, &timingButton, &learnPlay, &learnStop, &learnPlayLabel, &learnStopLabel,
                                                             &sampleButton, &knobButton, &audioButton, &controllersButton, &saveButton, &openButton,
                                                             &darkButton, &popOutButton, &pluginsButton, &pluginsLabel, &pluginsHint,
                                                             &padsLabel, &padBank, &keysLabel, &keysTo,
                                                             &sourceLabel, &sampleSource, &sampleDest, &sampleBars, &splitLabel, &splitNote, &lowerKeysTo,
                                                             &phoneCode, &phoneUnlink })
        setupPage->addAndMakeVisible (c);

    // Settings > iPhone: Kaleidobox Pocket links over the Wi-Fi (the app only: in Logic, the app
    // isn't what's open).
    phoneCode.setFont (vintage::lcd (40.0f));
    phoneCode.setColour (juce::Label::textColourId, vintage::colours::lcdAmber);
    phoneCode.setColour (juce::Label::backgroundColourId, vintage::colours::lcdBack);
    phoneCode.setJustificationType (juce::Justification::centred);
    phoneCode.setTooltip ("On the iPhone, in Kaleidobox Pocket: More > Mac link, then type these four numbers.");
    phoneUnlink.setTooltip ("Forgets every iPhone linked to this Mac. A phone links again with the new numbers.");
    phoneUnlink.onClick = [this]
    {
        if (phoneLink == nullptr)
            return;
        const bool was = phoneLink->isLinked();
        phoneLink->unlink();
        say (was ? "The iPhone is unlinked. New numbers are on Settings > iPhone." : "Every iPhone is forgotten. New numbers are on Settings > iPhone.");
    };
    if (devices != nullptr)
    {
        phoneLink = std::make_unique<RigPhoneLink> (engine);
        phoneLink->onSay = [this] (const juce::String& text) { say (text); };
        phoneLink->onChange = [this] { updatePhoneCard(); };
        phoneLink->onNewSound = [this] (const juce::File& file)
        {
            if (auto* sv = dynamic_cast<SoundsView*> (pageContent ("Sounds")))
            {
                auto& bank = sv->getBank();
                bool covered = false;
                for (auto& f : bank.getFolders())
                    covered = covered || file.isAChildOf (f);
                if (covered)
                    bank.rescan();
                else
                    bank.addFolder (RigPhoneLink::soundsFolder());
            }
        };
        phoneLink->pressRec = [this] (const juce::String& target)
        {
            if (target == "set")
                toggleRecordSet();
            else if (target == "take")
                pressRecTake();
            else if (target == "moves")
                pressRecMoves();
        };
        phoneLink->recState = [this] (const juce::String& target) { return phoneRecState (target); };
    }
    updatePhoneCard();

    clickButton.setClickingTogglesState (true);
    clickButton.onClick = [this, &transport] { transport.metronome = clickButton.getToggleState(); };
    clickButton.setTooltip ("A metronome click on every beat while playing.");
    timingButton.setTooltip ("Recording timing: what you sing or play into the mic or line in comes in a little late (your interface's "
                             "round trip), so loops and takes from it are moved earlier to land on the beat. If a sung loop still "
                             "sits late or early, set it here.");
    timingButton.onClick = [this] { showTimingMenu(); };
    updateTimingButton();
    micButton.setClickingTogglesState (true);
    micButton.onClick = [this]
    {
        engine.micOn = micButton.getToggleState();
        refresh();
        if (engine.micOn.load())
            sayMicOn();
    };
    micButton.setTooltip ("Hear your mic (the vocoder and the Input strip). Use headphones with it on, so the speakers don't feed back. "
                          "Sampling and looping from the mic work either way. The Mic lamp at the top right switches it too.");
    learnPlay.setTooltip ("Press, then press a button on your keyboard to make it start and stop Kaleidobox.");
    learnStop.setTooltip ("Press, then press a button on your keyboard to make it stop Kaleidobox.");
    learnPlay.onClick = [this] { startLearning (RigRouting::play, learnPlay); };
    learnStop.onClick = [this] { startLearning (RigRouting::stop, learnStop); };
    for (auto* l : { &learnPlayLabel, &learnStopLabel })
        l->setColour (juce::Label::textColourId, vintage::pal().wellInk2);

    sampleButton.setColour (juce::TextButton::buttonOnColourId, Theme::record);
    sampleButton.setTooltip ("Records one tool into another, as picked beside it: the mic, a tool or the "
                             "whole mix, into the Sampler or onto the next empty MPC pad. It starts at the next bar (now, if "
                             "nothing is playing) and stops after the bars picked, or when you press again.");
    sampleButton.onClick = [this] { toggleSampling(); };

    knobButton.setColour (juce::TextButton::buttonOnColourId, Theme::accent);
    knobButton.setTooltip ("Put a knob of your keyboard on any setting: choose Learn a knob, touch the setting on screen "
                           "(the bass's Cutoff, the synth's LFO depth...), then turn the knob.");
    knobButton.onClick = [this] { showKnobMenu(); };

    audioButton.onClick = [this] { showAudioSettings(); };
    controllersButton.setTooltip ("What each MIDI controller is (a Launchkey, another keyboard, a pad controller like a Launchpad, "
                                  "MPD or APC), so its keys, pads and knobs play Kaleidobox. Learn pad 1 and Learn knobs for any other.");
    controllersButton.onClick = [this] { showControllers(); };
    saveButton.onClick = [this] { saveShow(); };
    saveButton.setTooltip ("Saves everything: the show (sounds, patterns, mixer, song parts) and your loops, together. "
                           "The first time it asks for a name; after that it saves straight away (Cmd+S). The show menu (top left) has Save show as... for a copy.");
    openButton.onClick = [this] { openShow(); };
    darkButton.setClickingTogglesState (true);
    darkButton.setTooltip ("The dark panel, for dark rooms and stages.");
    darkButton.onClick = [this] { setDarkPanel (darkButton.getToggleState()); };
    popOutButton.setTooltip ("Puts a screen in its own window, to move onto a second screen.");
    popOutButton.onClick = [this] { showPopOutMenu(); };
    pluginsButton.setTooltip ("Your own plugins: Arturia's synths (or any instrument) on the Bass or Synth, with their own screens; "
                              "effects like Quake at the end of any track's effects; and the scan that finds them.");
    pluginsButton.onClick = [this] { showPluginsMenu(); };
    pluginsHint.setText ("Instruments play the Bass or Synth, with their own screen in Edit (F2). Effects go on any track: Mix (F4) > Effects > + Plugin.",
                         juce::dontSendNotification);
    pluginsHint.setColour (juce::Label::textColourId, Theme::textDim);
    pluginsHint.setFont (juce::FontOptions (13.0f));

    // The seven screens first (F1-F7), then what More shows. The tab bar is hidden: the screen
    // buttons and More pick the page.
    tabs.setTabBarDepth (0);
    tabs.setOutline (0);
    auto* grooveView = new GrooveView (engine);
    grooveView->setLookAndFeel (&vintageLook);
    grooveView->onOpen = [this] (const juce::String& name)
    {
        // From Line in, the Effects page opens on Line in's pedals.
        if (name == "Effects" && effects != nullptr && groove != nullptr && groove->getTrack() == GrooveView::lineIn)
            effects->showSource (EffectsView::lineIn);
        showTab (name);
    };
    grooveView->onSay = [this] (const juce::String& text) { say (text); };
    grooveView->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
    grooveView->onSample = [this] { toggleSampling(); };
    grooveView->isSampling = [this] { return engine.getCapture().getState() != RigCapture::State::idle; };
    grooveView->showMic = devices != nullptr;
    grooveView->onQuantize = [this] (int target, juce::Component* from) { showQuantize (target, from); };
    grooveView->refresh();
    grooveView->onTrackPicked = [this] (int t) { railClicked (t); };
    grooveView->onTrackOpened = [this] (int t) { railDoubleClicked (t); };
    {
        // A one-tool plugin's rail has just its own track (the MPC with its Sampler).
        auto cards = grooveView->takeTrackCards();
        if (! toolMode())
        {
            // In the order of the phone's tool row, with Chords and Effects as tools too.
            juce::Array<juce::Component*> ordered;
            for (int t : { GrooveView::loops, GrooveView::machine, GrooveView::drums, GrooveView::sampler, GrooveView::bass, GrooveView::synth })
                ordered.add (cards[t]);
            ordered.add (&rail.chords);
            for (int t : { GrooveView::vocoder, GrooveView::lineIn, GrooveView::drone })
                ordered.add (cards[t]);
            ordered.add (&rail.effects);
            cards = ordered;
        }
        else
        {
            juce::Array<juce::Component*> mine;
            for (int t = 0; t < cards.size(); ++t)
                if (engine.runsStrip (GrooveView::stripFor (t)) && GrooveView::stripFor (t) != RigEngine::masterStrip)
                    mine.add (cards[t]);
                else
                    cards[t]->setVisible (false);
            cards = mine;
        }
        rail.setCards (cards);
    }
    groove = grooveView;
    addPage ("Groove", RigColours::song, grooveView);
    {
        // The pedalboard, and Full window: every setting on one page.
        auto* screen = new VocoderScreen (engine, devices != nullptr);
        screen->setLookAndFeel (&vintageLook);
        screen->onSay = [this] (const juce::String& text) { say (text); };
        screen->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
        screen->micLevel = [this] { return micMeter; };
        screen->onRecVoice = [this] { toggleSampling(); };
        screen->isSampling = [this] { return engine.getCapture().getState() != RigCapture::State::idle; };
        screen->onMic = [this] { if (header.onMic) header.onMic(); };
        screen->onOpenChords = [this] { showChords (true); };
        vocoderScreen = screen;
        addPage ("Vocoder", vocoderColour, screen);
    }
    auto* looperView = new LooperView (engine);
    looperView->setLookAndFeel (&vintageLook);
    looperView->onSay = [this] (const juce::String& text) { say (text); };
    looperView->onSongBuilder = [this] { showTab ("Song"); if (song != nullptr) song->showBars (false); };
    looperView->onBarsView = [this] { showTab ("Song"); if (song != nullptr) song->showBars (true); };
    looperView->onPedals = [this] (int layer) { showEffectsFor (-1, layer); };
    looperView->onQuantize = [this] (int target, juce::Component* from) { showQuantize (target, from); };
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
        screen->onSendToMpc = [screen]
        {
            if (auto* mpcView = screen->getMpcView())
                mpcView->exportToCard();
        };
        screen->onSideChanged = [this] { if (currentPage() == "Sampler") pageChanged(); };
        // The full editor lives on the Sampler screen too (Edit · Effects · Keys), in a dark
        // well on the panel, so every one of its tools is on the one screen.
        samplerEditor->useHostLook (vintageLook, [samplerEditor] (juce::Graphics& g, juce::Rectangle<int> tools, juce::Rectangle<int> keys)
        {
            vintage::drawFace (g, samplerEditor->getLocalBounds());
            vintage::drawWell (g, samplerEditor->getLocalBounds().reduced (2).toFloat());
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (tools.toFloat(), 8.0f);
            g.fillRoundedRectangle (keys.toFloat(), 8.0f);
        });
        screen->ownEditor (samplerView);
        samplerView = nullptr;
        // The MPC (kits, sequencer, songs, pad settings) is the screen's Sequencer · Kits side,
        // beside the same pads: one MPC, not two.
        screen->ownMpc (dynamic_cast<MpcView*> (engine.getMpc().createEditorAndMakeActive()));
        samplerScreen = screen;
        addPage ("Sampler", samplerColour, screen);
    }
    {
        // The Mixer (F5): a strip per tool and loop layer, the Reverb and Echo returns, the
        // master, and a button into every tool.
        auto* view = new MixerView (engine);
        view->onOpen = [this] (const juce::String& name) { openTool (name); };
        view->onFx = [this] (int strip) { showFx (strip); };
        view->onLayerFx = [this] (int layer) { showLayerFx (layer); };
        view->onLearn = [this] (int control, juce::Button& b) { startLearning (control, b); };
        view->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
        view->onSay = [this] (const juce::String& text) { say (text); };
        // The track rail goes into every tool, so the Mixer doesn't need its own row of buttons.
        view->showHub (false);
        mixer = view;
        addPage ("Mixer", Theme::text, view);
    }
    {
        // The effects processor: a pedalboard for every sound (the line in, each tool, each loop).
        auto* view = new EffectsView (engine);
        view->setLookAndFeel (&vintageLook);
        view->onSay = [this] (const juce::String& text) { say (text); };
        view->onOpen = [this] (const juce::String& name) { showTab (name); };
        view->onSample = [this] { toggleSampling(); };
        view->isSampling = [this] { return engine.getCapture().getState() != RigCapture::State::idle; };
        view->meterOf = [this] (int strip) { return meters[(size_t) strip]; };
        effects = view;
        addPage ("Effects", RigColours::input, view);
    }
    if (samplerScreen == nullptr || samplerScreen->getMpcView() == nullptr)
        addPage ("MPC", mpcColour, engine.getMpc().createEditorAndMakeActive());
    if (samplerView != nullptr)
        addPage ("Sampler", samplerColour, samplerView);
    addPage ("Drum machine", RigColours::drums, engine.getDrums().createEditorAndMakeActive());
    {
        // The Drone box: its strings, tuning, playing and what its strings ring to.
        auto* view = new DroneView (engine);
        view->setLookAndFeel (&vintageLook);
        view->onSay = [this] (const juce::String& text) { say (text); };
        addPage ("Drone", RigColours::drone, view);
    }
    // The Bass and Synth as old analog synths, each with its instrument shell (your Arturia synths).
    for (auto* screen : std::initializer_list<AnalogScreen*> { new BassScreen (engine), new SynthScreen (engine) })
    {
        screen->setLookAndFeel (&vintageLook);
        screen->onSay = [this] (const juce::String& text) { say (text); };
        const bool isBass = dynamic_cast<BassScreen*> (screen) != nullptr;
        screen->onEffects = [this, isBass] { showEffectsFor (isBass ? RigEngine::bassStrip : RigEngine::synthStrip); };
        addPage (isBass ? "Bass" : "Synth", isBass ? bassColour : synthColour, screen);
    }
    auto* songView = new SongView (engine);
    songView->setLookAndFeel (&vintageLook);
    songView->onSay = [this] (const juce::String& text) { say (text); };
    songView->onExport = [this] { exportForLogic(); };
    songView->getShowName = [this] { return currentShow.existsAsFile() ? currentShow.getFileNameWithoutExtension() : juce::String ("Song"); };
    songView->onImportFromLogic = [this] { askImportFromLogic(); };
    if (auto* barsView = songView->getBarsView())
        barsView->onOpenAutomation = [this] { showAutomation(); };
    songView->onGoTo = [this] (const juce::String& page) { showTab (page); };
    songView->onOpenBox = [this] (int lane, int clip, bool edit) { openSongBox (lane, clip, edit); };
    songView->onAddLayer = [this] (int track) { addLayerWith (track); };
    song = songView;
    addPage ("Song", RigColours::song, songView);
    auto* soundsView = new SoundsView (engine);
    soundsView->onSay = [this] (const juce::String& text) { say (text); };
    if (auto* mpcView = samplerScreen != nullptr && samplerScreen->getMpcView() != nullptr ? samplerScreen->getMpcView()
                        : pageIndex ("MPC") >= 0 ? dynamic_cast<MpcView*> (pages[(size_t) pageIndex ("MPC")].content.get()) : nullptr)
    {
        // The MPC's sound bank under its pads is the Sounds page's, and the keyboard's drum pads
        // (Launchkey, Launchpad) follow the pad bank the MPC shows.
        mpcView->setSoundBank (soundsView->getSharedBank());
        mpcView->onPadBankChanged = [this] (int bank) { engine.getRouting().padBank = juce::jlimit (0, 3, bank); };
        // New kit offers the factory kits too (the same five as the phone's).
        for (auto& kit : crate::factory::kits())
            mpcView->factoryKitNames.add (kit.name);
        mpcView->onFactoryKit = [this] (int index) { say (engine.loadFactoryKit (index)); };
    }
    if (samplerScreen != nullptr)
        samplerScreen->setSoundBank (soundsView->getSharedBank());
    addPage ("Sounds", juce::Colour (0xffa1887f), soundsView);
    addPage ("Setup", Theme::text, setupPage);
    {
        // The More page: the tools in full, your show and the set-up, as big buttons.
        auto* view = new MoreView (devices != nullptr);
        view->setLookAndFeel (&vintageLook);
        view->onOpen = [this] (const juce::String& name) { openTool (name); };
        view->onSave = [this] { saveShow(); };
        view->onSaveAs = [this] { saveShowAs(); };
        view->onOpenShow = [this] { openShow(); };
        view->onExport = [this] { exportForLogic(); };
        view->onAudio = [this] { showAudioSettings(); };
        view->onClick = [this]
        {
            auto& t = engine.getTransport();
            t.metronome = ! t.metronome.load();
            refresh();
        };
        view->onDark = [this] { setDarkPanel (! (bool) engine.screens.getProperty ("dark", false)); };
        view->onPopOut = [this] { showPopOutMenu(); };
        view->isClickOn = [this] { return engine.getTransport().metronome.load(); };
        view->isDark = [this] { return (bool) engine.screens.getProperty ("dark", false); };
        view->showName = [this] { return currentShow.getFileNameWithoutExtension(); };
        view->hasPage = [this] (const juce::String& name) { return pageIndex (name) >= 0 || (name == "MPC" && samplerScreen != nullptr); };
        moreView = view;
        addPage ("More", Theme::text, view);
    }

    // The Guide: the front page, with an entry for every tool above (the ones the Rig has).
    auto* guideView = new GuideView ([this] (const juce::String& name)
                                     { return pageIndex (name) >= 0 || ((name == "Sampler editor" || name == "MPC") && samplerScreen != nullptr); });
    guideView->onOpen = [this] (const juce::String& name) { openTool (name); };
    guideView->onHelp = [this] { showHelp (true); };
    guideView->onTour = [this] { tourStop = 0; showHelp (true); };
    guideView->onLesson = [this] { startLesson(); };
    guideView->onStartMaking = [this]
    {
        showView ("Play");
        say ("Pick any tool on the left, play it, and press Rec: it lands on the next loop layer. Then pick another tool and play on top.");
    };
    guideView->onStartOver = [this] { askNewSong(); };
    guideView->startsHere = [this] { return ! (bool) engine.screens.getProperty ("skipGuide", false); };
    guideView->setStartsHere = [this] (bool start)
    {
        if (start)
            engine.screens.removeProperty ("skipGuide", nullptr);
        else
            engine.screens.setProperty ("skipGuide", true, nullptr);
    };
    guide = guideView;
    addPage ("Guide", RigColours::master, guideView);

    // My ideas: the front page, as the phone's.
    auto* ideasView = new RigIdeas (showsFolder());
    ideasView->getHero = [this]
    {
        RigIdeas::Hero h;
        auto& t = engine.getTransport();
        const auto lcd = [] (double bpm, int sections) { return juce::String (bpm, 0) + " BPM" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (sections) + (sections == 1 ? " SECTION" : " SECTIONS"); };
        const int sections = (int) rigsong::sections (engine.getTimeline()).size();
        if (currentShow.existsAsFile())
        {
            // The song that's open: its saved layers, with their sound.
            const auto saved = RigIdeas::read (currentShow);
            h.name = saved.name;
            h.file = currentShow;
            h.open = true;
            h.sources = saved.sources;
            for (int l = 0; l < saved.layers(); ++l)
                h.peaks.push_back (crate::lanes::peaksOf (RigIdeas::loopFile (saved, l), 120));
            h.meta = "Open now" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "saved " + RigIdeas::whenText (currentShow.getLastModificationTime()).toLowerCase();
            h.lcd = lcd (t.getBpm(), sections);
            return h;
        }
        if (! engine.isBlank())
        {
            // What's in the Rig now (the Last show, or a song not saved yet).
            h.name = "Your last session";
            h.open = true;
            for (int l = 0; l < RigLooper::kLayers; ++l)
                if (engine.getLooper().getLayer (l).lengthBeats.load() > 0)
                    h.sources.push_back (engine.getLooper().getLayer (l).source.load());
            h.meta = "Not saved as a song yet: Cmd+S keeps it in My ideas.";
            h.lcd = lcd (t.getBpm(), sections);
            return h;
        }
        // Nothing open: your newest song.
        for (auto& s : ideas->songs())
            if (! s.lastShow)
            {
                h.name = s.name;
                h.file = s.file;
                h.sources = s.sources;
                for (int l = 0; l < s.layers(); ++l)
                    h.peaks.push_back (crate::lanes::peaksOf (RigIdeas::loopFile (s, l), 120));
                h.meta = "Your newest song" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + RigIdeas::whenText (s.modified).toLowerCase();
                h.lcd = lcd (s.bpm, s.sections);
                break;
            }
        return h;
    };
    ideasView->isPlaying = [this] (const juce::File& f) { return f == currentShow && engine.getTransport().isPlaying(); };
    ideasView->onOpen = [this] (const juce::File& f) { askOpenSong (f); };
    ideasView->onPlay = [this] (const juce::File& f)
    {
        if (f == currentShow)
        {
            engine.playOrStop();
            ideas->repaint();
            return;
        }
        askOpenSong (f, [this] { engine.getTransport().play(); showView ("Play"); });
    };
    ideasView->onPlayHero = [this]
    {
        const auto h = ideas->getHero ? ideas->getHero() : RigIdeas::Hero();
        if (h.open)
        {
            engine.playOrStop();
            ideas->repaint();
        }
        else if (h.file != juce::File())
            ideas->onPlay (h.file);
    };
    ideasView->onOpenHero = [this]
    {
        const auto h = ideas->getHero ? ideas->getHero() : RigIdeas::Hero();
        if (h.open)
            showView ("Play");
        else if (h.file != juce::File())
            askOpenSong (h.file);
        else
            catchIdea();
    };
    ideasView->onRename = [this] (const juce::File& f) { renameSong (f); };
    ideasView->onTrash = [this] (const juce::File& f) { trashSong (f); };
    ideasView->onCatch = [this] { catchIdea(); };
    ideasView->onNew = [this] { askNewSong(); };
    ideasView->onSpark = [this] (const crate::spark::Spark& spark) { startSpark (spark); };
    ideasView->onStartWith = [this] (int tile) { startWithTile (tile); };
    ideasView->onLesson = [this] { startLesson(); };
    ideasView->onTour = [this] { tourStop = 0; showHelp (true); };
    ideasView->onGuide = [this] { showTab ("Guide"); };
    ideasView->onOpenFile = [this] { openShow(); };
    ideasView->onImportLogic = [this] { askImportFromLogic(); };
    ideasView->onSettings = [this] { showTab ("Setup"); };
    ideas = ideasView;
    addPage ("Ideas", RigColours::master, ideasView);
    header.onWordmark = [this] { showTab ("Ideas"); };
    header.onRecording = [this]
    {
        if (const auto place = whatsRecording().second; place == "Automation")
            showAutomation();
        else if (place.isNotEmpty())
            goTo (place);
    };

    // The Rig opens on My ideas (unless the front page was switched off in the Guide), else on the Groove.
    showTab (engine.screens.getProperty ("skipGuide", false) ? "Groove" : "Ideas");
    if (titleAtLaunch && devices != nullptr)
        showTitle (true);
    screenBar.setCurrent (getView(), soundsOpen, currentPage() == "Setup" || currentPage() == "More", chordsOpen);

    for (int t = 0; t < RigEngine::numTools; ++t)
        engine.getTool (t).addListener (this);

    // Routing.
    for (auto* l : { &padsLabel, &keysLabel, &sourceLabel, &splitLabel, &setupPage->intoLabel, &setupPage->lengthLabel })
    {
        l->setColour (juce::Label::textColourId, vintage::colours::master.withAlpha (0.75f));
        l->setFont (vintage::cond (15.0f, vintage::Weight::bold, 0.06f));
    }
    for (auto* l : { &learnPlayLabel, &learnStopLabel })
        l->setFont (vintage::cond (15.0f, vintage::Weight::bold, 0.06f));
    for (auto* b : { &clickButton, &micButton, &timingButton, &learnPlay, &learnStop, &sampleButton, &knobButton, &audioButton, &controllersButton, &saveButton, &openButton,
                     &darkButton, &popOutButton, &pluginsButton, &phoneUnlink })
        vintage::setInWell (*b, true);
    for (auto* b : { &clickButton, &micButton, &timingButton, &learnPlay, &learnStop, &sampleButton, &knobButton, &audioButton, &controllersButton, &saveButton, &openButton,
                     &darkButton, &popOutButton, &pluginsButton, &phoneUnlink })
        b->getProperties().set ("fontSize", 15.0f);
    vintage::setLit (sampleButton, vintage::Lit::arm);
    vintage::setLit (clickButton, vintage::Lit::play);
    vintage::setLit (micButton, vintage::Lit::play);
    setupPage->setLookAndFeel (&vintageLook);
    padBank.addItemList ({ "A", "B", "C", "D" }, 1);
    padBank.onChange = [this] { engine.getRouting().padBank = padBank.getSelectedItemIndex(); };
    padBank.setTooltip ("Your keyboard's 16 drum pads (MIDI channel 10) play these MPC pads.");
    for (auto d : { RigRouting::Dest::vocoder, RigRouting::Dest::synth, RigRouting::Dest::bass, RigRouting::Dest::sampler,
                    RigRouting::Dest::mpc, RigRouting::Dest::drums, RigRouting::Dest::off })
    {
        keysTo.addItem (RigRouting::destName ((int) d), 1 + (int) d);
        lowerKeysTo.addItem (RigRouting::destName ((int) d), 1 + (int) d);
    }
    keysTo.onChange = [this] { engine.getRouting().keysTo = keysTo.getSelectedId() - 1; engine.getDirty().authoredField (RigDirty::keysTo, 0); };
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
    sampleSource.setTooltip ("What Sample records: the mic, any tool as you hear it (after its effects and level), or the whole mix.");
    sampleDest.addItem ("into Sampler", RigCapture::toSampler + 1);
    sampleDest.addItem ("onto MPC pad", RigCapture::toMpcPad + 1);
    sampleDest.addItem ("for Logic (WAV)", RigCapture::toLogic + 1);
    sampleDest.onChange = [this] { engine.captureDest = sampleDest.getSelectedId() - 1; };
    sampleDest.setTooltip ("Where it goes: the Sampler (play it from the keys, chop it), or the next empty pad of the MPC "
                           "(in the bank your keyboard's pads play), or a WAV for Logic (drag it from Drag to Logic on the Effects screen).");
    for (int bars : RigCapture::barChoices())
        sampleBars.addItem (bars == 0 ? "Free" : juce::String (bars) + (bars == 1 ? " bar" : " bars"), bars + 1);
    sampleBars.onChange = [this] { engine.captureBars = sampleBars.getSelectedId() - 1; };
    sampleBars.setTooltip ("How long: a number of bars (it stops by itself, in time, ready to loop), or Free (until you press Sample again).");

    openMidiInputs();
    refresh();
    if (toolMode())
        startToolMode();
    updateRecordButton();   // its name ("Rec set") from the start
    footer.tick();          // the song bar's sections (thin until there's a song)
    updateCaptureButtons(); // the bottom bar, from the start
    updateHeader();
    startTimerHz (30);
    // Put popped-out tools back on their screens, once this window is up.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
                                     { if (safe != nullptr) safe->restoreWindows(); });
}

RigView::~RigView()
{
    visualsWindow.reset();
    visualsView.reset();
    if (activity != nullptr)
    {
        juce::Desktop::getInstance().removeGlobalMouseListener (activity.get());
        juce::Desktop::getInstance().removeFocusChangeListener (activity.get());
        removeKeyListener (activity.get());
    }
    phoneLink.reset();
    if (controllersWindow != nullptr)
        delete controllersWindow.getComponent();
    for (int t = 0; t < RigEngine::numTools; ++t)
        engine.getTool (t).removeListener (this);
    closeMixerPanel();
    // The tools' own windows (the MPC, the sample editor, the drum machine...) go with this one.
    // Let each tool know first: otherwise it still thinks its window is open, and the next time
    // this window opens (closing and reopening Kaleidobox in Logic) it can't make another, so
    // those screens stay blank or frozen.
    for (auto* tool : std::initializer_list<juce::AudioProcessor*> { &engine.getMpc(), &engine.getSampler(), &engine.getDrums() })
        if (auto* editor = tool->getActiveEditor())
            tool->editorBeingDeleted (editor);
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
    updateTimingButton();
    darkButton.setToggleState ((bool) engine.screens.getProperty ("dark", false), juce::dontSendNotification);
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
    if (mixer != nullptr)
        mixer->refresh();
    if (looper != nullptr)
        looper->refresh();
    if (song != nullptr)
        song->refresh();
    if (samplerScreen != nullptr)
        samplerScreen->refresh();
    if (vocoderScreen != nullptr)
        vocoderScreen->refresh();
    if (groove != nullptr)
        groove->refresh();
    updateLearnButtons();
}

void RigView::openTool (const juce::String& name)
{
    // The Sampler's own button (on the Mixer, More and the Guide) shows its Chop side; the MPC's
    // shows the Sequencer side of the same screen.
    if (name == "Sampler" && samplerScreen != nullptr)
        samplerScreen->setSide (SamplerScreen::chopSide);
    showTab (name);
}

void RigView::showTab (const juce::String& name)
{
    // The sound library opens as a drawer over whichever view is showing.
    if (name == "Sounds" && pageIndex ("Sounds") >= 0 && pages[(size_t) pageIndex ("Sounds")].window == nullptr)
    {
        if (currentPage() == "Guide" || currentPage() == "Ideas")
            showTab ("Groove");
        return showSounds (true);
    }
    // The full sample editor is the Sampler screen's Edit side.
    if (name == "Sampler editor" && samplerScreen != nullptr)
    {
        samplerScreen->setEditMode (true);
        return showTab ("Sampler");
    }
    // The old Big stage is gone: its FX pad and everything else are on the Mixer.
    if (name == "Stage")
    {
        showTab ("Mixer");
        if (mixer != nullptr)
            mixer->showPad (true);
        return;
    }
    // The MPC is the Sampler · MPC screen's Sequencer · Kits side (beside the same pads).
    if (name == "MPC" && pageIndex ("MPC") < 0 && samplerScreen != nullptr)
    {
        samplerScreen->setSide (SamplerScreen::mpcSide);
        return showTab ("Sampler");
    }
    const int i = pageIndex (name);
    if (i < 0)
        return;
    if (pages[(size_t) i].window != nullptr)
        pages[(size_t) i].window->toFront (true);
    else
        tabs.setCurrentTabIndex (i);
    // The keyboard's drum pads play the tool on screen: the MPC's pads (in the bank it shows) on
    // the MPC and Sampler · MPC pages, the drum machine on its own.
    // (Following the screen, as the Groove screen's tracks do: playback-like for the dirty model.)
    auto& routing = engine.getRouting();
    if (name == "MPC" || name == "Sampler")
    {
        engine.getDirty().runtimeField (RigDirty::padsTo, 0, (float) routing.padsTo.load(), (float) RigRouting::Dest::mpc);
        routing.padsTo = (int) RigRouting::Dest::mpc;
        routing.padBank = juce::jlimit (0, 3, engine.getMpc().padBankOnScreen.load());
    }
    else if (name == "Drum machine")
    {
        engine.getDirty().runtimeField (RigDirty::padsTo, 0, (float) routing.padsTo.load(), (float) RigRouting::Dest::drums);
        routing.padsTo = (int) RigRouting::Dest::drums;
    }
    pageChanged();
}

// The Guide fills the window on a black panel (a night sky): no top bar or footer, and the
// screen buttons go dark too. Leaving it puts the panel back as it was.
void RigView::pageChanged()
{
    // A tool's own screen picks its track on the rail, so the keys, the knobs and Edit follow it.
    if (groove != nullptr)
        if (const int t = trackOfPlace (currentPlace()); t >= 0 && t != groove->getTrack())
            groove->selectTrack (t);
    screenBar.setCurrent (getView(), soundsOpen, currentPage() == "Setup" || currentPage() == "More", chordsOpen);
    navigation.visit (currentPlace());
    updateNavButtons();
    const bool onStage = currentPage() == "Guide" || currentPage() == "Ideas";
    if (currentPage() == "Ideas" && ideas != nullptr)
        ideas->refresh();
    if (rail.isVisible() != railShown())
    {
        rail.setVisible (railShown());
        resized();
    }
    if (onStage == stageShown)
        return;
    stageShown = onStage;
    vintage::setStage (onStage || (bool) engine.screens.getProperty ("dark", false));
    header.setVisible (! onStage);
    footer.setVisible (! onStage);
    dock.setVisible (! onStage);
    rail.setVisible (railShown());
    if (onStage)
        showSounds (false);
    resized();
    repaint();
    for (auto* c : std::initializer_list<juce::Component*> { &header, &screenBar, &footer, &dock, &rail })
        c->repaint();
    if (groove != nullptr)
        groove->repaint();
}

//==============================================================================
bool RigView::railShown() const
{
    return currentPage() != "Guide" && currentPage() != "Ideas";
}

juce::String RigView::editPageFor (int track)
{
    switch (track)
    {
        case GrooveView::drums:   return "MPC";
        case GrooveView::machine: return "Drum machine";
        case GrooveView::sampler: return "Sampler";
        case GrooveView::loops:   return "Looper";
        case GrooveView::lineIn:  return "Effects";
        default:                  return GrooveView::trackName (track);
    }
}

int RigView::trackOfPlace (const juce::String& place) const
{
    if (place == "MPC")
        return GrooveView::drums;
    if (place == "Drum machine")
        return GrooveView::machine;
    if (place == "Bass")
        return GrooveView::bass;
    if (place == "Synth")
        return GrooveView::synth;
    if (place == "Vocoder")
        return GrooveView::vocoder;
    if (place == "Sampler" || place == "Sampler editor")
        return GrooveView::sampler;
    if (place.startsWith ("Looper"))
        return GrooveView::loops;
    if (place == "Effects" && effects != nullptr && effects->getSource() == EffectsView::lineIn)
        return GrooveView::lineIn;
    if (place == "Drone")
        return GrooveView::drone;
    return -1;
}

juce::String RigView::viewOfPlace (const juce::String& place) const
{
    if (place == "Groove")
        return "Play";
    if (place.startsWith ("Song"))
        return "Song";
    if (place == "Mixer")
        return "Mix";
    if (place == "Effects")
        return trackOfPlace (place) == GrooveView::lineIn ? "Edit" : "Mix";
    if (place == "Guide" || place == "Ideas" || place == "Setup" || place == "More" || place == "Sounds" || place.isEmpty())
        return {};
    return "Edit";
}

juce::String RigView::getView() const
{
    return viewOfPlace (currentPlace());
}

int RigView::getTrack() const
{
    return groove != nullptr ? groove->getTrack() : -1;
}

void RigView::pickTrack (int t)
{
    if (groove == nullptr || ! juce::isPositiveAndBelow (t, (int) GrooveView::numTracks))
        return;
    if (groove->getTrack() != t)
        groove->selectTrack (t);
    // On Edit, the instrument you picked shows in full; the other views stay where they are.
    if (getView() == "Edit")
        showView ("Edit");
}

int RigView::laneForTrack (int t) const
{
    if (t == GrooveView::loops)
        return RigTimeline::laneForLoop (groove != nullptr ? groove->getPickedLayer() : 0);
    return juce::isPositiveAndBelow (t, (int) GrooveView::numTracks) ? RigTimeline::laneForStrip (GrooveView::stripFor (t)) : -1;
}

void RigView::railClicked (int t)
{
    pickTrack (t);
    const auto view = getView();
    if (view == "Song" && song != nullptr)
    {
        if (song->isShowingBars())
            song->showBars (false);
        song->showLane (laneForTrack (t));
    }
    else if (view == "Mix")
    {
        // Its effects, along the bottom of the Mixer (a loop: that loop's own).
        if (t == GrooveView::loops)
        {
            if (layerFxPanel == nullptr || layerFxShown != groove->getPickedLayer())
                showLayerFx (groove->getPickedLayer());
        }
        else if (fxPanel == nullptr || fxPanel->strip != GrooveView::stripFor (t))
            showFx (GrooveView::stripFor (t));
    }
    else if (view.isEmpty() && ! toolMode())
        // Settings, the Sounds page...: off to Play, on that track.
        showView ("Play");
}

void RigView::railDoubleClicked (int t)
{
    pickTrack (t);
    showView ("Edit");
    say (GrooveView::trackName (t) + ", the whole tool. Play (F1) has its quick strip: steps, patterns, 8 knobs.");
}

void RigView::showTourPlace (const juce::String& place)
{
    if (place.isEmpty())
        return;
    const auto view = place.upToFirstOccurrenceOf (":", false, false);
    const auto trackName = place.fromFirstOccurrenceOf (":", false, false);
    if (view != "Sounds")
        showSounds (false);
    if (view != "Chords")
        showChords (false);
    if (trackName.isNotEmpty())
        for (int t = 0; t < (int) GrooveView::numTracks; ++t)
            if (GrooveView::trackName (t) == trackName)
                pickTrack (t);
    if (view == "Sounds")
        showSounds (true);
    else if (view == "Chords")
        showChords (true);
    else if (view == "Settings")
        showTab ("Setup");
    else
    {
        if (view == "Song" && song != nullptr && song->isShowingBars())
            song->showBars (false);
        showView (view);
    }
    if (helpPanel != nullptr)
        helpPanel->toFront (false);
}

int RigView::lastTakeOf (int track) const
{
    static_assert (std::tuple_size_v<decltype (lastTake)> == GrooveView::numTracks, "a last take for each track");
    if (! juce::isPositiveAndBelow (track, (int) lastTake.size()))
        return -1;
    const int l = lastTake[(size_t) track];
    return l >= 0 && engine.getLooper().getLayer (l).lengthBeats.load() > 0 ? l : -1;
}

namespace
{
int takeSourceFor (int track)
{
    switch (track)
    {
        case GrooveView::drums:   return RigLooper::mpc;
        case GrooveView::machine: return RigLooper::drums;
        case GrooveView::bass:    return RigLooper::bass;
        case GrooveView::synth:   return RigLooper::synth;
        case GrooveView::vocoder: return RigLooper::vocoder;
        case GrooveView::sampler: return RigLooper::sampler;
        case GrooveView::lineIn:  return RigLooper::line;
        case GrooveView::drone:   return RigLooper::drone;
        default:                  return -1;   // Loops: the layer's own
    }
}
}

int RigView::pressRecTake()
{
    auto& looper = engine.getLooper();
    // With a song, Rec take records into it (Rec into, the bar along the bottom), unless a free
    // take was asked for (Change > Free take) or it's the Loops track (its loops are its own).
    if (takeLayer < 0 && (engine.getTakes().isActive()
                          || (! recFree && getTrack() != GrooveView::loops && ! rigsong::sections (engine.getTimeline()).empty())))
        return pressRecInto();
    recFree = false;
    if (takeLayer >= 0)
    {
        const auto st = looper.getState (takeLayer);
        const int layer = std::exchange (takeLayer, -1);
        if (st == RigLooper::State::armed || (st == RigLooper::State::empty && ! takeStarted))
        {
            looper.pressRecord (layer);   // not started yet: called off
            say ("Rec take called off: nothing was recorded.");
        }
        else if (st == RigLooper::State::recording)
        {
            looper.pressRecord (layer);   // closes at the end of the bar
            if (juce::isPositiveAndBelow (takeTrack, (int) lastTake.size()))
                lastTake[(size_t) takeTrack] = layer;
            say ("Take on loop " + juce::String (layer + 1) + ": it closes at the end of this bar and plays round. To Song puts it in a section.");
        }
        updateCaptureButtons();
        return -1;
    }
    const int t = getTrack();
    if (t < 0)
        return -1;
    int layer = -1;
    for (int l = 0; l < RigLooper::kLayers && layer < 0; ++l)
        if (looper.getState (l) == RigLooper::State::empty)
            layer = l;
    if (layer < 0)
    {
        say ("All 16 loops are full. Clear one (Loops on the left, then Edit), or keep them as a bank and start a new batch there.");
        return -1;
    }
    if (int source = takeSourceFor (t); source >= 0)
    {
        // Line in with the Input strip off: your mic as it comes in (the strip would be silent).
        if (source == RigLooper::line && ! engine.inputOn.load())
            source = RigLooper::input;
        looper.getLayer (layer).source = source;
    }
    // The bottom bar's length (0: until you press again) and count-in.
    looper.getLayer (layer).bars = juce::jmax (0, recLength);
    looper.getLayer (layer).countIn = juce::jlimit (0, 2, recCountIn);
    looper.pressRecord (layer);
    takeLayer = layer;
    lastTakeLayer = layer;
    takeTrack = t;
    takeStarted = false;
    takeKeptSaid = false;
    juce::String note;
    if (t == GrooveView::vocoder && ! engine.micOn.load())
        note = " Your mic is off: switch it on (Mic, top right) to be heard.";
    else if (t == GrooveView::vocoder && ! engine.vocoderOn.load())
        note = " The vocoder is off: switch it on to be heard.";
    const bool counting = recCountIn > (engine.getTransport().isPlaying() ? 1 : 0);
    say ("Rec: " + GrooveView::trackName (t) + " onto loop " + juce::String (layer + 1)
         + (counting ? " after " + juce::String (recCountIn) + (recCountIn == 1 ? " bar" : " bars") + " of count-in" : juce::String (" from the next bar"))
         + (recLength > 0 ? ", " + juce::String (recLength) + (recLength == 1 ? " bar" : " bars") + ", then it loops."
                          : ". Play it over the beat; press Rec again to stop at the end of the bar.") + note);
    updateCaptureButtons();
    return layer;
}

void RigView::pressRec()
{
    if (toolMode())
        return toggleRecordSet();   // a tool plugin records itself, to drag into Logic
    // Whatever is recording from here stops first.
    if (engine.getRecorder().getState() != RigRecorder::State::idle)
        return toggleRecordSet();
    if (engine.getCapture().getState() != RigCapture::State::idle)
        return toggleSampling();
    if (engine.getMpc().getSequencer().isRecording())
    {
        engine.getMpc().toggleRecord();   // the pads stop going into the pattern (it keeps playing)
        say ("The pads stopped recording into the MPC pattern. Sequencer · Kits shows the hits to edit; Undo takes the pass back.");
        return updateDock();
    }
    if (takeLayer >= 0 || engine.getTakes().isActive())
    {
        pressRecTake();
        return updateDock();
    }
    switch (recLands)
    {
        case RecLands::layer:  recFree = true; pressRecTake(); break;
        case RecLands::song:   recFree = false; pressRecTake(); break;   // with a song, into it (Rec into)
        case RecLands::sample: toggleSampling(); break;
        case RecLands::set:    toggleRecordSet(); break;
        case RecLands::pattern:
            engine.getMpc().toggleRecord();
            say ("Rec: the pads go into the MPC pattern as you play (it loops; each pass adds). Rec again stops.");
            break;
    }
    updateDock();
}

void RigView::setRecSetup (int countInBars, int lengthBars)
{
    recCountIn = juce::jlimit (0, 2, countInBars);
    recLength = juce::jlimit (0, RigLooper::kMaxBars, lengthBars);
    saveRecSetup();
    updateDock();
}

bool RigView::undoLastTake()
{
    auto& looper = engine.getLooper();
    auto forget = [this] (int layer)
    {
        for (auto& l : lastTake)
            if (l == layer)
                l = -1;
        if (lastTakeLayer == layer)
            lastTakeLayer = -1;
    };
    if (takeLayer >= 0)
    {
        const int layer = std::exchange (takeLayer, -1);
        if (looper.getState (layer) == RigLooper::State::armed)
            looper.pressRecord (layer);   // not started: called off
        else
            looper.clear (layer);
        forget (layer);
        say ("Take on loop " + juce::String (layer + 1) + " thrown away.");
        updateCaptureButtons();
        return true;
    }
    if (juce::isPositiveAndBelow (lastTakeLayer, RigLooper::kLayers) && looper.getState (lastTakeLayer) != RigLooper::State::empty)
    {
        const int layer = lastTakeLayer;
        looper.clear (layer);
        forget (layer);
        say ("Undo: the last take (loop " + juce::String (layer + 1) + ") is cleared. Rec records onto it again next.");
        updateCaptureButtons();
        return true;
    }
    if (undoLast())
        return true;
    say ("Nothing to undo yet.");
    return false;
}

juce::String RigView::recTargetText() const
{
    if (toolMode())
        return "TO DRAG INTO LOGIC";
    auto& looper = engine.getLooper();
    const auto lengthText = recLength > 0 ? juce::String (recLength) + (recLength == 1 ? " BAR" : " BARS") : juce::String ("FREE");
    const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
    if (takeLayer >= 0)
        return "LAYER " + juce::String (takeLayer + 1) + dot + lengthText;
    switch (recLands)
    {
        case RecLands::song:
        {
            const auto list = rigsong::sections (engine.getTimeline());
            if (const int sec = recTargetSection(); juce::isPositiveAndBelow (sec, (int) list.size()) && getTrack() != GrooveView::loops)
                return ("INTO " + rigsong::letterFor (sec) + " " + list[(size_t) sec].name).toUpperCase();
            break;   // no song yet: a free take, onto the next layer
        }
        case RecLands::sample: return "SAMPLER OR PAD";
        case RecLands::set:    return "WHOLE SET" + dot + "STEMS";
        case RecLands::pattern: return "PADS" + dot + "MPC PATTERN";
        case RecLands::layer:  break;
    }
    for (int l = 0; l < RigLooper::kLayers; ++l)
        if (looper.getState (l) == RigLooper::State::empty)
            return "LAYER " + juce::String (l + 1) + dot + lengthText;
    return "LOOPS FULL";
}

juce::String RigView::countInText() const
{
    auto& transport = engine.getTransport();
    if (! transport.isPlaying() || takeLayer < 0)
        return {};
    auto& looper = engine.getLooper();
    if (looper.getState (takeLayer) != RigLooper::State::armed || looper.getLayer (takeLayer).countIn.load() <= 0)
        return {};
    const double left = looper.getLayer (takeLayer).startsAt.load() - transport.getPositionInBeats();
    if (left <= 0)
        return {};
    return "COUNT-IN " + juce::String ((int) std::ceil (left - 1.0e-6));
}

void RigView::updateDock()
{
    auto setButton = [] (rigpanels::PanelButton& b, const juce::String& text, const juce::String& sub, vintage::Lit lit)
    {
        if (b.text != text || b.sub != sub || b.lit != lit)
        {
            b.text = text;
            b.sub = sub;
            b.lit = lit;
            b.repaint();
        }
    };
    auto& transport = engine.getTransport();
    const bool click = transport.metronome.load();
    setButton (dock.click, "Click", click ? "on" : "off", click ? vintage::Lit::current : vintage::Lit::none);
    setButton (dock.countIn, "Count-in", recCountIn == 0 ? juce::String ("off") : juce::String (recCountIn) + (recCountIn == 1 ? " bar" : " bars"),
               vintage::Lit::none);
    setButton (dock.length, "Length", recLength > 0 ? juce::String (recLength) + (recLength == 1 ? " bar" : " bars") : juce::String ("free"),
               vintage::Lit::none);
    if (std::abs (dock.tempo.bpm - transport.getBpm()) > 0.001 || dock.tempo.rampTo != (transport.isHosted() ? 0.0 : transport.getRampTarget()))
    {
        dock.tempo.bpm = transport.getBpm();
        dock.tempo.rampTo = transport.isHosted() ? 0.0 : transport.getRampTarget();
        dock.tempo.repaint();
    }

    // Rec: what's recording from here, or where the next take goes.
    int state = 0;
    juce::String text = "Rec", sub = juce::String::fromUTF8 ("\xe2\x86\x92 ") + recTargetText();
    const auto recorder = engine.getRecorder().getState();
    const auto capture = engine.getCapture().getState();
    if (recorder != RigRecorder::State::idle)
    {
        state = recorder == RigRecorder::State::armed ? 1 : 2;
        text = shownSet.text;
        sub = toolMode() ? juce::String ("to drag into Logic") : juce::String ("the whole set, to stems");
    }
    else if (capture == RigCapture::State::armed || capture == RigCapture::State::recording)
    {
        state = capture == RigCapture::State::armed ? 1 : 2;
        text = "Stop sample";
        sub = "into the sampler or a pad";
    }
    else if (takeLayer >= 0 || engine.getTakes().isActive())
    {
        const bool armed = takeLayer >= 0 && engine.getLooper().getState (takeLayer) == RigLooper::State::armed;
        state = armed ? 1 : 2;
        text = armed ? "Rec" : "Stop rec";
        if (const auto counting = countInText(); counting.isNotEmpty())
            sub = counting + juce::String::fromUTF8 (" \xc2\xb7 ") + recTargetText();
        else
            sub = shownTake.sub;
    }
    if (dock.rec.text != text || dock.rec.sub != sub || dock.rec.state != state)
    {
        dock.rec.text = text;
        dock.rec.sub = sub;
        dock.rec.state = state;
        dock.rec.repaint();
    }
    else if (state == 1)
        dock.rec.repaint();   // (it blinks while it waits)
}

void RigView::showRecMenu()
{
    if (recSetup != nullptr)
        return closeRecSetup();
    struct Backdrop : juce::Component
    {
        std::function<void()> onDown;
        void mouseDown (const juce::MouseEvent&) override { if (onDown) onDown(); }
    };
    auto backdrop = std::make_unique<Backdrop>();
    backdrop->onDown = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)] { if (safe != nullptr) safe->closeRecSetup(); }); };
    recBackdrop = std::move (backdrop);
    addAndMakeVisible (*recBackdrop);
    recBackdrop->setBounds (getLocalBounds());

    recSetup = std::make_unique<rigpanels::RecSetup>();
    recSetup->onCountIn = [this] (int bars) { setRecSetup (bars, recLength); updateRecSetup(); };
    recSetup->onLength = [this] (int bars) { setRecSetup (recCountIn, bars); updateRecSetup(); };
    recSetup->onClick = [this]
    {
        engine.getTransport().metronome = ! engine.getTransport().metronome.load();
        updateDock();
        updateRecSetup();
    };
    recSetup->onLands = [this] (int i)
    {
        setRecLands (i == 0 ? RecLands::layer : i == 1 ? RecLands::song : i == 2 ? RecLands::sample : i == 3 ? RecLands::set : RecLands::pattern);
        updateRecSetup();
    };
    recSetup->onExtra = [this] (int i, juce::Component&)
    {
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this), i]
        {
            if (safe == nullptr)
                return;
            safe->closeRecSetup();
            switch (i)
            {
                case 0: safe->showToSongMenu(); break;
                case 1: safe->showRecTargetMenu (safe->dock.rec); break;
                case 2: safe->showTakesMenu (safe->dock.rec); break;
                default: safe->showAutomation(); break;
            }
        });
    };
    addAndMakeVisible (*recSetup);
    updateRecSetup();
    // Over the Rec, above the bottom bar.
    const auto rec = getLocalArea (&dock, dock.rec.getBounds());
    const int w = rigpanels::RecSetup::kWidth, h = recSetup->preferredHeight();
    recSetup->setBounds (juce::jlimit (8, juce::jmax (8, getWidth() - w - 8), rec.getCentreX() - w / 2), juce::jmax (8, dock.getY() - h + 6), w, h);
    recSetup->toFront (false);
}

void RigView::updateRecSetup()
{
    if (recSetup == nullptr)
        return;
    const int lands = recLands == RecLands::layer ? 0 : recLands == RecLands::song ? 1 : recLands == RecLands::sample ? 2 : recLands == RecLands::set ? 3 : 4;
    recSetup->setState (recCountIn, recLength, engine.getTransport().metronome.load(), lands, toolMode());
}

void RigView::closeRecSetup()
{
    if (recSetup != nullptr)
        removeChildComponent (recSetup.get());
    if (recBackdrop != nullptr)
        removeChildComponent (recBackdrop.get());
    recSetup.reset();
    recBackdrop.reset();
}

void RigView::loadRecSetup()
{
    if (devices == nullptr)
        return;   // (in Logic, and in the tests: the defaults)
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (RigEngine::timingFile()));
    if (xml == nullptr)
        return;
    recCountIn = juce::jlimit (0, 2, xml->getIntAttribute ("recCountIn", recCountIn));
    recLength = juce::jlimit (0, RigLooper::kMaxBars, xml->getIntAttribute ("recLength", recLength));
}

void RigView::saveRecSetup()
{
    if (devices == nullptr)
        return;
    const auto file = RigEngine::timingFile();
    std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
    if (xml == nullptr || ! xml->hasTagName ("Timing"))
        xml = std::make_unique<juce::XmlElement> ("Timing");
    xml->setAttribute ("recCountIn", recCountIn);
    xml->setAttribute ("recLength", recLength);
    file.getParentDirectory().createDirectory();
    xml->writeTo (file);
}

void RigView::pressRecMoves()
{
    auto& a = engine.getAutomation();
    const auto before = a.getRecState();
    engine.pressRecMoves();
    switch (before)
    {
        case RigAutomation::RecState::off:
            say (juce::String ("Rec moves from the next bar: turn any knob while it plays (the bass's Cutoff, an effect's Mix...). ")
                 + (engine.songInCharge() ? "Over the song, so it plays once along the song. " : "")
                 + "Press Rec moves again to stop at the end of the bar.");
            break;
        case RigAutomation::RecState::armed:
            say ("Rec moves called off: nothing was recorded.");
            break;
        case RigAutomation::RecState::recording:
            say ("Moves close at the end of this bar, then play back. Automation shows the lanes.");
            break;
        case RigAutomation::RecState::closing:
            break;
    }
    updateCaptureButtons();
}

void RigView::showAutomation()
{
    if (automationPanel != nullptr && automationPanel->isShowing())
        return;
    auto panel = std::make_unique<AutomationPanel> (engine);
    panel->setLookAndFeel (&vintageLook);
    panel->onSay = [this] (const juce::String& text) { say (text); };
    automationPanel = panel.get();
    juce::Component* anchor = dock.moves.isShowing() ? static_cast<juce::Component*> (&dock.moves) : static_cast<juce::Component*> (&dock.recMoves);
    const auto area = anchor == this ? getLocalBounds().withSizeKeepingCentre (10, 10) : getLocalArea (anchor, anchor->getLocalBounds());
    juce::CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

void RigView::updateCaptureButtons()
{
    const int t = getTrack();
    const auto name = GrooveView::trackName (t);
    auto& looper = engine.getLooper();
    juce::String takeText = "Rec take", takeSub;
    auto takeLit = vintage::Lit::none;
    if (takeLayer >= 0)
    {
        auto st = looper.getState (takeLayer);
        if (st == RigLooper::State::armed || st == RigLooper::State::recording)
            takeStarted = true;
        else if (st == RigLooper::State::empty && ! takeStarted)
            st = RigLooper::State::armed;   // (the press isn't through to the audio yet)
        if (st == RigLooper::State::armed)
        {
            takeSub = "next bar, loop " + juce::String (takeLayer + 1);
            takeLit = vintage::Lit::arm;
        }
        else if (st == RigLooper::State::recording)
        {
            takeText = "Stop take";
            const int secs = (int) looper.recordedSeconds (takeLayer);
            takeSub = "loop " + juce::String (takeLayer + 1) + "  " + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
            if (looper.peekEnding (takeLayer) == RigLooper::Ending::kept && ! takeKeptSaid)
            {
                takeKeptSaid = true;
                say ("The song switched loops, but loop " + juce::String (takeLayer + 1) + " is recording your take, so it was left alone. Keep playing.");
            }
            takeLit = vintage::Lit::arm;
        }
        else
        {
            // It closed by itself (or elsewhere): it's the track's last take now.
            if (st == RigLooper::State::playing && juce::isPositiveAndBelow (takeTrack, (int) lastTake.size()))
                lastTake[(size_t) takeTrack] = takeLayer;
            const auto mins = [] (double beats, double bpm) { return juce::String (beats * 60.0 / juce::jmax (1.0, bpm) / 60.0, 1) + " min"; };
            const double beats = looper.getLayer (takeLayer).lengthBeats.load();
            switch (looper.takeEnding (takeLayer))
            {
                case RigLooper::Ending::full:
                    say ("Take on loop " + juce::String (takeLayer + 1) + " stopped by itself at " + mins (beats, engine.getTransport().getBpm())
                         + ": that's all the room left (one take can be " + juce::String ((int) (RigLooper::kMaxRecordSeconds / 60)) + " minutes, "
                         + juce::String ((int) (RigLooper::kTotalRecordSeconds / 60)) + " for all the loops together). Clear a loop you don't need for more.");
                    break;
                case RigLooper::Ending::clockStopped:
                    say ("The clock stopped, so the take on loop " + juce::String (takeLayer + 1) + " closed at the end of that bar (" + mins (beats, engine.getTransport().getBpm())
                         + "). Nothing you played was lost. To Song puts it in a section.");
                    break;
                case RigLooper::Ending::kept:
                case RigLooper::Ending::asked:
                    break;
            }
            takeLayer = -1;
        }
    }
    if (takeLayer < 0)
        takeSub = t == GrooveView::loops ? "next empty loop" : name + " to a loop";
    if (takeLayer < 0 && engine.getTakes().isActive())
    {
        takeText = "Stop take";
        takeSub = "take " + juce::String (engine.getTakes().passNumber());
        takeLit = vintage::Lit::arm;
    }
    else if (takeLayer < 0 && ! recFree && t != GrooveView::loops)
    {
        const auto list = rigsong::sections (engine.getTimeline());
        if (const int sec = recTargetSection(); juce::isPositiveAndBelow (sec, (int) list.size()))
            takeSub = "into " + rigsong::letterFor (sec) + " " + list[(size_t) sec].name;
    }
    shownTake = { takeText, takeSub, takeLit };

    // Automation.
    {
        auto& a = engine.getAutomation();
        juce::String text = "Rec moves", sub = "any knob, next bar";
        auto lit = vintage::Lit::none;
        switch (a.getRecState())
        {
            case RigAutomation::RecState::off: break;
            case RigAutomation::RecState::armed: sub = "next bar"; lit = vintage::Lit::arm; break;
            case RigAutomation::RecState::recording:
                text = "Stop moves";
                sub = a.getNumRecording() == 0 ? juce::String ("turn a knob") : juce::String (a.getNumRecording()) + (a.getNumRecording() == 1 ? " knob" : " knobs");
                lit = vintage::Lit::arm;
                break;
            case RigAutomation::RecState::closing: sub = "end of bar"; lit = vintage::Lit::arm; break;
        }
        if (const int made = automationPanel == nullptr ? a.takeFinishedLanes() : -1; made >= 0)
            say (made == 0 ? juce::String ("Rec moves: nothing moved, so no lanes were made. Turn a knob while it records.")
                           : "Recorded " + juce::String (made) + (made == 1 ? " lane" : " lanes") + " of moves: they play back now. Automation shows them (draw, clear, Loop or Once).");
        const int lanes = a.getNumLanes();
        const juce::String movesSub = lanes == 0 ? juce::String ("no lanes yet")
                                                 : juce::String (lanes) + (lanes == 1 ? " lane" : " lanes") + (a.reading.load() ? "" : ", read off");
        auto& b = dock.recMoves;
        if (b.text != text || b.sub != sub || b.lit != lit)
        {
            b.text = text;
            b.sub = sub;
            b.lit = lit;
            b.repaint();
        }
        if (dock.moves.sub != movesSub)
        {
            dock.moves.sub = movesSub;
            dock.moves.repaint();
        }
    }

    const auto list = rigsong::sections (engine.getTimeline());
    juce::String songSub;
    if (const int sel = song != nullptr ? song->getSelectedSection() : -1; juce::isPositiveAndBelow (sel, (int) list.size()))
        songSub = "into " + rigsong::letterFor (sel) + " " + list[(size_t) sel].name;
    else
        songSub = list.empty() ? "a first section" : "into a section";
    toSongSub = songSub;
    updateDock();
}

int RigView::toSong (int section, bool take)
{
    const int t = getTrack();
    int lane = laneForTrack (t);
    juce::String what;
    if (take)
    {
        const int layer = lastTakeOf (t);
        if (layer < 0)
        {
            say ("No take yet for " + GrooveView::trackName (t) + ": Rec take records one.");
            return -1;
        }
        lane = RigTimeline::laneForLoop (layer);
        what = "Your take (loop " + juce::String (layer + 1) + ")";
    }
    if (lane < 0)
        return -1;
    if (! take)
        what = rigsong::laneName (lane) + " " + rigsong::clipName (engine, lane, rigsong::currentClip (engine, lane));
    auto& tl = engine.getTimeline();
    const auto list = rigsong::sections (tl);
    int shown = section;
    if (section == -2)
    {
        if (list.empty())
            return toSong (-1, take);
        for (int i = 0; i < (int) list.size(); ++i)
            rigsong::boxFromNow (engine, i, lane);
        shown = 0;
        say (what + " plays in every section now. Undo on the Song screen (Cmd+Z) takes it back.");
    }
    else if (section < 0)
    {
        shown = rigsong::sectionFromLane (engine, lane, rigsong::suggestName (tl));
        if (shown < 0)
        {
            say ("A song can have up to " + juce::String (rigsong::kMaxSections) + " sections.");
            return -1;
        }
        const auto now = rigsong::sections (tl);
        say (what + " is a new section: " + rigsong::letterFor (shown) + " (" + now[(size_t) shown].name + "). Song (F3) to arrange it; "
             "Undo there (Cmd+Z) takes it back.");
    }
    else if (juce::isPositiveAndBelow (section, (int) list.size()))
    {
        rigsong::boxFromNow (engine, section, lane);
        say (what + " plays in " + rigsong::letterFor (section) + " (" + list[(size_t) section].name + ") now. Song (F3) to arrange it; "
             "Undo there (Cmd+Z) takes it back.");
    }
    else
        return -1;
    if (song != nullptr)
    {
        song->refresh();
        song->select (shown, lane);
    }
    footer.repaint();
    updateCaptureButtons();
    return shown;
}

//==============================================================================
// Rec into.
int RigView::recTargetSection() const
{
    auto& tl = engine.getTimeline();
    const auto list = rigsong::sections (tl);
    if (list.empty())
        return -1;
    auto& takes = engine.getTakes();
    if (takes.isActive())
        for (int i = 0; i < (int) list.size(); ++i)
            if (std::abs (list[(size_t) i].start - takes.getStartBar()) < 1.0e-6)
                return i;
    if (juce::isPositiveAndBelow (recSection, (int) list.size()))
        return recSection;
    if (song != nullptr && juce::isPositiveAndBelow (song->getSelectedSection(), (int) list.size()))
        return song->getSelectedSection();
    auto& clock = engine.getTransport();
    if (tl.on.load() && clock.isPlaying())
        if (const int at = rigsong::sectionAt (tl, clock.getPositionInBeats() / juce::jmax (1, clock.getBeatsPerBar())); at >= 0)
            return at;
    return 0;
}

int RigView::recTargetLayer() const
{
    auto& takes = engine.getTakes();
    if (takes.isActive())
        return takes.getLayer();
    const int t = getTrack();
    if (t == GrooveView::loops)
        return groove != nullptr ? groove->getPickedLayer() : -1;
    if (juce::isPositiveAndBelow (recLayer, RigLooper::kLayers))
        return recLayer;
    // A tool played by notes records its notes (unless asked for its sound, onto a loop).
    if (const int source = takeSourceFor (t); RigEngine::isNotesSource (source) && ! recAsSound)
        return RigTakes::notesLayer (RigEngine::stripForSource (source));
    const auto list = rigsong::sections (engine.getTimeline());
    const int sec = recTargetSection();
    if (juce::isPositiveAndBelow (sec, (int) list.size()))
        if (const int l = takes.layerFor (takeSourceFor (t), list[(size_t) sec].start, list[(size_t) sec].bars); l >= 0)
            return l;
    return engine.freeLayerForSong();
}

void RigView::pickRecSection (int section)
{
    if (engine.getTakes().isActive())
        return;   // (a take is going: it stays where it is)
    recSection = section;
    recLayer = -1;
    // Cycling: round the newly picked section instead.
    if (engine.getTimeline().cycle.load() && recCycle)
        engine.cycleSection (section, true);
    updateRecInto();
    updateCaptureButtons();
}

void RigView::setRecTarget (int section, int layer)
{
    if (engine.getTakes().isActive())
        return;
    recSection = section;
    recLayer = layer;
    recFree = false;
    updateRecInto();
    updateCaptureButtons();
}

int RigView::pressRecInto()
{
    auto& takes = engine.getTakes();
    const auto list = rigsong::sections (engine.getTimeline());
    if (takes.isActive())
    {
        const bool recording = takes.getState() == RigTakes::State::recording;
        const int layer = takes.getLayer();
        takes.stop();
        engine.serviceTakes();
        if (recording)
            say ((RigTakes::isNotesLayer (layer) ? juce::String ("Stopped. Your takes are notes") : "Stopped. Your takes are on loop " + juce::String (layer + 1))
                 + ": the newest plays in the song (a take cut short too, if it had a bar in it). Takes picks another"
                 + (RigTakes::isNotesLayer (layer) ? juce::String (", or edits its notes.") : juce::String (".")));
        else
            say ("Rec into called off: nothing was recorded.");
        updateRecInto();
        updateCaptureButtons();
        return -1;
    }
    const int sec = recTargetSection();
    if (! juce::isPositiveAndBelow (sec, (int) list.size()))
    {
        say ("Rec into records into a section of the song: make one first (the + key at the bottom keeps what's playing as a section, "
             "or + Section on the Song screen). Or use Rec take on the rail for a free take.");
        return -1;
    }
    const int t = getTrack();
    const int layer = recTargetLayer();
    if (layer < 0)
    {
        say ("All 16 loops are in use. Clear one you don't need (Loops on the rail, then Edit), or record over one: Change > a loop.");
        return -1;
    }
    const int source = t == GrooveView::loops ? engine.getLooper().getLayer (layer).source.load() : takeSourceFor (t);
    const bool notes = RigTakes::isNotesLayer (layer);
    juce::String why;
    if (! engine.recordInto (sec, layer, source, recCycle, &why))
    {
        say (why);
        return -1;
    }
    recSection = sec;
    juce::String note;
    if (t == GrooveView::vocoder && ! engine.micOn.load())
        note = " Your mic is off: switch it on (Mic, top right) to be heard.";
    else if (t == GrooveView::vocoder && ! engine.vocoderOn.load())
        note = " The vocoder is off: switch it on to be heard.";
    const auto& s = list[(size_t) sec];
    say ("Rec into " + rigsong::letterFor (sec) + " " + s.name + ": " + GrooveView::trackName (t)
         + (notes ? juce::String (" as notes you can edit") : " onto loop " + juce::String (layer + 1))
         + " from the top of the section. "
         + (recCycle ? juce::String ("It plays round: every time round is a new take. Stop take when it's right.") : juce::String ("One time through.")) + note);
    updateRecInto();
    updateCaptureButtons();
    return layer;
}

void RigView::toggleCycle()
{
    recCycle = ! recCycle;
    const int sec = recTargetSection();
    engine.cycleSection (sec, recCycle);
    const auto list = rigsong::sections (engine.getTimeline());
    if (recCycle && juce::isPositiveAndBelow (sec, (int) list.size()))
        say ("Cycle: " + rigsong::letterFor (sec) + " " + list[(size_t) sec].name + " plays round, and every time round is a new take.");
    else
        say ("Cycle off: the song plays on, and a take is one time through its section.");
    updateRecInto();
}

void RigView::showRecTargetMenu (juce::Component& from)
{
    if (engine.getTakes().isActive())
    {
        say ("A take is recording: stop it first to record somewhere else.");
        return;
    }
    const auto list = rigsong::sections (engine.getTimeline());
    const int sec = recTargetSection(), layer = recTargetLayer();
    auto& looper = engine.getLooper();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Record into");
    for (int i = 0; i < (int) list.size(); ++i)
        menu.addItem (rigsong::letterFor (i) + juce::String::fromUTF8 (" \xc2\xb7 ") + list[(size_t) i].name + "  (" + rigsong::barsText (list[(size_t) i].bars) + ")",
                      true, i == sec && ! recFree,
                      [safe = juce::Component::SafePointer<RigView> (this), i] { if (safe != nullptr) safe->setRecTarget (i, -1); });
    menu.addSeparator();
    menu.addSectionHeader ("Onto");
    if (const int source = takeSourceFor (getTrack()); RigEngine::isNotesSource (source))
    {
        const auto name = GrooveView::trackName (getTrack());
        menu.addItem ("Notes: plays the " + name + " in the song, and you can edit them", true, ! recAsSound,
                      [safe = juce::Component::SafePointer<RigView> (this), sec]
                      {
                          if (safe == nullptr)
                              return;
                          safe->recAsSound = false;
                          safe->setRecTarget (sec, -1);
                      });
        menu.addItem ("Sound: onto a loop layer, as you hear it", true, recAsSound,
                      [safe = juce::Component::SafePointer<RigView> (this), sec]
                      {
                          if (safe == nullptr)
                              return;
                          safe->recAsSound = true;
                          safe->setRecTarget (sec, -1);
                      });
        if (! recAsSound)
        {
            menu.addSeparator();
            menu.addItem ("Free take: onto the next empty loop until you press again (the old way)", true, recFree,
                          [safe = juce::Component::SafePointer<RigView> (this)]
                          {
                              if (safe == nullptr)
                                  return;
                              safe->recFree = true;
                              safe->say ("The next Rec take is a free one: onto the next empty loop from the next bar, until you press again. To Song puts it in the song.");
                              safe->updateCaptureButtons();
                          });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (from));
            return;
        }
        menu.addSeparator();
    }
    const int free = engine.freeLayerForSong();
    menu.addItem (free >= 0 ? "A new layer (loop " + juce::String (free + 1) + ")" : juce::String ("A new layer (no loop free)"), free >= 0,
                  recLayer < 0 && layer == free,
                  [safe = juce::Component::SafePointer<RigView> (this), sec] { if (safe != nullptr) safe->setRecTarget (sec, -1); });
    for (int l = 0; l < RigLooper::kLayers; ++l)
    {
        const bool has = looper.getState (l) != RigLooper::State::empty || engine.getTimeline().laneUsed (RigTimeline::laneForLoop (l));
        if (! has)
            continue;
        auto name = engine.getLoopName (l);
        if (name.isEmpty())
            name = RigLooper::sourceName (looper.getLayer (l).source.load());
        const auto* set = engine.getTakes().setFor (l);
        menu.addItem ("Loop " + juce::String (l + 1) + ": " + name + (set != nullptr ? juce::String (" (more takes)") : juce::String (" (records over it)")),
                      true, layer == l && (recLayer == l || set != nullptr),
                      [safe = juce::Component::SafePointer<RigView> (this), sec, l] { if (safe != nullptr) safe->setRecTarget (sec, l); });
    }
    menu.addSeparator();
    menu.addItem ("Free take: onto the next empty loop until you press again (the old way)", true, recFree,
                  [safe = juce::Component::SafePointer<RigView> (this)]
                  {
                      if (safe == nullptr)
                          return;
                      safe->recFree = true;
                      safe->say ("The next Rec take is a free one: onto the next empty loop from the next bar, until you press again. To Song puts it in the song.");
                      safe->updateCaptureButtons();
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (from));
}

void RigView::showTakesMenu (juce::Component& from)
{
    const int layer = recTargetLayer();
    const auto* set = engine.getTakes().setFor (layer);
    if (set == nullptr || set->takes.empty())
    {
        say ("No takes on this layer yet: Rec into records some.");
        return;
    }
    juce::PopupMenu menu, del;
    const bool notes = RigTakes::isNotesLayer (layer);
    menu.addSectionHeader ((notes ? GrooveView::trackName (getTrack()) + " notes" : "Loop " + juce::String (layer + 1))
                           + juce::String::fromUTF8 (" \xc2\xb7 pick the take that plays"));
    for (int i = (int) set->takes.size(); --i >= 0;)
    {
        const auto& take = set->takes[(size_t) i];
        const auto name = "Take " + juce::String (take.number) + (take.partial ? " (cut short)" : "");
        menu.addItem (name + (i == set->playing ? "  - playing" : ""), true, i == set->playing,
                      [safe = juce::Component::SafePointer<RigView> (this), layer, i, name]
                      {
                          if (safe == nullptr)
                              return;
                          if (safe->engine.getTakes().isActive())
                          {
                              safe->say ("Stop the take first, then pick one.");
                              return;
                          }
                          safe->engine.useTake (layer, i);
                          safe->say (name + (RigTakes::isNotesLayer (layer) ? juce::String (" plays now (its notes, as played: your edits to the last one are gone).")
                                                                            : " plays on loop " + juce::String (layer + 1) + " now."));
                      });
        if (i != set->playing)
            del.addItem (name, [safe = juce::Component::SafePointer<RigView> (this), layer, i, name]
                         {
                             if (safe != nullptr && safe->engine.getTakes().deleteTake (layer, i))
                                 safe->say (name + " deleted.");
                         });
    }
    menu.addSeparator();
    if (notes)
    {
        const int strip = RigTakes::stripOfNotesLayer (layer);
        const double startBar = set->startBar;
        menu.addItem ("Edit notes...", engine.noteClipAt (strip, startBar) >= 0, false,
                      [safe = juce::Component::SafePointer<RigView> (this), strip, startBar]
                      {
                          if (safe != nullptr)
                              safe->openNoteEditor (strip, startBar);
                      });
        menu.addItem ("Make it sound: the take as it sounded, onto a loop", ! engine.getTakes().isActive(), false,
                      [safe = juce::Component::SafePointer<RigView> (this), layer]
                      {
                          if (safe == nullptr)
                              return;
                          const int l = safe->engine.takeToLoop (layer);
                          safe->say (l >= 0 ? "The take plays as sound on loop " + juce::String (l + 1) + " now, in place of its notes."
                                            : juce::String ("No loop is free (or the take's sound wasn't kept with the show). Clear a loop first."));
                      });
        menu.addSeparator();
    }
    menu.addSubMenu ("Delete a take", del, del.getNumItems() > 0);
    menu.addItem ("Keep only the one playing", set->takes.size() > 1, false,
                  [safe = juce::Component::SafePointer<RigView> (this), layer]
                  {
                      if (safe == nullptr)
                          return;
                      safe->engine.getTakes().keepOnlyPlaying (layer);
                      safe->say ("Kept the take that plays; the others are gone.");
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (from));
}

void RigView::openNoteEditor (int strip, double startBar)
{
    if (engine.noteClipAt (strip, startBar) < 0)
    {
        say ("There are no notes there yet: Rec into records some.");
        return;
    }
    if (noteEditorWindow != nullptr)
        delete noteEditorWindow.getComponent();
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (new NoteEditor (engine, strip, startBar));
    options.dialogTitle = "Edit notes";
    options.dialogBackgroundColour = vintage::pal().face;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    noteEditorWindow = options.launchAsync();
}

void RigView::addLayerWith (int track)
{
    const auto list = rigsong::sections (engine.getTimeline());
    if (list.empty())
    {
        say ("Make the song's first section first (+ First section), then add layers to it.");
        return;
    }
    if (engine.getTakes().isActive())
    {
        say ("A take is recording: stop it first (Stop take, bottom left).");
        return;
    }
    int sec = song != nullptr ? song->getSelectedSection() : -1;
    if (! juce::isPositiveAndBelow (sec, (int) list.size()))
        sec = recTargetSection();
    recSection = sec;
    recLayer = -1;
    recFree = false;
    pickTrack (track);
    showView ("Edit");
    // The song plays round that section, so you can find your part before recording it.
    engine.cycleSection (sec, recCycle);
    auto& tl = engine.getTimeline();
    auto& clock = engine.getTransport();
    const bool there = tl.on.load() && clock.isPlaying()
                       && rigsong::sectionAt (tl, clock.getPositionInBeats() / juce::jmax (1, clock.getBeatsPerBar())) == sec;
    if (! there)
        engine.launchSection (sec);
    say (GrooveView::trackName (track) + " is ready for " + rigsong::letterFor (sec) + " " + list[(size_t) sec].name
         + (recCycle ? ", playing round. " : ". ") + "Play along, then Rec into (bottom left) records a take.");
    updateRecInto();
    updateCaptureButtons();
}

void RigView::updateRecInto()
{
    if (toolMode())
        return;
    auto& takes = engine.getTakes();
    const auto list = rigsong::sections (engine.getTimeline());
    rigpanels::Footer::RecInto r;
    const int t = getTrack();
    r.section = recTargetSection();
    r.layer = recTargetLayer();
    r.colourStrip = takes.isActive() ? RigEngine::stripForSource (takes.getSource()) : GrooveView::stripFor (t);
    r.waiting = takes.getState() == RigTakes::State::waiting;
    r.recording = takes.getState() == RigTakes::State::recording;
    r.progress = takes.passProgress();
    const auto& tl = engine.getTimeline();
    if (juce::isPositiveAndBelow (r.section, (int) list.size()))
    {
        const auto& s = list[(size_t) r.section];
        r.cycle = tl.cycle.load() && std::abs (tl.cycleStart.load() - s.start) < 1.0e-6 && std::abs (tl.cycleEnd.load() - s.end()) < 1.0e-6;
        const auto where = rigsong::letterFor (r.section) + " " + s.name;
        const auto loop = RigTakes::isNotesLayer (r.layer) ? GrooveView::trackName (t) + " notes"
                          : r.layer >= 0 ? "loop " + juce::String (r.layer + 1) : juce::String ("no loop free");
        const auto* set = takes.setFor (r.layer);
        r.takes = set != nullptr ? (int) set->takes.size() : 0;
        const auto dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        if (r.recording)
        {
            const int bar = juce::jlimit (1, juce::jmax (1, (int) s.bars), 1 + (int) (r.progress * s.bars));
            r.text = "Take " + juce::String (takes.passNumber()) + dot + where + dot + "bar " + juce::String (bar) + " of " + rigsong::barsText (s.bars)
                     + juce::String::fromUTF8 (" \xe2\x86\x92 ") + loop;
        }
        else if (r.waiting)
            r.text = "Take " + juce::String (takes.nextPassNumber()) + " starts at the top of " + where + juce::String::fromUTF8 (" \xe2\x86\x92 ") + loop;
        else if (recFree)
            r.text = "Next: a free take (Rec take on the rail)";
        else
        {
            const auto bars = "bars " + juce::String ((int) s.start + 1) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String ((int) std::ceil (s.end() - 1.0e-6));
            const bool notesLayer = RigTakes::isNotesLayer (r.layer);
            const bool notesThere = notesLayer && engine.noteClipAt (RigTakes::stripOfNotesLayer (r.layer), s.start) >= 0;
            const auto onto = set != nullptr && ! set->takes.empty() && (! notesLayer || std::abs (set->startBar - s.start) < 1.0e-6) ? "more takes of " + loop
                              : notesLayer ? (notesThere ? "new " + loop + " (in place of these)" : loop)
                              : r.layer >= 0 && engine.getLooper().getState (r.layer) != RigLooper::State::empty ? "over " + loop
                              : "new layer on " + loop;
            r.text = "Rec into " + where + dot + bars + dot + GrooveView::trackName (t) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + onto;
        }
    }
    footer.setRecInto (r);
    if (const int made = takes.getTakesMade(); made != takesSeen)
    {
        takesSeen = made;
        const int layer = recTargetLayer();
        if (const auto* set = takes.setFor (layer); set != nullptr && juce::isPositiveAndBelow (set->playing, (int) set->takes.size()))
        {
            const auto& take = set->takes[(size_t) set->playing];
            say ("Take " + juce::String (take.number) + (take.partial ? " (cut short)" : "") + " is in, "
                 + (RigTakes::isNotesLayer (layer) ? juce::String (take.notes.size()) + " notes" : "on loop " + juce::String (layer + 1))
                 + (takes.isActive() ? ". Keep going for another, or Stop take." : ". Takes picks another."));
        }
    }
}

void RigView::showToSongMenu()
{
    const int t = getTrack();
    const int lane = laneForTrack (t);
    // The drone has no lane of its own in the song: its takes (loops) go in.
    if (lane < 0 && t == GrooveView::drone && lastTakeOf (t) < 0)
        say ("The drone has no lane of its own in the song: Rec take records it onto a loop, then To Song puts that loop in.");
    if (lane < 0 && (t != GrooveView::drone || lastTakeOf (t) < 0))
        return;
    const auto list = rigsong::sections (engine.getTimeline());
    const int picked = song != nullptr ? song->getSelectedSection() : -1;

    // The same choices, for what the track plays and for its take.
    auto fill = [&] (juce::PopupMenu& m, bool take)
    {
        for (int i = 0; i < (int) list.size(); ++i)
        {
            const int bi = rigsong::boxBlock (engine.getTimeline(), i, take ? RigTimeline::laneForLoop (juce::jmax (0, lastTakeOf (t))) : lane);
            auto text = "Into " + rigsong::letterFor (i) + juce::String::fromUTF8 (" \xc2\xb7 ") + list[(size_t) i].name;
            if (i == picked)
                text << "  (picked)";
            if (bi >= 0)
                text << "  - replaces " + rigsong::clipName (engine, engine.getTimeline().getBlocks()[(size_t) bi].lane,
                                                             engine.getTimeline().getBlocks()[(size_t) bi].clip);
            m.addItem (text, [safe = juce::Component::SafePointer<RigView> (this), i, take] { if (safe != nullptr) safe->toSong (i, take); });
        }
        if (list.size() > 1)
            m.addItem ("Into every section", [safe = juce::Component::SafePointer<RigView> (this), take] { if (safe != nullptr) safe->toSong (-2, take); });
        m.addItem (list.empty() ? "As the first section" : "As a new section at the end", list.size() < (size_t) rigsong::kMaxSections, false,
                   [safe = juce::Component::SafePointer<RigView> (this), take] { if (safe != nullptr) safe->toSong (-1, take); });
    };

    juce::PopupMenu menu;
    if (lane < 0)
    {
        menu.addSectionHeader ("Lock your drone take (loop " + juce::String (lastTakeOf (t) + 1) + ") into the song");
        fill (menu, true);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dock.rec));
        return;
    }
    const auto what = rigsong::laneName (lane) + " " + rigsong::clipName (engine, lane, rigsong::currentClip (engine, lane));
    menu.addSectionHeader ("Lock " + what + " into the song");
    fill (menu, false);
    if (const int layer = lastTakeOf (t); layer >= 0 && t != GrooveView::loops)
    {
        juce::PopupMenu takeMenu;
        fill (takeMenu, true);
        menu.addSeparator();
        menu.addSubMenu ("Your take (loop " + juce::String (layer + 1) + ")", takeMenu);
    }
    menu.addSeparator();
    menu.addItem ("A new section with everything playing now", list.size() < (size_t) rigsong::kMaxSections, false,
                  [safe = juce::Component::SafePointer<RigView> (this)] { if (safe != nullptr) safe->keepAsSongPart(); });
    menu.addItem (juce::String::fromUTF8 ("Open the Song screen \xe2\x80\xba"), [safe = juce::Component::SafePointer<RigView> (this)]
                  { if (safe != nullptr) safe->showView ("Song"); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (dock.rec));
}

int RigView::trackForLane (int lane)
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return -1;
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (info.isLoop)
        return GrooveView::loops;
    switch (info.index)
    {
        case RigEngine::mpcStrip:     return GrooveView::drums;
        case RigEngine::drumsStrip:   return GrooveView::machine;
        case RigEngine::bassStrip:    return GrooveView::bass;
        case RigEngine::synthStrip:   return GrooveView::synth;
        case RigEngine::vocoderStrip: return GrooveView::vocoder;
        case RigEngine::samplerStrip: return GrooveView::sampler;
        case RigEngine::inputStrip:   return GrooveView::lineIn;
        case RigEngine::droneStrip:   return GrooveView::drone;
        default:                      return -1;
    }
}

void RigView::openSongBox (int lane, int clip, bool edit)
{
    const int t = trackForLane (lane);
    if (t < 0)
        return;
    // The tool goes to the box's pattern, so what you change is what that box plays (and every
    // other box playing the same pattern).
    if (clip >= 0)
        engine.applyClip (lane, clip);
    pickTrack (t);
    showView (edit ? "Edit" : "Play");
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (info.isLoop && edit && looper != nullptr)
        looper->editLoop (info.index);
    const auto what = rigsong::laneName (lane) + (clip >= 0 ? ", " + rigsong::clipName (engine, lane, clip) : juce::String());
    say (clip >= 0 ? what + ": changes here play in every Song box with " + rigsong::clipName (engine, lane, clip)
                         + ". Song (F3) goes back."
                   : what + ". Song (F3) goes back.");
}

void RigView::showView (const juce::String& view)
{
    // A one-tool plugin is all Edit (its tool in full) and Mix (its effects): Logic is the song.
    if (toolMode() && (view == "Play" || view == "Song"))
        return showView ("Edit");
    if (view == "Play")
        return showTab ("Groove");
    if (view == "Song")
        return showTab ("Song");
    if (view == "Mix")
    {
        // The Effects board (for a track's or a layer's effects) counts as Mix too: from there,
        // Mix goes back to the faders.
        return showTab ("Mixer");
    }
    if (view != "Edit")
        return;
    const int t = getTrack();
    if (t == GrooveView::lineIn && effects != nullptr)
        effects->showSource (EffectsView::lineIn);
    if (t == GrooveView::sampler && samplerScreen != nullptr && samplerScreen->getSide() == SamplerScreen::mpcSide)
        samplerScreen->setSide (SamplerScreen::chopSide);
    if (t == GrooveView::loops && looper != nullptr)
    {
        looper->showTimeline (false);
        looper->showSongGrid (false);
    }
    showTab (editPageFor (t));
}

void RigView::showChords (bool show)
{
    if (show == chordsOpen)
        return;
    if (show && soundsOpen)
        showSounds (false);
    chordsOpen = show;
    chordsDrawer->setVisible (show);
    if (show)
        chordsDrawer->refresh();
    resized();
    screenBar.setCurrent (getView(), soundsOpen, currentPage() == "Setup" || currentPage() == "More", chordsOpen);
}

void RigView::showSounds (bool show)
{
    if (show && chordsOpen)
        showChords (false);
    const int i = pageIndex ("Sounds");
    if (i < 0 || show == soundsOpen)
        return;
    auto& page = pages[(size_t) i];
    if (show)
    {
        if (page.window != nullptr)
        {
            page.window->toFront (true);
            return;
        }
        // The tab keeps a note of where the library went (as when it's in its own window).
        const int current = tabs.getCurrentTabIndex();
        page.placeholder = std::make_unique<Placeholder> (*this, i);
        tabs.removeTab (i);
        tabs.addTab (page.name, Theme::panel, page.placeholder.get(), false, i);
        tabs.setCurrentTabIndex (current == i ? pageIndex ("Groove") : current);
        soundsOpen = true;
        soundsDrawer->setContent (page.content.get());
        page.content->setVisible (true);
        soundsDrawer->setVisible (true);
        soundsDrawer->toFront (false);
    }
    else
    {
        soundsDrawer->setContent (nullptr);
        soundsDrawer->removeChildComponent (page.content.get());
        soundsDrawer->setVisible (false);
        soundsOpen = false;
        const int current = tabs.getCurrentTabIndex();
        tabs.removeTab (i);
        tabs.addTab (page.name, Theme::panel, page.content.get(), false, i);
        tabs.setCurrentTabIndex (current);
        page.placeholder.reset();
    }
    resized();
    screenBar.setCurrent (getView(), soundsOpen, currentPage() == "Setup" || currentPage() == "More", chordsOpen);
}

void RigView::showShowMenu()
{
    juce::PopupMenu menu;
    const auto name = currentShow.getFileNameWithoutExtension();
    menu.addSectionHeader (name.isNotEmpty() ? "Show: " + name : juce::String ("This show isn't saved yet"));
    menu.addItem (1, "New song...");
    menu.addItem (2, "Open show...");
    menu.addItem (3, "Save show   (Cmd+S)");
    menu.addItem (4, "Save show as...");
    menu.addSeparator();
    menu.addItem (5, "Export for Logic (WAVs + MIDI)...");
    menu.addItem (9, "Import a song from Logic (stems + markers)...");
    menu.addSeparator();
    {
        // One tool's sound, for Kaleidobox Vocoder, MPC... on their own Logic tracks.
        const int strip = toolSoundStrip();
        const auto tool = RigEngine::onlyToolName (strip);
        menu.addItem (7, "Keep the " + (tool.isNotEmpty() ? tool : juce::String ("picked track")) + "'s sound as a tool sound...", tool.isNotEmpty());
        menu.addItem (8, "Open a tool sound...");
        // The factory kits, the same five as the phone's: each onto a new MPC kit.
        juce::PopupMenu kits;
        for (int i = 0; i < (int) crate::factory::kits().size(); ++i)
            kits.addItem (200 + i, juce::String (crate::factory::kits()[(size_t) i].name) + "  ·  " + crate::factory::kits()[(size_t) i].what);
        menu.addSubMenu ("Factory kits on the pads", kits);
    }
    menu.addSeparator();
    menu.addItem (10, "New pattern: start the next thing, or start over...");
    menu.addItem (11, "Settings   (F6)");
    menu.addItem (13, "Take the 2-minute tour");
    menu.addItem (14, "Show me: make a beat");
    menu.addItem (12, "How to: guides for this screen");
    menu.addItem (6, "The Guide: every tool, and where to start");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.menu),
                        [safe = juce::Component::SafePointer<RigView> (this)] (int result)
                        {
                            if (safe == nullptr)
                                return;
                            switch (result)
                            {
                                case 1: safe->askNewSong(); break;
                                case 2: safe->openShow(); break;
                                case 3: safe->saveShow(); break;
                                case 4: safe->saveShowAs(); break;
                                case 5: safe->exportForLogic(); break;
                                case 6: safe->showTab ("Guide"); break;
                                case 7: safe->keepToolSound (safe->toolSoundStrip()); break;
                                case 8: safe->openToolSound(); break;
                                case 9: safe->askImportFromLogic(); break;
                                case 10: safe->showPatternMenu(); break;
                                case 11: safe->showTab ("Setup"); break;
                                case 12: safe->showHelp (safe->helpPanel == nullptr); break;
                                case 13: safe->tourStop = 0; safe->showHelp (true); break;
                                case 14: safe->startLesson(); break;
                                default:
                                    if (result >= 200 && result < 200 + (int) crate::factory::kits().size())
                                    {
                                        safe->say (safe->engine.loadFactoryKit (result - 200));
                                        safe->refresh();
                                    }
                                    break;
                            }
                        });
}

//==============================================================================
// Make a beat
void RigView::startLesson()
{
    if (engine.isBlank())
        return beginLesson();
    const auto name = currentShow != juce::File() ? currentShow.getFileNameWithoutExtension() : juce::String ("this song");
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Make a beat")
                       .withMessage ("The lesson starts a new song, so " + name + " stays as it is. Save it first?")
                       .withButton ("Save, then the lesson")
                       .withButton ("Lesson, don't save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<RigView> (this)] (int result)
                                  {
                                      if (safe == nullptr || (result != 1 && result != 2))
                                          return;
                                      safe->newSong (result == 1, false);
                                      if (safe->engine.isBlank())
                                          safe->beginLesson();
                                  });
}

void RigView::beginLesson()
{
    if (groove == nullptr || toolMode())
        return;
    showHelp (false);
    if (lesson == nullptr)
    {
        lesson = std::make_unique<RigLesson>();
        lesson->onPhase = [this] (int phase) { applyLessonPhase (phase); };
        lesson->onEnd = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)] { if (safe != nullptr) safe->endLesson(); }); };
        lesson->onAddTool = [this]
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
            {
                if (safe == nullptr)
                    return;
                safe->endLesson();
                safe->say ("Your beat keeps looping. Pick another tool on the left (Bass, Synth, Chords, Vocoder...) and press Rec when you're ready.");
            });
        };
        lesson->onPressRec = [this] { pressRec(); };
        lesson->nextLayerNumber = [this]
        {
            for (int i = 0; i < RigLooper::kLayers; ++i)
                if (engine.getLooper().getState (i) == RigLooper::State::empty)
                    return i + 1;
            return 0;
        };
        addAndMakeVisible (*lesson);
        lessonMetronomeWas = engine.getTransport().metronome.load();
    }
    pickTrack (GrooveView::machine);
    showView ("Play");
    // An empty grid, as on the phone: the lesson's song is a new one, so its starter beat goes
    // (Clear's undo still has it).
    groove->clearPatternNow();
    say ("Make a beat: an empty grid, the click on. Follow the card (Skip lesson any time).");
    resized();
    lesson->begin();
}

void RigView::endLesson()
{
    if (lesson == nullptr)
        return;
    removeChildComponent (lesson.get());
    lesson.reset();
    if (groove != nullptr)
        groove->setGuide (-1, 0);
    engine.getTransport().metronome = lessonMetronomeWas;
    updateDock();
    resized();
}

// What each step sets up on the real screen. Only glows: nothing is ever written for you.
void RigView::applyLessonPhase (int phase)
{
    if (groove == nullptr)
        return;
    if (phase == RigLesson::kick || phase == RigLesson::snare || phase == RigLesson::hats)
    {
        if (groove->getTrack() != GrooveView::machine)
            pickTrack (GrooveView::machine);
        // The pattern loops with the click, so every step clicked is heard in the loop.
        if (! groove->isGoing (GrooveView::machine))
            groove->pressTrackButton (GrooveView::machine);
        engine.getTransport().metronome = true;
        // The grid's rows: the accent, then kick, snare... closed hat is the tenth sound.
        const int row = phase == RigLesson::kick ? 1 : phase == RigLesson::snare ? 2 : 10;
        const juce::uint32 mask = phase == RigLesson::kick ? ((1u << 0) | (1u << 10))
                                : phase == RigLesson::snare ? ((1u << 4) | (1u << 12)) : 0x5555u;
        groove->setGuide (row, mask);
    }
    else
        groove->setGuide (-1, 0);
    if (phase == RigLesson::rec)
    {
        // The take goes on the next layer, from the drum machine.
        if (groove->getTrack() != GrooveView::machine)
            pickTrack (GrooveView::machine);
        setRecLands (RecLands::layer);
        lastTakeLayer = -1;
    }
    if (phase == RigLesson::landed)
        engine.getTransport().metronome = lessonMetronomeWas;
    updateDock();
}

void RigView::tickLesson()
{
    if (lesson == nullptr || lesson->phase() != RigLesson::rec)
        return;
    if (! juce::isPositiveAndBelow (lastTakeLayer, RigLooper::kLayers))
    {
        lesson->setRecBusy (false);
        return;
    }
    const auto state = engine.getLooper().getState (lastTakeLayer);
    if (state == RigLooper::State::armed || state == RigLooper::State::recording)
        lesson->setRecBusy (true);
    else if (state == RigLooper::State::empty)
        lesson->setRecBusy (false);
    else
        lesson->showLanded (lastTakeLayer + 1);
}

//==============================================================================
bool RigView::toolMode() const
{
    return engine.getOnlyTool() >= 0;
}

int RigView::toolSoundStrip() const
{
    if (toolMode())
        return engine.getOnlyTool();
    // In the whole box, the picked track's tool (the Sampler goes with the MPC).
    int strip = GrooveView::stripFor (getTrack());
    if (strip == RigEngine::samplerStrip)
        strip = RigEngine::mpcStrip;
    return RigEngine::onlyToolName (strip).isNotEmpty() ? strip : -1;
}

void RigView::startToolMode()
{
    const int strip = engine.getOnlyTool();
    // Play (the groovebox) and Song are the whole box's; here Logic is the song.
    for (auto* b : screenBar.tabs)
        if (b->text == "Play" || b->text == "Song")
            b->setVisible (false);
    screenBar.resized();
    footer.partsShown = false;
    // Rec records the tool as Logic plays it: no count-in or length of its own.
    dock.countIn.setVisible (false);
    dock.length.setVisible (false);
    dock.resized();
    // The take's two files, to drag into Logic (Rec at the top records them).
    dragMidi = std::make_unique<DragFileChip> ("Drag MIDI onto this track", [this] { return printedMidi; });
    dragAudio = std::make_unique<DragFileChip> ("Drag audio into Logic", [this] { return printedAudio; });
    static_cast<DragFileChip*> (dragMidi.get())->setTooltip ("Drag onto this " + RigEngine::onlyToolName (strip) + " track at the bar in its name: "
                                                             "a MIDI region of what you played, which plays back through this plugin.");
    static_cast<DragFileChip*> (dragAudio.get())->setTooltip ("Drag under your tracks at the bar in its name: Logic makes an audio track "
                                                              "of exactly what this plugin played (its own pattern, pads, effects).");
    footer.addAndMakeVisible (*dragMidi);
    footer.addAndMakeVisible (*dragAudio);
    footer.onLayout = [this] (juce::Rectangle<int> r)
    {
        if (dragMidi == nullptr)
            return r;
        auto chips = r.removeFromRight (juce::jmin (640, r.getWidth() / 2)).reduced (0, 18);
        dragMidi->setBounds (chips.removeFromLeft (chips.getWidth() / 2).withTrimmedRight (6));
        dragAudio->setBounds (chips.withTrimmedLeft (6));
        return r.withTrimmedRight (12);
    };
    footer.resized();
    for (int t = 0; t < GrooveView::numTracks; ++t)
        if (GrooveView::stripFor (t) == strip)
            pickTrack (t);
    showView ("Edit");
    header.show.text = RigEngine::onlyToolName (strip);
    header.show.sub = juce::String::fromUTF8 ("Get from box \xc2\xb7 Send to box");
    header.show.repaint();
    say ("Kaleidobox " + RigEngine::onlyToolName (strip) + ": the box's " + RigEngine::onlyToolName (strip)
         + " on its own Logic track. Rec records it to drag onto the track (below). Click " + RigEngine::onlyToolName (strip)
         + " at the top to get its sound from a Kaleidobox, or send it back.");
}

void RigView::printTake()
{
    const auto& files = engine.getRecorder().getSavedFiles();
    const int strip = engine.getOnlyTool();
    const auto tool = RigEngine::onlyToolName (strip);
    auto& recorder = engine.getRecorder();
    const int bpb = juce::jmax (1, recorder.getStartBeatsPerBar());
    const int bar = (int) std::floor (recorder.getStartBeat() / bpb + 1.0e-6) + 1;
    const auto name = tool + " from bar " + juce::String (bar);
    // The mix is exactly what this plugin plays (only its tool runs: the MPC with its Sampler,
    // through the shared reverb and echo too), and the notes; the per-strip WAVs and the note
    // for the whole set aren't needed.
    const juce::String stemName ("Mix");
    juce::File audio, midi;
    for (const auto& f : files)
    {
        if (f.hasFileExtension ("wav") && f.getFileNameWithoutExtension().fromFirstOccurrenceOf (" ", false, false) == stemName)
            audio = f;
        else if (f.hasFileExtension ("mid"))
            midi = f;
        else
            f.deleteFile();
    }
    for (const auto& f : files)
        if (f.hasFileExtension ("wav") && f != audio)
            f.deleteFile();
    printedAudio = printedMidi = juce::File();
    if (audio.existsAsFile())
    {
        const auto to = audio.getSiblingFile (name + ".wav");
        printedAudio = audio.moveFileTo (to) ? to : audio;
    }
    if (midi.existsAsFile())
    {
        const auto to = midi.getSiblingFile (name + ".mid");
        printedMidi = midi.moveFileTo (to) ? to : midi;
    }
    if (dragMidi != nullptr)
    {
        dragMidi->repaint();
        dragAudio->repaint();
    }
    if (! printedAudio.existsAsFile() && ! printedMidi.existsAsFile())
        return say ("Nothing came out of the " + tool + " while recording, so there's nothing to drag.");
    say ("Recorded the " + tool + " from bar " + juce::String (bar) + ". Drag "
         + (printedMidi.existsAsFile() ? juce::String ("the MIDI onto this track at bar ") + juce::String (bar) + (printedAudio.existsAsFile() ? ", or " : ".") : juce::String())
         + (printedAudio.existsAsFile() ? "the audio under your tracks at bar " + juce::String (bar) + " (Logic makes an audio track)." : juce::String()));
}

void RigView::showToolMenu()
{
    const int strip = engine.getOnlyTool();
    const auto tool = RigEngine::onlyToolName (strip);
    auto boxes = RigEngine::openBoxes();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Kaleidobox " + tool);
    // The boxes open in this Logic song, by their place (Logic doesn't tell us track names).
    for (int i = 0; i < boxes.size(); ++i)
        menu.addItem (100 + i, "Get the " + tool + " from Kaleidobox" + (boxes.size() > 1 ? " " + juce::String (i + 1) : juce::String())
                                   + " in this song");
    for (int i = 0; i < boxes.size(); ++i)
        menu.addItem (200 + i, "Send this " + tool + " to Kaleidobox" + (boxes.size() > 1 ? " " + juce::String (i + 1) : juce::String())
                                   + " in this song");
    if (boxes.isEmpty())
        menu.addItem (99, "(No Kaleidobox in this song: add one on any track to get from it or send to it)", false);
    menu.addSeparator();
    menu.addItem (1, "Get the " + tool + " from a saved show...");
    menu.addItem (2, "Open a tool sound...");
    menu.addItem (3, "Keep as a tool sound...");
    menu.addSeparator();
    menu.addItem (4, "The Guide");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.show),
                        [safe = juce::Component::SafePointer<RigView> (this), strip, tool] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            auto& engine = safe->engine;
                            const auto now = RigEngine::openBoxes();
                            if (result >= 100 && result < 100 + now.size())
                            {
                                const auto sound = now[result - 100]->saveToolSound (strip);
                                safe->say (engine.loadToolSound (sound) ? "Got the " + tool + " from the Kaleidobox in this song."
                                                                        : "That Kaleidobox has no " + tool + " sound to give.");
                                safe->refresh();
                            }
                            else if (result >= 200 && result < 200 + now.size())
                            {
                                auto* box = now[result - 200];
                                if (box->loadToolSound (engine.saveToolSound (strip)))
                                {
                                    safe->say ("Sent this " + tool + " to the Kaleidobox in this song: its " + tool + " sounds like this now.");
                                    box->controlsMoved = true;
                                }
                            }
                            else if (result == 1) safe->getToolFromShowFile();
                            else if (result == 2) safe->openToolSound();
                            else if (result == 3) safe->keepToolSound (strip);
                            else if (result == 4) safe->showTab ("Guide");
                        });
}

void RigView::keepToolSound (int strip)
{
    const auto tool = RigEngine::onlyToolName (strip);
    if (tool.isEmpty())
        return say ("Pick the MPC, Drums, Bass, Synth or Vocoder on the left first.");
    auto folder = RigEngine::toolSoundsFolder();
    folder.createDirectory();
    toolChooser = std::make_unique<juce::FileChooser> ("Keep the " + tool + "'s sound", folder.getChildFile (tool + RigEngine::kToolSoundExtension),
                                                       juce::String ("*") + RigEngine::kToolSoundExtension);
    toolChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<RigView> (this), strip, tool] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || fc.getResult() == juce::File())
                                      return;
                                  const auto file = fc.getResult().withFileExtension (RigEngine::kToolSoundExtension);
                                  const auto sound = safe->engine.saveToolSound (strip);
                                  auto xml = sound.createXml();
                                  safe->say (xml != nullptr && xml->writeTo (file)
                                                 ? "Kept the " + tool + " as " + file.getFileName() + ". Open it in any Kaleidobox or Kaleidobox " + tool + "."
                                                 : "Couldn't save " + file.getFileName() + ".");
                              });
}

void RigView::openToolSound()
{
    auto folder = RigEngine::toolSoundsFolder();
    folder.createDirectory();
    toolChooser = std::make_unique<juce::FileChooser> ("Open a tool sound", folder, juce::String ("*") + RigEngine::kToolSoundExtension);
    toolChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<RigView> (this)] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || ! fc.getResult().existsAsFile())
                                      return;
                                  safe->say (safe->loadToolSoundFile (fc.getResult()));
                              });
}

juce::String RigView::loadToolSoundFile (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    const auto sound = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
    const int strip = sound.getProperty ("strip", -1);
    const auto tool = RigEngine::onlyToolName (strip);
    if (! sound.hasType (RigEngine::kToolSoundType) || tool.isEmpty())
        return file.getFileName() + " isn't a tool sound.";
    if (toolMode() && strip != engine.getOnlyTool())
        return file.getFileName() + " is a " + tool + " sound: open it in Kaleidobox " + tool + " or the whole Kaleidobox.";
    engine.loadToolSound (sound);
    refresh();
    for (int t = 0; t < GrooveView::numTracks; ++t)
        if (GrooveView::stripFor (t) == strip)
            pickTrack (t);
    return "The " + tool + " now has " + file.getFileNameWithoutExtension() + ".";
}

void RigView::getToolFromShowFile()
{
    const int strip = engine.getOnlyTool();
    toolChooser = std::make_unique<juce::FileChooser> ("Get the " + RigEngine::onlyToolName (strip) + " from a show",
                                                       showsFolder(), "*.crateshow");
    toolChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<RigView> (this), strip] (const juce::FileChooser& fc)
                              {
                                  if (safe == nullptr || ! fc.getResult().existsAsFile())
                                      return;
                                  safe->say (safe->getToolFromShow (fc.getResult(), strip));
                              });
}

juce::String RigView::getToolFromShow (const juce::File& showFile, int strip)
{
    auto xml = juce::XmlDocument::parse (showFile);
    const auto show = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
    const auto tool = RigEngine::onlyToolName (strip);
    if (! show.hasType (RigEngine::kShowType))
        return showFile.getFileName() + " isn't a Kaleidobox show.";
    if (! engine.loadToolSound (RigEngine::toolSoundFromShow (show, strip)))
        return showFile.getFileNameWithoutExtension() + " has no " + tool + " sound.";
    refresh();
    return "Got the " + tool + " from " + showFile.getFileNameWithoutExtension() + ".";
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
    if (soundsOpen && pages[(size_t) index].name == "Sounds")
        showSounds (false);
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
    if (mixer != nullptr)
        for (auto [button, control] : mixer->learnButtons())
            show (*button, control);
}

void RigView::timerCallback()
{
    tickLesson();
    auto& transport = engine.getTransport();
    const bool playing = transport.isPlaying();
    if (dock.play.lit != (playing ? vintage::Lit::play : vintage::Lit::none))
    {
        dock.play.lit = playing ? vintage::Lit::play : vintage::Lit::none;
        dock.play.text = playing ? "Stop" : "Play";
        dock.play.icon = playing ? rigpanels::PanelButton::Icon::stop : rigpanels::PanelButton::Icon::play;
        dock.play.repaint();
    }
    rail.chords.selected = chordsOpen;
    rail.chords.status = chordsOpen ? "Open" : "Ready";
    rail.effects.selected = currentPage() == "Effects";
    rail.effects.status = rail.effects.selected ? "Open" : "Ready";
    rail.chords.repaint();
    rail.effects.repaint();
    screenBar.setCurrent (getView(), soundsOpen, currentPage() == "Setup" || currentPage() == "More", chordsOpen);
    pageChanged();
    {
        bool error = false;
        const auto text = engine.takeShowFilesMessage (error);
        if (text.isNotEmpty())
        {
            say (text);
            if (error)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Kaleidobox couldn't save its loops", text);
        }
    }
    if (auto& q = engine.getQuantizer(); q.lastAutoMessage.isNotEmpty())
    {
        const auto text = std::exchange (q.lastAutoMessage, {});
        say (text);
        if (auto* panel = getQuantizePanel())
            panel->setStatus (text);
    }
    if (moreView != nullptr && moreView->isShowing())
        moreView->tick();
    updateCaptureButtons();
    if (phoneLink != nullptr)
        phoneLink->setShow (currentShow.getFileNameWithoutExtension());

    // The Main screen sends the keys to the track you pick.
    if (keysTo.getSelectedId() != engine.getRouting().keysTo.load() + 1)
        keysTo.setSelectedId (engine.getRouting().keysTo.load() + 1, juce::dontSendNotification);

    if (const auto done = engine.serviceCapture(); done.isNotEmpty())
        say (done);
    updateSampleButton();

    if (fxPanel != nullptr)
        fxPanel->tick();
    if (layerFxPanel != nullptr)
        layerFxPanel->tick();
    engine.tickScenes();
    if (engine.serviceRecorder())
    {
        if (song != nullptr && song->recordingSaved())
        {
            // a bounce's real-time pass: it said where the files are
        }
        else if (toolMode())
        {
            printTake();
        }
        else
        {
            const auto& files = engine.getRecorder().getSavedFiles();
            say ("Saved the set: " + juce::String (juce::jmax (0, files.size() - 1)) + " files in Recordings/"
                 + engine.getRecorder().getFolder().getFileName() + ". Open in Logic.txt there says how.");
        }
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
    micMeter = juce::jmax (engine.micPeak.exchange (0.0f), micMeter * 0.85f);

    const bool recent = juce::Time::getMillisecondCounter() - engine.lastMidiTime.load() < 150;
    keyboardActive = recent;

    updateHeader();
    footer.tick();
    {
        const auto& tl = engine.getTimeline();
        const int count = (int) rigsong::sections (tl).size();
        auto& clock = engine.getTransport();
        const bool songOn = tl.on.load() && clock.isPlaying();
        const int bpb = juce::jmax (1, clock.getBeatsPerBar());
        const int current = songOn ? rigsong::sectionAt (tl, clock.getPositionInBeats() / bpb) : -1;
        const double pending = tl.getPendingJump();
        dock.setSections (count, current, songOn && pending >= 0 ? rigsong::sectionAt (tl, pending) : -1);
    }
    updateRecInto();
    {
        // Your show's name, and whether it has been saved yet. A one-tool plugin's is its tool, and
        // its menu gets the sound from a box or sends it back.
        const auto name = currentShow.getFileNameWithoutExtension();
        auto text = name.isNotEmpty() ? name : juce::String ("Unsaved show");
        auto sub = juce::String::fromUTF8 (name.isNotEmpty() ? "Show \xc2\xb7 menu" : "Not saved yet \xc2\xb7 menu");
        if (toolMode())
        {
            text = RigEngine::onlyToolName (engine.getOnlyTool());
            sub = juce::String::fromUTF8 ("Get from box \xc2\xb7 Send to box");
        }
        const auto lit = vintage::Lit::none;
        // A small dot after the name while the song isn't saved anywhere yet.
        const bool dot = ! toolMode() && name.isEmpty();
        if (! toolMode() && name.isEmpty())
            text = "Untitled song";
        if (header.show.text != text || header.show.sub != sub || header.show.lit != lit || header.show.dot != dot)
        {
            header.show.text = text;
            header.show.sub = sub;
            header.show.lit = lit;
            header.show.dot = dot;
            header.show.repaint();
        }
    }

    if (++ticks % 60 == 0)
        openMidiInputs();
}

void RigView::updateHeader()
{
    // The top bar: where the song is and where the next take goes, the master meters, what's
    // plugged in.
    auto& transport = engine.getTransport();
    const bool recent = juce::Time::getMillisecondCounter() - engine.lastMidiTime.load() < 150;
    {
        rigpanels::Header::Shown shown;
        const int beatsPerBar = transport.getBeatsPerBar();
        const double beats = transport.getPositionInBeats();
        const int bar = (int) std::floor (beats / beatsPerBar) + 1;
        const int beat = (int) std::floor (std::fmod (beats, (double) beatsPerBar)) + 1;
        shown.barBeat = "BAR " + juce::String (bar) + "." + juce::String (beat);
        shown.target = recTargetText();
        shown.countIn = countInText();
        // The song's section: where it plays now (with the song on), and the one it goes to next.
        const auto& tl = engine.getTimeline();
        const auto list = rigsong::sections (tl);
        const bool songOn = tl.on.load() && ! list.empty();
        const int current = songOn ? rigsong::sectionAt (tl, beats / juce::jmax (1, beatsPerBar)) : -1;
        const double pending = tl.getPendingJump();
        const int queued = songOn && pending >= 0 ? rigsong::sectionAt (tl, pending) : -1;
        if (juce::isPositiveAndBelow (current, (int) list.size()))
        {
            shown.partLetter = rigsong::letterFor (current);
            shown.partName = list[(size_t) current].name;
        }
        if (juce::isPositiveAndBelow (queued, (int) list.size()) && queued != current)
        {
            shown.partQueued = true;
            shown.partName = "Next: " + rigsong::letterFor (queued) + " " + list[(size_t) queued].name;
        }
        else if (engine.songInCharge())
            shown.partName = shown.partName.isNotEmpty() ? shown.partName : juce::String ("Song");   // (beside Jam)
        else
            shown.partName = (shown.partName.isNotEmpty() ? shown.partName + juce::String::fromUTF8 (" \xc2\xb7 ") : (songOn ? juce::String::fromUTF8 ("Song \xc2\xb7 ") : juce::String()))
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
        shown.recording = whatsRecording().first;
        shown.songOn = engine.songInCharge();
        header.update (shown);
    }
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

void RigView::updatePhoneCard()
{
    const bool app = phoneLink != nullptr;
    phoneCode.setVisible (app);
    phoneUnlink.setVisible (app);
    if (! app)
        phoneHint = "Open the Kaleidobox app (not the Logic plugin) to link Kaleidobox Pocket on your iPhone.";
    else if (! phoneLink->isListening())
        phoneHint = "Couldn't open the link on this Mac's network. Quit and reopen Kaleidobox, or check the firewall in System Settings > Network.";
    else if (phoneLink->isLinked())
        phoneHint = "Linked to " + phoneLink->getPhoneName() + ". It sends sounds here, and its Remote page runs this Mac. Another phone can link with these numbers.";
    else
        phoneHint = "On the iPhone, in Kaleidobox Pocket: More > Mac link, then type these numbers. Same Wi-Fi as this Mac.";
    if (app)
    {
        auto digits = phoneLink->getCode();
        juce::String spaced;
        for (int i = 0; i < digits.length(); ++i)
            spaced << (i > 0 ? " " : "") << digits.substring (i, i + 1);
        phoneCode.setText (spaced, juce::dontSendNotification);
        phoneUnlink.setEnabled (true);
    }
    if (setupPage != nullptr)
    {
        setupPage->resized();
        setupPage->repaint();
    }
}

int RigView::phoneRecState (const juce::String& target) const
{
    if (target == "set")
    {
        const auto st = engine.getRecorder().getState();
        return st == RigRecorder::State::idle ? 0 : st == RigRecorder::State::armed ? 1 : 2;
    }
    if (target == "take")
    {
        if (takeLayer < 0)
            return 0;
        const auto st = engine.getLooper().getState (takeLayer);
        return st == RigLooper::State::armed ? 1 : st == RigLooper::State::recording ? 2 : 0;
    }
    if (target == "moves")
    {
        const auto st = engine.getAutomation().getRecState();
        return st == RigAutomation::RecState::off ? 0 : st == RigAutomation::RecState::armed ? 1 : 2;
    }
    return 0;
}

void RigView::toggleRecordSet()
{
    auto& recorder = engine.getRecorder();
    if (toolMode() && recorder.getState() == RigRecorder::State::idle)
    {
        // A one-tool plugin records for its own Logic track.
        const auto tool = RigEngine::onlyToolName (engine.getOnlyTool());
        const auto folder = RigEngine::logicFolder().getChildFile (tool + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S"));
        if (! engine.startRecording (folder))
            say ("Couldn't start recording in " + folder.getFullPathName() + ".");
        else
            say ((engine.getTransport().isPlaying() ? "Recording the " + tool + " from the next bar" : "Recording the " + tool + " when Logic plays")
                 + ". Press Rec again (or stop Logic) to finish, then drag it onto the track from the bottom of this window.");
        updateRecordButton();
        return;
    }
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
    // In the Rig it records the whole set (there are other Rec buttons); a tool plugin's, its tool.
    juce::String text = toolMode() ? "Rec" : "Rec set";
    if (state == RigRecorder::State::armed)
        text = juce::String::fromUTF8 ("Set \xc2\xb7 Armed");
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
    shownSet = { text, ring ? juce::String ("armed, next bar") : juce::String(), lit };
    setArmed = ring;
    updateDock();
}

void RigView::say (const juce::String& text)
{
    footer.message.setText (text, juce::dontSendNotification);
    sayUntil = juce::Time::getMillisecondCounter() + 5000;
}

void RigView::audioProcessorParameterChangeGestureBegin (juce::AudioProcessor* tool, int parameterIndex)
{
    // Called when a control is touched on screen (on the message thread for the Rig's own windows).
    // Every touch is noted for Move in the Automation panel; while learning, it's the knob's target.
    for (int t = 0; t < RigEngine::numTools; ++t)
        if (&engine.getTool (t) == tool)
        {
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this), t, parameterIndex]
            {
                if (safe != nullptr)
                {
                    // (Only settings that turn smoothly: a switch or a pattern choice can't Move.)
                    const auto& list = safe->engine.getTool (t).getParameters();
                    if (juce::isPositiveAndBelow (parameterIndex, list.size()) && safe->engine.getMacros().getAssigning() >= 0)
                        safe->engine.getMacros().touchBegan (t, parameterIndex, list[parameterIndex]->getValue());
                    if (juce::isPositiveAndBelow (parameterIndex, list.size()) && ! list[parameterIndex]->isDiscrete() && ! list[parameterIndex]->isBoolean())
                    {
                        safe->engine.lastTouchedTool = t;
                        safe->engine.lastTouchedParameter = parameterIndex;
                    }
                }
                if (safe != nullptr && safe->learningKnob)
                {
                    safe->touchedTool = t;
                    safe->touchedParameter = parameterIndex;
                }
            });
        }
}

void RigView::audioProcessorParameterChangeGestureEnd (juce::AudioProcessor* tool, int parameterIndex)
{
    for (int t = 0; t < RigEngine::numTools; ++t)
        if (&engine.getTool (t) == tool)
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this), t, parameterIndex]
            {
                if (safe == nullptr)
                    return;
                auto& macros = safe->engine.getMacros();
                const int m = macros.getAssigning();
                const auto& list = safe->engine.getTool (t).getParameters();
                if (m < 0 || ! juce::isPositiveAndBelow (parameterIndex, list.size()))
                    return;
                if (macros.touchEnded (t, parameterIndex, list[parameterIndex]->getValue()))
                    safe->say (macros.getName (m) + " now turns " + RigEngine::toolName (t) + " " + list[parameterIndex]->getName (40)
                               + " (" + juce::String (macros.getNumTargets (m)) + (macros.getNumTargets (m) == 1 ? " setting" : " settings")
                               + "). Move more, or press Done.");
            });
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
    auto& controllers = engine.getControllers();
    const auto inputs = juce::MidiInput::getAvailableDevices();
    juce::StringArray names, listened;
    juce::String firstNew;
    for (const auto& input : inputs)
    {
        // Its translator first, so it's read as what it is from its first note.
        controllers.translatorFor (input.name);
        const bool on = controllers.isOn (input.name);
        if (devices->isMidiInputDeviceEnabled (input.identifier) != on)
            devices->setMidiInputDeviceEnabled (input.identifier, on);
        names.add (input.name);
        if (on)
            listened.add (input.name);
        if (! controllers.isKnown (input.name))
        {
            // A Launchkey is what Kaleidobox is built around: nothing to ask.
            if (controllers.profileFor (input.name).kind == crate::controllers::Kind::launchkey)
                controllers.markKnown (input.name);
            else if (firstNew.isEmpty())
                firstNew = input.name;
        }
    }
    if (firstNew.isNotEmpty())
        showControllers (firstNew);

    if (inputs != knownInputs || keyboardName.isEmpty() || listened != listenedInputs)
    {
        knownInputs = inputs;
        listenedInputs = listened;
        // The top bar names the keyboard: a Launchkey if there is one, else the first listened to.
        juce::String shown;
        for (const auto& n : listened)
            if (n.containsIgnoreCase ("launchkey"))
                shown = n.containsIgnoreCase ("61") ? "Launchkey 61" : "Launchkey";
        if (shown.isEmpty() && ! listened.isEmpty())
            shown = listened[0].upToFirstOccurrenceOf (" MIDI", false, true).substring (0, 16);
        keyboardFound = ! listened.isEmpty();
        keyboardName = keyboardFound ? shown : (names.isEmpty() ? "No keyboard" : "Not listening");
        header.setTooltip ((names.isEmpty() ? juce::String ("No MIDI keyboard found") : "MIDI: " + listened.joinIntoString (", "))
                           + ". Click the keyboard lamp for MIDI controllers (what each one is).");
    }
}

void RigView::showControllers (const juce::String& newDevice)
{
    if (devices == nullptr)
        return;
    if (auto* open = getControllersPanel())
    {
        if (newDevice.isNotEmpty())
            open->setNewDevice (newDevice);
        controllersWindow->toFront (true);
        return;
    }
    auto panel = std::make_unique<ControllersPanel> (engine.getControllers(),
        [] {
            juce::StringArray n;
            for (const auto& d : juce::MidiInput::getAvailableDevices())
                n.add (d.name);
            return n;
        },
        [safe = juce::Component::SafePointer<RigView> (this)] { if (safe != nullptr) safe->openMidiInputs(); });
    if (newDevice.isNotEmpty())
        panel->setNewDevice (newDevice);
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (panel.release());
    options.dialogTitle = "MIDI controllers";
    options.dialogBackgroundColour = vintage::pal().face;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.componentToCentreAround = this;
    controllersWindow = options.launchAsync();
}

ControllersPanel* RigView::getControllersPanel() const
{
    return controllersWindow != nullptr ? dynamic_cast<ControllersPanel*> (controllersWindow->getContentComponent()) : nullptr;
}

void RigView::sayMicOn()
{
    const auto* device = devices != nullptr ? devices->getCurrentAudioDevice() : nullptr;
    if (devices != nullptr && (device == nullptr || device->getActiveInputChannels().isZero()))
    {
        // Straight to the set-up, where the input is picked (More > Audio and MIDI set-up).
        say ("Mic on, but no mic is coming in: pick your mic or interface as the Input here. If there's none to pick, "
             "allow Kaleidobox in System Settings > Privacy & Security > Microphone, then quit and reopen it.");
        showAudioSettings();
    }
    else
        say ("Mic on: use headphones, or your speakers can feed back into the mic.");
}

void RigView::updateTimingButton()
{
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    const int ms = (int) std::round (engine.getLiveDelaySamples() * 1000.0 / rate);
    timingButton.setButtonText ("Timing: " + juce::String (ms) + " ms earlier");
}

void RigView::showTimingMenu()
{
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    const int trip = (int) std::round (engine.getDeviceRoundTrip() * 1000.0 / rate);
    const float now = engine.getTimingOffsetMs();
    juce::PopupMenu menu;
    menu.addSectionHeader (devices != nullptr ? "Your interface's round trip: " + juce::String (trip) + " ms (always taken off)"
                                              : "In Logic: add Logic's round trip (Logic > Settings > Audio shows it)");
    menu.addSectionHeader ("What you sing still sits...");
    const int offsets[] = { -20, -10, -5, -2, 0, 2, 5, 10, 15, 20, 30, 40, 60 };
    for (int o : offsets)
    {
        juce::String text = o == 0 ? juce::String ("Right: no extra")
                          : o > 0 ? "Late: move it " + juce::String (o) + " ms more earlier"
                                  : "Early: move it " + juce::String (-o) + " ms less";
        menu.addItem (1000 + o + 100, text, true, std::abs (now - (float) o) < 0.5f);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (timingButton),
                        [safe = juce::Component::SafePointer<RigView> (this)] (int r)
                        {
                            if (safe == nullptr || r <= 0)
                                return;
                            safe->engine.setTimingOffsetMs ((float) (r - 1100));
                            safe->updateTimingButton();
                            safe->say ("Recording timing: what you sing or play into the mic is moved "
                                       + juce::String ((int) std::round (safe->engine.getLiveDelaySamples() * 1000.0 / juce::jmax (1.0, safe->engine.getSampleRate())))
                                       + " ms earlier, for the next loops and takes.");
                        });
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
                              writeShowTo (file.withFileExtension ("crateshow"));
                          });
}

bool RigView::writeShowTo (const juce::File& file)
{
    const auto result = engine.saveShowFile (file, rigshow::SaveKind::document);
    if (! result.ok)
    {
        say ("Couldn't save " + file.getFileName() + ": " + result.error);
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save " + file.getFileNameWithoutExtension(),
                                                result.error);
        return false;
    }
    currentShow = file;
    int loops = 0;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (const auto st = engine.getLooper().getState (i); st == RigLooper::State::playing || st == RigLooper::State::overdubbing)
            ++loops;
    say ("Saved " + file.getFileNameWithoutExtension() + ": the show" + (loops > 0 ? " and " + juce::String (loops) + (loops == 1 ? " loop" : " loops") : juce::String())
         + ", in " + file.getParentDirectory().getFullPathName() + ". Cmd+S saves it again."
         + (result.warning.isNotEmpty() ? " " + result.warning : juce::String()));
    return true;
}

void RigView::saveShow()
{
    if (currentShow != juce::File() && currentShow.getParentDirectory().isDirectory())
        writeShowTo (currentShow);
    else
        saveShowAs();
}

void RigView::showHelp (bool show)
{
    if (helpPanel != nullptr)
        removeChildComponent (helpPanel.get());
    helpPanel.reset();
    if (show)
    {
        auto page = currentPage();
        if ((page == "Looper" && looper != nullptr && looper->getTimeline() != nullptr) || (page == "Song" && song != nullptr && song->isShowingBars()))
            page = "Timeline";
        else if (page == "Looper" && looper != nullptr && looper->getSongGrid() != nullptr)
            page = "Song";
        helpPanel = std::make_unique<HelpPanel> (page);
        helpPanel->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<RigView> (this)]
                                                                      { if (safe != nullptr) safe->showHelp (false); }); };
        helpPanel->onPlace = [this] (const juce::String& place)
        {
            if (helpPanel != nullptr)
                tourStop = helpPanel->getStop();
            showTourPlace (place);
        };
        helpPanel->onModeChanged = [this] { resized(); };
        addAndMakeVisible (*helpPanel);
        // The walkthrough first, where you left it; All how-tos is a click away.
        if (! toolMode())
            helpPanel->startTour (tourStop);
        helpPanel->grabKeyboardFocus();
    }

    resized();
}

void RigView::exportForLogic()
{
    const auto result = RigExport::exportForLogic (engine, RigExport::defaultFolder());
    if (result.files == 0)
    {
        say (result.error);
        return;
    }
    say ("Exported " + juce::String (result.files) + " files for Logic to " + result.folder.getFullPathName() + ". Drag them in at bar 1.");
    result.folder.revealToUser();
}

void RigView::openShow()
{
    auto chooser = std::make_shared<juce::FileChooser> ("Open a show", showsFolder(), "*.crateshow");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, chooser] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  openShowFile (file);
                          });
}

void RigView::openShowFile (const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        engine.loadShow (juce::ValueTree::fromXml (*xml));
        engine.readLoopAudio (file);
        currentShow = file;
        refresh();
        if (engine.getLoopAudioProblem().isNotEmpty())
            say (engine.getLoopAudioProblem());
        if (song != nullptr)
            song->forgetUndo();
        restoreWindows();
    }
    else
        say ("Couldn't open " + file.getFileNameWithoutExtension() + ": the file isn't a Kaleidobox song.");
}

void RigView::askOpenSong (const juce::File& file, std::function<void()> then)
{
    if (! then)
        then = [this] { showView ("Play"); };
    if (file == currentShow)
        return then();
    auto open = [this, file, then]
    {
        openShowFile (file);
        if (currentShow == file)
        {
            say ("Opened " + file.getFileNameWithoutExtension() + ".");
            then();
        }
    };
    if (engine.isBlank())
        return open();
    const auto name = currentShow != juce::File() ? currentShow.getFileNameWithoutExtension() : juce::String ("this song");
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Open " + file.getFileNameWithoutExtension() + "?")
                       .withMessage ("Save " + name + " first?")
                       .withButton ("Save, then open")
                       .withButton ("Open, don't save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<RigView> (this), open] (int result)
                                  {
                                      if (safe == nullptr || (result != 1 && result != 2))
                                          return;
                                      if (result == 1)
                                      {
                                          safe->newSong (true, false);   // saves, then clears
                                          if (! safe->engine.isBlank())
                                              return;   // the save failed (it said why): stay
                                      }
                                      open();
                                  });
}

void RigView::startFresh (const juce::String& what, std::function<void()> then)
{
    if (engine.isBlank())
    {
        currentShow = juce::File();
        return then();
    }
    const auto name = currentShow != juce::File() ? currentShow.getFileNameWithoutExtension() : juce::String ("this song");
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle (what)
                       .withMessage ("This starts a new song. Save " + name + " first?")
                       .withButton ("Save, then start")
                       .withButton ("Start, don't save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<RigView> (this), then] (int result)
                                  {
                                      if (safe == nullptr || (result != 1 && result != 2))
                                          return;
                                      safe->newSong (result == 1, false);
                                      if (result == 1 && ! safe->engine.isBlank())
                                          return;   // the save failed (it said why)
                                      then();
                                  });
}

void RigView::catchIdea()
{
    startFresh ("Catch an idea", [this]
    {
        auto& looper = engine.getLooper();
        int layer = -1;
        for (int l = 0; l < RigLooper::kLayers && layer < 0; ++l)
            if (looper.getState (l) == RigLooper::State::empty)
                layer = l;
        if (layer < 0)
            return;
        pickTrack (GrooveView::lineIn);
        showView ("Play");
        // Your mic as it comes in (heard or not), free length, no count-in: it starts now.
        looper.getLayer (layer).source = RigLooper::input;
        looper.getLayer (layer).bars = 0;
        looper.getLayer (layer).countIn = 0;
        looper.pressRecord (layer);
        takeLayer = lastTakeLayer = layer;
        takeTrack = GrooveView::lineIn;
        takeStarted = false;
        takeKeptSaid = false;
        updateCaptureButtons();
        say ("Catching your idea on loop " + juce::String (layer + 1) + ": sing, hum or play. Press Rec when you're done and it loops."
             + (engine.micOn.load() ? juce::String() : juce::String (" (Your mic records without being heard; Mic, top right, lets you hear it.)")));
    });
}

void RigView::startSpark (const crate::spark::Spark& spark)
{
    startFresh ("Start this spark", [this, spark]
    {
        engine.getTransport().setBpm ((double) spark.bpm);
        if (chordsDrawer != nullptr)
            chordsDrawer->setKey (spark.root, spark.minor ? 1 : 0, engine.getChords().fillType);
        else
            engine.getChords().setKey (spark.root, spark.minor ? 1 : 0, engine.getChords().fillType);
        engine.loadFactoryKit (spark.kit, 0);
        refresh();
        using namespace crate::spark;
        switch (spark.steps[0].tool)
        {
            case drums:   pickTrack (GrooveView::machine); showView ("Play"); break;
            case pads:    pickTrack (GrooveView::drums); showView ("Play"); break;
            case sampler: pickTrack (GrooveView::sampler); showView ("Play"); break;
            case synth:   pickTrack (GrooveView::synth); showView ("Play"); break;
            case vocoder: pickTrack (GrooveView::vocoder); showView ("Play"); break;
            case voice:   pickTrack (GrooveView::lineIn); showView ("Play"); break;
            case drone:   pickTrack (GrooveView::drone); showView ("Play"); break;
            case chords:  pickTrack (GrooveView::synth); showView ("Play"); showChords (true); break;
            default:      showView ("Play"); break;
        }
        const auto& kits = crate::factory::kits();
        say (spark.title + ": " + juce::String (spark.bpm) + " BPM, " + spark.keyName()
             + (juce::isPositiveAndBelow (spark.kit, (int) kits.size()) ? ", the " + juce::String (kits[(size_t) spark.kit].name) + " kit" : juce::String())
             + ". 1. " + spark.steps[0].text + ". 2. " + spark.steps[1].text + ". 3. " + spark.steps[2].text + ".");
    });
}

void RigView::startWithTile (int tile)
{
    static const char* const names[RigIdeas::kTiles] = { "A beat", "Pads", "A melody", "Chords", "My voice", "A sample", "A bass line", "Line in" };
    if (! juce::isPositiveAndBelow (tile, RigIdeas::kTiles))
        return;
    startFresh (juce::String ("Start with ") + names[tile], [this, tile]
    {
        switch (tile)
        {
            case 0: pickTrack (GrooveView::machine); showView ("Play");
                    say ("A beat: click steps on the drum machine (or play the pads) and press Play. Rec puts it on a loop."); break;
            case 1: pickTrack (GrooveView::drums); showView ("Play");
                    say ("Pads: play the MPC's pads (the keys A to K, or your pad controller). Rec puts what you play on a loop."); break;
            case 2: pickTrack (GrooveView::synth); showView ("Play");
                    say ("A melody: play the Synth on your keys. Rec puts it on a loop, with its notes to edit."); break;
            case 3: pickTrack (GrooveView::synth); showView ("Play"); showChords (true);
                    say ("Chords: pick a key and play a chord on each pad or key, on the Synth."); break;
            case 4: pickTrack (GrooveView::lineIn); showView ("Play");
                    say ("Your voice: press Rec and sing. It records your mic onto the next loop; Mic, top right, lets you hear yourself."); break;
            case 5: openTool ("Sampler");
                    say ("A sample: record or drop in a sound, trim it, and play it on the pads."); break;
            case 6: pickTrack (GrooveView::bass); showView ("Play");
                    say ("A bass line: play the Bass on your keys, or click its steps. Rec puts it on a loop."); break;
            case 7: pickTrack (GrooveView::lineIn); showView ("Play");
                    say ("Line in: a guitar, a synth or a second mic from your interface. Switch Input on to hear it, then Rec."); break;
            default: break;
        }
    });
}

void RigView::renameSong (const juce::File& file)
{
    auto* w = new juce::AlertWindow ("Rename " + file.getFileNameWithoutExtension(), "A new name for this song:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", file.getFileNameWithoutExtension());
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<RigView> (this), w, file] (int result)
    {
        if (safe == nullptr || result != 1)
            return;
        const auto starred = [&]
        {
            for (auto& s : safe->ideas->songs())
                if (s.file == file)
                    return s.starred;
            return false;
        }();
        juce::File renamed;
        const auto why = RigIdeas::renameSong (file, w->getTextEditorContents ("name"), renamed);
        if (why.isNotEmpty())
            return safe->say (why);
        if (renamed == juce::File())
            return;
        if (starred)
        {
            safe->ideas->setStarred (file, false);
            safe->ideas->setStarred (renamed, true);
        }
        if (safe->currentShow == file)
        {
            safe->currentShow = renamed;
            safe->refresh();
        }
        safe->ideas->refresh();
        safe->say ("Renamed it " + renamed.getFileNameWithoutExtension() + ".");
    }), true);
}

void RigView::trashSong (const juce::File& file)
{
    if (file == currentShow)
        return say (file.getFileNameWithoutExtension() + " is open: start a new song or open another one first, then move it to the Trash.");
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::WarningIcon)
                       .withTitle ("Move " + file.getFileNameWithoutExtension() + " to the Trash?")
                       .withMessage ("The song and its loops go to the Trash. You can take them back out of the Trash in Finder.")
                       .withButton ("Move to Trash")
                       .withButton ("Keep it")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<RigView> (this), file] (int result)
                                  {
                                      if (safe == nullptr || result != 1)
                                          return;
                                      const bool ok = RigIdeas::trashSong (file);
                                      safe->ideas->setStarred (file, false);
                                      safe->ideas->refresh();
                                      safe->say (ok ? file.getFileNameWithoutExtension() + " is in the Trash."
                                                    : "Some of " + file.getFileNameWithoutExtension() + " couldn't go to the Trash.");
                                  });
}

void RigView::showTitle (bool show)
{
    if (! show)
    {
        if (title != nullptr)
        {
            title->setVisible (false);
            title.reset();
        }
        grabKeyboardFocus();
        return;
    }
    title = std::make_unique<RigTitle>();
    if (ideas != nullptr)
        ideas->refresh();
    if (! engine.isBlank())
    {
        int layers = 0;
        std::vector<juce::Colour> colours;
        for (int l = 0; l < RigLooper::kLayers; ++l)
            if (engine.getLooper().getLayer (l).lengthBeats.load() > 0)
            {
                ++layers;
                colours.push_back (RigIdeas::colourForSource (engine.getLooper().getLayer (l).source.load()));
            }
        const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        title->setContinue (currentShow != juce::File() ? currentShow.getFileNameWithoutExtension() : juce::String ("Your last session"),
                            juce::String (engine.getTransport().getBpm(), 0) + " BPM" + dot + juce::String (layers) + (layers == 1 ? " layer" : " layers"),
                            colours);
    }
    std::vector<std::pair<juce::String, juce::File>> recent;
    if (ideas != nullptr)
        for (auto& s : ideas->songs())
            if (! s.lastShow && s.file != currentShow && recent.size() < 3)
                recent.push_back ({ s.name, s.file });
    title->setRecent (std::move (recent));
    title->micLevel = [this] { return juce::jlimit (0.0f, 1.0f, engine.micPeak.load() * 2.0f); };
    // (Each copies its pointer first: closing the title deletes the button's callback, captures and all,
    // while it runs.)
    auto safe = juce::Component::SafePointer<RigView> (this);
    title->onStart = [safe] { if (auto v = safe; v != nullptr) { v->showTitle (false); v->showTab ("Ideas"); } };
    title->onContinue = [safe] { if (auto v = safe; v != nullptr) { v->showTitle (false); v->showView ("Play"); } };
    title->onTour = [safe] { if (auto v = safe; v != nullptr) { v->showTitle (false); v->showTab ("Ideas"); v->tourStop = 0; v->showHelp (true); } };
    title->onRecent = [safe] (const juce::File& f) { if (auto v = safe; v != nullptr) { const auto file = f; v->showTitle (false); v->askOpenSong (file); } };
    addAndMakeVisible (*title);
    title->setBounds (getLocalBounds());
    title->toFront (true);
}

bool RigView::keyPressed (const juce::KeyPress& key)
{
    // F8: Kaleidobox Visuals on and off (where it last showed). While it shows, Esc closes it and
    // Option+1-5 pick a look, Option+0 Drift (1-8 still play the song's sections).
    if (key.getKeyCode() == juce::KeyPress::F8Key)
    {
        showVisuals (visualsAt == VisualsPlace::off ? visualsLast : VisualsPlace::off, visualsDisplay);
        return true;
    }
    if (visualsAt != VisualsPlace::off)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            showVisuals (VisualsPlace::off);
            return true;
        }
        const auto vc = key.getTextCharacter();
        const auto code = key.getKeyCode();
        if (key.getModifiers().isAltDown() && ! key.getModifiers().isCommandDown())
        {
            const int digit = code >= '0' && code <= '9' ? code - '0' : (vc >= '0' && vc <= '9' ? (int) (vc - '0') : -1);
            if (digit >= 1 && digit <= crate::visuals::kLooks)
            {
                visualState.drift = false;
                visualState.look = digit - 1;
                crate::visuals::save (visualState, crate::visuals::defaultFile());
                if (visualsView != nullptr) visualsView->pickLook (digit - 1);
                return true;
            }
            if (digit == 0)
            {
                visualState.drift = ! visualState.drift.load();
                crate::visuals::save (visualState, crate::visuals::defaultFile());
                return true;
            }
        }
    }
    if (key == juce::KeyPress::spaceKey)
    {
        engine.playOrStop();
        return true;
    }
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        saveShow();
        return true;
    }
    // Cmd+U: quantize what's on screen (as in Ableton; Logic's Q plays notes here).
    if (key == juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0))
    {
        quantizeNow();
        return true;
    }
    // Cmd+[ and Cmd+]: back and forward, as in a web browser.
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
    {
        undoLast();
        return true;
    }
    // Delete (or backspace) on the Song screen: the picked section.
    if ((key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) && getView() == "Song" && song != nullptr && ! song->isShowingBars())
    {
        song->pressDelete();
        return true;
    }
    // Cmd+Shift+A: the automation lanes.
    if (juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode()) == 'a' && key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown())
    {
        showAutomation();
        return true;
    }
    if (key == juce::KeyPress ('[', juce::ModifierKeys::commandModifier, 0))
    {
        goBack();
        return true;
    }
    if (key == juce::KeyPress (']', juce::ModifierKeys::commandModifier, 0))
    {
        goForward();
        return true;
    }
    // Esc closes the Sounds or Chords drawer, or leaves the Guide for Play.
    if (key == juce::KeyPress::escapeKey && soundsOpen)
    {
        showSounds (false);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && chordsOpen)
    {
        showChords (false);
        return true;
    }
    if (key == juce::KeyPress::escapeKey && stageShown)
    {
        showTab ("Groove");
        return true;
    }
    // F1-F4: the views; F5 the Sounds drawer; F6 Settings.
    for (int i = 0; i < rigpanels::ScreenBar::views().size(); ++i)
        if (key.getKeyCode() == juce::KeyPress::F1Key + i)
        {
            showView (rigpanels::ScreenBar::views()[i]);
            return true;
        }
    if (key.getKeyCode() == juce::KeyPress::F5Key)
    {
        showSounds (! soundsOpen);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F7Key)
    {
        showChords (! chordsOpen);
        return true;
    }
    if (key.getKeyCode() == juce::KeyPress::F6Key)
    {
        showTab ("Setup");
        return true;
    }
    // 1-8: the song's sections (only ones that exist); on the Looper, or Play with Loops picked, the loop layers.
    const auto c = key.getTextCharacter();
    if (c >= '1' && c <= '8' && ! key.getModifiers().isAnyModifierKeyDown())
    {
        const int n = (int) (c - '1');
        if (currentPage() == "Looper")
            engine.getLooper().recordOrClose (n + (looper != nullptr ? looper->firstShownLayer() : 0));   // the rows shown (never an overdub by accident)
        else if (getView() == "Play" && groove != nullptr && groove->getTrack() == GrooveView::loops)
            groove->pressCell (0, n);               // the same as pressing the layer on screen
        else if (n < (int) rigsong::sections (engine.getTimeline()).size())
            rigpanels::Footer::pressPart (engine, n);
        else
            // A key never makes a section by itself: that's the + Save button, on purpose.
            say ("No section " + rigsong::letterFor (n) + " yet. To keep what's playing as a section, press + Save at the bottom.");
        return true;
    }
    return false;
}

//==============================================================================
bool RigView::undoLast()
{
    // Cmd+Z: takes back the last Clear on Play, or the last take on a loop (the Song screen's
    // own Cmd+Z undoes its boxes).
    const auto view = getView();
    if (view == "Song" && song != nullptr)
        return song->undo();
    if (view == "Play" && groove != nullptr && groove->undoClear())
        return true;
    if (view == "Play" || currentPage() == "Looper")
    {
        auto& looper = engine.getLooper();
        for (int i = 0; i < RigLooper::kLayers; ++i)
            if (looper.canUndo (i) && looper.getState (i) == RigLooper::State::playing)
            {
                looper.undo (i);
                say ("Took back the last recording on top of loop " + juce::String (i + 1) + ".");
                return true;
            }
    }
    say ("Nothing to undo here. (Cmd+Z takes back a Clear on Play, or the last overdub on a loop.)");
    return false;
}

std::pair<juce::String, juce::String> RigView::whatsRecording() const
{
    auto& looper = engine.getLooper();
    {
        auto& takes = engine.getTakes();
        const auto list = rigsong::sections (engine.getTimeline());
        int sec = -1;
        for (int i = 0; i < (int) list.size(); ++i)
            if (std::abs (list[(size_t) i].start - takes.getStartBar()) < 1.0e-6)
                sec = i;
        const auto where = sec >= 0 ? rigsong::letterFor (sec) + " " + list[(size_t) sec].name : juce::String ("the song");
        if (takes.getState() == RigTakes::State::recording)
            return { "Take " + juce::String (takes.passNumber()) + " into " + where, {} };
        if (takes.getState() == RigTakes::State::waiting)
            return { "Take " + juce::String (takes.nextPassNumber()) + " at the top of " + where, {} };
    }
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto n = juce::String (i + 1);
        switch (looper.getState (i))
        {
            case RigLooper::State::armed:       return { "Loop " + n + " records next bar", "Looper" };
            case RigLooper::State::recording:   return { "Recording loop " + n, "Looper" };
            case RigLooper::State::overdubbing: return { "Overdub on loop " + n, "Looper" };
            default: break;
        }
    }
    if (engine.getMpc().getSequencer().isRecording())
        return { "Recording the MPC beat", "MPC" };
    switch (engine.getAutomation().getRecState())
    {
        case RigAutomation::RecState::armed:     return { "Moves record next bar", "Automation" };
        case RigAutomation::RecState::recording:
        case RigAutomation::RecState::closing:   return { "Recording knob moves", "Automation" };
        case RigAutomation::RecState::off:       break;
    }
    const auto capture = engine.getCapture().getState();
    if (capture == RigCapture::State::armed || capture == RigCapture::State::recording)
        return { "Sampling", "Sampler" };
    const auto set = engine.getRecorder().getState();
    if (set == RigRecorder::State::armed || set == RigRecorder::State::recording)
        return { "Recording the set", {} };
    return {};
}

juce::String RigView::currentPlace() const
{
    const auto page = currentPage();
    if (page == "Sampler" && samplerScreen != nullptr && samplerScreen->isEditMode())
        return "Sampler editor";
    if (page == "Song" && song != nullptr && song->isShowingBars())
        return "Song / Bars";
    if (page == "Sampler" && samplerScreen != nullptr && samplerScreen->getSide() == SamplerScreen::mpcSide)
        return "MPC";
    if (page == "Looper" && looper != nullptr)
    {
        if (looper->getTimeline() != nullptr)
            return "Looper / Timeline";
        if (looper->getSongGrid() != nullptr)
            return "Looper / Song grid";
    }
    return page;
}

void RigView::goToPlace (const juce::String& place)
{
    if (place.startsWith ("Song"))
    {
        showTab ("Song");
        if (song != nullptr)
            song->showBars (place == "Song / Bars");
        pageChanged();
        return;
    }
    if (place.startsWith ("Looper / "))
    {
        showTab ("Looper");
        if (looper != nullptr && place.endsWith ("Timeline"))
            looper->showTimeline (true);
        else if (looper != nullptr)
            looper->showSongGrid (true);
        pageChanged();
        return;
    }
    if (place == "Sampler" && samplerScreen != nullptr)
        samplerScreen->setEditMode (false);
    if (place == "Looper" && looper != nullptr)
    {
        looper->showTimeline (false);
        looper->showSongGrid (false);
    }
    showTab (place);
}

void RigView::goBack()
{
    if (const auto place = navigation.back(); place.isNotEmpty())
        goToPlace (place);
    updateNavButtons();
}

void RigView::goForward()
{
    if (const auto place = navigation.forward(); place.isNotEmpty())
        goToPlace (place);
    updateNavButtons();
}

void RigView::updateNavButtons()
{
    // "Back to the Groove", so you know where it goes.
    auto nameOf = [] (juce::String place)
    {
        if (place == "Song / Bars")
            place = "Song: Bars view";
        return place.replace (" / ", ": ");
    };
    auto set = [] (rigpanels::PanelButton& b, bool can, const juce::String& tip)
    {
        if (b.isEnabled() != can)
            b.setEnabled (can);
        if (b.getTooltip() != tip)
            b.setTooltip (tip);
    };
    set (screenBar.back, navigation.canGoBack(),
         navigation.canGoBack() ? "Back to " + nameOf (navigation.previous()) + " (Cmd+[)." : juce::String ("Back to the screen you were on (Cmd+[)."));
    set (screenBar.forward, navigation.canGoForward(),
         navigation.canGoForward() ? "Forward to " + nameOf (navigation.next()) + " (Cmd+])." : juce::String ("Forward again (Cmd+])."));
}

//==============================================================================
void RigView::askNewSong()
{
    // Nothing made yet: no need to ask.
    if (engine.isBlank())
    {
        newSong (false);
        return;
    }
    const auto name = currentShow != juce::File() ? currentShow.getFileNameWithoutExtension() : juce::String ("this song");
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Start a new song?")
                       .withMessage ("Kaleidobox clears every tool, loop and song part for a new song. Save " + name + " first?")
                       .withButton ("Save, then new song")
                       .withButton ("New song, don't save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<RigView> (this)] (int result)
                                  {
                                      // 1, 2, then 0 for the last button (Cancel).
                                      if (safe != nullptr && (result == 1 || result == 2))
                                          safe->newSong (result == 1);
                                  });
}

void RigView::newSong (bool saveFirst, bool askWhatNext)
{
    if (saveFirst)
    {
        auto file = currentShow;
        if (file == juce::File() || ! file.getParentDirectory().isDirectory())
        {
            showsFolder().createDirectory();
            file = showsFolder().getNonexistentChildFile ("Song " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M"), ".crateshow", false);
        }
        if (! writeShowTo (file))
            return;   // (it said why) Nothing is cleared.
    }
    const auto saved = saveFirst ? currentShow.getFileNameWithoutExtension() : juce::String();

    engine.getTransport().stop();
    engine.allNotesOff();
    engine.newShow();
    currentShow = juce::File();
    refresh();
    if (song != nullptr)
        song->forgetUndo();
    takeLayer = -1;
    lastTake.fill (-1);
    say ((saved.isNotEmpty() ? "Saved " + saved + ". " : juce::String()) + "New song: everything's clear. Pick what to start with "
         "(New pattern), set the tempo, and press Play.");
    if (askWhatNext)
        showPatternMenu();
}

//==============================================================================
void RigView::askImportFromLogic()
{
    auto pick = [safe = juce::Component::SafePointer<RigView> (this)]
    {
        if (safe == nullptr)
            return;
        safe->toolChooser = std::make_unique<juce::FileChooser> (
            "Pick the folder with the song's stems (File > Export > All Tracks as Audio Files) and its MIDI file",
            juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        safe->toolChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                        [safe] (const juce::FileChooser& fc)
                                        {
                                            if (safe != nullptr && fc.getResult().isDirectory())
                                                safe->say (safe->importFromLogic (fc.getResult()));
                                        });
    };
    // It's a new song: the loops and sections go. Nothing made yet, no need to ask.
    if (engine.isBlank())
        return pick();
    const auto name = currentShow != juce::File() ? currentShow.getFileNameWithoutExtension() : juce::String ("this song");
    auto options = juce::MessageBoxOptions()
                       .withIconType (juce::MessageBoxIconType::QuestionIcon)
                       .withTitle ("Import a song from Logic")
                       .withMessage ("The Logic song comes in as a new song (its stems as loops, its markers as sections). Save " + name + " first?")
                       .withButton ("Save, then import")
                       .withButton ("Import, don't save")
                       .withButton ("Cancel")
                       .withAssociatedComponent (this);
    juce::AlertWindow::showAsync (options, [safe = juce::Component::SafePointer<RigView> (this), pick] (int result)
                                  {
                                      if (safe == nullptr || (result != 1 && result != 2))
                                          return;
                                      safe->newSong (result == 1, false);
                                      if (result == 2 || safe->engine.isBlank())
                                          pick();
                                  });
}

juce::String RigView::importFromLogic (const juce::File& folder)
{
    auto song = rigimport::read (folder, engine.getTransport().getBpm());
    if (song.layers.empty())
        return song.notes.joinIntoString (". ") + ".";
    engine.getTransport().stop();
    const auto said = rigimport::apply (engine, std::move (song));
    refresh();
    if (this->song != nullptr)
        this->song->forgetUndo();
    showView ("Song");
    return said;
}

void RigView::showPatternMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("Start a new pattern");
    for (int s = 0; s < songstart::numStarts; ++s)
        menu.addItem (s + 1, songstart::name (s));
    menu.addSeparator();
    menu.addSectionHeader ("The song");
    const int parts = (int) rigsong::sections (engine.getTimeline()).size();
    menu.addItem (100, "Keep what's playing as the song's next section (" + rigsong::letterFor (parts) + ")", parts < rigsong::kMaxSections);
    menu.addItem (101, "Open the Song screen");
    menu.addSeparator();
    menu.addItem (102, "Start over: a new, empty song...");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.menu),
                        [safe = juce::Component::SafePointer<RigView> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == 100)
                                safe->keepAsSongPart();
                            else if (result == 101)
                                safe->showTab ("Song");
                            else if (result == 102)
                                safe->askNewSong();
                            else
                                safe->startPattern (result - 1);
                        });
}

void RigView::startPattern (int start)
{
    const auto r = songstart::begin (engine, start);
    if (r.page.isEmpty())
        return;
    if (r.page == "Sampler" && samplerScreen != nullptr)
        samplerScreen->setEditMode (false);
    if (r.page == "Looper" && looper != nullptr)
    {
        looper->showTimeline (false);
        looper->showSongGrid (false);
    }
    showTab (r.page);
    if (r.track >= 0 && groove != nullptr)
        groove->selectTrack (r.track);
    say (r.message);
}

void RigView::keepAsSongPart()
{
    if (song == nullptr)
        return;
    song->addSectionFromPlaying();   // (it says which section, and how to change it)
    footer.repaint();
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

void RigView::showTempoMenu()
{
    auto& transport = engine.getTransport();
    if (transport.isHosted())
    {
        say ("In Logic, Logic sets the tempo (draw a tempo change in Logic's tempo track).");
        return;
    }
    const double now = transport.getBpm();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Ramp the tempo: a glide, in time, while it plays");
    const auto targets = { 5.0, 10.0, 20.0, -5.0, -10.0, -20.0 };
    auto lengths = [this] (double target)
    {
        juce::PopupMenu sub;
        for (int bars : { 1, 2, 4, 8, 16 })
            sub.addItem ("Over " + juce::String (bars) + (bars == 1 ? " bar" : " bars"), [this, target, bars] { startTempoRamp (target, bars); });
        return sub;
    };
    for (double d : targets)
    {
        const double target = juce::jlimit (RigTransport::kMinBpm, RigTransport::kMaxBpm, now + d);
        menu.addSubMenu ((d > 0 ? "Up to " : "Down to ") + juce::String (target, 0) + " BPM", lengths (target));
    }
    menu.addSubMenu ("Double time (" + juce::String (juce::jmin (RigTransport::kMaxBpm, now * 2.0), 0) + " BPM)", lengths (now * 2.0));
    menu.addSubMenu ("Half time (" + juce::String (juce::jmax (RigTransport::kMinBpm, now * 0.5), 0) + " BPM)", lengths (now * 0.5));
    if (transport.getRampTarget() > 0)
    {
        menu.addSeparator();
        menu.addItem ("Stop the ramp (stay at " + juce::String (now, 1) + " BPM)", [this]
        {
            engine.getTransport().cancelRamp();
            say ("Ramp stopped: the tempo stays where it is.");
        });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&header).withMousePosition());
}

void RigView::startTempoRamp (double target, int bars)
{
    auto& transport = engine.getTransport();
    target = juce::jlimit (RigTransport::kMinBpm, RigTransport::kMaxBpm, target);
    transport.rampTo (target, bars * (double) transport.getBeatsPerBar());
    say ("The tempo glides to " + juce::String (target, 0) + " BPM over " + juce::String (bars) + (bars == 1 ? " bar" : " bars")
         + (transport.isPlaying() ? "." : ", from when you press Play.") + " Right-click the tempo to stop it.");
}

void RigView::setDarkPanel (bool dark)
{
    vintage::setStage (dark || stageShown);
    engine.screens.setProperty ("dark", dark, nullptr);
    repaint();
    for (auto* c : std::initializer_list<juce::Component*> { &header, &screenBar, &footer, &dock, &rail, soundsDrawer.get(), chordsDrawer.get() })
        c->repaint();
    if (groove != nullptr)
        groove->repaint();
    darkButton.setToggleState (dark, juce::dontSendNotification);
}

void RigView::showPopOutMenu()
{
    // Any screen or tool, into its own window (or to the front, if it's out already).
    juce::PopupMenu menu;
    menu.addSectionHeader ("Put a screen in its own window");
    for (int i = 0; i < (int) pages.size(); ++i)
        if (pages[(size_t) i].name != "More")
            menu.addItem (i + 1, pages[(size_t) i].name
                                     + (pages[(size_t) i].window != nullptr ? " (out: bring to front)" : ""));
    auto* anchor = currentPage() == "More" ? static_cast<juce::Component*> (moreView)
                 : popOutButton.isShowing()  ? static_cast<juce::Component*> (&popOutButton)
                                             : static_cast<juce::Component*> (&header.menu);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (anchor),
                        [safe = juce::Component::SafePointer<RigView> (this)] (int result)
                        {
                            if (safe == nullptr || result <= 0 || result > (int) safe->pages.size())
                                return;
                            const int i = result - 1;
                            if (safe->pages[(size_t) i].window != nullptr)
                                safe->pages[(size_t) i].window->toFront (true);
                            else
                                safe->popOut (i);
                        });
}

void RigView::showMoreMenu()
{
    juce::PopupMenu menu;
    menu.addItem (20, "My ideas: your songs, and ways to start one", true, currentPage() == "Ideas");
    menu.addItem (1, "The Guide: every tool, and what it's for", true, currentPage() == "Guide");
    menu.addSectionHeader ("Tools in full");
    const char* tools[][2] = { { "MPC", "MPC: pads, programs, sequences" }, { "Drum machine", "Drum machine: analog sounds and patterns" },
                               { "Sampler editor", "Sampler: record, trim, effects" }, { "Bass", "Bass: sound and patterns" },
                               { "Synth", "Synth: sound and arp" }, { "Song", "Song: sections in order, what plays in each" },
                               { "Sounds", "Sounds: your samples and Splice" }, { "Mixer", "Mixer: levels, pan, solo, sends, effects" },
                               { "Setup", "Setup: keys, pads, split, sampling, knobs" } };
    const auto here = currentPage();
    for (int i = 0; i < (int) std::size (tools); ++i)
        menu.addItem (10 + i, tools[i][1], true, here == tools[i][0]);
    menu.addSeparator();
    menu.addItem (2, "Pop this screen out (for a second screen)", here.isNotEmpty());
    menu.addItem (3, "Dark panel (for dark rooms)", true, vintage::isStage());
    menu.addItem (4, "Click (metronome)", true, engine.getTransport().metronome.load());
    menu.addSeparator();
    menu.addItem (8, "Save show");
    menu.addItem (5, "Save show as...");
    menu.addItem (6, "Open show...");
    menu.addItem (9, "Export loops for Logic...");
    if (devices != nullptr)
        menu.addItem (7, "Audio and MIDI set-up...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.menu),
                        [safe = juce::Component::SafePointer<RigView> (this), names = std::vector<juce::String> { "MPC", "Drum machine", "Sampler editor", "Bass", "Synth", "Song", "Sounds", "Mixer", "Setup" }] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 20)
                                safe->showTab ("Ideas");
                            else if (result >= 10)
                                safe->showTab (names[(size_t) (result - 10)]);
                            else if (result == 1)
                                safe->showTab ("Guide");
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
                            else if (result == 8)
                                safe->saveShow();
                            else if (result == 9)
                                safe->exportForLogic();
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
        header.setBounds (area.removeFromTop (compact ? 76 : 84));
    screenBar.setBounds (area.removeFromTop (compact ? 54 : 60));
    if (! stageShown)
    {
        // Rec and the transport along the bottom (as on the phone), the song's strip over them.
        dock.setBounds (area.removeFromBottom (compact ? rigpanels::Dock::kCompactHeight : rigpanels::Dock::kHeight));
        footer.setBounds (area.removeFromBottom (footer.isSlim() ? 30 : (compact ? 112 : 128)));
    }
    if (railShown())
    {
        auto railArea = area.removeFromLeft (rigpanels::TrackRail::kWidth + 22).withTrimmedLeft (22).reduced (0, 8);
        rail.setBounds (railArea);
        area.removeFromLeft (6);
    }
    tabs.setBounds (area.reduced (0, 4));
    if (soundsOpen)
    {
        auto drawerArea = tabs.getBounds();
        soundsDrawer->setBounds (drawerArea.removeFromRight (juce::jlimit (480, 760, drawerArea.getWidth() * 60 / 100)));
        soundsDrawer->toFront (false);
    }
    if (chordsOpen)
    {
        auto drawerArea = tabs.getBounds();
        chordsDrawer->setBounds (drawerArea.removeFromBottom (juce::jmin (drawerArea.getHeight() - 120, ChordsView::kPreferredHeight)));
        chordsDrawer->toFront (false);
    }
    if (visualsView != nullptr)
    {
        // Over the screen, between the top bar and the Rec dock (they stay, so you can still play and record).
        const int top = stageShown ? screenBar.getY() : header.getBottom();
        const int bottom = stageShown ? getHeight() : dock.getY();
        visualsView->setBounds (juce::Rectangle<int>::leftTopRightBottom (kCheek, top, getWidth() - kCheek, bottom));
        visualsView->toFront (false);
    }
    if (title != nullptr)
    {
        title->setBounds (getLocalBounds());
        title->toFront (false);
    }
    if (lesson != nullptr)
    {
        // Bottom right, over the empty steps past the hats, above the song's strip.
        auto screen = tabs.getBounds();
        lesson->setBounds (screen.removeFromBottom (RigLesson::kHeight + 10).removeFromRight (RigLesson::kWidth + 10).reduced (5));
        lesson->toFront (false);
    }
    if (helpPanel != nullptr)
    {
        if (helpPanel->isTour())
        {
            // A card in the bottom right corner, so the screen it talks about shows beside it.
            auto screen = tabs.getBounds();
            helpPanel->setBounds (screen.removeFromBottom (juce::jmin (screen.getHeight() - 40, 380))
                                      .removeFromRight (juce::jmin (screen.getWidth() - 40, 560)).reduced (8));
        }
        else
            helpPanel->setBounds (area.reduced (juce::jmax (12, area.getWidth() / 10), 12));
        helpPanel->toFront (true);
    }
}

void RigView::layoutSetup (juce::Rectangle<int> page)
{
    auto& sp = *setupPage;
    auto area = page.reduced (22, 14);
    sp.titleArea = area.removeFromTop (40);
    area.removeFromTop (10);

    // Three columns of boxed sections, in three rows.
    const int gap = 14, cols = 3;
    const int colW = (area.getWidth() - gap * (cols - 1)) / cols;
    // The first row holds the most (three settings each), so it's the tallest.
    const int usable = area.getHeight() - gap * 2;
    const int rowHs[] = { juce::jmax (200, usable * 38 / 100), juce::jmax (140, usable * 31 / 100), 0 };
    const int rowH = juce::jmax (120, usable - rowHs[0] - rowHs[1]);
    auto cell = [&] (int col, int row)
    {
        const int y = area.getY() + (row > 0 ? rowHs[0] + gap : 0) + (row > 1 ? rowHs[1] + gap : 0);
        return juce::Rectangle<int> (area.getX() + col * (colW + gap), y, colW, row < 2 ? rowHs[row] : rowH);
    };
    sp.cards.clear();
    auto card = [&] (juce::Rectangle<int> box, const juce::String& title, juce::Colour colour, const juce::String& hint = {})
    {
        sp.cards.push_back ({ title, hint, colour, box, {} });
        return box.reduced (16, 0).withTrimmedTop (44).withTrimmedBottom (12);
    };
    const int rowGap = 8, labelW = 96;
    auto line = [&] (juce::Rectangle<int>& inside, juce::Label* label, std::initializer_list<std::pair<juce::Component*, int>> items)
    {
        auto row = inside.removeFromTop (32);
        inside.removeFromTop (rowGap);
        if (label != nullptr)
            label->setBounds (row.removeFromLeft (labelW));
        for (auto [c, w] : items)
        {
            c->setBounds ((w <= 0 ? row : row.removeFromLeft (juce::jmin (w, row.getWidth()))).reduced (0, 1));
            row.removeFromLeft (8);
        }
    };
    auto hintBelow = [&] (juce::Rectangle<int> inside) { sp.cards.back().hintArea = inside.withTrimmedTop (2); };

    // Keys & pads.
    auto in = card (cell (0, 0), "Keys & pads", vintage::colours::synth);
    line (in, &keysLabel, { { &keysTo, 0 } });
    line (in, &splitLabel, { { &splitNote, 84 }, { &lowerKeysTo, 0 } });
    line (in, &padsLabel, { { &padBank, 80 } });
    sp.cards.back().hint = "The keys follow the picked track unless set here. Split: the keys below the note play another track.";
    hintBelow (in);

    // Sample one tool into another.
    in = card (cell (1, 0), "Sample one tool into another", vintage::colours::sampler);
    line (in, &sourceLabel, { { &sampleSource, 0 } });
    line (in, &sp.intoLabel, { { &sampleDest, 0 } });
    line (in, &sp.lengthLabel, { { &sampleBars, 110 }, { &sampleButton, 0 } });
    sp.cards.back().hint = "Any track, the mic or the whole mix, into the Sampler or the next empty MPC pad. Starts at the next bar.";
    hintBelow (in);

    // Knobs & Learn.
    in = card (cell (2, 0), "Knobs & Learn", vintage::colours::vocoder);
    line (in, nullptr, { { &knobButton, 150 } });
    line (in, &learnPlayLabel, { { &learnPlay, 110 } });
    line (in, &learnStopLabel, { { &learnStop, 110 } });
    sp.cards.back().hint = "Knobs: put a knob of your keyboard on any setting (touch it on screen, then turn the knob).";
    hintBelow (in);

    // Click & mic.
    in = card (cell (0, 1), "Click & mic", vintage::colours::play);
    line (in, nullptr, { { &clickButton, 120 } });
    if (devices != nullptr)
        line (in, nullptr, { { &micButton, 120 } });
    line (in, nullptr, { { &timingButton, 190 } });
    sp.cards.back().hint = devices != nullptr ? "Click: a metronome on every beat while playing. Mic: hear your mic (use headphones). "
                                                "Timing: moves what you sing onto the beat."
                                              : "Click: a metronome on every beat while playing. Timing: moves what you sing onto the beat.";
    hintBelow (in);

    // Audio & MIDI.
    in = card (cell (1, 1), "Audio & MIDI", vintage::colours::arm);
    audioButton.setVisible (devices != nullptr);
    controllersButton.setVisible (devices != nullptr);
    if (devices != nullptr)
    {
        line (in, nullptr, { { &audioButton, 170 } });
        line (in, nullptr, { { &controllersButton, 200 } });
    }
    sp.cards.back().hint = devices != nullptr ? "Your audio interface and its inputs and outputs. MIDI controllers: what each keyboard or pad controller is, so it plays Kaleidobox."
                                              : "In Logic, Logic sets the audio and MIDI. Kaleidobox Input (on an audio track) brings in a mic or guitar.";
    hintBelow (in);

    // Display.
    in = card (cell (2, 1), "Display", vintage::colours::input);
    line (in, nullptr, { { &darkButton, 140 }, { &popOutButton, 170 } });
    sp.cards.back().hint = "Dark panel for dark rooms and stages. Second screen puts any screen in its own window.";
    hintBelow (in);

    // Your show.
    in = card (cell (0, 2), "Your show", vintage::colours::bass);
    line (in, nullptr, { { &saveButton, 140 }, { &openButton, 140 } });
    sp.cards.back().hint = "Everything together: sounds, patterns, mixer, the song and your loops. Cmd+S saves.";
    hintBelow (in);

    // Plugins.
    in = card (cell (1, 2), "Plugins (Arturia, Quake...)", vintage::colours::mute);
    line (in, nullptr, { { &pluginsButton, 180 } });
    pluginsLabel.setVisible (false);
    pluginsHint.setVisible (false);
    sp.cards.back().hint = "Instruments play the Bass or Synth, with their own screen in Edit (F2). "
                           "Effects go on any track: Mix (F4) > Effects > + Plugin.";
    hintBelow (in);

    // iPhone: Kaleidobox Pocket.
    in = card (cell (2, 2), "iPhone", vintage::colours::loops);
    if (phoneLink != nullptr)
    {
        auto row = in.removeFromTop (46);
        in.removeFromTop (rowGap);
        phoneCode.setBounds (row.removeFromLeft (150).reduced (0, 1));
        row.removeFromLeft (10);
        phoneUnlink.setBounds (row.removeFromLeft (juce::jmin (110, row.getWidth())).withSizeKeepingCentre (juce::jmin (110, row.getWidth()), 32));
    }
    sp.cards.back().hint = phoneHint;
    hintBelow (in);
}

void RigView::showPluginsMenu()
{
    juce::SharedResourcePointer<PluginHost> host;
    auto instruments = host->instruments();
    std::stable_partition (instruments.begin(), instruments.end(), [] (const juce::PluginDescription& d)
                           { return d.manufacturerName.containsIgnoreCase ("arturia"); });
    juce::PopupMenu menu;
    const std::pair<int, const char*> targets[] = { { GrooveView::bass, "Bass" }, { GrooveView::synth, "Synth" } };
    menu.addSectionHeader ("Instruments (Arturia...): play a track, with their own screen");
    for (int i = 0; i < 2; ++i)
    {
        auto* slot = i == 0 ? &engine.getBassPlugin() : &engine.getSynthPlugin();
        juce::PopupMenu sub;
        if (instruments.empty())
            sub.addItem (-1, host->hasScanned() ? "No instrument plugins found" : "Scan first (below)", false, false);
        else
            PluginMenu::addPlugins (sub, instruments, 10000 * (i + 1));
        if (slot->isLoaded())
        {
            sub.addSeparator();
            sub.addItem (500 + i, "Open " + slot->getName() + "'s screen (Edit)");
        }
        menu.addSubMenu (juce::String ("On the ") + targets[i].second + (slot->isLoaded() ? " (" + slot->getName() + ")" : juce::String()), sub);
    }
    menu.addSectionHeader ("Effects (Quake...): at the end of any track's effects");
    menu.addItem (600, "Pick a track's effects: Mix > Effects > + Plugin");
    menu.addSeparator();
    menu.addItem (700, host->hasScanned() ? "Scan for plugins again" : "Scan for plugins (Arturia, Quake...)");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (pluginsButton),
                        [safe = juce::Component::SafePointer<RigView> (this), instruments] (int result)
                        {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result >= 10000)
                            {
                                const int which = result / 10000 - 1, index = result % 10000;
                                if (juce::isPositiveAndBelow (index, (int) instruments.size()))
                                    safe->loadInstrumentPlugin (which == 0 ? GrooveView::bass : GrooveView::synth, instruments[(size_t) index]);
                            }
                            else if (result == 500 || result == 501)
                            {
                                const int t = result == 500 ? GrooveView::bass : GrooveView::synth;
                                safe->pickTrack (t);
                                safe->showView ("Edit");
                                if (auto* screen = dynamic_cast<AnalogScreen*> (safe->pageContent (editPageFor (t))))
                                {
                                    screen->showEngine (true);
                                    screen->getShell().showScreen (true);
                                }
                            }
                            else if (result == 600)
                            {
                                safe->showTab ("Effects");
                            }
                            else if (result == 700)
                            {
                                PluginMenu::scan ([safe] (const juce::String& t) { if (safe != nullptr) safe->say (t); },
                                                  [safe] { if (safe != nullptr) safe->showPluginsMenu(); });
                            }
                        });
}

void RigView::loadInstrumentPlugin (int t, const juce::PluginDescription& d)
{
    auto& slot = t == GrooveView::bass ? engine.getBassPlugin() : engine.getSynthPlugin();
    say ("Loading " + d.name + "...");
    slot.load (d, [safe = juce::Component::SafePointer<RigView> (this), t, &slot, name = d.name] (const juce::String& error)
               {
                   if (safe == nullptr)
                       return;
                   if (error.isNotEmpty())
                   {
                       safe->say (name + " didn't load: " + error);
                       return;
                   }
                   slot.bypassed = false;
                   safe->pickTrack (t);
                   safe->showView ("Edit");
                   if (auto* screen = dynamic_cast<AnalogScreen*> (safe->pageContent (editPageFor (t))))
                   {
                       screen->showEngine (true);
                       screen->getShell().showScreen (true);
                   }
                   safe->say (name + " plays the " + GrooveView::trackName (t) + " now, its own screen in Edit. "
                              "Its steps and patterns are on Play (F1).");
               });
}

//==============================================================================
int RigView::quantizeTarget() const
{
    const auto page = currentPage();
    if (page == "Groove" && groove != nullptr)
    {
        switch (groove->getTrack())
        {
            case GrooveView::drums:
            case GrooveView::sampler:  return RigQuantizer::mpc;
            case GrooveView::machine:  return RigQuantizer::machine;
            case GrooveView::bass:     return RigQuantizer::bass;
            case GrooveView::synth:    return RigQuantizer::synth;
            default:                   return RigQuantizer::allLoops;
        }
    }
    if (page == "Looper" && looper != nullptr)
    {
        const int layer = looper->getSelected();
        return juce::isPositiveAndBelow (layer, RigLooper::kLayers)
                && engine.getLooper().getState (layer) != RigLooper::State::empty
            ? RigQuantizer::loopTarget (layer) : (int) RigQuantizer::allLoops;
    }
    if (page == "MPC" || page == "Sampler")
        return RigQuantizer::mpc;
    if (page == "Drum machine")
        return RigQuantizer::machine;
    if (page == "Bass")
        return RigQuantizer::bass;
    if (page == "Synth")
        return RigQuantizer::synth;
    return RigQuantizer::allLoops;
}

QuantizePanel* RigView::getQuantizePanel() const
{
    return dynamic_cast<QuantizePanel*> (quantizePanel.getComponent());
}

void RigView::showQuantize (int target, juce::Component* from)
{
    if (auto* open = getQuantizePanel(); open != nullptr && open->isShowing())
    {
        // Already open: point it at this.
        open->setTarget (target);
        return;
    }
    auto panel = std::make_unique<QuantizePanel> (engine, target);
    panel->setLookAndFeel (&vintageLook);
    panel->onSay = [this] (const juce::String& text) { say (text); };
    quantizePanel = panel.get();
    auto* anchor = from != nullptr ? from : static_cast<juce::Component*> (&dock.quantize);
    const auto area = getLocalArea (anchor, anchor->getLocalBounds());
    juce::CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

juce::String RigView::quantizeNow()
{
    const int target = quantizeTarget();
    const auto text = engine.getQuantizer().apply (target);
    say (text);
    if (auto* panel = getQuantizePanel())
    {
        panel->setTarget (target);
        panel->setStatus (text);
    }
    if (groove != nullptr)
        groove->repaint();
    return text;
}

//==============================================================================
// Kaleidobox Visuals

// Full screen, on this display or another (a projector): the picture in its own window, in macOS's
// own full screen. Esc (or Visuals > Turn off) closes it.
class RigView::VisualsWindow : public juce::DocumentWindow
{
public:
    VisualsWindow (RigView& v, std::unique_ptr<crate::visuals::VisualsView> picture, juce::Rectangle<int> displayArea)
        : juce::DocumentWindow ("Kaleidobox Visuals", juce::Colours::black, juce::DocumentWindow::closeButton), view (v)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (picture.release(), false);
        setResizable (true, false);
        setBounds (displayArea.reduced (displayArea.getWidth() / 8, displayArea.getHeight() / 8));
        setVisible (true);
        setFullScreen (true);
    }
    void closeButtonPressed() override
    {
        juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RigView> (&view)] { if (sp != nullptr) sp->showVisuals (RigView::VisualsPlace::off); });
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey || k.getKeyCode() == juce::KeyPress::F8Key)
        {
            closeButtonPressed();
            return true;
        }
        return view.keyPressed (k);
    }
    crate::visuals::VisualsView* picture() { return dynamic_cast<crate::visuals::VisualsView*> (getContentComponent()); }

private:
    RigView& view;
};

void RigView::showVisuals (VisualsPlace place, int display)
{
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    if (place == VisualsPlace::display && ! juce::isPositiveAndBelow (display, displays.size()))
        place = VisualsPlace::fullScreen;
    visualsWindow.reset();
    visualsView.reset();
    visualsAt = place;
    if (place != VisualsPlace::off)
    {
        visualsLast = place;
        visualsDisplay = display;
    }

    auto make = [this] (bool controls)
    {
        crate::visuals::VisualsView::Options o;
        o.controls = controls;
        o.font = [] (float h) { return vintage::cond (h, vintage::Weight::bold, 0.06f); };
        auto v = std::make_unique<crate::visuals::VisualsView> (engine.visualTap, visualState, o);
        v->clock = [this]
        {
            auto& t = engine.getTransport();
            crate::visuals::Clock c;
            c.beats = t.getPositionInBeats();
            c.bpm = t.getBpm();
            c.beatsPerBar = t.getBeatsPerBar();
            c.playing = t.isPlaying();
            return c;
        };
        v->onClose = [this] { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<RigView> (this)] { if (sp != nullptr) sp->showVisuals (VisualsPlace::off); }); };
        v->onChanged = [this] { crate::visuals::save (visualState, crate::visuals::defaultFile()); updateVisualsButton(); };
        return v;
    };

    if (place == VisualsPlace::window)
    {
        visualsView = make (true);
        addAndMakeVisible (*visualsView);
        resized();
    }
    else if (place != VisualsPlace::off)
    {
        // Full screen: the display this window is on; a display: that one, with only the picture.
        const auto* here = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
        const auto area = place == VisualsPlace::display ? displays[display].totalArea
                        : here != nullptr ? here->totalArea : juce::Rectangle<int> (0, 0, 1280, 800);
        visualsWindow = std::make_unique<VisualsWindow> (*this, make (place == VisualsPlace::fullScreen), area);
    }
    updateVisualsButton();
}

void RigView::updateVisualsButton()
{
    using P = VisualsPlace;
    const juce::String where = visualsAt == P::off ? "OFF" : visualsAt == P::window ? "IN WINDOW" : visualsAt == P::fullScreen ? "FULL SCREEN" : "2ND SCREEN";
    header.visuals.sub = where;
    header.visuals.lit = visualsAt == P::off ? vintage::Lit::none : vintage::Lit::current;
    header.visuals.repaint();
}

void RigView::showVisualsMenu()
{
    using P = VisualsPlace;
    juce::PopupMenu m;
    m.addSectionHeader ("Visuals: your sound as a light show");
    m.addItem ("In this window", true, visualsAt == P::window, [this] { showVisuals (P::window); });
    m.addItem ("Full screen", true, visualsAt == P::fullScreen, [this] { showVisuals (P::fullScreen); });
    // A second screen or projector: only the picture, full screen there, while you play on this one.
    const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
    const auto* here = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
    for (int i = 0; i < displays.size(); ++i)
    {
        if (here != nullptr && displays[i].totalArea == here->totalArea)
            continue;
        const auto size = displays[i].totalArea;
        m.addItem ("On screen " + juce::String (i + 1) + " (" + juce::String (size.getWidth()) + " x " + juce::String (size.getHeight()) + "), picture only",
                   true, visualsAt == P::display && visualsDisplay == i, [this, i] { showVisuals (P::display, i); });
    }
    if (displays.size() < 2)
        m.addItem ("On a second screen (plug one in)", false, false, nullptr);
    m.addItem (visualsAt == P::off ? "Off" : "Turn off   (F8)", visualsAt != P::off, visualsAt == P::off, [this] { showVisuals (P::off); });

    m.addSectionHeader ("Look   (Option+1-5)");
    for (int i = 0; i < crate::visuals::kLooks; ++i)
        m.addItem (juce::String (i + 1) + "  " + crate::visuals::lookName (i), true, ! visualState.drift.load() && visualState.look.load() == i,
                   [this, i]
                   {
                       visualState.drift = false;
                       visualState.look = i;
                       crate::visuals::save (visualState, crate::visuals::defaultFile());
                       if (visualsView != nullptr) visualsView->pickLook (i);
                       if (visualsAt == P::off) showVisuals (visualsLast, visualsDisplay);
                   });
    m.addItem ("Drift: a new look every 8 bars   (Option+0)", true, visualState.drift.load(), [this]
               {
                   visualState.drift = ! visualState.drift.load();
                   crate::visuals::save (visualState, crate::visuals::defaultFile());
                   if (visualsAt == P::off) showVisuals (visualsLast, visualsDisplay);
               });

    juce::PopupMenu colours;
    for (int c = 0; c < (int) crate::visuals::Colours::count; ++c)
    {
        if (c == (int) crate::visuals::Colours::tools)
            colours.addSeparator();
        colours.addItem (crate::visuals::coloursName ((crate::visuals::Colours) c), true, visualState.colours.load() == c, [this, c]
                         {
                             visualState.colours = c;
                             crate::visuals::save (visualState, crate::visuals::defaultFile());
                         });
    }
    m.addSubMenu ("Colours", colours);
    auto levels = [this] (std::atomic<int>& value)
    {
        juce::PopupMenu sub;
        const char* names[4] { "1  Calm", "2", "3", "4  Wild" };
        for (int i = 1; i <= 4; ++i)
            sub.addItem (names[i - 1], true, value.load() == i, [this, &value, i]
                         {
                             value = i;
                             crate::visuals::save (visualState, crate::visuals::defaultFile());
                         });
        return sub;
    };
    m.addSubMenu ("Wild", levels (visualState.wild));
    m.addSubMenu ("Trails", levels (visualState.trails));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (header.visuals));
}
