#include "TestHelpers.h"

#include "NoteEditor.h"
#include "RigEngine.h"
#include "RigNotes.h"
#include "RigSong.h"
#include "RigTakes.h"
#include "RigView.h"
#include "GrooveView.h"

// Edit anything: notes takes (what's played on the Bass, Synth, Sampler, Drum machine or MPC stays
// notes, plays the tool in its section, and can be edited), takes saved with the show, and the
// recording timing that moves what's sung onto the beat.
class RigNotesTests : public juce::UnitTest
{
public:
    RigNotesTests() : juce::UnitTest ("Rig: notes takes, Edit notes, takes saved, recording timing", "Rig") {}

    static void run (RigEngine& engine, int blocks, const float* const* inputs = nullptr, int numInputs = 0)
    {
        juce::AudioBuffer<float> out (2, 480);
        for (int i = 0; i < blocks; ++i)
            engine.process (inputs, numInputs, out.getArrayOfWritePointers(), 2, 480);
    }

    void runTest() override
    {
        beginTest ("notes from what was played; quantize");
        {
            std::vector<rignotes::Event> ev { { 0, 60, 0.8f, 1, true }, { 500, 60, 0, 1, false }, { 1000, 64, 0.5f, 1, true },
                                              { 1500, 67, 1.0f, 1, true }, { 1800, 67, 0, 1, false } };
            const auto notes = rignotes::notesFrom (ev, 4000, 4.0);   // a beat is 1000 samples
            expectEquals ((int) notes.size(), 3);
            expectWithinAbsoluteError (notes[0].length, 0.5, 1.0e-9);
            expectEquals (notes[1].note, 64);
            expectWithinAbsoluteError (notes[1].length, 3.0, 1.0e-9, "held to the end");
            expectWithinAbsoluteError (notes[2].beat, 1.5, 1.0e-9);
            auto q = notes;
            q[2].beat = 1.6;
            rignotes::quantize (q, 0.5);
            expectWithinAbsoluteError (q[2].beat, 1.5, 1.0e-9);
            q[2].beat = 1.6;
            rignotes::quantize (q, 0.5, 0.5f);
            expectWithinAbsoluteError (q[2].beat, 1.55, 1.0e-9, "half way");
            expect (rignotes::noteName (60) == "C3");
            const auto again = rignotes::decode (rignotes::encode (notes));
            expect (again.size() == notes.size(), "saved and read back");
            for (size_t i = 0; i < juce::jmin (again.size(), notes.size()); ++i)
            {
                expectWithinAbsoluteError (again[i].beat, notes[i].beat, 1.0e-4);
                expectWithinAbsoluteError (again[i].length, notes[i].length, 1.0e-4);
                expectEquals (again[i].note, notes[i].note);
                expectWithinAbsoluteError (again[i].velocity, notes[i].velocity, 1.0e-3f);
            }
        }

        beginTest ("the player plays a clip's notes in its section, and lets go on a jump");
        {
            RigNotePlayer player;
            rignotes::Clip clip;
            clip.strip = 2;
            clip.startBar = 1;
            clip.bars = 1;
            clip.notes = { { 0.0, 1.0, 40, 0.9f, 1 }, { 2.0, 3.0, 43, 0.5f, 1 } };
            player.publish ({ clip });
            juce::MidiBuffer a, b, c;
            juce::MidiBuffer* dests[] = { &a, &b, &c };
            const double rate = 1000.0, bpm = 60.0;   // a beat is 1000 samples
            // Beats 3.5 .. 4.5: the clip starts at beat 4 (bar 2).
            player.process (3.5, 1000, bpm, rate, 4, true, dests, 3);
            int ons = 0, offs = 0;
            for (const auto m : c)
            {
                ons += m.getMessage().isNoteOn() ? 1 : 0;
                offs += m.getMessage().isNoteOff() ? 1 : 0;
                if (m.getMessage().isNoteOn())
                    expectEquals (m.samplePosition, 500, "on the bar");
            }
            expectEquals (ons, 1);
            expectEquals (offs, 0, "still held");
            expect (a.isEmpty() && b.isEmpty(), "only its tool");
            c.clear();
            player.process (4.5, 2000, bpm, rate, 4, true, dests, 3);   // beats 4.5 .. 6.5
            bool offAt = false, onAt = false;
            for (const auto m : c)
            {
                offAt = offAt || (m.getMessage().isNoteOff() && m.samplePosition == 500 && m.getMessage().getNoteNumber() == 40);
                onAt = onAt || (m.getMessage().isNoteOn() && m.samplePosition == 1500 && m.getMessage().getNoteNumber() == 43);
            }
            expect (offAt, "the first one ends after its beat");
            expect (onAt, "the second one starts on beat 3 of the bar");
            c.clear();
            player.process (2.0, 100, bpm, rate, 4, true, dests, 3);   // a jump back
            bool letGo = false;
            for (const auto m : c)
                letGo = letGo || (m.getMessage().isNoteOff() && m.getMessage().getNoteNumber() == 43);
            expect (letGo, "nothing hangs after a jump");
            c.clear();
            player.process (4.0, 100, bpm, rate, 4, true, dests, 3, 2, 1.0);
            expect (c.isEmpty(), "muted while it's recorded over");
        }

        beginTest ("Rec into with the Bass records its notes: a clip in the section that plays the bass, saved with the show");
        {
            juce::TemporaryFile showFile (".crateshow");
            {
                RigEngine engine;
                engine.prepare (48000.0, 480);
                RigView view (engine, nullptr);
                view.setSize (1440, 960);
                auto& tl = engine.getTimeline();
                rigsong::addSection (tl, "Verse", 1);
                rigsong::addSection (tl, "Drop", 1);
                tl.changed();
                view.showView ("Edit");
                view.pickTrack (GrooveView::bass);
                view.setRecTarget (1, -1);
                const int layer = view.pressRecInto();
                expectEquals (layer, RigTakes::notesLayer (RigEngine::bassStrip));
                run (engine, 1);
                auto& clock = engine.getTransport();
                expect (clock.isPlaying());
                // Play a note at the top of the Drop, and let go half a bar later.
                const double bpb = clock.getBeatsPerBar();
                for (int i = 0; i < 4000 && engine.getTakes().getState() != RigTakes::State::recording; ++i)
                    run (engine, 1);
                expect (engine.getTakes().getState() == RigTakes::State::recording);
                engine.addMidi (juce::MidiMessage::noteOn (1, 36, 0.9f));
                const double startBeat = clock.getPositionInBeats();
                while (clock.getPositionInBeats() < startBeat + bpb * 0.5)
                    run (engine, 1);
                engine.addMidi (juce::MidiMessage::noteOff (1, 36));
                run (engine, 1);
                engine.addMidi (juce::MidiMessage::noteOn (1, 43, 0.6f));
                run (engine, 4);
                engine.addMidi (juce::MidiMessage::noteOff (1, 43));
                for (int i = 0; i < 4000 && engine.getTakes().getTakesMade() < 1; ++i)
                {
                    run (engine, 1);
                    engine.serviceTakes();
                }
                view.pressRecInto();   // stop
                run (engine, 2);
                engine.serviceTakes();
                const int c = engine.noteClipAt (RigEngine::bassStrip, 1.0);
                expect (c >= 0, "a clip of the bass's notes in the Drop");
                if (c < 0)
                    return;
                const auto& clip = engine.getNoteClips()[(size_t) c];
                expectEquals ((int) clip.notes.size(), 2);
                if (clip.notes.size() == 2)
                {
                    expectEquals (clip.notes[0].note, 36);
                    expect (clip.notes[0].beat < 0.1, "on the section's top: " + juce::String (clip.notes[0].beat));
                    expectWithinAbsoluteError (clip.notes[0].length, bpb * 0.5, 0.1);
                    expectEquals (clip.notes[1].note, 43);
                }
                expect (rigsong::boxBlock (tl, 1, RigTimeline::laneForStrip (RigEngine::bassStrip)) >= 0, "the bass plays in the Drop");
                for (int l = 0; l < RigLooper::kLayers; ++l)
                    expect (engine.getLooper().getState (l) == RigLooper::State::empty, "no loop used");

                // Edit notes: pick all, up an octave, quantize, undo.
                NoteEditor editor (engine, RigEngine::bassStrip, 1.0);
                expect (editor.isValid());
                editor.selectAll();
                editor.transposeSelected (12);
                expectEquals (engine.getNoteClips()[(size_t) c].notes[0].note, 48);
                editor.addNote (2.0, 40, 0.5);
                expectEquals ((int) engine.getNoteClips()[(size_t) c].notes.size(), 3);
                editor.undo();
                expectEquals ((int) engine.getNoteClips()[(size_t) c].notes.size(), 2);
                editor.selectAll();
                editor.deleteSelected();
                expect (engine.getNoteClips()[(size_t) c].notes.empty());
                editor.undo();
                expectEquals ((int) engine.getNoteClips()[(size_t) c].notes.size(), 2);

                if (const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TAKES_SNAPSHOT", {}); path.isNotEmpty())
                {
                    editor.addNote (1.0, 41, 0.5);
                    editor.addNote (2.5, 43, 0.25);
                    editor.addNote (3.0, 46, 1.0);
                    editor.setSize (980, 560);
                    const auto image = editor.createComponentSnapshot (editor.getLocalBounds(), true, 2.0f);
                    juce::File file (path + "-notes.png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                    for (int k = 0; k < 3; ++k)
                        editor.undo();
                }

                // Saved: the clip in the show, the takes beside it.
                auto xml = engine.saveShow().createXml();
                expect (xml != nullptr && xml->writeTo (showFile.getFile()));
                expect (engine.saveShowFile (showFile.getFile()).ok);
            }
            {
                RigEngine engine;
                engine.prepare (48000.0, 480);
                std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (showFile.getFile()));
                expect (xml != nullptr);
                if (xml == nullptr)
                    return;
                engine.loadShow (juce::ValueTree::fromXml (*xml));
                engine.readLoopAudio (showFile.getFile());
                const int c = engine.noteClipAt (RigEngine::bassStrip, 1.0);
                expect (c >= 0, "the clip came back");
                if (c >= 0)
                    expectEquals (engine.getNoteClips()[(size_t) c].notes[0].note, 48, "with its edits");
                const auto* set = engine.getTakes().setFor (RigTakes::notesLayer (RigEngine::bassStrip));
                expect (set != nullptr && ! set->takes.empty(), "and its takes");
                if (set != nullptr && ! set->takes.empty())
                {
                    expectEquals ((int) set->takes[0].notes.size(), 2);
                    expect (set->takes[0].audio.getNumSamples() > 0, "with their sound");
                }
                // Make it sound: onto a loop, in place of the notes.
                const int l = engine.takeToLoop (RigTakes::notesLayer (RigEngine::bassStrip));
                expectEquals (l, 0);
                expect (engine.noteClipAt (RigEngine::bassStrip, 1.0) < 0);
                expect (engine.getLooper().getState (0) == RigLooper::State::playing);
            }
            loopsFolderCleanup (showFile.getFile());
        }

        beginTest ("loop takes are saved with the show too");
        {
            juce::TemporaryFile showFile (".crateshow");
            {
                RigEngine engine;
                engine.prepare (48000.0, 480);
                RigTakes::Set set;
                set.layer = 2;
                set.source = RigLooper::vocoder;
                set.startBar = 0;
                set.bars = 1;
                for (int k = 0; k < 2; ++k)
                {
                    RigTakes::Take t;
                    t.audio.setSize (2, 96000);
                    t.audio.clear();
                    t.audio.setSample (0, 10, 0.25f * (k + 1));
                    t.beats = 4;
                    t.number = k + 1;
                    set.takes.push_back (std::move (t));
                }
                set.playing = 1;
                engine.getTakes().restore (set);
                engine.useTake (2, 1);
                auto xml = engine.saveShow().createXml();
                expect (xml->writeTo (showFile.getFile()));
                expect (engine.saveShowFile (showFile.getFile()).ok);
            }
            {
                RigEngine engine;
                engine.prepare (48000.0, 480);
                std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (showFile.getFile()));
                engine.loadShow (juce::ValueTree::fromXml (*xml));
                engine.readLoopAudio (showFile.getFile());
                const auto* set = engine.getTakes().setFor (2);
                expect (set != nullptr && set->takes.size() == 2, "both takes");
                if (set != nullptr && set->takes.size() == 2)
                {
                    expectEquals (set->playing, 1);
                    expectWithinAbsoluteError (set->takes[0].audio.getSample (0, 10), 0.25f, 1.0e-4f);
                    expectEquals (set->nextNumber, 3);
                }
            }
            loopsFolderCleanup (showFile.getFile());
        }

        beginTest ("recording timing: a loop from the mic is moved earlier by the round trip; an overdub is written earlier");
        {
            RigLooper looper;
            looper.prepare (1000.0, 100);   // a beat is 1000 samples at 60 BPM
            looper.maintain();
            looper.setLiveDelay (30);
            auto& layer = looper.getLayer (0);
            layer.source = RigLooper::input;
            layer.bars = 1;
            juce::AudioBuffer<float> in (2, 100), out (2, 100);
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            sources[RigLooper::input] = &in;
            RigLooper::Clock clock;
            clock.bpm = 60;
            clock.beatsPerBar = 4;
            clock.playing = true;
            looper.pressRecord (0);
            // A click "sung" on beat 1 arrives 30 samples late (at sample 1030).
            for (int b = 0; b < 60; ++b)
            {
                in.clear();
                const int at = 1030 - b * 100;
                if (juce::isPositiveAndBelow (at, 100))
                    in.setSample (0, at, 1.0f);
                clock.beat = b * 0.1;
                looper.process (clock, sources, out);
            }
            looper.maintain();
            const auto audio = looper.copyLayerAudio (0);
            expect (audio.getNumSamples() >= 4000);
            if (audio.getNumSamples() >= 4000)
            {
                expectWithinAbsoluteError (audio.getSample (0, 1000), 1.0f, 1.0e-6f, "on the beat");
                expectWithinAbsoluteError (audio.getSample (0, 1030), 0.0f, 1.0e-6f);
            }

            RigEngine engine;
            engine.setTimingOffsetMs (5.0f, false);
            engine.prepare (48000.0, 480);
            expectEquals (engine.getLiveDelaySamples(), 240, "5 ms at 48k (no interface in a test)");
            engine.setTimingOffsetMs (0.0f, false);
        }
    }

    static void loopsFolderCleanup (const juce::File& show)
    {
        RigEngine::loopsFolder (show).deleteRecursively();
    }
};

static RigNotesTests rigNotesTests;
