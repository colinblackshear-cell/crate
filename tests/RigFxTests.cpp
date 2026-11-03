#include "TestHelpers.h"

#include "RigEngine.h"
#include "StripFx.h"

using namespace crate;

namespace
{
constexpr double kFxRate = 48000.0;

void setParam (juce::AudioProcessor& p, const juce::String& id, float value)
{
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (param))
            if (withId->paramID == id)
                withId->setValueNotifyingHost (withId->convertTo0to1 (value));
}

int indexOf (juce::AudioProcessor& p, const juce::String& id)
{
    const auto& list = p.getParameters();
    for (int i = 0; i < list.size(); ++i)
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (list[i]))
            if (withId->paramID == id)
                return i;
    return -1;
}

// Runs `input` through the effects in blocks of 256 and returns the result.
juce::AudioBuffer<float> run (StripFx& fx, const juce::AudioBuffer<float>& input)
{
    juce::AudioBuffer<float> out (input);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += 256)
    {
        const int n = juce::jmin (256, out.getNumSamples() - start);
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, start, n);
        fx.processBlock (block, midi);
    }
    return out;
}

juce::AudioBuffer<float> stereoSine (int samples, double hz, float level = 0.5f)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int i = 0; i < samples; ++i)
    {
        const float v = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / kFxRate);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}
}

struct RigFxTests : juce::UnitTest
{
    RigFxTests() : juce::UnitTest ("Rig strip effects", "Rig") {}

    void runTest() override
    {
        beginTest ("with everything off, the sound comes out untouched");
        {
            StripFx fx;
            fx.prepareToPlay (kFxRate, 256);
            expect (! fx.isActive());
            const auto in = stereoSine (9600, 440.0);
            const auto out = run (fx, in);
            float diff = 0;
            for (int i = 0; i < in.getNumSamples(); ++i)
                diff = juce::jmax (diff, std::abs (out.getSample (0, i) - in.getSample (0, i)));
            expectLessThan (diff, 1.0e-6f);
        }

        beginTest ("the filter knob: left closes a low-pass, right opens a high-pass");
        {
            StripFx fx;
            fx.prepareToPlay (kFxRate, 256);
            setParam (fx, "filter", -0.8f);
            expect (fx.isActive());
            const auto high = run (fx, stereoSine (24000, 6000.0));
            expectLessThan (high.getMagnitude (0, 12000, 12000), 0.02f, "6 kHz is cut by the low-pass");

            StripFx hp;
            hp.prepareToPlay (kFxRate, 256);
            setParam (hp, "filter", 0.8f);
            const auto low = run (hp, stereoSine (24000, 60.0));
            expectLessThan (low.getMagnitude (0, 12000, 12000), 0.05f, "60 Hz is cut by the high-pass");
        }

        beginTest ("the delay echoes in time with the tempo, and rings out after the send is pulled down");
        {
            StripFx fx;   // no host: 120 BPM, so a dotted 1/8 is 0.375 s
            fx.prepareToPlay (kFxRate, 256);
            setParam (fx, "delayMix", 1.0f);
            setParam (fx, "feedback", 0.5f);
            juce::AudioBuffer<float> click (2, 48000);
            click.clear();
            click.setSample (0, 0, 1.0f);
            click.setSample (1, 0, 1.0f);
            auto first = run (fx, juce::AudioBuffer<float> (click));
            const int echo = (int) (0.375 * kFxRate);
            expectGreaterThan (first.getMagnitude (0, echo - 4, 8), 0.2f, "first echo, on the left");
            expectLessThan (first.getMagnitude (0, 1000, echo - 1100), 1.0e-4f, "silence before it");
            expectGreaterThan (first.getMagnitude (1, 2 * echo - 4, 8), 0.05f, "second echo, on the right");

            setParam (fx, "delayMix", 0.0f);
            juce::AudioBuffer<float> silence (2, 48000);
            silence.clear();
            const auto later = run (fx, silence);
            expectGreaterThan (later.getMagnitude (0, 0, 48000) + later.getMagnitude (1, 0, 48000), 0.001f, "echoes keep going");
        }

        beginTest ("beat repeat loops a slice of the beat until it's let go");
        {
            StripFx fx;
            fx.prepareToPlay (kFxRate, 256);
            setParam (fx, "repeat", 3.0f);   // 1/16 at 120 BPM = 0.125 s
            const int slice = (int) (0.125 * kFxRate);
            juce::AudioBuffer<float> in (2, slice * 4);
            juce::Random r (7);
            for (int i = 0; i < in.getNumSamples(); ++i)
                in.setSample (0, i, r.nextFloat() - 0.5f), in.setSample (1, i, in.getSample (0, i));
            const auto out = run (fx, in);
            // The second and third slices are copies of the first (away from the fades at the ends).
            float diff = 0;
            for (int i = 200; i < slice - 200; ++i)
                diff = juce::jmax (diff, std::abs (out.getSample (0, slice + i) - in.getSample (0, i)),
                                   std::abs (out.getSample (0, 2 * slice + i) - in.getSample (0, i)));
            expectLessThan (diff, 1.0e-4f);

            setParam (fx, "repeat", 0.0f);
            const auto after = run (fx, in);
            float back = 0;
            for (int i = 1000; i < in.getNumSamples(); ++i)
                back = juce::jmax (back, std::abs (after.getSample (0, i) - in.getSample (0, i)));
            expectLessThan (back, 1.0e-4f, "Off goes back to the live sound");
        }

        beginTest ("Reset turns everything off");
        {
            StripFx fx;
            setParam (fx, "reverbMix", 0.7f);
            setParam (fx, "crush", 0.5f);
            expect (fx.isActive());
            fx.reset();
            expect (! fx.isActive());
        }

        beginTest ("in the Rig: a knob on the bass's filter, saved with the show");
        {
            RigEngine engine;
            engine.prepare (kFxRate, 480);
            const int tool = RigEngine::fxTool + RigEngine::bassStrip;
            expectEquals (RigEngine::toolName (tool), juce::String ("FX Bass"));
            const int filter = indexOf (engine.getTool (tool), "filter");
            expect (filter >= 0);
            engine.getRouting().addParameterKnob (1, 22, tool, filter);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 22, 0));
            juce::AudioBuffer<float> out (2, 480);
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            expectWithinAbsoluteError (engine.getFx (RigEngine::bassStrip).params.getRawParameterValue ("filter")->load(), -1.0f, 0.001f);

            setParam (engine.getFx (RigEngine::masterStrip), "reverbMix", 0.4f);
            const auto xml = engine.saveShow().createXml();
            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            expectWithinAbsoluteError (other.getFx (RigEngine::bassStrip).params.getRawParameterValue ("filter")->load(), -1.0f, 0.001f);
            expectWithinAbsoluteError (other.getFx (RigEngine::masterStrip).params.getRawParameterValue ("reverbMix")->load(), 0.4f, 0.001f);
            const auto knobs = other.getRouting().getParameterKnobs();
            expectEquals ((int) knobs.size(), 1);
            if (! knobs.empty())
                expectEquals (knobs[0].tool, tool);

            beginTest ("an old show without effects turns them off");
            juce::ValueTree old (RigEngine::kShowType);
            other.loadShow (old);
            expect (! other.getFx (RigEngine::bassStrip).isActive());
            expect (! other.getFx (RigEngine::masterStrip).isActive());
        }

        beginTest ("in the Rig: the high-pass thins out the bass");
        {
            auto loudness = [] (float filter)
            {
                RigEngine engine;
                engine.prepare (kFxRate, 480);
                engine.vocoderOn = false;
                engine.getRouting().keysTo = (int) RigRouting::Dest::bass;
                setParam (engine.getFx (RigEngine::bassStrip), "filter", filter);
                engine.addMidiNow (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 110));
                juce::AudioBuffer<float> out (2, 480);
                double level = 0;
                for (int b = 0; b < 40; ++b)
                {
                    engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
                    level += out.getRMSLevel (0, 0, 480);
                }
                return level;
            };
            const double full = loudness (0.0f), thin = loudness (0.6f);
            expectGreaterThan (full, 0.0);
            expectLessThan (thin, full * 0.5);
        }
    }
};

static RigFxTests rigFxTests;
