#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Novation Launchkey (61 and its smaller siblings) as it comes out of the box, in its own
// (standalone) mode, so every crate tool reads it the same way.
//
// The 16 drum pads, in Drum mode, send notes 36-51 on MIDI channel 10. They sit in two rows of
// eight, as two 4x2 blocks side by side:
//
//      top row:     40 41 42 43   48 49 50 51
//      bottom row:  36 37 38 39   44 45 46 47
//
// Counting from note 36 gives pads 1-16 in the MPC's order: pads 1-4 bottom left, 5-8 above
// them, 9-12 bottom right, 13-16 above those.
//
// Knobs, faders and buttons send CCs that depend on the Launchkey model and its settings, so
// tools learn those (see the Rig's Learn buttons) rather than guess them here.
namespace crate::launchkey
{
    constexpr int kDrumChannel = 10;
    constexpr int kFirstDrumNote = 36;
    constexpr int kNumDrumPads = 16;

    // The drum pad (0-15) a note is, or -1 if it isn't one.
    inline int drumPadForNote (int channel, int note)
    {
        if (channel != kDrumChannel || note < kFirstDrumNote || note >= kFirstDrumNote + kNumDrumPads)
            return -1;
        return note - kFirstDrumNote;
    }

    inline int drumPadForMessage (const juce::MidiMessage& m)
    {
        return m.isNoteOnOrOff() ? drumPadForNote (m.getChannel(), m.getNoteNumber()) : -1;
    }

    // The note drum pad `pad` (0-15) sends.
    constexpr int noteForDrumPad (int pad) { return kFirstDrumNote + pad; }
}
