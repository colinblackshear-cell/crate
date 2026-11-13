#include "TestHelpers.h"

#include "DrumProcessor.h"
#include "DrumView.h"
#include "RigEngine.h"
#include "GrooveView.h"

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// Renders `seconds` of the engine, stopped (only what's triggered), and returns the peak and
// the energy in the last tenth.
struct Heard { float peak = 0, tail = 0; bool finite = true; };
Heard listen (drums::Engine& e, double seconds, const drums::Clock& clock = {})
{
    Heard h;
    const int total = (int) (seconds * kRate);
    juce::AudioBuffer<float> b (2, kBlock);
    juce::MidiBuffer none;
    auto c = clock;
    for (int done = 0; done < total; done += kBlock)
    {
        b.clear();
        e.render (b.getWritePointer (0), b.getWritePointer (1), kBlock, none, c);
        if (c.playing)
            c.ppq += kBlock * c.bpm / (60.0 * kRate);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                const float x = b.getSample (ch, i);
                h.finite = h.finite && std::isfinite (x);
                h.peak = std::max (h.peak, std::abs (x));
                if (done > total * 9 / 10)
                    h.tail += x * x;
            }
    }
    return h;
}

void runBlocks (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        engine.tickScenes();
    }
}

void setParam (juce::AudioProcessorValueTreeState& p, const juce::String& id, float value)
{
    auto* param = p.getParameter (id);
    param->setValueNotifyingHost (param->convertTo0to1 (value));
}
}

struct DrumMachineTests : juce::UnitTest
{
    DrumMachineTests() : juce::UnitTest ("Drum machine", "Drums") {}

    void runTest() override
    {
        beginTest ("every sound makes a sound, stays in range and dies away");
        {
            for (int v = 0; v < drums::kVoices; ++v)
            {
                drums::Engine e;
                e.prepare (kRate);
                e.trigger (v, 1.0f, true);
                const auto h = listen (e, 4.0);
                expect (h.finite, drums::voiceName (v));
                expectGreaterThan (h.peak, 0.05f, drums::voiceName (v));
                expectLessThan (h.peak, 2.0f, drums::voiceName (v));
                expectLessThan (h.tail, 1.0e-3f, juce::String (drums::voiceName (v)) + " fades away");
            }
        }

        beginTest ("decay makes the kick boom longer; level and accent make it louder");
        {
            auto kick = [] (float decay, float level, bool accent)
            {
                drums::Engine e;
                e.prepare (kRate);
                drums::Settings s;
                s.voices[drums::kick].decay = decay;
                s.voices[drums::kick].level = level;
                e.setSettings (s);
                e.trigger (drums::kick, 0.7f, accent);
                return listen (e, 0.8);
            };
            expectGreaterThan (kick (1.0f, 0.8f, false).tail, kick (0.0f, 0.8f, false).tail * 10.0f + 1.0e-9f);
            expectGreaterThan (kick (0.5f, 0.8f, false).peak, kick (0.5f, 0.3f, false).peak);
            expectGreaterThan (kick (0.5f, 0.5f, true).peak, kick (0.5f, 0.5f, false).peak);
        }

        beginTest ("the notes: C plays the kick, in any octave, and back");
        {
            for (int v = 0; v < drums::kVoices; ++v)
            {
                expectEquals (drums::voiceForNote (drums::noteForVoice (v)), v);
                expectEquals (drums::voiceForNote (drums::noteForVoice (v) + 24), v);
            }
            expectEquals (drums::noteForVoice (drums::kick), 36);
            expectEquals (drums::noteForVoice (drums::snare), 38);
            expectEquals (drums::noteForVoice (drums::closedHat), 42);
            expectEquals (drums::noteForVoice (drums::openHat), 46);
        }

        beginTest ("the pattern plays in time with the clock, with rolls");
        {
            drums::Engine e;
            e.prepare (kRate);
            drums::Pattern p;
            p.hits[drums::kick][0] = 1;
            p.hits[drums::kick][8] = 1;
            p.hits[drums::snare][4] = 3;   // a roll of three
            e.setPatterns (&p, nullptr);
            drums::Clock clock;
            clock.playing = true;
            clock.bpm = 120;
            listen (e, 1.0, clock);   // one bar of 4/4 at 120: 2 seconds, so half a bar
            expectEquals (e.hitCounts[drums::kick].load(), 1, "the second kick (step 9) is not due yet");
            expectEquals (e.hitCounts[drums::snare].load(), 3, "the roll's three hits");
            drums::Clock later = clock;
            later.ppq = 2.0;
            listen (e, 1.0, later);
            expectEquals (e.hitCounts[drums::kick].load(), 2);
        }

        beginTest ("swing pushes the second step late");
        {
            auto firstHatBeat = [] (float swing)
            {
                drums::Engine e;
                e.prepare (kRate);
                drums::Settings s;
                s.swing = swing;
                e.setSettings (s);
                drums::Pattern p;
                p.hits[drums::closedHat][1] = 1;
                e.setPatterns (&p, nullptr);
                drums::Clock clock;
                clock.playing = true;
                juce::AudioBuffer<float> b (2, 1);
                juce::MidiBuffer none;
                for (int i = 0; i < (int) kRate; ++i)
                {
                    e.render (b.getWritePointer (0), b.getWritePointer (1), 1, none, clock);
                    if (e.hitCounts[drums::closedHat].load() > 0)
                        return clock.ppq;
                    clock.ppq += clock.bpm / (60.0 * kRate);
                }
                return -1.0;
            };
            expectWithinAbsoluteError (firstHatBeat (0.0f), 0.25, 0.001);
            expectGreaterThan (firstHatBeat (1.0f), 0.36);
        }

        beginTest ("the closed hat cuts the open hat off");
        {
            drums::Engine a, b;
            for (auto* e : { &a, &b })
            {
                e->prepare (kRate);
                drums::Settings s;
                s.voices[drums::openHat].decay = 1.0f;
                e->setSettings (s);
                e->trigger (drums::openHat, 1.0f, false);
                listen (*e, 0.05);
            }
            b.trigger (drums::closedHat, 0.01f, false);
            const auto ringing = listen (a, 0.4), cut = listen (b, 0.4);
            expectLessThan (cut.tail, ringing.tail * 0.01f);
        }

        beginTest ("the fill comes in every few times round, and once when pressed");
        {
            drums::Engine e;
            e.prepare (kRate);
            drums::Settings s;
            s.fillEvery = 2;
            e.setSettings (s);
            drums::Pattern main, fill;
            main.length = fill.length = 4;
            main.hits[drums::kick][0] = 1;
            fill.hits[drums::snare][0] = 1;
            e.setPatterns (&main, &fill);
            drums::Clock clock;
            clock.playing = true;
            listen (e, 2.0, clock);   // 4 beats = 4 times round a 4-step pattern
            expectEquals (e.hitCounts[drums::kick].load(), 2);
            expectEquals (e.hitCounts[drums::snare].load(), 2, "every 2nd time");

            drums::Engine once;
            once.prepare (kRate);
            once.setPatterns (&main, &fill);
            once.requestFill();
            listen (once, 2.0, clock);
            expectEquals (once.hitCounts[drums::snare].load(), 1, "just once");
            expectEquals (once.hitCounts[drums::kick].load(), 3);
        }

        beginTest ("patterns and settings are saved with the project");
        {
            DrumProcessor a;
            auto p = DrumProcessor::groove (2);
            p.length = 24;
            p.hits[drums::cowbell][20] = 4;
            a.setPattern (5, p);
            a.selectPattern (5);
            setParam (a.params, "kickDecay", 0.9f);
            setParam (a.params, "swing", 0.4f);
            juce::MemoryBlock state;
            a.getStateInformation (state);

            DrumProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            expect (b.getPattern (5) == p);
            expectEquals (b.getSelectedPattern(), 5);
            expectWithinAbsoluteError (b.params.getRawParameterValue ("kickDecay")->load(), 0.9f, 0.001f);
            expectWithinAbsoluteError (b.params.getRawParameterValue ("swing")->load(), 0.4f, 0.001f);
            expect (! DrumProcessor::groove (0).isEmpty(), "pattern 1 is a beat out of the box");
            juce::Random random (7);
            expect (! DrumProcessor::randomPattern (random).isEmpty());
        }

        beginTest ("in the Rig: its own track, strip, pads, patterns in song parts, and saved in the show");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            auto& d = engine.getDrums();
            expect (d.params.getRawParameterValue ("sequencer")->load() < 0.5f, "a new Rig starts quiet");

            GrooveView view (engine);
            view.setSize (1400, 740);
            view.selectTrack (GrooveView::machine);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::drums, "the keys play it");
            expectEquals (engine.getRouting().padsTo.load(), (int) RigRouting::Dest::drums, "so do the pads");
            expectEquals (GrooveView::stripFor (GrooveView::machine), (int) RigEngine::drumsStrip);
            expectEquals (GrooveView::tabFor (GrooveView::machine), juce::String ("Drum machine"));

            // The pads play its sounds, through its own strip.
            engine.addMidiNow (juce::MidiMessage::noteOn (10, 36, (juce::uint8) 120));
            const int before = d.getHitCount (drums::kick);
            runBlocks (engine, 2);
            expectEquals (d.getHitCount (drums::kick), before + 1, "pad 1 is the kick");
            expectGreaterThan (engine.getStrip (RigEngine::drumsStrip).peak.load(), 0.01f);

            // A step lit on the Groove screen plays the pattern.
            d.setPattern (0, {});
            view.pressCell (1 + drums::kick, 0);   // (row 0 is the accent, as on its page)
            expectEquals ((int) d.getPattern (0).hits[drums::kick][0], 1);
            expect (d.params.getRawParameterValue ("sequencer")->load() > 0.5f);
            view.pressRoll (1 + drums::snare, 4);
            expectEquals ((int) d.getPattern (0).hits[drums::snare][4], 2, "right-click: a roll");
            view.pressPattern (2);
            expectEquals (d.getSelectedPattern(), 2);
            expect (view.stateOf (GrooveView::machine).isNotEmpty());

            // Back to the MPC: the pads go back to it.
            view.selectTrack (GrooveView::drums);
            expectEquals (engine.getRouting().padsTo.load(), (int) RigRouting::Dest::mpc);

            // Song parts remember its pattern.
            auto scene = engine.captureScene();
            expectEquals (scene.drumPattern, 2);
            expectEquals (RigScene::fromTree (RigScene { "A", 0, scene }.toTree()).state.drumPattern, 2);

            // Saved in the show.
            d.setPattern (2, DrumProcessor::groove (4));
            const auto show = engine.saveShow();
            RigEngine other;
            other.prepare (kRate, kBlock);
            other.loadShow (show);
            expect (other.getDrums().getPattern (2) == DrumProcessor::groove (4));
            expectEquals (other.getDrums().getSelectedPattern(), 2);

            // The looper and the recorder hear it.
            expectEquals (RigLooper::sourceName (RigLooper::drums), juce::String ("Drum machine"));
            expectEquals (RigCapture::sourceName (RigCapture::drums), juce::String ("Drum machine"));
            expectEquals (RigEngine::stemName (RigEngine::drumsStem), juce::String ("Drum machine"));
            expect (RigTimeline::laneForStrip (RigEngine::drumsStrip) >= 0, "it has a lane on the timeline");
        }

        beginTest ("every choice of sound plays, at every snappy setting, and dies away; a muted sound is silent");
        {
            for (int v = 0; v < drums::kVoices; ++v)
                for (int sound = 0; sound < drums::soundNames (v).size(); ++sound)
                    for (float snappy : { 0.0f, 1.0f })
                    {
                        drums::Engine e;
                        e.prepare (kRate);
                        drums::Settings s;
                        s.voices[(size_t) v].sound = sound;
                        s.voices[(size_t) v].snappy = snappy;
                        s.voices[(size_t) v].decay = 1.0f;
                        e.setSettings (s);
                        e.trigger (v, 1.0f, true);
                        const auto h = listen (e, 7.0);
                        const auto what = juce::String (drums::voiceName (v)) + " " + drums::soundNames (v)[sound];
                        expect (h.finite, what);
                        expectGreaterThan (h.peak, 0.04f, what);
                        expectLessThan (h.peak, 2.0f, what);
                        expectLessThan (h.tail, 1.0e-3f, what + " fades away");
                    }

            drums::Engine e;
            e.prepare (kRate);
            drums::Settings s;
            s.voices[drums::kick].mute = true;
            e.setSettings (s);
            e.trigger (drums::kick, 1.0f, true);
            expectLessThan (listen (e, 0.3).peak, 1.0e-6f, "muted");
            expect (drums::soundNames (drums::lowTom).contains ("Conga"));
            expect (drums::soundNames (drums::clap).contains ("Maracas"));
            expect (drums::soundNames (drums::kick).contains ("Long boom"));
            expectWithinAbsoluteError (drums::stepBeats (4), 1.0 / 3.0, 1.0e-9);
        }

        beginTest ("snappy brings up the snare's snares");
        {
            auto highs = [] (float snappy)
            {
                drums::Engine e;
                e.prepare (kRate);
                drums::Settings s;
                s.voices[drums::snare].snappy = snappy;
                e.setSettings (s);
                e.trigger (drums::snare, 1.0f, false);
                juce::AudioBuffer<float> b (2, 9600);
                b.clear();
                juce::MidiBuffer none;
                e.render (b.getWritePointer (0), b.getWritePointer (1), 9600, none, {});
                float energy = 0;   // of the sample-to-sample changes: the hiss
                for (int i = 1; i < 9600; ++i)
                    energy += juce::square (b.getSample (0, i) - b.getSample (0, i - 1));
                return energy;
            };
            expectGreaterThan (highs (1.0f), highs (0.0f) * 3.0f);
        }

        beginTest ("A and B variations, an intro, and a chain");
        {
            std::array<drums::Pattern, drums::kPatterns> a {}, b {};
            for (auto* bank : { &a, &b })
                for (auto& p : *bank)
                    p.length = 4;
            a[0].hits[drums::kick][0] = 1;
            b[0].hits[drums::snare][0] = 1;
            a[1].hits[drums::clap][0] = 1;
            a[2].hits[drums::cowbell][0] = 1;
            drums::Clock clock;
            clock.playing = true;

            auto run = [&] (const drums::Plan& plan, double seconds)
            {
                auto e = std::make_unique<drums::Engine>();
                e->prepare (kRate);
                e->setBank (a.data(), b.data(), plan);
                listen (*e, seconds, clock);   // 2 s at 120 = 4 beats = 4 times round
                return e;
            };

            drums::Plan plan;
            plan.variation = drums::Plan::aThenB;
            auto e = run (plan, 2.0);
            expectEquals (e->hitCounts[drums::kick].load(), 2, "A, then B, then A...");
            expectEquals (e->hitCounts[drums::snare].load(), 2);

            plan.variation = drums::Plan::b;
            e = run (plan, 2.0);
            expectEquals (e->hitCounts[drums::kick].load(), 0, "just B");
            expectEquals (e->hitCounts[drums::snare].load(), 4);

            plan.variation = drums::Plan::a;
            plan.intro = 1;
            e = run (plan, 2.0);
            expectEquals (e->hitCounts[drums::clap].load(), 1, "the intro plays once");
            expectEquals (e->hitCounts[drums::kick].load(), 3, "then the beat");

            plan.intro = -1;
            plan.chainOn = true;
            plan.chainLength = 3;
            plan.chain = {};
            plan.chain[0] = 0;       // 1A
            plan.chain[1] = 4;       // 3A
            plan.chain[2] = 1;       // 1B
            e = run (plan, 3.0);     // 6 times round: twice through the chain
            expectEquals (e->hitCounts[drums::kick].load(), 2);
            expectEquals (e->hitCounts[drums::cowbell].load(), 2);
            expectEquals (e->hitCounts[drums::snare].load(), 2);
            expectEquals (e->getCurrentCode(), 1, "on the chain's last pattern");
            expectEquals (e->getChainPosition(), 2);

            // A fill takes the place of a chain entry, and the chain goes on after it.
            plan.fill = 1;
            e = run (plan, 0.4);
            e->requestFill();
            drums::Clock later = clock;
            later.ppq = 0.4 * 2.0;
            listen (*e, 1.6, later);
            expectEquals (e->hitCounts[drums::clap].load(), 1, "the fill, once");
        }

        beginTest ("B variations, the chain, sounds and kits are saved and load");
        {
            DrumProcessor a;
            drums::Pattern b;
            b.hits[drums::openHat][2] = 1;
            a.setPattern (3, b, 1);
            a.setChain ({ 0, 1, 6, 7 });
            setParam (a.params, "chain", 1.0f);
            setParam (a.params, "variation", 2.0f);
            setParam (a.params, "intro", 7.0f);
            setParam (a.params, "lowTomSound", 1.0f);
            setParam (a.params, "snareSnappy", 0.9f);
            setParam (a.params, "cowbellMute", 1.0f);
            juce::MemoryBlock state;
            a.getStateInformation (state);

            DrumProcessor c;
            c.setStateInformation (state.getData(), (int) state.getSize());
            expect (c.getPattern (3, 1) == b);
            expect (c.getPattern (3, 0) == a.getPattern (3, 0), "A stays A");
            expect (c.getChain() == std::vector<int> ({ 0, 1, 6, 7 }));
            expect (c.isChainOn());
            expectEquals (c.getVariation(), 2);
            expectEquals (c.getIntroPattern(), 6);
            expectEquals ((int) c.params.getRawParameterValue ("lowTomSound")->load(), 1);
            expectWithinAbsoluteError (c.params.getRawParameterValue ("snareSnappy")->load(), 0.9f, 0.001f);
            expect (c.params.getRawParameterValue ("cowbellMute")->load() > 0.5f);
            expectEquals (DrumProcessor::codeName (7), juce::String ("4B"));

            // An older save (no Bs, no chain) loads with empty Bs.
            DrumProcessor old;
            old.setPattern (0, b, 1);
            auto tree = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()));
            tree.removeChild (tree.getChildWithName ("PatternsB"), nullptr);
            tree.removeChild (tree.getChildWithName ("Chain"), nullptr);
            juce::MemoryBlock olderState;
            juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), olderState);
            old.setStateInformation (olderState.getData(), (int) olderState.getSize());
            expect (old.getPattern (0, 1).isEmpty());
            expect (old.getChain().empty());

            // Kits set the sounds; Classic puts them back.
            const int latin = DrumProcessor::kitNames().indexOf ("Latin (congas, maracas)");
            expect (latin > 0);
            c.loadKit (latin);
            expectEquals ((int) c.params.getRawParameterValue ("midTomSound")->load(), 1, "congas");
            expectEquals ((int) c.params.getRawParameterValue ("clapSound")->load(), 1, "maracas");
            c.loadKit (0);
            expectEquals ((int) c.params.getRawParameterValue ("midTomSound")->load(), 0);
            expect (c.params.getRawParameterValue ("cowbellMute")->load() < 0.5f);
            for (int k = 0; k < DrumProcessor::kitNames().size(); ++k)
                c.loadKit (k);
        }

        beginTest ("the processor plays A then B and the chain from its banks");
        {
            DrumProcessor p;
            p.prepareToPlay (kRate, kBlock);
            for (int i = 0; i < drums::kPatterns; ++i)
            {
                drums::Pattern empty;
                empty.length = 16;
                p.setPattern (i, empty);
            }
            drums::Pattern kick, snare;
            kick.hits[drums::kick][0] = 1;
            snare.hits[drums::snare][0] = 1;
            p.setPattern (0, kick);
            p.setPattern (0, snare, 1);
            setParam (p.params, "variation", 2.0f);
            juce::AudioBuffer<float> out (2, kBlock);
            juce::MidiBuffer midi;
            struct Head : juce::AudioPlayHead
            {
                double ppq = 0;
                juce::Optional<PositionInfo> getPosition() const override
                {
                    PositionInfo info;
                    info.setIsPlaying (true);
                    info.setBpm (120.0);
                    info.setPpqPosition (ppq);
                    return info;
                }
            } head;
            p.setPlayHead (&head);
            for (int i = 0; i < 400; ++i)   // 4 s: 8 beats, two bars
            {
                p.processBlock (out, midi);
                head.ppq += kBlock * 2.0 / kRate;
            }
            expectEquals (p.getHitCount (drums::kick), 1, "bar 1: A");
            expectEquals (p.getHitCount (drums::snare), 1, "bar 2: B");
            expectEquals (p.getPlayingCode(), 1);
            p.setPlayHead (nullptr);
        }

        beginTest ("the panel edits the B variation, and adds to the chain");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            p.setPattern (0, {});
            p.setPattern (0, {}, 1);
            p.setVariation (1);
            view->getGrid().click (2, 1 + drums::kick, false);
            expectEquals ((int) p.getPattern (0, 1).hits[drums::kick][2], 1, "B");
            expectEquals ((int) p.getPattern (0, 0).hits[drums::kick][2], 0, "not A");
            p.setVariation (0);
            expectEquals (p.editVariation, 0);

            // CRATE_DRUMS_SNAPSHOT=/path/to.png saves a picture of the panel.
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_DRUMS_SNAPSHOT", {}); path.isNotEmpty())
            {
                p.setPattern (0, DrumProcessor::groove (0));
                p.setPattern (0, DrumProcessor::groove (3), 1);
                p.setVariation (2);
                p.setChain ({ 0, 1, 0, 1, 2, 3, 12 });
                setParam (p.params, "lowTomSound", 1.0f);
                setParam (p.params, "kickSound", 1.0f);
                setParam (p.params, "cowbellMute", 1.0f);
                std::unique_ptr<juce::AudioProcessorEditor> fresh (p.createEditor());
                fresh->setSize (1180, 840);
                auto image = fresh->createComponentSnapshot (fresh->getLocalBounds(), true, 2.0f);
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }

        beginTest ("its panel opens and the grid edits the pattern");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            expect (view != nullptr);
            auto& grid = view->getGrid();
            p.setPattern (0, {});
            grid.click (3, 1 + drums::cowbell, false);
            expectEquals ((int) p.getPattern (0).hits[drums::cowbell][3], 1);
            grid.click (3, 1 + drums::cowbell, true);
            expectEquals ((int) p.getPattern (0).hits[drums::cowbell][3], 2);
            grid.click (5, 0, false);
            expect (p.getPattern (0).accent[5]);
        }
    }
};

static DrumMachineTests drumMachineTests;
