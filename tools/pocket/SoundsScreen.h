#pragma once

#include "PocketAudio.h"
#include "PocketHost.h"
#include "PocketLibrary.h"
#include "PocketTheme.h"
#include "core/TempoFilter.h"

class SoundsScreen : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener, private juce::Timer
{
public:
    SoundsScreen (PocketHost&, PocketAudio&, pocket::Library&);
    ~SoundsScreen() override;

    void selectFirst();
    void paint (juce::Graphics&) override;
    void resized() override;

    // For tests.
    void setTempo (const crate::Tempo::Filter& f) { tempo = f; nearSong = false; rebuild(); }
    int numShown() const { return (int) shown.size(); }
    const pocket::SoundItem* shownAt (int i) const { return juce::isPositiveAndBelow (i, (int) shown.size()) ? &shown[(size_t) i] : nullptr; }

private:
    enum class Filter { all, recorded, imported, fromMac };

    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
    juce::Component* refreshComponentForRow (int, bool, juce::Component*) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override { rebuild(); }
    void timerCallback() override;

    struct Row;
    void rebuild();
    const pocket::SoundItem* selected() const;
    void select (const juce::String& id);
    void togglePlay (const juce::String& id);
    void showMenu (const juce::String& id, juce::Component* from);
    void rename (const juce::String& id);
    void confirmDelete (const juce::String& id);
    void share (const juce::String& id);
    void import();
    void showTempoMenu();
    void askTempo();
    void tempoChanged();
    static juce::String dateText (juce::Time);
    static juce::String lengthText (double);

    PocketHost& host;
    PocketAudio& audio;
    pocket::Library& library;

    juce::TextEditor search;
    pocket::PButton importButton { "Import" }, bpmButton { "BPM" };
    pocket::PButton pills[4];
    pocket::PButton sendToMac { "Send to mac", pocket::Look::amber }, toPad { "To pad", pocket::Look::dark },
        edit { "Edit", pocket::Look::dark }, shareButton { {}, pocket::Look::dark };
    juce::ListBox list { "sounds", this };
    Filter filter = Filter::all;
    crate::Tempo::Filter tempo;     // the tempo picked from the BPM menu
    bool nearSong = false, halfDouble = false;
    std::vector<pocket::SoundItem> shown;
    juce::String selectedId, playingId;
    juce::Rectangle<float> barArea, searchBox;
    std::unique_ptr<juce::FileChooser> chooser;
   #if JUCE_IOS || JUCE_ANDROID
    juce::ScopedMessageBox sharing;
   #endif
};
