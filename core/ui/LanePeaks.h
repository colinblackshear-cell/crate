#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <map>
#include <mutex>
#include <vector>

// A layer's sound as a row of peaks (0..1), for the lanes My ideas draws (the phone and the Mac). It reads a little
// from each part of the file, so a ten minute loop is quick, and keeps the result until the file changes.
namespace crate::lanes
{
inline std::atomic<int>& filesRead()
{
    static std::atomic<int> n { 0 };
    return n;
}

inline std::vector<float> peaksOf (const juce::File& file, int bins)
{
    struct Cached { juce::Time modified; std::vector<float> peaks; };
    static std::map<juce::String, Cached> cache;
    static std::mutex lock;
    if (! file.existsAsFile() || bins <= 0)
        return {};
    const auto modified = file.getLastModificationTime();
    {
        std::lock_guard<std::mutex> l (lock);
        if (auto it = cache.find (file.getFullPathName()); it != cache.end() && it->second.modified == modified && (int) it->second.peaks.size() == bins)
            return it->second.peaks;
    }
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return {};
    ++filesRead();
    std::vector<float> out ((size_t) bins, 0.0f);
    const auto length = reader->lengthInSamples;
    const int channels = juce::jlimit (1, 2, (int) reader->numChannels);
    float top = 0.0001f;
    for (int b = 0; b < bins; ++b)
    {
        const auto from = length * b / bins, to = length * (b + 1) / bins;
        juce::Range<float> ranges[2];
        reader->readMaxLevels (from, (int) juce::jmin<juce::int64> (to - from, 6000), ranges, channels);
        float peak = 0.0f;
        for (int c = 0; c < channels; ++c)
            peak = juce::jmax (peak, std::abs (ranges[c].getStart()), std::abs (ranges[c].getEnd()));
        out[(size_t) b] = peak;
        top = juce::jmax (top, peak);
    }
    for (auto& p : out)
        p = juce::jlimit (0.0f, 1.0f, std::sqrt (p / top));
    std::lock_guard<std::mutex> l (lock);
    cache[file.getFullPathName()] = { modified, out };
    return out;
}

// Draws peaks as bars across `lane`, centred.
inline void drawPeaks (juce::Graphics& g, const std::vector<float>& peaks, juce::Rectangle<float> lane, juce::Colour colour)
{
    if (peaks.empty())
        return;
    g.setColour (colour);
    const float bw = lane.getWidth() / (float) peaks.size();
    for (size_t b = 0; b < peaks.size(); ++b)
    {
        const float h = juce::jmax (1.5f, (lane.getHeight() - 4.0f) * peaks[b]);
        g.fillRect (lane.getX() + (float) b * bw + 0.5f, lane.getCentreY() - h * 0.5f, juce::jmax (1.0f, bw - 1.0f), h);
    }
}
}
