#include "PocketSong.h"

#include <algorithm>
#include <cmath>

namespace pocket
{
namespace
{
constexpr double kEps = 1.0e-6;
constexpr double kRunUp = 2.0;        // bars a punch plays before its first bar
constexpr double kAhead = 4.0;        // an open block always reaches this far past now
constexpr double kAutoSection = 8.0;  // with no MARK pressed, a section every 8 bars
constexpr double kNone = -1.0e9;

juce::String barsText (double bars)
{
    const int b = (int) std::round (bars);
    return juce::String (b) + (b == 1 ? " bar" : " bars");
}
}

double PocketSong::wrap (double x, double length)
{
    if (length <= kEps)
        return 0.0;
    double r = std::fmod (x, length);
    if (r < 0)
        r += length;
    return length - r < 1.0e-4 ? 0.0 : r;
}

//==============================================================================
void PocketSong::changed()
{
    if ((int) blocks.size() > kMaxBlocks)
        blocks.resize ((size_t) kMaxBlocks);
    for (auto& b : blocks)
    {
        b.layer = juce::jlimit (0, kLayers - 1, b.layer);
        b.start = juce::jmax (0.0, b.start);
        b.length = juce::jmax (0.25, b.length);
        b.fadeIn = juce::jlimit (0.0, b.length, b.fadeIn);
        b.fadeOut = juce::jlimit (0.0, b.length - b.fadeIn, b.fadeOut);
        b.level = juce::jlimit (0.0f, 1.0f, b.level);
        b.offset = juce::jmax (0.0, b.offset);
    }
    std::stable_sort (markers.begin(), markers.end(), [] (const auto& a, const auto& b) { return a.bar < b.bar; });
    ++editCount;
    publish();
}

void PocketSong::publish()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    pending.count = juce::jmin (kMaxBlocks, (int) blocks.size());
    pending.used = {};
    for (int i = 0; i < pending.count; ++i)
    {
        pending.blocks[(size_t) i] = blocks[(size_t) i];
        pending.used[(size_t) blocks[(size_t) i].layer] = true;
    }
    pending.on = on.load();
    pending.gated = gated.load();
    pending.startBar = startBar.load();
    ++pendingVersion;
}

double PocketSong::lengthBars() const
{
    double end = 0;
    for (const auto& b : blocks)
        end = juce::jmax (end, b.end());
    return end;
}

bool PocketSong::layerUsed (int layer) const
{
    for (const auto& b : blocks)
        if (b.layer == layer)
            return true;
    return false;
}

int PocketSong::blockAt (int layer, double bar) const
{
    for (int i = (int) blocks.size(); --i >= 0;)
    {
        const auto& b = blocks[(size_t) i];
        if (b.layer == layer && bar >= b.start - kEps && bar < b.end() - kEps)
            return i;
    }
    return -1;
}

std::vector<PocketSong::Section> PocketSong::sections() const
{
    std::vector<Section> out;
    const double end = juce::jmax (1.0, std::ceil (lengthBars() - kEps));
    if (markers.empty())
    {
        out.push_back ({ "Song", 0.0, end });
        return out;
    }
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const double a = markers[i].bar;
        const double b = i + 1 < markers.size() ? markers[i + 1].bar : juce::jmax (end, a + 1.0);
        out.push_back ({ markers[i].name, a, b });
    }
    return out;
}

juce::String PocketSong::suggestName() const
{
    static const char* names[] = { "Intro", "Build", "Drop", "Break", "Ritual", "Rise", "Peak", "Outro" };
    const int n = (int) markers.size();
    juce::String base = names[n % 8];
    if (n >= 8)
        base << " " << (n / 8 + 1);
    return base;
}

double PocketSong::loopBars (int layer) const
{
    return host.loopBeats ? host.loopBeats (layer) / kBeatsPerBar : 0.0;
}

bool PocketSong::split (int i, double bar)
{
    if (! juce::isPositiveAndBelow (i, (int) blocks.size()) || (int) blocks.size() >= kMaxBlocks)
        return false;
    const auto b = blocks[(size_t) i];
    if (bar <= b.start + kEps || bar >= b.end() - kEps)
        return false;
    auto first = b, second = b;
    first.length = bar - b.start;
    first.fadeOut = 0;
    second.start = bar;
    second.length = b.end() - bar;
    second.fadeIn = 0;
    second.offset = wrap (b.offset + (bar - b.start), loopBars (b.layer));
    blocks[(size_t) i] = first;
    blocks.insert (blocks.begin() + i + 1, second);
    changed();
    return true;
}

void PocketSong::trimStart (int i, double newStart)
{
    if (! juce::isPositiveAndBelow (i, (int) blocks.size()))
        return;
    auto& b = blocks[(size_t) i];
    newStart = juce::jlimit (0.0, b.end() - 0.25, newStart);
    // The sound stays where it was in time: the loop carries on from further in (or earlier).
    b.offset = wrap (b.offset + (newStart - b.start), loopBars (b.layer));
    b.length = b.end() - newStart;
    b.start = newStart;
    changed();
}

void PocketSong::deleteBlock (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) blocks.size()))
        return;
    blocks.erase (blocks.begin() + i);
    changed();
}

int PocketSong::copyBlock (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) blocks.size()) || (int) blocks.size() >= kMaxBlocks)
        return -1;
    auto b = blocks[(size_t) i];
    b.start = b.end();
    blocks.insert (blocks.begin() + i + 1, b);
    changed();
    return i + 1;
}

//==============================================================================
void PocketSong::play (double fromBar)
{
    fromBar = juce::jmax (0.0, std::floor (fromBar + kEps));
    if (host.clockRunning())
    {
        // The clock counts from 0 only from a stop: stop it, and tick() starts it once it has.
        host.setClock (false);
        on = false;
        publish();
        pendingFrom = fromBar;
        pendingTicks = 0;
        return;
    }
    pendingFrom = -1;
    startBar = fromBar;
    on = true;
    seenRunning = false;
    publish();
    host.setClock (true);
}

void PocketSong::stopPlaying()
{
    pendingFrom = -1;
    if (isLive())
        finish (lastNow > startBarLive + 0.25);
    const bool was = on.load();
    on = false;
    gated = 0;
    publish();
    if (was && host.clockRunning && host.clockRunning())
        host.setClock (false);
    startBar = 0;
}

//==============================================================================
double PocketSong::nextBar() const
{
    return juce::jmax (startBarLive, std::floor (barNow() + kEps) + 1.0);
}

int PocketSong::openBlock (int layer) const
{
    const auto& st = lanes[(size_t) layer];
    if (! st.in)
        return -1;
    for (int i = 0; i < (int) blocks.size(); ++i)
        if (blocks[(size_t) i].layer == layer && std::abs (blocks[(size_t) i].start - st.openStart) < kEps)
            return i;
    return -1;
}

bool PocketSong::isIn (int layer) const
{
    if (! juce::isPositiveAndBelow (layer, kLayers))
        return false;
    if (! isLive())
        return host.isMuted && ! host.isMuted (layer);
    const auto& st = lanes[(size_t) layer];
    return st.in && st.outAt < 0;
}

double PocketSong::changeAt (int layer) const
{
    if (! isLive() || ! juce::isPositiveAndBelow (layer, kLayers))
        return -1;
    const auto& st = lanes[(size_t) layer];
    if (st.outAt >= 0)
        return st.outAt;
    if (st.in && st.openStart > barNow() + kEps)
        return st.openStart;
    return -1;
}

void PocketSong::bringIn (int layer, double bar)
{
    auto& st = lanes[(size_t) layer];
    st.outAt = -1;
    if (st.in)
    {
        if (const int i = openBlock (layer); i >= 0)
            blocks[(size_t) i].length = juce::jmax (blocks[(size_t) i].length, std::floor (barNow()) + kAhead - st.openStart);
        ++changeCount;
        return;
    }
    if (endBar >= 0 && bar >= endBar - kEps)
        return;
    SongBlock b;
    b.layer = layer;
    b.start = bar;
    b.length = juce::jmax (1.0, std::floor (barNow()) + kAhead - bar);
    if (endBar >= 0)
        b.length = juce::jmax (0.25, juce::jmin (b.length, endBar - bar));
    b.offset = wrap (bar - origin, loopBars (layer));   // keeps its groove against the take's first bar
    blocks.push_back (b);
    st.in = true;
    st.openStart = bar;
    ++changeCount;
}

void PocketSong::takeOut (int layer, double bar)
{
    auto& st = lanes[(size_t) layer];
    if (! st.in || st.outAt >= 0)
        return;
    const int i = openBlock (layer);
    ++changeCount;
    if (st.openStart >= bar - kEps || i < 0)
    {
        if (i >= 0)
            blocks.erase (blocks.begin() + i);
        st.in = false;
        st.openStart = -1;
        host.setMute (layer, true);
        st.lastMute = true;
        return;
    }
    blocks[(size_t) i].length = bar - blocks[(size_t) i].start;
    st.outAt = bar;
    host.setMute (layer, false);   // plays on to the bar
    st.lastMute = false;
}

void PocketSong::toggle (int layer)
{
    if (! juce::isPositiveAndBelow (layer, kLayers))
        return;
    if (! isLive())
    {
        host.setMute (layer, ! host.isMuted (layer));
        return;
    }
    if (std::find (liveLayers.begin(), liveLayers.end(), layer) == liveLayers.end())
    {
        if (! host.hasLoop (layer))
            return;
        liveLayers.push_back (layer);
        gated = gated.load() | (1u << (juce::uint32) layer);
        lanes[(size_t) layer] = {};
        lanes[(size_t) layer].lastMute = host.isMuted (layer);
    }
    if (isIn (layer))
        takeOut (layer, nextBar());
    else
    {
        host.setMute (layer, false);
        lanes[(size_t) layer].lastMute = false;
        bringIn (layer, nextBar());
    }
    changed();
}

void PocketSong::mark()
{
    if (! isLive())
        return;
    const double bar = nextBar();
    if (endBar >= 0 && bar >= endBar - kEps)
        return;
    for (auto& m : markers)
        if (std::abs (m.bar - bar) < kEps)
            return;
    markers.push_back ({ bar, suggestName() });
    ++marks;
    changed();
}

//==============================================================================
bool PocketSong::startLive (juce::String* why)
{
    const bool playing = host.clockRunning();
    double first = juce::jlimit (0, 2, countInBars);
    if (playing)
    {
        // The next bar where the loops playing are at their top (so the take starts on their downbeat).
        const double base = std::floor (barNow() + kEps) + 1.0;
        first = base;
        for (double bar = base; bar < base + 16.0; bar += 1.0)
        {
            bool top = true;
            for (int l = 0; l < kLayers && top; ++l)
            {
                if (! host.hasLoop (l) || host.isMuted (l))
                    continue;
                const double length = host.loopBeats (l);
                const double phase = wrap ((bar - startBar.load()) * kBeatsPerBar - host.loopStart (l), length);
                top = phase < 0.1 || length - phase < 0.1;
            }
            if (top)
            {
                first = bar;
                break;
            }
        }
    }
    return begin (first, playing ? -1.0 : 0.0, true, why);
}

bool PocketSong::startPunch (double from, double to, juce::String* why)
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

bool PocketSong::begin (double first, double runFrom, bool isFresh, juce::String* why)
{
    const auto fail = [&] (const juce::String& s) { if (why != nullptr) *why = s; endBar = -1; return false; };
    if (isLive())
        return fail ("REC LIVE is already on.");
    liveLayers.clear();
    for (int l = 0; l < kLayers; ++l)
        if (host.hasLoop (l))
            liveLayers.push_back (l);
    if (liveLayers.empty())
        return fail ("Record a loop first: REC LIVE brings your loops in and out.");

    storeCurrent();
    beforeBlocks = blocks;
    beforeMarkers = markers;
    beforeTake = current;
    fresh = isFresh;
    punch = ! isFresh;
    if (isFresh)
        endBar = -1;
    origin = isFresh ? first : 0.0;
    changeCount = marks = 0;
    for (auto& st : lanes)
        st = {};

    const bool playing = host.clockRunning();
    // A fresh take while jamming keeps the clock going: the song's bars are the clock's (from 0).
    if (isFresh && playing && ! on.load())
        startBar = 0;
    juce::uint32 mask = 0;
    for (const int l : liveLayers)
        mask |= 1u << (juce::uint32) l;

    if (isFresh)
    {
        blocks.clear();
        markers.clear();
        const double from = playing ? std::floor (barNow() + kEps) : first;
        for (const int l : liveLayers)
        {
            if (host.isMuted (l))
                continue;
            SongBlock b;
            b.layer = l;
            b.start = from;
            b.length = first + kAhead - from;
            const double length = host.loopBeats (l);
            if (playing && length > 0)
                b.offset = wrap ((from - startBar.load()) * kBeatsPerBar - host.loopStart (l), length) / kBeatsPerBar;
            blocks.push_back (b);
            lanes[(size_t) l].in = true;
            lanes[(size_t) l].openStart = from;
        }
        markers.push_back ({ first, suggestName() });
    }
    else
    {
        std::vector<SongBlock> kept;
        for (const auto& b : blocks)
        {
            if (b.end() <= first + kEps || (endBar >= 0 && b.start >= endBar - kEps))
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
                after.offset = wrap (b.offset + (endBar - b.start), loopBars (b.layer));
                kept.push_back (after);
            }
            if (b.start < first + kEps)
            {
                auto open = b;
                open.length = juce::jmax (b.length, first + kAhead - b.start);
                if (endBar >= 0)
                    open.length = juce::jmin (open.length, endBar - b.start);
                open.fadeOut = 0;
                if (! lanes[(size_t) b.layer].in)
                {
                    lanes[(size_t) b.layer].in = true;
                    lanes[(size_t) b.layer].openStart = open.start;
                    kept.push_back (open);
                }
            }
        }
        blocks = kept;
    }

    gated = mask;
    startBarLive = first;
    state = State::waiting;
    lastMessage = {};
    changed();

    if (isFresh && playing)
    {
        if (! on.load())
        {
            on = true;
            publish();
        }
    }
    else
    {
        play (runFrom);
        if (isFresh && first > 0 && host.countIn)
            host.countIn ((first - startBar.load()) * kBeatsPerBar);
    }
    for (const int l : liveLayers)
    {
        auto& st = lanes[(size_t) l];
        if (st.in)
            host.setMute (l, false);
        else if (! host.isMuted (l))
        {
            if (punch && first > runFrom + kEps)
                st.outAt = first;
            else
                host.setMute (l, true);
        }
        st.lastMute = host.isMuted (l);
    }
    arrived = runFrom < 0;
    waitTicks = 0;
    lastNow = arrived ? barNow() : runFrom;
    return true;
}

//==============================================================================
void PocketSong::extendOpen()
{
    const double reach = std::floor (barNow()) + kAhead;
    bool moved = false;
    for (const int l : liveLayers)
    {
        const auto& st = lanes[(size_t) l];
        if (! st.in || st.outAt >= 0)
            continue;
        const int i = openBlock (l);
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
        changed();
}

void PocketSong::tick()
{
    if (pendingFrom >= 0)
    {
        // Waiting for the clock to stop, to start it again from the song's bar.
        if (host.clockRunning() && ++pendingTicks < 30)
            return;
        const double from = pendingFrom;
        pendingFrom = -1;
        startBar = from;
        on = true;
        seenRunning = false;
        publish();
        host.setClock (true);
        return;
    }
    if (on.load() && host.clockRunning())
        seenRunning = true;
    if (! isLive())
    {
        // The song plays to its end and stops there; the transport's STOP stops it too.
        if (on.load() && host.clockRunning() && ! blocks.empty() && barNow() >= std::ceil (lengthBars() - kEps) - kEps)
            stopPlaying();
        else if (on.load() && seenRunning && ! host.clockRunning())
        {
            on = false;
            publish();
            startBar = 0;
        }
        return;
    }
    if (! host.clockRunning() && ! arrived && ++waitTicks < 60)
        return;
    if (! host.clockRunning())
    {
        finish (lastNow > startBarLive + 0.25);
        return;
    }
    const double now = barNow();
    if (! arrived)
    {
        if (now > lastNow + 1.0 + kEps && now > startBarLive + kEps)
            return;
        arrived = true;
        lastNow = now;
    }
    if (now + 2.0 < lastNow)
    {
        finish (lastNow > startBarLive + 0.25);
        return;
    }
    lastNow = now;
    if (now >= startBarLive - kEps)
    {
        state = State::recording;
        if (host.countIn)
            host.countIn (-1.0);
    }

    bool edited = false;
    for (const int l : liveLayers)
    {
        auto& st = lanes[(size_t) l];
        const bool m = host.isMuted (l);
        if (m == st.lastMute)
            continue;
        st.lastMute = m;
        edited = true;
        if (! m)
            bringIn (l, nextBar());
        else if (st.in && st.outAt < 0)
            takeOut (l, nextBar());
        else if (st.outAt >= 0)
        {
            host.setMute (l, false);
            st.lastMute = false;
        }
    }
    for (const int l : liveLayers)
    {
        auto& st = lanes[(size_t) l];
        if (st.outAt >= 0 && now >= st.outAt - kEps)
        {
            st.in = false;
            st.outAt = -1;
            st.openStart = -1;
            host.setMute (l, true);
            st.lastMute = true;
        }
    }
    if (edited)
        changed();
    extendOpen();

    if (endBar >= 0 && now >= endBar - kEps)
        finish (true);
}

void PocketSong::finish (bool keep)
{
    const double end = endBar >= 0 ? endBar : std::ceil (lastNow - 1.0e-3);
    const bool stopClock = endBar < 0;
    if (keep && end <= startBarLive + kEps)
        keep = false;

    gated = 0;
    if (host.countIn)
        host.countIn (-1.0);
    state = State::idle;

    const auto stop = [this, stopClock]
    {
        if (! stopClock)
            return;
        on = false;
        publish();
        if (host.clockRunning())
            host.setClock (false);
        startBar = 0;
    };

    if (! keep)
    {
        blocks = beforeBlocks;
        markers = beforeMarkers;
        current = beforeTake;
        changed();
        lastMessage = changeCount == 0 && lastNow <= startBarLive + 0.25 ? "REC LIVE called off." : "REC LIVE thrown away: the song is as it was.";
        stop();
        punch = fresh = false;
        endBar = -1;
        return;
    }

    std::vector<SongBlock> kept;
    std::array<bool, kLayers> inAtEnd {};
    for (auto b : blocks)
    {
        const bool live = std::find (liveLayers.begin(), liveLayers.end(), b.layer) != liveLayers.end();
        if (live && (fresh || endBar < 0))
        {
            if (b.start >= end - kEps)
                continue;
            if (b.end() > end + kEps)
                b.length = end - b.start;
        }
        if (live && b.start < end - kEps && b.end() >= end - kEps)
            inAtEnd[(size_t) b.layer] = true;
        kept.push_back (b);
    }
    blocks = kept;

    double length = end;
    if (fresh)
    {
        std::vector<SongBlock> moved;
        for (auto b : blocks)
        {
            if (b.end() <= startBarLive + kEps)
                continue;
            if (b.start < startBarLive)
            {
                b.offset = wrap (b.offset + (startBarLive - b.start), loopBars (b.layer));
                b.length = b.end() - startBarLive;
                b.start = startBarLive;
            }
            b.start -= startBarLive;
            moved.push_back (b);
        }
        blocks = moved;
        for (auto& m : markers)
            m.bar = juce::jmax (0.0, m.bar - startBarLive);
        length = end - startBarLive;
        if (marks == 0)
        {
            markers.clear();
            for (double bar = 0; bar < length - kEps; bar += kAutoSection)
                markers.push_back ({ bar, suggestName() });
        }
    }
    changed();

    for (const int l : liveLayers)
        host.setMute (l, stopClock ? ! inAtEnd[(size_t) l] : false);

    if (beforeTake < 0 && ! beforeBlocks.empty())
        takeList.insert (takeList.begin(), Take { "Song before REC LIVE", beforeBlocks, beforeMarkers });
    Take take;
    take.name = "Take " + juce::String ((int) takeList.size() + 1);
    if (punch)
        take.name << (endBar >= 0 ? " (bars " + juce::String ((int) startBarLive + 1) + "-" + juce::String ((int) endBar) + " again)"
                                  : " (on from bar " + juce::String ((int) startBarLive + 1) + ")");
    take.blocks = blocks;
    take.markers = markers;
    takeList.push_back (take);
    current = (int) takeList.size() - 1;
    lastMessage = take.name + " kept: " + barsText (length) + ", " + juce::String (changeCount) + (changeCount == 1 ? " change." : " changes.");
    stop();
    punch = fresh = false;
    endBar = -1;
}

int PocketSong::stopAndKeep()
{
    if (! isLive())
        return -1;
    lastNow = juce::jmax (lastNow, barNow());
    finish (lastNow > startBarLive + 0.25);
    return current;
}

void PocketSong::cancel()
{
    if (isLive())
        finish (false);
}

//==============================================================================
void PocketSong::storeCurrent()
{
    if (isLive() || ! juce::isPositiveAndBelow (current, (int) takeList.size()))
        return;
    takeList[(size_t) current].blocks = blocks;
    takeList[(size_t) current].markers = markers;
}

bool PocketSong::useTake (int i)
{
    if (isLive() || ! juce::isPositiveAndBelow (i, (int) takeList.size()))
        return false;
    storeCurrent();
    blocks = takeList[(size_t) i].blocks;
    markers = takeList[(size_t) i].markers;
    current = i;
    changed();
    return true;
}

bool PocketSong::deleteTake (int i)
{
    if (isLive() || ! juce::isPositiveAndBelow (i, (int) takeList.size()))
        return false;
    takeList.erase (takeList.begin() + i);
    if (current == i)
        current = -1;
    else if (current > i)
        --current;
    ++editCount;
    return true;
}

void PocketSong::clear()
{
    if (isLive())
    {
        gated = 0;
        state = State::idle;
        if (host.countIn)
            host.countIn (-1.0);
    }
    if (on.load())
        stopPlaying();
    blocks.clear();
    markers.clear();
    takeList.clear();
    current = -1;
    punch = fresh = false;
    endBar = -1;
    changed();
}

//==============================================================================
namespace
{
juce::var blocksVar (const std::vector<SongBlock>& blocks)
{
    juce::Array<juce::var> list;
    for (const auto& b : blocks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("layer", b.layer);
        o->setProperty ("start", b.start);
        o->setProperty ("length", b.length);
        o->setProperty ("fadeIn", b.fadeIn);
        o->setProperty ("fadeOut", b.fadeOut);
        o->setProperty ("level", b.level);
        o->setProperty ("offset", b.offset);
        list.add (juce::var (o));
    }
    return list;
}

juce::var markersVar (const std::vector<SongMarker>& markers)
{
    juce::Array<juce::var> list;
    for (const auto& m : markers)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("bar", m.bar);
        o->setProperty ("name", m.name);
        list.add (juce::var (o));
    }
    return list;
}

std::vector<SongBlock> blocksFrom (const juce::var& v)
{
    std::vector<SongBlock> out;
    if (auto* a = v.getArray())
        for (const auto& e : *a)
        {
            SongBlock b;
            b.layer = juce::jlimit (0, PocketSong::kLayers - 1, (int) e.getProperty ("layer", 0));
            b.start = juce::jmax (0.0, (double) e.getProperty ("start", 0.0));
            b.length = juce::jmax (0.25, (double) e.getProperty ("length", 4.0));
            b.fadeIn = juce::jmax (0.0, (double) e.getProperty ("fadeIn", 0.0));
            b.fadeOut = juce::jmax (0.0, (double) e.getProperty ("fadeOut", 0.0));
            b.level = juce::jlimit (0.0f, 1.0f, (float) (double) e.getProperty ("level", 1.0));
            b.offset = juce::jmax (0.0, (double) e.getProperty ("offset", 0.0));
            if ((int) out.size() < PocketSong::kMaxBlocks)
                out.push_back (b);
        }
    return out;
}

std::vector<SongMarker> markersFrom (const juce::var& v)
{
    std::vector<SongMarker> out;
    if (auto* a = v.getArray())
        for (const auto& e : *a)
            out.push_back ({ juce::jmax (0.0, (double) e.getProperty ("bar", 0.0)), e.getProperty ("name", "Part").toString() });
    return out;
}
}

juce::var PocketSong::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("version", 1);
    o->setProperty ("countIn", countInBars);
    o->setProperty ("blocks", blocksVar (blocks));
    o->setProperty ("markers", markersVar (markers));
    o->setProperty ("current", current);
    juce::Array<juce::var> list;
    for (int i = 0; i < (int) takeList.size(); ++i)
    {
        const auto& t = takeList[(size_t) i];
        const bool onScreen = i == current && ! isLive();
        auto* to = new juce::DynamicObject();
        to->setProperty ("name", t.name);
        to->setProperty ("blocks", blocksVar (onScreen ? blocks : t.blocks));
        to->setProperty ("markers", markersVar (onScreen ? markers : t.markers));
        list.add (juce::var (to));
    }
    o->setProperty ("takes", list);
    return juce::var (o);
}

void PocketSong::fromVar (const juce::var& v)
{
    if (isLive())
        cancel();
    if (on.load())
        stopPlaying();
    blocks = blocksFrom (v.getProperty ("blocks", {}));
    markers = markersFrom (v.getProperty ("markers", {}));
    countInBars = juce::jlimit (0, 2, (int) v.getProperty ("countIn", 1));
    takeList.clear();
    if (auto* a = v.getProperty ("takes", {}).getArray())
        for (const auto& e : *a)
            takeList.push_back ({ e.getProperty ("name", "Take").toString(), blocksFrom (e.getProperty ("blocks", {})),
                                  markersFrom (e.getProperty ("markers", {})) });
    const int c = v.getProperty ("current", -1);
    current = juce::isPositiveAndBelow (c, (int) takeList.size()) ? c : -1;
    changed();
}

//==============================================================================
void PocketSong::update()
{
    const juce::SpinLock::ScopedTryLockType sl (lock);
    if (sl.isLocked() && pendingVersion != liveVersion)
    {
        live = pending;
        liveVersion = pendingVersion;
    }
    liveOn = live.on;
    liveStartBar = live.startBar;
}

float PocketSong::gain (int layer, double bar) const
{
    if (! liveOn || ! juce::isPositiveAndBelow (layer, kLayers))
        return 1.0f;
    const bool gatedLayer = (live.gated & (1u << (juce::uint32) layer)) != 0;
    if (! live.used[(size_t) layer] && ! gatedLayer)
        return 1.0f;   // not in the song: plays as you set it
    float g = 0.0f;
    for (int i = 0; i < live.count; ++i)
    {
        const auto& b = live.blocks[(size_t) i];
        if (b.layer != layer || bar < b.start - kEps || bar >= b.end() - kEps)
            continue;
        float v = b.level;
        if (b.fadeIn > kEps && bar < b.start + b.fadeIn)
            v *= (float) ((bar - b.start) / b.fadeIn);
        if (b.fadeOut > kEps && bar > b.end() - b.fadeOut)
            v *= (float) ((b.end() - bar) / b.fadeOut);
        g = juce::jmax (g, v);
    }
    return g;
}

double PocketSong::originBeat (int layer, double bar) const
{
    if (! liveOn)
        return kNone;
    for (int i = live.count; --i >= 0;)
    {
        const auto& b = live.blocks[(size_t) i];
        if (b.layer == layer && bar >= b.start - kEps && bar < b.end() - kEps)
            return (b.start - b.offset - liveStartBar) * kBeatsPerBar;
    }
    return kNone;
}
}
