#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

// Where the Rig sends what you play on a keyboard like the Launchkey 61.
//
// - Drum pads (MIDI channel 10, the Launchkey's drum mode) play the MPC's pads. The 16 pads
//   send notes 36-51 from the bottom left, and play pads 1-16 of the chosen pad bank (A-D),
//   laid out like the MPC's 4x4 grid.
// - The keys (any other channel) play the instrument picked in "Keys play" (the sampler by
//   default, or the MPC's pads note for note).
// - Knobs, faders and buttons do whatever you teach them: press Learn next to a fader or
//   button in the Rig, then move the control. Learned controls are saved with the show.
//
// Everything here runs on the audio thread, so settings are atomics the window can change.
class RigRouting
{
public:
    enum class Dest { off, mpc, sampler };

    // Things a knob, fader or button can control.
    enum Control
    {
        mpcLevel, samplerLevel, masterLevel,       // faders and knobs
        mpcMute, samplerMute,                      // buttons: each press toggles
        play, stop, metronome,                     // buttons
        numControls
    };
    static juce::String controlName (int control);
    static bool isFader (int control) { return control <= masterLevel; }

    // What a block of incoming MIDI turned into.
    struct Result
    {
        // Fader values (0..1), or -1 when the fader didn't move this block.
        std::array<float, numControls> values {};
        // How many times each button was pressed.
        std::array<int, numControls> presses {};
    };

    RigRouting();

    std::atomic<int> padChannel { 10 };              // 1-16; 0 = the pads don't play the MPC
    std::atomic<int> padBank { 0 };                  // 0-3 = A-D
    std::atomic<int> keysTo { (int) Dest::sampler };

    // Teaching a control: the next knob, fader or button that moves gets `control`.
    void learn (int control) { learning = control; }
    void cancelLearn() { learning = -1; }
    int getLearning() const { return learning.load(); }
    void forget (int control);
    // The control's MIDI channel (1-16) and CC number, or {0, -1} if it has none.
    std::pair<int, int> getAssignment (int control) const;
    void setAssignment (int control, int channel, int cc);

    // The Launchkey's first drum pad note; pads are this note plus 0-15.
    static constexpr int kFirstPadNote = 36;

    // Audio thread: splits `in` into what each tool plays, and returns control moves.
    void route (const juce::MidiBuffer& in, juce::MidiBuffer& toMpc, juce::MidiBuffer& toSampler, Result&);

    // The MPC note a drum pad note plays (-1 if it's not one of the 16 pads).
    int mpcNoteForPad (int padNote) const;

private:
    void handleController (int channel, int cc, int value, Result&);

    std::atomic<int> learning { -1 };
    // Packed assignments: channel * 128 + cc, or -1.
    std::array<std::atomic<int>, numControls> assigned;
    // Last value per button control, to catch presses (value going above 0).
    std::array<int, numControls> lastValue {};
};
