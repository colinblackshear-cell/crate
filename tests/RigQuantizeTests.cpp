#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "GrooveView.h"
#include "QuantizePanel.h"

// Quantize: one set of settings that pulls the MPC, the drum machine, the Bass, the Synth's arp
// and the loops onto the beat, from wherever you are.
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// A loop of short clicks (a decaying burst each) at the given beats, 120 bpm.
juce::AudioBuffer<float> clicks (const std::vector<double>& beats, double loopBeats)
{
    const int n = (int) std::round (loopBeats * kRate / 2.0);
    juce::AudioBuffer<float> b (2, n);
    b.clear();
    juce::Random random (7);
    for (double beat : beats)
    {
        const int at = (int) std::round (beat * kRate / 2.0);
        for (int i = 0; i < 2400 && at + i < n; ++i)
        {
            const float v = 0.8f * std::exp (-i / 300.0f) * (random.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, at + i, v);
            b.setSample (1, at + i, v);
        }
    }
    return b;
}

double hitBeat (int sample) { return sample * 2.0 / kRate; }
}

struct RigQuantizeTests : juce::UnitTest
{
    RigQuantizeTests() : juce::UnitTest ("Rig quantize", "Rig") {}

    void runTest() override
    {
        using namespace quantize;

        beginTest ("the grids, triplets and swing");
        {
            Settings s;
            s.grid = sixteenth;
            expectWithinAbsoluteError (nearestLine (s, 0.3), 0.25, 1.0e-9);
            expectWithinAbsoluteError (nearestLine (s, 0.9), 1.0, 1.0e-9);
            s.grid = eighthTriplet;
            expectWithinAbsoluteError (nearestLine (s, 0.3), 1.0 / 3.0, 1.0e-9);
            expectWithinAbsoluteError (nearestLine (s, 0.6), 2.0 / 3.0, 1.0e-9);
            s.grid = quarter;
            expectWithinAbsoluteError (nearestLine (s, 1.4), 1.0, 1.0e-9);
            s.grid = eighth;
            s.swing = 66;
            expectWithinAbsoluteError (lineBeat (s, 1), 0.5 + 0.5 * 16.0 / 50.0, 1.0e-9);
            expectWithinAbsoluteError (nearestLine (s, 0.62), 0.66, 1.0e-9);
            s.strength = 0.5f;
            s.swing = 50;
            expectWithinAbsoluteError (pull (s, 0.6), 0.55, 1.0e-6);
            expectEquals (gridName (sixteenthTriplet), juce::String ("1/16 T"));

            Settings saved;
            saved.grid = eighthTriplet;
            saved.strength = 0.75f;
            saved.swing = 60;
            saved.newLoops = true;
            expect (Settings::fromTree (saved.toTree()) == saved);
        }

        beginTest ("the MPC's sequence: notes move in ticks, wrap round, and two on one tick become one");
        {
            seq::Sequence sq;
            sq.bars = 1;
            sq.tracks[0].add ({ 3, 0, 100 });
            sq.tracks[0].add ({ 50, 1, 90 });
            sq.tracks[0].add ({ 382, 2, 80 });    // just before the loop's end
            sq.tracks[1].add ({ 22, 4, 70 });
            sq.tracks[1].add ({ 26, 4, 120 });   // the same pad pulled onto the same tick: the louder stays
            Settings s;
            s.grid = sixteenth;
            const int moved = sequence (sq, s);
            expectEquals (moved, 5);
            const auto& a = sq.tracks[0].notes;
            expectEquals ((int) a.size(), 3);
            expectEquals (a[0].tick, 0);
            expect (a[0].pad == 0 || a[0].pad == 2, "the wrapped note is at the top");
            expectEquals (a[2].tick, 48);
            expectEquals ((int) sq.tracks[1].notes.size(), 1);
            expectEquals (sq.tracks[1].notes[0].tick, 24);
            expectEquals (sq.tracks[1].notes[0].velocity, 120);

            seq::Sequence half;
            half.tracks[0].add ({ 10, 0, 100 });
            Settings h;
            h.grid = eighth;
            h.strength = 0.5f;
            sequence (half, h);
            expectEquals (half.tracks[0].notes[0].tick, 5, "halfway to the 1/8 line");
        }

        beginTest ("step patterns: a coarser or triplet grid moves steps, a finer one leaves them");
        {
            Settings s;
            s.grid = eighth;
            auto to = stepTargets (16, 0.25, s);
            expectEquals (to[0], 0);
            expectEquals (to[1], 2, "halfway goes to the later line");
            expectEquals (to[2], 2);
            expectEquals (to[3], 4);
            expectEquals (to[15], 0, "the last 1/16 goes round to the top");
            s.grid = sixteenth;
            to = stepTargets (16, 0.25, s);
            for (int i = 0; i < 16; ++i)
                expectEquals (to[(size_t) i], i);
            s.grid = eighthTriplet;
            to = stepTargets (16, 0.25, s);
            expectEquals (to[2], 3, "the & of the beat goes to the last triplet");
            expectEquals (to[1], 1);

            crate::drums::Pattern p;
            p.hits[0][0] = 1;
            p.hits[0][15] = 3;   // a roll just before the top: it goes round onto the kick and keeps the roll
            p.hits[2][7] = 1;
            p.accent[7] = true;
            Settings e;
            e.grid = eighth;
            const int moved = drumPattern (p, 0.25, e);
            expectEquals (moved, 2);
            expectEquals ((int) p.hits[0][0], 3);
            expectEquals ((int) p.hits[0][15], 0);
            expectEquals ((int) p.hits[2][8], 1);
            expect (p.accent[8] && ! p.accent[7], "the accent goes with its hit");

            crate::bass::Pattern b;
            b.steps[0] = { true, 0 };
            b.steps[15] = { true, 5 };   // its place is taken by a note already on the grid: it stays
            b.steps[3] = { true, 7, true, true };
            expectEquals (bassPattern (b, 0.25, e), 1);
            expectEquals (b.steps[0].note, 0);
            expect (b.steps[15].on && b.steps[15].note == 5 && ! b.steps[3].on);
            expect (b.steps[4].on && b.steps[4].note == 7 && b.steps[4].accent && b.steps[4].slide);

            crate::synthtool::Rhythm r;
            r.on.fill (false);
            r.on[5] = true;
            r.accent[5] = true;
            expectEquals (rhythm (r, 0.25, e), 1);
            expect (r.on[6] && r.accent[6] && ! r.on[5]);
        }

        beginTest ("audio: each hit slides onto the grid, the loop keeps its length");
        {
            // Hits a little off the 1/8s, over two beats (one second at 120).
            const std::vector<double> played { 0.0, 0.56, 0.97, 1.45 };
            auto loop = clicks (played, 2.0);
            const auto found = findHits (loop, kRate);
            expectEquals ((int) found.size(), 4);
            for (size_t i = 0; i < found.size() && i < played.size(); ++i)
                expectWithinAbsoluteError (hitBeat (found[i]), played[i], 0.01);

            Settings s;
            s.grid = eighth;
            auto result = audio (loop, 2.0, kRate, s);
            expectEquals (result.audio.getNumSamples(), loop.getNumSamples());
            expectEquals (result.hits, 4);
            expectEquals (result.moved, 3);
            const auto after = findHits (result.audio, kRate);
            expectEquals ((int) after.size(), 4);
            const double grid[] = { 0.0, 0.5, 1.0, 1.5 };
            for (size_t i = 0; i < after.size() && i < 4; ++i)
                expectWithinAbsoluteError (hitBeat (after[i]), grid[i], 0.006);

            s.strength = 0.0f;
            auto none = audio (loop, 2.0, kRate, s);
            expectEquals (none.moved, 0);
            expectEquals (none.audio.getSample (0, 1000), loop.getSample (0, 1000));

            // A sustained sound with no hits stays as it is.
            auto pad = sine (48000, 2, 0.4f);
            auto kept = audio (pad, 2.0, kRate, Settings());
            expectLessOrEqual (kept.hits, 1);
            expectEquals (kept.moved, 0);
        }

        beginTest ("in the Rig: a loop is quantized in place, keeps playing, and Undo puts it back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            auto loop = clicks ({ 0.06, 1.1, 2.04, 2.9 }, 4.0);
            looper.setLayerAudio (2, loop, 4.0);
            auto& q = engine.getQuantizer();
            q.settings.grid = quarter;
            const int version = looper.getLayer (2).version.load();
            const auto said = q.apply (RigQuantizer::loopTarget (2));
            expect (said.startsWith ("Quantized Loop 3"), said);
            expect (looper.getLayer (2).version.load() != version);
            expect (looper.getState (2) == RigLooper::State::playing);
            expectEquals (looper.getLayer (2).lengthBeats.load(), 4.0);
            auto now = looper.copyLayerAudio (2);
            const auto hits = findHits (now, kRate);
            expectEquals ((int) hits.size(), 4);
            for (size_t i = 0; i < hits.size(); ++i)
                expectWithinAbsoluteError (hitBeat (hits[i]), (double) i, 0.006);
            expect (q.canUndo());
            q.undo();
            auto back = looper.copyLayerAudio (2);
            expectEquals (back.getSample (0, 5000), loop.getSample (0, 5000));
            expect (! q.canUndo());

            expect (q.apply (RigQuantizer::allLoops).startsWith ("Quantized 1 loop"));
            expect (q.apply (RigQuantizer::loopTarget (5)).contains ("empty"));
        }

        beginTest ("in the Rig: the MPC, drum machine, Bass and Synth, each with Undo");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& q = engine.getQuantizer();
            q.settings.grid = sixteenth;

            auto& project = engine.getMpc().getProject();
            project.currentSequence().tracks[0].add ({ 29, 0, 100 });
            expect (q.apply (RigQuantizer::mpc).startsWith ("Quantized 1 note"));
            expectEquals (project.currentSequence().tracks[0].notes[0].tick, 24);
            q.undo();
            expectEquals (project.currentSequence().tracks[0].notes[0].tick, 29);
            expect (q.apply (RigQuantizer::mpc).isNotEmpty());
            expect (q.apply (RigQuantizer::mpc).contains ("already on the 1/16 grid"));

            crate::drums::Pattern d;
            d.hits[0][3] = 1;
            engine.getDrums().setEditedPattern (d);
            expect (q.apply (RigQuantizer::machine).contains ("pick a coarser grid"), "1/16 steps are already on 1/16");
            q.settings.grid = eighth;
            expect (q.apply (RigQuantizer::machine).startsWith ("Quantized 1 hit"));
            expectEquals ((int) engine.getDrums().getEditedPattern().hits[0][4], 1);
            q.undo();
            expectEquals ((int) engine.getDrums().getEditedPattern().hits[0][3], 1);

            auto& bass = engine.getBass();
            crate::bass::Pattern b;
            b.steps[5] = { true, 3 };
            bass.setPattern (bass.getSelectedPattern(), b);
            expect (q.apply (RigQuantizer::bass).startsWith ("Quantized 1 note"));
            expect (bass.getPattern (bass.getSelectedPattern()).steps[6].on);
            q.undo();
            expect (bass.getPattern (bass.getSelectedPattern()).steps[5].on);

            crate::synthtool::Rhythm r;
            r.on.fill (false);
            r.on[1] = true;
            engine.getSynth().setRhythm (r);
            expect (q.apply (RigQuantizer::synth).startsWith ("Quantized 1 step"));
            expect (engine.getSynth().getRhythm().on[2]);
        }

        beginTest ("the settings are saved with the show and kept by New song");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            expect (engine.isBlank());
            auto& q = engine.getQuantizer();
            q.settings.grid = sixteenthTriplet;
            q.settings.strength = 0.6f;
            expect (engine.isBlank(), "changing quantize alone doesn't make a song to save");
            const auto show = engine.saveShow();
            RigEngine other;
            other.loadShow (show);
            expectEquals (other.getQuantizer().settings.grid, (int) sixteenthTriplet);
            expectWithinAbsoluteError (other.getQuantizer().settings.strength, 0.6f, 1.0e-6f);
            engine.newShow();
            expectEquals (q.settings.grid, (int) sixteenthTriplet);
        }

        beginTest ("New loops: a loop is quantized as its recording finishes");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            engine.micOn = true;
            engine.getTransport().setBpm (120.0);
            auto& q = engine.getQuantizer();
            q.settings.grid = quarter;
            q.settings.newLoops = true;
            auto& layer = engine.getLooper().getLayer (0);
            layer.source = RigLooper::input;
            layer.bars = 1;
            engine.getLooper().pressRecord (0);
            // Clicks a little late on each beat of the bar.
            const auto played = clicks ({ 0.08, 1.08, 2.08, 3.08 }, 4.0);
            juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
            const int bar = (int) (2.0 * kRate / kBlock);
            for (int b = 0; b < bar + 10; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    in.copyFrom (ch, 0, played, ch, (b * kBlock) % played.getNumSamples(), kBlock);
                engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
                q.service();
            }
            expect (engine.getLooper().getState (0) == RigLooper::State::playing);
            expect (q.lastAutoMessage.startsWith ("Quantized Loop 1"), q.lastAutoMessage);
            expectEquals (q.getUndoTarget(), RigQuantizer::loopTarget (0));
            const auto hits = findHits (engine.getLooper().copyLayerAudio (0), kRate);
            expectGreaterOrEqual ((int) hits.size(), 3);
            for (auto h : hits)
            {
                const double beat = hitBeat (h);
                expectWithinAbsoluteError (beat, std::round (beat), 0.01);
            }
        }

        beginTest ("the window: Cmd+U quantizes what the screen shows; the panel picks the grid");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Drum machine");
            expectEquals (view.quantizeTarget(), (int) RigQuantizer::machine);
            view.goTo ("Bass");
            expectEquals (view.quantizeTarget(), (int) RigQuantizer::bass);
            view.goTo ("MPC");
            expectEquals (view.quantizeTarget(), (int) RigQuantizer::mpc);
            view.goTo ("Looper");
            expectEquals (view.quantizeTarget(), (int) RigQuantizer::allLoops);

            engine.getMpc().getProject().currentSequence().tracks[0].add ({ 30, 0, 100 });
            view.goTo ("MPC");
            expect (view.keyPressed (juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0)));
            expectEquals (engine.getMpc().getProject().currentSequence().tracks[0].notes[0].tick, 24);

            QuantizePanel panel (engine, RigQuantizer::mpc);
            panel.setSize (QuantizePanel::kWidth, QuantizePanel::kHeight);
            panel.pickGrid (eighth);
            expectEquals (engine.getQuantizer().settings.grid, (int) eighth);
            panel.pressQuantize();
            expectEquals (engine.getMpc().getProject().currentSequence().tracks[0].notes[0].tick, 48, "halfway goes to the later 1/8");
            expect (panel.getStatus().startsWith ("Quantized"));
            panel.pressUndo();
            expectEquals (engine.getMpc().getProject().currentSequence().tracks[0].notes[0].tick, 24);
        }

        if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_QUANTIZE_SNAPSHOT", {}); path.isNotEmpty())
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getLooper().setLayerAudio (0, clicks ({ 0.06, 0.55, 1.1, 1.45, 2.04, 2.6, 2.9, 3.52 }, 4.0), 4.0);
            engine.getLooper().setLayerAudio (1, clicks ({ 0.1, 1.02, 2.12, 3.04 }, 4.0), 4.0);
            engine.getLooper().setLayerAudio (2, clicks ({ 0.5, 1.56, 2.47, 3.6 }, 4.0), 4.0);
            RigView view (engine, nullptr);
            const auto snap = [&view, &path] (const juce::String& shot)
            {
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                juce::File file (path + shot + ".png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            };
            view.setSize (1440, 960);
            view.goTo ("Looper");
            engine.getQuantizer().settings.grid = sixteenth;
            view.showQuantize (RigQuantizer::loopTarget (1));
            if (auto* panel = view.getQuantizePanel())
                panel->pressQuantize();
            snap ("looper-quantize");
            const auto closePanel = [&view]
            {
                if (auto* panel = view.getQuantizePanel())
                    if (auto* box = panel->findParentComponentOfClass<juce::CallOutBox>())
                        box->setVisible (false);   // (dismiss() frees it on the next message)
            };
            closePanel();

            view.goTo ("Groove");
            if (auto* groove = [&view]() -> GrooveView* {
                    std::function<GrooveView* (juce::Component&)> find = [&find] (juce::Component& c) -> GrooveView*
                    {
                        if (auto* g = dynamic_cast<GrooveView*> (&c))
                            return g;
                        for (auto* child : c.getChildren())
                            if (auto* g = find (*child))
                                return g;
                        return nullptr;
                    };
                    return find (view); }())
                groove->selectTrack (GrooveView::machine);
            engine.getQuantizer().settings.grid = eighthTriplet;
            engine.getQuantizer().settings.strength = 0.8f;
            view.showQuantize (view.quantizeTarget());
            snap ("groove-quantize");
            closePanel();
        }
    }
};

static RigQuantizeTests rigQuantizeTests;
