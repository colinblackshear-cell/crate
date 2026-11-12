#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Novation Launchpad (X, Mini MK3, Pro) as a set of MPC pads.
//
// In Drum mode (Note mode's drum layout on a Launchpad X or Pro, Custom mode 1 on a Mini) the
// 8x8 grid sends notes 36-99 in four blocks of 16, each laid out like the MPC's pads (four rows
// of four, from the bottom left):
//
//      top left:     68-83        top right:     84-99
//      bottom left:  36-51        bottom right:  52-67
//
// So the bottom-left block is the same as a Launchkey's 16 drum pads, and the Rig puts the pad
// bank on screen there and the next three banks on the other blocks: all 64 MPC pads at once.
namespace crate::launchpad
{
    constexpr int kFirstNote = 36;
    constexpr int kNumPads = 64;

    inline bool isLaunchpad (const juce::String& deviceName) { return deviceName.containsIgnoreCase ("launchpad"); }

    // A pad's note on, note off or pressure (on any channel).
    inline bool isPadMessage (const juce::MidiMessage& m)
    {
        return (m.isNoteOnOrOff() || m.isAftertouch())
               && juce::isPositiveAndBelow (m.getNoteNumber() - kFirstNote, kNumPads);
    }

    // Which of the 64 pads (0-63, block by block) a note is, or -1.
    inline int padForNote (int note)
    {
        return juce::isPositiveAndBelow (note - kFirstNote, kNumPads) ? note - kFirstNote : -1;
    }
}
