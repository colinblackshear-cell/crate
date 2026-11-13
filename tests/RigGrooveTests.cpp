#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigSong.h"
#include "GrooveView.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

void runBlocks (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        engine.tickScenes();
    }
}

float param (juce::AudioProcessorValueTreeState& p, const char* id) { return p.getRawParameterValue (id)->load(); }

void turnKnob (RigEngine& engine, int cc, int value, int channel = 1)
{
    engine.addMidi (juce::MidiMessage::controllerEvent (channel, cc, value));
    runBlocks (engine, 1);
}
}

struct RigGrooveTests : juce::UnitTest
{
    RigGrooveTests() : juce::UnitTest ("Rig main screen", "Rig") {}

    void runTest() override
    {
        beginTest ("picking a track: the keys play it and the keyboard's knobs turn it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            GrooveView view (engine);
            view.setSize (1400, 740);
            expectEquals (view.getTrack(), (int) GrooveView::vocoder, "starts where the keys already go");

            view.selectTrack (GrooveView::bass);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::bass);
            turnKnob (engine, 21, 127);
            expectWithinAbsoluteError (param (engine.getBass().params, "cutoff"), 8000.0f, 1.0f, "knob 1 is the bass's cutoff");

            view.selectTrack (GrooveView::synth);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::synth);
            turnKnob (engine, 21, 0);
            expectWithinAbsoluteError (param (engine.getSynth().params, "cutoff"), 60.0f, 1.0f, "now it's the synth's");
            expectWithinAbsoluteError (param (engine.getBass().params, "cutoff"), 8000.0f, 1.0f, "the bass stays put");
            turnKnob (engine, 28, 0, 16);
            expectWithinAbsoluteError (param (engine.getSynth().params, "volume"), 0.0f, 0.001f, "knob 8, on any channel");

            // A knob learned onto something else does that instead.
            const auto& list = engine.getBass().getParameters();
            int resonance = -1;
            for (int p = 0; p < list.size(); ++p)
                if (dynamic_cast<juce::AudioProcessorParameterWithID*> (list[p])->paramID == "resonance")
                    resonance = p;
            engine.getRouting().addParameterKnob (1, 22, RigEngine::bassTool, resonance);
            const float synthResonance = param (engine.getSynth().params, "resonance");
            turnKnob (engine, 22, 0);
            expectWithinAbsoluteError (param (engine.getBass().params, "resonance"), 0.0f, 0.001f);
            expectWithinAbsoluteError (param (engine.getSynth().params, "resonance"), synthResonance, 0.001f);

            // The loops have no notes: the keys stay where they were, the knobs turn its effects.
            view.selectTrack (GrooveView::loops);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::synth);
            expectEquals (view.knobFor (0).first, RigEngine::fxTool + RigEngine::loopsStrip);
        }

        beginTest ("each track says why it's quiet");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            engine.micOn = false;
            expect (view.stateOf (GrooveView::vocoder).contains ("Mic is off"));
            engine.micOn = true;
            expect (! view.stateOf (GrooveView::vocoder).contains ("Mic"));
            engine.vocoderOn = false;
            expect (view.stateOf (GrooveView::vocoder).contains ("off"));
            engine.getStrip (RigEngine::bassStrip).mute = true;
            expectEquals (view.stateOf (GrooveView::bass), juce::String ("Muted"));
            engine.getStrip (RigEngine::bassStrip).mute = false;
            expect (view.stateOf (GrooveView::bass).contains ("press Play"));
            expect (view.stateOf (GrooveView::drums).contains ("No sounds"));
            expect (view.stateOf (GrooveView::sampler).contains ("Empty"));
            expect (view.stateOf (GrooveView::loops).contains ("Empty"));
        }

        beginTest ("every instrument has a card with its own Play / On button");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            GrooveView view (engine);
            view.setSize (1400, 860);
            expectEquals ((int) GrooveView::numTracks, 8);
            expectEquals (GrooveView::trackName (GrooveView::drums), juce::String ("MPC"), "the MPC says MPC");
            expectEquals (GrooveView::stripFor (GrooveView::lineIn), (int) RigEngine::inputStrip);
            expectEquals (GrooveView::tabFor (GrooveView::lineIn), juce::String ("Effects"));
            for (int t = 0; t < GrooveView::numTracks; ++t)
            {
                view.selectTrack (t);
                expect (view.trackButtonText (t).isNotEmpty() && view.trackButtonTip (t).isNotEmpty());
                expect (view.stateOf (t).isNotEmpty(), GrooveView::trackName (t) + " says what it's doing");
            }

            // Bass and the drum machine: Play starts their pattern and Kaleidobox; Stop stops the pattern only.
            for (int t : { (int) GrooveView::bass, (int) GrooveView::machine })
            {
                auto& params = t == GrooveView::bass ? engine.getBass().params : engine.getDrums().params;
                expect (! view.isGoing (t));
                view.pressTrackButton (t);
                runBlocks (engine, 1);
                expect (param (params, "sequencer") > 0.5f && engine.getTransport().isPlaying());
                expect (view.isGoing (t));
                view.pressTrackButton (t);
                expect (param (params, "sequencer") < 0.5f, "Stop turns its pattern off");
                runBlocks (engine, 1);
                expect (engine.getTransport().isPlaying(), "the rest keeps playing");
            }

            // The synth's arp, the vocoder.
            view.pressTrackButton (GrooveView::synth);
            expect (param (engine.getSynth().params, "arpOn") > 0.5f);
            view.pressTrackButton (GrooveView::synth);
            expect (param (engine.getSynth().params, "arpOn") < 0.5f);
            view.pressTrackButton (GrooveView::vocoder);
            expect (engine.vocoderOn.load());
            expectEquals (view.trackButtonText (GrooveView::vocoder), juce::String ("On"));

            // Line in: on (and the audio input with it), its jack, and a loop of it.
            engine.micOn = false;
            expect (view.stateOf (GrooveView::lineIn).startsWith ("Off"));
            view.pressTrackButton (GrooveView::lineIn);
            expect (engine.inputOn.load() && engine.micOn.load());
            view.selectTrack (GrooveView::lineIn);
            expectEquals (view.numSteps(), 3);
            view.pressStep (2);
            expectEquals (engine.inputJack.load(), 2);
            view.pressPattern (0);   // Loop it
            runBlocks (engine, 1);
            expectEquals (engine.getLooper().getLayer (0).source.load(), (int) RigLooper::line);
            expect (engine.getLooper().getState (0) != RigLooper::State::empty);
            expect (view.isGoing (GrooveView::loops));
            view.pressTrackButton (GrooveView::lineIn);
            expect (! engine.inputOn.load());

            // Loops: Rec closes the layer that's recording, then records the next empty one.
            view.pressTrackButton (GrooveView::loops);
            runBlocks (engine, 1);
            view.pressTrackButton (GrooveView::loops);
            runBlocks (engine, 1);
            expect (view.isGoing (GrooveView::loops), "and the next press records again");
        }

        beginTest ("the Machine track is the drum machine: every sound, the accents, the variation it edits");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.setSize (1400, 860);
            view.selectTrack (GrooveView::machine);
            auto& d = engine.getDrums();
            expectEquals (view.numRows(), crate::drums::kVoices + 1, "the accent row and all 12 sounds");
            d.setPattern (0, {}, 0);
            d.setPattern (0, {}, 1);
            view.pressCell (0, 3);
            expect (d.getPattern (0).accent[3], "row 0 is the accent");
            view.pressCell (1 + crate::drums::cowbell, 5);
            expectEquals ((int) d.getPattern (0).hits[crate::drums::cowbell][5], 1, "the cowbell has its own row");
            // Its B variation, as the drum machine's own A / B buttons pick it.
            view.pressPattern (crate::drums::kPatterns + 1);   // B
            expectEquals (d.getVariation(), 1);
            expectEquals (d.editVariation, 1);
            view.pressCell (1 + crate::drums::kick, 0);
            expectEquals ((int) d.getPattern (0, 1).hits[crate::drums::kick][0], 1, "edits B");
            expectEquals ((int) d.getPattern (0, 0).hits[crate::drums::kick][0], 0, "A is untouched");
            // An edit on the drum machine's page shows here.
            auto p = d.getPattern (0, 1);
            p.hits[crate::drums::clap][7] = 1;
            d.setPattern (0, p, 1);
            expect (view.stateOf (GrooveView::machine).contains ("1B"), "the card says which pattern and variation");
            view.pressPattern (11);
            expectEquals (d.getSelectedPattern(), 11, "all 16 patterns");
        }

        beginTest ("bass: lighting a step turns the pattern on");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::bass);
            auto& bass = engine.getBass();
            bass.params.getParameter ("sequencer")->setValueNotifyingHost (0.0f);
            expect (view.stateOf (GrooveView::bass).contains ("Pattern off"));
            expect (! bass.getPattern (0).steps[1].on);
            view.pressStep (1);
            expect (bass.getPattern (0).steps[1].on);
            expect (param (bass.params, "sequencer") > 0.5f);
            view.pressStep (1);
            expect (! bass.getPattern (0).steps[1].on, "and off again");

            view.pressPattern (3);
            expectEquals (bass.getSelectedPattern(), 3);
            expect (view.stateOf (GrooveView::bass).contains ("no notes"));
        }

        beginTest ("drums: steps write the MPC's beat, patterns pick it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::drums);
            expectEquals (engine.getRouting().keysTo.load(), (int) RigRouting::Dest::mpc);
            auto& project = engine.getMpc().getProject();
            view.pressStep (4);
            int notes = 0;
            for (const auto& t : project.currentSequence().tracks)
                for (const auto& n : t.notes)
                {
                    ++notes;
                    expectEquals (n.tick, 4 * seq::kTicksPerBeat / 4);
                    expectEquals (n.pad, 0);
                }
            expectEquals (notes, 1);
            view.pressStep (4);
            expect (project.currentSequence().isEmpty());

            view.pressPattern (2);
            expect (project.getSequences().size() >= 3, "the beat is made when you pick it");
            expectEquals (project.getCurrentSequenceIndex(), 2);
        }

        beginTest ("synth: the row picks how the arp plays");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::synth);
            auto& params = engine.getSynth().params;
            view.pressPattern (2);
            expect (param (params, "arpOn") > 0.5f);
            expectEquals ((int) param (params, "arpMode"), 1);
            view.pressPattern (0);
            expect (param (params, "arpOn") < 0.5f);
            view.pressStep (3);   // on by default: off
            expect (! engine.getSynth().getRhythm().on[3]);
            view.pressStep (3);
            expect (engine.getSynth().getRhythm().on[3]);
            expect (param (params, "arpOn") > 0.5f, "lighting a step turns the arp on");

            // Slower and Faster, after Hold, step the arp's speed.
            const int speed = (int) param (params, "arpSpeed");
            view.pressPattern (7);
            expectEquals ((int) param (params, "arpSpeed"), speed - 1, "slower");
            view.pressPattern (8);
            view.pressPattern (8);
            expectEquals ((int) param (params, "arpSpeed"), speed + 1, "faster");
            view.pressPattern (6);
            expect (param (params, "latch") > 0.5f, "Hold is still Hold");
        }

        beginTest ("song sections: an empty key saves what's playing, a full one plays it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.vocoderOn = false;
            GrooveView view (engine);
            auto& tl = engine.getTimeline();
            engine.getBass().params.getParameter ("sequencer")->setValueNotifyingHost (1.0f);
            view.pressPart (0);
            expectEquals ((int) rigsong::sections (tl).size(), 1);
            expectEquals (rigsong::sections (tl)[0].name, juce::String ("Intro"));
            const int bassLane = RigTimeline::laneForStrip (RigEngine::bassStrip);
            expect (rigsong::boxBlock (tl, 0, bassLane) >= 0, "the intro plays the bass");
            engine.getStrip (RigEngine::bassStrip).mute = true;
            view.pressPart (5);   // any empty one: the next section
            expectEquals ((int) rigsong::sections (tl).size(), 2);
            expectEquals (rigsong::sections (tl)[1].name, juce::String ("Verse"));
            expectEquals (rigsong::boxBlock (tl, 1, bassLane), -1, "the bass was muted: quiet in the verse");

            view.pressPart (0);
            runBlocks (engine, 2);
            expect (tl.on.load(), "a section key plays the song");
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load(), "the intro has the bass playing");
        }

        beginTest ("loops and vocoder");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::loops);
            view.pressStep (0);
            runBlocks (engine, 1);
            expect (engine.getLooper().getState (0) != RigLooper::State::empty);
            view.pressPattern (1);
            expect (engine.getLooper().getLayer (1).mute.load());

            view.selectTrack (GrooveView::vocoder);
            engine.vocoderOn = false;
            auto& params = engine.getVocoder().params;
            const bool robot = param (params, "robotOn") > 0.5f;
            view.pressStep (0);
            expect ((param (params, "robotOn") > 0.5f) != robot);
            view.pressPattern (1);
            expectEquals (engine.getVocoder().getProgramName (engine.getVocoder().getCurrentProgram()), juce::String ("Classic Robot"));
            expect (engine.vocoderOn.load(), "picking a sound turns the vocoder on");
        }

        beginTest ("drums: a row per sound; bass: rows for steps, notes, accents and slides");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.selectTrack (GrooveView::drums);
            expectEquals (view.numRows(), 4);
            view.pressCell (1, 8);   // the second sound (pad 2 in an empty kit)
            int notes = 0;
            for (const auto& t : engine.getMpc().getProject().currentSequence().tracks)
                for (const auto& n : t.notes)
                {
                    ++notes;
                    expectEquals (n.pad, 1);
                    expectEquals (n.tick, 8 * seq::kTicksPerBeat / 4);
                }
            expectEquals (notes, 1);

            view.selectTrack (GrooveView::bass);
            auto& bass = engine.getBass();
            const auto before = bass.getPattern (bass.getSelectedPattern()).steps[2];
            view.pressCell (0, 2);
            view.pressCell (2, 2);
            view.pressCell (3, 2);
            const auto step = bass.getPattern (bass.getSelectedPattern()).steps[2];
            expect (step.on != before.on && step.accent != before.accent && step.slide != before.slide);
        }

        beginTest ("solo hears one track, and a second press brings the others back");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            engine.getStrip (RigEngine::synthStrip).mute = true;
            view.pressSolo (GrooveView::bass);
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load());
            expect (engine.getStrip (RigEngine::mpcStrip).mute.load());
            expect (engine.getStrip (RigEngine::loopsStrip).mute.load());
            view.pressSolo (GrooveView::bass);
            expect (! engine.getStrip (RigEngine::mpcStrip).mute.load());
            expect (engine.getStrip (RigEngine::synthStrip).mute.load(), "a mute from before the solo stays");
        }

        beginTest ("the dark panel is saved with the show");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            engine.screens.setProperty ("dark", true, nullptr);
            const auto show = engine.saveShow();
            RigEngine other;
            other.prepare (kRate, kBlock);
            other.loadShow (show);
            expect ((bool) other.screens.getProperty ("dark", false));
        }

        beginTest ("bass and synth: named sounds, Play, and keys on the screen");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.setSize (1400, 740);

            view.selectTrack (GrooveView::synth);
            expect (view.hasKeys());
            expect (view.numSounds() >= 16, "plenty of synth sounds");
            expectEquals (view.soundName(), juce::String ("Init Supersaw"));
            view.stepSound (1);
            expectEquals (view.soundName(), juce::String ("Big Supersaw"));
            expectWithinAbsoluteError (param (engine.getSynth().params, "detune"), 0.55f, 0.01f, "the sound's settings");
            engine.getSynth().params.getParameter ("volume")->setValueNotifyingHost (0.3f);
            view.pickSound (6);   // Pluck
            expectEquals (view.soundName(), juce::String ("Pluck"));
            expectWithinAbsoluteError (param (engine.getSynth().params, "detune"), 0.4f, 0.01f, "the last sound's settings don't leak in");
            expectWithinAbsoluteError (param (engine.getSynth().params, "volume"), 0.3f, 0.01f, "the level stays");

            // The screen's keys play it.
            juce::AudioBuffer<float> out (2, kBlock);
            for (int b = 0; b < 4; ++b)
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            view.getScreenKeys().noteOn (1, 60, 0.9f);
            float peak = 0;
            for (int b = 0; b < 10; ++b)
            {
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
                peak = juce::jmax (peak, out.getMagnitude (0, kBlock));
            }
            expect (peak > 0.01f, "a key on the screen makes the synth sound");
            view.getScreenKeys().noteOff (1, 60, 0.0f);

            view.selectTrack (GrooveView::bass);
            expect (view.numSounds() >= 10, "plenty of bass sounds");
            view.pickSound (1);
            expectEquals (view.soundName(), juce::String ("Acid 303"));
            expect (engine.getBass().params.state.getProperty ("soundName").toString() == "Acid 303", "kept with the bass");

            engine.getBass().params.getParameter ("sequencer")->setValueNotifyingHost (0.0f);
            engine.getTransport().stop();
            runBlocks (engine, 1);
            view.pressPlay();
            expect (param (engine.getBass().params, "sequencer") > 0.5f, "Play: the pattern is on");
            runBlocks (engine, 1);
            expect (engine.getTransport().isPlaying(), "and the Rig plays, so you hear it");
            view.pressPlay();
            expect (param (engine.getBass().params, "sequencer") < 0.5f, "Stop");

            view.selectTrack (GrooveView::drums);
            expect (! view.hasKeys());
        }

        beginTest ("the vocoder's parts: a switch each, and under it how much of it and what shapes it");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            GrooveView view (engine);
            view.setBounds (0, 0, 1392, 640);
            view.selectTrack (GrooveView::vocoder);
            auto& params = engine.getVocoder().params;
            expectEquals (view.numSteps(), 5, "Robot, Voice, Effects, Arp, Vintage");
            for (int part = 0; part < view.numSteps(); ++part)
            {
                expect (view.numPartKnobs (part) >= 3, "every part has its own knobs");
                for (int k = 0; k < view.numPartKnobs (part); ++k)
                {
                    auto* knob = view.partKnob (part, k);
                    expect (knob != nullptr && knob->isVisible() && knob->slider.getWidth() >= 36,
                            "part " + juce::String (part) + " knob " + juce::String (k) + " is on screen at a usable size");
                }
            }
            expect (view.partKnob (0, 0)->name == "Level" && view.partKnob (4, 0)->name == "Wet/dry", "how much of it comes first");
            expect (view.partKnob (0, 0)->getBottom() <= view.getHeight() && view.partKnob (0, 0)->getY() > 0);

            // The knobs turn the vocoder's own settings.
            view.partKnob (4, 0)->slider.setValue (0.25, juce::sendNotificationSync);
            expectWithinAbsoluteError (param (params, "vintageAmount"), 0.25f, 0.011f, "Vintage's wet/dry");
            view.partKnob (2, 0)->slider.setValue (0.6, juce::sendNotificationSync);
            expectWithinAbsoluteError (param (params, "reverbMix"), 0.6f, 0.011f, "Effects: the reverb's mix");
            expect (view.partKnob (0, 1)->slider.getTextFromValue (view.partKnob (0, 1)->slider.getValue()).endsWith ("x"),
                    "values read in their units");

            // Reaching for an Effects knob while Effects is off switches it on.
            params.getParameter ("fxOn")->setValueNotifyingHost (0.0f);
            view.partKnob (2, 1)->slider.onDragStart();
            expect (param (params, "fxOn") > 0.5f, "Effects comes on when you grab its knob");

            // Other tracks don't get them.
            view.selectTrack (GrooveView::sampler);
            expectEquals (view.numPartKnobs (0), 0);
            view.selectTrack (GrooveView::vocoder);
            expect (view.numPartKnobs (0) == 4);
            // A short screen (a laptop) and a tall one: every knob fits inside its part.
            for (int height : { 590, 760 })
            {
                view.setBounds (0, 0, 1392, height);
                for (int part = 0; part < view.numSteps(); ++part)
                    for (int k = 0; k < view.numPartKnobs (part); ++k)
                    {
                        auto* knob = view.partKnob (part, k);
                        expect (knob->slider.getWidth() >= 36 && knob->getBottom() < height - 150,
                                "at " + juce::String (height) + " high, part " + juce::String (part) + " knob " + juce::String (k) + " fits");
                    }
                if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_GROOVE_PARTS_SNAPSHOT", {}); path.isNotEmpty())
                {
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
                    juce::File file (path.replace (".png", "-" + juce::String (height) + ".png"));
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
        }
    }
};

static RigGrooveTests rigGrooveTests;
