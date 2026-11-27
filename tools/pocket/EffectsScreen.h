#pragma once

#include "PocketBoards.h"
#include "PocketRoute.h"
#include "PocketTheme.h"

// The EFFECTS tool: the Mac's Effects screen in a phone's height. From the top:
//  - ON, two rows, nothing hidden: INSTRUMENTS (DRUMS, PADS, BASS, SYNTH, CHORDS, VOICE, VOCODER, DRONE, each an icon
//    chip) and LOOPS (layers 1-8, ALL, MIX); a layer without a loop is dim and toasts when tapped. A dot on a chip
//    says its board is in use.
//  - VOICE only: the slim mic banner (the same rules as the VOCODER tool) and the TUNER.
//  - The board's display: its name, < > through the ready-made boards that suit the sound (tap the name for the
//    list, with your own saved boards, which can be deleted there), SAVE (name it) and BYPASS (the whole board).
//  - The pedals in rows of five, lit in their colour when on: the stomp dot switches one, the rest of the tile
//    picks it for editing. GAIN IN in the spare cells.
//  - The picked pedal: its name and state, its mode or tempo-division chips, up to four big knobs.
//  - FX PAD (the picked pedal's two main knobs on an XY pad, back where they were when you let go unless HOLD) and
//    COPY TO (this board onto another sound).
// It only talks to the boards through a small Model, so it could sit in a sheet over any tool later.
class EffectsScreen : public juce::Component, private juce::Timer
{
public:
    struct Model
    {
        std::function<pocket::Board& (int boardId)> board;
        std::function<bool (int boardId)> shown;          // a LAYER chip only for a layer that holds a loop
        std::function<void (int boardId)> picked;         // a sound was picked (VOICE: the app asks for the mic)
        std::function<void (int boardId)> changed;        // a board's settings changed (saved with the song)
        std::function<void (const juce::String&)> toast;
        std::function<juce::File()> ownFolder;            // your saved boards
        // VOICE: the mic banner
        std::function<bool()> micOn, hearing;
        std::function<pocket::Route()> route;
        std::function<void()> micButton, hearButton;
    };

    explicit EffectsScreen (Model);
    ~EffectsScreen() override;

    void setSource (int boardId);
    int source() const { return src; }
    void selectPedal (int pedal);   // a pedalboard::PedalId, or pocket::kGainIn
    int selectedPedal() const { return sel; }
    juce::Rectangle<float> chipRect (int boardId) const;   // a sound's chip (empty if it has none)
    void tapChip (int boardId) { press (chipBase + boardId); }   // as a tap on it (checks)
    void showList (bool);
    bool listShown() const { return mode == listMode; }
    void showPad (bool);
    bool padShown() const { return mode == padMode; }
    void setPadHold (bool h) { hold = h; repaint(); }
    void showTuner (bool);
    bool tunerShown() const { return tuner; }
    void pollTuner() { timerCallback(); }   // a reading now (screenshots, checks)
    void refresh();                  // the boards changed from outside (a song opened, a layer cleared)
    void stepBoard (int delta);      // < >
    void toggleBypass();
    void stomp (int pedal);          // the stomp dot
    void saveBoardAs (const juce::String& name);
    // The FX PAD by hand (screenshots, checks): x, y 0..1 from the bottom left
    void padTouch (float x, float y, bool down);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void visibilityChanged() override;

private:
    enum Mode { boardMode, listMode, padMode };
    enum Id
    {
        none = 0, lcdName = 10, prevId, nextId, saveId, bypassId, micId, hearId, tunerId, fxPadId, copyId,
        padHoldId, padDoneId, padAreaId, listDoneId, headerId,
        chipBase = 100, tileBase = 200, dotBase = 230, gainId = 260, knobBase = 300, optBase = 400,
        rowBase = 1000, deleteBase = 2000
    };

    void timerCallback() override;
    void layout();
    int idAt (juce::Point<float>) const;
    void press (int id);
    pocket::Board& board() const { return model.board (src); }
    const StripFx& view() const;                         // the board's rack, or a fresh one to read the starting values
    juce::RangedAudioParameter* param (const char* id) const;
    float norm (const char* id) const;
    void setNorm (const char* id, float n);
    void edited();
    std::vector<int> sources() const;
    bool shownLayer (int boardId) const;   // a layer's loop is there (any other board: true)
    void reloadChoices();
    void openCopyMenu();
    void deleteChoice (int index);
    void askSaveName();
    juce::String boardDisplayName() const;
    juce::String hintText() const;
    bool isOn (int pedal) const;
    juce::Colour pedalColour (int pedal) const;

    void drawChips (juce::Graphics&);
    void drawBanner (juce::Graphics&);
    void drawLcd (juce::Graphics&);
    void drawGrid (juce::Graphics&);
    void drawPanel (juce::Graphics&);
    void drawKnob (juce::Graphics&, juce::Rectangle<float>, const pocket::PedalKnob&, juce::Colour);
    void drawTuner (juce::Graphics&, juce::Rectangle<float>);
    void drawList (juce::Graphics&);
    void drawPad (juce::Graphics&);
    void drawButton (juce::Graphics&, juce::Rectangle<float>, const juce::String&, int id, bool lit = false, float font = 15.0f);

    Model model;
    int src = pocket::boardSynth, sel = pedalboard::delay;
    Mode mode = boardMode;
    bool hold = false, tuner = false;
    int pressed = none;
    std::vector<pocket::BoardChoice> choices;
    int lastChoice = 0;
    mutable std::unique_ptr<StripFx> fresh;
    std::array<std::array<std::array<float, 2>, pedalboard::numPedals>, pocket::kBoards> kept {};

    // the layout
    juce::Rectangle<float> chipsR, bannerR, lcdR, saveR, bypassR, middleR, gridR, panelR, bottomR, padR, copyR, hintR;
    juce::Rectangle<float> micR, hearR, tunerR;
    std::vector<std::pair<int, juce::Rectangle<float>>> chipRects;   // board id, rect
    std::array<juce::Rectangle<float>, pocket::kGridPedals> tiles;
    juce::Rectangle<float> gainTile;
    std::vector<juce::Rectangle<float>> knobRects;
    std::vector<std::pair<int, juce::Rectangle<float>>> optRects;    // optBase id, rect
    juce::Rectangle<float> padArea, padHoldR, padDoneR, listDoneR;
    float listScroll = 0.0f;

    // gestures
    int dragKnob = -1;
    float dragStartValue = 0.0f;
    bool draggingList = false, moved = false;
    float dragStartScroll = 0.0f;
    // the pad: the board as it was when the finger went down
    bool padDown = false;
    juce::var padSaved;
    juce::String padSavedName;
    juce::uint32 padSavedHash = 0;
    juce::Point<float> padDot { 0.5f, 0.5f };

    // the tuner
    std::vector<float> tunerBuffer;
    float tunerHz = 0.0f;
    juce::uint32 shownState = 0;   // the chips' lamps and layers last drawn
};
