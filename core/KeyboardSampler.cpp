#include "KeyboardSampler.h"

#include <algorithm>
#include <cmath>

namespace crate
{
    KeyboardSampler::KeyboardSampler()
    {
        retired.reserve (8);
    }

    KeyboardSampler::~KeyboardSampler() = default;

    void KeyboardSampler::setSound (SharedAudio newSound)
    {
        std::vector<SharedAudio> toFree;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            if (sound != nullptr)
            {
                if (retired.size() == retired.capacity())
                    freeUnusedSounds (toFree);   // never grow on the audio thread's watch
                retired.push_back (std::move (sound));
            }
            sound = std::move (newSound);
            for (auto& v : voices)
                if (v.active)
                    v.quickFade = true;
            freeUnusedSounds (toFree);
        }
        // `toFree` releases its sounds here, on this thread
    }

    void KeyboardSampler::freeUnusedSounds (std::vector<SharedAudio>& toFree)
    {
        for (auto it = retired.begin(); it != retired.end();)
        {
            const bool used = std::any_of (voices.begin(), voices.end(),
                                           [&] (const Voice& v) { return v.active && v.sound == it->get(); });
            if (used)
                ++it;
            else
            {
                toFree.push_back (std::move (*it));
                it = retired.erase (it);
            }
        }
    }

    void KeyboardSampler::setRange (juce::Range<int> newRange)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        range = newRange;
    }

    void KeyboardSampler::prepare (double sampleRate)
    {
        hostRate = sampleRate > 0 ? sampleRate : kSampleRate;
    }

    void KeyboardSampler::allNotesOff()
    {
        for (auto& v : voices)
            if (v.active)
                v.quickFade = true;
    }

    void KeyboardSampler::noteOn (int note, float velocity)
    {
        if (sound == nullptr || sound->getNumSamples() < 2)
            return;

        auto r = range.getIntersectionWith ({ 0, sound->getNumSamples() });
        if (r.getLength() < 2)
            r = { 0, sound->getNumSamples() };

        // a free voice, or else the oldest one
        Voice* voice = nullptr;
        for (auto& v : voices)
            if (! v.active) { voice = &v; break; }
        if (voice == nullptr)
            voice = &*std::min_element (voices.begin(), voices.end(), [] (const Voice& a, const Voice& b) { return a.age < b.age; });

        // the same key pressed again lets the previous note fade (no build-up of copies)
        for (auto& v : voices)
            if (v.active && v.note == note && &v != voice)
                v.quickFade = true;

        auto& v = *voice;
        v.sound = sound.get();
        v.note = note;
        v.active = true;
        v.keyDown = true;
        v.quickFade = false;
        v.releasing = false;
        v.start = r.getStart();
        v.end = r.getEnd();
        v.pos = v.start;
        v.step = std::pow (2.0, (note - rootNote.load()) / 12.0) * kSampleRate / hostRate;
        v.gain = 0.15f + 0.85f * velocity * velocity;
        v.env = attackSeconds.load() <= 0.0005f ? 1.0f : 0.0f;
        v.age = ++noteCounter;
    }

    void KeyboardSampler::noteOff (int note)
    {
        for (auto& v : voices)
            if (v.active && v.note == note && v.keyDown)
                v.keyDown = false;
    }

    void KeyboardSampler::renderVoice (Voice& v, juce::AudioBuffer<float>& out, int from, int to)
    {
        const auto& src = *v.sound;
        const int outChannels = juce::jmin (2, out.getNumChannels());
        const int srcChannels = src.getNumChannels();
        const auto m = (Mode) mode.load();

        const float attackStep = 1.0f / juce::jmax (1.0f, attackSeconds.load() * (float) hostRate);
        const float releaseStep = 1.0f / juce::jmax (1.0f, releaseSeconds.load() * (float) hostRate);
        const float quickStep = 1.0f / juce::jmax (1.0f, 0.005f * (float) hostRate);

        for (int i = from; i < to; ++i)
        {
            // envelope
            const bool release = v.quickFade || (m != Mode::oneShot && ! v.keyDown);
            if (release)
            {
                v.env -= v.quickFade ? quickStep : releaseStep;
                if (v.env <= 0.0f)
                {
                    v.active = false;
                    return;
                }
            }
            else if (v.env < 1.0f)
            {
                v.env = juce::jmin (1.0f, v.env + attackStep);
            }

            if (v.pos >= v.end - 1)
            {
                if (m == Mode::loop && ! release)
                    v.pos = v.start + std::fmod (v.pos - (v.end - 1), (double) juce::jmax (1, v.end - 1 - v.start));
                else
                {
                    v.active = false;
                    return;
                }
            }

            const int index = (int) v.pos;
            const float frac = (float) (v.pos - index);
            const float g = v.gain * v.env;
            for (int ch = 0; ch < outChannels; ++ch)
            {
                const auto* d = src.getReadPointer (juce::jmin (ch, srcChannels - 1));
                out.addSample (ch, i, (d[index] + (d[index + 1] - d[index]) * frac) * g);
            }
            v.pos += v.step;
        }
    }

    void KeyboardSampler::render (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked())
            return;   // the UI is swapping the sound: skip one block

        const int numSamples = out.getNumSamples();
        int done = 0;

        auto renderTo = [&] (int to)
        {
            for (auto& v : voices)
                if (v.active)
                    renderVoice (v, out, done, to);
            done = to;
        };

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            const int at = juce::jlimit (done, numSamples, metadata.samplePosition);
            renderTo (at);

            if (msg.isNoteOn())
                noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
                noteOff (msg.getNoteNumber());
            else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                allNotesOff();
        }
        renderTo (numSamples);

        int count = 0;
        for (auto& v : voices)
            count += v.active ? 1 : 0;
        activeVoices = count;
    }
}
