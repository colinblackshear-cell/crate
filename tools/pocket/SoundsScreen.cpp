#include "SoundsScreen.h"

using namespace pocket;

juce::String SoundsScreen::lengthText (double s)
{
    const int total = (int) std::round (s);
    return juce::String (total / 60) + ":" + juce::String (total % 60).paddedLeft ('0', 2);
}

juce::String SoundsScreen::dateText (juce::Time t)
{
    auto now = juce::Time::getCurrentTime();
    auto startOfDay = [] (juce::Time x) { return juce::Time (x.getYear(), x.getMonth(), x.getDayOfMonth(), 0, 0); };
    const auto days = (startOfDay (now).toMilliseconds() - startOfDay (t).toMilliseconds()) / 86400000;
    if (days <= 0)
        return "today";
    if (days == 1)
        return "yesterday";
    return juce::Time::getMonthName (t.getMonth(), true) + " " + juce::String (t.getDayOfMonth());
}

//==============================================================================
struct SoundsScreen::Row : juce::Component
{
    Row (SoundsScreen& o) : owner (o) {}

    void set (const SoundItem& s, bool isSelected, bool isPlaying)
    {
        item = s;
        selected = isSelected;
        playing = isPlaying;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (selected)
        {
            g.setColour (col::creamLight);
            g.fillRoundedRectangle (r.reduced (8, 1), 14.0f);
        }
        else
        {
            g.setColour (col::rule);
            g.fillRect (r.getX() + 16, r.getBottom() - 1, r.getWidth() - 32, 1.0f);
        }

        auto play = playButton();
        drawRaised (g, play, selected ? Look::green : Look::dark, false, 10.0f);
        auto pb = play.withTrimmedBottom (3);
        g.setColour (selected ? col::onBright : juce::Colour (0xffa877f5));
        juce::Path p;
        if (playing)
            p.addRoundedRectangle (pb.getCentreX() - 7, pb.getCentreY() - 7, 14.0f, 14.0f, 2.0f);
        else
            p.addTriangle (pb.getCentreX() - 6, pb.getCentreY() - 8, pb.getCentreX() - 6, pb.getCentreY() + 8, pb.getCentreX() + 8, pb.getCentreY());
        g.fillPath (p);

        const float x = play.getRight() + 14;
        g.setColour (col::ink);
        g.setFont (spaced (condensed (20.0f), 0.02f));
        g.drawText (item.name, juce::Rectangle<float> (x, 4, r.getWidth() - x - 100, 26), juce::Justification::centredLeft, true);

        juce::Colour tagColour = item.tag == kTagRecorded ? col::red : item.tag == kTagImported ? col::blue : juce::Colour (0xff2fc6a8);
        const juce::String tag = item.tag;
        g.setFont (spaced (condensed (11.0f), 0.1f));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), tag) + 18;
        juce::Rectangle<float> pill (x, 31, tw, 18);
        g.setColour (tagColour);
        g.fillRoundedRectangle (pill, 9.0f);
        g.setColour (col::onBright);
        g.drawText (tag, pill, juce::Justification::centred);
        g.setColour (col::inkDim);
        g.setFont (small (14.0f));
        g.drawText (dateText (item.date), juce::Rectangle<float> (pill.getRight() + 8, 31, 110, 18), juce::Justification::centredLeft);

        g.setColour (col::ink);
        g.setFont (lcdFont (25.0f));
        g.drawText (lengthText (item.seconds), juce::Rectangle<float> (r.getWidth() - 112, 0, 62, r.getHeight()), juce::Justification::centredRight);
        g.setColour (col::inkDim);
        for (int i = -1; i <= 1; ++i)
            g.fillEllipse (r.getWidth() - 30 + (float) i * 7 - 2, r.getCentreY() - 2, 4, 4);
    }

    juce::Rectangle<float> playButton() const { return juce::Rectangle<float> (16, 5, 46, 46); }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() > 6)
            return;
        auto pos = e.position;
        if (pos.x < 74)
        {
            owner.select (item.id);
            owner.togglePlay (item.id);
        }
        else if (pos.x > (float) getWidth() - 56)
            owner.showMenu (item.id, this);
        else
            owner.select (item.id);
    }

    SoundsScreen& owner;
    SoundItem item;
    bool selected = false, playing = false;
};

//==============================================================================
SoundsScreen::SoundsScreen (PocketHost& h, PocketAudio& a, Library& lib) : host (h), audio (a), library (lib)
{
    search.setFont (small (18.0f));
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    search.setColour (juce::TextEditor::textColourId, col::ink);
    search.setColour (juce::CaretComponent::caretColourId, col::amberDark);
    search.setIndents (0, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.setTextToShowWhenEmpty ("Search sounds", col::inkDim);
    search.setReturnKeyStartsNewLine (false);
    search.onReturnKey = [this] { search.giveAwayKeyboardFocus(); };
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    importButton.setFontHeight (17.0f);
    importButton.onClick = [this] { import(); };
    addAndMakeVisible (importButton);

    for (int i = 0; i < 4; ++i)
    {
        pills[i].setFontHeight (16.0f);
        pills[i].setPaintInset (5.0f);
        pills[i].onClick = [this, i] { filter = (Filter) i; rebuild(); };
        addAndMakeVisible (pills[i]);
    }

    list.setRowHeight (56);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.getViewport()->setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, col::inkFaint);
    addAndMakeVisible (list);

    sendToMac.setFontHeight (17.0f);
    sendToMac.onClick = [this] { if (auto* s = selected()) host.sendSoundToMac (s->id); else host.toast ("Pick a sound first"); };
    toPad.setFontHeight (17.0f);
    toPad.onClick = [this] { if (auto* s = selected()) host.sendToPad (s->id); else host.toast ("Pick a sound first"); };
    edit.setFontHeight (17.0f);
    edit.onClick = [this]
    {
        if (auto* s = selected())
            host.editSound (s->id);
        else
            host.toast ("Pick a sound first");
    };
    shareButton.glyph = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour)
    {
        g.setColour (juce::Colour (0xfff3eadb));
        auto c = r.getCentre();
        g.drawLine (c.x, c.y - 9, c.x, c.y + 4, 2.2f);
        juce::Path head;
        head.addTriangle (c.x - 6, c.y - 5, c.x + 6, c.y - 5, c.x, c.y - 13);
        g.fillPath (head);
        juce::Path box;
        box.startNewSubPath (c.x - 8, c.y - 1);
        box.lineTo (c.x - 8, c.y + 9);
        box.lineTo (c.x + 8, c.y + 9);
        box.lineTo (c.x + 8, c.y - 1);
        g.strokePath (box, juce::PathStrokeType (2.0f));
    };
    shareButton.onClick = [this] { if (auto* s = selected()) share (s->id); else host.toast ("Pick a sound first"); };
    for (auto* b : { &sendToMac, &toPad, &edit, &shareButton })
        addAndMakeVisible (b);

    library.addChangeListener (this);
    rebuild();
    startTimerHz (10);
}

SoundsScreen::~SoundsScreen()
{
    library.removeChangeListener (this);
    stopTimer();
}

const SoundItem* SoundsScreen::selected() const
{
    return library.find (selectedId);
}

void SoundsScreen::selectFirst()
{
    if (! shown.empty())
        select (shown.front().id);
}

void SoundsScreen::select (const juce::String& id)
{
    selectedId = id;
    list.updateContent();
    list.repaint();
    repaint();
}

void SoundsScreen::rebuild()
{
    int counts[4] = { 0, 0, 0, 0 };
    shown.clear();
    const auto query = search.getText().trim();
    for (auto& s : library.items())
    {
        ++counts[0];
        const int k = s.tag == kTagRecorded ? 1 : s.tag == kTagImported ? 2 : 3;
        ++counts[k];
        if ((filter == Filter::all || (int) filter == k) && (query.isEmpty() || s.name.containsIgnoreCase (query)))
            shown.push_back (s);
    }
    pills[0].setText ("All " + juce::String (counts[0]));
    pills[1].setText ("Recorded");
    pills[2].setText ("Imported");
    pills[3].setText ("From Mac");
    for (int i = 0; i < 4; ++i)
        pills[i].setLook ((int) filter == i ? Look::amber : Look::ghost);

    if (library.find (selectedId) == nullptr)
        selectedId = shown.empty() ? juce::String() : shown.front().id;
    list.updateContent();
    list.repaint();
    repaint();
}

juce::Component* SoundsScreen::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    if (row < 0 || row >= (int) shown.size())
    {
        delete existing;
        return nullptr;
    }
    auto* r = dynamic_cast<Row*> (existing);
    if (r == nullptr)
    {
        delete existing;
        r = new Row (*this);
    }
    r->set (shown[(size_t) row], shown[(size_t) row].id == selectedId, shown[(size_t) row].id == playingId && audio.isPlaying());
    return r;
}

void SoundsScreen::timerCallback()
{
    if (playingId.isNotEmpty() && ! audio.isPlaying())
    {
        playingId = {};
        list.updateContent();
    }
}

void SoundsScreen::togglePlay (const juce::String& id)
{
    if (playingId == id && audio.isPlaying())
    {
        audio.stopPlaying();
        playingId = {};
        list.updateContent();
        return;
    }
    auto a = library.load (id);
    if (a == nullptr)
        return host.toast ("That sound couldn't be opened");
    if (! audio.isRunning())
        host.toast ("The speaker isn't ready");
    audio.play (a, 0, a->getNumSamples());
    playingId = id;
    list.updateContent();
}

void SoundsScreen::showMenu (const juce::String& id, juce::Component* from)
{
    juce::PopupMenu m;
    m.addItem (1, "Rename");
    m.addItem (2, "Share");
    m.addItem (3, "Delete");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (from).withMinimumWidth (140),
                     [this, id] (int r)
    {
        if (r == 1) rename (id);
        else if (r == 2) share (id);
        else if (r == 3) confirmDelete (id);
    });
}

void SoundsScreen::rename (const juce::String& id)
{
    auto* item = library.find (id);
    if (item == nullptr)
        return;
    auto* w = new juce::AlertWindow ("Rename sound", "", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", item->name);
    w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, id, w] (int result)
    {
        if (result == 1)
            library.rename (id, w->getTextEditorContents ("name"));
    }), true);
}

void SoundsScreen::confirmDelete (const juce::String& id)
{
    auto* item = library.find (id);
    if (item == nullptr)
        return;
    auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete sound",
                                                               "Delete \"" + item->name + "\"? This can't be undone.", "Delete", "Cancel", this);
    juce::AlertWindow::showAsync (options, [this, id] (int r)
    {
        if (r != 1)
            return;
        if (playingId == id)
            audio.stopPlaying();
        library.remove (id);
        host.toast ("Deleted");
    });
}

void SoundsScreen::share (const juce::String& id)
{
    auto f = library.fileFor (id);
    if (! f.existsAsFile())
        return host.toast ("That sound couldn't be found");
   #if JUCE_IOS || JUCE_ANDROID
    sharing = juce::ContentSharer::shareFilesScoped ({ juce::URL (f) }, [this] (bool ok, const juce::String& error)
    {
        if (! ok && error.isNotEmpty())
            host.toast ("Couldn't share: " + error);
    }, getTopLevelComponent());
   #else
    host.toast ("Sharing works on the phone. This file: " + f.getFullPathName());
   #endif
}

void SoundsScreen::import()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose sounds to import", juce::File(), "*.wav;*.aif;*.aiff;*.mp3");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
    {
        int ok = 0;
        juce::String error;
        for (auto& f : fc.getResults())
        {
            juce::String e;
            if (library.importFile (f, e).isNotEmpty())
                ++ok;
            else
                error = e;
        }
        if (ok > 0)
            host.toast ("Imported " + juce::String (ok) + (ok == 1 ? " sound" : " sounds"));
        else if (error.isNotEmpty())
            host.toast (error);
    });
}

//==============================================================================
void SoundsScreen::paint (juce::Graphics& g)
{
    g.setColour (col::creamLight);
    g.fillRoundedRectangle (searchBox, 14.0f);
    g.setColour (col::rule.withAlpha (0.8f));
    g.drawRoundedRectangle (searchBox, 14.0f, 1.0f);

    // magnifier
    g.setColour (col::ink);
    auto m = juce::Rectangle<float> (searchBox.getX() + 16, searchBox.getCentreY() - 8, 13, 13);
    g.drawEllipse (m, 2.0f);
    g.drawLine (m.getRight() - 1, m.getBottom() - 1, m.getRight() + 4, m.getBottom() + 4, 2.2f);

    if (shown.empty())
    {
        g.setColour (col::inkDim);
        g.setFont (small (16.0f));
        g.drawFittedText (library.items().empty() ? "No sounds yet. Record one, or tap IMPORT." : "Nothing matches.",
                          list.getBounds().reduced (30), juce::Justification::centred, 3);
    }

    if (auto* s = selected())
    {
        drawWell (g, barArea, 16.0f);
        g.setColour (col::inkFaint);
        g.setFont (spaced (condensedBold (12.0f), 0.1f));
        g.drawText (s->name.toUpperCase() + U8(" · ") + lengthText (s->seconds), barArea.withTrimmedLeft (14).withHeight (22).translated (0, 4),
                    juce::Justification::centredLeft);
    }
}

void SoundsScreen::resized()
{
    const int w = getWidth(), h = getHeight(), pad = 16;
    importButton.setBounds (w - pad - 80, 8, 80, 48);
    searchBox = juce::Rectangle<float> ((float) pad, 8.0f, (float) (w - 2 * pad - 88), 48.0f);
    search.setBounds (searchBox.toNearestInt().withTrimmedLeft (46).withTrimmedRight (12).reduced (0, 8));

    int x = pad;
    const int widths[4] = { 86, 104, 100, 96 };
    for (int i = 0; i < 4; ++i)
    {
        pills[i].setBounds (x - 8, 62, widths[i] - 4, 44);
        x += widths[i];
    }

    const int barH = 90;
    barArea = juce::Rectangle<float> ((float) pad, (float) (h - barH - 6), (float) (w - 2 * pad), (float) barH);
    list.setBounds (0, 110, w, h - 110 - barH - 12);

    auto row = barArea.toNearestInt().withTrimmedTop (30).reduced (8, 4).withHeight (46);
    shareButton.setBounds (row.removeFromRight (50));
    row.removeFromRight (6);
    edit.setBounds (row.removeFromRight (62));
    row.removeFromRight (6);
    toPad.setBounds (row.removeFromRight (74));
    row.removeFromRight (6);
    sendToMac.setBounds (row);
}
