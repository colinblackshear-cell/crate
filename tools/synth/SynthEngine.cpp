#include "SynthEngine.h"

#include <algorithm>

namespace crate::synthtool
{
void Engine::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
    for (auto& v : voices)
    {
        v.filter.prepare (sampleRate);
        v.amp.prepare (sampleRate);
        v.filterEnv.prepare (sampleRate);
    }
    reset();
}

void Engine::reset()
{
    for (auto& v : voices)
    {
        v.amp.kill();
        v.filterEnv.kill();
        v.filter.reset();
        v.note = -1;
        v.held = false;
    }
    numHeld = numArp = numSounding = 0;
    lastArpIndex = -1;
    arpStep = -1;
    arpRepeat = 0;
    arpLastNote = -1;
    lastPitch = -1.0f;
}

void Engine::arpRelease()
{
    for (int i = 0; i < numSounding; ++i)
        voiceOff (arpSounding[(size_t) i]);
    numSounding = 0;
}

void Engine::setSettings (const Settings& s)
{
    const bool arpWasOn = settings.arpOn, latchWasOn = settings.arpOn && settings.latch;
    settings = s;
    auto arpTheHeldKeys = [this]
    {
        std::copy (held.begin(), held.begin() + numHeld, arpNotes.begin());
        numArp = numHeld;
    };

    if (s.arpOn && ! arpWasOn)
    {
        for (int i = 0; i < numHeld; ++i)
            voiceOff (held[(size_t) i]);
        arpTheHeldKeys();
    }
    else if (! s.arpOn && arpWasOn)
    {
        arpRelease();
        arpStep = -1;
        numArp = 0;
        for (int i = 0; i < numHeld; ++i)
            voiceOn (held[(size_t) i], 0.8f);
    }
    else if (s.arpOn && latchWasOn && ! s.latch)
    {
        arpTheHeldKeys();
    }
}

int Engine::getActiveVoices() const
{
    int n = 0;
    for (const auto& v : voices)
        n += v.amp.isActive() ? 1 : 0;
    return n;
}

void Engine::voiceOn (int note, float velocity)
{
    if (noteTap != nullptr)
        noteTap->addEvent (juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, note), juce::jlimit (0.05f, 1.0f, velocity)), tapSample);

    const bool mono = settings.voiceMode != VoiceMode::poly;
    Voice* chosen = nullptr;
    if (mono)
    {
        // One voice. Legato: a note played while it still sounds slides there without starting
        // the envelopes again.
        auto& v = voices[0];
        for (size_t i = 1; i < voices.size(); ++i)
            if (voices[i].amp.isActive())
                voices[i].amp.gateOff();
        const bool slide = settings.voiceMode == VoiceMode::legato && v.amp.isActive() && v.held;
        if (! v.amp.isActive())
        {
            for (size_t i = 0; i < v.osc.size(); ++i)
                v.osc[i].phase = random.nextFloat();
            v.filter.reset();
            v.pitch = settings.glide > 0.0f && lastPitch >= 0.0f ? lastPitch : (float) note;
        }
        v.note = note;
        v.velocity = velocity;
        v.started = ++counter;
        v.held = true;
        v.pan = 0.0f;
        if (settings.glide <= 0.0f && ! slide)
            v.pitch = (float) note;
        if (! slide)
        {
            v.amp.gateOn (true);
            v.filterEnv.gateOn (true);
        }
        lastPitch = (float) note;
        return;
    }

    // A free voice, or the one playing this note, or else the oldest.
    for (auto& v : voices)
        if (v.note == note && v.amp.isActive())
            chosen = &v;
    if (chosen == nullptr)
        for (auto& v : voices)
            if (! v.amp.isActive())
            {
                chosen = &v;
                break;
            }
    if (chosen == nullptr)
    {
        chosen = &voices[0];
        for (auto& v : voices)
            if (v.started < chosen->started)
                chosen = &v;
    }

    auto& v = *chosen;
    if (! v.amp.isActive())
    {
        // Spread the supersaw's phases so voices don't start identically.
        for (size_t i = 0; i < v.osc.size(); ++i)
            v.osc[i].phase = random.nextFloat();
        v.osc2.phase = random.nextFloat();
        v.filter.reset();
    }
    // Poly glide: each new note slides from the last one played.
    v.pitch = settings.glide > 0.0f && lastPitch >= 0.0f ? lastPitch : (float) note;
    lastPitch = (float) note;
    // The voices take turns left and right (Width sets how far).
    static constexpr float pans[] = { -0.8f, 0.8f, -0.4f, 0.4f, -1.0f, 1.0f, -0.2f, 0.2f };
    v.pan = pans[(size_t) (voiceCounter++ & 7)];
    v.note = note;
    v.velocity = velocity;
    v.started = ++counter;
    v.held = true;
    v.amp.gateOn (true);
    v.filterEnv.gateOn (true);
}

void Engine::voiceOff (int note)
{
    if (noteTap != nullptr)
        noteTap->addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, note)), tapSample);
    for (auto& v : voices)
        if (v.note == note && v.held)
        {
            v.held = false;
            v.amp.gateOff();
            v.filterEnv.gateOff();
        }
}

void Engine::keyDown (int note, float velocity)
{
    if (settings.arpOn && settings.latch && numHeld == 0)
        numArp = 0;   // a new chord after letting go replaces the latched one

    if (numHeld < (int) held.size())
        held[(size_t) numHeld++] = note;

    if (settings.arpOn)
    {
        arpLastNote = -1;
        if (numArp < (int) arpNotes.size())
            arpNotes[(size_t) numArp++] = note;
        return;
    }
    voiceOn (note, velocity);
}

void Engine::keyUp (int note)
{
    auto remove = [note] (std::array<int, 32>& list, int& count)
    {
        for (int i = 0; i < count; ++i)
            if (list[(size_t) i] == note)
            {
                for (int j = i; j < count - 1; ++j)
                    list[(size_t) j] = list[(size_t) j + 1];
                --count;
                return;
            }
    };
    remove (held, numHeld);
    if (! (settings.arpOn && settings.latch))
        remove (arpNotes, numArp);
    if (settings.arpOn)
        return;
    if (settings.voiceMode != VoiceMode::poly && voices[0].note == note && numHeld > 0)
    {
        // Mono: back to the key still held (a slide in Legato).
        if (noteTap != nullptr)
            noteTap->addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, note)), tapSample);
        voiceOn (held[(size_t) numHeld - 1], voices[0].velocity);
        return;
    }
    if (settings.voiceMode != VoiceMode::poly && voices[0].note != note)
    {
        if (noteTap != nullptr)
            noteTap->addEvent (juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, note)), tapSample);
        return;   // a key that isn't sounding any more
    }
    voiceOff (note);
}

void Engine::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        keyDown (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        keyUp (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        if (noteTap != nullptr)
            noteTap->addEvent (juce::MidiMessage::allNotesOff (1), tapSample);
        numHeld = numArp = numSounding = 0;
        for (auto& v : voices)
            if (v.held)
            {
                v.held = false;
                v.amp.gateOff();
                v.filterEnv.gateOff();
            }
    }
}

int Engine::nextArpNote()
{
    if (numArp == 0)
        return -1;

    // The notes in order (or as played), repeated up the octaves.
    std::array<int, 128> notes {};
    int count = 0;
    std::array<int, 32> base {};
    std::copy (arpNotes.begin(), arpNotes.begin() + numArp, base.begin());
    if (settings.arpMode != ArpMode::asPlayed)
        std::sort (base.begin(), base.begin() + numArp);
    for (int o = 0; o < juce::jlimit (1, 4, settings.arpOctaves); ++o)
        for (int i = 0; i < numArp && count < (int) notes.size(); ++i)
            notes[(size_t) count++] = base[(size_t) i] + 12 * o;

    if (settings.arpMode == ArpMode::converge)
    {
        // Outside in: lowest, highest, second lowest, second highest...
        std::array<int, 128> order {};
        for (int i = 0, lo = 0, hi = count - 1; i < count; ++i)
            order[(size_t) i] = notes[(size_t) ((i & 1) == 0 ? lo++ : hi--)];
        notes = order;
    }
    else if (settings.arpMode == ArpMode::pinky)
    {
        // The top note between each of the others: 1 top 2 top 3 top.
        if (count > 1)
        {
            std::array<int, 128> order {};
            const int top = notes[(size_t) count - 1];
            int n = 0;
            for (int i = 0; i < count - 1 && n + 1 < (int) order.size(); ++i)
            {
                order[(size_t) n++] = notes[(size_t) i];
                order[(size_t) n++] = top;
            }
            notes = order;
            count = n;
        }
    }

    switch (settings.arpMode)
    {
        case ArpMode::up:
        case ArpMode::asPlayed:
        case ArpMode::converge:
        case ArpMode::pinky:
        case ArpMode::chord:
            arpPosition = (arpPosition + 1) % count;
            break;
        case ArpMode::down:
            arpPosition = arpPosition <= 0 || arpPosition >= count ? count - 1 : arpPosition - 1;
            break;
        case ArpMode::upDown:
            if (count == 1)
            {
                arpPosition = 0;
                break;
            }
            arpPosition += arpDirection;
            if (arpPosition >= count)
            {
                arpDirection = -1;
                arpPosition = count - 2;
            }
            else if (arpPosition < 0)
            {
                arpDirection = 1;
                arpPosition = 1;
            }
            break;
        case ArpMode::random:
            arpPosition = random.nextInt (count);
            break;
    }
    arpPosition = juce::jlimit (0, count - 1, arpPosition);
    return notes[(size_t) arpPosition];
}

void Engine::arpTick (long long index, double beat, double stepBeats)
{
    const int length = juce::jlimit (1, kRhythmSteps, rhythm.length);
    arpStep = (int) (((index % length) + length) % length);
    if (! rhythm.on[(size_t) arpStep] || numArp == 0)
        return;
    if (settings.arpChance < 0.999f && random.nextFloat() >= settings.arpChance)
        return;

    const float velocity = rhythm.accent[(size_t) arpStep] ? 1.0f : 0.75f;
    arpRelease();

    if (settings.arpMode == ArpMode::chord)
    {
        // The whole chord on each step, climbing the octaves one step at a time.
        const int octaves = juce::jlimit (1, 4, settings.arpOctaves);
        if (++arpRepeat >= juce::jlimit (1, 4, settings.arpRepeats))
        {
            arpRepeat = 0;
            arpPosition = (arpPosition + 1) % octaves;
        }
        const int octave = juce::jlimit (0, octaves - 1, arpPosition);
        for (int i = 0; i < numArp && numSounding < (int) arpSounding.size(); ++i)
        {
            const int note = arpNotes[(size_t) i] + 12 * octave;
            voiceOn (note, velocity);
            arpSounding[(size_t) numSounding++] = note;
        }
    }
    else
    {
        // Repeats: the same note for this many lit steps, then the next.
        int note = arpLastNote;
        if (note < 0 || ++arpRepeat >= juce::jlimit (1, 4, settings.arpRepeats))
        {
            arpRepeat = 0;
            note = nextArpNote();
        }
        if (note < 0)
            return;
        arpLastNote = note;
        voiceOn (note, velocity);
        arpSounding[0] = note;
        numSounding = 1;
    }
    arpOffBeat = beat + settings.arpGate * stepBeats;
}

void Engine::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const Clock& clock)
{
    for (auto& v : voices)
    {
        v.amp.set (settings.attack, settings.decay, settings.sustain, settings.release);
        v.filterEnv.set (settings.filterAttack, settings.filterDecay, settings.filterSustain, settings.release);
        v.filter.setType (settings.filterType);
    }

    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    // A step's length in beats: from the tempo, or free in Hz.
    const double stepBeats = settings.arpSync ? synth::ArpRates::beats (settings.arpSpeed)
                                              : (clock.bpm / 60.0) / (double) juce::jlimit (0.05f, 50.0f, settings.arpHz);
    // Swing pushes every second step later by up to half a step.
    const double swingDelay = juce::jlimit (0.0f, 1.0f, settings.arpSwing) * 0.5 * stepBeats;
    double beat = clock.playing ? clock.ppq : freeClock;
    lfo.setShape (settings.lfoShape);
    lfo.setIncrement (beatsPerSample / synth::SyncRates::beats (settings.lfoRate));
    if (clock.playing)
        lfo.syncTo (clock.ppq / synth::SyncRates::beats (settings.lfoRate));

    if (! settings.arpOn && numSounding > 0)
    {
        arpRelease();
        arpStep = -1;
    }

    const float detuneCents = settings.detune * 30.0f;
    const float spread[3] = { 1.0f, std::exp2 (detuneCents / 1200.0f), std::exp2 (-detuneCents / 1200.0f) };
    const float octaveShift = 12.0f * (float) settings.octave;
    const float osc2Ratio = std::exp2 (((float) settings.osc2Semi + settings.osc2Fine / 100.0f) / 12.0f);
    const float glideCoef = settings.glide > 0.0f ? std::exp (-1.0f / (float) juce::jmax (1.0, settings.glide * sampleRate * 0.3)) : 0.0f;
    const float driveGain = 0.6f + settings.drive * 3.0f;
    const float driveTrim = 1.0f / (1.0f + settings.drive * 1.2f);
    const float width = juce::jlimit (0.0f, 1.0f, settings.width);

    auto event = midi.begin();
    for (int i = 0; i < numSamples; ++i, beat += beatsPerSample)
    {
        tapSample = i;
        for (; event != midi.end() && (*event).samplePosition <= i; ++event)
            handleMidi ((*event).getMessage());

        if (settings.arpOn)
        {
            // Steps in pairs: the second of each pair waits for the swing.
            const auto pair = (long long) std::floor (beat / (2.0 * stepBeats) + 1.0e-9);
            const double inPair = beat - (double) pair * 2.0 * stepBeats;
            const bool second = inPair + 1.0e-9 >= stepBeats + swingDelay;
            const auto index = pair * 2 + (second ? 1 : 0);
            if (index != lastArpIndex)
            {
                lastArpIndex = index;
                // The first of a pair is longer by the swing, the second shorter.
                arpTick (index, beat, stepBeats + (second ? -swingDelay : swingDelay));
            }
            if (numSounding > 0 && beat >= arpOffBeat)
                arpRelease();
        }

        const float wobble = lfo.next();
        const bool updateFilter = (filterCounter++ & 15) == 0;
        const float pitchWobble = settings.lfoDest == LfoDest::pitch ? settings.lfoDepth * wobble * 1.0f : 0.0f;   // up to a semitone
        const float volumeWobble = settings.lfoDest == LfoDest::volume ? 1.0f - settings.lfoDepth * (0.5f + 0.5f * wobble) : 1.0f;
        const float width01 = settings.lfoDest == LfoDest::pulseWidth
                                  ? juce::jlimit (0.05f, 0.5f, settings.pulseWidth - settings.lfoDepth * 0.22f * (1.0f + wobble))
                                  : settings.pulseWidth;

        float outL = 0.0f, outR = 0.0f;
        for (auto& v : voices)
        {
            if (! v.amp.isActive())
                continue;

            v.pitch = glideCoef > 0.0f ? (float) v.note + (v.pitch - (float) v.note) * glideCoef : (float) v.note;
            const float hz = synth::noteToHz (v.pitch + octaveShift + pitchWobble);
            const float dt = juce::jmin (0.45f, hz / (float) sampleRate);
            float s = 0.0f;
            switch (settings.wave)
            {
                case synth::Wave::saw:      s = 0.7f * v.osc[0].saw (dt) + 0.3f * v.osc[1].saw (dt * spread[1]); break;
                case synth::Wave::square:   s = 0.7f * v.osc[0].square (dt, width01) + 0.3f * v.osc[1].square (dt * spread[1], width01); break;
                case synth::Wave::triangle: s = v.osc[0].triangle (dt); break;
                case synth::Wave::supersaw:
                    s = 0.45f * (v.osc[0].saw (dt) + v.osc[1].saw (dt * spread[1]) + v.osc[2].saw (dt * spread[2]));
                    break;
            }
            if (settings.osc2Wave != Osc2Wave::off)
            {
                const float dt2 = juce::jmin (0.45f, dt * osc2Ratio);
                float o2 = 0.0f;
                switch (settings.osc2Wave)
                {
                    case Osc2Wave::saw:      o2 = v.osc2.saw (dt2); break;
                    case Osc2Wave::square:   o2 = v.osc2.square (dt2, width01); break;
                    case Osc2Wave::triangle: o2 = v.osc2.triangle (dt2); break;
                    case Osc2Wave::sine:     o2 = v.osc2.sine (dt2); break;
                    case Osc2Wave::off:      break;
                }
                s += settings.osc2Level * o2;
            }
            if (settings.subLevel > 0.0f)
                s += settings.subLevel * 0.8f * v.sub.square (dt * 0.5f);
            if (settings.noiseLevel > 0.0f)
                s += settings.noiseLevel * 0.6f * (random.nextFloat() * 2.0f - 1.0f);

            const float env = v.filterEnv.next();
            if (updateFilter)
            {
                float octaves = settings.envMod * env * 5.0f
                                + settings.keyTrack * ((float) v.note - 60.0f) / 12.0f
                                + settings.velFilter * (v.velocity - 0.6f) * 3.0f;
                if (settings.lfoDest == LfoDest::filter)
                    octaves += settings.lfoDepth * wobble * 3.0f;
                v.filter.set (settings.cutoff * std::exp2 (octaves), settings.resonance);
            }
            const float out = v.filter.process (s * 0.5f) * v.amp.next() * (0.4f + 0.6f * v.velocity);
            const float pan = v.pan * width;
            outL += out * (1.0f - juce::jmax (0.0f, pan));
            outR += out * (1.0f + juce::jmin (0.0f, pan));
        }

        const float gain = settings.volume * volumeWobble * driveTrim;
        const float l = std::tanh (outL * driveGain) * gain;
        const float r = std::tanh (outR * driveGain) * gain;
        if (right != nullptr)
        {
            left[i] += l;
            right[i] += r;
        }
        else
        {
            left[i] += 0.5f * (l + r);
        }
    }
    for (; event != midi.end(); ++event)
        handleMidi ((*event).getMessage());

    if (! clock.playing)
        freeClock = beat;
}
}
