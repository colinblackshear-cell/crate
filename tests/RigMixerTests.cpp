#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "MixerView.h"

// The Mixer (F5): pan, solo and the two sends in the engine, saved with the show; and the
// desk on screen, with a strip for every tool and loop layer and a way into every tool.
class RigMixerTests : public juce::UnitTest
{
public:
    RigMixerTests() : juce::UnitTest ("Rig mixer", "Rig") {}

    static constexpr double kRate = 48000.0;
    static constexpr int kBlock = 480;

    // Plays a sine into the audio input for `blocks` blocks; returns the loudest of each side
    // over the last block.
    static std::pair<float, float> playInput (RigEngine& engine, int blocks, float level = 0.4f)
    {
        juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
        std::pair<float, float> peaks;
        static int phase = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i, ++phase)
                for (int ch = 0; ch < 2; ++ch)
                    in.setSample (ch, i, level * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * phase / kRate));
            out.clear();
            engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            peaks = { out.getMagnitude (0, 0, kBlock), out.getMagnitude (1, 0, kBlock) };
        }
        return peaks;
    }

    static std::pair<float, float> playSilence (RigEngine& engine, int blocks)
    {
        juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
        in.clear();
        float l = 0, r = 0;
        for (int b = 0; b < blocks; ++b)
        {
            out.clear();
            engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            l = juce::jmax (l, out.getMagnitude (0, 0, kBlock));
            r = juce::jmax (r, out.getMagnitude (1, 0, kBlock));
        }
        return { l, r };
    }

    static void listenToInput (RigEngine& engine)
    {
        engine.vocoderOn = false;
        engine.micOn = true;
        engine.inputOn = true;
    }

    void runTest() override
    {
        beginTest ("pan: the middle leaves the sound as it was; hard left leaves the right silent");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            listenToInput (engine);
            const auto centre = playInput (engine, 20);
            expect (centre.first > 0.2f && std::abs (centre.first - centre.second) < 1.0e-4f, "both sides, as before");
            engine.getStrip (RigEngine::inputStrip).pan = -1.0f;
            const auto left = playInput (engine, 20);
            expectWithinAbsoluteError (left.first, centre.first, 0.02f);
            expect (left.second < 1.0e-4f, "nothing on the right");
            engine.getStrip (RigEngine::inputStrip).pan = 0.5f;
            const auto right = playInput (engine, 20);
            expect (right.first < centre.first * 0.6f && right.second > centre.second * 0.95f, "half right turns the left down");
        }

        beginTest ("solo: only soloed strips are heard; clearing it brings everything back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            listenToInput (engine);
            expect (playInput (engine, 20).first > 0.2f);
            engine.getStrip (RigEngine::bassStrip).solo = true;
            expect (engine.anySolo());
            expect (engine.isSilenced (RigEngine::inputStrip) && ! engine.isSilenced (RigEngine::bassStrip));
            expect (playInput (engine, 20).first < 1.0e-4f, "the input is silent while the bass is soloed");
            engine.getStrip (RigEngine::inputStrip).solo = true;
            expect (playInput (engine, 20).first > 0.2f, "soloed too, it's heard");
            engine.getStrip (RigEngine::bassStrip).solo = false;
            engine.getStrip (RigEngine::inputStrip).solo = false;
            expect (! engine.anySolo());
            expect (! engine.isSilenced (RigEngine::masterStrip), "the master is never silenced by a solo");
            engine.getStrip (RigEngine::masterStrip).solo = true;
            expect (! engine.anySolo(), "the master can't be soloed");
        }

        beginTest ("the sends: A rings on in the reverb, B echoes in time; a muted strip sends nothing");
        {
            for (int ret : { (int) RigEngine::reverbReturn, (int) RigEngine::echoReturn })
            {
                RigEngine engine;
                engine.prepare (kRate, kBlock);
                engine.getTransport().setBpm (120.0);
                listenToInput (engine);
                auto& strip = engine.getStrip (RigEngine::inputStrip);
                // Without a send, it stops when the input does.
                playInput (engine, 10);
                expect (playSilence (engine, 10).first < 0.01f, "dry: it stops");

                (ret == RigEngine::reverbReturn ? strip.sendA : strip.sendB) = 1.0f;
                playInput (engine, 10);
                // An echo of 1/8 at 120 is 0.25 s (25 blocks); the reverb rings straight away.
                const auto tail = playSilence (engine, 40);
                expect (tail.first > 0.01f && tail.second > 0.01f, RigEngine::returnName (ret) + " rings on after the input stops");
                expect (engine.getReturn (ret).peak.load() > 0.0f, "its meter moves");

                // Muted, its return is silent.
                engine.getReturn (ret).mute = true;
                playSilence (engine, 400);
                playInput (engine, 10);
                expect (playSilence (engine, 40).first < 0.01f, "a muted return is silent");
                engine.getReturn (ret).mute = false;

                // A muted strip sends nothing.
                strip.mute = true;
                playSilence (engine, 400);
                playInput (engine, 10);
                expect (playSilence (engine, 40).first < 1.0e-4f, "a muted strip sends nothing");
            }
        }

        beginTest ("pan, solo, the sends and the returns are saved with the show; older shows load as before");
        {
            RigEngine engine;
            auto& bass = engine.getStrip (RigEngine::bassStrip);
            bass.pan = -0.4f;
            bass.solo = true;
            bass.sendA = 0.3f;
            bass.sendB = 0.6f;
            engine.getReturn (RigEngine::reverbReturn).amount = 0.9f;
            engine.getReturn (RigEngine::reverbReturn).tone = 0.2f;
            engine.getReturn (RigEngine::echoReturn).time = 4;
            engine.getReturn (RigEngine::echoReturn).level = 0.5f;
            engine.getReturn (RigEngine::echoReturn).mute = true;
            const auto show = engine.saveShow();

            RigEngine loaded;
            loaded.loadShow (show);
            auto& b = loaded.getStrip (RigEngine::bassStrip);
            expectWithinAbsoluteError (b.pan.load(), -0.4f, 1.0e-6f);
            expect (b.solo.load());
            expectWithinAbsoluteError (b.sendA.load(), 0.3f, 1.0e-6f);
            expectWithinAbsoluteError (b.sendB.load(), 0.6f, 1.0e-6f);
            expectWithinAbsoluteError (loaded.getReturn (RigEngine::reverbReturn).amount.load(), 0.9f, 1.0e-6f);
            expectWithinAbsoluteError (loaded.getReturn (RigEngine::reverbReturn).tone.load(), 0.2f, 1.0e-6f);
            expectEquals (loaded.getReturn (RigEngine::echoReturn).time.load(), 4);
            expectWithinAbsoluteError (loaded.getReturn (RigEngine::echoReturn).level.load(), 0.5f, 1.0e-6f);
            expect (loaded.getReturn (RigEngine::echoReturn).mute.load());

            // A show from before the mixer: nothing panned, soloed or sent.
            auto older = show.createCopy();
            for (int i = older.getNumChildren(); --i >= 0;)
            {
                auto child = older.getChild (i);
                if (child.hasType ("Return"))
                    older.removeChild (i, nullptr);
                else if (child.hasType ("Strip"))
                    for (auto id : { "pan", "solo", "sendA", "sendB" })
                        child.removeProperty (id, nullptr);
            }
            loaded.loadShow (older);
            expectEquals (loaded.getStrip (RigEngine::bassStrip).pan.load(), 0.0f);
            expect (! loaded.anySolo());
            expectEquals (loaded.getStrip (RigEngine::bassStrip).sendA.load(), 0.0f);
            expect (! loaded.getReturn (RigEngine::echoReturn).mute.load());
            expectEquals (loaded.getReturn (RigEngine::echoReturn).time.load(), 1);
        }

        beginTest ("the Mixer is F5: a strip for every tool, the returns and the master; its buttons go into every tool");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            expectEquals (rigpanels::ScreenBar::screens()[4], juce::String ("Mixer"));
            view.goTo ("Mixer");
            expectEquals (view.getPage(), juce::String ("Mixer"));
            auto* mixer = view.getMixer();
            expect (mixer != nullptr && mixer->isVisible());
            if (mixer == nullptr)
                return;

            for (int strip : MixerView::toolStrips())
            {
                auto* c = mixer->channelFor (strip);
                expect (c != nullptr && c->isVisible() && c->getWidth() > 60 && c->getHeight() > 300, RigEngine::stripName (strip));
                if (c == nullptr)
                    continue;
                for (auto* control : std::initializer_list<juce::Component*> { &c->fader, &c->sendA, &c->sendB, &c->pan, &c->mute, &c->solo, &c->fx, &c->learn })
                    expect (control->isVisible() && ! control->getBounds().isEmpty(), RigEngine::stripName (strip));
                expect (c->fader.getHeight() > 80, "a tall fader");

                // The strip's controls play the engine.
                c->fader.setValue (0.5, juce::sendNotificationSync);
                expectWithinAbsoluteError (engine.getStrip (strip).level.load(), 0.5f, 1.0e-6f);
                c->pan.setValue (0.25, juce::sendNotificationSync);
                expectWithinAbsoluteError (engine.getStrip (strip).pan.load(), 0.25f, 1.0e-6f);
                c->sendA.setValue (0.4, juce::sendNotificationSync);
                c->sendB.setValue (0.7, juce::sendNotificationSync);
                expectWithinAbsoluteError (engine.getStrip (strip).sendA.load(), 0.4f, 1.0e-6f);
                expectWithinAbsoluteError (engine.getStrip (strip).sendB.load(), 0.7f, 1.0e-6f);
                c->mute.onClick();
                expect (engine.getStrip (strip).mute.load());
                c->mute.onClick();
                c->solo.onClick();
                expect (engine.getStrip (strip).solo.load());
                c->solo.onClick();
                expect (! engine.anySolo());
            }
            for (int r : { (int) RigEngine::reverbReturn, (int) RigEngine::echoReturn })
            {
                auto* c = mixer->returnChannel (r);
                expect (c != nullptr && c->isVisible() && c->fader.isVisible() && c->amount.isVisible(), RigEngine::returnName (r));
                if (c == nullptr)
                    continue;
                c->fader.setValue (0.3, juce::sendNotificationSync);
                expectWithinAbsoluteError (engine.getReturn (r).level.load(), 0.3f, 1.0e-6f);
                c->amount.setValue (0.6, juce::sendNotificationSync);
                expectWithinAbsoluteError (engine.getReturn (r).amount.load(), 0.6f, 1.0e-6f);
            }
            auto* master = mixer->channelFor (RigEngine::masterStrip);
            expect (master != nullptr && master->isVisible() && master->learn.isVisible());

            // The vocoder and the input have their switches.
            auto* vocoder = mixer->channelFor (RigEngine::vocoderStrip);
            expect (vocoder->on.isVisible());
            const bool was = engine.vocoderOn.load();
            vocoder->on.onClick();
            expect (engine.vocoderOn.load() != was);
            auto* input = mixer->channelFor (RigEngine::inputStrip);
            expect (input->on.isVisible() && input->jack.isVisible());

            // Every hub button goes to its tool (and the Big stage's Esc comes back here).
            expectEquals (mixer->hub.size(), MixerView::hubPages().size());
            for (int i = 0; i < mixer->hub.size(); ++i)
            {
                view.goTo ("Mixer");
                mixer->hub[i]->onClick();
                expectEquals (view.getPage(), MixerView::hubPages()[i]);
            }
            view.goTo ("Stage");
            view.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            expectEquals (view.getPage(), juce::String ("Mixer"), "Esc leaves the big Stage for the Mixer");

            // FX opens the strip's effects along the bottom; again closes them.
            mixer->channelFor (RigEngine::bassStrip)->fx.onClick();
            expect (mixer->getPanel() != nullptr && mixer->getPanel()->isVisible(), "FX shows the bass's effects");
            expect (mixer->channelFor (RigEngine::bassStrip)->getBottom() <= mixer->getPanel()->getY(), "above the effects");
            mixer->channelFor (RigEngine::bassStrip)->fx.onClick();
            expect (mixer->getPanel() == nullptr);

            // Learn waits for a keyboard fader.
            mixer->channelFor (RigEngine::synthStrip)->learn.onClick();
            expectEquals (engine.getRouting().getLearning(), (int) RigRouting::synthLevel);
            engine.getRouting().cancelLearn();

            // Loop layers: eight strips, into the Loops strip.
            mixer->layersBank.onClick();
            expect (mixer->isShowingLayers());
            for (int l = 0; l < RigLooper::kLayers; ++l)
            {
                auto* c = mixer->layerChannel (l);
                expect (c != nullptr && c->isVisible() && c->rec.isVisible() && c->fader.isVisible(), "layer " + juce::String (l + 1));
            }
            expect (mixer->channelFor (RigEngine::loopsStrip)->isVisible() && ! mixer->channelFor (RigEngine::mpcStrip)->isVisible());
            mixer->layerChannel (2)->fader.setValue (0.3, juce::sendNotificationSync);
            expectWithinAbsoluteError (engine.getLooper().getLayer (2).level.load(), 0.3f, 1.0e-6f);
            mixer->layerChannel (2)->mute.onClick();
            expect (engine.getLooper().getLayer (2).mute.load());
            mixer->layerChannel (0)->rec.onClick();
            playSilence (engine, 2);
            expect (engine.getLooper().getState (0) != RigLooper::State::empty, "Rec records the layer");
            mixer->layerChannel (1)->fx.onClick();
            expect (mixer->getPanel() != nullptr, "a layer's FX shows its loop effects");

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_MIXER_SNAPSHOT", {}); path.isNotEmpty())
            {
                // A fresh desk, then a few settings to show them.
                for (int st = 0; st < RigEngine::numStrips; ++st)
                {
                    auto& strip = engine.getStrip (st);
                    strip.level = 0.8f;
                    strip.pan = 0.0f;
                    strip.sendA = strip.sendB = 0.0f;
                    strip.solo = strip.mute = false;
                }
                for (int r = 0; r < RigEngine::numReturns; ++r)
                    engine.getReturn (r).level = 0.8f;
                engine.getRouting().cancelLearn();
                engine.getStrip (RigEngine::bassStrip).sendA = 0.35f;
                engine.getStrip (RigEngine::synthStrip).pan = -0.3f;
                engine.getStrip (RigEngine::synthStrip).sendB = 0.5f;
                engine.getStrip (RigEngine::vocoderStrip).solo = true;
                engine.getStrip (RigEngine::drumsStrip).mute = true;
                for (auto [w, h, layersShown, fx, suffix] : { std::tuple<int, int, bool, bool, const char*> { 1440, 960, false, false, "" },
                                                              { 1440, 960, false, true, "-fx" }, { 1440, 960, true, false, "-layers" },
                                                              { 1180, 820, false, false, "-small" } })
                {
                    view.setSize (w, h);
                    view.goTo ("Mixer");
                    view.closeMixerPanel();
                    mixer->showLayers (layersShown);
                    if (fx)
                        mixer->channelFor (RigEngine::synthStrip)->fx.onClick();
                    mixer->refresh();
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
                    juce::File file (path + suffix + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
        }

        beginTest ("the old More > Mixer page is Setup now; More and the Guide reach the Mixer, the Big stage and Setup");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            for (auto page : { "Setup", "Stage", "Mixer" })
            {
                view.goTo (page);
                expectEquals (view.getPage(), juce::String (page));
            }
        }
    }
};

static RigMixerTests rigMixerTests;
