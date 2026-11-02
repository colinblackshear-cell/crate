#include "TestHelpers.h"

#include "EditorProcessor.h"
#include "EditorView.h"

using namespace crate;

namespace
{
struct DocumentTests : juce::UnitTest
{
    DocumentTests() : juce::UnitTest ("Edit document", "Editor") {}

    void runTest() override
    {
        beginTest ("edits, undo and redo");
        EditDocument doc;
        expect (! doc.hasAudio());
        expect (! doc.hasUnsavedChanges());

        doc.load (share (ramp (1000)), "first", "Open", {}, true);
        expect (doc.hasAudio());
        expect (! doc.canUndo());
        expect (! doc.hasUnsavedChanges());

        doc.apply (Edits::crop (*doc.getAudio(), { 100, 300 }), "Trim to selection", { 100, 300 });
        expectEquals (doc.getAudio()->getNumSamples(), 200);
        expect (doc.hasUnsavedChanges());
        expectEquals (doc.getUndoName(), juce::String ("Trim to selection"));

        expect (doc.undo());
        expectEquals (doc.getAudio()->getNumSamples(), 1000);
        expect (doc.getSelection() == juce::Range<int> (100, 300), "selection comes back with the undo");
        expect (! doc.hasUnsavedChanges(), "back at the saved version");

        expect (doc.redo());
        expectEquals (doc.getAudio()->getNumSamples(), 200);
        expect (doc.getSelection() == juce::Range<int> (0, 200));

        beginTest ("a new edit after undo drops the redo");
        doc.undo();
        doc.apply (Edits::reverse (*doc.getAudio(), {}), "Reverse", {});
        expect (! doc.canRedo());
        expectEquals (doc.getAudio()->getNumSamples(), 1000);

        beginTest ("recording over a sound is undoable");
        doc.load (share (ramp (10)), "take", "Record");
        expectEquals (doc.getName(), juce::String ("take"));
        expect (doc.canUndo());
        doc.undo();
        expectEquals (doc.getName(), juce::String ("first"), "the name comes back too");
        expectEquals (doc.getAudio()->getNumSamples(), 1000);

        beginTest ("history is capped");
        for (int i = 0; i < EditDocument::kMaxSteps + 20; ++i)
            doc.apply (Edits::gain (*doc.getAudio(), {}, 0.1f), "Gain", {});
        int undos = 0;
        while (doc.undo())
            ++undos;
        expect (undos < EditDocument::kMaxSteps);
        expect (undos > EditDocument::kMaxSteps - 5);
    }
};

struct ProcessorTests : juce::UnitTest
{
    ProcessorTests() : juce::UnitTest ("Sample Editor processor", "Editor") {}

    void runTest() override
    {
        EditorProcessor p;
        const int block = 512;

        beginTest ("records the input at 48 kHz and keeps it as 44.1 kHz mono");
        {
            p.setRateAndBufferSizeDetails (48000.0, block);
            p.prepareToPlay (48000.0, block);
            p.startRecording();
            juce::MidiBuffer midi;
            for (int b = 0; b < 94; ++b)   // ~1 second
            {
                juce::AudioBuffer<float> io (2, block);
                for (int i = 0; i < block; ++i)
                {
                    const float v = 0.5f * std::sin ((float) (b * block + i) * 0.05f);
                    io.setSample (0, i, v);
                    io.setSample (1, i, v);   // a mono mic shows up on both sides
                }
                p.processBlock (io, midi);
                expectEquals (io.getMagnitude (0, block), 0.0f, "the input isn't passed through");
                if (b % 8 == 0)
                    p.collectRecording();
            }
            expect (p.takeInputLevel() > 0.4f);
            auto take = p.stopRecording();
            expectEquals (take.getNumChannels(), 1);
            expect (std::abs (take.getNumSamples() - (int) std::round (94 * block * 44100.0 / 48000.0)) < 4);
            expect (take.getMagnitude (0, take.getNumSamples()) > 0.45f);
        }

        beginTest ("plays the sound, loops it, and stops");
        {
            p.setRateAndBufferSizeDetails (44100.0, block);
            p.prepareToPlay (44100.0, block);
            auto sound = share (ramp (2000));
            p.play (sound, { 0, 2000 }, false);
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> io (2, block);
            io.clear();
            p.processBlock (io, midi);
            expectWithinAbsoluteError (io.getSample (0, 100), sound->getSample (0, 100), 1.0e-5f);
            expectWithinAbsoluteError (io.getSample (1, 100), sound->getSample (0, 100), 1.0e-5f, "mono plays on both sides");
            for (int b = 0; b < 4; ++b)
                p.processBlock (io, midi);
            expect (! p.isPlaying(), "stops at the end");
            expectEquals (p.getPlayPosition(), -1);

            p.play (sound, { 500, 600 }, true);
            for (int b = 0; b < 10; ++b)
                p.processBlock (io, midi);
            expect (p.isPlaying(), "still looping");
            const int pos = p.getPlayPosition();
            expect (pos >= 500 && pos < 600);

            p.stop();
            p.processBlock (io, midi);
            expect (! p.isPlaying(), "stop fades out within a block");
        }

        beginTest ("the sound is saved with the project");
        {
            p.getDocument().load (share (sine (4410, 2)), "my sound", "Record");
            juce::MemoryBlock state;
            p.getStateInformation (state);

            EditorProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            expect (restored.getDocument().hasAudio());
            expectEquals (restored.getDocument().getName(), juce::String ("my sound"));
            expectEquals (restored.getDocument().getAudio()->getNumSamples(), 4410);
            expectEquals (restored.getDocument().getAudio()->getNumChannels(), 2);
        }

        beginTest ("the window's edit buttons change the sound");
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<EditorView*> (editor.get());
            expect (view != nullptr);
            if (view != nullptr)
            {
                auto& doc = p.getDocument();
                const int before = doc.getAudio()->getNumSamples();
                view->getWaveView().setSelection ({ 1000, 2000 });
                view->runEdit ("crop");
                expectEquals (doc.getAudio()->getNumSamples(), 1000, "trim to selection");
                view->runEdit ("undo");
                expectEquals (doc.getAudio()->getNumSamples(), before);

                view->getWaveView().setSelection ({ 0, 441 });
                view->runEdit ("fadeIn");
                expect (doc.getAudio()->getMagnitude (0, 20) < 0.01f);
                view->runEdit ("copy");
                expect (p.clipboard.getNumSamples() > 400);
                view->getWaveView().setSelection ({ 0, 0 });
                view->runEdit ("paste");
                expect (doc.getAudio()->getNumSamples() > before);
                view->runEdit ("reverse");
                view->runEdit ("normalize");
                expectWithinAbsoluteError (doc.getAudio()->getMagnitude (0, doc.getAudio()->getNumSamples()),
                                           juce::Decibels::decibelsToGain (-0.3f), 0.01f);

                // CRATE_SNAPSHOT=/path/to.png saves a picture of the window, to check the layout
                if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_SNAPSHOT", {}); path.isNotEmpty())
                {
                    view->getWaveView().setSelection ({ 800, 2400 });
                    view->getEffectsPanel().loadPreset ("Dub echo");
                    view->runEdit ("refresh");
                    auto image = view->createComponentSnapshot (view->getLocalBounds(), true, 1.0f);
                    juce::File (path).deleteFile();
                    juce::FileOutputStream out { juce::File (path) };
                    juce::PNGImageFormat().writeImageToStream (image, out);
                    view->getEffectsPanel().reset();
                }

                // fades with nothing selected use the Length setting; Amount sets how far they drop
                {
                    p.getDocument().load (share (sine (44100)), "fade test", "Open");
                    view->getWaveView().setSelection ({ 0, 0 });
                    view->getFadeLengthSlider().setValue (100.0);   // ms
                    view->getFadeAmountSlider().setValue (50.0);    // %
                    view->runEdit ("fadeOut");
                    const auto& faded = *doc.getAudio();
                    expectEquals (faded.getNumSamples(), 44100);
                    expect (faded.getMagnitude (0, 40000) > 0.49f, "only the last 100 ms is faded");
                    const float endLevel = faded.getMagnitude (44100 - 200, 200);
                    expect (endLevel > 0.2f && endLevel < 0.3f, "half way down, not silent: " + juce::String (endLevel));
                }

                // effects: a preset, then apply to the whole sound; the panel resets afterwards
                {
                    auto& fx = view->getEffectsPanel();
                    expect (! fx.hasEffects());
                    for (const auto& name : crate::EffectsPanel::presetNames())
                    {
                        expect (fx.loadPreset (name), name);
                        expect (fx.hasEffects(), name);
                    }
                    expect (fx.loadPreset ("Chipmunk"));
                    expectWithinAbsoluteError (fx.getSettings().pitch, 12.0f, 0.01f);
                    const int undoBefore = doc.canUndo() ? 1 : 0;
                    view->runEdit ("applyEffects");
                    expect (doc.canUndo() && undoBefore == 1);
                    expectEquals (doc.getUndoName(), juce::String ("Effects"));
                    expect (! fx.hasEffects(), "panel resets after applying");

                    // effects on part of the sound
                    expect (fx.loadPreset ("Crushed"));
                    const int len = doc.getAudio()->getNumSamples();
                    view->getWaveView().setSelection ({ 1000, 5000 });
                    view->runEdit ("applyEffects");
                    expectEquals (doc.getAudio()->getNumSamples(), len, "no tail on a crush");
                    fx.reset();
                    expect (! fx.hasEffects());
                }

                // a move that needs a selection does nothing without one
                const int len = doc.getAudio()->getNumSamples();
                view->getWaveView().setSelection ({ 10, 10 });
                view->runEdit ("delete");
                expectEquals (doc.getAudio()->getNumSamples(), len);
            }
        }
    }
};

DocumentTests documentTests;
ProcessorTests processorTests;
}
