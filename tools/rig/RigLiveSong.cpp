#include "RigLiveSong.h"

#include "RigEngine.h"
#include "RigSong.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kEps = 1.0e-6;
constexpr double kRunUp = 2.0;        // bars a punch plays before its first bar
constexpr double kAhead = 4.0;        // an open block always reaches this far past now
constexpr double kAutoSection = 8.0;  // with no Mark pressed, a section every 8 bars

using Block = RigTimeline::Block;

double wrap (double x, double length)
{
    if (length <= kEps)
        return 0.0;
    double r = std::fmod (x, length);
    if (r < 0)
        r += length;
    return length - r < 1.0e-4 ? 0.0 : r;
}

// What a tool is playing now (its clip), and whether it's playing at all.
struct ToolNow
{
    int clip = -1;
    bool playing = false;
};

ToolNow toolNow (RigEngine& engine, const RigScene::State& s, int strip)
{
    switch (strip)
    {
        case RigEngine::mpcStrip:
        {
            auto& project = engine.getMpc().getProject();
            bool notes = false;
            if (juce::isPositiveAndBelow (s.mpcSequence, (int) project.getSequences().size()))
                for (const auto& t : project.getSequences()[(size_t) s.mpcSequence].tracks)
                    notes = notes || ! t.notes.empty();
            return { s.mpcSequence, notes };
        }
        case RigEngine::bassStrip:  return { s.bassPattern, s.bassSequencer == 1 };
        case RigEngine::drumsStrip: return { s.drumPattern, s.drumSequencer == 1 };
        case RigEngine::synthStrip: return { s.arp == 1 ? 1 : 0, s.arp == 1 };
        default:                    return {};
    }
}
}

RigLiveSong::RigLiveSong (RigEngine& e) : engine (e) {}

//==============================================================================
double RigLiveSong::nowBar() const
{
    auto& t = engine.getTransport();
    return t.getPositionInBeats() / juce::jmax (1, t.getBeatsPerBar());
}

double RigLiveSong::nextBar() const
{
    const double now = nowBar();
    return juce::jmax (startBar, std::floor (now + kEps) + 1.0);
}

double RigLiveSong::loopBars (int lane) const
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (! info.isLoop)
        return 0.0;
    return engine.getLooper().getLayer (info.index).lengthBeats.load() / juce::jmax (1, engine.getTransport().getBeatsPerBar());
}

double RigLiveSong::slipFor (int lane, double bar) const
{
    return wrap (bar - origin, loopBars (lane));
}

bool RigLiveSong::getMute (int lane) const
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    return info.isLoop ? engine.getLooper().getLayer (info.index).mute.load() : engine.getStrip (info.index).mute.load();
}

void RigLiveSong::setMute (int lane, bool m)
{
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    auto& flag = info.isLoop ? engine.getLooper().getLayer (info.index).mute : engine.getStrip (info.index).mute;
    if (flag.load() == m)
        return;
    engine.getDirty().runtimeField (info.isLoop ? RigDirty::loopMute : RigDirty::stripMute, info.index, flag.load() ? 1.0f : 0.0f,
                                    m ? 1.0f : 0.0f);
    flag = m;
}

int RigLiveSong::openBlock (int lane) const
{
    const auto& st = laneStates[(size_t) lane];
    if (! st.in)
        return -1;
    const auto& blocks = engine.getTimeline().getBlocks();
    for (int i = 0; i < (int) blocks.size(); ++i)
        if (blocks[(size_t) i].lane == lane && std::abs (blocks[(size_t) i].start - st.openStart) < kEps)
            return i;
    return -1;
}

std::vector<int> RigLiveSong::playableLanes() const
{
    std::vector<int> out;
    const auto state = engine.captureScene();
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        const auto& info = RigTimeline::lanes()[(size_t) lane];
        if (info.isLoop)
        {
            if (engine.getLooper().getLayer (info.index).lengthBeats.load() > 0)
                out.push_back (lane);
        }
        else if (info.index == RigEngine::mpcStrip || info.index == RigEngine::bassStrip
                 || info.index == RigEngine::drumsStrip || info.index == RigEngine::synthStrip)
        {
            // A tool is written when it plays by itself (its pattern or arp), so the keys you
            // play live on it never go quiet.
            if (toolNow (engine, state, info.index).playing || engine.getTimeline().laneUsed (lane))
                out.push_back (lane);
        }
    }
    return out;
}

bool RigLiveSong::isIn (int lane) const
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return false;
    if (! isActive())
        return ! getMute (lane);
    const auto& st = laneStates[(size_t) lane];
    return st.in && st.outAt < 0;
}

double RigLiveSong::changeAt (int lane) const
{
    if (! isActive() || ! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return -1;
    const auto& st = laneStates[(size_t) lane];
    if (st.outAt >= 0)
        return st.outAt;
    if (st.in && st.openStart > nowBar() + kEps)
        return st.openStart;
    return -1;
}

//==============================================================================
void RigLiveSong::bringIn (int lane, double bar)
{
    auto& st = laneStates[(size_t) lane];
    auto& tl = engine.getTimeline();
    st.outAt = -1;
    if (st.in)
    {
        // Going out at a bar still to come: it stays in (open again).
        if (const int i = openBlock (lane); i >= 0)
            tl.getBlocks()[(size_t) i].length = juce::jmax (tl.getBlocks()[(size_t) i].length, std::floor (nowBar()) + kAhead - st.openStart);
        ++changes;
        return;
    }
    if (endBar >= 0 && bar >= endBar - kEps)
        return;   // past the punch's end
    Block b;
    b.lane = lane;
    b.start = bar;
    b.length = juce::jmax (1.0, std::floor (nowBar()) + kAhead - bar);
    if (endBar >= 0)
        b.length = juce::jmax (0.25, juce::jmin (b.length, endBar - bar));
    b.offset = slipFor (lane, bar);
    const auto& info = RigTimeline::lanes()[(size_t) lane];
    if (! info.isLoop)
        b.clip = toolNow (engine, engine.captureScene(), info.index).clip;
    tl.getBlocks().push_back (b);
    st.in = true;
    st.openStart = bar;
    ++changes;
}

void RigLiveSong::takeOut (int lane, double bar)
{
    auto& st = laneStates[(size_t) lane];
    if (! st.in || st.outAt >= 0)
        return;
    auto& blocks = engine.getTimeline().getBlocks();
    const int i = openBlock (lane);
    ++changes;
    if (st.openStart >= bar - kEps || i < 0)
    {
        // It hadn't come in yet: as if it never did.
        if (i >= 0)
            blocks.erase (blocks.begin() + i);
        st.in = false;
        st.openStart = -1;
        setMute (lane, true);
        st.lastMute = true;
        return;
    }
    blocks[(size_t) i].length = bar - blocks[(size_t) i].start;
    st.outAt = bar;
    // It plays on to the bar (the block ends it exactly there).
    setMute (lane, false);
    st.lastMute = false;
}

void RigLiveSong::toggle (int lane)
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return;
    if (! isActive())
    {
        setMute (lane, ! getMute (lane));
        return;
    }
    if (std::find (lanes.begin(), lanes.end(), lane) == lanes.end())
    {
        // A loop recorded since it started: it joins in.
        lanes.push_back (lane);
        engine.getTimeline().gated = engine.getTimeline().gated.load() | RigTimeline::gateBit (lane);
        laneStates[(size_t) lane] = {};
        laneStates[(size_t) lane].lastMute = getMute (lane);
    }
    if (isIn (lane))
        takeOut (lane, nextBar());
    else
    {
        setMute (lane, false);
        laneStates[(size_t) lane].lastMute = false;
        bringIn (lane, nextBar());
    }
    engine.getTimeline().changed();
}

void RigLiveSong::mark()
{
    if (! isActive())
        return;
    auto& tl = engine.getTimeline();
    const double bar = nextBar();
    if (endBar >= 0 && bar >= endBar - kEps)
        return;
    auto& markers = tl.getMarkers();
    for (auto& m : markers)
        if (std::abs (m.bar - bar) < kEps)
            return;
    for (auto& m : markers)
        m.bars = 0;   // each runs to the next
    markers.push_back ({ bar, rigsong::suggestName (tl), 0.0, -1 });
    ++marks;
    tl.changed();
}

//==============================================================================
double RigLiveSong::phraseStart() const
{
    // The next bar where every loop that's playing is at its top (up to 16 bars away), so the
    // take begins on the loops' downbeat; else the next bar.
    auto& looper = engine.getLooper();
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double nowBeat = engine.getTransport().getPositionInBeats();
    const double first = std::floor (nowBar() + kEps) + 1.0;
    for (double bar = first; bar < first + 16.0; bar += 1.0)
    {
        bool top = true;
        for (int l = 0; l < RigLooper::kLayers && top; ++l)
        {
            const auto& layer = looper.getLayer (l);
            const double length = layer.lengthBeats.load();
            if (length <= 0 || layer.mute.load() || looper.getState (l) != RigLooper::State::playing)
                continue;
            const double loopOrigin = nowBeat - layer.position.load() * length;
            const double phase = wrap (bar * bpb - loopOrigin, length);
            top = phase < 0.1 || length - phase < 0.1;
        }
        if (top)
            return bar;
    }
    return first;
}

bool RigLiveSong::start (juce::String* why)
{
    auto& t = engine.getTransport();
    const bool playing = t.isPlaying();
    const double first = playing ? phraseStart() : (double) juce::jlimit (0, 2, countInBars);
    return begin (first, playing ? -1.0 : 0.0, true, why);
}

bool RigLiveSong::startPunch (double from, double to, juce::String* why)
{
    from = juce::jmax (0.0, std::round (from));
    if (to >= 0)
    {
        to = std::round (to);
        if (to <= from)
        {
            if (why != nullptr)
                *why = "Pick at least a bar to punch.";
            return false;
        }
    }
    endBar = to;
    return begin (from, juce::jmax (0.0, from - kRunUp), false, why);
}

bool RigLiveSong::startFromEnd (juce::String* why)
{
    const double end = std::ceil (juce::jmax (rigsong::songBars (engine.getTimeline()), engine.getTimeline().lengthBars()) - kEps);
    return startPunch (end, -1.0, why);
}

bool RigLiveSong::begin (double first, double runFrom, bool isFresh, juce::String* why)
{
    const auto fail = [&] (const juce::String& s) { if (why != nullptr) *why = s; endBar = -1; return false; };
    if (isActive())
        return fail ("Rec live is already on.");
    if (engine.getTakes().isActive())
        return fail ("Stop Rec into first.");
    if (engine.getTransport().isHosted())
        return fail ("Rec live runs on the Rig's own clock: use it in the app, not inside Logic.");
    lanes = playableLanes();
    if (lanes.empty())
        return fail ("Nothing to play yet: record a loop or start a pattern first.");

    auto& tl = engine.getTimeline();
    storeCurrent();
    beforeBlocks = tl.getBlocks();
    beforeMarkers = tl.getMarkers();
    beforeTake = current;
    fresh = isFresh;
    punch = ! isFresh;
    if (isFresh)
        endBar = -1;
    startBar = first;
    origin = isFresh ? first : 0.0;
    changes = marks = 0;
    for (auto& st : laneStates)
        st = {};

    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const bool playing = engine.getTransport().isPlaying();
    const auto scene = engine.captureScene();
    auto& blocks = tl.getBlocks();
    juce::uint32 mask = 0;
    for (const int lane : lanes)
        mask |= RigTimeline::gateBit (lane);

    if (isFresh)
    {
        blocks.clear();
        tl.getMarkers().clear();
        tl.cycle = false;
        // What's playing now is in from the first bar (while jamming, from now, carrying on
        // in its groove so nothing jumps).
        const double from = playing ? std::floor (nowBar() + kEps) : first;
        for (const int lane : lanes)
        {
            const auto& info = RigTimeline::lanes()[(size_t) lane];
            const bool on = ! getMute (lane) && (info.isLoop || toolNow (engine, scene, info.index).playing);
            if (! on)
                continue;
            Block b;
            b.lane = lane;
            b.start = from;
            b.length = first + kAhead - from;
            if (info.isLoop)
            {
                const auto& layer = engine.getLooper().getLayer (info.index);
                const double length = layer.lengthBeats.load();
                if (playing && length > 0)
                {
                    const double loopOrigin = engine.getTransport().getPositionInBeats() - layer.position.load() * length;
                    b.offset = wrap (from * bpb - std::round (loopOrigin * 4.0) / 4.0, length) / bpb;
                }
            }
            else
                b.clip = toolNow (engine, scene, info.index).clip;
            blocks.push_back (b);
            laneStates[(size_t) lane].in = true;
            laneStates[(size_t) lane].openStart = from;
        }
        tl.getMarkers().push_back ({ first, rigsong::suggestName (tl), 0.0, -1 });
    }
    else
    {
        // Punch: the song's blocks on these lanes go from `first` to `endBar`; one that plays
        // across `first` carries on (open) until you take it out.
        std::vector<Block> kept;
        for (const auto& b : blocks)
        {
            if ((mask & RigTimeline::gateBit (b.lane)) == 0 || b.end() <= first + kEps || (endBar >= 0 && b.start >= endBar - kEps))
            {
                kept.push_back (b);
                continue;
            }
            if (endBar >= 0 && b.end() > endBar + kEps)
            {
                auto after = b;
                after.start = endBar;
                after.length = b.end() - endBar;
                after.fadeIn = 0;
                after.offset = wrap (b.offset + (endBar - b.start), loopBars (b.lane));
                kept.push_back (after);
            }
            if (b.start < first - kEps || std::abs (b.start - first) < kEps)
            {
                auto open = b;
                open.length = juce::jmax (b.length, first + kAhead - b.start);
                if (endBar >= 0)
                    open.length = juce::jmin (open.length, endBar - b.start);
                open.fadeOut = 0;
                if (! laneStates[(size_t) b.lane].in)
                {
                    laneStates[(size_t) b.lane].in = true;
                    laneStates[(size_t) b.lane].openStart = open.start;
                    kept.push_back (open);
                }
            }
        }
        blocks = kept;
        mask = 0;
        for (const int lane : lanes)
            mask |= RigTimeline::gateBit (lane);
    }

    tl.gated = mask;
    tl.changed();

    if (isFresh && playing)
    {
        engine.getDirty().runtimeField (RigDirty::timelineOn, 0, tl.on.load() ? 1.0f : 0.0f, 1.0f);
        tl.on = true;
    }
    else
    {
        engine.playTimeline (runFrom);
        if (isFresh && first > 0)
            engine.songCountInUntil = first * bpb;
    }
    // In lanes sound (the blocks hold them back until their bar); out lanes go quiet at the
    // first bar (in a punch they may still be playing the run-up).
    for (const int lane : lanes)
    {
        auto& st = laneStates[(size_t) lane];
        if (st.in)
            setMute (lane, false);
        else if (! getMute (lane))
        {
            if (punch && first > nowBar() + kEps)
                st.outAt = first;
            else
                setMute (lane, true);
        }
        st.lastMute = getMute (lane);
    }
    state = State::waiting;
    runFromBar = runFrom >= 0 ? runFrom : nowBar();
    arrived = runFrom < 0;
    waitTicks = 0;
    lastNow = arrived ? nowBar() : runFromBar;
    lastMessage = {};
    return true;
}

//==============================================================================
void RigLiveSong::extendOpen()
{
    auto& blocks = engine.getTimeline().getBlocks();
    const double reach = std::floor (nowBar()) + kAhead;
    bool moved = false;
    for (const int lane : lanes)
    {
        const auto& st = laneStates[(size_t) lane];
        if (! st.in || st.outAt >= 0)
            continue;
        const int i = openBlock (lane);
        if (i < 0)
            continue;
        auto& b = blocks[(size_t) i];
        double end = juce::jmax (b.end(), reach);
        if (endBar >= 0)
            end = juce::jmin (end, endBar);
        if (end > b.end() + kEps)
        {
            b.length = end - b.start;
            moved = true;
        }
    }
    if (moved)
        engine.getTimeline().changed();
}

void RigLiveSong::tick()
{
    if (! isActive())
        return;
    auto& t = engine.getTransport();
    if (! t.isPlaying() && ! arrived && ++waitTicks < 60)
        return;   // (the clock starts at its next block)
    if (! t.isPlaying())
    {
        // The clock stopped (Stop, or the space bar): what was played is kept.
        finish (lastNow > startBar + 0.25);
        return;
    }
    const double now = nowBar();
    if (! arrived)
    {
        // Wait for the clock to get to where the run plays from (it moves at its next block,
        // or a punch while playing at the next bar line).
        if (std::abs (now - runFromBar) > 1.0 && now > startBar + kEps)
            return;
        arrived = true;
        lastNow = now;
    }
    if (now + 2.0 < lastNow)
    {
        // The clock jumped back (a section key, the ruler): that ends it.
        finish (lastNow > startBar + 0.25);
        return;
    }
    lastNow = now;
    if (now >= startBar - kEps)
    {
        state = State::recording;
        engine.songCountInUntil = -1.0;
    }

    bool edited = false;
    // Changes from anywhere (a layer's M, the mixer, the Launchkey).
    for (const int lane : lanes)
    {
        auto& st = laneStates[(size_t) lane];
        const bool m = getMute (lane);
        if (m == st.lastMute)
            continue;
        st.lastMute = m;
        edited = true;
        if (! m)
            bringIn (lane, nextBar());
        else if (st.in && st.outAt < 0)
            takeOut (lane, nextBar());
        else if (st.outAt >= 0)
        {
            // Already going out on the bar: it plays on to it.
            setMute (lane, false);
            st.lastMute = false;
        }
    }
    // A tool that's in and changed its pattern: a new block from the next bar.
    const auto scene = engine.captureScene();
    for (const int lane : lanes)
    {
        auto& st = laneStates[(size_t) lane];
        const auto& info = RigTimeline::lanes()[(size_t) lane];
        if (info.isLoop || ! st.in || st.outAt >= 0)
            continue;
        const int i = openBlock (lane);
        const auto tool = toolNow (engine, scene, info.index);
        if (i < 0 || tool.clip < 0 || tool.clip == engine.getTimeline().getBlocks()[(size_t) i].clip)
            continue;
        const double bar = nextBar();
        auto& blocks = engine.getTimeline().getBlocks();
        if (blocks[(size_t) i].start >= bar - kEps)
            blocks[(size_t) i].clip = tool.clip;
        else
        {
            blocks[(size_t) i].length = bar - blocks[(size_t) i].start;
            st.in = false;
            bringIn (lane, bar);
        }
        edited = true;
    }
    // Lanes whose bar to go out has come.
    for (const int lane : lanes)
    {
        auto& st = laneStates[(size_t) lane];
        if (st.outAt >= 0 && now >= st.outAt - kEps)
        {
            st.in = false;
            st.outAt = -1;
            st.openStart = -1;
            setMute (lane, true);
            st.lastMute = true;
        }
    }
    if (edited)
        engine.getTimeline().changed();
    extendOpen();

    if (endBar >= 0 && now >= endBar - kEps)
        finish (true);
}

void RigLiveSong::finish (bool keep)
{
    auto& tl = engine.getTimeline();
    auto& blocks = tl.getBlocks();
    const double end = endBar >= 0 ? endBar : std::ceil (lastNow - 1.0e-3);
    const bool stopClock = endBar < 0;
    if (keep && end <= startBar + kEps)
        keep = false;

    tl.gated = 0;
    engine.songCountInUntil = -1.0;
    state = State::idle;

    if (! keep)
    {
        blocks = beforeBlocks;
        tl.getMarkers() = beforeMarkers;
        current = beforeTake;
        tl.changed();
        lastMessage = changes == 0 && lastNow <= startBar + 0.25 ? "Rec live called off." : "Rec live thrown away: the song is as it was.";
        if (stopClock && engine.getTransport().isPlaying())
            engine.getTransport().stop();
        punch = fresh = false;
        endBar = -1;
        return;
    }

    // Close what's still open at the end, and drop what hadn't come in yet. (A punch with an
    // end keeps the song after it.)
    std::vector<Block> kept;
    std::array<bool, RigTimeline::kLanes> inAtEnd {};
    for (auto b : blocks)
    {
        const bool live = std::find (lanes.begin(), lanes.end(), b.lane) != lanes.end();
        if (live && (fresh || endBar < 0))
        {
            if (b.start >= end - kEps)
                continue;
            if (b.end() > end + kEps)
                b.length = end - b.start;
        }
        if (live && b.start < end - kEps && b.end() >= end - kEps)
            inAtEnd[(size_t) b.lane] = true;
        kept.push_back (b);
    }
    blocks = kept;

    auto& markers = tl.getMarkers();
    double length = end;
    if (fresh)
    {
        // The song starts at its first bar.
        std::vector<Block> moved;
        for (auto b : blocks)
        {
            if (b.end() <= startBar + kEps)
                continue;
            if (b.start < startBar)
            {
                b.offset = wrap (b.offset + (startBar - b.start), loopBars (b.lane));
                b.length = b.end() - startBar;
                b.start = startBar;
            }
            b.start -= startBar;
            moved.push_back (b);
        }
        blocks = moved;
        for (auto& m : markers)
            m.bar = juce::jmax (0.0, m.bar - startBar);
        length = end - startBar;
        if (marks == 0)
        {
            markers.clear();
            for (double bar = 0; bar < length - kEps; bar += kAutoSection)
            {
                markers.push_back ({ bar, rigsong::suggestName (tl), 0.0, -1 });
                tl.changed();
            }
        }
    }
    std::stable_sort (markers.begin(), markers.end(), [] (const auto& a, const auto& b) { return a.bar < b.bar; });
    for (auto& m : markers)
        m.bars = 0;
    if (! markers.empty() && (fresh || endBar < 0))
        markers.back().bars = juce::jmax (1.0, length - markers.back().bar);
    else if (! markers.empty() && ! beforeMarkers.empty())
        markers.back().bars = beforeMarkers.back().bars > 0 ? juce::jmax (1.0, beforeMarkers.back().bar + beforeMarkers.back().bars - markers.back().bar) : 0.0;
    tl.changed();

    // The mixer as the run ends. When the clock stops, what was in plays on (to jam on), the
    // rest is muted; when the song plays on (a punch), it brings everything in and out itself.
    for (const int lane : lanes)
        setMute (lane, stopClock ? ! inAtEnd[(size_t) lane] : false);

    // Keep the song from before as a take too, so nothing is lost.
    if (beforeTake < 0 && ! beforeBlocks.empty())
    {
        takes.insert (takes.begin(), Take { "Song before Rec live", beforeBlocks, beforeMarkers });
    }
    Take take;
    take.name = "Take " + juce::String ((int) takes.size() + 1);
    if (punch)
        take.name << (endBar >= 0 ? " (bars " + juce::String ((int) startBar + 1) + "-" + juce::String ((int) endBar) + " again)"
                                  : " (on from bar " + juce::String ((int) startBar + 1) + ")");
    take.blocks = blocks;
    take.markers = markers;
    takes.push_back (take);
    current = (int) takes.size() - 1;

    lastMessage = take.name + " kept: " + rigsong::barsText (length) + ", " + juce::String (changes) + (changes == 1 ? " change." : " changes.");
    if (stopClock && engine.getTransport().isPlaying())
        engine.getTransport().stop();
    punch = fresh = false;
    endBar = -1;
}

int RigLiveSong::stopAndKeep()
{
    if (! isActive())
        return -1;
    lastNow = juce::jmax (lastNow, nowBar());
    finish (lastNow > startBar + 0.25);
    return current;
}

void RigLiveSong::cancel()
{
    if (isActive())
        finish (false);
}

//==============================================================================
void RigLiveSong::storeCurrent()
{
    if (isActive() || ! juce::isPositiveAndBelow (current, (int) takes.size()))
        return;
    takes[(size_t) current].blocks = engine.getTimeline().getBlocks();
    takes[(size_t) current].markers = engine.getTimeline().getMarkers();
}

bool RigLiveSong::useTake (int i)
{
    if (isActive() || ! juce::isPositiveAndBelow (i, (int) takes.size()))
        return false;
    storeCurrent();
    auto& tl = engine.getTimeline();
    tl.getBlocks() = takes[(size_t) i].blocks;
    tl.getMarkers() = takes[(size_t) i].markers;
    current = i;
    tl.changed();
    return true;
}

bool RigLiveSong::deleteTake (int i)
{
    if (isActive() || ! juce::isPositiveAndBelow (i, (int) takes.size()))
        return false;
    takes.erase (takes.begin() + i);
    if (current == i)
        current = -1;
    else if (current > i)
        --current;
    engine.getTimeline().changed();
    return true;
}

void RigLiveSong::clear()
{
    if (isActive())
    {
        engine.getTimeline().gated = 0;
        engine.songCountInUntil = -1.0;
        state = State::idle;
    }
    takes.clear();
    current = -1;
    punch = fresh = false;
    endBar = -1;
}

juce::ValueTree RigLiveSong::toTree() const
{
    juce::ValueTree tree (kTreeType);
    tree.setProperty ("current", current, nullptr);
    tree.setProperty ("countIn", countInBars, nullptr);
    for (int i = 0; i < (int) takes.size(); ++i)
    {
        const auto& take = takes[(size_t) i];
        // The take on the timeline is as it is now (with its edits).
        const bool onTimeline = i == current && ! isActive();
        RigTimeline holder;
        holder.getBlocks() = onTimeline ? engine.getTimeline().getBlocks() : take.blocks;
        holder.getMarkers() = onTimeline ? engine.getTimeline().getMarkers() : take.markers;
        juce::ValueTree node ("Take");
        node.setProperty ("name", take.name, nullptr);
        node.appendChild (holder.toTree(), nullptr);
        tree.appendChild (node, nullptr);
    }
    return tree;
}

void RigLiveSong::fromTree (const juce::ValueTree& tree)
{
    clear();
    if (! tree.hasType (kTreeType))
        return;
    countInBars = juce::jlimit (0, 2, (int) tree.getProperty ("countIn", 1));
    for (const auto& node : tree)
    {
        if (! node.hasType ("Take"))
            continue;
        RigTimeline holder;
        holder.fromTree (node.getChildWithName (RigTimeline::kTreeType));
        takes.push_back ({ node.getProperty ("name").toString(), holder.getBlocks(), holder.getMarkers() });
    }
    const int c = tree.getProperty ("current", -1);
    current = juce::isPositiveAndBelow (c, (int) takes.size()) ? c : -1;
}
