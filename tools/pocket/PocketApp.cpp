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

//==============================================================================
// More (the header's ···): the sound recorder, Sounds, the Remote, the Mac link, Settings and the tour.
class PocketApp::MoreScreen : public juce::Component
{
public:
    static constexpr int kRows = 6;
    MoreScreen (Library& l, LinkClient& c, std::function<int()> loopLayerCount) : lib (l), link (c), loopLayers (std::move (loopLayerCount)) {}
    std::function<void (int)> onOpen;   // the row: 0 record a sound, 1 sounds, 2 remote, 3 mac link, 4 settings, 5 the tour

    void paint (juce::Graphics& g) override
    {
        static const char* titles[kRows] = { "RECORD A SOUND", "SOUNDS", "REMOTE", "MAC LINK", "SETTINGS", "TAKE THE TOUR" };
        const int n = (int) lib.items().size();
        const bool linked = link.isLinked();
        const auto mac = link.getMacName();
        const juce::String blurbs[kRows] = {
            U8("The sound recorder: catch a sample, trim it, add effects, chop it onto the pads"),
            U8("Everything you've recorded, imported or got from the Mac · ") + juce::String (n) + (n == 1 ? " sound" : " sounds"),
            linked ? "Run Kaleidobox on " + mac + U8(": play, sections, mutes, loops") : U8("Run the Rig on the Mac: play, sections, mutes, loops · link first"),
            linked ? U8("Linked to ") + mac + U8(" · Kaleidobox open")
                   : link.getStatus() == LinkClient::Status::connecting ? U8("Linking to ") + mac + U8("…")
                                                                        : U8("Not linked · link to your Mac on this Wi-Fi"),
            U8("Rec setup (count-in, take length) and the click"),
            U8("Seven stops round the tools · two minutes") };
        for (int i = 0; i < kRows; ++i)
        {
            auto r = rowBounds (i);
            drawRaised (g, r, Look::cream, false, 16.0f);
            auto icon = juce::Rectangle<float> (16, r.getY() + (r.getHeight() - 3 - 50) * 0.5f, 50, 50);
            g.setColour (col::well);
            g.fillRoundedRectangle (icon, 12.0f);
            drawIcon (g, i, icon.getCentre());
            g.setColour (col::ink);
            g.setFont (spaced (condensed (20.0f), 0.08f));
            g.drawText (titles[i], juce::Rectangle<float> (80, r.getY() + 8, r.getWidth() - 130, 24), juce::Justification::centredLeft);
            g.setColour (col::inkDim);
            g.setFont (small (13.5f));
            g.drawFittedText (blurbs[i], juce::Rectangle<int> (80, (int) r.getY() + 32, (int) r.getWidth() - 130, 38), juce::Justification::topLeft, 2, 0.9f);
            if (i == 3)
            {
                g.setColour (linked ? col::green : link.getStatus() == LinkClient::Status::connecting ? col::amber : col::grey);
                g.fillEllipse (r.getRight() - 52, r.getCentreY() - 5, 10, 10);
            }
            g.setColour (col::ink);
            g.setFont (condensed (22.0f));
            g.drawText (">", juce::Rectangle<float> (r.getRight() - 32, r.getY(), 24, r.getHeight() - 3), juce::Justification::centred);
        }

        // what's on the phone
        auto box = summaryBounds();
        g.setColour (col::well);
        g.fillRoundedRectangle (box, 16.0f);
        g.setColour (col::inkFaint);
        g.setFont (spaced (condensedBold (12.0f), 0.14f));
        g.drawText ("ON THIS IPHONE", box.withTrimmedLeft (16).withHeight (28), juce::Justification::bottomLeft);
        g.setColour (juce::Colour (0xfff1e7d6));
        g.setFont (small (16.0f));
        g.drawFittedText (juce::String ((int) lib.items().size()) + U8(" sounds · ") + juce::String (loopLayers()) + (loopLayers() == 1 ? " loop layer" : " loop layers") + U8(" in this song · ") + megabytes(),
                          box.reduced (16, 0).withTrimmedTop (30).toNearestInt(), juce::Justification::topLeft, 2);
    }

    void resized() override {}

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < kRows; ++i)
            if (rowBounds (i).contains (e.position) && onOpen)
                onOpen (i);
    }

private:
    float pitch() const { return juce::jlimit (70.0f, 96.0f, ((float) getHeight() - 96.0f - 8.0f) / (float) kRows); }
    juce::Rectangle<float> rowBounds (int i) const { return juce::Rectangle<float> (16, 8 + (float) i * pitch(), (float) getWidth() - 32, pitch() - 8); }
    juce::Rectangle<float> summaryBounds() const { return juce::Rectangle<float> (16, (float) getHeight() - 88, (float) getWidth() - 32, 80); }

    juce::String megabytes() const
    {
        juce::int64 bytes = 0;
        for (auto* folder : { "Sounds", "Ideas" })   // every sound, and every idea's loops
            for (auto& f : lib.getRoot().getChildFile (folder).findChildFiles (juce::File::findFiles, true))
                bytes += f.getSize();
        return juce::String (juce::roundToInt ((double) bytes / 1048576.0)) + " MB";
    }

    static void drawIcon (juce::Graphics& g, int i, juce::Point<float> c)
    {
        g.setColour (col::amber);
        if (i == 0)
        {
            g.setColour (col::red);
            g.fillEllipse (c.x - 9, c.y - 9, 18, 18);
            g.setColour (col::amber);
            g.drawEllipse (c.x - 13, c.y - 13, 26, 26, 2.2f);
        }
        else if (i == 1)
        {
            const float hs[] = { 8, 18, 28, 16, 22, 10 };
            for (int k = 0; k < 6; ++k)
                g.fillRoundedRectangle (c.x - 13 + (float) k * 5.0f, c.y - hs[k] * 0.5f, 3.0f, hs[k], 1.4f);
        }
        else if (i == 2)
        {
            g.drawRoundedRectangle (c.x - 14, c.y - 12, 28, 19, 3.0f, 2.4f);
            g.drawLine (c.x - 7, c.y + 13, c.x + 7, c.y + 13, 2.4f);
            juce::Path p;
            p.addTriangle (c.x - 3, c.y - 7, c.x - 3, c.y + 3, c.x + 5, c.y - 2);
            g.fillPath (p);
        }
        else if (i == 3)
        {
            g.drawRoundedRectangle (c.x - 15, c.y - 5, 17, 10, 5.0f, 2.6f);
            g.drawRoundedRectangle (c.x - 2, c.y - 5, 17, 10, 5.0f, 2.6f);
        }
        else if (i == 5)
        {
            for (int k = 0; k < 6; ++k)
            {
                juce::Path petal;
                petal.addRoundedRectangle (-3.0f, -15.0f, 6.0f, 15.0f, 3.0f);
                petal.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * (float) k / 6.0f).translated (c));
                g.fillPath (petal);
            }
        }
        else
        {
            g.drawEllipse (c.x - 7, c.y - 7, 14, 14, 2.6f);
            for (int k = 0; k < 8; ++k)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) k / 8.0f;
                g.drawLine (c.x + std::sin (a) * 10, c.y - std::cos (a) * 10, c.x + std::sin (a) * 14, c.y - std::cos (a) * 14, 2.6f);
            }
        }
    }

    Library& lib;
    LinkClient& link;
    std::function<int()> loopLayers;
};

// Settings: the Rec setup (the same sheet as the header's display) and the click.
class PocketApp::SettingsScreen : public juce::Component
{
public:
    SettingsScreen (AppSettings& s, std::function<void()> back, std::function<void()> recSetup, std::function<void()> clickChanged)
        : settings (s), onRecSetup (std::move (recSetup)), onClick (std::move (clickChanged))
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
    PButton backButton { "< More" };
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
        setVisible (true);
        toFront (false);
        repaint();
        startTimer (2600);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1);
        g.setColour (juce::Colour (0xf0201c18));
        g.fillRoundedRectangle (r, 14.0f);
        g.setColour (juce::Colour (0xfff1e7d6));
        g.setFont (small (15.0f));
        g.drawFittedText (text, r.reduced (14, 4).toNearestInt(), juce::Justification::centred, 2);
    }

private:
    void timerCallback() override { stopTimer(); setVisible (false); }
    juce::String text;
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
      singScreen (static_cast<PocketHost&> (*this), loopsScreen, vocoderScreen.engine(), beats, appSettings),
      padSheet (beats.kit()),
      record (static_cast<PocketHost&> (*this), audio, lib, sheet),
      sounds (static_cast<PocketHost&> (*this), audio, lib)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    juce::Desktop::getInstance().addGlobalMouseListener (&keyboardDismisser);

    auto backToMore = [this] { showPage (Page::more, 0); };
    remote = std::make_unique<RemoteScreen> (link);
    remote->onLink = [this] { openMacLink(); };
    remote->onSay = [this] (const juce::String& t) { toast (t); };
    macLink = std::make_unique<MacLinkScreen> (link);
    macLink->onClose = backToMore;
    macLink->onRemote = [this] { showPage (Page::more, 2); };
    macLink->onSay = [this] (const juce::String& t) { toast (t); };
    sendSheet = std::make_unique<SendToMacSheet> (link);
    sendSheet->onClose = [this] { closeSendSheet(); };
    sendSheet->onLink = [this] { closeSendSheet(); openMacLink(); };
    sendSheet->onSay = [this] (const juce::String& t) { toast (t); };
    link.onChange = [this] { linkChanged(); };
    link.onSay = [this] (const juce::String& t) { toast (t); };
    settings = std::make_unique<SettingsScreen> (appSettings, backToMore, [this] { openRecSetup(); }, [this] { applyClick(); thumb.repaint(); });
    more = std::make_unique<MoreScreen> (lib, link, [this] { return loopsScreen.layerCount(); });
    more->onOpen = [this] (int row)
    {
        if (row == 0) openRecorder();
        else if (row == 5) startTour();
        else showPage (Page::more, row);
    };
    ideasScreen = std::make_unique<IdeasScreen> (ideas);
    ideasScreen->onOpen = [this] (const juce::String& id) { openIdea (id); };
    ideasScreen->onNew = [this] { newIdea(); };
    ideasScreen->onNewBeat = [this] { newSongWithBeat(); };
    ideasScreen->onNewKit = [this] { newSongWithKit(); };
    ideasScreen->onPlay = [this] (const juce::String& id) { playIdea (id); };
    ideasScreen->isPlaying = [this] (const juce::String& id) { return id == ideas.current().id && beats.engine().clockRunning(); };
    ideasScreen->onRename = [this] (const juce::String& id) { renameIdea (id); };
    ideasScreen->onDelete = [this] (const juce::String& id) { deleteIdea (id); };
    title = std::make_unique<TitleScreen>();
    title->micLevel = [this] { return audio.takeLevel(); };   // a meter only: the mic is never heard here
    title->onStart = [this] { showTitle (false); };
    title->onTour = [this] { showTitle (false); startTour(); };
    tour = std::make_unique<TourOverlay>();
    tour->show = [this] (int stop) { return tourStop (stop); };
    tour->onFinish = [this] { endTour(); };
    toastBox = std::make_unique<Toast>();
    dim = std::make_unique<Dim>();
    dim->onTap = [this] { closeAllSheets(); };

    // The frame: the thumb bar, the tool row (stage 1: the tools that exist) and the count-in panel.
    toolRow.tools = { Tool::loops, Tool::drums, Tool::pads, Tool::vocoder };
    toolRow.onSelect = [this] (Tool t) { showTool (t); };
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
    recSetup.onDone = [this] { closeRecSetup(); };
    headphones.route = [this] { return route; };
    headphones.onHeadphones = [this] { chooseHeadphones (true); };
    headphones.onNoHeadphones = [this] { chooseHeadphones (false); };

    soundBank.onLoad = [this] (int k) { loadKit (k); };
    soundBank.onDone = [this] { closeSoundBank(); };
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
                toast ("Imported " + juce::String (ok) + (ok == 1 ? " sound" : " sounds") + U8(". They're in ··· › Sounds: TO PAD puts one on a pad."));
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
    loopsScreen.onSourceChanged = [this] { applyMic(); };
    loopsScreen.engine().setCountInBars (appSettings.countInBars);
    headerTick.startTimerHz (15);

    padSheet.onChanged = [this] (int pad) { beats.padChanged (pad); };
    padSheet.onDone = [this] { closePadSheet(); };
    padSheet.onClear = [this] (int) { beats.padsChanged(); };
    padSheet.onUndoClear = [this] (int) { beats.padsChanged(); };
    padSheet.onTrim = [this] (int pad)
    {
        auto id = beats.kit().pads[(size_t) pad].soundId;
        if (lib.find (id) == nullptr)
            return toast ("That sound is gone from Sounds");
        closePadSheet();
        showPage (Page::recorder);
        record.editForPad (pad, id);
    };
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
    for (auto* c : std::initializer_list<juce::Component*> { &beats, &loopsScreen, &vocoderScreen, &singScreen, remote.get(), macLink.get(), settings.get(), more.get() })
        addChildComponent (c);
    addChildComponent (countPanel);
    addChildComponent (thumb);
    addChildComponent (toolRow);
    addChildComponent (*dim);
    addChildComponent (sheet);
    addChildComponent (padSheet);
    addChildComponent (tempoSheet);
    addChildComponent (recSetup);
    addChildComponent (headphones);
    addChildComponent (soundBank);
    addChildComponent (*sendSheet);
    addChildComponent (*toastBox);
    addChildComponent (*tour);
    addChildComponent (*title);

    setSize (390, 844);
    if (o.audio)
        fillEmptyIdea();   // the first launch: the pads start with the Street kit
    if (o.audio)
        title->setVisible (true);   // the opening title, each time the app opens
    sounds.selectFirst();
    animate = o.audio;

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
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PocketApp::toast (const juce::String& t)
{
    toastBox->show (t);
}

//==============================================================================
// Pages and tools
void PocketApp::showPage (Page p, int morePage)
{
    if (sheetOpen)
        closeSheet();
    closePadSheet();
    closeSendSheet();
    closeHeadphones();
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
    vocoderScreen.setVisible (p == Page::vocoderSettings);
    showMorePage (p == Page::more ? morePage : -1);

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

void PocketApp::refreshToolRow()
{
    toolRow.selected = -1;
    if (page == Page::tool || page == Page::vocoderSettings)
        for (int i = 0; i < (int) toolRow.tools.size(); ++i)
            if (toolRow.tools[(size_t) i] == (page == Page::vocoderSettings ? Tool::vocoder : tool))
                toolRow.selected = i;
    toolRow.repaint();
}

// page: -1 = not on a More page
void PocketApp::showMorePage (int p)
{
    morePageShown = p;
    more->setVisible (p == 0);
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
        s = tool == Tool::loops ? stepSong : tool == Tool::vocoder ? stepSing : tool == Tool::drums ? stepBeat
          : beats.drums().padPattern.hits.empty() ? stepPads : stepBeat;
    if (s >= 0)
        ideas.reachedStep (s);
}

void PocketApp::stopEverything()
{
    lockWatch.stopTimer();
    lockKind = LockKind::none;
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
    closeSheet();
    closePadSheet();
    closeSendSheet();
    closeTempo();
    closeSoundBank();
    closeRecSetup();
    closeHeadphones();
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
        toast (juce::String (n) + (n == 1 ? " sound" : " sounds") + U8(" came in from Drop Sounds Here. They're in More › Sounds."));
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
    auto& kit = beats.kit();
    int n = 0;
    for (int p = 0; p < kPads; ++p)
    {
        auto old = kit.pads[(size_t) p];
        if (old.empty() || old.soundId != soundId)
            continue;
        if (! kit.assign (p, soundId, old.from, old.to, old.name))
            continue;
        auto& now = kit.pads[(size_t) p];
        now.volume = old.volume;
        now.pan = old.pan;
        now.tune = old.tune;
        now.mode = old.mode;
        ++n;
    }
    beats.padsChanged();
    if (n > 0)
        toast (n == 1 ? juce::String ("The pad has the new sound") : juce::String (n) + " pads have the new sound");
}

void PocketApp::backToPads (int pad)
{
    record.endPadMode();
    showTool (Tool::pads);
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
    if (sheetOpen)
        closeSheet();
    closePadSheet();
    closeSendSheet();
    audio.stopPlaying();
    beats.leave();
    loopsScreen.leave();
    beats.flush();
    loopsScreen.flush();
    ideas.setSummary (beats.summary() + (loopsScreen.layerCount() > 0 ? U8(" · ") + juce::String (loopsScreen.layerCount()) + (loopsScreen.layerCount() == 1 ? " loop" : " loops") : juce::String()));
    record.endPadMode();
    ideas.open (id);
    beats.reloadIdea (ideas.current().name);
    loopsScreen.reloadIdea();
    fillEmptyIdea();
}

void PocketApp::openIdea (const juce::String& id)
{
    switchIdea (id);
    // A song with layers opens on them; a fresh one on the pads (a kit is loaded, so they play at once).
    showTool (loopsScreen.layerCount() > 0 ? Tool::loops : Tool::pads);
}

void PocketApp::newIdea()
{
    switchIdea (ideas.create (ideas.nextName()));
    showPage (Page::recorder);
    record.startRecording();
    toast (ideas.current().name + U8(": recording a sound. Tap the red button again to stop."));
}

void PocketApp::renameIdea (const juce::String& id)
{
    auto* info = ideas.find (id);
    if (info == nullptr)
        return;
    auto* w = new juce::AlertWindow ("Name this idea", {}, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", info->name);
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, id] (int result)
    {
        if (result != 1)
            return;
        ideas.rename (id, w->getTextEditorContents ("name").substring (0, 60));
        ideasScreen->refresh();
        repaint();
    }), true);
}

void PocketApp::deleteIdea (const juce::String& id)
{
    auto* info = ideas.find (id);
    if (info == nullptr)
        return;
    auto* w = new juce::AlertWindow ("Delete " + info->name + "?", "Its pads, beat and loops are deleted. The sounds stay in Sounds.", juce::MessageBoxIconType::NoIcon, this);
    w->addButton ("Delete", 1);
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, id] (int result)
    {
        if (result != 1)
            return;
        if (! ideas.remove (id))
            return toast ("Open another idea before deleting this one");
        ideasScreen->refresh();
        toast ("Idea deleted");
    }), true);
}

void PocketApp::newSongWithBeat()
{
    switchIdea (ideas.create (ideas.nextName()));
    beats.fillStarterBeat();
    showTool (Tool::pads);
    togglePlay();
    toast ("A ready beat on the Street kit. Play the pads over it, or press REC to add your own hits.");
}

void PocketApp::newSongWithKit()
{
    switchIdea (ideas.create (ideas.nextName()));
    showTool (Tool::pads);
    openSoundBank();
    toast ("Pick a kit to start from. The Street kit is on the pads already.");
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
void PocketApp::togglePlay()
{
    auto& b = beats.engine();
    auto& l = loopsScreen.engine();
    if (b.clockRunning())
    {
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        beats.setRecordingPads (false);
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
    beats.engine().setClick (appSettings.click || beats.recordingPads());
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
    const int h = 336 + insets.getBottom();
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
                                            || (tool == Tool::loops && loopsScreen.source() == (int) LoopSource::mic));
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
        case ThumbBar::undo: loopsScreen.undo(); break;
        default: break;
    }
    thumb.repaint();
    headerTickNow();
}

bool PocketApp::recBusy() const
{
    return lockKind == LockKind::pads || loopsScreen.busyLayer() >= 0 || beats.recordingPads();
}

void PocketApp::recPressed()
{
    if (recBusy())
        return stopRec();
    if (page != Page::tool)
        return;
    const bool needsMic = tool == Tool::vocoder
                       || (tool == Tool::loops && (loopsScreen.source() == (int) LoopSource::mic || loopsScreen.source() == (int) LoopSource::vocoder));
    if (needsMic && ! micOn)
        return askForMic ([this] { recPressed(); });
    switch (tool)
    {
        case Tool::loops: recLoops(); break;
        case Tool::drums: recDrums(); break;
        case Tool::pads: recPads(); break;
        case Tool::vocoder: recVocoder(); break;
        default: break;
    }
    applyClick();
    headerTickNow();
}

void PocketApp::stopRec()
{
    if (lockKind == LockKind::pads && ! lockArmed)
    {
        // Still playing hits into the pattern: stop there. What's in the pattern stays.
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        beats.setRecordingPads (false);
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
    // What the drum machine plays, from a bar line (every bar is the drum pattern's start). Once the layer
    // plays, the live drum machine stops so the two don't double.
    auto& b = beats.engine();
    const int layer = loopsScreen.nextLayer();
    if (layer < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    if (! loopsScreen.armLayer (layer, (int) LoopSource::drums, appSettings.recBars()))
        return;
    if (! b.isPlaying())
        b.setPlaying (true);
    lockKind = LockKind::drums;
    lockLayer = layer;
    lockArmed = true;
    lockWatch.startTimer (20);
}

void PocketApp::recPads()
{
    // Pads: hits you play go into the pattern (snapped), and when it has gone round it is locked onto the
    // next layer from its start, so it keeps looping. A pattern that's already there just locks.
    auto& b = beats.engine();
    if (loopsScreen.nextLayer() < 0)
        return toast ("All 8 layers are used. Clear one in LOOPS to record.");
    auto& p = beats.drums().padPattern;
    const bool empty = p.hits.empty();
    if (const int bars = appSettings.recBars(); empty && bars > 0 && bars != p.bars)
        beats.setPatternBars (bars);
    lockLen = p.lengthBeats();
    lockBars = p.bars;
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
    if (empty)
        toast ("Play the pads " + juce::String (running ? "now" : "after the count-in") + ": " + lengthLabel (lockBars).toLowerCase()
               + ". Then it goes on layer " + juce::String (loopsScreen.nextLayer() + 1) + " and keeps looping.");
    else
        toast ("The pattern goes on layer " + juce::String (loopsScreen.nextLayer() + 1) + U8(" from its next start. Hits you play before then go in too."));
}

void PocketApp::lockWatchTick()
{
    auto& b = beats.engine();
    auto& loops = loopsScreen.engine();
    auto cancel = [this] (const juce::String& why)
    {
        lockWatch.stopTimer();
        lockKind = LockKind::none;
        beats.setRecordingPads (false);
        applyClick();
        if (why.isNotEmpty())
            toast (why);
    };
    if (lockKind == LockKind::none)
        return cancel ({});

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
            if (! loopsScreen.armLayer (layer, (int) LoopSource::drums, lockBars))
                return cancel ("Couldn't arm layer " + juce::String (layer + 1));
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
    if (st == pocket::LoopState::playing)
    {
        lockWatch.stopTimer();
        const bool pads = lockKind == LockKind::pads;
        lockKind = LockKind::none;
        loops.setTransport (true);     // the loop keeps the clock going once the pattern stops
        b.setPatternPlaying (false);
        b.setPlaying (false);
        applyClick();
        beats.refreshPads();
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
    const bool padsPass = lockKind == LockKind::pads && ! lockArmed;
    const int layer = busy >= 0 ? busy : (padsPass ? loopsScreen.nextLayer() : loopsScreen.nextLayer());
    int bars = appSettings.recBars();
    if (busy >= 0)
        bars = loops.recordingBars (busy);
    else if (page == Page::tool && tool == Tool::pads)
        bars = beats.drums().padPattern.hits.empty() && appSettings.recBars() > 0 ? appSettings.recBars() : beats.drums().padPattern.bars;
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
        if (padsPass)
            v.after = U8("THEN PLAY THE PADS \xc2\xb7 ") + lengthLabel (lockBars) + U8(" \xc2\xb7 THEN LAYER ") + juce::String (layer + 1);
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
        v.left = "PADS " + barText (std::fmod (juce::jmax (0.0, beat), lockLen));
    }
    else if (busy >= 0)
    {
        v.kind = RecView::waiting;
        v.left = "WAIT " + barText (beat);
    }
    else
        v.left = "BAR " + (running ? barText (beat) : juce::String ("1.1"));
    return v;
}

void PocketApp::headerTickNow()
{
    const auto v = recView();
    const auto text = v.left + "|" + v.right + "|" + juce::String ((int) v.kind);
    if (text != shownHeader)
    {
        shownHeader = text;
        repaint (lcdRect().expanded (2).toNearestInt());
    }
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
    more->repaint();
    macLink->refresh();
    remote->refresh();
    sendSheet->refresh();
}

bool PocketApp::remoteShowing() const
{
    return remote != nullptr && remote->isVisible();
}

// The header, 44 high: ‹ the song · the display (bar › layer · length) · the Mac link dot · ···
juce::Rectangle<float> PocketApp::moreRect() const
{
    return { (float) getWidth() - 50.0f, (float) insets.getTop() + 1.0f, 44.0f, 42.0f };
}

juce::Rectangle<float> PocketApp::chipRect() const
{
    const auto m = moreRect();
    if (page == Page::songs)
        return { m.getX() - 144.0f, (float) insets.getTop() + 4.0f, 140.0f, 36.0f };   // with the Mac's name
    return { m.getX() - 30.0f, (float) insets.getTop() + 1.0f, 30.0f, 42.0f };         // just the dot
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

void PocketApp::mouseUp (const juce::MouseEvent& e)
{
    if (e.position.y > (float) insets.getTop() + 46.0f)
        return;
    if (moreRect().contains (e.position))
        return showPage (Page::more, 0);
    if (chipRect().expanded (2).contains (e.position))
        return openMacLink();
    if (page == Page::songs)
        return;
    if (thumbShown() && lcdRect().expanded (2).contains (e.position))
        return openRecSetup();
    if (backRect().contains (e.position))
        return showIdeasList();
}

void PocketApp::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);

    const int top = insets.getTop();
    const bool dark = remoteShowing();
    const auto paper = juce::Colour (0xfff1e7d6);
    if (dark)
        g.fillAll (juce::Colour (0xff1f1b17));
    if (page != Page::songs)
    {
        // the song's name (tap it, or the arrow, for Your songs)
        auto back = backRect();
        g.setColour (dark ? paper : col::ink);
        juce::Path arrow;
        arrow.startNewSubPath (back.getX() + 15, back.getCentreY() - 8);
        arrow.lineTo (back.getX() + 8, back.getCentreY());
        arrow.lineTo (back.getX() + 15, back.getCentreY() + 8);
        g.strokePath (arrow, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (condensedBold (22.0f));
        g.drawFittedText (ideas.current().name, back.withTrimmedLeft (24).toNearestInt(), juce::Justification::centredLeft, 1, 0.75f);
    }
    else
    {
        g.setColour (dark ? paper : col::ink);
        g.setFont (fraunces (30.0f));
        g.drawText ("Kaleidobox", 16, top + 2, 200, 40, juce::Justification::centredLeft);
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

    // ···: More (the sound recorder, Sounds, the Remote, the Mac link, Settings, the tour)
    {
        auto m = moreRect().reduced (2, 4);
        if (page == Page::more && morePageShown == 0)
        {
            g.setColour (dark ? juce::Colour (0xff332c26) : col::creamDeep);
            g.fillRoundedRectangle (m, 10.0f);
        }
        g.setColour (dark ? paper : col::ink);
        for (int k = -1; k <= 1; ++k)
            g.fillEllipse (m.getCentreX() + (float) k * 9.0f - 2.8f, m.getCentreY() - 2.8f, 5.6f, 5.6f);
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
    return { 47, 0, 34, 0 };   // pretend to be an iPhone with a notch
   #endif
}

void PocketApp::resized()
{
    if (more == nullptr || toastBox == nullptr || dim == nullptr)
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
    for (auto* c : std::initializer_list<juce::Component*> { &record, ideasScreen.get(), &sounds, &beats, &loopsScreen, &vocoderScreen, &singScreen, remote.get(), macLink.get(), settings.get(), more.get() })
        c->setBounds (content);
    countPanel.setBounds (16, content.getY() + 8, w - 32, 136);
    sendSheet->setBounds (sendOpen ? sendSheetBounds() : sendSheetBounds().withY (h));

    dim->setBounds (0, 0, w, h);
    layoutSheet (sheetOpen);
    padSheet.setBounds (padSheetOpen ? padSheetBounds() : padSheetBounds().withY (h));
    tempoSheet.setBounds (tempoOpen ? tempoBounds() : tempoBounds().withY (h));
    recSetup.setBounds (recSetupOpen ? recSetupBounds() : recSetupBounds().withY (h));
    headphones.setBounds (phonesOpen ? headphonesBounds() : headphonesBounds().withY (h));
    soundBank.setBounds (bankOpen ? tempoBounds().withTop (h - 560 - insets.getBottom()) : tempoBounds().withTop (h - 560 - insets.getBottom()).withY (h));
    toastBox->setBounds (24, content.getBottom() - 60, w - 48, 52);
    title->setBounds (0, 0, w, h);
    tour->setBounds (0, 0, w, h);
}

//==============================================================================
// The opening title and the tour
void PocketApp::showTitle (bool shown)
{
    title->setVisible (shown);
    if (shown)
        title->toFront (false);
}

void PocketApp::startTour()
{
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
    toast ("That's the tour. Open a song, pick a tool, press REC.");
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
            showTool (Tool::pads, false);
            return getLocalArea (&thumb, thumb.buttonRect (ThumbBar::bpm).getUnion (thumb.buttonRect (ThumbBar::click)).toNearestInt());
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
            return getLocalArea (&loopsScreen, loopsScreen.tourSpot (1));
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

    // Your songs, like Voice Memos
    {
        const auto now = juce::Time::getCurrentTime();
        const auto day = juce::RelativeTime::days (1);
        ideas.rename (ideas.current().id, "Porch rain groove");
        ideas.setDemo (ideas.current().id, stepSing, false, U8("92 BPM · 6 pads · 3 loops"), now - juce::RelativeTime::minutes (20));
        ideas.setDemo (ideas.create ("Train horn chop"), stepBeat, false, U8("92 BPM · 4 pads · beat recorded"), now - day);
        ideas.setDemo (ideas.create ("Humming in the car"), stepRecord, false, U8("0:38 recording"), now - juce::RelativeTime::days (3));
        ideas.setDemo (ideas.create ("Late night vocoder"), stepSend, true, U8("84 BPM · 5 loops"), now - juce::RelativeTime::days (4));
    }
    showIdeasList();
    savePng (folder.getChildFile ("00-songs.png"));

    // The sound recorder (from PADS > RECORD A SOUND, or ··· > Record a sound) and its Effects + Chop sheet
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

    // LOOPS: layers 1-2 playing, 3 recording, 4 waiting for the next bar, 5 muted, 6-8 empty
    loopsScreen.showDemo();
    showTool (Tool::loops);
    savePng (folder.getChildFile ("03-tool-loops.png"));
    openTempo();
    savePng (folder.getChildFile ("03b-tempo.png"));
    closeTempo();
    openRecSetup();
    savePng (folder.getChildFile ("03c-rec-setup.png"));
    closeRecSetup();
    loopsScreen.showEditorDemo();
    savePng (folder.getChildFile ("03d-loop-editor.png"));
    loopsScreen.leave();

    // DRUMS: the drum machine with a beat in it
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
    beats.setDemoStep (-1);

    // PADS: a demo kit, then a pattern, the pad sheet, the sound bank and the pad's sample page
    beats.fillDemoKit();
    showTool (Tool::pads);
    savePng (folder.getChildFile ("05-tool-pads.png"));
    beats.fillDemoPattern();
    savePng (folder.getChildFile ("05b-pads-pattern.png"));
    openPadSheet (4);
    resized();
    savePng (folder.getChildFile ("05c-pad-sheet.png"));
    closePadSheet();
    openSoundBank();
    savePng (folder.getChildFile ("05d-sound-bank.png"));
    closeSoundBank();
    padSheet.onTrim (4);   // SAMPLE PAGE: the pad's sound in the sound recorder
    savePng (folder.getChildFile ("05e-pad-sample-page.png"));
    record.endPadMode();

    // VOCODER: the mic off, the headphones sheet (wired, Bluetooth, none), then the banner
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
    pocket::setDemoRoute ((int) pocket::Route::wired);
    routeChanged();
    micHear = true;
    applyMic();
    vocoderScreen.showDemo();
    showPage (Page::vocoderSettings);
    savePng (folder.getChildFile ("06g-vocoder-settings.png"));

    // More (···) and its pages
    showPage (Page::more, 0);
    savePng (folder.getChildFile ("08-more.png"));
    showPage (Page::more, 4);
    savePng (folder.getChildFile ("10-settings.png"));
    showPage (Page::more, 1);
    savePng (folder.getChildFile ("09-sounds.png"));
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

    // Ideas: each keeps its own pads and beat
    {
        const auto first = ideas.current().id;
        const int pads = beats.kit().usedPads();
        const auto hits = beats.drums().padPattern.hits.size();
        const auto check = ideas.create ("Self-check idea");
        switchIdea (check);
        const int emptyPads = beats.kit().usedPads();
        const auto emptyHits = beats.drums().padPattern.hits.size();
        switchIdea (first);
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

    juce::String vocoderReport;
    if (const auto why = VocoderEngine::selfCheck (vocoderReport); why.isNotEmpty())
        problems.add ("Vocoder: " + why);

    // One REC on every tool, the count-in and the mic gate, through the app itself
    juce::String recReport;
    if (const auto why = runRecChecks (recReport); why.isNotEmpty())
        problems.add (why);

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

    auto note = folder.getChildFile ("selfcheck.txt");
    note.replaceWithText ((problems.isEmpty() ? juce::String ("OK: ideas keep their own pads and beat, a new idea gets the Street kit, TO PAD, the MPC export round trip, the pad pattern, the drum machine and the pads make sound, loops record, line up and play with no gap, the vocoder makes sound that follows the voice, REC on LOOPS, DRUMS, PADS and VOCODER lands a take of the right length on the next layer, the count-in click is heard with CLICK off, and the mic is silent in the speaker until it's allowed\n")
                                              : "FAILED\n" + problems.joinIntoString ("\n") + "\n")
                          + "\nLoops self-check (97 BPM, 44.1 kHz):\n" + loopsReport
                          + "\nVocoder self-check (44.1 kHz):\n" + vocoderReport
                          + "\nREC, count-in and mic checks (97 BPM, 44.1 kHz):\n" + recReport);
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

    // DRUMS: what the drum machine plays; afterwards the live drum machine stops (no doubling)
    showTool (Tool::drums, false);
    want = loopsScreen.nextLayer();
    recPressed();
    if (want != 1 || ! waitPlaying (1, 6))
        problems.add ("REC on DRUMS: no take landed on layer 2");
    else
    {
        checkTake ("DRUMS", 1, 8.0, 4.0);
        if (b.isPlaying())
            problems.add ("REC on DRUMS: the live drum machine kept playing over its own layer");
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
    return problems.joinIntoString ("\n");
}
