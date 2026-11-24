#pragma once

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

#include "VisualLooks.h"
#include "VisualTap.h"

#include <array>
#include <memory>

// Turns what the tap heard into what the picture needs, once per frame (the picture's thread, never
// the audio thread): the kick, bass, mids and highs, the notes playing (from the sound, and straight
// from the tools that play notes), the bar on the song's clock, the six colours, and which look
// shows (Drift moves to the next one every 8-bar phrase and melts into it).
namespace crate::visuals
{
struct Clock
{
    double beats = 0.0, bpm = 120.0;
    int beatsPerBar = 4;
    bool playing = false;
};

struct Frame
{
    float time = 0, bass = 0, mid = 0, high = 0, beat = 0, bars = 0, wild = 0.7f, trail = 0.6f;
    int lookA = 0, lookB = 0;
    float mix = 1.0f, decay = 0.8f;
    std::array<float, 18> palette {};     // six colours, RGB 0..1
    std::array<float, 6> weights {};      // how much each of them shows
    std::array<std::uint8_t, 512> wave {};
    std::array<std::uint8_t, 64> spectrum {};
    std::array<float, 12> notes {};       // how strongly each note sounds now, 0..1
    std::array<float, VisualTap::kTools> tools {};
    bool hearing = false;                 // false: nothing is playing, the picture breathes on its own
};

class VisualAnalysis
{
public:
    explicit VisualAnalysis (VisualTap&);
    ~VisualAnalysis();

    // One frame, `dt` seconds after the last. `state` is read (and its look moved on by Drift).
    const Frame& step (double dt, const Clock&, VisualState&);
    const Frame& frame() const { return f; }

    static constexpr int kFftOrder = 11, kFft = 1 << kFftOrder;
    static constexpr double kRate = 44100.0;   // close enough for the bands: the Mac and phone run 44.1 or 48 kHz
    void setSampleRate (double r) { rate = r > 0 ? r : kRate; }

private:
    void analyseSound (float dt);
    void breathe (float dt);
    float band (float loHz, float hiHz) const;
    void buildPalette (VisualState&);

    VisualTap& tap;
    Frame f;
    double rate = kRate;
    struct Fft;   // juce::dsp's, kept out of this header
    std::unique_ptr<Fft> fft;
    std::array<float, kFft> samples {};
    std::array<float, 2 * kFft> work {};
    std::array<float, kFft / 2> level {};       // each bin 0..1 (-100 dB .. -30 dB), smoothed like a meter
    std::array<float, kFft / 2> magnitude {};   // each bin's size, as it is (for the notes)   // each bin 0..1 (-100 dB .. -30 dB), smoothed like a meter
    std::uint64_t lastWritten = 0;
    float quietFor = 10.0f;
    float peak = 0.2f, avgBass = 0.0f, sinceBeat = 1.0f;
    int onsets = 0;
    bool toolsReported = false;
    int lastPhrase = -1, shownLook = -1;
    std::array<float, 18> paletteNow {};
};
} // namespace crate::visuals
