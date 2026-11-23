#include "TestHelpers.h"

#include "RigEngine.h"
#include "SamplerScreen.h"
#include "EditorView.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

void runBlocks (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
}

// Two seconds of quiet with a short burst every quarter second: eight clear hits.
crate::SharedAudio hits()
{
    juce::AudioBuffer<float> b (1, 88200);
    b.clear();
    juce::Random random (7);
    for (int hit = 0; hit < 8; ++hit)
        for (int i = 0; i < 2000; ++i)
            b.setSample (0, hit * 11025 + i, (random.nextFloat() * 2.0f - 1.0f) * (1.0f - i / 2000.0f));
    return crate::share (std::move (b));
}
}

struct RigSamplerScreenTests : juce::UnitTest
{
    RigSamplerScreenTests() : juce::UnitTest ("Rig Sampler and MPC screen", "Rig") {}

    void runTest() override
    {
        beginTest ("chop a sound, put the slices on the pads, set a pad, send");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            std::unique_ptr<juce::AudioProcessorEditor> editor (engine.getSampler().createEditorAndMakeActive());
            auto* full = dynamic_cast<EditorView*> (editor.get());
            expect (full != nullptr);
            SamplerScreen screen (engine, *full);
            screen.setSize (1400, 560);

            expectEquals (screen.numSlices(), 0, "no sound yet");
            screen.pressChop (SamplerScreen::even16);
            expectEquals (screen.numSlices(), 0, "nothing to chop");

            engine.getSampler().getDocument().load (hits(), "Dusty break", "Load");
            screen.pressChop (SamplerScreen::even16);
            expectEquals (screen.numSlices(), 16, "Even 16");
            screen.pressChop (SamplerScreen::transients);
            expectEquals (screen.numSlices(), 8, "Transients finds the eight hits");

            // By hand: add, move and remove markers.
            screen.addMarker (5000);
            expectEquals (screen.numSlices(), 9);
            expectEquals (screen.sliceRange (1).getStart(), 5000);
            screen.moveMarker (0, 6000);
            expectEquals (screen.sliceRange (1).getStart(), 6000, "dragged");
            screen.moveMarker (0, 50000);
            expect (screen.sliceRange (1).getStart() < screen.sliceRange (2).getStart(), "a marker stays before the next one");
            screen.removeMarker (0);
            expectEquals (screen.numSlices(), 8);

            // To pads: a new kit with slice 1 on A01.
            screen.pressToPads();
            auto& kit = engine.getMpc().getProject().currentBank();
            expectEquals (kit.usedPads(), 8);
            expect (kit.name.contains ("Dusty"), kit.name);
            expectEquals (engine.getMpc().padBankOnScreen.load(), 0);

            // Picking slice 6 picks pad A06, and its knobs set that pad.
            screen.selectSlice (5);
            expectEquals (screen.getSelectedPad(), 5);
            screen.knobs[0]->slider.setValue (3.0, juce::sendNotificationSync);
            screen.knobs[1]->slider.setValue (80.0, juce::sendNotificationSync);
            screen.knobs[3]->slider.setValue (2.0, juce::sendNotificationSync);
            screen.knobs[4]->slider.setValue (4.0, juce::sendNotificationSync);
            expectEquals (kit.pads[5].tune, 30, "Pitch +3 st");
            expectEquals (kit.pads[5].volume, 80);
            expect (kit.pads[5].mode == PadMode::noteOff, "Mode");
            expectEquals (kit.pads[5].muteGroup, 4, "Choke group");
            expectEquals (kit.pads[4].tune, 0, "the others stay put");

            // A click on a pad picks it (and plays it).
            screen.pressPad (2);
            expectEquals (screen.getSelectedPad(), 2);
            // The keyboard's drum pads play it, and both the MPC's window and this screen see it light.
            engine.getMpc().getProject().dispatchPendingMessages();   // the MPC picks up the new kit
            engine.addMidiNow (juce::MidiMessage::noteOn (10, 38, 1.0f));
            runBlocks (engine, 4);
            engine.getMpc().releasePad (2);
            expect ((engine.getMpc().takePlayedPads (1) & 4u) != 0, "the screen sees pad A03");
            expect ((engine.getMpc().takePlayedPads (0) & 4u) != 0, "so does the MPC's window");

            // Bank B: the picked pad moves with it.
            screen.pressBank (1);
            expectEquals (screen.getSelectedPad(), 18);
            expectEquals (engine.getMpc().padBankOnScreen.load(), 1);
            expectEquals (engine.getRouting().padBank.load(), 1, "the keyboard's drum pads follow");

            // Full level.
            engine.getMpc().fullLevel = true;
            screen.refresh();

            // Send: a floppy is too small for big kits, a card isn't.
            expect (SamplerScreen::capacityOf (2) < (size_t) 2 * 1024 * 1024);
            expectEquals ((int) SamplerScreen::capacityOf (0), 0, "a CF card holds whatever the MPC can");
            int sends = 0;
            juce::String said;
            screen.onSendToMpc = [&] { ++sends; };
            screen.onSay = [&] (const juce::String& s) { said = s; };
            screen.medium = 0;
            juce::Image image (juce::Image::ARGB, 1400, 560, true);
            juce::Graphics g (image);
            screen.paintEntireComponent (g, false);   // draws with sounds, slices and pads
            for (auto* c : screen.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Send to MPC")
                    b->onClick();
            expectEquals (sends, 1, "Send to MPC writes the kits");
            engine.getMpc().fullLevel = false;

            // FX: the same knob row turns the picked pad's live sound.
            screen.pressBank (0);
            screen.selectSlice (5);
            expect (! screen.isShowingFxKnobs());
            screen.showFxKnobs (true);
            expect (screen.fxKnobs[0]->isVisible() && ! screen.knobs[0]->isVisible());
            screen.fxKnobs[0]->slider.setValue (800.0, juce::sendNotificationSync);
            screen.fxKnobs[2]->slider.setValue (250.0, juce::sendNotificationSync);
            screen.fxKnobs[3]->slider.setValue (30.0, juce::sendNotificationSync);
            screen.fxKnobs[4]->slider.setValue (45.0, juce::sendNotificationSync);
            expect (std::abs (kit.pads[5].cutoff - 800.0f) < 1.0f, "Filter");
            expectEquals (kit.pads[5].decay, 250, "Decay");
            expectEquals (kit.pads[5].delaySend, 30, "Delay");
            expectEquals (kit.pads[5].reverbSend, 45, "Reverb");
            screen.showFxKnobs (false);
            expect (screen.knobs[0]->isVisible());

            // 16 Levels: the picked pad's sound on every pad; hitting pads doesn't pick them.
            screen.setSixteenLevels (levels::tuneFilter);
            expectEquals (engine.getMpc().levelsType.load(), (int) levels::tuneFilter);
            expectEquals (engine.getMpc().levelsPad.load(), 5);
            screen.pressPad (9);
            expectEquals (screen.getSelectedPad(), 5, "the sound stays picked");
            screen.paintEntireComponent (g, false);
            screen.setSixteenLevels (levels::off);
            screen.pressBank (2);   // bank C: empty pads
            screen.setSixteenLevels (levels::tune);
            expectEquals (engine.getMpc().levelsType.load(), (int) levels::off, "an empty pad has nothing to spread");

            // Sounds: the bank shows over the wave.
            screen.showSounds (true);
            expect (screen.getBrowser().isVisible());
            screen.showSounds (false);
            expect (! screen.getBrowser().isVisible());
        }

        beginTest ("Clear empties the Sampler (the pads keep theirs), and Undo clear brings it back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            std::unique_ptr<juce::AudioProcessorEditor> editor (engine.getSampler().createEditorAndMakeActive());
            auto* full = dynamic_cast<EditorView*> (editor.get());
            SamplerScreen screen (engine, *full);
            screen.setSize (1400, 560);
            auto& doc = engine.getSampler().getDocument();

            expect (! screen.canUndoClear());
            screen.pressClear();   // nothing to clear: says so, changes nothing
            expect (! doc.hasAudio() && ! doc.canUndo());

            doc.load (hits(), "Dusty break", "Load");
            screen.pressChop (SamplerScreen::even16);
            screen.pressToPads();
            const auto onPad = engine.getMpc().getProject().currentBank().pads[0].audio;
            expect (onPad != nullptr);

            screen.pressClear();
            expect (! doc.hasAudio(), "the Sampler is empty");
            expectEquals (screen.numSlices(), 0, "and has no slices");
            expect (screen.canUndoClear(), "Clear turns into Undo clear");
            expect (engine.getMpc().getProject().currentBank().pads[0].audio == onPad, "the pads keep their slices");

            full->runEdit ("undo");
            expect (doc.hasAudio(), "Undo clear brings the sound back");
            expectEquals (doc.getName(), juce::String ("Dusty break"));
            expect (! screen.canUndoClear());

            // Cleared, then something new goes in: no Undo clear any more.
            full->clearSound();
            expect (screen.canUndoClear());
            doc.load (hits(), "Next one", "Load");
            expect (! screen.canUndoClear());
        }

        beginTest ("a pad's chop opens alone in the Sampler: edit it, Save to the pad, step to the next, Back saves too");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto* full = dynamic_cast<EditorView*> (engine.getSampler().createEditorAndMakeActive());
            SamplerScreen screen (engine, *full);
            screen.ownEditor (full);
            screen.setSize (1500, 760);
            auto& doc = engine.getSampler().getDocument();

            doc.load (hits(), "Dusty break", "Load");
            screen.pressChop (SamplerScreen::even16);
            screen.pressToPads();
            auto& kit = engine.getMpc().getProject().currentBank();
            for (int i = 4; i < 16; ++i)
                kit.pads[(size_t) i] = {};   // only A01..A04 have chops
            const auto slice3 = kit.pads[2].audio;
            const int slice3Length = slice3->getNumSamples();

            screen.openPadInSampler (2);
            expect (screen.isEditingPad() && screen.getEditPad() == 2);
            expect (screen.isEditMode(), "the Sampler's editor shows");
            expect (doc.getAudio() == slice3, "it holds just that chop");
            expect (! screen.padEditChanged());
            bool barShows = false;
            for (auto* c : screen.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Save to A03")
                    barShows = b->isVisible();
            expect (barShows, "Save to A03 is on screen");

            full->runEdit ("reverse");
            expect (screen.padEditChanged());
            expect (kit.pads[2].audio == slice3, "nothing changes on the pad until it's saved");
            screen.pressSaveToPad();
            expect (kit.pads[2].audio != slice3 && kit.pads[2].audio == doc.getAudio(), "Save puts the edit on the pad");
            expectEquals (kit.pads[2].audio->getNumSamples(), slice3Length);
            expect (! screen.padEditChanged());

            // Step on: an unsaved edit goes onto the pad first.
            full->runEdit ("fadeOut");
            const auto edited = doc.getAudio();
            screen.pressStepPad (1);
            expect (kit.pads[2].audio == edited, "stepping saves the edit onto A03");
            expectEquals (screen.getEditPad(), 3);
            screen.pressStepPad (1);
            expectEquals (screen.getEditPad(), 0, "it skips the empty pads and wraps round");
            screen.pressStepPad (-1);
            expectEquals (screen.getEditPad(), 3);

            // A different sound opened over it isn't saved onto the pad.
            const auto a04 = kit.pads[3].audio;
            doc.load (hits(), "Something else", "Load");
            expect (! screen.padEditChanged());
            screen.pressBackToPads();
            expect (kit.pads[3].audio == a04, "the pad keeps its own sound");
            expect (! screen.isEditingPad() && ! screen.isEditMode(), "back on the pads");

            // An empty pad doesn't open; Edit opens the selected pad.
            screen.openPadInSampler (9);
            expect (! screen.isEditingPad());
            screen.pressPad (1);
            screen.pressEditPad();
            expect (screen.isEditingPad() && screen.getEditPad() == 1);
            juce::Image image (juce::Image::ARGB, 1500, 760, true);
            juce::Graphics g (image);
            screen.paintEntireComponent (g, false);
        }

        beginTest ("Sample a tool: the synth, one bar, lands in the Sampler (whatever Settings > Sample is set to)");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            auto* full = dynamic_cast<EditorView*> (engine.getSampler().createEditorAndMakeActive());
            SamplerScreen screen (engine, *full);
            screen.ownEditor (full);
            screen.setSize (1500, 760);
            engine.captureDest = RigCapture::toMpcPad;   // Settings > Sample goes elsewhere
            engine.getTransport().setBpm (120.0);
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            engine.getTransport().play();
            runBlocks (engine, 10);
            screen.pressSampleTool (RigCapture::synth, 1);
            expect (screen.isSamplingTool(), "it waits for the bar");
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 64, 0.9f));
            runBlocks (engine, (int) (5.0 * kRate / kBlock));   // past the next bar and one bar on, at 120
            expect (! screen.isSamplingTool());
            const auto said = engine.serviceCapture();
            expect (said.contains ("Sampler"), said);
            auto& doc = engine.getSampler().getDocument();
            expect (doc.hasAudio() && doc.getName().startsWith ("Synth"), "the synth is in the Sampler");
            if (doc.hasAudio())
                expectWithinAbsoluteError ((double) doc.getAudio()->getNumSamples() / crate::kSampleRate, 2.0, 0.02, "one bar at 120 BPM");
            expect (engine.getMpc().getProject().currentBank().pads[0].isEmpty(), "nothing went onto a pad");
            expectEquals (engine.captureDest, (int) RigCapture::toMpcPad, "Settings > Sample keeps its own choice");

            // free: pressed again, it stops
            screen.pressSampleTool (RigCapture::mix, 0);
            runBlocks (engine, (int) (2.5 * kRate / kBlock));
            expect (screen.isSamplingTool());
            screen.pressSampleTool (RigCapture::mix, 0);
            runBlocks (engine, 2);
            expect (! screen.isSamplingTool());
            engine.serviceCapture();
            screen.pressSampleTool (RigCapture::sampler, 1);
            expect (! screen.isSamplingTool(), "the Sampler can't sample itself");
        }

        beginTest ("Sets: save bank A, load it onto bank B (Undo), and a whole kit onto a new kit");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto* full = dynamic_cast<EditorView*> (engine.getSampler().createEditorAndMakeActive());
            SamplerScreen screen (engine, *full);
            screen.ownEditor (full);
            screen.setSize (1500, 760);
            juce::TemporaryFile temp;
            screen.setsFolder = temp.getFile();
            auto& doc = engine.getSampler().getDocument();
            doc.load (hits(), "Dusty break", "Load");
            screen.pressChop (SamplerScreen::even16);
            screen.pressToPads();
            auto& project = engine.getMpc().getProject();
            project.currentBank().pads[2].volume = 61;
            project.currentBank().pads[2].tune = -30;
            project.currentBank().pads[2].mode = PadMode::noteOff;
            const auto slice3 = project.currentBank().pads[2].audio;

            const auto folder = screen.saveSet (0, "Dusty chops");
            expect (folder.isDirectory() && folder.getChildFile ("set.json").existsAsFile(), "the set is a folder with set.json");
            expectEquals (folder.findChildFiles (juce::File::findFiles, false, "*.wav").size(), 16, "one WAV a pad");
            const auto peeked = SoundSet::peek (folder);
            expect (peeked && peeked->padCount() == 16 && ! peeked->wholeKit && peeked->name == "Dusty chops");
            expect (screen.saveSet (0, "Dusty chops").getFileName() == "Dusty chops 2", "the same name again doesn't overwrite it");
            expectEquals (SoundSet::list (screen.setsFolder).size(), 2);

            screen.pressBank (1);
            expect (screen.loadSetFile (folder));
            const auto& b3 = project.currentBank().pads[16 + 2];
            expect (! b3.isEmpty() && b3.audio->getNumSamples() == slice3->getNumSamples(), "B03 has A03's slice");
            expectEquals (b3.volume, 61);
            expectEquals (b3.tune, -30);
            expect (b3.mode == PadMode::noteOff);
            expect (b3.name == project.currentBank().pads[2].name);
            expect (screen.canUndoLoadSet());
            screen.pressUndoLoadSet();
            expect (project.currentBank().pads[16 + 2].isEmpty(), "Undo puts bank B back as it was (empty)");
            expect (! screen.canUndoLoadSet());

            const int kits = (int) project.getBanks().size(), mine = project.getCurrentIndex();
            const auto whole = screen.saveSet (-1, "All of it");
            expect (SoundSet::peek (whole)->wholeKit);
            expect (screen.loadSetFile (whole));
            expectEquals ((int) project.getBanks().size(), kits + 1, "a whole-kit set goes on a new kit");
            expectEquals (project.currentBank().usedPads(), 16);
            expect (project.getBanks()[(size_t) mine].usedPads() == 16 && project.getCurrentIndex() != mine, "your kit stays");
            temp.getFile().deleteRecursively();
        }

        beginTest ("the steps: 1 Get lit on an empty Sampler, 2 Shape opens the editor, 3 Chop the chop side");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto* full = dynamic_cast<EditorView*> (engine.getSampler().createEditorAndMakeActive());
            SamplerScreen screen (engine, *full);
            screen.ownEditor (full);
            screen.setSize (1500, 760);
            expectEquals (screen.currentStep(), 0);
            screen.pressStep (1);
            expect (screen.isEditMode(), "2 Shape shows the editor");
            engine.getSampler().getDocument().load (hits(), "Dusty break", "Load");
            expectEquals (screen.currentStep(), 1);
            screen.pressStep (2);
            expect (! screen.isEditMode());
            expectEquals (screen.currentStep(), 2);
            bool shown = false;
            for (auto* c : screen.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText().endsWith ("Save set"))
                    shown = b->isVisible() && b->getWidth() > 60;
            expect (shown, "5 Save set is on screen");
            if (const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_SAMPLER_SNAPSHOT", {}); path.isNotEmpty())
            {
                screen.openPadInSampler (0);   // nothing on A01: stays on the chop side
                juce::File file (path + "-steps.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (screen.createComponentSnapshot (screen.getLocalBounds(), true, 1.0f), stream);
            }
        }

        beginTest ("the full editor is on the Sampler screen: Edit shows it, Chop shows the pads");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto* full = dynamic_cast<EditorView*> (engine.getSampler().createEditorAndMakeActive());
            expect (full != nullptr);
            SamplerScreen screen (engine, *full);
            screen.ownEditor (full);
            screen.setSize (1500, 760);
            expect (! screen.isEditMode());
            expect (! full->isVisible(), "Chop · Pads first");
            screen.setEditMode (true);
            expect (screen.isEditMode());
            expect (full->isVisible());
            expect (full->getWidth() >= 1400 && full->getHeight() > 500, "the editor gets the screen");
            for (auto* c : screen.getChildren())
                if (c != full)
                    if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "Transients")
                        expect (! b->isVisible(), "the chop tools hide while editing");
            juce::Image image (juce::Image::ARGB, 1500, 760, true);
            juce::Graphics g (image);
            screen.paintEntireComponent (g, false);
            screen.setEditMode (false);
            expect (! full->isVisible());
        }
    }
};

static RigSamplerScreenTests rigSamplerScreenTests;
