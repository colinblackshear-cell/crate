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
class PocketApp::MoreScreen : public juce::Component
{
public:
    MoreScreen (Library& l, LinkClient& c, std::function<int()> loopLayerCount) : lib (l), link (c), loopLayers (std::move (loopLayerCount)) {}
    std::function<void (int)> onOpen;

    void paint (juce::Graphics& g) override
    {
        static const char* titles[] = { "SOUNDS", "REMOTE", "MAC LINK", "SETTINGS" };
        const int n = (int) lib.items().size();
        const bool linked = link.isLinked();
        const auto mac = link.getMacName();
        const juce::String blurbs[] = {
            U8("Everything you've recorded, imported or got from the Mac · ") + juce::String (n) + (n == 1 ? " sound" : " sounds"),
            linked ? "Run Kaleidobox on " + mac + U8(": play, sections, mutes, loops") : U8("Run the Rig on the Mac: play, sections, mutes, loops · link first"),
            linked ? U8("Linked to ") + mac + U8(" · Kaleidobox open")
                   : link.getStatus() == LinkClient::Status::connecting ? U8("Linking to ") + mac + U8("…")
                                                                        : U8("Not linked · link to your Mac on this Wi-Fi"),
            U8("Loop count-in · mic, tempo and click come later") };
        for (int i = 0; i < 4; ++i)
        {
            auto r = rowBounds (i);
            drawRaised (g, r, Look::cream, false, 16.0f);
            auto icon = juce::Rectangle<float> (16, r.getY() + 14, 52, 52);
            g.setColour (col::well);
            g.fillRoundedRectangle (icon, 12.0f);
            drawIcon (g, i, icon.getCentre());
            g.setColour (col::ink);
            g.setFont (spaced (condensed (21.0f), 0.08f));
            g.drawText (titles[i], juce::Rectangle<float> (84, r.getY() + 10, r.getWidth() - 140, 26), juce::Justification::centredLeft);
            g.setColour (col::inkDim);
            g.setFont (small (14.0f));
            g.drawFittedText (blurbs[i], juce::Rectangle<int> (84, (int) r.getY() + 36, (int) r.getWidth() - 140, 44), juce::Justification::topLeft, 2);
            if (i == 2)
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
        g.drawText ("ON THIS IPHONE", box.withTrimmedLeft (16).withHeight (30), juce::Justification::bottomLeft);
        g.setColour (juce::Colour (0xfff1e7d6));
        g.setFont (small (17.0f));
        g.drawFittedText (juce::String ((int) lib.items().size()) + U8(" sounds · 0 kits · 0 drum patterns · ") + juce::String (loopLayers()) + (loopLayers() == 1 ? " loop layer" : " loop layers") + U8(" · ") + megabytes(),
                          box.reduced (16, 0).withTrimmedTop (32).toNearestInt(), juce::Justification::topLeft, 2);
    }

    void resized() override {}

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 4; ++i)
            if (rowBounds (i).contains (e.position) && onOpen)
                onOpen (i + 1);
    }

private:
    juce::Rectangle<float> rowBounds (int i) const { return juce::Rectangle<float> (16, 8 + (float) i * 96, (float) getWidth() - 32, 86); }
    juce::Rectangle<float> summaryBounds() const { return juce::Rectangle<float> (16, (float) getHeight() - 96, (float) getWidth() - 32, 86); }

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
            const float hs[] = { 8, 18, 28, 16, 22, 10 };
            for (int k = 0; k < 6; ++k)
                g.fillRoundedRectangle (c.x - 13 + (float) k * 5.0f, c.y - hs[k] * 0.5f, 3.0f, hs[k], 1.4f);
        }
        else if (i == 1)
        {
            g.drawRoundedRectangle (c.x - 14, c.y - 12, 28, 19, 3.0f, 2.4f);
            g.drawLine (c.x - 7, c.y + 13, c.x + 7, c.y + 13, 2.4f);
            juce::Path p;
            p.addTriangle (c.x - 3, c.y - 7, c.x - 3, c.y + 3, c.x + 5, c.y - 2);
            g.fillPath (p);
        }
        else if (i == 2)
        {
            g.drawRoundedRectangle (c.x - 15, c.y - 5, 17, 10, 5.0f, 2.6f);
            g.drawRoundedRectangle (c.x - 2, c.y - 5, 17, 10, 5.0f, 2.6f);
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

// Settings: for now the loop count-in. (Mic, tempo and click come later.)
class PocketApp::SettingsScreen : public juce::Component
{
public:
    SettingsScreen (AppSettings& s, std::function<void()> back, std::function<void()> changed)
        : settings (s), onChanged (std::move (changed))
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

        auto r = row();
        drawRaised (g, r, Look::cream, false, 16.0f);
        g.setColour (col::ink);
        g.setFont (spaced (condensed (21.0f), 0.08f));
        g.drawText ("LOOP COUNT-IN", juce::Rectangle<float> (r.getX() + 18, r.getY() + 10, r.getWidth() - 110, 26), juce::Justification::centredLeft);
        g.setColour (col::inkDim);
        g.setFont (small (14.0f));
        g.drawFittedText ("A silent one-bar count before a loop recording starts the clock.",
                          juce::Rectangle<int> ((int) r.getX() + 18, (int) r.getY() + 38, (int) r.getWidth() - 110, 40), juce::Justification::topLeft, 2);
        auto sw = switchBounds();
        drawWell (g, sw, 15.0f);
        g.setColour (settings.countIn ? col::amber : col::inkDim);
        g.fillEllipse (settings.countIn ? sw.getRight() - 29 : sw.getX() + 3, sw.getY() + 3, 24, 24);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (row().contains (e.position))
        {
            settings.countIn = ! settings.countIn;
            settings.save();
            if (onChanged)
                onChanged();
            repaint();
        }
    }

private:
    juce::Rectangle<float> row() const { return { 16, 108, (float) getWidth() - 32, 86 }; }
    juce::Rectangle<float> switchBounds() const { auto r = row(); return { r.getRight() - 78, r.getCentreY() - 17, 58, 30 }; }

    AppSettings& settings;
    std::function<void()> onChanged;
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

class PocketApp::TabBar : public juce::Component
{
public:
    std::function<void (int)> onSelect;
    int selected = 0;
    int bottomInset = 0;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::creamDeep);
        g.setColour (col::rule);
        g.fillRect (0, 0, getWidth(), 2);
        static const char* names[] = { "RECORD", "BEATS", "LOOPS", "VOCODER", "MORE" };
        const float w = (float) getWidth() / 5.0f;
        for (int i = 0; i < 5; ++i)
        {
            auto cell = juce::Rectangle<float> (w * (float) i, 4, w, (float) getHeight() - (float) bottomInset - 4).reduced (3, 2);
            const bool on = i == selected;
            if (on)
                drawRaised (g, cell, Look::amber, false, 14.0f);
            auto top = cell.withTrimmedBottom (3);
            auto icon = top.withHeight (top.getHeight() * 0.58f).withSizeKeepingCentre (30, 26).translated (0, 3);
            drawIcon (g, i, icon);
            g.setColour (col::ink);
            g.setFont (spaced (condensed (11.5f), 0.06f));
            g.drawText (names[i], top.withTrimmedTop (top.getHeight() * 0.62f), juce::Justification::centred);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.position.y < (float) getHeight() - (float) bottomInset + 20 && onSelect)
            onSelect (juce::jlimit (0, 4, (int) (e.position.x / ((float) getWidth() / 5.0f))));
    }

private:
    static void drawIcon (juce::Graphics& g, int i, juce::Rectangle<float> r)
    {
        auto c = r.getCentre();
        g.setColour (col::ink);
        if (i == 0)
        {
            g.setColour (col::red);
            g.fillEllipse (c.x - 8, c.y - 8, 16, 16);
            g.setColour (col::ink);
            g.drawEllipse (c.x - 11, c.y - 11, 22, 22, 2.2f);
        }
        else if (i == 1)
        {
            for (int k = 0; k < 4; ++k)
            {
                g.setColour (k == 1 ? col::inkDim : col::ink);
                g.fillRoundedRectangle (c.x - 11 + (float) (k % 2) * 12.0f, c.y - 11 + (float) (k / 2) * 12.0f, 10, 10, 2.5f);
            }
        }
        else if (i == 2)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, 10, 10, 0, 0.5f, 2.9f, true);
            p.addCentredArc (c.x, c.y, 10, 10, 0, 3.64f, 6.04f, true);
            g.strokePath (p, juce::PathStrokeType (2.4f));
            juce::Path a;
            a.addTriangle (c.x + 4, c.y - 13, c.x + 4, c.y - 5, c.x + 11, c.y - 9);
            a.addTriangle (c.x - 4, c.y + 13, c.x - 4, c.y + 5, c.x - 11, c.y + 9);
            g.fillPath (a);
        }
        else if (i == 3)
        {
            g.fillRoundedRectangle (c.x - 4.5f, c.y - 12, 9, 15, 4.5f);
            juce::Path p;
            p.addCentredArc (c.x, c.y - 1, 8, 8, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::pi * 1.5f + 3.14159f, true);
            p.startNewSubPath (c.x, c.y + 7);
            p.lineTo (c.x, c.y + 12);
            g.strokePath (p, juce::PathStrokeType (2.2f));
            g.drawLine (c.x - 5, c.y + 12, c.x + 5, c.y + 12, 2.2f);
        }
        else
        {
            for (int k = -1; k <= 1; ++k)
                g.fillEllipse (c.x + (float) k * 9 - 2.8f, c.y - 2.8f, 5.6f, 5.6f);
        }
    }
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
      padSheet (beats.kit()),
      record (static_cast<PocketHost&> (*this), audio, lib, sheet),
      sounds (static_cast<PocketHost&> (*this), audio, lib)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);
    juce::Desktop::getInstance().addGlobalMouseListener (&keyboardDismisser);

    auto backToMore = [this] { showMorePage (0); };
    remote = std::make_unique<RemoteScreen> (link);
    remote->onLink = [this] { openMacLink(); };
    remote->onSay = [this] (const juce::String& t) { toast (t); };
    macLink = std::make_unique<MacLinkScreen> (link);
    macLink->onClose = backToMore;
    macLink->onRemote = [this] { showMorePage (2); };
    macLink->onSay = [this] (const juce::String& t) { toast (t); };
    sendSheet = std::make_unique<SendToMacSheet> (link);
    sendSheet->onClose = [this] { closeSendSheet(); };
    sendSheet->onLink = [this] { closeSendSheet(); openMacLink(); };
    sendSheet->onSay = [this] (const juce::String& t) { toast (t); };
    link.onChange = [this] { linkChanged(); };
    link.onSay = [this] (const juce::String& t) { toast (t); };
    settings = std::make_unique<SettingsScreen> (appSettings, backToMore, [this] { loopsScreen.engine().setCountIn (appSettings.countIn); });
    more = std::make_unique<MoreScreen> (lib, link, [this] { return loopsScreen.layerCount(); });
    more->onOpen = [this] (int page) { showMorePage (page); };
    ideasScreen = std::make_unique<IdeasScreen> (ideas);
    ideasScreen->onOpen = [this] (const juce::String& id) { openIdea (id); };
    ideasScreen->onNew = [this] { newIdea(); };
    ideasScreen->onRename = [this] (const juce::String& id) { renameIdea (id); };
    ideasScreen->onDelete = [this] (const juce::String& id) { deleteIdea (id); };
    tabs = std::make_unique<TabBar>();
    tabs->onSelect = [this] (int i) { showTab (i); };
    toastBox = std::make_unique<Toast>();
    dim = std::make_unique<Dim>();
    dim->onTap = [this] { closeSheet(); closePadSheet(); closeSendSheet(); closeTempo(); closeSoundBank(); };
    soundBank.onLoad = [this] (int k) { loadKit (k); };
    soundBank.onDone = [this] { closeSoundBank(); };
    soundBank.onOpenSounds = [this] { closeSoundBank(); showMorePage (1); };
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
                toast ("Imported " + juce::String (ok) + (ok == 1 ? " sound" : " sounds") + U8(". They're in More › Sounds: TO PAD puts one on a pad."));
            else if (error.isNotEmpty())
                toast (error);
        });
    };
    tempoSheet.getBpm = [this] { return beats.drums().bpm; };
    tempoSheet.setBpm = [this] (double b) { beats.setBpm (b); repaint(); };
    tempoSheet.getClick = [this] { return appSettings.click; };
    tempoSheet.setClick = [this] (bool on)
    {
        appSettings.click = on;
        appSettings.save();
        if (beats.engine().clockRunning())
            beats.engine().setClick (on);
    };
    tempoSheet.getCountIn = [this] { return appSettings.countIn; };
    tempoSheet.setCountIn = [this] (bool on)
    {
        appSettings.countIn = on;
        appSettings.save();
        loopsScreen.engine().setCountIn (on);
    };
    tempoSheet.onDone = [this] { closeTempo(); };
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
        ideasShown = false;
        showTab (0);
        record.editForPad (pad, id);
        noteStep();
    };
    padSheet.onChop = [this] (int pad)
    {
        const auto id = beats.kit().pads[(size_t) pad].soundId;
        auto* item = lib.find (id);
        if (item == nullptr)
            return toast ("That sound is gone from Sounds");
        if (item->markers.empty())
            return toast ("No chop markers on this sound. Add some in Record, under Chop.");
        const int n = beats.kit().chop (pad, id, false);
        beats.padsChanged();
        closePadSheet();
        toast ("Chopped across " + juce::String (n) + (n == 1 ? " pad" : " pads"));
    };

    addChildComponent (record);
    addAndMakeVisible (*ideasScreen);
    addChildComponent (sounds);
    for (auto* c : std::initializer_list<juce::Component*> { &beats, &loopsScreen, &vocoderScreen, remote.get(), macLink.get(), settings.get(), more.get() })
        addChildComponent (c);
    addAndMakeVisible (*tabs);
    addChildComponent (*dim);
    addChildComponent (sheet);
    addChildComponent (padSheet);
    addChildComponent (tempoSheet);
    addChildComponent (soundBank);
    addChildComponent (*sendSheet);
    addChildComponent (*toastBox);

    setSize (390, 844);
    if (o.audio)
        fillEmptyIdea();   // the first launch: the pads start with the Street kit
    sounds.selectFirst();
    animate = o.audio;

    loopsScreen.engine().setVocoderSource (&vocoderScreen.engine(), &vocoderScreen.engine().monitorFlag());
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
    juce::Desktop::getInstance().removeGlobalMouseListener (&keyboardDismisser);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
}

void PocketApp::toast (const juce::String& t)
{
    toastBox->show (t);
}

void PocketApp::showTab (int i)
{
    if (sheetOpen)
        closeSheet();
    closePadSheet();
    closeSendSheet();
    audio.stopPlaying();
    if (i != 1 && i != 2 && i != 3)
    {
        beats.leave();     // Beats, Loops and Vocoder share the clock, so moving between them keeps it going
        loopsScreen.leave();
    }
    if (i == 0 && tab == 0 && ! ideasShown && ! audio.isRecording())
        ideasShown = true;   // RECORD again from the recorder: back to the Ideas list, like Voice Memos
    tab = i;
    tabs->selected = i;
    record.setVisible (i == 0 && ! ideasShown);
    ideasScreen->setVisible (i == 0 && ideasShown);
    if (i == 0 && ideasShown)
    {
        ideas.setSummary (beats.summary() + (loopsScreen.layerCount() > 0 ? U8(" · ") + juce::String (loopsScreen.layerCount()) + (loopsScreen.layerCount() == 1 ? " loop" : " loops") : juce::String()));
        ideasScreen->refresh();
    }
    beats.setVisible (i == 1);
    loopsScreen.setVisible (i == 2);
    vocoderScreen.setVisible (i == 3);
    if (i == 4)
        showMorePage (0);
    else
        showMorePage (-1);
    tabs->repaint();
    noteStep();
    repaint();
}

// page: -1 = not on the More tab
void PocketApp::showMorePage (int page)
{
    if (page >= 0 && tab != 4)
    {
        tab = 4;
        tabs->selected = 4;
        tabs->repaint();
        record.setVisible (false);
        ideasScreen->setVisible (false);
        beats.setVisible (false);
        beats.leave();
        loopsScreen.leave();
        loopsScreen.setVisible (false);
        vocoderScreen.setVisible (false);
    }
    more->setVisible (page == 0);
    sounds.setVisible (page == 1);
    remote->setVisible (page == 2);
    macLink->setVisible (page == 3);
    settings->setVisible (page == 4);
    if (page == 1)
        sounds.selectFirst();
    if (page == 3)
        macLink->shown();
    repaint();   // the header is dark on the Remote
}

void PocketApp::editSound (const juce::String& id)
{
    ideasShown = false;
    showTab (0);
    record.loadSound (id);
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
    const int top = insets.getTop() + 102;
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
        return toast ("Bank " + juce::String::charToString ((juce::juce_wchar) ('A' + beats.currentBank())) + " is full. Pick another bank in Beats.");

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
    showTab (1);
    beats.showMode (1);
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
    sheet.setBounds (sheet.getBounds().withY (getHeight()).withHeight (getHeight() - insets.getTop() - 102).withWidth (getWidth()));
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
    showMorePage (3);
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
    showTab (1);
    beats.showMode (1);
    beats.selectPad (pad);
}

//==============================================================================
// Ideas and the seven steps
void PocketApp::showIdeasList (bool shown)
{
    ideasShown = shown;
    showTab (0);
}

void PocketApp::switchIdea (const juce::String& id)
{
    if (id == ideas.current().id)
        return;
    lockWatch.stopTimer();
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
    const auto& idea = ideas.current();
    // Carry on where it left off: the step after the furthest one reached.
    goStep (idea.sent ? stepSong : juce::jlimit (0, kSteps - 1, idea.reached + 1), true);
}

void PocketApp::newIdea()
{
    switchIdea (ideas.create (ideas.nextName()));
    ideasShown = false;
    showTab (0);
    record.startRecording();
    toast (ideas.current().name + U8(": recording. Tap the red button again to stop."));
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

int PocketApp::currentStep() const
{
    if (sendOpen)
        return stepSend;
    switch (tab)
    {
        case 0:  return ideasShown ? -1 : (sheetOpen ? stepShape : stepRecord);
        case 1:  return beats.mode() == 1 && beats.drums().padPattern.hits.empty() ? stepPads : stepBeat;
        case 2:
        case 3:  return stepSing;
        default: return -1;
    }
}

void PocketApp::noteStep()
{
    if (const int s = currentStep(); s >= 0)
        ideas.reachedStep (s);
}

void PocketApp::goStep (int step, bool explain)
{
    switch (step)
    {
        case stepRecord:
            showIdeasList (false);
            break;
        case stepShape:
            showIdeasList (false);
            openSheet();
            break;
        case stepPads:
        case stepBeat:
            showTab (1);
            beats.showMode (1);
            break;
        case stepSing:
            showTab (2);
            break;
        case stepSong:
            showTab (2);
            break;
        case stepSend:
            showTab (2);
            loopsScreen.sendToMac (false);
            break;
        default:
            return;
    }
    noteStep();
    repaint();
    if (! explain)
        return;
    static const char* tips[kSteps] = {
        "RECORD: tap the red button to record a sound, then Save to sounds",
        "SHAPE: trim, fade and add effects; Chop finds the hits",
        "PADS: TO PAD puts sounds on pads, or EDIT PADS",
        "BEAT: REC PADS, play along with the click, then LOCK BEAT",
        "SING: pick Mic or Vocoder, then REC records a layer over the beat",
        "SONG: mute and unmute layers as it plays. Song sections come next.",
        "SEND: every loop layer goes to your Mac" };
    if (step != stepSend)
        toast (tips[step]);
}

bool PocketApp::stripShown() const
{
    return currentStep() >= 0 || (tab == 0 && ! ideasShown);
}

juce::Rectangle<float> PocketApp::stepRect (int step) const
{
    const float x0 = 16.0f, x1 = (float) getWidth() - 98.0f, gap = 4.0f;
    const float w = (x1 - x0 - gap * (kSteps - 1)) / (float) kSteps;
    return { x0 + (float) step * (w + gap), (float) insets.getTop() + 36.0f, w, 24.0f };
}

juce::Rectangle<float> PocketApp::nextRect() const
{
    return { (float) getWidth() - 90.0f, (float) insets.getTop() + 35.0f, 78.0f, 22.0f };
}

juce::Rectangle<float> PocketApp::backRect() const
{
    return { 8.0f, (float) insets.getTop() + 2.0f, transportRect().getX() - 14.0f, 32.0f };
}

juce::Rectangle<float> PocketApp::transportRect() const
{
    return { (float) getWidth() - 182.0f, (float) insets.getTop() + 3.0f, 146.0f, 30.0f };
}

juce::Rectangle<float> PocketApp::playRect() const
{
    auto t = transportRect();
    return { t.getRight() - 38.0f, t.getY() + 2.0f, 36.0f, t.getHeight() - 4.0f };
}

// Play: the loop layers if there are any, otherwise the pad pattern, otherwise the drum machine.
// Stop: everything.
void PocketApp::togglePlay()
{
    auto& b = beats.engine();
    auto& l = loopsScreen.engine();
    if (b.clockRunning())
    {
        lockWatch.stopTimer();
        l.setTransport (false);
        b.setPlaying (false);
        b.setPatternPlaying (false);
        b.setClick (false);
        b.stopPads();
    }
    else
    {
        if (loopsScreen.layerCount() > 0)
            l.setTransport (true);
        else if (! beats.drums().padPattern.hits.empty())
            b.setPatternPlaying (true);
        else
            b.setPlaying (true);
        b.setClick (appSettings.click);
    }
    beats.refreshPads();
    repaint();
}

juce::Rectangle<int> PocketApp::tempoBounds() const
{
    const int h = 410 + insets.getBottom();
    return { 0, getHeight() - h, getWidth(), h };
}

void PocketApp::openTempo()
{
    if (tempoOpen)
        return;
    closeSheet();
    closePadSheet();
    closeSendSheet();
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

//==============================================================================
// LOCK BEAT
void PocketApp::lockBeat (int bars)
{
    if (lockWatch.isTimerRunning())
        return toast ("Already locking the beat");
    auto& loops = loopsScreen.engine();
    int layer = -1;
    for (int i = 0; i < pocket::kLoopLayers && layer < 0; ++i)
        if (loops.state (i) == pocket::LoopState::empty)
            layer = i;
    if (layer < 0)
        return toast ("All 8 loop layers are full: clear one in Loops first");
    lockLayer = layer;
    lockBars = bars;
    lockArmed = false;
    auto& b = beats.engine();
    if (! b.patternPlaying())
        b.setPatternPlaying (true);
    toast ("Locking: the beat goes onto loop " + juce::String (layer + 1) + " from its next start");
    lockWatch.startTimer (20);
}

void PocketApp::lockWatchTick()
{
    auto& b = beats.engine();
    auto& loops = loopsScreen.engine();
    if (! lockArmed)
    {
        if (! b.patternPlaying())
        {
            lockWatch.stopTimer();
            return toast ("Lock cancelled");
        }
        if (! b.clockRunning())
            return;
        const double len = lockBars * 4.0;
        const double pos = std::fmod (b.clockBeat(), len);
        // Arm in the pattern's last bar, so recording starts on the next bar line: the pattern's start.
        if (pos >= len - 4.0 && pos < len - 0.3)
        {
            if (! loops.arm (lockLayer, pocket::LoopSource::drums, lockBars))
            {
                lockWatch.stopTimer();
                return toast ("Couldn't arm loop " + juce::String (lockLayer + 1));
            }
            lockArmed = true;
        }
        return;
    }
    const auto st = loops.state (lockLayer);
    if (st == pocket::LoopState::playing)
    {
        lockWatch.stopTimer();
        loops.setTransport (true);     // the loop keeps the clock going once the pattern stops
        b.setPatternPlaying (false);
        b.setPlaying (false);
        b.setClick (false);
        beats.refreshPads();
        toast ("Beat locked on loop " + juce::String (lockLayer + 1) + ". Next: sing over it");
    }
    else if (st == pocket::LoopState::empty)
    {
        lockWatch.stopTimer();
        toast ("Lock cancelled");
    }
}

void PocketApp::linkChanged()
{
    repaint();
    more->repaint();
    macLink->refresh();
    remote->refresh();
    sendSheet->refresh();
}

juce::Rectangle<float> PocketApp::chipRect() const
{
    if (stripShown())
        return { (float) getWidth() - 32, (float) insets.getTop() + 5, 26, 26 };   // just the dot: the transport needs the room
    return { (float) getWidth() - 150, (float) insets.getTop() + 12, 138, 38 };
}

bool PocketApp::remoteShowing() const
{
    return remote != nullptr && remote->isVisible();
}

void PocketApp::mouseUp (const juce::MouseEvent& e)
{
    if (chipRect().expanded (4).contains (e.position))
        return openMacLink();
    if (! stripShown())
        return;
    if (playRect().expanded (3).contains (e.position))
        return togglePlay();
    if (transportRect().contains (e.position))
        return openTempo();
    if (backRect().contains (e.position))
        return showIdeasList (true);
    const int now = currentStep();
    if (nextRect().expanded (2).contains (e.position))
        return goStep (juce::jmin (kSteps - 1, now + 1), true);
    for (int s = 0; s < kSteps; ++s)
        if (stepRect (s).expanded (2, 4).contains (e.position))
            return goStep (s, s != now);
}

//==============================================================================
void PocketApp::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);

    const int top = insets.getTop();
    const bool dark = remoteShowing();
    if (dark)
        g.fillAll (juce::Colour (0xff1f1b17));
    if (stripShown())
    {
        // The idea's name (tap it, or the arrow, for the Ideas list), then the seven steps and NEXT.
        auto back = backRect();
        g.setColour (col::ink);
        juce::Path arrow;
        arrow.startNewSubPath (back.getX() + 15, back.getCentreY() - 8);
        arrow.lineTo (back.getX() + 8, back.getCentreY());
        arrow.lineTo (back.getX() + 15, back.getCentreY() + 8);
        g.strokePath (arrow, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (condensedBold (23.0f));
        g.drawFittedText (ideas.current().name, back.withTrimmedLeft (24).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);

        const int now = currentStep();
        const int reached = ideas.current().reached;
        for (int s = 0; s < kSteps; ++s)
        {
            auto r = stepRect (s);
            g.setColour (s <= juce::jmax (reached, now) ? stepColour (s) : col::creamDeep);
            g.fillRoundedRectangle (r.getX(), r.getY(), r.getWidth(), s == now ? 7.0f : 5.0f, 2.5f);
            g.setColour (s == now ? col::ink : col::inkDim);
            g.setFont (spaced (condensedBold (s == now ? 12.0f : 11.0f), 0.04f));
            g.drawFittedText (stepName (s), r.withTrimmedTop (8).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
        if (now < kSteps - 1)
        {
            auto n = nextRect();
            drawRaised (g, n, Look::amber, false, 8.0f);
            g.setColour (col::ink);
            g.setFont (spaced (condensedBold (13.0f), 0.06f));
            g.drawFittedText (juce::String (stepName (now + 1)) + U8(" \xe2\x80\xba"), n.withTrimmedBottom (3).toNearestInt(), juce::Justification::centred, 1, 0.7f);
        }
    }
    else
    {
        g.setColour (dark ? juce::Colour (0xfff1e7d6) : col::ink);
        g.setFont (fraunces (32.0f));
        g.drawText ("Kaleidobox", 16, top + 8, 220, 46, juce::Justification::centredLeft);
    }

    // the link to the Mac: green with its name when linked; tap it for Link to your Mac
    auto chip = chipRect();
    if (dark)
    {
        g.setColour (juce::Colour (0xff332c26));
        g.fillRoundedRectangle (chip, 9.0f);
    }
    const auto status = link.getStatus();
    const auto dotColour = status == LinkClient::Status::linked ? col::green : status == LinkClient::Status::connecting ? col::amber : col::grey;
    const bool dotOnly = stripShown();
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
    g.setColour (dark ? juce::Colour (0xfff1e7d6) : status == LinkClient::Status::linked ? col::ink : col::inkDim);
    g.setFont (spaced (condensed (15.0f), 0.1f));
    const auto text = status == LinkClient::Status::linked ? link.getMacName().toUpperCase()
                    : status == LinkClient::Status::connecting ? U8("LINKING…") : juce::String ("NOT LINKED");
    g.drawFittedText (text, chip.withTrimmedLeft (30).withTrimmedRight (6).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    if (stripShown())
    {
        // The transport: BPM (tap for Tempo), where the song is, Play/Stop
        auto t = transportRect();
        g.setColour (col::well);
        g.fillRoundedRectangle (t, 9.0f);
        const bool running = beats.engine().clockRunning();
        g.setFont (lcdFont (22.0f));
        g.setColour (col::lcdAmber);
        const auto bpmText = juce::String (juce::roundToInt (beats.drums().bpm));
        g.drawText (bpmText, t.getX() + 8, t.getY(), 34, t.getHeight(), juce::Justification::centredLeft);
        g.setColour (col::inkFaint);
        g.setFont (lcdFont (14.0f));
        g.drawText ("BPM", t.getX() + 38, t.getY() + 2, 26, t.getHeight(), juce::Justification::centredLeft);
        g.setColour (col::lcdText);
        g.setFont (lcdFont (22.0f));
        g.drawText (shownBar.isEmpty() ? juce::String ("1.1") : shownBar, t.getX() + 62, t.getY(), 42, t.getHeight(), juce::Justification::centredRight);
        auto p = playRect();
        g.setColour (running ? col::red : juce::Colour (0xff3c8f56));
        g.fillRoundedRectangle (p, 7.0f);
        g.setColour (juce::Colour (0xfff1e7d1));
        auto c = p.getCentre();
        if (running)
            g.fillRoundedRectangle (c.x - 6, c.y - 6, 12, 12, 2.0f);
        else
        {
            juce::Path tri;
            tri.addTriangle (c.x - 5, c.y - 7, c.x - 5, c.y + 7, c.x + 7, c.y);
            g.fillPath (tri);
        }
    }

    g.setColour (dark ? juce::Colour (0xff3a332c) : col::rule);
    g.fillRect (0, top + 60, getWidth(), 2);
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
    const int tabH = 47 + insets.getBottom();
    tabs->bottomInset = insets.getBottom();
    tabs->setBounds (0, h - tabH, w, tabH);

    auto content = juce::Rectangle<int> (0, insets.getTop() + 62, w, h - tabH - insets.getTop() - 62);
    for (auto* c : std::initializer_list<juce::Component*> { &record, ideasScreen.get(), &sounds, &beats, &loopsScreen, &vocoderScreen, remote.get(), macLink.get(), settings.get(), more.get() })
        c->setBounds (content);
    sendSheet->setBounds (sendOpen ? sendSheetBounds() : sendSheetBounds().withY (h));

    dim->setBounds (0, 0, w, h);
    layoutSheet (sheetOpen);
    padSheet.setBounds (padSheetOpen ? padSheetBounds() : padSheetBounds().withY (h));
    tempoSheet.setBounds (tempoOpen ? tempoBounds() : tempoBounds().withY (h));
    soundBank.setBounds (bankOpen ? tempoBounds().withTop (h - 560 - insets.getBottom()) : tempoBounds().withTop (h - 560 - insets.getBottom()).withY (h));
    toastBox->setBounds (24, h - tabH - 62, w - 48, 48);
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

    auto take = lib.load (lib.items().front().id);

    // The Ideas list, like Voice Memos
    {
        const auto now = juce::Time::getCurrentTime();
        const auto day = juce::RelativeTime::days (1);
        ideas.rename (ideas.current().id, "Porch rain groove");
        ideas.setDemo (ideas.current().id, stepSing, false, U8("92 BPM · 6 pads · 3 loops"), now - juce::RelativeTime::minutes (20));
        ideas.setDemo (ideas.create ("Train horn chop"), stepBeat, false, U8("92 BPM · 4 pads · beat recorded"), now - day);
        ideas.setDemo (ideas.create ("Humming in the car"), stepRecord, false, U8("0:38 recording"), now - juce::RelativeTime::days (3));
        ideas.setDemo (ideas.create ("Late night vocoder"), stepSend, true, U8("84 BPM · 5 loops"), now - juce::RelativeTime::days (4));
    }
    showIdeasList (true);
    savePng (folder.getChildFile ("00-ideas.png"));

    ideasShown = false;
    showTab (0);
    record.showDemoTake (take, "Porch rain", false);
    savePng (folder.getChildFile ("01-record.png"));
    record.showDemoTake (take, "Porch rain", true);
    savePng (folder.getChildFile ("01b-recording.png"));
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

    // Loops: layers 1-2 playing, 3 recording, 4 waiting for the next bar, 5 muted, 6-8 empty
    loopsScreen.showDemo();
    showTab (2);
    savePng (folder.getChildFile ("07-loops.png"));
    openTempo();
    savePng (folder.getChildFile ("07c-tempo.png"));
    closeTempo();
    loopsScreen.showEditorDemo();
    savePng (folder.getChildFile ("07d-loop-editor.png"));
    loopsScreen.leave();

    // Vocoder: the Robot pedal open, A-flat picked, five pedals on
    vocoderScreen.showDemo();
    showTab (3);
    savePng (folder.getChildFile ("07-vocoder.png"));
    vocoderScreen.engine().setMonitor (true);
    vocoderScreen.setDemoHeadphones (false);
    savePng (folder.getChildFile ("07b-vocoder-no-headphones.png"));
    vocoderScreen.engine().setMonitor (false);
    vocoderScreen.setDemoHeadphones (true);

    showTab (4);
    savePng (folder.getChildFile ("08-more.png"));

    showMorePage (4);
    savePng (folder.getChildFile ("10-settings.png"));

    showMorePage (1);
    savePng (folder.getChildFile ("09-sounds.png"));

    // Beats: the drum machine with a beat in it, the MPC pads with a demo kit, and the pad sheet
    showTab (1);
    beats.drums().variation = 2;
    beats.drums().bpm = 97.0;
    beats.drums().settings.swing = 0.16f;
    beats.drums().settings.voices[0].level = 0.8f;
    beats.drums().settings.voices[0].decay = 0.62f;
    beats.drums().settings.voices[0].tone = 0.45f;
    beats.setBpm (97.0);
    beats.drumsChanged();
    beats.showMode (0);
    beats.selectVoice (0);
    beats.setDemoStep (4);
    savePng (folder.getChildFile ("03-beats-drum-machine.png"));
    beats.setDemoStep (-1);

    beats.fillDemoKit();
    beats.showMode (1);
    savePng (folder.getChildFile ("04-beats-mpc-pads.png"));
    beats.fillDemoPattern();
    savePng (folder.getChildFile ("04b-pads-pattern.png"));

    openPadSheet (4);
    resized();
    savePng (folder.getChildFile ("05-pad-sheet.png"));
    closePadSheet();
    openSoundBank();
    savePng (folder.getChildFile ("05b-sound-bank.png"));
    closeSoundBank();
    padSheet.onTrim (4);   // SAMPLE PAGE: the pad's sound in the editor
    savePng (folder.getChildFile ("05c-pad-sample-page.png"));
    record.endPadMode();

    // The Mac pages: Send to Mac (from Sounds), the Remote, and Link to your Mac (not linked yet, three numbers typed)
    showTab (4);
    showMorePage (1);
    sendSoundToMac (lib.items().front().id, "sounds");
    resized();
    savePng (folder.getChildFile ("13-send-to-mac.png"));
    closeSendSheet();

    showMorePage (2);
    savePng (folder.getChildFile ("12-remote.png"));

    link.setDemo ({ { "Colin's MacBook", "Friday Set", true, false }, { "Studio iMac", {}, false, true } }, {}, {});
    showMorePage (3);
    macLink->setDemoCode ("472");
    savePng (folder.getChildFile ("11-link-mac.png"));
    showMorePage (2);
    savePng (folder.getChildFile ("12b-remote-not-linked.png"));
    link.setDemo (demoMacs, "Colin's MacBook", state);

    // Checks for the things a picture can't show: TO PAD puts a sound on the next empty pad of the bank
    // and shows it, and an export for the MPC reads back through the MPC file code.
    juce::StringArray problems;
    showTab (4);
    showMorePage (1);
    const auto* firstSound = &lib.items().front();
    sendToPad (firstSound->id);
    if (beats.selectedPad() != 2 || beats.kit().pads[2].empty() || tab != 1 || beats.mode() != 1)
        problems.add ("TO PAD did not land on A03 in Beats > MPC Pads");
    savePng (folder.getChildFile ("06-to-pad.png"));
    if (const auto why = beats.selfCheckExport (folder.getChildFile ("export-check")); why.isNotEmpty())
        problems.add ("MPC export: " + why);
    else
        DBG ("MPC export round-trip ok");

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
    for (int k = 0; k < (int) factoryKits().size(); ++k)
    {
        auto sounds = makeFactorySounds (k);
        if (sounds.size() != 16)
            problems.add (juce::String ("Factory kit ") + factoryKits()[(size_t) k].name + " has " + juce::String ((int) sounds.size()) + " sounds");
        auto dir = folder.getChildFile ("factory").getChildFile (factoryKits()[(size_t) k].name);
        dir.createDirectory();
        for (auto& [name, b] : sounds)
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

    auto note = folder.getChildFile ("selfcheck.txt");
    note.replaceWithText ((problems.isEmpty() ? juce::String ("OK: ideas keep their own pads and beat, a new idea gets the Street kit, TO PAD, the MPC export round trip, the pad pattern, the drum machine and the pads make sound, and loops record, line up and play with no gap, and the vocoder makes sound that follows the voice\n")
                                              : "FAILED\n" + problems.joinIntoString ("\n") + "\n")
                          + "\nLoops self-check (97 BPM, 44.1 kHz):\n" + loopsReport
                          + "\nVocoder self-check (44.1 kHz):\n" + vocoderReport);
    for (auto& line : problems)
        std::fprintf (stderr, "SELF-CHECK FAILED: %s\n", line.toRawUTF8());
}
