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

// EDIT CHORD: the chord types the builder offers (the Mac's chord builder names them the same way).
struct OwnChordType { const char* label; const char* suffix; std::vector<int> intervals; };
const std::vector<OwnChordType>& ownChordTypes();   // MAJ, MIN, 7, MAJ7, MIN7, 9, MAJ9, MIN9, 6, ADD9, SUS2, SUS4, DIM, AUG, POWER

// A chord pad's own chord: any notes, from a root and a type (with inversion and octave) or toggled on the keys.
struct OwnChord
{
    bool on = false;            // false: the pad plays the song key's chord
    int root = 0;               // 0..11 (what the ROOT buttons show lit)
    int type = 0;               // ownChordTypes(), -1 = notes from the keys that aren't one of them
    int inversion = 0, octave = 0;
    juce::Array<int> notes;     // what it plays, lowest first

    juce::var toVar() const;
    static OwnChord fromVar (const juce::var&);
    bool operator== (const OwnChord& o) const { return on == o.on && root == o.root && type == o.type && notes == o.notes; }
};

namespace chordmath
{
    // The notes of a root (0..11) and type, inverted (the lowest note up an octave, `inversion` times) and moved by octaves.
    juce::Array<int> buildOwn (int root, int type, int inversion, int octave);
    // A chord's name from its notes ("F#7", "Dm/F"), with the app's flats and sharps; empty when it isn't a chord we can name.
    juce::String nameNotes (const juce::Array<int>& notes);
    // The root and type a set of notes is (the lowest note as the root first); false when none fits.
    bool detectOwn (const juce::Array<int>& notes, int& root, int& type);
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
    // LIVE KEY: everything the chords play moved by `semitones`, landing now (0), on the next beat (1) or on the
    // next bar (2) while the clock runs (stopped: at once). A chord sounding then moves with it.
    void setLiveShift (int semitones, int when) { liveWhen = juce::jlimit (0, 2, when); liveTarget = semitones; }
    int playingShift() const { return liveApplied.load(); }   // what plays now (differs while a change waits)
    // CHORDS ON / OFF: off, a pad plays its chord's lowest note alone (the progression keeps its chords).
    // `full`: the whole chord anyway (HEAR, while you build one).
    void setChordsOn (bool on) { chordsOff = ! on; }
    void setFullChord (bool full) { fullChord = full; }
    bool clockRunning() const;   // the song playing (a LIVE KEY change waits for its bar only then)
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
    void sendToVocoder (int pad, bool single);
    void logChange (double beat, int pad);
    int notesOf (int pad, int* out, bool single = false) const;   // single: the lowest note alone (chords off)

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
    std::atomic<int> liveTarget { 0 }, liveWhen { 2 }, liveApplied { 0 };
    std::atomic<bool> chordsOff { false }, fullChord { false };
    std::atomic<float> peak { 0.0f };
    std::array<std::atomic<int>, 6> sentNotes;
    std::array<std::atomic<double>, 64> logBeat;
    std::array<std::atomic<int>, 64> logPad;
    std::atomic<int> logCount { 0 };

    // audio thread
    juce::MidiBuffer midi;
    juce::Array<int> forVocoder;
    int playing = -1, playingVersion = -1, playingStyle = -1;
    int shiftNow = 0, playingShiftNow = 0;   // the live key's shift, and the one the sounding chord was played with
    double liveWaitUntil = -1;                // the beat a waiting live key change lands on, -1 none
    bool playingSingle = false;               // the sounding pad plays one note (chords off)
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
    ~ChordsTool() { *alive = false; }
    ChordsEngine& engine() { return eng; }

    SongKey key;
    int colour = 1;            // ChordColour (7TH to start, as the design)
    int style = styleChord;
    bool hold = true;          // pads latch: tap again (or another) to change
    bool chordsOn = true;      // CHORDS ON / OFF: off, each pad plays its chord's lowest note (the Mac's Chords on, the same way)
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
    // CHORDS ON / OFF. Flipping it lets go of a latched chord; a pad held down changes under your finger.
    void setChordsOn (bool on);
    int heldPad() const { return live; }
    int soundingPad() const { return eng.soundingPad(); }
    juce::Array<int> padNotes (int pad) const;
    juce::String padName (int pad) const;
    juce::String padNumeral (int pad) const;   // "OWN" for a pad with its own chord

    // EDIT CHORD: a pad's own chord (saved with the song; the song's KEY and COLOUR leave it alone).
    const OwnChord& ownChord (int pad) const { return own[(size_t) juce::jlimit (0, kChordPads - 1, pad)]; }
    bool isOwn (int pad) const { return ownChord (pad).on; }
    void setOwnChord (int pad, const OwnChord&);
    void resetToKey (int pad);                 // RESET TO SONG KEY: the automatic chord again
    juce::Array<int> keyNotes (int pad) const; // the song key's chord for the pad (with the COLOUR, without + BASS)
    void copyChord (int from, int to);         // COPY CHORD TO: the pad's chord (own or the key's) becomes `to`'s own
    void audition (int pad, int ms = 700);     // HEAR: the pad sounds for a moment
    int usedBoxes() const;     // the progression up to its last chord (0 = none)
    int progressionBars() const { return usedBoxes(); }
    void progressionEdited();  // a box changed, cleared or the progression switched: its sound back on
    void reset();
    juce::var toVar() const;
    void fromVar (const juce::var&);

    // LIVE KEY (the Mac's Live key, the same way): the twelve keys move everything the chords play so the song's
    // key becomes the key tapped, the nearest way (A to G is down 2), and OCT - / OCT + move it by octaves on top.
    // Nothing is rewritten: it's a shift on what plays, saved with the song. ON BAR: the change lands on the next
    // bar while the song plays; off, at once.
    static constexpr int kLiveOctaves = 2;
    bool liveOpen = false;     // the LIVE KEY panel showing on the CHORDS screen
    bool liveOnBar = true;
    void setLiveKey (int pitchClass);
    void nudgeLiveOctave (int delta);
    void liveHome();           // back to the song's key, octave 0
    void resetLiveKey() { liveKeyShift = 0; applyLive(); }   // the song key changed: the live key starts from it
    int liveKey() const { return ((key.root + liveKeyShift) % 12 + 12) % 12; }   // the key asked for
    int liveOctave() const { return liveOct; }
    int liveShift() const { return liveKeyShift + 12 * liveOct; }
    int playingLiveKey() const { return ((key.root + eng.playingShift()) % 12 + 12) % 12; }
    bool liveWaiting() const { return eng.playingShift() != liveShift(); }
    juce::String livePadName (int pad) const;   // the pad's chord as it plays now (after the live key)

private:
    void applyLive() { eng.setLiveShift (liveShift(), liveOnBar ? 2 : 0); }
    int liveKeyShift = 0, liveOct = 0;
    ChordsEngine eng;
    std::array<OwnChord, kChordPads> own;
    int auditionId = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);   // (a HEAR still timing out after the tool is gone)
    int live = -1;
    bool momentary = false;   // the pad sounds only while it's held
};

juce::String chordsSelfCheck (juce::String& report);
}
