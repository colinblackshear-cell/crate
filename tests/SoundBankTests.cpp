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
