#include "PocketLooper.h"

using namespace pocket;

namespace
{
constexpr float kLayerLevel = 0.8f;
constexpr double kEps = 1.0e-9;
}

LoopsEngine::LoopsEngine (BeatsEngine& b) : beats (b)
{
    for (auto& c : cmds)
        c = {};
    prepare (sampleRate);
}

SharedTake LoopsEngine::makeTake (int samples, double rate)
{
    auto t = std::make_shared<LoopTake>();
    t->data.assign ((size_t) juce::jmax (1, samples), 0.0f);
    t->sampleRate = rate;
    return t;
}

//==============================================================================
// Message thread
void LoopsEngine::push (const Cmd& c)
{
    layers[(size_t) c.layer].pending.fetch_add (1);
    const juce::SpinLock::ScopedLockType lock (pushLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        cmds[(size_t) s1] = c;
    else
        layers[(size_t) c.layer].pending.fetch_sub (1);   // queue full: dropped
    fifo.finishedWrite (n1 + n2);
}

bool LoopsEngine::arm (int layer, LoopSource source, int bars)
{
    if (layer < 0 || layer >= kLoopLayers || state (layer) != LoopState::empty
        || layers[(size_t) layer].pending.load() > 0)
        return false;

    const double tempo = juce::jmax (40.0, beats.getBpm());
    const int lat = latency.load();
    const int room = bars > 0 ? (int) (bars * 4.0 * 60.0 / tempo * sampleRate * 1.6) + lat + 8192
                              : (int) (600.0 * sampleRate) + lat;   // FREE: up to 10 minutes
    auto take = makeTake (room, sampleRate);
    take->source = (int) source;
    if (takes[(size_t) layer] != nullptr)
        retired.push_back ({ takes[(size_t) layer], juce::Time::getMillisecondCounter() });
    takes[(size_t) layer] = take;

    Cmd c;
    c.kind = Cmd::arm;
    c.layer = layer;
    c.a = (int) source;
    c.b = bars;
    c.c = countIn ? 1 : 0;
    c.take = take.get();
    push (c);
    return true;
}

void LoopsEngine::stop (int layer)
{
    Cmd c;
    c.kind = Cmd::stop;
    c.layer = layer;
    push (c);
}

bool LoopsEngine::dub (int layer)
{
    const auto st = state (layer);
    if (st != LoopState::playing)
        return false;
    Cmd c;
    c.kind = Cmd::dub;
    c.layer = layer;
    push (c);
    return true;
}

void LoopsEngine::setMute (int layer, bool m) { layers[(size_t) layer].mute = m; }

void LoopsEngine::clear (int layer)
{
    Cmd c;
    c.kind = Cmd::clear;
    c.layer = layer;
    push (c);
}

void LoopsEngine::setLayer (int layer, SharedTake take, bool mute)
{
    if (takes[(size_t) layer] != nullptr)
        retired.push_back ({ takes[(size_t) layer], juce::Time::getMillisecondCounter() });
    takes[(size_t) layer] = take;
    layers[(size_t) layer].mute = mute;
    Cmd c;
    c.kind = Cmd::set;
    c.layer = layer;
    c.take = take.get();
    push (c);
}

SharedTake LoopsEngine::copyOf (int layer) const
{
    auto src = takes[(size_t) layer];
    if (src == nullptr)
        return nullptr;
    const int n = juce::jmax (1, src->length.load());
    auto t = makeTake (n, src->sampleRate);
    std::copy (src->data.begin(), src->data.begin() + juce::jmin ((int) src->data.size(), n), t->data.begin());
    t->length = src->length.load();
    t->lengthBeats = src->lengthBeats.load();
    t->source = src->source;
    return t;
}

void LoopsEngine::setTransport (bool on)
{
    transportOn = on;
    if (! on)
        beats.holdClock (false);
}

double LoopsEngine::masterBeats() const
{
    for (int i = 0; i < kLoopLayers; ++i)
    {
        const auto st = state (i);
        if ((st == LoopState::playing || st == LoopState::overdubWait || st == LoopState::overdubbing) && takes[(size_t) i] != nullptr)
            return takes[(size_t) i]->lengthBeats.load();
    }
    return 0.0;
}

std::vector<float> LoopsEngine::peaks (int layer, int count) const
{
    std::vector<float> out ((size_t) count, 0.0f);
    auto take = takes[(size_t) layer];
    if (take == nullptr || count <= 0)
        return out;
    const auto st = state (layer);
    int n = st == LoopState::recording ? layers[(size_t) layer].uiRecorded.load() : take->length.load();
    n = juce::jmin (n, (int) take->data.size());
    if (n <= 0)
        return out;
    for (int k = 0; k < count; ++k)
    {
        const int a = (int) ((juce::int64) n * k / count), b = juce::jmax (a + 1, (int) ((juce::int64) n * (k + 1) / count));
        float p = 0;
        for (int i = a; i < b && i < n; ++i)
            p = juce::jmax (p, std::abs (take->data[(size_t) i]));
        out[(size_t) k] = p;
    }
    return out;
}

void LoopsEngine::maintain()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (int i = 0; i < kLoopLayers; ++i)
        if (takes[(size_t) i] != nullptr && state (i) == LoopState::empty && layers[(size_t) i].pending.load() == 0)
        {
            retired.push_back ({ takes[(size_t) i], now });
            takes[(size_t) i] = nullptr;
        }
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (auto& r) { return now - r.second > 4000; }), retired.end());
}

//==============================================================================
// Audio thread
void LoopsEngine::prepare (double rate)
{
    sampleRate = rate;
    for (auto& v : layerOut)
        v.assign ((size_t) kChunk, 0.0f);
    for (auto* v : { &srcMic, &srcDrums, &srcVoc, &srcMix, &vocL, &vocR })
        v->assign ((size_t) kChunk, 0.0f);
    beats.prepare (rate);
    if (vocoder != nullptr)
        vocoder->prepare (rate);
}

void LoopsEngine::setState (Layer& l, LoopState s)
{
    l.st = s;
    l.uiState = (int) s;
}

void LoopsEngine::handle (const Cmd& c)
{
    auto& l = layers[(size_t) c.layer];
    switch (c.kind)
    {
        case Cmd::arm:
            if (l.st != LoopState::empty)
                break;
            l.take = c.take;
            l.take->length = 0;
            l.source = (LoopSource) juce::jlimit (0, (int) LoopSource::count - 1, c.a);
            l.bars = c.b;
            l.uiBars = c.b;
            l.withCountIn = c.c != 0;
            l.fresh = ! wasRunning;
            l.armBeat = -1;
            l.endBeat = -1;
            l.writeIdx = 0;
            l.uiRecorded = 0;
            setState (l, LoopState::waiting);
            transportOn = true;   // the loops hold the clock once they're in use
            break;
        case Cmd::stop:
            if (l.st == LoopState::waiting)
                release (l);
            else if (l.st == LoopState::recording)
            {
                const double e = juce::jmax (l.startBeat + 4.0, std::ceil (nextBeat / 4.0 - kEps) * 4.0);
                l.endBeat = l.endBeat < 0 ? e : juce::jmin (l.endBeat, e);
            }
            else if (l.st == LoopState::overdubWait || l.st == LoopState::overdubbing)
                setState (l, LoopState::playing);
            break;
        case Cmd::dub:
            if (l.st == LoopState::playing && l.take != nullptr)
            {
                l.dubBeat = -1;
                setState (l, LoopState::overdubWait);
                transportOn = true;
            }
            break;
        case Cmd::clear:
            release (l);
            break;
        case Cmd::set:
            l.take = c.take;
            if (c.take == nullptr || c.take->length.load() <= 0)
                release (l);
            else
            {
                l.lenBeats = c.take->lengthBeats.load();
                l.startBeat = 0;
                setState (l, LoopState::playing);
                ++l.version;
            }
            break;
    }
    l.pending.fetch_sub (1);
}

void LoopsEngine::release (Layer& l)
{
    l.take = nullptr;
    l.armBeat = -1;
    l.uiCounting = false;
    setState (l, LoopState::empty);
    ++l.version;
}

void LoopsEngine::cancelRecording (Layer& l)
{
    if (l.st == LoopState::recording || l.st == LoopState::waiting)
        release (l);
    else if (l.st == LoopState::overdubbing || l.st == LoopState::overdubWait)
        setState (l, LoopState::playing);
}

void LoopsEngine::finishRecording (Layer& l, double end)
{
    l.take->length = juce::jmax (1, l.writeIdx);
    l.lenBeats = end - l.startBeat;
    l.take->lengthBeats = l.lenBeats;
    l.uiRecorded = l.writeIdx;
    setState (l, LoopState::playing);
    ++l.version;
}

void LoopsEngine::render (float* left, float* right, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;

    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i)
        handle (cmds[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i)
        handle (cmds[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);

    for (int off = 0; off < numSamples; off += kChunk)
    {
        chunkOffset = off;
        chunk (left + off, right != nullptr ? right + off : nullptr, juce::jmin (kChunk, numSamples - off));
    }
}

void LoopsEngine::chunk (float* left, float* right, int n)
{
    bool anyActive = false;
    for (auto& l : layers)
        anyActive = anyActive || l.st != LoopState::empty;
    if (! anyActive && wasRunning && transportOn.load() && ! beats.isPlaying())
        transportOn = false;   // everything was cleared: let go of the clock
    beats.holdClock (transportOn.load());

    beats.render (left, right, n);

    const bool running = beats.clockRunning();
    const double blockBeat = beats.clockBeat();
    const double bps = beats.getBpm() / (60.0 * sampleRate);

    if (running != wasRunning)
    {
        for (auto& l : layers)
        {
            if (running)
            {
                if (l.st == LoopState::playing || l.st == LoopState::overdubWait || l.st == LoopState::overdubbing)
                    l.startBeat = 0;   // loops start again from their top with the clock
                if (l.st == LoopState::waiting)
                {
                    l.armBeat = -1;
                    l.fresh = true;
                }
                if (l.st == LoopState::overdubWait)
                    l.dubBeat = -1;
            }
            else
            {
                if (l.st == LoopState::recording || l.st == LoopState::overdubbing)
                    cancelRecording (l);
                if (l.st == LoopState::waiting)
                {
                    l.armBeat = -1;
                    l.fresh = true;
                }
            }
        }
        wasRunning = running;
    }

    // what each source hears this block
    {
        float p = 0;
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            int used = 0;
            for (int c = 0; c < inChannels && c < 2; ++c)
                if (inPtr != nullptr && inPtr[c] != nullptr)
                {
                    s += inPtr[c][chunkOffset + i];
                    ++used;
                }
            srcMic[(size_t) i] = used > 1 ? s / (float) used : s;
            p = juce::jmax (p, std::abs (srcMic[(size_t) i]));
            srcDrums[(size_t) i] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
            srcVoc[(size_t) i] = 0.0f;
        }
        if (p > inputPeak.load())
            inputPeak = p;
    }
    if (vocoder != nullptr)
    {
        std::fill (vocL.begin(), vocL.begin() + n, 0.0f);
        std::fill (vocR.begin(), vocR.begin() + n, 0.0f);
        const float* shifted[2] = { inChannels > 0 && inPtr != nullptr ? inPtr[0] + chunkOffset : nullptr,
                                    inChannels > 1 && inPtr != nullptr ? inPtr[1] + chunkOffset : nullptr };
        vocoder->setInput (shifted, juce::jmin (2, inChannels));
        vocoder->render (vocL.data(), vocR.data(), n);
        vocoderHeard = vocoderMonitor == nullptr || vocoderMonitor->load();
        for (int i = 0; i < n; ++i)
        {
            if (vocoderHeard)
            {
                left[i] += vocL[(size_t) i];
                if (right != nullptr)
                    right[i] += vocR[(size_t) i];
            }
            srcVoc[(size_t) i] = 0.5f * (vocL[(size_t) i] + vocR[(size_t) i]);
        }
    }

    // play the layers
    for (int l = 0; l < kLoopLayers; ++l)
        playLayer (layers[(size_t) l], l, blockBeat, bps, n);

    for (int i = 0; i < n; ++i)
    {
        float m = srcDrums[(size_t) i] + (vocoderHeard ? srcVoc[(size_t) i] : 0.0f);
        for (auto& o : layerOut)
            m += o[(size_t) i];
        srcMix[(size_t) i] = m;
    }

    // record
    for (int l = 0; l < kLoopLayers; ++l)
        recordLayer (layers[(size_t) l], l, blockBeat, bps, n);

    for (auto& l : layers)
        l.uiCounting = l.st == LoopState::waiting && l.fresh && l.armBeat > blockBeat && l.withCountIn;

    for (auto& l : layers)
        l.uiStart = l.startBeat;

    for (int l = 0; l < kLoopLayers; ++l)
        for (int i = 0; i < n; ++i)
        {
            const float s = layerOut[(size_t) l][(size_t) i];
            left[i] += s;
            if (right != nullptr)
                right[i] += s;
        }

    nextBeat = running ? blockBeat + n * bps : 0.0;
}

void LoopsEngine::playLayer (Layer& l, int index, double blockBeat, double bps, int n)
{
    auto& out = layerOut[(size_t) index];
    const bool playing = l.st == LoopState::playing || l.st == LoopState::overdubWait || l.st == LoopState::overdubbing;
    if (! playing || l.take == nullptr || ! wasRunning)
    {
        std::fill (out.begin(), out.begin() + n, 0.0f);
        l.gain = 0.0f;
        return;
    }

    const int len = l.take->length.load();
    const double lenBeats = l.lenBeats;
    if (len < 2 || lenBeats <= 0.0)
    {
        std::fill (out.begin(), out.begin() + n, 0.0f);
        return;
    }

    const float* d = l.take->data.data();
    const double perBeat = (double) len / lenBeats;
    double phase = std::fmod (blockBeat - l.startBeat, lenBeats);
    if (phase < 0)
        phase += lenBeats;
    double pos = phase * perBeat;
    const double step = bps * perBeat;
    const float target = l.mute.load() ? 0.0f : kLayerLevel;
    const float coef = 1.0f - std::exp (-1.0f / (0.005f * (float) sampleRate));
    float g = l.gain;

    for (int i = 0; i < n; ++i)
    {
        const int i0 = juce::jmin ((int) pos, len - 1);
        const int i1 = i0 + 1 >= len ? 0 : i0 + 1;
        const float frac = (float) (pos - (double) i0);
        g += (target - g) * coef;
        out[(size_t) i] = (d[i0] + (d[i1] - d[i0]) * frac) * g;
        pos += step;
        if (pos >= (double) len)
            pos -= (double) len;
    }
    l.gain = g;
    l.uiPos = (float) (pos / (double) len);
}

void LoopsEngine::recordLayer (Layer& l, int index, double blockBeat, double bps, int n)
{
    if (l.st == LoopState::empty || l.st == LoopState::playing || ! wasRunning)
        return;

    if (l.st == LoopState::waiting && l.armBeat < 0)
        l.armBeat = l.fresh ? (l.withCountIn ? 4.0 : 0.0) : std::ceil (blockBeat / 4.0 - kEps) * 4.0;
    if (l.st == LoopState::overdubWait && l.dubBeat < 0 && l.lenBeats > 0)
        l.dubBeat = l.startBeat + std::ceil ((blockBeat - l.startBeat) / l.lenBeats - kEps) * l.lenBeats;

    const bool fromDevice = l.source == LoopSource::mic || l.source == LoopSource::vocoder;
    const double latBeats = fromDevice ? (double) latency.load() * bps : 0.0;
    const float* src = srcMic.data();
    switch (l.source)
    {
        case LoopSource::drums: src = srcDrums.data(); break;
        case LoopSource::vocoder: src = srcVoc.data(); break;
        case LoopSource::mix: src = srcMix.data(); break;
        default: break;
    }
    const bool takeOutSelf = l.source == LoopSource::mix;
    const auto& own = layerOut[(size_t) index];
    const int capacity = l.take != nullptr ? (int) l.take->data.size() : 0;
    bool wrote = false;

    for (int i = 0; i < n; ++i)
    {
        const double b = blockBeat + (double) i * bps;
        const double sb = b - latBeats;

        if (l.st == LoopState::waiting)
        {
            if (b < l.armBeat)
                continue;
            l.startBeat = l.armBeat;
            l.endBeat = l.bars > 0 ? l.armBeat + l.bars * 4.0 : -1.0;
            l.writeIdx = 0;
            setState (l, LoopState::recording);
        }

        if (l.st == LoopState::recording)
        {
            if (sb < l.startBeat)
                continue;
            if (l.endBeat >= 0 && sb >= l.endBeat)
            {
                finishRecording (l, l.endBeat);
                return;
            }
            if (l.writeIdx >= capacity)
            {
                // ran out of room: keep the whole bars so far
                const double total = sb - l.startBeat;
                const double whole = juce::jmax (4.0, std::floor (total / 4.0) * 4.0);
                l.writeIdx = juce::jmax (1, (int) ((double) l.writeIdx * whole / juce::jmax (total, 1.0e-6)));
                finishRecording (l, l.startBeat + whole);
                return;
            }
            l.take->data[(size_t) l.writeIdx++] = src[i];
            continue;
        }

        if (l.st == LoopState::overdubWait)
        {
            if (l.dubBeat < 0 || b < l.dubBeat)
                continue;
            setState (l, LoopState::overdubbing);
        }

        if (l.st == LoopState::overdubbing)
        {
            const double dubEnd = l.dubBeat + l.lenBeats;
            if (sb >= dubEnd)
            {
                setState (l, LoopState::playing);
                ++l.version;
                return;
            }
            if (sb < l.dubBeat)
                continue;
            const int len = l.take->length.load();
            double phase = std::fmod (sb - l.startBeat, l.lenBeats);
            if (phase < 0)
                phase += l.lenBeats;
            const int idx = juce::jlimit (0, len - 1, (int) (phase / l.lenBeats * (double) len));
            l.take->data[(size_t) idx] += takeOutSelf ? src[i] - own[(size_t) i] : src[i];
            wrote = true;
        }
    }

    if (l.st == LoopState::recording)
        l.uiRecorded = l.writeIdx;
    if (wrote)
        ++l.version;
}
