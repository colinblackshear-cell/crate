#include "TestHelpers.h"
#include "DrumProcessor.h"
#include "core/AudioFiles.h"

using namespace crate;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

juce::AudioBuffer<float> tone (int samples, float level)
{
    juce::AudioBuffer<float> b (1, samples);
    for (int i = 0; i < samples; ++i)
        b.setSample (0, i, level * std::sin (0.05f * (float) i));
    return b;
}

// Plays voice `v` once and returns how many samples it sounded for and its peak.
std::pair<int, float> hear (drums::Engine& e, int v, double seconds = 1.0)
{
    e.trigger (v, 1.0f, false);
    juce::AudioBuffer<float> b (2, kBlock);
    juce::MidiBuffer none;
    drums::Clock stopped;
    int last = 0;
    float peak = 0;
    for (int done = 0; done < (int) (seconds * kRate); done += kBlock)
    {
        b.clear();
        e.render (b.getWritePointer (0), b.getWritePointer (1), kBlock, none, stopped);
        for (int i = 0; i < kBlock; ++i)
        {
            const float x = std::abs (b.getSample (0, i));
            peak = std::max (peak, x);
            if (x > 1.0e-3f)
                last = done + i;
        }
    }
    return { last, peak };
}
}

struct DrumSampleKitTests : juce::UnitTest
{
    DrumSampleKitTests() : juce::UnitTest ("Drum machine: your own kits", "Drums") {}

    void runTest() override
    {
        using namespace drums;

        beginTest ("file names find their sounds");
        {
            std::array<bool, kVoices> none {};
            expectEquals (kits::voiceForName ("Quake_Kick_Ritual.wav", none), (int) kick);
            expectEquals (kits::voiceForName ("BD_808_01.wav", none), (int) kick);
            expectEquals (kits::voiceForName ("Dusty Snare 3.aif", none), (int) snare);
            expectEquals (kits::voiceForName ("Clap_Room.wav", none), (int) clap);
            expectEquals (kits::voiceForName ("Rim_Wood.wav", none), (int) rim);
            expectEquals (kits::voiceForName ("Hat_Closed.wav", none), (int) closedHat);
            expectEquals (kits::voiceForName ("Hat_Open.wav", none), (int) openHat);
            expectEquals (kits::voiceForName ("Crash 2.wav", none), (int) cymbal);
            expectEquals (kits::voiceForName ("Tom_Lo.wav", none), (int) lowTom);
            expectEquals (kits::voiceForName ("Tom_Hi.wav", none), (int) highTom);
            expectEquals (kits::voiceForName ("Cowbell.wav", none), (int) cowbell);
            expectEquals (kits::voiceForName ("Vox chop.wav", none), -1);
            auto taken = none;
            taken[closedHat] = true;
            expectEquals (kits::voiceForName ("hihat 2.wav", taken), (int) openHat, "a second hat goes to the open hat");
        }

        beginTest ("a folder maps to a kit, leftovers fill the empty percussion");
        {
            juce::Array<juce::File> files;
            for (auto* n : { "Kick.wav", "Snare.wav", "Tom.wav", "Tom 2.wav", "Tom 3.wav", "Hat.wav", "Vox.wav", "FX hit.wav" })
                files.add (juce::File ("/tmp/kit/" + juce::String (n)));
            const auto map = kits::mapFiles (files);
            expectEquals (map[kick].getFileName(), juce::String ("Kick.wav"));
            expectEquals (map[snare].getFileName(), juce::String ("Snare.wav"));
            expectEquals (map[lowTom].getFileName(), juce::String ("Tom.wav"));
            expectEquals (map[midTom].getFileName(), juce::String ("Tom 2.wav"));
            expectEquals (map[highTom].getFileName(), juce::String ("Tom 3.wav"));
            expectEquals (map[closedHat].getFileName(), juce::String ("Hat.wav"));
            expectEquals (map[clave].getFileName(), juce::String ("FX hit.wav"));
            expectEquals (map[cowbell].getFileName(), juce::String ("Vox.wav"));
        }

        beginTest ("a sample plays instead of the analog sound; tune, decay and mute work on it");
        {
            Engine e;
            e.prepare (kRate);
            Settings s;
            s.voices[snare].decay = 1.0f;   // all the way: as recorded
            e.setSettings (s);
            auto smp = std::make_shared<Sample>();
            smp->audio = share (tone (4410, 0.8f));   // 0.1 s at 44.1 kHz
            e.setSample (snare, smp.get());
            const auto plain = hear (e, snare);
            expect (plain.second > 0.1f, "it sounds");
            expectWithinAbsoluteError ((double) plain.first, 0.1 * kRate, 0.01 * kRate);

            s.voices[snare].tune = 12.0f;   // an octave up: half as long
            e.setSettings (s);
            expectWithinAbsoluteError ((double) hear (e, snare).first, 0.05 * kRate, 0.01 * kRate);

            s.voices[snare].tune = 0;
            s.voices[snare].decay = 0.0f;   // shortest: fades out fast
            e.setSettings (s);
            expect (hear (e, snare).first < plain.first, "decay shortens it");

            s.voices[snare].mute = true;
            e.setSettings (s);
            expectEquals (hear (e, snare).first, 0, "muted");

            e.setSample (snare, nullptr);
            s.voices[snare].mute = false;
            s.voices[snare].decay = 0.5f;
            e.setSettings (s);
            expect (hear (e, snare).second > 0.01f, "back to the analog snare");
        }

        beginTest ("the kit saves with the project, even when the files are gone");
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-drumkit-test");
            dir.deleteRecursively();
            dir.createDirectory();
            expect (AudioFiles::write (dir.getChildFile ("Kick_Deep.wav"), tone (8000, 0.5f)));
            expect (AudioFiles::write (dir.getChildFile ("Snare_Dusty.wav"), tone (6000, 0.5f)));
            expect (AudioFiles::write (dir.getChildFile ("Hat_Closed.wav"), tone (2000, 0.5f)));

            DrumProcessor a;
            expectEquals (a.loadKitFolder (dir), 3);
            expectEquals (a.getKitName(), juce::String ("crate-drumkit-test"));
            expect (a.getVoiceSample (kick) != nullptr && a.getVoiceSample (kick)->name == "Kick_Deep.wav");
            expect (a.getVoiceSample (clap) == nullptr, "no clap in the folder: the analog clap");
            juce::MemoryBlock state;
            a.getStateInformation (state);

            dir.deleteRecursively();   // the files move away: the project still has the sounds
            DrumProcessor b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            expect (b.getVoiceSample (snare) != nullptr);
            expectEquals (b.getVoiceSample (snare)->audio->getNumSamples(), 6000);
            expectEquals (b.getKitName(), juce::String ("crate-drumkit-test"));

            DrumProcessor c;
            c.setStateInformation (state.getData(), (int) state.getSize());
            c.clearSamples();
            expect (! c.hasSamples());
        }

        beginTest ("a kit file keeps each sound's file and knobs");
        {
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-drumkit-file");
            dir.deleteRecursively();
            dir.getChildFile ("sounds").createDirectory();
            expect (AudioFiles::write (dir.getChildFile ("sounds").getChildFile ("Kick.wav"), tone (4000, 0.5f)));
            kits::Kit kit;
            kit.name = "Tribal night";
            kit.files[kick] = dir.getChildFile ("sounds").getChildFile ("Kick.wav");
            kit.hasVoices = true;
            kit.voices[kick].tune = -3.0f;
            const auto file = dir.getChildFile ("Tribal night.drumkit");
            expect (kits::save (kit, file));
            kits::Kit back;
            expect (kits::load (file, back));
            expectEquals (back.name, juce::String ("Tribal night"));
            expect (back.files[kick] == kit.files[kick]);
            expect (back.files[snare] == juce::File());
            expectWithinAbsoluteError (back.voices[kick].tune, -3.0f, 0.001f);

            DrumProcessor p;
            expect (p.loadUserKit (file));
            expect (p.getVoiceSample (kick) != nullptr);
            expectWithinAbsoluteError (p.params.getRawParameterValue ("kickTune")->load(), -3.0f, 0.01f);
            dir.deleteRecursively();
        }
    }
};

static DrumSampleKitTests drumSampleKitTests;
