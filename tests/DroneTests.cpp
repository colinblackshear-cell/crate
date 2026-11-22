#include "TestHelpers.h"

#include "DroneProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

// The Drone box on its own: the tanpura's strings, the jawari's bloom, the reeds, and the
// strings that ring to what's fed in.
struct DroneTests : juce::UnitTest
{
    DroneTests() : juce::UnitTest ("Drone box", "Rig") {}

    static void setParam (DroneProcessor& d, const char* id, float value)
    {
        auto* p = d.params.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    // Renders seconds of it; `feed` (mono) is what the strings ring to.
    static juce::AudioBuffer<float> render (DroneProcessor& d, double seconds, const std::vector<float>* feed = nullptr)
    {
        const int total = (int) (seconds * 48000.0);
        juce::AudioBuffer<float> out (2, total), block (2, 480), in (2, 480);
        juce::MidiBuffer midi;
        for (int at = 0; at < total; at += 480)
        {
            const int n = juce::jmin (480, total - at);
            block.setSize (2, n, false, false, true);
            in.setSize (2, n, false, false, true);
            in.clear();
            if (feed != nullptr)
                for (int i = 0; i < n && at + i < (int) feed->size(); ++i)
                {
                    in.setSample (0, i, (*feed)[(size_t) (at + i)]);
                    in.setSample (1, i, (*feed)[(size_t) (at + i)]);
                }
            d.process (block, feed != nullptr ? &in : nullptr, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, at, block, c, 0, n);
        }
        return out;
    }

    static float rmsDb (const juce::AudioBuffer<float>& b, double from, double to)
    {
        const int a = (int) (from * 48000.0), n = juce::jmin (b.getNumSamples(), (int) (to * 48000.0)) - a;
        const float l = b.getRMSLevel (0, a, n), r = b.getRMSLevel (1, a, n);
        return juce::Decibels::gainToDecibels (std::sqrt ((l * l + r * r) * 0.5f) * 1.41421f, -120.0f);
    }

    // How much of the sound sits around `hz` (a Goertzel, +/- a few Hz), as a gain.
    static float at (const juce::AudioBuffer<float>& b, double hz, double from, double to)
    {
        const int a = (int) (from * 48000.0), n = juce::jmin (b.getNumSamples(), (int) (to * 48000.0)) - a;
        double re = 0, im = 0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / n);
            const double v = (b.getSample (0, a + i) + b.getSample (1, a + i)) * 0.5 * w;
            re += v * std::cos (juce::MathConstants<double>::twoPi * hz * i / 48000.0);
            im += v * std::sin (juce::MathConstants<double>::twoPi * hz * i / 48000.0);
        }
        return (float) (4.0 * std::sqrt (re * re + im * im) / n);
    }

    // The share of the sound above 1.5 kHz (how bright it is).
    static float brightness (const juce::AudioBuffer<float>& b, double from, double to)
    {
        const int a = (int) (from * 48000.0), n = juce::jmin (b.getNumSamples(), (int) (to * 48000.0)) - a;
        juce::dsp::IIR::Filter<float> hp;
        hp.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (48000.0, 1500.0f);
        double high = 0, all = 0;
        for (int i = 0; i < n; ++i)
        {
            const float v = b.getSample (0, a + i);
            const float h = hp.processSample (v);
            high += h * h;
            all += v * v;
        }
        return (float) std::sqrt (high / juce::jmax (1.0e-12, all));
    }

    void runTest() override
    {
        beginTest ("off it is silent; on, four strings pluck round and round at its pace");
        {
            DroneProcessor d;
            d.prepareToPlay (48000.0, 480);
            auto quiet = render (d, 1.0);
            expectEquals (quiet.getMagnitude (0, quiet.getNumSamples()), 0.0f);
            expect (! d.wantsToPlay());

            setParam (d, "droneOn", 1.0f);
            setParam (d, "dronePace", 0.5f);
            auto out = render (d, 4.1);
            for (int s = 0; s < 4; ++s)
                expectEquals (d.getPluckCount (s), s == 0 ? 3 : 2, "string " + juce::String (s));
            expect (rmsDb (out, 1.0, 4.0) > -40.0f);
            expect (d.stringNotes() == juce::StringArray ({ "G1", "C2", "C2", "C1" }), d.stringNotes().joinIntoString (" "));

            // Off: no more plucks, and it rings out.
            setParam (d, "droneOn", 0.0f);
            auto tail = render (d, 4.0);
            expectEquals (d.getPluckCount (0), 3);
            expect (rmsDb (tail, 3.5, 4.0) < -90.0f, "rung out: " + juce::String (rmsDb (tail, 3.5, 4.0)));
            expect (! d.wantsToPlay());
        }

        beginTest ("the strings are Pa, Sa, Sa and low Sa of its note; the first string can be Ma or Ni");
        {
            DroneProcessor d;
            d.prepareToPlay (48000.0, 480);
            setParam (d, "droneOn", 1.0f);
            setParam (d, "droneRoot", 2.0f);   // D
            auto out = render (d, 6.0);
            const double sa = 130.8127826502993 * std::pow (2.0, 2.0 / 12.0);
            const float pa = at (out, sa * 0.75, 1.0, 6.0), mid = at (out, sa, 1.0, 6.0), low = at (out, sa * 0.5, 1.0, 6.0);
            const float ma = at (out, sa * 2.0 / 3.0, 1.0, 6.0), off = at (out, sa * 0.84, 1.0, 6.0);
            expect (pa > off * 8.0f && mid > off * 8.0f && low > off * 8.0f, "Pa, Sa and low Sa are there: " + juce::String (pa, 5) + " " + juce::String (mid, 5) + " " + juce::String (low, 5) + " off " + juce::String (off, 5) + " ma " + juce::String (ma, 5));
            expect (ma < pa * 0.2f, "no Ma yet");
            expect (d.stringNotes() == juce::StringArray ({ "A1", "D2", "D2", "D1" }));

            setParam (d, "droneFirst", 1.0f);
            render (d, 6.0);
            out = render (d, 6.0);
            expect (at (out, sa * 2.0 / 3.0, 1.0, 6.0) > at (out, sa * 0.75, 1.0, 6.0) * 4.0f, "now Ma, not Pa");
            expect (d.stringNotes()[0] == "G1");
        }

        beginTest ("jawari: the buzz makes it brighter, and the brightness falls after each pluck");
        {
            auto brightAt = [this] (float jawari, double from, double to)
            {
                DroneProcessor d;
                d.prepareToPlay (48000.0, 480);
                setParam (d, "droneJawari", jawari);
                setParam (d, "dronePace", 4.0f);
                d.pluck (1);
                // (Not on: one pluck by hand.)
                setParam (d, "droneResonate", 0.0f);
                juce::AudioBuffer<float> out = render (d, 3.0);
                return brightness (out, from, to);
            };
            const float plain = brightAt (0.0f, 0.05, 1.5), full = brightAt (1.0f, 0.05, 1.5);
            expect (full > plain * 3.0f, "plain " + juce::String (plain) + " full " + juce::String (full));
            const float early = brightAt (1.0f, 0.05, 0.35), late = brightAt (1.0f, 1.6, 2.4);
            expect (early > late * 1.5f, "early " + juce::String (early) + " late " + juce::String (late));
        }

        beginTest ("it sits 6 dB under the other tools' sounds (a bed), and never over the ceiling");
        {
            for (int preset = 0; preset < DroneProcessor::presetNames().size(); ++preset)
            {
                DroneProcessor d;
                d.prepareToPlay (48000.0, 480);
                d.loadPreset (preset);
                setParam (d, "droneOn", 1.0f);
                auto out = render (d, 14.0);
                const float level = rmsDb (out, 6.0, 14.0);
                logMessage ("DRONE|" + DroneProcessor::presetNames()[preset] + "|" + juce::String (level, 1) + "|" + juce::String (juce::Decibels::gainToDecibels (out.getMagnitude (0, out.getNumSamples())), 1));
                expect (std::abs (level - -24.0f) <= 2.0f, DroneProcessor::presetNames()[preset] + ": " + juce::String (level, 1) + " dB");
                expect (out.getMagnitude (0, out.getNumSamples()) <= juce::Decibels::decibelsToGain (crate::SafetyLimiter::kCeilingDb) + 1.0e-4f);
            }
        }

        beginTest ("every instrument and every pluck order sits as loud as the tanpura's Classic drone");
        {
            auto loudness = [this] (const char* id, float value)
            {
                DroneProcessor d;
                d.prepareToPlay (48000.0, 480);
                d.loadPreset (0);
                setParam (d, id, value);
                setParam (d, "droneOn", 1.0f);
                return rmsDb (render (d, 14.0), 6.0, 14.0);
            };
            const float classic = loudness ("droneVoice", 0.0f);
            for (int v = 1; v < crate::drone::numVoices; ++v)
            {
                const float level = loudness ("droneVoice", (float) v);
                logMessage ("VOICE|" + juce::String (crate::drone::voiceName (v)) + "|" + juce::String (level, 1));
                expect (std::abs (level - classic) <= 3.0f, juce::String (crate::drone::voiceName (v)) + ": " + juce::String (level, 1));
            }
            for (int o = 1; o < crate::drone::numOrders; ++o)
            {
                const float level = loudness ("droneOrder", (float) o);
                expect (std::abs (level - classic) <= 3.0f, juce::String (crate::drone::orderName (o)) + ": " + juce::String (level, 1));
            }
            for (const char* id : { "droneBright", "droneBody", "droneSpace", "droneHuman", "droneDetune" })
                for (float value : { 0.0f, 1.0f })
                {
                    const float level = loudness (id, value);
                    expect (std::abs (level - classic) <= 3.0f, juce::String (id) + " " + juce::String (value) + ": " + juce::String (level, 1));
                }
        }

        beginTest ("pluck orders: Reverse starts on the low Sa, Pairs plucks two at once, Roll all four");
        {
            auto firstPlucks = [this] (int order)
            {
                DroneProcessor d;
                d.prepareToPlay (48000.0, 480);
                setParam (d, "droneOrder", (float) order);
                setParam (d, "droneOn", 1.0f);
                render (d, 0.5);
                int mask = 0;
                for (int s = 0; s < crate::drone::kStrings; ++s)
                    mask |= d.getPluckCount (s) > 0 ? 1 << s : 0;
                return mask;
            };
            expectEquals (firstPlucks (crate::drone::orderClassic), 0x1);
            expectEquals (firstPlucks (crate::drone::orderReverse), 0x8);
            expectEquals (firstPlucks (crate::drone::orderPairs), 0x9);
            expectEquals (firstPlucks (crate::drone::orderRoll), 0xf);
        }

        beginTest ("steps: the strings lit on each step pluck on it, in time with the clock; rests stay quiet; saved with the drone");
        {
            struct Head : juce::AudioPlayHead
            {
                double ppq = 0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setIsPlaying (true);
                    info.setPpqPosition (ppq);
                    info.setBpm (120.0);
                    return info;
                }
            } head;
            DroneProcessor d;
            d.prepareToPlay (48000.0, 480);
            d.setPlayHead (&head);
            setParam (d, "droneOn", 1.0f);
            setParam (d, "droneStepCount", 1.0f);   // 8 steps
            setParam (d, "droneStepRate", 1.0f);    // of an 1/8 each: 4 beats round
            d.setStep (0, 0x1);
            d.setStep (2, 0x6);
            d.setStep (5, 0x8);
            juce::AudioBuffer<float> block (2, 480);
            juce::MidiBuffer midi;
            for (int b = 0; b < 400; ++b)   // 4 s = 8 beats = twice round
            {
                head.ppq = b * 480 * 120.0 / (60.0 * 48000.0);
                d.process (block, nullptr, midi);
            }
            for (int s = 0; s < 4; ++s)
                expectEquals (d.getPluckCount (s), 2, "string " + juce::String (s));
            expect (juce::isPositiveAndBelow (d.getCurrentStep(), 8));
            d.setPlayHead (nullptr);

            juce::MemoryBlock state;
            d.getStateInformation (state);
            DroneProcessor other;
            other.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (other.getStep (0), 0x1);
            expectEquals (other.getStep (2), 0x6);
            expectEquals (other.getStep (5), 0x8);
            expectEquals (other.getStep (1), 0);
            expectEquals (juce::roundToInt (other.params.getParameter ("droneStepCount")->getValue() * 2.0f), 1);
        }

        beginTest ("resonate: a note of the drone fed in rings the strings on after it stops; a note between them barely does");
        {
            auto ringAfter = [this] (double hz)
            {
                DroneProcessor d;
                d.prepareToPlay (48000.0, 480);
                setParam (d, "droneResonate", 0.8f);
                std::vector<float> feed ((size_t) 48000, 0.0f);
                for (size_t i = 0; i < feed.size(); ++i)
                    feed[i] = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / 48000.0);
                expect (d.wantsToPlay());
                auto out = render (d, 2.5, &feed);
                return std::pair<float, float> (rmsDb (out, 0.5, 1.0), rmsDb (out, 1.2, 1.7));
            };
            const double sa = 130.8127826502993;
            const auto tuned = ringAfter (sa), between = ringAfter (sa * 1.19);
            logMessage ("RESONATE|tuned " + juce::String (tuned.first, 1) + " then " + juce::String (tuned.second, 1) + "|between " + juce::String (between.first, 1) + " then " + juce::String (between.second, 1));
            expect (tuned.second > -45.0f, "still ringing: " + juce::String (tuned.second));
            expect (tuned.second > between.second + 12.0f);
            expect (tuned.first < -6.0f && tuned.first > -40.0f, "a sensible level while fed: " + juce::String (tuned.first));
        }

        beginTest ("pluck every beat: on the beats of the clock");
        {
            struct Head : juce::AudioPlayHead
            {
                double ppq = 0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setIsPlaying (true);
                    info.setPpqPosition (ppq);
                    info.setBpm (120.0);
                    return info;
                }
            } head;
            DroneProcessor d;
            d.prepareToPlay (48000.0, 480);
            d.setPlayHead (&head);
            setParam (d, "droneOn", 1.0f);
            setParam (d, "droneSync", 1.0f);
            juce::AudioBuffer<float> block (2, 480);
            juce::MidiBuffer midi;
            int plucks = 0;
            for (int b = 0; b < 400; ++b)   // 4 s = 8 beats
            {
                head.ppq = b * 480 * 120.0 / (60.0 * 48000.0);
                d.process (block, nullptr, midi);
            }
            for (int s = 0; s < 4; ++s)
                plucks += d.getPluckCount (s);
            expectEquals (plucks, 8);
            d.setPlayHead (nullptr);
        }

        beginTest ("follow keys: the last note played is its note; saved and loaded with everything else");
        {
            DroneProcessor d;
            d.prepareToPlay (48000.0, 480);
            setParam (d, "droneFollow", 1.0f);
            juce::AudioBuffer<float> block (2, 480);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 57, 0.8f), 0);   // A
            d.process (block, nullptr, midi);
            expectEquals (d.params.getParameter ("droneRoot")->getCurrentValueAsText(), juce::String ("A"));

            setParam (d, "droneJawari", 0.25f);
            juce::MemoryBlock state;
            d.getStateInformation (state);
            DroneProcessor other;
            other.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (other.params.getParameter ("droneRoot")->getCurrentValueAsText(), juce::String ("A"));
            expectWithinAbsoluteError (other.params.getRawParameterValue ("droneJawari")->load(), 0.25f, 1.0e-4f);
        }

        if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_DRONE_WAV", {}); path.isNotEmpty())
        {
            DroneProcessor d;
            d.prepareToPlay (48000.0, 480);
            setParam (d, "droneOn", 1.0f);
            auto out = render (d, 12.0);
            juce::File file (path);
            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (file), 48000.0, 2, 16, {}, 0)))
                writer->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        }
    }
};

static DroneTests droneTests;
