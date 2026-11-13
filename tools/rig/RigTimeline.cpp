#include "RigTimeline.h"

#include "RigEngine.h"

#include <algorithm>

namespace
{
constexpr double kEpsilon = 1.0e-9;
constexpr double kMinLength = 1.0 / 64.0;   // bars
}

const std::array<RigTimeline::Lane, RigTimeline::kLanes>& RigTimeline::lanes()
{
    static const std::array<Lane, kLanes> list {
        Lane { true, 0 }, Lane { true, 1 }, Lane { true, 2 }, Lane { true, 3 },
        Lane { true, 4 }, Lane { true, 5 }, Lane { true, 6 }, Lane { true, 7 },
        Lane { false, RigEngine::mpcStrip }, Lane { false, RigEngine::bassStrip }, Lane { false, RigEngine::synthStrip },
        Lane { false, RigEngine::vocoderStrip }, Lane { false, RigEngine::samplerStrip }, Lane { false, RigEngine::inputStrip },
        Lane { false, RigEngine::drumsStrip }
    };
    return list;
}

int RigTimeline::laneForStrip (int strip)
{
    const auto& list = lanes();
    for (int i = 0; i < kLanes; ++i)
        if (! list[(size_t) i].isLoop && list[(size_t) i].index == strip)
            return i;
    return -1;
}

//==============================================================================
void RigTimeline::changed()
{
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(), [] (const Block& b) { return ! juce::isPositiveAndBelow (b.lane, kLanes); }),
                  blocks.end());
    if ((int) blocks.size() > kMaxBlocks)
        blocks.resize ((size_t) kMaxBlocks);
    for (auto& b : blocks)
    {
        b.start = juce::jmax (0.0, b.start);
        b.length = juce::jmax (kMinLength, b.length);
        b.fadeIn = juce::jlimit (0.0, b.length, b.fadeIn);
        b.fadeOut = juce::jlimit (0.0, b.length - b.fadeIn, b.fadeOut);
        b.level = juce::jlimit (0.0f, 1.0f, b.level);
    }
    std::stable_sort (blocks.begin(), blocks.end(), [] (const Block& a, const Block& b) { return a.start < b.start; });
    std::stable_sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.bar < b.bar; });

    const juce::SpinLock::ScopedLockType sl (lock);
    fill (pending, blocks);
    // The song lasts to the end of its last section, even when that's quiet (an empty outro).
    if (! markers.empty() && markers.back().bars > 0)
        pending.end = juce::jmax (pending.end, markers.back().bar + markers.back().bars);
    ++pendingVersion;
}

void RigTimeline::clear()
{
    blocks.clear();
    markers.clear();
    cycle = false;
    changed();
}

void RigTimeline::fill (Snapshot& s, const std::vector<Block>& list)
{
    s.count = juce::jmin (kMaxBlocks, (int) list.size());
    s.used.fill (false);
    s.end = 0;
    for (int i = 0; i < s.count; ++i)
    {
        s.blocks[(size_t) i] = list[(size_t) i];
        s.used[(size_t) list[(size_t) i].lane] = true;
        s.end = juce::jmax (s.end, list[(size_t) i].end());
    }
}

double RigTimeline::lengthBars() const
{
    double end = 0;
    for (const auto& b : blocks)
        end = juce::jmax (end, b.end());
    return end;
}

bool RigTimeline::laneUsed (int lane) const
{
    return std::any_of (blocks.begin(), blocks.end(), [lane] (const Block& b) { return b.lane == lane; });
}

int RigTimeline::blockAt (int lane, double bar) const
{
    int found = -1;
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        const auto& b = blocks[(size_t) i];
        if (b.lane == lane && bar >= b.start - kEpsilon && bar < b.end() - kEpsilon)
            found = i;   // the last one that starts there wins (they're sorted by start)
    }
    return found;
}

float RigTimeline::gainAt (const Block* list, int count, int lane, double bar, bool used)
{
    if (! used)
        return 1.0f;
    double gain = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& b = list[i];
        if (b.lane != lane)
            continue;
        const double x = bar - b.start;
        if (x < -kEpsilon || x >= b.length - kEpsilon)
            continue;
        double g = 1.0;
        if (b.fadeIn > kEpsilon && x < b.fadeIn)
            g = juce::jmax (0.0, x) / b.fadeIn;
        if (b.fadeOut > kEpsilon && b.length - x < b.fadeOut)
            g = juce::jmin (g, (b.length - x) / b.fadeOut);
        // An equal-power feel: a fade sounds even, not as if it drops out at the end.
        gain = juce::jmax (gain, std::sin (g * juce::MathConstants<double>::halfPi) * (double) b.level);
    }
    return (float) gain;
}

double RigTimeline::blockStartAt (const Block* list, int count, int lane, double bar)
{
    double start = -1;
    for (int i = 0; i < count; ++i)
    {
        const auto& b = list[i];
        if (b.lane == lane && bar >= b.start - kEpsilon && bar < b.end() - kEpsilon)
            start = juce::jmax (start, b.start);
    }
    return start;
}

//==============================================================================
juce::ValueTree RigTimeline::toTree() const
{
    juce::ValueTree tree (kTreeType);
    tree.setProperty ("on", on.load(), nullptr);
    tree.setProperty ("cycle", cycle.load(), nullptr);
    tree.setProperty ("cycleStart", cycleStart.load(), nullptr);
    tree.setProperty ("cycleEnd", cycleEnd.load(), nullptr);
    tree.setProperty ("stopAtEnd", stopAtEnd.load(), nullptr);
    for (const auto& b : blocks)
    {
        juce::ValueTree node ("Block");
        node.setProperty ("lane", b.lane, nullptr);
        node.setProperty ("start", b.start, nullptr);
        node.setProperty ("length", b.length, nullptr);
        node.setProperty ("fadeIn", b.fadeIn, nullptr);
        node.setProperty ("fadeOut", b.fadeOut, nullptr);
        if (b.clip >= 0)
            node.setProperty ("clip", b.clip, nullptr);
        if (b.level < 1.0f)
            node.setProperty ("level", b.level, nullptr);
        tree.appendChild (node, nullptr);
    }
    for (const auto& m : markers)
    {
        juce::ValueTree node ("Marker");
        node.setProperty ("bar", m.bar, nullptr);
        node.setProperty ("name", m.name, nullptr);
        if (m.bars > 0)
            node.setProperty ("bars", m.bars, nullptr);
        if (m.loopBank >= 0)
            node.setProperty ("loopBank", m.loopBank, nullptr);
        tree.appendChild (node, nullptr);
    }
    return tree;
}

void RigTimeline::fromTree (const juce::ValueTree& tree)
{
    blocks.clear();
    markers.clear();
    if (tree.hasType (kTreeType))
    {
        on = (bool) tree.getProperty ("on", false);
        cycle = (bool) tree.getProperty ("cycle", false);
        cycleStart = (double) tree.getProperty ("cycleStart", 0.0);
        cycleEnd = (double) tree.getProperty ("cycleEnd", 4.0);
        stopAtEnd = (bool) tree.getProperty ("stopAtEnd", true);
        for (const auto& node : tree)
        {
            if (node.hasType ("Block"))
            {
                Block b;
                b.lane = node.getProperty ("lane", 0);
                b.start = node.getProperty ("start", 0.0);
                b.length = node.getProperty ("length", 4.0);
                b.fadeIn = node.getProperty ("fadeIn", 0.0);
                b.fadeOut = node.getProperty ("fadeOut", 0.0);
                b.clip = node.getProperty ("clip", -1);
                b.level = (float) (double) node.getProperty ("level", 1.0);
                blocks.push_back (b);
            }
            else if (node.hasType ("Marker"))
            {
                markers.push_back ({ (double) node.getProperty ("bar", 0.0), node.getProperty ("name").toString(),
                                     (double) node.getProperty ("bars", 0.0), (int) node.getProperty ("loopBank", -1) });
            }
        }
    }
    else
    {
        on = false;
        cycle = false;
    }
    changed();
}

//==============================================================================
void RigTimeline::update()
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (sl.isLocked() && pendingVersion != liveVersion)
    {
        live = pending;
        liveVersion = pendingVersion;
    }
}

double RigTimeline::takeJump (double beat, int beatsPerBar)
{
    const double bpb = juce::jmax (1, beatsPerBar);
    if (const double request = jumpRequest.exchange (-1.0); request >= 0)
    {
        jumpTarget = request;
        jumpAtBeat = std::ceil (beat / bpb - kEpsilon) * bpb;
    }
    if (jumpAtBeat >= 0 && beat >= jumpAtBeat - kEpsilon)
    {
        const double target = jumpTarget.exchange (-1.0) * bpb;
        jumpAtBeat = -1;
        return target;
    }
    if (cycle.load())
    {
        const double from = cycleStart.load() * bpb, to = cycleEnd.load() * bpb;
        // Only once it gets there from inside the cycle (playing from later on carries on).
        if (to > from + kEpsilon && beat >= to - kEpsilon && beat < to + 0.25)
            return from;
    }
    return -1.0;
}

double RigTimeline::beatsToNextEvent (double beat, int beatsPerBar) const
{
    const double bpb = juce::jmax (1, beatsPerBar);
    double next = 1.0e9;
    if (jumpAtBeat >= 0 && jumpAtBeat > beat + kEpsilon)
        next = jumpAtBeat - beat;
    if (cycle.load())
    {
        const double from = cycleStart.load() * bpb, to = cycleEnd.load() * bpb;
        if (to > from + kEpsilon && beat < to - kEpsilon)
            next = juce::jmin (next, to - beat);
    }
    return next;
}
