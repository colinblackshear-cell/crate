#pragma once

#include <juce_core/juce_core.h>

// Tonight's spark: a starting point for a new song, the same on the phone and the Mac. A mood, a tempo, a key, one of
// the factory kits and three steps, each on a tool both apps have. START THIS SPARK makes a new song with that tempo,
// key and kit and opens the first step's tool; ROLL ANOTHER deals the next one. Tonight's is the same all day (it
// follows the date), so the phone and the Mac show the same spark.
namespace crate::spark
{
// The tools a step can name: ones both apps have.
enum Tool { drums, pads, sampler, synth, chords, vocoder, voice, drone };

struct Step
{
    int tool = drums;
    juce::String text;   // "A drone on A under it"
};

struct Spark
{
    juce::String title;   // "A ritual at 104, in A minor"
    juce::String mood;    // "RITUAL"
    int bpm = 100;
    int root = 9;         // 0 = C ... 11 = B
    bool minor = true;
    int kit = 0;          // crate::factory::kits()
    Step steps[3];

    juce::String keyName() const { return rootName (root) + (minor ? " minor" : " major"); }

    static juce::String rootName (int pc)
    {
        static const char* const names[12] = { "C", "D\xe2\x99\xad", "D", "E\xe2\x99\xad", "E", "F", "F\xe2\x99\xaf", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
        return juce::String (juce::CharPointer_UTF8 (names[((pc % 12) + 12) % 12]));
    }
};

// Tonight's number: the day, so a spark stays put until midnight. ROLL ANOTHER adds one.
inline int tonight (juce::Time when = juce::Time::getCurrentTime())
{
    return (int) (juce::Time (when.getYear(), when.getMonth(), when.getDayOfMonth(), 12, 0).toMilliseconds() / 86400000LL);
}

// The spark for a number (tonight() + however many rolls). Always the same spark for the same number.
inline Spark make (int number)
{
    // Kits: 0 Street, 1 Dusty lo-fi (A minor), 2 808 classic, 3 Psych percussion, 4 Vocal chops (A minor).
    struct Recipe
    {
        const char* mood;
        const char* title;    // %b = the tempo, %k = the key
        int bpmLow, bpmHigh, kit;
        int fixedRoot;        // -1: any; else the kit's own key (always minor)
        bool canBeMajor;
        Step steps[3];        // %r = the key's root, %k = the key
    };
    static const Recipe recipes[] {
        { "RITUAL", "A ritual at %b, in %k", 98, 110, 3, -1, false,
          { { drums, "Hand drums kit, 4 bars, keep it loose" }, { drone, "A drone on %r under it" }, { vocoder, "Hum a chant through the vocoder" } } },
        { "TROPICAL", "Tropical night at %b, in %k", 100, 114, 3, -1, true,
          { { pads, "Congas and shakers on the pads" }, { chords, "Four chords in %k, one a bar" }, { synth, "A plucky melody over the top" } } },
        { "MINIMAL", "Minimal and deep at %b", 118, 124, 2, -1, false,
          { { drums, "Kick on every beat, hats in between" }, { synth, "One low note, let it breathe" }, { voice, "A whisper or a breath, chopped short" } } },
        { "LATE NIGHT", "Late night vocal chops at %b", 84, 92, 4, 9, false,
          { { pads, "Play the vocal chops like a melody" }, { drums, "A lazy beat under it" }, { chords, "A minor chords to glue it" } } },
        { "LO-FI", "Lo-fi porch at %b, in %k", 78, 88, 1, 9, false,
          { { drums, "A dusty beat, a little behind" }, { chords, "Keys chords in A minor" }, { voice, "Hum or sing whatever comes" } } },
        { "TRIBAL", "Tribal stomp at %b, in %k", 110, 122, 3, -1, false,
          { { pads, "Toms and the gong, stomp on the one" }, { drone, "A drone on %r" }, { vocoder, "Shout a call, then answer it" } } },
        { "FOUND SOUND", "Flip a sound at %b, in %k", 88, 100, 0, -1, true,
          { { sampler, "Record any sound around you and chop it" }, { drums, "Street kit beat under the chops" }, { synth, "A bass line in %k" } } },
        { "DRIFT", "A slow drift at %b, in %k", 70, 80, 3, -1, true,
          { { drone, "A drone on %r, long and wide" }, { voice, "Breathe and hum over it" }, { pads, "A bell every few bars" } } },
    };
    constexpr int n = (int) (sizeof (recipes) / sizeof (recipes[0]));

    // A small steady shuffle: the same number always deals the same spark, and neighbours differ.
    auto mix = [] (juce::uint32 x)
    {
        x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
        return x;
    };
    const auto h = mix ((juce::uint32) number * 2654435761U + 0x9e3779b9U);
    const auto& r = recipes[(int) (h % (juce::uint32) n)];

    Spark s;
    s.mood = r.mood;
    s.kit = r.kit;
    s.bpm = r.bpmLow + (int) ((h >> 8) % (juce::uint32) (r.bpmHigh - r.bpmLow + 1));
    s.bpm -= s.bpm % 2;   // even tempos read better
    static const int roots[7] = { 9, 2, 4, 7, 0, 5, 11 };   // A D E G C F B: keys that sit well on a keyboard
    s.root = r.fixedRoot >= 0 ? r.fixedRoot : roots[(h >> 16) % 7];
    s.minor = r.fixedRoot >= 0 || ! r.canBeMajor || ((h >> 24) & 3) != 0;

    auto fill = [&s] (juce::String t)
    {
        return t.replace ("%b", juce::String (s.bpm)).replace ("%k", s.keyName()).replace ("%r", Spark::rootName (s.root));
    };
    s.title = fill (r.title);
    for (int i = 0; i < 3; ++i)
        s.steps[i] = { r.steps[i].tool, fill (r.steps[i].text) };
    return s;
}
}
