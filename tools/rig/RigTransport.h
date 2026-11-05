#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>

// The Rig's one clock. Every tool in the Rig reads its tempo and play position from here, the
// same way a plugin follows Logic, so they all start, stop and stay in time together.
//
// The message thread asks to play or stop; the audio thread applies the request at the start
// of the next block, so a start always lands exactly on a block edge for every tool at once.
class RigTransport : public juce::AudioPlayHead
{
public:
    //==========================================================================
    // Message thread (and tests).
    void play();
    void stop();             // stops and goes back to the start
    void togglePlay() { isPlaying() ? stop() : play(); }

    void setBpm (double);
    double getBpm() const { return bpm.load(); }
    void setBeatsPerBar (int);
    int getBeatsPerBar() const { return beatsPerBar.load(); }

    bool isPlaying() const { return playingNow.load(); }
    double getPositionInBeats() const { return ppqNow.load(); }

    static constexpr double kMinBpm = 40.0, kMaxBpm = 240.0;

    // The metronome: a click on every beat while playing, higher on the first beat of the bar.
    std::atomic<bool> metronome { false };

    //==========================================================================
    // Audio thread.
    void prepare (double sampleRate);
    // Call before the tools play a block: applies play and stop requests.
    void beginBlock();
    // Call after they've played it: moves the clock on and adds the metronome to `out`.
    void endBlock (int numSamples, juce::AudioBuffer<float>* out = nullptr);

    // When the Rig is a plugin (in Logic): the host's position for this block, given before
    // beginBlock. Logic's tempo and time signature always win; while Logic plays, the Rig plays
    // where Logic is, and when Logic stops the Rig stops. With Logic stopped the Rig's own
    // Play still works, to jam.
    void setHostPosition (const juce::Optional<PositionInfo>&);
    bool isHosted() const { return hosted.load(); }

    juce::Optional<PositionInfo> getPosition() const override;
    // Where this block starts, and whether it plays (between beginBlock and endBlock).
    double getBlockPosition() const { return ppq; }
    bool isPlayingThisBlock() const { return playing; }

private:
    double sampleRate = 44100.0;
    std::atomic<double> bpm { 90.0 };
    std::atomic<int> beatsPerBar { 4 };
    std::atomic<int> request { 0 };     // 0 none, 1 play, 2 stop

    // Audio thread.
    bool playing = false;
    double ppq = 0;
    long long samplePosition = 0;
    int clickAge = 0;
    bool clickActive = false;
    bool clickAccent = false;

    std::atomic<bool> hosted { false };
    bool hostValid = false, hostFresh = false, hostPlaying = false, hostWasPlaying = false;
    double hostPpq = 0;
    long long hostSamples = 0;

    std::atomic<bool> playingNow { false };
    std::atomic<double> ppqNow { 0 };
};
