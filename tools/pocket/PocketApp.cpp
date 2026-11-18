#include "PocketApp.h"

#include "core/AudioFiles.h"

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
class PocketApp::Panel : public juce::Component
{
public:
    Panel (juce::String t, juce::String b, std::function<void()> back = {}) : title (std::move (t)), blurb (std::move (b))
    {
        if (back)
        {
            backButton.setFontHeight (16.0f);
            backButton.onClick = std::move (back);
            addAndMakeVisible (backButton);
        }
    }

    void resized() override { backButton.setBounds (16, 6, 96, 44); }

    void paint (juce::Graphics& g) override
    {
        auto card = getLocalBounds().toFloat().reduced (16, 20).withHeight (230).translated (0, backButton.onClick ? 44.0f : 0.0f);
        drawRaised (g, card, Look::cream, false, 20.0f);
        g.setColour (col::ink);
        g.setFont (fraunces (30.0f));
        g.drawText (title, card.withTrimmedTop (28).withHeight (40), juce::Justification::centred);
        g.setFont (spaced (condensed (14.0f), 0.18f));
        g.setColour (col::amberDark);
        g.drawText ("COMING NEXT", card.withTrimmedTop (84).withHeight (24), juce::Justification::centred);
        g.setColour (col::inkDim);
        g.setFont (small (16.0f));
        g.drawFittedText (blurb, card.reduced (28, 0).withTrimmedTop (120).withHeight (80).toNearestInt(), juce::Justification::centredTop, 4);
    }

private:
    juce::String title, blurb;
    PButton backButton { "< More" };
};

class PocketApp::MoreScreen : public juce::Component
{
public:
    MoreScreen (Library& l, std::function<int()> loopLayerCount) : lib (l), loopLayers (std::move (loopLayerCount)) {}
    std::function<void (int)> onOpen;

    void paint (juce::Graphics& g) override
    {
        static const char* titles[] = { "SOUNDS", "REMOTE", "MAC LINK", "SETTINGS" };
        const int n = (int) lib.items().size();
        const juce::String blurbs[] = {
            U8("Everything you've recorded, imported or got from the Mac · ") + juce::String (n) + (n == 1 ? " sound" : " sounds"),
            "Run the Rig on the Mac: play, sections, mutes, loops",
            U8("Not linked · linking to your Mac comes in a later update"),
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
                g.setColour (col::grey);
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
        for (auto* folder : { "Sounds", "Loops" })
            for (auto& f : lib.getRoot().getChildFile (folder).findChildFiles (juce::File::findFiles, false))
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
      appSettings (lib.getRoot()),
      beats (static_cast<PocketHost&> (*this), lib),
      loopsScreen (static_cast<PocketHost&> (*this), lib, beats.engine(), appSettings),
      vocoderScreen (static_cast<PocketHost&> (*this), lib, audio, loopsScreen, beats.engine()),
      padSheet (beats.kit()),
      record (static_cast<PocketHost&> (*this), audio, lib, sheet),
      sounds (static_cast<PocketHost&> (*this), audio, lib)
{
    juce::LookAndFeel::setDefaultLookAndFeel (&look);

    auto backToMore = [this] { showMorePage (0); };
    remote = std::make_unique<Panel> ("Remote", "Run the Rig on the Mac from here: play, sections, mutes and loops.", backToMore);
    macLink = std::make_unique<Panel> ("Mac link", "Link this iPhone to your Mac on the same Wi-Fi to send sounds across.", backToMore);
    settings = std::make_unique<SettingsScreen> (appSettings, backToMore, [this] { loopsScreen.engine().setCountIn (appSettings.countIn); });
    more = std::make_unique<MoreScreen> (lib, [this] { return loopsScreen.layerCount(); });
    more->onOpen = [this] (int page) { showMorePage (page); };
    tabs = std::make_unique<TabBar>();
    tabs->onSelect = [this] (int i) { showTab (i); };
    toastBox = std::make_unique<Toast>();
    dim = std::make_unique<Dim>();
    dim->onTap = [this] { closeSheet(); closePadSheet(); };

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
        editSound (id);
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

    addAndMakeVisible (record);
    addChildComponent (sounds);
    for (auto* c : std::initializer_list<juce::Component*> { &beats, &loopsScreen, &vocoderScreen, remote.get(), macLink.get(), settings.get(), more.get() })
        addChildComponent (c);
    addAndMakeVisible (*tabs);
    addChildComponent (*dim);
    addChildComponent (sheet);
    addChildComponent (padSheet);
    addChildComponent (*toastBox);

    setSize (390, 844);
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
    audio.stopPlaying();
    if (i != 1 && i != 2 && i != 3)
    {
        beats.leave();     // Beats, Loops and Vocoder share the clock, so moving between them keeps it going
        loopsScreen.leave();
    }
    tab = i;
    tabs->selected = i;
    record.setVisible (i == 0);
    beats.setVisible (i == 1);
    loopsScreen.setVisible (i == 2);
    vocoderScreen.setVisible (i == 3);
    if (i == 4)
        showMorePage (0);
    else
        showMorePage (-1);
    tabs->repaint();
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
}

void PocketApp::editSound (const juce::String& id)
{
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
void PocketApp::paint (juce::Graphics& g)
{
    g.fillAll (col::cream);

    const int top = insets.getTop();
    g.setColour (col::ink);
    g.setFont (fraunces (32.0f));
    g.drawText ("Kaleidobox", 16, top + 8, 220, 46, juce::Justification::centredLeft);

    // link status: grey, because linking to the Mac comes in a later update
    g.setColour (col::grey);
    g.fillEllipse ((float) getWidth() - 132, (float) top + 24, 11, 11);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse ((float) getWidth() - 132, (float) top + 24, 11, 11, 1.0f);
    g.setColour (col::inkDim);
    g.setFont (spaced (condensed (15.0f), 0.1f));
    g.drawText ("NOT LINKED", getWidth() - 116, top + 8, 100, 46, juce::Justification::centredLeft);

    g.setColour (col::rule);
    g.fillRect (0, top + 60, getWidth(), 2);
}

void PocketApp::resized()
{
    if (more == nullptr || toastBox == nullptr || dim == nullptr)
        return;   // still being built
    auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    insets = display != nullptr ? display->safeAreaInsets : juce::BorderSize<int>();
   #if ! JUCE_IOS
    insets = juce::BorderSize<int> (47, 0, 34, 0);   // pretend to be an iPhone with a notch
   #endif

    const int w = getWidth(), h = getHeight();
    const int tabH = 47 + insets.getBottom();
    tabs->bottomInset = insets.getBottom();
    tabs->setBounds (0, h - tabH, w, tabH);

    auto content = juce::Rectangle<int> (0, insets.getTop() + 62, w, h - tabH - insets.getTop() - 62);
    for (auto* c : std::initializer_list<juce::Component*> { &record, &sounds, &beats, &loopsScreen, &vocoderScreen, remote.get(), macLink.get(), settings.get(), more.get() })
        c->setBounds (content);

    dim->setBounds (0, 0, w, h);
    layoutSheet (sheetOpen);
    padSheet.setBounds (padSheetOpen ? padSheetBounds() : padSheetBounds().withY (h));
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

    auto take = lib.load (lib.items().front().id);

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

    openPadSheet (4);
    resized();
    savePng (folder.getChildFile ("05-pad-sheet.png"));
    closePadSheet();

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

    juce::String loopsReport;
    if (const auto why = LoopsScreen::selfCheck (loopsReport); why.isNotEmpty())
        problems.add ("Loops: " + why);

    juce::String vocoderReport;
    if (const auto why = VocoderEngine::selfCheck (vocoderReport); why.isNotEmpty())
        problems.add ("Vocoder: " + why);

    auto note = folder.getChildFile ("selfcheck.txt");
    note.replaceWithText ((problems.isEmpty() ? juce::String ("OK: TO PAD, the MPC export round trip, the drum machine and the pads make sound, and loops record, line up and play with no gap, and the vocoder makes sound that follows the voice\n")
                                              : "FAILED\n" + problems.joinIntoString ("\n") + "\n")
                          + "\nLoops self-check (97 BPM, 44.1 kHz):\n" + loopsReport
                          + "\nVocoder self-check (44.1 kHz):\n" + vocoderReport);
    for (auto& line : problems)
        std::fprintf (stderr, "SELF-CHECK FAILED: %s\n", line.toRawUTF8());
}
