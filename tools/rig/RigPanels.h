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
    // Shown in the song's window only while the song decides what plays: hands control back.
    PanelButton jam { "Jam" };
    std::function<void()> onMic;
    std::function<void()> onKeyboard;   // a click on the keyboard lamp: MIDI controllers (in the app)
    std::function<void()> onWordmark;   // a click on "Kaleidobox": the Guide
    std::function<void()> onRecording;  // a click on the red "recording" line: go to what records

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
        // What's recording right now ("Overdub loop 3", "Rec MPC beat"...), empty when nothing:
        // shown in red where the song's place is, so nothing records without you seeing it.
        juce::String recording;
        // The song is in charge (song mode): the window says so, with Jam to take over.
        bool songOn = false;
        bool operator== (const Shown&) const;
    } shown;
    void update (const Shown&);

private:
    const bool standalone;
    juce::Rectangle<int> wordmarkArea, tempoArea, partArea, vuArea, lampsArea, micLampArea, keyboardLampArea;
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
    PanelButton help { "Guide" };   // the walkthrough and the how-tos, always there
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
    PanelButton newPattern { "+ New / start over" };
    // Capture, under the tracks, for the picked one on every view: Rec take records it (as you
    // play it, over the beat) onto the next empty loop from the next bar, until pressed again;
    // To Song puts what it plays (or the take) into the song's sections.
    PanelButton recTake { "Rec take" }, toSong { juce::String::fromUTF8 ("To Song \xe2\x80\xba") };
    // Automation, under those: Rec moves records the knobs you turn (any tool, any effect) from the
    // next bar, until pressed again; Automation opens the lanes (play, draw, clear).
    PanelButton recMoves { "Rec moves" }, moves { juce::String::fromUTF8 ("Automation \xe2\x80\xba") };

    static constexpr int kWidth = 232;

private:
    juce::Array<juce::Component::SafePointer<juce::Component>> cards;
    juce::Rectangle<int> labelArea;
};

//==============================================================================
class Footer : public juce::Component,
               public juce::SettableTooltipClient
{
public:
    explicit Footer (RigEngine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    // The song strip (once the song has sections): the sections as keys as wide as their bars,
    // under them the song's layers as thin lanes with the play head, and over them the Rec into
    // bar: where the next take goes (a red box on the strip), with Rec, Change, Cycle, Takes and
    // Song. RigView fills in recInto on its timer.
    struct RecInto
    {
        juce::String text;              // "B Drop · bars 9-16 · new layer on loop 3"
        int section = -1;               // the section it records into (-1: none)
        int layer = -1;                 // the loop it records onto (-1: none free)
        int colourStrip = -1;           // whose colour the new layer is (a StripId)
        bool waiting = false, recording = false, cycle = false;
        double progress = 0;            // through the section, while recording
        int takes = 0;                  // takes on that layer so far
        bool operator== (const RecInto&) const;
    };
    void setRecInto (const RecInto&);
    const RecInto& getRecInto() const { return recInto; }
    PanelButton recButton { "Rec into" }, changeButton { juce::String::fromUTF8 ("Change \xe2\x96\xbe") }, cycleButton { "Cycle" },
        takesButton { juce::String::fromUTF8 ("Takes \xe2\x96\xbe") }, songButton { juce::String::fromUTF8 ("Song \xe2\x80\xba") };
    std::function<void()> onRecInto, onCycle, onSong;
    std::function<void (juce::Component&)> onChange, onTakes;
    // A section picked as where the next take goes (a click on its stretch of the strip, or its key).
    std::function<void (int section)> onPickSection;
    // Where a bar of the song is across the strip (piecewise, as the keys are laid out).
    float xForBar (double bar) const;
    juce::Rectangle<int> getLanesArea() const { return lanesArea; }

    // Re-reads the song's sections (RigView calls it on its timer).
    void tick();

    // Song parts: a full one plays from the next bar; an empty one saves what's playing.
    static void pressPart (RigEngine&, int part);

    juce::OwnedArray<PanelButton> parts;
    juce::Label message;
    // Off in a one-tool plugin (Kaleidobox Vocoder, MPC...): no sections, just the messages.
    bool partsShown = true;
    // Without parts: takes the footer's area and returns what's left for the messages (a
    // one-tool plugin's drag chips go on the right).
    std::function<juce::Rectangle<int> (juce::Rectangle<int>)> onLayout;
    std::function<void()> onEditParts;
    // No song yet: the bar is one thin row (A, to keep what's playing as the first section, and
    // the messages), so it doesn't take room on every screen. onSlimChanged: lay out again.
    bool isSlim() const { return partsShown && sectionCount == 0; }
    std::function<void()> onSlimChanged;

private:
    void showPartMenu (int part);
    int sectionCount = 0;
    int sectionsHash = 0;
    void paintStrip (juce::Graphics&);
    int sectionAtX (int x) const;

    RigEngine& engine;
    juce::Rectangle<int> partsArea, partsLabel, recRow, recText, keysRow, lanesArea;
    // Each section's stretch across the strip (all of them, also past the 8 keys).
    std::vector<juce::Rectangle<int>> sectionBoxes;
    std::vector<std::pair<double, double>> sectionBars;   // start, bars
    RecInto recInto;
};
}
