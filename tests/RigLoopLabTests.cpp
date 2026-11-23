#include "TestHelpers.h"

#include "LoopLab.h"
#include "LooperView.h"
#include "RigEngine.h"
#include "Vintage.h"

namespace
{
constexpr double kRate = 44100.0;
constexpr int kBeat = 22050;   // 120 bpm

juce::AudioBuffer<float> latePhrase()
{
    // A "voice" of tone bursts, each played a little off the beat, 4 beats long.
    juce::AudioBuffer<float> b (2, 4 * kBeat);
    b.clear();
    for (double at : { 0.0, 1.12, 2.08, 2.93 })
    {
        const int start = (int) (at * kBeat);
        for (int i = 0; i < 4000 && start + i < b.getNumSamples(); ++i)
        {
            const float v = 0.6f * std::exp (-(float) i / 1200.0f) * std::sin (0.047f * (float) i);
            b.setSample (0, start + i, v);
            b.setSample (1, start + i, v);
        }
    }
    return b;
}

int onsetNear (const juce::AudioBuffer<float>& b, int centre, int range)
{
    for (int i = juce::jmax (0, centre - range); i < juce::jmin (b.getNumSamples(), centre + range); ++i)
        if (std::abs (b.getSample (0, i)) > 0.08f)
            return i;
    return -1;
}
}

struct RigLoopLabTests : juce::UnitTest
{
    RigLoopLabTests() : juce::UnitTest ("Loop Lab", "Rig") {}

    void runTest() override
    {
        beginTest ("warp a layer: snap its hits onto the beat, undo, original, print, and follow the tempo");
        {
            RigEngine engine;
            engine.prepare (kRate, 512);
            engine.getTransport().setBpm (120.0);
            auto& looper = engine.getLooper();
            auto& lab = engine.getLoopLab();
            lab.renderInline = true;
            looper.setLayerAudio (0, latePhrase(), 4.0);

            expect (! lab.open (5), "an empty layer doesn't open");
            expect (lab.open (0));
            const auto& l = lab.get (0);
            expectEquals ((int) l.hits.size(), 3);
            expect (! lab.canUndo (0));

            lab.setSnap (0, true, 1.0, 1.0f);
            expectEquals ((int) l.plan.markers.size(), 3);
            auto out = looper.copyLayerAudio (0);
            expectEquals (out.getNumSamples(), 4 * kBeat);
            for (int k = 1; k <= 3; ++k)
            {
                const int at = onsetNear (out, k * kBeat, kBeat / 3);
                expect (at >= 0 && std::abs (at - k * kBeat) < (int) (0.012 * kRate), "hit " + juce::String (k) + " lands on the beat");
            }
            expect (lab.isOpen (0), "the lab still holds the layer after its own change");

            lab.setSettings (0, { crate::Warp::Style::beats, 0.0f, 0.0f });
            out = looper.copyLayerAudio (0);
            expect (std::abs (onsetNear (out, 2 * kBeat, kBeat / 3) - 2 * kBeat) < 120, "Beats places the hit on the beat (just after its pre-attack)");

            lab.undo (0);
            expect (l.plan.settings.style == crate::Warp::Style::voice);
            expect (lab.canUndo (0));

            lab.setOriginal (0, true);
            out = looper.copyLayerAudio (0);
            expect (std::abs (onsetNear (out, (int) (1.12 * kBeat), kBeat / 4) - (int) (1.12 * kBeat)) < 16, "Original plays it as played");
            lab.setOriginal (0, false);
            out = looper.copyLayerAudio (0);
            expect (std::abs (onsetNear (out, kBeat, kBeat / 3) - kBeat) < (int) (0.012 * kRate), "and back to the warped one");

            const int printed = lab.print (0);
            expectEquals (printed, 1);
            expect (looper.getState (1) == RigLooper::State::playing);
            expectEquals (looper.copyLayerAudio (1).getNumSamples(), 4 * kBeat);

            lab.fitTo (0, 8.0);
            expectEquals (looper.getLayer (0).lengthBeats.load(), 8.0);
            expectEquals (looper.copyLayerAudio (0).getNumSamples(), 8 * kBeat);

            // Follow song tempo: a new tempo renders it again at the new length, same pitch.
            engine.getTransport().setBpm (100.0);
            lab.tick();
            expectEquals (looper.copyLayerAudio (0).getNumSamples(), (int) std::round (8.0 * 60.0 / 100.0 * kRate));
            lab.setFollow (0, false);
            engine.getTransport().setBpm (110.0);
            lab.tick();
            expectEquals (looper.copyLayerAudio (0).getNumSamples(), (int) std::round (8.0 * 60.0 / 100.0 * kRate), "follow off: left alone");

            // Something else changes the layer: the lab lets go, and opens fresh next time.
            looper.setLayerAudio (0, latePhrase(), 4.0);
            expect (! lab.isOpen (0));
            lab.setSnap (0, true, 1.0, 1.0f);   // does nothing to a layer it let go of
            expectEquals (looper.getLayer (0).lengthBeats.load(), 4.0);
            expect (lab.open (0));
            expectEquals ((int) lab.get (0).plan.markers.size(), 0);
            expect (! lab.get (0).follow, "your Follow setting carries over");
        }

        beginTest ("the Loop Lab window: opens from the looper on Warp, In / Out still trims, and the controls work");
        {
            RigEngine engine;
            engine.prepare (kRate, 512);
            engine.getTransport().setBpm (120.0);
            engine.getLoopLab().renderInline = true;
            engine.getLooper().setLayerAudio (2, latePhrase(), 4.0);

            vintage::LookAndFeel look;
            LooperView view (engine);
            view.setLookAndFeel (&look);
            view.setBounds (0, 0, 1280, 820);
            view.openLab (0);
            expect (view.getLab() == nullptr, "no lab for an empty layer");
            view.openLab (2);
            auto* lab = view.getLab();
            expect (lab != nullptr && lab->isVisible() && lab->getPage() == LoopLab::warpPage);
            if (lab == nullptr)
                return;
            expect (! lab->getBounds().isEmpty() && lab->getHeight() >= 400);
            expect (view.getEditor() == nullptr, "In / Out isn't showing");

            lab->pressSnap();
            expectEquals ((int) engine.getLoopLab().get (2).plan.markers.size(), 3);
            lab->dragMarker (0, 1.25);
            expect (engine.getLoopLab().get (2).plan.markers[0].manual);
            expectWithinAbsoluteError (engine.getLoopLab().get (2).plan.markers[0].target, 1.25, 1.0e-9);
            lab->setTranspose (-2.0f, 10.0f);
            expectWithinAbsoluteError (engine.getLoopLab().get (2).plan.settings.transpose, -1.9f, 1.0e-4f);
            lab->setFormant (1.5f);
            expectWithinAbsoluteError (engine.getLoopLab().get (2).plan.settings.formant, 1.5f, 1.0e-4f);
            lab->pressStyle (crate::Warp::Style::texture);
            expect (engine.getLoopLab().get (2).plan.settings.style == crate::Warp::Style::texture);
            lab->pressUndo();
            expect (engine.getLoopLab().get (2).plan.settings.style == crate::Warp::Style::voice);
            expectEquals (lab->pressPrint(), 0, "prints onto the first empty layer");
            lab->pressFit (8.0);
            expectEquals (engine.getLooper().getLayer (2).lengthBeats.load(), 8.0);
            lab->tick();

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_LOOP_LAB_SNAPSHOT", {}); path.isNotEmpty())
            {
                lab->pressUndo();   // back to 4 bars, markers showing
                lab->tick();
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
                juce::File file (path);
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            // The menu's "Edit where it starts and ends" opens the same lab on In / Out.
            view.editLoop (2);
            expect (view.getLab() == lab && lab->getPage() == LoopLab::inOutPage);
            auto* editor = view.getEditor();
            expect (editor != nullptr && editor->isVisible());
            if (editor != nullptr)
            {
                editor->setRange (0.0, 4.0);
                expectWithinAbsoluteError (engine.getLooper().getLayer (2).lengthBeats.load(), 4.0, 1.0e-9);
            }
            lab->showPage (LoopLab::warpPage);
            expect (engine.getLoopLab().isOpen (2), "Warp opens the trimmed loop afresh");
            expectEquals ((int) engine.getLoopLab().get (2).plan.markers.size(), 0);
            view.setLookAndFeel (nullptr);
        }
    }
};

static RigLoopLabTests rigLoopLabTests;
