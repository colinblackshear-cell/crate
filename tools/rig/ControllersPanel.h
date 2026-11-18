#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Vintage.h"
#include "core/control/Controllers.h"

// Settings > Audio & MIDI > MIDI controllers (and a click on the keyboard lamp at the top right):
// every MIDI controller plugged in, each with what it is (a Launchkey, a keyboard with drum
// pads, a keyboard, a pad controller, or not listened to), where its pads start (Learn pad 1),
// how a pad controller's pads are laid out, an octave for its keys, and its eight knobs (Learn
// knobs). A lamp lights when a controller sends anything, so you can tell which is which.
// A controller plugged in for the first time opens it once, on that controller.
class ControllersPanel : public juce::Component,
                         private juce::Timer
{
public:
    // `listDevices` gives the names of the MIDI inputs plugged in now. `onChanged` runs after a
    // controller is switched (so the app can open or close it).
    ControllersPanel (crate::controllers::Setup&, std::function<juce::StringArray()> listDevices,
                      std::function<void()> onChanged = {});
    ~ControllersPanel() override;

    // A controller just plugged in: says so at the top and puts it first.
    void setNewDevice (const juce::String& name);

    void paint (juce::Graphics&) override;
    void resized() override;

    class Row;
    Row* getRow (const juce::String& name) const;
    int getNumRows() const { return rows.size(); }
    juce::TextButton& getDoneButton() { return done; }
    std::function<void()> onDone;

    // Re-reads the devices plugged in and their profiles.
    void rebuild();

    class Row : public juce::Component
    {
    public:
        Row (ControllersPanel&, const juce::String& name);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void tick();
        const juce::String name;
        juce::ComboBox kind, layout, octave;
        juce::TextButton learnPad { "Learn pad 1" }, learnKnobs { "Learn knobs" }, reset { "Defaults" };
        juce::Label padsInfo, knobsInfo, status;
        bool lit = false;
        static int heightFor (const crate::controllers::Profile&);

    private:
        ControllersPanel& panel;
        crate::controllers::Translator& translator;
        void apply (const crate::controllers::Profile&);
        juce::Rectangle<int> lampArea, nameArea, hintArea, padsLabel, keysLabel, knobsLabel;
    };

private:
    void timerCallback() override;
    crate::controllers::Setup& setup;
    std::function<juce::StringArray()> listDevices;
    std::function<void()> onChanged;
    vintage::LookAndFeel look;
    juce::Viewport viewport;
    juce::Component list;
    juce::OwnedArray<Row> rows;
    juce::StringArray shownDevices;
    juce::String newDevice;
    juce::TextButton done { "Done" };
    juce::Rectangle<int> titleArea, footerArea;
    int checkTicks = 0;
    void layoutList();
    friend class Row;
};
