#include "TestHelpers.h"

#include "core/AudioFiles.h"
#include "core/Edits.h"
#include "core/SoundFx.h"
#include "core/SoundLibrary.h"

using namespace crate;

namespace
{
struct EditsTests : juce::UnitTest
{
    EditsTests() : juce::UnitTest ("Edits", "Core") {}

    void runTest() override
    {
        const auto src = ramp (1000);

        beginTest ("crop keeps only the selection");
        {
            auto r = Edits::crop (src, { 100, 300 });
            expectEquals (r.audio.getNumSamples(), 200);
            expectWithinAbsoluteError (r.audio.getSample (0, 0), src.getSample (0, 100), 1.0e-6f);
            expect (r.selection == juce::Range<int> (0, 200));
            expectEquals (Edits::crop (src, {}).audio.getNumSamples(), 1000, "empty selection leaves it alone");
        }

        beginTest ("remove deletes the selection and leaves a cursor");
        {
            auto r = Edits::remove (src, { 100, 300 });
            expectEquals (r.audio.getNumSamples(), 800);
            expect (r.selection == juce::Range<int> (100, 100));
            // well away from the join, the samples are the ones after the cut
            expectWithinAbsoluteError (r.audio.getSample (0, 500), src.getSample (0, 700), 1.0e-6f);
            expectEquals (Edits::remove (src, { 0, 1000 }).audio.getNumSamples(), 0);
        }

        beginTest ("silence keeps the length");
        {
            auto r = Edits::silence (src, { 10, 20 });
            expectEquals (r.audio.getNumSamples(), 1000);
            expectEquals (r.audio.getMagnitude (10, 10), 0.0f);
            expect (r.audio.getSample (0, 21) > 0.0f);
        }

        beginTest ("insert and paste");
        {
            auto clip = ramp (50);
            auto ins = Edits::insert (src, 500, clip);
            expectEquals (ins.audio.getNumSamples(), 1050);
            expect (ins.selection == juce::Range<int> (500, 550));
            expectWithinAbsoluteError (ins.audio.getSample (0, 549), clip.getSample (0, 49), 1.0e-6f);
            expectWithinAbsoluteError (ins.audio.getSample (0, 550), src.getSample (0, 500), 1.0e-6f);

            auto pasted = Edits::paste (src, { 100, 400 }, clip);
            expectEquals (pasted.audio.getNumSamples(), 750);

            // a stereo clip into a mono sound makes the sound stereo
            auto stereo = Edits::insert (src, 0, ramp (10, 2));
            expectEquals (stereo.audio.getNumChannels(), 2);

            auto intoEmpty = Edits::insert (juce::AudioBuffer<float> (1, 0), 0, clip);
            expectEquals (intoEmpty.audio.getNumSamples(), 50);
        }

        beginTest ("fade shapes start at 0 and end at 1");
        for (auto c : { Edits::FadeCurve::linear, Edits::FadeCurve::smooth, Edits::FadeCurve::fast, Edits::FadeCurve::slow })
        {
            expectWithinAbsoluteError (Edits::fadeShape (c, 0.0f), 0.0f, 1.0e-6f);
            expectWithinAbsoluteError (Edits::fadeShape (c, 1.0f), 1.0f, 1.0e-6f);
            expect (Edits::fadeShape (c, 0.25f) <= Edits::fadeShape (c, 0.75f));
        }
        expect (Edits::fadeShape (Edits::FadeCurve::fast, 0.5f) > 0.5f);
        expect (Edits::fadeShape (Edits::FadeCurve::slow, 0.5f) < 0.5f);

        beginTest ("fade in and out");
        {
            auto loud = sine (4410);
            auto in = Edits::fade (loud, { 0, 4410 }, true, Edits::FadeCurve::linear).audio;
            expect (in.getMagnitude (0, 100) < 0.02f);
            expect (in.getMagnitude (4300, 110) > 0.45f);
            auto out = Edits::fade (loud, { 2000, 4410 }, false, Edits::FadeCurve::smooth).audio;
            expect (out.getMagnitude (4350, 60) < 0.01f);
            expect (out.getMagnitude (0, 2000) > 0.49f, "before the fade is untouched");
        }

        beginTest ("reverse");
        {
            auto whole = Edits::reverse (src, {});
            expectWithinAbsoluteError (whole.audio.getSample (0, 0), src.getSample (0, 999), 1.0e-6f);
            expect (whole.selection.isEmpty());
            auto part = Edits::reverse (src, { 0, 10 });
            expectWithinAbsoluteError (part.audio.getSample (0, 0), src.getSample (0, 9), 1.0e-6f);
            expectWithinAbsoluteError (part.audio.getSample (0, 10), src.getSample (0, 10), 1.0e-6f);
        }

        beginTest ("gain and normalize");
        {
            auto quiet = sine (4410, 2, 0.1f);
            auto up = Edits::gain (quiet, {}, 6.0f).audio;
            expectWithinAbsoluteError (up.getMagnitude (0, up.getNumSamples()), 0.1f * juce::Decibels::decibelsToGain (6.0f), 0.002f);
            auto norm = Edits::normalize (quiet, {}).audio;
            expectWithinAbsoluteError (norm.getMagnitude (0, norm.getNumSamples()), juce::Decibels::decibelsToGain (-0.3f), 0.002f);
            auto silent = juce::AudioBuffer<float> (1, 100);
            silent.clear();
            expectEquals (Edits::normalize (silent, {}).audio.getMagnitude (0, 100), 0.0f);
        }

        beginTest ("trim quiet ends");
        {
            juce::AudioBuffer<float> b (1, 44100);
            b.clear();
            auto tone = sine (4410);
            b.copyFrom (0, 20000, tone, 0, 0, 4410);
            auto trimmed = Edits::trimSilence (b).audio;
            expect (trimmed.getNumSamples() > 4410 && trimmed.getNumSamples() < 4410 + samples (0.03));
            juce::AudioBuffer<float> quiet (1, 100);
            quiet.clear();
            expectEquals (Edits::trimSilence (quiet).audio.getNumSamples(), 100, "all quiet: left alone");
        }

        beginTest ("fake stereo becomes mono, real stereo stays");
        {
            auto same = sine (1000, 2);
            expectEquals (Edits::collapseFakeStereo (same).getNumChannels(), 1);
            auto leftOnly = sine (1000, 2);
            leftOnly.clear (1, 0, 1000);
            auto mono = Edits::collapseFakeStereo (leftOnly);
            expectEquals (mono.getNumChannels(), 1);
            expect (mono.getMagnitude (0, 0, 1000) > 0.4f);
            auto rightOnly = sine (1000, 2);
            rightOnly.clear (0, 0, 1000);
            expect (Edits::collapseFakeStereo (rightOnly).getMagnitude (0, 0, 1000) > 0.4f, "keeps the side with sound");
            auto real = ramp (1000, 2);
            expectEquals (Edits::collapseFakeStereo (real).getNumChannels(), 2);
        }

        beginTest ("snap to zero crossings");
        {
            auto s = sine (4410);   // crosses zero every 50 samples at 441 Hz
            auto snapped = Edits::snapToZeroCrossings (s, { 120, 330 });
            auto nearCrossing = [] (int pos) { const int m = pos % 50; return m <= 1 || m >= 49; };
            expect (nearCrossing (snapped.getStart()), juce::String (snapped.getStart()));
            expect (nearCrossing (snapped.getEnd()), juce::String (snapped.getEnd()));
        }
    }
};

struct FileTests : juce::UnitTest
{
    FileTests() : juce::UnitTest ("Files and library", "Core") {}

    void runTest() override
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("crate-tests", {});
        dir.createDirectory();

        beginTest ("load resamples to 44.1 kHz");
        {
            // write a 48 kHz file by hand
            auto file = dir.getChildFile ("48k.wav");
            juce::AudioBuffer<float> b (2, 48000);
            for (int i = 0; i < 48000; ++i)
                b.setSample (0, i, 0.5f), b.setSample (1, i, -0.5f);
            {
                std::unique_ptr<juce::OutputStream> stream (file.createOutputStream().release());
                juce::WavAudioFormat wav;
                auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
                expect (writer != nullptr && writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()));
            }
            juce::String error;
            auto loaded = AudioFiles::load (file, formats, error);
            expect (loaded != nullptr, error);
            if (loaded != nullptr)
            {
                expectEquals (loaded->getNumChannels(), 2);
                expect (std::abs (loaded->getNumSamples() - 44100) <= 2);
                expectWithinAbsoluteError (loaded->getSample (0, 20000), 0.5f, 0.01f);
            }

            auto bad = dir.getChildFile ("not-audio.wav");
            bad.replaceWithText ("hello");
            expect (AudioFiles::load (bad, formats, error) == nullptr);
            expect (error.isNotEmpty());
        }

        beginTest ("WAV and AIFF round trip, and plugin-state WAV data");
        {
            auto s = sine (4410, 2);
            for (auto name : { "a.wav", "a.aiff" })
            {
                auto file = dir.getChildFile (name);
                expect (AudioFiles::write (file, s), name);
                juce::String error;
                auto back = AudioFiles::load (file, formats, error);
                expect (back != nullptr && back->getNumSamples() == 4410, name);
                if (back != nullptr)
                    expectWithinAbsoluteError (back->getSample (1, 1000), s.getSample (1, 1000), 1.0e-4f);
            }
            auto data = AudioFiles::toWavData (s);
            auto back = AudioFiles::fromWavData (data.getData(), data.getSize());
            expect (back != nullptr && back->getNumSamples() == 4410 && back->getNumChannels() == 2);
        }

        beginTest ("library saves sounds with tempo, key, tags and tool inside the WAV");
        {
            SoundLibrary library (formats, dir.getChildFile ("Sounds"));
            expectEquals ((int) library.getEntries().size(), 0);

            SoundLibrary::Info info;
            info.name = "Kick / Snare?";   // characters that can't go in a file name
            info.bpm = 92.5;
            info.beats = 8;
            info.rootNote = 60;
            info.tags = { "drums", "dusty" };
            info.tool = "Sample Editor";
            auto file = library.save (sine (44100), info);
            expect (file.existsAsFile());

            auto second = library.save (sine (4410), info);
            expect (second != file, "never overwrites");
            expectEquals ((int) library.getEntries().size(), 2);

            auto entry = library.getEntries().back();
            for (const auto& e : library.getEntries())
                if (e.file == file)
                    entry = e;
            expectWithinAbsoluteError (entry.info.bpm, 92.5, 0.01);
            expectEquals (entry.info.beats, 8.0);
            expectEquals (entry.info.rootNote, 60);
            expect (entry.info.tags == juce::StringArray ({ "drums", "dusty" }));
            expectEquals (entry.info.tool, juce::String ("Sample Editor"));
            expectWithinAbsoluteError (entry.seconds, 1.0, 0.001);

            // a plain hit: no tempo, nothing odd written
            SoundLibrary::Info hit;
            hit.name = "hit";
            auto hitFile = library.save (sine (441), hit);
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (hitFile));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                auto read = SoundLibrary::fromMetadata (reader->metadataValues, "hit");
                expectEquals (read.bpm, 0.0);
                expectEquals (read.rootNote, -1);
            }

            expect (library.rename (entry, "Renamed"));
            bool found = false;
            for (const auto& e : library.getEntries())
                found = found || e.info.name == "Renamed";
            expect (found);
        }

        dir.deleteRecursively();
    }
};

struct FxTests : juce::UnitTest
{
    FxTests() : juce::UnitTest ("Effects", "Core") {}

    void runTest() override
    {
        beginTest ("stretch keeps length targets; effects don't clip");
        auto s = sine (44100, 2);
        expectEquals (SoundFx::stretch (s, 22050).getNumSamples(), 22050);
        SoundFx::Settings settings;
        settings.drive = 1.0f;
        settings.reverbMix = 0.5f;
        auto out = SoundFx::apply (s, settings);
        expect (out.getNumSamples() >= 44100);
        expect (out.getMagnitude (0, out.getNumSamples()) <= 1.0f);
    }
};

EditsTests editsTests;
FileTests fileTests;
FxTests fxTests;
}
