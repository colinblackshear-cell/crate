#include "BeatsEngine.h"

#include <algorithm>

using namespace crate;

namespace pocket
{
namespace
{
// The starting sound of each voice, as the Drum Machine plugin has it (decay, tone, snappy, level, pan).
constexpr float defaultDecay[drums::kVoices] = { 0.55f, 0.4f, 0.4f, 0.5f, 0.45f, 0.45f, 0.45f, 0.5f, 0.35f, 0.35f, 0.4f, 0.5f };
constexpr float defaultTone[drums::kVoices] = { 0.35f, 0.55f, 0.5f, 0.5f, 0.4f, 0.4f, 0.4f, 0.3f, 0.5f, 0.5f, 0.5f, 0.5f };
constexpr float defaultSnappy[drums::kVoices] = { 0.35f, 0.55f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.2f, 0.5f, 0.2f, 0.2f, 0.2f };
constexpr float defaultLevel[drums::kVoices] = { 0.9f, 0.75f, 0.7f, 0.6f, 0.7f, 0.7f, 0.7f, 0.6f, 0.55f, 0.55f, 0.5f, 0.45f };
constexpr float defaultPan[drums::kVoices] = { 0.0f, 0.0f, 0.0f, -0.1f, -0.35f, 0.0f, 0.35f, -0.25f, -0.2f, 0.2f, 0.2f, 0.15f };

drums::Pattern fromRows (const std::array<const char*, drums::kVoices>& rows, const char* accent)
{
    drums::Pattern p;
    for (int v = 0; v < drums::kVoices; ++v)
        for (int s = 0; s < 16 && rows[(size_t) v][0] != 0 && rows[(size_t) v][s] != 0; ++s)
        {
            const char c = rows[(size_t) v][s];
            p.hits[(size_t) v][(size_t) s] = c == 'x' ? 1 : (c >= '2' && c <= '4' ? (std::uint8_t) (c - '0') : 0);
        }
    for (int s = 0; s < 16 && accent[s] != 0; ++s)
        p.accent[(size_t) s] = accent[s] == '>';
    return p;
}

juce::String rowText (const std::array<std::uint8_t, drums::kMaxSteps>& row)
{
    juce::String t;
    for (auto h : row)
        t << (int) h;
    return t;
}
}

//==============================================================================
DrumData::DrumData()
{
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& s = settings.voices[(size_t) v];
        s.level = defaultLevel[v];
        s.decay = defaultDecay[v];
        s.tone = defaultTone[v];
        s.snappy = defaultSnappy[v];
        s.pan = defaultPan[v];
    }
    patterns[0][0] = demoPattern();
}

drums::Pattern DrumData::demoPattern()
{
    // A boom-bap beat: kick on 1 and the "and" of 2, snare on 2 and 4, ticking hats.
    return fromRows ({ "x....x..x.x.....", "....x.......x...", "", "...........x....", "", "", "", "", "",
                       "x.x.x.x.x.x.x.xx", "..............x.", "x..............." },
                     ">...>...>...>...");
}

juce::var DrumData::toVar() const
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("bpm", bpm);
    root->setProperty ("slot", slot);
    root->setProperty ("variation", variation);
    root->setProperty ("editB", editB);
    root->setProperty ("swing", settings.swing);

    juce::Array<juce::var> pats;
    for (auto& pair : patterns)
        for (auto& p : pair)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("length", p.length);
            juce::Array<juce::var> rows;
            for (auto& row : p.hits)
                rows.add (rowText (row));
            o->setProperty ("rows", rows);
            juce::String accent;
            for (bool a : p.accent)
                accent << (a ? "1" : "0");
            o->setProperty ("accent", accent);
            pats.add (juce::var (o));
        }
    root->setProperty ("patterns", pats);

    juce::Array<juce::var> voices;
    for (auto& v : settings.voices)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("level", v.level);
        o->setProperty ("tune", v.tune);
        o->setProperty ("decay", v.decay);
        o->setProperty ("tone", v.tone);
        voices.add (juce::var (o));
    }
    root->setProperty ("voices", voices);
    root->setProperty ("padPattern", padPattern.toVar());
    return juce::var (root);
}

juce::var PadPattern::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("bars", bars);
    o->setProperty ("quantize", quantize);
    juce::Array<juce::var> list;
    for (auto& h : hits)
    {
        juce::Array<juce::var> one;
        one.add (h.pad);
        one.add (h.beat);
        one.add (h.velocity);
        list.add (one);
    }
    o->setProperty ("hits", list);
    return juce::var (o);
}

PadPattern PadPattern::fromVar (const juce::var& v)
{
    PadPattern p;
    if (! v.isObject())
        return p;
    const int b = (int) v.getProperty ("bars", 2);
    p.bars = b == 1 || b == 4 ? b : 2;
    const int q = (int) v.getProperty ("quantize", 4);
    p.quantize = q == 0 || q == 2 || q == 4 ? q : 4;
    if (auto* list = v.getProperty ("hits", {}).getArray())
        for (auto& h : *list)
            if (h.isArray() && h.size() >= 2 && (int) p.hits.size() < kMaxPadHits)
            {
                PadHit hit;
                hit.pad = juce::jlimit (0, kPads - 1, (int) h[0]);
                hit.beat = juce::jlimit (0.0, p.lengthBeats() - 1.0e-6, (double) h[1]);
                hit.velocity = h.size() > 2 ? juce::jlimit (0.05f, 1.0f, (float) (double) h[2]) : 1.0f;
                p.hits.push_back (hit);
            }
    return p;
}

DrumData DrumData::fromVar (const juce::var& v)
{
    DrumData d;
    if (! v.isObject())
        return d;
    d.bpm = juce::jlimit (40.0, 240.0, (double) v.getProperty ("bpm", d.bpm));
    d.slot = juce::jlimit (0, kPatternSlots - 1, (int) v.getProperty ("slot", 0));
    d.variation = juce::jlimit (0, 2, (int) v.getProperty ("variation", 0));
    d.editB = (bool) v.getProperty ("editB", false);
    d.settings.swing = juce::jlimit (0.0f, 1.0f, (float) (double) v.getProperty ("swing", 0.0));
    d.padPattern = PadPattern::fromVar (v.getProperty ("padPattern", {}));

    if (auto* pats = v.getProperty ("patterns", {}).getArray())
    {
        if (pats->size() == kPatternSlots * 2)
        {
            for (int i = 0; i < kPatternSlots * 2; ++i)
            {
                auto& p = d.patterns[(size_t) (i / 2)][(size_t) (i % 2)];
                p = {};
                const auto& o = (*pats)[i];
                p.length = juce::jlimit (1, drums::kMaxSteps, (int) o.getProperty ("length", 16));
                if (auto* rows = o.getProperty ("rows", {}).getArray())
                    for (int r = 0; r < juce::jmin (drums::kVoices, rows->size()); ++r)
                    {
                        auto text = (*rows)[r].toString();
                        for (int s = 0; s < juce::jmin (drums::kMaxSteps, text.length()); ++s)
                            p.hits[(size_t) r][(size_t) s] = (std::uint8_t) juce::jlimit (0, 4, text[s] - '0');
                    }
                auto accent = o.getProperty ("accent", "").toString();
                for (int s = 0; s < juce::jmin (drums::kMaxSteps, accent.length()); ++s)
                    p.accent[(size_t) s] = accent[s] == '1';
            }
        }
    }
    if (auto* voices = v.getProperty ("voices", {}).getArray())
        for (int i = 0; i < juce::jmin (drums::kVoices, voices->size()); ++i)
        {
            const auto& o = (*voices)[i];
            auto& s = d.settings.voices[(size_t) i];
            s.level = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("level", s.level));
            s.tune = juce::jlimit (-12.0f, 12.0f, (float) (double) o.getProperty ("tune", s.tune));
            s.decay = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("decay", s.decay));
            s.tone = juce::jlimit (0.0f, 1.0f, (float) (double) o.getProperty ("tone", s.tone));
        }
    return d;
}

//==============================================================================
BeatsEngine::BeatsEngine()
{
    drums.prepare (sampleRate);
    setDrums (DrumData());
    for (auto& e : events)
        e = {};
}

void BeatsEngine::prepare (double rate)
{
    sampleRate = rate;
    drums.prepare (rate);
    for (auto& v : voices)
        v.audio = nullptr;
}

void BeatsEngine::setDrums (const DrumData& d)
{
    {
        const juce::SpinLock::ScopedLockType lock (drumLock);
        for (int i = 0; i < drums::kPatterns; ++i)
        {
            const int slot = juce::jmin (i, kPatternSlots - 1);
            pending.a[(size_t) i] = d.patterns[(size_t) slot][0];
            pending.b[(size_t) i] = d.patterns[(size_t) slot][1];
        }
        pending.settings = d.settings;
        drumVersion.fetch_add (1);
    }
    wantedSlot = d.slot;
    wantedVariation = d.variation;
    bpm = d.bpm;
}

void BeatsEngine::setPlaying (bool on) { wantPlay = on; }

void BeatsEngine::setPadPattern (const PadPattern& p)
{
    const juce::SpinLock::ScopedLockType lock (patternLock);
    pendingCount = juce::jmin ((int) p.hits.size(), kMaxPadHits);
    for (int i = 0; i < pendingCount; ++i)
        pendingHits[(size_t) i] = p.hits[(size_t) i];
    pendingLength = p.lengthBeats();
    ++patternVersion;
}
void BeatsEngine::setBpm (double b) { bpm = juce::jlimit (40.0, 240.0, b); }

void BeatsEngine::push (const Event& e)
{
    const juce::SpinLock::ScopedLockType lock (pushLock);
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0)
        events[(size_t) s1] = e;
    fifo.finishedWrite (n1 + n2);
}

void BeatsEngine::hitVoice (int voice, float velocity, bool accent)
{
    Event e;
    e.kind = Event::drum;
    e.index = voice;
    e.velocity = velocity;
    e.accent = accent;
    push (e);
}

void BeatsEngine::padOn (int pad, float velocity)
{
    Event e;
    e.kind = Event::padOn;
    e.index = pad;
    e.velocity = velocity;
    push (e);
}

void BeatsEngine::padOff (int pad)
{
    Event e;
    e.kind = Event::padOff;
    e.index = pad;
    push (e);
}

void BeatsEngine::stopPads()
{
    Event e;
    e.kind = Event::stopPads;
    push (e);
}

void BeatsEngine::setPad (int pad, const PadSetup& setup)
{
    if (pad < 0 || pad >= kPads)
        return;
    auto& s = slots[(size_t) pad];
    if (setup.audio != nullptr)
    {
        bool known = false;
        for (auto& k : keepAlive)
            known = known || k == setup.audio;
        if (! known)
        {
            keepAlive.push_back (setup.audio);
            if (keepAlive.size() > 256)
                keepAlive.erase (keepAlive.begin(), keepAlive.begin() + 64);
        }
    }
    const float p = (float) juce::jlimit (0, 100, setup.pan) / 100.0f;
    const float gain = (float) juce::jlimit (0, 100, setup.volume) / 100.0f;
    s.left = juce::jmin (1.0f, std::cos (p * juce::MathConstants<float>::halfPi) * 1.41421f) * gain;
    s.right = juce::jmin (1.0f, std::sin (p * juce::MathConstants<float>::halfPi) * 1.41421f) * gain;
    s.tune = juce::jlimit (-120, 120, setup.tune);
    s.mode = (int) setup.mode;
    s.audio = setup.audio != nullptr && setup.audio->getNumSamples() > 0 ? setup.audio.get() : nullptr;
}

//==============================================================================
void BeatsEngine::startPad (int pad, float velocity)
{
    auto& s = slots[(size_t) pad];
    const auto* a = s.audio.load();
    if (a == nullptr)
        return;

    if (s.mode.load() == (int) PadMode::mono)
        for (auto& v : voices)
            if (v.audio != nullptr && v.pad == pad)
                v.releasing = true;

    Voice* slot = &voices[0];
    for (auto& v : voices)
    {
        if (v.audio == nullptr)
        {
            slot = &v;
            break;
        }
        if (v.age < slot->age)
            slot = &v;
    }
    slot->audio = a;
    slot->position = 0;
    slot->step = (crate::kSampleRate / sampleRate) * std::pow (2.0, (double) s.tune.load() / 120.0);
    slot->gainL = s.left.load() * velocity;
    slot->gainR = s.right.load() * velocity;
    slot->release = 1.0f;
    slot->releasing = false;
    slot->pad = pad;
    slot->age = ++voiceClock;
}

void BeatsEngine::handle (const Event& e)
{
    switch (e.kind)
    {
        case Event::drum: drums.trigger (juce::jlimit (0, drums::kVoices - 1, e.index), e.velocity, e.accent); break;
        case Event::padOn: startPad (juce::jlimit (0, kPads - 1, e.index), e.velocity); break;
        case Event::padOff:
            if (slots[(size_t) juce::jlimit (0, kPads - 1, e.index)].mode.load() == (int) PadMode::noteOff)
                for (auto& v : voices)
                    if (v.audio != nullptr && v.pad == e.index)
                        v.releasing = true;
            break;
        case Event::stopPads:
            for (auto& v : voices)
                if (v.audio != nullptr)
                    v.releasing = true;
            break;
    }
}

void BeatsEngine::renderPads (float* left, float* right, int n)
{
    juce::uint64 mask = 0;
    const float fadeStep = 1.0f / (0.006f * (float) sampleRate);
    for (auto& v : voices)
    {
        if (v.audio == nullptr)
            continue;
        const auto& a = *v.audio;
        const int last = a.getNumSamples() - 1;
        const float* d0 = a.getReadPointer (0);
        const float* d1 = a.getNumChannels() > 1 ? a.getReadPointer (1) : d0;
        bool alive = true;
        for (int i = 0; i < n; ++i)
        {
            if (v.position >= (double) last)
            {
                alive = false;
                break;
            }
            const int i0 = (int) v.position;
            const int i1 = juce::jmin (last, i0 + 1);
            const float frac = (float) (v.position - (double) i0);
            const float s0 = d0[i0] + (d0[i1] - d0[i0]) * frac;
            const float s1 = d1[i0] + (d1[i1] - d1[i0]) * frac;
            if (v.releasing)
            {
                v.release -= fadeStep;
                if (v.release <= 0.0f)
                {
                    alive = false;
                    break;
                }
            }
            left[i] += s0 * v.gainL * v.release;
            if (right != nullptr)
                right[i] += s1 * v.gainR * v.release;
            v.position += v.step;
        }
        if (alive)
            mask |= (juce::uint64) 1 << v.pad;
        else
            v.audio = nullptr;
    }
    sounding = mask;
}

void BeatsEngine::render (float* left, float* right, int numSamples)
{
    juce::ScopedNoDenormals noDenormals;

    int s1, n1, s2, n2;
    fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i)
        handle (events[(size_t) (s1 + i)]);
    for (int i = 0; i < n2; ++i)
        handle (events[(size_t) (s2 + i)]);
    fifo.finishedRead (n1 + n2);

    if (liveVersion != drumVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (drumLock);
        if (lock.isLocked())
        {
            live = pending;
            liveVersion = drumVersion.load();
        }
    }

    if (livePatternVersion != patternVersion.load())
    {
        const juce::SpinLock::ScopedTryLockType lock (patternLock);
        if (lock.isLocked())
        {
            liveHits = pendingHits;
            liveCount = pendingCount;
            liveLength = pendingLength;
            livePatternVersion = patternVersion.load();
        }
    }

    const bool running = wantPlay.load() || clockHeld.load() || patternOn.load();
    const bool drumsOn = wantPlay.load();
    const double tempo = bpm.load();
    if (running && ! wasRunning)
    {
        ppq = 0;
        lastPpq = -1;
        run = run.load() + 1;
    }
    wasRunning = running;
    clockOn = running;
    clockPpq = ppq;

    // A new pattern starts at the next bar (or straight away when stopped), so switching live stays in time.
    const int wanted = wantedSlot.load();
    bool switchNow = ! running || lastPpq < 0;
    if (running && ! switchNow)
    {
        const double blockBeats = numSamples * tempo / (60.0 * sampleRate);
        switchNow = std::floor (ppq / 4.0) != std::floor ((ppq + blockBeats) / 4.0) || std::fmod (ppq, 4.0) < 1.0e-6;
    }
    if (switchNow)
        playingSlot = wanted;
    lastPpq = running ? ppq : -1;

    drums::Plan plan;
    plan.pattern = playingSlot;
    plan.variation = wantedVariation.load();
    drums.setBank (live.a.data(), live.b.data(), plan);
    drums.setSettings (live.settings);

    drums::Clock clock;
    clock.playing = drumsOn;
    clock.ppq = ppq;
    clock.bpm = tempo;
    drums.render (left, right, numSamples, noMidi, clock);

    // The pad pattern's hits in this block, at the sample they fall on.
    const double beatsPerSample = tempo / (60.0 * sampleRate);
    int numTriggers = 0;
    if (running && patternOn.load() && liveCount > 0 && liveLength > 0)
    {
        const double start = ppq, end = ppq + numSamples * beatsPerSample;
        const int thisRun = run.load();
        for (int i = 0; i < liveCount && numTriggers < (int) triggers.size(); ++i)
        {
            const auto& h = liveHits[(size_t) i];
            const double k = std::ceil ((start - h.beat) / liveLength - 1.0e-9);
            const double t = h.beat + k * liveLength;
            if (t < start || t >= end)
                continue;
            if (h.skipRun == thisRun && t < h.skipUntil)
                continue;
            triggers[(size_t) numTriggers++] = { juce::jlimit (0, numSamples - 1, (int) ((t - start) / beatsPerSample)), h.pad, h.velocity };
        }
        std::sort (triggers.begin(), triggers.begin() + numTriggers, [] (const Trigger& a, const Trigger& b) { return a.offset < b.offset; });
    }
    if (running && clickOn.load())
        renderClick (left, right, numSamples, ppq, beatsPerSample);

    if (running)
        ppq += numSamples * beatsPerSample;
    step = drums.getCurrentStep();
    playingB = drums.getCurrentCode() >= 0 ? (drums.getCurrentCode() & 1) : 0;

    int done = 0;
    for (int i = 0; i <= numTriggers; ++i)
    {
        const int upTo = i < numTriggers ? triggers[(size_t) i].offset : numSamples;
        if (upTo > done)
        {
            renderPads (left + done, right != nullptr ? right + done : nullptr, upTo - done);
            done = upTo;
        }
        if (i < numTriggers)
        {
            startPad (triggers[(size_t) i].pad, triggers[(size_t) i].velocity);
            hitsPlayed = hitsPlayed.load() + 1;
        }
    }
}

// A click on each beat (higher on the bar), while REC PADS records.
void BeatsEngine::renderClick (float* left, float* right, int n, double startBeat, double beatsPerSample)
{
    const float decay = std::exp (-1.0f / (0.012f * (float) sampleRate));
    double beat = startBeat;
    for (int i = 0; i < n; ++i)
    {
        const double next = beat + beatsPerSample;
        if (std::floor (next) != std::floor (beat) || (i == 0 && beat == 0.0))
        {
            const auto whole = (long long) std::floor (beat == 0.0 && i == 0 ? 0.0 : next);
            clickFreq = whole % 4 == 0 ? 1500.0f : 1000.0f;
            clickEnv = 0.35f;
            clickPhase = 0;
        }
        if (clickEnv > 1.0e-4f)
        {
            const float s = std::sin (clickPhase) * clickEnv;
            clickPhase += juce::MathConstants<float>::twoPi * clickFreq / (float) sampleRate;
            clickEnv *= decay;
            left[i] += s;
            if (right != nullptr)
                right[i] += s;
        }
        beat = next;
    }
}
}
