#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigChords.h"
#include "RigView.h"
#include "ChordsView.h"
#include "GrooveView.h"
#include "VocoderScreen.h"
#include "Parameters.h"   // the vocoder's
#include "core/AudioFiles.h"

// The chord builder: your own chords on 16 pads, onto any instrument, from clicks, the Launchkey's
// pads or keys, a progression on the clock, and out to MIDI files for Logic.
class RigChordsTests : public juce::UnitTest
{
public:
    RigChordsTests() : juce::UnitTest ("Rig chord builder", "Rig") {}

    static void run (RigEngine& engine, int blocks = 1, int size = 480)
    {
        juce::AudioBuffer<float> out (2, size);
        for (int b = 0; b < blocks; ++b)
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, size);
    }
    static bool synthHas (RigEngine& e, int note) { return e.getSynth().keyboardState.isNoteOnForChannels (0xffff, note); }
    static int synthCount (RigEngine& e)
    {
        int n = 0;
        for (int note = 0; note < 128; ++note)
            n += synthHas (e, note) ? 1 : 0;
        return n;
    }
    juce::Array<int> notes (std::initializer_list<int> n) { return juce::Array<int> (n); }

    void runTest() override
    {
        beginTest ("chord names from any notes");
        {
            const std::pair<juce::Array<int>, const char*> cases[] = {
                { notes ({ 48, 52, 55 }), "C" }, { notes ({ 57, 60, 64, 67 }), "Am7" }, { notes ({ 52, 55, 60 }), "C/E" },
                { notes ({ 48, 52, 55, 57 }), "C6" }, { notes ({ 50, 53, 57, 60, 64 }), "Dm9" }, { notes ({ 48, 55 }), "C5" },
                { notes ({ 48, 53, 55 }), "Csus4" }, { notes ({ 59, 62, 65 }), "Bdim" }, { notes ({ 53, 57, 60, 64 }), "Fmaj7" },
                { notes ({ 55, 59, 62, 65 }), "G7" }, { notes ({ 48, 52, 54 }), "C E F#" }, { notes ({ 60 }), "C" },
                { notes ({ 43, 48, 52, 55 }), "C/G" } };
            for (const auto& c : cases)
                expectEquals (RigChords::nameOf (c.first), juce::String (c.second));
            expectEquals (RigChords::nameOf ({}), juce::String());
            expectEquals (RigChords::noteName (60), juce::String ("C3"));
        }

        beginTest ("fill from a key, as the vocoder's chord buttons, and voicing");
        {
            RigChords chords;
            chords.fillFromKey (0, 0, 0);
            expectEquals (chords.padName (0), juce::String ("C"));
            expectEquals (chords.padName (1), juce::String ("Dm"));
            expectEquals (chords.padName (5), juce::String ("Am"));
            expectEquals (chords.padName (6), juce::String ("Bdim"));
            expectEquals (chords.getPad (7).notes.getFirst(), chords.getPad (0).notes.getFirst() + 12, "pad 8: the key's chord an octave up");
            chords.fillFromKey (2, 1, 1);
            expectEquals (chords.padName (0), juce::String ("Dm7"));

            chords.setPad (0, notes ({ 55, 48, 52 }));
            expect (chords.getPad (0).notes == notes ({ 48, 52, 55 }), "kept lowest first");
            chords.invert (0, 1);
            expect (chords.getPad (0).notes == notes ({ 52, 55, 60 }), "first inversion");
            expectEquals (chords.padName (0), juce::String ("C/E"));
            chords.invert (0, -1);
            expect (chords.getPad (0).notes == notes ({ 48, 52, 55 }), "back down");
            chords.octave (0, 1);
            expect (chords.getPad (0).notes == notes ({ 60, 64, 67 }));
            chords.setPad (0, notes ({ 48, 52, 55, 59 }));
            chords.spread (0);
            expect (chords.getPad (0).notes == notes ({ 48, 55, 64, 71 }), "spread opens it up");
            chords.close (0);
            expect (chords.getPad (0).notes == notes ({ 48, 52, 55, 59 }), "close packs it again");
            chords.toggleNote (0, 62);
            expectEquals (chords.padName (0), juce::String ("Cmaj9"));
            chords.toggleNote (0, 62);
            expectEquals (chords.padName (0), juce::String ("Cmaj7"));
            chords.setPad (1, notes ({ 48, 52, 55 }), "Verse");
            expectEquals (chords.padName (1), juce::String ("Verse"), "your own name");
            chords.copyPad (1, 9);
            expectEquals (chords.padName (9), juce::String ("Verse"));
            chords.setPad (2, notes ({ 30, 31, 32, 33, 34, 35, 36, 37, 38, 39 }));
            expectEquals (chords.getPad (2).notes.size(), RigChords::kMaxNotes, "up to 8 notes");
        }

        beginTest ("a pad plays its chord on the instrument it's sent to, and lets go");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.target = (int) RigRouting::Dest::synth;
            chords.press (0);
            run (engine);
            expect (synthHas (engine, 48) && synthHas (engine, 52) && synthHas (engine, 55));
            expectEquals (chords.getSounding(), 0);
            expect (chords.isNoteSounding (52) && ! chords.isNoteSounding (53));
            chords.release (0);
            run (engine);
            expectEquals (synthCount (engine), 0);
            expect (! chords.anySounding());

            // The picked track: the keys' instrument.
            chords.target = RigChords::kFollowKeys;
            engine.getRouting().keysTo = (int) RigRouting::Dest::bass;
            chords.press (0);
            run (engine);
            expect (engine.getBass().keyboardState.isNoteOnForChannels (0xffff, 48), "follows the keys to the bass");
            expectEquals (synthCount (engine), 0);
            chords.release (0);
            run (engine);
            expect (! engine.getBass().keyboardState.isNoteOnForChannels (0xffff, 48));

            // Two chords sharing a note: letting one go leaves the other's.
            chords.target = (int) RigRouting::Dest::synth;
            chords.setPad (1, notes ({ 48, 53, 57 }));
            chords.press (0);
            chords.press (1);
            run (engine);
            chords.release (0);
            run (engine);
            expect (synthHas (engine, 48) && synthHas (engine, 53) && ! synthHas (engine, 52), "the shared C stays with pad 2");
            chords.release (1);
            run (engine);
            expectEquals (synthCount (engine), 0);

            // Stop quiets it.
            chords.hold = true;
            chords.press (0);
            run (engine);
            engine.allNotesOff();
            run (engine);
            expect (! chords.anySounding(), "Stop lets held chords go");
        }

        beginTest ("Hold keeps a chord until the next, or the same again");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.setPad (1, notes ({ 53, 57, 60 }));
            chords.hold = true;
            chords.press (0);
            chords.release (0);
            run (engine);
            expect (synthHas (engine, 52), "still sounding after letting go");
            expect (chords.isPadDown (0));
            chords.press (1);
            chords.release (1);
            run (engine);
            expect (! synthHas (engine, 52) && synthHas (engine, 57), "the next chord takes over");
            expect (! chords.isPadDown (0) && chords.isPadDown (1));
            chords.press (1);
            run (engine);
            expectEquals (synthCount (engine), 0, "the same pad again lets it go");
        }

        beginTest ("the Launchkey's pads, its lowest keys, every key, and Catch");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            chords.setPad (1, notes ({ 50, 53, 57 }));
            chords.trigger = (int) RigChords::Trigger::pads;
            engine.addMidiNow (juce::MidiMessage::noteOn (10, RigRouting::kFirstPadNote + 1, 0.9f));
            run (engine);
            expect (synthHas (engine, 50) && synthHas (engine, 57), "pad 2 plays chord 2");
            // The mode changes while the pad is down: letting go still stops it.
            chords.trigger = (int) RigChords::Trigger::screen;
            engine.addMidiNow (juce::MidiMessage::noteOff (10, RigRouting::kFirstPadNote + 1));
            run (engine);
            expectEquals (synthCount (engine), 0);

            chords.trigger = (int) RigChords::Trigger::lowKeys;
            chords.lowKey = 36;
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 37, 0.9f));
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 72, 0.9f));
            run (engine);
            expect (synthHas (engine, 50) && synthHas (engine, 53) && ! synthHas (engine, 37), "the low key plays chord 2, not itself");
            expect (synthHas (engine, 72), "keys above play as usual");
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 37));
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 72));
            run (engine);
            expectEquals (synthCount (engine), 0);

            chords.trigger = (int) RigChords::Trigger::everyKey;
            chords.setPad (0, notes ({ 48, 52, 55, 59 }));
            chords.shapePad = 0;
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 53, 0.9f));
            run (engine);
            expect (synthHas (engine, 53) && synthHas (engine, 57) && synthHas (engine, 60) && synthHas (engine, 64),
                    "F plays the maj7 shape from F");
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 53));
            run (engine);
            expectEquals (synthCount (engine), 0);

            chords.trigger = (int) RigChords::Trigger::screen;
            chords.catchFromKeys (5);
            expect (chords.isCatching());
            for (int n : { 55, 59, 62, 65 })
                engine.addMidiNow (juce::MidiMessage::noteOn (1, n, 0.8f));
            run (engine);
            expect (synthHas (engine, 59), "caught keys still play");
            expect (! chords.serviceCatch(), "not done while they're down");
            for (int n : { 55, 59, 62, 65 })
                engine.addMidiNow (juce::MidiMessage::noteOff (1, n));
            run (engine);
            expect (chords.serviceCatch());
            expectEquals (chords.padName (5), juce::String ("G7"));
            expect (! chords.isCatching());
        }

        beginTest ("the key map: every key plays a chord of the key");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            chords.trigger = (int) RigChords::Trigger::keyMap;
            chords.setKey (0, 0, 0);   // C major, triads
            auto play = [&] (int note)
            {
                engine.addMidiNow (juce::MidiMessage::noteOn (1, note, 0.9f));
                run (engine);
                juce::Array<int> on;
                for (int n = 0; n < 128; ++n)
                    if (synthHas (engine, n))
                        on.add (n);
                engine.addMidiNow (juce::MidiMessage::noteOff (1, note));
                run (engine);
                expectEquals (synthCount (engine), 0, "letting go stops it");
                return on;
            };

            // Each scale note plays its own chord, rooted on the key.
            expectEquals (chords.mapChordName (50), juce::String ("Dm"));
            expectEquals (chords.mapDegreeName (50), juce::String ("ii"));
            expectEquals (chords.mapDegreeName (55), juce::String ("V"));
            expectEquals (chords.mapDegreeName (59), juce::String (juce::CharPointer_UTF8 ("vii\xc2\xb0")));
            expect (play (50) == notes ({ 50, 53, 57, 62 }), "D3 plays Dm from D3");
            expect (play (38) == notes ({ 38, 41, 45, 50 }), "an octave down, Dm an octave down");
            expect (play (81) == notes ({ 81, 84, 88, 93 }), "A5 plays Am up there");
            // Off the scale: the chord of the note below, as single notes, or nothing.
            expect (play (51) == notes ({ 50, 53, 57, 62 }), "Eb plays the chord below it (Dm)");
            chords.mapOffScale = (int) RigChords::OffScale::notes;
            chords.rebuildMap();
            expect (play (51) == notes ({ 51 }), "or just itself");
            chords.mapOffScale = (int) RigChords::OffScale::silent;
            chords.rebuildMap();
            expect (play (51).isEmpty(), "or nothing");

            // Another key and type: A minor sevenths. G plays G7 (VII7), A plays Am7.
            chords.setKey (9, 1, 1);
            expectEquals (chords.mapChordName (57), juce::String ("Am7"));
            expectEquals (chords.mapDegreeName (57), juce::String ("i7"));
            expectEquals (chords.mapChordName (55), juce::String ("G7"));
            expect (play (57).getFirst() == 57, "Am7 from A3");

            // White keys: C to B play I to vii of the key, whatever it is (here E minor).
            chords.mapLayout = (int) RigChords::MapLayout::whiteKeys;
            chords.mapOffScale = (int) RigChords::OffScale::snap;
            chords.setKey (4, 1, 0);
            const char* eMinor[] = { "Em", "F#dim", "G", "Am", "Bm", "C", "D" };
            const int whites[] = { 48, 50, 52, 53, 55, 57, 59 };
            for (int i = 0; i < 7; ++i)
                expectEquals (chords.mapChordName (whites[i]), juce::String (eMinor[i]));
            expectEquals (chords.mapChordName (61), chords.mapChordName (60), "a black key: the white key's chord below");
            const auto em = play (48);
            expect (em.contains (52) && em.contains (55) && em.contains (59), "C3 plays Em (E3 G3 B3)");
            expect (em.getFirst() >= 48 && em.getLast() < 72, "in the octave of the key you press");

            // Your pads: C to B pads 1-7, the black keys 8-12, in every octave.
            chords.mapLayout = (int) RigChords::MapLayout::yourPads;
            chords.rebuildMap();
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.setPad (7, notes ({ 53, 57, 60, 64 }), "Mine");
            expectEquals (chords.mapChordName (61), juce::String ("Mine"), "C# plays pad 8");
            expect (play (49) == notes ({ 53, 57, 60, 64 }), "in the pads' own octave, C# plays pad 8 as it is");
            expect (play (36) == notes ({ 36, 40, 43 }), "C1 plays pad 1 an octave down");
            chords.mapVoicing = (int) RigChords::MapVoicing::sameOctave;
            expect (play (36) == notes ({ 48, 52, 55 }), "Same octave: wherever you play it");
            chords.mapOctave = 1;
            expect (play (84) == notes ({ 60, 64, 67 }), "Octave moves them all");
            chords.mapOctave = 0;

            // Bass: the root under each chord.
            chords.mapLayout = (int) RigChords::MapLayout::scaleNotes;
            chords.mapVoicing = (int) RigChords::MapVoicing::followKeys;
            chords.setKey (0, 0, 0);
            chords.mapBass = true;
            expect (play (55) == notes ({ 43, 55, 59, 62, 67 }), "G with G2 under it");
            chords.mapBass = false;

            // Smooth voicing: C then F then G stay close (no chord jumps up a fourth or fifth).
            chords.mapVoicing = (int) RigChords::MapVoicing::smooth;
            auto mean = [] (const juce::Array<int>& a) { double m = 0; for (int n : a) m += n; return a.isEmpty() ? 0.0 : m / a.size(); };
            chords.hold = false;
            const auto c = play (48), f = play (53), g = play (55);
            expect (std::abs (mean (f) - mean (c)) <= 3.0 && std::abs (mean (g) - mean (f)) <= 3.0, "the chords glide");
            expectEquals (RigChords::nameOf (f).substring (0, 1), juce::String ("F"), "still an F chord");
            expect (c.size() == 4 && f.size() == 4 && g.size() == 4);

            // Hold: a key latches its chord until the next.
            chords.mapVoicing = (int) RigChords::MapVoicing::followKeys;
            chords.hold = true;
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 57, 0.9f));
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 57));
            run (engine);
            expect (synthHas (engine, 57) && synthHas (engine, 60), "Am held");
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 53, 0.9f));
            run (engine);
            expect (synthHas (engine, 53) && ! synthHas (engine, 64), "F takes over");
            chords.stopAll();
            chords.hold = false;
            run (engine);
            expectEquals (synthCount (engine), 0);

            // Below the keyboard split, keys play as usual.
            engine.getRouting().splitNote = 48;
            expect (play (40) == notes ({ 40 }) || play (40).isEmpty(), "under the split it isn't a chord");
            engine.getRouting().splitNote = 0;

            // Saved, and loaded again.
            chords.mapLayout = (int) RigChords::MapLayout::whiteKeys;
            chords.mapVoicing = (int) RigChords::MapVoicing::smooth;
            chords.mapBass = true;
            chords.mapOctave = -1;
            chords.setKey (7, 2, 1);
            RigChords other;
            other.fromTree (chords.toTree());
            expectEquals (other.mapLayout.load(), (int) RigChords::MapLayout::whiteKeys);
            expectEquals (other.mapVoicing.load(), (int) RigChords::MapVoicing::smooth);
            expect (other.mapBass.load());
            expectEquals (other.mapOctave.load(), -1);
            expectEquals (other.mapChordName (48), chords.mapChordName (48));
            expectEquals (other.mapChordName (48), juce::String ("Gm7"), "G Dorian sevenths: C is i7");
            expectEquals (other.fillKey, 7);
        }

        beginTest ("the full keyboard: a different chord on every key, your keys, your sound");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            chords.trigger = (int) RigChords::Trigger::keyMap;
            chords.mapLayout = (int) RigChords::MapLayout::fullKeyboard;
            chords.setKey (0, 0, 0);
            auto play = [&] (int note)
            {
                engine.addMidiNow (juce::MidiMessage::noteOn (1, note, 0.9f));
                run (engine);
                juce::Array<int> on;
                for (int n = 0; n < 128; ++n)
                    if (synthHas (engine, n))
                        on.add (n);
                engine.addMidiNow (juce::MidiMessage::noteOff (1, note));
                run (engine);
                return on;
            };

            // Every key of a Launchkey 61 (C1-C6) plays a chord, and no two the same.
            juce::StringArray seen, names;
            for (int k = 36; k <= 96; ++k)
            {
                const auto on = play (k);
                expect (on.size() >= 3, "key " + juce::String (k) + " plays a chord");
                expectEquals (synthCount (engine), 0);
                juce::StringArray text;
                for (int n : on)
                    text.add (juce::String (n));
                expect (! seen.contains (text.joinIntoString (" ")), "key " + juce::String (k) + "'s chord is its own");
                seen.add (text.joinIntoString (" "));
                expect (chords.mapChordName (k).isNotEmpty());
                names.addIfNotAlreadyThere (chords.mapChordName (k) + "@" + juce::String (k / 12));
            }
            expectEquals (names.size(), 61);
            expectEquals (chords.mapChordName (48), juce::String ("Cmaj7"), "C2's octave: sevenths");
            expectEquals (chords.mapChordName (36), juce::String ("C"), "C1's octave: triads");
            expectEquals (chords.mapChordName (60), juce::String ("Cadd9"));
            expectEquals (chords.mapChordName (44), juce::String ("Ab"), "Ab: borrowed from C minor");
            expectEquals (chords.mapDegreeName (44), juce::String ("bVI"));
            expectEquals (chords.mapDegreeName (46), juce::String ("bVII"));
            expect (play (60).getFirst() == 60, "rooted on the key");

            // Your keys: starts as the map, then a key of your own, caught while the map plays.
            chords.copyMapToYourKeys();
            expectEquals (chords.mapLayout.load(), (int) RigChords::MapLayout::yourKeys);
            expect (play (60) == chords.getKeyChord (60) && ! chords.getKeyChord (60).isEmpty(), "kept what it played");
            chords.setKeyChord (61, notes ({ 57, 61, 64, 67 }));
            expectEquals (chords.mapChordName (61), juce::String ("A7"));
            expect (play (61) == notes ({ 57, 61, 64, 67 }), "your chord, where you put it");
            chords.catchForKey (62);
            expectEquals (chords.getCatchKey(), 62);
            for (int n : { 50, 53, 57 })
                engine.addMidiNow (juce::MidiMessage::noteOn (1, n, 0.8f));
            run (engine);
            expect (synthHas (engine, 50) && synthHas (engine, 53) && synthHas (engine, 57), "caught keys play as notes, not their chords");
            for (int n : { 50, 53, 57 })
                engine.addMidiNow (juce::MidiMessage::noteOff (1, n));
            run (engine);
            expect (chords.serviceCatch());
            expect (chords.getKeyChord (62) == notes ({ 50, 53, 57 }));
            expect (play (62) == notes ({ 50, 53, 57 }));
            chords.setKeyChord (62, {});
            expect (play (62) == notes ({ 62 }), "an empty key plays its note");
            RigChords other;
            other.fromTree (chords.toTree());
            expect (other.getKeyChord (61) == notes ({ 57, 61, 64, 67 }), "your keys are saved");
            expectEquals (other.mapChordName (61), juce::String ("A7"));

            // In a one-tool plugin (Kaleidobox Synth in Logic), the chords play that tool whatever they're sent to.
            RigEngine tool;
            tool.prepare (48000.0, 480);
            tool.setOnlyTool (RigEngine::synthStrip);
            auto& tc = tool.getChords();
            tc.target = (int) RigRouting::Dest::bass;
            tc.trigger = (int) RigChords::Trigger::keyMap;
            tc.mapLayout = (int) RigChords::MapLayout::fullKeyboard;
            tc.setKey (0, 0, 0);
            tool.addMidiNow (juce::MidiMessage::noteOn (1, 48, 0.9f));
            run (tool);
            expect (synthHas (tool, 48) && synthHas (tool, 52) && synthHas (tool, 55) && synthHas (tool, 59), "Cmaj7 on the Synth plugin");
            tool.addMidiNow (juce::MidiMessage::noteOff (1, 48));
            run (tool);
            expectEquals (synthCount (tool), 0);

            // Your sound: a note in the file's name, or heard.
            expectEquals (ChordsView::noteInName ("Pad_Cmin_120"), 0);
            expectEquals (ChordsView::noteInName ("Warm Keys - F#maj"), 6);
            expectEquals (ChordsView::noteInName ("Bass_Bb_90bpm"), 10);
            expectEquals (ChordsView::noteInName ("Kick 01"), -1);
            juce::AudioBuffer<float> tone (1, 44100);
            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 44100.0));
            juce::String how;
            expectEquals (ChordsView::guessRoot (tone, "my tone", how), 57, "220 Hz is A2 (57)");
            expectEquals (ChordsView::guessRoot (tone, "tone_C", how), 60, "the name's C, near the pitch heard");
            juce::TemporaryFile wav (".wav");
            expect (crate::AudioFiles::write (wav.getFile(), tone));
            const auto named = wav.getFile().getSiblingFile ("Soft pad_A.wav");
            expect (wav.getFile().copyFileTo (named));
            ChordsView cv (engine);
            const auto said = cv.loadSound (named);
            expect (said.contains ("Soft pad_A"), said);
            expectEquals (engine.getSampler().getKeys().getRootNote(), 57);
            expectEquals (chords.target.load(), (int) RigRouting::Dest::sampler, "the chords play it");
            expect (engine.getSampler().getDocument().hasAudio());
            named.deleteFile();
        }

        beginTest ("Strum spreads the notes out");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.strum = 3;   // 90 ms
            chords.press (0);
            run (engine);
            expect (synthHas (engine, 48) && ! synthHas (engine, 52), "the low note first");
            run (engine, 9);
            expect (synthHas (engine, 52) && ! synthHas (engine, 55));
            run (engine, 10);
            expect (synthHas (engine, 55));
            chords.release (0);
            run (engine);
            expectEquals (synthCount (engine), 0);
            // Let go mid-strum: the notes still to come never play.
            chords.press (0);
            run (engine);
            chords.release (0);
            run (engine, 25);
            expectEquals (synthCount (engine), 0);
        }

        beginTest ("the progression plays on the clock");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.setPad (1, notes ({ 53, 57, 60 }));
            for (auto& s : chords.steps)
                s = -1;
            chords.steps[0] = 0;
            chords.steps[1] = 1;
            chords.stepLength = 0;   // a beat each
            expectEquals (chords.progressionSteps(), 2);
            expectEquals (chords.progressionBeats(), 2.0);
            chords.progressionOn = true;
            engine.getTransport().setBpm (120.0);
            run (engine);
            expectEquals (synthCount (engine), 0, "quiet while stopped");
            engine.getTransport().play();
            run (engine);
            expect (synthHas (engine, 52), "step 1");
            expectEquals (chords.getPlayingStep(), 0);
            run (engine, 50);   // a beat at 120 is 24000 samples
            expect (synthHas (engine, 57) && ! synthHas (engine, 52), "step 2");
            expectEquals (chords.getPlayingStep(), 1);
            run (engine, 50);
            expect (synthHas (engine, 52), "and round again");
            chords.progressionOn = false;
            run (engine);
            expectEquals (synthCount (engine), 0);
        }

        beginTest ("To loop: the progression stops at the end of its take, so the chords don't play twice");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            ChordsView cv (engine);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.setPad (1, notes ({ 53, 57, 60 }));
            for (auto& s : chords.steps)
                s = -1;
            chords.steps[0] = 0;
            chords.steps[1] = 1;
            chords.stepLength = 2;   // a bar each: two bars
            chords.progressionOn = true;
            engine.getTransport().setBpm (120.0);
            engine.getTransport().play();
            run (engine);
            expect (synthHas (engine, 52), "the progression plays");
            const auto said = cv.pressToLoop();
            expect (said.contains ("layer 1"), said);
            // The take: from the next bar (beat 4) for two bars, to beat 12; a beat is 50 blocks.
            run (engine, 570);
            expectEquals ((int) engine.getLooper().getState (0), (int) RigLooper::State::recording);
            expect (chords.progressionOn.load(), "still playing into the take");
            expect (synthCount (engine) > 0);
            run (engine, 40);
            expectEquals ((int) engine.getLooper().getState (0), (int) RigLooper::State::playing);
            expect (! chords.progressionOn.load(), "the loop plays it now, not the progression too");
            expectEquals (synthCount (engine), 0, "no chord left playing live over the loop");
            expectEquals (chords.getPlayingStep(), -1);
            // Change a pad: the loop keeps the old chord and nothing new plays on top of it.
            chords.setPad (0, notes ({ 50, 53, 57 }));
            run (engine, 200);
            expectEquals (synthCount (engine), 0, "a changed pad doesn't stack on the loop");
            cv.refresh();
            // Play it brings it back, and it plays on.
            cv.pressPlayProgression();
            expect (chords.progressionOn.load());
            run (engine, 450);
            expect (chords.progressionOn.load() && synthCount (engine) > 0, "back on, it keeps playing");

            // Changed your mind (To loop again before the take starts): the progression plays on.
            const auto again = cv.pressToLoop();   // the next free layer
            expect (again.contains ("layer 2"), again);
            run (engine, 2);
            expectEquals ((int) engine.getLooper().getState (1), (int) RigLooper::State::armed);
            engine.getLooper().pressRecord (1);   // cancel
            run (engine, 900);
            expectEquals ((int) engine.getLooper().getState (1), (int) RigLooper::State::empty);
            expect (chords.progressionOn.load() && synthCount (engine) > 0, "a cancelled take leaves it playing");
        }

        beginTest ("All off, and Chords off, stop every chord (a latched one, the progression)");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            ChordsView cv (engine);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.setPad (0, notes ({ 48, 52, 55 }));
            chords.setPad (1, notes ({ 53, 57, 60 }));
            for (auto& s : chords.steps)
                s = -1;
            chords.steps[0] = 1;
            chords.hold = true;
            chords.press (0);
            engine.getTransport().play();
            chords.progressionOn = true;
            run (engine, 2);
            expect (synthCount (engine) >= 6, "a latched chord and the progression");
            cv.pressAllOff();
            run (engine, 2);
            expectEquals (synthCount (engine), 0, "All off: nothing plays");
            run (engine, 200);
            expectEquals (synthCount (engine), 0, "and nothing comes back on the next step");
            expect (! chords.progressionOn.load());

            chords.progressionOn = true;
            run (engine, 2);
            expect (synthCount (engine) >= 3);
            cv.pressChordsOn();   // off
            run (engine, 200);
            expectEquals (synthCount (engine), 0, "Chords off stops the progression too");
            cv.pressChordsOn();   // on again
            chords.hold = false;
        }

        beginTest ("Your keys copied from the map keep its Bass and Octave once, not twice");
        {
            RigChords chords;
            chords.fillFromKey (0, 0, 0);   // C major
            chords.mapLayout = (int) RigChords::MapLayout::scaleNotes;
            chords.mapBass = true;
            chords.mapOctave = 1;
            chords.rebuildMap();
            std::array<juce::Array<int>, 3> before;
            const int keys[] = { 48, 62, 67 };
            for (int i = 0; i < 3; ++i)
                before[(size_t) i] = chords.mapNotesFor (keys[i]);
            chords.copyMapToYourKeys();
            expectEquals (chords.mapLayout.load(), (int) RigChords::MapLayout::yourKeys);
            for (int i = 0; i < 3; ++i)
                expect (chords.mapNotesFor (keys[i]) == before[(size_t) i],
                        "key " + juce::String (keys[i]) + " plays what it did: " + juce::String (chords.mapNotesFor (keys[i]).size()) + " notes");
            chords.mapBass = false;
            chords.mapOctave = 0;
            expectEquals (chords.mapNotesFor (48).size(), before[0].size() - 1, "the chord alone: the bass wasn't copied into it");
            expectEquals (chords.mapNotesFor (48).getFirst(), before[0][1] - 12, "and the octave wasn't either");
        }

        beginTest ("saved with the show, kept by New song, and as sets");
        {
            RigEngine engine;
            auto& chords = engine.getChords();
            expect (engine.isBlank());
            chords.setPad (12, notes ({ 49, 53, 56 }), "Mine");
            chords.steps[5] = 12;
            chords.hold = true;
            expect (engine.isBlank(), "your chords don't make a song unsaved");
            const auto show = engine.saveShow();
            RigEngine other;
            other.loadShow (show);
            expectEquals (other.getChords().padName (12), juce::String ("Mine"));
            expect (other.getChords().getPad (12).notes == notes ({ 49, 53, 56 }));
            expectEquals (other.getChords().steps[5].load(), 12);
            expect (other.getChords().hold.load());
            engine.newShow();
            expectEquals (chords.padName (12), juce::String ("Mine"), "New song keeps your chords");
            // An old show without chords: the chords a new Kaleidobox has.
            auto old = show.createCopy();
            old.removeChild (old.getChildWithName (RigChords::kTreeType), nullptr);
            other.loadShow (old);
            expect (other.getChords().isEmpty (12));
            expectEquals (other.getChords().padName (0), juce::String ("C"));

            juce::TemporaryFile temp (".chords");
            expect (chords.saveSet (temp.getFile()));
            other.getChords().target = (int) RigRouting::Dest::bass;
            expect (other.getChords().loadSet (temp.getFile()));
            expectEquals (other.getChords().padName (12), juce::String ("Mine"));
            expectEquals (other.getChords().target.load(), (int) RigRouting::Dest::bass, "a set leaves where they play");

            for (int i = 0; i < RigChords::starterNames().size(); ++i)
            {
                other.getChords().loadStarter (i);
                expect (i == RigChords::starterNames().size() - 1 ? other.getChords().progressionSteps() == 0
                                                                  : other.getChords().progressionSteps() == 4);
            }
        }

        beginTest ("Live key: change key on the fly, the nearest way, on the beat or bar");
        {
            expectEquals (RigChords::shiftName ("Dm7", 2), juce::String ("Em7"));
            expectEquals (RigChords::shiftName ("C/E", 1), juce::String ("C#/F"));
            expectEquals (RigChords::shiftName ("Bbmaj7", -3), juce::String ("Gmaj7"));
            expectEquals (RigChords::shiftName ("Big F", 2), juce::String ("Big F"), "your own names stay");

            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.fillFromKey (2, 1, 1);   // D minor sevenths: pad 1 Dm7
            expectEquals (chords.getLiveKey(), 2);
            expect (chords.isLiveKeyHome());

            // Stopped: at once. D to E is up 2, D to B down 3 (not up 9).
            chords.setLiveKey (4);
            expectEquals (chords.getLiveShift(), 2);
            run (engine);
            expectEquals (chords.getPlayingShift(), 2);
            expectEquals (chords.livePadName (0), juce::String ("Em7"));
            chords.press (0);
            run (engine);
            const auto dm7 = chords.getPad (0).notes;
            for (int n : dm7)
                expect (synthHas (engine, n + 2), "the pad plays Em7");
            chords.release (0);
            run (engine);
            expectEquals (synthCount (engine), 0, "and lets go of the moved notes");
            chords.setLiveKey (11);
            expectEquals (chords.getLiveShift(), -3, "the nearest way down");
            chords.nudgeLiveOctave (1);
            expectEquals (chords.getLiveShift(), 9);
            chords.nudgeLiveOctave (5);
            expectEquals (chords.getLiveOctave(), 2, "two octaves at most");
            chords.liveKeyHome();
            expect (chords.isLiveKeyHome() && chords.getLiveOctave() == 0);
            run (engine);

            // Playing, Next bar: a held chord waits for the bar line, then moves there.
            engine.getTransport().setBpm (120.0);
            chords.hold = true;
            chords.press (0);
            engine.getTransport().play();
            run (engine, 10);   // beat 0.2
            expect (synthHas (engine, dm7[0]));
            chords.setLiveKey (7);   // G: up 5
            run (engine, 50);   // a beat later: still waiting for the bar
            expect (chords.isLiveKeyWaiting());
            expect (synthHas (engine, dm7[0]) && ! synthHas (engine, dm7[0] + 5));
            run (engine, 150);   // past beat 4
            expect (! chords.isLiveKeyWaiting());
            expectEquals (chords.getPlayingKey(), 7);
            for (int n : dm7)
                expect (synthHas (engine, n + 5), "the held chord moved to Gm7 on the bar");
            expect (! synthHas (engine, dm7[0]), "and let go of the old one");

            // Next beat.
            chords.liveChange = (int) RigChords::LiveChange::beat;
            chords.setLiveKey (2);
            run (engine, 10);
            expect (chords.isLiveKeyWaiting() || chords.getPlayingShift() == 0);
            run (engine, 55);
            expectEquals (chords.getPlayingShift(), 0, "home on the next beat");
            chords.press (0);   // Hold: the same again lets go
            run (engine);
            expectEquals (synthCount (engine), 0);
            engine.getTransport().stop();
            run (engine);

            // The key map moves too: with C major mapped, D (up 2) makes D3 play Em, not Dm.
            chords.liveChange = (int) RigChords::LiveChange::now;
            chords.hold = false;
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            chords.trigger = (int) RigChords::Trigger::keyMap;
            chords.setKey (0, 0, 0);
            chords.setLiveKey (2);
            run (engine);
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 50, 0.9f));
            run (engine);
            expect (synthHas (engine, 52) && synthHas (engine, 55) && synthHas (engine, 59), "D3 plays Em from E3");
            expectEquals (chords.liveMapChordName (50), juce::String ("Em"));
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 50));
            run (engine);
            expectEquals (synthCount (engine), 0);

            // A new home key starts the live key from it.
            chords.setKey (5, 0, 0);
            expect (chords.isLiveKeyHome());

            // Learned keyboard buttons.
            auto& routing = engine.getRouting();
            routing.setAssignment (RigRouting::liveKeyC + 9, 1, 40);
            routing.setAssignment (RigRouting::liveOctaveUp, 1, 41);
            routing.setAssignment (RigRouting::liveKeyHome, 1, 42);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 40, 127));
            run (engine);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 40, 0));
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 41, 127));
            run (engine);
            expectEquals (chords.getLiveKey(), 9, "a learned button picks A");
            expectEquals (chords.getLiveOctave(), 1, "and one goes up an octave");
            expectEquals (RigRouting::controlName (RigRouting::liveKeyC + 1), juce::String ("Live key C#"));

            // Saved with the show (and its buttons); a set keeps the key you're in.
            const auto show = engine.saveShow();
            RigEngine other;
            other.loadShow (show);
            expectEquals (other.getChords().getLiveKey(), 9);
            expectEquals (other.getChords().getLiveOctave(), 1);
            expectEquals (other.getRouting().getAssignment (RigRouting::liveKeyC + 9).second, 40);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 41, 0));
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 42, 127));
            run (engine);
            expect (chords.isLiveKeyHome(), "a learned Back");
        }

        beginTest ("Chords on / off: plain single notes, no stuck notes, a learned button, saved");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            chords.fillFromKey (0, 0, 0);   // C major
            chords.trigger = (int) RigChords::Trigger::keyMap;
            chords.mapLayout = (int) RigChords::MapLayout::scaleNotes;
            chords.rebuildMap();
            expect (chords.areChordsOn(), "on to start");

            // On: D plays Dm.
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 62, 0.9f));
            run (engine);
            expect (synthCount (engine) >= 3, "D plays a chord");
            // Off while D is down: it plays on until let go, then nothing sticks.
            chords.toggleChords();
            run (engine);
            expect (synthCount (engine) >= 3, "the held chord plays on");
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 62));
            run (engine);
            expectEquals (synthCount (engine), 0, "let go: nothing stuck");

            // Off: every key plays just itself, on the chords' instrument.
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 64, 0.9f));
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 61, 0.9f));
            run (engine);
            expectEquals (synthCount (engine), 2, "two keys, two notes");
            expect (synthHas (engine, 64) && synthHas (engine, 61));
            // A live key change doesn't move a plain note (it's the key you pressed).
            chords.setLiveKey (2);
            run (engine);
            expect (synthHas (engine, 64) && synthHas (engine, 61), "plain notes stay put");
            chords.liveKeyHome();
            // Back on while they're down: they let go as plain notes, nothing stuck.
            chords.toggleChords();
            run (engine);
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 64));
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 61));
            run (engine);
            expectEquals (synthCount (engine), 0, "switched back: nothing stuck");
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 67, 0.9f));
            run (engine);
            expect (synthCount (engine) >= 3, "on again: G plays G major");
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 67));
            run (engine);

            // Low keys and every key play themselves too.
            chords.setChordsOn (false);
            chords.trigger = (int) RigChords::Trigger::everyKey;
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 53, 0.9f));
            run (engine);
            expectEquals (synthCount (engine), 1);
            expect (synthHas (engine, 53));
            engine.addMidiNow (juce::MidiMessage::noteOff (1, 53));
            run (engine);
            expectEquals (synthCount (engine), 0);

            // A pad plays its chord's lowest note; Hold's latched chord stops when it flips.
            chords.trigger = (int) RigChords::Trigger::screen;
            chords.press (5);   // Am: A C E
            run (engine);
            expectEquals (synthCount (engine), 1, "a pad: one note");
            expect (synthHas (engine, chords.getPad (5).notes[0]), "its lowest");
            chords.release (5);
            run (engine);
            expectEquals (synthCount (engine), 0);
            chords.audition (5, 5000);
            run (engine);
            expect (synthCount (engine) >= 3, "hearing a pad you build: the whole chord");
            chords.release (5);
            run (engine);
            chords.setChordsOn (true);
            chords.hold = true;
            chords.press (5);
            run (engine);
            expect (synthCount (engine) >= 3, "latched");
            chords.toggleChords();
            run (engine);
            expectEquals (synthCount (engine), 0, "flipping it lets a latched chord go");
            chords.hold = false;

            // The progression keeps its chords with chords off.
            chords.steps[0] = 0;
            chords.progressionOn = true;
            engine.getTransport().play();
            run (engine, 2);
            expect (synthCount (engine) >= 3, "progression: chords");
            engine.getTransport().stop();
            chords.progressionOn = false;
            run (engine, 2);
            expectEquals (synthCount (engine), 0);

            // A learned keyboard button flips it (a press and its release are one flip).
            auto& routing = engine.getRouting();
            routing.setAssignment (RigRouting::chordsOnOff, 1, 44);
            expectEquals (RigRouting::controlName (RigRouting::chordsOnOff), juce::String ("Chords on/off"));
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 44, 127));
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 44, 0));
            run (engine);
            expect (chords.areChordsOn(), "a learned button turns them back on");

            // Saved with the show, with its button; a set keeps what you're playing.
            chords.setChordsOn (false);
            const auto show = engine.saveShow();
            RigEngine other;
            other.loadShow (show);
            expect (! other.getChords().areChordsOn(), "saved off");
            expectEquals (other.getRouting().getAssignment (RigRouting::chordsOnOff).second, 44);
            const auto setFile = juce::File::createTempFile (".chords");
            chords.setChordsOn (true);
            expect (chords.saveSet (setFile));
            chords.setChordsOn (false);
            expect (chords.loadSet (setFile));
            expect (! chords.areChordsOn(), "a set leaves it as it is");
            setFile.deleteFile();
            chords.reset();
            expect (chords.areChordsOn(), "a new song: on");
        }

        beginTest ("Live key on the Chords drawer: the keys, octaves, Back and Learn");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F7Key)));
            auto& cv = view.getChordsView();
            auto& chords = engine.getChords();
            cv.setKey (2, 1, 1);
            cv.pressFill();
            cv.pressLiveKey (4);
            run (engine);
            expectEquals (chords.getPlayingKey(), 4);
            expectEquals (chords.livePadName (0), juce::String ("Em7"));
            cv.pressLiveOctave (-1);
            expectEquals (chords.getLiveShift(), 2 - 12);
            cv.pressLiveHome();
            expect (chords.isLiveKeyHome());
            cv.setLiveChange ((int) RigChords::LiveChange::beat);
            expectEquals (chords.liveChange.load(), (int) RigChords::LiveChange::beat);
            // Every Live key button is on screen, inside the drawer.
            int found = 0;
            for (auto* c : cv.getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*> (c))
                    for (auto* name : { "Oct -", "Oct +", "Learn" })
                        if (b->getButtonText() == name && cv.getLocalBounds().contains (b->getBounds()) && ! b->getBounds().isEmpty())
                            ++found;
            expect (found >= 5, "Oct - / Oct + twice (pad and live) and Learn: " + juce::String (found));

            if (const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_LIVEKEY_SNAPSHOT", {}); path.isNotEmpty())
            {
                cv.setLiveChange ((int) RigChords::LiveChange::bar);
                cv.pressLiveKey (4);
                run (engine);
                cv.selectPad (2);
                cv.refresh();
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                juce::FileOutputStream out { juce::File (path) };
                out.setPosition (0);
                out.truncate();
                juce::PNGImageFormat().writeImageToStream (image, out);
            }
        }

        beginTest ("MIDI files for Logic: the progression, and a take");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& chords = engine.getChords();
            chords.target = (int) RigRouting::Dest::synth;
            chords.loadStarter (0);   // C G Am F, a bar each
            juce::TemporaryFile prog (".mid");
            expect (chords.writeProgression (prog.getFile(), 120.0, 4));
            auto read = [] (const juce::File& f, int& noteOns, double& lastBeat)
            {
                juce::FileInputStream in (f);
                juce::MidiFile mf;
                noteOns = 0;
                lastBeat = 0;
                if (! mf.readFrom (in))
                    return false;
                for (int t = 0; t < mf.getNumTracks(); ++t)
                    for (auto* e : *mf.getTrack (t))
                        if (e->message.isNoteOn())
                        {
                            ++noteOns;
                            lastBeat = juce::jmax (lastBeat, e->message.getTimeStamp() / mf.getTimeFormat());
                        }
                return true;
            };
            int ons = 0;
            double last = 0;
            expect (read (prog.getFile(), ons, last));
            expectEquals (ons, chords.getPad (0).notes.size() + chords.getPad (4).notes.size() + chords.getPad (5).notes.size()
                                   + chords.getPad (3).notes.size());
            expectWithinAbsoluteError (last, 12.0, 0.001, "the fourth chord starts on bar 4");

            engine.getTransport().setBpm (120.0);
            chords.startTake();
            run (engine, 10);
            chords.press (0);
            run (engine, 20);
            chords.release (0);
            run (engine, 5);
            chords.press (5);
            run (engine, 5);
            chords.release (5);
            run (engine);
            chords.stopTake();
            expect (chords.hasTake());
            juce::TemporaryFile take (".mid");
            expect (chords.writeTake (take.getFile(), 120.0));
            expect (read (take.getFile(), ons, last));
            expectEquals (ons, chords.getPad (0).notes.size() + chords.getPad (5).notes.size());
            expectWithinAbsoluteError (last, 25 * 480 / 24000.0, 0.01, "timed from the first chord");
        }

        beginTest ("the Chords drawer: F7 over any view, Build your own on the vocoder, keys build a chord");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Song");
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F7Key)));
            expect (view.isChordsOpen());
            expectEquals (view.getPage(), juce::String ("Song"), "the view stays underneath");
            expect (view.getChordsView().getHeight() >= 300 && view.getChordsView().getWidth() > 900);
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F5Key)));
            expect (view.isSoundsOpen() && ! view.isChordsOpen(), "one drawer at a time");
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F7Key)));
            expect (! view.isSoundsOpen() && view.isChordsOpen());
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (! view.isChordsOpen());

            // The vocoder's Chords pedal opens it.
            view.pickTrack (GrooveView::vocoder);
            view.showView ("Edit");
            auto* vocoder = dynamic_cast<VocoderScreen*> (view.getCurrentContent());
            expect (vocoder != nullptr);
            if (vocoder != nullptr)
            {
                vocoder->selectPedal (VocoderScreen::chords);
                expect (vocoder->onOpenChords != nullptr);
                vocoder->onOpenChords();
                expect (view.isChordsOpen());
                vocoder->pressChord (2);   // its own seven chord buttons are still there
                expectEquals (vocoder->getActiveChord(), 2);
                vocoder->pressChord (2);
                expectEquals (vocoder->getActiveChord(), -1);
            }

            auto& cv = view.getChordsView();
            auto& chords = engine.getChords();
            cv.selectPad (10);
            expect (chords.isEmpty (10));
            for (int n : { 45, 48, 52, 55 })
                cv.clickKey (n);
            expectEquals (chords.padName (10), juce::String ("Am7"));
            cv.clickKey (55);
            expectEquals (chords.padName (10), juce::String ("Am"));
            expectEquals (chords.shapePad.load(), 10, "every key plays the pad you're on");
            // Chords sent to the vocoder bring its robot in.
            cv.setTarget ((int) RigRouting::Dest::vocoder);
            if (auto* p = engine.getVocoder().params.getParameter (ParamIDs::robotOn))
                p->setValueNotifyingHost (0.0f);
            cv.pressPad (10);
            expect (engine.getVocoder().params.getRawParameterValue (ParamIDs::robotOn)->load() > 0.5f, "the robot comes on to sing it");
            cv.releasePad (10);
            cv.pickStep (0, 10);
            expectEquals (chords.steps[0].load(), 10);
            cv.pressHold();
            expect (chords.hold.load());
            cv.pressHold();
            cv.setTarget ((int) RigRouting::Dest::synth);

            // To loop: the synth onto the next free layer, for the progression's length.
            chords.loadStarter (0);
            cv.pressPlayProgression();
            expect (chords.progressionOn.load());
            const auto said = cv.pressToLoop();
            expect (said.contains ("layer 1"), said);
            expectEquals (engine.getLooper().getLayer (0).source.load(), (int) RigLooper::synth);
            expectEquals (engine.getLooper().getLayer (0).bars.load(), 4);
            run (engine);

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_CHORDS_SNAPSHOT", {}); path.isNotEmpty())
            {
                chords.loadStarter (1);
                chords.setPad (8, notes ({ 41, 52, 57, 60, 64 }), "Big F");
                chords.steps[4] = 8;
                cv.selectPad (2);
                chords.target = RigChords::kFollowKeys;
                chords.trigger = (int) RigChords::Trigger::pads;
                chords.hold = true;
                chords.press (2);
                run (engine);
                for (auto [track, name] : { std::pair<int, const char*> { GrooveView::synth, "-synth" }, { GrooveView::vocoder, "-vocoder" } })
                {
                    view.pickTrack (track);
                    view.showView ("Edit");
                    view.showChords (true);
                    cv.refresh();
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File file (path + name + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
                // The vocoder's Chords pedal, with Build your own beside its seven chords.
                view.showChords (false);
                if (auto* v = dynamic_cast<VocoderScreen*> (view.getCurrentContent()))
                {
                    v->selectPedal (VocoderScreen::chords);
                    const auto pedalImage = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File pedalFile (path + "-vocoder-pedal.png");
                    pedalFile.deleteFile();
                    juce::FileOutputStream pedalStream (pedalFile);
                    juce::PNGImageFormat().writeImageToStream (pedalImage, pedalStream);
                }
                view.showChords (true);
                view.showView ("Play");
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                juce::File file (path + "-play.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
                chords.stopAll();
                run (engine);

                // The key map: every key plays a chord of A minor (sevenths), then the white keys as I to vii.
                chords.hold = false;
                chords.progressionOn = false;
                if (engine.getLooper().getState (0) != RigLooper::State::empty)
                    engine.getLooper().clear (0);
                chords.stopAll();
                run (engine, 4);
                view.pickTrack (GrooveView::synth);
                view.showView ("Edit");
                view.showChords (true);
                for (auto [layout, key, scale, type, name] : { std::tuple<int, int, int, int, const char*> { 0, 9, 1, 1, "-keymap" },
                                                               { 1, 2, 2, 0, "-keymap-white" },
                                                               { 3, 0, 0, 0, "-keymap-full" } })
                {
                    chords.mapLayout = layout;
                    cv.setKey (key, scale, type);
                    cv.setTrigger ((int) RigChords::Trigger::keyMap);
                    engine.addMidiNow (juce::MidiMessage::noteOn (1, layout == 0 ? 60 : 57, 0.9f));
                    run (engine);
                    cv.refresh();
                    const auto mapImage = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File mapFile (path + name + ".png");
                    mapFile.deleteFile();
                    juce::FileOutputStream mapStream (mapFile);
                    juce::PNGImageFormat().writeImageToStream (mapImage, mapStream);
                    engine.addMidiNow (juce::MidiMessage::noteOff (1, layout == 0 ? 60 : 57));
                    run (engine);
                }
            }
        }
    }
};

static RigChordsTests rigChordsTests;
