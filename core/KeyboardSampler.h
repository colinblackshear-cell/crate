#pragma once

#include "Audio.h"

#include <array>
#include <atomic>
#include <vector>

namespace crate
{
    // Plays one sound across the keys: the root note plays it as recorded, each key above or
    // below plays it a semitone higher or lower (faster or slower, like a classic sampler).
    // Up to 16 notes at once; velocity sets the level.
    //
    // The sound and settings are set from the message thread. render() runs on the audio
    // thread and never allocates or frees.
    class KeyboardSampler
    {
    public:
        enum class Mode
        {
            hold,      // plays while the key is down, fades out when you let go
            loop,      // like hold, but loops the sound (or selection) while the key is down
            oneShot    // plays to the end every time, whatever the key does
        };

        KeyboardSampler();
        ~KeyboardSampler();

        // The sound to play. Notes still sounding fade out quickly.
        void setSound (SharedAudio);
        // The part of the sound the next notes play (e.g. the selection); empty plays it all.
        void setRange (juce::Range<int>);

        void setRootNote (int note) { rootNote = juce::jlimit (0, 127, note); }
        int getRootNote() const { return rootNote.load(); }
        void setMode (Mode m) { mode = (int) m; }
        Mode getMode() const { return (Mode) mode.load(); }
        void setAttack (float seconds) { attackSeconds = juce::jmax (0.0f, seconds); }
        float getAttack() const { return attackSeconds.load(); }
        void setRelease (float seconds) { releaseSeconds = juce::jmax (0.0f, seconds); }
        float getRelease() const { return releaseSeconds.load(); }

        // Audio thread.
        void prepare (double sampleRate);
        // Adds the notes in `midi` (and any still sounding) into `out`.
        void render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);
        void allNotesOff();
        int getActiveVoiceCount() const { return activeVoices.load(); }

        static constexpr int kMaxVoices = 16;

    private:
        struct Voice
        {
            const juce::AudioBuffer<float>* sound = nullptr;
            int note = -1;
            bool active = false, keyDown = false, quickFade = false;
            double pos = 0, step = 1;
            int start = 0, end = 0;
            float gain = 1, env = 0;
            bool releasing = false;
            juce::uint32 age = 0;
        };

        void noteOn (int note, float velocity);
        void noteOff (int note);
        void renderVoice (Voice&, juce::AudioBuffer<float>&, int from, int to);
        void freeUnusedSounds (std::vector<SharedAudio>& toFree);

        juce::SpinLock lock;
        SharedAudio sound;
        std::vector<SharedAudio> retired;       // older sounds voices may still be reading
        juce::Range<int> range;
        std::array<Voice, kMaxVoices> voices;
        juce::uint32 noteCounter = 0;
        double hostRate = kSampleRate;

        std::atomic<int> rootNote { 60 }, mode { (int) Mode::hold };
        std::atomic<float> attackSeconds { 0.002f }, releaseSeconds { 0.15f };
        std::atomic<int> activeVoices { 0 };
    };
}
