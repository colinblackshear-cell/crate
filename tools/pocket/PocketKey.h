#pragma once

#include <juce_core/juce_core.h>

// The song's key: one for the whole app (SYNTH's keys, CHORDS' pads and the vocoder all follow it),
// saved with the song. Major or minor; the scale notes are the ones the keyboard and chords use.
namespace pocket
{
struct SongKey
{
    int root = 0;        // 0 = C ... 11 = B
    bool minor = true;

    bool operator== (const SongKey& o) const { return root == o.root && minor == o.minor; }
    bool operator!= (const SongKey& o) const { return ! (*this == o); }

    // Semitones above the root of scale degree `degree` (0 = the root; 7 = the root an octave up).
    int degreeSemis (int degree) const
    {
        static constexpr int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
        const int oct = degree >= 0 ? degree / 7 : -((6 - degree) / 7);
        const int d = degree - oct * 7;
        return (minor ? minorScale : major)[d] + 12 * oct;
    }
    bool inScale (int midiNote) const
    {
        const int pc = ((midiNote - root) % 12 + 12) % 12;
        for (int d = 0; d < 7; ++d)
            if (degreeSemis (d) == pc)
                return true;
        return false;
    }
    static juce::String pitchName (int pitchClass)
    {
        static const char* const names[12] = { "C", "D\xe2\x99\xad", "D", "E\xe2\x99\xad", "E", "F", "F\xe2\x99\xaf", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
        return juce::String (juce::CharPointer_UTF8 (names[((pitchClass % 12) + 12) % 12]));
    }
    static juce::String noteName (int midi) { return pitchName (midi) + juce::String (midi / 12 - 1); }   // "A3"
    juce::String label() const { return pitchName (root).toUpperCase() + (minor ? " MINOR" : " MAJOR"); }

    juce::var toVar() const
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("root", root);
        o->setProperty ("minor", minor);
        return juce::var (o);
    }
    static SongKey fromVar (const juce::var& v, SongKey fallback)
    {
        if (! v.isObject())
            return fallback;
        SongKey k;
        k.root = juce::jlimit (0, 11, (int) v.getProperty ("root", fallback.root));
        k.minor = (bool) v.getProperty ("minor", fallback.minor);
        return k;
    }
};
}
