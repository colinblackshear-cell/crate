#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigChords.h"
#include "RigView.h"
#include "ChordsView.h"
#include "GrooveView.h"
#include "VocoderScreen.h"
#include "Parameters.h"   // the vocoder's

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
            }
        }
    }
};

static RigChordsTests rigChordsTests;
