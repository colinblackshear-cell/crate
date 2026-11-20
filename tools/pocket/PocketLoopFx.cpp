#include "PocketLoopFx.h"

using namespace pocket;

namespace
{
constexpr float kDeadZone = 0.03f;   // filter knob within this of the middle = off
constexpr float kDelayFeedback = 0.42f, kDelayReturn = 0.8f, kReverbReturn = 1.0f;
constexpr int kBusBlock = 1024;

float volumeGain (float v) { const float r = juce::jmax (0.0f, v) / 0.75f; return r * r; }   // 0.75 = 0 dB, top = +5 dB

// < 0 = low-pass at -cutoff, > 0 = high-pass at cutoff, 0 = off
float filterCutoff (float f)
{
    if (f < 0.5f - kDeadZone)
    {
        const float t = (0.5f - kDeadZone - f) / (0.5f - kDeadZone);
        return -20000.0f * std::pow (150.0f / 20000.0f, t);
    }
    if (f > 0.5f + kDeadZone)
    {
        const float t = (f - 0.5f - kDeadZone) / (0.5f - kDeadZone);
        return 20.0f * std::pow (4000.0f / 20.0f, t);
    }
    return 0.0f;
}

juce::String hz (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (f)); }
}

const char* pocket::fxKnobName (int k)
{
    static const char* const names[kFxKnobs] = { "VOLUME", "FILTER", "DELAY", "REVERB", "DRIVE" };
    return names[juce::jlimit (0, kFxKnobs - 1, k)];
}

float pocket::fxDefault (int k) { return k == fxVolume ? 0.75f : (k == fxFilter ? 0.5f : 0.0f); }

juce::String pocket::fxValueText (int k, float v)
{
    switch (k)
    {
        case fxVolume:
        {
            const float g = volumeGain (v);
            if (g < 0.001f)
                return "OFF";
            const float db = juce::Decibels::gainToDecibels (g);
            return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
        }
        case fxFilter:
        {
            const float c = filterCutoff (v);
            return c == 0.0f ? juce::String ("OFF") : (c < 0 ? "LP " + hz (-c) : "HP " + hz (c));
        }
        default: return v < 0.005f ? juce::String ("OFF") : juce::String (juce::roundToInt (v * 100.0f)) + "%";
    }
}

//==============================================================================
bool LayerFxState::isNeutral() const
{
    for (int k = 0; k < kFxKnobs; ++k)
        if (! moves[(size_t) k].empty() || std::abs (knob[(size_t) k] - fxDefault (k)) > 1.0e-4f)
            return false;
    return true;
}

float LayerFxState::valueAt (int k, double frac) const
{
    const auto& m = moves[(size_t) k];
    if (m.size() != (size_t) kMoveBins)
        return knob[(size_t) k];
    frac -= std::floor (frac);
    const double p = frac * kMoveBins;
    const int a = juce::jmin (kMoveBins - 1, (int) p), b = (a + 1) % kMoveBins;
    const float t = (float) (p - a);
    return m[(size_t) a] + (m[(size_t) b] - m[(size_t) a]) * t;
}

void LayerFxState::remap (double from, double to, int times, bool reversed)
{
    times = juce::jmax (1, times);
    for (auto& m : moves)
    {
        if (m.size() != (size_t) kMoveBins)
            continue;
        std::vector<float> out ((size_t) kMoveBins);
        for (int j = 0; j < kMoveBins; ++j)
        {
            double t = (double) j / kMoveBins * times;
            t -= std::floor (t);
            if (reversed)
                t = 1.0 - t;
            const double src = (from + t * (to - from)) * kMoveBins;
            const int a = juce::jlimit (0, kMoveBins - 1, (int) src), b = juce::jmin (kMoveBins - 1, a + 1);
            const float f = (float) juce::jlimit (0.0, 1.0, src - a);
            out[(size_t) j] = m[(size_t) a] + (m[(size_t) b] - m[(size_t) a]) * f;
        }
        m = std::move (out);
    }
}

//==============================================================================
void FxDsp::process (float* x, float* toDelay, float* toReverb, int n, const float* values, double sampleRate)
{
    if (n <= 0)
        return;
    const float tVol = volumeGain (values[fxVolume]), tDrive = values[fxDrive], tDly = values[fxDelay], tRev = values[fxReverb];
    const float inv = 1.0f / (float) n;
    const float dVol = (tVol - vol) * inv, dDrive = (tDrive - drive) * inv, dDly = (tDly - dly) * inv, dRev = (tRev - rev) * inv;

    const float cutoff = filterCutoff (values[fxFilter]);
    float a1 = 0, a2 = 0, a3 = 0, k = 0;
    const bool lowPass = cutoff < 0;
    if (cutoff == 0.0f)
    {
        filterOn = false;
        ic1 = ic2 = 0;
    }
    else
    {
        if (! filterOn)
            ic1 = ic2 = 0;
        filterOn = true;
        const float fc = juce::jlimit (10.0f, (float) sampleRate * 0.45f, std::abs (cutoff));
        const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) sampleRate);
        k = 1.0f / 0.9f;   // a touch of resonance
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    const bool driving = drive > 1.0e-4f || tDrive > 1.0e-4f;
    const bool sending = dly > 1.0e-5f || tDly > 1.0e-5f || rev > 1.0e-5f || tRev > 1.0e-5f;

    for (int i = 0; i < n; ++i)
    {
        float s = x[i];
        if (filterOn)
        {
            const float v3 = s - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            s = lowPass ? v2 : s - k * v1 - v2;
        }
        drive += dDrive;
        if (driving)
        {
            const float d = juce::jmax (0.0f, drive);
            const float pre = 1.0f + d * 15.0f;
            const float shaped = std::tanh (s * pre) / std::sqrt (pre);
            const float w = juce::jmin (1.0f, d * 10.0f);
            s += (shaped - s) * w;
        }
        vol += dVol;
        s *= vol;
        dly += dDly;
        rev += dRev;
        if (sending)
        {
            toDelay[i] += s * dly;
            toReverb[i] += s * rev;
        }
        x[i] = s;
    }
    vol = tVol;
    drive = tDrive;
    dly = tDly;
    rev = tRev;
}

//==============================================================================
void FxBus::prepare (double rate)
{
    sampleRate = rate;
    dL.assign ((size_t) (rate * 3.0) + 8, 0.0f);
    dR.assign (dL.size(), 0.0f);
    rL.assign ((size_t) kBusBlock, 0.0f);
    rR.assign ((size_t) kBusBlock, 0.0f);
    reverb.setSampleRate (rate);
    juce::Reverb::Parameters p;
    p.roomSize = 0.78f;
    p.damping = 0.4f;
    p.wetLevel = 0.6f;
    p.dryLevel = 0.0f;
    p.width = 1.0f;
    reverb.setParameters (p);
    clear();
}

void FxBus::clear()
{
    std::fill (dL.begin(), dL.end(), 0.0f);
    std::fill (dR.begin(), dR.end(), 0.0f);
    reverb.reset();
    write = 0;
    quiet = 1 << 30;
}

void FxBus::process (const float* toDelay, const float* toReverb, float* left, float* right, int n, double beatSamples)
{
    if (dL.empty())
        return;
    bool any = false;
    for (int i = 0; i < n && ! any; ++i)
        any = toDelay[i] != 0.0f || toReverb[i] != 0.0f;
    if (any)
        quiet = 0;
    else if (quiet < (1 << 30))
    {
        quiet += n;
        if (quiet > (int) (sampleRate * 10.0))
        {
            clear();   // the tails have died away
            return;
        }
    }
    else
        return;

    const int size = (int) dL.size();
    const int delay = juce::jlimit (1, size - 1, (int) (beatSamples * 0.75));
    for (int i = 0; i < n; ++i)
    {
        int r = write - delay;
        if (r < 0)
            r += size;
        const float a = dL[(size_t) r], b = dR[(size_t) r];
        dL[(size_t) write] = toDelay[i] + b * kDelayFeedback;   // ping-pong: left, then right
        dR[(size_t) write] = a * kDelayFeedback;
        if (++write >= size)
            write = 0;
        left[i] += a * kDelayReturn;
        if (right != nullptr)
            right[i] += b * kDelayReturn;
    }

    for (int off = 0; off < n; off += kBusBlock)
    {
        const int m = juce::jmin (kBusBlock, n - off);
        std::copy (toReverb + off, toReverb + off + m, rL.begin());
        std::copy (toReverb + off, toReverb + off + m, rR.begin());
        reverb.processStereo (rL.data(), rR.data(), m);
        for (int i = 0; i < m; ++i)
        {
            left[off + i] += rL[(size_t) i] * kReverbReturn;
            if (right != nullptr)
                right[off + i] += rR[(size_t) i] * kReverbReturn;
        }
    }
}

//==============================================================================
juce::AudioBuffer<float> pocket::renderLayer (const float* data, int length, double lengthBeats, double sampleRate, const LayerFxState& fx)
{
    juce::AudioBuffer<float> out (2, juce::jmax (1, length));
    out.clear();
    if (data == nullptr || length < 2 || lengthBeats <= 0)
        return out;

    FxDsp dsp;
    dsp.reset();
    FxBus bus;
    bus.prepare (sampleRate);
    const double beatSamples = (double) length / lengthBeats;
    constexpr int block = 256;
    std::vector<float> x (block), toD (block), toR (block), l (block), r (block);
    float values[kFxKnobs];

    // a warm-up through the last few seconds, so the echoes and reverb of the end carry into the start
    const int warm = juce::jmax (0, length - (int) (sampleRate * 8.0));
    for (int pass = 0; pass < 2; ++pass)
        for (int pos = pass == 0 ? warm : 0; pos < length; pos += block)
        {
            const int n = juce::jmin (block, length - pos);
            for (int k = 0; k < kFxKnobs; ++k)
                values[k] = fx.valueAt (k, (double) pos / length);
            std::copy (data + pos, data + pos + n, x.begin());
            std::fill (toD.begin(), toD.end(), 0.0f);
            std::fill (toR.begin(), toR.end(), 0.0f);
            dsp.process (x.data(), toD.data(), toR.data(), n, values, sampleRate);
            std::copy (x.begin(), x.begin() + n, l.begin());
            std::copy (x.begin(), x.begin() + n, r.begin());
            bus.process (toD.data(), toR.data(), l.data(), r.data(), n, beatSamples);
            if (pass == 1)
            {
                out.copyFrom (0, pos, l.data(), n);
                out.copyFrom (1, pos, r.data(), n);
            }
        }
    return out;
}
