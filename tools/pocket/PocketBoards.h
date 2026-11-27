#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "rig/Pedalboard.h"
#include "rig/StripFx.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

// The EFFECTS tool's pedalboards: one board per sound (DRUMS, PADS, SYNTH, CHORDS, VOICE, VOCODER, each loop
// layer, ALL LOOPS, MASTER), each the Mac's own StripFx rack (the same pedals, the same code, so a board sounds
// the same on both), here without the plugin slot.
//
// A Board lives inside the looper (the one renderer). Its rack is only made the first time the board is used,
// and the audio thread skips a board while nothing on it changes the sound (once its echoes have died away), so
// an untouched board costs nothing and leaves the sound bit for bit as it was.
namespace pocket
{
enum BoardId
{
    boardDrums, boardPads, boardSynth, boardChords, boardVoice, boardVocoder, boardLayer1,
    boardAll = boardLayer1 + 8, boardMaster,
    boardBass, boardDrone,   // (added later: after MASTER, so the other ids stay as they were)
    kBoards
};
constexpr int kLayerBoards = 8;
inline int layerBoardId (int layer) { return boardLayer1 + layer; }
inline bool isLayerBoard (int id) { return id >= boardLayer1 && id < boardLayer1 + kLayerBoards; }
juce::String boardLabel (int id);   // "DRUMS", "LAYER 3", "ALL", "MASTER"
juce::String boardTitle (int id);   // "Drums", "Layer 3", "All loops", "Master"

// The phone's grid: the Mac board's pedals (pedalboard::pedals()) in the order the sound goes through them.
// The Mac's Mic pedal stays in the rack (and in saved boards) but isn't on the phone's grid.
constexpr int kGridPedals = 13;
int gridPedal (int cell);             // the pedalboard::PedalId at a cell of the grid
constexpr int kGainIn = -1;           // the grid's GAIN IN cell
struct PedalKnob { const char* id; const char* label; };
struct PedalChips { const char* id; std::vector<const char*> labels; };   // a choice setting, as chips
struct PedalPage
{
    std::vector<PedalKnob> knobs;     // the big knobs, at most 4
    std::vector<PedalChips> chips;    // its modes or tempo divisions
};
const PedalPage& pedalPage (int pedal);   // pedalboard::PedalId, or kGainIn
juce::String pedalShortName (int pedal);  // "TREM", "GAIN IN"

// The song's clock, for the pedals that play in time (Delay, Tremolo, Repeat). Written by the audio thread
// before each block, read by the racks.
class BoardClock : public juce::AudioPlayHead
{
public:
    std::atomic<double> bpm { 120.0 }, ppq { 0.0 };
    std::atomic<bool> playing { false };
    juce::Optional<PositionInfo> getPosition() const override;
};

// The settings that differ from a fresh rack, by parameter id ({} when it's all at its start).
juce::var settingsToVar (const StripFx&);
void settingsFromVar (StripFx&, const juce::var&);   // everything else back to its start
bool settingsAreNeutral (const juce::var&);

class Board
{
public:
    Board();

    //==========================================================================
    // Message thread
    StripFx& fx();                       // made, prepared and handed to the audio thread the first time
    const StripFx* existing() const { return owned.get(); }
    bool isNeutral() const;              // every setting at its start (or never made)
    bool audible() const;                // something on it changes the sound
    int pedalsOn() const;
    void reset();
    juce::var toVar() const;             // { name, fx } ({} when neutral)
    void fromVar (const juce::var&);     // {} = neutral
    juce::var settings() const;          // only the pedals' settings (for a take's notes)
    void setSettings (const juce::var&);
    void copyFrom (const Board&);
    juce::uint32 hash() const;
    void setTuner (bool on);
    bool tunerOn() const { return owned != nullptr && owned->tunerListening.load(); }

    // The board's name: the factory or saved board it came from (empty = your own). Edited since: it differs.
    juce::String name;
    juce::uint32 namedHash = 0;
    void setName (const juce::String& n) { name = n; namedHash = hash(); }
    bool edited() const { return name.isNotEmpty() && hash() != namedHash; }

    //==========================================================================
    // The engine (its constructor and prepare)
    void attach (juce::AudioPlayHead* clock) { head = clock; }
    void prepare (double sampleRate);

    // Audio thread: in place, stereo, any length.
    bool willRun() const;
    void process (float* left, float* right, int n);

    static constexpr int kMaxBlock = 512;

private:
    static bool audibleNow (const StripFx&, const std::atomic<float>* gainIn);

    std::unique_ptr<StripFx> owned;
    std::atomic<StripFx*> live { nullptr };
    std::atomic<float>* gainIn = nullptr;
    juce::AudioPlayHead* head = nullptr;
    std::atomic<double> rate { 44100.0 };

    // audio thread
    bool running = false;
    int quiet = 0;
    std::vector<float> dryL, dryR;
    juce::MidiBuffer noMidi;
};

//==============================================================================
// Offline: sound through a board's settings, on its own clock (QUANTIZE and tempo re-renders, TO MAC prints).
class OfflineBoard
{
public:
    OfflineBoard (const juce::var& settings, double sampleRate, double bpm, double startBeat);
    bool active() const { return fx != nullptr; }
    void process (float* left, float* right, int n);

private:
    std::unique_ptr<StripFx> fx;
    BoardClock clock;
    double beatsPerSample = 0.0;
    juce::MidiBuffer noMidi;
};
// A loop through a board: played twice, the second pass kept, so the echoes of the end ring into the start.
void applyBoardToLoop (juce::AudioBuffer<float>&, const juce::var& settings, double sampleRate, double bpm);

//==============================================================================
// Ready-made and saved boards: EMPTY BOARD, the Mac's factory boards that suit the sound, then your own
// (Documents/Boards, one .json each).
struct BoardChoice
{
    juce::String name;
    int preset = -1;       // pedalboard::presets() index
    juce::File file;       // your own
    bool empty = false;
};
std::vector<BoardChoice> boardChoices (int boardId, const juce::File& ownFolder);
bool applyChoice (Board&, const BoardChoice&);
juce::File saveOwnBoard (const Board&, const juce::String& name, const juce::File& ownFolder);
int choiceIndexOf (const std::vector<BoardChoice>&, const Board&);   // -1 = own settings

// Self-checks of the pedals themselves: each changes the sound when on and is bit-transparent when off, an
// untouched board is skipped, the delay follows the tempo. Returns what was wrong (empty = fine).
juce::String boardsSelfCheck (juce::String& report);
}
