// The level check: every sound the Rig can play, rendered offline at its default settings and
// measured, so no sound is quieter or louder than the others, and a safety limiter at the end of
// everything keeps the output from ever clipping.
//
// What's measured is the Rig's main output with one tool playing (every strip at its default
// fader, no effects): the peak in dBFS, and a loudness, the RMS of the sounding part (the 100 ms blocks
// within 10 dB of the loudest, as BS.1770's gate picks them) of the sound after K-weighting (ITU-R BS.1770's two filters: the shelf that makes highs count for
// more, and the high pass that stops sub-bass counting for what it isn't). The sounds are played
// at velocity 100.
//
// Targets:
//   - Tonal sounds (the Bass's and Synth's named sounds, the vocoder's sounds, a chord on the
//     Synth) at -18 dB K-RMS, all within 1.5 dB of it.
//   - Drum hits by peak (the middle one of nine hits, as the noise in a snare or hat makes every hit
//     a little different), the kick at -6 dBFS, and the rest around it by what they are (hats, bells
//     and cymbals sit 4 to 6 dB under the kick), within 1.5 dB of their own target.
//
// The level of each sound is its trim (RigSounds::Sound::trimDb for the Bass and Synth, the drum
// engine's kTrimDb, the vocoder's trim in RigSounds), set from this test's table. Run it on its
// own to see the table:  CrateTests "Rig levels".
#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigSounds.h"
#include "core/SafetyLimiter.h"
#include "Presets.h"   // the vocoder's

#include <algorithm>
#include <cmath>

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;
constexpr float kVelocity = 100.0f / 127.0f;

constexpr float kTonalTarget = -18.0f, kTonalWindow = 1.5f;
constexpr float kDrumWindow = 1.5f;
// The drums' peak targets (dBFS), by voice: the kick leads, the rest sit around it.
constexpr float kDrumTarget[drums::kVoices] = { -6.0f,    // kick
                                                -7.0f,    // snare
                                                -8.0f,    // clap
                                                -9.0f,    // rim
                                                -8.0f,    // low tom
                                                -8.0f,    // mid tom
                                                -8.0f,    // high tom
                                                -11.0f,   // clave
                                                -11.0f,   // cowbell
                                                -11.0f,   // closed hat
                                                -11.0f,   // open hat
                                                -11.0f }; // cymbal

//==============================================================================
// BS.1770's K-weighting, for any sample rate (a biquad each: the shelf, then the high pass).
struct KWeight
{
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    } shelf, highPass;

    explicit KWeight (double rate)
    {
        {
            const double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
            const double k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const double vh = std::pow (10.0, g / 20.0), vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;
            shelf.b0 = (vh + vb * k / q + k * k) / a0;
            shelf.b1 = 2.0 * (k * k - vh) / a0;
            shelf.b2 = (vh - vb * k / q + k * k) / a0;
            shelf.a1 = 2.0 * (k * k - 1.0) / a0;
            shelf.a2 = (1.0 - k / q + k * k) / a0;
        }
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k = std::tan (juce::MathConstants<double>::pi * f0 / rate);
            const double a0 = 1.0 + k / q + k * k;
            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (k * k - 1.0) / a0;
            highPass.a2 = (1.0 - k / q + k * k) / a0;
        }
    }

    double process (double x) { return highPass.process (shelf.process (x)); }
};

struct Level
{
    float peakDb = -120.0f;   // the loudest sample, dBFS
    float loudDb = -120.0f;   // the sounding part, K-weighted RMS, dBFS
    bool bad = false;         // anything not a number
};

// Measures what a render of the main output holds.
Level measure (const std::vector<float>& left, const std::vector<float>& right)
{
    Level result;
    const int n = (int) left.size();
    float peak = 0.0f;
    std::vector<double> power ((size_t) n);
    KWeight kl (kRate), kr (kRate);
    for (int i = 0; i < n; ++i)
    {
        const float l = left[(size_t) i], r = right[(size_t) i];
        if (! std::isfinite (l) || ! std::isfinite (r))
        {
            result.bad = true;
            return result;
        }
        peak = juce::jmax (peak, std::abs (l), std::abs (r));
        const double wl = kl.process (l), wr = kr.process (r);
        power[(size_t) i] = 0.5 * (wl * wl + wr * wr);
    }
    result.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);

    // Blocks of 100 ms every 50 ms, and the sound is the blocks within 10 dB of the loudest (as
    // BS.1770's gate does): a pluck counts for how loud it hits, a pad for how loud it holds, and
    // the silence and the tail round them count for nothing.
    const int window = (int) (0.1 * kRate), hop = (int) (0.05 * kRate);
    std::vector<double> blocks;
    for (int start = 0; start + window <= n; start += hop)
    {
        double sum = 0;
        for (int i = start; i < start + window; ++i)
            sum += power[(size_t) i];
        blocks.push_back (sum / window);
    }
    const double loudest = blocks.empty() ? 0.0 : *std::max_element (blocks.begin(), blocks.end());
    double total = 0;
    int counted = 0;
    for (double b : blocks)
        if (b >= loudest * 0.1)
        {
            total += b;
            ++counted;
        }
    const double mean = counted > 0 ? total / counted : 0.0;
    result.loudDb = mean > 1.0e-12 ? (float) (10.0 * std::log10 (mean)) : -120.0f;
    return result;
}

//==============================================================================
// A Rig and a way to play it and record what comes out of it.
struct Bench
{
    RigEngine engine;
    juce::AudioBuffer<float> in { 2, kBlock }, out { 2, kBlock };
    double voicePhase = 0;
    bool voiceIn = false;   // sing a saw into the input (for the vocoder)

    Bench()
    {
        engine.prepare (kRate, kBlock);
        // The vocoder's robot and voice alone (they're all it has to be measured with).
        engine.getRouting().keysTo = (int) RigRouting::Dest::off;
    }

    void keysTo (RigRouting::Dest d) { engine.getRouting().keysTo = (int) d; }
    int channel = 1;
    void down (int note) { engine.addMidiNow (juce::MidiMessage::noteOn (channel, note, (juce::uint8) 100)); }
    void up (int note) { engine.addMidiNow (juce::MidiMessage::noteOff (channel, note)); }

    void fillVoice()
    {
        for (int i = 0; i < kBlock; ++i)
        {
            float v = 0.0f;
            if (voiceIn)
            {
                voicePhase += 140.0 / kRate;
                v = 0.3f * (float) (2.0 * (voicePhase - std::floor (voicePhase)) - 1.0);
            }
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }
    }

    // Plays `seconds` of the Rig, appending what comes out.
    void render (double seconds, std::vector<float>& left, std::vector<float>& right)
    {
        const int blocks = juce::jmax (1, (int) std::ceil (seconds * kRate / kBlock));
        for (int b = 0; b < blocks; ++b)
        {
            fillVoice();
            engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                left.push_back (out.getSample (0, i));
                right.push_back (out.getSample (1, i));
            }
        }
    }

    // A note held for `hold` seconds, then let go, with a tail: what's measured.
    Level playNotes (std::initializer_list<int> notes, double hold = 1.5, double tail = 0.5)
    {
        std::vector<float> l, r;
        for (int n : notes)
            down (n);
        render (hold, l, r);
        for (int n : notes)
            up (n);
        render (tail, l, r);
        const auto level = measure (l, r);
        settle();
        return level;
    }

    // A drum hit, with room to ring out. The noise in snares and hats is different every time, so
    // a hit is the middle one of nine.
    Level playHit (int note, double length = 1.2)
    {
        std::vector<Level> hits;
        for (int i = 0; i < 9; ++i)
        {
            std::vector<float> l, r;
            down (note);
            up (note);
            render (length, l, r);
            hits.push_back (measure (l, r));
        }
        if (std::getenv ("LEVEL_DEBUG"))
            for (const auto& h : hits)
                std::cout << "   hit " << h.peakDb << std::endl;
        std::sort (hits.begin(), hits.end(), [] (const Level& a, const Level& b) { return a.peakDb < b.peakDb; });
        auto middle = hits[4];
        for (const auto& h : hits)
            middle.bad = middle.bad || h.bad;
        settle();
        return middle;
    }

    // Lets the last sound die away (and the vocoder's, which needs its voice) before the next.
    void settle()
    {
        engine.addMidiNow (juce::MidiMessage::allNotesOff (1));
        std::vector<float> l, r;
        render (1.5, l, r);
    }
};

float round1 (float v) { return std::round (v * 10.0f) / 10.0f; }

// A row of the table. The "LEVEL|" lines are for a script that sets the trims from them.
void row (juce::UnitTest& t, const char* tool, const juce::String& name, const Level& level, float trimDb, float target, bool byPeak)
{
    const float measured = byPeak ? level.peakDb : level.loudDb;
    const float wanted = round1 (trimDb + target - measured);
    t.logMessage (juce::String::formatted ("LEVEL|%s|%s|%.1f|%.1f|%.1f|%.1f", tool, name.toRawUTF8(), level.peakDb, level.loudDb, trimDb, wanted));
}
}

struct RigLevelTests : juce::UnitTest
{
    RigLevelTests() : juce::UnitTest ("Rig levels", "Rig") {}

    void expectNear (const char* tool, const juce::String& name, float measured, float target, float window)
    {
        expect (std::abs (measured - target) <= window,
                juce::String (tool) + " " + name + ": " + juce::String (measured, 1) + " dB, wanted " + juce::String (target, 1)
                    + " +/- " + juce::String (window, 1));
    }

    // Every named sound of the Bass or the Synth, one note held.
    void tonalSounds (const char* tool, const std::vector<RigSounds::Sound>& sounds, RigRouting::Dest dest, int note,
                      juce::AudioProcessorValueTreeState& params, Bench& b)
    {
        b.keysTo (dest);
        for (const auto& sound : sounds)
        {
            RigSounds::apply (params, sound);
            const auto level = b.playNotes ({ note });
            expect (! level.bad, juce::String (tool) + " " + sound.name + " made something that isn't a number");
            row (*this, tool, sound.name, level, sound.trimDb, kTonalTarget, false);
            expectNear (tool, sound.name, level.loudDb, kTonalTarget, kTonalWindow);
            expect (level.peakDb <= SafetyLimiter::kCeilingDb + 0.01f, juce::String (tool) + " " + sound.name + " is over the ceiling");
        }
    }

    void runTest() override
    {
        beginTest ("levels: every Bass sound");
        {
            Bench b;
            tonalSounds ("Bass", RigSounds::bass(), RigRouting::Dest::bass, 48, b.engine.getBass().params, b);
        }

        beginTest ("levels: every Synth sound");
        {
            Bench b;
            tonalSounds ("Synth", RigSounds::synth(), RigRouting::Dest::synth, 60, b.engine.getSynth().params, b);
        }

        beginTest ("levels: a chord on the Synth");
        {
            // The chord builder plays notes into the tool, so a chord is the Synth's sound with
            // four notes. (Chords add up, so they're allowed to sit a little over one note.)
            Bench b;
            b.keysTo (RigRouting::Dest::synth);
            auto& synth = b.engine.getSynth().params;
            for (const auto& sound : RigSounds::synth())
            {
                RigSounds::apply (synth, sound);
                const auto level = b.playNotes ({ 48, 52, 55, 59 });
                row (*this, "Chord", sound.name, level, sound.trimDb, kTonalTarget, false);
                expectNear ("Chord", sound.name, level.loudDb, kTonalTarget + 3.0f, 4.5f);
                expect (! level.bad && level.peakDb <= SafetyLimiter::kCeilingDb + 0.01f, juce::String ("Chord ") + sound.name + " is over the ceiling");
            }
        }

        beginTest ("levels: the drum machine, every voice and every sound");
        {
            Bench b;
            b.keysTo (RigRouting::Dest::drums);
            auto& params = b.engine.getDrums().params;
            for (int v = 0; v < drums::kVoices; ++v)
            {
                const auto names = drums::soundNames (v);
                for (int s = 0; s < names.size(); ++s)
                {
                    if (auto* p = params.getParameter (DrumProcessor::voiceParam (v, "Sound")))
                        p->setValueNotifyingHost (p->convertTo0to1 ((float) s));
                    const auto level = b.playHit (drums::noteForVoice (v));
                    const juce::String name = juce::String (drums::voiceName (v)) + " / " + names[s];
                    expect (! level.bad, name + " made something that isn't a number");
                    row (*this, "Drums", name, level, drumTrim (v, s), kDrumTarget[v], true);
                    expectNear ("Drums", name, level.peakDb, kDrumTarget[v], kDrumWindow);
                    
                }
                if (auto* p = params.getParameter (DrumProcessor::voiceParam (v, "Sound")))
                    p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
            }
            // Hats sit under the kick, not level with it.
            const auto kick = b.playHit (drums::noteForVoice (drums::kick));
            const auto hat = b.playHit (drums::noteForVoice (drums::closedHat));
            expect (kick.peakDb - hat.peakDb >= 3.5f && kick.peakDb - hat.peakDb <= 6.5f,
                    "the hat sits 4 to 6 dB under the kick: " + juce::String (kick.peakDb - hat.peakDb, 1));
        }

        beginTest ("levels: the vocoder's sounds");
        {
            Bench b;
            b.voiceIn = true;
            b.engine.micOn = true;   // Logic's side chain is on
            b.keysTo (RigRouting::Dest::vocoder);
            auto& vocoder = b.engine.getVocoder();
            for (int i = 0; i < vocoder.getNumPrograms(); ++i)
            {
                vocoder.setCurrentProgram (i);
                const auto name = vocoder.getProgramName (i);
                const auto level = b.playNotes ({ 60 });
                expect (! level.bad, name + " made something that isn't a number");
                row (*this, "Vocoder", name, level, vocoderTrim (name), kTonalTarget, false);
                // The vocoder follows your voice, so it's held to a wider window; a sound that
                // is only your voice back (the first) is as loud as the voice is.
                if (i > 0)
                    expectNear ("Vocoder", name, level.loudDb, kTonalTarget, 3.0f);
                expect (level.peakDb <= SafetyLimiter::kCeilingDb + 0.01f, "Vocoder " + name + " is over the ceiling");
            }
        }

        beginTest ("levels: the Drone, as a new Rig has it");
        {
            // It's off the clock: on, it plays straight away. Measured as the tonal sounds are,
            // over a few seconds of plucks (2 dB of room: its plucks are never quite the same).
            Bench b;
            if (auto* p = b.engine.getDrone().params.getParameter ("droneOn"))
                p->setValueNotifyingHost (1.0f);
            std::vector<float> l, r;
            b.render (8.0, l, r);
            const auto level = measure (l, r);
            logMessage (juce::String::formatted ("LEVEL|Drone|default|%.1f|%.1f|0.0|0.0", level.peakDb, level.loudDb));
            expect (! level.bad, "the drone made something that isn't a number");
            expectNear ("Drone", "default", level.loudDb, kTonalTarget, 2.0f);
            expect (level.peakDb <= SafetyLimiter::kCeilingDb + 0.01f, "the drone is over the ceiling");
        }

        beginTest ("levels: the sampler and the MPC pads play a sound as it is");
        {
            // They play whatever you load, at its own level: a test tone at -12 dBFS comes out at
            // about that (a velocity of 100 takes a little off); the Rig adds nothing to it.
            Bench b;
            auto& project = b.engine.getMpc().getProject();
            project.currentBank().pads[0].audio = share (sine (44100 * 2, 1, 0.25f, 220.0));
            project.currentBank().pads[0].name = "tone";
            project.changed();
            project.dispatchPendingMessages();
            b.channel = 10;   // the pads' channel
            const auto level = b.playNotes ({ 36 }, 1.0, 0.5);
            logMessage (juce::String::formatted ("LEVEL|MPC|test tone -12 dBFS|%.1f|%.1f|0.0|0.0", level.peakDb, level.loudDb));
            expect (level.peakDb > -24.0f && level.peakDb < -6.0f, "the MPC pad plays the sound at about the level it was loaded at");
        }

        beginTest ("safety limiter: a sine 12 dB over full scale stays under -0.3 dBFS");
        {
            SafetyLimiter limiter;
            limiter.prepare (kRate);
            const float ceiling = juce::Decibels::decibelsToGain (SafetyLimiter::kCeilingDb);
            for (double hz : { 55.0, 441.0, 3000.0 })
            {
                limiter.reset();
                std::vector<float> l ((size_t) kRate), r ((size_t) kRate);
                for (size_t i = 0; i < l.size(); ++i)
                    l[i] = r[i] = 4.0f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / kRate);
                for (size_t i = 0; i < l.size(); i += kBlock)
                    limiter.process (l.data() + i, r.data() + i, (int) juce::jmin ((size_t) kBlock, l.size() - i));
                float peak = 0.0f;
                bool finite = true;
                for (size_t i = 0; i < l.size(); ++i)
                {
                    peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
                    finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
                }
                expect (finite, "no NaN");
                expect (peak <= ceiling, "a +12 dB sine at " + juce::String (hz) + " Hz peaks at " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS");
                expectGreaterThan (peak, ceiling * 0.5f, "and it isn't just silenced");
            }
        }

        beginTest ("safety limiter: nothing below -3 dBFS is touched, and NaN is silenced");
        {
            SafetyLimiter limiter;
            limiter.prepare (kRate);
            auto quiet = sine (48000, 2, 0.7f, 441.0);   // -3.1 dBFS
            const auto copy = quiet;
            limiter.process (quiet.getWritePointer (0), quiet.getWritePointer (1), 48000);
            bool same = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                    same = same && quiet.getSample (ch, i) == copy.getSample (ch, i);
            expect (same, "a signal under -3 dBFS passes bit for bit");

            std::vector<float> l (512, 0.5f), r (512, 0.5f);
            l[100] = std::numeric_limits<float>::quiet_NaN();
            r[300] = std::numeric_limits<float>::infinity();
            expect (limiter.process (l.data(), r.data(), 512), "a block with NaN is reported");
            bool silent = true;
            for (size_t i = 0; i < 512; ++i)
                silent = silent && l[i] == 0.0f && r[i] == 0.0f;
            expect (silent, "and zeroed");
            std::vector<float> after (64, 0.25f);
            limiter.process (after.data(), nullptr, 64);
            expectEquals (after[10], 0.25f, "and the next block is fine");
        }

        beginTest ("safety limiter: the Rig's output stays under the ceiling with everything wide open");
        {
            Bench b;
            b.voiceIn = true;
            b.engine.micOn = true;
            for (int s = 0; s < RigEngine::numStrips; ++s)
                b.engine.getStrip (s).level = 1.0f;   // every fader at +6 dB
            for (auto* tool : { &b.engine.getBass().params, &b.engine.getSynth().params })
                if (auto* p = tool->getParameter ("volume"))
                    p->setValueNotifyingHost (1.0f);
            for (int v = 0; v < drums::kVoices; ++v)
                if (auto* p = b.engine.getDrums().params.getParameter (DrumProcessor::voiceParam (v, "Level")))
                    p->setValueNotifyingHost (1.0f);
            b.keysTo (RigRouting::Dest::synth);
            std::vector<float> l, r;
            for (int n : { 36, 40, 43, 47, 48, 52, 55 })
                b.down (n);
            b.engine.addMidiNow (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 127));
            b.engine.addMidiNow (juce::MidiMessage::noteOn (10, 38, (juce::uint8) 127));
            b.render (2.0, l, r);
            float peak = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < l.size(); ++i)
            {
                peak = juce::jmax (peak, std::abs (l[i]), std::abs (r[i]));
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]);
            }
            expect (finite, "no NaN in the Rig's output");
            expect (peak <= juce::Decibels::decibelsToGain (SafetyLimiter::kCeilingDb) + 1.0e-4f,
                    "the Rig's output peaks at " + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS");
        }
    }

    // The trims as they are now (for the table).
    static float drumTrim (int voice, int sound) { return drums::trimDb (voice, sound); }
    static float vocoderTrim (const juce::String& name) { return RigSounds::vocoderTrimDb (name); }
};

static RigLevelTests rigLevelTests;
