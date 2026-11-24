#include "VisualAnalysis.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <numeric>

namespace crate::visuals
{
struct VisualAnalysis::Fft
{
    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFft, juce::dsp::WindowingFunction<float>::hann };
};

VisualAnalysis::VisualAnalysis (VisualTap& t) : tap (t), fft (std::make_unique<Fft>()) {}
VisualAnalysis::~VisualAnalysis() = default;

static float decayBy (float dt, float perSecond) { return std::exp (-dt * perSecond); }

float VisualAnalysis::band (float loHz, float hiHz) const
{
    const double hz = rate / kFft;
    const int lo = juce::jmax (1, (int) std::floor (loHz / hz)), hi = juce::jmin ((int) level.size() - 1, (int) std::floor (hiHz / hz));
    if (hi < lo)
        return 0.0f;
    float s = 0;
    for (int i = lo; i <= hi; ++i)
        s += level[(size_t) i];
    return s / (float) (hi - lo + 1);
}

void VisualAnalysis::breathe (float dt)
{
    // Nothing playing: a slow breath, so the picture is alive before you press Play.
    const float t = f.time;
    auto toward = [dt] (float& v, float to) { v += (to - v) * juce::jmin (1.0f, dt * 3.0f); };
    toward (f.bass, 0.18f + 0.12f * std::sin (t * 0.8f));
    toward (f.mid, 0.2f + 0.1f * std::sin (t * 0.5f));
    toward (f.high, 0.1f);
    f.beat *= decayBy (dt, 3.0f);
    for (size_t i = 0; i < f.wave.size(); ++i)
        f.wave[i] = (std::uint8_t) juce::jlimit (0, 255, (int) (128.0f + 40.0f * std::sin ((float) i / 512.0f * juce::MathConstants<float>::pi * 6.0f + t * 2.0f) * std::sin (t * 0.7f)));
    for (size_t i = 0; i < f.spectrum.size(); ++i)
        f.spectrum[i] = (std::uint8_t) juce::jlimit (0, 255, (int) (60.0f + 40.0f * std::sin ((float) i * 0.3f + t)));
    const int pcs[4] { 9, 0, 4, 7 };
    auto& n = f.notes[(size_t) pcs[(int) (t / 2.0f) % 4]];
    n = juce::jmax (n, 0.6f);
    for (size_t i = 0; i < f.tools.size(); ++i)
        f.tools[i] = juce::jmax (f.tools[i], 0.25f + 0.2f * std::sin (t * 0.6f + (float) i));
}

void VisualAnalysis::analyseSound (float dt)
{
    tap.latest (samples.data(), kFft);

    // The wave, for Trails: the newest 1024 samples, every other one, turned up when it's quiet.
    float wavePeak = 1.0e-3f, sumSquares = 0.0f;
    for (int i = kFft - 1024; i < kFft; ++i)
    {
        wavePeak = juce::jmax (wavePeak, std::abs (samples[(size_t) i]));
        sumSquares += samples[(size_t) i] * samples[(size_t) i];
    }
    const float rms = std::sqrt (sumSquares / 1024.0f);
    const float waveGain = juce::jmin (4.0f, 0.45f / wavePeak);
    for (int i = 0; i < 512; ++i)
        f.wave[(size_t) i] = (std::uint8_t) juce::jlimit (0, 255, (int) std::lround (128.0f + 127.0f * samples[(size_t) (kFft - 1024 + i * 2)] * waveGain));

    // The spectrum, as a meter reads it: each bin from -100 dB (0) to -30 dB (1), smoothed a little.
    std::copy (samples.begin(), samples.end(), work.begin());
    fft->window.multiplyWithWindowingTable (work.data(), (size_t) kFft);
    fft->fft.performFrequencyOnlyForwardTransform (work.data(), true);
    for (size_t i = 0; i < level.size(); ++i)
    {
        const float mag = work[i] * 4.0f / (float) kFft;   // a full-scale sine reads about 0 dB (Hann halves it)
        magnitude[i] = mag;
        const float db = 20.0f * std::log10 (mag + 1.0e-9f);
        const float v = juce::jlimit (0.0f, 1.0f, (db + 100.0f) / 70.0f);
        level[i] = 0.55f * level[i] + 0.45f * v;
    }
    for (int i = 0; i < 64; ++i)
    {
        const int lo = (int) std::floor (std::pow (2.0, i / 64.0 * 9.2)), hi = (int) std::floor (std::pow (2.0, (i + 1) / 64.0 * 9.2)) + 1;
        float m = 0;
        for (int j = lo; j < hi && j < (int) level.size(); ++j)
            m = juce::jmax (m, level[(size_t) j]);
        f.spectrum[(size_t) i] = (std::uint8_t) juce::jlimit (0, 255, (int) (m * 255.0f));
    }

    const float b = band (30, 150), m = band (300, 2000), h = band (4000, 12000);
    peak = juce::jmax (peak * std::pow (0.999f, dt * 60.0f), juce::jmax (b, m, 0.15f));
    f.bass += (b / peak - f.bass) * 0.35f;
    f.mid += (m / peak - f.mid) * 0.2f;
    f.high += (juce::jmin (1.0f, h / peak * 1.6f) - f.high) * 0.3f;

    // The kick: the low end jumping over its own average.
    avgBass += (b - avgBass) * 0.04f;
    sinceBeat += dt;
    if (b > avgBass * 1.25f + 0.04f && sinceBeat > 0.28f)
    {
        f.beat = 1.0f;
        sinceBeat = 0.0f;
        ++onsets;
        if (! toolsReported)
            f.tools[0] = 1.0f;
    }
    f.beat *= decayBy (dt, 5.5f);

    // The notes, heard: the energy of every bin from 55 Hz to 2 kHz, by its place in the octave.
    std::array<float, 12> heard {};
    const double hz = rate / kFft;
    for (int i = (int) std::ceil (55.0 / hz); i < (int) level.size() && i * hz < 2000.0; ++i)
    {
        const double fq = i * hz;
        const int pc = (((int) std::lround (12.0 * std::log2 (fq / 440.0)) + 69) % 12 + 12) % 12;
        heard[(size_t) pc] += magnitude[(size_t) i] * magnitude[(size_t) i];   // the energy itself, so a note's own bins win
    }
    const float top = juce::jmax (1.0e-6f, *std::max_element (heard.begin(), heard.end()));
    const float loud = juce::jlimit (0.0f, 1.0f, (rms - 0.002f) * 30.0f);   // silence (or hiss) names no notes
    for (size_t pc = 0; pc < 12; ++pc)
        f.notes[pc] = juce::jmax (f.notes[pc], std::pow (heard[pc] / top, 4.0f) * loud);

    // Each tool: as loud as it played (the Mac's mixer tells us); the phone guesses from the bands.
    if (! toolsReported)
    {
        f.tools[2] = juce::jmax (f.tools[2], f.bass);
        f.tools[3] = juce::jmax (f.tools[3], f.mid);
        f.tools[4] = juce::jmax (f.tools[4], f.mid * 0.7f);
        f.tools[1] = juce::jmax (f.tools[1], f.high);
        f.tools[5] = juce::jmax (f.tools[5], 0.3f);
    }
}

const Frame& VisualAnalysis::step (double dtIn, const Clock& clock, VisualState& state)
{
    const float dt = (float) juce::jlimit (0.0, 0.1, dtIn);
    f.time += dt;
    for (auto& t : f.tools)
        t *= decayBy (dt, 3.2f);
    for (auto& n : f.notes)
        n *= decayBy (dt, 1.6f);

    // Exact notes and levels from the tools (the Mac's Bass and Synth, and its mixer).
    std::array<float, 12> hits {};
    tap.takeNotes (hits);
    for (size_t pc = 0; pc < 12; ++pc)
        f.notes[pc] = juce::jmax (f.notes[pc], hits[pc]);
    for (int s = 0; s < VisualTap::kTools; ++s)
        if (const float l = tap.takeToolLevel (s); l > 0.0f)
        {
            toolsReported = true;
            f.tools[(size_t) s] = juce::jmax (f.tools[(size_t) s], juce::jmin (1.0f, std::sqrt (l) * 1.2f));
        }

    const auto written = tap.samplesWritten();
    quietFor = written != lastWritten ? 0.0f : quietFor + dt;
    lastWritten = written;
    // Silence reads as silence for a moment (a stopped song), then the picture breathes on its own.
    f.hearing = quietFor < 0.5f && written >= (std::uint64_t) kFft;
    if (f.hearing)
        analyseSound (dt);
    else
        breathe (dt);

    // The clock: the song's bar while it plays; otherwise the kicks counted (4 to a bar), or the time.
    if (clock.playing && clock.bpm > 0)
        f.bars = (float) (clock.beats / juce::jmax (1, clock.beatsPerBar));
    else if (f.hearing)
        f.bars = (float) onsets / 4.0f;
    else
        f.bars = f.time * 0.12f;

    // Which look: picked, or Drift's next one on bar 1 of every 8-bar phrase.
    const int phrase = (int) std::floor (f.bars / 8.0f);
    if (state.drift.load() && (clock.playing || f.hearing))
    {
        if (lastPhrase >= 0 && phrase != lastPhrase)
            state.look = (state.look.load() + 1) % kLooks;
        lastPhrase = phrase;
    }
    else
        lastPhrase = -1;
    const int want = juce::jlimit (0, kLooks - 1, state.look.load());
    if (shownLook < 0)
        f.lookA = f.lookB = shownLook = want;
    else if (want != f.lookB)
    {
        f.lookA = f.mix < 0.5f ? f.lookA : f.lookB;
        f.lookB = want;
        f.mix = 0.0f;
    }
    if (f.mix < 1.0f)
        f.mix = juce::jmin (1.0f, f.mix + dt / (state.drift.load() ? 2.5f : 1.2f));

    f.wild = 0.25f + 0.25f * (float) (juce::jlimit (1, 4, state.wild.load()) - 1);
    f.trail = (float) (juce::jlimit (1, 4, state.trails.load()) - 1) / 3.0f;
    const float lookTrailNow = lookTrail (f.lookA) * (1.0f - f.mix) + lookTrail (f.lookB) * f.mix;
    f.decay = juce::jmin (0.965f, (0.55f + 0.42f * f.trail) * (0.6f + 0.4f * lookTrailNow));

    buildPalette (state);
    return f;
}

void VisualAnalysis::buildPalette (VisualState& state)
{
    const auto colours = state.getColours();
    if (followsNotes (colours))
    {
        // The six notes sounding most, in circle-of-fifths order so the colours don't jump about.
        std::array<int, 12> order;
        std::iota (order.begin(), order.end(), 0);
        std::stable_sort (order.begin(), order.end(), [this] (int a, int b) { return f.notes[(size_t) a] > f.notes[(size_t) b]; });
        std::array<int, 6> six;
        std::copy (order.begin(), order.begin() + 6, six.begin());
        const float top = juce::jmax (0.05f, f.notes[(size_t) six[0]]);
        std::sort (six.begin(), six.end(), [] (int a, int b) { return (a * 7) % 12 < (b * 7) % 12; });
        const auto map = state.noteColours();
        for (size_t j = 0; j < 6; ++j)
        {
            const auto c = map[(size_t) six[j]];
            const float rgb[3] { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() };
            for (size_t q = 0; q < 3; ++q)
                paletteNow[j * 3 + q] += (rgb[q] - paletteNow[j * 3 + q]) * 0.25f;
            f.weights[j] = std::pow (f.notes[(size_t) six[j]] / top, 1.5f) * 1.6f;
        }
        f.palette = paletteNow;
        return;
    }
    const auto six = fixedColours (colours);
    for (size_t j = 0; j < 6; ++j)
    {
        f.palette[j * 3] = six[j].getFloatRed();
        f.palette[j * 3 + 1] = six[j].getFloatGreen();
        f.palette[j * 3 + 2] = six[j].getFloatBlue();
        f.weights[j] = colours == Colours::tools ? f.tools[j] : 0.7f;
    }
    paletteNow = f.palette;
}
} // namespace crate::visuals
