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
