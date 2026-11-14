#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigSong.h"
#include "RigScenes.h"
#include "RigView.h"
#include "SongView.h"
#include "TimelineView.h"

namespace
{
int laneOf (int strip) { return RigTimeline::laneForStrip (strip); }

// One audio block, so the clock takes what it was asked.
void step (RigEngine& engine)
{
    juce::AudioBuffer<float> out (2, 480);
    engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
}

template <typename T>
T* findIn (juce::Component& c)
{
    if (auto* t = dynamic_cast<T*> (&c))
        return t;
    for (auto* child : c.getChildren())
        if (auto* t = findIn<T> (*child))
            return t;
    return nullptr;
}

void snapshot (juce::Component& c, const juce::String& path)
{
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f);
    juce::File (path).deleteFile();
    juce::FileOutputStream out { juce::File (path) };
    juce::PNGImageFormat().writeImageToStream (image, out);
}

double paramOf (juce::AudioProcessorValueTreeState& s, const char* id)
{
    auto* p = s.getParameter (id);
    return p != nullptr ? p->convertFrom0to1 (p->getValue()) : -1.0;
}
}

struct RigSongTests : juce::UnitTest
{
    RigSongTests() : juce::UnitTest ("Rig song", "Rig") {}

    void runTest() override
    {
        using namespace rigsong;

        beginTest ("sections sit end to end, and boxes fill them");
        {
            RigTimeline t;
            expect (sections (t).empty());
            expectEquals (suggestName (t), juce::String ("Intro"));
            expectEquals (addSection (t, "Intro", 4), 0);
            expectEquals (suggestName (t), juce::String ("Verse"));
            expectEquals (addSection (t, "Verse", 8), 1);
            expectEquals (addSection (t, "Chorus", 8), 2);
            auto list = sections (t);
            expectEquals ((int) list.size(), 3);
            expectEquals (list[1].start, 4.0);
            expectEquals (list[2].start, 12.0);
            expectEquals (songBars (t), 20.0);
            expectEquals (sectionAt (t, 13.0), 2);
            expectEquals (sectionAt (t, 25.0), -1);

            const int bass = laneOf (RigEngine::bassStrip);
            const int b = setBox (t, 1, bass, 2);
            expectGreaterOrEqual (b, 0);
            expectEquals (t.getBlocks()[(size_t) b].start, 4.0);
            expectEquals (t.getBlocks()[(size_t) b].length, 8.0, "a box fills its section");
            expectEquals (t.getBlocks()[(size_t) b].clip, 2);
            expectEquals (boxBlock (t, 0, bass), -1, "the Intro's box is empty");
            expectEquals (boxBlock (t, 2, bass), -1);

            // Fades and level stay when the pattern changes.
            t.getBlocks()[(size_t) b].fadeIn = 1.0;
            t.getBlocks()[(size_t) b].level = 0.5f;
            const int b2 = setBox (t, 1, bass, 3);
            expectEquals (t.getBlocks()[(size_t) b2].clip, 3);
            expectEquals (t.getBlocks()[(size_t) b2].fadeIn, 1.0);
            expectEquals (t.getBlocks()[(size_t) b2].level, 0.5f);
            expectEquals ((int) t.getBlocks().size(), 1);

            // A longer verse: the box grows with it, and the chorus moves along.
            setBox (t, 2, bass, 1);
            setSectionBars (t, 1, 12);
            list = sections (t);
            expectEquals (list[1].bars, 12.0);
            expectEquals (list[2].start, 16.0);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 1, bass)].length, 12.0);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 2, bass)].start, 16.0, "the chorus's box moved with it");
            // Shorter again: the box shrinks.
            setSectionBars (t, 1, 4);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 1, bass)].length, 4.0);
            expectEquals (sections (t)[2].start, 8.0);

            // Copy the verse: right after it, with its boxes.
            expectEquals (duplicateSection (t, 1), 2);
            list = sections (t);
            expectEquals ((int) list.size(), 4);
            expectEquals (list[2].name, juce::String ("Verse"));
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 2, bass)].clip, 3);
            expectEquals (list[3].name, juce::String ("Chorus"));
            expectEquals (list[3].start, 12.0);

            // Move the chorus to the front, with its box.
            moveSection (t, 3, 0);
            list = sections (t);
            expectEquals (list[0].name, juce::String ("Chorus"));
            expectEquals (list[1].start, 8.0);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 0, bass)].clip, 1);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 0, bass)].start, 0.0);

            // Delete the intro (now second): the rest moves up.
            removeSection (t, 1);
            list = sections (t);
            expectEquals ((int) list.size(), 3);
            expectEquals (list[1].name, juce::String ("Verse"));
            expectEquals (list[1].start, 8.0);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 1, bass)].start, 8.0);

            // Same as the chorus.
            copyBox (t, 0, 2, bass);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 2, bass)].clip, 1);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 2, bass)].length, 4.0, "fills the shorter section");

            clearBox (t, 2, bass);
            expectEquals (boxBlock (t, 2, bass), -1);

            renameSection (t, 0, "Hook");
            RigTimeline copy;
            copy.fromTree (t.toTree());
            expectEquals (sections (copy)[0].name, juce::String ("Hook"));
            expectEquals (sections (copy).back().bars, sections (t).back().bars, "the last section keeps its length");
            expectEquals (copy.getBlocks()[(size_t) boxBlock (copy, 0, bass)].clip, 1, "the box keeps its pattern");
        }

        beginTest ("a box cuts across a long block made in Bars view");
        {
            RigTimeline t;
            addSection (t, "A", 4);
            addSection (t, "B", 4);
            addSection (t, "C", 4);
            RigTimeline::Block long_;
            long_.lane = 0;
            long_.start = 0;
            long_.length = 12;
            t.getBlocks().push_back (long_);
            t.changed();
            expect (boxCarriesOn (t, 1, 0), "the loop carries on into B");
            clearBox (t, 1, 0);
            expectEquals ((int) t.getBlocks().size(), 2, "cut in two");
            expectEquals (boxBlock (t, 1, 0), -1);
            expectEquals (t.getBlocks()[(size_t) boxBlock (t, 2, 0)].start, 8.0);
        }

        beginTest ("level: a box plays at its level");
        {
            RigTimeline t;
            addSection (t, "A", 4);
            const int i = setBox (t, 0, 0, -1);
            t.getBlocks()[(size_t) i].level = 0.5f;
            t.changed();
            expectWithinAbsoluteError (t.gainAt (0, 1.0), 0.5f, 1.0e-5f);
        }

        beginTest ("playing the song sets each box's pattern; Play follows Jam / Song");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& t = engine.getTimeline();
            const int bass = laneOf (RigEngine::bassStrip), drums = laneOf (RigEngine::drumsStrip);
            addSection (t, "Intro", 2);
            addSection (t, "Verse", 2);
            setBox (t, 0, bass, 4);
            setBox (t, 1, bass, 6);
            setBox (t, 1, drums, 2);
            engine.getStrip (RigEngine::bassStrip).mute = true;

            t.on = true;
            engine.playOrStop();
            step (engine);
            expect (engine.getTransport().isPlaying());
            expect (! engine.getStrip (RigEngine::bassStrip).mute.load(), "the song brings in what it plays");
            engine.tickScenes();
            expectEquals (paramOf (engine.getBass().params, "pattern"), 4.0, "the intro's bass pattern");
            expectEquals (paramOf (engine.getBass().params, "sequencer"), 1.0, "and it runs");

            engine.getTransport().setPosition (4 * 2 - 0.1);   // just before the verse
            step (engine);
            engine.tickScenes();
            expectEquals (paramOf (engine.getBass().params, "pattern"), 6.0);
            expectEquals (paramOf (engine.getDrums().params, "pattern"), 2.0);

            engine.playOrStop();
            step (engine);
            expect (! engine.getTransport().isPlaying());

            // Section keys: the verse, from its first bar.
            engine.launchSection (1);
            step (engine);
            expect (engine.getTransport().isPlaying());
            expectWithinAbsoluteError (engine.getTransport().getPositionInBeats(), 8.0, 0.1);
            engine.playOrStop();
            step (engine);

            // Jam: Play just runs the clock, from where it is.
            t.on = false;
            engine.getTransport().setPosition (2.0);
            step (engine);
            engine.playOrStop();
            step (engine);
            expect (engine.getTransport().isPlaying());
            expectWithinAbsoluteError (engine.getTransport().getPositionInBeats(), 2.0, 0.1);
            engine.playOrStop();
        }

        beginTest ("old shows: the Song builder's parts become sections");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto& parts = engine.getScenes().getScenes();
            RigScene intro, verse;
            intro.name = "Intro";
            intro.bars = 4;
            intro.state.bassSequencer = 1;
            intro.state.bassPattern = 2;
            intro.state.stripMutes[(size_t) RigEngine::bassStrip] = 0;
            verse.name = "Verse";
            verse.bars = 0;   // Hold
            verse.state.stripMutes[(size_t) RigEngine::bassStrip] = 1;
            verse.state.loopBank = 1;
            parts = { intro, verse };
            expect (sectionsFromParts (engine));
            const auto list = sections (engine.getTimeline());
            expectEquals ((int) list.size(), 2);
            expectEquals (list[0].name, juce::String ("Intro"));
            expectEquals (list[1].bars, 8.0, "Hold becomes 8 bars");
            const int bass = laneOf (RigEngine::bassStrip);
            expectEquals (engine.getTimeline().getBlocks()[(size_t) boxBlock (engine.getTimeline(), 0, bass)].clip, 2);
            expectEquals (boxBlock (engine.getTimeline(), 1, bass), -1, "muted in the verse");
            expectEquals (engine.getTimeline().getMarkers()[1].loopBank, 1);
            expect (! sectionsFromParts (engine), "only once");
        }

        beginTest ("the Song screen: F3, sections, boxes, the panel, Jam / Song, Bars view, the section keys");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 900);
            // CRATE_SONG_SNAPSHOT=/folder saves pictures of it there.
            const auto folder = juce::SystemStats::getEnvironmentVariable ("CRATE_SONG_SNAPSHOT", {});

            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F3Key)));
            expectEquals (view.getPage(), juce::String ("Song"));
            auto* song = findIn<SongView> (view);
            expect (song != nullptr);
            if (song == nullptr)
                return;
            if (folder.isNotEmpty())
                snapshot (view, folder + "/song-empty.png");

            // Some loops, so they have lanes.
            const char* sources[] = { "guitar", "keys", "vox chop" };
            juce::ignoreUnused (sources);
            for (int l = 0; l < 3; ++l)
            {
                juce::AudioBuffer<float> a (2, 48000 * 2 * (l + 1));
                a.clear();
                engine.getLooper().setLayerAudio (l, a, 4.0 * (l + 1));
            }
            auto& t = engine.getTimeline();
            const int mpc = laneOf (RigEngine::mpcStrip), bass = laneOf (RigEngine::bassStrip), synth = laneOf (RigEngine::synthStrip),
                      voc = laneOf (RigEngine::vocoderStrip), drums = laneOf (RigEngine::drumsStrip);
            expectEquals (song->addEmptySection(), 0);
            expectEquals (rigsong::sections (t)[0].name, juce::String ("Intro"));
            song->setSectionLength (4);
            song->select (0, mpc);
            song->pickClip (0);
            song->select (0, synth);
            song->pickClip (0);
            song->setFadeIn (2.0);
            song->select (0, 0);
            song->pickClip (-1);
            expectEquals (song->addEmptySection(), 1);
            song->setSectionLength (8);
            for (int lane : { mpc, 0, voc })
            {
                song->select (1, lane);
                song->pickClip (lane == mpc ? 0 : -1);
            }
            song->select (1, bass);
            song->pickClip (1);
            song->setFadeOut (2.0);
            expectEquals (t.getBlocks()[(size_t) rigsong::boxBlock (t, 1, bass)].clip, 1);
            expectEquals (t.getBlocks()[(size_t) rigsong::boxBlock (t, 1, bass)].fadeOut, 2.0);
            song->addEmptySection();   // Chorus
            song->setSectionLength (8);
            for (int lane : { mpc, drums, bass, synth, 0, 1, voc })
            {
                song->select (2, lane);
                song->pickClip (lane == bass ? 2 : lane == synth ? 1 : lane == drums ? 3 : lane == mpc ? 0 : -1);
            }
            song->select (2, 1);
            song->setFadeIn (1.0);
            song->addEmptySection();   // Verse 2
            song->setSectionLength (4);
            for (int lane : { synth, 1, 2 })
            {
                song->select (3, lane);
                song->pickClip (lane == synth ? 0 : -1);
            }
            song->select (2, -1);
            song->showBars (false);
            {
                // Copy the chorus to the end, then an outro.
                const int c = rigsong::duplicateSection (t, 2);
                rigsong::moveSection (t, c, 4);
                song->refresh();
            }
            song->addEmptySection();
            song->setSectionLength (4);
            song->select (5, mpc);
            song->pickClip (0);
            song->setFadeOut (4.0);
            song->select (5, 1);
            song->pickClip (-1);
            song->setFadeOut (4.0);
            expectEquals ((int) rigsong::sections (t).size(), 6);
            expectEquals (rigsong::songBars (t), 36.0);

            // The section keys: A-F are the sections, G saves what's playing.
            song->setSongMode (true);
            expect (t.on.load(), "Song mode");
            rigpanels::Footer::pressPart (engine, 1);
            step (engine);
            expect (engine.getTransport().isPlaying());
            expectWithinAbsoluteError (engine.getTransport().getPositionInBeats(), 16.0, 0.1, "the verse starts on bar 5");
            engine.getTransport().setPosition (4.0 * 6.5);
            step (engine);
            engine.tickScenes();

            song->select (1, bass);
            view.resized();
            if (folder.isNotEmpty())
            {
                snapshot (view, folder + "/song-screen.png");
                song->select (2, -1);
                snapshot (view, folder + "/song-section.png");
                song->showBars (true);
                snapshot (view, folder + "/song-bars.png");
                song->showBars (false);
            }

            song->setSongMode (false);
            expect (! t.on.load(), "Jam mode");
            engine.playOrStop();
            step (engine);

            // Bars view is the same song: back and forward.
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F1Key)));
            view.goTo ("Song");
            song->showBars (true);
            expectEquals (view.getPlace(), juce::String ("Song / Bars"));
            expect (song->getBarsView() != nullptr && song->getBarsView()->isVisible());
            song->showBars (false);
            expectEquals (view.getPlace(), juce::String ("Song"));
        }

        beginTest ("what's playing, as a new section");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            auto* seq = engine.getBass().params.getParameter ("sequencer");
            seq->setValueNotifyingHost (1.0f);
            auto* pat = engine.getBass().params.getParameter ("pattern");
            pat->setValueNotifyingHost (pat->convertTo0to1 (5.0f));
            engine.getStrip (RigEngine::bassStrip).mute = false;
            const int i = addSectionFromWhatsPlaying (engine, "Verse", 8);
            expectEquals (i, 0);
            const int b = boxBlock (engine.getTimeline(), 0, laneOf (RigEngine::bassStrip));
            expectGreaterOrEqual (b, 0);
            expectEquals (engine.getTimeline().getBlocks()[(size_t) b].clip, 5);
            expectEquals (clipName (engine, laneOf (RigEngine::bassStrip), 5), juce::String ("Pattern 6"));
            expectEquals (laneName (laneOf (RigEngine::drumsStrip)), juce::String ("Drum machine"));
        }
    }
};

static RigSongTests rigSongTests;
