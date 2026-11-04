#include "RigCapture.h"

juce::String RigCapture::sourceName (int s)
{
    switch (s)
    {
        case mic:     return "Mic / input";
        case mpc:     return "MPC";
        case vocoder: return "Vocoder";
        case bass:    return "Bass";
        case synth:   return "Synth";
        case input:   return "Input strip";
        case sampler: return "Sampler";
        case loops:   return "Loops";
        case mix:     return "Rig mix";
        default:      return {};
    }
}

juce::String RigCapture::destName (int d)
{
    return d == toMpcPad ? "an MPC pad" : "the Sampler";
}

const std::array<int, 6>& RigCapture::barChoices()
{
    static const std::array<int, 6> choices { 0, 1, 2, 4, 8, 16 };
    return choices;
}

void RigCapture::prepare (double sampleRate)
{
    jassert (getState() != State::armed && getState() != State::recording);
    rate = sampleRate;
    buffer.setSize (2, (int) (kMaxSeconds * sampleRate));
    state = (int) State::idle;
    written = 0;
}

bool RigCapture::start (int s, int b)
{
    if (getState() != State::idle || buffer.getNumSamples() == 0 || ! juce::isPositiveAndBelow (s, (int) numSources))
        return false;
    source = s;
    bars = juce::jmax (0, b);
    written = 0;
    target = 0;
    stopRequested = false;
    state = (int) State::armed;
    return true;
}

void RigCapture::stop()
{
    if (getState() == State::armed || getState() == State::recording)
        stopRequested = true;
}

juce::AudioBuffer<float> RigCapture::take (double& sampleRate, double& beats)
{
    sampleRate = rate;
    beats = 0;
    if (getState() != State::done)
        return {};
    const int n = written.load();
    juce::AudioBuffer<float> out (2, n);
    for (int ch = 0; ch < 2; ++ch)
        if (n > 0)
            out.copyFrom (ch, 0, buffer, ch, 0, n);
    beats = n / rate * beatsPerSecond.load();
    written = 0;
    state = (int) State::idle;
    return out;
}

void RigCapture::process (const RigRecorder::Clock& clock, const std::array<const juce::AudioBuffer<float>*, numSources>& sources,
                          int numSamples)
{
    const auto current = getState();
    if (current != State::armed && current != State::recording)
        return;

    if (stopRequested.load() || (current == State::recording && startedPlaying && ! clock.playing))
    {
        state = (int) State::done;
        return;
    }

    int from = 0;
    if (current == State::armed)
    {
        if (clock.playing)
        {
            const double beatsPerSample = clock.bpm / (60.0 * clock.sampleRate);
            const int bpb = juce::jmax (1, clock.beatsPerBar);
            const double bar = std::ceil (clock.beat / bpb - 1.0e-9) * bpb;
            const double offset = (bar - clock.beat) / beatsPerSample;
            if (offset >= numSamples)
                return;
            from = juce::jlimit (0, numSamples - 1, (int) std::round (offset));
        }
        beatsPerSecond = clock.bpm / 60.0;
        startedPlaying = clock.playing;
        target = bars > 0 ? (int) std::round (bars * juce::jmax (1, clock.beatsPerBar) * 60.0 / clock.bpm * rate) : 0;
        state = (int) State::recording;
    }

    const auto* in = sources[(size_t) source];
    const int capacity = buffer.getNumSamples();
    const int limit = target > 0 ? juce::jmin (target, capacity) : capacity;
    const int pos = written.load();
    const int count = juce::jmin (numSamples - from, limit - pos);
    if (count > 0)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            if (in != nullptr)
                buffer.copyFrom (ch, pos, *in, juce::jmin (ch, in->getNumChannels() - 1), from, count);
            else
                buffer.clear (ch, pos, count);
        }
        written = pos + count;
    }
    if (written.load() >= limit)
        state = (int) State::done;
}
