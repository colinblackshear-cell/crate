#include "TestHelpers.h"

#include "BassProcessor.h"
#include "RigEngine.h"
#include "SynthProcessor.h"

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;

// Renders `seconds` of an engine with a steady clock and returns channel 0.
template <typename EngineType, typename ClockType>
std::vector<float> renderFor (EngineType& engine, double seconds, ClockType clock, juce::MidiBuffer first = {})
{
    std::vector<float> out;
    const int block = 480;
    const int blocks = (int) std::ceil (seconds * kRate / block);
    std::vector<float> l ((size_t) block), r ((size_t) block);
    for (int b = 0; b < blocks; ++b)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        engine.render (l.data(), r.data(), block, b == 0 ? first : juce::MidiBuffer(), clock);
        out.insert (out.end(), l.begin(), l.end());
        if (clock.playing)
            clock.ppq += block * clock.bpm / (60.0 * kRate);
    }
    return out;
}

float peakIn (const std::vector<float>& v, double fromSeconds, double toSeconds)
{
    float peak = 0;
    const auto a = (size_t) (fromSeconds * kRate), b = juce::jmin (v.size(), (size_t) (toSeconds * kRate));
    for (size_t i = a; i < b; ++i)
        peak = juce::jmax (peak, std::abs (v[i]));
    return peak;
}

int parameterIndex (juce::AudioProcessor& p, const juce::String& id)
{
    const auto& list = p.getParameters();
    for (int i = 0; i < list.size(); ++i)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (list[i]))
            if (withId->paramID == id)
                return i;
    return -1;
}
}

struct BassTests : juce::UnitTest
{
    BassTests() : juce::UnitTest ("Bass", "Synths") {}

    void runTest() override
    {
        bass::Clock playing;
        playing.playing = true;
        playing.bpm = 120.0;   // a 1/16 step is 0.125 s

        beginTest ("the pattern plays its steps in time");
        {
            bass::Engine engine;
            engine.prepare (kRate);
            bass::Pattern p;
            p.steps[4].on = true;     // the second beat
            engine.setPattern (&p);
            const auto out = renderFor (engine, 1.0, playing);
            expect (peakIn (out, 0.0, 0.49) < 1.0e-4f, "silent before step 5");
            expect (peakIn (out, 0.5, 0.56) > 0.05f, "step 5 plays at half a second");
            expect (peakIn (out, 0.65, 0.99) < 1.0e-3f, "and stops after its gate");
            expectEquals (engine.getCurrentStep(), 7, "a second at 120 BPM ends in step 8");
        }

        beginTest ("nothing plays when the clock is stopped, and the keys play it instead");
        {
            bass::Engine engine;
            engine.prepare (kRate);
            bass::Pattern p;
            p.steps[0].on = true;
            engine.setPattern (&p);
            bass::Clock stopped;
            expect (peakIn (renderFor (engine, 0.3, stopped), 0, 0.3) < 1.0e-6f);

            juce::MidiBuffer key;
            key.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 0);
            expect (peakIn (renderFor (engine, 0.3, stopped, key), 0, 0.3) > 0.05f, "a key plays the bass");
        }

        beginTest ("a key transposes the running pattern");
        {
            bass::Engine engine;
            engine.prepare (kRate);
            bass::Pattern p;
            p.steps[0].on = true;
            engine.setPattern (&p);
            juce::MidiBuffer key;
            key.addEvent (juce::MidiMessage::noteOn (1, 43, 1.0f), 0);   // root is 36
            renderFor (engine, 0.2, playing, key);
            expectEquals (engine.getTranspose(), 7);
        }

        beginTest ("a slide holds the note into the next step");
        {
            auto gapBetweenSteps = [&] (bool slide)
            {
                bass::Engine engine;
                engine.prepare (kRate);
                auto s = engine.getSettings();
                s.gate = 0.3f;
                engine.setSettings (s);
                bass::Pattern p;
                p.steps[0].on = p.steps[1].on = true;
                p.steps[1].note = 5;
                p.steps[0].slide = slide;
                engine.setPattern (&p);
                const auto out = renderFor (engine, 0.3, playing);
                return peakIn (out, 0.09, 0.12);   // after step 1's gate, before step 2
            };
            expect (gapBetweenSteps (false) < 0.01f, "without slide the note ends at its gate");
            expect (gapBetweenSteps (true) > 0.05f, "with slide it carries on");
        }

        beginTest ("patterns are saved with the project, and switch at the bar");
        {
            BassProcessor processor;
            juce::Random r (5);
            const auto line = BassProcessor::randomPattern (r);
            processor.setPattern (3, line);
            juce::MemoryBlock state;
            processor.getStateInformation (state);

            BassProcessor other;
            other.setStateInformation (state.getData(), (int) state.getSize());
            const auto back = other.getPattern (3);
            bool same = back.length == line.length;
            for (int i = 0; i < bass::kSteps; ++i)
                same = same && back.steps[(size_t) i].on == line.steps[(size_t) i].on && back.steps[(size_t) i].note == line.steps[(size_t) i].note
                       && back.steps[(size_t) i].accent == line.steps[(size_t) i].accent && back.steps[(size_t) i].slide == line.steps[(size_t) i].slide;
            expect (same);

            // Mid-bar, the new pattern waits for the bar line.
            RigEngine rig;
            rig.prepare (kRate, 480);
            auto& b = rig.getBass();
            rig.getTransport().setBpm (120.0);
            rig.getTransport().play();
            juce::AudioBuffer<float> out (2, 480);
            for (int i = 0; i < 50; ++i)   // half a second: beat 1 of bar 1
                rig.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            b.selectPattern (2);
            for (int i = 0; i < 10; ++i)
                rig.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expectEquals (b.getPlayingPattern(), 0, "still the old pattern mid-bar");
            for (int i = 0; i < 160; ++i)  // past the end of the bar (2 s)
                rig.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expectEquals (b.getPlayingPattern(), 2, "the new one from the next bar");
        }
    }
};

struct SynthToolTests : juce::UnitTest
{
    SynthToolTests() : juce::UnitTest ("Synth", "Synths") {}

    void runTest() override
    {
        beginTest ("keys play chords");
        {
            synthtool::Engine engine;
            engine.prepare (kRate);
            juce::MidiBuffer chord;
            for (int n : { 60, 64, 67 })
                chord.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 0);
            synthtool::Clock stopped;
            expect (peakIn (renderFor (engine, 0.3, stopped, chord), 0.05, 0.3) > 0.05f);
            expectEquals (engine.getActiveVoices(), 3);
        }

        beginTest ("the arp plays only on the rhythm's lit steps, in time");
        {
            synthtool::Engine engine;
            engine.prepare (kRate);
            synthtool::Settings s;
            s.arpOn = true;
            s.release = 0.01f;
            s.arpGate = 0.5f;
            engine.setSettings (s);
            synthtool::Rhythm rhythm;
            rhythm.on.fill (false);
            rhythm.on[0] = true;     // only the first 1/16 of the bar
            engine.setRhythm (rhythm);

            synthtool::Clock playing;
            playing.playing = true;
            playing.bpm = 120.0;
            juce::MidiBuffer chord;
            for (int n : { 60, 64, 67 })
                chord.addEvent (juce::MidiMessage::noteOn (1, n, 1.0f), 0);
            const auto out = renderFor (engine, 2.2, playing, chord);
            expect (peakIn (out, 0.0, 0.06) > 0.05f, "step 1 plays");
            expect (peakIn (out, 0.3, 1.95) < 1.0e-3f, "steps 2-16 are silent");
            expect (peakIn (out, 2.0, 2.06) > 0.05f, "and step 1 of the next bar plays");
        }

        beginTest ("latch keeps the arp going after you let go");
        {
            synthtool::Engine engine;
            engine.prepare (kRate);
            synthtool::Settings s;
            s.arpOn = true;
            s.latch = true;
            engine.setSettings (s);
            synthtool::Clock playing;
            playing.playing = true;
            juce::MidiBuffer keys;
            keys.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 0);
            keys.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
            const auto out = renderFor (engine, 1.0, playing, keys);
            expect (peakIn (out, 0.5, 1.0) > 0.05f);
        }

        beginTest ("the arp's rhythm is saved with the project");
        {
            SynthProcessor p;
            auto r = p.getRhythm();
            r.on[3] = false;
            r.accent[5] = true;
            r.length = 12;
            p.setRhythm (r);
            juce::MemoryBlock state;
            p.getStateInformation (state);
            SynthProcessor other;
            other.setStateInformation (state.getData(), (int) state.getSize());
            const auto back = other.getRhythm();
            expect (! back.on[3] && back.on[4] && back.accent[5] && back.length == 12);
        }
    }
};

struct RigSynthTests : juce::UnitTest
{
    RigSynthTests() : juce::UnitTest ("Bass and Synth in the Rig", "Rig") {}

    void runTest() override
    {
        beginTest ("a split sends the left hand to the bass");
        {
            RigRouting r;
            r.keysTo = (int) RigRouting::Dest::synth;
            r.splitNote = 48;
            r.lowerKeysTo = (int) RigRouting::Dest::bass;
            juce::MidiBuffer in, mpc, sampler, vocoder, bass, synth;
            std::array<juce::MidiBuffer*, RigRouting::kNumDests> to { nullptr, &mpc, &sampler, &vocoder, &bass, &synth };
            in.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 0);
            in.addEvent (juce::MidiMessage::noteOn (1, 64, 1.0f), 1);
            RigRouting::Result result;
            r.route (in, to, result);
            expectEquals (bass.getNumEvents(), 1);
            expectEquals (synth.getNumEvents(), 1);
            expectEquals (vocoder.getNumEvents(), 0);
        }

        beginTest ("learn a knob onto the bass's cutoff");
        {
            RigEngine engine;
            engine.prepare (kRate, 480);
            auto& routing = engine.getRouting();
            routing.catchNextKnob();
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 21, 10));
            juce::AudioBuffer<float> out (2, 480);
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            const auto caught = routing.takeCaughtKnob();
            expect (caught == std::pair<int, int> { 1, 21 }, "the turned knob is caught");

            const int cutoff = parameterIndex (engine.getBass(), "cutoff");
            expect (cutoff >= 0);
            routing.addParameterKnob (caught.first, caught.second, RigEngine::bassTool, cutoff);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 21, 127));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expectWithinAbsoluteError (engine.getBass().params.getRawParameterValue ("cutoff")->load(), 8000.0f, 1.0f);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 21, 0));
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expectWithinAbsoluteError (engine.getBass().params.getRawParameterValue ("cutoff")->load(), 40.0f, 1.0f);

            beginTest ("learned knobs are saved with the show, by setting name");
            const auto xml = engine.saveShow().createXml();
            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            const auto knobs = other.getRouting().getParameterKnobs();
            expectEquals ((int) knobs.size(), 1);
            if (! knobs.empty())
            {
                expectEquals (knobs[0].cc, 21);
                expectEquals (knobs[0].tool, (int) RigEngine::bassTool);
                expectEquals (knobs[0].parameter, cutoff);
            }
        }

        beginTest ("the bass plays its pattern on the Rig's clock, through its strip");
        {
            RigEngine engine;
            engine.prepare (kRate, 480);
            engine.vocoderOn = false;
            engine.getTransport().play();
            juce::AudioBuffer<float> out (2, 480);
            float peak = 0;
            for (int i = 0; i < 100; ++i)
            {
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
                peak = juce::jmax (peak, out.getMagnitude (0, 0, 480));
            }
            expect (peak > 0.05f, "pattern 1 plays out of the box");
            expect (engine.getStrip (RigEngine::bassStrip).peak.load() > 0.05f);
        }
    }
};

static BassTests bassTests;
static SynthToolTests synthToolTests;
static RigSynthTests rigSynthTests;
