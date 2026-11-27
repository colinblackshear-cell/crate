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
    enum class Icon { none, play, stop, rec, back, forward, menu };

    explicit PanelButton (const juce::String& text = {});
    void paintButton (juce::Graphics&, bool over, bool down) override;

    juce::String text, sub, key;
    Icon icon = Icon::none;
    vintage::Lit lit = vintage::Lit::none;
    float fontSize = 18.0f, subSize = 12.0f;
    bool inWell = false, empty = false, redRing = false;
    // Flat: no face, just the words on the panel (the song's name in the top bar), with a dot
    // after them when `dot` (not saved).
    bool flat = false, dot = false;
};

//==============================================================================
// The top bar, as on Kaleidobox Pocket: the menu, your song's name, and one LCD that says where
// the song is and where your next take goes ("BAR 2.3 > LAYER 6 · 4 BARS"), with the master VU
// meters and what's plugged in. The transport and Rec are along the bottom (the Dock).
class Header : public juce::Component,
               public juce::SettableTooltipClient
{
public:
    explicit Header (bool standalone);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // The menu (three lines, like the phone's): your song, Settings, the Guide...
    PanelButton menu { "Menu" };
    // Your show: its name, whether it's saved, and its menu (New song, Open, Save, Save as, Export
    // for Logic, the Guide).
    PanelButton show { "Unsaved show" };
    // Shown in the LCD only while the song decides what plays: hands control back.
    PanelButton jam { "Jam" };
    // Kaleidobox Visuals: the light show (its menu: where it shows, the looks, the colours). F8 turns it on and off.
    PanelButton visuals { "Visuals" };
    std::function<void()> onMic;
    std::function<void()> onKeyboard;   // a click on the keyboard lamp: MIDI controllers (in the app)
    std::function<void()> onWordmark;   // a click on "Kaleidobox": the Guide
    std::function<void()> onRecording;  // a click on the red "recording" line: go to what records

    // What it shows; RigView sets these on its timer, then calls update().
    struct Shown
    {
        juce::String partLetter, partName, barBeat;
        bool partQueued = false;
        // Where Rec lands next ("LAYER 6 · 4 BARS"), and a count-in under way ("COUNT-IN 3 OF 4").
        juce::String target, countIn;
        float vuLeft = 0, vuRight = 0;
        juce::String keyboard;      // "Launchkey 61", "No keyboard", "MIDI from Logic"
        bool keyboardFound = false, keyboardActive = false;
        bool macKeys = true;
        bool micOn = false;
        // What's recording right now ("Overdub loop 3", "Rec MPC beat"...), empty when nothing:
        // shown in red in the LCD, so nothing records without you seeing it.
        juce::String recording;
        // The song is in charge (song mode): the LCD says so, with Jam to take over.
        bool songOn = false;
        bool operator== (const Shown&) const;
    } shown;
    void update (const Shown&);

private:
    const bool standalone;
    juce::Rectangle<int> wordmarkArea, partArea, vuArea, lampsArea, micLampArea, keyboardLampArea;
    bool showVu = true, showWordmark = true;
    int dividerX = -1;
};

//==============================================================================
// Along the bottom of every screen, as the phone's thumb bar: the tempo (click to tap, drag or
// scroll to change, the arrows for a tenth with Shift), the click, the count-in and length for
// the next take, one big Rec (its arrow: where Rec lands, and the rest of the record options),
// Play, Undo last take, To looper, then Rec moves (and Automation) and Quantize.
class Dock : public juce::Component
{
public:
    Dock();
    void paint (juce::Graphics&) override;
    void resized() override;

    // The tempo box: an LCD of the BPM. A click taps the tempo, a drag or the wheel changes it,
    // a right-click (or Ctrl) opens Ramp to...
    class TempoBox : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        double bpm = 120, rampTo = 0;
        std::function<void()> onTap, onMenu;
        std::function<void (double delta)> onNudge;
    private:
        int dragStartY = 0, dragSteps = 0;
        bool dragged = false;
    };

    // The big Rec: a red pill saying where the take goes, with a small arrow on its right end for
    // the options. `arrowHit` says which part the last click was on.
    class RecButton : public juce::Button
    {
    public:
        RecButton() : juce::Button ("Rec") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::Rectangle<int> arrowArea() const;
        juce::String text { "Rec" }, sub;
        int state = 0;   // 0 ready, 1 armed or counting in, 2 recording
        bool arrowHit = false;
    };

    TempoBox tempo;
    PanelButton up { "+" }, down { juce::String::fromUTF8 ("\xe2\x88\x92") };
    PanelButton click { "Click" }, countIn { "Count-in" }, length { "Length" };
    RecButton rec;
    PanelButton play { "Play" }, undo { "Undo" };
    // TO LOOPER, as on the phone: the picked instrument's part onto the next empty loop layer, then
    // that instrument off so it doesn't play twice (hidden for tracks that have no part to send).
    PanelButton toLooper { "To looper" };
    PanelButton recMoves { "Rec moves" }, moves { juce::String::fromUTF8 ("Automation \xe2\x80\xba") }, quantize { "Quantize" };

    // The song's sections on the right, as on the phone (keys 1-8 play them): a click plays one from
    // the next bar, a right-click its menu, + keeps what's playing as the next one.
    void setSections (int count, int current, int queued);
    juce::OwnedArray<PanelButton> sections;
    PanelButton addSection { "+" };
    std::function<void (int section)> onSection;
    std::function<void (int section, juce::Component&)> onSectionMenu;
    std::function<void()> onAddSection;

    static constexpr int kHeight = 96, kCompactHeight = 84;

private:
    int sectionCount = 0;
    juce::Rectangle<int> sectionsLabel;
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
// The tools, down the left of every view, in the order of Kaleidobox Pocket's tool row: Loops,
// Drums, Pads (the MPC), Sampler, Bass, Synth, Chords, Vocoder, Line in, Drone, Effects. The
// instruments are the Groove's track cards, each with what it's playing, its own Play, Mute and
// Solo; picking one makes the keys and knobs play it, Rec records it, and Edit shows it in full.
// Chords and Effects are tools too (a click opens them).
class TrackRail : public juce::Component
{
public:
    TrackRail();
    // The cards and tool buttons, top to bottom.
    void setCards (const juce::Array<juce::Component*>&);
    void paint (juce::Graphics&) override;
    void resized() override;

    // Chords and Effects, as tools on the rail.
    class ToolButton : public PanelButton
    {
    public:
        ToolButton (const juce::String& name, int icon, juce::Colour);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        const int icon;
        const juce::Colour colour;
        juce::String status { "Ready" };
        bool selected = false;
    };
    ToolButton chords, effects;

    static constexpr int kWidth = 280;

private:
    juce::Array<juce::Component::SafePointer<juce::Component>> cards;
    int labelHeight = 0;
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

    // A section's menu (Play from here, Rename...), by `target` (its key, or the bottom bar's chip).
    void showPartMenu (int part, juce::Component* target = nullptr);

private:
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

namespace rigpanels
{
//==============================================================================
// The Rec arrow's panel (cream, as in the design and on the phone's Rec setup): the count-in, the
// length and the click for the next take; then where Rec lands on the Mac (the next loop layer, as on
// the phone; a take on the song; into the Sampler or an MPC pad; the whole set to stems); then To
// Song, Rec into, Takes and the automation lanes. A click outside closes it.
class RecSetup : public juce::Component
{
public:
    RecSetup();
    void paint (juce::Graphics&) override;
    void resized() override;

    // What it shows. lands: 0 layer, 1 song, 2 sample, 3 set, 4 MPC pattern; tool mode has no "where".
    // singFree: My voice is Sing free (no click; fitted to the beat after) rather than To a click.
    void setState (int countIn, int length, bool click, int lands, bool toolMode, bool singFree = false);
    int preferredHeight() const { return whereShown ? 682 : 308; }
    static constexpr int kWidth = 640;

    std::function<void (int bars)> onCountIn, onLength;
    std::function<void()> onClick;
    std::function<void (bool singFree)> onSingFree;
    std::function<void (int lands)> onLands;
    // 0 To Song, 1 Rec into, 2 Takes, 3 Automation lanes (each opens its menu by `from`).
    std::function<void (int extra, juce::Component& from)> onExtra;

    juce::OwnedArray<PanelButton> countIns, lengths, extras;
    PanelButton click { "Click" };
    // My voice, as on the phone: To a click, or Sing free.
    PanelButton toAClick { "To a click" }, singFreeButton { "Sing free" };
    // A "where Rec lands" choice: a radio, its name and what it does.
    class LandRow : public juce::Button
    {
    public:
        LandRow (const juce::String& t, const juce::String& s) : juce::Button (t), title (t), sub (s)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        void paintButton (juce::Graphics&, bool over, bool down) override;
        juce::String title, sub;
        bool on = false;
    };
    juce::OwnedArray<LandRow> lands;

private:
    bool whereShown = true;
    juce::Rectangle<int> countInLabel, lengthLabel, voiceLabel, whereLabel, extrasLabel, topLabel;
};
}
