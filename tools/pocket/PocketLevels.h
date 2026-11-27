#pragma once

#include "BeatsEngine.h"
#include "PocketKey.h"
#include "SixteenLevels.h"   // the Mac MPC's 16 Levels: the filter, decay and velocity maths are shared

// 16 LEVELS on the pads, as on the MPC2000XL: one pad's sound on all 16 pads of the bank, each pad a step higher in
// one thing. The Mac's types (tune, tune + filter, filter, velocity, decay) are all here, plus ATTACK, and PITCH can
// climb the song's key instead of every semitone. A hit played this way records with its step's mods (PadHit::mods)
// and plays back the same.
namespace pocket::padlevels
{
enum Kind { pitch, inKey, pitchFilter, velocity, filter, decay, attack, numKinds };

inline const char* name (int kind)
{
    switch (kind)
    {
        case pitch:       return "PITCH";
        case inKey:       return "IN KEY";
        case pitchFilter: return "PITCH+FILTER";
        case velocity:    return "VELOCITY";
        case filter:      return "FILTER";
        case decay:       return "DECAY";
        case attack:      return "ATTACK";
        default:          return "";
    }
}

// Pad 1 = step 0 (the sound as it is), pad 16 = step 15.
inline double attackSeconds (int step) { return step <= 0 ? 0.0 : 0.004 * std::pow (2.0, 0.5 * juce::jlimit (0, 15, step)); }

// Semitones above the sound that a step plays at (PITCH, IN KEY and PITCH+FILTER).
inline int semitones (int kind, int step, const SongKey& key)
{
    step = juce::jlimit (0, 15, step);
    return kind == inKey ? key.degreeSemis (step) : kind == pitch || kind == pitchFilter ? step : 0;
}

// What a step plays: the level (the pad's velocity) and the mods for the hit.
struct Step { float velocity = 1.0f; PadMods mods; };

inline Step stepFor (int kind, int step, const SongKey& key)
{
    step = juce::jlimit (0, 15, step);
    Step s;
    s.mods.tune = semitones (kind, step, key) * 10;
    switch (kind)
    {
        case pitchFilter: s.mods.cutoff = levels::cutoff (levels::tuneFilter, step); break;
        case velocity:    s.velocity = (float) (step + 1) / 16.0f; break;
        case filter:      s.mods.cutoff = levels::cutoff (levels::filter, step); break;
        case decay:       s.mods.decay = (float) levels::decaySeconds (levels::decay, step); break;
        case attack:      s.mods.attack = (float) attackSeconds (step); break;
        default: break;
    }
    return s;
}

// What a step does, for the pad that plays it: "+3 st", "1.2 kHz", "Vel 64", "180 ms".
inline juce::String stepText (int kind, int step, const SongKey& key)
{
    step = juce::jlimit (0, 15, step);
    auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (f)); };
    switch (kind)
    {
        case pitch: case inKey: return step == 0 ? juce::String ("root") : "+" + juce::String (semitones (kind, step, key));
        case pitchFilter:       return "+" + juce::String (step) + " " + hz (levels::cutoff (levels::tuneFilter, step));
        case velocity:          return "v" + juce::String (levels::velocityFor (step));
        case filter:            return hz (levels::cutoff (levels::filter, step));
        case decay:             return step >= 15 ? juce::String ("whole") : juce::String (juce::roundToInt (levels::decaySeconds (levels::decay, step) * 1000.0)) + "ms";
        case attack:            return step == 0 ? juce::String ("0") : juce::String (juce::roundToInt (attackSeconds (step) * 1000.0)) + "ms";
        default:                return {};
    }
}

// The banner's second line.
inline const char* hint (int kind)
{
    switch (kind)
    {
        case pitch:       return "A semitone a pad, up from the sound. Play it like keys.";
        case inKey:       return "The song's scale, a note a pad. Every pad is in key.";
        case pitchFilter: return "A semitone a pad, the filter opening as it climbs.";
        case velocity:    return "Quiet on the first pad, loud on the last.";
        case filter:      return "Dark on the first pad, opening to the full sound.";
        case decay:       return "Short on the first pad, the whole sound on the last.";
        case attack:      return "Instant on the first pad, a slower fade-in up the pads.";
        default:          return "";
    }
}
}
