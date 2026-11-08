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
        }
    }
};

static RigSamplerScreenTests rigSamplerScreenTests;
