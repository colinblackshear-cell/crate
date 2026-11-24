#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigBounce.h"
#include "RigLiveSong.h"
#include "Mp3Writer.h"
#include "RigSong.h"
#include "RigTimeline.h"
#include "TimelineView.h"

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;
constexpr int kPerBeat = (int) (kRate / 2);   // at 120 BPM

juce::AudioBuffer<float> steady (int beats, float value)
{
    juce::AudioBuffer<float> a (2, beats * kPerBeat);
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (a.getWritePointer (ch), value, a.getNumSamples());
    return a;
}

// A loop whose level says which bar of it is playing (bar 1: 0.1, bar 2: 0.2...).
juce::AudioBuffer<float> countingBars (int bars)
{
    juce::AudioBuffer<float> a (2, bars * 4 * kPerBeat);
    for (int i = 0; i < a.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
            a.setSample (ch, i, (float) (i / (4 * kPerBeat) + 1) / 10.0f);
    return a;
}

void quietTools (RigEngine& engine)
{
    engine.vocoderOn = false;
    for (int st : { RigEngine::mpcStrip, RigEngine::samplerStrip, RigEngine::vocoderStrip, RigEngine::bassStrip,
                    RigEngine::synthStrip, RigEngine::inputStrip, RigEngine::drumsStrip, RigEngine::droneStrip })
        engine.getStrip (st).mute = true;
    engine.getTransport().metronome = false;
}

RigEngine& fresh (RigEngine& engine)
{
    engine.prepare (kRate, kBlock);
    engine.getTransport().setBpm (120.0);
    quietTools (engine);
    for (int l = 0; l < RigLooper::kLayers; ++l)
        engine.getLooper().getLayer (l).level = 1.0f;
    return engine;
}

struct Player
{
    RigEngine& engine;
    juce::AudioBuffer<float> out { 2, kBlock };
    int blocks = 0;

    // Plays (ticking the message thread every 3 blocks, ~30 Hz) until the clock reaches `beat`.
    // Returns what the last block sounded like (its left side's middle sample).
    float to (double beat, int maxBlocks = 20000)
    {
        for (int b = 0; b < maxBlocks; ++b)
        {
            if (engine.getTransport().isPlaying() && engine.getTransport().getPositionInBeats() >= beat)
                break;
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            if (++blocks % 3 == 0)
                engine.tickScenes();
        }
        return out.getSample (0, kBlock / 2);
    }
    // Plays `n` more blocks (ticking).
    void blocksMore (int n)
    {
        for (int b = 0; b < n; ++b)
        {
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
            if (++blocks % 3 == 0)
                engine.tickScenes();
        }
    }
};

const RigTimeline::Block* blockOn (const RigTimeline& tl, int lane, int nth = 0)
{
    for (const auto& b : tl.getBlocks())
        if (b.lane == lane && nth-- == 0)
            return &b;
    return nullptr;
}
}

struct RigLiveSongTests : juce::UnitTest
{
    RigLiveSongTests() : juce::UnitTest ("Rec live", "Rig") {}

    void runTest() override
    {
        beginTest ("count in, bring loops in and out on the bar, keep the take, play it back the same");
        {
            RigEngine engine;
            fresh (engine);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, steady (4, 0.1f), 4.0);
            looper.setLayerAudio (1, steady (4, 0.3f), 4.0);
            looper.getLayer (0).level = looper.getLayer (1).level = 1.0f;
            looper.getLayer (1).mute = true;   // out to start with
            auto& live = engine.getLiveSong();
            juce::String why;
            expect (live.start (&why), why);
            expect (live.getState() == RigLiveSong::State::waiting, "counting in");
            Player p { engine };

            expectLessThan (std::abs (p.to (2.0)), 1.0e-4f, "the count-in bar is quiet");
            expectWithinAbsoluteError (p.to (5.0), 0.1f, 0.01f, "bar 2: loop 1 was playing, so it's in");
            expect (live.getState() == RigLiveSong::State::recording);

            live.toggle (1);   // at beat 5: lands on bar 3 (beat 8)
            expectEquals (live.changeAt (1), 2.0, "the next bar (take bars count from the count-in)");
            expectWithinAbsoluteError (p.to (7.5), 0.1f, 0.01f, "not before its bar");
            expectWithinAbsoluteError (p.to (8.5), 0.4f, 0.01f, "both from bar 3");

            live.toggle (0);   // out at bar 4 (beat 12)
            expectWithinAbsoluteError (p.to (11.5), 0.4f, 0.01f, "plays on to the bar");
            expectWithinAbsoluteError (p.to (12.5), 0.3f, 0.01f, "out on the bar");

            // Muting a layer anywhere (its M) does the same.
            looper.getLayer (1).mute = true;   // beat ~12.5: out at bar 5 (beat 16)
            expectWithinAbsoluteError (p.to (15.5), 0.3f, 0.01f, "a mute waits for the bar too");
            expect (! looper.getLayer (1).mute.load(), "(it's unmuted again until then)");
            expectLessThan (std::abs (p.to (16.5)), 1.0e-4f, "quiet from bar 5");
            expect (looper.getLayer (1).mute.load(), "muted on the bar");

            looper.getLayer (0).mute = false;   // in again at bar 6 (beat 20)
            p.to (21.0);
            const int take = live.stopAndKeep();
            p.blocksMore (2);   // (the clock stops at its next block)
            expectEquals (take, 0, "the first take");
            expect (! live.isActive());
            expect (! engine.getTransport().isPlaying(), "Stop + Keep stops the clock");
            expectEquals ((int) live.getTakes().size(), 1);
            expectEquals (live.getTakes()[0].name, juce::String ("Take 1"));
            expect (live.getChanges() >= 4);

            const auto& tl = engine.getTimeline();
            const auto* a = blockOn (tl, 0), *a2 = blockOn (tl, 0, 1), *b = blockOn (tl, 1);
            expect (a != nullptr && a2 != nullptr && b != nullptr);
            expectEquals (a->start, 0.0, "the song starts on the take's first bar");
            expectEquals (a->end(), 2.0);
            expectEquals (b->start, 1.0);
            expectEquals (b->end(), 3.0);
            expectEquals (a2->start, 4.0);
            expectEquals (a2->end(), 5.0, "closed where it stopped (the end of that bar)");
            expectEquals ((int) tl.getMarkers().size(), 1, "one section (shorter than 8 bars)");
            expectEquals (rigsong::songBars (tl), 5.0);
            expectEquals ((int) tl.gated.load(), 0);

            // Played back: the same.
            engine.playTimeline (0);
            Player q { engine };
            expectWithinAbsoluteError (q.to (2.0), 0.1f, 0.01f, "bar 1");
            expectWithinAbsoluteError (q.to (6.0), 0.4f, 0.01f, "bar 2");
            expectWithinAbsoluteError (q.to (10.0), 0.3f, 0.01f, "bar 3");
            expectLessThan (std::abs (q.to (14.0)), 1.0e-4f, "bar 4");
            expectWithinAbsoluteError (q.to (18.0), 0.1f, 0.01f, "bar 5");
        }

        beginTest ("a loop brought in keeps its groove (slipped), not from its top");
        {
            RigEngine engine;
            fresh (engine);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, countingBars (4), 16.0);
            looper.getLayer (0).level = 1.0f;
            looper.getLayer (0).mute = true;
            auto& live = engine.getLiveSong();
            live.countInBars = 0;
            expect (live.start());
            Player p { engine };
            p.to (1.0);
            live.toggle (0);   // in at bar 2 (of the take): the loop's bar 2
            expectWithinAbsoluteError (p.to (5.0), 0.2f, 0.01f, "its second bar, in time with the take");
            expectWithinAbsoluteError (p.to (9.0), 0.3f, 0.01f);
            p.to (10.0);
            live.stopAndKeep();
            p.blocksMore (2);
            const auto* b = blockOn (engine.getTimeline(), 0);
            expect (b != nullptr);
            expectEquals (b->start, 1.0);
            expectEquals (b->offset, 1.0, "slipped one bar");
            engine.playTimeline (0);
            Player q { engine };
            expectWithinAbsoluteError (q.to (5.0), 0.2f, 0.01f, "played back slipped too");
        }

        beginTest ("Cancel puts the song back; a stopped count-in keeps nothing");
        {
            RigEngine engine;
            fresh (engine);
            engine.getLooper().setLayerAudio (0, steady (4, 0.1f), 4.0);
            auto& tl = engine.getTimeline();
            tl.getBlocks().push_back ({ 0, 0.0, 8.0 });
            tl.getMarkers().push_back ({ 0.0, "Intro", 8.0, -1 });
            tl.changed();
            auto& live = engine.getLiveSong();
            expect (live.start());
            Player p { engine };
            p.to (9.0);
            live.cancel();
            expectEquals ((int) tl.getBlocks().size(), 1);
            expectEquals (tl.getBlocks()[0].length, 8.0, "the song as it was");
            expect (live.getTakes().empty(), "no take");
            expectEquals ((int) tl.gated.load(), 0);
        }

        beginTest ("punch: replaces only bars 3-4, from a run-up, and the song plays on");
        {
            RigEngine engine;
            fresh (engine);
            auto& looper = engine.getLooper();
            looper.setLayerAudio (0, steady (4, 0.1f), 4.0);
            looper.setLayerAudio (1, steady (4, 0.3f), 4.0);
            auto& tl = engine.getTimeline();
            tl.getBlocks() = { { 0, 0.0, 8.0 }, { 1, 0.0, 2.0 } };
            tl.getMarkers().push_back ({ 0.0, "Intro", 8.0, -1 });
            tl.changed();
            auto& live = engine.getLiveSong();
            juce::String why;
            expect (live.startPunch (2.0, 4.0, &why), why);
            Player p { engine };
            expectWithinAbsoluteError (p.to (1.0), 0.4f, 0.01f, "the run-up plays the song");
            p.to (9.0);   // bar 3 (punch's first)
            live.toggle (0);   // out at bar 4 (beat 12)
            live.toggle (1);   // in at bar 4
            expectWithinAbsoluteError (p.to (11.5), 0.1f, 0.01f);
            expectWithinAbsoluteError (p.to (12.5), 0.3f, 0.01f, "swapped on the bar");
            p.to (16.5);
            expect (! live.isActive(), "ends itself at the punch's end");
            expect (engine.getTransport().isPlaying(), "and the song plays on");
            expectEquals ((int) live.getTakes().size(), 2, "the song before is kept as a take too");
            expectEquals (live.getCurrentTake(), 1);
            const auto* a = blockOn (tl, 0), *a2 = blockOn (tl, 0, 1);
            expect (a != nullptr && a2 != nullptr);
            expectEquals (a->start, 0.0, "the song's block carries on through the run-up");
            expectEquals (a->end(), 3.0);
            expectEquals (a2->start, 4.0, "after the punch: the song as it was");
            expectEquals (a2->end(), 8.0);
            const auto* b2 = blockOn (tl, 1, 1);
            expect (b2 != nullptr);
            expectEquals (b2->start, 3.0);
            expectEquals (b2->end(), 4.0, "only up to the punch's end");
            expectEquals (rigsong::songBars (tl), 8.0, "the song keeps its length");
        }

        beginTest ("takes: switch, edits kept, saved with the show");
        {
            RigEngine engine;
            fresh (engine);
            engine.getLooper().setLayerAudio (0, steady (4, 0.1f), 4.0);
            auto& live = engine.getLiveSong();
            live.countInBars = 0;
            for (int run = 0; run < 2; ++run)
            {
                expect (live.start());
                Player p { engine };
                p.to (run == 0 ? 9.0 : 17.0);
                live.stopAndKeep();
                p.blocksMore (2);
            }
            expectEquals ((int) live.getTakes().size(), 2);
            expectEquals (live.getCurrentTake(), 1);
            expectEquals (engine.getTimeline().getBlocks()[0].end(), 5.0);
            engine.getTimeline().getBlocks()[0].level = 0.5f;   // an edit to Take 2
            engine.getTimeline().changed();
            expect (live.useTake (0));
            expectEquals (engine.getTimeline().getBlocks()[0].end(), 3.0, "Take 1");
            expect (live.useTake (1));
            expectEquals (engine.getTimeline().getBlocks()[0].level, 0.5f, "Take 2 kept its edit");
            engine.getTimeline().getBlocks()[0].offset = 0.5;
            engine.getTimeline().changed();

            const auto show = engine.saveShow();
            RigEngine other;
            fresh (other);
            other.loadShow (show);
            expectEquals ((int) other.getLiveSong().getTakes().size(), 2);
            expectEquals (other.getLiveSong().getCurrentTake(), 1);
            expectEquals (other.getLiveSong().getTakes()[1].blocks[0].offset, 0.5, "the slip is saved");
            expect (other.getLiveSong().deleteTake (0));
            expectEquals (other.getLiveSong().getCurrentTake(), 0);
        }

        beginTest ("Bars view: split carries the loop on, slip and level, heard in playback");
        {
            RigEngine engine;
            fresh (engine);
            engine.getLooper().setLayerAudio (0, countingBars (4), 16.0);
            auto& tl = engine.getTimeline();
            TimelineView view (engine);
            view.setSize (1000, 700);
            expect (! view.splitSelected (3.0), "nothing picked yet");
            const int i = view.addBlock (0, 0.0);
            tl.getBlocks()[(size_t) i].length = 8.0;
            tl.changed();
            view.select (i);
            expect (view.splitSelected (3.0));
            expectEquals ((int) tl.getBlocks().size(), 2);
            const auto* second = blockOn (tl, 0, 1);
            expect (second != nullptr);
            expectEquals (second->start, 3.0);
            expectEquals (second->offset, 3.0, "the second half carries on in the loop (its 4th bar)");
            view.setLevel (0, 0.5f);
            expectEquals (tl.getBlocks()[0].level, 0.5f);
            view.slip (1, 5.0);
            expectEquals (tl.getBlocks()[1].offset, 1.0, "a slip wraps round the loop");
            view.slip (1, 3.0);

            engine.playTimeline (0);
            Player q { engine };
            expectWithinAbsoluteError (q.to (5.0), 0.1f, 0.01f, "bar 2: the loop's bar 2, at half level");
            expectWithinAbsoluteError (q.to (13.0), 0.4f, 0.01f, "bar 4: carries on to the loop's bar 4 across the split");
            expectWithinAbsoluteError (q.to (17.0), 0.1f, 0.01f, "bar 5: round again to its bar 1");
        }

        beginTest ("Bounce: loop stems over the song keep the slip; a section; the loops playing now");
        {
            RigEngine engine;
            fresh (engine);
            engine.getLooper().setLayerAudio (0, countingBars (4), 16.0);
            auto& tl = engine.getTimeline();
            RigTimeline::Block a;
            a.lane = 0;
            a.start = 0;
            a.length = 4;
            a.offset = 2;   // starts on the loop's bar 3
            RigTimeline::Block b = a;
            b.start = 4;
            b.length = 2;
            b.offset = 0;
            tl.getBlocks() = { a, b };
            tl.getMarkers() = { { 0.0, "A", 0.0, -1 }, { 4.0, "B", 0.0, -1 } };
            tl.changed();
            const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("bounce-test-" + juce::String (juce::Random().nextInt()));
            auto read = [] (const juce::File& f)
            {
                juce::WavAudioFormat wav;
                juce::AudioBuffer<float> out;
                if (std::unique_ptr<juce::AudioFormatReader> r { wav.createReaderFor (f.createInputStream().release(), true) })
                {
                    out.setSize (2, (int) r->lengthInSamples);
                    r->read (&out, 0, (int) r->lengthInSamples, 0, true, true);
                }
                return out;
            };
            auto at = [] (const juce::AudioBuffer<float>& audio, double beat)
            {
                const int i = (int) (beat * kPerBeat);
                return i < audio.getNumSamples() ? audio.getSample (0, i) : -1.0f;
            };

            RigBounce::Options o;
            o.wav = true;
            o.mp3 = false;
            o.dry = true;
            o.tailSeconds = 0;
            o.name = "Test";
            expect (RigBounce::loopsFor (engine, o).size() == 1);
            expectEquals (RigBounce::barsFor (engine, o).getEnd(), 6.0);
            auto r = RigBounce::bounceLoops (engine, o, temp.getChildFile ("song"));
            expect (r.error.isEmpty(), r.error);
            expect (r.files.contains ("Song - Loop 1 - 120 BPM.wav") && r.files.contains ("Song - Mix - 120 BPM.wav") && r.files.contains ("Song markers.mid"),
                    r.files.joinIntoString (", "));
            const auto stem = read (temp.getChildFile ("song").getChildFile ("Song - Loop 1 - 120 BPM.wav"));
            expectEquals (stem.getNumSamples(), 6 * 4 * kPerBeat, "6 bars long");
            expectWithinAbsoluteError (at (stem, 2.0), 0.3f, 0.002f, "bar 1: the loop's bar 3 (its slip)");
            expectWithinAbsoluteError (at (stem, 10.0), 0.1f, 0.002f, "bar 3: round to its bar 1");
            expectWithinAbsoluteError (at (stem, 18.0), 0.1f, 0.002f, "bar 5: the second block from its top");

            o.what = RigBounce::What::section;
            o.section = 1;
            o.split = RigBounce::Split::mix;
            r = RigBounce::bounceLoops (engine, o, temp.getChildFile ("section"));
            expect (r.files.contains ("B - Mix - 120 BPM.wav") && ! r.files.contains ("B - Loop 1 - 120 BPM.wav"), r.files.joinIntoString (", "));
            const auto part = read (temp.getChildFile ("section").getChildFile ("B - Mix - 120 BPM.wav"));
            expectEquals (part.getNumSamples(), 4 * 4 * kPerBeat, "the section as the Song screen has it (the last one: 4 bars)");
            expectWithinAbsoluteError (at (part, 6.0), 0.2f, 0.002f, "its bar 2: the loop's bar 2");

            o.what = RigBounce::What::loops;
            o.split = RigBounce::Split::eachLoop;
            o.tailSeconds = 2.0;
            r = RigBounce::bounceLoops (engine, o, temp.getChildFile ("loops"));
            const auto pass = read (temp.getChildFile ("loops").getChildFile ("Loops - Loop 1 - 120 BPM.wav"));
            expectEquals (pass.getNumSamples(), 4 * 4 * kPerBeat + 2 * (int) kRate, "one pass of the loop and its 2 s tail");
            expectWithinAbsoluteError (at (pass, 14.0), 0.4f, 0.002f, "from its top");
            expectWithinAbsoluteError (at (pass, 17.0), 0.0f, 0.0001f, "the tail is quiet (dry)");
            expect (! r.files.contains ("Loops markers.mid"), "no markers for the loops");
            engine.getLooper().getLayer (0).mute = true;
            expect (RigBounce::loopsFor (engine, o).isEmpty() && RigBounce::describe (engine, o).startsWith ("No loops"), "a muted loop isn't playing");
            temp.deleteRecursively();
        }

        beginTest ("MP3: writes a file that reads back at the right length and pitch");
        {
            if (! mp3::available())
            {
                logMessage ("MP3 encoder not built in; skipped");
            }
            else
            {
                auto file = juce::File::createTempFile (".mp3");
                const double rate = 48000.0;
                juce::AudioBuffer<float> tone (2, (int) rate * 2);
                for (int i = 0; i < tone.getNumSamples(); ++i)
                {
                    const float v = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate);
                    tone.setSample (0, i, v);
                    tone.setSample (1, i, v);
                }
                expect (mp3::write (file, tone, rate, 320));
                expect (file.getSize() > 60000 && file.getSize() < 100000, "about 80 KB for 2 s at 320");
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
                expect (reader != nullptr, "reads back as MP3");
                if (reader != nullptr)
                {
                    expectEquals ((int) reader->sampleRate, 48000);
                    const double secs = (double) reader->lengthInSamples / reader->sampleRate;
                    expect (secs > 1.95 && secs < 2.2, "about 2 s long, got " + juce::String (secs));
                    juce::AudioBuffer<float> back (2, (int) reader->lengthInSamples);
                    reader->read (&back, 0, back.getNumSamples(), 0, true, true);
                    const float peak = back.getMagnitude (0, 4800, 48000);
                    expect (peak > 0.4f && peak < 0.6f, "level kept, got " + juce::String (peak));
                    int crossings = 0;
                    for (int i = 4801; i < 52800; ++i)
                        crossings += (back.getSample (0, i - 1) < 0.0f) != (back.getSample (0, i) < 0.0f) ? 1 : 0;
                    expect (crossings > 860 && crossings < 900, "still 440 Hz, got " + juce::String (crossings));
                }
                reader.reset();
                file.deleteFile();
            }
        }

        beginTest ("nothing to play: says so");
        {
            RigEngine engine;
            fresh (engine);
            juce::String why;
            // (A blank show's tools aren't playing by themselves.)
            const bool started = engine.getLiveSong().start (&why);
            if (! started)
                expect (why.isNotEmpty());
            else
                engine.getLiveSong().cancel();
        }
    }
};

static RigLiveSongTests rigLiveSongTests;
