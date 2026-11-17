#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "LooperView.h"
#include "SamplerScreen.h"
#include "MpcView.h"

// Starting fresh: Clear all on the Looper (the loops and their effects, with Undo), and taking
// a sound off an MPC pad (Remove, with Undo).
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

void setFx (StripFx& fx, const char* id, float value)
{
    auto* p = fx.params.getParameter (id);
    p->setValueNotifyingHost (p->convertTo0to1 (value));
}

template <typename T>
T* find (juce::Component& c, std::function<bool (T&)> wanted = {})
{
    if (auto* t = dynamic_cast<T*> (&c); t != nullptr && (! wanted || wanted (*t)))
        return t;
    for (auto* child : c.getChildren())
        if (auto* t = find<T> (*child, wanted))
            return t;
    return nullptr;
}

juce::TextButton* button (juce::Component& c, const juce::String& text)
{
    return find<juce::TextButton> (c, [&text] (juce::TextButton& b) { return b.getButtonText() == text && b.isVisible(); });
}
}

struct RigClearTests : juce::UnitTest
{
    RigClearTests() : juce::UnitTest ("Rig: clear all loops, remove a pad's sound", "Rig") {}

    void runTest() override
    {
        beginTest ("Clear all empties every layer and takes their effects off; Undo brings it all back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, constant (0.3f, 96000), 4.0);
            looper.setLayerAudio (3, constant (0.2f, 48000), 2.0);
            looper.getLayer (0).level = 0.5f;
            looper.getLayer (3).mute = true;
            looper.getLayer (3).source = RigLooper::vocoder;
            looper.getLayer (3).bars = 2;
            setFx (engine.getLoopFx (0), "drive", 0.7f);
            setFx (engine.getLoopFx (5), "drive", 0.4f);   // effects on a layer with no loop
            engine.setLoopName (0, "Drums");
            expect (engine.getLoopFx (0).isActive() && engine.getLoopFx (5).isActive());

            auto text = engine.clearAllLoops();
            expect (text.contains ("2 loops"), text);
            for (int i = 0; i < RigLooper::kLayers; ++i)
            {
                expect (looper.getState (i) == RigLooper::State::empty, "layer " + juce::String (i + 1) + " empty");
                expect (! engine.getLoopFx (i).isActive(), "and its effects off");
                expect (! looper.getLayer (i).mute.load());
            }
            expectWithinAbsoluteError (looper.getLayer (0).level.load(), 0.8f, 1.0e-6f, "level as a new layer has it");
            expectEquals (engine.getLoopName (0), juce::String());
            expectEquals (looper.getLayer (3).source.load(), (int) RigLooper::vocoder, "sources stay, to record the same way");
            expectEquals (looper.getLayer (3).bars.load(), 2);
            expect (engine.canUndoClearAllLoops());

            text = engine.undoClearAllLoops();
            expect (text.contains ("2 loops"), text);
            expect (looper.getState (0) == RigLooper::State::playing && looper.getState (3) == RigLooper::State::playing);
            expectWithinAbsoluteError (looper.getLayer (0).lengthBeats.load(), 4.0, 1.0e-9);
            expectWithinAbsoluteError (looper.getLayer (0).level.load(), 0.5f, 1.0e-6f);
            expect (looper.getLayer (3).mute.load());
            expect (engine.getLoopFx (0).isActive() && engine.getLoopFx (5).isActive(), "the effects come back");
            expectEquals (engine.getLoopName (0), juce::String ("Drums"));
            expect (! engine.canUndoClearAllLoops(), "one undo");

            // Once something new is on a layer, Undo never records over it.
            engine.clearAllLoops();
            looper.setLayerAudio (2, constant (0.1f, 48000), 2.0);
            expect (! engine.canUndoClearAllLoops());
            engine.undoClearAllLoops();
            expect (looper.getState (0) == RigLooper::State::empty);
            expect (looper.getState (2) == RigLooper::State::playing, "the new loop stays");

            // Nothing there: it says so.
            engine.clearAllLoops();
            expect (engine.clearAllLoops().contains ("already empty"));
        }

        beginTest ("the Looper screen's Clear all button, and Undo taking it back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (1, constant (0.3f, 96000), 4.0);
            setFx (engine.getLoopFx (1), "drive", 0.6f);
            LooperView view (engine);
            view.setSize (1400, 800);
            juce::String said;
            view.onSay = [&said] (const juce::String& s) { said = s; };
            auto* clearAll = button (view, "Clear all loops");
            expect (clearAll != nullptr && clearAll->getWidth() > 0, "a Clear all loops button on the bank row");
            if (clearAll == nullptr)
                return;
            clearAll->onClick();   // as a click does (triggerClick waits for the message loop)
            expect (looper.getState (1) == RigLooper::State::empty);
            expect (! engine.getLoopFx (1).isActive());
            expect (said.contains ("Cleared all"), said);
            view.pressUndo();
            expect (looper.getState (1) == RigLooper::State::playing);
            expect (engine.getLoopFx (1).isActive());
        }

        beginTest ("Remove takes the sound off the picked MPC pad; Undo puts it back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Sampler");
            auto* screen = find<SamplerScreen> (view);
            expect (screen != nullptr);
            if (screen == nullptr)
                return;
            auto& project = engine.getMpc().getProject();
            auto& kit = project.currentBank();
            kit.pads[2].audio = crate::share (sine (4000, 1, 0.5f, 220.0));
            kit.pads[2].name = "Snare";
            kit.pads[2].tune = 30;
            project.changed();
            screen->pressPad (2);
            expectEquals (screen->getSelectedPad(), 2);
            expect (button (*screen, "Remove A03") != nullptr, "a Remove button over the pads, naming the pad");

            screen->pressRemovePad();
            expect (project.currentBank().pads[2].isEmpty(), "the pad is empty");
            expect (screen->canUndoRemovePad());

            screen->pressUndoRemovePad();
            expect (! project.currentBank().pads[2].isEmpty());
            expectEquals (project.currentBank().pads[2].name, juce::String ("Snare"));
            expectEquals ((int) project.currentBank().pads[2].tune, 30, "with its settings");
            expect (! screen->canUndoRemovePad());

            // Undo is for that pad only, while it's still empty.
            screen->pressRemovePad();
            screen->pressPad (3);
            expect (! screen->canUndoRemovePad());
            screen->pressPad (2);
            expect (screen->canUndoRemovePad());
            project.currentBank().pads[2].audio = crate::share (sine (2000));
            expect (! screen->canUndoRemovePad(), "something new on the pad");

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_CLEAR_SNAPSHOT", {}); path.isNotEmpty())
            {
                const auto snap = [] (juce::Component& c, const juce::String& file)
                {
                    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
                    juce::File f (file);
                    f.deleteFile();
                    juce::FileOutputStream stream (f);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                };
                for (int i = 0; i < 16; ++i)
                {
                    kit.pads[(size_t) i].audio = crate::share (sine (20000, 1, 0.4f, 110.0 * (1 + i % 5)));
                    kit.pads[(size_t) i].name = juce::StringArray { "Kick", "Snare", "Hat", "Clap", "Bass" }[i % 5] + " " + juce::String (i + 1);
                }
                project.changed();
                project.dispatchPendingMessages();
                screen->pressPad (4);
                screen->resized();
                snap (view, path + "-pad-remove.png");
                screen->pressRemovePad();
                snap (view, path + "-pad-undo.png");

                engine.getLooper().setLayerAudio (0, constant (0.3f, 96000), 4.0);
                engine.getLooper().setLayerAudio (1, constant (0.2f, 96000), 4.0);
                setFx (engine.getLoopFx (0), "drive", 0.6f);
                view.goTo ("Looper");
                snap (view, path + "-looper.png");
            }
        }
    }
};

static RigClearTests rigClearTests;
