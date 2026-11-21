#pragma once

#include "PocketAudio.h"
#include "core/Audio.h"

#include <array>
#include <atomic>

// Sing's chord pads in your own sound: one pad's sample played at each note of the chord, together.
// A PocketAudio::Renderer that plays inside the Loops engine, so what it plays is heard in time and goes
// into VOCODER and BOTH takes. The sample is treated as a C (MIDI 60), tuned by the pad's tune.
namespace pocket
{
class ChordPlayer : public PocketAudio::Renderer
{
public:
    ChordPlayer();

    // Message thread
    void setSound (crate::SharedAudio, double tuneSemitones);   // nullptr = silent
    bool hasSound() const { return sound != nullptr; }
    void play (const juce::Array<int>& midiNotes);   // replaces the chord that's sounding
    void release();                                   // lets the chord fade out
    // The same from any thread, without allocating (the CHORDS engine plays it from the audio thread).
    void playNotes (const int* notes, int count);
    void setStrum (int samplesBetweenNotes) { strum = juce::jmax (0, samplesBetweenNotes); }

    // Audio thread
    void prepare (double rate) override { sampleRate = rate; }
    void render (float* left, float* right, int numSamples) override;

private:
    static constexpr int kVoices = 6;
    struct Voice { double pos = 0, step = 1; float gain = 0, target = 0; bool on = false; int wait = 0; };
    std::atomic<int> strum { 0 };

    crate::SharedAudio sound;                         // the message thread's
    std::vector<crate::SharedAudio> keepAlive;
    std::atomic<const juce::AudioBuffer<float>*> live { nullptr };
    std::atomic<double> tune { 0.0 };

    // chord changes, handed over whole: the audio thread takes it when `serial` moves
    std::array<std::atomic<int>, kVoices> nextNotes;
    std::atomic<int> serial { 0 };
    std::atomic<bool> releasing { false };

    int heard = 0;
    double sampleRate = 44100.0;
    std::array<Voice, kVoices> voices;
};
}
