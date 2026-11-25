#include "TrackAnalysis.h"

#include "TempoFilter.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace crate::TrackAnalysis
{
namespace
{
constexpr double kListenRate = 11025.0;
constexpr int kOnsetOrder = 10, kOnsetSize = 1 << kOnsetOrder, kHop = 128;

// The song in mono, about 11 kHz (plenty to hear beats and notes).
struct Mono
{
    std::vector<float> x;
    double rate = kListenRate;
};

Mono mono (const juce::AudioBuffer<float>& audio, double rate)
{
    Mono m;
    const int chans = audio.getNumChannels(), n = audio.getNumSamples();
    if (chans == 0 || n == 0 || rate <= 0)
        return m;
    const int factor = juce::jmax (1, (int) std::round (rate / kListenRate));
    m.rate = rate / factor;
    m.x.resize ((size_t) (n / factor));
    for (size_t i = 0; i < m.x.size(); ++i)
    {
        double sum = 0;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = audio.getReadPointer (ch) + (int) i * factor;
            for (int k = 0; k < factor; ++k)
                sum += d[k];
        }
        m.x[i] = (float) (sum / (factor * chans));
    }
    return m;
}

// How much new sound starts in each hop (spectral flux), the whole song and the low end (kick, bass).
struct Onsets
{
    std::vector<float> all, low;
    double fps = kListenRate / kHop;
};

Onsets onsets (const Mono& m)
{
    Onsets o;
    o.fps = m.rate / kHop;
    if ((int) m.x.size() < kOnsetSize * 2)
        return o;
    juce::dsp::FFT fft (kOnsetOrder);
    juce::dsp::WindowingFunction<float> window ((size_t) kOnsetSize, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> frame ((size_t) kOnsetSize * 2), prev ((size_t) kOnsetSize / 2 + 1, 0.0f);
    const int lowBins = juce::jmax (2, (int) std::round (180.0 / (m.rate / kOnsetSize)));
    const int frames = ((int) m.x.size() - kOnsetSize) / kHop;
    o.all.resize ((size_t) frames);
    o.low.resize ((size_t) frames);
    for (int f = 0; f < frames; ++f)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        std::copy_n (m.x.begin() + f * kHop, kOnsetSize, frame.begin());
        window.multiplyWithWindowingTable (frame.data(), (size_t) kOnsetSize);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        float flux = 0, lowFlux = 0;
        for (int b = 1; b <= kOnsetSize / 2; ++b)
        {
            const float v = std::log1p (100.0f * frame[(size_t) b]);
            const float rise = juce::jmax (0.0f, v - prev[(size_t) b]);
            flux += rise;
            if (b <= lowBins)
                lowFlux += rise;
            prev[(size_t) b] = v;
        }
        o.all[(size_t) f] = flux;
        o.low[(size_t) f] = lowFlux;
    }
    // Only what stands out from its half second around it.
    const auto lift = [&o] (std::vector<float>& e)
    {
        const int half = juce::jmax (1, (int) (o.fps * 0.25));
        std::vector<double> sum (e.size() + 1, 0.0);
        for (size_t i = 0; i < e.size(); ++i)
            sum[i + 1] = sum[i] + e[i];
        std::vector<float> out (e.size());
        for (int i = 0; i < (int) e.size(); ++i)
        {
            const int a = juce::jmax (0, i - half), b = juce::jmin ((int) e.size(), i + half + 1);
            out[(size_t) i] = juce::jmax (0.0f, e[(size_t) i] - (float) ((sum[(size_t) b] - sum[(size_t) a]) / (b - a)));
        }
        e = std::move (out);
    };
    lift (o.all);
    lift (o.low);
    return o;
}

float at (const std::vector<float>& e, double frame)
{
    const int i = (int) std::round (frame);
    return juce::isPositiveAndBelow (i, (int) e.size()) ? e[(size_t) i] : 0.0f;
}

// How well beats every `period` frames from `phase` land on the onsets.
double gridScore (const std::vector<float>& e, double phase, double period, int every = 1, int offsetBeats = 0)
{
    double s = 0;
    int count = 0;
    for (double t = phase + offsetBeats * period; t < (double) e.size(); t += period * every)
    {
        // (a frame either side: the grid is rarely exactly on a frame)
        s += juce::jmax (at (e, t), juce::jmax (at (e, t - 1), at (e, t + 1)) * 0.7f);
        ++count;
    }
    return count > 0 ? s / count : 0.0;
}

double bestPhase (const std::vector<float>& e, double period, double& score)
{
    double best = 0;
    score = -1;
    const int steps = juce::jmax (8, (int) std::ceil (period));
    for (int k = 0; k < steps; ++k)
    {
        const double phase = period * k / steps;
        const double s = gridScore (e, phase, period);
        if (s > score)
            score = s, best = phase;
    }
    return best;
}

double fold (double bpm)
{
    if (bpm <= 0)
        return 0;
    while (bpm < 70.0)
        bpm *= 2.0;
    while (bpm > 180.0)
        bpm /= 2.0;
    return bpm;
}

double tempoFrom (const Onsets& o)
{
    const auto& e = o.all;
    if (e.size() < 64)
        return 0;
    const int minLag = (int) std::floor (o.fps * 60.0 / 200.0), maxLag = (int) std::ceil (o.fps * 60.0 / 55.0);
    if ((int) e.size() <= maxLag * 3)
        return 0;
    std::vector<double> ac ((size_t) maxLag * 2 + 2, 0.0);
    for (int lag = minLag; lag <= juce::jmin (maxLag * 2 + 1, (int) e.size() - 1); ++lag)
    {
        double s = 0;
        for (size_t i = 0; i + (size_t) lag < e.size(); ++i)
            s += (double) e[i] * e[i + (size_t) lag];
        ac[(size_t) lag] = s / (double) (e.size() - (size_t) lag);
    }
    int bestLag = -1;
    double best = 0;
    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        const double bpm = o.fps * 60.0 / lag;
        // (people hear most songs between about 80 and 160)
        const double prior = std::exp (-0.5 * std::pow (std::log2 (bpm / 115.0) / 0.9, 2.0));
        const double s = (ac[(size_t) lag] + 0.5 * ac[(size_t) juce::jmin ((int) ac.size() - 1, lag * 2)]) * prior;
        if (s > best)
            best = s, bestLag = lag;
    }
    if (bestLag < 0)
        return 0;
    double lag = bestLag;
    if (bestLag > minLag && bestLag < maxLag)
    {
        const double a = ac[(size_t) bestLag - 1], b = ac[(size_t) bestLag], c = ac[(size_t) bestLag + 1];
        const double d = a - 2 * b + c;
        if (std::abs (d) > 1.0e-12)
            lag += juce::jlimit (-0.5, 0.5, 0.5 * (a - c) / d);
    }
    double bpm = fold (o.fps * 60.0 / lag);

    // Finer: the tempo whose beat grid lands on the most onsets over the whole song.
    double bestBpm = bpm, bestScore = -1;
    for (double t = bpm * 0.97; t <= bpm * 1.03; t += 0.02)
    {
        double s = 0;
        bestPhase (e, o.fps * 60.0 / t, s);
        if (s > bestScore)
            bestScore = s, bestBpm = t;
    }
    bpm = bestBpm;
    // Most songs are made at a whole number.
    if (std::abs (bpm - std::round (bpm)) < 0.15)
        return std::round (bpm);
    return std::round (bpm * 10.0) / 10.0;
}

double firstHitFrom (const Mono& m)
{
    if (m.x.empty())
        return 0;
    const int hop = kHop;
    std::vector<float> rms ((size_t) (m.x.size() / (size_t) hop));
    float top = 0;
    for (size_t f = 0; f < rms.size(); ++f)
    {
        double s = 0;
        for (int k = 0; k < hop; ++k)
            s += (double) m.x[f * (size_t) hop + (size_t) k] * m.x[f * (size_t) hop + (size_t) k];
        rms[f] = (float) std::sqrt (s / hop);
        top = juce::jmax (top, rms[f]);
    }
    if (top <= 1.0e-6f)
        return 0;
    for (size_t f = 0; f < rms.size(); ++f)
        if (rms[f] > top * 0.06f)
            return (double) f * hop / m.rate;
    return 0;
}

// The exact moment of the hit nearest `t` (within 70 ms): where the sound jumps up most sharply.
double sharpen (const Mono& m, double t)
{
    constexpr int hop = 32, window = 64;
    const int from = juce::jmax (window, (int) ((t - 0.07) * m.rate)), to = juce::jmin ((int) m.x.size() - window, (int) ((t + 0.07) * m.rate));
    if (to <= from)
        return t;
    const auto energy = [&m] (int at)
    {
        double s = 0;
        for (int k = 0; k < window; ++k)
            s += (double) m.x[(size_t) (at + k)] * m.x[(size_t) (at + k)];
        return s;
    };
    double best = 0, bestAt = t;
    for (int at = from; at < to; at += hop)
    {
        const double rise = energy (at) - energy (at - window);
        if (rise > best)
            best = rise, bestAt = (double) at / m.rate;
    }
    return bestAt;
}

double firstBeatFrom (const Mono& m, const Onsets& o, double bpm, int beatsPerBar)
{
    if (bpm <= 0 || o.all.empty())
        return firstHitFrom (m);
    const double period = o.fps * 60.0 / bpm;
    // The beat is where the low end hits (kick and bass) more than the hi-hats between.
    std::vector<float> beats (o.all.size());
    {
        double lowMean = 1.0e-9, allMean = 1.0e-9;
        for (size_t i = 0; i < o.all.size(); ++i)
            lowMean += o.low[i], allMean += o.all[i];
        lowMean /= (double) o.all.size();
        allMean /= (double) o.all.size();
        for (size_t i = 0; i < o.all.size(); ++i)
            beats[i] = (float) (o.low[i] / lowMean * 2.0 + o.all[i] / allMean * 0.5);
    }
    double score = 0;
    const double phase = bestPhase (beats, period, score);
    // Which beat of the bar is the downbeat: where the kick and bass hit hardest.
    const int bpb = juce::jmax (1, beatsPerBar);
    int down = 0;
    double best = -1;
    for (int b = 0; b < bpb; ++b)
    {
        const double s = gridScore (o.low, phase, period, bpb, b);
        if (s > best)
            best = s, down = b;
    }
    const double barFrames = period * bpb;
    // (a frame's flux shows a hit that's well inside its window: about half a window after its start)
    const double latency = (kOnsetSize * 0.5) / m.rate * o.fps;
    const double start = firstHitFrom (m) * o.fps;
    double bar1 = phase + down * period + latency;
    while (bar1 < start - period * 0.35)
        bar1 += barFrames;
    while (bar1 - barFrames >= start - period * 0.35)
        bar1 -= barFrames;
    return juce::jmax (0.0, sharpen (m, bar1 / o.fps));
}

double pearson (const double* a, const double* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i)
        ma += a[i], mb += b[i];
    ma /= n, mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i)
    {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return saa > 0 && sbb > 0 ? sab / std::sqrt (saa * sbb) : 0.0;
}

int keyFrom (const Mono& m, bool& minor)
{
    minor = false;
    constexpr int order = 12, size = 1 << order;
    if ((int) m.x.size() < size * 4)
        return -1;
    juce::dsp::FFT fft (order);
    juce::dsp::WindowingFunction<float> window ((size_t) size, juce::dsp::WindowingFunction<float>::hann);
    std::vector<float> frame ((size_t) size * 2);
    double chroma[12] {};
    const double binHz = m.rate / size;
    // (about every half second is plenty for a whole song)
    const int hop = juce::jmax (size, (int) (m.rate * 0.5));
    for (size_t start = 0; start + (size_t) size <= m.x.size(); start += (size_t) hop)
    {
        std::fill (frame.begin(), frame.end(), 0.0f);
        std::copy_n (m.x.begin() + (std::ptrdiff_t) start, size, frame.begin());
        window.multiplyWithWindowingTable (frame.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (frame.data(), true);
        for (int b = 1; b < size / 2; ++b)
        {
            const double hz = b * binHz;
            if (hz < 60.0 || hz > 2000.0)
                continue;
            const double pitch = 69.0 + 12.0 * std::log2 (hz / 440.0);
            const int pc = ((int) std::round (pitch) % 12 + 12) % 12;
            chroma[pc] += std::sqrt ((double) frame[(size_t) b]);
        }
    }
    static const double major[12] { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorProfile[12] { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    int bestKey = -1;
    double best = 0.3;   // below this it's no key in particular (drums, noise)
    for (int key = 0; key < 12; ++key)
        for (int mode = 0; mode < 2; ++mode)
        {
            double rotated[12];
            for (int i = 0; i < 12; ++i)
                rotated[i] = (mode == 0 ? major : minorProfile)[((i - key) % 12 + 12) % 12];
            const double r = pearson (chroma, rotated, 12);
            if (r > best)
                best = r, bestKey = key, minor = mode == 1;
        }
    return bestKey;
}
}

//==============================================================================
Result analyse (const juce::AudioBuffer<float>& audio, double rate, int beatsPerBar)
{
    Result r;
    const auto m = mono (audio, rate);
    const auto o = onsets (m);
    r.bpm = tempoFrom (o);
    r.firstBeat = firstBeatFrom (m, o, r.bpm, beatsPerBar);
    r.key = keyFrom (m, r.minor);
    return r;
}

double guessBpm (const juce::AudioBuffer<float>& audio, double rate)
{
    return tempoFrom (onsets (mono (audio, rate)));
}

double guessFirstBeat (const juce::AudioBuffer<float>& audio, double rate, double bpm, int beatsPerBar)
{
    const auto m = mono (audio, rate);
    return firstBeatFrom (m, onsets (m), bpm, beatsPerBar);
}

double firstHit (const juce::AudioBuffer<float>& audio, double rate)
{
    return firstHitFrom (mono (audio, rate));
}

int guessKey (const juce::AudioBuffer<float>& audio, double rate, bool& minor)
{
    return keyFrom (mono (audio, rate), minor);
}

double bpmFromName (const juce::String& fileName)
{
    auto text = juce::File::createFileWithoutCheckingPath (fileName).getFileNameWithoutExtension();
    if (text.isEmpty())
        text = fileName;
    text = text.replaceCharacters ("_-.()[]", "       ");
    // "94bpm", "94 BPM", and "bpm 94" too
    auto filter = crate::Tempo::takeFromSearch (text);
    if (filter.isActive() && filter.target >= 40.0 && filter.target <= 240.0)
        return filter.target;
    auto words = juce::StringArray::fromTokens (text.toLowerCase(), " ", "");
    words.removeEmptyStrings();
    for (int i = 0; i + 1 < words.size(); ++i)
        if (words[i] == "bpm")
            if (const double v = words[i + 1].getDoubleValue(); v >= 40.0 && v <= 240.0 && words[i + 1].containsOnly ("0123456789."))
                return v;
    return 0;
}

juce::String keyName (int key, bool minor)
{
    static const char* names[12] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    if (! juce::isPositiveAndBelow (key, 12))
        return {};
    return juce::String (names[key]) + (minor ? " minor" : " major");
}

std::vector<Section> guessSections (const juce::AudioBuffer<float>& audio, double rate, double bpm, double firstBeat, int beatsPerBar)
{
    std::vector<Section> out;
    if (bpm <= 0)
        return out;
    const auto m = mono (audio, rate);
    const double barSeconds = 60.0 / bpm * juce::jmax (1, beatsPerBar);
    const int bars = (int) std::floor (((double) m.x.size() / m.rate - firstBeat) / barSeconds + 0.25);
    if (bars < 8)
        return out;
    constexpr int phrase = 8;
    std::vector<double> energy;
    for (int p = 0; p * phrase < bars; ++p)
    {
        const int from = (int) ((firstBeat + p * phrase * barSeconds) * m.rate);
        const int to = juce::jmin ((int) m.x.size(), (int) ((firstBeat + juce::jmin (bars, (p + 1) * phrase) * barSeconds) * m.rate));
        double s = 0;
        for (int i = juce::jmax (0, from); i < to; ++i)
            s += (double) m.x[(size_t) i] * m.x[(size_t) i];
        energy.push_back (to > from ? std::sqrt (s / (to - from)) : 0.0);
    }
    // A short last phrase joins the one before.
    if (energy.size() > 1 && bars % phrase != 0 && bars % phrase < 4)
        energy.pop_back();
    const double top = *std::max_element (energy.begin(), energy.end());
    if (top <= 1.0e-6)
        return out;
    std::vector<juce::String> labels;
    for (size_t p = 0; p < energy.size(); ++p)
    {
        const double e = energy[p] / top;
        juce::String l;
        if (e >= 0.86)
            l = "Hook";
        else if (p == 0 && e < 0.75)
            l = "Intro";
        else if (p + 1 == energy.size() && e < 0.75)
            l = "Outro";
        else if (e < 0.6)
            l = "Break";
        else
            l = "Verse";
        labels.push_back (l);
    }
    std::map<juce::String, int> seen;
    for (size_t p = 0; p < labels.size();)
    {
        size_t q = p;
        while (q + 1 < labels.size() && labels[q + 1] == labels[p])
            ++q;
        Section s;
        s.bar = (double) p * phrase;
        s.bars = q + 1 == labels.size() ? bars - s.bar : (double) (q + 1 - p) * phrase;
        const int n = ++seen[labels[p]];
        s.name = n == 1 ? labels[p] : labels[p] + " " + juce::String (n);
        out.push_back (s);
        p = q + 1;
    }
    return out;
}

double tapTempo (const std::vector<double>& taps)
{
    if (taps.size() < 3)
        return 0;
    // the last run (taps under 2 s apart), its last 8
    size_t first = taps.size() - 1;
    while (first > 0 && taps[first] - taps[first - 1] < 2.0 && taps.size() - first < 8)
        --first;
    const size_t count = taps.size() - first;
    if (count < 3)
        return 0;
    const double span = taps.back() - taps[first];
    if (span <= 0)
        return 0;
    const double bpm = 60.0 * (double) (count - 1) / span;
    return bpm >= 30.0 && bpm <= 300.0 ? std::round (bpm * 10.0) / 10.0 : 0.0;
}
}
