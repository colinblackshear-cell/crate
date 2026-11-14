#pragma once

#include "Vintage.h"
#include "RigEngine.h"

// The Rig's frame, around whichever screen is showing: the top bar (your show, transport, tempo,
// where the song is, the master VU meters, what's plugged in), the view buttons (Play, Edit, Song,
// Mix; F1-F4) with Sounds, Settings and the guide, the track rail down the left, and the song's
// sections along the bottom.
namespace rigpanels
{
// A chunky panel button: a word (and maybe a small line under it, an icon and a key hint),
// lit in a status colour.
class PanelButton : public juce::Button
{
public:
    enum class Icon { none, play, stop, rec, back, forward };

    explicit PanelButton (const juce::String& text = {});
    void paintButton (juce::Graphics&, bool over, bool down) override;

    juce::String text, sub, key;
    Icon icon = Icon::none;
    vintage::Lit lit = vintage::Lit::none;
    float fontSize = 18.0f, subSize = 12.0f;
    bool inWell = false, empty = false, redRing = false;
};

//==============================================================================
class Header : public juce::Component,
               public juce::SettableTooltipClient
{
public:
    explicit Header (bool standalone);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    PanelButton play { "Play" }, stop { "Stop" }, rec { "Rec" }, up { "+" }, down { juce::String::fromUTF8 ("\xe2\x88\x92") }, tap { "Tap" };
    // Your show: its name, whether it's saved, and a menu (New song, Open, Save, Save as, Export
    // for Logic, the Guide).
    PanelButton show { "Unsaved show" };
    // Quantize what's on screen (under Tap): opens the Quantize panel. Cmd+U does it straight away.
    PanelButton quantize { "Quantize" };
    std::function<void()> onMic;
    std::function<void()> onWordmark;   // a click on "Kaleidobox": the Guide

    // What it shows; RigView sets these on its timer, then calls update().
    struct Shown
    {
        double bpm = 120;
        juce::String partLetter, partName, barBeat;
        bool partQueued = false;
        float vuLeft = 0, vuRight = 0;
        juce::String keyboard;      // "Launchkey 61", "No keyboard", "MIDI from Logic"
        bool keyboardFound = false, keyboardActive = false;
        bool macKeys = true;
        bool micOn = false;
        bool operator== (const Shown&) const;
    } shown;
    void update (const Shown&);

private:
    const bool standalone;
    juce::Rectangle<int> wordmarkArea, tempoArea, partArea, vuArea, lampsArea, micLampArea;
    bool showVu = true, showWordmark = true;
};

//==============================================================================
class ScreenBar : public juce::Component
{
public:
    ScreenBar();
    void resized() override;

    // The four views (F1-F4): Play (the picked track's steps, patterns and knobs), Edit (that
    // track's instrument in full), Song, and Mix (the mixer and the effects).
    static const juce::StringArray& views();
    // Which view is lit ("" for none), and whether the Sounds drawer and Settings are.
    void setCurrent (const juce::String& view, bool soundsOpen = false, bool settingsOpen = false, bool chordsOpen = false);

    juce::OwnedArray<PanelButton> tabs;
    // Back and forward through the screens you've been on (Cmd+[ and Cmd+]).
    PanelButton back { "Back" }, forward { "Forward" };
    PanelButton sounds { "Sounds" };   // the sound library, as a drawer over any view (F5)
    PanelButton chords { "Chords" };   // the chord builder, as a drawer over any view (F7)
    PanelButton settings { "Settings" };   // keys and pads, split, sampling, knobs, click, audio, look (F6)
    PanelButton help { "?" };   // the how-to guide
    std::function<void (const juce::String& view)> onView;
};

//==============================================================================
// The tracks, down the left of every view: every instrument Kaleidobox has (the Groove's track
// cards), each with what it's playing, its own Play, Mute and Solo. Picking one makes the keys
// and knobs play it, and Edit shows it in full.
class TrackRail : public juce::Component
{
public:
    TrackRail();
    void setCards (const juce::Array<juce::Component*>&);
    void paint (juce::Graphics&) override;
    void resized() override;

    // The next thing in the song, on an empty pattern (a beat, a bass line, keys...).
    PanelButton newPattern { "+ New pattern" };

    static constexpr int kWidth = 232;

private:
    juce::Array<juce::Component::SafePointer<juce::Component>> cards;
    juce::Rectangle<int> labelArea;
};

//==============================================================================
class Footer : public juce::Component
{
public:
    explicit Footer (RigEngine&);
    void paint (juce::Graphics&) override;
    void resized() override;

    // Re-reads the song's sections (RigView calls it on its timer).
    void tick();

    // Song parts: a full one plays from the next bar; an empty one saves what's playing.
    static void pressPart (RigEngine&, int part);

    juce::OwnedArray<PanelButton> parts;
    juce::Label message;
    std::function<void()> onEditParts;

private:
    void showPartMenu (int part);

    RigEngine& engine;
    juce::Rectangle<int> partsArea, partsLabel;
};
}
