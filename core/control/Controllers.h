#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <vector>

// Your MIDI controllers, whatever they are: a Novation Launchkey, another keyboard (with drum
// pads or without), a pad controller (Launchpad, MPD, APC, Maschine...), or none at all (the
// Mac's keys always play too).
//
// Each controller plugged in gets a profile: what kind it is, where its pads start, how its
// pads are laid out, an octave for its keys, and which CCs its eight knobs send. The Rig is
// built around one way of talking (the Launchkey's): drum pads on MIDI channel 10 from note 36,
// keys on any other channel, knobs on CC 21-28. A Translator turns what any controller sends
// into that, so every screen (the MPC's pads, the drum machine, the chord builder, the keys and
// split, the knobs following the picked track) works the same with every controller.
//
// What isn't known from the box is learned: press "Learn pad 1" and hit the first pad, press
// "Learn knobs" and turn knobs 1 to 8.
namespace crate::controllers
{
    enum class Kind
    {
        launchkey,      // Novation Launchkey: keys, 16 drum pads on channel 10 from note 36, knobs CC 21-28
        keysAndPads,    // a keyboard with drum pads on their own channel (MPK mini, KeyLab, MiniLab, Oxygen...)
        keys,           // a keyboard: every note is a key, nothing is a pad
        pads,           // a pad controller: every note is a pad (up to 64, the MPC's four banks)
        off,            // not listened to
        numKinds
    };
    juce::String kindName (Kind);
    // One line on what the kind does, for the picker.
    juce::String kindHint (Kind);

    // How a pad controller's pads count up from pad 1's note.
    enum class Layout
    {
        blocks,   // four 4x4 blocks of 16 (MPC, MPD, a Launchpad in Drum mode): bottom left, bottom right, top left, top right
        rows      // rows of eight from the bottom left (an APC mini, a Launchpad's Programmer mode)
    };
    juce::String layoutName (Layout);

    // The Rig's own way: pads are notes from this on its pad channel, knobs these CCs.
    constexpr int kRigFirstPadNote = 36, kRigFirstKnobCc = 21, kNumKnobs = 8, kMaxPads = 64;

    struct Profile
    {
        Kind kind = Kind::keys;
        int padChannel = 10;        // 1-16: the channel its pads send on (keys and pads kinds)
        int firstPadNote = 36;      // the note pad 1 sends
        Layout layout = Layout::blocks;
        int octave = 0;             // -3..+3: moves the keys (not the pads)
        std::array<int, kNumKnobs> knobs { 21, 22, 23, 24, 25, 26, 27, 28 };   // CC per knob, -1 none

        static Profile defaultsFor (Kind);
        bool hasPads() const { return kind == Kind::launchkey || kind == Kind::keysAndPads || kind == Kind::pads; }
        bool hasKeys() const { return kind == Kind::launchkey || kind == Kind::keysAndPads || kind == Kind::keys; }
        // Pad index (0-63) a note on `channel` is, or -1.
        int padFor (int channel, int note) const;

        juce::ValueTree toTree() const;
        static Profile fromTree (const juce::ValueTree&);
        bool operator== (const Profile&) const;
        bool operator!= (const Profile& o) const { return ! (*this == o); }
    };

    // The best guess from a device's name: Launchkey, a known pad controller or keyboard with
    // pads, else a keyboard with drum pads on channel 10 (how the Rig always read everything).
    Profile guessFor (const juce::String& deviceName);

    // Turns one controller's MIDI into the Rig's. Message thread sets the profile and starts a
    // learn; translate() runs on the MIDI thread.
    class Translator
    {
    public:
        void setProfile (const Profile&);
        Profile getProfile() const;

        // Rewrites `m` the Rig's way, given the Rig's pad channel. False: drop it (the device is
        // off, or the message was caught by a learn).
        bool translate (juce::MidiMessage& m, int rigPadChannel);

        // Learning: the next note on is pad 1 (its note, and its channel unless a pad
        // controller); or the next eight different CCs are knobs 1-8 (stopLearning keeps the ones
        // turned so far).
        enum class Learn { none, pad, knobs };
        void learn (Learn);
        void stopLearning();
        Learn learning() const { return (Learn) learnWhat.load(); }
        int knobsLearned() const { return knobsSoFar.load(); }
        // True once since the profile changed by a learn (so the window can save it).
        bool takeLearned() { return learned.exchange (false); }

        // When it last sent anything (Time::getMillisecondCounter), so the picker can light it.
        juce::uint32 lastHeard() const { return heard.load(); }

    private:
        mutable juce::SpinLock lock;
        Profile profile;
        std::atomic<int> learnWhat { 0 }, knobsSoFar { 0 };
        std::atomic<bool> learned { false };
        std::atomic<juce::uint32> heard { 0 };
        std::array<int, kNumKnobs> caughtKnobs {};
        // The note each key down sent, +1 (0: none), so a key let go after the octave
        // changed still stops its note. By channel and note.
        std::array<std::array<juce::uint8, 128>, 16> sent {};
    };

    // Every controller: profiles saved in a file of their own (they belong to this Mac, not to
    // a show), translators by device name.
    class Setup
    {
    public:
        // `file` empty: no saving (tests).
        explicit Setup (juce::File file = {});
        static juce::File defaultFile();

        // Message thread. The profile a device has: saved, or guessed.
        Profile profileFor (const juce::String& deviceName);
        void setProfile (const juce::String& deviceName, const Profile&);
        // Whether you've seen this device in the picker (or set it), so a new one can be shown once.
        bool isKnown (const juce::String& deviceName) const;
        void markKnown (const juce::String& deviceName);
        bool isOn (const juce::String& deviceName) { return profileFor (deviceName).kind != Kind::off; }
        Translator& translatorFor (const juce::String& deviceName);
        // Saves any profile a learn changed. Returns true if one did.
        bool saveLearned();

        // MIDI thread: rewrites a device's message the Rig's way; false drops it.
        bool translate (const juce::String& deviceName, juce::MidiMessage&, int rigPadChannel);

    private:
        struct Slot
        {
            juce::String name;
            std::unique_ptr<Translator> translator;
        };
        Slot* find (const juce::String& name);
        juce::File file;
        juce::ValueTree saved { "Controllers" };
        // Slots are only ever added (never removed), under the lock, and each translator is on
        // the heap, so the MIDI thread can keep using one it found.
        juce::SpinLock slotsLock;
        std::vector<Slot> slots;
        void save();
    };
}
