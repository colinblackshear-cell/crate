#pragma once

#include <juce_core/juce_core.h>

#include <vector>

// Pattern life: what keeps a step pattern moving for an hour without getting stale. Shared by the
// step grids (the drum machine, the bass, the MPC's drum grid).
//
//  - Chance: a hit plays only some of the times round (100% always).
//  - Condition: a hit plays only on some times round: 1:2 is the first of every two, 3:4 the third
//    of every four; Fill only when the fill plays; First only the first time round after Play.
//  - Nudge: a hit a little early or late (a part of a step), for push and drag.
//  - A row of its own length (polymeter): 3 steps against 16 and it comes round differently
//    every bar.
//  - Euclid: spreads some hits as evenly as they go over some steps (the tresillo is 3 in 8).
//  - Mutate: moves, adds and drops a few hits, so a pattern can grow.
namespace crate::life
{
enum Cond
{
    always, oneOfTwo, twoOfTwo, oneOfThree, twoOfThree, threeOfThree,
    oneOfFour, twoOfFour, threeOfFour, fourOfFour, fill, notFill, first, notFirst, numConds
};

inline const char* condName (int c)
{
    static const char* names[] = { "Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                   "Fill", "Not fill", "First", "Not first" };
    return names[juce::jlimit (0, (int) numConds - 1, c)];
}

// Whether a hit with condition `c` plays on time round `loop` (0 the first since Play).
inline bool passes (int c, long long loop, bool inFill)
{
    loop = juce::jmax (0LL, loop);
    switch (c)
    {
        case oneOfTwo:     return loop % 2 == 0;
        case twoOfTwo:     return loop % 2 == 1;
        case oneOfThree:   return loop % 3 == 0;
        case twoOfThree:   return loop % 3 == 1;
        case threeOfThree: return loop % 3 == 2;
        case oneOfFour:    return loop % 4 == 0;
        case twoOfFour:    return loop % 4 == 1;
        case threeOfFour:  return loop % 4 == 2;
        case fourOfFour:   return loop % 4 == 3;
        case fill:         return inFill;
        case notFill:      return ! inFill;
        case first:        return loop == 0;
        case notFirst:     return loop != 0;
        default:           return true;
    }
}

// The chances a click on a step goes through, and the nudges (percent of a step).
inline const std::vector<int>& chanceSteps() { static const std::vector<int> v { 100, 75, 50, 25, 10 }; return v; }
inline const std::vector<int>& nudgeSteps() { static const std::vector<int> v { 0, 10, 25, 40, -10, -25, -40 }; return v; }
template <typename T>
inline T nextIn (const std::vector<int>& list, T now, bool back = false)
{
    int at = 0;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i] == (int) now)
            at = i;
    const int n = (int) list.size();
    return (T) list[(size_t) ((at + (back ? n - 1 : 1)) % n)];
}

// `hits` spread as evenly as they go over `steps` (Bjorklund), turned by `rotate` steps.
inline std::vector<bool> euclid (int hits, int steps, int rotate = 0)
{
    steps = juce::jmax (1, steps);
    hits = juce::jlimit (0, steps, hits);
    std::vector<bool> out ((size_t) steps, false);
    for (int i = 0; i < steps; ++i)
        out[(size_t) i] = ((i * hits) % steps) < hits;   // (the same evenness as Bjorklund's, from the top)
    std::vector<bool> turned ((size_t) steps);
    for (int i = 0; i < steps; ++i)
        turned[(size_t) (((i + rotate) % steps + steps) % steps)] = out[(size_t) i];
    return turned;
}

// The classic spreads, by name, for the menus.
struct EuclidPreset { const char* name; int hits, steps; };
inline const std::vector<EuclidPreset>& euclidPresets()
{
    static const std::vector<EuclidPreset> v {
        { "Tresillo (3 in 8)", 3, 8 }, { "Cinquillo (5 in 8)", 5, 8 }, { "Clave feel (5 in 16)", 5, 16 },
        { "Bembe (7 in 12)", 7, 12 }, { "Samba (7 in 16)", 7, 16 }, { "Aksak (4 in 9)", 4, 9 },
        { "Bossa (3 in 16)", 3, 16 }, { "Ruchenitza (4 in 7)", 4, 7 }, { "Kpanlogo (9 in 16)", 9, 16 },
        { "Every 3rd (6 in 16)", 6, 16 }
    };
    return v;
}
} // namespace crate::life
