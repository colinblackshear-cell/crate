#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Vintage.h"
#include "core/control/Controllers.h"

// Settings > Audio & MIDI > MIDI controllers (and a click on the keyboard lamp at the top right):
// every MIDI controller plugged in, each with what it is (a Launchkey, a keyboard with drum
// pads, a keyboard, a pad controller, or not listened to), where its pads start (Learn pad 1),
// how a pad controller's pads are laid out, an octave for its keys, and its eight knobs (Learn
// knobs). A lamp lights when a controller sends anything, so you can tell which is which.
// Drum triggers (a Kaleidobox Trigger box, an e-drum kit) get "Hit to learn": hit each drum
// in turn and it takes the next pad (or a loop layer's Rec), then a line per drum to change it,
// each with a lamp that lights on its hits, and how hard hits come out (Feel).
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
        // Drum triggers.
        juce::TextButton learnTriggers { "Hit to learn" };
        juce::ComboBox learnAs, feel;
        class TriggerLine;
        TriggerLine* getTriggerLine (int note) const;
        int getNumTriggerLines() const { return triggerLines.size(); }
        // Forgets a learned drum: it plays the pad its note is again.
        void forgetTrigger (int note);
        bool lit = false;
        static int heightFor (const crate::controllers::Profile&);

    private:
        ControllersPanel& panel;
        crate::controllers::Translator& translator;
        void apply (const crate::controllers::Profile&);
        void rebuildTriggerLines (const crate::controllers::Profile&);
        juce::OwnedArray<TriggerLine> triggerLines;
        juce::Rectangle<int> lampArea, nameArea, hintArea, padsLabel, keysLabel, knobsLabel, triggersLabel, feelLabel;
        friend class TriggerLine;
    };

    // One learned drum trigger: its lamp, its note, what it plays, and Forget.
    class Row::TriggerLine : public juce::Component
    {
    public:
        TriggerLine (Row&, int note);
        void paint (juce::Graphics&) override;
        void resized() override;
        void tick();
        const int note;
        juce::ComboBox target;
        juce::TextButton forget { "Forget" };
        bool lit = false;
    private:
        Row& row;
        juce::Rectangle<int> lampArea, textArea;
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
