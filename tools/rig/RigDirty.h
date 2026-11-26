#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "RigLooper.h"
#include "RigScenes.h"
#include "RigShowFiles.h"

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class RigEngine;

// Whether the show differs from what was last opened or saved ("dirty"), without being fooled
// by playback.
//
// The show's authored state is compared with the baseline (the show as it was opened, made new,
// or saved as the document): the show's tree flattened to keys and values, plus a hash of every
// loop, other bank's loop and take. Playback writes settings all the time (automation, macros,
// the song's parts and sections, a section's loop bank, the screen the keys follow), and saving
// would keep what playback left; those writes go through the runtime hooks below, which keep the
// value you set, so the comparison uses yours. A setting you change yourself afterwards (by hand,
// a learned knob, loading a show) is yours again: for parameters the parameter's own listener
// says so; for the other fields the explicit controls call authoredField.
//
// Audio is never copied or hashed on the message thread for loops or banks: a loop's sound is
// named by (layer, version) and a bank's by its buffer (held by the cache, so its address can't
// be reused), a take by its id; hashes come from the saved show's index when it's opened or
// saved, from the sound it was copied from when that's known, and otherwise from a low-priority
// thread here (which copies a playing loop the looper's way, without the audio thread waiting).
// A loop that's recording or overdubbing is "dirty, provisionally" and isn't copied until it stops.
//
// State: pending means not known yet (a change not looked at, a hash not ready, a bank switch
// waiting for its bar, nothing to compare with). It's never taken as clean.
//
// Threads. The hooks marked "any thread" are atomics only (the audio thread calls them). The
// rest is the message thread's. Off by default (the plugins never switch it on): switched off,
// every hook is one relaxed load and nothing runs.
class RigDirty
{
public:
    enum class State { clean, dirty, pending };
    struct Status
    {
        State state = State::pending;
        bool provisional = false;   // dirty only because a loop is recording or overdubbing
        juce::uint64 changeGeneration = 0, baselineId = 0;
        int unresolvedAudio = 0;    // sounds whose hash isn't known yet
    };
    // The fields playback writes besides parameters.
    enum Field { stripMute, loopMute, vocoderOn, inputOn, mpcBeat, timelineOn, songMode, bankCurrent, keysTo, padsTo, numFields };
    static constexpr int kMaxStrips = 16;

    using Flat = std::map<std::string, std::string>;

    explicit RigDirty (RigEngine&);
    ~RigDirty();

    // Message thread. Off by default. Switching on resets the runtime record and starts the
    // hashing thread; there's no baseline until the show is next opened, made new or saved.
    void setEnabled (bool);
    bool isEnabled() const { return enabled.load (std::memory_order_relaxed); }

    // Any thread, cheap.
    State getState() const;
    // The last clean or dirty verdict (pending before any).
    Status getLastResolved() const;

    //==========================================================================
    // Hooks. Any thread unless said.
    void noteChange();
    void noteMidi();
    void noteActivity() { noteChange(); }   // a click, a key, a wheel (the window)

    // Just before playback writes a parameter (newValue 0..1), and just after.
    void runtimeParam (juce::AudioProcessorParameter*, float newValue);
    void runtimeParamWritten (juce::AudioProcessorParameter*);
    // Just before playback writes a field, with its value before and after.
    void runtimeField (Field, int index, float before, float after);
    // After you set a field with an explicit control (a mute button, a bank button).
    void authoredField (Field, int index);
    // Around the audio thread's field writes (a song part coming in): a check reading the fields
    // meanwhile is thrown away and tried again.
    void beginAudioFields() { if (isEnabled()) fieldEpoch.fetch_add (1); }
    void endAudioFields() { if (isEnabled()) fieldEpoch.fetch_add (1); }

    // Message thread, from the engine.
    void showLoaded();                         // loadShow: no baseline until it's opened
    void showMadeNew();                        // newShow: the blank show is the baseline
    // readLoopAudio's end: the show as it is now, with the hashes of what was read.
    void opened (const rigshow::Committed&, const std::array<int, RigLooper::kLayers>& layerVersions);
    void markOpened();                         // a show loaded with no audio step
    struct SaveTicket { juce::uint64 serial = 0; int epoch = 0; };
    SaveTicket beginSave();
    // Just before the save takes the show's tree.
    // Just before the save reads the show's tree: notes which settings you'd edited by then.
    void beforeTree (SaveTicket&);
    // `show`: the tree being committed; `staged`: its audio's hashes; `assets`: what they are.
    void captureSave (SaveTicket, const juce::ValueTree& show, const rigshow::Staged&, const std::vector<rigshow::Asset>&);
    void finishSave (SaveTicket, bool ok, rigshow::SaveKind);
    // A save is copying loops (any thread that saves): the hashing thread starts no copy of its
    // own meanwhile. Use SaveGuard, which covers every way out of the save.
    bool saveStarting();
    void saveEnded();
    struct SaveGuard
    {
        explicit SaveGuard (RigDirty& d) : dirty (d), counted (d.saveStarting()) {}
        ~SaveGuard() { if (counted) dirty.saveEnded(); }
        RigDirty& dirty;
        const bool counted;
        JUCE_DECLARE_NON_COPYABLE (SaveGuard)
    };
    // Loop banks: the layers were kept in `bank` (with the versions copied, -1 for not copied);
    // `bank` is going onto the layers (with the swaps' serials), before it's queued and after;
    // banks were added, removed or cleared by you.
    void bankStored (int bank, const std::array<int, RigLooper::kLayers>& copiedVersions);
    void bankPut (int bank, const RigScene::State* scene);
    void bankQueued (int bank, int layer, int swapSerial);
    void banksChanged();
    // A take's sound went onto a layer at `version`.
    void takeOnLayer (juce::uint64 takeId, int layer, int version);

    // A show's own hosted plugin going live (message thread, from PluginSlot via the engine): one
    // restored from the show's tree, never one picked by hand. Until then every save writes the
    // show's tree for it; once it's in, it reports its state in its own bytes (an Arturia synth
    // doesn't hand back the bytes it was given), which would read as a change nobody made.
    // pluginLanding just before the slot reports the plugin (`slotKey`: where that slot's plugin
    // is in the flattened show, before the loops are filed under their bank), pluginLanded just
    // after (its state and knobs in). Only that slot's plugin entries can move, in the baseline
    // and in a save's candidate, from what the show had to what the plugin reports now, and
    // only if the baseline had exactly what the show had there (all of them, or none); nothing
    // else in the baseline changes. `levelAndBypassAsShowHad`: false when the slot's level or
    // bypass was changed while it loaded: those are yours, not the plugin's, and never move.
    // No baseline (or switched off): nothing (the baseline, when it's taken, has the plugin).
    void pluginLanding (const std::string& slotKey);
    void pluginLanded (bool levelAndBypassAsShowHad);

    // Message thread, from the engine's timer: looks at a few counters, and checks when
    // something changed and things have been quiet for a moment.
    void service();
    // Message thread: compares now (never hashes here; pending if hashes are missing).
    Status checkNow();
    // Services until the state isn't pending, or the time is up. True if it resolved. Works
    // without a message loop (tests, Save and Quit).
    bool waitUntilResolved (int timeoutMs);
    // What differs from the baseline, as of the last check ("key: saved -> now").
    juce::StringArray differences (int max) const;
    // Tools whose settings couldn't be matched to their saved form (no playback override for them).
    juce::StringArray unmatchedTools() const;

    // Tests only: a slot's state (false if there's none), the baseline's settings, and a call
    // made just after beforeTree (to change things between it and the tree being read).
    bool paramSlotForTests (juce::AudioProcessorParameter*, float& authored, bool& active) const;
    bool fieldSlotForTests (Field, int index, float& authored, bool& active);
    Flat baselineForTests() const;
    static inline std::function<void()> afterBeforeTreeForTests;
    // Tests only: a show's tree as the comparison sees it, and the old way of comparing a hosted
    // plugin's description (with the time JUCE stamps on it), to show the tests catch it.
    static Flat flattenForTests (const juce::ValueTree& show);
    static inline bool comparePluginInfoTimeForTests = false;
    // Tests only: a show's plugin going live leaves the baseline as it was (the old fault).
    static inline bool noLandingRebaseForTests = false;

    struct Stats
    {
        int checks = 0, retries = 0, discarded = 0;
        double lastCheckMs = 0, maxCheckMs = 0;
        int hashJobsPosted = 0, hashJobsCompleted = 0, staleResults = 0, seedsUsed = 0, layerCopies = 0;
        int jobsStarted = 0;            // jobs the hashing thread began (copies included)
        int jobsSatisfiedBySave = 0;    // queued jobs dropped because a save's own hashes answered them
        int slotsAdopted = 0, slotsKeptForEdits = 0, slotsRacedEdits = 0;   // document saves making what they wrote yours
        int takeFallbacks = 0;
        int pinnedBuffers = 0;   // bank sounds the cache holds, after the last check
        double maxJobMs = 0;     // the longest single copy-and-hash
        double copyMs = 0;       // of hashMs, the part spent copying live loops
        // What asked for checks: changes noted (any source), and which watched counter moved.
        juce::uint64 notes = 0;
        enum { byVersion, byState, bySwap, byTakes, byQueuedBank, byAutomation, byMacros, byTimeline, byBanks, numWatched };
        std::array<int, numWatched> watchedBy {};
        double takeFallbackMs = 0;
        juce::int64 bytesHashed = 0;
        double hashMs = 0;
        // A show's plugins going live: the baseline's entries for it moved; left (the baseline
        // didn't have what the show had there); nothing differed; no baseline yet. The keys
        // moved, and what the two looks at the show cost (milliseconds, message thread).
        int pluginLandingsRebased = 0, pluginLandingsLeft = 0, pluginLandingsSame = 0, pluginLandingsNoBaseline = 0;
        int pluginLandingKeysMoved = 0;
        double lastPluginLandingMs = 0, maxPluginLandingMs = 0;
    };
    Stats getStats() const;
    void resetStats();
    // Called (message thread) with a line for each state change, each check over 5 ms, and
    // each document save (the app's dirty.log).
    std::function<void (const juce::String&)> log;

private:
    struct Slot
    {
        std::atomic<bool> active { false };
        std::atomic<float> authored { 0 }, runtime { 0 };
        std::atomic<bool> writing { false };
        std::atomic<juce::Thread::ThreadID> writer { nullptr };
        std::atomic<juce::uint32> edits { 0 };      // bumped whenever it's made yours (a hand edit)
        std::atomic<bool> poisoned { false };       // a hand edit raced a save taking it: reads as edited
    };
    struct ParamWatch;
    struct Hasher;

    // A sound's identity: what can say two sounds are the same without hashing them.
    struct Identity
    {
        rigshow::Asset::Kind kind = rigshow::Asset::Kind::other;
        int layer = -1, version = -1;
        std::weak_ptr<const juce::AudioBuffer<float>> buffer;
        const void* address = nullptr;
        juce::uint64 takeId = 0;
        bool sameAs (const Identity&) const;
    };
    struct AudioRef
    {
        bool present = false, busy = false, known = false;
        juce::uint64 hash = 0;
        Identity id;
    };
    using AudioMap = std::map<std::string, AudioRef>;
    struct Canon
    {
        Flat flat;
        AudioMap audio;
        bool busy = false, transition = false, torn = false;
    };
    struct Baseline
    {
        Flat flat;
        AudioMap audio;
    };

    struct Job
    {
        int layer = -1, version = -1;                            // a live loop
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;  // or a buffer (a bank's, a take's copy)
        juce::uint64 takeId = 0;
    };
    struct Done
    {
        Job job;
        int versionOfCopy = -1;
        juce::uint64 hash = 0;
        bool empty = false;
        juce::int64 bytes = 0;
        double ms = 0, copyMs = 0;
    };

    RigEngine& engine;
    std::atomic<bool> enabled { false };
    std::atomic<juce::uint64> changeGeneration { 1 };
    std::atomic<bool> midiNoted { false };
    std::atomic<int> fieldEpoch { 0 };

    std::vector<std::unique_ptr<ParamWatch>> watches;
    std::unordered_map<juce::AudioProcessorParameter*, ParamWatch*> byParam;   // built once, then only read
    std::array<Slot, kMaxStrips> stripSlots;
    std::array<Slot, RigLooper::kLayers> loopSlots;
    std::array<Slot, numFields> fieldSlots;
    Slot* slotFor (Field, int index);
    std::array<std::string, kMaxStrips> stripKeys;
    std::array<std::string, RigLooper::kLayers> loopKeys;
    std::array<std::string, numFields> fieldKeys;
    bool keysMatched = false;
    juce::StringArray unmatched;
    void matchKeys();
    void resetSlots();

    // Published (any thread reads): the result's generation, then its state.
    std::atomic<juce::uint64> publishedGeneration { 0 };
    std::atomic<int> publishedState { (int) State::pending };
    std::atomic<bool> publishedProvisional { false };
    std::atomic<bool> hasBaselineFlag { false };
    std::atomic<int> resolvedState { (int) State::pending };
    std::atomic<bool> resolvedProvisional { false };
    std::atomic<juce::uint64> resolvedGeneration { 0 }, resolvedBaseline { 0 };

    // Message thread.
    std::unique_ptr<Baseline> baseline;
    juce::uint64 baselineId = 0;
    void setBaseline (std::unique_ptr<Baseline>);
    Canon last;
    Status lastStatus;
    State loggedState = State::pending;
    juce::uint64 evaluatedGeneration = 0, seenGeneration = 0;
    double firstNoteMs = 0, lastNoteMs = 0, firstMidiMs = 0, lastMidiMs = 0;
    bool midiPending = false, retryPending = false, resultsArrived = false;

    // Watched counters (service compares them).
    struct Watched
    {
        std::array<int, RigLooper::kLayers> versions {}, states {};
        std::array<bool, RigLooper::kLayers> swaps {};
        int takes = 0, queuedBank = -1, automation = 0, macros = 0, timeline = 0, banks = 0;
        bool operator== (const Watched&) const;
    };
    Watched watched;
    Watched readWatched() const;

    // Hash caches and what's in flight (message thread).
    struct LayerCache { int version = -1; juce::uint64 hash = 0; };
    std::array<LayerCache, RigLooper::kLayers> layerCache;
    struct Pinned { std::shared_ptr<const juce::AudioBuffer<float>> buffer; juce::uint64 hash = 0; };
    std::map<const void*, Pinned> pinned;
    std::map<juce::uint64, juce::uint64> takeHashes;
    struct TakeAlias { int layer = -1, version = -1; };
    std::map<juce::uint64, TakeAlias> takeAliases;
    struct SwapAlias { int serial = 0; std::shared_ptr<const juce::AudioBuffer<float>> buffer; };
    std::array<SwapAlias, RigLooper::kLayers> swapAliases;
    std::array<bool, RigLooper::kLayers> layerInFlight {};
    std::set<const void*> buffersInFlight;
    std::set<juce::uint64> takesInFlight;
    // Bank mute migration: per (bank, layer), the mute you set and the one playback left.
    struct MutePair { bool set = false; float authored = 0, runtime = 0; };
    std::map<int, std::array<MutePair, RigLooper::kLayers>> bankMutes;

    // Saves in flight: their serial and the baseline each would make.
    juce::uint64 saveSerial = 0;
    std::map<juce::uint64, std::unique_ptr<Baseline>> candidates;
    // Per save (message thread): each slot's edit count before the tree was read, and the
    // values the tree wrote (what becomes yours if that save is adopted).
    struct SlotValues
    {
        std::vector<juce::uint32> paramEdits;
        std::array<juce::uint32, kMaxStrips> stripEdits {};
        std::array<juce::uint32, RigLooper::kLayers> loopEdits {};
        std::array<juce::uint32, numFields> fieldEdits {};
        std::vector<std::pair<bool, float>> params;
        std::vector<std::string> paramTexts;
        std::array<std::pair<bool, float>, kMaxStrips> strips {};
        std::array<std::pair<bool, float>, RigLooper::kLayers> loops {};
        std::array<std::pair<bool, float>, numFields> fields {};
    };
    std::map<juce::uint64, SlotValues> saveSlots;
    void adopt (const SlotValues&);
    void dropAnsweredJobs();
    std::atomic<int> savesWaiting { 0 };

    // A show's plugin going live (between pluginLanding and pluginLanded).
    struct Landing
    {
        bool active = false;
        std::string slotKey, prefix;
        juce::uint64 baselineId = 0;
        Flat before;
        double ms = 0;
    };
    Landing landing;
    std::string slotFlat (const std::string& slotKey, Flat& out);

    std::unique_ptr<Hasher> hasher;
    std::mutex jobsLock, doneLock;
    std::deque<Job> jobs;
    std::vector<Done> done;
    juce::WaitableEvent resultsReady;
    void post (Job);
    bool drain();

    mutable std::mutex statsLock;
    Stats stats;
    juce::uint64 notesAtReset = 0;

    Canon build (const juce::ValueTree& show, bool withAudio);
    void flatten (const juce::ValueTree& show, Flat&);
    void normalise (Flat&, int currentBank, bool withPlayback = true);
    void playbackValues (Flat&, int currentBank);
    void currentAudio (Canon&);
    bool lookupLayer (int layer, int version, juce::uint64& hash);
    bool lookupBuffer (const std::shared_ptr<const juce::AudioBuffer<float>>&, juce::uint64& hash);
    bool lookupTake (juce::uint64 id, juce::uint64& hash);
    void resolveBaseline();
    void prune();
    Status verdict (const Canon&) const;
    Status runCheck (bool& torn);
    void publish (const Status&);
    void say (const juce::String&);
    std::unique_ptr<Baseline> fromCurrent (const rigshow::Committed*, const std::array<int, RigLooper::kLayers>*);

    friend struct ParamWatch;
    JUCE_DECLARE_NON_COPYABLE (RigDirty)
};
