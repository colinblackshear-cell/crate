#pragma once

#include "PocketKey.h"
#include "PocketRoute.h"
#include "PocketTheme.h"

// The frame around the tools: the thumb bar (BPM, CLICK, REC, PLAY, UNDO), the tool row, the count-in
// panel, and the two sheets that go with recording (Rec setup, Headphones on first).
namespace pocket
{
// Every tool the row can show, in the row's order. Stage 1 shows the ones that exist (PocketApp's
// tools list); SYNTH and CHORDS slot in between PADS and VOCODER when they come.
enum class Tool { loops, drums, pads, synth, chords, vocoder };
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
    enum Button { bpm, click, rec, play, undo, count };

    struct State
    {
        double bpm = 97.0;
        bool click = false, recBusy = false, recCounting = false, playing = false;
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
    void flash();   // outline the tools in amber for a moment ("pick another tool")

    juce::Rectangle<float> cellRect (int index) const;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    bool glow = false;
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
class RecSetupSheet : public juce::Component
{
public:
    RecSetupSheet();
    std::function<int()> getCountIn, getLength;      // bars 0..2; choice 0..4 (1, 2, 4, 8 bars, FREE)
    std::function<void (int)> setCountIn, setLength;
    std::function<void()> onDone;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    PButton countButtons[3], lengthButtons[5], done { "Done", Look::amber };
};

//==============================================================================
// Headphones on first: the mic stays off until you say what you're listening on.
class HeadphonesSheet : public juce::Component
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
