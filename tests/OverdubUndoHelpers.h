#pragma once

// Shared by the overdub and undo tests (RigOverdubUndoTests.cpp) and the overdub/undo benchmark
// (OverdubUndoBench.cpp): loops to play, a looper driven one block at a time with each block's
// time measured, and a plain model of what an overdub writes.

#include "RigLooper.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <functional>
#include <vector>

#if JUCE_LINUX
 #include <sys/resource.h>
#endif

namespace odu
{
// A tone that repeats every `period` samples (a whole number of cycles in it), `samples` long.
inline juce::AudioBuffer<float> toneLoop (int samples, int bin, int period = 256, float level = 0.1f)
{
    juce::AudioBuffer<float> b (2, samples);
    std::vector<float> one ((size_t) period);
    for (int i = 0; i < period; ++i)
        one[(size_t) i] = level * (float) std::sin (juce::MathConstants<double>::twoPi * bin * i / period);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        for (int i = 0; i < samples; i += period)
            std::copy (one.begin(), one.begin() + juce::jmin (period, samples - i), d + i);
    }
    return b;
}

// A different value at every sample (so a page out of place shows), small enough that adding
// many overdub passes stays exact.
inline juce::AudioBuffer<float> markedLoop (int samples, int seed)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = b.getWritePointer (ch);
        juce::uint32 x = (juce::uint32) (seed * 7919 + ch * 104729 + 1);
        for (int i = 0; i < samples; ++i)
        {
            x = x * 1664525u + 1013904223u;
            d[i] = (float) ((int) (x >> 20) - 2048) / 65536.0f;   // multiples of 2^-16, under 1/32
        }
    }
    return b;
}

inline bool same (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() == 0 && b.getNumSamples() == 0)
        return true;   // both nothing
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() < 2 || b.getNumChannels() < 2)
        return false;
    for (int ch = 0; ch < 2; ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}

// The first sample where two buffers differ (-1 if none, -2 if their lengths differ).
inline int firstDifference (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples())
        return -2;
    for (int i = 0; i < a.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            if (std::memcmp (a.getReadPointer (ch) + i, b.getReadPointer (ch) + i, sizeof (float)) != 0)
                return i;
    return -1;
}

inline double threadCpuMicros()
{
    timespec t {};
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &t);
    return t.tv_sec * 1.0e6 + t.tv_nsec * 1.0e-3;
}

// Minor and major page faults of this thread so far (Linux; -1 elsewhere).
inline long threadPageFaults()
{
#if JUCE_LINUX
    rusage u {};
    getrusage (RUSAGE_THREAD, &u);
    return u.ru_minflt + u.ru_majflt;
#else
    return -1;
#endif
}

// Times, with their median, 99th percentile and most.
struct Times
{
    std::vector<double> v;
    void add (double x) { v.push_back (x); }
    double at (double q) const
    {
        if (v.empty())
            return 0;
        auto s = v;
        std::sort (s.begin(), s.end());
        return s[(size_t) juce::jlimit (0, (int) s.size() - 1, (int) std::ceil (q * (double) s.size()) - 1)];
    }
    double median() const { return at (0.5); }
    double p99() const { return at (0.99); }
    double most() const { return v.empty() ? 0 : *std::max_element (v.begin(), v.end()); }
};

// A looper driven one block at a time on the calling thread. The clock plays from beat 0; each
// layer records from the input, which `input` fills (sample `at` since the start, channel ch).
struct Drive
{
    RigLooper looper;
    RigLooper::Clock clock;
    double rate;
    int maxBlock;
    juce::AudioBuffer<float> in, out;
    std::function<float (juce::int64 at, int ch)> input;
    std::function<int (juce::int64 blockNumber)> blockSize;   // default: maxBlock every block
    // After each block's process, with the clock as it was for that block and its input.
    std::function<void (const RigLooper::Clock&, const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>& output, int n)> after;
    juce::int64 fed = 0, blockNumber = 0;
    // Each block's wall-clock and audio-thread CPU time (microseconds), while `timing`.
    bool timing = false;
    Times wall, cpu;
    int late = 0;   // blocks that took longer than they play for
    // While `timing`: blocks in which the audio thread took a page fault (the system's work,
    // counted in its time), and late blocks that had one. (Linux; 0 elsewhere.)
    int faultBlocks = 0, lateWithFault = 0;

    Drive (double sampleRate, int maximumBlock, double bpm = 120.0)
        : rate (sampleRate), maxBlock (maximumBlock), in (2, maximumBlock), out (2, maximumBlock)
    {
        looper.prepare (rate, maxBlock);
        clock.bpm = bpm;
        clock.beatsPerBar = 4;
        clock.playing = true;
    }

    // One block; returns its size.
    int block()
    {
        const int n = juce::jlimit (1, maxBlock, blockSize ? blockSize (blockNumber) : maxBlock);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = in.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = input ? input (fed + i, ch) : 0.0f;
        }
        juce::AudioBuffer<float> inView (in.getArrayOfWritePointers(), 2, n), outView (out.getArrayOfWritePointers(), 2, n);
        std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
        sources[RigLooper::input] = &inView;
        const double w0 = juce::Time::getMillisecondCounterHiRes();
        const double c0 = timing ? threadCpuMicros() : 0.0;
        const long f0 = timing ? threadPageFaults() : 0;
        looper.process (clock, sources, outView);
        if (timing)
        {
            const double us = (juce::Time::getMillisecondCounterHiRes() - w0) * 1000.0;
            const bool faulted = threadPageFaults() > f0;
            wall.add (us);
            cpu.add (threadCpuMicros() - c0);
            faultBlocks += faulted ? 1 : 0;
            if (us > 1.0e6 * n / rate)
            {
                ++late;
                lateWithFault += faulted ? 1 : 0;
            }
        }
        if (after)
            after (clock, inView, outView, n);
        if (clock.playing)
            clock.beat += n * clock.bpm / (60.0 * rate);
        fed += n;
        ++blockNumber;
        return n;
    }
    void blocks (int count)
    {
        for (int k = 0; k < count; ++k)
            block();
    }
    void seconds (double s)
    {
        for (juce::int64 until = fed + (juce::int64) (s * rate); fed < until;)
            block();
    }
    // Blocks until `done` (at most `maxBlocks`); true if it got there.
    bool until (const std::function<bool()>& done, int maxBlocks)
    {
        for (int k = 0; k < maxBlocks; ++k)
        {
            if (done())
                return true;
            block();
        }
        return done();
    }
    void resetTiming()
    {
        wall = {};
        cpu = {};
        late = 0;
        faultBlocks = lateWithFault = 0;
    }
};

// What an overdub writes, worked out plainly: for each block the layer overdubs, the positions
// the engine's formula gives, and loop[w] += input there. (The same arithmetic, in the same
// order, as RigLooper::process, so it is exact.)
struct DubModel
{
    juce::AudioBuffer<float> loop;
    double lengthBeats = 0, startBeat = 0;
    bool live = false;

    void block (const RigLooper::Clock& clock, double rate, int liveDelay, const juce::AudioBuffer<float>& input, int n)
    {
        const int length = loop.getNumSamples();
        if (! clock.playing || length <= 1)
            return;
        const double beatsPerSample = clock.bpm / (60.0 * rate);
        int dubDelay = 0;
        if (live && length > 1 && lengthBeats > 0)
        {
            const double loopPerDevice = length / (lengthBeats * rate * 60.0 / juce::jmax (1.0, clock.bpm));
            dubDelay = juce::jlimit (0, length / 4, (int) std::round (liveDelay * loopPerDevice));
        }
        const double lb = juce::jmax (1.0e-6, lengthBeats);
        for (int i = 0; i < n; ++i)
        {
            const double beat = clock.beat + i * beatsPerSample;
            double p = std::fmod (beat - startBeat, lb);
            if (p < 0)
                p += lb;
            const double pos = p / lb * length;
            const int i0 = juce::jlimit (0, length - 1, (int) pos);
            const int w = (i0 - dubDelay + length) % length;
            for (int ch = 0; ch < 2; ++ch)
                loop.getWritePointer (ch)[w] += input.getReadPointer (ch)[i];
        }
    }
};
}
