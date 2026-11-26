#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <atomic>
#include <memory>
#include <vector>

// A friend's track: a whole song (a mixdown someone sent you) that plays under everything you add,
// locked to Kaleidobox's bars. It plays once, start to end, from bar 1: at beat b of the clock it
// plays the moment of the song that is b beats after its bar 1 (firstBeat seconds into the file) at
// its own tempo. Stop goes back to bar 1 with everything else; a jump or a cycle goes there too.
// It's never stretched: at another tempo it plays faster or slower, like a tape (and so higher or
// lower), still locked to the bars.
//
// It's stereo, at the 44.1 kHz the tools load sounds at, and isn't a mixer strip: it has its own
// level and mute, and goes into the mix before the master (so Rec song and Bounce have it), unless
// `recorded` is off (a "My parts only" bounce), when it's heard like the click but kept out of
// every recording. Saved with the show (its sound with the loops' WAVs).
class RigFriendTrack
{
public:
    struct Info
    {
        juce::String name;           // the file's name, "Jay - idea"
        double bpm = 120;            // its tempo
        double firstBeat = 0;        // seconds into the file where its bar 1 is
        int beatsPerBar = 4;
        int key = -1;                // what it's in (0-11, -1: not set) and whether minor
        bool minor = false;
    };

    static constexpr const char* kTreeType = "FriendTrack";
    static constexpr const char* kAssetPath = "Friend track/Track.wav";
    static constexpr double kRate = 44100.0;

    //==========================================================================
    // Message thread.
    // A new song to play over (`audio` at kRate, 1 or 2 channels). Null clears it.
    void set (std::shared_ptr<const juce::AudioBuffer<float>> audio, const Info&);
    void clear() { set (nullptr, {}); }
    // Its tempo, bar 1 and key (the sound stays): takes effect at the next block.
    void setInfo (const Info&);
    Info getInfo() const { return info; }
    bool hasAudio() const { return audio != nullptr; }
    std::shared_ptr<const juce::AudioBuffer<float>> getAudio() const { return audio; }
    // How long it plays from its bar 1 (bars at its own tempo).
    double lengthBars() const;
    double lengthSeconds() const;
    // Says the sound changed (a new id for the show, so Save knows).
    juce::String getId() const { return id; }
    // Frees sounds the audio thread has let go of. Call often (the engine's timer does).
    void service();

    juce::ValueTree toTree() const;
    // The settings (the sound comes from the show's WAVs: setAudioFromShow). An invalid tree: none.
    void fromTree (const juce::ValueTree&);
    // The sound read from the show's folder, with the settings fromTree read.
    void setAudioFromShow (std::shared_ptr<const juce::AudioBuffer<float>>);
    // Whether the show it came from had a friend's track (fromTree), whose sound readLoopAudio brings.
    bool expectsAudio() const { return pendingFromShow; }

    std::atomic<float> level { 0.8f };   // fader position 0..1 (0.8 is unity, as the strips)
    std::atomic<bool> mute { false };
    std::atomic<bool> recorded { true };
    std::atomic<float> peak { 0.0f };    // since the window last looked

    //==========================================================================
    // Audio thread.
    void prepare (double sampleRate);
    // Adds this block of it into `out` (2 channels, `numSamples`), from clock beat `beat` at `bpm`.
    // Silent while the clock is stopped.
    void render (juce::AudioBuffer<float>& out, int numSamples, double beat, double bpm, bool playing);
    // Whether it has a sound to play (as of the last render).
    bool isLoadedForAudio() const { return live != nullptr; }

    //==========================================================================
    // Any thread, on its own copy: bars [fromBar, toBar) of the song (plus `tailSeconds`) at `bpm`
    // and `rate`, as it plays (the level and mute too), for a bounce.
    static juce::AudioBuffer<float> renderRange (const juce::AudioBuffer<float>& audio, const Info&, double bpm, double rate, int beatsPerBar,
                                                 double fromBar, double toBar, double tailSeconds, float gain);
    float gain() const;   // the level as a gain (0 when muted)

private:
    static void play (const juce::AudioBuffer<float>& audio, const Info&, double bpm, double rate, double beat,
                      float* const* out, int numSamples, float gainFrom, float gainTo);

    std::shared_ptr<const juce::AudioBuffer<float>> audio;
    Info info;
    juce::String id;
    bool pendingFromShow = false;
    std::vector<std::shared_ptr<const juce::AudioBuffer<float>>> retired;

    // Handed to the audio thread under the lock.
    juce::SpinLock lock;
    const juce::AudioBuffer<float>* next = nullptr;
    Info nextInfo;
    int version = 0;
    std::atomic<int> seen { 0 };

    // Audio thread.
    const juce::AudioBuffer<float>* live = nullptr;
    Info liveInfo;
    int liveVersion = 0;
    double sampleRate = 44100.0;
    float lastGain = 0.0f;
};
