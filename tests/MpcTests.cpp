#include "TestHelpers.h"

#include "MpcProcessor.h"
#include "PadSoundEditor.h"
#include "MpcView.h"
#include "SampleTools.h"
#include "core/AudioFiles.h"

using namespace crate;

namespace
{
// Clicks every `gap` samples on silence: easy hits to find.
juce::AudioBuffer<float> clicks (int count, int gap)
{
    juce::AudioBuffer<float> b (1, count * gap);
    b.clear();
    for (int c = 0; c < count; ++c)
        for (int i = 0; i < 400; ++i)
            b.setSample (0, c * gap + i, 0.8f * std::exp (-(float) i / 80.0f) * std::sin ((float) i * 0.3f));
    return b;
}

bool near (double a, double b, double within = 1.0e-9) { return std::abs (a - b) <= within; }

struct KitExportTests : juce::UnitTest
{
    KitExportTests() : juce::UnitTest ("Kits and export", "MPC") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-mpc-test");
        dir.deleteRecursively();
        dir.createDirectory();

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();

        beginTest ("load sounds");
        auto kickFile = dir.getChildFile ("Kick Drum.wav");
        AudioFiles::write (kickFile, sine (44100, 2));
        auto snareFile = dir.getChildFile ("snare.wav");
        AudioFiles::write (snareFile, sine (1000, 1, 0.2f));
        juce::String error;
        auto stereo = SampleTools::load (kickFile, formats, error);
        auto mono = SampleTools::load (snareFile, formats, error);
        expect (stereo != nullptr && stereo->getNumChannels() == 2 && stereo->getNumSamples() == 44100, error);
        expect (mono != nullptr && mono->getNumChannels() == 1 && mono->getNumSamples() == 1000);
        expectEquals (SampleTools::expandDroppedFiles ({ dir.getFullPathName() }, formats).size(), 2);
        if (stereo == nullptr || mono == nullptr)
            return;

        beginTest ("export a kit and open it again");
        Bank bank;
        bank.name = "Test Kit";
        bank.pads[0].audio = stereo;
        bank.pads[0].name = "Kick Drum";
        bank.pads[0].tune = -25;
        bank.pads[1].audio = stereo;
        bank.pads[1].name = "Kick Drum";
        bank.pads[1].mono = true;
        bank.pads[1].reverse = true;
        bank.pads[17].audio = mono;
        bank.pads[17].name = "snare";
        bank.pads[17].mode = PadMode::noteOff;
        bank.pads[17].normalize = true;

        expectEquals (bank.usedPads(), 3);
        expect (bank.memoryBytes() == (size_t) stereo->getNumSamples() * 6 + 2000);
        expect (! SampleTools::renderPad (bank.pads[1]).isStereo());

        auto normalized = SampleTools::renderPad (bank.pads[17]);
        int peak = 0;
        for (auto s : normalized.left)
            peak = std::max (peak, std::abs ((int) s));
        expectGreaterThan (peak, 31000);

        std::vector<std::string> taken;
        auto files = mpc::buildBank (SampleTools::toMpcBank (bank), taken);
        auto card = dir.getChildFile ("card");
        expect (mpc::writeBankFolder (card.getFullPathName().toStdString(), files).ok);

        auto folder = card.getChildFile ("TESTKIT");
        auto pgm = folder.getChildFile ("TESTKIT.PGM");
        expect (pgm.existsAsFile());
        expect (folder.getChildFile ("KICKDRUM.SND").existsAsFile());
        expect (folder.getChildFile ("KICKDR_2.SND").existsAsFile());
        expect (folder.getChildFile ("SNARE.SND").existsAsFile());

        juce::StringArray missing;
        auto opened = SampleTools::importProgram (pgm, missing, error);
        expect (opened.has_value() && missing.isEmpty());
        if (opened)
        {
            expectEquals (opened->name, juce::String ("Test Kit"));
            expectEquals (opened->usedPads(), 3);
            expectEquals (opened->pads[0].tune, -25);
            expectEquals (opened->pads[0].audio->getNumChannels(), 2);
            expectEquals (opened->pads[1].audio->getNumChannels(), 1);
            expect (opened->pads[17].mode == PadMode::noteOff);
            expectEquals (opened->pads[17].audio->getNumSamples(), 1000);
        }

        expect (padLabel (0) == "A01" && padLabel (17) == "B02" && padLabel (63) == "D16");
        expect (noteForPad (0) == 37 && padForNote (37) == 0 && padForNote (20) == -1);

        beginTest ("keep in time");
        Bank tempo;
        tempo.bpm = 100.0;
        auto loop = share (clicks (4, 11025));   // 1 s, 4 hits
        tempo.pads[0].audio = loop;
        tempo.pads[0].name = "loop";
        tempo.pads[1].audio = loop;
        tempo.pads[1].name = "free";
        tempo.pads[1].playNormal = true;
        tempo.pads[2].audio = share (clicks (1, 2000));
        tempo.pads[2].name = "hit";

        expect (tempo.audioToPlay (0) == loop, "off: plays as recorded");
        tempo.keepInTime = true;
        expect (near (tempo.beatsFor (0), 2.0), "1 s at 100 BPM is closest to 2 beats");
        expect (near (tempo.beatsFor (1), 0.0), "play normal");
        expect (near (tempo.beatsFor (2), 0.0), "short hits aren't stretched");
        expect (near (tempo.audioToPlay (0)->getNumSamples(), 52920, 1));
        expect (tempo.audioToPlay (0) == tempo.audioToPlay (0), "cached");
        expect (tempo.audioToPlay (1) == loop);

        tempo.pads[0].beats = 4.0;
        taken.clear();
        auto tempoFiles = mpc::buildBank (SampleTools::toMpcBank (tempo), taken);
        expectEquals ((int) tempoFiles.sounds.size(), 3);
        if (tempoFiles.sounds.size() == 3)
        {
            expect (near ((double) tempoFiles.sounds[0].frameCount(), 105840, 1));
            expectEquals ((int) tempoFiles.sounds[0].beatCount, 4);
            expectEquals ((int) tempoFiles.sounds[1].frameCount(), 44100);
        }

        beginTest ("chop");
        expectEquals ((int) SampleTools::chop (loop, 0).size(), 4, "one slice per hit");
        auto even = SampleTools::chop (loop, 8);
        expectEquals ((int) even.size(), 8);
        int total = 0;
        for (const auto& s : even)
            total += s->getNumSamples();
        expectEquals (total, loop->getNumSamples(), "slices add up to the whole sound");

        dir.deleteRecursively();
    }
};

struct ProcessorTests : juce::UnitTest
{
    ProcessorTests() : juce::UnitTest ("MPC plugin", "MPC") {}

    static float peakOf (const juce::AudioBuffer<float>& b) { return b.getMagnitude (0, b.getNumSamples()); }

    void runTest() override
    {
        beginTest ("MIDI notes play pads");
        MpcProcessor processor;
        processor.setPlayConfigDetails (0, 2, 44100.0, 512);
        processor.prepareToPlay (44100.0, 512);

        auto& bank = processor.getProject().currentBank();
        bank.pads[0].audio = share (sine (44100, 1, 0.5f));
        bank.pads[0].name = "tone";
        bank.pads[5].audio = share (sine (44100, 1, 0.5f));
        bank.pads[5].mode = PadMode::noteOff;
        processor.getProject().changed();
        processor.getProject().dispatchPendingMessages();   // the kit is rebuilt on change

        juce::AudioBuffer<float> out (2, 512);
        juce::MidiBuffer midi;
        processor.processBlock (out, midi);
        expect (peakOf (out) < 1.0e-6f, "silent before any note");

        midi.addEvent (juce::MidiMessage::noteOn (1, noteForPad (0), 1.0f), 100);
        processor.processBlock (out, midi);
        expect (out.getMagnitude (0, 0, 100) < 1.0e-6f, "starts at the note's position in the block");
        expect (peakOf (out) > 0.3f);
        expect (midi.isEmpty(), "an instrument doesn't pass MIDI on");
        expect ((processor.takePlayedPads() & 1u) != 0, "the pad lights");

        beginTest ("a note no pad uses is ignored");
        processor.stopAll();
        for (int i = 0; i < 4; ++i)
            processor.processBlock (out, midi);
        midi.addEvent (juce::MidiMessage::noteOn (1, 20, 1.0f), 0);
        processor.processBlock (out, midi);
        expect (peakOf (out) < 1.0e-6f);

        beginTest ("note-off pads stop when released");
        midi.addEvent (juce::MidiMessage::noteOn (1, noteForPad (5), 1.0f), 0);
        processor.processBlock (out, midi);
        expect (peakOf (out) > 0.3f);
        midi.addEvent (juce::MidiMessage::noteOff (1, noteForPad (5)), 0);
        processor.processBlock (out, midi);
        processor.processBlock (out, midi);
        expect (peakOf (out) < 1.0e-6f);

        beginTest ("the kit is saved with the project");
        bank.name = "Saved";
        bank.bpm = 120.0;
        bank.keepInTime = true;
        bank.pads[0].volume = 70;
        bank.pads[5].pan = 20;
        bank.pads[9].audio = bank.pads[0].audio;   // shared sounds are saved once
        const auto toneSample = bank.pads[0].audio->getSample (0, 1000);
        processor.getProject().addBank ("Second");   // (moves `bank`)

        juce::MemoryBlock state;
        processor.getStateInformation (state);

        MpcProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        auto& project = other.getProject();
        expectEquals ((int) project.getBanks().size(), 2);
        expectEquals (project.getCurrentIndex(), 1);
        const auto& saved = project.getBanks()[0];
        expectEquals (saved.name, juce::String ("Saved"));
        expect (near (saved.bpm, 120.0) && saved.keepInTime);
        expectEquals (saved.usedPads(), 3);
        expectEquals (saved.pads[0].volume, 70);
        expectEquals (saved.pads[5].pan, 20);
        expect (saved.pads[5].mode == PadMode::noteOff);
        expect (saved.pads[0].audio == saved.pads[9].audio);
        expect (near (saved.pads[0].audio->getSample (0, 1000), toneSample, 1.0e-5));

        beginTest ("hear a sound before it's on a pad");
        processor.stopAll();
        for (int i = 0; i < 4; ++i)
            processor.processBlock (out, midi);
        expect (processor.getPreviewPosition() < 0);
        processor.preview (share (sine (44100, 1, 0.5f)), { 1000, 2000 });
        processor.processBlock (out, midi);
        expect (peakOf (out) > 0.3f, "the preview plays");
        expectEquals (processor.getPreviewPosition(), 1512);
        processor.processBlock (out, midi);
        processor.processBlock (out, midi);
        expect (processor.getPreviewPosition() < 0, "and stops at the end of the selection");
        processor.processBlock (out, midi);
        expect (peakOf (out) < 1.0e-6f);
        processor.preview (share (sine (44100, 1, 0.5f)), {});
        processor.processBlock (out, midi);
        processor.stopPreview();
        for (int i = 0; i < 4; ++i)
            processor.processBlock (out, midi);
        expect (peakOf (out) < 1.0e-6f, "Stop stops it");

        beginTest ("trim and put effects on a pad's sound");
        {
            auto& pads = processor.getProject().currentBank().pads;
            pads[2].audio = share (sine (44100, 1, 0.5f));
            const auto original = pads[2].audio;
            PadSoundEditor soundEditor (processor);
            soundEditor.setSize (912, 650);
            soundEditor.open (2);

            auto findButton = [&soundEditor] (const juce::String& text) -> juce::Button&
            {
                for (auto* c : soundEditor.getChildren())
                    if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == text)
                        return *b;
                for (auto* c : soundEditor.getChildren())
                    for (auto* inner : c->getChildren())
                        if (auto* b = dynamic_cast<juce::TextButton*> (inner); b != nullptr && b->getButtonText() == text)
                            return *b;
                jassertfalse;
                return *static_cast<juce::Button*> (nullptr);
            };
            crate::WaveView* wave = nullptr;
            crate::EffectsPanel* fx = nullptr;
            for (auto* c : soundEditor.getChildren())
            {
                if (auto* w = dynamic_cast<crate::WaveView*> (c)) wave = w;
                if (auto* e = dynamic_cast<crate::EffectsPanel*> (c)) fx = e;
            }
            expect (wave != nullptr && fx != nullptr);

            wave->setSelection ({ 11025, 22050 });
            findButton ("Trim").onClick();
            expectEquals (pads[2].audio->getNumSamples(), 11025);
            expect (near (pads[2].audio->getSample (0, 0), original->getSample (0, 11025), 1.0e-6));

            findButton ("Undo").onClick();
            expect (pads[2].audio == original, "undo brings the whole sound back");
            findButton ("Redo").onClick();
            expectEquals (pads[2].audio->getNumSamples(), 11025);

            wave->setSelection ({});
            findButton ("Fade out").onClick();
            const auto* faded = pads[2].audio->getReadPointer (0);
            expect (std::abs (faded[11024]) < 0.01f, "the fade ends silent");
            expect (std::abs (faded[1000]) > 0.0f);

            wave->setSelection ({});   // a fade selects what it faded
            findButton ("Reverse").onClick();
            expect (std::abs (pads[2].audio->getSample (0, 0)) < 0.01f, "the faded end is now at the start");

            expect (fx->loadPreset ("Punchy"), "a preset switches effects on");
            const auto before = pads[2].audio;
            findButton ("Undo").onClick();
            findButton ("Redo").onClick();
            wave->selectAll();
            fx->onApply();
            expect (pads[2].audio != before, "effects are written into the pad");
            expect (! fx->hasEffects(), "and the panel resets after");

            if (auto* path = std::getenv ("CRATE_PAD_EDITOR_SNAPSHOT"))
            {
                wave->setSelection ({ 2000, 6000 });
                fx->loadPreset ("Dub echo");
                auto image = soundEditor.createComponentSnapshot (soundEditor.getLocalBounds());
                juce::File file { juce::String { path } };
                file.deleteFile();
                juce::FileOutputStream stream { file };
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            processor.getProject().dispatchPendingMessages();
            pads[2] = {};   // cleared from outside the editor
            soundEditor.sync();
            findButton ("Undo").onClick();
            expect (pads[2].isEmpty(), "undo doesn't bring back a sound that was cleared elsewhere");
        }

        beginTest ("the window opens");
        std::unique_ptr<juce::AudioProcessorEditor> view (processor.createEditor());
        expect (view != nullptr && view->getWidth() > 0);
        if (auto* path = std::getenv ("CRATE_MPC_SNAPSHOT"))
        {
            view->setSize (1180, 740);
            auto image = view->createComponentSnapshot (view->getLocalBounds());
            juce::FileOutputStream stream { juce::File { juce::String { path } } };
            stream.setPosition (0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream (image, stream);
        }
    }
};

KitExportTests kitExportTests;
ProcessorTests processorTests;
}
