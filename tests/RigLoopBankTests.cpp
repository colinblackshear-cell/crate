#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigLooper.h"
#include "LooperView.h"
#include "SongView.h"

// Loop banks: keep a batch of loops, start a new one, come back to it (and song parts that do).
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

juce::AudioBuffer<float> constant (float value, int samples)
{
    juce::AudioBuffer<float> b (2, samples);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, samples);
    return b;
}

void run (RigEngine& engine, int blocks, float* lastPeak = nullptr)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        if (lastPeak != nullptr)
            *lastPeak = out.getMagnitude (0, 0, kBlock);
    }
}

void setFx (StripFx& fx, const char* id, float value)
{
    auto* p = fx.params.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}
}

struct RigLoopBankTests : juce::UnitTest
{
    RigLoopBankTests() : juce::UnitTest ("Rig loop banks", "Rig") {}

    void runTest() override
    {
        beginTest ("the looper swaps a layer's loop exactly at a beat, from its top");
        {
            RigLooper looper;
            looper.prepare (kRate);
            RigLooper::Clock clock;
            clock.bpm = 120;
            clock.playing = true;
            auto& layer = looper.getLayer (0);
            layer.level = 1.0f;
            looper.setLayerAudio (0, constant (0.5f, 48000), 4.0);
            looper.queueLayerAudio (0, constant (0.25f, 24000), 2.0, 4.0, 0.5f, false);
            expect (looper.isSwapPending (0));
            juce::AudioBuffer<float> out (2, kBlock);
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            std::vector<float> heard;
            for (int b = 0; b < 300; ++b)   // 3 seconds = 6 beats
            {
                looper.process (clock, sources, out);
                for (int i = 0; i < kBlock; ++i)
                    heard.push_back (out.getSample (0, i));
                clock.beat += kBlock * clock.bpm / (60.0 * kRate);
            }
            const auto at = [&] (double beat) { return heard[(size_t) (beat * 0.5 * kRate)]; };
            expectWithinAbsoluteError (at (3.9), 0.5f, 0.02f, "the old loop until the beat");
            expectWithinAbsoluteError (at (4.2), 0.125f, 0.02f, "the new one (at its own level) after it");
            expect (! looper.isSwapPending (0));
            expectWithinAbsoluteError (layer.lengthBeats.load(), 2.0, 1.0e-9);
            expectWithinAbsoluteError (layer.level.load(), 0.5f, 1.0e-6f);
            looper.maintain();   // frees the old sound

            // An empty swap, right away.
            looper.queueLayerAudio (0, {}, 0, -1.0, 0.8f, true);
            expect (looper.getState (0) == RigLooper::State::empty);
            expect (layer.mute.load());
        }

        beginTest ("a new batch keeps the loops in their bank, and switching back brings them back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            looper.setLayerAudio (2, constant (0.2f, 48000), 2.0);
            looper.getLayer (0).level = 0.6f;
            looper.getLayer (2).mute = true;
            looper.getLayer (1).source = RigLooper::line;
            setFx (engine.getLoopFx (0), "drive", 0.7f);
            expectEquals (engine.getNumLoopBanks(), 1);
            expectEquals (engine.getLoopBankName (0), juce::String ("Bank 1"));

            engine.saveLoopBank ("Verse ideas");
            expectEquals (engine.getLoopBankName (0), juce::String ("Verse ideas"));
            const auto said = engine.newLoopBank();
            expect (said.contains ("Verse ideas"), said);
            expectEquals (engine.getNumLoopBanks(), 2);
            expectEquals (engine.getCurrentLoopBank(), 1);
            for (int i = 0; i < RigLooper::kLayers; ++i)
                expect (looper.getState (i) == RigLooper::State::empty, "a fresh batch");
            expectEquals (looper.getLayer (1).source.load(), (int) RigLooper::line, "sources stay for recording");
            expect (! engine.getLoopFx (0).isActive(), "effects start fresh");
            expect (! looper.getLayer (2).mute.load());

            // Something new in bank 2.
            looper.setLayerAudio (5, constant (0.1f, 48000), 8.0);
            expectEquals (engine.getLoopBanks()[0].loopCount(), 2);
            expectEquals (engine.getLoopBanks()[1].loopCount(), 1);

            engine.switchLoopBank (0);
            expect (looper.getState (0) == RigLooper::State::playing);
            expect (looper.getState (2) == RigLooper::State::playing);
            expect (looper.getState (5) == RigLooper::State::empty);
            expectWithinAbsoluteError (looper.getLayer (0).lengthBeats.load(), 4.0, 1.0e-9);
            expectWithinAbsoluteError (looper.copyLayerAudio (0).getSample (0, 40000), 0.3f, 1.0e-6f);
            expectWithinAbsoluteError (looper.getLayer (0).level.load(), 0.6f, 1.0e-6f);
            expect (looper.getLayer (2).mute.load());
            expect (engine.getLoopFx (0).isActive(), "the effects come back");

            engine.switchLoopBank (1);
            expect (looper.getState (5) == RigLooper::State::playing);
            expect (looper.getState (0) == RigLooper::State::empty);
        }

        beginTest ("while playing, the bank comes in at the next bar; not while a layer records");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            engine.newLoopBank();
            looper.setLayerAudio (1, constant (0.2f, 96000), 4.0);
            engine.getTransport().setBpm (120.0);
            engine.getTransport().play();
            run (engine, 20);   // 0.2 s: inside bar 1
            engine.switchLoopBank (0);
            expectEquals (engine.getQueuedLoopBank(), 0);
            expect (looper.getState (1) == RigLooper::State::playing, "bank 2 plays to the bar");
            run (engine, 200);   // past beat 4
            expect (looper.getState (0) == RigLooper::State::playing);
            expect (looper.getState (1) == RigLooper::State::empty);
            engine.serviceLoopBanks();
            expectEquals (engine.getQueuedLoopBank(), -1);

            looper.pressRecord (3);
            run (engine, 2);
            const auto why = engine.newLoopBank();
            expect (why.contains ("Layer 4"), why);
            expectEquals (engine.getNumLoopBanks(), 2, "refused");
        }

        beginTest ("banks save with the show, their loops in the loops folder");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            setFx (engine.getLoopFx (0), "drive", 0.5f);
            engine.saveLoopBank ("Groove");
            engine.newLoopBank ("Hook");
            looper.setLayerAudio (3, constant (0.2f, 48000), 2.0);

            const auto folder = juce::File::createTempFile ("rigbanks");
            folder.createDirectory();
            const auto showFile = folder.getChildFile ("Bank show.crateshow");
            const auto xml = engine.saveShow().createXml();
            expect (xml->writeTo (showFile));
            expect (engine.writeLoopAudio (showFile));
            expect (RigEngine::loopsFolder (showFile).getChildFile ("Loop 4.wav").existsAsFile(), "the current bank as before");
            expect (RigEngine::loopsFolder (showFile).getChildFile ("Bank 1").getChildFile ("Loop 1.wav").existsAsFile());

            RigEngine other;
            other.prepare (kRate, kBlock);
            other.loadShow (juce::ValueTree::fromXml (*xml));
            other.readLoopAudio (showFile);
            expectEquals (other.getNumLoopBanks(), 2);
            expectEquals (other.getCurrentLoopBank(), 1);
            expectEquals (other.getLoopBankName (0), juce::String ("Groove"));
            expectEquals (other.getLoopBankName (1), juce::String ("Hook"));
            expect (other.getLooper().getState (3) == RigLooper::State::playing);
            other.switchLoopBank (0);
            expect (other.getLooper().getState (0) == RigLooper::State::playing, "Groove's loop came back from its file");
            expect (other.getLooper().getState (3) == RigLooper::State::empty);
            expect (other.getLoopFx (0).isActive());
            folder.deleteRecursively();

            // A show from before banks: one bank, the layers as they were.
            RigEngine old;
            auto tree = engine.saveShow();
            tree.removeChild (tree.getChildWithName ("LoopBanks"), nullptr);
            old.loadShow (tree);
            expectEquals (old.getNumLoopBanks(), 1);
        }

        beginTest ("song parts remember their bank and switch to it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            auto& scenes = engine.getScenes();
            RigScene a;
            a.name = "Verse";
            a.state = engine.captureScene();
            expectEquals (a.state.loopBank, 0);
            scenes.getScenes().push_back (a);
            engine.newLoopBank ("Chorus loops");
            looper.setLayerAudio (1, constant (0.2f, 96000), 4.0);
            RigScene b;
            b.name = "Chorus";
            b.state = engine.captureScene();
            expectEquals (b.state.loopBank, 1);
            scenes.getScenes().push_back (b);

            scenes.launch (0);
            engine.tickScenes();
            run (engine, 1);
            expectEquals (engine.getCurrentLoopBank(), 0, "part A plays bank 1");
            expect (looper.getState (0) == RigLooper::State::playing);
            expect (looper.getState (1) == RigLooper::State::empty);
            scenes.launch (1);
            engine.tickScenes();
            run (engine, 1);
            expectEquals (engine.getCurrentLoopBank(), 1);
            expect (looper.getState (1) == RigLooper::State::playing);

            // The tree keeps it; deleting a bank leaves parts that used it as they are.
            expectEquals (RigScene::fromTree (scenes.getScenes()[1].toTree()).state.loopBank, 1);
            engine.duplicateLoopBank (0);
            expectEquals (scenes.getScenes()[1].state.loopBank, 2, "a copy before it moves it along");
            engine.deleteLoopBank (1);
            expectEquals (scenes.getScenes()[1].state.loopBank, 1);
            expectEquals (scenes.getScenes()[0].state.loopBank, 0);
            engine.deleteLoopBank (0);
            expectEquals (scenes.getScenes()[0].state.loopBank, -1);
        }

        beginTest ("the Looper screen's bank row and the Song builder's bank pick");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.getLooper().setLayerAudio (0, constant (0.3f, 96000), 4.0);
            LooperView view (engine);
            view.setSize (1400, 800);
            expect (view.bankText().contains ("1/1"), view.bankText());
            view.pressNewBank();
            expect (view.bankText().contains ("2/2"), view.bankText());
            expect (engine.getLooper().getState (0) == RigLooper::State::empty);
            view.stepBank (-1);
            expect (view.bankText().contains ("1/2"), view.bankText());
            expect (engine.getLooper().getState (0) == RigLooper::State::playing);
            view.stepBank (-1);   // already the first
            expectEquals (engine.getCurrentLoopBank(), 0);
            engine.saveLoopBank ("Verse ideas");
            engine.renameLoopBank (1, "Chorus batch");
            view.refresh();

            SongView song (engine);
            song.setSize (1200, 700);
            song.addPart();
            expectEquals (engine.getScenes().getScenes()[0].state.loopBank, 0);
            song.setPartLoopBank (0, 1);
            expectEquals (engine.getScenes().getScenes()[0].state.loopBank, 1);
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_LOOP_BANK_SNAPSHOT", {}); path.isNotEmpty())
            {
                const auto save = [] (juce::Component& c, const juce::String& file)
                {
                    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
                    juce::File f (file);
                    f.deleteFile();
                    juce::FileOutputStream stream (f);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                };
                save (view, path + "-looper.png");
                save (song, path + "-song.png");
            }
            song.setPartLoopBank (0, -1);
            expectEquals (engine.getScenes().getScenes()[0].state.loopBank, -1);
        }
    }
};

static RigLoopBankTests rigLoopBankTests;
