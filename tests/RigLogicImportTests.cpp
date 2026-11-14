#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigLogicImport.h"
#include "RigSong.h"
#include "RigView.h"

// A song finished in Logic, back in Kaleidobox to play live: its stems as loops, its markers as
// the Song screen's sections.
namespace
{
juce::MidiFile logicMidi (double bpm, int num, int den, std::vector<std::pair<double, juce::String>> markers, bool tempoChange = false)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence meta;
    auto tempo = juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / bpm));
    tempo.setTimeStamp (0);
    meta.addEvent (tempo);
    if (tempoChange)
    {
        auto later = juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / (bpm + 20)));
        later.setTimeStamp (960 * 16);
        meta.addEvent (later);
    }
    auto sig = juce::MidiMessage::timeSignatureMetaEvent (num, den);
    sig.setTimeStamp (0);
    meta.addEvent (sig);
    for (auto& [beat, name] : markers)
    {
        auto m = juce::MidiMessage::textMetaEvent (6, name);
        m.setTimeStamp (beat * 960);
        meta.addEvent (m);
    }
    meta.updateMatchedPairs();
    file.addTrack (meta);
    return file;
}

rigimport::Stem stem (const juce::String& name, double seconds, float value, double rate = 44100.0)
{
    rigimport::Stem s;
    s.name = name;
    s.sampleRate = rate;
    s.audio.setSize (2, (int) (seconds * rate));
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (s.audio.getWritePointer (ch), value, s.audio.getNumSamples());
    return s;
}

void writeWav (const juce::File& f, double seconds, float value)
{
    juce::WavAudioFormat wav;
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (out.get(), 44100.0, 2, 16, {}, 0));
    if (writer == nullptr)
        return;
    out.release();
    juce::AudioBuffer<float> b (2, (int) (seconds * 44100.0));
    for (int ch = 0; ch < 2; ++ch)
        juce::FloatVectorOperations::fill (b.getWritePointer (ch), value, b.getNumSamples());
    writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
}

struct RigLogicImportTests : juce::UnitTest
{
    RigLogicImportTests() : juce::UnitTest ("Bringing a Logic song back to play live", "Rig") {}

    void runTest() override
    {
        beginTest ("stem names lose the project's name and Logic's numbers");
        {
            juce::Array<juce::File> files { juce::File ("/x/My Song_Bass.wav"), juce::File ("/x/My Song_Lead Vox.wav"), juce::File ("/x/My Song_Drums_bip.wav") };
            const auto names = rigimport::stemNames (files);
            expectEquals (names[0], juce::String ("Bass"));
            expectEquals (names[1], juce::String ("Lead Vox"));
            expectEquals (names[2], juce::String ("Drums"));
            expectEquals (rigimport::stemNames ({ juce::File ("/x/01 Keys.aif") })[0], juce::String ("Keys"));
        }

        beginTest ("the MIDI file gives the tempo, time signature and markers");
        {
            const auto info = rigimport::readMidi (logicMidi (90.0, 3, 4, { { 0, "Intro" }, { 12, "Verse" }, { 36, "Chorus" } }, true));
            expect (info.found);
            expectWithinAbsoluteError (info.bpm, 90.0, 0.01);
            expectEquals (info.numerator, 3);
            expectEquals ((int) info.markers.size(), 3);
            expectWithinAbsoluteError (info.markers[1].first, 12.0, 1.0e-9);
            expectEquals (info.markers[2].second, juce::String ("Chorus"));
            expectEquals (info.tempoChanges, 1);
        }

        beginTest ("markers become sections end to end, from bar 1 to the end of the stems");
        {
            std::vector<rigimport::Stem> stems;
            stems.push_back (stem ("Drums", 60.0, 0.1f));
            stems.push_back (stem ("Bass", 60.0, 0.2f));
            // 60 s at 90 BPM is 90 beats: 30 bars of 3.
            const auto song = rigimport::make (std::move (stems), rigimport::readMidi (logicMidi (90.0, 3, 4, { { 12, "Verse" }, { 36, "Chorus" } })), 120.0, "Tune");
            expectWithinAbsoluteError (song.bpm, 90.0, 0.01);
            expectEquals (song.beatsPerBar, 3);
            expectWithinAbsoluteError (song.bars, 30.0, 1.0e-6);
            expectEquals ((int) song.sections.size(), 3);
            expectEquals (song.sections[0].name, juce::String ("Start"));
            expectWithinAbsoluteError (song.sections[0].bars, 4.0, 1.0e-6);
            expectEquals (song.sections[1].name, juce::String ("Verse"));
            expectWithinAbsoluteError (song.sections[1].bar, 4.0, 1.0e-6);
            expectWithinAbsoluteError (song.sections[2].bars, 18.0, 1.0e-6);

            // No MIDI file: one section, at the box's tempo.
            std::vector<rigimport::Stem> one;
            one.push_back (stem ("Mix", 30.0, 0.1f));
            const auto plain = rigimport::make (std::move (one), {}, 100.0, "Demo");
            expectWithinAbsoluteError (plain.bpm, 100.0, 1.0e-9);
            expectEquals ((int) plain.sections.size(), 1);
            expectEquals (plain.sections[0].name, juce::String ("Demo"));
            expectWithinAbsoluteError (plain.sections[0].bars, 30.0 * 100.0 / 60.0 / 4.0, 1.0e-6);
        }

        beginTest ("more than eight stems: the rest are mixed on loop 8, Other");
        {
            std::vector<rigimport::Stem> stems;
            for (int i = 0; i < 11; ++i)
                stems.push_back (stem ("S" + juce::String (i + 1), 2.0, 0.1f));
            const auto song = rigimport::make (std::move (stems), {}, 120.0, "Big");
            expectEquals ((int) song.layers.size(), 8);
            expectEquals (song.layers[7].name, juce::String ("Other"));
            expectWithinAbsoluteError (song.layers[7].audio.getSample (0, 10), 0.4f, 1.0e-5f, "S8 to S11 together");
        }

        beginTest ("on the box: the tempo, a loop per stem from bar 1, sections on the Song screen, and it plays");
        {
            juce::TemporaryFile folderFile;
            const auto folder = folderFile.getFile();
            folder.createDirectory();
            writeWav (folder.getChildFile ("Tune_Drums.wav"), 8.0, 0.25f);
            writeWav (folder.getChildFile ("Tune_Keys.wav"), 8.0, 0.25f);
            {
                auto mid = logicMidi (120.0, 4, 4, { { 0, "Intro" }, { 8, "Hook" } });
                juce::FileOutputStream out (folder.getChildFile ("Tune.mid"));
                mid.writeTo (out);
            }

            RigEngine engine;
            auto song = rigimport::read (folder, 100.0);
            expectEquals ((int) song.layers.size(), 2);
            expectEquals (song.layers[0].name, juce::String ("Drums"));
            const auto said = rigimport::apply (engine, std::move (song));
            expect (said.contains ("Loop 1 Drums") && said.contains ("Loop 2 Keys"), said);
            expectWithinAbsoluteError (engine.getTransport().getBpm(), 120.0, 0.01);
            for (int i = 0; i < 2; ++i)
            {
                expect (engine.getLooper().getState (i) == RigLooper::State::playing);
                expectWithinAbsoluteError (engine.getLooper().getLayer (i).lengthBeats.load(), 16.0, 1.0e-6);
            }
            expect (engine.getLooper().getState (2) == RigLooper::State::empty);
            const auto sections = rigsong::sections (engine.getTimeline());
            expectEquals ((int) sections.size(), 2);
            expectEquals (sections[1].name, juce::String ("Hook"));
            expectWithinAbsoluteError (sections[1].start, 2.0, 1.0e-6);
            expectWithinAbsoluteError (rigsong::songBars (engine.getTimeline()), 4.0, 1.0e-6);
            expect (engine.getTimeline().on.load());
            expectEquals ((int) engine.getTimeline().getBlocks().size(), 2);
            expect (rigsong::boxBlock (engine.getTimeline(), 1, RigTimeline::laneForLoop (0)) >= 0, "the Hook has the drums");

            // Play the Hook: the stems sound.
            engine.prepare (44100.0, 512);
            engine.launchSection (1);
            engine.getTransport().play();
            juce::AudioBuffer<float> out (2, 512);
            float peak = 0;
            for (int b = 0; b < 40; ++b)
            {
                engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 512);
                peak = juce::jmax (peak, out.getMagnitude (0, 0, 512));
            }
            expectGreaterThan (peak, 0.05f);

            // CRATE_PLUGIN_SNAPSHOT=/path/prefix: a picture of the Song screen with the song on it.
            if (const auto snapshot = juce::SystemStats::getEnvironmentVariable ("CRATE_PLUGIN_SNAPSHOT", {}); snapshot.isNotEmpty())
            {
                RigView view (engine, nullptr);
                view.setSize (1440, 960);
                view.showView ("Song");
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                juce::File file (snapshot + "-imported-song.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }

            // The Song screen names the loops after the stems.
            expect (rigsong::laneHint (engine, RigTimeline::laneForLoop (0)).startsWith ("Drums"), rigsong::laneHint (engine, 0));

            // It saves with the show like any loops, names and all.
            juce::TemporaryFile showFile (".crateshow");
            expect (engine.writeLoopAudio (showFile.getFile()));
            expect (RigEngine::loopsFolder (showFile.getFile()).getChildFile ("Loop 2.wav").existsAsFile());
            {
                RigEngine again;
                again.loadShow (engine.saveShow());
                again.readLoopAudio (showFile.getFile());
                expectEquals (again.getLoopName (1), juce::String ("Keys"));
                expectEquals ((int) rigsong::sections (again.getTimeline()).size(), 2);
                // A loop recorded over it isn't "Keys" any more.
                again.getLooper().setLayerAudio (1, juce::AudioBuffer<float> (2, 100), 4.0);
                expect (again.getLoopName (1).isEmpty());
            }
            RigEngine::loopsFolder (showFile.getFile()).deleteRecursively();
            folder.deleteRecursively();
        }

        beginTest ("a folder with no stems says what to do in Logic, and changes nothing");
        {
            juce::TemporaryFile folderFile;
            folderFile.getFile().createDirectory();
            RigEngine engine;
            const double bpm = engine.getTransport().getBpm();
            auto song = rigimport::read (folderFile.getFile(), 100.0);
            const auto said = rigimport::apply (engine, std::move (song));
            expect (said.contains ("All Tracks as Audio Files"), said);
            expectWithinAbsoluteError (engine.getTransport().getBpm(), bpm, 1.0e-9);
            folderFile.getFile().deleteRecursively();
        }
    }
};

static RigLogicImportTests rigLogicImportTests;
