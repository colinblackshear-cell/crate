#include "RigScenes.h"

namespace
{
juce::String joinInts (const int* values, int count)
{
    juce::StringArray parts;
    for (int i = 0; i < count; ++i)
        parts.add (juce::String (values[i]));
    return parts.joinIntoString (" ");
}

template <size_t N>
void splitInts (const juce::String& text, std::array<int, N>& out)
{
    const auto parts = juce::StringArray::fromTokens (text, " ", {});
    for (size_t i = 0; i < N; ++i)
        out[i] = (int) i < parts.size() ? juce::jlimit (-1, 1, parts[(int) i].getIntValue()) : -1;
}
}

juce::ValueTree RigScene::toTree() const
{
    juce::ValueTree t ("Scene");
    t.setProperty ("name", name, nullptr);
    t.setProperty ("bars", bars, nullptr);
    t.setProperty ("mpcSequence", state.mpcSequence, nullptr);
    t.setProperty ("bassPattern", state.bassPattern, nullptr);
    t.setProperty ("bassSequencer", state.bassSequencer, nullptr);
    t.setProperty ("arp", state.arp, nullptr);
    t.setProperty ("drumPattern", state.drumPattern, nullptr);
    t.setProperty ("drumSequencer", state.drumSequencer, nullptr);
    t.setProperty ("vocoderOn", state.vocoderOn, nullptr);
    t.setProperty ("inputOn", state.inputOn, nullptr);
    if (state.loopBank >= 0)
        t.setProperty ("loopBank", state.loopBank, nullptr);
    t.setProperty ("stripMutes", joinInts (state.stripMutes.data(), kMaxStrips), nullptr);
    t.setProperty ("loopMutes", joinInts (state.loopMutes.data(), kLayers), nullptr);
    return t;
}

RigScene RigScene::fromTree (const juce::ValueTree& t)
{
    RigScene s;
    s.name = t.getProperty ("name", "Scene").toString();
    s.bars = juce::jlimit (0, 256, (int) t.getProperty ("bars", 0));
    s.state.mpcSequence = t.getProperty ("mpcSequence", -1);
    s.state.bassPattern = t.getProperty ("bassPattern", -1);
    s.state.bassSequencer = t.getProperty ("bassSequencer", -1);
    s.state.arp = t.getProperty ("arp", -1);
    s.state.drumPattern = t.getProperty ("drumPattern", -1);
    s.state.drumSequencer = t.getProperty ("drumSequencer", -1);
    s.state.vocoderOn = t.getProperty ("vocoderOn", -1);
    s.state.inputOn = t.getProperty ("inputOn", -1);
    s.state.loopBank = juce::jmax (-1, (int) t.getProperty ("loopBank", -1));
    splitInts (t.getProperty ("stripMutes").toString(), s.state.stripMutes);
    splitInts (t.getProperty ("loopMutes").toString(), s.state.loopMutes);
    return s;
}

//==============================================================================
void RigScenes::queue (int index, double beat)
{
    queued = index;
    queuedBeat = beat;
    earlyDone = false;
    const juce::SpinLock::ScopedLockType sl (pendingLock);
    pending = true;
    pendingBeat = beat;
    pendingIndex = index;
    pendingState = scenes[(size_t) index].state;
}

void RigScenes::launch (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) scenes.size()) || index == queued)
        return;
    const int bpb = juce::jmax (1, lastBeatsPerBar);
    const double target = lastPlaying ? std::ceil (lastBeat / bpb + 1.0e-6) * bpb : lastBeat;
    queue (index, target);
}

void RigScenes::next()
{
    const int from = queued >= 0 ? queued : current;
    if (from + 1 < (int) scenes.size())
        launch (from + 1);
}

void RigScenes::back()
{
    const int from = queued >= 0 ? queued : current;
    launch (juce::jmax (0, from - 1));
}

void RigScenes::clear()
{
    scenes.clear();
    current = queued = -1;
    const juce::SpinLock::ScopedLockType sl (pendingLock);
    pending = false;
}

void RigScenes::move (int from, int to)
{
    const int count = (int) scenes.size();
    if (! juce::isPositiveAndBelow (from, count) || ! juce::isPositiveAndBelow (to, count) || from == to)
        return;
    auto scene = scenes[(size_t) from];
    scenes.erase (scenes.begin() + from);
    scenes.insert (scenes.begin() + to, scene);
    const auto remap = [from, to] (int i)
    {
        if (i == from) return to;
        if (from < to && i > from && i <= to) return i - 1;
        if (to < from && i >= to && i < from) return i + 1;
        return i;
    };
    current = current >= 0 ? remap (current) : current;
    queued = queued >= 0 ? remap (queued) : queued;
    const juce::SpinLock::ScopedLockType sl (pendingLock);
    if (pending)
        pendingIndex = remap (pendingIndex);
}

void RigScenes::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) scenes.size()))
        return;
    scenes.erase (scenes.begin() + index);
    const auto shift = [index] (int i) { return i < 0 || i < index ? i : (i == index ? -1 : i - 1); };
    current = shift (current);
    const bool wasQueued = queued == index;
    queued = shift (queued);
    const juce::SpinLock::ScopedLockType sl (pendingLock);
    if (wasQueued)
        pending = false;
    else if (pending)
        pendingIndex = shift (pendingIndex);
}

int RigScenes::duplicate (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) scenes.size()) || (int) scenes.size() >= kMaxScenes)
        return -1;
    auto copy = scenes[(size_t) index];
    copy.name = copy.name + " 2";
    scenes.push_back (copy);
    move ((int) scenes.size() - 1, index + 1);
    return index + 1;
}

int RigScenes::tick (double beat, bool playing, int beatsPerBar)
{
    lastBeat = beat;
    lastPlaying = playing;
    lastBeatsPerBar = juce::jmax (1, beatsPerBar);

    for (int p = nextPresses.exchange (0); p > 0; --p)
        next();
    for (int p = backPresses.exchange (0); p > 0; --p)
        back();

    const int fired = firedScene.exchange (-1);
    int late = -1;
    if (fired >= 0)
    {
        // It started before its tools got ready (the clock was stopped): ready them now.
        if (queued == fired && ! earlyDone)
            late = fired;
        current = fired;
        currentStartBeat = firedAt.load();
        if (queued == fired)
            queued = -1;
    }

    // Song mode: the next scene goes on the list as soon as this one starts.
    if (songMode && queued < 0 && juce::isPositiveAndBelow (current, (int) scenes.size()) && playing)
    {
        const int bars = scenes[(size_t) current].bars;
        if (bars > 0 && current + 1 < (int) scenes.size())
            queue (current + 1, currentStartBeat + bars * lastBeatsPerBar);
    }

    if (late >= 0)
        return late;

    // The MPC and the bass change a moment early, so they're ready on the beat.
    if (queued >= 0 && ! earlyDone && (! playing || queuedBeat - beat <= 0.5))
    {
        earlyDone = true;
        return queued;
    }
    return -1;
}

bool RigScenes::takeDue (double beat, bool playing, RigScene::State& out, double& firedBeat)
{
    const juce::SpinLock::ScopedTryLockType sl (pendingLock);
    if (! sl.isLocked() || ! pending)
        return false;
    if (playing && beat < pendingBeat - 1.0e-6)
        return false;
    out = pendingState;
    firedBeat = beat;
    firedAt = beat;
    firedScene = pendingIndex;
    pending = false;
    return true;
}
