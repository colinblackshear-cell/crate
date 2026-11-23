#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigIdeas.h"
#include "RigTitle.h"
#include "RigView.h"
#include "GrooveView.h"
#include "core/Spark.h"
#include "core/ui/LanePeaks.h"

// My ideas on the Mac (the front page, as the phone's) and the opening title.
class RigIdeasTests : public juce::UnitTest
{
public:
    RigIdeasTests() : juce::UnitTest ("Rig: My ideas and the title", "Rig") {}

    // A layer's sound: hits, notes or a voice, so the lanes look like something.
    static juce::AudioBuffer<float> sound (int kind, int samples)
    {
        juce::AudioBuffer<float> b (2, samples);
        for (int i = 0; i < samples; ++i)
        {
            const double t = i / 48000.0;
            float v = 0.0f;
            if (kind == 0)        // hits
                v = (float) (std::exp (-std::fmod (t, 0.25) * 30.0) * std::sin (t * 2.0 * juce::MathConstants<double>::pi * 60.0));
            else if (kind == 1)   // notes
                v = (float) (0.6 * std::exp (-std::fmod (t, 0.5) * 6.0) * std::sin (t * 2.0 * juce::MathConstants<double>::pi * 220.0));
            else                  // a voice, swelling
                v = (float) (0.5 * (0.5 + 0.5 * std::sin (t * 3.1)) * std::sin (t * 2.0 * juce::MathConstants<double>::pi * 180.0));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        return b;
    }

    // Saves a song with these layers (each a RigLooper::Source) into `folder`, `daysAgo` old.
    static juce::File makeSong (const juce::File& folder, const juce::String& name, double bpm, std::vector<int> sources, double daysAgo)
    {
        RigEngine engine;
        engine.prepare (48000.0, 480);
        engine.getTransport().setBpm (bpm);
        for (int l = 0; l < (int) sources.size(); ++l)
        {
            const int s = sources[(size_t) l];
            const int kind = s == RigLooper::drums || s == RigLooper::mpc ? 0 : (s == RigLooper::input || s == RigLooper::vocoder ? 2 : 1);
            const double beats = l % 2 == 0 ? 4.0 : 8.0;
            engine.getLooper().setLayerAudio (l, sound (kind, (int) (48000.0 * 60.0 / bpm * beats)), beats);
            engine.getLooper().getLayer (l).source = s;
        }
        const auto file = folder.getChildFile (name + ".crateshow");
        engine.saveShowFile (file);
        file.setLastModificationTime (juce::Time::getCurrentTime() - juce::RelativeTime::days (daysAgo));
        return file;
    }

    static void snapshot (juce::Component& c, const juce::String& path)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        juce::File file (path);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    void runTest() override
    {
        juce::TemporaryFile temp;
        const auto folder = temp.getFile();
        folder.createDirectory();

        beginTest ("YOUR SONGS: every saved song, newest first, the Last show at the end");
        const auto porch = makeSong (folder, "Porch at dusk", 92.0, { RigLooper::drums, RigLooper::bass, RigLooper::input, RigLooper::synth }, 0.02);
        const auto ritual = makeSong (folder, "Ritual sketch", 104.0, { RigLooper::mpc, RigLooper::drone }, 1.0);
        const auto rain = makeSong (folder, "Rain loop", 118.0, { RigLooper::sampler }, 3.0);
        makeSong (folder, "Last show", 120.0, { RigLooper::drums }, 0.0);
        {
            RigIdeas ideas (folder);
            ideas.setSize (1440, 900);
            ideas.refresh();
            const auto& songs = ideas.songs();
            expectEquals ((int) songs.size(), 4);
            expectEquals (songs[0].name, juce::String ("Porch at dusk"));
            expectEquals (songs[1].name, juce::String ("Ritual sketch"));
            expectEquals (songs[2].name, juce::String ("Rain loop"));
            expect (songs[3].lastShow, "the Last show stays at the end, even though it's the newest");
            expectEquals (songs[0].layers(), 4);
            expectWithinAbsoluteError (songs[0].bpm, 92.0, 0.01);
            expectEquals (songs[0].sources[2], (int) RigLooper::input);
            expectEquals (songs[1].sources[1], (int) RigLooper::drone);

            // Each layer's saved sound is found (in the revision folder the show names), and drawn.
            for (int l = 0; l < songs[0].layers(); ++l)
            {
                const auto wav = RigIdeas::loopFile (songs[0], l);
                expect (wav.existsAsFile(), wav.getFullPathName());
                const auto peaks = crate::lanes::peaksOf (wav, 60);
                expectEquals ((int) peaks.size(), 60);
                expect (*std::max_element (peaks.begin(), peaks.end()) > 0.9f);
            }
        }

        beginTest ("stars stay (in My ideas.json); the filters and search");
        {
            {
                RigIdeas ideas (folder);
                ideas.refresh();
                ideas.setStarred (rain, true);
            }
            RigIdeas ideas (folder);
            ideas.setSize (1440, 900);
            ideas.refresh();
            expect (ideas.songs()[2].starred, "the star is still there in a new My ideas");
            ideas.setFilter (1);
            expectEquals ((int) ideas.order().size(), 1);
            expect (ideas.songs()[(size_t) ideas.order()[0]].file == rain);
            ideas.setFilter (2);   // A-Z
            expectEquals (ideas.songs()[(size_t) ideas.order()[0]].name, juce::String ("Last show (auto)"));
            expectEquals (ideas.songs()[(size_t) ideas.order()[1]].name, juce::String ("Porch at dusk"));
            ideas.setFilter (3);   // most layers
            expect (ideas.songs()[(size_t) ideas.order()[0]].file == porch);
            ideas.setFilter (0);
            ideas.setSearch ("drone");
            expectEquals ((int) ideas.order().size(), 1, "a tool finds the songs with it");
            expect (ideas.songs()[(size_t) ideas.order()[0]].file == ritual);
            ideas.setSearch ("118");
            expect (ideas.songs()[(size_t) ideas.order()[0]].file == rain, "a tempo finds its songs");
            ideas.setSearch ("porch");
            expectEquals ((int) ideas.order().size(), 1);
            ideas.setSearch ({});
            expectEquals ((int) ideas.order().size(), 4);

            // Every visible button is wired to something.
            int calls = 0;
            ideas.onCatch = ideas.onNew = ideas.onLesson = ideas.onTour = ideas.onGuide = ideas.onOpenFile = ideas.onImportLogic
                = ideas.onSettings = ideas.onPlayHero = ideas.onOpenHero = [&calls] { ++calls; };
            ideas.onSpark = [&calls] (const crate::spark::Spark&) { ++calls; };
            for (auto name : { "catch", "new", "spark", "open", "play", "lesson", "tour", "guide", "file", "logic", "settings" })
            {
                auto* b = ideas.button (name);
                expect (b != nullptr && b->isVisible() && b->getWidth() > 30, name);
                if (b != nullptr)
                    b->onClick();
            }
            expectEquals (calls, 11);
            const auto before = ideas.spark().title;
            for (int i = 0; i < 3 && ideas.spark().title == before; ++i)
                ideas.button ("roll")->onClick();
            expect (ideas.spark().title != before, "ROLL ANOTHER deals another spark");
            int tile = -1;
            ideas.onStartWith = [&tile] (int t) { tile = t; };
            ideas.pressTile (6);
            expectEquals (tile, 6);
            juce::File opened;
            ideas.onOpen = [&opened] (const juce::File& f) { opened = f; };
            ideas.clickSong (1);
            expect (opened == ritual);
            expect (ideas.menuFor (0).getNumItems() >= 6, "the ··· menu: open, play, star, rename, show in Finder, trash");
            expect (ideas.howItWorksOpen());
            ideas.setHowItWorksOpen (false);
            expect (! RigIdeas (folder).howItWorksOpen(), "folding HOW IT WORKS away stays");
            ideas.setHowItWorksOpen (true);
        }

        beginTest ("the phone and the Mac show the same spark tonight");
        {
            const auto a = crate::spark::make (crate::spark::tonight());
            RigIdeas ideas (folder);
            expectEquals (ideas.spark().title, a.title);
            expect (a.bpm >= 60 && a.bpm <= 180 && a.steps[2].text.isNotEmpty());
        }

        beginTest ("renaming a song takes its loops with it");
        {
            juce::File renamed;
            expect (RigIdeas::renameSong (rain, "Rain on the roof", renamed).isEmpty());
            expect (! rain.exists() && renamed.existsAsFile());
            expect (RigIdeas::renameSong (renamed, "Porch at dusk", renamed).isNotEmpty(), "not over another song");
            expect (RigIdeas::renameSong (renamed, "Last show", renamed).isNotEmpty(), "not as the Last show");
            RigEngine engine;
            engine.prepare (48000.0, 480);
            if (auto xml = juce::parseXML (renamed))
                engine.loadShow (juce::ValueTree::fromXml (*xml));
            engine.readLoopAudio (renamed);
            expect (engine.getLoopAudioProblem().isEmpty(), engine.getLoopAudioProblem());
            expect (engine.getLooper().getLayer (0).lengthBeats.load() > 0.0, "its loop came along");
            expect (! folder.getChildFile ("Rain loop loops").exists());
            juce::File back;
            RigIdeas::renameSong (renamed, "Rain loop", back);
        }

        beginTest ("the Rig opens on My ideas; its ways in start a song");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            expectEquals (view.getPage(), juce::String ("Ideas"));
            expect (view.getIdeas() != nullptr && view.getIdeas()->isVisible());
            expect (view.getTitle() == nullptr, "no title in the tests or in Logic");

            expect (engine.isBlank());
            view.catchIdea();
            expectEquals (view.getTakeLayer(), 0, "CATCH AN IDEA records onto loop 1 straight away");
            expectEquals (engine.getLooper().getLayer (0).source.load(), (int) RigLooper::input, "your mic as it comes in");
            expectEquals (engine.getLooper().getLayer (0).bars.load(), 0, "free length");
            expectEquals (view.getPage(), juce::String ("Groove"));

            RigEngine e2;
            e2.prepare (48000.0, 480);
            RigView v2 (e2, nullptr);
            v2.setSize (1440, 960);
            auto spark = crate::spark::make (7);
            v2.startSpark (spark);
            expectWithinAbsoluteError (e2.getTransport().getBpm(), (double) spark.bpm, 0.01);
            expectEquals (e2.getChords().fillKey, spark.root);
            expectEquals (e2.getChords().fillScale, spark.minor ? 1 : 0);
            expect (! v2.getPage().isEmpty() && v2.getPage() != "Ideas", "the spark opens its first tool");

            // Each tile on a blank Rig (on one that isn't, it asks to save first).
            auto tileOn = [] (int tile, std::function<void (RigEngine&, RigView&)> check)
            {
                RigEngine e;
                e.prepare (48000.0, 480);
                RigView v (e, nullptr);
                v.setSize (1440, 960);
                v.startWithTile (tile);
                check (e, v);
            };
            tileOn (0, [this] (RigEngine&, RigView& v) { expectEquals (v.getTrack(), (int) GrooveView::machine); });
            tileOn (6, [this] (RigEngine&, RigView& v) { expectEquals (v.getTrack(), (int) GrooveView::bass); });
            tileOn (5, [this] (RigEngine&, RigView& v) { expectEquals (v.getPage(), juce::String ("Sampler")); });
            // Line in with the Input strip off records your mic (the strip would be silent).
            tileOn (7, [this] (RigEngine& e, RigView& v)
            {
                expectEquals (v.getTrack(), (int) GrooveView::lineIn);
                const int layer = v.pressRecTake();
                expect (layer >= 0);
                if (layer >= 0)
                    expectEquals (e.getLooper().getLayer (layer).source.load(), (int) RigLooper::input);
            });
        }

        beginTest ("the title: its ways in");
        {
            RigTitle title;
            title.setSize (1440, 900);
            int started = 0, toured = 0, continued = 0;
            juce::File recent;
            title.onStart = [&started] { ++started; };
            title.onTour = [&toured] { ++toured; };
            title.onContinue = [&continued] { ++continued; };
            title.onRecent = [&recent] (const juce::File& f) { recent = f; };
            expect (title.continueCard().isEmpty(), "no song yet: no card");
            title.setContinue ("Porch at dusk", juce::String (juce::CharPointer_UTF8 ("92 BPM \xc2\xb7 4 layers")), { juce::Colours::red, juce::Colours::orange });
            title.setRecent ({ { "Ritual sketch", ritual }, { "Rain loop", rain } });
            title.resized();
            expect (! title.continueCard().isEmpty());
            expectEquals (title.recentCount(), 2);
            title.startButton().onClick();
            title.tourButton().onClick();
            expect (title.keyPressed (juce::KeyPress (juce::KeyPress::returnKey)));
            expectEquals (started, 2);
            expectEquals (toured, 1);

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_IDEAS_SNAPSHOT", {}); path.isNotEmpty())
            {
                title.showOpen();
                snapshot (title, path + "-title.png");
                title.setSize (1180, 760);
                title.showOpen();
                snapshot (title, path + "-title-small.png");

                RigIdeas ideas (folder);
                ideas.getHero = [&ideas]
                {
                    RigIdeas::Hero h;
                    const auto& s = ideas.songs()[0];
                    h.name = s.name;
                    h.file = s.file;
                    h.open = true;
                    h.sources = s.sources;
                    for (int l = 0; l < s.layers(); ++l)
                        h.peaks.push_back (crate::lanes::peaksOf (RigIdeas::loopFile (s, l), 120));
                    h.meta = juce::String (juce::CharPointer_UTF8 ("Open now \xc2\xb7 saved ")) + RigIdeas::whenText (s.modified).toLowerCase();
                    h.lcd = juce::String (juce::CharPointer_UTF8 ("92 BPM \xc2\xb7 3 SECTIONS"));
                    return h;
                };
                ideas.setSize (1440, 900);
                ideas.refresh();
                snapshot (ideas, path + "-ideas.png");
                ideas.setSize (1440, 1500);
                snapshot (ideas, path + "-ideas-full.png");
                ideas.setSize (980, 1700);
                snapshot (ideas, path + "-ideas-narrow.png");
            }
        }
        folder.deleteRecursively();
    }
};

static RigIdeasTests rigIdeasTests;
