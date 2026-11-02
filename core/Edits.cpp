#include "Edits.h"

namespace crate::Edits
{

namespace
{
    juce::Range<int> clip (const juce::AudioBuffer<float>& b, juce::Range<int> r)
    {
        return r.getIntersectionWith ({ 0, b.getNumSamples() });
    }

    // A short fade at a cut so it doesn't click (about 2 ms).
    constexpr int kDeclick = 96;

    juce::AudioBuffer<float> matchChannels (const juce::AudioBuffer<float>& b, int channels)
    {
        if (b.getNumChannels() == channels)
            return b;

        juce::AudioBuffer<float> out (channels, b.getNumSamples());
        if (channels == 1)
        {
            out.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
            out.addFrom (0, 0, b, 1, 0, b.getNumSamples());
            out.applyGain (0.5f);
        }
        else
        {
            for (int ch = 0; ch < channels; ++ch)
                out.copyFrom (ch, 0, b, 0, 0, b.getNumSamples());
        }
        return out;
    }
}

juce::Range<int> orWhole (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    r = clip (b, r);
    return r.isEmpty() ? juce::Range<int> { 0, b.getNumSamples() } : r;
}

float fadeShape (FadeCurve curve, float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    switch (curve)
    {
        case FadeCurve::linear: return t;
        case FadeCurve::smooth: return 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
        case FadeCurve::fast:   return 1.0f - (1.0f - t) * (1.0f - t);
        case FadeCurve::slow:   return t * t;
    }
    return t;
}

Result crop (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    r = clip (b, r);
    if (r.isEmpty())
        return { b, {} };

    juce::AudioBuffer<float> out (b.getNumChannels(), r.getLength());
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        out.copyFrom (ch, 0, b, ch, r.getStart(), r.getLength());
    return { std::move (out), { 0, r.getLength() } };
}

juce::AudioBuffer<float> copy (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    return crop (b, orWhole (b, r)).audio;
}

Result remove (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    r = clip (b, r);
    if (r.isEmpty())
        return { b, {} };
    if (r.getLength() >= b.getNumSamples())
        return { juce::AudioBuffer<float> (b.getNumChannels(), 0), {} };

    const int remaining = b.getNumSamples() - r.getLength();
    juce::AudioBuffer<float> out (b.getNumChannels(), remaining);
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        out.copyFrom (ch, 0, b, ch, 0, r.getStart());
        out.copyFrom (ch, r.getStart(), b, ch, r.getEnd(), b.getNumSamples() - r.getEnd());
    }

    // smooth the join
    const int at = r.getStart();
    const int before = juce::jmin (kDeclick, at), after = juce::jmin (kDeclick, remaining - at);
    if (before > 0 && after > 0)
    {
        out.applyGainRamp (at - before, before, 1.0f, 0.0f);
        out.applyGainRamp (at, after, 0.0f, 1.0f);
    }
    return { std::move (out), { at, at } };
}

Result silence (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    r = clip (b, r);
    juce::AudioBuffer<float> out (b);
    if (! r.isEmpty())
        out.clear (r.getStart(), r.getLength());
    return { std::move (out), r };
}

Result insert (const juce::AudioBuffer<float>& b, int at, const juce::AudioBuffer<float>& clipIn)
{
    if (b.getNumSamples() == 0)
        return { clipIn, { 0, clipIn.getNumSamples() } };

    at = juce::jlimit (0, b.getNumSamples(), at);
    const int channels = juce::jmax (b.getNumChannels(), clipIn.getNumChannels());
    auto base = matchChannels (b, channels);
    auto piece = matchChannels (clipIn, channels);

    juce::AudioBuffer<float> out (channels, base.getNumSamples() + piece.getNumSamples());
    for (int ch = 0; ch < channels; ++ch)
    {
        out.copyFrom (ch, 0, base, ch, 0, at);
        out.copyFrom (ch, at, piece, ch, 0, piece.getNumSamples());
        out.copyFrom (ch, at + piece.getNumSamples(), base, ch, at, base.getNumSamples() - at);
    }
    return { std::move (out), { at, at + piece.getNumSamples() } };
}

Result paste (const juce::AudioBuffer<float>& b, juce::Range<int> r, const juce::AudioBuffer<float>& clipIn)
{
    r = clip (b, r);
    if (r.isEmpty())
        return insert (b, r.getStart(), clipIn);
    auto removed = remove (b, r);
    return insert (removed.audio, r.getStart(), clipIn);
}

Result fade (const juce::AudioBuffer<float>& b, juce::Range<int> r, bool fadeIn, FadeCurve curve, float amount)
{
    r = clip (b, r);
    amount = juce::jlimit (0.0f, 1.0f, amount);
    juce::AudioBuffer<float> out (b);
    const int n = r.getLength();
    if (n > 0)
    {
        for (int ch = 0; ch < out.getNumChannels(); ++ch)
        {
            auto* d = out.getWritePointer (ch, r.getStart());
            for (int i = 0; i < n; ++i)
            {
                const float t = n > 1 ? (float) i / (float) (n - 1) : 1.0f;
                const float shape = fadeIn ? fadeShape (curve, t) : fadeShape (curve, 1.0f - t);
                d[i] *= 1.0f - amount * (1.0f - shape);
            }
        }
    }
    return { std::move (out), r };
}

Result reverse (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    const bool whole = clip (b, r).isEmpty();
    r = orWhole (b, r);
    juce::AudioBuffer<float> out (b);
    if (r.getLength() > 1)
        out.reverse (r.getStart(), r.getLength());
    return { std::move (out), whole ? juce::Range<int>() : r };
}

Result gain (const juce::AudioBuffer<float>& b, juce::Range<int> r, float decibels)
{
    const bool whole = clip (b, r).isEmpty();
    r = orWhole (b, r);
    juce::AudioBuffer<float> out (b);
    if (! r.isEmpty())
        out.applyGain (r.getStart(), r.getLength(), juce::Decibels::decibelsToGain (decibels));
    return { std::move (out), whole ? juce::Range<int>() : r };
}

Result normalize (const juce::AudioBuffer<float>& b, juce::Range<int> r, float peakDecibels)
{
    const auto area = orWhole (b, r);
    const float peak = area.isEmpty() ? 0.0f : b.getMagnitude (area.getStart(), area.getLength());
    if (peak < 1.0e-6f)
        return { b, clip (b, r) };
    return gain (b, r, peakDecibels - juce::Decibels::gainToDecibels (peak));
}

Result trimSilence (const juce::AudioBuffer<float>& b, float thresholdDecibels)
{
    const float threshold = juce::Decibels::decibelsToGain (thresholdDecibels);
    int start = 0, end = b.getNumSamples();
    while (start < end && b.getMagnitude (start, 1) < threshold)
        ++start;
    while (end > start && b.getMagnitude (end - 1, 1) < threshold)
        --end;

    if (start >= end)
        return { b, {} };   // all quiet: leave it alone rather than delete everything

    // keep a little before the first sound so the attack isn't cut, and let the end ring
    start = juce::jmax (0, start - samples (0.005));
    end = juce::jmin (b.getNumSamples(), end + samples (0.02));

    auto result = crop (b, { start, end });
    const int n = result.audio.getNumSamples();
    const int tail = juce::jmin (samples (0.01), n / 4);
    if (tail > 0)
        result.audio.applyGainRamp (n - tail, tail, 1.0f, 0.0f);
    result.selection = {};
    return result;
}

juce::AudioBuffer<float> collapseFakeStereo (juce::AudioBuffer<float> b)
{
    if (b.getNumChannels() != 2 || b.getNumSamples() == 0)
        return b;

    const int n = b.getNumSamples();
    const float left = b.getRMSLevel (0, 0, n), right = b.getRMSLevel (1, 0, n);
    const float louder = juce::jmax (left, right);

    if (louder < 1.0e-6f)
    {
        b.setSize (1, n, true);
        return b;
    }

    // one side (nearly) silent: keep the other
    if (juce::jmin (left, right) < louder * 0.01f)
    {
        if (right > left)
            b.copyFrom (0, 0, b, 1, 0, n);
        b.setSize (1, n, true);
        return b;
    }

    // both sides the same
    double difference = 0;
    const auto* l = b.getReadPointer (0);
    const auto* r = b.getReadPointer (1);
    for (int i = 0; i < n; ++i)
        difference += (double) (l[i] - r[i]) * (l[i] - r[i]);
    if (std::sqrt (difference / n) < louder * 0.001f)
        b.setSize (1, n, true);

    return b;
}

juce::Range<int> snapToZeroCrossings (const juce::AudioBuffer<float>& b, juce::Range<int> r)
{
    const int n = b.getNumSamples();
    if (n < 2 || b.getNumChannels() == 0)
        return r;

    const int reach = samples (0.01);
    const auto* d = b.getReadPointer (0);

    auto snap = [&] (int position)
    {
        if (position <= 0 || position >= n)
            return position;
        for (int offset = 0; offset < reach; ++offset)
            for (int candidate : { position - offset, position + offset })
                if (candidate > 0 && candidate < n && (d[candidate - 1] <= 0.0f) != (d[candidate] <= 0.0f))
                    return candidate;
        return position;
    };

    const int start = snap (r.getStart());
    const int end = r.isEmpty() ? start : snap (r.getEnd());
    return { juce::jmin (start, end), juce::jmax (start, end) };
}

} // namespace crate::Edits
