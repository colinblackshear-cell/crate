#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "BeatsEngine.h"
#include "PocketBoards.h"
#include "PocketLoopFx.h"
#include "PocketAudio.h"

#include <array>
#include <atomic>
#include <memory>

// The Loops tab's looper: eight mono layers recorded live, in time with the shared clock (the one
// inside BeatsEngine). One renderer for the whole phone: it renders the Beats engine, plays the layers
// on top and records the picked source. Written compactly for the phone; the Mac's RigLooper is tied
// to the Rig's effects racks and transport, so it isn't used here.
//
// A layer records from the next bar line and stops at its length, so every loop is a whole number of
// bars. Playback is locked to the clock's beat (the loop's place is read from the beat position), so
// loops stay in sync with the drums and follow tempo changes. The screens talk to it from the message
// thread through a lock-free queue; the audio thread never allocates.
namespace pocket
{
constexpr int kLoopLayers = 8;

// both = your voice and the vocoder (Sing). loops = every layer (after the ALL board), master = the whole mix (after the
// MASTER board), layerTap = one layer after its board: the EFFECTS tool's resample takes.
enum class LoopSource { mic, drums, vocoder, mix, both, synth, chords, loops, master, layerTap, drone, count };
constexpr int kInstruments = 2;   // the SYNTH and CHORDS tools' sound, each its own source

// A tool that plays notes inside the looper (SYNTH, CHORDS): always heard, recorded as its own source, and
// sending echoSend() of itself to the shared delay and reverb.
struct Instrument : public PocketAudio::Renderer
{
    virtual float echoSend() const { return 0.0f; }
};
// The DRONE tool inside the looper: heard, the DRONE source, and fed what its strings ring to (a DroneFeed:
// 0 your voice, 1 the vocoder, 2 the drums, 3 SYNTH, 4 CHORDS), one mono block before each render.
struct DroneVoice : public Instrument
{
    virtual int feedSource() const = 0;
    virtual void setFeed (const float* mono) = 0;   // null: nothing
};
enum class LoopState { empty, waiting, recording, playing, overdubWait, overdubbing };

// The tool that made a layer (what the Loops rows show): set when the take is armed.
enum class LayerTool { mic, drums, pads, synth, chords, vocoder, mix, drone, count };
int toolForSource (int source);   // a LoopSource's usual maker (DRUMS for the drums source: PADS says so itself)

// QUANTIZE on a layer that came from notes: the grid (0 = as played, 1 = 1/4, 2 = 1/8, 3 = 1/16), how far the
// notes move onto it, and swing (every second grid line later, by a third of a grid step).
struct LoopQuantize
{
    int grid = 0;
    float strength = 1.0f;
    bool swing = false;
    double gridBeats() const { return grid == 1 ? 1.0 : grid == 2 ? 0.5 : grid == 3 ? 0.25 : 0.0; }
    bool operator== (const LoopQuantize& o) const { return grid == o.grid && std::abs (strength - o.strength) < 1.0e-4f && swing == o.swing; }
};

struct NotePart;   // PocketLoopTools.h: the notes a layer was printed from

// One layer's recorded sound. The audio thread writes `data` while the layer records or overdubs (and reads
// only `data`, `length` and `lengthBeats`); the rest is the message thread's.
struct LoopTake
{
    std::vector<float> data;               // capacity; the loop is the first `length` samples
    std::atomic<int> length { 0 };
    std::atomic<double> lengthBeats { 0 };
    double sampleRate = 44100.0;
    int source = 0;
    int tool = 0;                          // LayerTool
    int content = 0;                       // samples of real sound from the start (the rest pads it to the bar), 0 = all
    std::shared_ptr<const NotePart> part;  // the notes it was printed from (QUANTIZE re-renders them), or null
    LoopQuantize quant;                    // how `part` was re-timed for this audio
    // The audio before it was stretched to follow a tempo change (null = `data` is it): every new tempo is
    // stretched from this, so changes don't pile up.
    std::shared_ptr<const std::vector<float>> origin;
    int contentSamples() const { return content > 0 ? juce::jmin (content, length.load()) : length.load(); }
};
using SharedTake = std::shared_ptr<LoopTake>;

class LoopsEngine : public PocketAudio::Renderer
{
public:
    explicit LoopsEngine (BeatsEngine&);

    //==========================================================================
    // Message thread
    // The vocoder's hookup: call this once with whatever renders the vocoder and the VOCODER source
    // works. Not owned. The vocoder always runs, so the VOCODER source can record it; it only reaches the
    // speaker (and the MIX source) while `monitor` is true (HEAR ME). nullptr = always heard.
    void setVocoderSource (PocketAudio::Renderer* r, const std::atomic<bool>* monitor = nullptr) { vocoder = r; vocoderMonitor = monitor; }
    bool hasVocoder() const { return vocoder != nullptr; }
    // Sing's chord pads in your own sound: always heard, and part of the VOCODER and BOTH sources. Not owned.
    void setChordSource (PocketAudio::Renderer* r) { chordSource = r; }
    // The SYNTH (0) and CHORDS (1) tools: heard, and the SYNTH / CHORDS sources. Not owned. Set before the sound starts.
    void setInstrument (int slot, Instrument* i) { if (juce::isPositiveAndBelow (slot, kInstruments)) instruments[(size_t) slot] = i; }
    // The DRONE tool: heard, and the DRONE source. Not owned. Set before the sound starts.
    void setDrone (DroneVoice* d) { drone = d; }
    // The count-in, in bars (0, 1 or 2), for a recording that starts the clock. The click plays through it.
    void setCountInBars (int bars) { countInBars = juce::jlimit (0, 2, bars); }

    // The mic, for the whole app. Off at every launch: with `on` false the mic is silence everywhere (no
    // take, no vocoder, nothing in the speaker). `heard` lets what comes from the mic (the vocoder while
    // its monitor is on, and your dry voice while setDryMonitor is on) reach the speaker; off, it is still
    // recorded but never heard, so a phone speaker can't feed back into the mic.
    void setMic (bool on, bool heard) { micOn = on; micHeard = on && heard; }
    bool micIsOn() const { return micOn.load(); }
    bool micIsHeard() const { return micHeard.load(); }
    // Your own voice, dry, in the speaker (only while the mic is heard).
    void setDryMonitor (bool on) { dryWanted = on; }

    // Arms an empty layer: it starts at the next bar (or after the count-in when the clock is stopped
    // and the count-in is on) and runs `bars` bars (0 = until stop()).
    bool arm (int layer, LoopSource, int bars, int tool = -1, int tap = 0);   // tool: a LayerTool, -1 = the source's usual one;
                                                                              // tap: the layer a layerTap take records
    // A take printed from a pattern (its notes go with it, for QUANTIZE) keeps its own top: it isn't moved onto
    // the shared bars as it closes. Call right after arm().
    void keepOwnTop (int layer) { layers[(size_t) layer].keepTop = true; }
    // Recording: ends at the end of the bar. Waiting for the next bar: cancels.
    void stop (int layer);
    // A playing layer records on top of itself from the next loop.
    bool dub (int layer);
    void setMute (int layer, bool);
    bool isMuted (int layer) const { return layers[(size_t) layer].mute.load(); }
    void clear (int layer);
    // Puts a loop on a layer (restoring a saved one, or undoing). nullptr empties it. `startBeat` is the clock
    // beat its top falls on (the loop's place against the song; it plays from there, round and round).
    void setLayer (int layer, SharedTake, bool mute, double startBeat = 0.0);
    // Plays only [fromBeat, toBeat) of the layer, round and round (the editor's HEAR IN-OUT). to <= from: all of it.
    void setRegion (int layer, double fromBeat, double toBeat) { layers[(size_t) layer].regionFrom = fromBeat; layers[(size_t) layer].regionTo = toBeat; }
    bool hasRegion (int layer) const { return layers[(size_t) layer].regionTo.load() > layers[(size_t) layer].regionFrom.load() + 1.0e-9; }
    SharedTake takeOf (int layer) const { return takes[(size_t) layer]; }
    // A copy of the layer's loop, to put back with setLayer later (overdub's undo).
    SharedTake copyOf (int layer) const;
    static SharedTake makeTake (int samples, double sampleRate);

    // The clock, for loops that start it themselves: running until stopped here.
    void setTransport (bool on);
    bool transportRunning() const { return beats.clockRunning(); }

    LoopState state (int layer) const { return (LoopState) layers[(size_t) layer].uiState.load(); }
    bool settling (int layer) const { return layers[(size_t) layer].pending.load() > 0; }   // a command the audio hasn't handled yet
    float position (int layer) const { return layers[(size_t) layer].uiPos.load(); }   // 0..1 through the loop
    int version (int layer) const { return layers[(size_t) layer].version.load(); }
    double startBeat (int layer) const { return layers[(size_t) layer].uiStart.load(); }
    int recordingBars (int layer) const { return layers[(size_t) layer].uiBars.load(); }
    int recordedSamples (int layer) const { return layers[(size_t) layer].uiRecorded.load(); }
    double beatNow() const { return beats.clockBeat(); }
    double bpm() const { return beats.getBpm(); }
    bool countingIn() const { for (auto& l : layers) if (l.uiCounting.load()) return true; return false; }
    // The beat a count-in ends on (where the take starts), -1 when there is no count-in going.
    double countInEnd() const { return uiCountEnd.load(); }
    // Message thread, often: lets go of sounds the audio thread no longer uses.
    void maintain();
    //==========================================================================
    // Effects on each layer (the loop editor) and their recorded moves. Message thread.
    void setKnob (int layer, int knob, float v);
    float knob (int layer, int knob) const { return fx[(size_t) layer].knob[(size_t) knob].load(); }
    float shownKnob (int layer, int knob) const { return fx[(size_t) layer].shown[(size_t) knob].load(); }   // with its moves playing
    // A finger on the knob: it plays the knob, not its moves, and with REC MOVES on it writes them.
    void touchKnob (int layer, int knob, bool down);
    void setRecMoves (int layer, bool on);
    bool recMoves (int layer) const { return fx[(size_t) layer].rec.load(); }
    bool hasMoves (int layer, int knob) const { return fx[(size_t) layer].has[(size_t) knob].load(); }
    bool hasAnyMoves (int layer) const { for (int k = 0; k < kFxKnobs; ++k) if (hasMoves (layer, k)) return true; return false; }
    std::vector<float> moves (int layer, int knob) const;
    void clearMoves (int layer);
    LayerFxState fxOf (int layer) const;
    void setFx (int layer, const LayerFxState&);
    void resetFx (int layer) { setFx (layer, {}); }
    // Writes the touched knobs into their moves at the loop's place. Call it often (the screen's timer).
    void recordMoves();
    // The layer's loop printed with its effects and its board (stereo), or nullptr.
    std::unique_ptr<juce::AudioBuffer<float>> printed (int layer) const;

    //==========================================================================
    // The EFFECTS tool's pedalboards (PocketBoards.h), one per sound, run live in render(): DRUMS and PADS after the
    // drum machine and the pads, SYNTH and CHORDS after their instruments, VOICE on the mic (what you hear of
    // yourself and what the mic source records), VOCODER after the vocoder, each layer after its five knobs, ALL on
    // the layers together, MASTER on the whole mix (not the click). A board nothing is on is skipped. Message thread
    // (the settings are the racks' atomic parameters).
    Board& board (int id) { return boards[(size_t) juce::jlimit (0, kBoards - 1, id)]; }
    const Board& board (int id) const { return boards[(size_t) juce::jlimit (0, kBoards - 1, id)]; }
    // A new take or a cleared layer starts with its board neutral.
    void resetBoard (int layer) { auto& b = board (layerBoardId (layer)); b.reset(); b.setName ({}); }

    // The longest-running layer's length in beats (0 = nothing recorded), for FREE and the display.
    double masterBeats() const;
    // The layer's loudness in `count` slices, for drawing it (also while it records).
    std::vector<float> peaks (int layer, int count) const;
    // The mic's loudness since the last call, 0..1.
    float takeInputLevel() { return inputPeak.exchange (0.0f); }

    // Raw access to the engine's block for tests: gives the mic channels the next render() sees.
    void setInput (const float* const* in, int numIn) override { inPtr = in; inChannels = numIn; }
    void setLatency (int samples) override { latency = juce::jmax (0, samples); }

    //==========================================================================
    // Audio thread
    void prepare (double sampleRate) override;
    void render (float* left, float* right, int numSamples) override;

private:
    static constexpr int kChunk = 256;

    struct Cmd { enum Kind { arm, stop, dub, clear, set } kind = arm; int layer = 0, a = 0, b = 0, c = 0, d = 0; double beat = 0; LoopTake* take = nullptr; };

    struct Layer
    {
        // read by the screen
        std::atomic<int> uiState { (int) LoopState::empty };
        std::atomic<float> uiPos { 0.0f };
        std::atomic<int> uiRecorded { 0 };
        std::atomic<int> version { 0 };
        std::atomic<bool> mute { false };
        std::atomic<int> pending { 0 };   // commands pushed and not yet handled
        std::atomic<bool> uiCounting { false };
        std::atomic<double> uiStart { 0.0 };   // beat the loop (or the recording) started on
        std::atomic<int> uiBars { 0 };         // the recording's length in bars, 0 = until stopped
        std::atomic<double> regionFrom { 0.0 }, regionTo { 0.0 };   // HEAR IN-OUT
        // All the loops share one timeline. A take recorded while others played is turned round by this many
        // beats as it closes, so its bar 1 falls on theirs (maintain() does it, with a turned copy); until then
        // it plays unturned from startBeat and its place on the shared bars is startBeat - shift.
        std::atomic<double> uiShift { 0.0 };
        std::atomic<bool> keepTop { false };
        // the audio thread's
        LoopTake* take = nullptr;
        LoopState st = LoopState::empty;
        LoopSource source = LoopSource::mic;
        int tap = 0;          // layerTap: the layer it records
        int bars = 0, writeIdx = 0;
        double armBeat = -1, startBeat = 0, lenBeats = 0, endBeat = -1, dubBeat = 0;
        double shift = 0;     // uiShift, the audio thread's
        bool fresh = false;   // armed while the clock was stopped
        int withCountIn = 0;  // bars
        float gain = 0.0f;
        FxDsp dsp;
    };

    struct FxCtl
    {
        std::array<std::atomic<float>, kFxKnobs> knob, shown;
        std::array<std::atomic<bool>, kFxKnobs> touched, has;
        std::array<std::array<std::atomic<float>, kMoveBins>, kFxKnobs> lane;
        std::atomic<bool> rec { false };
        std::array<int, kFxKnobs> lastBin {};   // message thread: where the last write ended
        std::array<float, kFxKnobs> before {};  // message thread: the knob when the finger went down
    };

    void push (const Cmd&);
    float fxValue (int layer, int knob, double frac) const;
    void handle (const Cmd&);
    void setState (Layer&, LoopState);
    void chunk (float* left, float* right, int n);
    void playLayer (Layer&, int index, double blockBeat, double bps, int n);
    void recordLayer (Layer&, int index, double blockBeat, double bps, int n);
    void finishRecording (Layer&, double endBeat);
    void setShift (Layer& l, double s) { l.shift = s; l.uiShift = s; }
    void cancelRecording (Layer&);
    void release (Layer&);

    BeatsEngine& beats;
    PocketAudio::Renderer* vocoder = nullptr;
    PocketAudio::Renderer* chordSource = nullptr;
    std::array<Instrument*, kInstruments> instruments {};
    DroneVoice* drone = nullptr;
    const std::atomic<bool>* vocoderMonitor = nullptr;
    bool vocoderHeard = true;
    std::array<Layer, kLoopLayers> layers;
    std::array<FxCtl, kLoopLayers> fx;
    FxBus bus;
    std::array<Board, kBoards> boards;
    BoardClock boardClock;
    std::array<SharedTake, kLoopLayers> takes;                          // the message thread's
    std::vector<std::pair<SharedTake, juce::uint32>> retired;           // kept a while after the audio thread lets go
    int countInBars = 1;
    std::atomic<bool> micOn { false }, micHeard { false }, dryWanted { false };
    std::atomic<double> uiCountEnd { -1.0 };

    juce::AbstractFifo fifo { 128 };
    std::array<Cmd, 128> cmds;
    juce::SpinLock pushLock;

    const float* const* inPtr = nullptr;
    int inChannels = 0;
    std::atomic<int> latency { 0 };
    double sampleRate = 44100.0;
    std::atomic<bool> transportOn { false };
    std::atomic<float> inputPeak { 0.0f };

    // audio thread state
    bool wasRunning = false;
    double nextBeat = 0;
    int chunkOffset = 0;
    std::array<std::vector<float>, kLoopLayers> layerOut;
    std::vector<float> busDelay, busReverb, busL, busR;
    std::vector<float> srcMic, srcDrums, srcVoc, srcMix, srcBoth, vocL, vocR, clickBuf;
    std::array<std::vector<float>, kInstruments> srcInst;
    std::vector<float> srcDrone;
    // the boards' stereo buses
    std::vector<float> padsL, padsR, voiceL, voiceR, allL, allR, srcLoops, srcMaster;
    std::array<std::vector<float>, kLoopLayers> layerL, layerR;
    std::array<bool, kLoopLayers> layerWet {};
};
}
