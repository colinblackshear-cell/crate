#include "BassEngine.h"

namespace crate::bass
{
void Engine::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
    filter.prepare (sampleRate);
    amp.prepare (sampleRate);
    filterEnv.prepare (sampleRate);
    reset();
}

void Engine::reset()
{
    filter.reset();
    amp.kill();
    filterEnv.kill();
    numHeld = 0;
    lastStepIndex = -1;
    currentStep = -1;
    stepGateOpen = false;
    wasPlaying = false;
}

void Engine::noteOn (int note, float vel, bool accent, bool legato)
{
    if (noteTap != nullptr)
    {
        const int n = juce::jlimit (0, 127, note);
        const int previous = tapNote;
        // A slide plays the new note before letting the old one go, so the other synth glides.
        if (previous >= 0 && ! legato)
            noteTap->addEvent (juce::MidiMessage::noteOff (1, previous), tapSample);
        noteTap->addEvent (juce::MidiMessage::noteOn (1, n, juce::jlimit (0.05f, 1.0f, accent ? 1.0f : vel * 0.8f)), tapSample);
        if (previous >= 0 && legato && previous != n)
            noteTap->addEvent (juce::MidiMessage::noteOff (1, previous), tapSample);
        tapNote = n;
    }
    targetNote = (float) juce::jlimit (0, 127, note);
    noteAccent = accent ? settings.accent : 0.0f;
    velocity = vel;
    gliding = legato && amp.isActive();
    if (! gliding)
    {
        currentNote = targetNote;
        amp.gateOn (true);
        filterEnv.gateOn (true);
    }
}

void Engine::noteOff()
{
    if (noteTap != nullptr && tapNote >= 0)
    {
        noteTap->addEvent (juce::MidiMessage::noteOff (1, tapNote), tapSample);
        tapNote = -1;
    }
    amp.gateOff();
    filterEnv.gateOff();
}

void Engine::handleMidi (const juce::MidiMessage& m, bool sequencing)
{
    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        if (noteTap != nullptr)
        {
            noteTap->addEvent (juce::MidiMessage::allNotesOff (1), tapSample);
            tapNote = -1;
        }
        numHeld = 0;
        noteOff();
        return;
    }

    if (m.isNoteOn())
    {
        const int note = m.getNoteNumber();
        if (numHeld < (int) heldKeys.size())
            heldKeys[(size_t) numHeld++] = note;

        if (sequencing && settings.keysTranspose)
            transpose = juce::jlimit (-24, 24, note - settings.root);
        else
            noteOn (note, m.getFloatVelocity(), m.getFloatVelocity() > 0.85f, numHeld > 1);
        return;
    }

    if (m.isNoteOff())
    {
        const int note = m.getNoteNumber();
        for (int i = 0; i < numHeld; ++i)
            if (heldKeys[(size_t) i] == note)
            {
                for (int j = i; j < numHeld - 1; ++j)
                    heldKeys[(size_t) j] = heldKeys[(size_t) j + 1];
                --numHeld;
                break;
            }

        if (sequencing && settings.keysTranspose)
            return;   // the pattern stays where the last key put it

        // Mono, last note wins: go back to the key still held, or stop.
        if (numHeld > 0)
            noteOn (heldKeys[(size_t) numHeld - 1], velocity, false, true);
        else
            noteOff();
    }
}

float Engine::renderSample (double beatsPerSample)
{
    // Pitch, gliding to the target on slides.
    if (gliding)
    {
        const float coef = std::exp (-1.0f / (float) juce::jmax (1.0, settings.glide * sampleRate * 0.3));
        currentNote = targetNote + (currentNote - targetNote) * coef;
        if (std::abs (currentNote - targetNote) < 0.001f)
            gliding = false;
    }
    else
    {
        currentNote = targetNote;
    }

    const float hz = synth::noteToHz (currentNote);
    const float dt = juce::jmin (0.45f, hz / (float) sampleRate);
    float v = 0.0f;
    switch (settings.wave)
    {
        case synth::Wave::square:   v = osc.square (dt); break;
        case synth::Wave::triangle: v = osc.triangle (dt); break;
        default:                    v = osc.saw (dt); break;
    }
    v += settings.sub * 0.8f * subOsc.sine (dt * 0.5f);

    // The filter: cutoff, opened by the envelope (more on accents) and moved by the LFO.
    const float env = filterEnv.next();
    lfo.setIncrement (beatsPerSample / synth::SyncRates::beats (settings.lfoRate));
    const float wobble = lfo.next();
    if ((filterCounter++ & 7) == 0)
    {
        const float octaves = (settings.envMod * (1.0f + noteAccent)) * env * 5.0f
                              + settings.lfoDepth * wobble * 3.0f;
        filter.set (settings.cutoff * std::exp2 (octaves), juce::jmin (1.0f, settings.resonance + noteAccent * 0.15f));
    }

    const float drive = 1.0f + settings.drive * 4.0f;
    float out = filter.process (v * 0.5f * drive);
    out *= amp.next() * (0.7f + 0.3f * velocity) * (1.0f + noteAccent * 0.6f);
    return std::tanh (out * 1.2f) * settings.volume;
}

void Engine::render (float* left, float* right, int numSamples, const juce::MidiBuffer& midi, const Clock& clock)
{
    amp.set (0.003f, 1.5f, 0.85f, 0.012f);
    filterEnv.set (0.001f, settings.decay, 0.0f, settings.decay);

    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    const bool sequencing = clock.playing && settings.sequencerOn && pattern != nullptr;

    if (wasPlaying && ! sequencing)
    {
        // The pattern stopped: let the last note go.
        if (stepGateOpen)
            noteOff();
        stepGateOpen = false;
        currentStep = -1;
        lastStepIndex = -1;
    }
    wasPlaying = sequencing;

    const double stepBeats = synth::SyncRates::beats (settings.stepRate);
    double beat = clock.playing ? clock.ppq : freeClock;
    if (clock.playing)
        lfo.syncTo (clock.ppq / synth::SyncRates::beats (settings.lfoRate));

    auto event = midi.begin();
    for (int i = 0; i < numSamples; ++i, beat += beatsPerSample)
    {
        tapSample = i;
        for (; event != midi.end() && (*event).samplePosition <= i; ++event)
            handleMidi ((*event).getMessage(), sequencing);

        if (sequencing)
        {
            const auto index = (long long) std::floor (beat / stepBeats + 1.0e-9);
            if (index != lastStepIndex)
            {
                lastStepIndex = index;
                const int length = juce::jlimit (1, kSteps, pattern->length);
                const int s = (int) (((index % length) + length) % length);
                const int prev = (s + length - 1) % length;
                const auto& step = pattern->steps[(size_t) s];
                currentStep = s;

                const bool slideIn = stepGateOpen && pattern->steps[(size_t) prev].slide;
                if (step.on)
                {
                    noteOn (settings.root + step.note + transpose, 1.0f, step.accent, slideIn);
                    stepGateOpen = true;
                    stepEndBeat = (double) index * stepBeats + settings.gate * stepBeats;
                }
                else if (stepGateOpen)
                {
                    noteOff();
                    stepGateOpen = false;
                }
            }

            // End the note at its gate length, unless it slides into an active next step.
            if (stepGateOpen && beat >= stepEndBeat)
            {
                const int length = juce::jlimit (1, kSteps, pattern->length);
                const auto& step = pattern->steps[(size_t) currentStep];
                const auto& next = pattern->steps[(size_t) ((currentStep + 1) % length)];
                if (! (step.slide && next.on))
                {
                    noteOff();
                    stepGateOpen = false;
                }
                else
                {
                    stepEndBeat = std::numeric_limits<double>::max();   // held into the next step
                }
            }
        }

        const float s = renderSample (beatsPerSample);
        left[i] += s;
        if (right != nullptr)
            right[i] += s;
    }
    for (; event != midi.end(); ++event)
        handleMidi ((*event).getMessage(), sequencing);

    if (! clock.playing)
        freeClock = beat;
}
}
