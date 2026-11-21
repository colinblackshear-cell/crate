#include "RigAutomation.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPointSpacing = 1.0 / 64.0;   // beats between recorded points, at most
constexpr float kMoved = 1.0e-6f;              // a change smaller than this isn't a move
constexpr double kTouchHoldSeconds = 0.5;      // a lane waits this long after you let go
constexpr int kStartPoints = 4096;
constexpr size_t kUndoSteps = 16;
// More settings than this moving in one block is a new sound loading, not your hands.
constexpr int kMaxMovesPerBlock = 8;
}

//==============================================================================
float RigAutomation::Lane::valueAt (double beat) const
{
    if (points.empty())
        return 0.0f;
    if (beat <= points.front().beat)
        return points.front().value;
    if (beat >= points.back().beat)
        return points.back().value;
    const auto next = std::upper_bound (points.begin(), points.end(), beat, [] (double b, const Point& p) { return b < p.beat; });
    const auto& b = *next;
    const auto& a = *(next - 1);
    const double span = b.beat - a.beat;
    if (span <= 1.0e-12)
        return b.value;
    return a.value + (b.value - a.value) * (float) ((beat - a.beat) / span);
}

double RigAutomation::Lane::positionAt (double clockBeat) const
{
    if (lengthBeats <= 0)
        return -1.0;
    double pos = clockBeat - startBeat;
    if (loop)
    {
        pos = std::fmod (pos, lengthBeats);
        if (pos < 0)
            pos += lengthBeats;
        return pos;
    }
    return pos < 0 || pos > lengthBeats ? -1.0 : pos;
}

//==============================================================================
void RigAutomation::pressRecord (bool overSong)
{
    switch (getRecState())
    {
        case RecState::off:
        {
            // Every setting that turns smoothly, on every tool, is watched.
            std::vector<Watch> list;
            for (int t = 0; t < numTools; ++t)
            {
                const int n = numParameters != nullptr ? numParameters (t) : 0;
                for (int p = 0; p < n; ++p)
                {
                    auto* param = getParameter != nullptr ? getParameter (t, p) : nullptr;
                    if (param == nullptr || param->isDiscrete() || param->isBoolean() || ! param->isAutomatable())
                        continue;
                    Watch w;
                    w.param = param;
                    w.tool = t;
                    w.parameter = p;
                    w.seen = param->getValue();
                    list.push_back (w);
                }
            }
            for (auto& slot : slots)
                if ((int) slot.points.size() < kStartPoints)
                    slot.points.resize (kStartPoints);
            const juce::SpinLock::ScopedLockType sl (lock);
            std::swap (watch, list);
            for (auto& slot : slots)
            {
                slot.watch = -1;
                slot.used = 0;
            }
            slotsUsed = 0;
            numRecording = 0;
            recStart = -1;
            recStop = -1;
            recOverSong = overSong;
            closeRequested = false;
            cancelRequested = false;
            recFinished = false;
            linkWatch();
            recState = (int) RecState::armed;
            break;
        }
        case RecState::armed:
            cancelRecord();
            break;
        case RecState::recording:
            closeRequested = true;
            break;
        case RecState::closing:
            break;
    }
}

void RigAutomation::cancelRecord()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (getRecState() == RecState::armed)
        recState = (int) RecState::off;
}

bool RigAutomation::service()
{
    if (recFinished.load())
    {
        finishRecording();
        return true;
    }
    // A long recording: more room before a setting's slot fills up.
    if (getRecState() == RecState::recording)
        for (auto& slot : slots)
        {
            const int used = slot.used.load();
            const int capacity = (int) slot.points.size();
            if (slot.watch < 0 || used < capacity * 3 / 4)
                continue;
            std::vector<Point> bigger ((size_t) capacity * 2);
            const juce::SpinLock::ScopedLockType sl (lock);
            std::copy (slot.points.begin(), slot.points.begin() + slot.used.load(), bigger.begin());
            std::swap (slot.points, bigger);
        }
    return false;
}

void RigAutomation::finishRecording()
{
    // The audio thread has stopped writing the slots (closing, finished), so they're read freely.
    std::vector<Lane> made;
    const double length = juce::jmax (1.0, recStop - recStart);
    for (int s = 0; s < slotsUsed; ++s)
    {
        auto& slot = slots[(size_t) s];
        if (slot.watch < 0 || slot.watch >= (int) watch.size())
            continue;
        const auto& w = watch[(size_t) slot.watch];
        Lane lane;
        lane.tool = w.tool;
        lane.parameter = w.parameter;
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (w.param))
            lane.paramId = withId->paramID;
        lane.startBeat = recStart;
        lane.lengthBeats = length;
        lane.loop = ! recOverSong.load();
        const int used = slot.used.load();
        lane.points.reserve ((size_t) used + 1);
        for (int i = 0; i < used; ++i)
            if (slot.points[(size_t) i].beat <= length)
                lane.points.push_back (slot.points[(size_t) i]);
        if (lane.points.empty())
            continue;
        // It ends where you left it.
        if (lane.points.back().beat < length)
            lane.points.push_back ({ length, lane.points.back().value });
        made.push_back (std::move (lane));
    }
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        recFinished = false;
        recState = (int) RecState::off;
        numRecording = 0;
        slotsUsed = 0;
        for (auto& slot : slots)
            slot.watch = -1;
    }
    if (! made.empty())
    {
        snapshotForUndo();
        auto now = getLanes();
        for (auto& lane : made)
        {
            const auto same = std::find_if (now.begin(), now.end(), [&] (const Lane& l) { return l.tool == lane.tool && l.parameter == lane.parameter; });
            if (same != now.end())
                *same = std::move (lane);
            else if ((int) now.size() < kMaxLanes)
                now.push_back (std::move (lane));
        }
        install (std::move (now));
    }
    finishedLanes = (int) made.size();
}

//==============================================================================
std::vector<RigAutomation::Lane> RigAutomation::getLanes() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return lanes;
}

int RigAutomation::getNumLanes() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return (int) lanes.size();
}

juce::String RigAutomation::laneName (const Lane& lane) const
{
    juce::String name = toolName != nullptr ? toolName (lane.tool) : juce::String (lane.tool);
    if (auto* p = getParameter != nullptr ? getParameter (lane.tool, lane.parameter) : nullptr)
        name << " " << p->getName (40);
    return name;
}

juce::String RigAutomation::laneName (int index) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return juce::isPositiveAndBelow (index, (int) lanes.size()) ? laneName (lanes[(size_t) index]) : juce::String();
}

int RigAutomation::findLane (int tool, int parameter) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i].tool == tool && lanes[i].parameter == parameter)
            return (int) i;
    return -1;
}

void RigAutomation::snapshotForUndo()
{
    undoStack.push_back (getLanes());
    if (undoStack.size() > kUndoSteps)
        undoStack.erase (undoStack.begin());
}

void RigAutomation::undo()
{
    if (undoStack.empty())
        return;
    auto back = std::move (undoStack.back());
    undoStack.pop_back();
    install (std::move (back));
}

void RigAutomation::install (std::vector<Lane> newLanes)
{
    std::vector<Live> newLive (newLanes.size());
    for (size_t i = 0; i < newLanes.size(); ++i)
    {
        auto& l = newLive[i];
        l.param = getParameter != nullptr ? getParameter (newLanes[i].tool, newLanes[i].parameter) : nullptr;
        l.seen = l.param != nullptr ? l.param->getValue() : 0.0f;
    }
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        // A lane that's still the same setting keeps what it knows (so it doesn't jump).
        for (size_t i = 0; i < newLanes.size(); ++i)
            for (size_t j = 0; j < lanes.size() && j < live.size(); ++j)
                if (lanes[j].tool == newLanes[i].tool && lanes[j].parameter == newLanes[i].parameter)
                {
                    newLive[i].written = live[j].written;
                    newLive[i].seen = live[j].seen;
                    newLive[i].heldSamples = live[j].heldSamples;
                    break;
                }
        std::swap (lanes, newLanes);
        std::swap (live, newLive);
        linkWatch();
    }
    ++version;
    // The old lanes are freed here, outside the lock.
}

void RigAutomation::linkWatch()
{
    for (auto& w : watch)
        w.lane = -1;
    for (size_t i = 0; i < lanes.size(); ++i)
    {
        live[i].watch = -1;
        for (size_t k = 0; k < watch.size(); ++k)
            if (watch[k].tool == lanes[i].tool && watch[k].parameter == lanes[i].parameter)
            {
                watch[k].lane = (int) i;
                live[i].watch = (int) k;
                break;
            }
    }
}

void RigAutomation::setLane (int index, const Lane& lane)
{
    auto now = getLanes();
    if (! juce::isPositiveAndBelow (index, (int) now.size()))
        return;
    snapshotForUndo();
    now[(size_t) index] = lane;
    install (std::move (now));
}

void RigAutomation::setLaneOn (int index, bool on)
{
    auto now = getLanes();
    if (! juce::isPositiveAndBelow (index, (int) now.size()) || now[(size_t) index].on == on)
        return;
    snapshotForUndo();
    now[(size_t) index].on = on;
    install (std::move (now));
}

void RigAutomation::setLaneLoop (int index, bool loop)
{
    auto now = getLanes();
    if (! juce::isPositiveAndBelow (index, (int) now.size()) || now[(size_t) index].loop == loop)
        return;
    snapshotForUndo();
    now[(size_t) index].loop = loop;
    install (std::move (now));
}

void RigAutomation::setLaneLength (int index, double beats)
{
    auto now = getLanes();
    if (! juce::isPositiveAndBelow (index, (int) now.size()) || beats < 1.0)
        return;
    snapshotForUndo();
    auto& lane = now[(size_t) index];
    if (beats < lane.lengthBeats)
    {
        const float end = lane.valueAt (beats);
        lane.points.erase (std::remove_if (lane.points.begin(), lane.points.end(), [beats] (const Point& p) { return p.beat > beats; }), lane.points.end());
        lane.points.push_back ({ beats, end });
    }
    lane.lengthBeats = beats;
    install (std::move (now));
}

void RigAutomation::drawLine (int index, double fromBeat, float fromValue, double toBeat, float toValue, bool snapshot)
{
    auto now = getLanes();
    if (! juce::isPositiveAndBelow (index, (int) now.size()))
        return;
    if (snapshot)
        snapshotForUndo();
    auto& lane = now[(size_t) index];
    if (toBeat < fromBeat)
    {
        std::swap (fromBeat, toBeat);
        std::swap (fromValue, toValue);
    }
    fromBeat = juce::jlimit (0.0, lane.lengthBeats, fromBeat);
    toBeat = juce::jlimit (0.0, lane.lengthBeats, toBeat);
    fromValue = juce::jlimit (0.0f, 1.0f, fromValue);
    toValue = juce::jlimit (0.0f, 1.0f, toValue);
    auto& pts = lane.points;
    // What it was just outside the line, so the rest of the lane stays as it was.
    const float before = lane.valueAt (fromBeat - 1.0e-4), after = lane.valueAt (toBeat + 1.0e-4);
    const bool hadPoints = ! pts.empty();
    pts.erase (std::remove_if (pts.begin(), pts.end(), [=] (const Point& p) { return p.beat >= fromBeat && p.beat <= toBeat; }), pts.end());
    if (hadPoints && fromBeat > 1.0e-4)
        pts.push_back ({ fromBeat - 1.0e-4, before });
    pts.push_back ({ fromBeat, fromValue });
    if (toBeat > fromBeat)
        pts.push_back ({ toBeat, toValue });
    if (hadPoints && toBeat < lane.lengthBeats - 1.0e-4)
        pts.push_back ({ toBeat + 1.0e-4, after });
    std::sort (pts.begin(), pts.end(), [] (const Point& a, const Point& b) { return a.beat < b.beat; });
    install (std::move (now));
}

int RigAutomation::addLane (Lane lane)
{
    auto now = getLanes();
    snapshotForUndo();
    int index = -1;
    for (size_t i = 0; i < now.size(); ++i)
        if (now[i].tool == lane.tool && now[i].parameter == lane.parameter)
            index = (int) i;
    if (lane.paramId.isEmpty())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (getParameter != nullptr ? getParameter (lane.tool, lane.parameter) : nullptr))
            lane.paramId = withId->paramID;
    if (index >= 0)
        now[(size_t) index] = std::move (lane);
    else if ((int) now.size() < kMaxLanes)
    {
        now.push_back (std::move (lane));
        index = (int) now.size() - 1;
    }
    install (std::move (now));
    return index;
}

void RigAutomation::clearLane (int index)
{
    auto now = getLanes();
    if (! juce::isPositiveAndBelow (index, (int) now.size()))
        return;
    snapshotForUndo();
    now.erase (now.begin() + index);
    install (std::move (now));
}

void RigAutomation::clearAll()
{
    if (getNumLanes() == 0)
        return;
    snapshotForUndo();
    install ({});
}

void RigAutomation::clearTool (int tool, int fxTool)
{
    auto now = getLanes();
    const auto size = now.size();
    now.erase (std::remove_if (now.begin(), now.end(), [=] (const Lane& l) { return l.tool == tool || (fxTool >= 0 && l.tool == fxTool); }), now.end());
    if (now.size() == size)
        return;
    snapshotForUndo();
    install (std::move (now));
}

float RigAutomation::getLanePosition (int index) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    if (! juce::isPositiveAndBelow (index, (int) lanes.size()))
        return -1.0f;
    const auto& lane = lanes[(size_t) index];
    const double pos = lane.positionAt (lastBeat.load());
    return pos < 0 ? -1.0f : (float) (pos / lane.lengthBeats);
}

bool RigAutomation::isTouched (int index) const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return juce::isPositiveAndBelow (index, (int) live.size()) && live[(size_t) index].heldSamples > 0;
}

//==============================================================================
juce::ValueTree RigAutomation::toTree() const
{
    juce::ValueTree tree (kTreeType);
    tree.setProperty ("read", reading.load(), nullptr);
    for (const auto& lane : getLanes())
    {
        juce::ValueTree t ("Lane");
        t.setProperty ("tool", lane.tool, nullptr);
        t.setProperty ("parameter", lane.parameter, nullptr);
        t.setProperty ("id", lane.paramId, nullptr);
        t.setProperty ("start", lane.startBeat, nullptr);
        t.setProperty ("length", lane.lengthBeats, nullptr);
        t.setProperty ("loop", lane.loop, nullptr);
        t.setProperty ("on", lane.on, nullptr);
        juce::MemoryOutputStream points;
        for (const auto& p : lane.points)
        {
            points.writeDouble (p.beat);
            points.writeFloat (p.value);
        }
        t.setProperty ("points", points.getMemoryBlock(), nullptr);
        tree.appendChild (t, nullptr);
    }
    return tree;
}

void RigAutomation::fromTree (const juce::ValueTree& tree)
{
    std::vector<Lane> list;
    reading = ! tree.isValid() || (bool) tree.getProperty ("read", true);
    for (const auto& t : tree)
    {
        if (! t.hasType ("Lane"))
            continue;
        Lane lane;
        lane.tool = t.getProperty ("tool", 0);
        lane.parameter = t.getProperty ("parameter", 0);
        lane.paramId = t.getProperty ("id").toString();
        // The setting by its ID, in case the tool's list of settings has changed.
        if (lane.paramId.isNotEmpty() && numParameters != nullptr && getParameter != nullptr)
        {
            auto* at = dynamic_cast<juce::AudioProcessorParameterWithID*> (getParameter (lane.tool, lane.parameter));
            if (at == nullptr || at->paramID != lane.paramId)
                for (int p = 0; p < numParameters (lane.tool); ++p)
                    if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (getParameter (lane.tool, p)); withId != nullptr && withId->paramID == lane.paramId)
                    {
                        lane.parameter = p;
                        break;
                    }
        }
        lane.startBeat = t.getProperty ("start", 0.0);
        lane.lengthBeats = juce::jmax (1.0, (double) t.getProperty ("length", 4.0));
        lane.loop = t.getProperty ("loop", true);
        lane.on = t.getProperty ("on", true);
        if (const auto* block = t.getProperty ("points").getBinaryData())
        {
            juce::MemoryInputStream in (*block, false);
            while (in.getNumBytesRemaining() >= 12)
            {
                Point p;
                p.beat = in.readDouble();
                p.value = juce::jlimit (0.0f, 1.0f, in.readFloat());
                lane.points.push_back (p);
            }
        }
        if (! lane.points.empty() && (int) list.size() < kMaxLanes)
            list.push_back (std::move (lane));
    }
    undoStack.clear();
    install (std::move (list));
}

//==============================================================================
void RigAutomation::recordInto (Slot& slot, double pos, float value)
{
    auto& pts = slot.points;
    int used = slot.used.load (std::memory_order_relaxed);
    const auto add = [&] (double beat, float v)
    {
        if (used < (int) pts.size())
        {
            pts[(size_t) used++] = { beat, v };
            slot.lastBeat = beat;
        }
    };
    if (std::abs (value - slot.lastValue) > kMoved)
    {
        // It held still since the last point: keep the hold, then the move.
        if (slot.lastBeat < slot.lastPos - 1.0e-9)
            add (slot.lastPos, slot.lastValue);
        if (pos - slot.lastBeat >= kPointSpacing || used == 0)
            add (pos, value);
        else if (used > 0)
            pts[(size_t) used - 1].value = value;   // (too close: the last point moves instead)
    }
    slot.used.store (used, std::memory_order_release);
    slot.lastValue = value;
    slot.lastPos = pos;
}

void RigAutomation::startSlot (int watchIndex, float before, double prevPos, double pos, float value)
{
    if (slotsUsed >= kMaxRecording || ! juce::isPositiveAndBelow (watchIndex, (int) watch.size()))
        return;
    auto& slot = slots[(size_t) slotsUsed];
    if (slot.points.size() < 3)
        return;
    slot.watch = watchIndex;
    watch[(size_t) watchIndex].slot = slotsUsed;
    ++slotsUsed;
    numRecording = slotsUsed;
    // Where it was from the start of the recording until you moved it.
    int used = 0;
    slot.points[(size_t) used++] = { 0.0, before };
    if (prevPos > 0)
        slot.points[(size_t) used++] = { prevPos, before };
    slot.points[(size_t) used++] = { juce::jmax (pos, prevPos), value };
    slot.lastBeat = juce::jmax (pos, prevPos);
    slot.lastValue = value;
    slot.lastPos = pos;
    slot.used.store (used, std::memory_order_release);
}

void RigAutomation::process (const Clock& c)
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (! sl.isLocked())
        return;
    lastBeat = c.beat;
    const int bpb = juce::jmax (1, c.beatsPerBar);
    const int holdSamples = (int) (kTouchHoldSeconds * c.sampleRate);
    auto state = (RecState) recState.load();

    // Rec moves.
    if (state == RecState::armed && c.playing)
    {
        if (recStart < 0)
            recStart = std::ceil (c.beat / bpb - 1.0e-9) * bpb;   // the next bar (or this one, right on it)
        if (c.beat >= recStart - 1.0e-9)
        {
            for (auto& w : watch)
            {
                w.seen = w.param->getValue();
                w.slot = -1;
            }
            slotsUsed = 0;
            numRecording = 0;
            recState = (int) (state = RecState::recording);
        }
    }
    const bool recording = state == RecState::recording || (state == RecState::closing && ! recFinished.load());
    const double pos = c.beat - recStart;
    if (recording)
    {
        if (! c.playing)
        {
            // The clock stopped: it closes at the end of the bar it was in (what was moved is kept).
            const double done = juce::jmax (0.0, lastPlayingBeat - recStart);
            recStop = recStart + juce::jmax (1.0, std::ceil (done / bpb - 1.0e-9)) * bpb;
            recState = (int) RecState::closing;
            recFinished = true;
        }
        else
        {
            if (closeRequested.exchange (false) && recStop < 0)
            {
                recStop = recStart + juce::jmax (1.0, std::ceil (juce::jmax (0.0, pos) / bpb - 1.0e-9)) * bpb;
                recState = (int) (state = RecState::closing);
            }
            if (recStop >= 0 && c.beat >= recStop - 1.0e-9)
            {
                recState = (int) RecState::closing;
                recFinished = true;
            }
        }
    }
    const bool writing = recording && ! recFinished.load() && pos >= 0;

    if (writing)
    {
        // Settings being recorded, and settings that just started moving (that no lane plays).
        std::array<int, kMaxMovesPerBlock + 1> moved {};
        int numMoved = 0;
        for (int i = 0; i < (int) watch.size(); ++i)
        {
            auto& w = watch[(size_t) i];
            if (w.slot >= 0)
            {
                recordInto (slots[(size_t) w.slot], pos, w.param->getValue());
                continue;
            }
            if (w.lane >= 0)
                continue;   // its lane notices (below)
            if (std::abs (w.param->getValue() - w.seen) > kMoved)
            {
                if (numMoved <= kMaxMovesPerBlock)
                    moved[(size_t) numMoved] = i;
                ++numMoved;
            }
        }
        if (numMoved > kMaxMovesPerBlock)
        {
            for (auto& w : watch)   // a new sound: everything starts from here
                if (w.slot < 0)
                    w.seen = w.param->getValue();
        }
        else
            for (int m = 0; m < numMoved; ++m)
            {
                auto& w = watch[(size_t) moved[(size_t) m]];
                const float v = w.param->getValue();
                startSlot (moved[(size_t) m], w.seen, juce::jmax (0.0, lastPlayingBeat - recStart), pos, v);
                w.seen = v;
            }
    }

    // Playing the lanes back.
    for (size_t i = 0; i < lanes.size() && i < live.size(); ++i)
    {
        auto& lane = lanes[i];
        auto& l = live[i];
        if (l.param == nullptr)
            continue;
        const float v = l.param->getValue();
        const bool touched = std::abs (v - l.seen) > kMoved;
        if (touched)
            l.heldSamples = holdSamples;
        // Moved by hand while recording: from now on the recording has it.
        const int w = l.watch;
        const bool beingRecorded = writing && w >= 0 && watch[(size_t) w].slot >= 0;
        if (writing && touched && w >= 0 && watch[(size_t) w].slot < 0)
        {
            startSlot (w, l.seen, juce::jmax (0.0, lastPlayingBeat - recStart), pos, v);
            l.seen = v;
            continue;
        }
        if (beingRecorded || ! c.playing || ! reading.load() || ! lane.on || lane.points.empty())
        {
            l.seen = v;
            continue;
        }
        if (l.heldSamples > 0)
        {
            l.heldSamples = juce::jmax (0, l.heldSamples - c.samples);
            l.seen = v;
            continue;
        }
        const double at = lane.positionAt (c.beat);
        if (at < 0)
        {
            l.seen = v;
            continue;
        }
        const float target = lane.valueAt (at);
        if (std::abs (target - v) > kMoved)
            l.param->setValueNotifyingHost (target);
        l.written = target;
        l.seen = l.param->getValue();
    }

    if (c.playing)
        lastPlayingBeat = c.beat;
}

//==============================================================================
juce::String RigAutomation::moveShapeName (int shape)
{
    switch (shape)
    {
        case moveSine:     return "Smooth wave";
        case moveTriangle: return "Triangle";
        case moveRampUp:   return "Rise";
        case moveRampDown: return "Fall";
        case moveSquare:   return "On and off";
        case moveRandom:   return "Random steps";
        default:           return {};
    }
}

RigAutomation::Lane RigAutomation::makeMove (int tool, int parameter, int shape, double beats, float centre, float depth, int seed)
{
    Lane lane;
    lane.tool = tool;
    lane.parameter = parameter;
    lane.startBeat = 0;   // on the bar grid
    lane.lengthBeats = juce::jmax (0.25, beats);
    lane.loop = true;
    lane.on = true;
    depth = juce::jlimit (0.0f, 0.5f, depth);
    float lo = centre - depth, hi = centre + depth;
    if (lo < 0.0f) { hi -= lo; lo = 0.0f; }
    if (hi > 1.0f) { lo -= hi - 1.0f; hi = 1.0f; }
    lo = juce::jlimit (0.0f, 1.0f, lo);
    hi = juce::jlimit (0.0f, 1.0f, hi);
    const double len = lane.lengthBeats;
    const double edge = juce::jmin (0.01, len / 200.0);   // how quick a jump is
    auto add = [&lane, len] (double beat, float v) { lane.points.push_back ({ juce::jlimit (0.0, len, beat), juce::jlimit (0.0f, 1.0f, v) }); };
    switch (shape)
    {
        case moveSquare:
            add (0, hi);
            add (len * 0.5 - edge, hi);
            add (len * 0.5, lo);
            add (len - edge, lo);
            add (len, hi);
            break;
        case moveRampUp:
            add (0, lo);
            add (len - edge, hi);
            add (len, lo);
            break;
        case moveRampDown:
            add (0, hi);
            add (len - edge, lo);
            add (len, hi);
            break;
        case moveRandom:
        {
            juce::Random random (seed);
            const int steps = juce::jlimit (4, 32, (int) std::round (len * 2.0));   // 8th notes
            float v = lo + (hi - lo) * random.nextFloat();
            const float first = v;
            for (int i = 0; i < steps; ++i)
            {
                const double at = len * i / steps;
                if (i > 0)
                {
                    add (at - edge, v);
                    v = lo + (hi - lo) * random.nextFloat();
                }
                add (at, v);
            }
            add (len - edge, v);
            add (len, first);
            break;
        }
        case moveTriangle:
        {
            const float mid = (lo + hi) * 0.5f;
            add (0, mid);
            add (len * 0.25, hi);
            add (len * 0.75, lo);
            add (len, mid);
            break;
        }
        default:   // a smooth wave, from the middle, up first
        {
            const float mid = (lo + hi) * 0.5f, swing = (hi - lo) * 0.5f;
            constexpr int kPoints = 48;
            for (int i = 0; i <= kPoints; ++i)
                add (len * i / kPoints, mid + swing * (float) std::sin (juce::MathConstants<double>::twoPi * i / kPoints));
            break;
        }
    }
    return lane;
}
