#pragma once

#include "Vintage.h"
#include "RigEngine.h"

// The Rig's frame, around whichever screen is showing: the top bar (transport, tempo, where the
// song is, the master VU meters, what's plugged in), the screen buttons (F1-F5), and the song
// parts with "Live now" along the bottom.
namespace rigpanels
{
// A chunky panel button: a word (and maybe a small line under it, an icon and a key hint),
// lit in a status colour.
class PanelButton : public juce::Button
{
public:
    enum class Icon { none, play, stop, rec };

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

    // The five screens, as their pages are named.
    static const juce::StringArray& screens();
    static juce::String label (int);
    void setCurrent (const juce::String& page);

    juce::OwnedArray<PanelButton> tabs;
    PanelButton more { "More" };
    PanelButton help { "?" };   // the how-to guide
    std::function<void (const juce::String& page)> onScreen;
};

//==============================================================================
class Footer : public juce::Component
{
public:
    explicit Footer (RigEngine&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    // Re-reads the song parts and the tracks (RigView calls it on its timer).
    void tick (const std::function<float (int strip)>& meterOf);

    // Song parts: a full one plays from the next bar; an empty one saves what's playing.
    static void pressPart (RigEngine&, int part);

    juce::OwnedArray<PanelButton> parts;
    juce::Label message;
    std::function<void (int strip)> onTrack;   // a Live now chip clicked
    std::function<void()> onEditParts;

    // The tracks Live now shows: their mixer strips.
    static constexpr int kChips = 6, kChipWidth = 94;   // six chips fit beside the parts
    static int chipStrip (int chip);
    static juce::String chipName (int chip);

private:
    void showPartMenu (int part);

    RigEngine& engine;
    struct Chip { juce::String text; vintage::Lit lit = vintage::Lit::none; };
    std::array<Chip, kChips> chips;
    std::array<float, kChips> heard {};
    juce::Rectangle<int> partsArea, chipsArea, partsLabel, chipsLabel;
    bool showChips = true;
};
}
