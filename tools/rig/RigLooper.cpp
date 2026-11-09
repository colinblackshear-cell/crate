#include "RigLooper.h"

namespace
{
constexpr int kFade = 96;   // samples of fade at each end of a new loop, so it doesn't click
}

juce::String RigLooper::sourceName (int s)
{
    switch (s)
    {
        case input:   return "Mic / input";
        case vocoder: return "Vocoder";
        case bass:    return "Bass";
        case synth:   return "Synth";
        case mpc:     return "MPC";
        case sampler: return "Sampler";
        case mix:     return "Rig mix";
        case line:    return "Line in";
        default:      return {};
    }
}

const std::array<int, 6>& RigLooper::barChoices()
{
    static const std::array<int, 6> choices { 0, 1, 2, 4, 8, 16 };
    return choices;
}

RigLooper::RigLooper() = default;

void RigLooper::prepare (double rate, int maxBlock)
{
    sampleRate = rate > 0 ? rate : 44100.0;
    for (size_t i = 0; i < layers.size(); ++i)
    {
        layerOut[i].setSize (2, juce::jmax (1, maxBlock));
        fxTail[i] = 0;
        if (effects[i] != nullptr)
        {
            effects[i]->setRateAndBufferSizeDetails (sampleRate, maxBlock);
            effects[i]->prepareToPlay (sampleRate, maxBlock);
        }
    }
    const int capacity = (int) (sampleRate * kStartSeconds);
    for (auto& layer : layers)
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        if (layer.audio.getNumSamples() < capacity && (State) layer.state.load() == State::empty)
            layer.audio.setSize (2, capacity);
    }
    undoAudio.setSize (2, capacity);
    undoLayer = -1;
}

void RigLooper::setLayerAudio (int index, juce::AudioBuffer<float> audio, double beats)
{
    auto& layer = layers[(size_t) index];
    if (audio.getNumSamples() == 0 || beats <= 0)
        return;
    if (audio.getNumChannels() == 1)
    {
        audio.setSize (2, audio.getNumSamples(), true);
        audio.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
    }
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        std::swap (layer.audio, audio);
        layer.length = layer.audio.getNumSamples();
        layer.startBeat = 0;
        layer.stopBeat = -1;
        layer.lengthBeats = beats;
        layer.state = (int) State::playing;
        if (undoLayer == index)
            undoLayer = -1;
    }
    ++layer.version;
    // The old buffer is freed here, outside the lock.
}

juce::AudioBuffer<float> RigLooper::copyLayerAudio (int index) const
{
    auto& layer = const_cast<Layer&> (layers[(size_t) index]);
    const juce::SpinLock::ScopedLockType sl (layer.lock);
    const auto state = (State) layer.state.load();
    if (layer.length <= 0 || (state != State::playing && state != State::overdubbing))
        return {};
    juce::AudioBuffer<float> copy (2, layer.length);
    for (int ch = 0; ch < 2; ++ch)
        copy.copyFrom (ch, 0, layer.audio, ch, 0, layer.length);
    return copy;
}

std::vector<float> RigLooper::readPeaks (int index, int count) const
{
    std::vector<float> peaks ((size_t) juce::jmax (1, count), 0.0f);
    const auto& layer = layers[(size_t) index];
    const auto state = (State) layer.state.load();
    const int n = juce::jmin (layer.audio.getNumSamples(), state == State::recording ? layer.writeIndex : layer.length);
    if (n <= 0 || state == State::empty || state == State::armed)
        return peaks;
    const float* l = layer.audio.getReadPointer (0);
    const float* r = layer.audio.getReadPointer (1);
    const int step = juce::jmax (1, n / (count * 32));
    for (size_t p = 0; p < peaks.size(); ++p)
    {
        const int from = (int) ((double) p / peaks.size() * n);
        const int to = juce::jmin (n, (int) ((double) (p + 1) / peaks.size() * n));
        float m = 0;
        for (int i = from; i < to; i += step)
            m = juce::jmax (m, std::abs (l[i]), std::abs (r[i]));
        peaks[p] = juce::jmin (1.0f, m);
    }
    return peaks;
}

void RigLooper::reset (int index)
{
    auto& layer = layers[(size_t) index];
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        layer.presses = 0;
        layer.clears = 0;
        layer.undos = 0;
        layer.state = (int) State::empty;
        layer.length = 0;
        layer.lengthBeats = 0;
        layer.position = 0;
        if (undoLayer == index)
            undoLayer = -1;
    }
    ++layer.version;
}

void RigLooper::makeRoom (int index)
{
    auto& layer = layers[(size_t) index];
    const int capacity = (int) (sampleRate * kStartSeconds);
    if (layer.audio.getNumSamples() >= capacity || (State) layer.state.load() != State::empty)
        return;
    juce::AudioBuffer<float> room (2, capacity);
    room.clear();
    const juce::SpinLock::ScopedLockType sl (layer.lock);
    if ((State) layer.state.load() == State::empty)
        std::swap (layer.audio, room);
}

bool RigLooper::openTrim (int index)
{
    auto& trim = trims[(size_t) index];
    const auto& layer = layers[(size_t) index];
    if (trim.wholeBeats > 0 && trimVersions[(size_t) index] == layer.version.load() && trim.whole.getNumSamples() > 0)
        return true;
    auto audio = copyLayerAudio (index);
    const double beats = layer.lengthBeats.load();
    if (audio.getNumSamples() == 0 || beats <= 0)
    {
        trim = {};
        return false;
    }
    trim.whole = std::move (audio);
    trim.wholeBeats = beats;
    trim.start = 0;
    trim.end = beats;
    trimVersions[(size_t) index] = layer.version.load();
    return true;
}

juce::AudioBuffer<float> RigLooper::cropBeats (const juce::AudioBuffer<float>& audio, double beats, double from, double to)
{
    const int total = audio.getNumSamples();
    if (total == 0 || beats <= 0)
        return {};
    const auto toSample = [&] (double b) { return juce::jlimit (0, total, (int) std::round (b / beats * total)); };
    const int a = toSample (juce::jmin (from, to)), b = toSample (juce::jmax (from, to));
    const int n = juce::jmax (1, b - a);
    juce::AudioBuffer<float> out (2, n);
    for (int ch = 0; ch < 2; ++ch)
        out.copyFrom (ch, 0, audio, juce::jmin (ch, audio.getNumChannels() - 1), juce::jmin (a, total - n), n);
    const int fade = juce::jmin (kFade, n / 4);
    if (fade > 0)
        for (int ch = 0; ch < 2; ++ch)
        {
            if (a > 0)
                out.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
            if (b < total)
                out.applyGainRamp (ch, n - fade, fade, 1.0f, 0.0f);
        }
    return out;
}

void RigLooper::setTrim (int index, double startBeat, double endBeat, double playBeats)
{
    auto& trim = trims[(size_t) index];
    if (trim.wholeBeats <= 0)
        return;
    startBeat = juce::jlimit (0.0, trim.wholeBeats, startBeat);
    endBeat = juce::jlimit (0.0, trim.wholeBeats, endBeat);
    if (endBeat - startBeat < 1.0e-3 || playBeats <= 0)
        return;
    auto part = cropBeats (trim.whole, trim.wholeBeats, startBeat, endBeat);
    trim.start = startBeat;
    trim.end = endBeat;
    setLayerAudio (index, std::move (part), playBeats);
    trimVersions[(size_t) index] = layers[(size_t) index].version.load();
}

void RigLooper::maintain()
{
    const int maxCapacity = (int) (sampleRate * kMaxRecordSeconds);
    const int ahead = (int) (sampleRate * kGrowAheadSeconds);
    int longest = 0;
    for (auto& layer : layers)
    {
        const auto state = (State) layer.state.load();
        const int capacity = layer.audio.getNumSamples();
        if (state == State::playing || state == State::overdubbing)
            longest = juce::jmax (longest, capacity);
        if ((state != State::recording && state != State::armed) || capacity >= maxCapacity
            || capacity - layer.recorded.load() > ahead)
            continue;

        // Copy what's recorded so far into a bigger buffer, outside the lock (the audio thread
        // only writes past it), then the last bit and the swap under the lock, which is quick.
        const int bigger = juce::jmin (maxCapacity, juce::jmax (capacity * 2, capacity + 4 * ahead));
        juce::AudioBuffer<float> room (2, bigger);
        const int done = juce::jmin (capacity, layer.recorded.load());
        for (int ch = 0; ch < 2; ++ch)
        {
            room.copyFrom (ch, 0, layer.audio, ch, 0, done);
            room.clear (ch, done, bigger - done);
        }
        {
            const juce::SpinLock::ScopedLockType sl (layer.lock);
            const int now = juce::jlimit (0, capacity, layer.writeIndex);
            if (now > done)
                for (int ch = 0; ch < 2; ++ch)
                    room.copyFrom (ch, done, layer.audio, ch, done, now - done);
            std::swap (layer.audio, room);
        }
        // The old buffer goes here, outside the lock.
    }

    // Undo needs room for the longest loop.
    if (longest > undoAudio.getNumSamples())
    {
        juce::AudioBuffer<float> room (2, longest);
        const juce::SpinLock::ScopedLockType sl (undoLock);
        std::swap (undoAudio, room);
        undoLayer = -1;
    }
}

void RigLooper::finishRecording (Layer& layer)
{
    layer.length = juce::jmax (1, layer.writeIndex);
    layer.lengthBeats = layer.stopBeat - layer.startBeat;
    const int fade = juce::jmin (kFade, layer.length / 4);
    for (int ch = 0; ch < 2; ++ch)
    {
        layer.audio.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
        layer.audio.applyGainRamp (ch, layer.length - fade, fade, 1.0f, 0.0f);
    }
    layer.state = (int) State::playing;
    ++layer.version;
}

void RigLooper::handleRequests (Layer& layer, int index, const Clock& clock, bool& wantsPlay)
{
    const int bpb = juce::jmax (1, clock.beatsPerBar);

    if (layer.clears.exchange (0) > 0)
    {
        layer.state = (int) State::empty;
        layer.length = 0;
        layer.lengthBeats = 0;
        layer.position = 0;
        if (undoLayer == index)
            undoLayer = -1;
        ++layer.version;
    }

    if (layer.undos.exchange (0) > 0 && undoLayer == index)
    {
        const juce::SpinLock::ScopedTryLockType ul (undoLock);
        const auto state = (State) layer.state.load();
        if (ul.isLocked() && (state == State::playing || state == State::overdubbing) && undoLength == layer.length)
        {
            for (int ch = 0; ch < 2; ++ch)
                layer.audio.copyFrom (ch, 0, undoAudio, ch, 0, undoLength);
            layer.state = (int) State::playing;
            ++layer.version;
        }
        undoLayer = -1;
    }

    if (const double restart = layer.restartBeat.exchange (-1.0); restart >= 0)
    {
        const auto state = (State) layer.state.load();
        if (state == State::playing || state == State::overdubbing)
            layer.startBeat = restart;
    }

    for (int p = layer.presses.exchange (0); p > 0; --p)
    {
        switch ((State) layer.state.load())
        {
            case State::empty:
            {
                if (layer.audio.getNumSamples() < 1024)
                    break;
                // The next bar line, or now if the clock is stopped (it starts with the layer).
                layer.startBeat = clock.playing ? std::ceil (clock.beat / bpb - 1.0e-9) * bpb : clock.beat;
                const int bars = layer.bars.load();
                layer.stopBeat = bars > 0 ? layer.startBeat + bars * bpb : -1.0;
                layer.writeIndex = 0;
                layer.recorded = 0;
                layer.lengthBeats = 0;
                layer.position = 0;
                layer.state = (int) State::armed;
                if (! clock.playing)
                    wantsPlay = true;
                break;
            }
            case State::armed:
                layer.state = (int) State::empty;   // changed your mind
                break;
            case State::recording:
            {
                // Stop at the end of the bar you're in (at least one bar).
                const double done = juce::jmax (0.0, clock.beat - layer.startBeat);
                const double barsDone = juce::jmax (1.0, std::ceil (done / bpb - 1.0e-9));
                const double stop = layer.startBeat + barsDone * bpb;
                layer.stopBeat = layer.stopBeat < 0 ? stop : juce::jmin (layer.stopBeat, stop);
                break;
            }
            case State::playing:
                if (const juce::SpinLock::ScopedTryLockType ul (undoLock); ul.isLocked() && layer.length <= undoAudio.getNumSamples())
                {
                    for (int ch = 0; ch < 2; ++ch)
                        undoAudio.copyFrom (ch, 0, layer.audio, ch, 0, layer.length);
                    undoLayer = index;
                    undoLength = layer.length;
                }
                layer.state = (int) State::overdubbing;
                break;
            case State::overdubbing:
                layer.state = (int) State::playing;
                break;
        }
    }
}

bool RigLooper::process (const Clock& clock, const std::array<const juce::AudioBuffer<float>*, numSources>& sources,
                         juce::AudioBuffer<float>& out)
{
    out.clear();
    const int n = out.getNumSamples();
    bool wantsPlay = false;
    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    const float gainStep = 1.0f / (0.01f * (float) sampleRate);   // 10 ms to any level

    for (int index = 0; index < kLayers; ++index)
    {
        auto& layer = layers[(size_t) index];
        // With effects, the layer plays into its own buffer first, then through them.
        auto* fx = effects[(size_t) index];
        auto& own = layerOut[(size_t) index];
        const bool viaFx = fx != nullptr && own.getNumSamples() >= n;
        if (viaFx)
            own.clear (0, n);
        auto* outL = viaFx ? own.getWritePointer (0) : out.getWritePointer (0);
        auto* outR = viaFx ? own.getWritePointer (1) : out.getWritePointer (1);
        const auto finishLayer = [&]
        {
            if (! viaFx)
                return;
            if (fx->isActive())
                fxTail[(size_t) index] = (int) (sampleRate * fx->getTailLengthSeconds());
            if (fxTail[(size_t) index] > 0)
            {
                juce::AudioBuffer<float> block (own.getArrayOfWritePointers(), 2, n);
                juce::MidiBuffer none;
                fx->processBlock (block, none);
                fxTail[(size_t) index] = juce::jmax (0, fxTail[(size_t) index] - n);
            }
            for (int ch = 0; ch < 2; ++ch)
                out.addFrom (ch, 0, own, ch, 0, n);
        };

        const juce::SpinLock::ScopedTryLockType sl (layer.lock);
        if (! sl.isLocked())
        {
            finishLayer();   // the window is swapping in a new sound; back next block (the effects ring on)
            continue;
        }

        handleRequests (layer, index, clock, wantsPlay);

        const auto* src = sources[(size_t) juce::jlimit (0, numSources - 1, layer.source.load())];
        const float* srcL = src != nullptr ? src->getReadPointer (0) : nullptr;
        const float* srcR = src != nullptr ? src->getReadPointer (juce::jmin (1, src->getNumChannels() - 1)) : nullptr;
        auto* audioL = layer.audio.getWritePointer (0);
        auto* audioR = layer.audio.getWritePointer (1);
        const int capacity = layer.audio.getNumSamples();
        const int bpb = juce::jmax (1, clock.beatsPerBar);
        const float target = layer.mute.load() ? 0.0f : layer.level.load();
        float peak = 0;
        bool changed = false;

        for (int i = 0; i < n; ++i)
        {
            const double beat = clock.beat + (clock.playing ? i * beatsPerSample : 0.0);
            auto state = (State) layer.state.load (std::memory_order_relaxed);

            if (state == State::armed)
            {
                if (! clock.playing || beat < layer.startBeat - 1.0e-9)
                    continue;
                layer.state = (int) (state = State::recording);
                layer.writeIndex = 0;
            }

            if (state == State::recording)
            {
                if (! clock.playing)
                {
                    layer.state = (int) State::empty;   // the clock stopped before the loop was done
                    break;
                }
                if (layer.stopBeat >= 0 && beat >= layer.stopBeat - 1.0e-9)
                {
                    finishRecording (layer);
                    state = State::playing;
                }
                else if (layer.writeIndex < capacity)
                {
                    audioL[layer.writeIndex] = srcL != nullptr ? srcL[i] : 0.0f;
                    audioR[layer.writeIndex] = srcR != nullptr ? srcR[i] : 0.0f;
                    ++layer.writeIndex;
                    changed = true;
                    continue;
                }
                else
                {
                    // Full: keep the whole bars that fit.
                    const double spb = sampleRate * 60.0 / clock.bpm;
                    const double bars = std::floor (layer.writeIndex / spb / bpb);
                    if (bars < 1)
                    {
                        layer.state = (int) State::empty;
                        break;
                    }
                    layer.stopBeat = layer.startBeat + bars * bpb;
                    layer.writeIndex = juce::jmin (capacity, (int) std::round (bars * bpb * spb));
                    finishRecording (layer);
                    state = State::playing;
                }
            }

            if ((state == State::playing || state == State::overdubbing) && clock.playing && layer.length > 1)
            {
                const double lengthBeats = juce::jmax (1.0e-6, layer.lengthBeats.load (std::memory_order_relaxed));
                double p = std::fmod (beat - layer.startBeat, lengthBeats);
                if (p < 0)
                    p += lengthBeats;
                const double pos = p / lengthBeats * layer.length;
                const int i0 = juce::jlimit (0, layer.length - 1, (int) pos);
                const int i1 = (i0 + 1) % layer.length;
                const float frac = (float) (pos - i0);
                const float l = audioL[i0] + (audioL[i1] - audioL[i0]) * frac;
                const float r = audioR[i0] + (audioR[i1] - audioR[i0]) * frac;

                if (layer.gain < target)
                    layer.gain = juce::jmin (target, layer.gain + gainStep);
                else if (layer.gain > target)
                    layer.gain = juce::jmax (target, layer.gain - gainStep);
                const float arrange = arrangeFrom[(size_t) index] + (arrangeTo[(size_t) index] - arrangeFrom[(size_t) index]) * (float) i / (float) n;
                const float g = layer.gain * arrange;
                outL[i] += l * g;
                outR[i] += r * g;
                peak = juce::jmax (peak, std::abs (l * g), std::abs (r * g));

                if (state == State::overdubbing)
                {
                    if (srcL != nullptr)
                        audioL[i0] += srcL[i];
                    if (srcR != nullptr)
                        audioR[i0] += srcR[i];
                    changed = true;
                }
            }
        }

        layer.recorded = layer.writeIndex;

        // What the window shows.
        const auto state = (State) layer.state.load();
        if (state == State::recording)
        {
            const double done = juce::jmax (0.0, clock.beat - layer.startBeat);
            layer.lengthBeats = done;
            layer.position = layer.stopBeat > layer.startBeat ? done / (layer.stopBeat - layer.startBeat) : 0.0;
        }
        else if ((state == State::playing || state == State::overdubbing) && layer.lengthBeats.load() > 0)
        {
            double p = std::fmod (clock.beat - layer.startBeat, layer.lengthBeats.load());
            layer.position = (p < 0 ? p + layer.lengthBeats.load() : p) / layer.lengthBeats.load();
        }
        if (peak > layer.peak.load())
            layer.peak = peak;
        if (changed)
            ++layer.version;
        finishLayer();
    }
    return wantsPlay;
}
