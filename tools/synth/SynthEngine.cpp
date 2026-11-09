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
    numHeld = numArp = 0;
    lastArpIndex = -1;
    arpStep = arpPlaying = -1;
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
        if (arpPlaying >= 0)
            voiceOff (arpPlaying);
        arpPlaying = arpStep = -1;
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
    // A free voice, or the one playing this note, or else the oldest.
    Voice* chosen = nullptr;
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
        v.filter.reset();
    }
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
    if (! settings.arpOn)
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
        numHeld = numArp = 0;
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
    std::array<int, 96> notes {};
    int count = 0;
    std::array<int, 32> base {};
    std::copy (arpNotes.begin(), arpNotes.begin() + numArp, base.begin());
    if (settings.arpMode != ArpMode::asPlayed)
        std::sort (base.begin(), base.begin() + numArp);
    for (int o = 0; o < juce::jlimit (1, 3, settings.arpOctaves); ++o)
        for (int i = 0; i < numArp && count < (int) notes.size(); ++i)
            notes[(size_t) count++] = base[(size_t) i] + 12 * o;

    switch (settings.arpMode)
    {
        case ArpMode::up:
        case ArpMode::asPlayed:
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

    const int note = nextArpNote();
    if (note < 0)
        return;
    if (arpPlaying >= 0)
        voiceOff (arpPlaying);
    voiceOn (note, rhythm.accent[(size_t) arpStep] ? 1.0f : 0.75f);
    arpPlaying = note;
    arpOffBeat = beat + settings.arpGate * stepBeats;
}

void Engine::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const Clock& clock)
{
    for (auto& v : voices)
    {
        v.amp.set (settings.attack, settings.decay, settings.sustain, settings.release);
        v.filterEnv.set (0.002f, settings.filterDecay, 0.0f, settings.release);
    }

    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    const double stepBeats = synth::SyncRates::beats (settings.arpRate);
    double beat = clock.playing ? clock.ppq : freeClock;
    lfo.setShape (settings.lfoShape);
    lfo.setIncrement (beatsPerSample / synth::SyncRates::beats (settings.lfoRate));
    if (clock.playing)
        lfo.syncTo (clock.ppq / synth::SyncRates::beats (settings.lfoRate));

    if (! settings.arpOn && arpPlaying >= 0)
    {
        voiceOff (arpPlaying);
        arpPlaying = -1;
        arpStep = -1;
    }

    const float detuneCents = settings.detune * 30.0f;
    const float spread[3] = { 1.0f, std::exp2 (detuneCents / 1200.0f), std::exp2 (-detuneCents / 1200.0f) };
    const float octaveShift = 12.0f * (float) settings.octave;

    auto event = midi.begin();
    for (int i = 0; i < numSamples; ++i, beat += beatsPerSample)
    {
        tapSample = i;
        for (; event != midi.end() && (*event).samplePosition <= i; ++event)
            handleMidi ((*event).getMessage());

        if (settings.arpOn)
        {
            const auto index = (long long) std::floor (beat / stepBeats + 1.0e-9);
            if (index != lastArpIndex)
            {
                lastArpIndex = index;
                arpTick (index, beat, stepBeats);
            }
            if (arpPlaying >= 0 && beat >= arpOffBeat)
            {
                voiceOff (arpPlaying);
                arpPlaying = -1;
            }
        }

        const float wobble = lfo.next();
        const bool updateFilter = (filterCounter++ & 15) == 0;

        float out = 0.0f;
        for (auto& v : voices)
        {
            if (! v.amp.isActive())
                continue;

            const float hz = synth::noteToHz ((float) v.note + octaveShift);
            const float dt = juce::jmin (0.45f, hz / (float) sampleRate);
            float s = 0.0f;
            switch (settings.wave)
            {
                case synth::Wave::saw:      s = 0.7f * v.osc[0].saw (dt) + 0.3f * v.osc[1].saw (dt * spread[1]); break;
                case synth::Wave::square:   s = 0.7f * v.osc[0].square (dt) + 0.3f * v.osc[1].square (dt * spread[1]); break;
                case synth::Wave::triangle: s = v.osc[0].triangle (dt); break;
                case synth::Wave::supersaw:
                    s = 0.45f * (v.osc[0].saw (dt) + v.osc[1].saw (dt * spread[1]) + v.osc[2].saw (dt * spread[2]));
                    break;
            }

            const float env = v.filterEnv.next();
            if (updateFilter)
            {
                const float octaves = settings.envMod * env * 5.0f + settings.lfoDepth * wobble * 3.0f;
                v.filter.set (settings.cutoff * std::exp2 (octaves), settings.resonance);
            }
            out += v.filter.process (s * 0.5f) * v.amp.next() * (0.4f + 0.6f * v.velocity);
        }

        out = std::tanh (out * 0.6f) * settings.volume;
        left[i] += out;
        if (right != nullptr)
            right[i] += out;
    }
    for (; event != midi.end(); ++event)
        handleMidi ((*event).getMessage());

    if (! clock.playing)
        freeClock = beat;
}
}
