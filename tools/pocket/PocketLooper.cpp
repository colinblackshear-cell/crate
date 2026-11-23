#include "PocketLooper.h"

using namespace pocket;

namespace
{
constexpr float kLayerLevel = 0.8f;
constexpr double kEps = 1.0e-9;
}

int pocket::toolForSource (int source)
{
    switch ((LoopSource) source)
    {
        case LoopSource::drums: return (int) LayerTool::drums;
        case LoopSource::vocoder:
        case LoopSource::both: return (int) LayerTool::vocoder;
        case LoopSource::mix: return (int) LayerTool::mix;
        case LoopSource::synth: return (int) LayerTool::synth;
        case LoopSource::chords: return (int) LayerTool::chords;
        case LoopSource::drone: return (int) LayerTool::drone;
        case LoopSource::loops:
        case LoopSource::master:
        case LoopSource::layerTap: return (int) LayerTool::mix;
        default: return (int) LayerTool::mic;
    }
}

LoopsEngine::LoopsEngine (BeatsEngine& b) : beats (b)
{
    for (auto& c : cmds)
        c = {};
    for (int l = 0; l < kLoopLayers; ++l)
    {
        auto& f = fx[(size_t) l];
        for (int k = 0; k < kFxKnobs; ++k)
        {
            f.knob[(size_t) k] = fxDefault (k);
            f.shown[(size_t) k] = fxDefault (k);
            f.touched[(size_t) k] = false;
            f.has[(size_t) k] = false;
            for (auto& v : f.lane[(size_t) k])
                v = fxDefault (k);
        }
    }
    for (auto& b : boards)
        b.attach (&boardClock);
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

bool LoopsEngine::arm (int layer, LoopSource source, int bars, int tool, int tap)
{
    if (layer < 0 || layer >= kLoopLayers || state (layer) != LoopState::empty
        || layers[(size_t) layer].pending.load() > 0)
        return false;

    const double tempo = juce::jmax (40.0, beats.getBpm());
    const int lat = latency.load();
    // FREE: up to 10 minutes. A set length (up to 64 bars) gets room for it with headroom for a tempo
    // change, never more than FREE's (a long take at a slow tempo keeps the whole bars that fit).
    const int freeRoom = (int) (600.0 * sampleRate) + lat;
    const int room = bars > 0 ? (int) juce::jmin ((double) freeRoom, bars * 4.0 * 60.0 / tempo * sampleRate * 1.6 + lat + 8192)
                              : freeRoom;
    auto take = makeTake (room, sampleRate);
    take->source = (int) source;
    take->tool = tool >= 0 ? tool : toolForSource ((int) source);
    setRegion (layer, 0, 0);
    layers[(size_t) layer].keepTop = false;
    resetFx (layer);   // a new recording starts with its effects off
    resetBoard (layer);   // and its board neutral: what the take went through is in its sound already
    if (takes[(size_t) layer] != nullptr)
        retired.push_back ({ takes[(size_t) layer], juce::Time::getMillisecondCounter() });
    takes[(size_t) layer] = take;

    Cmd c;
    c.kind = Cmd::arm;
    c.layer = layer;
    c.a = (int) source;
    c.b = bars;
    c.c = countInBars;
    c.d = juce::jlimit (0, kLoopLayers - 1, tap);
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
    resetFx (layer);
    resetBoard (layer);
    Cmd c;
    c.kind = Cmd::clear;
    c.layer = layer;
    push (c);
}

void LoopsEngine::setLayer (int layer, SharedTake take, bool mute, double startBeat)
{
    setRegion (layer, 0, 0);
    if (takes[(size_t) layer] != nullptr)
        retired.push_back ({ takes[(size_t) layer], juce::Time::getMillisecondCounter() });
    takes[(size_t) layer] = take;
    layers[(size_t) layer].mute = mute;
    if (take == nullptr)
    {
        resetFx (layer);
        resetBoard (layer);
    }
    Cmd c;
    c.kind = Cmd::set;
    c.layer = layer;
    c.beat = startBeat;
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
    t->tool = src->tool;
    t->content = src->content;
    t->part = src->part;
    t->quant = src->quant;
    t->origin = src->origin;
    t->warp = src->warp;
    return t;
}

//==============================================================================
// Effects and moves
void LoopsEngine::setKnob (int layer, int k, float v)
{
    auto& f = fx[(size_t) layer];
    f.knob[(size_t) k] = juce::jlimit (0.0f, 1.0f, v);
    if (f.touched[(size_t) k].load() || ! f.has[(size_t) k].load())
        f.shown[(size_t) k] = f.knob[(size_t) k].load();   // shown at once, even with the audio stopped
}

void LoopsEngine::touchKnob (int layer, int k, bool down)
{
    auto& f = fx[(size_t) layer];
    f.lastBin[(size_t) k] = -1;
    f.before[(size_t) k] = f.has[(size_t) k].load() ? f.shown[(size_t) k].load() : f.knob[(size_t) k].load();
    f.touched[(size_t) k] = down;
}

void LoopsEngine::setRecMoves (int layer, bool on)
{
    auto& f = fx[(size_t) layer];
    f.rec = on;
    for (auto& b : f.lastBin)
        b = -1;
}

std::vector<float> LoopsEngine::moves (int layer, int k) const
{
    auto& f = fx[(size_t) layer];
    if (! f.has[(size_t) k].load())
        return {};
    std::vector<float> m ((size_t) kMoveBins);
    for (int i = 0; i < kMoveBins; ++i)
        m[(size_t) i] = f.lane[(size_t) k][(size_t) i].load();
    return m;
}

void LoopsEngine::clearMoves (int layer)
{
    for (auto& h : fx[(size_t) layer].has)
        h = false;
}

LayerFxState LoopsEngine::fxOf (int layer) const
{
    LayerFxState s;
    for (int k = 0; k < kFxKnobs; ++k)
    {
        s.knob[(size_t) k] = knob (layer, k);
        s.moves[(size_t) k] = moves (layer, k);
    }
    return s;
}

void LoopsEngine::setFx (int layer, const LayerFxState& s)
{
    auto& f = fx[(size_t) layer];
    for (int k = 0; k < kFxKnobs; ++k)
    {
        f.knob[(size_t) k] = juce::jlimit (0.0f, 1.0f, s.knob[(size_t) k]);
        const auto& m = s.moves[(size_t) k];
        if (m.size() == (size_t) kMoveBins)
        {
            for (int i = 0; i < kMoveBins; ++i)
                f.lane[(size_t) k][(size_t) i] = juce::jlimit (0.0f, 1.0f, m[(size_t) i]);
            f.has[(size_t) k] = true;
        }
        else
            f.has[(size_t) k] = false;
        f.shown[(size_t) k] = m.size() == (size_t) kMoveBins ? f.lane[(size_t) k][0].load() : f.knob[(size_t) k].load();
        f.touched[(size_t) k] = false;
        f.lastBin[(size_t) k] = -1;
    }
    f.rec = false;
}

void LoopsEngine::recordMoves()
{
    if (! beats.clockRunning())
        return;
    for (int l = 0; l < kLoopLayers; ++l)
    {
        auto& f = fx[(size_t) l];
        const auto st = state (l);
        if (! f.rec.load() || ! (st == LoopState::playing || st == LoopState::overdubWait || st == LoopState::overdubbing))
            continue;
        const int bin = juce::jlimit (0, kMoveBins - 1, (int) (position (l) * kMoveBins));
        for (int k = 0; k < kFxKnobs; ++k)
        {
            if (! f.touched[(size_t) k].load())
                continue;
            auto& lane = f.lane[(size_t) k];
            const float v = f.knob[(size_t) k].load();
            int& last = f.lastBin[(size_t) k];
            if (! f.has[(size_t) k].load())
            {
                for (auto& x : lane)
                    x = f.before[(size_t) k];   // the rest of the loop keeps what it was
                f.has[(size_t) k] = true;
                last = -1;
            }
            const int dist = last < 0 ? 0 : (bin - last + kMoveBins) % kMoveBins;
            if (dist == 0 || dist > kMoveBins / 2)
                lane[(size_t) bin] = v;
            else
                for (int i = 1; i <= dist; ++i)
                    lane[(size_t) ((last + i) % kMoveBins)] = v;
            last = bin;
        }
    }
}

std::unique_ptr<juce::AudioBuffer<float>> LoopsEngine::printed (int layer) const
{
    auto take = takes[(size_t) layer];
    if (take == nullptr || take->length.load() < 2 || take->lengthBeats.load() <= 0)
        return nullptr;
    auto out = std::make_unique<juce::AudioBuffer<float>> (renderLayer (take->data.data(), take->length.load(), take->lengthBeats.load(), take->sampleRate, fxOf (layer)));
    // and its board, at the loop's own tempo, with the echoes of its end wrapped round into its start
    const double loopBpm = take->lengthBeats.load() * 60.0 * take->sampleRate / (double) take->length.load();
    applyBoardToLoop (*out, board (layerBoardId (layer)).settings(), take->sampleRate, loopBpm);
    return out;
}

float LoopsEngine::fxValue (int layer, int k, double frac) const
{
    auto& f = fx[(size_t) layer];
    if (f.touched[(size_t) k].load() || ! f.has[(size_t) k].load())
        return f.knob[(size_t) k].load();
    frac -= std::floor (frac);
    const double p = frac * kMoveBins;
    const int a = juce::jmin (kMoveBins - 1, (int) p), b = (a + 1) % kMoveBins;
    const float t = (float) (p - a);
    const float va = f.lane[(size_t) k][(size_t) a].load(), vb = f.lane[(size_t) k][(size_t) b].load();
    return va + (vb - va) * t;
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
    // New loops onto the shared bars: a turned copy, from the same place (it sounds the same).
    for (int i = 0; i < kLoopLayers; ++i)
    {
        auto& l = layers[(size_t) i];
        const double shift = l.uiShift.load();
        if (shift == 0.0 || state (i) != LoopState::playing || l.pending.load() > 0 || takes[(size_t) i] == nullptr)
            continue;
        auto t = copyOf (i);
        const int len = t->length.load();
        const double beats = t->lengthBeats.load();
        if (len < 2 || beats <= 0.0)
            continue;
        const int by = (int) (std::llround (shift / beats * len) % len);
        if (by > 0)
            std::rotate (t->data.begin(), t->data.begin() + (len - by), t->data.begin() + len);
        setLayer (i, t, isMuted (i), l.uiStart.load() - shift);
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
    for (auto* v : { &srcMic, &srcDrums, &srcVoc, &srcMix, &srcBoth, &srcDrone, &vocL, &vocR, &clickBuf, &busDelay, &busReverb, &busL, &busR })
        v->assign ((size_t) kChunk, 0.0f);
    for (auto& v : srcInst)
        v.assign ((size_t) kChunk, 0.0f);
    for (auto* v : { &padsL, &padsR, &voiceL, &voiceR, &allL, &allR, &srcLoops, &srcMaster })
        v->assign ((size_t) kChunk, 0.0f);
    for (int l = 0; l < kLoopLayers; ++l)
    {
        layerL[(size_t) l].assign ((size_t) kChunk, 0.0f);
        layerR[(size_t) l].assign ((size_t) kChunk, 0.0f);
    }
    for (auto& b : boards)
        b.prepare (rate);
    bus.prepare (rate);
    for (auto& l : layers)
        l.dsp.reset();
    beats.prepare (rate);
    if (vocoder != nullptr)
        vocoder->prepare (rate);
    if (chordSource != nullptr)
        chordSource->prepare (rate);
    for (auto* i : instruments)
        if (i != nullptr)
            i->prepare (rate);
    if (drone != nullptr)
        drone->prepare (rate);
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
            l.tap = juce::jlimit (0, kLoopLayers - 1, c.d);
            l.bars = c.b;
            l.uiBars = c.b;
            l.withCountIn = juce::jlimit (0, 2, c.c);
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
                l.startBeat = c.beat;
                setShift (l, 0.0);
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
    setShift (l, 0.0);
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
    // Onto the shared bars: its bar 1 where the other loops' is (the longest one playing), so the playheads
    // move together. It sounds the same: maintain() turns the sound round as much as it moves the start.
    // (With none playing, a take still recording that began earlier is the one to line up with.)
    double shift = 0.0, refBeats = 0.0, refAnchor = 0.0, earliestTake = l.startBeat;
    if (! l.keepTop.load())
        for (auto& o : layers)
        {
            if (&o != &l && o.st == LoopState::recording && o.startBeat < earliestTake)
                earliestTake = o.startBeat;
            if (&o == &l || o.take == nullptr || o.lenBeats <= refBeats)
                continue;
            if (o.st != LoopState::playing && o.st != LoopState::overdubWait && o.st != LoopState::overdubbing)
                continue;
            refBeats = o.lenBeats;
            refAnchor = o.startBeat - o.shift;
        }
    if (refBeats <= 0.0 && earliestTake < l.startBeat)
    {
        refBeats = 1.0;
        refAnchor = earliestTake;
    }
    if (refBeats > 0.0 && l.lenBeats > 0.0)
    {
        shift = std::fmod (l.startBeat - refAnchor, l.lenBeats);
        if (shift < 0.0)
            shift += l.lenBeats;
        if (shift < 1.0e-6 || l.lenBeats - shift < 1.0e-6)
            shift = 0.0;
    }
    setShift (l, shift);
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

    // The input belongs to this block only: the caller's buffers may be gone by the next render (the audio
    // callback sets it again every block). A render without setInput then hears silence, not freed memory.
    inPtr = nullptr;
    inChannels = 0;
}

void LoopsEngine::chunk (float* left, float* right, int n)
{
    bool anyActive = false;
    for (auto& l : layers)
        anyActive = anyActive || l.st != LoopState::empty;
    if (! anyActive && wasRunning && transportOn.load() && ! beats.isPlaying())
        transportOn = false;   // everything was cleared: let go of the clock
    beats.holdClock (transportOn.load());

    // A take that starts the clock after a count-in: the click sounds through the count-in, even with
    // the click off. It goes into its own buffer, so no take records it.
    double countEnd = -1.0;
    for (auto& l : layers)
        if (l.st == LoopState::waiting && l.fresh && l.withCountIn > 0)
            countEnd = juce::jmax (countEnd, l.armBeat >= 0 ? l.armBeat : l.withCountIn * 4.0);
    beats.setCountInClick (countEnd);
    std::fill (clickBuf.begin(), clickBuf.begin() + n, 0.0f);
    // The drum machine and the pads: summed as before, unless one of their boards is in use, then each on its own
    // bus through its board.
    const bool stereo = right != nullptr;
    auto& drumBoard = boards[(size_t) boardDrums];
    auto& padBoard = boards[(size_t) boardPads];
    const bool splitPads = stereo && (drumBoard.willRun() || padBoard.willRun());
    if (splitPads)
    {
        std::fill (padsL.begin(), padsL.begin() + n, 0.0f);
        std::fill (padsR.begin(), padsR.begin() + n, 0.0f);
        beats.renderWithClick (left, right, n, clickBuf.data(), padsL.data(), padsR.data());
    }
    else
        beats.renderWithClick (left, right, n, clickBuf.data());

    const bool running = beats.clockRunning();
    const double blockBeat = beats.clockBeat();
    const double bps = beats.getBpm() / (60.0 * sampleRate);
    boardClock.bpm = beats.getBpm();
    boardClock.ppq = blockBeat;
    boardClock.playing = running;
    if (splitPads)
    {
        drumBoard.process (left, right, n);
        padBoard.process (padsL.data(), padsR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            left[i] += padsL[(size_t) i];
            right[i] += padsR[(size_t) i];
        }
    }

    if (running != wasRunning)
    {
        for (auto& l : layers)
        {
            if (running)
            {
                if (l.st == LoopState::playing || l.st == LoopState::overdubWait || l.st == LoopState::overdubbing)
                    l.startBeat = l.shift;   // loops start again from their top (bar 1) with the clock, together
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

    // what each source hears this block (the mic only while it's allowed)
    const bool mic = micOn.load();
    const bool heard = mic && micHeard.load();
    {
        float p = 0;
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            int used = 0;
            for (int c = 0; mic && c < inChannels && c < 2; ++c)
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
    // VOICE: the mic through its board, for what you hear of yourself and what the mic source records (the vocoder
    // hears the mic as it is: it has its own pedals, and the VOCODER board after it)
    auto& voiceBoard = boards[(size_t) boardVoice];
    const bool voiceWet = stereo && voiceBoard.willRun();
    if (voiceWet)
    {
        std::copy (srcMic.begin(), srcMic.begin() + n, voiceL.begin());
        std::copy (srcMic.begin(), srcMic.begin() + n, voiceR.begin());
        voiceBoard.process (voiceL.data(), voiceR.data(), n);
        for (int i = 0; i < n; ++i)
            srcMic[(size_t) i] = 0.5f * (voiceL[(size_t) i] + voiceR[(size_t) i]);
    }

    // the delay and reverb sends, filled by the instruments and then the layers
    std::fill (busDelay.begin(), busDelay.begin() + n, 0.0f);
    std::fill (busReverb.begin(), busReverb.begin() + n, 0.0f);

    // SYNTH and CHORDS: heard, their own sources, and their echo into the shared delay and reverb. Before the
    // vocoder, so a chord the CHORDS tool sends it this block is sung this block.
    for (int k = 0; k < kInstruments; ++k)
    {
        auto& src = srcInst[(size_t) k];
        std::fill (src.begin(), src.begin() + n, 0.0f);
        auto* inst = instruments[(size_t) k];
        if (inst == nullptr)
            continue;
        std::fill (vocL.begin(), vocL.begin() + n, 0.0f);
        std::fill (vocR.begin(), vocR.begin() + n, 0.0f);
        inst->render (vocL.data(), vocR.data(), n);
        if (stereo)
            boards[(size_t) (boardSynth + k)].process (vocL.data(), vocR.data(), n);   // SYNTH, CHORDS
        const float echo = inst->echoSend();
        const float toDelay = echo * 0.55f, toReverb = echo * 0.3f;   // pocket::echoToDelay / echoToReverb
        for (int i = 0; i < n; ++i)
        {
            left[i] += vocL[(size_t) i];
            if (right != nullptr)
                right[i] += vocR[(size_t) i];
            const float m = 0.5f * (vocL[(size_t) i] + vocR[(size_t) i]);
            src[(size_t) i] = m;
            busDelay[(size_t) i] += m * toDelay;
            busReverb[(size_t) i] += m * toReverb;
        }
    }
    if (vocoder != nullptr)
    {
        std::fill (vocL.begin(), vocL.begin() + n, 0.0f);
        std::fill (vocR.begin(), vocR.begin() + n, 0.0f);
        const float* shifted[2] = { mic && inChannels > 0 && inPtr != nullptr ? inPtr[0] + chunkOffset : nullptr,
                                    mic && inChannels > 1 && inPtr != nullptr ? inPtr[1] + chunkOffset : nullptr };
        vocoder->setInput (shifted, mic ? juce::jmin (2, inChannels) : 0);
        vocoder->render (vocL.data(), vocR.data(), n);
        vocoder->setInput (nullptr, 0);   // `shifted` lives on this stack frame only
        if (stereo)
            boards[(size_t) boardVocoder].process (vocL.data(), vocR.data(), n);
        vocoderHeard = heard && (vocoderMonitor == nullptr || vocoderMonitor->load());
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

    // the chord pads in your own sound: heard, and recorded with the vocoder
    if (chordSource != nullptr)
    {
        std::fill (vocL.begin(), vocL.begin() + n, 0.0f);
        std::fill (vocR.begin(), vocR.begin() + n, 0.0f);
        chordSource->render (vocL.data(), vocR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            left[i] += vocL[(size_t) i];
            if (right != nullptr)
                right[i] += vocR[(size_t) i];
            srcVoc[(size_t) i] += 0.5f * (vocL[(size_t) i] + vocR[(size_t) i]);
        }
    }
    for (int i = 0; i < n; ++i)
        srcBoth[(size_t) i] = srcMic[(size_t) i] + srcVoc[(size_t) i];

    // DRONE: heard, its own source, its strings ringing to your voice (the mic as you hear it, only while the
    // mic is on), the vocoder, the drums or an instrument
    std::fill (srcDrone.begin(), srcDrone.begin() + n, 0.0f);
    if (drone != nullptr)
    {
        const float* feed = nullptr;
        switch (drone->feedSource())
        {
            case 0: feed = mic ? srcMic.data() : nullptr; break;
            case 1: feed = srcVoc.data(); break;
            case 2: feed = srcDrums.data(); break;
            case 3: feed = srcInst[0].data(); break;
            case 4: feed = srcInst[1].data(); break;
            default: break;
        }
        std::fill (vocL.begin(), vocL.begin() + n, 0.0f);
        std::fill (vocR.begin(), vocR.begin() + n, 0.0f);
        drone->setFeed (feed);
        drone->render (vocL.data(), vocR.data(), n);
        drone->setFeed (nullptr);
        for (int i = 0; i < n; ++i)
        {
            left[i] += vocL[(size_t) i];
            if (right != nullptr)
                right[i] += vocR[(size_t) i];
            srcDrone[(size_t) i] = 0.5f * (vocL[(size_t) i] + vocR[(size_t) i]);
        }
    }

    // play the layers, through their effects; the delay and reverb they send to
    for (int l = 0; l < kLoopLayers; ++l)
        playLayer (layers[(size_t) l], l, blockBeat, bps, n);
    // each layer's board after its knobs (in stereo from there on), then ALL on the layers together
    for (int l = 0; l < kLoopLayers; ++l)
    {
        auto& b = boards[(size_t) layerBoardId (l)];
        layerWet[(size_t) l] = stereo && b.willRun();
        if (! layerWet[(size_t) l])
            continue;
        auto& o = layerOut[(size_t) l];
        auto& lL = layerL[(size_t) l];
        auto& lR = layerR[(size_t) l];
        std::copy (o.begin(), o.begin() + n, lL.begin());
        std::copy (o.begin(), o.begin() + n, lR.begin());
        b.process (lL.data(), lR.data(), n);
        for (int i = 0; i < n; ++i)
            o[(size_t) i] = 0.5f * (lL[(size_t) i] + lR[(size_t) i]);   // what the mix and the resample sources hear
    }
    auto& allBoard = boards[(size_t) boardAll];
    const bool allWet = stereo && allBoard.willRun();
    if (allWet)
    {
        std::fill (allL.begin(), allL.begin() + n, 0.0f);
        std::fill (allR.begin(), allR.begin() + n, 0.0f);
        for (int l = 0; l < kLoopLayers; ++l)
        {
            const bool wet = layerWet[(size_t) l];
            const auto& sl = wet ? layerL[(size_t) l] : layerOut[(size_t) l];
            const auto& sr = wet ? layerR[(size_t) l] : layerOut[(size_t) l];
            for (int i = 0; i < n; ++i)
            {
                allL[(size_t) i] += sl[(size_t) i];
                allR[(size_t) i] += sr[(size_t) i];
            }
        }
        allBoard.process (allL.data(), allR.data(), n);
    }
    // the resample sources, only while a take records them
    bool needLoops = false, needMaster = false;
    for (auto& l : layers)
        if (l.st != LoopState::empty && l.st != LoopState::playing)
        {
            needLoops = needLoops || l.source == LoopSource::loops;
            needMaster = needMaster || l.source == LoopSource::master;
        }
    if (needLoops)
        for (int i = 0; i < n; ++i)
        {
            float m = 0.0f;
            if (allWet)
                m = 0.5f * (allL[(size_t) i] + allR[(size_t) i]);
            else
                for (auto& o : layerOut)
                    m += o[(size_t) i];
            srcLoops[(size_t) i] = m;
        }
    std::fill (busL.begin(), busL.begin() + n, 0.0f);
    std::fill (busR.begin(), busR.begin() + n, 0.0f);
    bus.process (busDelay.data(), busReverb.data(), busL.data(), busR.data(), n, bps > 0 ? 1.0 / bps : sampleRate * 0.5);

    for (int i = 0; i < n; ++i)
    {
        float m = srcDrums[(size_t) i] + (vocoderHeard ? srcVoc[(size_t) i] : 0.0f) + 0.5f * (busL[(size_t) i] + busR[(size_t) i]);
        for (auto& inst : srcInst)
            m += inst[(size_t) i];
        m += srcDrone[(size_t) i];
        if (allWet)
            m += 0.5f * (allL[(size_t) i] + allR[(size_t) i]);
        else
            for (auto& o : layerOut)
                m += o[(size_t) i];
        srcMix[(size_t) i] = m;
    }

    if (allWet)
        for (int i = 0; i < n; ++i)
        {
            left[i] += allL[(size_t) i];
            right[i] += allR[(size_t) i];
        }
    else
        for (int l = 0; l < kLoopLayers; ++l)
        {
            if (layerWet[(size_t) l])
            {
                for (int i = 0; i < n; ++i)
                {
                    left[i] += layerL[(size_t) l][(size_t) i];
                    right[i] += layerR[(size_t) l][(size_t) i];
                }
                continue;
            }
            for (int i = 0; i < n; ++i)
            {
                const float s = layerOut[(size_t) l][(size_t) i];
                left[i] += s;
                if (right != nullptr)
                    right[i] += s;
            }
        }
    // the click, and your dry voice while you hear yourself: heard, never recorded. MASTER goes on the mix before
    // the click.
    const bool dry = heard && dryWanted.load();
    auto& masterBoard = boards[(size_t) boardMaster];
    const bool masterWet = stereo && masterBoard.willRun();
    if (masterWet)
    {
        for (int i = 0; i < n; ++i)
        {
            left[i] += busL[(size_t) i] + (dry ? (voiceWet ? voiceL[(size_t) i] : srcMic[(size_t) i]) : 0.0f);
            right[i] += busR[(size_t) i] + (dry ? (voiceWet ? voiceR[(size_t) i] : srcMic[(size_t) i]) : 0.0f);
        }
        masterBoard.process (left, right, n);
        if (needMaster)
            for (int i = 0; i < n; ++i)
                srcMaster[(size_t) i] = 0.5f * (left[i] + right[i]);
        for (int i = 0; i < n; ++i)
        {
            left[i] += clickBuf[(size_t) i];
            right[i] += clickBuf[(size_t) i];
        }
    }
    else if (voiceWet)
    {
        for (int i = 0; i < n; ++i)
        {
            left[i] += busL[(size_t) i] + (clickBuf[(size_t) i] + (dry ? voiceL[(size_t) i] : 0.0f));
            right[i] += busR[(size_t) i] + (clickBuf[(size_t) i] + (dry ? voiceR[(size_t) i] : 0.0f));
        }
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float extra = clickBuf[(size_t) i] + (dry ? srcMic[(size_t) i] : 0.0f);
            left[i] += busL[(size_t) i] + extra;
            if (right != nullptr)
                right[i] += busR[(size_t) i] + extra;
        }
    }
    if (needMaster && ! masterWet)
        for (int i = 0; i < n; ++i)
            srcMaster[(size_t) i] = (right != nullptr ? 0.5f * (left[i] + right[i]) : left[i]) - clickBuf[(size_t) i];

    // record (after the mix, so MASTER can be resampled)
    for (int l = 0; l < kLoopLayers; ++l)
        recordLayer (layers[(size_t) l], l, blockBeat, bps, n);

    for (auto& l : layers)
        l.uiCounting = l.st == LoopState::waiting && l.fresh && l.armBeat > blockBeat && l.withCountIn > 0;
    uiCountEnd = running && countEnd > blockBeat ? countEnd : -1.0;

    for (auto& l : layers)
        l.uiStart = l.startBeat;

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
        l.dsp.reset();
        for (int k = 0; k < kFxKnobs; ++k)
            fx[(size_t) index].shown[(size_t) k] = fx[(size_t) index].knob[(size_t) k].load();
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
    // HEAR IN-OUT: only that part, round and round
    const double rf = l.regionFrom.load(), rt = juce::jmin (lenBeats, l.regionTo.load());
    const bool region = rt > rf + 1.0e-6 && rf >= 0.0;
    const double loopFrom = region ? rf : 0.0, loopBeats = region ? rt - rf : lenBeats;
    double phase = std::fmod (blockBeat - l.startBeat - loopFrom, loopBeats);
    if (phase < 0)
        phase += loopBeats;
    phase += loopFrom;
    double pos = phase * perBeat;
    const double loopStart = loopFrom * perBeat, loopEnd = juce::jmin ((double) len, (loopFrom + loopBeats) * perBeat);
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
        if (pos >= loopEnd)
            pos -= loopEnd - loopStart;
    }
    l.gain = g;
    // Where it is on the shared bars (the same for every loop).
    double shown = pos / (double) len - l.shift / lenBeats;
    shown -= std::floor (shown);
    l.uiPos = (float) shown;

    // the layer's effects, at the knobs (or their moves) for this part of the loop
    float values[kFxKnobs];
    const double frac = phase / lenBeats;
    for (int k = 0; k < kFxKnobs; ++k)
    {
        values[k] = fxValue (index, k, frac);
        fx[(size_t) index].shown[(size_t) k] = values[k];
    }
    l.dsp.process (out.data(), busDelay.data(), busReverb.data(), n, values, sampleRate);
}

void LoopsEngine::recordLayer (Layer& l, int index, double blockBeat, double bps, int n)
{
    if (l.st == LoopState::empty || l.st == LoopState::playing || ! wasRunning)
        return;

    if (l.st == LoopState::waiting && l.armBeat < 0)
        l.armBeat = l.fresh ? l.withCountIn * 4.0 : std::ceil (blockBeat / 4.0 - kEps) * 4.0;
    if (l.st == LoopState::overdubWait && l.dubBeat < 0 && l.lenBeats > 0)
        l.dubBeat = l.startBeat + std::ceil ((blockBeat - l.startBeat) / l.lenBeats - kEps) * l.lenBeats;

    const bool fromDevice = l.source == LoopSource::mic || l.source == LoopSource::vocoder || l.source == LoopSource::both;
    const double latBeats = fromDevice ? (double) latency.load() * bps : 0.0;
    const float* src = srcMic.data();
    switch (l.source)
    {
        case LoopSource::drums: src = srcDrums.data(); break;
        case LoopSource::vocoder: src = srcVoc.data(); break;
        case LoopSource::mix: src = srcMix.data(); break;
        case LoopSource::both: src = srcBoth.data(); break;
        case LoopSource::synth: src = srcInst[0].data(); break;
        case LoopSource::chords: src = srcInst[1].data(); break;
        case LoopSource::drone: src = srcDrone.data(); break;
        case LoopSource::loops: src = srcLoops.data(); break;
        case LoopSource::master: src = srcMaster.data(); break;
        case LoopSource::layerTap: src = layerOut[(size_t) l.tap].data(); break;
        default: break;
    }
    const bool takeOutSelf = l.source == LoopSource::mix || l.source == LoopSource::loops || l.source == LoopSource::master;
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
