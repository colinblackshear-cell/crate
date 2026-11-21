#pragma once

#include "PocketChordPlayer.h"
#include "PocketKey.h"
#include "PocketSynth.h"

class VocoderProcessor;

// The CHORDS tool's chords, for the whole app: one chord state that the CHORDS tool and the VOCODER tool's quick
// pads both play, and that drives the vocoder's carrier.
//
// chordmath: the eight pads of a key (its seven chords, then the dominant seventh), voiced by a COLOUR (plain,
// 7th, 9th, sus, power), with the vocoder's own chord builder (vox::Chords) doing the stacking.
//
// ChordsEngine is an Instrument inside the looper: the pad held (or latched), else the progression's chord for
// this bar, played the PLAY way (as a chord, strummed, arpeggiated, or in stabs) on its own synth voice (the
// SYNTH tool's engine, a second instance) and/or a pad's sample, and sent to the vocoder as its notes. All on the
// audio thread from atomics the message thread sets: nothing allocates or waits.
namespace pocket
{
constexpr int kChordPads = 8, kMaxChordNotes = 8, kProgressionMax = 8, kChordColours = 5;
enum class ChordColour { plain, seventh, ninth, sus, power };
const char* chordColourName (int colour);   // "PLAIN", "7TH"
enum ChordStyle { styleChord, styleStrum, styleArp, styleStabs };

namespace chordmath
{
    int flavourOf (int colour);   // the vocoder's chord flavour (vox::Chords) for a COLOUR
    // A pad's notes (lowest first) for a key and a vocoder chord flavour; pad 7 is the dominant (V7, V9...).
    juce::Array<int> padNotes (SongKey, int pad, int flavour);
    juce::String padName (SongKey, int pad, int flavour);      // "Am7", "E7"
    juce::String padNumeral (SongKey, int pad, int flavour);   // "i", "V7"
}

class ChordsEngine : public Instrument
{
public:
    explicit ChordsEngine (BeatsEngine&);

    //==========================================================================
    // Message thread
    void setPadNotes (int pad, const juce::Array<int>&);   // with the bass, if it's on
    void setLivePad (int pad) { livePad = pad; }            // the pad held or latched, -1 = none
    void setStyle (int style) { styleNow = style; }
    void setProgression (const std::array<int, kProgressionMax>& steps, int length, bool isOn);
    // The bar the progression's first chord falls on (a CHORDS take starts the progression from its top).
    void setOrigin (double beat) { origin = beat; }
    double getOrigin() const { return origin.load(); }
    // The progression's sound off (it went onto a layer), while it still drives the vocoder.
    void setProgressionMuted (bool m) { progMuted = m; }
    bool progressionMuted() const { return progMuted.load(); }
    void setPlays (bool synth, bool pad, bool toVocoder) { playSynth = synth; playPad = pad; playVocoder = toVocoder; }
    void setVocoder (VocoderProcessor* p) { vocoder = p; }
    void setSynthSettings (const crate::synthtool::Settings& s) { voice.setSettings (s); }
    void setPadSound (crate::SharedAudio a, double tune) { padPlayer.setSound (std::move (a), tune); }
    bool hasPadSound() const { return padPlayer.hasSound(); }
    float echoSend() const override { return 0.12f; }

    int soundingPad() const { return current.load(); }         // what plays now (held or the progression's), -1 = none
    int progressionStep() const { return progStep.load(); }    // -1 when the progression isn't playing
    bool fromProgression() const { return currentFromProg.load(); }
    // For the self-checks: the chord changes (beat, pad) and what the vocoder was sent.
    int changes (double* beats, int* pads, int max) const;
    int vocoderSends() const { return sends.load(); }
    juce::Array<int> lastVocoderNotes() const;
    float takePeak() { return peak.exchange (0.0f); }

    //==========================================================================
    // Audio thread
    void prepare (double sampleRate) override;
    void render (float* left, float* right, int numSamples) override;
    void renderAt (float* left, float* right, int numSamples, bool running, double beat, double bpm);

private:
    void sendToVocoder (int pad);
    void logChange (double beat, int pad);
    int notesOf (int pad, int* out) const;

    BeatsEngine& beats;
    SynthCore voice;
    ChordPlayer padPlayer;
    VocoderProcessor* vocoder = nullptr;
    double sampleRate = 44100.0;

    std::array<std::array<std::atomic<int>, kMaxChordNotes>, kChordPads> padNotesLive;
    std::atomic<int> notesVersion { 0 };
    std::atomic<int> livePad { -1 }, styleNow { styleChord }, progLength { 4 };
    std::array<std::atomic<int>, kProgressionMax> progSteps;
    std::atomic<bool> progOn { false }, progMuted { false }, playSynth { true }, playPad { false }, playVocoder { true };
    std::atomic<double> origin { 0.0 };
    std::atomic<int> current { -1 }, progStep { -1 }, sends { 0 };
    std::atomic<bool> currentFromProg { false };
    std::atomic<float> peak { 0.0f };
    std::array<std::atomic<int>, 6> sentNotes;
    std::array<std::atomic<double>, 64> logBeat;
    std::array<std::atomic<int>, 64> logPad;
    std::atomic<int> logCount { 0 };

    // audio thread
    juce::MidiBuffer midi;
    juce::Array<int> forVocoder;
    int playing = -1, playingVersion = -1, playingStyle = -1;
    bool playingProg = false, playingAudible = false, wasRunning = false, wasSynth = true, wasPad = false;
    std::array<int, kMaxChordNotes> notes {};
    int numNotes = 0;
    std::array<int, kMaxChordNotes> on {};      // synth notes sounding
    int numOn = 0;
    double offBeat = -1;                         // ARP / STABS: when the sounding notes stop
    long long lastTick = -1;
    int arpAt = 0;
    std::array<int, kMaxChordNotes> strumLeft {};   // STRUM: samples until each note starts (-1 started)
    double freeBeat = 0;
};

//==============================================================================
// The CHORDS tool's settings and the one chord state (message thread). Saved with the song.
class ChordsTool
{
public:
    explicit ChordsTool (BeatsEngine&);
    ChordsEngine& engine() { return eng; }

    SongKey key;
    int colour = 1;            // ChordColour (7TH to start, as the design)
    int style = styleChord;
    bool hold = true;          // pads latch: tap again (or another) to change
    bool bass = false;         // + BASS: the root an octave under
    std::array<int, kProgressionMax> progression;
    int progLength = 4;        // 4 or 8 boxes
    bool progressionOn = false;
    bool playsSynth = true, playsPad = false, playsVocoder = true;
    int synthPreset = 0;
    int padSoundPad = -1;      // the PADS pad whose sound MY PAD SOUND plays
    juce::String padSoundId;
    int editingBox = -1;       // the progression box picked: the next pad goes there
    std::function<void()> onPlay;   // a chord was played or the progression switched on (the vocoder takes the chords)

    void setKey (SongKey);
    void refreshNotes();       // key, colour or bass changed
    void apply();              // everything to the engine
    // A finger on a pad (`setBox`: a picked box takes the chord instead of playing it).
    void padDown (int pad, bool setBox = true);
    void padUp (int pad);
    void releaseAll();         // no pad held or latched
    int heldPad() const { return live; }
    int soundingPad() const { return eng.soundingPad(); }
    juce::Array<int> padNotes (int pad) const;
    juce::String padName (int pad) const;
    juce::String padNumeral (int pad) const;
    int usedBoxes() const;     // the progression up to its last chord (0 = none)
    int progressionBars() const { return usedBoxes(); }
    void progressionEdited();  // a box changed, cleared or the progression switched: its sound back on
    void reset();
    juce::var toVar() const;
    void fromVar (const juce::var&);

private:
    ChordsEngine eng;
    int live = -1;
    bool momentary = false;   // the pad sounds only while it's held
};

juce::String chordsSelfCheck (juce::String& report);
}
