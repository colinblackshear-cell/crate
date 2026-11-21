#include "PocketFxChecks.h"
#include "PocketLooper.h"
#include "PocketSynth.h"

namespace pocket
{
juce::uint64 mixFingerprint (std::function<void (LoopsEngine&)> tweak, bool synthNotes)
{
    const double sr = 44100.0, bpm = 97.0;
    const int barSamples = (int) (4.0 * 60.0 / bpm * sr);
    BeatsEngine b;
    LoopsEngine L (b);
    SynthEngine synth (b);
    L.setInstrument (0, &synth);
    L.prepare (sr);
    L.setLatency (0);
    L.setCountInBars (0);
    b.setBpm (bpm);

    DrumData d;
    d.patterns[0][0] = DrumData::demoPattern();
    d.bpm = bpm;
    b.setDrums (d);

    juce::AudioBuffer<float> pad (2, 9000);
    for (int i = 0; i < pad.getNumSamples(); ++i)
    {
        const float env = std::exp (-(float) i / 2500.0f);
        pad.setSample (0, i, 0.5f * env * (float) std::sin (i * 0.05));
        pad.setSample (1, i, 0.4f * env * (float) std::sin (i * 0.031));
    }
    PadSetup ps;
    ps.audio = crate::share (std::move (pad));
    b.setPad (0, ps);
    PadPattern pp;
    pp.bars = 1;
    for (double beat : { 0.0, 1.5, 2.0, 2.25, 3.5 })
    {
        PadHit h;
        h.pad = 0;
        h.beat = beat;
        h.velocity = 0.9f;
        pp.hits.push_back (h);
    }
    b.setPadPattern (pp);
    b.setPatternPlaying (true);

    synth.setSettings (makeSynthSettings (0, SynthKnobs(), 0));
    SynthPattern sp;
    if (synthNotes)
    {
        sp.setStep (0, 57);
        sp.setStep (4, 60);
        sp.setStep (8, 64);
        sp.setStep (12, 62);
    }
    synth.setPattern (sp);
    synth.setPlayFrom (-1.0e9);
    synth.setEcho (0.2f);

    auto t = LoopsEngine::makeTake (barSamples, sr);
    for (int i = 0; i < barSamples; ++i)
        t->data[(size_t) i] = 0.3f * (float) std::sin (i * 0.02) * (float) ((i / 4000) % 2);
    t->length = barSamples;
    t->lengthBeats = 4.0;
    L.setLayer (0, t, false, 0.0);
    L.setKnob (0, fxDelay, 0.4f);
    L.setKnob (0, fxReverb, 0.3f);
    L.setKnob (0, fxFilter, 0.35f);
    L.setMic (true, true);
    L.setDryMonitor (true);
    b.setClick (true);
    b.setPlaying (true);
    if (tweak)
        tweak (L);

    std::vector<float> l (512), r (512), in (512);
    juce::uint64 hash = 14695981039346656037ull;
    juce::int64 counter = 0;
    for (int block = 0; block < 300; ++block)
    {
        for (auto& s : in)
        {
            s = 0.25f * (float) std::sin ((double) counter * 0.0313) * (float) ((counter / 9000) % 2);
            ++counter;
        }
        const float* ptr = in.data();
        L.setInput (&ptr, 1);
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        L.render (l.data(), r.data(), 512);
        for (int i = 0; i < 512; ++i)
            for (float v : { l[(size_t) i], r[(size_t) i] })
            {
                juce::uint32 bits;
                std::memcpy (&bits, &v, sizeof (bits));
                hash = (hash ^ bits) * 1099511628211ull;
            }
    }
    return hash;
}
}
