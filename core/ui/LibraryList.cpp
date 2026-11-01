#include "LibraryList.h"

#include "Theme.h"

namespace crate
{

LibraryList::LibraryList (SoundLibrary& lib) : library (lib)
{
    title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    search.setTextToShowWhenEmpty ("Search names and tags", Theme::textDim);
    search.onTextChange = [this] { filter(); };
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (8, 0);
    emptyNote.setColour (juce::Label::textColourId, Theme::textDim);
    emptyNote.setJustificationType (juce::Justification::centredTop);
    emptyNote.setInterceptsMouseClicks (false, false);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, Theme::panel);
    folderButton.setTooltip ("Show the library folder");
    folderButton.onClick = [this] { library.getFolder().revealToUser(); };
    refreshButton.setTooltip ("Look again for sounds added in Finder or by other tools");
    refreshButton.onClick = [this] { library.rescan(); };

    for (auto* c : std::initializer_list<juce::Component*> { &title, &search, &list, &folderButton, &refreshButton, &emptyNote })
        addAndMakeVisible (c);

    library.addChangeListener (this);
    filter();
}

LibraryList::~LibraryList()
{
    library.removeChangeListener (this);
}

void LibraryList::filter()
{
    shown.clear();
    const auto query = search.getText().trim();
    for (const auto& e : library.getEntries())
        if (query.isEmpty() || e.info.name.containsIgnoreCase (query)
            || e.info.tags.joinIntoString (" ").containsIgnoreCase (query))
            shown.push_back (e);
    list.updateContent();
    list.repaint();

    emptyNote.setText (! shown.empty() ? juce::String()
                       : library.getEntries().empty() ? "No sounds yet.\nSounds you save to the library show up here, for every Crate tool."
                                                      : "No sounds match.", juce::dontSendNotification);
}

void LibraryList::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (26);
    refreshButton.setBounds (top.removeFromRight (64).reduced (0, 1));
    top.removeFromRight (4);
    folderButton.setBounds (top.removeFromRight (56).reduced (0, 1));
    title.setBounds (top);
    r.removeFromTop (6);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);
    list.setBounds (r);
    emptyNote.setBounds (r.reduced (8, 16));
}

void LibraryList::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) shown.size())
        return;

    const auto& e = shown[(size_t) row];
    if (selected)
    {
        g.setColour (Theme::raised);
        g.fillRect (0, 0, width, height);
    }

    g.setColour (Theme::text);
    g.setFont (juce::FontOptions (14.0f));
    g.drawText (e.info.name, 10, 3, width - 20, 18, juce::Justification::centredLeft);

    juce::StringArray details;
    details.add (juce::String (e.seconds, e.seconds < 10 ? 2 : 1) + " s");
    details.add (e.channels > 1 ? "stereo" : "mono");
    if (e.info.bpm > 0)
        details.add (juce::String (e.info.bpm, 1) + " BPM");
    if (e.info.rootNote >= 0)
        details.add (juce::MidiMessage::getMidiNoteName (e.info.rootNote, true, true, 3));
    details.addArray (e.info.tags);

    g.setColour (Theme::textDim);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (details.joinIntoString (", "), 10, 21, width - 20, 15, juce::Justification::centredLeft);
}

void LibraryList::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= (int) shown.size())
        return;

    if (e.mods.isPopupMenu())
        showMenu (row);
    else if (onOpen)
        onOpen (shown[(size_t) row]);
}

juce::var LibraryList::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.isEmpty() || rows[0] >= (int) shown.size())
        return {};
    return juce::String (SoundLibrary::dragPrefix) + shown[(size_t) rows[0]].file.getFullPathName();
}

void LibraryList::showMenu (int row)
{
    const auto entry = shown[(size_t) row];
    juce::PopupMenu menu;

    menu.addItem ("Open", [this, entry] { if (onOpen) onOpen (entry); });
    menu.addItem ("Rename...", [this, entry]
    {
        auto* window = new juce::AlertWindow ("Rename sound", {}, juce::MessageBoxIconType::NoIcon, this);
        window->addTextEditor ("name", entry.info.name);
        window->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        window->enterModalState (true, juce::ModalCallbackFunction::create ([this, entry, window] (int result)
        {
            if (result == 1)
                library.rename (entry, window->getTextEditorContents ("name"));
        }), true);
    });
    menu.addItem ("Show in Finder", [entry] { entry.file.revealToUser(); });
    menu.addSeparator();
    menu.addItem ("Move to Trash", [this, entry] { library.remove (entry); });

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
}

} // namespace crate
