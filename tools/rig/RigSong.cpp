#include "RigSong.h"

#include "RigEngine.h"
#include "RigScenes.h"

#include <algorithm>
#include <cmath>

namespace rigsong
{
namespace
{
constexpr double kEps = 1.0e-6;
constexpr double kMinBars = 0.25;

using Block = RigTimeline::Block;
using Marker = RigTimeline::Marker;

// Markers in order, starting at bar 1, each running to the next.
std::vector<Section> sectionsOf (std::vector<Marker> markers, const RigTimeline& tl)
{
    std::vector<Section> out;
    std::stable_sort (markers.begin(), markers.end(), [] (const Marker& a, const Marker& b) { return a.bar < b.bar; });
    if (markers.empty())
    {
        if (! tl.isEmpty())
            out.push_back ({ "Song", 0.0, juce::jmax (1.0, std::ceil (tl.lengthBars() - kEps)) });
        return out;
    }
    if (markers.front().bar > kEps)
        out.push_back ({ "Start", 0.0, markers.front().bar });
    for (size_t i = 0; i < markers.size(); ++i)
    {
        const auto& m = markers[i];
        double bars;
        if (i + 1 < markers.size())
            bars = markers[i + 1].bar - m.bar;
        else if (m.bars > kEps)
            bars = m.bars;
        else
            bars = juce::jmax (4.0, std::ceil (tl.lengthBars() - m.bar - kEps));
        if (bars < kEps)
            continue;   // two markers on one bar: the later one wins
        out.push_back ({ m.name, m.bar, bars });
    }
    return out;
}

// Puts the markers back as the sections say (every section has its marker, with its length).
void writeSections (RigTimeline& tl, const std::vector<Section>& list, const std::vector<int>& banks)
{
    auto& markers = tl.getMarkers();
    markers.clear();
    for (size_t i = 0; i < list.size(); ++i)
        markers.push_back ({ list[i].start, list[i].name, list[i].bars, i < banks.size() ? banks[i] : -1 });
}

std::vector<int> banksOf (const RigTimeline& tl, const std::vector<Section>& list)
{
    std::vector<int> banks;
    for (const auto& s : list)
    {
        int bank = -1;
        for (const auto& m : tl.getMarkers())
            if (std::abs (m.bar - s.start) < kEps)
                bank = m.loopBank;
        banks.push_back (bank);
    }
    return banks;
}

// Every section gets a marker of its own (a song made on the timeline has none at first).
std::vector<Section> ensure (RigTimeline& tl)
{
    auto list = sections (tl);
    writeSections (tl, list, banksOf (tl, list));
    return list;
}

// Moves every block that starts at or after `from`, and every marker there, by `delta` bars.
void shiftFrom (RigTimeline& tl, double from, double delta)
{
    for (auto& b : tl.getBlocks())
        if (b.start >= from - kEps)
            b.start += delta;
    for (auto& m : tl.getMarkers())
        if (m.bar >= from - kEps)
            m.bar += delta;
}

// Empties a lane between two bars, cutting blocks that cross the edges.
void clearRange (RigTimeline& tl, int lane, double a, double b)
{
    auto& blocks = tl.getBlocks();
    std::vector<Block> added;
    for (auto it = blocks.begin(); it != blocks.end();)
    {
        auto& k = *it;
        if (k.lane != lane || k.end() <= a + kEps || k.start >= b - kEps)
        {
            ++it;
            continue;
        }
        if (k.start >= a - kEps && k.end() <= b + kEps)
        {
            it = blocks.erase (it);
            continue;
        }
        if (k.start < a && k.end() > b)
        {
            Block right = k;
            right.start = b;
            right.length = k.end() - b;
            right.fadeIn = 0;
            added.push_back (right);
            k.length = a - k.start;
            k.fadeOut = 0;
        }
        else if (k.start < a)
        {
            k.length = a - k.start;
            k.fadeOut = juce::jmin (k.fadeOut, k.length);
        }
        else
        {
            const double end = k.end();
            k.start = b;
            k.length = end - b;
            k.fadeIn = 0;
        }
        ++it;
    }
    blocks.insert (blocks.end(), added.begin(), added.end());
}

// The blocks that start in a section, from its start, cut at its end.
std::vector<Block> blocksIn (const RigTimeline& tl, const Section& s, int lane = -1)
{
    std::vector<Block> out;
    for (const auto& b : tl.getBlocks())
    {
        if (lane >= 0 && b.lane != lane)
            continue;
        if (b.start < s.start - kEps || b.start >= s.end() - kEps)
            continue;
        Block c = b;
        c.start -= s.start;
        if (c.end() > s.bars + kEps)
        {
            c.length = s.bars - c.start;
            c.fadeOut = juce::jmin (c.fadeOut, c.length);
        }
        out.push_back (c);
    }
    return out;
}

bool isLoopLane (int lane) { return RigTimeline::lanes()[(size_t) lane].isLoop; }
int stripOf (int lane) { return RigTimeline::lanes()[(size_t) lane].index; }
}

//==============================================================================
std::vector<Section> sections (const RigTimeline& tl)
{
    return sectionsOf (tl.getMarkers(), tl);
}

double songBars (const RigTimeline& tl)
{
    const auto list = sections (tl);
    return list.empty() ? 0.0 : list.back().end();
}

int sectionAt (const RigTimeline& tl, double bar)
{
    const auto list = sections (tl);
    for (int i = 0; i < (int) list.size(); ++i)
        if (bar >= list[(size_t) i].start - kEps && bar < list[(size_t) i].end() - kEps)
            return i;
    return -1;
}

juce::String suggestName (const RigTimeline& tl)
{
    static const char* names[] = { "Intro", "Verse", "Chorus", "Verse", "Chorus", "Bridge", "Chorus", "Outro" };
    const auto list = sections (tl);
    const int n = (int) list.size();
    const juce::String base = n < 8 ? names[n] : "Part";
    int same = 0;
    for (const auto& s : list)
        if (s.name == base || s.name.startsWith (base + " "))
            ++same;
    return same == 0 ? base : base + " " + juce::String (same + 1);
}

juce::String letterFor (int section)
{
    return section < 26 ? juce::String::charToString ((juce::juce_wchar) ('A' + section)) : juce::String (section + 1);
}

juce::String barsText (double bars)
{
    const bool whole = std::abs (bars - std::round (bars)) < 0.01;
    const auto n = whole ? juce::String ((int) std::round (bars)) : juce::String (bars, 2).trimCharactersAtEnd ("0");
    return n + (whole && (int) std::round (bars) == 1 ? " bar" : " bars");
}

//==============================================================================
int addSection (RigTimeline& tl, const juce::String& name, double bars)
{
    auto list = ensure (tl);
    if ((int) list.size() >= kMaxSections)
        return -1;
    const double start = list.empty() ? 0.0 : list.back().end();
    tl.getMarkers().push_back ({ start, name, juce::jmax (kMinBars, bars), -1 });
    tl.changed();
    return (int) list.size();
}

void setSectionBars (RigTimeline& tl, int index, double bars)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto s = list[(size_t) index];
    bars = juce::jmax (kMinBars, bars);
    const double delta = bars - s.bars, newEnd = s.start + bars;
    if (std::abs (delta) < kEps)
        return;
    auto& blocks = tl.getBlocks();
    // The boxes that fill the section grow or shrink with it; what's past the new end goes.
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(), [&] (const Block& b)
                                  { return b.start >= newEnd - kEps && b.start < s.end() - kEps; }),
                  blocks.end());
    std::vector<size_t> inside;
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        auto& b = blocks[i];
        if (b.start >= s.end() - kEps)
            continue;
        if (std::abs (b.end() - s.end()) < kEps && b.start < s.end() - kEps)
            b.length += delta;
        else if (b.end() > newEnd + kEps && b.end() <= s.end() + kEps)
            b.length = newEnd - b.start;
    }
    shiftFrom (tl, s.end(), delta);
    for (auto& m : tl.getMarkers())
        if (std::abs (m.bar - s.start) < kEps)
            m.bars = bars;
    tl.changed();
}

void renameSection (RigTimeline& tl, int index, const juce::String& name)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    tl.getMarkers()[(size_t) index].name = name.trim().isEmpty() ? "Part " + letterFor (index) : name.trim();
    tl.changed();
}

void removeSection (RigTimeline& tl, int index)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto s = list[(size_t) index];
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
        clearRange (tl, lane, s.start, s.end());
    // A block that ran right through the section is shorter by it now.
    tl.getMarkers().erase (tl.getMarkers().begin() + index);
    for (auto& b : tl.getBlocks())
        if (b.start >= s.end() - kEps)
            b.start -= s.bars;
    for (auto& m : tl.getMarkers())
        if (m.bar >= s.end() - kEps)
            m.bar -= s.bars;
    // Two halves of one block cut by clearRange meet again: join them.
    auto& blocks = tl.getBlocks();
    std::stable_sort (blocks.begin(), blocks.end(), [] (const Block& a, const Block& b) { return a.start < b.start; });
    tl.changed();
}

int duplicateSection (RigTimeline& tl, int index)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (index, (int) list.size()) || (int) list.size() >= kMaxSections)
        return -1;
    const auto s = list[(size_t) index];
    const auto copies = blocksIn (tl, s);
    const int bank = tl.getMarkers()[(size_t) index].loopBank;
    shiftFrom (tl, s.end(), s.bars);
    for (auto b : copies)
    {
        b.start += s.end();
        tl.getBlocks().push_back (b);
    }
    tl.getMarkers().push_back ({ s.end(), s.name, s.bars, bank });
    tl.changed();
    return index + 1;
}

void moveSection (RigTimeline& tl, int from, int to)
{
    auto list = ensure (tl);
    const int n = (int) list.size();
    if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n) || from == to)
        return;
    auto banks = banksOf (tl, list);
    std::vector<std::vector<Block>> content;
    for (const auto& s : list)
        content.push_back (blocksIn (tl, s));
    // Blocks after the song (made on the timeline past the last section) stay where they are.
    const double end = list.back().end();
    auto& blocks = tl.getBlocks();
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(), [end] (const Block& b) { return b.start < end - kEps; }), blocks.end());

    auto take = [from, to] (auto& v)
    {
        auto item = v[(size_t) from];
        v.erase (v.begin() + from);
        v.insert (v.begin() + to, item);
    };
    take (list);
    take (content);
    take (banks);
    double at = 0;
    for (size_t i = 0; i < list.size(); ++i)
    {
        list[i].start = at;
        for (auto b : content[i])
        {
            b.start += at;
            blocks.push_back (b);
        }
        at += list[i].bars;
    }
    writeSections (tl, list, banks);
    tl.changed();
}

//==============================================================================
int boxBlock (const RigTimeline& tl, int section, int lane)
{
    const auto list = sections (tl);
    if (! juce::isPositiveAndBelow (section, (int) list.size()))
        return -1;
    const auto& s = list[(size_t) section];
    const auto& blocks = tl.getBlocks();
    for (int i = 0; i < (int) blocks.size(); ++i)
    {
        const auto& b = blocks[(size_t) i];
        if (b.lane == lane && b.start < s.end() - kEps && b.end() > s.start + kEps)
            return i;
    }
    return -1;
}

bool boxCarriesOn (const RigTimeline& tl, int section, int lane)
{
    const int i = boxBlock (tl, section, lane);
    const auto list = sections (tl);
    return i >= 0 && tl.getBlocks()[(size_t) i].start < list[(size_t) section].start - kEps;
}

int setBox (RigTimeline& tl, int section, int lane, int clip)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (section, (int) list.size()) || ! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return -1;
    const auto s = list[(size_t) section];
    Block b;
    b.lane = lane;
    b.start = s.start;
    b.length = s.bars;
    b.clip = clip;
    if (const int was = boxBlock (tl, section, lane); was >= 0)
    {
        const auto& old = tl.getBlocks()[(size_t) was];
        b.level = old.level;
        if (old.start >= s.start - kEps)
            b.fadeIn = old.fadeIn;
        if (old.end() <= s.end() + kEps)
            b.fadeOut = old.fadeOut;
    }
    clearRange (tl, lane, s.start, s.end());
    tl.getBlocks().push_back (b);
    tl.changed();
    return boxBlock (tl, section, lane);
}

void clearBox (RigTimeline& tl, int section, int lane)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (section, (int) list.size()))
        return;
    clearRange (tl, lane, list[(size_t) section].start, list[(size_t) section].end());
    tl.changed();
}

void copyBox (RigTimeline& tl, int fromSection, int toSection, int lane)
{
    const auto list = ensure (tl);
    if (! juce::isPositiveAndBelow (fromSection, (int) list.size()) || ! juce::isPositiveAndBelow (toSection, (int) list.size())
        || fromSection == toSection)
        return;
    const auto& to = list[(size_t) toSection];
    auto copies = blocksIn (tl, list[(size_t) fromSection], lane);
    // A box that carries on from before the section: copy what plays at its start.
    if (copies.empty())
        if (const int i = boxBlock (tl, fromSection, lane); i >= 0)
        {
            Block b = tl.getBlocks()[(size_t) i];
            b.start = 0;
            b.length = list[(size_t) fromSection].bars;
            b.fadeIn = 0;
            copies.push_back (b);
        }
    clearRange (tl, lane, to.start, to.end());
    for (auto b : copies)
    {
        if (b.start >= to.bars - kEps)
            continue;
        // A box that filled its section fills this one too, however long it is.
        if (std::abs (b.end() - list[(size_t) fromSection].bars) < kEps)
            b.length = to.bars - b.start;
        b.length = juce::jmin (b.length, to.bars - b.start);
        b.start += to.start;
        tl.getBlocks().push_back (b);
    }
    tl.changed();
}

//==============================================================================
std::vector<std::pair<int, juce::String>> clipChoices (RigEngine& engine, int lane)
{
    std::vector<std::pair<int, juce::String>> out;
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return out;
    if (isLoopLane (lane))
        return { { -1, "Plays" } };
    switch (stripOf (lane))
    {
        case RigEngine::mpcStrip:
        {
            out.push_back ({ -1, "As it is" });
            const auto& seqs = engine.getMpc().getProject().getSequences();
            for (int i = 0; i < (int) seqs.size(); ++i)
            {
                const auto& name = seqs[(size_t) i].name;
                out.push_back ({ i, name.isNotEmpty() ? name : "Seq " + juce::String (i + 1) });
            }
            return out;
        }
        case RigEngine::bassStrip:
            out.push_back ({ -1, "As it is" });
            for (int i = 0; i < crate::bass::kPatterns; ++i)
                out.push_back ({ i, "Pattern " + juce::String (i + 1) });
            return out;
        case RigEngine::drumsStrip:
            out.push_back ({ -1, "As it is" });
            for (int i = 0; i < 16; ++i)
                out.push_back ({ i, "Pattern " + juce::String (i + 1) });
            return out;
        case RigEngine::synthStrip:
            return { { -1, "As it is" }, { 0, "Keys" }, { 1, "Arp" } };
        default:
            return { { -1, "Live" } };
    }
}

juce::String clipName (RigEngine& engine, int lane, int clip)
{
    for (const auto& [c, name] : clipChoices (engine, lane))
        if (c == clip)
            return name;
    return clip < 0 ? "Plays" : juce::String (clip + 1);
}

juce::String laneName (int lane)
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return {};
    if (isLoopLane (lane))
        return "Loop " + juce::String (stripOf (lane) + 1);
    switch (stripOf (lane))
    {
        case RigEngine::mpcStrip:     return "MPC";
        case RigEngine::drumsStrip:   return "Drums";
        case RigEngine::bassStrip:    return "Bass";
        case RigEngine::synthStrip:   return "Synth";
        case RigEngine::vocoderStrip: return "Vocoder";
        case RigEngine::samplerStrip: return "Sampler";
        case RigEngine::inputStrip:   return "Line in";
        default:                      return "Track";
    }
}

juce::String laneHint (RigEngine& engine, int lane)
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return {};
    if (isLoopLane (lane))
    {
        const auto& layer = engine.getLooper().getLayer (stripOf (lane));
        const double beats = layer.lengthBeats.load();
        if (beats <= 0)
            return "empty";
        return RigLooper::sourceName (layer.source.load()).toLowerCase() + juce::String::fromUTF8 (" \xc2\xb7 ")
             + barsText (beats / juce::jmax (1, engine.getTransport().getBeatsPerBar()));
    }
    switch (stripOf (lane))
    {
        case RigEngine::mpcStrip:     return "sequences";
        case RigEngine::drumsStrip:   return "808 patterns";
        case RigEngine::bassStrip:    return "bass patterns";
        case RigEngine::synthStrip:   return "keys or arp";
        case RigEngine::vocoderStrip: return "your mic, live";
        case RigEngine::samplerStrip: return "sampler keys, live";
        case RigEngine::inputStrip:   return "guitar / keytar, live";
        default:                      return {};
    }
}

bool laneHasSomething (RigEngine& engine, int lane)
{
    if (! juce::isPositiveAndBelow (lane, RigTimeline::kLanes))
        return false;
    if (engine.getTimeline().laneUsed (lane))
        return true;
    if (isLoopLane (lane))
        return engine.getLooper().getLayer (stripOf (lane)).lengthBeats.load() > 0;
    const int strip = stripOf (lane);
    return strip != RigEngine::samplerStrip && strip != RigEngine::inputStrip;
}

//==============================================================================
namespace
{
// Fills a section's boxes from a scene's state (what's playing, or an old part).
void boxesFromState (RigEngine& engine, RigTimeline& tl, int section, const RigScene::State& s)
{
    const auto& lanes = RigTimeline::lanes();
    auto& project = engine.getMpc().getProject();
    for (int lane = 0; lane < RigTimeline::kLanes; ++lane)
    {
        const auto& info = lanes[(size_t) lane];
        if (info.isLoop)
        {
            if (s.loopMutes[(size_t) info.index] == 0 && engine.getLooper().getLayer (info.index).lengthBeats.load() > 0)
                setBox (tl, section, lane, -1);
            continue;
        }
        const bool on = info.index < RigScene::kMaxStrips && s.stripMutes[(size_t) info.index] == 0;
        if (! on)
            continue;
        switch (info.index)
        {
            case RigEngine::mpcStrip:
            {
                const int seq = s.mpcSequence;
                bool notes = false;
                if (juce::isPositiveAndBelow (seq, (int) project.getSequences().size()))
                    for (const auto& t : project.getSequences()[(size_t) seq].tracks)
                        notes = notes || ! t.notes.empty();
                if (notes)
                    setBox (tl, section, lane, seq);
                break;
            }
            case RigEngine::bassStrip:
                if (s.bassSequencer == 1)
                    setBox (tl, section, lane, s.bassPattern);
                break;
            case RigEngine::drumsStrip:
                if (s.drumSequencer == 1)
                    setBox (tl, section, lane, s.drumPattern);
                break;
            case RigEngine::synthStrip:
                if (s.arp == 1)
                    setBox (tl, section, lane, 1);
                break;
            case RigEngine::vocoderStrip:
                if (s.vocoderOn == 1)
                    setBox (tl, section, lane, -1);
                break;
            case RigEngine::inputStrip:
                if (s.inputOn == 1)
                    setBox (tl, section, lane, -1);
                break;
            default:
                break;
        }
    }
}
}

int addSectionFromWhatsPlaying (RigEngine& engine, const juce::String& name, double bars)
{
    auto& tl = engine.getTimeline();
    const int index = addSection (tl, name, bars);
    if (index < 0)
        return -1;
    const auto state = engine.captureScene();
    boxesFromState (engine, tl, index, state);
    if (state.loopBank >= 0 && engine.getNumLoopBanks() > 1)
        tl.getMarkers()[(size_t) index].loopBank = state.loopBank;
    tl.changed();
    return index;
}

bool sectionsFromParts (RigEngine& engine)
{
    auto& tl = engine.getTimeline();
    const auto& parts = engine.getScenes().getScenes();
    if (parts.empty() || ! tl.getMarkers().empty() || ! tl.isEmpty())
        return false;
    // A part leaves some things as they were: they carry on from the part before (or from how
    // the show was saved, for the first).
    auto state = engine.captureScene();
    auto take = [] (int& into, int value) { if (value >= 0) into = value; };
    for (const auto& part : parts)
    {
        const auto& p = part.state;
        take (state.mpcSequence, p.mpcSequence);
        take (state.bassPattern, p.bassPattern);
        take (state.bassSequencer, p.bassSequencer);
        take (state.arp, p.arp);
        take (state.vocoderOn, p.vocoderOn);
        take (state.inputOn, p.inputOn);
        take (state.drumPattern, p.drumPattern);
        take (state.drumSequencer, p.drumSequencer);
        for (size_t i = 0; i < state.stripMutes.size(); ++i)
            take (state.stripMutes[i], p.stripMutes[i]);
        for (size_t i = 0; i < state.loopMutes.size(); ++i)
            take (state.loopMutes[i], p.loopMutes[i]);
        const int index = addSection (tl, part.name, part.bars > 0 ? part.bars : 8);
        if (index < 0)
            break;
        boxesFromState (engine, tl, index, state);
        tl.getMarkers()[(size_t) index].loopBank = p.loopBank;
    }
    tl.changed();
    return true;
}
}
