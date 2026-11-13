#include "RigQuantize.h"

#include "RigEngine.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace quantize
{
juce::String gridName (int g)
{
    static const juce::StringArray names { "1/4", "1/8", "1/8 T", "1/16", "1/16 T", "1/32" };
    return names[juce::jlimit (0, numGrids - 1, g)];
}

double gridBeats (int g)
{
    static constexpr double beats[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return beats[juce::jlimit (0, numGrids - 1, g)];
}

bool gridSwings (int g) { return g == eighth || g == sixteenth; }

juce::ValueTree Settings::toTree() const
{
    juce::ValueTree t ("Quantize");
    t.setProperty ("grid", grid, nullptr);
    t.setProperty ("strength", strength, nullptr);
    t.setProperty ("swing", swing, nullptr);
    t.setProperty ("newLoops", newLoops, nullptr);
    return t;
}

Settings Settings::fromTree (const juce::ValueTree& t)
{
    Settings s;
    if (! t.isValid())
        return s;
    s.grid = juce::jlimit (0, numGrids - 1, (int) t.getProperty ("grid", s.grid));
    s.strength = juce::jlimit (0.0f, 1.0f, (float) t.getProperty ("strength", s.strength));
    s.swing = juce::jlimit (50, 75, (int) t.getProperty ("swing", s.swing));
    s.newLoops = (bool) t.getProperty ("newLoops", s.newLoops);
    return s;
}

bool Settings::operator== (const Settings& o) const
{
    return grid == o.grid && std::abs (strength - o.strength) < 1.0e-6f && swing == o.swing && newLoops == o.newLoops;
}

double lineBeat (const Settings& s, long long n)
{
    const double step = gridBeats (s.grid);
    double beat = (double) n * step;
    if (gridSwings (s.grid) && (n & 1) != 0)
        beat += step * (juce::jlimit (50, 75, s.swing) - 50) / 50.0;
    return beat;
}

double nearestLine (const Settings& s, double beat)
{
    // Halfway between two lines goes to the later one (as rounding does).
    const auto n0 = (long long) std::floor (beat / gridBeats (s.grid));
    double best = lineBeat (s, n0 - 1), bestDistance = std::abs (beat - best);
    for (long long n = n0; n <= n0 + 2; ++n)
    {
        const double line = lineBeat (s, n);
        if (const double d = std::abs (beat - line); d <= bestDistance + 1.0e-9)
        {
            best = line;
            bestDistance = d;
        }
    }
    return best;
}

double pull (const Settings& s, double beat)
{
    return beat + juce::jlimit (0.0f, 1.0f, s.strength) * (nearestLine (s, beat) - beat);
}

//==============================================================================
int sequence (seq::Sequence& sq, const Settings& s)
{
    const int length = sq.lengthTicks();
    if (length <= 0)
        return 0;
    int moved = 0;
    for (auto& track : sq.tracks)
    {
        if (track.notes.empty())
            continue;
        std::map<std::pair<int, int>, seq::Note> landed;   // (tick, pad): the note there
        for (auto n : track.notes)
        {
            const double beat = (double) n.tick / seq::kTicksPerBeat;
            int tick = (int) std::lround (pull (s, beat) * seq::kTicksPerBeat);
            tick = ((tick % length) + length) % length;
            if (tick != n.tick)
                ++moved;
            n.tick = tick;
            auto [it, isNew] = landed.try_emplace ({ tick, n.pad }, n);
            if (! isNew && n.velocity > it->second.velocity)
                it->second = n;
        }
        track.notes.clear();
        for (auto& [key, note] : landed)
            track.notes.push_back (note);
        std::sort (track.notes.begin(), track.notes.end());
    }
    return moved;
}

//==============================================================================
std::vector<int> stepTargets (int length, double stepBeats, const Settings& s)
{
    std::vector<int> to ((size_t) juce::jmax (0, length));
    for (int i = 0; i < length; ++i)
    {
        const double beat = pull (s, i * stepBeats);
        const int step = (int) std::lround (beat / stepBeats);
        to[(size_t) i] = ((step % length) + length) % length;
    }
    return to;
}

int drumPattern (crate::drums::Pattern& p, double stepBeats, const Settings& s)
{
    const int length = juce::jlimit (1, crate::drums::kMaxSteps, p.length);
    const auto to = stepTargets (length, stepBeats, s);
    auto out = p;
    for (auto& row : out.hits)
        std::fill (row.begin(), row.begin() + length, (std::uint8_t) 0);
    std::fill (out.accent.begin(), out.accent.begin() + length, false);
    int moved = 0;
    for (int i = 0; i < length; ++i)
    {
        const int t = to[(size_t) i];
        bool any = false;
        for (size_t v = 0; v < p.hits.size(); ++v)
            if (const auto h = p.hits[v][(size_t) i]; h != 0)
            {
                out.hits[v][(size_t) t] = juce::jmax (out.hits[v][(size_t) t], h);
                any = true;
                if (t != i)
                    ++moved;
            }
        if (p.accent[(size_t) i])
            out.accent[(size_t) (any ? t : i)] = true;
    }
    p = out;
    return moved;
}

int bassPattern (crate::bass::Pattern& p, double stepBeats, const Settings& s)
{
    const int length = juce::jlimit (1, crate::bass::kSteps, p.length);
    const auto to = stepTargets (length, stepBeats, s);
    auto out = p;
    for (int i = 0; i < length; ++i)
        out.steps[(size_t) i] = {};
    // The notes already on the grid keep their place; the others go where there's room, or stay
    // where they were (so no note is lost).
    int moved = 0;
    std::array<bool, crate::bass::kSteps> placed {};
    for (int pass = 0; pass < 3; ++pass)
        for (int i = 0; i < length; ++i)
        {
            const auto& step = p.steps[(size_t) i];
            const bool onGrid = to[(size_t) i] == i;
            if (! step.on || placed[(size_t) i] || (pass == 0) != onGrid)
                continue;
            const int t = pass == 2 ? i : to[(size_t) i];
            if (out.steps[(size_t) t].on)
                continue;
            out.steps[(size_t) t] = step;
            placed[(size_t) i] = true;
            if (t != i)
                ++moved;
        }
    p = out;
    return moved;
}

int rhythm (crate::synthtool::Rhythm& r, double stepBeats, const Settings& s)
{
    const int length = juce::jlimit (1, crate::synthtool::kRhythmSteps, r.length);
    const auto to = stepTargets (length, stepBeats, s);
    auto out = r;
    std::fill (out.on.begin(), out.on.begin() + length, false);
    std::fill (out.accent.begin(), out.accent.begin() + length, false);
    int moved = 0;
    for (int i = 0; i < length; ++i)
        if (r.on[(size_t) i])
        {
            const int t = to[(size_t) i];
            out.on[(size_t) t] = true;
            out.accent[(size_t) t] = out.accent[(size_t) t] || r.accent[(size_t) i];
            if (t != i)
                ++moved;
        }
    r = out;
    return moved;
}

//==============================================================================
namespace
{
    double ms (double sampleRate, double milliseconds) { return sampleRate * milliseconds / 1000.0; }
}

std::vector<int> findHits (const juce::AudioBuffer<float>& audio, double sampleRate)
{
    std::vector<int> hits;
    const int n = audio.getNumSamples(), channels = juce::jmin (2, audio.getNumChannels());
    if (n == 0 || channels == 0 || sampleRate <= 0)
        return hits;

    // The loudness, every ~3 ms, over ~6 ms.
    const int hop = juce::jmax (16, (int) ms (sampleRate, 2.9)), window = hop * 2;
    const int frames = n / hop;
    std::vector<float> env ((size_t) frames, 0.0f);
    float loudest = 0;
    for (int f = 0; f < frames; ++f)
    {
        double sum = 0;
        const int from = f * hop, to = juce::jmin (n, from + window);
        for (int ch = 0; ch < channels; ++ch)
        {
            const float* x = audio.getReadPointer (ch);
            for (int i = from; i < to; ++i)
                sum += (double) x[i] * x[i];
        }
        env[(size_t) f] = (float) std::sqrt (sum / juce::jmax (1, (to - from) * channels));
        loudest = juce::jmax (loudest, env[(size_t) f]);
    }
    if (loudest < 1.0e-4f)
        return hits;

    // A hit: the sound jumps up well above what came just before, and isn't too quiet.
    const float floor = loudest * 0.08f;
    const int gapFrames = juce::jmax (1, (int) (ms (sampleRate, 70.0) / hop));
    const int lookBack = 6;
    int last = -gapFrames;
    // A loop goes round, so what comes before its first moment is its end.
    const auto envAt = [&env, frames] (int f) { return env[(size_t) (((f % frames) + frames) % frames)]; };
    for (int f = 0; f < frames; ++f)
    {
        const float e = env[(size_t) f];
        if (e < floor || f - last < gapFrames)
            continue;
        float before = 0;
        for (int k = 1; k <= lookBack; ++k)
            before += envAt (f - k);
        before /= lookBack;
        if (e > 2.2f * before + loudest * 0.01f && e > envAt (f - 1))
        {
            // The attack itself: the first sample past a quarter of the hit's peak, near here.
            const int from = juce::jmax (0, (f - 1) * hop), to = juce::jmin (n, (f + 3) * hop);
            float peak = 0;
            for (int ch = 0; ch < channels; ++ch)
                for (int i = from; i < to; ++i)
                    peak = juce::jmax (peak, std::abs (audio.getSample (ch, i)));
            int start = from;
            for (int i = from; i < to; ++i)
            {
                float v = 0;
                for (int ch = 0; ch < channels; ++ch)
                    v = juce::jmax (v, std::abs (audio.getSample (ch, i)));
                if (v >= peak * 0.25f)
                {
                    start = i;
                    break;
                }
            }
            if (hits.empty() || start > hits.back() + (int) ms (sampleRate, 30.0))
                hits.push_back (start);
            last = f;
        }
    }
    return hits;
}

AudioResult audio (const juce::AudioBuffer<float>& in, double beats, double sampleRate, const Settings& s)
{
    AudioResult result;
    const int n = in.getNumSamples();
    const int channels = juce::jmax (1, juce::jmin (2, in.getNumChannels()));
    result.audio.makeCopyOf (in);
    if (n == 0 || beats <= 0 || sampleRate <= 0)
        return result;

    // Each slice starts a little before its hit, so the crossfade into it is over before the attack.
    const int fade = juce::jmax (8, (int) ms (sampleRate, 2.5));
    const double perBeat = n / beats;
    struct Slice { int from; double to; };
    std::vector<Slice> slices;
    for (int hit : findHits (in, sampleRate))
    {
        const double beat = hit / perBeat;
        const double target = pull (s, beat) * perBeat;
        slices.push_back ({ juce::jmax (0, hit - fade), target - (hit - juce::jmax (0, hit - fade)) });
    }
    result.hits = (int) slices.size();
    if (slices.empty())
        return result;

    // Keep them in order, each long enough for its crossfades (two hits pulled onto one line:
    // the later stays with the earlier's slice).
    const double minLength = 3.0 * fade;
    std::vector<Slice> kept;
    for (const auto& sl : slices)
        if (kept.empty() || sl.to >= kept.back().to + minLength)
            kept.push_back (sl);
    while (kept.size() > 1 && kept.front().to + n < kept.back().to + minLength)
        kept.pop_back();

    for (const auto& sl : kept)
        if (std::abs (sl.to - sl.from) >= 1.0)
            ++result.moved;
    if (result.moved == 0)
        return result;

    // Each slice plays from its start to the next one's place, then fades out over the next one's
    // fade in (so slices that didn't move join up exactly). Round the loop: the last slice runs on
    // to the first one's place.
    result.audio.clear();
    const auto wrap = [n] (long long i) { return (int) (((i % n) + n) % n); };
    for (size_t k = 0; k < kept.size(); ++k)
    {
        const auto& sl = kept[k];
        const double nextTo = k + 1 < kept.size() ? kept[k + 1].to : kept.front().to + n;
        const auto to = (long long) std::llround (sl.to);
        const int length = (int) (std::llround (nextTo) - to);
        // Never into the next hit: a slice moved away from the next one fades out where its own
        // sound ends, leaving a little silence.
        const int nextFrom = k + 1 < kept.size() ? kept[k + 1].from : kept.front().from + n;
        const int play = juce::jmin (length, nextFrom - sl.from);
        for (int ch = 0; ch < 2 && ch < result.audio.getNumChannels(); ++ch)
        {
            const float* src = in.getReadPointer (juce::jmin (ch, channels - 1));
            float* dst = result.audio.getWritePointer (ch);
            for (int j = 0; j < play + fade; ++j)
            {
                float g = 1.0f;
                if (j < fade)
                    g = (float) j / fade;
                else if (j >= play)
                    g = 1.0f - (float) (j - play) / fade;
                dst[wrap (to + j)] += src[wrap ((long long) sl.from + j)] * g;
            }
        }
    }
    return result;
}
}

//==============================================================================
RigQuantizer::RigQuantizer (RigEngine& e) : engine (e)
{
    lastStates.fill ((int) RigLooper::State::empty);
}

juce::String RigQuantizer::targetName (int t)
{
    switch (t)
    {
        case mpc:      return "MPC";
        case machine:  return "Drum machine";
        case bass:     return "Bass";
        case synth:    return "Synth arp";
        case allLoops: return "All loops";
        default:       break;
    }
    if (t >= firstLoop && t < numTargets)
        return "Loop " + juce::String (t - firstLoop + 1);
    return {};
}

juce::String RigQuantizer::targetDetail (int t) const
{
    auto& looper = engine.getLooper();
    switch (t)
    {
        case mpc:
        {
            auto& project = engine.getMpc().getProject();
            const auto& sq = project.currentSequence();
            return sq.name.isNotEmpty() ? sq.name : "Sequence " + juce::String (project.getCurrentSequenceIndex() + 1);
        }
        case machine:
        {
            auto& d = engine.getDrums();
            return "Pattern " + DrumProcessor::codeName (d.getSelectedPattern() * 2 + d.editVariation);
        }
        case bass:     return "Pattern " + juce::String (engine.getBass().getSelectedPattern() + 1);
        case synth:    return "the arp's steps";
        case allLoops:
        {
            int count = 0;
            for (int i = 0; i < RigLooper::kLayers; ++i)
                if (looper.getState (i) == RigLooper::State::playing)
                    ++count;
            return count == 0 ? juce::String ("no loops yet") : juce::String (count) + (count == 1 ? " loop" : " loops");
        }
        default: break;
    }
    if (t >= firstLoop && t < numTargets)
    {
        const auto state = looper.getState (t - firstLoop);
        if (state == RigLooper::State::empty)
            return "empty";
        if (state != RigLooper::State::playing)
            return "recording";
        const double bars = looper.getLayer (t - firstLoop).lengthBeats.load() / 4.0;
        const auto text = std::abs (bars - std::round (bars)) < 0.01 ? juce::String ((int) std::round (bars)) : juce::String (bars, 2);
        return text + (text == "1" ? " bar" : " bars");
    }
    return {};
}

double RigQuantizer::stepBeatsOf (int t) const
{
    auto rate = [] (juce::AudioProcessorValueTreeState& p, const char* id)
    {
        auto* v = p.getRawParameterValue (id);
        return v != nullptr ? (int) v->load() : -1;
    };
    switch (t)
    {
        case machine: return crate::drums::stepBeats (juce::jmax (0, rate (engine.getDrums().params, "stepRate")));
        case bass:    return crate::synth::SyncRates::beats (juce::jmax (0, rate (engine.getBass().params, "stepRate")));
        case synth:   return crate::synth::ArpRates::beats (juce::jmax (0, rate (engine.getSynth().params, "arpSpeed")));
        default:      return 0.25;
    }
}

void RigQuantizer::forgetUndo()
{
    undoTarget = none;
    undoLoops.clear();
}

juce::String RigQuantizer::apply (int t)
{
    const auto how = quantize::gridName (settings.grid)
                   + (settings.strength < 0.995f ? " at " + juce::String (juce::roundToInt (settings.strength * 100)) + "%" : juce::String())
                   + (quantize::gridSwings (settings.grid) && settings.swing > 50 ? ", swing " + juce::String (settings.swing) + "%" : juce::String());
    auto nothing = [&] (const juce::String& what)
    {
        return what + " is already on the " + quantize::gridName (settings.grid) + " grid.";
    };
    auto stepsNote = [&] (double stepBeats)
    {
        return quantize::gridBeats (settings.grid) <= stepBeats + 1.0e-9
             ? " Its steps are already 1/" + juce::String (juce::roundToInt (4.0 / stepBeats)) + "s: pick a coarser grid (1/8, 1/4) or a triplet one to move them."
             : juce::String();
    };

    switch (t)
    {
        case mpc:
        {
            auto& m = engine.getMpc();
            auto& project = m.getProject();
            auto copy = project.currentSequence();
            auto changed = copy;
            const int moved = quantize::sequence (changed, settings);
            if (moved == 0)
                return nothing ("The MPC's " + targetDetail (mpc));
            forgetUndo();
            m.beginEdit();   // the MPC's own Undo takes it back too
            project.currentSequence() = changed;
            project.sequenceChanged();
            undoSequence = copy;
            undoSequenceIndex = project.getCurrentSequenceIndex();
            undoTarget = mpc;
            return "Quantized " + juce::String (moved) + (moved == 1 ? " note" : " notes") + " of the MPC's " + targetDetail (mpc)
                 + " to " + how + ".";
        }
        case machine:
        {
            auto& d = engine.getDrums();
            const auto before = d.getEditedPattern();
            auto p = before;
            const double stepBeats = stepBeatsOf (machine);
            const int moved = quantize::drumPattern (p, stepBeats, settings);
            if (moved == 0)
                return nothing ("The drum machine's " + targetDetail (machine)) + stepsNote (stepBeats);
            forgetUndo();
            undoDrums = before;
            undoDrumIndex = d.getSelectedPattern();
            undoDrumVariation = d.editVariation;
            d.setEditedPattern (p);
            undoTarget = machine;
            return "Quantized " + juce::String (moved) + (moved == 1 ? " hit" : " hits") + " of the drum machine's " + targetDetail (machine)
                 + " to " + how + ".";
        }
        case bass:
        {
            auto& b = engine.getBass();
            const int index = b.getSelectedPattern();
            const auto before = b.getPattern (index);
            auto p = before;
            const double stepBeats = stepBeatsOf (bass);
            const int moved = quantize::bassPattern (p, stepBeats, settings);
            if (moved == 0)
                return nothing ("The Bass's " + targetDetail (bass)) + stepsNote (stepBeats);
            forgetUndo();
            undoBass = before;
            undoBassIndex = index;
            b.setPattern (index, p);
            undoTarget = bass;
            return "Quantized " + juce::String (moved) + (moved == 1 ? " note" : " notes") + " of the Bass's " + targetDetail (bass)
                 + " to " + how + ".";
        }
        case synth:
        {
            auto& sy = engine.getSynth();
            const auto before = sy.getRhythm();
            auto r = before;
            const double stepBeats = stepBeatsOf (synth);
            const int moved = quantize::rhythm (r, stepBeats, settings);
            if (moved == 0)
                return nothing ("The Synth's arp") + stepsNote (stepBeats);
            forgetUndo();
            undoRhythm = before;
            sy.setRhythm (r);
            undoTarget = synth;
            return "Quantized " + juce::String (moved) + (moved == 1 ? " step" : " steps") + " of the Synth's arp to " + how + ".";
        }
        case allLoops:
        {
            forgetUndo();
            for (int i = 0; i < RigLooper::kLayers; ++i)
                if (engine.getLooper().getState (i) == RigLooper::State::playing)
                    applyLoop (i, true);
            const int done = (int) undoLoops.size();
            if (done == 0)
            {
                bool any = false;
                for (int i = 0; i < RigLooper::kLayers; ++i)
                    any = any || engine.getLooper().getState (i) != RigLooper::State::empty;
                return any ? "The loops are already on the " + quantize::gridName (settings.grid) + " grid (or still recording)."
                           : juce::String ("There are no loops to quantize yet.");
            }
            undoTarget = allLoops;
            return "Quantized " + juce::String (done) + (done == 1 ? " loop" : " loops") + " to " + how + ".";
        }
        default: break;
    }
    if (t >= firstLoop && t < numTargets)
    {
        forgetUndo();
        auto said = applyLoop (t - firstLoop, true);
        if (! undoLoops.empty())
            undoTarget = t;
        return said;
    }
    return "Pick what to quantize.";
}

juce::String RigQuantizer::applyLoop (int layer, bool keepUndo)
{
    auto& looper = engine.getLooper();
    const auto name = "Loop " + juce::String (layer + 1);
    const auto state = looper.getState (layer);
    if (state == RigLooper::State::empty)
        return name + " is empty.";
    if (state != RigLooper::State::playing)
        return name + " is still recording: quantize it when it's done.";
    auto before = looper.copyLayerAudio (layer);
    const double beats = looper.getLayer (layer).lengthBeats.load();
    if (before.getNumSamples() == 0 || beats <= 0)
        return name + " is empty.";
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    auto result = quantize::audio (before, beats, rate, settings);
    if (result.hits == 0)
        return name + " has no clear hits to move (it's quantized by the beat; sustained sounds stay as they are).";
    if (result.moved == 0)
        return name + " is already on the " + quantize::gridName (settings.grid) + " grid.";
    if (! looper.replaceLayerAudio (layer, std::move (result.audio)))
        return name + " changed while quantizing it: try again.";
    if (keepUndo)
        undoLoops.push_back ({ layer, std::move (before), looper.getLayer (layer).version.load() });
    return "Quantized " + name + ": " + juce::String (result.moved) + " of its " + juce::String (result.hits) + " hits moved onto "
         + quantize::gridName (settings.grid) + ".";
}

juce::String RigQuantizer::undo()
{
    const int t = undoTarget;
    undoTarget = none;
    switch (t)
    {
        case none: return "Nothing to undo.";
        case mpc:
        {
            auto& project = engine.getMpc().getProject();
            if (undoSequenceIndex >= (int) project.getSequences().size())
                return "That sequence is gone.";
            engine.getMpc().beginEdit();
            project.getSequences()[(size_t) undoSequenceIndex] = undoSequence;
            project.sequenceChanged();
            return "Put the MPC's notes back where they were.";
        }
        case machine:
        {
            engine.getDrums().setPattern (undoDrumIndex, undoDrums, undoDrumVariation);
            return "Put the drum machine's pattern back.";
        }
        case bass:
            engine.getBass().setPattern (undoBassIndex, undoBass);
            return "Put the Bass's pattern back.";
        case synth:
            engine.getSynth().setRhythm (undoRhythm);
            return "Put the arp's steps back.";
        default: break;
    }
    int back = 0;
    for (auto& u : undoLoops)
        if (engine.getLooper().getLayer (u.layer).version.load() == u.version
            && engine.getLooper().replaceLayerAudio (u.layer, std::move (u.audio)))
            ++back;
    undoLoops.clear();
    return back > 0 ? "Put " + juce::String (back) + (back == 1 ? " loop" : " loops") + " back as they were."
                    : juce::String ("The loop has changed since, so there's nothing to put back.");
}

juce::String RigQuantizer::service()
{
    juce::String said;
    auto& looper = engine.getLooper();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const int now = (int) looper.getState (i);
        const int was = std::exchange (lastStates[(size_t) i], now);
        if (settings.newLoops && was == (int) RigLooper::State::recording && now == (int) RigLooper::State::playing)
        {
            forgetUndo();
            said = applyLoop (i, true);
            if (! undoLoops.empty())
                undoTarget = loopTarget (i);
        }
    }
    if (said.isNotEmpty())
        lastAutoMessage = said;
    return said;
}
