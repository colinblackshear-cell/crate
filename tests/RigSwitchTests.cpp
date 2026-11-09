// Every switch in the Rig, flipped while things play: the tools must stop when you let go,
// go quiet when they're switched off, come back when they're switched on, never click, never
// leave a note hanging, and never start playing on their own.
//
// Like the vocoder's sound matrix test, it ends with a long random walk of switches (someone
// clicking around the Rig mid-show) and checks the sound stays sane the whole way, and that
// Stop always brings it back to silence.
#include "TestHelpers.h"

#include "RigEngine.h"

#include <functional>

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

void setParam (juce::AudioProcessor& tool, const juce::String& id, float plain)
{
    for (auto* p : tool.getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (r->getParameterID() == id)
                r->setValueNotifyingHost (r->convertTo0to1 (plain));
}

// One Rig, a voice at its input, and measurements of what comes out.
struct Bench
{
    RigEngine engine;
    juce::AudioBuffer<float> in { 2, kBlock }, out { 2, kBlock };
    double phase = 0, time = 0;
    float voiceLevel = 0.3f;
    bool smoothVoice = false;   // a steady sine instead of a sung saw, to hear clicks
    float lastOut = 0;

    struct Result
    {
        float peak = 0, jump = 0;   // jump: the biggest change from one sample to the next
        bool bad = false;           // NaN, infinity
        std::array<float, RigEngine::numStrips> strips {};
    };

    Bench()
    {
        engine.prepare (kRate, kBlock);
        // A pad and a sound in the sampler, smooth sines so clicks stand out.
        auto& project = engine.getMpc().getProject();
        project.currentBank().pads[0].audio = share (sine (44100 * 2, 1, 0.5f, 220.0));
        project.currentBank().pads[0].name = "tone";
        project.changed();
        project.dispatchPendingMessages();
        engine.getSampler().getDocument().load (share (sine (44100 * 4, 1, 0.5f, 220.0)), "tone", "Open");
        engine.getSampler().getDocument().dispatchPendingMessages();
        // The vocoder's robot alone: your voice only stands in once it has no notes, which is
        // how the tests tell a note is still playing in it.
        setParam (engine.getVocoder(), "robotOn", 1.0f);
        setParam (engine.getVocoder(), "voiceOn", 0.0f);
    }

    void fillVoice()
    {
        for (int i = 0; i < kBlock; ++i)
        {
            phase += 140.0 / kRate;
            time += 1.0 / kRate;
            const float saw = (float) (2.0 * (phase - std::floor (phase)) - 1.0);
            const float env = (float) (0.5 + 0.5 * std::sin (time * juce::MathConstants<double>::twoPi * 3.0));
            const float v = smoothVoice ? voiceLevel * (float) std::sin (phase * juce::MathConstants<double>::twoPi)
                                        : voiceLevel * env * saw;
            in.setSample (0, i, v);
            in.setSample (1, i, v);
        }
    }

    Result run (double seconds)
    {
        Result r;
        for (int s = 0; s < RigEngine::numStrips; ++s)
            engine.getStrip (s).peak = 0.0f;
        const int blocks = juce::jmax (1, (int) (seconds * kRate / kBlock));
        for (int b = 0; b < blocks; ++b)
        {
            fillVoice();
            engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            for (int i = 0; i < kBlock; ++i)
            {
                const float v = out.getSample (0, i);
                if (! std::isfinite (v) || ! std::isfinite (out.getSample (1, i)))
                    r.bad = true;
                r.peak = juce::jmax (r.peak, std::abs (v), std::abs (out.getSample (1, i)));
                r.jump = juce::jmax (r.jump, std::abs (v - lastOut));
                lastOut = v;
            }
        }
        for (int s = 0; s < RigEngine::numStrips; ++s)
            r.strips[(size_t) s] = engine.getStrip (s).peak.exchange (0.0f);
        return r;
    }

    // How loud a tool's strip is. The vocoder lets your voice stand in whenever its robot has
    // no notes (so it's never silent by surprise), so for it, a note still playing is the robot
    // still being heard instead of your voice.
    float strip (int s, double seconds)
    {
        const float level = run (seconds).strips[(size_t) s];
        if (s == RigEngine::vocoderStrip && engine.getVocoder().isVoiceStandingIn())
            return 0.0f;
        return level;
    }

    void keysTo (RigRouting::Dest d) { engine.getRouting().keysTo = (int) d; }
    void down (int note, int channel = 1) { engine.addMidiNow (juce::MidiMessage::noteOn (channel, note, 1.0f)); }
    void up (int note, int channel = 1) { engine.addMidiNow (juce::MidiMessage::noteOff (channel, note)); }
};

// The strip each tool plays into, and where the keys go to play it.
struct ToolCase
{
    const char* name;
    RigRouting::Dest dest;
    int strip;
};

const ToolCase keyTools[] = {
    { "Sampler", RigRouting::Dest::sampler, RigEngine::samplerStrip },
    { "Vocoder", RigRouting::Dest::vocoder, RigEngine::vocoderStrip },
    { "Bass", RigRouting::Dest::bass, RigEngine::bassStrip },
    { "Synth", RigRouting::Dest::synth, RigEngine::synthStrip },
};

constexpr float kQuiet = 1.0e-4f;
}

struct RigSwitchTests : juce::UnitTest
{
    RigSwitchTests() : juce::UnitTest ("Rig switches", "Rig") {}

    void runTest() override
    {
        beginTest ("a fresh Rig is silent until you play something");
        {
            Bench b;
            const auto r = b.run (3.0);
            expect (! r.bad);
            juce::String which;
            for (int s = 0; s < RigEngine::numStrips; ++s)
                which << RigEngine::stripName (s) << " " << juce::String (r.strips[(size_t) s], 3) << " ";
            expect (r.peak < 0.05f, "nothing plays by itself (peak " + juce::String (r.peak) + ": " + which + ")");
        }

        beginTest ("every tool plays from the keys, and stops when you let go");
        for (const auto& t : keyTools)
        {
            Bench b;
            b.engine.micOn = true;
            b.keysTo (t.dest);
            b.down (60);
            const float playing = b.strip (t.strip, 0.5);
            expect (playing > 0.01f, juce::String (t.name) + " plays (" + juce::String (playing) + ")");
            b.up (60);
            b.run (2.5);
            const float after = b.strip (t.strip, 0.5);
            expect (after < kQuiet, juce::String (t.name) + " stops after you let go (" + juce::String (after) + ")");
        }

        // A switch flipped while a key is held, then the key let go: the note must still end.
        struct Flip
        {
            const char* name;
            std::function<void (RigEngine&)> on, off;
        };
        const Flip flips[] = {
            { "the vocoder switched off and on", [] (RigEngine& e) { e.vocoderOn = false; }, [] (RigEngine& e) { e.vocoderOn = true; } },
            { "the strip muted and unmuted", [] (RigEngine& e) { for (int s = 0; s < RigEngine::numStrips; ++s) e.getStrip (s).mute = true; },
              [] (RigEngine& e) { for (int s = 0; s < RigEngine::numStrips; ++s) e.getStrip (s).mute = false; } },
            { "play pressed", [] (RigEngine& e) { e.getTransport().play(); }, [] (RigEngine&) {} },
            { "play then stop pressed", [] (RigEngine& e) { e.getTransport().play(); }, [] (RigEngine& e) { e.getTransport().stop(); } },
            { "the arp switched on", [] (RigEngine& e) { setParam (e.getSynth(), "arpOn", 1.0f); }, [] (RigEngine&) {} },
            { "the arp switched on and off", [] (RigEngine& e) { setParam (e.getSynth(), "arpOn", 1.0f); },
              [] (RigEngine& e) { setParam (e.getSynth(), "arpOn", 0.0f); } },
            { "the bass sequencer switched off", [] (RigEngine& e) { setParam (e.getBass(), "sequencer", 0.0f); }, [] (RigEngine&) {} },
            { "the keys moved to another tool", [] (RigEngine& e) { e.getRouting().keysTo = (int) RigRouting::Dest::mpc; }, [] (RigEngine&) {} },
            { "a split turned on", [] (RigEngine& e) { e.getRouting().splitNote = 72; e.getRouting().lowerKeysTo = (int) RigRouting::Dest::mpc; }, [] (RigEngine&) {} },
            { "the mic switched off", [] (RigEngine& e) { e.micOn = false; }, [] (RigEngine&) {} },
        };

        for (const auto& f : flips)
            for (const auto& t : keyTools)
            {
                beginTest (juce::String ("no stuck note: ") + t.name + ", " + f.name + " while a key is held");
                Bench b;
                b.engine.micOn = true;
                b.keysTo (t.dest);
                b.down (60);
                b.run (0.3);
                f.on (b.engine);
                b.run (0.3);
                f.off (b.engine);
                b.run (0.3);
                b.up (60);
                // Back to normal, so we can hear whatever is left.
                b.engine.vocoderOn = true;
                b.engine.micOn = true;
                for (int s = 0; s < RigEngine::numStrips; ++s)
                    b.engine.getStrip (s).mute = false;
                b.engine.getTransport().stop();
                b.run (3.0);
                const float after = b.strip (t.strip, 0.5);
                expect (after < kQuiet, juce::String (t.name) + " still sounds (" + juce::String (after) + ")");
            }

        beginTest ("the arp, latched, then latch switched off with no keys held, stops");
        {
            Bench b;
            b.keysTo (RigRouting::Dest::synth);
            setParam (b.engine.getSynth(), "arpOn", 1.0f);
            setParam (b.engine.getSynth(), "latch", 1.0f);
            b.down (60);
            b.run (0.2);
            b.up (60);
            expect (b.strip (RigEngine::synthStrip, 1.0) > 0.01f, "latched, it keeps going");
            setParam (b.engine.getSynth(), "latch", 0.0f);
            b.run (2.0);
            expect (b.strip (RigEngine::synthStrip, 1.0) < kQuiet, "latch off: the arp lets go");
        }

        beginTest ("the arp switched on while you hold a chord arps it; switched off, the chord plays");
        {
            Bench b;
            b.keysTo (RigRouting::Dest::synth);
            b.down (60);
            b.down (64);
            b.run (0.2);
            setParam (b.engine.getSynth(), "arpOn", 1.0f);
            b.run (0.1);
            expect (b.strip (RigEngine::synthStrip, 1.0) > 0.01f, "the held chord is arped");
            setParam (b.engine.getSynth(), "arpOn", 0.0f);
            b.run (0.3);
            expect (b.strip (RigEngine::synthStrip, 0.5) > 0.01f, "arp off: the chord you hold keeps playing");
            b.up (60);
            b.up (64);
            b.run (2.0);
            expect (b.strip (RigEngine::synthStrip, 0.5) < kQuiet, "let go: quiet");
        }

        beginTest ("a key held on the bass as Play starts the pattern doesn't hang");
        {
            Bench b;
            b.keysTo (RigRouting::Dest::bass);
            b.engine.getBass().setPattern (0, {});   // an empty pattern: rests only
            b.down (40);
            b.run (0.3);
            b.engine.getTransport().play();
            b.run (0.5);
            b.up (40);
            b.run (2.0);
            expect (b.strip (RigEngine::bassStrip, 0.5) < kQuiet);
        }

        beginTest ("a drum pad held while the pad bank changes still stops when you let go");
        {
            Bench b;
            auto& pad = b.engine.getMpc().getProject().currentBank().pads[0];
            pad.mode = PadMode::noteOff;   // plays while you hold the pad
            b.engine.getMpc().getProject().changed();
            b.engine.getMpc().getProject().dispatchPendingMessages();
            b.down (36, 10);
            expect (b.strip (RigEngine::mpcStrip, 0.3) > 0.01f, "the pad plays");
            b.engine.getRouting().padBank = 1;
            b.run (0.1);
            b.up (36, 10);
            b.run (0.5);
            expect (b.strip (RigEngine::mpcStrip, 0.3) < kQuiet, "the pad stopped");
        }

        beginTest ("the vocoder switched back on doesn't bring back old notes");
        {
            Bench b;
            b.engine.micOn = true;
            b.keysTo (RigRouting::Dest::vocoder);
            b.down (60);
            b.run (0.3);
            b.engine.vocoderOn = false;
            b.run (0.1);
            b.up (60);
            b.run (0.5);
            b.engine.vocoderOn = true;
            b.run (2.0);
            expect (b.strip (RigEngine::vocoderStrip, 0.5) < kQuiet);
        }

        beginTest ("switches don't click");
        {
            // Each switch flipped while a smooth sound plays: the biggest step between two
            // samples around the flip can't be much bigger than while it plays steadily.
            struct Click
            {
                const char* name;
                RigRouting::Dest dest;
                std::function<void (RigEngine&)> flip;
            };
            const Click clicks[] = {
                { "sampler strip mute", RigRouting::Dest::sampler, [] (RigEngine& e) { e.getStrip (RigEngine::samplerStrip).mute = true; } },
                { "master mute", RigRouting::Dest::sampler, [] (RigEngine& e) { e.getStrip (RigEngine::masterStrip).mute = true; } },
                { "vocoder off", RigRouting::Dest::vocoder, [] (RigEngine& e) { e.vocoderOn = false; } },
                { "mic off (vocoder)", RigRouting::Dest::vocoder, [] (RigEngine& e) { e.micOn = false; } },
                { "input off", RigRouting::Dest::off, [] (RigEngine& e) { e.inputOn = false; } },
                { "mic off (input)", RigRouting::Dest::off, [] (RigEngine& e) { e.micOn = false; } },
                { "input jack 1+2 to 2", RigRouting::Dest::off, [] (RigEngine& e) { e.inputJack = (int) RigEngine::InputJack::second; } },
                { "sampler source changed", RigRouting::Dest::off, [] (RigEngine& e) { e.sampleSource = (int) RigEngine::SampleSource::mix; } },
            };
            for (const auto& c : clicks)
            {
                Bench b;
                b.smoothVoice = true;
                b.engine.micOn = true;
                b.engine.inputOn = true;
                b.engine.vocoderOn = c.dest == RigRouting::Dest::vocoder;
                b.keysTo (c.dest);
                if (c.dest != RigRouting::Dest::off)
                    b.down (57);
                b.run (0.5);
                const auto steady = b.run (0.3);
                c.flip (b.engine);
                const auto flipped = b.run (0.1);
                expect (flipped.jump < juce::jmax (0.02f, steady.jump * 2.0f),
                        juce::String (c.name) + " clicks: a jump of " + juce::String (flipped.jump, 3) + " (steady " + juce::String (steady.jump, 3) + ")");
            }
        }

        beginTest ("the bass and synth stay quiet on Play unless they've something to play");
        {
            Bench b;
            setParam (b.engine.getBass(), "sequencer", 0.0f);
            b.engine.getTransport().play();
            const auto r = b.run (4.0);
            expect (r.strips[RigEngine::bassStrip] < kQuiet, "bass sequencer off: silent");
            expect (r.strips[RigEngine::synthStrip] < kQuiet, "synth with no keys: silent");
            expect (r.strips[RigEngine::mpcStrip] < kQuiet, "MPC with an empty sequence: silent");
        }

        beginTest ("a random walk of switches stays sane, and Stop always ends in silence");
        {
            Bench b;
            b.engine.micOn = true;
            juce::Random random (7);
            const int notes[] = { 48, 52, 55, 60, 64, 67, 72 };
            std::array<bool, 128> held {};
            int problems = 0;
            for (int step = 0; step < 600; ++step)
            {
                auto& e = b.engine;
                switch (random.nextInt (20))
                {
                    case 0: e.vocoderOn = ! e.vocoderOn.load(); break;
                    case 1: e.micOn = ! e.micOn.load(); break;
                    case 2: e.inputOn = ! e.inputOn.load(); break;
                    case 3: { auto& s = e.getStrip (random.nextInt (RigEngine::numStrips)); s.mute = ! s.mute.load(); break; }
                    case 4: e.getStrip (random.nextInt (RigEngine::numStrips)).level = random.nextFloat(); break;
                    case 5: e.getTransport().togglePlay(); break;
                    case 6: e.getRouting().keysTo = random.nextInt (RigRouting::kNumDests); break;
                    case 7: e.getRouting().splitNote = random.nextBool() ? 0 : 60; e.getRouting().lowerKeysTo = random.nextInt (RigRouting::kNumDests); break;
                    case 8: setParam (e.getSynth(), "arpOn", random.nextBool() ? 1.0f : 0.0f); break;
                    case 9: setParam (e.getSynth(), "latch", random.nextBool() ? 1.0f : 0.0f); break;
                    case 10: setParam (e.getBass(), "sequencer", random.nextBool() ? 1.0f : 0.0f); break;
                    case 11: setParam (e.getBass(), "pattern", (float) random.nextInt (8)); break;
                    case 12: e.getTransport().metronome = ! e.getTransport().metronome.load(); break;
                    case 13: e.sampleSource = random.nextInt (3); break;
                    case 14: e.inputJack = random.nextInt (3); break;
                    case 15: e.addMidiNow (juce::MidiMessage::noteOn (10, 36 + random.nextInt (4), 1.0f)); break;
                    default:
                    {
                        const int n = notes[random.nextInt (7)];
                        if (held[(size_t) n])
                            b.up (n);
                        else
                            b.down (n);
                        held[(size_t) n] = ! held[(size_t) n];
                        break;
                    }
                }
                const auto r = b.run (0.05 + random.nextFloat() * 0.2);
                if (r.bad || r.peak > 1.0f)
                {
                    ++problems;
                    logMessage ("step " + juce::String (step) + ": bad sound (peak " + juce::String (r.peak) + ")");
                }
            }
            expectEquals (problems, 0, "no NaNs, never past full scale");

            // Let go of everything, and press Stop the way the window does.
            for (int n = 0; n < 128; ++n)
                if (held[(size_t) n])
                    b.up (n);
            b.engine.getTransport().stop();
            b.engine.allNotesOff();
            b.engine.getTransport().metronome = false;
            b.run (4.0);
            const auto after = b.run (1.0);
            for (int s = 0; s < RigEngine::numStrips; ++s)
                expect (after.strips[(size_t) s] < kQuiet || s == RigEngine::inputStrip || s == RigEngine::masterStrip || s == RigEngine::loopsStrip,
                        RigEngine::stripName (s) + " still sounds after Stop (" + juce::String (after.strips[(size_t) s]) + ")");
        }
    }
};

static RigSwitchTests rigSwitchTests;
