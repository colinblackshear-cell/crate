// Saving a show never breaks the last saved one (RigShowFiles): legacy shows load, unchanged
// audio isn't written again, a failed or killed save leaves the old show whole, and the plugin
// never pairs its state with the wrong sound.
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <iostream>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include "RigEngine.h"
#include "RigPlugin.h"
#include "RigShowFiles.h"
#include "RigLastShow.h"
#include "RigSong.h"

namespace
{
constexpr double kRate = 8000.0;   // the fixture's rate (small files)
constexpr const char* kChildTest = "Rig show files (killed save)";

juce::File fixtureFolder() { return juce::File (KALEIDOBOX_TEST_FIXTURES).getChildFile ("legacy-show"); }

juce::File newTempFolder (const juce::String& name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                 .getChildFile ("kb-showfiles-" + name + "-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    f.createDirectory();
    return f;
}

// A copy of the legacy fixture (made by the code on main before this change), so nothing
// touches the checked-in one.
juce::File copyOfFixture (const juce::File& into)
{
    fixtureFolder().copyDirectoryTo (into);
    return into.getChildFile ("Legacy show.crateshow");
}

juce::AudioBuffer<float> tone (int frames, double hz, float level)
{
    juce::AudioBuffer<float> b (2, frames);
    for (int i = 0; i < frames; ++i)
    {
        const float v = level * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / kRate);
        b.setSample (0, i, v);
        b.setSample (1, i, 0.5f * v);
    }
    return b;
}

bool load (RigEngine& engine, const juce::File& show)
{
    auto xml = juce::parseXML (show);
    if (xml == nullptr)
        return false;
    engine.loadShow (juce::ValueTree::fromXml (*xml));
    engine.readLoopAudio (show);
    return true;
}

// Everything a save keeps of the loops: each layer's sound, length and name, every take (with
// its notes), every bank's sound. Sound is compared within 24-bit rounding.
struct Snapshot
{
    juce::StringArray meta;
    std::vector<juce::AudioBuffer<float>> audio;

    void add (const juce::String& what, const juce::AudioBuffer<float>& a)
    {
        meta.add (what + " frames=" + juce::String (a.getNumSamples()));
        audio.push_back (a);
    }
};

Snapshot snapshot (RigEngine& engine)
{
    Snapshot s;
    auto& looper = engine.getLooper();
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        const auto a = looper.copyLayerAudio (i);
        if (a.getNumSamples() == 0)
            continue;
        s.add ("loop " + juce::String (i + 1) + " beats=" + juce::String (looper.getLayer (i).lengthBeats.load(), 3)
                   + " name=" + engine.getLoopName (i), a);
    }
    for (const auto& set : engine.getTakes().getSets())
    {
        s.meta.add ("takes layer=" + juce::String (set.layer) + " playing=" + juce::String (set.playing) + " count=" + juce::String ((int) set.takes.size()));
        for (const auto& t : set.takes)
            s.add ("  take " + juce::String (t.number) + " notes=" + juce::String ((int) t.notes.size()), t.audio);
    }
    const auto& banks = engine.getLoopBanks();
    s.meta.add ("banks=" + juce::String ((int) banks.size()) + " current=" + juce::String (engine.getCurrentLoopBank()));
    for (int b = 0; b < (int) banks.size(); ++b)
    {
        s.meta.add ("bank " + juce::String (b) + " " + banks[(size_t) b].name);
        if (b == engine.getCurrentLoopBank())
            continue;
        for (int i = 0; i < RigLooper::kLayers; ++i)
            if (const auto& l = banks[(size_t) b].layers[(size_t) i]; l.audio != nullptr)
                s.add ("  bank loop " + juce::String (i + 1) + " beats=" + juce::String (l.beats, 3), *l.audio);
    }
    return s;
}

juce::String differences (const Snapshot& a, const Snapshot& b)
{
    if (a.meta != b.meta)
        return "meta differs:\n" + a.meta.joinIntoString ("\n") + "\n--- vs ---\n" + b.meta.joinIntoString ("\n");
    for (size_t i = 0; i < a.audio.size(); ++i)
    {
        const auto& x = a.audio[i];
        const auto& y = b.audio[i];
        if (x.getNumSamples() != y.getNumSamples() || y.getNumChannels() < 2)
            return "audio " + juce::String ((int) i) + " size differs";
        for (int c = 0; c < 2; ++c)
            for (int n = 0; n < x.getNumSamples(); ++n)
                if (std::abs (x.getSample (juce::jmin (c, x.getNumChannels() - 1), n) - y.getSample (c, n)) > 1.0e-4f)
                    return "audio " + juce::String ((int) i) + " (" + a.meta[0] + "...) differs at " + juce::String (n);
    }
    return {};
}

juce::String readAll (const juce::File& f) { return f.loadFileAsString(); }

ino_t inodeOf (const juce::File& f)
{
    struct stat st {};
    return ::stat (f.getFullPathName().toRawUTF8(), &st) == 0 ? st.st_ino : 0;
}

juce::StringArray siblingsOf (const juce::File& show)
{
    juce::StringArray names;
    for (const auto& f : show.getParentDirectory().findChildFiles (juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, false, "*"))
        names.add (f.getFileName());
    for (const auto& f : show.getParentDirectory().findChildFiles (juce::File::findFiles, false, ".*"))
        names.add (f.getFileName());
    names.sort (true);
    return names;
}

// Every file under a folder with its bytes, to show nothing in it changed.
juce::String contentsOf (const juce::File& folder)
{
    juce::StringArray lines;
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, "*"))
    {
        juce::MemoryBlock mb;
        f.loadFileAsData (mb);
        lines.add (f.getRelativePathFrom (folder) + " " + juce::String ((juce::int64) mb.getSize()) + " " + juce::String (mb.toBase64Encoding().hashCode64()));
    }
    lines.sort (true);
    return lines.joinIntoString ("\n");
}

// The change the save tests make: a new sound on loop 1 (long enough for several write steps),
// a new name on loop 3, and a new loop 7.
void change (RigEngine& engine)
{
    engine.getLooper().setLayerAudio (0, tone (150000, 300.0, 0.6f), 4.0);
    engine.setLoopName (2, "Renamed pad");
    engine.getLooper().setLayerAudio (6, tone (8000, 900.0, 0.2f), 4.0);
}

struct HookScope
{
    explicit HookScope (std::function<bool (const juce::String&, int)> f) { rigshow::hooks().at = std::move (f); }
    ~HookScope() { rigshow::hooks().at = nullptr; }
};

// The plugin's loops folder for a state's song (its "In Logic" folder).
juce::String songOf (const juce::MemoryBlock& state)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
        return xml->getStringAttribute ("logicSong");
    return {};
}
juce::String revisionOf (const juce::MemoryBlock& state)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize()))
        return xml->getStringAttribute (rigshow::kRevisionAttribute);
    return {};
}
juce::File inLogic (const juce::String& song)
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig")
        .getChildFile ("In Logic").getChildFile (song + ".crateshow");
}
void removeSongFolders (const juce::String& song)
{
    if (song.isEmpty())
        return;
    const auto show = inLogic (song);
    for (const auto& f : show.getParentDirectory().findChildFiles (juce::File::findDirectories, false, "*"))
        if (f.getFileName().startsWith (show.getFileNameWithoutExtension() + " loops"))
            f.deleteRecursively();
}
} // namespace

struct RigShowFilesTests : juce::UnitTest
{
    RigShowFilesTests() : juce::UnitTest ("Rig show files", "Rig") {}

    void runTest() override
    {
        const auto root = newTempFolder ("main");

        //======================================================================
        beginTest ("A. A show saved before this change loads, with its loops, banks, audio takes and note takes, and saves back the same");
        Snapshot legacy;
        juce::File saved;
        {
            const auto show = copyOfFixture (root.getChildFile ("A"));
            auto engineOwner = std::make_unique<RigEngine>();
            auto& engine = *engineOwner;
            engine.prepare (kRate, 256);
            expect (load (engine, show));
            expectEquals (engine.getLoopAudioProblem(), juce::String());
            auto& looper = engine.getLooper();
            expect (looper.getState (0) != RigLooper::State::empty, "loop 1");
            expect (looper.getState (2) != RigLooper::State::empty, "loop 3");
            expectWithinAbsoluteError (looper.getLayer (2).lengthBeats.load(), 8.0, 1.0e-9);
            expectEquals (engine.getLoopName (2), juce::String ("Chorus pad"));
            expectEquals (engine.getNumLoopBanks(), 2);
            expectEquals (engine.getCurrentLoopBank(), 1);
            expectEquals (engine.getLoopBankName (0), juce::String ("Verse"));
            expectEquals (engine.getLoopBankName (1), juce::String ("Chorus"));
            const auto* audioTakes = engine.getTakes().setFor (4);
            expect (audioTakes != nullptr && audioTakes->takes.size() == 2 && audioTakes->playing == 1, "loop 5's two takes, take 2 on");
            const auto* noteTakes = engine.getTakes().setFor (RigTakes::notesLayer (RigEngine::bassStrip));
            expect (noteTakes != nullptr && noteTakes->takes.size() == 1 && noteTakes->takes[0].notes.size() == 4
                        && noteTakes->takes[0].audio.getNumSamples() == 8000,
                    "the Bass notes take, notes and sound");
            legacy = snapshot (engine);

            // Its sound is the fixture's WAVs.
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (fixtureFolder().getChildFile ("Legacy show loops/Bank 1/Loop 2.wav")));
            expect (reader != nullptr);
            juce::AudioBuffer<float> bankLoop (2, (int) reader->lengthInSamples);
            reader->read (&bankLoop, 0, bankLoop.getNumSamples(), 0, true, true);
            const auto& verse = engine.getLoopBanks()[0].layers[1].audio;
            expect (verse != nullptr && rigshow::hashAudio (*verse) == rigshow::hashAudio (bankLoop), "the Verse bank's loop 2 is the file's sound");

            // Saved in the new way (in place, over the legacy show), it loads the same.
            const auto result = engine.saveShowFile (show);
            expect (result.ok, result.error);
            expect (result.revision.isNotEmpty());
            expect (rigshow::holdsRevision (rigshow::currentFolder (show), result.revision), "the new revision is \"<show> loops\"");
            expectEquals (result.written, 0, "the legacy files were linked, not written again");
            saved = show;
            auto againOwner = std::make_unique<RigEngine>();
            auto& again = *againOwner;
            again.prepare (kRate, 256);
            expect (load (again, show));
            expectEquals (again.getLoopAudioProblem(), juce::String());
            expectEquals (differences (legacy, snapshot (again)), juce::String());
            // The legacy folder is kept beside it (nothing deleted).
            bool legacyKept = false;
            for (const auto& f : show.getParentDirectory().findChildFiles (juce::File::findDirectories, false, "Legacy show loops.r-legacy-*"))
                legacyKept = legacyKept || f.getChildFile ("Loop 1.wav").existsAsFile();
            expect (legacyKept, "the old folder is kept");
        }

        beginTest ("A. A completed new save reads in an older build (which knows only \"<show> loops\" and ignores the revision)");
        {
            auto xml = juce::parseXML (saved);
            expect (xml != nullptr && xml->hasAttribute (rigshow::kRevisionAttribute));
            xml->removeAttribute (rigshow::kRevisionAttribute);   // what an older build sees
            auto oldOwner = std::make_unique<RigEngine>();
            auto& old = *oldOwner;
            old.prepare (kRate, 256);
            old.loadShow (juce::ValueTree::fromXml (*xml));
            old.readLoopAudio (saved);
            expectEquals (differences (legacy, snapshot (old)), juce::String());
        }

        //======================================================================
        beginTest ("B. Unchanged audio isn't written again");
        {
            auto engineOwner = std::make_unique<RigEngine>();
            auto& engine = *engineOwner;
            engine.prepare (kRate, 256);
            expect (load (engine, saved));
            const auto loop1 = rigshow::currentFolder (saved).getChildFile ("Loop 1.wav");
            const auto inode = inodeOf (loop1);
            const auto modified = loop1.getLastModificationTime();
            const auto before = siblingsOf (saved);
            const auto xmlBefore = readAll (saved);

            auto r = engine.saveShowFile (saved);
            expect (r.ok && r.unchanged, "nothing changed after a reload");
            expectEquals (r.written + r.linked, 0);
            expect (inodeOf (loop1) == inode && loop1.getLastModificationTime() == modified, "the file is the same file");
            expect (siblingsOf (saved) == before, "no new folders");

            r = engine.saveShowFile (saved);
            expect (r.ok && r.unchanged, "or after a save");
            juce::ignoreUnused (xmlBefore);

            // One loop changes: only it is written; the rest are links to the same files.
            const auto oldRevision = r.revision;
            const auto oldLoop3 = rigshow::currentFolder (saved).getChildFile ("Loop 3.wav");
            const auto loop3Contents = contentsOf (rigshow::currentFolder (saved));
            engine.getLooper().setLayerAudio (2, tone (16000, 700.0, 0.3f), 8.0);
            r = engine.saveShowFile (saved);
            expect (r.ok, r.error);
            expectEquals (r.written, 1, "just loop 3");
            expectGreaterThan (r.linked, 5);
            expect (inodeOf (rigshow::currentFolder (saved).getChildFile ("Loop 1.wav")) == inode, "loop 1 is a link to the saved file");
            const auto oldFolder = rigshow::revisionFolder (saved, oldRevision);
            expect (rigshow::holdsRevision (oldFolder, oldRevision), "the previous revision is kept, whole");
            expectEquals (contentsOf (oldFolder), loop3Contents, "and not one byte of it changed");
        }

        //======================================================================
        beginTest ("C. A WAV that fails part way leaves the last saved show as it was");
        expectFailedSaveKeepsShow (root, "C", [] (const juce::String& point, int n) { return ! (point == "wavChunk" && n == 1); });

        beginTest ("D. A show file that can't be written leaves the last saved show as it was");
        expectFailedSaveKeepsShow (root, "D", [] (const juce::String& point, int) { return point != "commit"; });

        beginTest ("G. A full disk (the index or a WAV can't be written) fails the save and says so");
        expectFailedSaveKeepsShow (root, "G-index", [] (const juce::String& point, int) { return point != "index"; });
        expectFailedSaveKeepsShow (root, "G-wav", [] (const juce::String& point, int) { return point != "wav"; });
        expectFailedSaveKeepsShow (root, "G-rename", [] (const juce::String& point, int) { return point != "stageRename"; });

        beginTest ("G. A folder that can't be written fails the save");
        {
            // A real error: the show's "folder" is a file.
            const auto blocker = root.getChildFile ("not a folder");
            blocker.replaceWithText ("x");
            auto engineOwner = std::make_unique<RigEngine>();
            auto& engine = *engineOwner;
            engine.prepare (kRate, 256);
            engine.getLooper().setLayerAudio (0, tone (8000, 440.0, 0.4f), 4.0);
            const auto r = engine.saveShowFile (blocker.getChildFile ("Show.crateshow"));
            expect (! r.ok && r.error.isNotEmpty(), "fails, with why");

            if (::geteuid() == 0)
                logMessage ("(running as root, which can write anywhere: the read-only folder check runs on the Mac CI only)");
            else
            {
                const auto show = copyOfFixture (root.getChildFile ("G-perm"));
                auto e2Owner = std::make_unique<RigEngine>();
                auto& e2 = *e2Owner;
                e2.prepare (kRate, 256);
                expect (load (e2, show));
                change (e2);
                const auto before = contentsOf (show.getParentDirectory());
                ::chmod (show.getParentDirectory().getFullPathName().toRawUTF8(), 0555);
                const auto r2 = e2.saveShowFile (show);
                ::chmod (show.getParentDirectory().getFullPathName().toRawUTF8(), 0755);
                expect (! r2.ok && r2.error.isNotEmpty(), "a read-only folder fails, with why");
                expectEquals (contentsOf (show.getParentDirectory()), before, "nothing changed");
            }
        }

        //======================================================================
        beginTest ("E. A save killed at any step leaves the old show or the new one, whole, never a mix");
        for (const bool fromLegacy : { true, false })
        {
            const juce::StringArray points { "stage", "wav", "wavChunk", "index", "stageRename", "beforeCommit", "commit",
                                             "afterCommit", "aside", "promote", "done" };
            for (const auto& point : points)
            {
                const bool committed = points.indexOf (point) >= points.indexOf ("afterCommit");
                const auto folder = root.getChildFile ("E-" + juce::String (fromLegacy ? "legacy-" : "new-") + point);
                const auto show = copyOfFixture (folder);
                if (! fromLegacy)
                {
                    auto eOwner = std::make_unique<RigEngine>();
                    auto& e = *eOwner;
                    e.prepare (kRate, 256);
                    load (e, show);
                    expect (e.saveShowFile (show).ok);
                }
                Snapshot before, after;
                {
                    auto eOwner = std::make_unique<RigEngine>();
                    auto& e = *eOwner;
                    e.prepare (kRate, 256);
                    load (e, show);
                    before = snapshot (e);
                    change (e);
                    after = snapshot (e);
                }
                const auto oldRevision = juce::parseXML (show)->getStringAttribute (rigshow::kRevisionAttribute);
                expect (fromLegacy == oldRevision.isEmpty());
                const int n = point == "wavChunk" ? 1 : 0;
                const auto out = runChild (show, point, n);
                expect (out.contains ("KILLED AT " + point), point + ": the copy was killed there:\n" + out.getLastCharacters (2000));

                auto loadedOwner = std::make_unique<RigEngine>();

                auto& loaded = *loadedOwner;
                loaded.prepare (kRate, 256);
                expect (load (loaded, show), point + ": the show file is there");
                expectEquals (loaded.getLoopAudioProblem(), juce::String(), point);
                const auto got = snapshot (loaded);
                expectEquals (differences (committed ? after : before, got), juce::String(),
                              juce::String (fromLegacy ? "from legacy, " : "from new, ") + "killed at " + point
                                  + (committed ? ": the new show" : ": the old show"));

                // The XML on disk names exactly the revision it should, and that resolves to a whole folder.
                expectRevisionOnDisk (show, point, fromLegacy, committed, oldRevision);

                // And the next save, after the kill, works and loads.
                change (loaded);
                const auto r = loaded.saveShowFile (show);
                expect (r.ok, point + ": saving again after the kill: " + r.error);
                auto againOwner = std::make_unique<RigEngine>();
                auto& again = *againOwner;
                again.prepare (kRate, 256);
                load (again, show);
                expectEquals (differences (after, snapshot (again)), juce::String(), point + ": saved again");
            }
        }

        //======================================================================
        beginTest ("E. A legacy folder's promotion stopped half way (old folder moved to .r-legacy-*, new one not yet renamed) loads the new show, and the next save finishes it");
        {
            const auto show = copyOfFixture (root.getChildFile ("E-legacy-promotion"));
            Snapshot after;
            {
                auto eOwner = std::make_unique<RigEngine>();
                auto& e = *eOwner;
                e.prepare (kRate, 256);
                load (e, show);
                change (e);
                after = snapshot (e);
            }
            const auto out = runChild (show, "promote", 0);
            expect (out.contains ("KILLED AT promote"), out.getLastCharacters (2000));
            expectRevisionOnDisk (show, "promote", true, true, {});
            const auto revision = juce::parseXML (show)->getStringAttribute (rigshow::kRevisionAttribute);
            expect (! rigshow::currentFolder (show).exists(), "no \"<show> loops\" (the stop was between the two renames)");

            auto loadedOwner = std::make_unique<RigEngine>();
            auto& loaded = *loadedOwner;
            loaded.prepare (kRate, 256);
            expect (load (loaded, show));
            expectEquals (loaded.getLoopAudioProblem(), juce::String());
            expectEquals (differences (after, snapshot (loaded)), juce::String(), "the new show, from .r-<id>");

            // Saving again with nothing changed names the same revision and finishes the promotion.
            const auto r = loaded.saveShowFile (show);
            expect (r.ok && r.unchanged && r.revision == revision, "the same revision: " + r.error);
            expect (rigshow::holdsRevision (rigshow::currentFolder (show), revision), "now \"<show> loops\" holds it, for older builds too");
            juce::Array<juce::File> legacyAside;
            for (const auto& f : show.getParentDirectory().findChildFiles (juce::File::findDirectories, false, "Legacy show loops.r-legacy-*"))
                legacyAside.add (f);
            expectEquals (legacyAside.size(), 1);
            if (legacyAside.size() == 1)
                expectEquals (contentsOf (legacyAside[0]), contentsOf (fixtureFolder().getChildFile ("Legacy show loops")), "the legacy folder kept whole");
        }

        //======================================================================
        beginTest ("Last show: a save that fails on quit says why, keeps the Last show from before, and tells the next launch");
        {
            const auto folder = root.getChildFile ("Last show");
            const auto show = copyOfFixture (folder);
            juce::PropertiesFile::Options options;
            options.applicationName = "Last show test";
            options.filenameSuffix = ".settings";
            juce::PropertiesFile settings (folder.getChildFile ("test.settings"), options);
            Snapshot before;
            {
                auto eOwner = std::make_unique<RigEngine>();
                auto& e = *eOwner;
                e.prepare (kRate, 256);
                load (e, show);
                expect (riglastshow::save (e, show, settings).ok, "a good save");
                expect (riglastshow::takeFailureFromLastQuit (settings).isEmpty(), "says nothing");
                before = snapshot (e);
            }
            const auto xmlBefore = readAll (show);
            auto eOwner = std::make_unique<RigEngine>();
            auto& e = *eOwner;
            e.prepare (kRate, 256);
            load (e, show);
            change (e);
            rigshow::Result r;
            {
                HookScope fail ([] (const juce::String& point, int) { return point != "wav"; });
                r = riglastshow::save (e, show, settings);
            }
            expect (! r.ok, "the save says it failed");
            const auto onQuit = riglastshow::quitMessage (r);
            expect (onQuit.contains (r.error) && onQuit.contains ("Last show from before is still there"), onQuit);
            expectEquals (readAll (show), xmlBefore, "the Last show file is as it was");

            // The settings file (as the next launch reads it) has why.
            juce::PropertiesFile nextLaunch (folder.getChildFile ("test.settings"), options);
            const auto atLaunch = riglastshow::takeFailureFromLastQuit (nextLaunch);
            expect (atLaunch.contains ("wasn't saved") && atLaunch.contains (r.error), atLaunch);
            expect (riglastshow::takeFailureFromLastQuit (nextLaunch).isEmpty(), "said once");
            auto againOwner = std::make_unique<RigEngine>();
            auto& again = *againOwner;
            again.prepare (kRate, 256);
            expect (load (again, show));
            expectEquals (differences (before, snapshot (again)), juce::String(), "and it opens as the Last show from before");

            // A later good save clears it.
            expect (riglastshow::save (e, show, settings).ok);
            expect (riglastshow::takeFailureFromLastQuit (settings).isEmpty());
        }

        //======================================================================
        beginTest ("F. Bank switching and takes from a legacy show, a moved show, and a revision that can't be found");
        {
            const auto show = copyOfFixture (root.getChildFile ("F"));
            auto engineOwner = std::make_unique<RigEngine>();
            auto& engine = *engineOwner;
            engine.prepare (kRate, 256);
            expect (load (engine, show));
            const auto verse1 = engine.getLoopBanks()[0].layers[0].audio;
            expect (verse1 != nullptr);
            engine.switchLoopBank (0);
            expectEquals (engine.getCurrentLoopBank(), 0);
            const auto onLayer = engine.getLooper().copyLayerAudio (0);
            expect (verse1 != nullptr && onLayer.getNumSamples() == verse1->getNumSamples()
                        && rigshow::hashAudio (onLayer) == rigshow::hashAudio (*verse1),
                    "the Verse bank's loop 1 is on the looper");
            const auto take1 = engine.getTakes().setFor (4)->takes[0].audio;
            expect (engine.useTake (4, 0));
            expect (rigshow::hashAudio (engine.getLooper().copyLayerAudio (4)) == rigshow::hashAudio (take1), "take 1 is on loop 5");

            // Saved, then the show and its loops folder moved to another folder: it loads.
            expect (engine.saveShowFile (show).ok);
            const auto expected = snapshot (engine);
            const auto moved = root.getChildFile ("F moved");
            moved.createDirectory();
            expect (show.moveFileTo (moved.getChildFile (show.getFileName())));
            expect (rigshow::currentFolder (show).moveFileTo (moved.getChildFile (rigshow::currentFolder (show).getFileName())));
            auto thereOwner = std::make_unique<RigEngine>();
            auto& there = *thereOwner;
            there.prepare (kRate, 256);
            expect (load (there, moved.getChildFile (show.getFileName())));
            expectEquals (there.getLoopAudioProblem(), juce::String());
            const auto got = snapshot (there);
            expectEquals (differences (expected, got), juce::String(), "the moved show");

            // A show naming a revision that isn't there loads no loop sound (never another's), and says why.
            auto xml = juce::parseXML (moved.getChildFile (show.getFileName()));
            xml->setAttribute (rigshow::kRevisionAttribute, "0123456789abcdef");
            const auto wrong = moved.getChildFile ("Legacy show.crateshow");
            expect (xml->writeTo (wrong));
            auto lostOwner = std::make_unique<RigEngine>();
            auto& lost = *lostOwner;
            lost.prepare (kRate, 256);
            expect (load (lost, wrong));
            expect (lost.getLoopAudioProblem().isNotEmpty(), "it says the loops weren't found");
            for (int i = 0; i < RigLooper::kLayers; ++i)
                expect (lost.getLooper().copyLayerAudio (i).getNumSamples() == 0, "no loop sound");
        }

        //======================================================================
        beginTest ("Plugin: the first save failing gives Logic a state marked unsaved, that loads no other sound, with a clear warning");
        {
            juce::String song;
            {
                auto pluginOwner = std::make_unique<RigPlugin>();
                auto& plugin = *pluginOwner;
                plugin.getEngine().getLooper().setLayerAudio (0, tone (8000, 440.0, 0.4f), 4.0);
                juce::MemoryBlock state;
                {
                    HookScope fail ([] (const juce::String& point, int) { return point != "wav"; });
                    plugin.getStateInformation (state);
                }
                song = songOf (state);
                expect (song.isNotEmpty());
                expectEquals (revisionOf (state), juce::String (rigshow::kUnsavedRevision), "the state is marked: its loops weren't saved");
                bool error = false;
                const auto warning = plugin.getEngine().takeShowFilesMessage (error);
                expect (error && warning.contains ("couldn't save its loops") && warning.contains ("no loops"), warning);
                expect (! rigshow::currentFolder (inLogic (song)).exists(), "nothing saved");

                // Sound that happens to be in this song's folder (any other loops) isn't picked up.
                const auto stray = rigshow::currentFolder (inLogic (song));
                stray.createDirectory();
                {
                    juce::WavAudioFormat wav;
                    std::unique_ptr<juce::OutputStream> stream (stray.getChildFile ("Loop 1.wav").createOutputStream().release());
                    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (kRate).withNumChannels (2).withBitsPerSample (24));
                    const auto other = tone (8000, 123.0, 0.9f);
                    expect (writer != nullptr && writer->writeFromAudioSampleBuffer (other, 0, other.getNumSamples()));
                }
                auto againOwner = std::make_unique<RigPlugin>();
                auto& again = *againOwner;
                again.setStateInformation (state.getData(), (int) state.getSize());
                expect (again.getEngine().getLooper().copyLayerAudio (0).getNumSamples() == 0, "no unrelated sound");
                expect (again.getEngine().getLoopAudioProblem().contains ("weren't saved"), again.getEngine().getLoopAudioProblem());
                const auto shown = again.getEngine().takeShowFilesMessage (error);
                expect (shown.contains ("weren't saved"), "and it says so when it opens");
                stray.deleteRecursively();
            }
            removeSongFolders (song);
        }

        beginTest ("Plugin: a later save failing gives Logic the last whole state (its show and its sound), not this show with old sound");
        {
            juce::String song;
            {
                auto pluginOwner = std::make_unique<RigPlugin>();
                auto& plugin = *pluginOwner;
                auto& engine = plugin.getEngine();
                engine.getLooper().setLayerAudio (0, tone (8000, 440.0, 0.4f), 4.0);
                engine.setLoopName (0, "First");
                juce::MemoryBlock good;
                plugin.getStateInformation (good);
                song = songOf (good);
                const auto goodRevision = revisionOf (good);
                expect (goodRevision.isNotEmpty() && goodRevision != rigshow::kUnsavedRevision);
                bool error = false;
                expect (engine.takeShowFilesMessage (error).isEmpty(), "a good save says nothing");

                engine.getLooper().setLayerAudio (0, tone (8000, 220.0, 0.4f), 4.0);
                engine.setLoopName (0, "Second");
                juce::MemoryBlock failed;
                {
                    HookScope fail ([] (const juce::String& point, int) { return point != "wav"; });
                    plugin.getStateInformation (failed);
                }
                expect (failed == good, "the host gets the last whole state, byte for byte");
                const auto warning = engine.takeShowFilesMessage (error);
                expect (error && warning.contains ("couldn't save its loops") && warning.contains ("last saved state"), warning);

                auto againOwner = std::make_unique<RigPlugin>();

                auto& again = *againOwner;
                again.setStateInformation (failed.getData(), (int) failed.getSize());
                expectEquals (again.getEngine().getLoopName (0), juce::String ("First"));
                const auto loaded = again.getEngine().getLooper().copyLayerAudio (0);
                expect (loaded.getNumSamples() == 8000 && std::abs (loaded.getSample (0, 1) - tone (8000, 440.0, 0.4f).getSample (0, 1)) < 1.0e-4f,
                        "with the first sound");

                // And once the disk is fine again, the next save is whole and new.
                juce::MemoryBlock next;
                plugin.getStateInformation (next);
                expect (revisionOf (next) != goodRevision && revisionOf (next) != rigshow::kUnsavedRevision);
                auto thirdOwner = std::make_unique<RigPlugin>();
                auto& third = *thirdOwner;
                third.setStateInformation (next.getData(), (int) next.getSize());
                expectEquals (third.getEngine().getLoopName (0), juce::String ("Second"));

                // The first state still loads its own sound (nothing was deleted).
                auto olderOwner = std::make_unique<RigPlugin>();
                auto& older = *olderOwner;
                older.setStateInformation (good.getData(), (int) good.getSize());
                expectEquals (older.getEngine().getLoopAudioProblem(), juce::String());
                expect (older.getEngine().getLooper().copyLayerAudio (0).getNumSamples() == 8000);
            }
            removeSongFolders (song);
        }

        root.deleteRecursively();
    }

    // After a save was killed at `point`: which revision the show file names, where its folder is,
    // and (from a legacy show) where the older build's folder went.
    void expectRevisionOnDisk (const juce::File& show, const juce::String& point, bool fromLegacy, bool committed, const juce::String& oldRevision)
    {
        const auto where = juce::String (fromLegacy ? "from legacy, " : "from new, ") + "killed at " + point + ": ";
        const auto revision = juce::parseXML (show)->getStringAttribute (rigshow::kRevisionAttribute);
        const auto current = rigshow::currentFolder (show);
        juce::Array<juce::File> legacyAside;
        for (const auto& f : show.getParentDirectory().findChildFiles (juce::File::findDirectories, false, "*"))
            if (f.getFileName().startsWith (current.getFileName() + ".r-legacy-"))
                legacyAside.add (f);
        juce::String problem;
        const auto resolved = rigshow::resolve (show, revision, &problem);
        expect (resolved != juce::File(), where + "the show's revision resolves: " + problem);

        if (! committed)
        {
            expectEquals (revision, oldRevision, where + "the show file still names the old revision");
            expect (resolved == current, where + "in \"<show> loops\", untouched");
            // (From a new show, the first save already moved the legacy folder to .r-legacy-*.)
            expectEquals (legacyAside.size(), fromLegacy ? 0 : 1, where + "the legacy folder wasn't moved");
            return;
        }
        expect (revision.isNotEmpty() && revision != oldRevision, where + "the show file names the new revision");
        expect (rigshow::holdsRevision (resolved, revision), where + "and its folder is whole");
        const bool promoted = point == "done";
        expect (resolved == (promoted ? current : rigshow::revisionFolder (show, revision)),
                where + "found at " + resolved.getFileName());
        if (fromLegacy)
        {
            // "aside" stopped before the legacy folder moved; "promote" after it moved, before the new one took its name.
            const bool moved = point == "promote" || point == "done";
            expectEquals (legacyAside.size(), moved ? 1 : 0, where + "the legacy folder went to .r-legacy-*");
            expect (current.exists() == (point != "promote"), where + (point == "promote" ? "no \"<show> loops\" yet" : "\"<show> loops\" is there"));
            if (moved && legacyAside.size() == 1)
                expectEquals (contentsOf (legacyAside[0]), contentsOf (fixtureFolder().getChildFile ("Legacy show loops")),
                              where + "the legacy folder, every file as it was");
            if (! moved)
                expect (! current.getChildFile (rigshow::kIndexName).exists(), where + "\"<show> loops\" is still the legacy one");
        }
        else if (point != "done")
        {
            expect (rigshow::holdsRevision (point == "promote" ? rigshow::revisionFolder (show, oldRevision) : current, oldRevision),
                    where + "the old revision is still whole");
        }

        // What an older build (which reads only "<show> loops") would see here, for the report.
        auto oldBuild = std::make_unique<RigEngine>();
        oldBuild->prepare (kRate, 256);
        auto xml = juce::parseXML (show);
        xml->removeAttribute (rigshow::kRevisionAttribute);
        oldBuild->loadShow (juce::ValueTree::fromXml (*xml));
        oldBuild->readLoopAudio (show);
        int loops = 0;
        for (int i = 0; i < RigLooper::kLayers; ++i)
            loops += oldBuild->getLooper().copyLayerAudio (i).getNumSamples() > 0 ? 1 : 0;
        logMessage ("  " + where + "an older build would load " + juce::String (loops) + " loops from \"<show> loops\""
                    + (promoted ? " (the new revision)" : current.exists() ? " (the previous revision's sound)" : " (folder not there yet)"));
    }

    // A save made to fail at one step: false with why, and the show on disk is exactly as it was
    // (its file, its folder, nothing left beside it), and loads as before.
    void expectFailedSaveKeepsShow (const juce::File& root, const juce::String& label, std::function<bool (const juce::String&, int)> hook)
    {
        const auto show = copyOfFixture (root.getChildFile (label));
        {
            auto eOwner = std::make_unique<RigEngine>();
            auto& e = *eOwner;
            e.prepare (kRate, 256);
            load (e, show);
            expect (e.saveShowFile (show).ok, label + ": saved once");
        }
        auto engineOwner = std::make_unique<RigEngine>();
        auto& engine = *engineOwner;
        engine.prepare (kRate, 256);
        expect (load (engine, show));
        const auto before = snapshot (engine);
        const auto xmlBefore = readAll (show);
        const auto folderBefore = contentsOf (show.getParentDirectory());
        const auto namesBefore = siblingsOf (show);
        change (engine);
        rigshow::Result r;
        {
            HookScope fail (std::move (hook));
            r = engine.saveShowFile (show);
        }
        expect (! r.ok, label + ": the save says it failed");
        expect (r.error.isNotEmpty(), label + ": and why");
        expectEquals (readAll (show), xmlBefore, label + ": the show file is as it was");
        expectEquals (contentsOf (show.getParentDirectory()), folderBefore, label + ": every file is as it was");
        expect (siblingsOf (show) == namesBefore, label + ": nothing left beside it\n" + siblingsOf (show).joinIntoString ("\n"));
        auto againOwner = std::make_unique<RigEngine>();
        auto& again = *againOwner;
        again.prepare (kRate, 256);
        expect (load (again, show));
        expectEquals (differences (before, snapshot (again)), juce::String(), label + ": it loads as before");

        // The same save, once the disk is fine, works.
        const auto ok = engine.saveShowFile (show);
        expect (ok.ok, label + ": saving again works: " + ok.error);
    }

    // Runs the save in a copy of this program that is killed (std::_Exit, no clean up) at a step.
    juce::String runChild (const juce::File& show, const juce::String& point, int n)
    {
        setenv ("KB_SHOWFILES_CHILD", show.getFullPathName().toRawUTF8(), 1);
        setenv ("KB_SHOWFILES_POINT", point.toRawUTF8(), 1);
        setenv ("KB_SHOWFILES_N", juce::String (n).toRawUTF8(), 1);
        juce::ChildProcess child;
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        const bool started = child.start (juce::StringArray { exe, kChildTest });
        unsetenv ("KB_SHOWFILES_CHILD");
        unsetenv ("KB_SHOWFILES_POINT");
        unsetenv ("KB_SHOWFILES_N");
        expect (started, "the copy starts");
        if (! started)
            return {};
        juce::String out;
        std::atomic<bool> reading { true };
        std::thread reader ([&] { out = child.readAllProcessOutput(); reading = false; });
        const auto end = juce::Time::getMillisecondCounter() + 120000;
        while (reading && juce::Time::getMillisecondCounter() < end)
            juce::Thread::sleep (20);
        if (reading)
            child.kill();
        reader.join();
        child.waitForProcessToFinish (5000);
        return out;
    }
};

// The copy: loads the show, makes the change, and saves, dying at the step it was given.
struct RigShowFilesKilledSave : juce::UnitTest
{
    RigShowFilesKilledSave() : juce::UnitTest (kChildTest, "Rig") {}

    void runTest() override
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("KB_SHOWFILES_CHILD", {});
        if (path.isEmpty())
            return;   // only in the copy the test above starts
        beginTest ("killed save");
        const juce::File show (path);
        const auto point = juce::SystemStats::getEnvironmentVariable ("KB_SHOWFILES_POINT", {});
        const int n = juce::SystemStats::getEnvironmentVariable ("KB_SHOWFILES_N", "0").getIntValue();
        auto engineOwner = std::make_unique<RigEngine>();
        auto& engine = *engineOwner;
        engine.prepare (kRate, 256);
        expect (load (engine, show));
        change (engine);
        rigshow::hooks().at = [point, n] (const juce::String& p, int k)
        {
            if (p == point && k == n)
            {
                std::cout << "KILLED AT " << p << std::endl;
                std::_Exit (0);
            }
            return true;
        };
        const auto r = engine.saveShowFile (show);
        rigshow::hooks().at = nullptr;
        std::cout << "NOT KILLED (" << point << "): " << (r.ok ? "saved" : r.error) << std::endl;
    }
};

static RigShowFilesTests rigShowFilesTests;
static RigShowFilesKilledSave rigShowFilesKilledSave;
