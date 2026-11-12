#include "BankBrowser.h"

#include "SampleTools.h"
#include "core/SoundFx.h"
#include "core/SoundLibrary.h"
#include "core/ui/Theme.h"

namespace Theme = crate::Theme;

BankBrowser::BankBrowser (MpcProcessor& p) : processor (p)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    for (auto* c : std::initializer_list<juce::Component*> { &title, &search, &kinds, &length, &list, &onto, &folders, &hear, &count })
        addAndMakeVisible (c);

    search.setTextToShowWhenEmpty ("Search your sounds: \"dusty kick\", \"808\", \"vocal Fmin\"...", Theme::textDim);
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { list.grabKeyboardFocus(); if (list.getSelectedRow() < 0 && ! shown.empty()) list.selectRow (0); };

    kinds.addItem ("Every kind", 1);
    for (int k = 0; k < SoundBank::numKinds; ++k)
        kinds.addItem (SoundBank::kindName (k), k + 2);
    kinds.setSelectedId (1, juce::dontSendNotification);
    kinds.onChange = [this] { kind = kinds.getSelectedId() - 2; filter(); };

    length.addItem ("Hits and loops", 1);
    length.addItem ("One-shots", 2);
    length.addItem ("Loops", 3);
    length.setSelectedId (1, juce::dontSendNotification);
    length.onChange = [this] { filter(); };

    list.setRowHeight (24);
    list.setColour (juce::ListBox::backgroundColourId, Theme::background);
    list.setMultipleSelectionEnabled (false);

    hear.setToggleState (true, juce::dontSendNotification);
    hear.setTooltip ("Hear each sound as you click it (or move with the arrow keys)");
    onto.setTooltip ("Puts the sound on the pad you picked (or double-click it, or press Enter). Drag a sound onto any pad too.");
    onto.setColour (juce::TextButton::buttonColourId, Theme::accent);
    onto.setColour (juce::TextButton::textColourOffId, Theme::background);
    onto.onClick = [this] { putSelectedOnPad(); };
    folders.setTooltip ("The folders the bank looks through: your samples, Splice, and any you add");
    folders.onClick = [this] { addFolder(); };
    count.setColour (juce::Label::textColourId, Theme::textDim);
    count.setJustificationType (juce::Justification::centredRight);
    count.setFont (juce::FontOptions (12.0f));

    startTimerHz (4);
}

BankBrowser::~BankBrowser()
{
    stopTimer();
}

void BankBrowser::setSoundBank (std::shared_ptr<SoundBank> b)
{
    bank = std::move (b);
    lastSize = -1;
    filter();
}

void BankBrowser::timerCallback()
{
    // Opening the bank reads its index, so it waits until the browser is on screen.
    if (bank == nullptr)
    {
        if (! isShowing())
            return;
        bank = std::make_shared<SoundBank>();
        bank->rescan();
    }
    const int size = bank->size();
    const bool scanning = bank->isScanning();
    if (size != lastSize || scanning != lastScanning)
    {
        lastScanning = scanning;
        filter();
    }
}

void BankBrowser::filter()
{
    if (bank == nullptr)
        return;
    lastSize = bank->size();

    SoundBank::Query q;
    q.text = search.getText();
    q.kind = kind;
    q.length = length.getSelectedId() == 2 ? SoundBank::Length::oneShots
             : length.getSelectedId() == 3 ? SoundBank::Length::loops
                                           : SoundBank::Length::any;
    q.limit = 1000;
    shown = bank->search (q);
    list.updateContent();
    list.repaint();

    juce::String text;
    if (bank->size() == 0)
        text = bank->isScanning() ? "Looking through your folders..." : "No sounds yet: add a folder";
    else
        text = juce::String ((int) shown.size()) + (shown.size() >= (size_t) q.limit ? "+" : "") + " of "
               + juce::String (bank->size()) + (bank->isScanning() ? " (still looking)" : "");
    count.setText (text, juce::dontSendNotification);
}

void BankBrowser::setSearch (const juce::String& text)
{
    search.setText (text, false);   // (the editor tells its listeners later, so filter now)
    filter();
}

void BankBrowser::setKind (int k)
{
    kinds.setSelectedId (juce::jlimit (-1, SoundBank::numKinds - 1, k) + 2, juce::sendNotificationSync);
}

void BankBrowser::selectRow (int row)
{
    list.selectRow (row);
}

void BankBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    const auto& s = shown[(size_t) row];
    if (selected)
    {
        g.setColour (Theme::accent.withAlpha (0.3f));
        g.fillRect (0, 0, width, height);
    }

    juce::String detail = SoundBank::kindName (s.kind).dropLastCharacters (s.kind == SoundBank::other || s.kind == SoundBank::fx ? 0 : 1);
    if (s.loop)
        detail << " loop";
    if (s.bpm > 0)
        detail << ", " << juce::String (juce::roundToInt (s.bpm)) << " BPM";
    if (s.key.isNotEmpty())
        detail << ", " << s.key;
    detail << ", " << juce::String (s.seconds, s.seconds < 10 ? 1 : 0) << " s";

    const int detailWidth = juce::jmin (width / 2, 230);
    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (s.name, 8, 0, width - detailWidth - 16, height, juce::Justification::centredLeft, true);
    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (detail, width - detailWidth - 4, 0, detailWidth, height, juce::Justification::centredRight, true);
}

void BankBrowser::selectedRowsChanged (int row)
{
    if (! hear.getToggleState() || ! juce::isPositiveAndBelow (row, (int) shown.size()))
        return;
    juce::String error;
    if (auto audio = SampleTools::load (shown[(size_t) row].file, processor.getFormats(), error))
        processor.preview (std::move (audio), {});
    else
        say ("Couldn't open " + shown[(size_t) row].file.getFileName() + (error.isNotEmpty() ? ": " + error : juce::String()), true);
}

void BankBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    putSelectedOnPad();
}

bool BankBrowser::putSelectedOnPad()
{
    const int row = list.getSelectedRow();
    if (! juce::isPositiveAndBelow (row, (int) shown.size()))
    {
        say ("Pick a sound in the list first.", true);
        return false;
    }
    const auto& sound = shown[(size_t) row];
    juce::String error;
    auto audio = SampleTools::load (sound.file, processor.getFormats(), error);
    if (audio == nullptr)
    {
        say ("Couldn't open " + sound.file.getFileName() + (error.isNotEmpty() ? ": " + error : juce::String()), true);
        return false;
    }

    const int pad = juce::jlimit (0, mpc::kPadCount - 1, targetPad ? targetPad() : 0);
    auto& project = processor.getProject();
    auto& p = project.currentBank().pads[(size_t) pad];
    p = {};
    p.audio = audio;
    p.name = sound.name.substring (0, 16);
    // A loop with its tempo in the name: its length in beats, for Keep in time.
    if (sound.loop && sound.bpm > 0)
        p.beats = juce::jmax (1.0, std::round (audio->getNumSamples() / kMpcSampleRate * sound.bpm / 60.0));
    processor.stopPreview();
    project.changed();
    say (sound.name + " is on pad " + padLabel (pad) + ". Turn its knobs to shape it as it plays.");
    if (onPlaced)
        onPlaced (pad);
    return true;
}

juce::var BankBrowser::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], (int) shown.size()))
        return {};
    return juce::String (SoundLibrary::dragPrefix) + shown[(size_t) rows[0]].file.getFullPathName();
}

bool BankBrowser::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey)
        return putSelectedOnPad();
    return false;
}

void BankBrowser::addFolder()
{
    if (bank == nullptr)
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader ("Folders the bank looks through");
    const auto current = bank->getFolders();
    for (int i = 0; i < current.size(); ++i)
        menu.addItem (100 + i, current[i].getFullPathName() + "   (click to remove)");
    menu.addSeparator();
    menu.addItem (1, "Add a folder...");
    menu.addItem (2, "Look again for new sounds");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&folders),
                        [safe = juce::Component::SafePointer<BankBrowser> (this), current] (int result)
                        {
                            if (safe == nullptr || safe->bank == nullptr)
                                return;
                            if (result == 2)
                                safe->bank->rescan();
                            else if (juce::isPositiveAndBelow (result - 100, current.size()))
                                safe->bank->removeFolder (current[result - 100]);
                            else if (result == 1)
                            {
                                safe->chooser = std::make_unique<juce::FileChooser> ("Add a folder of sounds",
                                                                                     juce::File::getSpecialLocation (juce::File::userMusicDirectory));
                                safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                                            [safe] (const juce::FileChooser& fc)
                                                            {
                                                                if (safe != nullptr && safe->bank != nullptr && fc.getResult().isDirectory())
                                                                    safe->bank->addFolder (fc.getResult());
                                                            });
                            }
                        });
}

void BankBrowser::paint (juce::Graphics& g)
{
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
}

void BankBrowser::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (28);
    title.setBounds (top.removeFromLeft (juce::jmin (96, top.getWidth() / 6)));
    length.setBounds (top.removeFromRight (juce::jmin (126, top.getWidth() / 4)));
    top.removeFromRight (6);
    kinds.setBounds (top.removeFromRight (juce::jmin (120, top.getWidth() / 3)));
    top.removeFromRight (6);
    search.setBounds (top);
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (28);
    hear.setBounds (bottom.removeFromLeft (120));
    folders.setBounds (bottom.removeFromRight (86));
    bottom.removeFromRight (6);
    onto.setBounds (bottom.removeFromRight (90));
    bottom.removeFromRight (8);
    count.setBounds (bottom);
    r.removeFromBottom (6);
    list.setBounds (r);
}
