#pragma once

#include <juce_core/juce_core.h>

#include <cmath>

// 16 Levels, as on the MPC: one sound spread over all 16 pads of the bank on screen, each pad
// playing it a step differently. Pad 1 (bottom left) is the lowest step, pad 16 (top right)
// the highest.
//
// - Tune: a semitone a pad, pad 1 as the sound is, up to 15 semitones higher at pad 16. Play
//   a bass note or a vocal chop as a melody.
// - Filter: the same pitch, darker on pad 1 and opening up to the full sound at pad 16.
// - Tune + filter: a semitone a pad, with the filter opening as it goes up.
// - Velocity: from soft on pad 1 to full on pad 16 (ghost notes to accents).
// - Decay: short on pad 1, longer up the pads, the whole sound at pad 16 (an 808 kick's tail).
//
// A hit played this way keeps its step (its "note variation", as the MPC calls it): the
// sequencer records the sound's own pad with the step, and plays it back the same way.
namespace levels
{
    enum Type { off, tune, filter, tuneFilter, velocity, decay, numTypes };

    inline juce::String typeName (int type)
    {
        switch (type)
        {
            case tune:       return "Tune";
            case filter:     return "Filter";
            case tuneFilter: return "Tune + filter";
            case velocity:   return "Velocity";
            case decay:      return "Decay";
            default:         return "Off";
        }
    }

    // What a step does to the sound. Step 0 is pad 1, 15 is pad 16.
    inline double semitones (int type, int step)
    {
        return type == tune || type == tuneFilter ? (double) juce::jlimit (0, 15, step) : 0.0;
    }

    // The low-pass filter's cutoff in Hz, or 0 when the step doesn't filter the sound.
    inline float cutoff (int type, int step)
    {
        if (type != filter && type != tuneFilter)
            return 0.0f;
        // 150 Hz on pad 1 to 18 kHz on pad 16, evenly in octaves.
        return 150.0f * std::pow (120.0f, (float) juce::jlimit (0, 15, step) / 15.0f);
    }

    inline int velocityFor (int step) { return juce::jlimit (1, 127, 8 * (juce::jlimit (0, 15, step) + 1) - 1); }

    // How long the sound lasts before it's faded out, in seconds; 0 = the whole sound.
    inline double decaySeconds (int type, int step)
    {
        if (type != decay || step >= 15)
            return 0.0;
        // 40 ms on pad 1, doubling about every two and a half pads.
        return 0.04 * std::pow (2.0, juce::jlimit (0, 15, step) * 0.42);
    }

    // What a step does, for the pad that plays it: "+3 st", "1.2 kHz", "Vel 64", "180 ms".
    inline juce::String stepText (int type, int step)
    {
        auto hz = [] (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; };
        switch (type)
        {
            case tune:       return "+" + juce::String ((int) semitones (type, step)) + " st";
            case tuneFilter: return "+" + juce::String ((int) semitones (type, step)) + " st " + hz (cutoff (type, step));
            case filter:     return hz (cutoff (type, step));
            case velocity:   return "Vel " + juce::String (velocityFor (step));
            case decay:
            {
                const double secs = decaySeconds (type, step);
                return secs <= 0 ? juce::String ("Whole") : juce::String (juce::roundToInt (secs * 1000.0)) + " ms";
            }
            default:         return {};
        }
    }

    // A hit's variation in one number, as notes keep it: 0 = played as it is.
    inline int pack (int type, int step)
    {
        return type <= off || type >= numTypes || type == velocity ? 0 : type * 16 + juce::jlimit (0, 15, step);
    }
    inline int typeOf (int variation) { return juce::isPositiveAndBelow (variation / 16, (int) numTypes) ? variation / 16 : off; }
    inline int stepOf (int variation) { return variation % 16; }

    // The sequencer passes a hit's variation to the pads as a key-pressure message on this
    // channel, just before the note: its note is the pad's, its value the packed variation.
    // (Channel 15 is the sequencer's notes, 16 clicks on the pads.)
    constexpr int kVariationChannel = 14;
    inline bool isVariation (const juce::MidiMessage& m)
    {
        return m.isAftertouch() && m.getChannel() == kVariationChannel;
    }
    inline juce::MidiMessage message (int note, int variation)
    {
        return juce::MidiMessage::aftertouchChange (kVariationChannel, note, juce::jlimit (0, 127, variation));
    }
}
