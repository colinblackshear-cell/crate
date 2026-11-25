#include "RigDirty.h"
#include "RigEngine.h"

#include <algorithm>
#include <cmath>

static_assert (RigEngine::numStrips <= RigDirty::kMaxStrips, "a mute slot for every strip");

namespace
{
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

std::string bankKey (int bank, int layer)
{
    return "Bank " + std::to_string (bank + 1) + "/Loop " + std::to_string (layer + 1);
}

juce::String takesFolder (int layer)
{
    return RigTakes::isNotesLayer (layer) ? "Notes " + juce::String (RigTakes::stripOfNotesLayer (layer))
                                          : "Loop " + juce::String (layer + 1);
}

// The take's path in the loops folder (as RigEngine saves it), and its key here.
juce::String takePath (int layer, int number)
{
    return "Takes/" + takesFolder (layer) + "/Take " + juce::String (number) + ".wav";
}
std::string takeKey (int layer, int number)
{
    return takePath (layer, number).dropLastCharacters (4).toStdString();
}

bool isBoolField (int f)
{
    return f != RigDirty::bankCurrent && f != RigDirty::keysTo && f != RigDirty::padsTo;
}
std::string stored (int field, float v)
{
    if (isBoolField (field))
        return v != 0.0f ? "1" : "0";
    return std::to_string (juce::roundToInt (v));
}

// A value as the show's tree has it ("state", "fx" and "data" are base64; when they're XML,
// their own keys and values; other binary is hashed).
void flattenTree (const juce::ValueTree& t, const std::string& path, RigDirty::Flat& out, bool byPosition);
void flattenValue (const std::string& path, const juce::String& name, const juce::String& s, RigDirty::Flat& out)
{
    if ((name == "state" || name == "fx" || name == "data") && s.length() > 16)
    {
        juce::MemoryBlock m;
        if (m.fromBase64Encoding (s))
        {
            if (auto x = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize()))
                return flattenTree (juce::ValueTree::fromXml (*x), path + "@" + name.toStdString() + "{xml}", out, false);
            out[path + "@" + name.toStdString() + "{bin}"] = juce::String::toHexString ((juce::int64) m.toString().hashCode64()).toStdString();
            return;
        }
    }
    out[path + "@" + name.toStdString()] = s.toStdString();
}

void flattenTree (const juce::ValueTree& t, const std::string& path, RigDirty::Flat& out, bool byPosition)
{
    for (int i = 0; i < t.getNumProperties(); ++i)
    {
        const auto name = t.getPropertyName (i);
        flattenValue (path, name.toString(), t.getProperty (name).toString(), out);
    }
    std::map<juce::String, int> seen;
    int position = 0;
    for (const auto c : t)
    {
        juce::String key = c.getType().toString();
        if (byPosition)
            key = "Bank[" + juce::String (position++) + "]";
        else
            for (auto id : { "index", "id", "layer", "number", "tool", "name" })
                if (c.hasProperty (id))
                {
                    key << "[" << id << "=" << c.getProperty (id).toString() << "]";
                    break;
                }
        if (const int n = seen[key]++; n > 0)
            key << "#" << n;
        // The loop banks by their place, not their name (a name is a value like any other).
        flattenTree (c, path + "/" + key.toStdString(), out, path == "Show" && c.hasType ("LoopBanks"));
    }
}
} // namespace

//==============================================================================
struct RigDirty::ParamWatch : juce::AudioProcessorParameter::Listener
{
    ParamWatch (RigDirty& o, juce::AudioProcessorParameter& p, std::string k) : owner (o), param (p), key (std::move (k))
    {
        ranged = dynamic_cast<juce::RangedAudioParameter*> (&param);
    }

    // On whichever thread changed it. The playback write itself is ignored (it's on the writing
    // thread, between runtimeParam and runtimeParamWritten); anything else is yours.
    void parameterValueChanged (int, float) override
    {
        if (! owner.isEnabled())
            return;
        if (slot.writing.load() && slot.writer.load() == juce::Thread::getCurrentThreadId())
            return;
        // (The count first: a save adopting this slot sees it moved, before it's yours.)
        slot.edits.fetch_add (1);
        slot.poisoned = false;
        slot.active = false;
        owner.noteChange();
    }
    void parameterGestureChanged (int, bool) override {}

    // A 0..1 value as the tool's saved state has it.
    std::string asStored (float v) const
    {
        const float plain = ranged != nullptr ? ranged->convertFrom0to1 (v) : v;
        return juce::var (plain).toString().toStdString();
    }

    RigDirty& owner;
    juce::AudioProcessorParameter& param;
    juce::RangedAudioParameter* ranged = nullptr;
    std::string key;     // its value in the flattened show (empty: none found)
    juce::String tool;
    Slot slot;
    // Message thread: the value a document save wrote, as it wrote it, once that save made it
    // yours (so it reads back exactly, whatever converting it to 0..1 and back would round to).
    bool hasAdopted = false;
    float adoptedValue = 0;
    std::string adoptedText;
};

//==============================================================================
struct RigDirty::Hasher : juce::Thread
{
    explicit Hasher (RigDirty& o) : juce::Thread ("Dirty hashes"), owner (o) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            // A save is copying loops: no copy of ours queues up in front of it (it waits for
            // at most the one already running). saveEnded wakes us.
            if (owner.savesWaiting.load() > 0)
            {
                wait (100);
                continue;
            }
            Job job;
            bool have = false;
            {
                const std::lock_guard<std::mutex> l (owner.jobsLock);
                // (Checked again with the queue held: a save that began meanwhile goes first.)
                if (! owner.jobs.empty() && owner.savesWaiting.load() == 0)
                {
                    job = std::move (owner.jobs.front());
                    owner.jobs.pop_front();
                    have = true;
                }
            }
            if (! have)
            {
                if (owner.savesWaiting.load() == 0)
                    wait (200);
                continue;
            }
            {
                const std::lock_guard<std::mutex> l (owner.statsLock);
                ++owner.stats.jobsStarted;
            }
            Done d;
            const auto t0 = nowMs();
            if (job.buffer == nullptr)
            {
                // A live loop: copied the looper's way (the audio thread copies it a slice at a
                // time), here, never on the message thread.
                const auto audio = owner.engine.getLooper().copyLayerAudio (job.layer, &d.versionOfCopy);
                d.copyMs = nowMs() - t0;
                d.empty = audio.getNumSamples() == 0;
                if (! d.empty)
                {
                    d.hash = rigshow::hashAudio (audio);
                    d.bytes = (juce::int64) audio.getNumSamples() * audio.getNumChannels() * (juce::int64) sizeof (float);
                }
            }
            else
            {
                d.hash = rigshow::hashAudio (*job.buffer);
                d.bytes = (juce::int64) job.buffer->getNumSamples() * job.buffer->getNumChannels() * (juce::int64) sizeof (float);
            }
            d.ms = nowMs() - t0;
            d.job = std::move (job);
            {
                const std::lock_guard<std::mutex> l (owner.doneLock);
                owner.done.push_back (std::move (d));
            }
            owner.resultsReady.signal();
        }
    }

    RigDirty& owner;
};

//==============================================================================
bool RigDirty::Identity::sameAs (const Identity& o) const
{
    if (kind != o.kind)
        return false;
    switch (kind)
    {
        case rigshow::Asset::Kind::liveLayer: return layer == o.layer && version == o.version && version >= 0;
        case rigshow::Asset::Kind::take:      return takeId != 0 && takeId == o.takeId;
        case rigshow::Asset::Kind::bank:
        {
            // While both are alive the address can't be another buffer's.
            const auto a = buffer.lock(), b = o.buffer.lock();
            return a != nullptr && a == b;
        }
        case rigshow::Asset::Kind::other:
        default: return false;
    }
}

bool RigDirty::Watched::operator== (const Watched& o) const
{
    return versions == o.versions && states == o.states && swaps == o.swaps && takes == o.takes && queuedBank == o.queuedBank
           && automation == o.automation && macros == o.macros && timeline == o.timeline && banks == o.banks;
}

//==============================================================================
RigDirty::RigDirty (RigEngine& e) : engine (e)
{
    // A slot for every built-in tool's parameters, made once: the audio thread looks them up.
    for (int id = 0; id < RigEngine::numTools; ++id)
    {
        auto& tool = engine.getTool (id);
        const auto toolName = RigEngine::toolName (id);
        for (auto* p : tool.getParameters())
        {
            std::string key;
            if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                key = ("Show/Tool[name=" + toolName + "]@state{xml}/PARAM[id=" + w->paramID + "]@value").toStdString();
            auto watch = std::make_unique<ParamWatch> (*this, *p, std::move (key));
            watch->tool = toolName;
            byParam.emplace (p, watch.get());
            p->addListener (watch.get());
            watches.push_back (std::move (watch));
        }
    }
    for (int s = 0; s < RigEngine::numStrips; ++s)
        stripKeys[(size_t) s] = ("Show/Strip[name=" + RigEngine::stripName (s) + "]@mute").toStdString();
    for (int l = 0; l < RigLooper::kLayers; ++l)
        loopKeys[(size_t) l] = "Show/Loop[index=" + std::to_string (l) + "]@mute";
    fieldKeys[vocoderOn] = "Show@vocoderOn";
    fieldKeys[inputOn] = "Show@inputOn";
    fieldKeys[mpcBeat] = "Show@mpcBeat";
    fieldKeys[timelineOn] = "Show/Timeline@on";
    fieldKeys[songMode] = "Show@songMode";
    fieldKeys[bankCurrent] = "Show/LoopBanks@current";
    fieldKeys[keysTo] = "Show@keysTo";
    fieldKeys[padsTo] = "Show@padsTo";
}

RigDirty::~RigDirty()
{
    setEnabled (false);
    for (auto& w : watches)
        w->param.removeListener (w.get());
}

void RigDirty::setEnabled (bool on)
{
    if (on == isEnabled())
        return;
    if (on)
    {
        if (! keysMatched)
            matchKeys();
        resetSlots();
        enabled = true;
        watched = readWatched();
        hasher = std::make_unique<Hasher> (*this);
        hasher->startThread (juce::Thread::Priority::low);
        noteChange();
    }
    else
    {
        enabled = false;
        if (hasher != nullptr)
        {
            hasher->signalThreadShouldExit();
            hasher->notify();
            hasher->stopThread (20000);
            hasher.reset();
        }
        {
            const std::lock_guard<std::mutex> l (jobsLock);
            jobs.clear();
        }
        {
            const std::lock_guard<std::mutex> l (doneLock);
            done.clear();
        }
        layerInFlight.fill (false);
        buffersInFlight.clear();
        takesInFlight.clear();
        // Nothing is followed while off, so nothing known is kept (and no bank's sound is held).
        layerCache.fill ({});
        pinned.clear();
        takeHashes.clear();
        takeAliases.clear();
        swapAliases.fill ({});
        bankMutes.clear();
        baseline.reset();
        hasBaselineFlag = false;
        candidates.clear();
        saveSlots.clear();
        publishedGeneration = 0;
    }
}

void RigDirty::matchKeys()
{
    // Which parameters' saved values can be found where they were looked for: each tool's saved
    // state, flattened as a show's is. Those that can't get no playback override.
    keysMatched = true;
    std::map<juce::String, Flat> byTool;
    for (int id = 0; id < RigEngine::numTools; ++id)
    {
        juce::MemoryBlock state;
        engine.getTool (id).getStateInformation (state);
        juce::ValueTree t ("Tool");
        t.setProperty ("name", RigEngine::toolName (id), nullptr);
        t.setProperty ("state", state.toBase64Encoding(), nullptr);
        auto& flat = byTool[RigEngine::toolName (id)];
        flattenTree (t, "Show/Tool[name=" + RigEngine::toolName (id).toStdString() + "]", flat, false);
    }
    std::map<juce::String, std::pair<int, int>> counts;   // tool -> (matched, all)
    for (auto& w : watches)
    {
        auto& c = counts[w->tool];
        ++c.second;
        if (! w->key.empty() && byTool[w->tool].count (w->key) > 0)
            ++c.first;
        else
            w->key.clear();
    }
    unmatched.clear();
    for (const auto& [tool, c] : counts)
        if (c.first < c.second)
            unmatched.add (tool + " (" + juce::String (c.second - c.first) + " of " + juce::String (c.second) + ")");
}

juce::StringArray RigDirty::unmatchedTools() const { return unmatched; }

void RigDirty::resetSlots()
{
    auto reset = [] (Slot& s)
    {
        s.edits.fetch_add (1);
        s.poisoned = false;
        s.active = false;
    };
    for (auto& w : watches)
    {
        reset (w->slot);
        w->slot.writing = false;
        w->hasAdopted = false;
    }
    for (auto& s : stripSlots)
        reset (s);
    for (auto& s : fieldSlots)
        reset (s);
    for (auto& s : loopSlots)
        reset (s);
}

RigDirty::Slot* RigDirty::slotFor (Field f, int index)
{
    if (f == stripMute)
        return juce::isPositiveAndBelow (index, (int) RigEngine::numStrips) ? &stripSlots[(size_t) index] : nullptr;
    if (f == loopMute)
        return juce::isPositiveAndBelow (index, RigLooper::kLayers) ? &loopSlots[(size_t) index] : nullptr;
    return juce::isPositiveAndBelow ((int) f, (int) numFields) ? &fieldSlots[(size_t) f] : nullptr;
}

//==============================================================================
// Hooks (any thread: atomics only).
void RigDirty::noteChange()
{
    if (isEnabled())
        changeGeneration.fetch_add (1);
}

void RigDirty::noteMidi()
{
    if (isEnabled())
        midiNoted.store (true, std::memory_order_relaxed);
}

void RigDirty::runtimeParam (juce::AudioProcessorParameter* p, float newValue)
{
    if (! isEnabled())
        return;
    const auto it = byParam.find (p);
    if (it == byParam.end())
        return;
    auto& s = it->second->slot;
    const float now = p->getValue();
    // The first playback write since you set it (or you set it since playback last did): yours
    // is the value it had.
    if (! s.active.load() || now != s.runtime.load())
        s.authored = now;
    s.runtime = newValue;
    s.writer = juce::Thread::getCurrentThreadId();
    s.writing = true;
    s.active = true;
}

void RigDirty::runtimeParamWritten (juce::AudioProcessorParameter* p)
{
    if (! isEnabled())
        return;
    const auto it = byParam.find (p);
    if (it == byParam.end())
        return;
    auto& s = it->second->slot;
    s.runtime = p->getValue();   // (its range may have rounded it)
    s.writing = false;
}

void RigDirty::runtimeField (Field f, int index, float before, float after)
{
    if (! isEnabled())
        return;
    auto* s = slotFor (f, index);
    if (s == nullptr)
        return;
    if (! s->active.load() || before != s->runtime.load())
        s->authored = before;
    s->runtime = after;
    s->active = true;
}

void RigDirty::authoredField (Field f, int index)
{
    if (! isEnabled())
        return;
    if (auto* s = slotFor (f, index))
    {
        s->edits.fetch_add (1);
        s->poisoned = false;
        s->active = false;
    }
    noteChange();
}

//==============================================================================
RigDirty::State RigDirty::getState() const
{
    if (! isEnabled() || ! hasBaselineFlag.load())
        return State::pending;
    const auto g = publishedGeneration.load (std::memory_order_acquire);
    if (g == 0 || g != changeGeneration.load())
        return State::pending;
    const auto s = (State) publishedState.load (std::memory_order_acquire);
    if (publishedGeneration.load (std::memory_order_acquire) != g)
        return State::pending;
    return s;
}

RigDirty::Status RigDirty::getLastResolved() const
{
    Status s;
    s.state = (State) resolvedState.load();
    s.provisional = resolvedProvisional.load();
    s.changeGeneration = resolvedGeneration.load();
    s.baselineId = resolvedBaseline.load();
    return s;
}

void RigDirty::publish (const Status& s)
{
    publishedGeneration.store (0, std::memory_order_release);
    publishedState.store ((int) s.state, std::memory_order_release);
    publishedProvisional.store (s.provisional);
    publishedGeneration.store (s.changeGeneration, std::memory_order_release);
    if (s.state != State::pending)
    {
        resolvedState = (int) s.state;
        resolvedProvisional = s.provisional;
        resolvedGeneration = s.changeGeneration;
        resolvedBaseline = s.baselineId;
    }
}

void RigDirty::say (const juce::String& text)
{
    if (log)
        log (text);
}

RigDirty::Stats RigDirty::getStats() const
{
    const std::lock_guard<std::mutex> l (statsLock);
    auto s = stats;
    s.notes = changeGeneration.load() - notesAtReset;
    return s;
}

void RigDirty::resetStats()
{
    const std::lock_guard<std::mutex> l (statsLock);
    stats = {};
    notesAtReset = changeGeneration.load();
}

//==============================================================================
// Hashing.
void RigDirty::post (Job job)
{
    if (hasher == nullptr)
        return;
    {
        const std::lock_guard<std::mutex> l (jobsLock);
        jobs.push_back (std::move (job));
    }
    {
        const std::lock_guard<std::mutex> l (statsLock);
        ++stats.hashJobsPosted;
    }
    hasher->notify();
}

bool RigDirty::drain()
{
    std::vector<Done> got;
    {
        const std::lock_guard<std::mutex> l (doneLock);
        std::swap (got, done);
    }
    for (auto& d : got)
    {
        {
            const std::lock_guard<std::mutex> l (statsLock);
            ++stats.hashJobsCompleted;
            stats.bytesHashed += d.bytes;
            stats.hashMs += d.ms;
            stats.maxJobMs = juce::jmax (stats.maxJobMs, d.ms);
            stats.copyMs += d.copyMs;
        }
        if (d.job.buffer == nullptr)
        {
            const int layer = d.job.layer;
            layerInFlight[(size_t) layer] = false;
            if (d.empty)
                continue;
            // Only under its own identity, and only while that's still the loop's.
            if (d.versionOfCopy >= 0 && d.versionOfCopy == engine.getLooper().getLayer (layer).version.load())
            {
                layerCache[(size_t) layer] = { d.versionOfCopy, d.hash };
                for (auto it = takeAliases.begin(); it != takeAliases.end();)
                    if (it->second.layer == layer && it->second.version == d.versionOfCopy)
                    {
                        takeHashes[it->first] = d.hash;
                        it = takeAliases.erase (it);
                    }
                    else
                        ++it;
            }
            else
            {
                const std::lock_guard<std::mutex> l (statsLock);
                ++stats.staleResults;
            }
        }
        else if (d.job.takeId != 0)
        {
            takesInFlight.erase (d.job.takeId);
            takeHashes[d.job.takeId] = d.hash;
        }
        else
        {
            buffersInFlight.erase (d.job.buffer.get());
            pinned[d.job.buffer.get()] = { d.job.buffer, d.hash };
        }
    }
    return ! got.empty();
}

bool RigDirty::lookupLayer (int layer, int version, juce::uint64& hash)
{
    auto& c = layerCache[(size_t) layer];
    if (c.version == version && version >= 0)
    {
        hash = c.hash;
        return true;
    }
    // A bank's buffer swapped onto it: the same sound, once that swap is the one it holds.
    auto& alias = swapAliases[(size_t) layer];
    if (alias.serial != 0 && ! engine.getLooper().isSwapPending (layer))
    {
        int left = -1;
        const int serial = engine.getLooper().getAppliedSwap (layer, left);
        if (serial != alias.serial || alias.buffer == nullptr)
            alias = {};
        else if (left == version)
        {
            juce::uint64 h = 0;
            if (lookupBuffer (alias.buffer, h))
            {
                c = { version, h };
                alias = {};
                hash = h;
                const std::lock_guard<std::mutex> l (statsLock);
                ++stats.seedsUsed;
                return true;
            }
            return false;   // (lookupBuffer has asked for the buffer's hash)
        }
        else
            alias = {};
    }
    return false;
}

bool RigDirty::lookupBuffer (const std::shared_ptr<const juce::AudioBuffer<float>>& b, juce::uint64& hash)
{
    if (b == nullptr)
        return false;
    if (const auto it = pinned.find (b.get()); it != pinned.end() && it->second.buffer == b)
    {
        hash = it->second.hash;
        return true;
    }
    if (buffersInFlight.insert (b.get()).second)
    {
        Job job;
        job.buffer = b;   // pinned until it's hashed
        post (std::move (job));
    }
    return false;
}

bool RigDirty::lookupTake (juce::uint64 id, juce::uint64& hash)
{
    if (const auto it = takeHashes.find (id); it != takeHashes.end())
    {
        hash = it->second;
        return true;
    }
    if (const auto a = takeAliases.find (id); a != takeAliases.end())
    {
        const auto& cache = layerCache[(size_t) a->second.layer];
        if (cache.version == a->second.version)
        {
            hash = takeHashes[id] = cache.hash;
            takeAliases.erase (a);
            const std::lock_guard<std::mutex> l (statsLock);
            ++stats.seedsUsed;
            return true;
        }
        if (engine.getLooper().getLayer (a->second.layer).version.load() != a->second.version)
            takeAliases.erase (a);   // the layer's moved on: the take is hashed itself
    }
    return false;
}

//==============================================================================
// The canonical show.
void RigDirty::flatten (const juce::ValueTree& show, Flat& out)
{
    flattenTree (show, "Show", out, false);
}

void RigDirty::playbackValues (Flat& f, int currentBank)
{
    static const std::string edited = "(edited while a save took it)";
    // Parameters playback wrote: the value you set (the tree was read first, then the slots).
    for (auto& w : watches)
    {
        if (w->key.empty() || ! w->slot.active.load())
            continue;
        const float authored = w->slot.authored.load();
        const bool poisoned = w->slot.poisoned.load();
        if (auto it = f.find (w->key); it != f.end())
            it->second = poisoned ? edited
                       : w->hasAdopted && juce::exactlyEqual (authored, w->adoptedValue) ? w->adoptedText
                                                                                       : w->asStored (authored);
    }
    // Fields playback wrote: yours, while they still hold what playback left.
    auto field = [&f] (Slot& s, const std::string& key, int kind)
    {
        if (key.empty() || ! s.active.load())
            return;
        const float runtime = s.runtime.load(), authored = s.authored.load();
        const bool poisoned = s.poisoned.load();
        if (auto it = f.find (key); it != f.end() && it->second == stored (kind, runtime))
            it->second = poisoned ? edited : stored (kind, authored);
    };
    for (int s = 0; s < RigEngine::numStrips; ++s)
        field (stripSlots[(size_t) s], stripKeys[(size_t) s], stripMute);
    for (int l = 0; l < RigLooper::kLayers; ++l)
        field (loopSlots[(size_t) l], loopKeys[(size_t) l], loopMute);
    for (int k = vocoderOn; k < numFields; ++k)
        field (fieldSlots[(size_t) k], fieldKeys[(size_t) k], k);

    // Other banks' mutes that playback left in them when they were kept.
    for (const auto& [bank, pairs] : bankMutes)
    {
        if (bank == currentBank)
            continue;
        for (int l = 0; l < RigLooper::kLayers; ++l)
        {
            const auto& p = pairs[(size_t) l];
            if (! p.set)
                continue;
            const auto key = "Show/LoopBanks/Bank[" + std::to_string (bank) + "]/Loop[index=" + std::to_string (l) + "]@mute";
            if (auto it = f.find (key); it != f.end() && it->second == stored (loopMute, p.runtime))
                it->second = stored (loopMute, p.authored);
        }
    }
}

void RigDirty::normalise (Flat& f, int currentBank, bool withPlayback)
{
    f.erase (std::string ("Show@") + rigshow::kRevisionAttribute);
    // The window writes the dark panel's "off" out when it first shows (it reads a missing one as off).
    if (auto it = f.find ("Show/Screens@dark"); it != f.end() && it->second == "0")
        f.erase (it);

    // (A save's tree as written keeps playback's values: only the rest applies to it.)
    if (withPlayback)
        playbackValues (f, currentBank);

    // The layers are the current bank's: filed under its place, like the other banks.
    const std::string from = "Show/Loop[", to = "Show/LoopBanks/Bank[" + std::to_string (currentBank) + "]/Loop[";
    Flat moved;
    for (auto it = f.lower_bound (from); it != f.end() && it->first.compare (0, from.size(), from) == 0;)
    {
        moved[to + it->first.substr (from.size())] = it->second;
        it = f.erase (it);
    }
    f.insert (moved.begin(), moved.end());
}

void RigDirty::currentAudio (Canon& c)
{
    auto& looper = engine.getLooper();
    const int cur = engine.getCurrentLoopBank();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& layer = looper.getLayer (i);
        const int v0 = layer.version.load();
        const auto st = looper.getState (i);
        const int v1 = layer.version.load();
        if (looper.isSwapPending (i))
            c.transition = true;
        if (st == RigLooper::State::recording || st == RigLooper::State::overdubbing)
        {
            AudioRef r;
            r.present = r.busy = true;
            c.audio[bankKey (cur, i)] = r;
            c.busy = true;
            continue;
        }
        if (st != RigLooper::State::playing)
            continue;
        AudioRef r;
        r.present = true;
        r.id.kind = rigshow::Asset::Kind::liveLayer;
        r.id.layer = i;
        r.id.version = v1;
        if (v0 == v1)
        {
            r.known = lookupLayer (i, v1, r.hash);
            if (! r.known && ! layerInFlight[(size_t) i])
            {
                layerInFlight[(size_t) i] = true;
                Job job;
                job.layer = i;
                job.version = v1;
                post (std::move (job));
                const std::lock_guard<std::mutex> l (statsLock);
                ++stats.layerCopies;
            }
        }
        c.audio[bankKey (cur, i)] = r;
    }
    if (engine.getQueuedLoopBank() >= 0)
        c.transition = true;

    const auto& banks = engine.peekLoopBanks();
    for (int b = 0; b < (int) banks.size(); ++b)
    {
        if (b == cur)
            continue;
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            const auto& l = banks[(size_t) b].layers[(size_t) i];
            if (l.audio == nullptr || l.beats <= 0 || l.audio->getNumSamples() == 0)
                continue;
            AudioRef r;
            r.present = true;
            r.id.kind = rigshow::Asset::Kind::bank;
            r.id.buffer = l.audio;
            r.id.address = l.audio.get();
            r.known = lookupBuffer (l.audio, r.hash);
            c.audio[bankKey (b, i)] = r;
        }
    }

    for (const auto& set : engine.getTakes().getSets())
        for (const auto& take : set.takes)
        {
            if (take.audio.getNumSamples() == 0)
                continue;
            AudioRef r;
            r.present = true;
            r.id.kind = rigshow::Asset::Kind::take;
            r.id.takeId = take.id;
            r.known = lookupTake (take.id, r.hash);
            if (! r.known && takeAliases.count (take.id) == 0 && takesInFlight.insert (take.id).second)
            {
                // Its sound isn't on a layer we can hash: a copy, hashed on the other thread.
                const auto t0 = nowMs();
                Job job;
                job.buffer = std::make_shared<const juce::AudioBuffer<float>> (take.audio);
                job.takeId = take.id;
                {
                    const std::lock_guard<std::mutex> l (statsLock);
                    ++stats.takeFallbacks;
                    stats.takeFallbackMs += nowMs() - t0;
                }
                post (std::move (job));
            }
            c.audio[takeKey (set.layer, take.number)] = r;
        }
}

RigDirty::Canon RigDirty::build (const juce::ValueTree& show, bool withAudio)
{
    Canon c;
    flatten (show, c.flat);
    const auto banks = show.getChildWithName ("LoopBanks");
    normalise (c.flat, banks.isValid() ? (int) banks.getProperty ("current", 0) : 0);
    if (withAudio)
        currentAudio (c);
    return c;
}

void RigDirty::resolveBaseline()
{
    if (baseline == nullptr)
        return;
    for (auto& [key, r] : baseline->audio)
    {
        if (! r.present || r.known)
            continue;
        switch (r.id.kind)
        {
            case rigshow::Asset::Kind::liveLayer:
                if (const auto& cache = layerCache[(size_t) r.id.layer]; cache.version == r.id.version)
                    r.known = true, r.hash = cache.hash;
                break;
            case rigshow::Asset::Kind::bank:
                if (auto b = r.id.buffer.lock())
                    r.known = lookupBuffer (b, r.hash);
                break;
            case rigshow::Asset::Kind::take:
                r.known = lookupTake (r.id.takeId, r.hash);
                break;
            case rigshow::Asset::Kind::other:
            default:
                break;
        }
    }
}

void RigDirty::prune()
{
    // A bank buffer no bank holds any more is let go (and with it its address).
    std::set<const void*> held;
    for (const auto& bank : engine.peekLoopBanks())
        for (const auto& l : bank.layers)
            if (l.audio != nullptr)
                held.insert (l.audio.get());
    for (const auto& a : swapAliases)
        if (a.buffer != nullptr)
            held.insert (a.buffer.get());
    for (auto it = pinned.begin(); it != pinned.end();)
        it = held.count (it->first) > 0 ? std::next (it) : pinned.erase (it);
    // Takes that are gone.
    std::set<juce::uint64> ids;
    for (const auto& set : engine.getTakes().getSets())
        for (const auto& take : set.takes)
            ids.insert (take.id);
    if (baseline != nullptr)
        for (const auto& [key, r] : baseline->audio)
            if (r.id.kind == rigshow::Asset::Kind::take)
                ids.insert (r.id.takeId);
    for (auto it = takeHashes.begin(); it != takeHashes.end();)
        it = ids.count (it->first) > 0 ? std::next (it) : takeHashes.erase (it);
    for (auto it = takeAliases.begin(); it != takeAliases.end();)
        it = ids.count (it->first) > 0 ? std::next (it) : takeAliases.erase (it);
}

RigDirty::Status RigDirty::verdict (const Canon& c) const
{
    Status s;
    s.baselineId = baselineId;
    if (baseline == nullptr || c.transition)
        return s;   // pending
    if (c.flat != baseline->flat)
    {
        s.state = State::dirty;
        return s;
    }
    if (c.busy)
    {
        s.state = State::dirty;
        s.provisional = true;
        return s;
    }
    bool differs = false;
    int unresolved = 0;
    auto compare = [&] (const AudioRef* saved, const AudioRef* now)
    {
        const bool a = saved != nullptr && saved->present, b = now != nullptr && now->present;
        if (a != b)
        {
            differs = true;
            return;
        }
        if (! a)
            return;
        if (saved->id.sameAs (now->id))
            return;
        if (saved->known && now->known)
            differs = differs || saved->hash != now->hash;
        else
            ++unresolved;
    };
    for (const auto& [key, r] : baseline->audio)
    {
        const auto it = c.audio.find (key);
        compare (&r, it != c.audio.end() ? &it->second : nullptr);
    }
    for (const auto& [key, r] : c.audio)
        if (baseline->audio.count (key) == 0)
            compare (nullptr, &r);
    s.unresolvedAudio = unresolved;
    s.state = differs ? State::dirty : unresolved > 0 ? State::pending : State::clean;
    return s;
}

RigDirty::Status RigDirty::runCheck (bool& torn)
{
    const auto t0 = nowMs();
    const auto generation = changeGeneration.load();
    const int epoch0 = fieldEpoch.load();
    const auto show = engine.saveShow();
    const double treeMs = nowMs() - t0;
    auto canon = build (show, true);   // the tree, then the slots
    const int epoch1 = fieldEpoch.load();
    if (log)
    {
        // The hosted plugins' own state is part of every check (an Arturia synth's can be big).
        juce::int64 pluginBytes = 0;
        for (const auto child : show)
            if (child.hasType ("Instrument"))
                if (auto x = child.createXml())
                    pluginBytes += x->toString().length();
        if (pluginBytes > 0)
            say ("plugin state " + juce::String (pluginBytes) + " bytes; the show's tree took " + juce::String (treeMs, 1) + " ms");
    }
    torn = (epoch0 & 1) != 0 || epoch0 != epoch1;
    const double ms = nowMs() - t0;
    {
        const std::lock_guard<std::mutex> l (statsLock);
        ++stats.checks;
        stats.lastCheckMs = ms;
        stats.maxCheckMs = juce::jmax (stats.maxCheckMs, ms);
        if (torn)
            ++stats.retries;
    }
    if (ms > 5.0)
        say ("check took " + juce::String (ms, 1) + " ms");
    Status s;
    s.changeGeneration = generation;
    s.baselineId = baselineId;
    if (torn)
        return s;
    resolveBaseline();
    prune();
    {
        const std::lock_guard<std::mutex> l (statsLock);
        stats.pinnedBuffers = (int) pinned.size();
    }
    s = verdict (canon);
    s.changeGeneration = generation;
    last = std::move (canon);
    return s;
}

RigDirty::Status RigDirty::checkNow()
{
    if (! isEnabled())
        return {};
    drain();
    // What the watched counters say now is part of this check (so the timer doesn't check again for it).
    watched = readWatched();
    bool torn = false;
    auto s = runCheck (torn);
    evaluatedGeneration = s.changeGeneration;
    if (evaluatedGeneration == changeGeneration.load())
        firstNoteMs = 0;
    if (torn)
    {
        retryPending = true;
        return s;
    }
    retryPending = false;
    // Only a result from the newest change replaces what's published.
    if (s.changeGeneration == changeGeneration.load() && s.changeGeneration >= publishedGeneration.load())
        publish (s);
    else
    {
        const std::lock_guard<std::mutex> l (statsLock);
        ++stats.discarded;
    }
    lastStatus = s;
    if (s.state != loggedState)
    {
        loggedState = s.state;
        say (juce::String ("state ") + (s.state == State::clean ? "clean" : s.state == State::dirty ? (s.provisional ? "dirty (recording)" : "dirty") : "pending")
             + (s.unresolvedAudio > 0 ? " (" + juce::String (s.unresolvedAudio) + " sounds being hashed)" : juce::String()));
    }
    return s;
}

bool RigDirty::waitUntilResolved (int timeoutMs)
{
    if (! isEnabled())
        return false;
    const auto end = nowMs() + timeoutMs;
    for (;;)
    {
        const auto s = checkNow();
        if (s.state != State::pending && s.changeGeneration == changeGeneration.load())
            return true;
        if (baseline == nullptr || nowMs() >= end)
            return false;
        // (A short wait for the hashing thread; a bank switch or a busy check just tries again.)
        resultsReady.wait (juce::jlimit (1, 50, (int) (end - nowMs())));
    }
}

juce::StringArray RigDirty::differences (int max) const
{
    juce::StringArray out;
    if (baseline == nullptr)
        return out;
    auto add = [&] (const juce::String& s) { if (out.size() < max) out.add (s); };
    const auto& a = baseline->flat;
    const auto& b = last.flat;
    for (const auto& [k, v] : a)
    {
        const auto it = b.find (k);
        if (it == b.end())
            add (juce::String (k) + ": " + juce::String (v).substring (0, 40) + " -> (gone)");
        else if (it->second != v)
            add (juce::String (k) + ": " + juce::String (v).substring (0, 40) + " -> " + juce::String (it->second).substring (0, 40));
    }
    for (const auto& [k, v] : b)
        if (a.count (k) == 0)
            add (juce::String (k) + ": (new) " + juce::String (v).substring (0, 40));
    auto desc = [] (const AudioRef* r) -> juce::String
    {
        if (r == nullptr || ! r->present)
            return "(none)";
        if (r->busy)
            return "(recording)";
        return r->known ? juce::String::toHexString ((juce::int64) r->hash) : juce::String ("(hashing)");
    };
    for (const auto& [k, r] : baseline->audio)
    {
        const auto it = last.audio.find (k);
        const AudioRef* now = it != last.audio.end() ? &it->second : nullptr;
        if (now == nullptr || r.present != now->present || now->busy || ! r.id.sameAs (now->id))
            if (now == nullptr || ! (r.known && now->known && r.hash == now->hash))
                add ("Audio/" + juce::String (k) + ": " + desc (&r) + " -> " + desc (now));
    }
    for (const auto& [k, r] : last.audio)
        if (baseline->audio.count (k) == 0)
            add ("Audio/" + juce::String (k) + ": (none) -> " + desc (&r));
    return out;
}

//==============================================================================
// Service: compares a few counters; checks when something was noted and it's gone quiet.
RigDirty::Watched RigDirty::readWatched() const
{
    Watched w;
    auto& looper = engine.getLooper();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto st = looper.getState (i);
        // A loop recording or overdubbing changes every block; it's dirty meanwhile, and its
        // stopping is what counts.
        const bool busy = st == RigLooper::State::recording || st == RigLooper::State::overdubbing;
        w.versions[(size_t) i] = busy ? -2 : looper.getLayer (i).version.load();
        w.states[(size_t) i] = (int) st;
        w.swaps[(size_t) i] = looper.isSwapPending (i);
    }
    w.takes = engine.getTakes().getGeneration();
    w.queuedBank = engine.getQueuedLoopBank();
    w.automation = engine.getAutomation().getVersion();
    int macros = engine.getMacros().getVersion();
    for (int m = 0; m < RigMacros::kMacros; ++m)
        macros = macros * 31 + juce::roundToInt (engine.getMacros().value[(size_t) m].load() * 100000.0f);
    w.macros = macros;
    w.timeline = engine.getTimeline().getEdits();
    w.banks = engine.getNumLoopBanks() * 1000 + engine.getCurrentLoopBank();
    return w;
}

void RigDirty::service()
{
    if (! isEnabled())
        return;
    if (drain())
        resultsArrived = true;
    if (const auto w = readWatched(); ! (w == watched))
    {
        {
            const std::lock_guard<std::mutex> l (statsLock);
            auto& by = stats.watchedBy;
            by[Stats::byVersion] += w.versions != watched.versions ? 1 : 0;
            by[Stats::byState] += w.states != watched.states ? 1 : 0;
            by[Stats::bySwap] += w.swaps != watched.swaps ? 1 : 0;
            by[Stats::byTakes] += w.takes != watched.takes ? 1 : 0;
            by[Stats::byQueuedBank] += w.queuedBank != watched.queuedBank ? 1 : 0;
            by[Stats::byAutomation] += w.automation != watched.automation ? 1 : 0;
            by[Stats::byMacros] += w.macros != watched.macros ? 1 : 0;
            by[Stats::byTimeline] += w.timeline != watched.timeline ? 1 : 0;
            by[Stats::byBanks] += w.banks != watched.banks ? 1 : 0;
        }
        watched = w;
        noteChange();
    }
    const double now = nowMs();
    const auto g = changeGeneration.load();
    if (g != seenGeneration)
    {
        seenGeneration = g;
        lastNoteMs = now;
        if (firstNoteMs <= 0 && g != evaluatedGeneration)
            firstNoteMs = now;
    }
    if (midiNoted.exchange (false))
    {
        lastMidiMs = now;
        if (! midiPending)
            firstMidiMs = now;
        midiPending = true;
    }
    bool due = resultsArrived || retryPending;
    if (g != evaluatedGeneration && firstNoteMs > 0 && (now - lastNoteMs >= 150.0 || now - firstNoteMs >= 1000.0))
        due = true;
    if (midiPending && (now - lastMidiMs >= 1500.0 || now - firstMidiMs >= 5000.0))
        due = true;
    if (! due)
        return;
    resultsArrived = false;
    midiPending = false;
    checkNow();
}

//==============================================================================
// Baselines.
void RigDirty::setBaseline (std::unique_ptr<Baseline> b)
{
    baseline = std::move (b);
    ++baselineId;
    hasBaselineFlag = baseline != nullptr;
    noteChange();
}

std::unique_ptr<RigDirty::Baseline> RigDirty::fromCurrent (const rigshow::Committed* committed, const std::array<int, RigLooper::kLayers>* versions)
{
    if (committed != nullptr)
    {
        // What was read is what's saved: each sound's hash from the saved index, by its path.
        std::map<juce::String, juce::uint64> byPath;
        for (const auto& e : committed->entries)
            byPath[e.path] = e.decodedKey;
        int seeds = 0;
        auto& looper = engine.getLooper();
        for (int i = 0; i < RigLooper::kLayers; ++i)
            if (const auto it = byPath.find ("Loop " + juce::String (i + 1) + ".wav"); it != byPath.end() && versions != nullptr)
            {
                const int v = (*versions)[(size_t) i];
                if (v >= 0 && v == looper.getLayer (i).version.load())
                    layerCache[(size_t) i] = { v, it->second }, ++seeds;
            }
        const auto& banks = engine.peekLoopBanks();
        for (int b = 0; b < (int) banks.size(); ++b)
            for (int i = 0; i < RigLooper::kLayers; ++i)
                if (const auto& l = banks[(size_t) b].layers[(size_t) i]; l.audio != nullptr && b != engine.getCurrentLoopBank())
                    if (const auto it = byPath.find ("Bank " + juce::String (b + 1) + "/Loop " + juce::String (i + 1) + ".wav"); it != byPath.end())
                        pinned[l.audio.get()] = { l.audio, it->second }, ++seeds;
        for (const auto& set : engine.getTakes().getSets())
            for (const auto& take : set.takes)
                if (const auto it = byPath.find (takePath (set.layer, take.number)); it != byPath.end() && take.audio.getNumSamples() > 0)
                    takeHashes[take.id] = it->second, ++seeds;
        const std::lock_guard<std::mutex> l (statsLock);
        stats.seedsUsed += seeds;
    }
    auto c = build (engine.saveShow(), true);
    auto b = std::make_unique<Baseline>();
    b->flat = std::move (c.flat);
    for (auto& [key, r] : c.audio)
        if (! r.busy)
            b->audio[key] = r;
    return b;
}

void RigDirty::showLoaded()
{
    if (! isEnabled())
        return;
    resetSlots();
    bankMutes.clear();
    takeAliases.clear();
    for (auto& a : swapAliases)
        a = {};
    baseline.reset();
    hasBaselineFlag = false;
    noteChange();
}

void RigDirty::showMadeNew()
{
    if (isEnabled())
        setBaseline (fromCurrent (nullptr, nullptr));
}

void RigDirty::opened (const rigshow::Committed& committed, const std::array<int, RigLooper::kLayers>& versions)
{
    if (isEnabled())
        setBaseline (fromCurrent (&committed, &versions));
}

void RigDirty::markOpened()
{
    if (isEnabled())
        setBaseline (fromCurrent (nullptr, nullptr));
}

RigDirty::SaveTicket RigDirty::beginSave()
{
    if (! isEnabled())
        return {};
    return { ++saveSerial };
}

void RigDirty::captureSave (SaveTicket ticket, const juce::ValueTree& show, const rigshow::Staged& staged, const std::vector<rigshow::Asset>& assets)
{
    if (! isEnabled() || ticket.serial == 0)
        return;
    // What each sound hashed to as it was saved: true of that sound whatever happens next.
    auto& looper = engine.getLooper();
    int seeds = 0;
    auto cand = std::make_unique<Baseline>();
    for (size_t k = 0; k < assets.size() && k < staged.keys.size(); ++k)
    {
        const auto& a = assets[k];
        const auto hash = staged.keys[k];
        if (a.path.startsWith ("Friend track/"))
            continue;   // (a friend's track: the show's tree has its id, which is what tells it changed)
        AudioRef r;
        r.present = r.known = true;
        r.hash = hash;
        r.id.kind = a.kind;
        std::string key;
        switch (a.kind)
        {
            case rigshow::Asset::Kind::liveLayer:
                r.id.layer = a.layer;
                r.id.version = a.version;
                key = bankKey (a.bank, a.layer);
                if (a.version >= 0 && a.version == looper.getLayer (a.layer).version.load() && layerCache[(size_t) a.layer].version != a.version)
                    layerCache[(size_t) a.layer] = { a.version, hash }, ++seeds;
                break;
            case rigshow::Asset::Kind::bank:
                r.id.buffer = a.audio;
                r.id.address = a.audio.get();
                key = bankKey (a.bank, a.layer);
                if (pinned.count (a.audio.get()) == 0)
                    pinned[a.audio.get()] = { a.audio, hash }, ++seeds;
                break;
            case rigshow::Asset::Kind::take:
                r.id.takeId = a.takeId;
                key = a.path.dropLastCharacters (4).toStdString();
                if (takeHashes.count (a.takeId) == 0)
                    takeHashes[a.takeId] = hash, ++seeds;
                break;
            case rigshow::Asset::Kind::other:
            default:
                key = a.path.toStdString();
                break;
        }
        cand->audio[key] = r;
    }
    {
        const std::lock_guard<std::mutex> l (statsLock);
        stats.seedsUsed += seeds;
    }
    dropAnsweredJobs();

    // The very tree that's committed, as it's written: playback's values and all (a save makes
    // what it wrote yours). A song part coming in while it was read changes nothing here: the
    // tree is what the file has, whichever side of the part each setting was read on.
    Flat raw;
    flatten (show, raw);
    if ((ticket.epoch & 1) != 0 || ticket.epoch != fieldEpoch.load())
        say ("save: a song part came in while the tree was read (the file has what was read)");
    if (auto it = saveSlots.find (ticket.serial); it != saveSlots.end())
    {
        // What each slot's setting was written as, for adopting it.
        auto& v = it->second;
        auto read = [&raw] (const std::string& key, std::pair<bool, float>& out)
        {
            if (const auto f = raw.find (key); ! key.empty() && f != raw.end())
                out = { true, juce::String (f->second).getFloatValue() };
        };
        v.params.assign (watches.size(), { false, 0.0f });
        v.paramTexts.assign (watches.size(), {});
        for (size_t i = 0; i < watches.size(); ++i)
            if (const auto& w = watches[i]; ! w->key.empty())
                if (const auto f = raw.find (w->key); f != raw.end())
                {
                    const float plain = juce::String (f->second).getFloatValue();
                    v.params[i] = { true, w->ranged != nullptr ? w->ranged->convertTo0to1 (plain) : plain };
                    v.paramTexts[i] = f->second;
                }
        for (int i = 0; i < RigEngine::numStrips; ++i)
            read (stripKeys[(size_t) i], v.strips[(size_t) i]);
        for (int i = 0; i < RigLooper::kLayers; ++i)
            read (loopKeys[(size_t) i], v.loops[(size_t) i]);
        for (int k = vocoderOn; k < numFields; ++k)
            read (fieldKeys[(size_t) k], v.fields[(size_t) k]);
    }
    const auto banks = show.getChildWithName ("LoopBanks");
    normalise (raw, banks.isValid() ? (int) banks.getProperty ("current", 0) : 0, false);
    cand->flat = std::move (raw);
    candidates[ticket.serial] = std::move (cand);
}

void RigDirty::dropAnsweredJobs()
{
    // Jobs still queued for sounds the save has just hashed: answered, so they never run.
    int dropped = 0;
    {
        const std::lock_guard<std::mutex> l (jobsLock);
        for (auto it = jobs.begin(); it != jobs.end();)
        {
            bool answered = false;
            if (it->buffer == nullptr)
            {
                const auto& c = layerCache[(size_t) it->layer];
                answered = c.version >= 0 && c.version == it->version;
                if (answered)
                    layerInFlight[(size_t) it->layer] = false;
            }
            else if (it->takeId != 0)
            {
                answered = takeHashes.count (it->takeId) > 0;
                if (answered)
                    takesInFlight.erase (it->takeId);
            }
            else if (const auto p = pinned.find (it->buffer.get()); p != pinned.end() && p->second.buffer == it->buffer)
            {
                answered = true;
                buffersInFlight.erase (it->buffer.get());
            }
            if (answered)
            {
                it = jobs.erase (it);
                ++dropped;
            }
            else
                ++it;
        }
    }
    if (dropped > 0)
    {
        const std::lock_guard<std::mutex> l (statsLock);
        stats.jobsSatisfiedBySave += dropped;
    }
}

void RigDirty::beforeTree (SaveTicket& t)
{
    t.epoch = fieldEpoch.load();
    if (! isEnabled() || t.serial == 0)
        return;
    // Every slot's edit count now, before the tree: an edit from here on (even one racing the
    // tree being read) counts as made after the save, and the save leaves that slot alone.
    auto& v = saveSlots[t.serial];
    v.paramEdits.resize (watches.size());
    for (size_t i = 0; i < watches.size(); ++i)
        v.paramEdits[i] = watches[i]->slot.edits.load();
    for (size_t i = 0; i < stripSlots.size(); ++i)
        v.stripEdits[i] = stripSlots[i].edits.load();
    for (size_t i = 0; i < loopSlots.size(); ++i)
        v.loopEdits[i] = loopSlots[i].edits.load();
    for (size_t i = 0; i < fieldSlots.size(); ++i)
        v.fieldEdits[i] = fieldSlots[i].edits.load();
    if (afterBeforeTreeForTests)
        afterBeforeTreeForTests();
}

bool RigDirty::saveStarting()
{
    if (! isEnabled())
        return false;
    savesWaiting.fetch_add (1);
    return true;
}

void RigDirty::saveEnded()
{
    // (Counted even if the model was turned off meanwhile, so the count always comes back to 0.)
    if (savesWaiting.fetch_sub (1) == 1 && hasher != nullptr)
        hasher->notify();
}

bool RigDirty::paramSlotForTests (juce::AudioProcessorParameter* p, float& authored, bool& active) const
{
    const auto it = byParam.find (p);
    if (it == byParam.end())
        return false;
    authored = it->second->slot.authored.load();
    active = it->second->slot.active.load();
    return true;
}

bool RigDirty::fieldSlotForTests (Field f, int index, float& authored, bool& active)
{
    const auto* s = slotFor (f, index);
    if (s == nullptr)
        return false;
    authored = s->authored.load();
    active = s->active.load();
    return true;
}

RigDirty::Flat RigDirty::baselineForTests() const
{
    return baseline != nullptr ? baseline->flat : Flat();
}

void RigDirty::adopt (const SlotValues& v)
{
    // A document save made what it wrote yours: each slot's "yours" becomes the value in the
    // tree, unless you edited it after the tree's edit counts were taken. `active` and `runtime`
    // stay as they are (playback goes on; deactivating would let its next write capture a newer
    // value as yours).
    //
    // The audio thread may be in runtimeParam / runtimeField for the same slot meanwhile:
    //  - with no hand edit, its "yours" rule only sets authored to the value it found, which is
    //    the value the tree read unless playback moved it since, and then authored was already
    //    the tree's value (set here before or after: the same value either way);
    //  - a hand edit bumps `edits` before it clears `active`. One seen before the check below:
    //    the slot is left alone. One landing between the check and the store: the store may
    //    overwrite what a playback write just took as yours, so the count is read again after
    //    the store and a move "poisons" the slot (it reads as edited, never as the saved value)
    //    until your next edit of it. One after the second read clears `active` after this
    //    store, so the slot reads your value. So: right, or dirty; never clean over an edit.
    int adopted = 0, kept = 0, raced = 0;
    auto one = [&] (Slot& s, juce::uint32 before, const std::pair<bool, float>& value) -> bool
    {
        if (! value.first)
            return false;
        if (s.edits.load() != before)
        {
            ++kept;
            return false;
        }
        s.authored = value.second;
        if (s.edits.load() != before)
        {
            s.poisoned = true;
            ++raced;
            return false;
        }
        ++adopted;
        return true;
    };
    for (size_t i = 0; i < watches.size() && i < v.params.size() && i < v.paramEdits.size(); ++i)
    {
        auto& w = *watches[i];
        if (one (w.slot, v.paramEdits[i], v.params[i]))
        {
            w.hasAdopted = true;
            w.adoptedValue = v.params[i].second;
            w.adoptedText = v.paramTexts[i];
        }
    }
    for (int i = 0; i < RigEngine::numStrips; ++i)
        one (stripSlots[(size_t) i], v.stripEdits[(size_t) i], v.strips[(size_t) i]);
    for (int i = 0; i < RigLooper::kLayers; ++i)
        one (loopSlots[(size_t) i], v.loopEdits[(size_t) i], v.loops[(size_t) i]);
    for (int k = vocoderOn; k < numFields; ++k)
        one (fieldSlots[(size_t) k], v.fieldEdits[(size_t) k], v.fields[(size_t) k]);
    // The other banks' mutes as written are yours now too.
    bankMutes.clear();
    const std::lock_guard<std::mutex> l (statsLock);
    stats.slotsAdopted += adopted;
    stats.slotsKeptForEdits += kept;
    stats.slotsRacedEdits += raced;
}

void RigDirty::finishSave (SaveTicket ticket, bool ok, rigshow::SaveKind kind)
{
    if (! isEnabled() || ticket.serial == 0)
        return;
    std::unique_ptr<Baseline> cand;
    if (const auto it = candidates.find (ticket.serial); it != candidates.end())
    {
        cand = std::move (it->second);
        candidates.erase (it);
    }
    SlotValues values;
    bool haveValues = false;
    if (const auto it = saveSlots.find (ticket.serial); it != saveSlots.end())
    {
        values = std::move (it->second);
        haveValues = true;
        saveSlots.erase (it);
    }
    // Only a document save that worked, and only the newest one begun, moves the baseline (and
    // makes what it wrote yours). Any other save changes nothing here.
    if (ok && kind == rigshow::SaveKind::document && ticket.serial == saveSerial && cand != nullptr)
    {
        if (haveValues)
            adopt (values);
        setBaseline (std::move (cand));
        say ("saved: new baseline " + juce::String ((juce::int64) baselineId));
    }
}

//==============================================================================
// Loop banks.
void RigDirty::bankStored (int bank, const std::array<int, RigLooper::kLayers>& copiedVersions)
{
    if (! isEnabled())
        return;
    const auto& banks = engine.peekLoopBanks();
    if (! juce::isPositiveAndBelow (bank, (int) banks.size()))
        return;
    auto& pairs = bankMutes[bank];
    int seeds = 0;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto& l = banks[(size_t) bank].layers[(size_t) i];
        // A loop copied into the bank is the sound the layer had at that version.
        if (const int v = copiedVersions[(size_t) i]; v >= 0 && l.audio != nullptr && layerCache[(size_t) i].version == v && pinned.count (l.audio.get()) == 0)
            pinned[l.audio.get()] = { l.audio, layerCache[(size_t) i].hash }, ++seeds;
        // The mute kept with it: if it's playback's, the one you set goes with it.
        auto& s = loopSlots[(size_t) i];
        auto& p = pairs[(size_t) i];
        if (s.active.load() && (l.mute ? 1.0f : 0.0f) == s.runtime.load())
            p = { true, s.authored.load(), s.runtime.load() };
        else
            p = {};
    }
    const std::lock_guard<std::mutex> lk (statsLock);
    stats.seedsUsed += seeds;
}

void RigDirty::bankPut (int bank, const RigScene::State* scene)
{
    if (! isEnabled())
        return;
    const auto& banks = engine.peekLoopBanks();
    if (! juce::isPositiveAndBelow (bank, (int) banks.size()))
        return;
    const auto it = bankMutes.find (bank);
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const float storedMute = banks[(size_t) bank].layers[(size_t) i].mute ? 1.0f : 0.0f;
        MutePair p;
        if (it != bankMutes.end() && it->second[(size_t) i].set && it->second[(size_t) i].runtime == storedMute)
            p = it->second[(size_t) i];
        auto& s = loopSlots[(size_t) i];
        if (scene != nullptr && scene->loopMutes[(size_t) i] >= 0)
        {
            s.authored = p.set ? p.authored : storedMute;
            s.runtime = scene->loopMutes[(size_t) i] == 1 ? 1.0f : 0.0f;
            s.active = true;
        }
        else if (p.set)
        {
            s.authored = p.authored;
            s.runtime = p.runtime;
            s.active = true;
        }
        else
            s.active = false;
    }
}

void RigDirty::bankQueued (int bank, int layer, int swapSerial)
{
    if (! isEnabled() || ! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return;
    const auto& banks = engine.peekLoopBanks();
    auto& alias = swapAliases[(size_t) layer];
    alias = {};
    if (! juce::isPositiveAndBelow (bank, (int) banks.size()))
        return;
    const auto& l = banks[(size_t) bank].layers[(size_t) layer];
    // (A mono buffer is made stereo on the layer, so it's another sound.)
    if (l.audio != nullptr && l.beats > 0 && l.audio->getNumChannels() == 2 && l.audio->getNumSamples() > 0)
        alias = { swapSerial, l.audio };
}

void RigDirty::banksChanged()
{
    if (! isEnabled())
        return;
    bankMutes.clear();
    noteChange();
}

void RigDirty::takeOnLayer (juce::uint64 takeId, int layer, int version)
{
    if (! isEnabled() || version < 0 || ! juce::isPositiveAndBelow (layer, RigLooper::kLayers))
        return;
    if (const auto it = takeHashes.find (takeId); it != takeHashes.end())
    {
        layerCache[(size_t) layer] = { version, it->second };
        const std::lock_guard<std::mutex> l (statsLock);
        ++stats.seedsUsed;
    }
    else
        takeAliases[takeId] = { layer, version };
    swapAliases[(size_t) layer] = {};
    noteChange();
}
