#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// What Kaleidobox Visuals hears, handed from the audio thread to the picture without locks.
//
// The audio thread copies the mix it plays (mono, a few thousand samples in a ring), how loud each
// tool is, and the notes the tools play. The picture reads them whenever it draws. Nothing here
// waits, allocates or grows: each block costs a copy of that block, and only while a picture is
// open (listening() is true). With Visuals off the audio side does nothing at all.
namespace crate::visuals
{
class VisualTap
{
public:
    static constexpr int kRing = 4096;   // a power of two
    static constexpr int kTools = 6;     // DRUMS, PADS, BASS, SYNTH, VOCODER, DRONE: the "Tools" colours, in order

    // A picture opens and closes (message thread). Counted, so two pictures can share one tap.
    void addListener() { listeners.fetch_add (1); }
    void removeListener() { listeners.fetch_sub (1); }
    bool listening() const { return listeners.load (std::memory_order_relaxed) > 0; }

    //==========================================================================
    // Audio thread (one writer).
    void push (const float* left, const float* right, int n)
    {
        if (left == nullptr || n <= 0)
            return;
        auto w = written.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            ring[(size_t) ((w + (std::uint64_t) i) & (kRing - 1))].store (right != nullptr ? 0.5f * (left[i] + right[i]) : left[i], std::memory_order_relaxed);
        written.store (w + (std::uint64_t) n, std::memory_order_release);
    }
    // How loud tool `slot` (0..kTools-1) played this block, 0..1; kept until the picture reads it.
    void toolLevel (int slot, float level)
    {
        if (slot < 0 || slot >= kTools)
            return;
        auto& a = tools[(size_t) slot];
        if (level > a.load (std::memory_order_relaxed))
            a.store (level, std::memory_order_relaxed);
    }
    // A note started (MIDI note number, velocity 0..1). Only its place in the octave counts.
    void noteOn (int note, float velocity)
    {
        if (note < 0)
            return;
        const auto pc = (size_t) (note % 12);
        noteVelocity[pc].store (velocity, std::memory_order_relaxed);
        noteCount[pc].fetch_add (1, std::memory_order_release);
    }

    //==========================================================================
    // The picture (one reader).
    // The last `n` samples (n <= kRing), oldest first. Returns false while nothing has played yet.
    bool latest (float* dest, int n) const
    {
        const auto w = written.load (std::memory_order_acquire);
        if (w == 0)
            return false;
        const auto start = w - (std::uint64_t) n;
        for (int i = 0; i < n; ++i)
            dest[i] = ring[(size_t) ((start + (std::uint64_t) i) & (kRing - 1))].load (std::memory_order_relaxed);
        return true;
    }
    std::uint64_t samplesWritten() const { return written.load (std::memory_order_acquire); }
    // How loud each tool played since the last call (and starts over).
    float takeToolLevel (int slot) { return tools[(size_t) slot].exchange (0.0f, std::memory_order_relaxed); }
    // Notes that started since the last call: fills `velocities` with the hit notes' velocities (0: no new hit).
    // Returns how many places in the octave were hit.
    int takeNotes (std::array<float, 12>& velocities)
    {
        int hits = 0;
        for (size_t pc = 0; pc < 12; ++pc)
        {
            const auto c = noteCount[pc].load (std::memory_order_acquire);
            velocities[pc] = 0.0f;
            if (c != seen[pc])
            {
                seen[pc] = c;
                velocities[pc] = noteVelocity[pc].load (std::memory_order_relaxed);
                ++hits;
            }
        }
        return hits;
    }

private:
    std::array<std::atomic<float>, kRing> ring {};   // atomic so the picture may read while the sound writes (no torn values)
    std::atomic<std::uint64_t> written { 0 };
    std::array<std::atomic<float>, kTools> tools {};
    std::array<std::atomic<float>, 12> noteVelocity {};
    std::array<std::atomic<std::uint32_t>, 12> noteCount {};
    std::array<std::uint32_t, 12> seen {};
    std::atomic<int> listeners { 0 };
};
} // namespace crate::visuals
