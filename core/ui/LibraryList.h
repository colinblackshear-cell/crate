#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "../SoundLibrary.h"

namespace crate
{

// The shared sound library as a searchable list. Click a sound to open it, drag it somewhere,
// or right-click for rename / show in Finder / move to Trash.
class LibraryList : public juce::Component,
                    private juce::ListBoxModel,
                    private juce::ChangeListener
{
public:
    explicit LibraryList (SoundLibrary&);
    ~LibraryList() override;

    std::function<void (const SoundLibrary::Entry&)> onOpen;

    void resized() override;

private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { filter(); }
    void filter();
    void showMenu (int row);

    SoundLibrary& library;
    std::vector<SoundLibrary::Entry> shown;
    juce::Label title { {}, "Sound library" };
    juce::TextEditor search;
    juce::ListBox list { "Sounds", this };
    juce::TextButton folderButton { "Folder" }, refreshButton { "Refresh" };
    juce::Label emptyNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryList)
};

} // namespace crate
