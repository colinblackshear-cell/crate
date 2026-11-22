#pragma once

#include <juce_core/juce_core.h>

#include <functional>

// The effects tool's engine checks that need no screen.
namespace pocket
{
class LoopsEngine;

// A fixed little song (the drum machine, a pad pattern, a synth pattern, a loop layer with its delay and reverb
// sends, the mic heard dry, the click) rendered through the looper, as a fingerprint of every output bit.
// `tweak` runs on the engine before it plays (to touch the boards). The same song rendered by the engine
// before the boards existed gave kUntouchedFingerprint.
// synthNotes false: the synth plays nothing (to show a SYNTH board changes nothing else).
juce::uint64 mixFingerprint (std::function<void (LoopsEngine&)> tweak = {}, bool synthNotes = true);
// Re-recorded 2026-11 after the drum and synth sounds changed on main (the old value no longer matched
// even with no board touched). The report prints it in full now, so it can be copied straight from it.
constexpr juce::uint64 kUntouchedFingerprint = 14133004927360232518ull;
}
