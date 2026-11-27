#include "TestHelpers.h"

#include "BouncePanel.h"
#include "FriendTrackPanel.h"
#include "RigBounce.h"
#include "RigEngine.h"
#include "RigFriendTrack.h"
#include "core/TrackAnalysis.h"

#include <cmath>

// A friend's track: a whole song brought in with its tempo, played under everything locked to the
// bars, saved with the show, and left out of (or put in) the files you send back.
namespace
{
constexpr double kSongRate = RigFriendTrack::kRate;
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// A made-up song at `bpm`: a kick on every beat (louder and lower on bar lines), a hi-hat between,
// and an F minor chord held under it. `lead` seconds of silence before bar 1. The sections get
// louder: 8 quiet bars, 16 medium, 8 loud.
juce::AudioBuffer<float> song (double bpm, double lead, int bars = 32)
{
    const double beat = 60.0 / bpm;
    const int n = (int) ((lead + bars * 4 * beat + 1.0) * kSongRate);
    juce::AudioBuffer<float> a (2, n);
    a.clear();
    const double chord[] { 174.61, 207.65, 261.63, 349.23 };   // F3 Ab3 C4 F4
    for (int i = (int) (lead * kSongRate); i < n; ++i)
    {
        const double t = i / kSongRate - lead;
        const int bar = (int) (t / (4 * beat));
        const float loud = bar < 8 ? 0.25f : bar < 24 ? 0.55f : 1.0f;
        double v = 0;
        for (double hz : chord)
            v += 0.05 * std::sin (juce::MathConstants<double>::twoPi * hz * t);
        const double sinceBeat = std::fmod (t, beat);
        const int beatInBar = (int) std::fmod (t / beat, 4.0);
        const double kickHz = beatInBar == 0 ? 55.0 : 90.0;
        v += (beatInBar == 0 ? 0.9 : 0.5) * std::exp (-sinceBeat * 18.0) * std::sin (juce::MathConstants<double>::twoPi * kickHz * sinceBeat);
        const double sinceHalf = std::fmod (t + beat / 2, beat);
        v += 0.15 * std::exp (-sinceHalf * 60.0) * (juce::Random::getSystemRandom().nextFloat() * 2.0 - 1.0);
        const float s = (float) v * loud * 0.6f;
        a.setSample (0, i, s);
        a.setSample (1, i, s);
    }
    return a;
}

// A ramp, so a test can tell exactly which moment of the song is playing.
juce::AudioBuffer<float> rampSong (double seconds)
{
    juce::AudioBuffer<float> a (2, (int) (seconds * kSongRate));
    for (int i = 0; i < a.getNumSamples(); ++i)
    {
        a.setSample (0, i, (float) i / (float) a.getNumSamples());
        a.setSample (1, i, -(float) i / (float) a.getNumSamples());
    }
    return a;
}

void quiet (RigEngine& engine)
{
    engine.prepare (kRate, kBlock);
    engine.vocoderOn = false;
    for (int st : { RigEngine::mpcStrip, RigEngine::samplerStrip, RigEngine::vocoderStrip, RigEngine::bassStrip,
                    RigEngine::synthStrip, RigEngine::inputStrip, RigEngine::drumsStrip, RigEngine::droneStrip })
        engine.getStrip (st).mute = true;
    engine.getTransport().metronome = false;
}

void play (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        if (b % 3 == 0)
            engine.tickScenes();
    }
}

float loudness (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    float peak = 0;
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        peak = juce::jmax (peak, out.getMagnitude (0, 0, kBlock));
    }
    return peak;
}

juce::File tempFolder (const juce::String& name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name + "-" + juce::String (juce::Random().nextInt (1000000)));
    f.createDirectory();
    return f;
}

juce::AudioBuffer<float> readWav (const juce::File& f, double* rate = nullptr)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
    if (reader == nullptr)
        return {};
    juce::AudioBuffer<float> a ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&a, 0, a.getNumSamples(), 0, true, true);
    if (rate != nullptr)
        *rate = reader->sampleRate;
    return a;
}
}

struct RigFriendTrackTests : juce::UnitTest
{
    RigFriendTrackTests() : juce::UnitTest ("Rig friend's track", "Rig") {}

    void runTest() override
    {
        beginTest ("listening to a song: its tempo, bar 1, key and sections");
        {
            const auto a = song (94.0, 0.42);
            const auto r = crate::TrackAnalysis::analyse (a, kSongRate, 4);
            expectWithinAbsoluteError (r.bpm, 94.0, 0.2, "the tempo");
            expectWithinAbsoluteError (r.firstBeat, 0.42, 0.03, "bar 1 is on the first downbeat");
            expectEquals (r.key, 5, "F");
            expect (r.minor, "minor");
            const auto secs = crate::TrackAnalysis::guessSections (a, kSongRate, 94.0, 0.42, 4);
            expect (secs.size() >= 3, "sections");
            if (secs.size() >= 3)
            {
                expectEquals (secs.front().name, juce::String ("Intro"));
                expectEquals (secs.front().bars, 8.0);
                expectEquals (secs.back().name, juce::String ("Hook"));
                expectEquals (secs.back().bar, 24.0);
            }
            // A song in 3/4 isn't a 4/4 one (its tempo is the beat's).
            const auto fast = song (128.0, 0.0, 24);
            expectWithinAbsoluteError (crate::TrackAnalysis::guessBpm (fast, kSongRate), 128.0, 0.3, "128");
            expectWithinAbsoluteError (crate::TrackAnalysis::guessFirstBeat (fast, kSongRate, 128.0, 4), 0.0, 0.03, "bar 1 at the very start");
            const auto late = song (128.0, 1.7, 24);
            expectWithinAbsoluteError (crate::TrackAnalysis::guessFirstBeat (late, kSongRate, 128.0, 4), 1.7, 0.03, "bar 1 after a silence");
            const auto slow = song (76.0, 0.9, 20);
            const auto rs = crate::TrackAnalysis::analyse (slow, kSongRate, 4);
            expectWithinAbsoluteError (rs.bpm, 76.0, 0.3, "76");
            expectWithinAbsoluteError (rs.firstBeat, 0.9, 0.03, "bar 1 at 76");
        }

        beginTest ("a tempo written in the file's name, and tap tempo");
        {
            expectEquals (crate::TrackAnalysis::bpmFromName ("Jay idea 94bpm.wav"), 94.0);
            expectEquals (crate::TrackAnalysis::bpmFromName ("beat_128_BPM.mp3"), 128.0);
            expectEquals (crate::TrackAnalysis::bpmFromName ("BPM 87 - rough.wav"), 87.0);
            expectEquals (crate::TrackAnalysis::bpmFromName ("my 808 mix.wav"), 0.0, "a number alone isn't a tempo");
            expectEquals (crate::TrackAnalysis::tapTempo ({ 10.0, 10.5, 11.0, 11.5 }), 120.0);
            expectEquals (crate::TrackAnalysis::tapTempo ({ 10.0, 10.5 }), 0.0, "needs three taps");
            expectEquals (crate::TrackAnalysis::tapTempo ({ 1.0, 5.0, 5.6, 6.2, 6.8 }), 100.0, "a long gap starts again");
            expectEquals (crate::TrackAnalysis::keyName (5, true), juce::String ("F minor"));
        }

        beginTest ("it plays the moment of the song that's under the clock's beat, never stretched");
        {
            RigFriendTrack ft;
            ft.prepare (kRate);
            const auto a = std::make_shared<juce::AudioBuffer<float>> (rampSong (20.0));
            RigFriendTrack::Info info;
            info.bpm = 100.0;
            info.firstBeat = 0.5;
            ft.set (a, info);
            juce::AudioBuffer<float> out (2, kBlock);
            out.clear();
            ft.render (out, kBlock, 8.0, 100.0, true);   // (the first block picks it up and ramps the level in)
            out.clear();
            ft.render (out, kBlock, 8.0, 100.0, true);
            const double at = (0.5 + 8.0 * 0.6) * kSongRate;
            expectWithinAbsoluteError (out.getSample (0, 0), (float) (at / a->getNumSamples()), 1.0e-5f, "bar 3 is 4.8 s after bar 1");
            expectWithinAbsoluteError (out.getSample (0, 100), (float) ((at + 100 * kSongRate / kRate) / a->getNumSamples()), 1.0e-5f,
                                       "at its own speed");
            expectWithinAbsoluteError (out.getSample (1, 0), -(float) (at / a->getNumSamples()), 1.0e-5f, "stereo");
            out.clear();
            ft.render (out, kBlock, 8.0, 120.0, true);
            expectWithinAbsoluteError (out.getSample (0, 0), (float) (at / a->getNumSamples()), 1.0e-5f, "at another tempo, still on the beat");
            expectWithinAbsoluteError (out.getSample (0, 100), (float) ((at + 100 * 1.2 * kSongRate / kRate) / a->getNumSamples()), 1.0e-5f,
                                       "and faster, like a tape");
            out.clear();
            ft.render (out, kBlock, 8.0, 100.0, false);
            expectEquals (out.getMagnitude (0, kBlock), 0.0f, "quiet while stopped");
            out.clear();
            ft.mute = true;
            ft.render (out, kBlock, 8.0, 100.0, true);
            ft.render (out, kBlock, 8.0, 100.0, true);
            expect (out.getMagnitude (0, kBlock / 2, kBlock / 2) < 1.0e-4f, "muted");
            expectWithinAbsoluteError (ft.lengthBars(), 19.5 / 0.6 / 4.0, 1.0e-6, "its bars, from bar 1");
        }

        beginTest ("in the Rig: heard while the clock plays, and the song plays on to its end");
        {
            RigEngine engine;
            quiet (engine);
            auto a = std::make_shared<juce::AudioBuffer<float>> (song (120.0, 0.0, 8));
            RigFriendTrack::Info info;
            info.name = "Jay idea";
            info.bpm = 120.0;
            const auto said = engine.bringInFriendTrack (a, info, true, false);
            expect (said.contains ("Jay idea") && said.contains ("120 BPM"), said);
            expectEquals (engine.getTransport().getBpm(), 120.0, "the Rig's tempo is the song's");
            expectEquals (loudness (engine, 20), 0.0f, "nothing while stopped");
            engine.getTransport().play();
            expect (loudness (engine, 100) > 0.05f, "heard while playing");

            // The song is 2 bars of boxes, but his track is 8: Song mode plays to its end.
            engine.getTransport().stop();
            play (engine, 2);
            RigTimeline::Block b;
            b.lane = RigTimeline::laneForLoop (0);
            b.start = 0;
            b.length = 2;
            engine.getTimeline().getBlocks().push_back (b);
            engine.getTimeline().changed();
            engine.playTimeline (0);
            play (engine, (int) (4.0 * 2.0 * kRate / kBlock) + 50);   // past bar 4
            expect (engine.getTransport().isPlaying(), "still playing after the boxes end");
            expectWithinAbsoluteError (engine.songLengthBars(), 8.5, 0.01, "the song is as long as his track");

            // Practice loop: bars 2-3 over and over.
            engine.practiceLoop (1, 3);
            play (engine, (int) (4.0 * 2.0 * 4 * kRate / kBlock));
            const double bar = engine.getTransport().getPositionInBeats() / 4.0;
            expect (bar >= 1.0 && bar < 3.0, "inside the practice loop: bar " + juce::String (bar + 1));
            expect (engine.isPracticing());
            engine.practiceOff();
            expect (! engine.isPracticing());
        }

        beginTest ("saved with the show and back");
        {
            const auto folder = tempFolder ("friend-show");
            const auto show = folder.getChildFile ("Collab.crateshow");
            const auto a = std::make_shared<juce::AudioBuffer<float>> (rampSong (6.0));
            {
                RigEngine engine;
                quiet (engine);
                RigFriendTrack::Info info;
                info.name = "Jay idea";
                info.bpm = 94.0;
                info.firstBeat = 0.42;
                info.key = 5;
                info.minor = true;
                engine.bringInFriendTrack (a, info, true, false);
                engine.getFriendTrack().level = 0.6f;
                const auto r = engine.saveShowFile (show);
                expect (r.ok, r.error);
            }
            RigEngine back;
            quiet (back);
            auto xml = juce::parseXML (show);
            expect (xml != nullptr);
            back.loadShow (juce::ValueTree::fromXml (*xml));
            back.readLoopAudio (show);
            const auto& ft = back.getFriendTrack();
            expect (ft.hasAudio(), "its sound came back");
            if (ft.hasAudio())
            {
                expectEquals (ft.getAudio()->getNumSamples(), a->getNumSamples());
                expectWithinAbsoluteError (ft.getAudio()->getSample (0, 44100), a->getSample (0, 44100), 1.0e-5f);
            }
            expectEquals (ft.getInfo().name, juce::String ("Jay idea"));
            expectEquals (ft.getInfo().bpm, 94.0);
            expectEquals (ft.getInfo().firstBeat, 0.42);
            expectEquals (ft.getInfo().key, 5);
            expect (ft.getInfo().minor);
            expectWithinAbsoluteError (ft.level.load(), 0.6f, 1.0e-6f);
            // Saved again unchanged: its WAV isn't written again.
            const auto r = back.saveShowFile (show);
            expect (r.ok && r.written == 0, "unchanged: " + juce::String (r.written) + " written");
            // New song: gone.
            back.newShow();
            expect (! back.getFriendTrack().hasAudio(), "New song has none");
            folder.deleteRecursively();
        }

        beginTest ("Send it back: the song is his track's length; his track in the mix, or left out");
        {
            RigEngine engine;
            quiet (engine);
            auto a = std::make_shared<juce::AudioBuffer<float>> (song (120.0, 0.25, 8));
            RigFriendTrack::Info info;
            info.name = "Jay";
            info.bpm = 120.0;
            info.firstBeat = 0.25;
            engine.bringInFriendTrack (a, info, true, false);
            RigBounce::Options o;
            o.what = RigBounce::What::song;
            o.split = RigBounce::Split::mix;
            o.mp3 = false;
            o.tailSeconds = 0;
            expectEquals (RigBounce::barsFor (engine, o).getEnd(), 9.0, "to the end of his track (8 bars and a bit)");
            expect (RigBounce::describe (engine, o).contains ("with his track"), RigBounce::describe (engine, o));
            const auto folder = tempFolder ("friend-bounce");
            o.leadSeconds = 0.25;
            auto r = RigBounce::bounceLoops (engine, o, folder.getChildFile ("with"));
            expect (r.error.isEmpty(), r.error);
            juce::String mixName;
            for (const auto& f : r.files)
                if (f.endsWith (".wav"))
                    mixName = f;
            expect (mixName.contains ("Mix"), r.files.joinIntoString (", "));
            if (mixName.isNotEmpty())
            {
                double rate = 0;
                const auto mix = readWav (folder.getChildFile ("with").getChildFile (mixName), &rate);
                expectEquals (mix.getNumSamples(), (int) std::round (0.25 * rate) + (int) std::ceil (9 * 2.0 * rate),
                              "starts where his mixdown starts, to the end");
                expect (mix.getMagnitude (0, (int) (0.2 * rate)) < 1.0e-4f, "quiet before his bar 1");
                expect (mix.getMagnitude (0, (int) (0.25 * rate), (int) rate) > 0.05f, "his track in the mix");
            }
            const auto note = RigBounce::writeNoteForFriend (engine, o, folder.getChildFile ("with"));
            expect (note.isNotEmpty());
            expect (folder.getChildFile ("with").getChildFile (note).loadFileAsString().contains ("120 BPM"), "the note has the tempo");
            o.withTrack = false;
            expect (RigBounce::describe (engine, o).startsWith ("Nothing"), "no parts of yours yet, without his track");

            BouncePanel panel (engine, {});
            expect (panel.showsTrackRow(), "the Send back row is there with a track");
            panel.pickSendBack (BouncePanel::SendBack::myPartsEach);
            expect (panel.getOptions().split == RigBounce::Split::everything && ! panel.getOptions().withTrack && ! panel.getOptions().mixOnly);
            panel.pickSendBack (BouncePanel::SendBack::myPartsMixed);
            expect (! panel.getOptions().withTrack && panel.getOptions().mixOnly);
            panel.pickSendBack (BouncePanel::SendBack::everythingToListen);
            expect (panel.getOptions().withTrack && panel.getOptions().mixOnly);
            panel.pickSplit (RigBounce::Split::eachLoop);
            expect (panel.getOptions().withTrack && ! panel.getOptions().mixOnly, "another choice puts it back to normal");

            // Left out: heard while it plays, but not in what's recorded.
            engine.getFriendTrack().recorded = false;
            engine.getTransport().play();
            expect (loudness (engine, 100) > 0.02f, "still heard");
            engine.getFriendTrack().recorded = true;
            folder.deleteRecursively();
        }

        beginTest ("AIFF export: writeAudio with aiff writes a .aif that reads back (24-bit, same length); one format always stays on");
        {
            RigEngine engine;
            quiet (engine);
            RigBounce::Options o;
            o.wav = false;
            o.mp3 = false;
            o.aiff = true;
            const double rate = 48000.0;
            juce::AudioBuffer<float> audio (2, 12000);
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                audio.setSample (0, i, 0.5f * std::sin (0.05f * (float) i));
                audio.setSample (1, i, 0.25f * std::sin (0.03f * (float) i));
            }
            const auto folder = tempFolder ("aiff-export");
            juce::StringArray files;
            expect (RigBounce::writeAudio (folder.getChildFile ("Stem"), audio, rate, o, files), "written");
            expectEquals (files.joinIntoString (","), juce::String ("Stem.aif"), "only the AIFF");
            expect (! folder.getChildFile ("Stem.wav").existsAsFile(), "no WAV when it wasn't asked for");
            juce::AiffAudioFormat aiff;
            std::unique_ptr<juce::AudioFormatReader> reader (aiff.createReaderFor (folder.getChildFile ("Stem.aif").createInputStream().release(), true));
            expect (reader != nullptr, "reads back");
            if (reader != nullptr)
            {
                expectEquals ((int) reader->lengthInSamples, 12000, "same length");
                expectEquals ((int) reader->numChannels, 2, "stereo");
                expectEquals ((int) reader->bitsPerSample, 24, "24-bit");
                expectWithinAbsoluteError (reader->sampleRate, rate, 0.5);
            }
            o.wav = true;
            files.clear();
            RigBounce::writeAudio (folder.getChildFile ("Both"), audio, rate, o, files);
            expectEquals (files.joinIntoString (","), juce::String ("Both.wav,Both.aif"));
            BouncePanel panel (engine, o);
            panel.setFormats (false, true, false);
            expect (panel.getOptions().aiff && ! panel.getOptions().wav, "AIFF alone");
            panel.setFormats (false, false, false);
            expect (panel.getOptions().aiff, "at least one stays on");
            folder.deleteRecursively();
        }

        beginTest ("the sheet: reads the file, takes the tempo in its name, Cancel puts it all back");
        {
            const auto folder = tempFolder ("friend-sheet");
            const auto file = folder.getChildFile ("Jay idea 94bpm.wav");
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (kSongRate).withNumChannels (2).withBitsPerSample (24));
                const auto a = song (94.0, 0.42);
                expect (writer != nullptr && writer->writeFromAudioSampleBuffer (a, 0, a.getNumSamples()));
            }
            RigEngine engine;
            quiet (engine);
            engine.getTransport().setBpm (110.0);
            {
                FriendTrackSheet sheet (engine, file, false);
                expect (sheet.isReady(), sheet.getProblem());
                expectEquals (sheet.getInfo().bpm, 94.0, "the tempo in its name");
                expectWithinAbsoluteError (sheet.getInfo().firstBeat, 0.42, 0.03, "bar 1");
                expect (engine.getFriendTrack().hasAudio(), "tried in the Rig while it's open");
                sheet.doubleTempo();
                expectEquals (sheet.getInfo().bpm, 188.0);
                sheet.halfTempo();
                sheet.nudge (0.01);
                expectWithinAbsoluteError (sheet.getInfo().firstBeat, 0.43, 0.035);
                sheet.findBarOne();
                expectWithinAbsoluteError (sheet.getInfo().firstBeat, 0.42, 0.03, "found again");
                sheet.toggleListen();
                expect (engine.getTransport().metronome.load() && sheet.isListening());
                expectEquals (engine.getTransport().getBpm(), 94.0);
            }
            expect (! engine.getFriendTrack().hasAudio(), "Cancel: no track");
            expectEquals (engine.getTransport().getBpm(), 110.0, "Cancel: the tempo as it was");
            expect (! engine.getTransport().metronome.load(), "Cancel: the click as it was");
            {
                FriendTrackSheet sheet (engine, file, false);
                const auto said = sheet.bringIn();
                expect (said.contains ("94 BPM"), said);
                expect (said.contains ("F minor"), said);
                expect (said.contains ("sections"), said);
            }
            expect (engine.getFriendTrack().hasAudio(), "brought in");
            expectEquals (engine.getTransport().getBpm(), 94.0);
            expectEquals (engine.getChords().fillKey, 5, "the Chords in F");
            expect (engine.getTimeline().getMarkers().size() >= 3, "its sections marked");
            // The strip: take it out, put it back.
            FriendTrackStrip strip (engine);
            expect (strip.preferredHeight() > 50);
            strip.takeOut();
            expect (! engine.getFriendTrack().hasAudio());
            strip.putBack();
            expect (engine.getFriendTrack().hasAudio(), "put back");
            strip.toggleMute();
            expect (engine.getFriendTrack().mute.load());
            folder.deleteRecursively();
        }
    }
};

static RigFriendTrackTests rigFriendTrackTests;
