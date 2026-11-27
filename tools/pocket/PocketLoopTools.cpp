#include "PocketLoopTools.h"

#include "PocketFrame.h"
#include "core/SoundFx.h"

namespace pocket
{
//==============================================================================
// What made a layer
const char* layerToolName (int tool)
{
    switch ((LayerTool) tool)
    {
        case LayerTool::drums: return "DRUMS";
        case LayerTool::pads: return "PADS";
        case LayerTool::synth: return "SYNTH";
        case LayerTool::bass: return "BASS";
        case LayerTool::chords: return "CHORDS";
        case LayerTool::vocoder: return "VOCODER";
        case LayerTool::mix: return "MIX";
        case LayerTool::drone: return "DRONE";
        default: return "MIC";
    }
}

juce::Colour layerToolColour (int tool)
{
    switch ((LayerTool) tool)
    {
        case LayerTool::drums: return toolColour (Tool::drums);
        case LayerTool::pads: return toolColour (Tool::pads);
        case LayerTool::synth: return toolColour (Tool::synth);
        case LayerTool::bass: return toolColour (Tool::bass);
        case LayerTool::chords: return toolColour (Tool::chords);
        case LayerTool::vocoder: return toolColour (Tool::vocoder);
        case LayerTool::mix: return juce::Colour (0xffb7a9c2);
        case LayerTool::drone: return toolColour (Tool::drone);
        default: return juce::Colour (0xfff59a2a);   // your voice: warm, apart from the vocoder's green
    }
}

void drawLayerToolIcon (juce::Graphics& g, int tool, juce::Point<float> c, juce::Colour ink)
{
    switch ((LayerTool) tool)
    {
        case LayerTool::drums: drawToolIcon (g, Tool::drums, c, ink); break;
        case LayerTool::pads: drawToolIcon (g, Tool::pads, c, ink); break;
        case LayerTool::synth: drawToolIcon (g, Tool::synth, c, ink); break;
        case LayerTool::bass: drawToolIcon (g, Tool::bass, c, ink); break;
        case LayerTool::chords: drawToolIcon (g, Tool::chords, c, ink); break;
        case LayerTool::mix: drawToolIcon (g, Tool::loops, c, ink); break;
        case LayerTool::drone: drawToolIcon (g, Tool::drone, c, ink); break;
        default: drawToolIcon (g, Tool::vocoder, c, ink); break;   // a mic, for the voice and the vocoder
    }
}

//==============================================================================
// The notes a layer came from
juce::String NotePart::describe() const
{
    if (kind == pads)
        return "PADS";
    const auto& all = synthPresets();
    return ! all.empty() && familyOfKind (all[(size_t) juce::jlimit (0, (int) all.size() - 1, preset)].kind) == bassFamily ? "BASS" : "SYNTH";
}

juce::var NotePart::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("kind", kind == pads ? "pads" : "synth");
    o->setProperty ("beats", lengthBeats);
    o->setProperty ("start", startBeat);
    if (kind == pads)
    {
        o->setProperty ("pattern", pattern.toVar());
        o->setProperty ("drumsOn", drumsOn);
        if (drumsOn)
            o->setProperty ("drums", drums.toVar());
    }
    else
    {
        o->setProperty ("notes", notes.toVar());
        o->setProperty ("sound", synthPresets()[(size_t) juce::jlimit (0, (int) synthPresets().size() - 1, preset)].name);
        o->setProperty ("octave", octave);
        o->setProperty ("tone", knobs.tone);
        o->setProperty ("shape", knobs.shape);
        o->setProperty ("decay", knobs.decay);
        o->setProperty ("echo", knobs.echo);
    }
    if (! settingsAreNeutral (board))
        o->setProperty ("board", board);
    if (! settingsAreNeutral (padsBoard))
        o->setProperty ("padsBoard", padsBoard);
    return juce::var (o);
}

std::shared_ptr<NotePart> NotePart::fromVar (const juce::var& v)
{
    if (! v.isObject())
        return nullptr;
    auto p = std::make_shared<NotePart>();
    p->kind = v.getProperty ("kind", "pads").toString() == "synth" ? synth : pads;
    p->lengthBeats = juce::jmax (1.0, (double) v.getProperty ("beats", 4.0));
    p->startBeat = juce::jmax (0.0, (double) v.getProperty ("start", 0.0));
    if (p->kind == pads)
    {
        p->pattern = PadPattern::fromVar (v.getProperty ("pattern", {}));
        p->drumsOn = (bool) v.getProperty ("drumsOn", false);
        if (p->drumsOn)
            p->drums = DrumData::fromVar (v.getProperty ("drums", {}));
        if (p->pattern.hits.empty() && ! p->drumsOn)
            return nullptr;
    }
    else
    {
        p->notes = SynthPattern::fromVar (v.getProperty ("notes", {}));
        p->preset = juce::jmax (0, synthPresetIndex (v.getProperty ("sound", {}).toString()));
        p->octave = juce::jlimit (-2, 2, (int) v.getProperty ("octave", 0));
        p->knobs.tone = (float) (double) v.getProperty ("tone", 0.5);
        p->knobs.shape = juce::jlimit (0, 3, (int) v.getProperty ("shape", 0));
        p->knobs.decay = (float) (double) v.getProperty ("decay", 0.5);
        p->knobs.echo = (float) (double) v.getProperty ("echo", 0.0);
        if (p->notes.empty())
            return nullptr;
    }
    p->board = v.getProperty ("board", {});      // older songs: none
    p->padsBoard = v.getProperty ("padsBoard", {});
    return p;
}

double quantizedBeatOn (double beat, double g, float strength, bool swing, double length)
{
    double out = beat;
    if (g > 0.0)
    {
        const double line = std::round (beat / g);
        double target = line * g;
        if (swing && (((long long) line) & 1) != 0)
            target += g / 3.0;   // the off-beat at two thirds: a triplet swing
        out = beat + (target - beat) * (double) juce::jlimit (0.0f, 1.0f, strength);
    }
    if (length > 0.0)
    {
        out = std::fmod (out, length);
        if (out < 0.0)
            out += length;
        if (out > length - 1.0e-9)
            out = 0.0;
    }
    return out;
}

double quantizedBeat (double beat, const LoopQuantize& q, double length)
{
    return quantizedBeatOn (beat, q.gridBeats(), q.strength, q.swing, length);
}

double quantizeGridBeats (int grid)
{
    static constexpr double g[kQuantizeGrids] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
    return g[juce::jlimit (0, kQuantizeGrids - 1, grid)];
}

const char* quantizeGridName (int grid)
{
    static const char* n[kQuantizeGrids] = { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" };
    return n[juce::jlimit (0, kQuantizeGrids - 1, grid)];
}

QuantizeResult quantizeSynthPattern (SynthPattern& pat, const QuantizeSpec& q)
{
    QuantizeResult r;
    r.total = (int) pat.notes.size();
    const double g = quantizeGridBeats (q.grid), len = pat.lengthBeats();
    const bool swing = q.swing && q.grid < 4;
    for (auto& n : pat.notes)
    {
        const double was = n.beat;
        n.beat = quantizedBeatOn (n.beat, g, q.strength, swing, len);
        n.skipUntil = -1;
        if (std::abs (n.beat - was) > 1.0e-6)
            ++r.moved;
    }
    // notes of one pitch on one spot become one note
    std::stable_sort (pat.notes.begin(), pat.notes.end(), [] (const SynthNote& a, const SynthNote& b)
                      { return a.note != b.note ? a.note < b.note : a.beat < b.beat; });
    std::vector<SynthNote> out;
    for (auto& n : pat.notes)
    {
        if (! out.empty() && out.back().note == n.note)
        {
            auto& p = out.back();
            if (std::abs (p.beat - n.beat) < 1.0e-6)
            {
                p.velocity = juce::jmax (p.velocity, n.velocity);
                p.length = juce::jmax (p.length, n.length);
                ++r.merged;
                continue;
            }
            if (p.beat + p.length > n.beat + 1.0e-9)
                p.length = juce::jmax (0.02, n.beat - p.beat);   // it would still sound as the next one starts
        }
        out.push_back (n);
    }
    pat.notes = std::move (out);
    std::stable_sort (pat.notes.begin(), pat.notes.end(), [] (const SynthNote& a, const SynthNote& b) { return a.beat < b.beat; });
    return r;
}

QuantizeResult quantizePadPattern (PadPattern& pat, const QuantizeSpec& q)
{
    QuantizeResult r;
    r.total = (int) pat.hits.size();
    const double g = quantizeGridBeats (q.grid), len = pat.lengthBeats();
    const bool swing = q.swing && q.grid < 4;
    for (auto& h : pat.hits)
    {
        const double was = h.beat;
        h.beat = quantizedBeatOn (h.beat, g, q.strength, swing, len);
        h.skipRun = -1;
        h.skipUntil = 0;
        if (std::abs (h.beat - was) > 1.0e-6)
            ++r.moved;
    }
    std::vector<PadHit> out;
    for (auto& h : pat.hits)
    {
        bool merged = false;
        for (auto& o : out)
            if (o.pad == h.pad && o.mods == h.mods && std::abs (o.beat - h.beat) < 1.0e-6)
            {
                o.velocity = juce::jmax (o.velocity, h.velocity);
                merged = true;
                ++r.merged;
                break;
            }
        if (! merged)
            out.push_back (h);
    }
    pat.hits = std::move (out);
    return r;
}

std::vector<float> renderPart (const NotePart& part, const LoopQuantize& q, double bpm, double sampleRate, int lengthSamples)
{
    std::vector<float> out ((size_t) juce::jmax (1, lengthSamples), 0.0f);
    bpm = juce::jlimit (40.0, 240.0, bpm);
    const double patLen = juce::jmax (1.0, part.patternBeats());
    // whole passes before the layer's top, so the tails of the pass before ring into it as they did live
    double pre = std::ceil (part.lengthBeats / patLen - 1.0e-9) * patLen;
    if (part.startBeat >= part.lengthBeats && part.startBeat <= 64.0 && std::fmod (part.startBeat + 1.0e-9, patLen) < 1.0e-6)
        pre = part.startBeat;   // where it really started, so the drum machine's A/B is where it was
    const double samplesPerBeat = 60.0 / bpm * sampleRate;
    const int preSamples = (int) std::llround (pre * samplesPerBeat);
    const int total = preSamples + lengthSamples;
    std::vector<float> l (512), r (512), click (512), pl (512), pr (512);
    // the boards it went through, on the clock it played on (the pass before the layer's top included)
    const double clockStart = part.startBeat - pre;
    OfflineBoard board (part.board, sampleRate, bpm, clockStart);
    OfflineBoard padsBoard (part.kind == NotePart::pads ? part.padsBoard : juce::var(), sampleRate, bpm, clockStart);
    const bool split = board.active() || padsBoard.active();

    auto take = [&] (int done, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int at = done + i - preSamples;
            if (at >= 0 && at < lengthSamples)
                out[(size_t) at] = 0.5f * (l[(size_t) i] + r[(size_t) i]);
        }
    };

    if (part.kind == NotePart::pads)
    {
        auto eng = std::make_unique<BeatsEngine>();
        eng->prepare (sampleRate);
        eng->setBpm (bpm);
        for (int p = 0; p < (int) part.kit.size() && p < kPads; ++p)
            eng->setPad (p, part.kit[(size_t) p]);
        PadPattern pat = part.pattern;
        for (auto& h : pat.hits)
        {
            h.beat = quantizedBeat (h.beat, q, pat.lengthBeats());
            h.skipRun = -1;
            h.skipUntil = 0;
        }
        eng->setPadPattern (pat);
        if (part.drumsOn)
        {
            auto d = part.drums;
            d.bpm = bpm;
            for (int v = 0; v < crate::drums::kVoices; ++v)
                eng->setDrumSample (v, part.drumSamples[(size_t) v]);
            eng->setDrums (d);
            eng->setPlaying (true);
        }
        eng->setPatternPlaying (true);
        for (int done = 0; done < total; done += 512)
        {
            const int n = juce::jmin (512, total - done);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            std::fill (click.begin(), click.end(), 0.0f);
            if (split)
            {
                std::fill (pl.begin(), pl.end(), 0.0f);
                std::fill (pr.begin(), pr.end(), 0.0f);
                eng->renderWithClick (l.data(), r.data(), n, click.data(), pl.data(), pr.data());
                board.process (l.data(), r.data(), n);
                padsBoard.process (pl.data(), pr.data(), n);
                for (int i = 0; i < n; ++i)
                {
                    l[(size_t) i] += pl[(size_t) i];
                    r[(size_t) i] += pr[(size_t) i];
                }
            }
            else
                eng->renderWithClick (l.data(), r.data(), n, click.data());
            take (done, n);
        }
    }
    else
    {
        auto dummy = std::make_unique<BeatsEngine>();
        auto eng = std::make_unique<SynthEngine> (*dummy);
        eng->prepare (sampleRate);
        eng->setSettings (makeSynthSettings (part.preset, part.knobs, part.octave));
        SynthPattern pat = part.notes;
        for (auto& n : pat.notes)
        {
            n.beat = quantizedBeat (n.beat, q, pat.lengthBeats());
            n.skipUntil = -1;
        }
        eng->setPattern (pat);
        eng->setPlayFrom (-1.0e9);
        double beat = 0.0;
        const double perSample = 1.0 / samplesPerBeat;
        for (int done = 0; done < total; done += 512)
        {
            const int n = juce::jmin (512, total - done);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            eng->renderAt (l.data(), r.data(), n, true, beat, bpm);
            board.process (l.data(), r.data(), n);
            beat += n * perSample;
            take (done, n);
        }
    }
    return out;
}

//==============================================================================
double snapBeats (int snap) { return snap == 0 ? 4.0 : snap == 1 ? 1.0 : snap == 2 ? 0.25 : 0.0; }

double snapBeat (double beat, int snap)
{
    const double g = snapBeats (snap);
    return g > 0.0 ? std::round (beat / g) * g : beat;
}

double loopLengthFor (double contentBeats)
{
    if (contentBeats <= 1.0 + 1.0e-6)
        return 1.0;
    if (contentBeats <= 2.0 + 1.0e-6)
        return 2.0;
    return std::ceil (contentBeats / 4.0 - 1.0e-6) * 4.0;
}

FitChoices fitChoices (double bars)
{
    static const int ladder[] = { 1, 2, 4, 8, 16 };
    FitChoices c;
    c.nearest = juce::jlimit (1, 64, (int) std::round (bars));
    c.exact = std::abs (bars - (double) c.nearest) < 0.01;
    for (int v : ladder)
        if (v < c.nearest)
            c.smaller = v;
    for (int i = 4; i >= 0; --i)
        if (ladder[i] > c.nearest)
            c.larger = ladder[i];
    return c;
}

std::vector<float> stretchAudio (const float* data, int n, int newLength)
{
    n = juce::jmax (1, n);
    newLength = juce::jmax (1, newLength);
    juce::AudioBuffer<float> in (1, n);
    in.copyFrom (0, 0, data, n);
    const auto out = crate::SoundFx::stretch (in, newLength);
    std::vector<float> v ((size_t) newLength, 0.0f);
    std::copy (out.getReadPointer (0), out.getReadPointer (0) + juce::jmin (newLength, out.getNumSamples()), v.begin());
    return v;
}

double nativeBpm (const LoopTake& t)
{
    const int len = t.length.load();
    return len > 0 ? t.lengthBeats.load() * 60.0 * t.sampleRate / (double) len : 0.0;
}

namespace
{
SharedTake like (const LoopTake& t, int samples)
{
    auto m = LoopsEngine::makeTake (samples, t.sampleRate);
    m->length = juce::jmax (1, samples);
    m->source = t.source;
    m->tool = t.tool;
    return m;
}

juce::String barsText (double beats)
{
    const double bars = beats / 4.0;
    if (std::abs (bars - std::round (bars)) < 1.0e-6)
        return juce::String ((int) std::round (bars)) + (std::round (bars) == 1.0 ? " bar" : " bars");
    if (beats < 4.0 - 1.0e-6)
        return juce::String (beats, std::abs (beats - std::round (beats)) < 1.0e-6 ? 0 : 2) + (beats == 1.0 ? " beat" : " beats");
    return juce::String (bars, 2) + " bars";
}
}

Edited trimTake (const LoopTake& t, const LayerFxState& fx, double from, double to)
{
    Edited e;
    const int len = t.length.load();
    const double total = t.lengthBeats.load();
    if (len < 2 || total <= 0.0)
        return e.error = "Nothing to trim", e;
    from = juce::jlimit (0.0, total, from);
    to = juce::jlimit (from, total, to);
    if (to - from < 0.99)
        return e.error = "Keep at least one beat between IN and OUT.", e;
    const double perBeat = (double) len / total;
    const int a = juce::jlimit (0, len - 1, (int) std::llround (from * perBeat));
    const int b = juce::jlimit (a + 1, len, (int) std::llround (to * perBeat));
    const double content = to - from, loopBeats = loopLengthFor (content);
    const int loopSamples = juce::jmax (b - a, (int) std::llround (loopBeats * perBeat));
    e.take = like (t, loopSamples);
    std::copy (t.data.begin() + a, t.data.begin() + b, e.take->data.begin());
    e.take->lengthBeats = loopBeats;
    const bool padded = b - a < loopSamples - 1;
    if (padded)
    {
        e.take->content = b - a;
        const int fade = juce::jmin (b - a, 96);   // the cut into the silence, without a click
        for (int i = 0; i < fade; ++i)
            e.take->data[(size_t) (b - a - 1 - i)] *= (float) i / (float) fade;
    }
    e.fx = fx;
    e.fx.remap (from / total, to / total, 1, false, content / loopBeats);
    e.shift = from;
    e.said = "Trimmed to " + barsText (content) + (padded ? ", with silence to " + barsText (loopBeats) + " so it comes round on the beat. FIT stretches it instead." : ".");
    if (t.part != nullptr)
        e.said << " It's plain audio now: UNDO brings QUANTIZE back.";
    return e;
}

Edited fitTake (const LoopTake& t, const LayerFxState& fx, double from, double to, int bars, double bpm)
{
    Edited e;
    const int len = t.length.load();
    const double total = t.lengthBeats.load();
    if (len < 2 || total <= 0.0 || bars <= 0)
        return e.error = "Nothing to fit", e;
    from = juce::jlimit (0.0, total, from);
    to = juce::jlimit (from, total, to);
    const double perBeat = (double) len / total;
    const int a = juce::jlimit (0, len - 1, (int) std::llround (from * perBeat));
    const int b = juce::jlimit (a + 1, len, (int) std::llround (to * perBeat));
    const int target = (int) std::llround (bars * 4.0 * 60.0 / juce::jlimit (40.0, 240.0, bpm) * t.sampleRate);
    if ((double) target / t.sampleRate > 600.0)
        return e.error = "That would pass 10 minutes.", e;
    auto out = stretchAudio (t.data.data() + a, b - a, target);
    e.take = like (t, target);
    std::copy (out.begin(), out.end(), e.take->data.begin());
    e.take->lengthBeats = bars * 4.0;
    e.fx = fx;
    e.fx.remap (from / total, to / total, 1, false);
    e.shift = from;
    e.said = "Fitted to " + barsText (bars * 4.0) + ": it lands exactly on the bars, at the same pitch. UNDO takes it back.";
    return e;
}

Edited quantizeTake (const LoopTake& t, const LayerFxState& fx, const LoopQuantize& q, double bpm)
{
    Edited e;
    if (t.part == nullptr)
        return e.error = "This layer is audio: QUANTIZE works on layers made from notes.", e;
    const auto& part = *t.part;
    const int samples = (int) std::llround (part.lengthBeats * 60.0 / juce::jlimit (40.0, 240.0, bpm) * t.sampleRate);
    auto out = renderPart (part, q, bpm, t.sampleRate, samples);
    e.take = like (t, samples);
    std::copy (out.begin(), out.end(), e.take->data.begin());
    e.take->lengthBeats = part.lengthBeats;
    e.take->part = t.part;
    e.take->quant = q;
    e.fx = fx;
    static const char* grids[] = { "as played", "1/4", "1/8", "1/16" };
    e.said = q.grid == 0 ? juce::String ("Back to as played.")
                         : "Quantized to " + juce::String (grids[q.grid]) + (q.strength < 0.99f ? ", 50%" : "") + (q.swing ? " with swing" : "") + ". UNDO takes it back.";
    return e;
}

namespace
{
SharedTake renderWarp (const LoopTake& t, std::shared_ptr<const WarpSource> src, double bpm)
{
    bpm = juce::jlimit (40.0, 240.0, bpm);
    const int target = juce::jmax (2, (int) std::llround (src->plan.targetBeats * 60.0 / bpm * t.sampleRate));
    juce::AudioBuffer<float> in (1, juce::jmax (1, (int) src->original.size()));
    in.clear();
    std::copy (src->original.begin(), src->original.end(), in.getWritePointer (0));
    const auto out = crate::Warp::render (in, src->plan, target, t.sampleRate);
    SharedTake m = like (t, target);
    m->lengthBeats = src->plan.targetBeats;
    if (out.getNumSamples() > 0)
        std::copy (out.getReadPointer (0), out.getReadPointer (0) + juce::jmin (target, out.getNumSamples()), m->data.begin());
    m->warp = std::move (src);
    return m;
}
}

WarpSource warpSourceOf (const LoopTake& t)
{
    if (t.warp != nullptr)
        return *t.warp;
    WarpSource w;
    const int len = t.length.load();
    if (t.origin != nullptr && t.part == nullptr)
        w.original = *t.origin;
    else
        w.original.assign (t.data.begin(), t.data.begin() + juce::jlimit (0, (int) t.data.size(), len));
    w.originalBeats = t.lengthBeats.load();
    w.plan.sourceBeats = w.plan.targetBeats = w.originalBeats;
    return w;
}

Edited warpTake (const LoopTake& t, const LayerFxState& fx, const crate::Warp::Plan& plan, double bpm)
{
    Edited e;
    auto src = std::make_shared<WarpSource> (warpSourceOf (t));
    if (src->original.size() < 2 || src->originalBeats <= 0.0)
        return e.error = "Nothing to warp", e;
    src->plan = plan;
    src->plan.sourceBeats = src->originalBeats;
    if (src->plan.targetBeats <= 0.0)
        src->plan.targetBeats = src->originalBeats;
    crate::Warp::tidy (src->plan);
    if (src->plan.targetBeats * 60.0 / juce::jlimit (40.0, 240.0, bpm) > 600.0)
        return e.error = "That would pass 10 minutes.", e;
    e.take = renderWarp (t, std::move (src), bpm);
    e.fx = fx;   // the moves are per pass: they stretch with it
    return e;
}

SharedTake originalTake (const LoopTake& t, double bpm)
{
    auto src = std::make_shared<WarpSource> (warpSourceOf (t));
    if (src->original.size() < 2 || src->originalBeats <= 0.0)
        return nullptr;
    src->plan = {};
    src->plan.sourceBeats = src->plan.targetBeats = src->originalBeats;
    return renderWarp (t, std::move (src), bpm);
}

std::vector<double> warpHits (const WarpSource& w, double sampleRate, float sensitivity)
{
    juce::ignoreUnused (sampleRate);
    if (w.original.size() < 2 || w.originalBeats <= 0.0)
        return {};
    juce::AudioBuffer<float> in (1, (int) w.original.size());
    std::copy (w.original.begin(), w.original.end(), in.getWritePointer (0));
    return crate::Warp::findHits (in, w.originalBeats, sensitivity);
}

double warpSoundsLike (const WarpSource& w, const std::vector<double>& hits, double sampleRate, double songBpm)
{
    if (w.originalBeats <= 0.0 || sampleRate <= 0.0)
        return 0.0;
    const double seconds = (double) w.original.size() / sampleRate;
    std::vector<double> s;
    for (double h : hits)
        s.push_back (h / w.originalBeats * seconds);
    return crate::Warp::soundsLikeBpm (s, songBpm);
}

crate::Warp::Plan fitToBeatPlan (const WarpSource& w, double sampleRate, double songBpm, double& soundsLike)
{
    soundsLike = 0.0;
    crate::Warp::Plan p;
    p.sourceBeats = p.targetBeats = w.originalBeats;
    p.settings.style = crate::Warp::Style::voice;
    if (w.original.size() < 2 || w.originalBeats <= 0.0 || sampleRate <= 0.0 || songBpm <= 0.0)
        return p;
    const auto hits = warpHits (w, sampleRate, 0.5f);
    soundsLike = hits.size() >= 4 ? warpSoundsLike (w, hits, sampleRate, songBpm) : 0.0;
    const double seconds = (double) w.original.size() / sampleRate;
    const double sungBeats = soundsLike > 0.0 ? seconds * soundsLike / 60.0 : w.originalBeats;
    crate::Warp::setLength (p, juce::jmax (4.0, std::round (sungBeats / 4.0) * 4.0));
    crate::Warp::snap (p, hits, 0.5, 0.85f);
    return p;
}

SharedTake tempoTake (const LoopTake& t, double bpm)
{
    const int len = t.length.load();
    const double beats = t.lengthBeats.load();
    if (len < 2 || beats <= 0.0)
        return nullptr;
    bpm = juce::jlimit (40.0, 240.0, bpm);
    const int target = (int) std::llround (beats * 60.0 / bpm * t.sampleRate);
    if (std::abs (target - len) <= 1)
        return nullptr;
    if (t.warp != nullptr)
        return renderWarp (t, t.warp, bpm);   // from the recording, by its plan: same pitch, same warp
    SharedTake m = like (t, target);
    m->lengthBeats = beats;
    m->part = t.part;
    m->quant = t.quant;
    if (t.part != nullptr)
    {
        auto out = renderPart (*t.part, t.quant, bpm, t.sampleRate, target);
        std::copy (out.begin(), out.end(), m->data.begin());
        return m;
    }
    auto origin = t.origin != nullptr ? t.origin : std::make_shared<const std::vector<float>> (t.data.begin(), t.data.begin() + len);
    if (std::abs ((int) origin->size() - target) <= 1)
    {
        // back at the tempo it was made at: the audio it had, untouched
        m = like (t, (int) origin->size());
        m->lengthBeats = beats;
        std::copy (origin->begin(), origin->end(), m->data.begin());
        if (t.content > 0)
            m->content = juce::jlimit (1, (int) origin->size(), (int) std::llround ((double) t.content * (double) origin->size() / len));
        return m;
    }
    auto out = stretchAudio (origin->data(), (int) origin->size(), target);
    std::copy (out.begin(), out.end(), m->data.begin());
    m->origin = origin;
    if (t.content > 0)
        m->content = juce::jlimit (1, target, (int) std::llround ((double) t.content * target / len));
    return m;
}

//==============================================================================
namespace
{
SharedTake sineTake (double hz, int samples, double beats, double sr)
{
    auto t = LoopsEngine::makeTake (samples, sr);
    for (int i = 0; i < samples; ++i)
        t->data[(size_t) i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr);
    t->length = samples;
    t->lengthBeats = beats;
    return t;
}

// The tone's frequency from its zero crossings over the middle half.
double frequencyOf (const float* d, int n, double sr)
{
    const int a = n / 4, b = n * 3 / 4;
    int first = -1, last = -1, count = 0;
    for (int i = a; i < b - 1; ++i)
        if (d[i] <= 0.0f && d[i + 1] > 0.0f)
        {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    return count > 1 ? (count - 1) * sr / (double) (last - first) : 0.0;
}

int firstAbove (const std::vector<float>& d, int from, float level)
{
    for (int i = juce::jmax (0, from); i < (int) d.size(); ++i)
        if (std::abs (d[(size_t) i]) > level)
            return i;
    return -1;
}
}

juce::String loopToolsSelfCheck (juce::String& report)
{
    juce::StringArray problems;
    const double sr = 44100.0;

    // FIT TO THE BEAT: sung free at 90 BPM (a sound on every beat, 8 beats and a little late) over a song at 100: it
    // hears ~90, stretches to 8 beats (2 bars), and every hit lands near a 1/8 line.
    {
        const double songBpm = 100.0, sungBpm = 90.0, lead = 0.07;
        const int n = (int) ((8.0 * 60.0 / sungBpm + lead + 0.05) * sr);
        WarpSource w;
        w.original.assign ((size_t) n, 0.0f);
        juce::Random rng (5);
        for (int b = 0; b < 8; ++b)
        {
            const int at = (int) ((lead + b * 60.0 / sungBpm) * sr);
            for (int i = 0; i < (int) (0.18 * sr) && at + i < n; ++i)
                w.original[(size_t) (at + i)] = (rng.nextFloat() * 2 - 1) * 0.7f * (float) std::exp (-i / (0.04 * sr));
        }
        w.originalBeats = (double) n / sr * songBpm / 60.0;
        double like = 0;
        const auto plan = fitToBeatPlan (w, sr, songBpm, like);
        double worst = 0;
        for (int b = 1; b < 8; ++b)   // (the first sits at the take's very start, which stays pinned)
        {
            const double srcBeat = (lead + b * 60.0 / sungBpm) * songBpm / 60.0;
            const double t = plan.targetAt (srcBeat);
            worst = juce::jmax (worst, std::abs (t - std::round (t * 2.0) / 2.0));
        }
        report << "fit to the beat: heard " << juce::String (like, 1) << " BPM, " << juce::String (w.originalBeats, 2) << " beats became "
               << juce::String (plan.targetBeats, 2) << ", hits at most " << juce::String (worst, 3) << " beats off a 1/8\n";
        if (std::abs (like - sungBpm) > 4.0 || std::abs (plan.targetBeats - 8.0) > 1.0e-6 || worst > 0.06)
            problems.add ("fit to the beat: heard " + juce::String (like, 1) + " BPM (90 sung), length " + juce::String (plan.targetBeats, 2)
                          + " (8 expected), a hit " + juce::String (worst, 3) + " beats off the grid");
    }

    // FIT: a free take 4.2 bars long at 92 BPM, fitted to 4 bars (and to 8): exact length, the same pitch
    {
        const double bpm = 92.0;
        const int n = (int) (4.2 * 4.0 * 60.0 / bpm * sr);
        auto take = sineTake (440.0, n, 20.0, sr);   // recorded FREE: 5 bars, the sound 4.2 of them
        take->content = n;
        LayerFxState fx;
        fx.knob[fxReverb] = 0.4f;
        fx.moves[fxFilter].assign ((size_t) kMoveBins, 0.3f);
        for (int bars : { 4, 8 })
        {
            const double contentBeats = (double) n / take->length.load() * 20.0;
            auto e = fitTake (*take, fx, 0.0, contentBeats, bars, bpm);
            const int want = (int) std::llround (bars * 4.0 * 60.0 / bpm * sr);
            const int got = e.take != nullptr ? e.take->length.load() : 0;
            const double hz = e.take != nullptr ? frequencyOf (e.take->data.data(), got, sr) : 0.0;
            report << "fit: 4.2 bars to " << bars << " bars = " << got << " samples (expected " << want << "), " << juce::String (hz, 2) << " Hz (was 440)\n";
            if (got != want || std::abs ((e.take != nullptr ? e.take->lengthBeats.load() : 0.0) - bars * 4.0) > 1.0e-9)
                problems.add ("FIT TO " + juce::String (bars) + " BARS gave " + juce::String (got) + " samples, expected " + juce::String (want));
            if (std::abs (hz - 440.0) > 440.0 * 0.01)
                problems.add ("FIT changed the pitch: " + juce::String (hz, 1) + " Hz instead of 440");
            if (std::abs (e.fx.knob[fxReverb] - 0.4f) > 1.0e-6f || e.fx.moves[fxFilter].size() != (size_t) kMoveBins)
                problems.add ("FIT lost the layer's effects or moves");
        }
    }

    // A tempo change: 92 -> 120 BPM keeps the pitch; back to 92 gives the audio it had exactly
    {
        const int n = (int) (16.0 * 60.0 / 92.0 * sr);
        auto take = sineTake (330.0, n, 16.0, sr);
        auto faster = tempoTake (*take, 120.0);
        const int want = (int) std::llround (16.0 * 60.0 / 120.0 * sr);
        const double hz = faster != nullptr ? frequencyOf (faster->data.data(), faster->length.load(), sr) : 0.0;
        auto back = faster != nullptr ? tempoTake (*faster, 92.0) : nullptr;
        bool same = back != nullptr && back->length.load() == n;
        for (int i = 0; same && i < n; i += 97)
            same = back->data[(size_t) i] == take->data[(size_t) i];
        report << "tempo: 92 -> 120 BPM = " << (faster != nullptr ? faster->length.load() : 0) << " samples (expected " << want << "), "
               << juce::String (hz, 2) << " Hz (was 330); back to 92: " << (same ? "the original audio" : "NOT the original") << "\n";
        if (faster == nullptr || faster->length.load() != want || std::abs (hz - 330.0) > 3.3)
            problems.add ("following a tempo change did not keep the layer's length in beats and its pitch");
        if (! same)
            problems.add ("going back to the old tempo did not give the layer's original audio");
    }

    // TRIM: IN at bar 2, OUT at bar 4 of a 4-bar ramp: 2 bars, starting where IN was; the moves follow.
    // Then IN-OUT of 7 beats: 2 bars long, the last beat silence.
    {
        const int n = (int) (16.0 * 60.0 / 97.0 * sr);
        auto take = LoopsEngine::makeTake (n, sr);
        for (int i = 0; i < n; ++i)
            take->data[(size_t) i] = (float) i / (float) n;
        take->length = n;
        take->lengthBeats = 16.0;
        LayerFxState fx;
        fx.moves[fxDelay].resize ((size_t) kMoveBins);
        for (int i = 0; i < kMoveBins; ++i)
            fx.moves[fxDelay][(size_t) i] = (float) i / kMoveBins;
        auto e = trimTake (*take, fx, 4.0, 12.0);
        const int want = (int) std::llround (8.0 * n / 16.0);
        const bool ok = e.take != nullptr && std::abs (e.take->length.load() - want) <= 1 && std::abs (e.take->lengthBeats.load() - 8.0) < 1.0e-9
                        && std::abs (e.take->data[0] - 0.25f) < 1.0e-3f && e.take->content == 0 && std::abs (e.shift - 4.0) < 1.0e-9;
        const auto& m = e.fx.moves[fxDelay];
        const bool movesOk = m.size() == (size_t) kMoveBins && std::abs (m[0] - 0.25f) < 0.01f && std::abs (m[(size_t) kMoveBins - 1] - 0.75f) < 0.01f;
        report << "trim: bars 2-3 of 4 = " << (e.take != nullptr ? e.take->length.load() : 0) << " samples, " << (e.take != nullptr ? e.take->lengthBeats.load() : 0.0)
               << " beats, starts at " << (e.take != nullptr ? e.take->data[0] : -1.0f) << "; moves " << (movesOk ? "follow" : "WRONG") << "\n";
        if (! ok)
            problems.add ("TRIM of bars 2-3 did not give a 2-bar loop starting at IN");
        if (! movesOk)
            problems.add ("the moves did not follow a trim");
        auto odd = trimTake (*take, fx, 4.0, 11.0);
        const int content = (int) std::llround (7.0 * n / 16.0);
        const bool padded = odd.take != nullptr && std::abs (odd.take->lengthBeats.load() - 8.0) < 1.0e-9 && std::abs (odd.take->content - content) <= 1
                            && odd.take->data[(size_t) odd.take->length.load() - 10] == 0.0f;
        report << "trim: 7 beats -> " << (odd.take != nullptr ? odd.take->lengthBeats.load() : 0.0) << " beats long, " << (odd.take != nullptr ? odd.take->content : 0)
               << " samples of sound (expected " << content << ")\n";
        if (! padded)
            problems.add ("a 7-beat TRIM was not padded to 2 bars");
    }

    // QUANTIZE: loose pad hits and synth notes land on the grid in the rendered layer; as played they don't
    {
        const double bpm = 100.0, spb = 60.0 / bpm * sr;
        juce::AudioBuffer<float> hit (1, 600);
        for (int i = 0; i < 600; ++i)
            hit.setSample (0, i, (float) std::exp (-i / 80.0) * (i % 2 == 0 ? 0.8f : -0.8f));
        auto pads = std::make_shared<NotePart>();
        pads->kind = NotePart::pads;
        pads->pattern.bars = 1;
        pads->pattern.quantize = 0;
        pads->pattern.hits = { { 0, 0.09 }, { 0, 1.12 }, { 0, 2.93 } };
        pads->lengthBeats = 4.0;
        pads->kit.resize ((size_t) kPads);
        pads->kit[0].audio = crate::share (std::move (hit));
        const int samples = (int) std::llround (4.0 * spb);
        auto loose = renderPart (*pads, {}, bpm, sr, samples);
        LoopQuantize q;
        q.grid = 1;
        auto tight = renderPart (*pads, q, bpm, sr, samples);
        juce::String where;
        bool onGrid = true, wasLoose = std::abs (firstAbove (loose, 0, 0.1f) - 0.09 * spb) < 64;
        for (int beat : { 0, 1, 3 })
        {
            const int at = firstAbove (tight, (int) ((beat - 0.4) * spb), 0.1f);
            where << (where.isEmpty() ? "" : ", ") << juce::String (at / spb, 3);
            onGrid = onGrid && std::abs (at - beat * spb) < 64;
        }
        report << "quantize pads: hits played at 0.09, 1.12, 2.93 sound at " << where << " after 1/4\n";
        if (! onGrid || ! wasLoose)
            problems.add ("QUANTIZE did not move the loose pad hits onto the beats (" + where + ")");

        auto synth = std::make_shared<NotePart>();
        synth->kind = NotePart::synth;
        synth->preset = juce::jmax (0, synthPresetIndex ("Rubber Funk"));
        synth->notes.bars = 2;
        synth->notes.notes = { { 0.07, 0.4, 45 }, { 4.12, 0.4, 45 } };
        synth->lengthBeats = 8.0;
        const int synthSamples = (int) std::llround (8.0 * spb);
        auto sLoose = renderPart (*synth, {}, bpm, sr, synthSamples);
        q.grid = 3;
        auto sTight = renderPart (*synth, q, bpm, sr, synthSamples);
        const int l0 = firstAbove (sLoose, 0, 0.01f), t0 = firstAbove (sTight, 0, 0.01f), t1 = firstAbove (sTight, (int) (3.0 * spb), 0.01f);
        report << "quantize synth: first note at " << juce::String (l0 / spb, 3) << " as played, " << juce::String (t0 / spb, 3) << " and "
               << juce::String (t1 / spb, 3) << " after 1/16\n";
        if (l0 < 0 || t0 < 0 || t1 < 0 || std::abs (l0 - 0.07 * spb) > 400 || t0 > 400 || std::abs (t1 - 4.0 * spb) > 400)
            problems.add ("QUANTIZE did not move the loose synth notes onto the grid");

        LoopQuantize half;
        half.grid = 3;
        half.strength = 0.5f;
        if (std::abs (quantizedBeat (1.12, half, 4.0) - 1.06) > 1.0e-9 || std::abs (quantizedBeat (3.98, q, 4.0)) > 1.0e-9)
            problems.add ("QUANTIZE's 50% or its wrap at the pattern's end is wrong");
    }
    return problems.joinIntoString ("\n");
}
}
