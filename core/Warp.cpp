#include "Warp.h"

#include "SoundFx.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>
#include <complex>

namespace crate::Warp
{
namespace
{
constexpr double kGap = 1.0 / 64.0;   // beats: the closest two markers (or a marker and an end) get

// Output sample -> source sample, through the plan's markers, round and round: past the end of
// the warped loop it carries on into the next pass of the recording.
struct Map
{
    std::vector<double> out, src;   // anchors in samples, ends included
    double outLen = 1, srcLen = 1;

    double operator() (double o) const
    {
        const double wraps = std::floor (o / outLen);
        const double r = o - wraps * outLen;
        auto it = std::upper_bound (out.begin(), out.end(), r);
        const size_t k = (size_t) juce::jlimit<long> (1, (long) out.size() - 1, (long) (it - out.begin()));
        const double a = out[k - 1], b = out[k];
        const double t = b > a ? (r - a) / (b - a) : 0.0;
        return wraps * srcLen + src[k - 1] + t * (src[k] - src[k - 1]);
    }
};

Map makeMap (const Plan& p, int outLen, int srcLen)
{
    Map m;
    m.outLen = juce::jmax (1, outLen);
    m.srcLen = juce::jmax (1, srcLen);
    m.out.push_back (0);
    m.src.push_back (0);
    for (const auto& mk : p.markers)
    {
        m.out.push_back (mk.target / p.targetBeats * outLen);
        m.src.push_back (mk.source / p.sourceBeats * srcLen);
    }
    m.out.push_back (outLen);
    m.src.push_back (srcLen);
    return m;
}

Map straightMap (int length)
{
    Map m;
    m.outLen = m.srcLen = juce::jmax (1, length);
    m.out = { 0.0, (double) m.outLen };
    m.src = m.out;
    return m;
}

// The recording read round and round from `start` (what the stretcher takes as input).
struct RoundIn
{
    const juce::AudioBuffer<float>& b;
    long long start;

    struct Channel
    {
        const float* d;
        long long n, start;
        float operator[] (int i) const
        {
            long long k = (start + i) % n;
            if (k < 0)
                k += n;
            return d[k];
        }
    };
    Channel operator[] (int c) const
    {
        return { b.getReadPointer (juce::jmin (c, b.getNumChannels() - 1)), (long long) b.getNumSamples(), start };
    }
};

struct BlockOut
{
    juce::AudioBuffer<float>& b;
    float* operator[] (int c) { return b.getWritePointer (c); }
};

// A sample between samples (Catmull-Rom), the recording read round and round.
float readRound (const float* d, long long n, double pos)
{
    const double f = std::floor (pos);
    const float t = (float) (pos - f);
    const auto at = [&] (long long k)
    {
        k %= n;
        if (k < 0)
            k += n;
        return d[k];
    };
    const long long i = (long long) f;
    const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}

// VOICE: a phase vocoder fed as fast as the map runs through the recording, so its pitch stays
// (and transpose and formant can move it). It starts a little before the loop's top, reading
// the end of the recording, so the warped loop joins itself cleanly.
juce::AudioBuffer<float> stretchAlong (const juce::AudioBuffer<float>& src, const Map& map, int outLen, double sampleRate,
                                       float transpose, float formant)
{
    const int channels = juce::jmax (1, src.getNumChannels());
    juce::AudioBuffer<float> out (channels, outLen);
    out.clear();
    if (src.getNumSamples() < 16 || outLen <= 0)
        return out;

    signalsmith::stretch::SignalsmithStretch<float> st;
    st.presetDefault (channels, (float) sampleRate);
    st.setTransposeSemitones (transpose);
    if (transpose != 0.0f || formant != 0.0f)
        st.setFormantSemitones (formant, true);   // true: the voice's character stays put when it's transposed

    constexpr int block = 256;
    const int warmUp = block * 48;
    const auto rateAt = [&] (double o) { return juce::jmax (1.0e-3, (map (o + block) - map (o)) / block); };

    double o = -warmUp;
    long long fed = std::llround (map (o));
    const int seek = st.outputSeekLength ((float) rateAt (o));
    st.outputSeek (RoundIn { src, fed }, seek);
    fed += seek;

    juce::AudioBuffer<float> chunk (channels, block);
    for (long long at = -warmUp; at < outLen; at += block)
    {
        const double next = (double) (at + block);
        const double lead = st.inputLatency() + rateAt (next) * st.outputLatency();
        const long long want = std::llround (map (next) + lead);
        const int take = (int) juce::jmax<long long> (0, want - fed);
        st.process (RoundIn { src, fed }, take, BlockOut { chunk }, block);
        fed += take;
        for (int i = 0; i < block; ++i)
        {
            const long long k = at + i;
            if (k >= 0 && k < outLen)
                for (int c = 0; c < channels; ++c)
                    out.setSample (c, (int) k, chunk.getSample (c, i));
        }
    }
    return out;
}

// TAPE: read along the map at whatever speed it runs, so the pitch goes with it.
juce::AudioBuffer<float> tapeAlong (const juce::AudioBuffer<float>& src, const Map& map, int outLen)
{
    const int channels = juce::jmax (1, src.getNumChannels());
    juce::AudioBuffer<float> out (channels, outLen);
    const long long n = src.getNumSamples();
    for (int c = 0; c < channels; ++c)
    {
        const float* d = src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1));
        float* w = out.getWritePointer (c);
        for (int i = 0; i < outLen; ++i)
            w[i] = readRound (d, n, map (i));
    }
    return out;
}

// BEATS: each piece between two markers starts exactly on its marker and plays at its own
// speed; a piece too long is cut with a short fade, a short one leaves a little silence.
juce::AudioBuffer<float> beatsAlong (const juce::AudioBuffer<float>& src, const Map& map, int outLen, double sampleRate)
{
    const int channels = juce::jmax (1, src.getNumChannels());
    juce::AudioBuffer<float> out (channels, outLen);
    out.clear();
    const long long n = src.getNumSamples();
    const int fade = juce::jmax (8, (int) (0.004 * sampleRate));
    for (size_t k = 0; k + 1 < map.out.size(); ++k)
    {
        const long long outA = std::llround (map.out[k]), outB = std::llround (map.out[k + 1]);
        const long long srcA = std::llround (map.src[k]), srcB = std::llround (map.src[k + 1]);
        const long long len = juce::jmin (outB - outA, srcB - srcA);
        if (len <= 0)
            continue;
        const int f = (int) juce::jmin<long long> (fade, len / 4);
        for (int c = 0; c < channels; ++c)
        {
            const float* d = src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1));
            float* w = out.getWritePointer (c);
            for (long long j = 0; j < len; ++j)
            {
                const long long o = outA + j;
                if (o < 0 || o >= outLen)
                    continue;
                long long s = (srcA + j) % n;
                if (s < 0)
                    s += n;
                float g = 1.0f;
                if (j >= len - f)
                    g = (float) (len - j) / (float) juce::jmax (1, f);
                w[o] += d[s] * g;
            }
        }
    }
    return out;
}

// TEXTURE: overlapping grains read along the map with a little wander.
juce::AudioBuffer<float> grainsAlong (const juce::AudioBuffer<float>& src, const Map& map, int outLen, double sampleRate)
{
    const int channels = juce::jmax (1, src.getNumChannels());
    juce::AudioBuffer<float> out (channels, outLen);
    out.clear();
    const long long n = src.getNumSamples();
    const int grain = juce::jmax (64, ((int) (0.09 * sampleRate)) & ~1);
    const int hop = grain / 4;
    std::vector<float> window ((size_t) grain);
    for (int j = 0; j < grain; ++j)
        window[(size_t) j] = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * (float) j / (float) grain));
    juce::Random random (0x5eed);
    const double wander = 0.012 * sampleRate;
    for (long long centre = 0; centre < outLen; centre += hop)
    {
        const double from = map ((double) centre) + (random.nextDouble() * 2.0 - 1.0) * wander - grain / 2;
        for (int c = 0; c < channels; ++c)
        {
            const float* d = src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1));
            float* w = out.getWritePointer (c);
            for (int j = 0; j < grain; ++j)
            {
                long long o = (centre - grain / 2 + j) % outLen;
                if (o < 0)
                    o += outLen;
                w[o] += 0.5f * window[(size_t) j] * readRound (d, n, from + j);
            }
        }
    }
    return out;
}

double interpolate (const std::vector<std::pair<double, double>>& pts, double x)
{
    if (pts.size() < 2)
        return x;
    for (size_t k = 1; k < pts.size(); ++k)
        if (x <= pts[k].first || k + 1 == pts.size())
        {
            const auto [x0, y0] = pts[k - 1];
            const auto [x1, y1] = pts[k];
            return x1 > x0 ? y0 + (x - x0) / (x1 - x0) * (y1 - y0) : y0;
        }
    return x;
}
}

juce::String styleName (Style s)
{
    switch (s)
    {
        case Style::voice: return "VOICE";
        case Style::beats: return "BEATS";
        case Style::texture: return "TEXTURE";
        case Style::tape: return "TAPE";
    }
    return {};
}

bool Plan::isStraight() const
{
    if (settings.transpose != 0.0f || settings.formant != 0.0f || ! isValid())
        return false;
    for (const auto& m : markers)
        if (std::abs (m.target - m.source * targetBeats / sourceBeats) > 1.0e-6)
            return false;
    return true;
}

double Plan::sourceAt (double targetBeat) const
{
    std::vector<std::pair<double, double>> pts { { 0.0, 0.0 } };
    for (const auto& m : markers)
        pts.push_back ({ m.target, m.source });
    pts.push_back ({ targetBeats, sourceBeats });
    return interpolate (pts, targetBeat);
}

double Plan::targetAt (double sourceBeat) const
{
    std::vector<std::pair<double, double>> pts { { 0.0, 0.0 } };
    for (const auto& m : markers)
        pts.push_back ({ m.source, m.target });
    pts.push_back ({ sourceBeats, targetBeats });
    return interpolate (pts, sourceBeat);
}

juce::var Plan::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("sourceBeats", sourceBeats);
    o->setProperty ("targetBeats", targetBeats);
    o->setProperty ("style", (int) settings.style);
    o->setProperty ("transpose", settings.transpose);
    o->setProperty ("formant", settings.formant);
    juce::Array<juce::var> list;
    for (const auto& m : markers)
        list.add (juce::Array<juce::var> { m.source, m.target, m.manual });
    o->setProperty ("markers", list);
    return o;
}

Plan Plan::fromVar (const juce::var& v)
{
    Plan p;
    p.sourceBeats = v.getProperty ("sourceBeats", 0.0);
    p.targetBeats = v.getProperty ("targetBeats", 0.0);
    p.settings.style = (Style) juce::jlimit (0, 3, (int) v.getProperty ("style", 0));
    p.settings.transpose = juce::jlimit (-12.0f, 12.0f, (float) v.getProperty ("transpose", 0.0f));
    p.settings.formant = juce::jlimit (-12.0f, 12.0f, (float) v.getProperty ("formant", 0.0f));
    if (auto* list = v.getProperty ("markers", {}).getArray())
        for (const auto& m : *list)
            if (m.isArray() && m.size() >= 3)
                p.markers.push_back ({ (double) m[0], (double) m[1], (bool) m[2] });
    tidy (p);
    return p;
}

void tidy (Plan& p)
{
    if (! p.isValid())
    {
        p.markers.clear();
        return;
    }
    auto byTarget = p.markers;
    std::stable_sort (byTarget.begin(), byTarget.end(), [] (const Marker& a, const Marker& b) { return a.manual > b.manual; });
    std::vector<Marker> kept;
    for (const auto& m : byTarget)
    {
        if (m.source <= kGap || m.source >= p.sourceBeats - kGap || m.target <= kGap || m.target >= p.targetBeats - kGap)
            continue;
        auto at = std::lower_bound (kept.begin(), kept.end(), m, [] (const Marker& a, const Marker& b) { return a.source < b.source; });
        const bool afterOk = at == kept.begin() || (m.source > std::prev (at)->source + kGap && m.target > std::prev (at)->target + kGap);
        const bool beforeOk = at == kept.end() || (m.source < at->source - kGap && m.target < at->target - kGap);
        if (afterOk && beforeOk)
            kept.insert (at, m);
    }
    p.markers = std::move (kept);
}

void setLength (Plan& p, double targetBeats)
{
    if (targetBeats <= 0 || ! p.isValid())
        return;
    const double scale = targetBeats / p.targetBeats;
    for (auto& m : p.markers)
        m.target *= scale;
    p.targetBeats = targetBeats;
    tidy (p);
}

std::vector<double> findHits (const juce::AudioBuffer<float>& b, double sourceBeats, float sensitivity)
{
    std::vector<double> beats;
    const int n = b.getNumSamples();
    if (n == 0 || sourceBeats <= 0)
        return beats;
    for (int s : SoundFx::findHits (b, sensitivity, 128))
        beats.push_back ((double) s / n * sourceBeats);
    return beats;
}

void snap (Plan& p, const std::vector<double>& hits, double gridBeats, float strength)
{
    p.markers.erase (std::remove_if (p.markers.begin(), p.markers.end(), [] (const Marker& m) { return ! m.manual; }), p.markers.end());
    if (! p.isValid() || gridBeats <= 0 || strength <= 0.0f)
    {
        tidy (p);
        return;
    }
    const Plan handOnly = p;
    for (double h : hits)
    {
        bool nearHand = false;
        for (const auto& m : handOnly.markers)
            nearHand = nearHand || std::abs (m.source - h) < 1.0 / 32.0;
        if (nearHand)
            continue;
        const double now = handOnly.targetAt (h);
        const double line = std::round (now / gridBeats) * gridBeats;
        p.markers.push_back ({ h, now + juce::jlimit (0.0f, 1.0f, strength) * (line - now), false });
    }
    tidy (p);
}

double moveMarker (Plan& p, int index, double targetBeat)
{
    if (! juce::isPositiveAndBelow (index, (int) p.markers.size()))
        return targetBeat;
    const double lo = (index > 0 ? p.markers[(size_t) index - 1].target : 0.0) + kGap;
    const double hi = (index + 1 < (int) p.markers.size() ? p.markers[(size_t) index + 1].target : p.targetBeats) - kGap;
    auto& m = p.markers[(size_t) index];
    m.target = juce::jlimit (lo, juce::jmax (lo, hi), targetBeat);
    m.manual = true;
    return m.target;
}

int addMarker (Plan& p, double targetBeat)
{
    if (! p.isValid())
        return -1;
    const double source = p.sourceAt (targetBeat);
    p.markers.push_back ({ source, targetBeat, true });
    tidy (p);
    for (int i = 0; i < (int) p.markers.size(); ++i)
        if (std::abs (p.markers[(size_t) i].source - source) < 1.0e-9)
            return i;
    return -1;
}

void removeMarker (Plan& p, int index)
{
    if (juce::isPositiveAndBelow (index, (int) p.markers.size()))
        p.markers.erase (p.markers.begin() + index);
}

double soundsLikeBpm (const std::vector<double>& hitSeconds, double songBpm)
{
    if (hitSeconds.size() < 4)
        return 0.0;
    double best = 0, bestScore = 0;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.05)
    {
        std::complex<double> sum;
        const double f = bpm / 60.0 * juce::MathConstants<double>::twoPi;
        for (double t : hitSeconds)
            sum += std::polar (1.0, f * t);
        const double score = std::abs (sum) / (double) hitSeconds.size();
        if (score > bestScore + 1.0e-9)
        {
            bestScore = score;
            best = bpm;
        }
    }
    if (bestScore < 0.55)
        return 0.0;
    if (songBpm > 0)
    {
        double pick = best;
        for (double c : { best * 0.5, best * 2.0 })
            if (c >= 50.0 && c <= 240.0 && std::abs (std::log (c / songBpm)) < std::abs (std::log (pick / songBpm)))
                pick = c;
        best = pick;
    }
    return std::round (best * 10.0) / 10.0;
}

juce::AudioBuffer<float> render (const juce::AudioBuffer<float>& source, const Plan& plan, int lengthSamples, double sampleRate)
{
    const int n = source.getNumSamples();
    if (n == 0 || lengthSamples <= 0 || ! plan.isValid() || sampleRate <= 0)
        return {};
    if (plan.isStraight() && lengthSamples == n)
        return source;

    const auto map = makeMap (plan, lengthSamples, n);
    const auto& s = plan.settings;
    switch (s.style)
    {
        case Style::voice: return stretchAlong (source, map, lengthSamples, sampleRate, s.transpose, s.formant);
        case Style::beats:
        case Style::texture:
        case Style::tape: break;
    }
    auto out = s.style == Style::beats ? beatsAlong (source, map, lengthSamples, sampleRate)
             : s.style == Style::texture ? grainsAlong (source, map, lengthSamples, sampleRate)
                                         : tapeAlong (source, map, lengthSamples);
    if (s.transpose != 0.0f || s.formant != 0.0f)
        out = stretchAlong (out, straightMap (lengthSamples), lengthSamples, sampleRate, s.transpose, s.formant);
    return out;
}
}
