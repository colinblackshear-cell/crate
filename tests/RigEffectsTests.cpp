#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "EffectsView.h"
#include "Pedalboard.h"
#include "SynthScreens.h"

// The effects processor: the new pedals in StripFx, the pedalboard model (on/off, presets,
// copy and paste, the tuner), and the Effects screen (any sound's board, the Line in, recording).
namespace
{
constexpr double kRate = 48000.0;

juce::AudioBuffer<float> runFx (StripFx& fx, juce::AudioBuffer<float> out)
{
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += 256)
    {
        const int n = juce::jmin (256, out.getNumSamples() - start);
        juce::AudioBuffer<float> block (out.getArrayOfWritePointers(), 2, start, n);
        fx.processBlock (block, midi);
    }
    return out;
}

juce::AudioBuffer<float> sine (int samples, double hz, float level = 0.5f)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int i = 0; i < samples; ++i)
    {
        const float v = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / kRate);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

float rms (const juce::AudioBuffer<float>& b, int from)
{
    return b.getRMSLevel (0, from, b.getNumSamples() - from);
}

// With CRATE_EFFECTS_SNAPSHOT set (a path without .png), saves the screen as it is now.
void snapshot (juce::Component& c, const char* suffix)
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_EFFECTS_SNAPSHOT", {});
    if (path.isEmpty())
        return;
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    juce::File file (path + suffix + ".png");
    file.deleteFile();
    juce::FileOutputStream stream (file);
    juce::PNGImageFormat().writeImageToStream (image, stream);
}

template <typename T>
T* findChild (juce::Component& c)
{
    if (auto* t = dynamic_cast<T*> (&c))
        return t;
    for (auto* child : c.getChildren())
        if (auto* found = findChild<T> (*child))
            return found;
    return nullptr;
}

juce::Button* findButton (juce::Component& c, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getButtonText() == text && b->isVisible())
        return b;
    for (auto* child : c.getChildren())
        if (child->isVisible())
            if (auto* found = findButton (*child, text))
            return found;
    return nullptr;
}

float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float d = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = juce::jmax (d, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return d;
}
}

struct RigEffectsTests : juce::UnitTest
{
    RigEffectsTests() : juce::UnitTest ("Rig effects processor", "Rig") {}

    void runTest() override
    {
        beginTest ("the first pedals keep their parameter numbers, so learned knobs and shows keep working");
        {
            StripFx fx;
            const char* first[] = { "filter", "resonance", "drive", "crush", "delayTime", "feedback", "delayMix", "reverbSize", "reverbMix", "repeat" };
            const auto& list = fx.getParameters();
            expect (list.size() > 10);
            for (int i = 0; i < 10; ++i)
                if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*> (list[i]))
                    expectEquals (p->paramID, juce::String (first[i]));
        }

        beginTest ("every new pedal is off to start with, and an untouched rack leaves the sound alone");
        {
            StripFx fx;
            fx.prepareToPlay (kRate, 256);
            expect (! fx.isActive());
            for (int i = 0; i < pedalboard::numPedals; ++i)
                expect (! pedalboard::isOn (fx, i), pedalboard::pedals()[(size_t) i].name);
            const auto in = sine (9600, 330.0);
            expectLessThan (maxDiff (runFx (fx, in), in), 1.0e-6f);
        }

        beginTest ("each pedal changes the sound when it's on, and stays finite");
        {
            for (int pedal = 0; pedal < pedalboard::numPedals; ++pedal)
            {
                StripFx fx;
                fx.prepareToPlay (kRate, 256);
                pedalboard::setOn (fx, pedal, true);
                if (pedal == pedalboard::gate)
                    pedalboard::set (fx, "gateThreshold", -20);   // shuts as the note dies away
                const auto& info = pedalboard::pedals()[(size_t) pedal];
                expect (pedalboard::isOn (fx, pedal), info.name);
                expect (fx.isActive(), info.name);
                // A plucked note: loud, then dying away (the gate, comp and wah react to that).
                juce::AudioBuffer<float> in (2, 48000);
                for (int i = 0; i < in.getNumSamples(); ++i)
                {
                    const float v = 0.6f * std::exp (-3.0f * (float) i / (float) kRate) * (float) std::sin (juce::MathConstants<double>::twoPi * 196.0 * i / kRate);
                    in.setSample (0, i, v);
                    in.setSample (1, i, v);
                }
                const auto out = runFx (fx, in);
                bool finite = true;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < out.getNumSamples(); ++i)
                        finite = finite && std::isfinite (out.getSample (ch, i)) && std::abs (out.getSample (ch, i)) < 8.0f;
                expect (finite, juce::String ("finite: ") + info.name);
                expectGreaterThan (maxDiff (out, in), 0.01f, juce::String ("changes the sound: ") + info.name);

                pedalboard::setOn (fx, pedal, false);
                expect (! pedalboard::isOn (fx, pedal), info.name);
            }
        }

        beginTest ("the gate shuts out quiet hum and lets notes through");
        {
            StripFx fx;
            fx.prepareToPlay (kRate, 256);
            pedalboard::set (fx, "gateOn", 1);
            pedalboard::set (fx, "gateThreshold", -40);
            const auto hum = runFx (fx, sine (24000, 60.0, 0.003f));   // about -50 dB
            expectLessThan (rms (hum, 12000), 0.0002f, "hum is shut out");
            const auto note = runFx (fx, sine (24000, 220.0, 0.3f));
            expectGreaterThan (rms (note, 12000), 0.18f, "a note gets through");
        }

        beginTest ("the comp evens out loud and quiet notes");
        {
            auto level = [] (float in, bool on)
            {
                StripFx fx;
                fx.prepareToPlay (kRate, 256);
                pedalboard::set (fx, "compOn", on ? 1.0f : 0.0f);
                pedalboard::set (fx, "compAmount", 0.8f);
                return rms (runFx (fx, sine (24000, 220.0, in)), 12000);
            };
            const float dry = level (0.8f, false) / level (0.1f, false);
            const float squeezed = level (0.8f, true) / level (0.1f, true);
            expectGreaterThan (dry, 7.0f);
            expectLessThan (squeezed, dry * 0.5f, "the loud note is brought down towards the quiet one");
        }

        beginTest ("the EQ's bass knob lifts the lows and leaves the highs");
        {
            StripFx fx;
            fx.prepareToPlay (kRate, 256);
            pedalboard::set (fx, "eqOn", 1);
            pedalboard::set (fx, "eqLow", 12);
            expectGreaterThan (rms (runFx (fx, sine (24000, 50.0, 0.1f)), 12000) / sine (24000, 50.0, 0.1f).getRMSLevel (0, 12000, 12000), 3.0f);
            StripFx again;
            again.prepareToPlay (kRate, 256);
            pedalboard::set (again, "eqOn", 1);
            pedalboard::set (again, "eqLow", 12);
            const float high = rms (runFx (again, sine (24000, 8000.0, 0.1f)), 12000) / sine (24000, 8000.0, 0.1f).getRMSLevel (0, 12000, 12000);
            expectWithinAbsoluteError (high, 1.0f, 0.15f);
        }

        beginTest ("the tremolo pulses in time: four dips a beat at 1/16");
        {
            StripFx fx;
            fx.prepareToPlay (kRate, 256);
            pedalboard::set (fx, "tremOn", 1);
            pedalboard::set (fx, "tremDepth", 1.0f);
            pedalboard::set (fx, "tremRate", 3);   // 1/16: 8 a second at 120 BPM
            juce::AudioBuffer<float> dc (2, 48000);
            for (int ch = 0; ch < 2; ++ch)
                juce::FloatVectorOperations::fill (dc.getWritePointer (ch), 0.5f, 48000);
            const auto out = runFx (fx, dc);
            int dips = 0;
            bool low = false;
            for (int i = 4800; i < 48000; ++i)
            {
                const float v = out.getSample (0, i);
                if (! low && v < 0.05f)
                {
                    ++dips;
                    low = true;
                }
                else if (low && v > 0.4f)
                    low = false;
            }
            expect (dips >= 6 && dips <= 8, juce::String (dips) + " dips in 0.9 s");
        }

        beginTest ("Gain in brings a guitar up, and Bypass takes the pedals out");
        {
            StripFx fx;
            fx.prepareToPlay (kRate, 256);
            pedalboard::set (fx, "inputGain", 12);
            const auto louder = runFx (fx, sine (24000, 110.0, 0.05f));
            expectWithinAbsoluteError (rms (louder, 6000) / (0.05f / std::sqrt (2.0f)), 3.98f, 0.1f);
            pedalboard::set (fx, "inputGain", 0);
            pedalboard::set (fx, "ampOn", 1);
            pedalboard::set (fx, "ampGain", 1.0f);
            expect (fx.isActive());
            pedalboard::set (fx, "bypass", 1);
            expect (! fx.isActive(), "bypassed");
            StripFx fresh;   // (the gain had a moment to settle)
            fresh.prepareToPlay (kRate, 256);
            pedalboard::set (fresh, "ampOn", 1);
            pedalboard::set (fresh, "bypass", 1);
            const auto in = sine (9600, 110.0);
            expectLessThan (maxDiff (runFx (fresh, in), in), 1.0e-6f, "bypassed is dry");
        }

        beginTest ("an amount pedal comes back as it was when it's stomped on again");
        {
            StripFx fx;
            float kept[2] = { 0, 0 };
            pedalboard::setOn (fx, pedalboard::delay, true, kept);
            expectWithinAbsoluteError (pedalboard::get (fx, "delayMix"), 0.3f, 0.001f, "a good starting amount");
            pedalboard::set (fx, "delayMix", 0.7f);
            pedalboard::setOn (fx, pedalboard::delay, false, kept);
            expectEquals (pedalboard::get (fx, "delayMix"), 0.0f);
            pedalboard::setOn (fx, pedalboard::delay, true, kept);
            expectWithinAbsoluteError (pedalboard::get (fx, "delayMix"), 0.7f, 0.001f);
        }

        beginTest ("presets, copy and paste, and saved boards");
        {
            StripFx fx;
            pedalboard::set (fx, "inputGain", 9);
            pedalboard::set (fx, "phaserOn", 1);
            const auto& list = pedalboard::presets();
            expect (list.size() >= 12);
            int crunch = -1;
            for (int i = 0; i < (int) list.size(); ++i)
            {
                // Every preset's settings are real settings.
                for (const auto& [id, value] : list[(size_t) i].values)
                    expect (fx.params.getParameter (id) != nullptr, juce::String (list[(size_t) i].name) + ": " + id);
                if (juce::String (list[(size_t) i].name) == "Crunch")
                    crunch = i;
            }
            expect (crunch >= 0);
            pedalboard::applyPreset (fx, list[(size_t) crunch]);
            expect (pedalboard::isOn (fx, pedalboard::amp) && pedalboard::isOn (fx, pedalboard::gate));
            expect (! pedalboard::isOn (fx, pedalboard::phaser), "the board starts fresh");
            expectWithinAbsoluteError (pedalboard::get (fx, "inputGain"), 9.0f, 0.01f, "the line's level stays");

            StripFx other;
            pedalboard::pasteSettings (other, pedalboard::copySettings (fx));
            expect (pedalboard::isOn (other, pedalboard::amp));
            expectWithinAbsoluteError (pedalboard::get (other, "ampGain"), 0.55f, 0.001f);
            pedalboard::set (other, "ampGain", 0.9f);
            expectWithinAbsoluteError (pedalboard::get (fx, "ampGain"), 0.55f, 0.001f, "each sound has its own settings");

            const auto file = juce::File::createTempFile (".xml");
            expect (pedalboard::savePreset (other, file));
            StripFx third;
            expect (pedalboard::loadPreset (third, file));
            expectWithinAbsoluteError (pedalboard::get (third, "ampGain"), 0.9f, 0.001f);
            file.deleteFile();
        }

        beginTest ("the tuner hears a guitar's low E and a bass's low E");
        {
            for (double hz : { 82.41, 41.2, 110.0, 329.63 })
            {
                std::vector<float> s (4096);
                for (size_t i = 0; i < s.size(); ++i)
                    s[i] = 0.4f * (float) (std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / kRate)
                                           + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 2 * hz * (double) i / kRate));
                const float found = pedalboard::detectPitch (s.data(), (int) s.size(), kRate);
                expectWithinAbsoluteError (found, (float) hz, (float) hz * 0.01f, juce::String (hz));
            }
            const auto e = pedalboard::nearestNote (82.41f);
            expectEquals (e.name, juce::String ("E"));
            expectEquals (e.octave, 2);
            expectLessThan (std::abs (e.cents), 2.0f);
            const auto sharp = pedalboard::nearestNote (440.0f * std::pow (2.0f, 20.0f / 1200.0f));
            expectEquals (sharp.name, juce::String ("A"));
            expectWithinAbsoluteError (sharp.cents, 20.0f, 0.5f);
            std::vector<float> quiet (4096, 0.0f);
            expectEquals (pedalboard::detectPitch (quiet.data(), 4096, kRate), 0.0f, "silence has no note");
        }

        beginTest ("in the Rig: a guitar on the Line in, through its pedals, onto a loop");
        {
            RigEngine engine;
            engine.prepare (kRate, 480);
            engine.vocoderOn = false;
            vintage::LookAndFeel look;
            EffectsView view (engine);
            view.setLookAndFeel (&look);
            view.setSize (1440, 760);
            expectEquals (view.getSource(), (int) EffectsView::lineIn);
            expect (&view.rack() == &engine.getFx (RigEngine::inputStrip));

            view.pressPlugIn();
            expect (engine.inputOn.load() && engine.micOn.load(), "Plug in switches the Line in on");
            view.pickPreset (1);   // Crunch
            expect (view.isOn (pedalboard::amp));
            view.stomp (pedalboard::delay);
            expect (view.isOn (pedalboard::delay));
            expectEquals (view.getSelectedPedal(), (int) pedalboard::delay);
            expectEquals (view.numRackKnobs(), 3);

            // Rec to looper: the next empty layer records the Line in.
            view.pressRecToLooper();
            expect (view.getLooperLayer() == 0);
            expectEquals (engine.getLooper().getLayer (0).source.load(), (int) RigLooper::line);

            // The guitar comes out changed by the amp (louder and clipped, not just as it went in).
            const auto guitar = sine (480, 110.0, 0.1f);
            juce::AudioBuffer<float> out (2, 480);
            const float* in[] = { guitar.getReadPointer (0), guitar.getReadPointer (1) };
            float peak = 0;
            for (int b = 0; b < 40; ++b)
            {
                engine.process (in, 2, out.getArrayOfWritePointers(), 2, 480);
                peak = juce::jmax (peak, out.getMagnitude (0, 0, 480));
            }
            expectGreaterThan (peak, 0.05f, "the guitar is heard");
            view.selectPedal (pedalboard::amp);
            snapshot (view, "-line-in");
            view.setLookAndFeel (nullptr);
        }

        beginTest ("every sound has its own board: the bass's delay is not the synth's");
        {
            RigEngine engine;
            engine.prepare (kRate, 480);
            vintage::LookAndFeel look;
            EffectsView view (engine);
            view.setLookAndFeel (&look);
            view.setSize (1440, 760);
            view.showStrip (RigEngine::bassStrip);
            expectEquals (view.getSource(), (int) EffectsView::bassSource);
            view.stomp (pedalboard::reverb);
            view.stomp (pedalboard::chorus);
            expect (engine.getFx (RigEngine::bassStrip).isActive());
            expect (! engine.getFx (RigEngine::synthStrip).isActive());
            view.pressCopy();
            view.showLoop (2);
            expect (&view.rack() == &engine.getLoopFx (2));
            {
                view.showStrip (RigEngine::bassStrip);
                view.pickPreset (7);   // Fat bass amp
                view.stomp (pedalboard::delay);
                snapshot (view, "-bass");
                view.pressResetAll();
                view.stomp (pedalboard::reverb);
                view.stomp (pedalboard::chorus);
                view.pressCopy();
                view.showLoop (2);
            }
            view.pressPaste();
            expect (view.isOn (pedalboard::chorus), "pasted onto loop 3");
            view.stomp (pedalboard::chorus);
            expect (pedalboard::isOn (engine.getFx (RigEngine::bassStrip), pedalboard::chorus), "the bass keeps its own");

            // The show keeps every board.
            pedalboard::set (engine.getLoopFx (2), "phaserOn", 1);
            const auto xml = engine.saveShow().createXml();
            RigEngine other;
            other.loadShow (juce::ValueTree::fromXml (*xml));
            expect (pedalboard::isOn (other.getFx (RigEngine::bassStrip), pedalboard::chorus));
            expect (pedalboard::isOn (other.getLoopFx (2), pedalboard::phaser));

            view.pressBypass();
            expect (! engine.getLoopFx (2).isActive());
            view.pressBypass();
            expect (engine.getLoopFx (2).isActive());
            view.pressResetAll();
            expect (! engine.getLoopFx (2).isActive());
            view.setLookAndFeel (nullptr);
        }

        beginTest ("in the window: F6 is Effects, and the Bass screen's Effects button opens its board");
        {
            RigEngine engine;
            engine.prepare (kRate, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 900);
            view.goTo ("Effects");
            expectEquals (view.getPage(), juce::String ("Effects"));
            view.goTo ("Groove");
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F6Key)));
            expectEquals (view.getPage(), juce::String ("Effects"));
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F5Key)));
            expectEquals (view.getPage(), juce::String ("Stage"), "F5 is still the Stage");

            view.goTo ("Bass");
            auto* bassScreen = findChild<BassScreen> (view);
            auto* button = bassScreen != nullptr ? findButton (*bassScreen, "Effects") : nullptr;
            expect (button != nullptr, "the Bass screen has an Effects button");
            if (button != nullptr)
            {
                snapshot (view, "-bass-screen");
                button->onClick();
                expectEquals (view.getPage(), juce::String ("Effects"));
                expect (findButton (view, "Open Bass") != nullptr, "on the Bass's board");

            }

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_EFFECTS_SNAPSHOT", {}); path.isNotEmpty())
            {
                for (auto [w, h, suffix] : { std::tuple<int, int, const char*> { 1440, 900, "" }, { 1180, 820, "-narrow" } })
                {
                    view.setSize (w, h);
                    view.goTo ("Effects");
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File file (path + suffix + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
        }
    }
};

static RigEffectsTests rigEffectsTests;
