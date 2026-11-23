#include "TestHelpers.h"

#include "core/AudioFiles.h"
#include "core/SoundBank.h"
#include "SoundsView.h"

namespace
{
SoundBank::Sound named (const juce::String& path, float seconds = 0.5f)
{
    SoundBank::Sound s;
    s.file = juce::File ("/x/" + path);
    s.seconds = seconds;
    SoundBank::describe (s, path);
    return s;
}
}

struct SoundBankTests : juce::UnitTest
{
    SoundBankTests() : juce::UnitTest ("Sound bank", "Core") {}

    void runTest() override
    {
        beginTest ("sorts sounds by their names and folders");
        {
            expectEquals (named ("Splice/packs/Trap Drums/KSHMR_Kick_Hard_01.wav").kind, (int) SoundBank::kick);
            expectEquals (named ("pack/Drums/Snares/tight_snr_03.wav").kind, (int) SoundBank::snare);
            expectEquals (named ("pack/one_shots/HiHat_Closed_2.wav").kind, (int) SoundBank::hat);
            expectEquals (named ("pack/808s/Big 808 C.wav").kind, (int) SoundBank::bass);
            expectEquals (named ("pack/Bass Drums/thump.wav").kind, (int) SoundBank::kick, "a bass drum is a kick");
            expectEquals (named ("pack/Vocals/ooh_adlib.wav").kind, (int) SoundBank::vocal);
            expectEquals (named ("pack/Crash_Cymbal_Long.wav").kind, (int) SoundBank::cymbal);
            expectEquals (named ("pack/misc/thing.wav").kind, (int) SoundBank::other);
            expectEquals (named ("pack/Drums/Percussion/conga hi.wav").kind, (int) SoundBank::perc);
        }

        beginTest ("finds loops, tempo and key in names");
        {
            const auto a = named ("Splice/sounds/packs/Neo Soul/loops/NS_Keys_Loop_92_Fmin.wav", 10.4f);
            expect (a.loop);
            expectEquals (a.bpm, 92.0);
            expectEquals (a.key, juce::String ("F min"));
            expectEquals (a.kind, (int) SoundBank::keys);

            const auto b = named ("pack/GuitarChords 120bpm C#maj.wav", 8.0f);
            expect (b.loop, "a tempo and a few seconds: a loop");
            expectEquals (b.bpm, 120.0);
            expectEquals (b.key, juce::String ("C# maj"));

            const auto c = named ("pack/one_shots/Kick 01.wav", 0.4f);
            expect (! c.loop);
            expectEquals (c.bpm, 0.0);

            expectEquals (named ("pack/Bass_Loop_140_Eb.wav", 6.0f).key, juce::String ("Eb"));
            expectEquals (named ("pack/Pad A minor.wav", 6.0f).key, juce::String ("A min"));
        }

        beginTest ("reads a tempo typed in a BPM box or a search");
        {
            using namespace crate::Tempo;
            auto f = parse ("92");
            expect (f.isActive() && f.matches (92.0) && f.matches (92.4) && ! f.matches (93.0) && ! f.matches (46.0));
            expect (parse ("92 BPM").matches (92.0));
            auto r = parse ("90-100");
            expect (r.matches (90.0) && r.matches (100.0) && r.matches (95.5) && ! r.matches (101.0));
            expect (parse ("100 to 90").matches (95.0), "a range either way round");
            expect (parse (juce::String (juce::CharPointer_UTF8 ("90\xe2\x80\x93" "100"))).matches (95.0), "an en dash");
            auto n = parse ("~120");
            expect (n.matches (115.0) && n.matches (126.0) && ! n.matches (110.0));
            expect (! parse ("kick").isActive() && ! parse ("").isActive() && ! parse ("5000").isActive());

            auto hd = parse ("92", true);
            expect (hd.matches (46.0) && hd.matches (184.0) && hd.matches (92.0) && ! hd.matches (138.0));
            expectEquals (hd.distance (184.0), 0.0);
            expect (parse ("92").distance (95.0) > parse ("92").distance (93.0));
            expectEquals (parse ("92").describe(), juce::String ("92 BPM"));
            expectEquals (parse ("92").shortText(), juce::String ("92"));
            expectEquals (parse ("90-100").shortText(), juce::String ("90-100"));
            expectEquals (n.shortText(), juce::String ("~120"));
            expectEquals (parse ("90-100", true).describe(), juce::String ("90-100 BPM (or half/double)"));
            expectEquals (Filter::near (120.0, false).describe(), juce::String ("near 120 BPM"));

            juce::String text = "dusty drums 92bpm";
            expect (takeFromSearch (text).matches (92.0));
            expectEquals (text, juce::String ("dusty drums"));
            text = "keys 90-100 bpm Fmin";
            expect (takeFromSearch (text).matches (95.0));
            expectEquals (text, juce::String ("keys Fmin"));
            text = "keys 90 to 100 bpm";
            expect (takeFromSearch (text).matches (95.0));
            expectEquals (text, juce::String ("keys"));
            text = "808 kick 02";
            expect (! takeFromSearch (text).isActive(), "a bare number is a word, not a tempo");
            expectEquals (text, juce::String ("808 kick 02"));
        }

        beginTest ("guesses a loop's tempo from its length");
        {
            using crate::Tempo::guessFromLength;
            expectEquals (guessFromLength (2.0), 120.0);              // a bar at 120
            expectEquals (guessFromLength (480.0 / 92.0), 92.0);      // two bars at 92
            expectEquals (guessFromLength (16.0 * 60.0 / 140.0), 140.0);
            expectEquals (guessFromLength (3.7), 0.0, "a length that isn't whole bars at a whole tempo");
            expectEquals (guessFromLength (0.3), 0.0);

            const auto a = named ("pack/loops/Dark_Groove.wav", 4.0f);   // a loop, no tempo in the name
            expect (a.loop && a.bpmGuessed);
            expectEquals (a.bpm, 120.0);
            const auto b = named ("pack/loops/Dark_Groove_100.wav", 4.0f);
            expect (! b.bpmGuessed, "a tempo in the name wins");
            expectEquals (b.bpm, 100.0);
            expect (! named ("pack/one_shots/Kick 01.wav", 2.0f).bpmGuessed, "one-shots aren't guessed");
        }

        beginTest ("scans folders, searches, and remembers what it found");
        {
            const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-bank-" + juce::String (juce::Random::getSystemRandom().nextInt()));
            const auto splice = home.getChildFile ("Splice");
            juce::AudioBuffer<float> tone (1, 4410);
            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample (0, i, 0.3f * std::sin (i * 0.05f));
            juce::AudioBuffer<float> loop (1, 44100 * 4);
            loop.clear();
            for (auto path : { "packs/Trap/Kick_Hard.wav", "packs/Trap/Snare_Crack.wav", "packs/Trap/hats/HH_closed.wav" })
            {
                splice.getChildFile (path).getParentDirectory().createDirectory();
                expect (crate::AudioFiles::write (splice.getChildFile (path), tone));
            }
            splice.getChildFile ("packs/Soul/loops").createDirectory();
            expect (crate::AudioFiles::write (splice.getChildFile ("packs/Soul/loops/Soul_Drums_Loop_90.wav"), loop));
            splice.getChildFile ("packs/Trap/readme.txt").replaceWithText ("not a sound");

            {
                SoundBank bank (home);
                for (auto& f : bank.getFolders())
                    bank.removeFolder (f);
                bank.addFolder (splice);
                for (int i = 0; i < 200 && (bank.isScanning() || bank.size() < 4); ++i)
                    juce::Thread::sleep (20);
                expectEquals (bank.size(), 4);

                SoundBank::Query q;
                q.kind = SoundBank::kick;
                auto kicks = bank.search (q);
                expect (kicks.size() == 1 && kicks[0].name == "Kick_Hard");
                expect (kicks.size() == 1 && kicks[0].where == "Splice/packs/Trap");

                SoundBank::Query text;
                text.text = "trap hh";
                expectEquals ((int) bank.search (text).size(), 1, "every word must match");

                SoundBank::Query loops;
                loops.length = SoundBank::Length::loops;
                loops.nearBpm = 180.0;   // double time counts
                const auto found = bank.search (loops);
                expect (found.size() == 1 && found[0].bpm == 90.0 && std::abs (found[0].seconds - 4.0f) < 0.01f);

                SoundBank::Query exact;
                exact.tempo = crate::Tempo::parse ("90");
                expectEquals ((int) bank.search (exact).size(), 1, "only the 90 BPM loop");
                exact.tempo = crate::Tempo::parse ("100-120");
                expectEquals ((int) bank.search (exact).size(), 0);
                SoundBank::Query typed;
                typed.text = "soul 90bpm";
                expectEquals ((int) bank.search (typed).size(), 1, "a tempo typed in the search");
                typed.text = "trap 90bpm";
                expectEquals ((int) bank.search (typed).size(), 0);
            }

            SoundBank again (home);
            expectEquals (again.size(), 4, "the index remembers");
            expectEquals (again.getFolders().size(), 1);
            home.deleteRecursively();
        }
    }
};

struct SoundsTabTests : juce::UnitTest
{
    SoundsTabTests() : juce::UnitTest ("Sounds tab", "Rig") {}

    void runTest() override
    {
        beginTest ("finds Splice sounds and sends them to the MPC, the Sampler and the looper");
        const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-sounds-" + juce::String (juce::Random::getSystemRandom().nextInt()));
        const auto splice = home.getChildFile ("Splice");
        juce::AudioBuffer<float> hit (1, 4410), loop (2, 44100 * 2);   // a 2 s loop: one bar at 120
        for (int i = 0; i < hit.getNumSamples(); ++i)
            hit.setSample (0, i, 0.5f * std::sin (i * 0.1f));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < loop.getNumSamples(); ++i)
                loop.setSample (ch, i, 0.2f * std::sin (i * 0.02f));
        splice.getChildFile ("packs/Boom/loops").createDirectory();
        expect (crate::AudioFiles::write (splice.getChildFile ("packs/Boom/Boom_Kick_01.wav"), hit));
        expect (crate::AudioFiles::write (splice.getChildFile ("packs/Boom/loops/Boom_Drum_Loop_120.wav"), loop));
        home.getChildFile ("Sound bank folders.txt").replaceWithText (splice.getFullPathName());

        RigEngine engine;
        engine.prepare (48000.0, 480);
        engine.getTransport().setBpm (100.0);
        SoundsView view (engine, home);
        view.setSize (1200, 600);
        for (int i = 0; i < 200 && (view.getBank().isScanning() || view.getBank().size() < 2); ++i)
            juce::Thread::sleep (20);
        view.filter();
        expectEquals (view.getNumShown(), 2);

        view.setBpmText ("120");
        expect (view.getNumShown() == 1 && view.shownAt (0)->name == "Boom_Drum_Loop_120", "the BPM box");
        view.setBpmText ("90-110");
        expectEquals (view.getNumShown(), 0);
        view.setBpmText ({});
        expectEquals (view.getNumShown(), 2);

        juce::String said;
        view.onSay = [&] (const juce::String& s) { said = s; };
        view.selectRow (1);   // sorted by name: the kick is second
        view.sendToMpc();
        expect (said.contains ("pad A1"), said);
        expect (! engine.getMpc().getProject().currentBank().pads[0].isEmpty());

        view.selectRow (0);
        view.sendToLoop();
        expect (said.contains ("loop 1"), said);
        expect (engine.getLooper().getState (0) == RigLooper::State::playing);
        expectWithinAbsoluteError (engine.getLooper().getLayer (0).lengthBeats.load(), 4.0, 0.001, "one bar, kept as one bar at the Rig's tempo");

        view.sendToSampler();
        expect (engine.getSampler().getDocument().getName().startsWith ("Boom_Drum_Loop"));
        home.deleteRecursively();
    }
};

static SoundBankTests soundBankTests;
static SoundsTabTests soundsTabTests;
