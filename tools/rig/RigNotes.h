#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <bitset>
#include <vector>

// Notes that stay notes. A take recorded into the song from the Bass, the Synth, the Sampler,
// the Drum machine or the MPC keeps what you played as notes (a clip), not only as sound: the
// clip plays the tool live in its section of the song, so you can still change its sound, and
// fix a note, move it, quantize it or change how hard it's hit (Edit notes).
namespace rignotes
{
struct Note
{
    double beat = 0;        // from the clip's start
    double length = 0.25;   // beats
    int note = 60;
    float velocity = 0.8f;  // 0..1
    int channel = 1;
    bool operator== (const Note& o) const
    {
        return beat == o.beat && length == o.length && note == o.note && velocity == o.velocity && channel == o.channel;
    }
};

// The notes of one tool over one stretch of the song.
struct Clip
{
    int strip = -1;           // RigEngine::StripId
    double startBar = 0, bars = 4;
    std::vector<Note> notes;  // in order of their beat
    void sort();
    double endBar() const { return startBar + bars; }
};

// Notes as text, "beat length note velocity channel;" each (to save them).
juce::String encode (const std::vector<Note>&);
std::vector<Note> decode (const juce::String&);
juce::ValueTree toTree (const std::vector<Clip>&);
std::vector<Clip> fromTree (const juce::ValueTree&);
constexpr const char* kTreeType = "NoteClips";

// Notes from what was played (note ons and offs at sample positions from the start), over a
// stretch `samples` long that is `beats` long. A note still held at the end ends there.
struct Event { int sample = 0; int note = 60; float velocity = 0; int channel = 1; bool on = true; };
std::vector<Note> notesFrom (const std::vector<Event>&, int samples, double beats);

// Moves notes onto a grid (in beats; 0.25 is 16ths), by `strength` (0..1). Lengths snap too,
// never under one step.
void quantize (std::vector<Note>&, double grid, float strength = 1.0f, bool lengths = false);
// The note's name: "C3", "F#4" (middle C is C3, as in Logic).
juce::String noteName (int note);
} // namespace rignotes

// Plays the clips into the tools while the song plays. The message thread hands it the clips
// (publish); the audio thread picks the newest copy up at its next block.
class RigNotePlayer
{
public:
    static constexpr int kMaxClips = 64, kMaxNotes = 16384, kMaxStrips = 16;

    // Message thread.
    void publish (const std::vector<rignotes::Clip>&);

    // Audio thread, once per block (the engine splits blocks at the song's jumps): adds the
    // clips' notes due in it to `dests[strip]`. While not playing, or after a jump, the notes
    // left sounding are let go. A clip of `muteStrip` starting at `muteBar` is skipped (it's
    // being recorded over).
    void process (double beat, int numSamples, double bpm, double sampleRate, int beatsPerBar, bool playing,
                  juce::MidiBuffer* const* dests, int numDests, int muteStrip = -1, double muteBar = -1);
    // Lets go of everything at the next block.
    void releaseAll() { releaseRequested = true; }

private:
    struct Live
    {
        struct C { int strip; double startBar, bars; int first, count; };
        std::array<C, kMaxClips> clips {};
        int numClips = 0;
        std::array<rignotes::Note, kMaxNotes> notes {};
    };
    static void fill (Live&, const std::vector<rignotes::Clip>&);
    void letGo (juce::MidiBuffer* const* dests, int numDests);

    juce::SpinLock lock;
    std::unique_ptr<Live> pending = std::make_unique<Live>(), live = std::make_unique<Live>();
    int pendingVersion = 0, liveVersion = 0;
    double lastEnd = -1;
    std::array<std::array<std::bitset<128>, 16>, kMaxStrips> sounding {};
    std::atomic<bool> releaseRequested { false };
};
