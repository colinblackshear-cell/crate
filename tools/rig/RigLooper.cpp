#include "RigLooper.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr int kFade = 96;   // samples of fade at each end of a new loop, so it doesn't click

static_assert (std::atomic<float>::is_always_lock_free && std::atomic<int>::is_always_lock_free
                   && std::atomic<juce::uint32>::is_always_lock_free,
               "the waveform is read by the window without a lock");

// The smallest shift whose bins hold `length` samples.
int peakShiftFor (int length)
{
    int shift = 0;
    while (((juce::int64) RigLooper::kPeakBins << shift) < (juce::int64) length)
        ++shift;
    return shift;
}

// A whole-table rewrite of the waveform: odd while it's under way (a seqlock).
void beginRewrite (std::atomic<juce::uint32>& seq)
{
    seq.store (seq.load (std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
}
void endRewrite (std::atomic<juce::uint32>& seq)
{
    seq.store (seq.load (std::memory_order_relaxed) + 1, std::memory_order_release);
}

template <typename T>
void raiseTo (std::atomic<T>& a, T v)
{
    for (T now = a.load (std::memory_order_relaxed); v > now && ! a.compare_exchange_weak (now, v, std::memory_order_relaxed);)
    {
    }
}

// Raises a bin to `v` (only ever written by the one holding the layer's lock).
void raise (std::atomic<float>& bin, float v)
{
    if (v > bin.load (std::memory_order_relaxed))
        bin.store (v, std::memory_order_relaxed);
}
}

RigLooper::Layer::PeakTable::PeakTable() : bins (new std::atomic<float>[(size_t) kPeakBins])
{
    for (int i = 0; i < kPeakBins; ++i)
        bins[(size_t) i].store (0.0f, std::memory_order_relaxed);
}

int RigLooper::scanPeaks (const juce::AudioBuffer<float>& audio, int length, float* bins)
{
    std::fill (bins, bins + kPeakBins, 0.0f);
    length = juce::jmin (length, audio.getNumSamples());
    if (length <= 0 || audio.getNumChannels() == 0)
        return 0;
    const int shift = peakShiftFor (length);
    const float* l = audio.getReadPointer (0);
    const float* r = audio.getReadPointer (juce::jmin (1, audio.getNumChannels() - 1));
    for (int i = 0; i < length; ++i)
    {
        float& b = bins[i >> shift];
        b = juce::jmax (b, std::abs (l[i]), std::abs (r[i]));
    }
    return shift;
}

int RigLooper::installPeaks (Layer& layer, const float* bins, int shift, int covered)
{
    auto& t = layer.peaks;
    beginRewrite (t.seq);
    for (int i = 0; i < kPeakBins; ++i)
        t.bins[(size_t) i].store (bins[i], std::memory_order_relaxed);
    t.shift.store (shift, std::memory_order_relaxed);
    t.covered.store (covered, std::memory_order_relaxed);
    endRewrite (t.seq);
    return kPeakBins;
}

int RigLooper::clearPeaks (Layer& layer)
{
    auto& t = layer.peaks;
    beginRewrite (t.seq);
    for (int i = 0; i < kPeakBins; ++i)
        t.bins[(size_t) i].store (0.0f, std::memory_order_relaxed);
    t.shift.store (0, std::memory_order_relaxed);
    t.covered.store (0, std::memory_order_relaxed);
    endRewrite (t.seq);
    return kPeakBins;
}

int RigLooper::coverPeaks (Layer& layer, int length)
{
    // As a recording outgrows the table, each bin takes in the next 2^k (k doublings at once):
    // one pass over the table, however long the loop is.
    auto& t = layer.peaks;
    const int shift = t.shift.load (std::memory_order_relaxed);
    const int wanted = juce::jmax (shift, peakShiftFor (length));
    beginRewrite (t.seq);
    if (wanted > shift)
    {
        const int by = 1 << (wanted - shift), keep = kPeakBins / by;
        for (int i = 0; i < keep; ++i)
        {
            float m = 0;
            for (int j = 0; j < by; ++j)
                m = juce::jmax (m, t.bins[(size_t) (i * by + j)].load (std::memory_order_relaxed));
            t.bins[(size_t) i].store (m, std::memory_order_relaxed);
        }
        for (int i = keep; i < kPeakBins; ++i)
            t.bins[(size_t) i].store (0.0f, std::memory_order_relaxed);
        t.shift.store (wanted, std::memory_order_relaxed);
    }
    t.covered.store (length, std::memory_order_relaxed);
    endRewrite (t.seq);
    return wanted > shift ? kPeakBins : 1;
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
        case mix:     return "Whole mix";
        case line:    return "Line in";
        case drums:   return "Drum machine";
        case drone:   return "Drone";
        default:      return {};
    }
}

const std::array<int, 8>& RigLooper::barChoices()
{
    static const std::array<int, 8> choices { 0, 1, 2, 4, 8, 16, 32, 64 };
    return choices;
}

RigLooper::RigLooper() = default;

void RigLooper::prepare (double rate, int maxBlock)
{
    sampleRate = rate > 0 ? rate : 44100.0;
    maxBlockSize = juce::jmax (1, maxBlock);
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
        {
            abortSnapshot (layer);
            layer.audio.setSize (2, capacity);
        }
    }
    // (No audio runs while it's prepared: the room is made here, written through, and any
    // spare is let go of.)
    {
        Room old;
        {
            const juce::SpinLock::ScopedLockType ul (undoLock);
            swapRooms (old, spare);
            spareReady = false;
        }
        if (old.audio.getNumSamples() > 0)
            roomBufferFreed();
    }
    fillRoom (undoRoom, capacity);
    roomUsableSamples = undoRoom.usable();
    undoLayer = -1;
    cap = {};
    marksWorn = false;
    restoring = false;
    for (auto& layer : layers)
        layer.deferred = 0;
}

void RigLooper::fillRoom (Room& r, int samples)
{
    r.audio.setSize (2, juce::jmax (1, samples));
    // Written through, so its memory is really there before the audio thread writes it.
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::clear (r.audio.getWritePointer (ch), r.audio.getNumSamples());
    r.stampPages = (r.audio.getNumSamples() + kCopyPageSamples - 1) / kCopyPageSamples;
    r.stamps.reset (new juce::uint32[(size_t) r.stampPages]());
}

void RigLooper::prepareRoom (int samples)
{
    samples = juce::jmin (samples, (int) (sampleRate * kMaxUndoSeconds));
    const juce::ScopedLock ml (roomMaking);
    // The slot first: a room the audio thread let go of is freed; a spare still waiting is kept
    // if it's big enough, else it's replaced. (Only the audio thread changes the slot meanwhile,
    // and only by taking a spare: after this the slot is empty, or holds a big enough spare.)
    {
        Room old, small;
        {
            const juce::SpinLock::ScopedLockType ul (undoLock);
            if (! spareReady.load() && spare.audio.getNumSamples() > 0)
                swapRooms (old, spare);
            else if (spareReady.load() && (spare.usable() >= samples || samples <= roomUsableSamples.load()))
                return;   // (also for worn marks: a spare is a new set)
            else if (spareReady.load())
            {
                swapRooms (small, spare);
                spareReady = false;
            }
        }
        for (auto* r : { &old, &small })
            if (r->audio.getNumSamples() > 0)
            {
                *r = {};
                roomBufferFreed();
            }
    }
    if (marksWorn.load())
        samples = juce::jmax (samples, roomUsableSamples.load(), 1);   // the same size again, with new marks
    else if (samples <= 0 || samples <= roomUsableSamples.load())
        return;
    Room fresh;
    roomBufferMade();
    fillRoom (fresh, samples);
    const juce::SpinLock::ScopedLockType ul (undoLock);
    swapRooms (spare, fresh);   // (the slot is empty: see above)
    spareReady.store (true, std::memory_order_release);
}

bool RigLooper::adoptSpare()
{
    // One try a block; a busy lock only means next block. Only buffers change hands.
    if (std::exchange (adoptTried, true) || ! undoLock.tryEnter())
        return false;
    bool took = false;
    if (spareReady.load (std::memory_order_acquire))
    {
        // (A spare that is no bigger than the room now, after an undo moved buffers, is let go.)
        if (spare.usable() > undoRoom.usable() || (marksWorn.load() && spare.usable() >= undoRoom.usable()))
        {
            swapRooms (undoRoom, spare);
            took = true;
        }
        spareReady.store (false, std::memory_order_relaxed);
    }
    undoLock.exit();
    if (took)
    {
        // New marks (all nothing): captures count again from the start.
        cap.generation = 0;
        cap.layer = -1;
        marksWorn = false;
        roomUsableSamples.store (undoRoom.usable(), std::memory_order_relaxed);
        counters.adoptions.fetch_add (1, std::memory_order_relaxed);
    }
    return took;
}

int RigLooper::setLayerAudio (int index, juce::AudioBuffer<float> audio, double beats)
{
    auto& layer = layers[(size_t) index];
    if (audio.getNumSamples() == 0 || beats <= 0)
        return -1;
    if (audio.getNumChannels() == 1)
    {
        audio.setSize (2, audio.getNumSamples(), true);
        audio.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
    }
    // The undo room ready for it before it's in, so its first overdub can have an undo.
    prepareRoom (audio.getNumSamples());
    // Its waveform, worked out here (the sound is still ours), so the lock is only held to put it in.
    std::vector<float> peaks ((size_t) kPeakBins);
    const int peakShift = scanPeaks (audio, audio.getNumSamples(), peaks.data());
    int version = 0;
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        abortSnapshot (layer);
        std::swap (layer.audio, audio);
        layer.length = layer.audio.getNumSamples();
        layer.publishedLength.store (layer.length, std::memory_order_relaxed);
        installPeaks (layer, peaks.data(), peakShift, layer.length);
        layer.startBeat = 0;
        layer.stopBeat = -1;
        layer.syncShift = 0.0;
        layer.lengthBeats = beats;
        layer.state = (int) State::playing;
        cancelUndo (index);
        // (Inside the lock, so a version read under it names exactly this sound.)
        version = ++layer.version;
    }
    // The old buffer is freed here, outside the lock.
    return version;
}

int RigLooper::queueLayerAudio (int index, juce::AudioBuffer<float> audio, double beats, double beat, float level, bool mute)
{
    auto& layer = layers[(size_t) index];
    if (audio.getNumChannels() == 1)
    {
        audio.setSize (2, audio.getNumSamples(), true);
        audio.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
    }
    if (audio.getNumSamples() == 0 || beats <= 0)
    {
        audio = {};
        beats = 0;
    }
    prepareRoom (audio.getNumSamples());   // the undo room ready for it ahead of the swap
    // Its waveform goes with it, worked out here (outside the lock).
    std::vector<float> peaks ((size_t) kPeakBins);
    const int peakShift = scanPeaks (audio, audio.getNumSamples(), peaks.data());
    int serial = 0;
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        std::copy (peaks.begin(), peaks.end(), layer.swapPeaks.get());
        layer.swapPeakShift = peakShift;
        layer.swapPeakCovered = audio.getNumSamples();
        std::swap (layer.swapAudio, audio);
        layer.swapBeats = beats;
        layer.swapBeat = beat;
        layer.swapLevel = juce::jlimit (0.0f, 1.0f, level);
        layer.swapMute = mute;
        layer.swapSerial = ++swapSerials;
        serial = layer.swapSerial;
        layer.swapPending = true;
        if (beat < 0)
            applySwap (layer, index);
    }
    // A swap not yet done (or the sound it replaced) is freed here, outside the lock.
    return serial;
}

void RigLooper::cancelSwap (int index)
{
    auto& layer = layers[(size_t) index];
    juce::AudioBuffer<float> old;
    const juce::SpinLock::ScopedLockType sl (layer.lock);
    layer.swapPending = false;
    std::swap (layer.swapAudio, old);
}

int RigLooper::applySwap (Layer& layer, int index)
{
    // Only moves buffers (and copies the waveform that came with it), so it's fine on the audio thread.
    // A take that's recording (or about to) is never thrown away by a loop bank or song part
    // coming in: the switch skips this layer, and the window says so.
    if (const auto st = (State) layer.state.load(); st == State::recording || st == State::armed)
    {
        layer.swapPending = false;   // the sound it would have put here is freed by maintain()
        layer.ending = (int) Ending::kept;
        return 0;
    }
    int peakOps = 0;
    abortSnapshot (layer);
    layer.syncShift = 0.0;   // the new sound plays from its own top at swapBeat
    if (layer.swapBeats > 0 && layer.swapAudio.getNumSamples() > 0)
    {
        std::swap (layer.audio, layer.swapAudio);
        layer.length = layer.audio.getNumSamples();
        layer.publishedLength.store (layer.length, std::memory_order_relaxed);
        peakOps = installPeaks (layer, layer.swapPeaks.get(), layer.swapPeakShift, layer.length);
        layer.lengthBeats = layer.swapBeats;
        layer.startBeat = juce::jmax (0.0, layer.swapBeat);
        layer.stopBeat = -1;
        layer.state = (int) State::playing;
    }
    else
    {
        layer.state = (int) State::empty;
        layer.length = 0;
        layer.publishedLength.store (layer.length, std::memory_order_relaxed);
        peakOps = clearPeaks (layer);
        layer.lengthBeats = 0;
        layer.position = 0;
    }
    layer.writeIndex = 0;
    layer.recorded = 0;
    layer.level = layer.swapLevel;
    layer.mute = layer.swapMute;
    layer.presses = 0;
    layer.restartBeat = -1.0;
    layer.swapPending = false;
    cancelUndo (index);
    const int version = ++layer.version;
    // Which queued swap this was, and the version it left: the layer now holds exactly that sound.
    layer.swapApplied.store (((juce::int64) layer.swapSerial << 32) | (juce::uint32) version, std::memory_order_release);
    return peakOps;
}

bool RigLooper::replaceLayerAudio (int index, juce::AudioBuffer<float> audio)
{
    auto& layer = layers[(size_t) index];
    if (audio.getNumChannels() == 1)
    {
        audio.setSize (2, audio.getNumSamples(), true);
        audio.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
    }
    std::vector<float> peaks ((size_t) kPeakBins);   // (outside the lock)
    const int peakShift = scanPeaks (audio, audio.getNumSamples(), peaks.data());
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        if ((State) layer.state.load() != State::playing || layer.length <= 0 || audio.getNumSamples() != layer.length)
            return false;
        abortSnapshot (layer);
        std::swap (layer.audio, audio);
        installPeaks (layer, peaks.data(), peakShift, layer.length);
        cancelUndo (index);
        ++layer.version;
    }
    return true;   // the old buffer is freed here, outside the lock
}

// ---- Copying a playing loop ----------------------------------------------------------------
//
// Who owns what. The copier (copyLayerAudio, one at a time under copyMutex) allocates the
// destination and the page flags, and frees them when it returns. It writes the snapshot's
// fields only while `state` is idle, then publishes them with a release store of `posted`.
// The audio thread takes a posted copy (acquire) at the start of a block, under the layer's
// lock, and from then on reads the loop and writes the destination only under that lock, while
// `active`. It stops being active (under the lock) when the copy is finished or the loop is about
// to change, and only then stores the final state (release). The copier reads the destination
// only after seeing a final state (acquire), or under the lock after making it inactive itself;
// either way the audio thread will never touch it again. The loop's samples are never read
// outside the layer's lock.

void RigLooper::abortSnapshot (Layer& layer)
{
    if (layer.snap.active)
    {
        layer.snap.active = false;
        layer.snap.state.store (snapAborted, std::memory_order_release);
    }
}

void RigLooper::startSnapshot (Layer& layer)
{
    auto& s = layer.snap;
    const auto st = (State) layer.state.load (std::memory_order_relaxed);
    if (layer.length <= 0 || (st != State::playing && st != State::overdubbing))
        s.state.store (snapNothing, std::memory_order_release);
    else if (layer.length != s.length || layer.audio.getNumChannels() < 2 || layer.audio.getNumSamples() < layer.length
             || s.pages != (layer.length + kCopyPageSamples - 1) / kCopyPageSamples)
        s.state.store (snapMismatch, std::memory_order_release);   // it changed since the copier looked
    else
    {
        s.active = true;
        s.next = 0;
        s.version = layer.version.load (std::memory_order_relaxed);   // the sound as it is now
        s.state.store (snapActive, std::memory_order_relaxed);
    }
}

void RigLooper::copyPage (Layer& layer, int page, juce::uint8 mark)
{
    auto& s = layer.snap;
    const int from = page << kCopyPageShift;
    const int count = juce::jmin (kCopyPageSamples, s.length - from);
    for (int ch = 0; ch < 2; ++ch)
        std::memcpy (s.dest[ch] + from, layer.audio.getReadPointer (ch) + from, sizeof (float) * (size_t) count);
    s.copied[page] = mark;
}

void RigLooper::continueSnapshot (Layer& layer)
{
    // In order, with what is left of the block's credit (after every copy that had to be made),
    // and within the block's limit of pages looked at.
    auto& s = layer.snap;
    while (s.next < s.pages && credit >= 1.0 && tally.looks < lookLimit)
    {
        ++tally.looks;
        if (s.copied[s.next] == 0)
        {
            copyPage (layer, s.next, 1);
            noteCopy (saveInOrder, juce::jmin (kCopyPageSamples, s.length - (s.next << kCopyPageShift)));
            credit -= 1.0;
        }
        ++s.next;
    }
}

bool RigLooper::audioRunningElsewhere() const
{
    const auto last = lastBlockMs.load (std::memory_order_relaxed);
    if (last == 0 || audioThread.load (std::memory_order_relaxed) == juce::Thread::getCurrentThreadId())
        return false;
    return juce::Time::getMillisecondCounter() - last < stallLimitMs();
}

juce::uint32 RigLooper::stallLimitMs() const
{
    return juce::jlimit (kStallFloorMs, kStallCapMs, 4 * longestGap256.load (std::memory_order_relaxed) / 256);
}

RigLooper::Waited RigLooper::waitForSnapshot (Layer& layer, int index) const
{
    auto& s = layer.snap;
    bool paused = false;
    auto seen = blocksDone.load (std::memory_order_acquire);
    auto seenAt = juce::Time::getMillisecondCounter();
    for (;;)
    {
        const int st = s.state.load (std::memory_order_acquire);
        if (st == snapDone || st == snapAborted || st == snapMismatch || st == snapNothing)
        {
            s.state.store (snapIdle, std::memory_order_relaxed);
            return st == snapDone ? Waited::done : st == snapNothing ? Waited::nothing : Waited::aborted;
        }
        if (st == snapActive && ! paused && copyPauseForTests)
        {
            paused = true;
            copyPauseForTests (index, false);
            continue;
        }
        const auto blocks = blocksDone.load (std::memory_order_acquire);
        const auto now = juce::Time::getMillisecondCounter();
        if (blocks != seen)
        {
            seen = blocks;
            seenAt = now;
        }
        else if (now - seenAt >= stallLimitMs())
        {
            // The audio has stopped. Under the lock, the audio thread can't be in the middle of it.
            const juce::SpinLock::ScopedLockType sl (layer.lock);
            const int again = s.state.load (std::memory_order_acquire);
            if (again == snapPosted)
            {
                s.state.store (snapIdle, std::memory_order_relaxed);
                return Waited::audioStopped;   // never started: copy it all under the lock
            }
            if (again == snapActive && s.active)
            {
                // Finish the pages left, from the loop as it is: nothing has written it since it
                // was started without first keeping its page (the lock is held).
                for (int page = 0; page < s.pages; ++page)
                    if (s.copied[page] == 0)
                        copyPage (layer, page, 1);
                s.active = false;
                s.state.store (snapIdle, std::memory_order_relaxed);
                return Waited::finishedUnderLock;
            }
            continue;   // it finished just now
        }
        juce::Thread::sleep (1);
    }
}

juce::AudioBuffer<float> RigLooper::copyUnderLock (Layer& layer, int index, int* versionOfCopy) const
{
    const juce::SpinLock::ScopedLockType sl (layer.lock);
    statUnderLock.fetch_add (1, std::memory_order_relaxed);
    const_cast<RigLooper*> (this)->finishRestorationUnderLock (layer, index);
    if (copyPauseForTests)
        copyPauseForTests (index, true);
    const auto state = (State) layer.state.load();
    if (layer.length <= 0 || (state != State::playing && state != State::overdubbing))
        return {};
    if (versionOfCopy != nullptr)
        *versionOfCopy = layer.version.load();
    juce::AudioBuffer<float> copy (2, layer.length);
    for (int ch = 0; ch < 2; ++ch)
        copy.copyFrom (ch, 0, layer.audio, juce::jmin (ch, layer.audio.getNumChannels() - 1), 0, layer.length);
    return copy;
}

juce::AudioBuffer<float> RigLooper::copyLayerAudio (int index) const
{
    return copyLayerAudio (index, nullptr);
}

juce::AudioBuffer<float> RigLooper::copyLayerAudio (int index, int* versionOfCopy) const
{
    if (versionOfCopy != nullptr)
        *versionOfCopy = -1;
    auto& layer = const_cast<Layer&> (layers[(size_t) index]);
    if (const auto st = (State) layer.state.load(); st != State::playing && st != State::overdubbing)
        return {};
    const juce::ScopedLock cl (copyMutex);
    if (copyUnderLockForTests.load() || ! audioRunningElsewhere())
        return copyUnderLock (layer, index, versionOfCopy);

    auto& s = layer.snap;
    for (int round = 0; round < kCopyRounds; ++round)
    {
        const int length = layer.publishedLength.load (std::memory_order_relaxed);
        if (length <= 0)
            return {};
        const int pages = (length + kCopyPageSamples - 1) / kCopyPageSamples;
        juce::AudioBuffer<float> out (2, length);
        // Written here first, so its memory is really there (the system hands out a big
        // allocation a page at a time, as it's first touched): the audio thread then only copies.
        out.clear();
        std::vector<juce::uint8> copied ((size_t) pages, 0);
        // The state is idle: the audio thread doesn't look at these until it's posted.
        s.dest[0] = out.getWritePointer (0);
        s.dest[1] = out.getWritePointer (1);
        s.copied = copied.data();
        s.length = length;
        s.pages = pages;
        s.state.store (snapPosted, std::memory_order_release);

        const auto waited = waitForSnapshot (layer, index);
        s.dest[0] = s.dest[1] = nullptr;
        s.copied = nullptr;
        switch (waited)
        {
            case Waited::done:
                statByAudio.fetch_add (1, std::memory_order_relaxed);
                statBeforeOverdub.fetch_add ((int) std::count (copied.begin(), copied.end(), (juce::uint8) 2),
                                             std::memory_order_relaxed);
                if (versionOfCopy != nullptr)
                    *versionOfCopy = s.version;
                return out;
            case Waited::finishedUnderLock:
                statFinishedUnderLock.fetch_add (1, std::memory_order_relaxed);
                if (versionOfCopy != nullptr)
                    *versionOfCopy = s.version;
                return out;
            case Waited::nothing:
                return {};
            case Waited::audioStopped:
                return copyUnderLock (layer, index, versionOfCopy);
            case Waited::aborted:
                statRetries.fetch_add (1, std::memory_order_relaxed);
                break;
        }
    }
    // Replaced every time: it can't be pinned down without holding it still.
    return copyUnderLock (layer, index, versionOfCopy);
}

RigLooper::CopyStats RigLooper::getCopyStats() const
{
    return { statByAudio.load(), statBeforeOverdub.load(), statRetries.load(), statUnderLock.load(), statFinishedUnderLock.load() };
}

void RigLooper::publishTally (int blockSize, double creditAvailable, double creditNow)
{
    // The bounds a block's copying must keep to (see the copy budget's design): the most it may
    // copy (what it earned, `accrual`, and the fraction of a page, or its mandatory bound if more),
    // and the most that may be copies it must make whatever the budget, for the budget's real rate
    // (a slowed test budget doesn't slow those).
    const double earned = 48000.0 * blockSize / sampleRate;
    const double capped = juce::jmin (1024.0, earned);
    const int mandatoryBound = (int) std::floor (7.0 * capped / 16.0) + 8;
    const int blockBound = (int) juce::jmin (1024.0, juce::jmax (std::floor (accrual + 1.0), (double) mandatoryBound));
    const int lookBound = (int) std::floor (8.0 * accrual) + 8;

    int mandatory = 0, discretionary = 0;
    for (int k = 0; k < numCopyKinds; ++k)
    {
        const int p = tally.pages[(size_t) k];
        (isMandatoryCopy (k) ? mandatory : discretionary) += p;
        if (p == 0)
            continue;
        counters.pages[(size_t) k].fetch_add (p, std::memory_order_relaxed);
        counters.bytes[(size_t) k].fetch_add (tally.bytes[(size_t) k], std::memory_order_relaxed);
        raiseTo (counters.maxPages[(size_t) k], p);
        raiseTo (counters.maxBytes[(size_t) k], tally.bytes[(size_t) k]);
    }
    raiseTo (counters.maxMandatory, mandatory);
    raiseTo (counters.maxDiscretionary, discretionary);
    raiseTo (counters.maxTotal, mandatory + discretionary);
    raiseTo (counters.maxLooks, tally.looks);
    if (mandatory + discretionary > blockBound)
        counters.overBudget.fetch_add (1, std::memory_order_relaxed);
    if (mandatory > mandatoryBound)
        counters.overMandatory.fetch_add (1, std::memory_order_relaxed);
    if (tally.looks > lookBound)
        counters.overLooks.fetch_add (1, std::memory_order_relaxed);
    raiseTo (counters.mostCredit, creditAvailable);
    if (creditAvailable > juce::jmin ((double) kCopyMaxPages, accrual + 1.0) + 1.0e-9)
        counters.overCredit.fetch_add (1, std::memory_order_relaxed);
    raiseTo (counters.deepestDebt, -creditNow);
    counters.blocks.fetch_add (1, std::memory_order_relaxed);
    tally = {};
}

RigLooper::CopyCounters RigLooper::getCopyCounters() const
{
    CopyCounters c;
    for (size_t k = 0; k < (size_t) numCopyKinds; ++k)
    {
        c.pages[k] = counters.pages[k].load();
        c.bytes[k] = counters.bytes[k].load();
        c.maxPages[k] = counters.maxPages[k].load();
        c.maxBytes[k] = counters.maxBytes[k].load();
    }
    c.maxMandatory = counters.maxMandatory.load();
    c.maxDiscretionary = counters.maxDiscretionary.load();
    c.maxTotal = counters.maxTotal.load();
    c.maxLooks = counters.maxLooks.load();
    c.blocks = counters.blocks.load();
    c.skippedLayerBlocks = counters.skipped.load();
    c.overBudgetBlocks = counters.overBudget.load();
    c.overMandatoryBlocks = counters.overMandatory.load();
    c.overLookBlocks = counters.overLooks.load();
    c.overCreditBlocks = counters.overCredit.load();
    c.mostCredit = counters.mostCredit.load();
    c.deepestDebt = counters.deepestDebt.load();
    c.adoptions = counters.adoptions.load();
    c.swaps = counters.swaps.load();
    c.restorations = counters.restorations.load();
    c.roomBuffersMost = counters.roomBuffersMost.load();
    return c;
}

void RigLooper::resetCopyCounters()
{
    for (size_t k = 0; k < (size_t) numCopyKinds; ++k)
    {
        counters.pages[k] = 0;
        counters.bytes[k] = 0;
        counters.maxPages[k] = 0;
        counters.maxBytes[k] = 0;
    }
    counters.maxMandatory = counters.maxDiscretionary = counters.maxTotal = counters.maxLooks = 0;
    counters.blocks = counters.skipped = counters.overBudget = counters.overMandatory = counters.overLooks = counters.overCredit = 0;
    counters.mostCredit = 0;
    counters.deepestDebt = 0;
    counters.adoptions = counters.swaps = counters.restorations = 0;
    counters.roomBuffersMost = roomBuffers.load();
}

juce::AudioBuffer<float> RigLooper::soundingLoopForTests (int index) const
{
    auto& layer = const_cast<Layer&> (layers[(size_t) index]);
    const juce::SpinLock::ScopedLockType sl (layer.lock);
    const auto state = (State) layer.state.load();
    if (layer.length <= 0 || (state != State::playing && state != State::overdubbing))
        return {};
    juce::AudioBuffer<float> copy (2, layer.length);
    for (int ch = 0; ch < 2; ++ch)
        copy.copyFrom (ch, 0, layer.audio, juce::jmin (ch, layer.audio.getNumChannels() - 1), 0, layer.length);
    // A page not put back yet sounds as the room holds it.
    if (restoring.load (std::memory_order_acquire) && restoreLayer.load (std::memory_order_relaxed) == index)
        for (int page = 0; page < restore.pages; ++page)
            if (const auto stamp = undoRoom.stamps[(size_t) page]; (stamp & ~3u) == (restore.generation << 2) && (stamp & 2u) != 0)
            {
                const int from = page << kCopyPageShift;
                for (int ch = 0; ch < 2; ++ch)
                    copy.copyFrom (ch, from, undoRoom.audio, ch, from, juce::jmin (kCopyPageSamples, layer.length - from));
            }
    return copy;
}

std::vector<float> RigLooper::readPeaks (int index, int count) const
{
    std::vector<float> peaks ((size_t) juce::jmax (1, count), 0.0f);
    const auto& layer = layers[(size_t) index];
    const auto state = (State) layer.state.load();
    if (state == State::empty || state == State::armed)
        return peaks;
    // Only the waveform, never the loop: it never moves, and all of it is atomics. If it was
    // rewritten while it was read (a new sound, a clear, a recording doubling it), it's read
    // again, a few times at most (then this is a mix of the two, drawn until the next look).
    const auto& t = layer.peaks;
    const auto slices = (juce::int64) peaks.size();
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto seq = t.seq.load (std::memory_order_acquire);
        // While it records, what's recorded so far; else the loop's length.
        const int n = t.covered.load (std::memory_order_relaxed);
        const int shift = juce::jlimit (0, 30, t.shift.load (std::memory_order_relaxed));
        for (juce::int64 p = 0; p < slices; ++p)
        {
            const int from = (int) (p * n / slices), to = (int) ((p + 1) * n / slices);
            float m = 0;
            if (to > from)
                for (int b = from >> shift, last = juce::jmin (kPeakBins - 1, (to - 1) >> shift); b <= last; ++b)
                    m = juce::jmax (m, t.bins[(size_t) b].load (std::memory_order_relaxed));
            peaks[(size_t) p] = juce::jmin (1.0f, m);
        }
        std::atomic_thread_fence (std::memory_order_acquire);
        if ((seq & 1) == 0 && t.seq.load (std::memory_order_relaxed) == seq)
            break;
    }
    return peaks;
}

void RigLooper::reset (int index)
{
    auto& layer = layers[(size_t) index];
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        abortSnapshot (layer);
        layer.presses = 0;
        layer.clears = 0;
        layer.undos = 0;
        layer.state = (int) State::empty;
        layer.length = 0;
        layer.publishedLength.store (layer.length, std::memory_order_relaxed);
        clearPeaks (layer);
        layer.lengthBeats = 0;
        layer.position = 0;
        layer.bend = straight;
        layer.bendNow = layer.bendBefore = {};
        layer.bendFade = 0;
        layer.syncShift = 0.0;
        cancelUndo (index);
        ++layer.version;
    }
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
    {
        abortSnapshot (layer);
        std::swap (layer.audio, room);
    }
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
    trim.playBeats = beats;
    trim.shape = {};
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
    trim.start = startBeat;
    trim.end = endBeat;
    trim.playBeats = playBeats;
    setShape (index, trim.shape);
}

void RigLooper::setShape (int index, const Shape& shape)
{
    auto& trim = trims[(size_t) index];
    if (trim.wholeBeats <= 0 || trim.playBeats <= 0)
        return;
    trim.shape = shape;
    juce::AudioBuffer<float> source;
    const int total = trim.whole.getNumSamples();
    if (shape.reversed || shape.slipBeats != 0 || shape.nudgeMs != 0)
    {
        source.setSize (2, total);
        const long long shift = (long long) std::llround (shape.slipBeats / trim.wholeBeats * total)
                                + (long long) std::llround (shape.nudgeMs * 0.001 * sampleRate);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* in = trim.whole.getReadPointer (juce::jmin (ch, trim.whole.getNumChannels() - 1));
            float* out = source.getWritePointer (ch);
            for (int i = 0; i < total; ++i)
            {
                long long from = ((long long) i - shift) % total;   // out[i] = in[i - shift]
                if (from < 0)
                    from += total;
                out[i] = in[shape.reversed ? total - 1 - (int) from : (int) from];
            }
        }
    }
    auto part = cropBeats (shape.reversed || shape.slipBeats != 0 || shape.nudgeMs != 0 ? source : trim.whole,
                           trim.wholeBeats, trim.start, trim.end);
    const int n = part.getNumSamples();
    if (shape.gainDb != 0)
        part.applyGain (juce::Decibels::decibelsToGain (shape.gainDb));
    const auto fadeSamples = [&] (double beats) { return juce::jlimit (0, n, (int) std::round (beats / trim.playBeats * n)); };
    const int fi = fadeSamples (shape.fadeInBeats), fo = fadeSamples (shape.fadeOutBeats);
    for (int ch = 0; ch < 2; ++ch)
    {
        if (fi > 0)
            part.applyGainRamp (ch, 0, fi, 0.0f, 1.0f);
        if (fo > 0)
            part.applyGainRamp (ch, n - fo, fo, 1.0f, 0.0f);
    }
    setLayerAudio (index, std::move (part), trim.playBeats);
    trimVersions[(size_t) index] = layers[(size_t) index].version.load();
}

double RigLooper::roomLeftSeconds (int index) const
{
    juce::int64 others = 0;
    for (size_t i = 0; i < layers.size(); ++i)
        if ((int) i != index)
            others += layers[i].audio.getNumSamples();
    const double total = kTotalRecordSeconds - (double) others / sampleRate;
    return juce::jmax (0.0, juce::jmin (kMaxRecordSeconds, total));
}

void RigLooper::maintain()
{
    // The sounds that swaps replaced.
    for (auto& layer : layers)
        if (! layer.swapPending.load() && layer.swapAudio.getNumSamples() > 0)
        {
            juce::AudioBuffer<float> old;
            {
                const juce::SpinLock::ScopedLockType sl (layer.lock);
                if (! layer.swapPending.load())
                    std::swap (layer.swapAudio, old);
            }
        }

    const int maxCapacity = (int) (sampleRate * kMaxRecordSeconds);
    const int ahead = (int) (sampleRate * kGrowAheadSeconds);
    const int step = (int) (sampleRate * kGrowStepSeconds);
    // All the layers' room together (the undo room aside) stays under kTotalRecordSeconds.
    const juce::int64 budget = (juce::int64) (sampleRate * kTotalRecordSeconds);
    juce::int64 total = 0;
    for (auto& layer : layers)
        total += layer.audio.getNumSamples();
    // The undo room is made ready for the longest loop, and for a recording's room as it grows
    // (so it's ready before the take closes), up to kMaxUndoSeconds.
    int longest = 0;
    for (auto& layer : layers)
    {
        const auto state = (State) layer.state.load();
        const int capacity = layer.audio.getNumSamples();
        if (state == State::playing || state == State::overdubbing)
            longest = juce::jmax (longest, layer.publishedLength.load (std::memory_order_relaxed));
        if (state == State::recording || state == State::armed)
            longest = juce::jmax (longest, capacity);
        if ((state != State::recording && state != State::armed) || capacity >= maxCapacity
            || capacity - layer.recorded.load() > ahead)
            continue;

        // Copy what's recorded so far into a bigger buffer, outside the lock (the audio thread
        // only writes past it), then the last bit and the swap under the lock, which is quick.
        int bigger = juce::jmin (maxCapacity, juce::jmax (capacity * 2, capacity + 4 * ahead));
        bigger = juce::jmin (bigger, capacity + juce::jmax (4 * ahead, step));
        bigger = (int) juce::jmin ((juce::int64) bigger, capacity + juce::jmax ((juce::int64) 0, budget - total));
        if (bigger <= capacity)
            continue;   // out of room: it closes at its last whole bar (Ending::full)
        total += bigger - capacity;
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
            abortSnapshot (layer);
            std::swap (layer.audio, room);
        }
        longest = juce::jmax (longest, bigger);
        // The old buffer goes here, outside the lock.
    }

    // Undo needs room for the longest loop (up to kMaxUndoSeconds: longer ones have no undo).
    // A bigger room goes through the spare slot, so an undo held now is never lost; a room the
    // audio thread let go of is freed here.
    prepareRoom (longest);

    // New loops onto the shared bars.
    for (int i = 0; i < kLayers; ++i)
        if (layers[(size_t) i].syncShift.load() != 0.0)
            applySyncShift (i);
}

void RigLooper::applySyncShift (int index)
{
    auto& layer = layers[(size_t) index];
    const double shift = layer.syncShift.load();
    if ((State) layer.state.load() != State::playing)
        return;   // overdubbing: after it (cleared or replaced: it was reset)
    // A copy made the safe way (the loop keeps playing), turned round off the audio thread.
    int version = -1;
    auto turned = copyLayerAudio (index, &version);
    const int len = turned.getNumSamples();
    const double beats = layer.lengthBeats.load();
    if (len < 2 || version < 0 || beats <= 0)
        return;
    const int by = (int) (std::llround (shift / beats * len) % len);
    if (by > 0)
        for (int ch = 0; ch < turned.getNumChannels(); ++ch)
        {
            auto* d = turned.getWritePointer (ch);
            std::rotate (d, d + (len - by), d + len);
        }
    std::vector<float> peaks ((size_t) kPeakBins);   // the turned sound's waveform (outside the lock)
    const int peakShift = scanPeaks (turned, len, peaks.data());
    {
        const juce::SpinLock::ScopedLockType sl (layer.lock);
        // Only if nothing touched it meanwhile (an overdub, a clear, a new sound): else next time.
        if (layer.version.load() != version || layer.syncShift.load() != shift || layer.length != len
            || (State) layer.state.load() != State::playing)
            return;
        abortSnapshot (layer);
        std::swap (layer.audio, turned);
        installPeaks (layer, peaks.data(), peakShift, len);
        layer.startBeat -= shift;
        layer.syncShift = 0.0;
        cancelUndo (index);
        ++layer.version;
    }
    // The unturned sound is freed here, outside the lock.
}

int RigLooper::finishRecording (Layer& layer)
{
    abortSnapshot (layer);   // (none can be running while it records; for certain)
    layer.length = juce::jmax (1, layer.writeIndex);
    layer.publishedLength.store (layer.length, std::memory_order_relaxed);
    layer.lengthBeats = layer.stopBeat - layer.startBeat;
    // A live source came in late by the round trip: what was sung on the beat is that far in.
    if (const int delay = isLiveSource (layer.source.load()) ? liveDelay.load() : 0; delay > 0 && delay < layer.length / 4)
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = layer.audio.getWritePointer (ch);
            std::rotate (d, d + delay, d + layer.length);
        }
    const int fade = juce::jmin (kFade, layer.length / 4);
    for (int ch = 0; ch < 2; ++ch)
    {
        layer.audio.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
        layer.audio.applyGainRamp (ch, layer.length - fade, fade, 1.0f, 0.0f);
    }
    // The waveform was made as it recorded, and isn't scanned again (that would take as long as
    // the loop): it is the loop as played, before the turn by the live delay and the fades. So it
    // can sit the live delay (a few ms: a pixel or two of a 1-bar loop) late, and the first and
    // last kFade samples can draw a little louder than they now are.
    const int peakOps = coverPeaks (layer, layer.length);
    // Onto the shared bars: its bar 1 goes where the other loops' bar 1 is (the longest one
    // playing), so their playheads move together. It sounds exactly as before: maintain() turns
    // the sound round by the same amount as it moves the loop's start.
    double shift = 0.0;
    // (With none playing, a loop still recording that began earlier is the one to line up with:
    // it closes on its own top.)
    double refBeats = 0.0, refAnchor = 0.0, earliestTake = layer.startBeat;
    for (auto& other : layers)
    {
        if (&other == &layer)
            continue;
        const auto st = (State) other.state.load();
        if (st == State::recording && other.startBeat < earliestTake)
            earliestTake = other.startBeat;
        if (st != State::playing && st != State::overdubbing)
            continue;
        // (A layer whose lock this block still holds, for its copying at the block's end, is read
        // as it is: this is the thread holding it.)
        const bool mine = heldLocks[(size_t) (&other - layers.data())];
        if (! mine && ! other.lock.tryEnter())
            continue;
        if (other.length >= 2 && other.lengthBeats.load() > refBeats)
        {
            refBeats = other.lengthBeats.load();
            refAnchor = syncAnchor (other);
        }
        if (! mine)
            other.lock.exit();
    }
    if (refBeats <= 0 && earliestTake < layer.startBeat)
    {
        refBeats = 1.0;
        refAnchor = earliestTake;
    }
    if (const double beats = layer.lengthBeats.load(); refBeats > 0 && beats > 0)
    {
        shift = std::fmod (layer.startBeat - refAnchor, beats);
        if (shift < 0)
            shift += beats;
        if (shift < 1.0e-6 || beats - shift < 1.0e-6)
            shift = 0.0;
    }
    layer.syncShift = shift;
    layer.state = (int) State::playing;
    ++layer.version;
    return peakOps;
}

void RigLooper::handleRequests (Layer& layer, int index, const Clock& clock, bool& wantsPlay, int& peakOps)
{
    const int bpb = juce::jmax (1, clock.beatsPerBar);

    if (layer.clears.exchange (0) > 0)
    {
        abortSnapshot (layer);
        layer.bend = straight;   // a new loop here starts straight
        layer.bendNow = layer.bendBefore = {};
        layer.bendFade = 0;
        layer.state = (int) State::empty;
        layer.length = 0;
        layer.publishedLength.store (layer.length, std::memory_order_relaxed);
        peakOps += clearPeaks (layer);
        layer.lengthBeats = 0;
        layer.position = 0;
        layer.syncShift = 0.0;
        cancelUndo (index);
        ++layer.version;
    }

    if (layer.undos.exchange (0) > 0 && undoLayer == index)
    {
        const auto state = (State) layer.state.load();
        if ((state == State::playing || state == State::overdubbing) && cap.layer == index && cap.length == layer.length)
        {
            abortSnapshot (layer);
            // The whole loop captured, and the room's buffer no bigger than the loop's (so no
            // layer ever gains room through an undo), both holding the loop: the buffers change
            // places, and nothing is copied. Otherwise the pages the overdub wrote are put back
            // from the room, a slice a block, each before anything reads it (restoreBeforeRead):
            // from this block on, the loop sounds as it was.
            const bool swap = cap.complete && undoRoom.audio.getNumChannels() == 2 && layer.audio.getNumChannels() == 2
                              && undoRoom.audio.getNumSamples() >= layer.length && layer.audio.getNumSamples() >= layer.length
                              && undoRoom.audio.getNumSamples() <= layer.audio.getNumSamples() && undoRoom.stampPages >= cap.pages;
            if (swap)
            {
                std::swap (layer.audio, undoRoom.audio);
                roomUsableSamples.store (undoRoom.usable(), std::memory_order_relaxed);
                counters.swaps.fetch_add (1, std::memory_order_relaxed);
            }
            else if (cap.written > 0)
            {
                restore.layer = index;
                restore.stale = cap.written;
                restore.cursor = juce::jmax (0, cap.firstWritten);
                restore.pages = cap.pages;
                restore.looked = 0;
                restore.generation = cap.generation;
                restoreLayer.store (index, std::memory_order_relaxed);
                restoring.store (true, std::memory_order_release);
                counters.restorations.fetch_add (1, std::memory_order_relaxed);
            }
            peakOps += installPeaks (layer, undoPeaks.get(), undoPeakShift, undoPeakCovered);   // its waveform too
            layer.state = (int) State::playing;
            ++layer.version;
        }
        undoLayer = -1;
        cap.layer = -1;
    }

    if (const double restart = layer.restartBeat.exchange (-1.0); restart >= 0)
    {
        const auto state = (State) layer.state.load();
        if (state == State::playing || state == State::overdubbing)
            layer.startBeat = restart + layer.syncShift.load();   // its bar 1 there
    }

    int presses = layer.presses.exchange (0);
    // Presses held back while an undo's restoration ran: applied once it's done, exactly once and
    // in order, if the loop still plays as it was; otherwise (cleared, replaced) dropped.
    if (layer.deferred > 0 && ((State) layer.state.load() != State::playing || layer.version.load() != layer.deferredVersion))
        layer.deferred = 0;
    if (layer.deferred > 0 && ! restoring.load (std::memory_order_acquire))
    {
        presses += layer.deferred;
        layer.deferred = 0;
    }
    for (int p = presses; p > 0; --p)
    {
        // While a restoration uses the undo room, a press that would start an overdub (and any
        // after it on this layer) waits. Never on an empty, armed or recording layer.
        if (restoring.load (std::memory_order_acquire) && ((State) layer.state.load() == State::playing || layer.deferred > 0))
        {
            if (layer.deferred == 0)
                layer.deferredVersion = layer.version.load();
            layer.deferred += p;
            break;
        }
        switch ((State) layer.state.load())
        {
            case State::empty:
            {
                if (layer.audio.getNumSamples() < 1024)
                    break;
                // The next bar line, or now if the clock is stopped (it starts with the layer).
                // A count-in waits whole bars first: from stop, that many; while playing, the next
                // bar line counts as the first.
                const int countIn = juce::jlimit (0, 4, layer.countIn.load());
                // Always on a bar line, even if the clock was left partway into a bar.
                layer.startBeat = std::ceil (clock.beat / bpb - 1.0e-9) * bpb
                                  + (clock.playing ? juce::jmax (0, countIn - 1) : countIn) * bpb;
                layer.syncShift = 0.0;
                layer.startsAt = layer.startBeat;
                const int bars = layer.bars.load();
                layer.stopBeat = bars > 0 ? layer.startBeat + bars * bpb : -1.0;
                layer.stopsAt = layer.stopBeat;
                layer.writeIndex = 0;
                layer.recorded = 0;
                layer.lengthBeats = 0;
                layer.position = 0;
                layer.state = (int) State::armed;
                layer.ending = (int) Ending::asked;
                if (! clock.playing)
                    wantsPlay = true;
                break;
            }
            case State::armed:
                layer.state = (int) State::empty;   // changed your mind
                layer.startsAt = -1.0;
                break;
            case State::recording:
            {
                // Stop at the end of the bar you're in (at least one bar).
                const double done = juce::jmax (0.0, clock.beat - layer.startBeat);
                const double barsDone = juce::jmax (1.0, std::ceil (done / bpb - 1.0e-9));
                const double stop = layer.startBeat + barsDone * bpb;
                layer.stopBeat = layer.stopBeat < 0 ? stop : juce::jmin (layer.stopBeat, stop);
                layer.stopsAt = layer.stopBeat;
                break;
            }
            case State::playing:
            {
                // Whether this overdub has an undo is decided now, once (in constant time), and
                // the window is told why if not. An undo already held elsewhere stays if not.
                const auto why = startCapture (layer, index);
                layer.noUndo = (int) why;
                if (why != NoUndo::none)
                    layer.noUndoNews = (int) why;
                if (why == NoUndo::none)
                {
                    for (int b = 0; b < kPeakBins; ++b)
                        undoPeaks[(size_t) b] = layer.peaks.bins[(size_t) b].load (std::memory_order_relaxed);
                    undoPeakShift = layer.peaks.shift.load (std::memory_order_relaxed);
                    undoPeakCovered = layer.peaks.covered.load (std::memory_order_relaxed);
                    peakOps += kPeakBins;
                }
                layer.state = (int) State::overdubbing;
                break;
            }
            case State::overdubbing:
                layer.state = (int) State::playing;
                break;
        }
    }
}

void RigLooper::cancelUndo (int index)
{
    // (Only if it's this layer's: an undo another layer was just given stays.)
    int expected = index;
    undoLayer.compare_exchange_strong (expected, -1);
    // A restoration of this loop ends too: the pages it would put back are in a buffer that is
    // no longer the loop.
    if (restoring.load (std::memory_order_acquire) && restoreLayer.load (std::memory_order_relaxed) == index)
        restoring.store (false, std::memory_order_release);
}

bool RigLooper::restorePage (Layer& layer, int page)
{
    auto& stamp = undoRoom.stamps[(size_t) page];
    const juce::uint32 g = restore.generation << 2;
    if ((stamp & ~3u) != g || (stamp & 2u) == 0)
        return false;
    // Both channels, then the mark: a page is only ever seen whole, before or after.
    copyRoomPage (layer, page, false);
    stamp = g | 1u;
    if (--restore.stale <= 0)
        restoring.store (false, std::memory_order_release);
    return true;
}

void RigLooper::restoreBeforeRead (Layer& layer, int sample)
{
    const int page = sample >> kCopyPageShift;
    if (restorePage (layer, page))
    {
        noteCopy (undoBeforeRead, juce::jmin (kCopyPageSamples, layer.length - (page << kCopyPageShift)));
        credit -= 1.0;   // it must be copied: charged, and may take the credit below nothing
    }
}

void RigLooper::continueRestoration (Layer& layer)
{
    // In order from the first page written, round the loop, with the block's positive credit.
    while (restore.stale > 0 && credit >= 1.0 && tally.looks < lookLimit && restore.looked < 2 * restore.pages)
    {
        ++tally.looks;
        ++restore.looked;
        const int page = restore.cursor;
        restore.cursor = (restore.cursor + 1) % restore.pages;
        if (restorePage (layer, page))
        {
            noteCopy (undoInOrder, juce::jmin (kCopyPageSamples, layer.length - (page << kCopyPageShift)));
            credit -= 1.0;
        }
    }
    if (restore.stale > 0 && restore.looked >= 2 * restore.pages)
    {
        jassertfalse;   // (can't happen: the count is of marked pages)
        restoring.store (false, std::memory_order_release);
    }
}

void RigLooper::finishRestorationUnderLock (Layer& layer, int index)
{
    // Off the audio thread, holding the loop's lock (the audio has stopped, or a copy must be made
    // under the lock): what's left of the restoration, so the loop read is the loop as it was.
    if (! restoring.load (std::memory_order_acquire) || restoreLayer.load (std::memory_order_relaxed) != index)
        return;
    for (int page = 0; page < restore.pages && restore.stale > 0; ++page)
        restorePage (layer, page);
    restoring.store (false, std::memory_order_release);
}

bool RigLooper::passesGuarantee (const Layer& layer) const
{
    // The guarantee test: even at the fastest tempo (kMaxBpm), in the largest block, the loop moves
    // through at most a sixteenth of what the block earns, so the copies an overdub needs before
    // it writes stay a small, fixed part of the budget. r_worst: loop samples a sample at 240 BPM.
    const double beats = layer.lengthBeats.load (std::memory_order_relaxed);
    if (layer.length <= 0 || beats <= 0)
        return false;
    const double rWorst = 4.0 * layer.length / (sampleRate * beats);
    return rWorst <= 64.0 * kCopyPagesPerSecond / sampleRate && maxBlockSize * rWorst / kCopyPageSamples <= 63.0;
}

RigLooper::NoUndo RigLooper::startCapture (Layer& layer, int index)
{
    if (layer.length > (int) (sampleRate * kMaxUndoSeconds))
        return NoUndo::tooLong;
    if (! passesGuarantee (layer))
        return NoUndo::squeezed;
    if (cap.generation >= kMaxGeneration)
    {
        // Every mark has been used: the room's marks are made afresh off the audio thread.
        cancelUndo (undoLayer.load());
        cap.layer = -1;
        marksWorn = true;
        return NoUndo::roomNotReady;
    }
    if (layer.length > undoRoom.usable() && spareReady.load (std::memory_order_acquire))
        adoptSpare();   // a bigger room is ready: this press replaces any undo anyway
    if (layer.length > undoRoom.usable())
        return NoUndo::roomNotReady;
    // A new capture: every mark of an older one is stale at once.
    ++cap.generation;
    cap.layer = index;
    cap.length = layer.length;
    cap.pages = (layer.length + kCopyPageSamples - 1) / kCopyPageSamples;
    cap.cursor = 0;
    cap.complete = false;
    cap.written = 0;
    cap.firstWritten = -1;
    cap.lastMarked = -1;
    undoLayer = index;
    return NoUndo::none;
}

void RigLooper::copyRoomPage (Layer& layer, int page, bool intoRoom)
{
    const int from = page << kCopyPageShift;
    const int count = juce::jmin (kCopyPageSamples, layer.length - from);
    for (int ch = 0; ch < 2; ++ch)
    {
        if (intoRoom)
            std::memcpy (undoRoom.audio.getWritePointer (ch) + from, layer.audio.getReadPointer (ch) + from, sizeof (float) * (size_t) count);
        else
            std::memcpy (layer.audio.getWritePointer (ch) + from, undoRoom.audio.getReadPointer (ch) + from, sizeof (float) * (size_t) count);
    }
}

void RigLooper::markBeforeWrite (Layer& layer, int sample)
{
    const int page = sample >> kCopyPageShift;
    if (page == cap.lastMarked)
        return;
    cap.lastMarked = page;
    auto& stamp = undoRoom.stamps[(size_t) page];
    const juce::uint32 g = cap.generation << 2;
    if ((stamp & ~3u) != g)
    {
        // Not captured yet: captured now, before the overdub writes it.
        copyRoomPage (layer, page, true);
        stamp = g | 3u;
        ++cap.written;
        noteCopy (undoBeforeWrite, juce::jmin (kCopyPageSamples, layer.length - (page << kCopyPageShift)));
        credit -= 1.0;
    }
    else if ((stamp & 2u) == 0)
    {
        stamp |= 2u;
        ++cap.written;
    }
    if (cap.firstWritten < 0)
        cap.firstWritten = page;
}

void RigLooper::continueCapture (Layer& layer)
{
    // In order, from the cursor, with the block's positive credit: the pages not captured yet.
    const juce::uint32 g = cap.generation << 2;
    while (cap.cursor < cap.pages && credit >= 1.0 && tally.looks < lookLimit)
    {
        ++tally.looks;
        auto& stamp = undoRoom.stamps[(size_t) cap.cursor];
        if ((stamp & ~3u) != g)
        {
            copyRoomPage (layer, cap.cursor, true);
            stamp = g | 1u;
            noteCopy (undoInOrder, juce::jmin (kCopyPageSamples, layer.length - (cap.cursor << kCopyPageShift)));
            credit -= 1.0;
        }
        ++cap.cursor;
    }
    cap.complete = cap.cursor >= cap.pages;
}

RigLooper::NoUndo RigLooper::undoLimit (int index) const
{
    const auto& layer = layers[(size_t) index];
    const int length = layer.publishedLength.load();
    const double beats = layer.lengthBeats.load();
    if (length <= 0 || beats <= 0)
        return NoUndo::none;
    if (length > (int) (sampleRate * kMaxUndoSeconds))
        return NoUndo::tooLong;
    const double rWorst = 4.0 * length / (sampleRate * beats);
    if (rWorst > 64.0 * kCopyPagesPerSecond / sampleRate || maxBlockSize * rWorst / kCopyPageSamples > 63.0)
        return NoUndo::squeezed;
    return NoUndo::none;
}

juce::String RigLooper::noUndoMessage (NoUndo why)
{
    switch (why)
    {
        case NoUndo::tooLong:      return "No undo for this overdub. Loops over 10 minutes can't be undone.";
        case NoUndo::squeezed:     return "No undo for this overdub. This sound is squeezed too far to keep an undo copy.";
        case NoUndo::roomNotReady: return "Undo wasn't ready for this overdub yet. It will be for the next one.";
        case NoUndo::none:         break;
    }
    return {};
}

juce::String RigLooper::bendName (int b)
{
    switch (b)
    {
        case reverse:   return "Reverse";
        case half:      return "Half speed";
        case twice:     return "Double speed";
        case stutter4:  return "Stutter 1/4";
        case stutter8:  return "Stutter 1/8";
        case stutter16: return "Stutter 1/16";
        default:        return "Straight";
    }
}

double RigLooper::bentPosition (const Layer::BendAt& b, double straightPos, double beat, double lengthBeats, int)
{
    if (b.mode == straight)
        return straightPos;
    const double since = beat - b.at;
    double p = b.from;
    switch (b.mode)
    {
        case reverse: p = b.from - since; break;
        case half:    p = b.from + since * 0.5; break;
        case twice:   p = b.from + since * 2.0; break;
        case stutter4: case stutter8: case stutter16:
        {
            // The slice it was in (on the grid), round and round from where it was.
            const double slice = juce::jmin (lengthBeats, b.mode == stutter4 ? 1.0 : b.mode == stutter8 ? 0.5 : 0.25);
            const double top = std::floor (b.from / slice + 1.0e-9) * slice;
            p = top + std::fmod (b.from - top + since, slice);
            break;
        }
        default: break;
    }
    p = std::fmod (p, lengthBeats);
    return p < 0 ? p + lengthBeats : p;
}

bool RigLooper::process (const Clock& clock, const std::array<const juce::AudioBuffer<float>*, numSources>& sources,
                         juce::AudioBuffer<float>& out)
{
    out.clear();
    const int n = out.getNumSamples();
    bool wantsPlay = false;
    const double beatsPerSample = clock.bpm / (60.0 * sampleRate);
    const float gainStep = 1.0f / (0.01f * (float) sampleRate);   // 10 ms to any level
    // The clock just started: every loop starts again from its top, together.
    if (clock.playing && ! wasPlaying)
        for (auto& layer : layers)
            layer.restartPending = true;
    wasPlaying = clock.playing;
    // This block's copy credit: what it earns (48,000 pages a second of audio, at most 1,024
    // pages), added to what's left, never more than one block's worth and the fraction of a page.
    // A debt (copies that had to be made) is paid back first.
    {
        const int forTests = copySliceForTests.load (std::memory_order_relaxed);
        accrual = forTests > 0 ? juce::jmin ((double) kCopyMaxPages, (double) forTests / kCopyPageSamples)
                                : juce::jmin ((double) kCopyMaxPages, kCopyPagesPerSecond * (double) n / sampleRate);
        credit = juce::jmin (credit + accrual, juce::jmin ((double) kCopyMaxPages, accrual + 1.0));
        lookLimit = (int) std::floor (8.0 * accrual) + 8;
    }
    const double available = credit;
    // A bigger undo room waiting: taken now if no undo is held (else at the press that needs it).
    adoptTried = false;
    if (spareReady.load (std::memory_order_acquire) && undoLayer.load() < 0 && ! restoring.load (std::memory_order_acquire))
        adoptSpare();   // (never while a restoration still reads the room)

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

        // The layer's one try of its lock this block. It's let go of at the end of the layer's
        // turn, or, if it has copying to do with the block's credit, at the end of the block.
        if (! layer.lock.tryEnter())
        {
            counters.skipped.fetch_add (1, std::memory_order_relaxed);
            finishLayer();   // the window is swapping in a new sound; back next block (the effects ring on)
            continue;
        }
        heldLocks[(size_t) index] = true;

        if (std::exchange (layer.restartPending, false))
        {
            const auto st = (State) layer.state.load();
            if (st == State::playing || st == State::overdubbing)
            {
                // From the bar it starts in (Logic can start partway into one).
                const int bpbNow = juce::jmax (1, clock.beatsPerBar);
                layer.startBeat = std::floor (clock.beat / bpbNow + 1.0e-9) * bpbNow + layer.syncShift.load();
                layer.bendNow = { layer.bendNow.mode, 0.0, clock.beat };
                layer.bendBefore = layer.bendNow;
                layer.bendFade = 0;
            }
        }
        // The undo's capture ends if the loop was replaced or its undo cancelled (under this lock).
        if (cap.layer == index && undoLayer.load() != index)
            cap.layer = -1;
        int peakOps = 0;   // the waveform's work this block (bounded: see kMaxPeakOpsPerBlock)
        handleRequests (layer, index, clock, wantsPlay, peakOps);
        if (cap.layer == index && undoLayer.load() != index)
            cap.layer = -1;   // (cleared, or its undo used)
        // A save's copy of a loop that overdubs faster than the guarantee allows can't keep to
        // the budget: it's called off (the copier tries again), and the undo is never touched.
        if (layer.snap.active && (State) layer.state.load() == State::overdubbing && ! passesGuarantee (layer))
            abortSnapshot (layer);
        bool capturing = cap.layer == index;
        // The moment a copy asked for is of: here, after this block's requests.
        // (A copy asked for while this loop's restoration runs starts once it's done, so it never
        // copies a page not put back yet.)
        bool restoringHere = restoring.load (std::memory_order_acquire) && restoreLayer.load (std::memory_order_relaxed) == index;
        if (layer.snap.state.load (std::memory_order_acquire) == snapPosted && ! restoringHere)
            startSnapshot (layer);

        const auto* src = sources[(size_t) juce::jlimit (0, numSources - 1, layer.source.load())];
        const float* srcL = src != nullptr ? src->getReadPointer (0) : nullptr;
        const float* srcR = src != nullptr ? src->getReadPointer (juce::jmin (1, src->getNumChannels() - 1)) : nullptr;
        auto* audioL = layer.audio.getWritePointer (0);
        auto* audioR = layer.audio.getWritePointer (1);
        int capacity = layer.audio.getNumSamples();
        const int bpb = juce::jmax (1, clock.beatsPerBar);
        float target = layer.mute.load() ? 0.0f : layer.level.load();
        float peak = 0;
        bool changed = false;
        // An overdub from a live source is written earlier by its delay (in the loop's samples).
        int dubDelay = 0;
        if (isLiveSource (layer.source.load()) && layer.length > 1 && layer.lengthBeats.load() > 0)
        {
            const double loopPerDevice = layer.length / (layer.lengthBeats.load() * sampleRate * 60.0 / juce::jmax (1.0, clock.bpm));
            dubDelay = juce::jlimit (0, layer.length / 4, (int) std::round (liveDelay.load() * loopPerDevice));
        }

        for (int i = 0; i < n; ++i)
        {
            const double beat = clock.beat + (clock.playing ? i * beatsPerSample : 0.0);
            if (layer.swapPending.load (std::memory_order_relaxed) && (! clock.playing || beat >= layer.swapBeat - 1.0e-9))
            {
                peakOps += applySwap (layer, index);
                if (cap.layer == index && undoLayer.load() != index)
                    cap.layer = -1;
                capturing = cap.layer == index;
                restoringHere = restoringHere && restoring.load (std::memory_order_relaxed);
                audioL = layer.audio.getWritePointer (0);
                audioR = layer.audio.getWritePointer (1);
                capacity = layer.audio.getNumSamples();
                target = layer.mute.load() ? 0.0f : layer.level.load();
                // (applySwap moved the version itself; moving it again would hide that this
                // version is exactly the swapped-in sound. Nothing after a swap writes this block.)
                changed = false;
            }
            auto state = (State) layer.state.load (std::memory_order_relaxed);

            if (state == State::armed)
            {
                if (! clock.playing || beat < layer.startBeat - 1.0e-9)
                    continue;
                layer.state = (int) (state = State::recording);
                layer.startsAt = -1.0;
                layer.writeIndex = 0;
                peakOps += clearPeaks (layer);
            }

            if (state == State::recording)
            {
                if (! clock.playing)
                {
                    // The clock stopped before the loop was done. A free take (until pressed
                    // again) keeps what was played, filled out to the end of that bar; a loop of
                    // set bars that didn't get there is called off.
                    const double spb = sampleRate * 60.0 / juce::jmax (1.0, clock.bpm);
                    const double barsDone = std::ceil (layer.writeIndex / spb / bpb - 1.0e-6);
                    const int fill = (int) std::round (barsDone * bpb * spb);
                    if (layer.stopBeat < 0 && barsDone >= 1 && layer.writeIndex >= (int) (spb * 0.5))
                    {
                        double keep = barsDone;
                        if (fill > capacity)
                            keep = std::floor (capacity / spb / bpb);
                        if (keep >= 1)
                        {
                            const int to = juce::jmin (capacity, (int) std::round (keep * bpb * spb));
                            for (int ch = 0; ch < 2; ++ch)
                                if (to > layer.writeIndex)
                                    layer.audio.clear (ch, layer.writeIndex, to - layer.writeIndex);
                            layer.writeIndex = to;
                            layer.stopBeat = layer.startBeat + keep * bpb;
                            layer.stopsAt = layer.stopBeat;
                            peakOps += finishRecording (layer);
                            layer.ending = (int) Ending::clockStopped;
                            break;
                        }
                    }
                    layer.state = (int) State::empty;
                    peakOps += clearPeaks (layer);
                    break;
                }
                if (layer.stopBeat >= 0 && beat >= layer.stopBeat - 1.0e-9)
                {
                    peakOps += finishRecording (layer);
                    state = State::playing;
                }
                else if (layer.writeIndex < capacity)
                {
                    const float l = srcL != nullptr ? srcL[i] : 0.0f, r = srcR != nullptr ? srcR[i] : 0.0f;
                    audioL[layer.writeIndex] = l;
                    audioR[layer.writeIndex] = r;
                    // Its waveform as it goes; past the table's end, the table doubles first.
                    if (((juce::int64) kPeakBins << layer.peaks.shift.load (std::memory_order_relaxed)) <= layer.writeIndex)
                        peakOps += coverPeaks (layer, layer.writeIndex + 1);
                    raise (layer.peaks.bins[(size_t) (layer.writeIndex >> layer.peaks.shift.load (std::memory_order_relaxed))],
                           juce::jmax (std::abs (l), std::abs (r)));
                    ++peakOps;
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
                        peakOps += clearPeaks (layer);
                        break;
                    }
                    layer.stopBeat = layer.startBeat + bars * bpb;
                    layer.stopsAt = layer.stopBeat;
                    layer.writeIndex = juce::jmin (capacity, (int) std::round (bars * bpb * spb));
                    peakOps += finishRecording (layer);
                    layer.ending = (int) Ending::full;
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
                auto readAt = [&] (double beats, float& outL2, float& outR2)
                {
                    const double at = beats / lengthBeats * layer.length;
                    const int j0 = juce::jlimit (0, layer.length - 1, (int) at);
                    const int j1 = (j0 + 1) % layer.length;
                    if (restoringHere)
                    {
                        // Restore before read: a page not put back yet is put back first.
                        restoreBeforeRead (layer, j0);
                        restoreBeforeRead (layer, j1);
                        restoringHere = restoring.load (std::memory_order_relaxed);
                    }
                    const float f = (float) (at - j0);
                    outL2 = audioL[j0] + (audioL[j1] - audioL[j0]) * f;
                    outR2 = audioR[j0] + (audioR[j1] - audioR[j0]) * f;
                };

                // A bend: where it plays from, if not straight on the beat.
                const int wanted = layer.bend.load (std::memory_order_relaxed);
                if (wanted != layer.bendNow.mode)
                {
                    layer.bendBefore = layer.bendNow;
                    layer.bendNow = { wanted, bentPosition (layer.bendBefore, p, beat, lengthBeats, clock.beatsPerBar), beat };
                    layer.bendFade = juce::jmax (1, (int) (0.008 * sampleRate));
                }
                float l, r;
                if (layer.bendNow.mode == straight && layer.bendFade == 0)
                    readAt (p, l, r);
                else
                {
                    readAt (bentPosition (layer.bendNow, p, beat, lengthBeats, clock.beatsPerBar), l, r);
                    if (layer.bendFade > 0)
                    {
                        // A short fade from how it played before, so a bend never clicks.
                        float bl, br;
                        readAt (bentPosition (layer.bendBefore, p, beat, lengthBeats, clock.beatsPerBar), bl, br);
                        const float t = (float) layer.bendFade / (float) juce::jmax (1, (int) (0.008 * sampleRate));
                        l += (bl - l) * t;
                        r += (br - r) * t;
                        --layer.bendFade;
                    }
                }

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
                    const int w = (i0 - dubDelay + layer.length) % layer.length;
                    if (capturing)
                        markBeforeWrite (layer, w);   // the undo keeps what was here
                    copyBeforeWrite (layer, w);   // a copy being made keeps what was here
                    if (srcL != nullptr)
                        audioL[w] += srcL[i];
                    if (srcR != nullptr)
                        audioR[w] += srcR[i];
                    // The waveform only ever rises here (an overdub that cancels a sound out
                    // leaves it drawn as loud as it was, until the sound is put in again).
                    raise (layer.peaks.bins[(size_t) juce::jmin (kPeakBins - 1, w >> layer.peaks.shift.load (std::memory_order_relaxed))],
                           juce::jmax (std::abs (audioL[w]), std::abs (audioR[w])));
                    ++peakOps;
                    changed = true;
                }
            }
        }

        layer.recorded = layer.writeIndex;
        if ((State) layer.state.load (std::memory_order_relaxed) == State::recording)
            layer.peaks.covered.store (layer.writeIndex, std::memory_order_relaxed);   // (only grows)
        if (peakOps > statPeakOps.load (std::memory_order_relaxed))
            statPeakOps.store (peakOps, std::memory_order_relaxed);

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
            // Where it is on the shared bars (the same for every loop).
            double p = std::fmod (clock.beat - syncAnchor (layer), layer.lengthBeats.load());
            layer.position = (p < 0 ? p + layer.lengthBeats.load() : p) / layer.lengthBeats.load();
        }
        if (peak > layer.peak.load())
            layer.peak = peak;
        if (changed)
            ++layer.version;
        finishLayer();
        if (! layer.snap.active && ! (cap.layer == index && ! cap.complete) && ! (restoringHere && restoring.load (std::memory_order_relaxed)))
        {
            heldLocks[(size_t) index] = false;
            layer.lock.exit();
        }
    }

    // The copying in order, with what's left of the credit: undo work first, then a save's copy
    // (which waits while undo work is outstanding).
    if (cap.layer >= 0 && undoLayer.load() != cap.layer)
        cap.layer = -1;   // (its loop was replaced as it played: a bank swap)
    const bool undoWork = (cap.layer >= 0 && ! cap.complete) || restoring.load (std::memory_order_relaxed);
    if (cap.layer >= 0 && ! cap.complete && heldLocks[(size_t) cap.layer])
        continueCapture (layers[(size_t) cap.layer]);
    if (restoring.load (std::memory_order_relaxed) && heldLocks[(size_t) restore.layer])
        continueRestoration (layers[(size_t) restore.layer]);
    for (int index = 0; index < kLayers; ++index)
        if (! undoWork && heldLocks[(size_t) index] && layers[(size_t) index].snap.active)
        {
            auto& layer = layers[(size_t) index];
            continueSnapshot (layer);
            if (layer.snap.next >= layer.snap.pages)
            {
                layer.snap.active = false;
                layer.snap.state.store (snapDone, std::memory_order_release);
            }
        }
    for (int index = 0; index < kLayers; ++index)
        if (std::exchange (heldLocks[(size_t) index], false))
            layers[(size_t) index].lock.exit();

    publishTally (n, available, credit);

    // The beat a copier listens for: blocks done, and the longest recent gap between them.
    const auto now = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
    if (const auto last = lastBlockMs.load (std::memory_order_relaxed); last != 0)
    {
        const auto gap = juce::jmin ((juce::uint32) 2000, now - last);
        const auto fading = longestGap256.load (std::memory_order_relaxed);
        longestGap256.store (juce::jmax (gap * 256, fading - fading / 256), std::memory_order_relaxed);
    }
    lastBlockMs.store (now, std::memory_order_relaxed);
    audioThread.store (juce::Thread::getCurrentThreadId(), std::memory_order_relaxed);
    blocksDone.fetch_add (1, std::memory_order_release);
    return wantsPlay;
}
