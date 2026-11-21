#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigLooper.h"
#include "LoopEditor.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBeat = 24000;   // one beat at 120 bpm, 48 kHz
constexpr int kSamples = 4 * kBeat;

int peakAt (const juce::AudioBuffer<float>& b)
{
    int at = 0;
    float best = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
        if (std::abs (b.getSample (0, i)) > best)
        {
            best = std::abs (b.getSample (0, i));
            at = i;
        }
    return at;
}
}

struct LoopShapeTests : juce::UnitTest
{
    LoopShapeTests() : juce::UnitTest ("Loop shape", "Rig") {}

    void runTest() override
    {
        beginTest ("slip, nudge, reverse, level and fades shape the loop without changing its length");
        {
            RigEngine engine;
            engine.prepare (kRate, 480);
            auto& looper = engine.getLooper();
            juce::AudioBuffer<float> impulse (2, kSamples);
            impulse.clear();
            impulse.setSample (0, 0, 0.5f);
            impulse.setSample (1, 0, 0.5f);
            looper.setLayerAudio (0, impulse, 4.0);

            LoopEditor editor (engine, 0);
            expect (editor.isValid());
            const auto audio = [&] { return looper.copyLayerAudio (0); };
            const auto lengthOk = [&] (const char* what)
            {
                expectWithinAbsoluteError (looper.getLayer (0).lengthBeats.load(), 4.0, 1.0e-9, what);
                expectEquals (audio().getNumSamples(), kSamples, what);
            };

            editor.slip (1);   // snap to beats: one beat
            expectEquals (peakAt (audio()), kBeat, "slip by 1 beat");
            expectEquals (LoopEditor::slipText (1.0), juce::String ("Slip +1 beat"));
            expectEquals (LoopEditor::slipText (0.25), juce::String ("Slip +0.25 beats"));
            lengthOk ("slip");
            editor.resetShape();
            expectEquals (peakAt (audio()), 0, "reset restores it");

            RigLooper::Shape s;
            s.nudgeMs = -10;
            editor.setShape (s);
            expectEquals (peakAt (audio()), kSamples - 480, "nudge -10 ms wraps 480 samples earlier");
            expectEquals (LoopEditor::nudgeText (-10), juce::String ("Nudge -10 ms (earlier)"));
            editor.resetShape();
            editor.nudgeAudio (1);
            editor.nudgeAudio (1);
            expectEquals (peakAt (audio()), 480, "two 5 ms clicks later");
            lengthOk ("nudge");
            editor.resetShape();

            s = {};
            s.reversed = true;
            editor.setShape (s);
            expectEquals (peakAt (audio()), kSamples - 1, "reverse puts it at the end");
            lengthOk ("reverse");

            s = {};
            s.gainDb = 6.0f;
            editor.setShape (s);
            expectWithinAbsoluteError (audio().getSample (0, 0), 0.5f * 1.9953f, 0.01f, "+6 dB is about double");
            lengthOk ("level");

            // A steady tone for the fades.
            juce::AudioBuffer<float> tone (2, kSamples);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < kSamples; ++i)
                    tone.setSample (ch, i, 0.5f);
            looper.setLayerAudio (1, tone, 4.0);
            LoopEditor second (engine, 1);
            s = {};
            s.fadeInBeats = 1.0;
            s.fadeOutBeats = 1.0;
            second.setShape (s);
            const auto faded = looper.copyLayerAudio (1);
            expectWithinAbsoluteError (faded.getSample (0, 0), 0.0f, 1.0e-3f, "fade in starts silent");
            expectWithinAbsoluteError (faded.getSample (0, kBeat / 2), 0.25f, 0.01f, "halfway up");
            expectWithinAbsoluteError (faded.getSample (0, 2 * kBeat), 0.5f, 1.0e-4f, "full in the middle");
            expectWithinAbsoluteError (faded.getSample (0, kSamples - 1), 0.0f, 1.0e-3f, "fade out ends silent");
            expectWithinAbsoluteError (looper.getLayer (1).lengthBeats.load(), 4.0, 1.0e-9);
            second.resetShape();
            expectWithinAbsoluteError (looper.copyLayerAudio (1).getSample (0, 0), 0.5f, 1.0e-4f, "reset restores");

            // The shape survives a new start and end; Whole loop only resets the range.
            s = {};
            s.reversed = true;
            editor.setShape (s);
            editor.setRange (0.0, 2.0);
            expectEquals (looper.copyLayerAudio (0).getNumSamples(), 2 * kBeat);
            expect (looper.getTrim (0).shape.reversed, "trimming keeps the shape");
            editor.resetShape();
            editor.setRange (0.0, 4.0);
            expectEquals (peakAt (audio()), 0);
            expect (looper.getTrim (0).shape.isDefault());
        }
    }
};

static LoopShapeTests loopShapeTests;
