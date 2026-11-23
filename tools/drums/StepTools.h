#pragma once

#include "DrumEngine.h"

// The drum machine's step tools: what Hit, Roll, Builds, Flam, Accent, Soft/Loud and Erase do to one
// step of a pattern, and the row and bar tools (shift, reverse, spread, copy a bar, double). One
// copy, used by the Mac panel (DrumView), the Rig's Groove screen (GrooveView) and the phone's
// Beats screen, so all three write a pattern the same way.
namespace crate::drums::steptools
{
enum class Tool { hit, roll, builds, flam, accent, level, erase, numTools };

inline const char* toolName (Tool t)
{
    switch (t)
    {
        case Tool::hit:    return "Hit";
        case Tool::roll:   return "Roll";
        case Tool::builds: return "Builds";
        case Tool::flam:   return "Flam";
        case Tool::accent: return "Accent";
        case Tool::level:  return "Soft/Loud";
        case Tool::erase:  return "Erase";
        case Tool::numTools: break;
    }
    return "";
}

// The roll sizes the phone steps through (a tap on the ROLL chip again), and the ones the right-click cycles.
inline int nextRollChoice (int n)
{
    static constexpr int sizes[] = { 2, 3, 4, 6, 8 };
    for (int s : sizes)
        if (n < s)
            return s;
    return 2;
}

// Right-click (Option-click) on a step: none or one hit -> 2 -> 3 -> 4 -> 6 -> 8 -> one hit.
inline int nextRollCycle (int hits)
{
    static constexpr int sizes[] = { 3, 4, 6, 8 };
    if (hits <= 1)
        return 2;
    if (hits == 2)
        return 3;
    for (int s : sizes)
        if (hits < s)
            return s;
    return 1;
}

// Soft/Loud: 100 -> 60 -> 30 -> 100.
inline int nextLevel (int velocity)
{
    return velocity > 80 ? 60 : velocity > 45 ? 30 : 100;
}

inline void clearStep (Pattern& p, int voice, int step)
{
    const auto v = (size_t) voice, s = (size_t) step;
    p.hits[v][s] = 0;
    p.shape[v][s] = plain;
    p.velocity[v][s] = 100;
}

// The right-click cycle on one step.
inline int cycleRoll (Pattern& p, int voice, int step)
{
    if (voice < 0 || voice >= kVoices || step < 0 || step >= kMaxSteps)
        return 0;
    auto& h = p.hits[(size_t) voice][(size_t) step];
    h = (std::uint8_t) nextRollCycle (h);
    if (h <= 1)
        p.shape[(size_t) voice][(size_t) step] = plain;
    return h;
}

// Applies a tool to a step (`voice` is ignored by Accent). `rollN` (2..8) is the roll's size for
// Roll and Builds. Returns true when the step now plays (so the caller can let you hear it).
inline bool apply (Pattern& p, Tool tool, int voice, int step, int rollN = 3)
{
    if (step < 0 || step >= kMaxSteps)
        return false;
    if (tool == Tool::accent)
    {
        p.accent[(size_t) step] = ! p.accent[(size_t) step];
        return false;
    }
    if (voice < 0 || voice >= kVoices)
        return false;
    const auto v = (size_t) voice, s = (size_t) step;
    auto& h = p.hits[v][s];
    auto& sh = p.shape[v][s];
    rollN = juce::jlimit (2, kMaxRoll, rollN);
    switch (tool)
    {
        case Tool::hit:
            if (h == 0)
            {
                h = 1;
                sh = plain;
            }
            else
            {
                clearStep (p, voice, step);
            }
            break;
        case Tool::roll:
            if (h == rollN && sh == plain)
                h = 1;
            else
            {
                h = (std::uint8_t) rollN;
                sh = plain;
            }
            break;
        case Tool::builds:
            if (h == rollN && sh == builds)
            {
                h = 1;
                sh = plain;
            }
            else
            {
                h = (std::uint8_t) rollN;
                sh = builds;
            }
            break;
        case Tool::flam:
            if (h == 1 && sh == flam)
                sh = plain;
            else
            {
                h = 1;
                sh = flam;
            }
            break;
        case Tool::level:
            if (h == 0)
                return false;
            p.velocity[v][s] = (std::uint8_t) nextLevel (p.velocity[v][s]);
            break;
        case Tool::erase:
            clearStep (p, voice, step);
            break;
        case Tool::accent:
        case Tool::numTools:
            break;
    }
    return h != 0 && tool != Tool::erase;
}

// Everything a step holds for one sound, moved from one place to another.
inline void copyStep (Pattern& p, int voice, int from, int to)
{
    const auto v = (size_t) voice, a = (size_t) from, b = (size_t) to;
    p.hits[v][b] = p.hits[v][a];
    p.shape[v][b] = p.shape[v][a];
    p.chance[v][b] = p.chance[v][a];
    p.cond[v][b] = p.cond[v][a];
    p.nudge[v][b] = p.nudge[v][a];
    p.pitch[v][b] = p.pitch[v][a];
    p.decayLock[v][b] = p.decayLock[v][a];
    p.velocity[v][b] = p.velocity[v][a];
}

namespace detail
{
template <typename F>
void forEachRowArray (Pattern& p, int voice, F&& f)
{
    const auto v = (size_t) voice;
    f (p.hits[v]);
    f (p.shape[v]);
    f (p.chance[v]);
    f (p.cond[v]);
    f (p.nudge[v]);
    f (p.pitch[v]);
    f (p.decayLock[v]);
    f (p.velocity[v]);
}
}

// Moves a sound's row by `by` steps (round its own length), all its per-step settings with it.
inline void shiftRow (Pattern& p, int voice, int by)
{
    const int len = juce::jlimit (1, kMaxSteps, p.rowLengthOf (voice));
    detail::forEachRowArray (p, voice, [&] (auto& a)
    {
        const auto copy = a;
        for (int i = 0; i < len; ++i)
            a[(size_t) (((i + by) % len + len) % len)] = copy[(size_t) i];
    });
}

inline void reverseRow (Pattern& p, int voice)
{
    const int len = juce::jlimit (1, kMaxSteps, p.rowLengthOf (voice));
    detail::forEachRowArray (p, voice, [&] (auto& a)
    {
        const auto copy = a;
        for (int i = 0; i < len; ++i)
            a[(size_t) i] = copy[(size_t) (len - 1 - i)];
    });
}

inline void clearRow (Pattern& p, int voice)
{
    for (int i = 0; i < kMaxSteps; ++i)
    {
        clearStep (p, voice, i);
        p.chance[(size_t) voice][(size_t) i] = 100;
        p.cond[(size_t) voice][(size_t) i] = 0;
        p.nudge[(size_t) voice][(size_t) i] = 0;
        p.pitch[(size_t) voice][(size_t) i] = 0;
        p.decayLock[(size_t) voice][(size_t) i] = 0;
    }
}

// Spreads `hits` as evenly as they go over the row's length (Euclid). The other steps are emptied.
inline void spreadRow (Pattern& p, int voice, int hits)
{
    const int len = juce::jlimit (1, kMaxSteps, p.rowLengthOf (voice));
    const auto spread = crate::life::euclid (juce::jlimit (0, len, hits), len, 0);
    for (int i = 0; i < len; ++i)
    {
        const auto v = (size_t) voice, s = (size_t) i;
        if (spread[s])
        {
            p.hits[v][s] = (std::uint8_t) juce::jmax (1, (int) p.hits[v][s]);
        }
        else
        {
            clearStep (p, voice, i);
            p.chance[v][s] = 100;
            p.cond[v][s] = 0;
            p.nudge[v][s] = 0;
            p.pitch[v][s] = 0;
            p.decayLock[v][s] = 0;
        }
    }
}

// Copies bar `bar` (16 steps, counted from 0) into the next bar, making the pattern long enough to
// hold it. Returns false when there's no next bar.
inline bool copyBar (Pattern& p, int bar)
{
    constexpr int per = 16;
    if (bar < 0 || (bar + 2) * per > kMaxSteps)
        return false;
    for (int v = 0; v < kVoices; ++v)
        for (int i = 0; i < per; ++i)
            copyStep (p, v, bar * per + i, (bar + 1) * per + i);
    for (int i = 0; i < per; ++i)
        p.accent[(size_t) ((bar + 1) * per + i)] = p.accent[(size_t) (bar * per + i)];
    p.length = juce::jmax (p.length, (bar + 2) * per);
    return true;
}

// Repeats steps 1..length after themselves, up to 64 steps. Returns false when it's already full.
inline bool doubleLength (Pattern& p)
{
    const int len = juce::jlimit (1, kMaxSteps, p.length);
    if (len >= kMaxSteps)
        return false;
    const int total = juce::jmin (kMaxSteps, len * 2);
    for (int i = len; i < total; ++i)
    {
        for (int v = 0; v < kVoices; ++v)
            copyStep (p, v, i - len, i);
        p.accent[(size_t) i] = p.accent[(size_t) (i - len)];
    }
    p.length = total;
    return true;
}

// The bars a pattern has (its length in 16s, rounded up).
inline int barsOf (const Pattern& p) { return juce::jlimit (1, 4, (p.length + 15) / 16); }

// Makes the pattern long enough to hold step `step`: whole bars, so a bar is always 16 steps.
inline void extendTo (Pattern& p, int step)
{
    if (step >= p.length)
        p.length = juce::jlimit (1, kMaxSteps, (step / 16 + 1) * 16);
}
}
