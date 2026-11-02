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

KeyboardSamplerTests keyboardSamplerTests;
KeysInEditorTests keysInEditorTests;
}
