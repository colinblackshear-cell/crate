#include "RigTakes.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr int kEdgeFade = 64;   // samples faded in and out at a take's edges (no clicks as it loops)
}

bool RigTakes::start (int layer, int source, double startBar, double bars, bool cycleOn,
                      double sampleRate, double bpm, int bpb, juce::String* why)
{
    auto fail = [why] (const juce::String& text)
    {
        if (why != nullptr)
            *why = text;
        return false;
    };
    collect();
    if (isActive())
        return fail ("Already recording a take: stop it first.");
    if (layer < 0)
        return fail ("No loop is free for a new layer.");
    if (bars <= 0 || bpm <= 0 || sampleRate <= 0)
        return fail ("That section has no length.");
    bpb = juce::jmax (1, bpb);
    const double seconds = bars * bpb * 60.0 / bpm;
    if (seconds > kMaxSeconds)
        return fail ("That section is " + juce::String (seconds / 60.0, 1) + " minutes long; takes can be up to "
                     + juce::String ((int) (kMaxSeconds / 60.0)) + " minutes. Record a shorter section, or use a free take.");

    expected = juce::jmax (1, (int) std::round (seconds * sampleRate));
    // Room for the tempo drifting a little while it records.
    const int capacity = (int) (expected * 1.1) + 2048;
    for (int i = 0; i < kPool; ++i)
    {
        jassert (status[(size_t) i].load() == 0);
        if (pool[(size_t) i].getNumSamples() < capacity || pool[(size_t) i].getNumSamples() > capacity * 2)
            pool[(size_t) i].setSize (2, capacity, false, false, true);
        events[(size_t) i].clear();
        events[(size_t) i].reserve (isNotesLayer (layer) ? (size_t) kMaxEvents : 0);
        status[(size_t) i] = 0;
    }

    // The layer's set: the same stretch of song adds to it; anything else starts it afresh.
    auto* set = setFor (layer);
    if (set == nullptr)
    {
        sets.push_back ({});
        set = &sets.back();
    }
    else if (set->source != source || std::abs (set->startBar - startBar) > 1.0e-6 || std::abs (set->bars - bars) > 1.0e-6)
    {
        *set = {};
    }
    set->layer = layer;
    set->source = source;
    set->startBar = startBar;
    set->bars = bars;
    changed();

    startBeat = startBar * bpb;
    endBeat = (startBar + bars) * bpb;
    rate = sampleRate;
    beatsPerBar = bpb;
    activeLayer = layer;
    activeSource = source;
    activeStart = startBar;
    activeBars = bars;
    firstNumber = set->nextNumber;
    current = -1;
    writeIndex = 0;
    passCount = 0;
    lastEnd = -1.0;
    passesStarted = 0;
    progressSamples = 0;
    cycle = cycleOn;
    stopRequested = false;
    state.store ((int) State::waiting, std::memory_order_release);
    return true;
}

void RigTakes::stop()
{
    stopRequested = true;
    // Still waiting for the section's top: nothing to keep, so it's called off now (the audio
    // thread may be stopped).
    int waiting = (int) State::waiting;
    state.compare_exchange_strong (waiting, (int) State::idle);
}

int RigTakes::passNumber() const
{
    return firstNumber + juce::jmax (0, passesStarted.load() - 1);
}

double RigTakes::passProgress() const
{
    return getState() == State::recording ? juce::jlimit (0.0, 1.0, progressSamples.load() / (double) juce::jmax (1, expected)) : 0.0;
}

//==============================================================================
void RigTakes::process (const juce::AudioBuffer<float>* source, int n, double beat, double bpm,
                        bool playing, double sampleRate, const juce::MidiBuffer* midi)
{
    int st = state.load (std::memory_order_acquire);
    if (st == (int) State::idle || n <= 0)
        return;

    if (stopRequested.exchange (false))
    {
        if (st == (int) State::recording)
            finishPass();
        state = (int) State::idle;
        return;
    }
    if (! playing)
    {
        // The clock stopped: a pass in the middle is kept; waiting goes on (the clock may be
        // about to start at the section's top).
        if (st == (int) State::recording)
        {
            finishPass();
            state = (int) State::idle;
        }
        lastEnd = -1.0;
        return;
    }

    const double bps = bpm / (60.0 * juce::jmax (1.0, sampleRate));
    beat -= delay.load() * bps;   // (where what's coming in was played)
    const double end = beat + n * bps;
    const bool jumped = lastEnd >= 0 && std::abs (beat - lastEnd) > 1.0e-4;
    lastEnd = end;

    if (st == (int) State::recording && (jumped || beat >= endBeat - 1.0e-9))
    {
        finishPass();
        if (! cycle.load())
        {
            state = (int) State::idle;
            return;
        }
        st = (int) State::waiting;
        state = st;
    }

    int offset = 0;
    if (st == (int) State::waiting)
    {
        // The section's top: at the start of this block (a jump or the clock starting there), or
        // somewhere inside it (playing into the section).
        if (beat >= startBeat - 1.0e-6 && beat < startBeat + 1.0e-3)
            offset = 0;
        else if (beat < startBeat && end > startBeat + 1.0e-9)
            offset = (int) std::ceil ((startBeat - beat) / bps - 1.0e-6);
        else
            return;
        if (offset >= n)
            return;
        int free = -1;
        for (int i = 0; i < kPool && free < 0; ++i)
            if (status[(size_t) i].load (std::memory_order_acquire) == 0)
                free = i;
        if (free < 0)
            return;   // (the window hasn't collected the last passes yet: this one is skipped)
        status[(size_t) free].store (1, std::memory_order_relaxed);
        current = free;
        writeIndex = 0;
        events[(size_t) free].clear();
        order[(size_t) free] = ++passCount;
        passesStarted = passCount;
        st = (int) State::recording;
        state = st;
    }

    if (st != (int) State::recording || current < 0)
        return;

    auto& buffer = pool[(size_t) current];
    const double from = beat + offset * bps;
    int count = n - offset;
    bool reachesEnd = false;
    if (const double toEnd = (endBeat - from) / bps; toEnd < count)
    {
        count = juce::jmax (0, (int) std::ceil (toEnd - 1.0e-6));
        reachesEnd = true;
    }
    count = juce::jmin (count, buffer.getNumSamples() - writeIndex);
    if (count > 0)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            if (source != nullptr && source->getNumChannels() > 0 && source->getNumSamples() >= offset + count)
                buffer.copyFrom (ch, writeIndex, *source, juce::jmin (ch, source->getNumChannels() - 1), offset, count);
            else
                buffer.clear (ch, writeIndex, count);
        }
        if (midi != nullptr)
        {
            auto& list = events[(size_t) current];
            for (const auto m : *midi)
            {
                if (m.samplePosition < offset || m.samplePosition >= offset + count || list.size() >= list.capacity())
                    continue;
                const auto msg = m.getMessage();
                if (msg.isNoteOnOrOff())
                    list.push_back ({ writeIndex + m.samplePosition - offset, msg.getNoteNumber(), msg.getFloatVelocity(),
                                      msg.getChannel(), msg.isNoteOn() });
            }
        }
        writeIndex += count;
        progressSamples = writeIndex;
    }
    if (reachesEnd)
    {
        finishPass();
        state = cycle.load() ? (int) State::waiting : (int) State::idle;
    }
}

void RigTakes::finishPass()
{
    if (current < 0)
        return;
    written[(size_t) current] = writeIndex;
    status[(size_t) current].store (2, std::memory_order_release);
    current = -1;
    writeIndex = 0;
    progressSamples = 0;
}

//==============================================================================
int RigTakes::collect()
{
    int made = 0;
    auto* set = activeLayer.load() >= 0 ? setFor (activeLayer.load()) : nullptr;
    const int samplesPerBar = (int) std::round (expected / juce::jmax (1.0e-6, activeBars));
    for (;;)
    {
        int pick = -1;
        for (int i = 0; i < kPool; ++i)
            if (status[(size_t) i].load (std::memory_order_acquire) == 2 && (pick < 0 || order[(size_t) i] < order[(size_t) pick]))
                pick = i;
        if (pick < 0)
            break;
        const int w = written[(size_t) pick];
        // Less than a bar isn't a take (a stop just after it began).
        if (set != nullptr && w >= samplesPerBar * 0.95)
        {
            Take take;
            take.audio.setSize (2, expected);
            take.audio.clear();
            const int keep = juce::jmin (w, expected);
            for (int ch = 0; ch < 2; ++ch)
            {
                take.audio.copyFrom (ch, 0, pool[(size_t) pick], ch, 0, keep);
                const int fade = juce::jmin (kEdgeFade, keep / 4);
                take.audio.applyGainRamp (ch, 0, fade, 0.0f, 1.0f);
                take.audio.applyGainRamp (ch, keep - fade, fade, 1.0f, 0.0f);
            }
            take.beats = activeBars * beatsPerBar;
            if (isNotesLayer (set->layer))
                take.notes = rignotes::notesFrom (events[(size_t) pick], expected, take.beats);
            take.partial = w < expected - juce::jmax (64, expected / 200);
            take.number = set->nextNumber++;
            set->takes.push_back (std::move (take));
            set->playing = (int) set->takes.size() - 1;
            set->fresh = true;
            // Too many: the oldest one that isn't playing goes.
            while ((int) set->takes.size() > kMaxTakes)
            {
                const int drop = set->playing == 0 ? 1 : 0;
                set->takes.erase (set->takes.begin() + drop);
                if (set->playing > drop)
                    --set->playing;
            }
            ++made;
            ++takesMade;
            changed();
        }
        events[(size_t) pick].clear();
        status[(size_t) pick].store (0, std::memory_order_release);
    }
    // Done recording (one pass, the clock stopped, or stopped), and every pass collected.
    if (getState() == State::idle && activeLayer.load() >= 0)
    {
        bool pending = false;
        for (int i = 0; i < kPool; ++i)
            pending = pending || status[(size_t) i].load() != 0;
        if (! pending)
            activeLayer = -1;
    }
    return made;
}

//==============================================================================
RigTakes::Set* RigTakes::setFor (int layer)
{
    for (auto& s : sets)
        if (s.layer == layer)
            return &s;
    return nullptr;
}

const RigTakes::Set* RigTakes::setFor (int layer) const
{
    for (auto& s : sets)
        if (s.layer == layer)
            return &s;
    return nullptr;
}

int RigTakes::layerFor (int source, double startBar, double bars) const
{
    for (auto& s : sets)
        if (s.source == source && ! s.takes.empty() && std::abs (s.startBar - startBar) < 1.0e-6 && std::abs (s.bars - bars) < 1.0e-6)
            return s.layer;
    return -1;
}

bool RigTakes::deleteTake (int layer, int take)
{
    auto* set = setFor (layer);
    if (set == nullptr || ! juce::isPositiveAndBelow (take, (int) set->takes.size()) || take == set->playing)
        return false;
    set->takes.erase (set->takes.begin() + take);
    if (set->playing > take)
        --set->playing;
    changed();
    return true;
}

void RigTakes::keepOnlyPlaying (int layer)
{
    auto* set = setFor (layer);
    if (set == nullptr || ! juce::isPositiveAndBelow (set->playing, (int) set->takes.size()))
        return;
    auto keep = std::move (set->takes[(size_t) set->playing]);
    set->takes.clear();
    set->takes.push_back (std::move (keep));
    set->playing = 0;
    changed();
}

void RigTakes::forget (int layer)
{
    if (layer == activeLayer.load())
        return;   // (it's recording)
    sets.erase (std::remove_if (sets.begin(), sets.end(), [layer] (const Set& s) { return s.layer == layer; }), sets.end());
    changed();
}

void RigTakes::clear()
{
    stop();
    // Nothing to keep from a song that's going: the passes in flight are dropped too.
    state = (int) State::idle;
    for (int i = 0; i < kPool; ++i)
        if (status[(size_t) i].load() == 2)
            status[(size_t) i] = 0;
    sets.clear();
    activeLayer = -1;
    changed();
}

void RigTakes::restore (Set set)
{
    if (set.layer < 0 || set.takes.empty())
        return;
    set.playing = juce::jlimit (0, (int) set.takes.size() - 1, set.playing);
    for (auto& t : set.takes)
        set.nextNumber = juce::jmax (set.nextNumber, t.number + 1);
    set.fresh = false;
    forget (set.layer);
    sets.push_back (std::move (set));
    changed();
}
