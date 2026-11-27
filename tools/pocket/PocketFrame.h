#pragma once

#include "PocketKey.h"
#include "PocketRoute.h"
#include "PocketSheet.h"
#include "PocketTheme.h"

// The frame around the tools: the thumb bar (BPM, CLICK, REC, PLAY, UNDO), the tool row, the count-in
// panel, and the two sheets that go with recording (Rec setup, Headphones on first).
namespace pocket
{
// Every tool the row can show, in the row's order (PocketApp's tools list has them all).
enum class Tool { loops, drums, pads, sampler, synth, chords, vocoder, drone, effects, song, add, bass };   // add: + ADD A LAYER (a sheet, not a screen)
const char* toolName (Tool);
juce::Colour toolColour (Tool);
void drawToolIcon (juce::Graphics&, Tool, juce::Point<float> centre, juce::Colour);

// The song's KEY menu (SYNTH, CHORDS and VOCODER all show it): major / minor, then the twelve roots.
void showSongKeyMenu (juce::Component& target, juce::Rectangle<int> area, SongKey current, std::function<void (SongKey)> picked);
// A chip as the tools draw them: raised, cream or lit, the label fitted in.
void drawChip (juce::Graphics&, juce::Rectangle<float>, const juce::String& text, bool lit, bool pressed, float fontHeight = 14.0f,
               juce::Colour litColour = juce::Colour());

//==============================================================================
class ThumbBar : public juce::Component
{
public:
    enum Button { bpm, click, rec, play, undo, visuals, count };   // visuals: Kaleidobox Visuals on / off

    struct State
    {
        double bpm = 97.0;
        bool click = false, recBusy = false, recCounting = false, playing = false, visuals = false;
        bool undoPass = false;   // looping REC: UNDO takes back the last pass
        bool toLooper = false;   // VISUALS' place shows TO LOOPER: what this tool made can go on a layer
    };
    std::function<State()> state;
    std::function<void (int)> onPress;   // a Button

    juce::Rectangle<float> buttonRect (int) const;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    int buttonAt (juce::Point<float>) const;
    int pressed = -1;
};

//==============================================================================
class ToolRow : public juce::Component
{
public:
    std::vector<Tool> tools;
    int selected = -1;       // index into tools, -1 = none lit
    int bottomInset = 0;
    std::function<void (Tool)> onSelect;
    void flash();   // outline the tools in amber for a moment ("pick another tool"); it ends by the clock, whatever else happens
    bool flashing() const { return juce::Time::getMillisecondCounter() < glowUntil; }
    void stopFlash() { glowUntil = 0; repaint(); }

    // Slide a finger along the row: the tool under it lights up, and it is picked when the finger lifts.
    int cellAt (float x) const;   // the tool under x (the row's ends take the first and last), -1 with no tools
    int slidingTo() const { return sliding; }
    void slideStart (float x);
    void slideTo (float x);
    void slideEnd (float x, float y);   // lifts: picks the tool under x (y well above the row calls it off)

    juce::Rectangle<float> cellRect (int index) const;
    float labelHeight() const;   // the labels' one font size: the longest name fits the cell
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::uint32 glowUntil = 0;
    int sliding = -1;
};

//==============================================================================
// COUNT-IN · THE CLICK IS PLAYING, four big beats, and what happens after.
class CountInPanel : public juce::Component
{
public:
    void set (int beatInBar, int bar, int bars, const juce::String& after);
    void paint (juce::Graphics&) override;

private:
    int beat = 0, barNow = 0, totalBars = 1;
    juce::String afterText;
};

//==============================================================================
// Rec setup (tap the header's display): COUNT-IN and LENGTH, the same for every tool.
class RecSetupSheet : public DragSheet
{
public:
    RecSetupSheet();
    std::function<int()> getCountIn, getLength;      // bars 0..2; choice: an AppSettings length choice
    std::function<void (int)> setCountIn, setLength;
    std::function<int()> getPadsTo;                  // where PADS records: 0 a loop layer, 1 the pad pattern only
    std::function<void (int)> setPadsTo;
    std::function<void()> onDone;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PButton countButtons[3], lengthButtons[8], padsButtons[2], done { "Done", Look::amber };
};

//==============================================================================
// Headphones on first: the mic stays off until you say what you're listening on.
class HeadphonesSheet : public DragSheet
{
public:
    HeadphonesSheet();
    std::function<Route()> route;
    std::function<void()> onHeadphones, onNoHeadphones;

    void refresh() { repaint(); }
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Rectangle<float> cardRect (int i) const;   // 0 wired, 1 Bluetooth, 2 none
    PButton wearing { U8("I'm wearing headphones \xc2\xb7 turn the mic on"), Look::amber },
            without { U8("No headphones \xc2\xb7 record without hearing myself") };
};
}
