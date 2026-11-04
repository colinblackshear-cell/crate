#include "SequencerEngine.h"

#include "BankModel.h"

namespace
{
    constexpr int kSequencerChannel = 15;

    double wrap (double t, double length)
    {
        t = std::fmod (t, length);
        return t < 0 ? t + length : t;
    }

    int floorMod (int a, int b) { return ((a % b) + b) % b; }
}

SequencerEngine::SequencerEngine()
{
    latest = std::make_shared<const Snapshot>();
}

std::shared_ptr<const SequencerEngine::Snapshot> SequencerEngine::makeSnapshot (const seq::Sequence& s, int id, bool forceLoop)
{
    auto snap = std::make_shared<Snapshot>();
    snap->length = s.lengthTicks();
    snap->beatsPerBar = s.beatsPerBar;
    snap->bpm = s.bpm;
    snap->loop = s.loop || forceLoop;
    snap->id = id;
    const bool anySolo = std::any_of (s.tracks.begin(), s.tracks.end(), [] (const seq::Track& t) { return t.solo; });
    for (const auto& t : s.tracks)
        if (anySolo ? t.solo : ! t.mute)
            snap->notes.insert (snap->notes.end(), t.notes.begin(), t.notes.end());
    std::sort (snap->notes.begin(), snap->notes.end());
    return snap;
}

//==============================================================================
void SequencerEngine::setSnapshot (std::shared_ptr<const Snapshot> next)
{
    std::shared_ptr<const Snapshot> old;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        old = std::exchange (latest, std::move (next));
        ++latestVersion;
    }
    // The audio thread may still be playing it; it's freed in freeUnusedSnapshots once it isn't.
    if (old != nullptr)
        retired.push_back (std::move (old));
}

void SequencerEngine::queue (std::shared_ptr<const Snapshot> next, bool atNextBar)
{
    std::shared_ptr<const Snapshot> old;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        old = std::exchange (queuedLatest, std::move (next));
        queuedAtBarLatest = atNextBar;
        ++queuedVersion;
    }
    if (old != nullptr)
        retired.push_back (std::move (old));
}

void SequencerEngine::freeUnusedSnapshots()
{
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& s) { return s.use_count() == 1; }),
                   retired.end());
}

void SequencerEngine::switchToQueued()
{
    // The snapshot being let go of is still held on the message thread (as `latest`, the queue,
    // or in `retired`), so this never frees it here.
    playingSnapshot = std::move (queuedSnapshot);
    queuedSnapshot = nullptr;
    ++passes;
    for (auto& s : skips)
        s.pad = -1;
}

void SequencerEngine::play (bool fromStart) { request = fromStart ? 2 : 1; }
void SequencerEngine::record()               { request = 3; }
void SequencerEngine::stop()                 { request = 4; }

void SequencerEngine::locate (double t)
{
    locateTo = t;
    request = 5;
}

int SequencerEngine::takeRecorded (Recorded* dest, int max)
{
    const auto scope = fifo.read (max);
    int n = 0;
    for (int i = 0; i < scope.blockSize1; ++i) dest[n++] = fifoData[(size_t) (scope.startIndex1 + i)];
    for (int i = 0; i < scope.blockSize2; ++i) dest[n++] = fifoData[(size_t) (scope.startIndex2 + i)];
    return n;
}

void SequencerEngine::push (const Recorded& r)
{
    const auto scope = fifo.write (1);
    if (scope.blockSize1 > 0)
        fifoData[(size_t) scope.startIndex1] = r;
    else if (scope.blockSize2 > 0)
        fifoData[(size_t) scope.startIndex2] = r;
}

//==============================================================================
void SequencerEngine::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
}

void SequencerEngine::noteOn (juce::MidiBuffer& out, int pad, int velocity, int offset, int lengthSamples)
{
    out.addEvent (juce::MidiMessage::noteOn (kSequencerChannel, noteForPad (pad), (juce::uint8) juce::jlimit (1, 127, velocity)), offset);
    for (auto& slot : noteOffs)
        if (slot.pad < 0)
        {
            slot.pad = pad;
            slot.samplesLeft = offset + juce::jmax (1, lengthSamples);
            break;
        }
}

void SequencerEngine::recordHit (const Snapshot& snap, int pad, int velocity, double raw, double)
{
    const auto g = (seq::Grid) grid.load();
    const int step = seq::gridTicks (g);

    // A hit just before the count-in ends belongs on the downbeat.
    if (raw < 0)
    {
        if (raw < -juce::jmax (12, step / 2))
            return;
        raw = 0;
    }

    const double rawInLoop = wrap (raw, snap.length);
    const int q = seq::correct (juce::roundToInt (rawInLoop), g, swing.load(), snap.length);

    // Timing correct can move the hit a little later than now; the sequence will have it by the
    // time the playhead gets there, so skip it once to not hear it twice.
    const double ahead = q - rawInLoop;
    if ((ahead > 0 && ahead < snap.length / 2) || ahead < -snap.length / 2)
        for (auto& s : skips)
            if (s.pad < 0)
            {
                s = { pad, q };
                break;
            }

    auto& h = held[(size_t) pad];
    if (h.recording)
        finishHeld (pad, raw, 0);
    h.recording = true;
    h.recordedTick = q;
    h.velocity = velocity;
    h.startTick = raw;
}

void SequencerEngine::finishHeld (int pad, double now, int length)
{
    auto& h = held[(size_t) pad];
    if (! h.recording)
        return;
    h.recording = false;

    if (length <= 0)
    {
        double held = now - h.startTick;
        if (held < 0 && playingSnapshot != nullptr)
            held += playingSnapshot->length;
        length = juce::jmax (1, juce::roundToInt (held));
    }
    push ({ h.recordedTick, pad, h.velocity, length });
}

void SequencerEngine::emitSegment (const Snapshot& snap, const Segment& seg, double samplesPerTick, juce::MidiBuffer& out,
                                   std::array<Click, 16>& clicks, int& numClicks, bool clicking, int numSamples)
{
    auto offsetOf = [&] (double t) { return juce::jlimit (0, numSamples - 1, (int) (seg.sampleStart + (t - seg.from) * samplesPerTick)); };

    if (clicking)
    {
        const int beat = seq::kTicksPerBeat;
        for (int b = (int) std::ceil (seg.from / beat); b * beat < seg.to && numClicks < (int) clicks.size(); ++b)
            clicks[(size_t) numClicks++] = { offsetOf (b * beat), floorMod (b, snap.beatsPerBar) == 0 };
    }

    if (seg.from < 0)
        return;   // counting in

    const int first = (int) std::ceil (seg.from);
    auto it = std::lower_bound (snap.notes.begin(), snap.notes.end(), seq::Note { first, -1, 0, 0 },
                                [] (const seq::Note& a, const seq::Note& b) { return a.tick < b.tick; });
    for (; it != snap.notes.end() && it->tick < seg.to; ++it)
    {
        bool skip = false;
        for (auto& s : skips)
            if (s.pad == it->pad && s.tick == it->tick)
            {
                s.pad = -1;
                skip = true;
                break;
            }
        if (! skip)
            noteOn (out, it->pad, it->velocity, offsetOf (it->tick), juce::roundToInt (it->length * samplesPerTick));
    }

    for (auto& s : skips)
        if (s.pad >= 0 && s.tick >= seg.from && s.tick < seg.to)
            s.pad = -1;

    // Note repeat: held pads play on every grid step.
    const auto g = (seq::Grid) grid.load();
    const int step = seq::gridTicks (g);
    if (noteRepeat.load() && step > 0)
    {
        const int sw = swing.load();
        for (int n = (int) std::floor (seg.from / step) - 1; n <= (int) std::ceil (seg.to / step) + 1; ++n)
        {
            const int st = seq::stepTick (g, sw, n);
            if (st < seg.from || st >= seg.to || st >= snap.length)
                continue;
            for (int pad = 0; pad < (int) held.size(); ++pad)
            {
                const auto& h = held[(size_t) pad];
                // The press itself was the hit; repeats start from the next step.
                if (! h.down || sampleClock + offsetOf (st) - h.pressedAt < step / 2 * samplesPerTick)
                    continue;
                noteOn (out, pad, h.velocity, offsetOf (st), juce::roundToInt (step / 2 * samplesPerTick));
                if (recording)
                    push ({ st, pad, h.velocity, juce::jmax (1, step / 2) });
            }
        }
    }
}

void SequencerEngine::process (int numSamples, const Host* host, const juce::MidiBuffer& played, juce::MidiBuffer& out,
                               std::array<Click, 16>& clicks, int& numClicks)
{
    numClicks = 0;
    {
        const juce::SpinLock::ScopedTryLockType sl (snapshotLock);
        if (sl.isLocked())
        {
            // The old ones are still held on the message thread, so these never free anything.
            if (latestVersion != seenVersion)
            {
                playingSnapshot = latest;
                seenVersion = latestVersion;
            }
            if (queuedVersion != seenQueuedVersion)
            {
                queuedSnapshot = queuedLatest;
                queuedAtBar = queuedAtBarLatest;
                seenQueuedVersion = queuedVersion;
            }
        }
    }
    if (playingSnapshot == nullptr || numSamples <= 0)
        return;
    const auto& snap = *playingSnapshot;

    auto stopNow = [&] (int offset)
    {
        for (int pad = 0; pad < (int) held.size(); ++pad)
            finishHeld (pad, tick, 0);
        playing = recording = false;
        for (auto& slot : noteOffs)
            if (slot.pad >= 0)
            {
                out.addEvent (juce::MidiMessage::noteOff (kSequencerChannel, noteForPad (slot.pad)), offset);
                slot.pad = -1;
            }
        for (auto& s : skips)
            s.pad = -1;
        tick = juce::jmax (0.0, tick);
    };

    switch (request.exchange (0))
    {
        case 1: if (! playing) { playing = true; tick = juce::jmax (0.0, tick); } break;
        case 2: playing = true; recording = false; tick = 0; break;
        case 3:
            if (playing)
            {
                recording = ! recording;
                if (! recording)
                    for (int pad = 0; pad < (int) held.size(); ++pad)
                        finishHeld (pad, tick, 0);
            }
            else
            {
                playing = recording = true;
                tick = countIn.load() ? -snap.beatsPerBar * (double) seq::kTicksPerBeat : 0.0;
            }
            break;
        case 4: if (playing) stopNow (0); break;
        case 5: if (! playing) tick = juce::jlimit (0.0, (double) snap.length, locateTo.load()); break;
        default: break;
    }

    double bpm = snap.bpm;
    if (host != nullptr && host->playing)
    {
        following = playing = true;
        bpm = host->bpm > 0 ? host->bpm : bpm;
        const double hostTick = host->ppq * seq::kTicksPerBeat;
        tick = snap.loop ? wrap (hostTick, snap.length) : hostTick;
    }
    else if (following)
    {
        following = false;
        if (playing)
            stopNow (0);
    }

    const double ticksPerSample = bpm * seq::kTicksPerBeat / (60.0 * sampleRate);
    const double samplesPerTick = 1.0 / ticksPerSample;

    // What you played: held pads (for note repeat) and, while recording, new notes.
    for (const auto m : played)
    {
        const auto msg = m.getMessage();
        if (! (msg.isNoteOn() || msg.isNoteOff()))
            continue;
        const int pad = padForNote (msg.getNoteNumber());
        if (pad < 0)
            continue;
        auto& h = held[(size_t) pad];
        const double at = tick + m.samplePosition * ticksPerSample;
        if (msg.isNoteOn())
        {
            h.down = true;
            h.velocity = msg.getVelocity();
            h.pressedAt = sampleClock + m.samplePosition;
            if (playing && recording)
                recordHit (snap, pad, h.velocity, at, at);
        }
        else
        {
            h.down = false;
            finishHeld (pad, at, 0);
        }
    }

    if (playing)
    {
        const int mode = clickMode.load();
        const bool clicking = mode == (int) ClickMode::always || (mode == (int) ClickMode::recording && (recording || tick < 0));

        double t = tick, remaining = numSamples * ticksPerSample, sampleStart = 0;
        while (remaining > 1.0e-9 && playing)
        {
            const Snapshot* now = playingSnapshot.get();
            if (t >= now->length)
            {
                if (queuedSnapshot != nullptr && ! following)
                {
                    switchToQueued();
                    t = 0;
                    continue;
                }
                if (stopAtEnd.exchange (false) && ! following)
                {
                    stopNow (juce::jlimit (0, numSamples - 1, (int) sampleStart));
                    break;
                }
                if (now->loop)
                {
                    t -= now->length;
                    ++passes;
                }
                else
                {
                    if (! following)
                        stopNow (juce::jlimit (0, numSamples - 1, (int) sampleStart));
                    break;
                }
            }

            double end = t < 0 ? juce::jmin (0.0, t + remaining) : juce::jmin ((double) now->length, t + remaining);

            // A sequence queued for the next bar takes over at the bar line.
            bool barSwitch = false;
            if (queuedSnapshot != nullptr && queuedAtBar && ! following && t >= 0)
            {
                const double nextBar = (std::floor (t / now->ticksPerBar()) + 1) * now->ticksPerBar();
                if (nextBar <= end && nextBar < now->length)
                {
                    end = nextBar;
                    barSwitch = true;
                }
            }

            emitSegment (*now, { t, end, sampleStart }, samplesPerTick, out, clicks, numClicks, clicking, numSamples);
            sampleStart += (end - t) * samplesPerTick;
            remaining -= end - t;
            t = end;

            if (barSwitch)
            {
                switchToQueued();
                t = 0;
            }
        }
        if (playing)
            tick = t;
    }

    // Let go of sequenced notes when they end (note-off pads stop then).
    for (auto& slot : noteOffs)
    {
        if (slot.pad < 0)
            continue;
        if (slot.samplesLeft < numSamples)
        {
            out.addEvent (juce::MidiMessage::noteOff (kSequencerChannel, noteForPad (slot.pad)), juce::jmax (0, (int) slot.samplesLeft));
            slot.pad = -1;
        }
        else
        {
            slot.samplesLeft -= numSamples;
        }
    }

    sampleClock += numSamples;
    playingNow = playing;
    queuedNow = queuedSnapshot != nullptr;
    playingIdNow = playingSnapshot->id;
    passesNow = passes;
    recordingNow = recording;
    followingNow = following;
    positionNow = tick;
}
