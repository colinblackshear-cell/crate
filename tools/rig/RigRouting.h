#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

// Where the Rig sends what you play on a keyboard like the Launchkey 61.
//
// - Drum pads (MIDI channel 10, the Launchkey's drum mode) play the MPC's pads. The 16 pads
//   send notes 36-51 from the bottom left, and play pads 1-16 of the chosen pad bank (A-D),
//   laid out like the MPC's 4x4 grid.
// - The keys (any other channel) play the instrument picked in "Keys play": the vocoder (by
//   default), the bass, the synth, the sampler, or the MPC's pads note for note. With a split
//   point, the keys below it play a second instrument (say the bass with your left hand).
// - Knobs can also be learned onto any setting of any tool (the bass's cutoff, the synth's LFO
//   depth...): see learnParameterKnob().
// - Knobs, faders and buttons do whatever you teach them: press Learn next to a fader or
//   button in the Rig, then move the control. Learned controls are saved with the show.
//
// Everything here runs on the audio thread, so settings are atomics the window can change.
class RigRouting
{
public:
    enum class Dest { off, mpc, sampler, vocoder, bass, synth, drums };
    static constexpr int kNumDests = 7;
    static juce::String destName (int);

    // Things a knob, fader or button can control.
    enum Control
    {
        mpcLevel, samplerLevel, masterLevel,       // faders and knobs
        mpcMute, samplerMute,                      // buttons: each press toggles
        play, stop, metronome,                     // buttons
        vocoderLevel,                              // a fader (added later, so saved shows keep their numbers)
        vocoderMute, vocoderOn,                    // buttons
        bassLevel, synthLevel,                     // faders
        bassMute, synthMute,                       // buttons
        inputLevel,                                // a fader
        inputMute, inputHear,                      // buttons
        loopsLevel,                                // a fader
        loopsMute,                                 // a button
        loopRecord1, loopRecord8 = loopRecord1 + 7, // buttons: record / stop / overdub each layer
        loopMute1, loopMute8 = loopMute1 + 7,       // buttons: mute each layer
        nextScene, previousScene,                  // buttons
        drumsLevel,                                // a fader (the drum machine)
        drumsMute,                                 // a button
        numControls
    };
    static juce::String controlName (int control);
    static bool isFader (int control) { return control <= masterLevel || control == vocoderLevel || control == bassLevel || control == synthLevel || control == inputLevel || control == loopsLevel || control == drumsLevel; }

    // What a block of incoming MIDI turned into.
    struct Result
    {
        // Fader values (0..1), or -1 when the fader didn't move this block.
        std::array<float, numControls> values {};
        // How many times each button was pressed.
        std::array<int, numControls> presses {};
        // Learned knobs that moved: which tool, which parameter, the new value (0..1).
        struct ParamMove { int tool = 0, parameter = 0; float value = 0; };
        std::array<ParamMove, 32> paramMoves {};
        int numParamMoves = 0;
    };

    RigRouting();

    std::atomic<int> padChannel { 10 };              // 1-16; 0 = the pads don't play the MPC
    std::atomic<int> padBank { 0 };                  // 0-3 = A-D
    // What the pads play: the MPC (Dest::mpc), or the drum machine (Dest::drums: pad 1 the
    // kick, then a sound per pad up the octave).
    std::atomic<int> padsTo { (int) Dest::mpc };
    std::atomic<int> keysTo { (int) Dest::vocoder };
    std::atomic<int> splitNote { 0 };                 // 0 = no split; else keys below it play lowerKeysTo
    std::atomic<int> lowerKeysTo { (int) Dest::bass };

    // Teaching a control: the next knob, fader or button that moves gets `control`.
    void learn (int control) { learning = control; }
    void cancelLearn() { learning = -1; }
    int getLearning() const { return learning.load(); }
    void forget (int control);
    // The control's MIDI channel (1-16) and CC number, or {0, -1} if it has none.
    std::pair<int, int> getAssignment (int control) const;
    void setAssignment (int control, int channel, int cc);

    //==========================================================================
    // Knobs on any setting. The window finds which setting you touched; the next knob you turn
    // is caught here (takeCaughtKnob), and the two are paired with addParameterKnob.
    static constexpr int kMaxParameterKnobs = 64;
    struct ParameterKnob { int channel = 0, cc = -1, tool = 0, parameter = 0; };
    void catchNextKnob() { caught = -1; catching = true; }
    void stopCatching() { catching = false; }
    bool isCatching() const { return catching.load(); }
    // The knob caught (channel, cc), or {0, -1} if none yet.
    std::pair<int, int> takeCaughtKnob();
    void addParameterKnob (int channel, int cc, int tool, int parameter);
    void removeParameterKnob (int index);
    void clearParameterKnobs();
    std::vector<ParameterKnob> getParameterKnobs() const;

    //==========================================================================
    // The keyboard's eight knobs follow the track picked on the Main screen: knob 1 turns that
    // track's first knob, and so on. The knobs are the CCs from kFirstFollowCc (21-28, what the
    // Launchkey's knobs send) on any channel, unless a knob is learned onto something else.
    static constexpr int kFollowKnobs = 8, kFirstFollowCc = 21;
    // Points knob `index` at a tool's parameter (its index in the tool's parameter list), or
    // nothing with parameter -1.
    void setFollowKnob (int index, int tool, int parameter);
    std::pair<int, int> getFollowKnob (int index) const;

    // The Launchkey's first drum pad note; pads are this note plus 0-15.
    static constexpr int kFirstPadNote = 36;

    // Audio thread: splits `in` into what each tool plays, and returns control moves.
    // `to` has one buffer per Dest (off is never written).
    void route (const juce::MidiBuffer& in, std::array<juce::MidiBuffer*, kNumDests>& to, Result&);

    // The MPC note a drum pad note plays (-1 if it's not one of the 16 pads).
    int mpcNoteForPad (int padNote) const;

private:
    // Where each key that's down went (by channel and note; 0 = not down), so its release goes
    // to the same tool even if the keys were sent somewhere else meanwhile. Audio thread.
    std::array<std::array<juce::uint8, 128>, 16> sounding {};
    // The MPC note each drum pad held down is playing (+1; 0: none), so letting go stops that
    // pad even if the pad bank changed meanwhile. Audio thread.
    std::array<juce::int16, 128> padSounding {};
    void handleController (int channel, int cc, int value, Result&);

    std::atomic<int> learning { -1 };
    std::atomic<bool> catching { false };
    std::atomic<int> caught { -1 };
    // Parameter knobs: packed cc (channel * 128 + cc, or -1), tool, parameter. Written on the
    // message thread with the slot's cc set last, so the audio thread never sees half a knob.
    struct AtomicKnob { std::atomic<int> packed { -1 }, tool { 0 }, parameter { 0 }; };
    std::array<AtomicKnob, kMaxParameterKnobs> knobs;
    std::array<AtomicKnob, kFollowKnobs> follow;   // packed = parameter (-1 = none)
    // Packed assignments: channel * 128 + cc, or -1.
    std::array<std::atomic<int>, numControls> assigned;
    // Last value per button control, to catch presses (value going above 0).
    std::array<int, numControls> lastValue {};
};
