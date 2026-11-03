#include "TestHelpers.h"

#include "EditorProcessor.h"
#include "EditorView.h"
#include "core/KeyboardSampler.h"

using namespace crate;

namespace
{
// Plays `blocks` blocks and returns everything that came out, channel 0.
std::vector<float> run (KeyboardSampler& keys, int blocks, juce::MidiBuffer first = {}, int block = 512)
{
    std::vector<float> out;
    for (int b = 0; b < blocks; ++b)
    {
        juce::AudioBuffer<float> io (2, block);
        io.clear();
        keys.render (io, b == 0 ? first : juce::MidiBuffer());
        out.insert (out.end(), io.getReadPointer (0), io.getReadPointer (0) + block);
    }
    return out;
}

int lastNonSilent (const std::vector<float>& v)
{
    for (int i = (int) v.size(); --i >= 0;)
        if (std::abs (v[(size_t) i]) > 1.0e-4f)
            return i;
    return -1;
}

juce::MidiBuffer notes (std::initializer_list<std::pair<int, juce::MidiMessage>> list)
{
    juce::MidiBuffer m;
    for (auto& [at, msg] : list)
        m.addEvent (msg, at);
    return m;
}

struct KeyboardSamplerTests : juce::UnitTest
{
    KeyboardSamplerTests() : juce::UnitTest ("Keyboard sampler", "Keys") {}

    void runTest() override
    {
        beginTest ("the root note plays the sound as recorded; an octave up plays it twice as fast");
        {
            KeyboardSampler keys;
            keys.prepare (44100.0);
            keys.setMode (KeyboardSampler::Mode::oneShot);
            keys.setAttack (0);
            auto sound = share (ramp (4410));
            keys.setSound (sound);

            auto out = run (keys, 12, notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } }));
            expectWithinAbsoluteError (out[1000], sound->getSample (0, 1000), 1.0e-4f, "same samples at the root");
            expect (std::abs (lastNonSilent (out) - 4409) < 3, "plays to the end, once");
            expectEquals (keys.getActiveVoiceCount(), 0);

            out = run (keys, 12, notes ({ { 0, juce::MidiMessage::noteOn (1, 72, 1.0f) } }));
            expect (std::abs (lastNonSilent (out) - 2204) < 4, "twice as fast: " + juce::String (lastNonSilent (out)));
            expectWithinAbsoluteError (out[500], sound->getSample (0, 1000), 1.0e-4f);

            out = run (keys, 24, notes ({ { 0, juce::MidiMessage::noteOn (1, 48, 1.0f) } }));
            expect (std::abs (lastNonSilent (out) - 8818) < 6, "half speed an octave down");

            keys.setRootNote (72);
            out = run (keys, 12, notes ({ { 0, juce::MidiMessage::noteOn (1, 72, 1.0f) } }));
            expect (std::abs (lastNonSilent (out) - 4409) < 3, "a new root plays as recorded");
        }

        beginTest ("velocity sets the level");
        {
            KeyboardSampler keys;
            keys.prepare (44100.0);
            keys.setAttack (0);
            keys.setSound (share (sine (44100)));
            const auto loud = run (keys, 4, notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } }));
            keys.allNotesOff();
            run (keys, 2);
            const auto soft = run (keys, 4, notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 0.3f) } }));
            auto peak = [] (const std::vector<float>& v) { float p = 0; for (auto x : v) p = juce::jmax (p, std::abs (x)); return p; };
            expectWithinAbsoluteError (peak (loud), 0.5f, 0.01f);
            expect (peak (soft) < 0.5f * peak (loud));
            expect (peak (soft) > 0.05f);
        }

        beginTest ("hold stops after the key is let go; one-shot doesn't; loop repeats while held");
        {
            KeyboardSampler keys;
            keys.prepare (44100.0);
            keys.setAttack (0);
            keys.setRelease (0.01f);
            keys.setSound (share (sine (44100)));

            auto held = notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) },
                                 { 400, juce::MidiMessage::noteOff (1, 60) } });
            keys.setMode (KeyboardSampler::Mode::hold);
            auto out = run (keys, 8, held);
            const int end = lastNonSilent (out);
            expect (end > 400 + 300 && end < 400 + 441 + 10, "fades over the release time: " + juce::String (end));

            keys.setMode (KeyboardSampler::Mode::oneShot);
            out = run (keys, 4, held);
            expect (lastNonSilent (out) > 2000, "one-shot keeps going");
            keys.allNotesOff();
            run (keys, 2);

            keys.setMode (KeyboardSampler::Mode::loop);
            keys.setRange ({ 1000, 1441 });
            out = run (keys, 20, notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } }));
            expectEquals (keys.getActiveVoiceCount(), 1, "still going after 10000 samples of a 441-sample loop");
            keys.render (*std::make_unique<juce::AudioBuffer<float>> (2, 64), notes ({ { 0, juce::MidiMessage::noteOff (1, 60) } }));
            run (keys, 4);
            expectEquals (keys.getActiveVoiceCount(), 0, "stops when let go");
        }

        beginTest ("chords, and no more than 16 notes at once");
        {
            KeyboardSampler keys;
            keys.prepare (48000.0);
            keys.setSound (share (sine (44100)));
            juce::MidiBuffer chord;
            for (int n = 40; n < 60; ++n)
                chord.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
            run (keys, 1, chord);
            expectEquals (keys.getActiveVoiceCount(), KeyboardSampler::kMaxVoices);
        }

        beginTest ("changing the sound while notes play is safe");
        {
            KeyboardSampler keys;
            keys.prepare (44100.0);
            keys.setSound (share (sine (44100)));
            run (keys, 1, notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } }));
            for (int i = 0; i < 20; ++i)
            {
                keys.setSound (share (sine (2000 + i)));
                run (keys, 1, notes ({ { 10, juce::MidiMessage::noteOn (1, 60 + i, 1.0f) } }));
            }
            keys.setSound (nullptr);
            run (keys, 4, notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } }));
            expectEquals (keys.getActiveVoiceCount(), 0);
        }
    }
};

struct KeysInEditorTests : juce::UnitTest
{
    KeysInEditorTests() : juce::UnitTest ("Sample Editor keys", "Keys") {}

    void runTest() override
    {
        beginTest ("MIDI and the on-screen keys play the sound, or the selection");
        {
            EditorProcessor p;
            p.setRateAndBufferSizeDetails (44100.0, 512);
            p.prepareToPlay (44100.0, 512);
            p.getDocument().load (share (sine (44100)), "tone", "Open");
            p.getDocument().dispatchPendingMessages();   // (the app's message loop does this)

            juce::AudioBuffer<float> io (2, 512);
            io.clear();
            auto midi = notes ({ { 0, juce::MidiMessage::noteOn (1, 60, 1.0f) } });
            p.processBlock (io, midi);
            expect (io.getMagnitude (0, 256, 256) > 0.4f, "a MIDI note plays");
            expect (io.getMagnitude (1, 256, 256) > 0.4f, "on both sides");

            p.getKeys().allNotesOff();
            juce::MidiBuffer none;
            p.processBlock (io, none);
            p.keyboardState.noteOn (1, 67, 1.0f);
            p.processBlock (io, none);
            expect (io.getMagnitude (0, 256, 256) > 0.4f, "an on-screen key plays");
            p.keyboardState.noteOff (1, 67, 0.0f);
            for (int b = 0; b < 30; ++b)
                p.processBlock (io, none);
            expectEquals (p.getKeys().getActiveVoiceCount(), 0);
        }

        beginTest ("the keys' settings are saved with the project");
        {
            EditorProcessor p;
            p.getDocument().load (share (sine (4410)), "x", "Open");
            p.getKeys().setRootNote (48);
            p.getKeys().setMode (KeyboardSampler::Mode::loop);
            p.getKeys().setRelease (1.5f);
            juce::MemoryBlock state;
            p.getStateInformation (state);

            EditorProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            expectEquals (restored.getKeys().getRootNote(), 48);
            expect (restored.getKeys().getMode() == KeyboardSampler::Mode::loop);
            expectWithinAbsoluteError (restored.getKeys().getRelease(), 1.5f, 1.0e-4f);
        }

        beginTest ("the window's key controls set the root note and play mode");
        {
            EditorProcessor p;
            p.getDocument().load (share (sine (44100)), "tone", "Open");
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<EditorView*> (editor.get());
            expect (view != nullptr);
            if (view == nullptr)
                return;

            view->getRootBox().setSelectedId (57 + 1, juce::sendNotificationSync);
            expectEquals (p.getKeys().getRootNote(), 57);
            view->getKeyModeBox().setSelectedId (3, juce::sendNotificationSync);
            expect (p.getKeys().getMode() == KeyboardSampler::Mode::oneShot);

            // with computer keys on, letters play notes instead of running shortcuts
            view->getComputerKeysToggle().setToggleState (true, juce::sendNotificationSync);
            expect (view->keyPressed (juce::KeyPress ('a')));
            expect (view->keyPressed (juce::KeyPress ('r')), "R doesn't start recording");
            expect (! p.isRecording());
            expect (view->keyPressed (juce::KeyPress ('x')), "X moves up an octave");
            view->getComputerKeysToggle().setToggleState (false, juce::sendNotificationSync);
            expect (! view->keyPressed (juce::KeyPress ('a')), "letters are shortcuts again");

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_KEYS_SNAPSHOT", {}); path.isNotEmpty())
            {
                p.keyboardState.noteOn (1, 64, 1.0f);
                view->getKeysView().repaint();
                auto image = view->createComponentSnapshot (view->getLocalBounds(), true, 1.0f);
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (image, out);
                p.keyboardState.allNotesOff (1);
            }
        }
    }
};

// A drum-like loop: short decaying bursts at known places.
juce::AudioBuffer<float> hitsAt (std::initializer_list<int> positions, int length)
{
    juce::AudioBuffer<float> b (1, length);
    b.clear();
    juce::Random random (7);
    for (int p : positions)
        for (int i = 0; i < 4000 && p + i < length; ++i)
            b.setSample (0, p + i, (random.nextFloat() * 2.0f - 1.0f) * 0.8f * std::exp (-(float) i / 900.0f));
    return b;
}

struct ChopTests : juce::UnitTest
{
    ChopTests() : juce::UnitTest ("Smart chop", "Keys") {}

    void runTest() override
    {
        beginTest ("each key from C1 plays one slice at its own pitch");
        {
            KeyboardSampler keys;
            keys.prepare (44100.0);
            keys.setAttack (0);
            keys.setMode (KeyboardSampler::Mode::slices);
            auto sound = share (ramp (10000));
            keys.setSound (sound);
            keys.setSlices ({ 2000, 5000 });

            auto out = run (keys, 8, notes ({ { 0, juce::MidiMessage::noteOn (1, KeyboardSampler::kFirstSliceNote + 1, 1.0f) } }));
            expectWithinAbsoluteError (out[100], sound->getSample (0, 2100), 1.0e-4f, "slice 2 starts at its marker");
            const int end = lastNonSilent (out);
            expect (std::abs (end - 2999) < 3, "and ends at the next one: " + juce::String (end));

            out = run (keys, 16, notes ({ { 0, juce::MidiMessage::noteOn (1, KeyboardSampler::kFirstSliceNote + 2, 1.0f),
                                            /* let go straight away: slices play to the end */ } }));
            expect (std::abs (lastNonSilent (out) - 4999) < 3, "the last slice runs to the end of the sound");

            out = run (keys, 4, notes ({ { 0, juce::MidiMessage::noteOn (1, KeyboardSampler::kFirstSliceNote + 3, 1.0f) } }));
            expectEquals (lastNonSilent (out), -1, "keys past the last slice are silent");
            out = run (keys, 4, notes ({ { 0, juce::MidiMessage::noteOn (1, KeyboardSampler::kFirstSliceNote - 1, 1.0f) } }));
            expectEquals (lastNonSilent (out), -1, "and so are keys below C1");
        }

        beginTest ("Find hits marks each hit, and the slices are saved with the project");
        {
            EditorProcessor p;
            p.getDocument().load (share (hitsAt ({ 0, 11025, 22050, 33075 }, 44100)), "beat", "Open");
            p.getDocument().dispatchPendingMessages();
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<EditorView*> (editor.get());
            expect (view != nullptr);
            if (view == nullptr)
                return;

            view->runChop ("findHits");
            const auto chops = p.getChops();
            expectEquals ((int) chops.size(), 3, "three markers make four slices");
            for (size_t i = 0; i < chops.size() && i < 3; ++i)
                expect (std::abs (chops[i] - 11025 * (int) (i + 1)) < 300, "marker near the hit: " + juce::String (chops[i]));
            expect (p.getKeys().getMode() == KeyboardSampler::Mode::slices, "the keys switch to slices");
            expect (view->getWaveView().getMarkers() == chops, "the waveform shows them");

            // double-click a slice to select it (here, straight from the waveform's helper)
            expect (view->getWaveView().sliceAt (15000) == juce::Range<int> (chops[0], chops[1]));

            // the keys play them
            p.setRateAndBufferSizeDetails (44100.0, 512);
            p.prepareToPlay (44100.0, 512);
            juce::AudioBuffer<float> io (2, 512);
            auto midi = notes ({ { 0, juce::MidiMessage::noteOn (1, KeyboardSampler::kFirstSliceNote + 2, 1.0f) } });
            p.processBlock (io, midi);
            expect (io.getMagnitude (0, 0, 512) > 0.2f, "slice 3 plays from its hit");

            juce::MemoryBlock state;
            p.getStateInformation (state);
            EditorProcessor restored;
            restored.setStateInformation (state.getData(), (int) state.getSize());
            restored.getDocument().dispatchPendingMessages();
            expect (restored.getChops() == chops, "chops come back with the project");
            expect (restored.getKeys().getMode() == KeyboardSampler::Mode::slices);

            // lower sensitivity finds fewer; an even split replaces them; an edit that changes
            // the length clears them
            view->getSensitivitySlider().setValue (0.0, juce::sendNotificationSync);
            expect (p.getChops().size() <= chops.size());
            view->runChop ("split8");
            expectEquals ((int) p.getChops().size(), 7);
            view->getWaveView().setSelection ({ 0, 1000 });
            view->runEdit ("delete");
            p.getDocument().dispatchPendingMessages();
            expect (p.getChops().empty(), "a cut clears the slices");

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_CHOP_SNAPSHOT", {}); path.isNotEmpty())
            {
                view->runEdit ("undo");
                view->runChop ("findHits");
                view->getWaveView().setSelection (view->getWaveView().sliceAt (15000));
                auto image = view->createComponentSnapshot (view->getLocalBounds(), true, 1.0f);
                juce::File (path).deleteFile();
                juce::FileOutputStream out { juce::File (path) };
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }
    }
};

KeyboardSamplerTests keyboardSamplerTests;
ChopTests chopTests;
KeysInEditorTests keysInEditorTests;
}
