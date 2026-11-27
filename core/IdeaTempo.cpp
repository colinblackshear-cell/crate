#include "IdeaTempo.h"

#include <cmath>
#include <vector>

namespace crate::IdeaTempo
{
namespace
{
constexpr double kHopSeconds = 0.01;

struct Envelope
{
    std::vector<float> energy;   // mean square per hop
    std::vector<float> onset;    // how sharply the sound rises at each hop (0 = not at all)
    int hop = 441;
    float peak = 0;
};

Envelope envelopeOf (const juce::AudioBuffer<float>& b, int length, double rate)
{
    Envelope e;
    e.hop = juce::jmax (16, (int) std::lround (rate * kHopSeconds));
    const int hops = length / e.hop;
    const int chans = juce::jmax (1, b.getNumChannels());
    e.energy.assign ((size_t) juce::jmax (0, hops), 0.0f);
    for (int h = 0; h < hops; ++h)
    {
        double sum = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            const auto* d = b.getReadPointer (ch, h * e.hop);
            for (int i = 0; i < e.hop; ++i)
                sum += (double) d[i] * d[i];
        }
        e.energy[(size_t) h] = (float) (sum / (e.hop * chans));
        e.peak = juce::jmax (e.peak, e.energy[(size_t) h]);
    }
    // The rise in loudness (in dB, so a quiet word counts like a loud one), only above the room's
    // hiss (40 dB under the loudest moment).
    e.onset.assign (e.energy.size(), 0.0f);
    const float gate = juce::jmax (1.0e-9f, e.peak * 1.0e-4f);
    auto db = [&] (int h) { return 10.0f * std::log10 (juce::jmax (gate, e.energy[(size_t) h])); };
    for (int h = 1; h < hops; ++h)
    {
        // against the quietest of the last three hops: a soft start (a sung "m") still shows
        float before = db (h - 1);
        for (int k = 2; k <= 3 && h - k >= 0; ++k)
            before = juce::jmin (before, db (h - k));
        e.onset[(size_t) h] = juce::jmax (0.0f, db (h) - before);
    }
    return e;
}

// How well a beat every `period` hops starting at `phase` lines up with the starts (their sum at the
// beats; starts between the beats, the 1/8s singing is full of, neither help nor count against).
double combAt (const std::vector<float>& o, double period, double phase)
{
    double sum = 0;
    for (double t = phase; t < (double) o.size(); t += period)
    {
        // the nearest start within a hop either side (a word is never sung exactly on the grid)
        const int i = (int) std::lround (t);
        double best = 0;
        for (int k = i - 1; k <= i + 1; ++k)
            if (k >= 0 && k < (int) o.size())
                best = juce::jmax (best, (double) o[(size_t) k]);
        sum += best;
    }
    return sum;
}

double bestPhase (const std::vector<float>& o, double period, double& score)
{
    score = -1;
    double phase = 0;
    for (double p = 0; p < period; p += 0.5)
        if (const double s = combAt (o, period, p); s > score)
        {
            score = s;
            phase = p;
        }
    return phase;
}

// A gentle leaning to the tempos songs are usually at (around 110, falling off an octave away), so a
// take that would fit 60 and 120 equally well gets 120, and 180 and 90 gets 90.
double leaning (double bpm)
{
    const double octaves = std::log2 (bpm / 110.0) / 0.9;
    return std::exp (-0.5 * octaves * octaves);
}
}

Fit fit (const juce::AudioBuffer<float>& b, double rate, int endSample, int beatsPerBar)
{
    Fit f;
    beatsPerBar = juce::jmax (1, beatsPerBar);
    const int n = b.getNumSamples();
    if (n <= 0 || rate <= 0 || b.getNumChannels() == 0)
        return f;
    const int end0 = endSample < 0 ? n : juce::jlimit (0, n, endSample);
    auto env = envelopeOf (b, end0, rate);
    const int hops = (int) env.energy.size();
    if (hops < 50 || env.peak < 1.0e-5f)   // under half a second, or nothing louder than the room (-50 dB)
        return f;

    // Where the sound is: from the first moment 30 dB under the loudest to the last (and its ring).
    const float present = env.peak * 1.0e-3f;
    int first = -1, last = -1;
    for (int h = 0; h < hops; ++h)
        if (env.energy[(size_t) h] > present)
        {
            if (first < 0)
                first = h;
            last = h;
        }
    if (first < 0 || (last - first) * kHopSeconds < 0.8)
        return f;
    // The start of that first sound: back to where it began rising.
    while (first > 0 && env.energy[(size_t) first - 1] > env.peak * 1.0e-4f && env.energy[(size_t) first - 1] < env.energy[(size_t) first])
        --first;
    const int endSampleUsed = juce::jmin (end0, (last + 1) * env.hop + (int) (0.15 * rate));

    // Only the starts while there's sound (a hop's hiss at the end isn't a beat).
    std::vector<float> o (env.onset.begin(), env.onset.begin() + juce::jmin (hops, last + 2));
    const double soundHops = (double) (last - first);

    // The tempo: every 0.5 BPM first, then the best one to 0.05.
    double bestBpm = 0, bestScore = -1, bestRaw = 0, bestPhaseHops = 0;
    auto tryBpm = [&] (double bpm)
    {
        const double period = 60.0 / bpm / kHopSeconds;
        if (soundHops < period * 3.0)   // fewer than 4 beats of sound: no tempo can be heard
            return;
        double raw = 0;
        const double phase = bestPhase (o, period, raw);
        // Per beat, so slow tempos (fewer beats) aren't penalised for having fewer, but not quite: of two
        // tempos that fit as well (claps on every beat at 140 fit 70 too, every other one), the one with
        // more beats filled wins, which is the one the take was played at.
        const double beatsIn = juce::jmax (1.0, (double) o.size() / period);
        const double perBeat = raw / beatsIn;
        const double score = raw / std::pow (beatsIn, 0.7) * leaning (bpm);
        if (score > bestScore)
        {
            bestScore = score;
            bestBpm = bpm;
            bestRaw = perBeat;
            bestPhaseHops = phase;
        }
    };
    for (double bpm = 60.0; bpm <= 180.0 + 1.0e-9; bpm += 0.5)
        tryBpm (bpm);
    if (bestBpm > 0)
    {
        const double around = bestBpm;
        for (double bpm = around - 0.5; bpm <= around + 0.5 + 1.0e-9; bpm += 0.05)
            tryBpm (bpm);
    }

    // Ideas are nearly always 1, 2, 4 or 8 bars. When the tempo picked makes it an odd number of bars and
    // one half again as fast (or two thirds as fast) makes it a 1-2-4-8 and fits the starts nearly as well, the
    // picked one was hearing the dotted beat (syncopation does that): take the other.
    if (bestBpm > 0)
    {
        const double seconds = (endSampleUsed - (double) first * env.hop) / rate;
        auto barsAt = [&] (double bpm) { return juce::jmax (1.0, std::ceil (seconds * bpm / 60.0 / beatsPerBar - 0.4)); };
        auto even = [] (double bars) { const int b = (int) bars; return b > 0 && (b & (b - 1)) == 0; };
        auto rawAt = [&] (double bpm) { double r = 0; bestPhase (o, 60.0 / bpm / kHopSeconds, r); return r / juce::jmax (1.0, (double) o.size() / (60.0 / bpm / kHopSeconds)); };
        if (! even (barsAt (bestBpm)))
            for (double ratio : { 1.5, 2.0 / 3.0 })
            {
                const double alt = bestBpm * ratio;
                if (alt < 60.0 || alt > 180.0 || ! even (barsAt (alt)))
                    continue;
                double top = 0, topRaw = 0;
                for (double bpm = alt - 0.5; bpm <= alt + 0.5 + 1.0e-9; bpm += 0.05)
                    if (const double r = rawAt (bpm); r > topRaw)
                    {
                        topRaw = r;
                        top = bpm;
                    }
                if (topRaw >= bestRaw * 0.75)
                {
                    bestBpm = top;
                    bestRaw = topRaw;
                    break;
                }
            }
    }

    // Heard? The beats must carry clearly more of the starts than an average hop does.
    double meanOnset = 0;
    for (auto v : o)
        meanOnset += v;
    meanOnset /= juce::jmax ((size_t) 1, o.size());
    // And there must be a few clear starts (6 dB or more) at all: a held hum has one.
    int starts = 0;
    for (size_t h = 1; h < o.size(); ++h)
        if (o[h] >= 6.0f && o[h - 1] < 6.0f)
            ++starts;
    const bool heard = bestBpm > 0 && starts >= 4 && meanOnset > 0 && bestRaw > meanOnset * 3.5;

    double bpm = 0;
    double startSample = 0;
    if (heard)
    {
        bpm = std::round (bestBpm * 10.0) / 10.0;
        const double period = 60.0 / bpm / kHopSeconds;
        double unused = 0;
        const double phase = bestPhase (o, period, unused);
        // bar 1: the beat at the first sound (up to a fifth of a beat early still counts as on it); a
        // sound further ahead of a beat is a pickup into it, and bar 1 is that beat.
        const double k = std::ceil ((first - phase) / period - 0.2);
        startSample = (phase + k * period) * env.hop;
        juce::ignoreUnused (bestPhaseHops);
    }
    else
    {
        startSample = juce::jmax (0, first * env.hop - (int) (0.01 * rate));
    }

    const double spb = rate * 60.0 / (heard ? bpm : 120.0);
    const double contentBeats = (endSampleUsed - startSample) / spb;
    double bars = 0;
    if (heard)
    {
        bars = juce::jmax (1.0, std::ceil (contentBeats / beatsPerBar - 0.4));
    }
    else
    {
        // The take's length is the loop: the whole bars giving a tempo from 70 to 140.
        const double seconds = (endSampleUsed - startSample) / rate;
        if (seconds < 1.0)
            return f;
        for (bars = 1; bars <= 64; bars *= 2)
            if (bars * beatsPerBar * 60.0 / seconds >= 70.0)
                break;
        bpm = juce::jlimit (40.0, 240.0, std::round (bars * beatsPerBar * 60.0 / seconds * 10.0) / 10.0);
    }
    f.bpm = bpm;
    f.beats = bars * beatsPerBar;
    f.start = (int) std::lround (startSample);
    f.length = (int) std::lround (f.beats * 60.0 / bpm * rate);
    f.heard = heard;
    return f;
}

juce::AudioBuffer<float> cut (const juce::AudioBuffer<float>& b, const Fit& f)
{
    const int chans = juce::jmax (1, b.getNumChannels());
    juce::AudioBuffer<float> out (chans, juce::jmax (0, f.length));
    out.clear();
    if (! f.isValid() || b.getNumChannels() == 0)
        return out;
    const int n = b.getNumSamples(), len = f.length;
    const int spb = (int) std::lround ((double) len / f.beats);
    const int before = juce::jmin (len / 2, spb);       // a pickup up to a beat before bar 1
    const int after = juce::jmin (len / 2, spb);         // a ring up to a beat past the end
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* src = b.getReadPointer (juce::jmin (ch, b.getNumChannels() - 1));
        auto* dst = out.getWritePointer (ch);
        for (int i = 0; i < len; ++i)
        {
            const int at = f.start + i;
            if (at >= 0 && at < n)
                dst[i] = src[at];
        }
        // the moments before bar 1, faded in, at the loop's end (where they play from, going round)
        for (int i = 0; i < before; ++i)
        {
            const int at = f.start - before + i;
            if (at >= 0 && at < n)
                dst[len - before + i] += src[at] * (float) i / (float) before;
        }
        // the ring past the last bar, faded out, over the loop's start
        for (int i = 0; i < after; ++i)
        {
            const int at = f.start + len + i;
            if (at >= 0 && at < n)
                dst[i] += src[at] * (1.0f - (float) i / (float) after);
        }
    }
    // A few milliseconds' fade where the recording had nothing (it started late or ended early).
    const int edge = juce::jmin (len / 4, 64);
    if (f.start + len > n && n > f.start)
        for (int ch = 0; ch < chans; ++ch)
            for (int i = 0; i < edge; ++i)
            {
                const int at = n - f.start - edge + i;
                if (at >= 0 && at < len)
                    out.getWritePointer (ch)[at] *= 1.0f - (float) i / (float) edge;
            }
    return out;
}
}
