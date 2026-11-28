// P2 CP4, headless: quitting the standalone app with its dirty model on (Save / Don't Save /
// Cancel through RigDocFlow), the launch record and what the next launch opens (riglaunch), the
// unsaved dot after the show's name, and the plugins (none of it there). Each "run" is what the
// app's Main.cpp wires: a settings file, an engine with the model on, the Tracker, riglaunch::launch,
// a standalone RigView with a fake Asker, and RigView::requestQuit; a relaunch is a new run on the
// same settings file and the same show files.

#include "RigDirtyTestHelpers.h"
#include "RigAsker.h"
#include "RigDocFlow.h"
#include "RigLaunch.h"
#include "RigLastShow.h"
#include "RigPlugin.h"
#include "PluginSlot.h"
#include "RigPluginFakes.h"
#include "IdeaTempoSounds.h"

#include <deque>

using namespace dirtytest;

namespace
{
using DS = RigDirty::State;
using rigdirty::Choice;
using Dot = RigView::DocDot;
using pluginfakes::FakeFormat;
using pluginfakes::FakeSound;
using pluginfakes::pump;
using pluginfakes::pumpUntil;

// Answers what's queued straight away; otherwise holds the answer until the test gives it.
struct FakeAsker final : rigdirty::Asker
{
    enum class Type { ask, saveFile, openFile, folder, error };
    struct Call
    {
        Type type;
        rigdirty::Question question;
        juce::File file;
        juce::String text;
    };
    std::vector<Call> calls;
    std::deque<Choice> choices;
    std::deque<juce::File> files;
    std::function<void()> before;
    std::function<void (Choice)> heldAsk;
    std::function<void (const juce::File&)> heldFile;

    void ask (const rigdirty::Question& q, std::function<void (Choice)> done) override
    {
        calls.push_back ({ Type::ask, q, {}, {} });
        if (! choices.empty())
        {
            const auto c = choices.front();
            choices.pop_front();
            if (before) before();
            done (c);
            return;
        }
        heldAsk = std::move (done);
    }
    void file (Type t, const juce::File& f, std::function<void (const juce::File&)> done)
    {
        calls.push_back ({ t, {}, f, {} });
        if (! files.empty())
        {
            const auto picked = files.front();
            files.pop_front();
            if (before) before();
            done (picked);
            return;
        }
        heldFile = std::move (done);
    }
    void chooseSaveFile (const juce::File& s, std::function<void (const juce::File&)> d) override { file (Type::saveFile, s, std::move (d)); }
    void chooseOpenFile (const juce::File& f, std::function<void (const juce::File&)> d) override { file (Type::openFile, f, std::move (d)); }
    void chooseFolder (const juce::String&, const juce::File& f, std::function<void (const juce::File&)> d) override { file (Type::folder, f, std::move (d)); }
    void showError (const juce::String& title, const juce::String& message) override { calls.push_back ({ Type::error, {}, {}, title + ": " + message }); }

    int count (Type t) const { return (int) std::count_if (calls.begin(), calls.end(), [t] (const Call& c) { return c.type == t; }); }
    int questions() const { return count (Type::ask); }
    const Call* last (Type t) const
    {
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->type == t)
                return &*it;
        return nullptr;
    }
    const rigdirty::Question& lastQuestion() const
    {
        static rigdirty::Question none;
        const auto* c = last (Type::ask);
        return c != nullptr ? c->question : none;
    }
    bool answer (Choice c)
    {
        if (! heldAsk) return false;
        auto cb = std::move (heldAsk);
        heldAsk = nullptr;
        cb (c);
        return true;
    }
    bool pick (const juce::File& f)
    {
        if (! heldFile) return false;
        auto cb = std::move (heldFile);
        heldFile = nullptr;
        cb (f);
        return true;
    }
};

juce::String xmlOf (RigEngine& e)
{
    auto t = e.saveShow();
    t.removeChild (t.getChildWithName ("Screens"), nullptr);
    auto xml = t.createXml();
    return xml != nullptr ? xml->toString (juce::XmlElement::TextFormat().singleLine()) : juce::String();
}

juce::String audioOf (RigEngine& e)
{
    juce::String s;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        if (e.getLooper().getState (i) != RigLooper::State::empty)
            s << i << ":" << juce::String::toHexString ((juce::int64) rigshow::hashAudio (e.getLooper().soundingLoopForTests (i))) << " ";
    return s;
}

double bpmIn (const juce::File& show)
{
    auto xml = juce::parseXML (show);
    return xml != nullptr ? xml->getDoubleAttribute ("bpm", -1) : -1;
}

void tick (RigView& v) { (v.*get (TimerTag {})) (); }

// One run of the app (as Main.cpp wires it), the dirty model on unless `model` is false.
struct Run
{
    std::unique_ptr<juce::PropertiesFile> settings;
    std::unique_ptr<RigEngine> e;
    std::unique_ptr<riglaunch::Tracker> tracker;
    riglaunch::Launch launched;
    FakeAsker asker;
    std::unique_ptr<RigView> v;
    int quits = 0;

    bool requestQuit() { return v->requestQuit ([this] { ++quits; }); }
    juce::File current() const { return v->getCurrentShow(); }
    RigDocFlow& flow() { return v->getDocFlow(); }
    // The app's shutdown after it quit (the settings saved once more, then everything goes).
    void shutdown()
    {
        settings->saveIfNeeded();
        v.reset();
        e.reset();
    }
};

std::unique_ptr<juce::PropertiesFile> settingsAt (const juce::File& f)
{
    juce::PropertiesFile::Options o;
    o.millisecondsBeforeSaving = -1;
    return std::make_unique<juce::PropertiesFile> (f, o);
}

std::unique_ptr<Run> startRun (const juce::File& settingsFile, bool model = true)
{
    auto r = std::make_unique<Run>();
    r->settings = settingsAt (settingsFile);
    r->e = std::make_unique<RigEngine>();
    r->e->prepare (kRate, kBlock);
    if (model)
        r->e->getDirty().setEnabled (true);
    if (riglaunch::active (*r->e))
    {
        r->tracker = std::make_unique<riglaunch::Tracker> (*r->settings);
        r->launched = riglaunch::launch (*r->e, *r->tracker);
        if (r->launched.opened)
            RigView::launchShow = r->launched.document;
    }
    r->v = std::make_unique<RigView> (*r->e, nullptr);
    r->v->setSize (1440, 960);
    r->v->setLaunchTracker (r->tracker.get());
    r->v->setAskerForTests (&r->asker);
    return r;
}

struct Reset
{
    ~Reset()
    {
        RigDocFlow::verifyTimeoutMs = 2000;
        RigDocFlow::maxRounds = 4;
        RigView::standaloneDocuments = false;
        RigView::launchShow = juce::File();
        riglaunch::Tracker::failWritesForTests = false;
        riglastshow::fileForTests() = juce::File();
        rigshow::hooks().at = nullptr;
    }
};
} // namespace

//==============================================================================
struct RigQuitFlowTests final : juce::UnitTest
{
    RigQuitFlowTests() : juce::UnitTest ("Rig quit and relaunch", "Rig") {}

    juce::File folder;
    int fileCount = 0;
    juce::File newFile (const juce::String& label) { return folder.getChildFile (juce::String (++fileCount) + " " + label + ".crateshow"); }
    juce::File newSettings() { return folder.getChildFile ("Settings " + juce::String (++fileCount) + ".settings"); }

    static void fill (RigEngine& e, double bpm)
    {
        e.getTransport().setBpm (bpm);
        auto p = e.getBass().getPattern (0);
        p.steps[3].on = ! p.steps[3].on;
        e.getBass().setPattern (0, p);
        e.getLooper().setLayerAudio (0, noise ((int) kRate, 1), 2.0);
        e.saveLoopBank ("A");
        e.newLoopBank ("B");
        e.getLooper().setLayerAudio (0, noise ((int) kRate, 2), 2.0);
        e.switchLoopBank (0);
        e.serviceLoopBanks();
    }
    juce::File makeNamed (const juce::String& label, double bpm = 97.0)
    {
        RigEngine e;
        e.prepare (kRate, kBlock);
        fill (e, bpm);
        const auto f = newFile (label);
        expect (e.saveShowFile (f, rigshow::SaveKind::document).ok, "made " + label);
        return f;
    }
    // A settings file whose record says `show` was open when the app last quit.
    juce::File settingsOn (const juce::File& show)
    {
        const auto s = newSettings();
        auto p = settingsAt (s);
        riglaunch::Tracker t (*p);
        t.opened (show);
        expect (t.quitting (show), "(the record written)");
        return s;
    }
    // A run on `show`, opened by the launch (it was open at the last quit).
    std::unique_ptr<Run> runOn (const juce::File& show)
    {
        auto r = startRun (settingsOn (show));
        expect (r->launched.opened && r->current() == show, "(launched on " + show.getFileName() + ")");
        expect (settle (*r->e).state == DS::clean, "(" + show.getFileName() + " opens clean)");
        return r;
    }
    void playing (RigEngine& e)
    {
        if (! e.getTransport().isPlaying())
            e.getTransport().play();
        Runner r { e };
        r.seconds (0.05);
        expect (e.getTransport().isPlaying(), "(playing)");
    }
    // What a relaunch on the same settings brings up: `file` as it is on disk (named), or a new song.
    std::unique_ptr<Run> relaunch (Run& was, const juce::File& expected, const juce::String& what)
    {
        const auto settingsFile = was.settings->getFile();
        was.shutdown();
        auto r = startRun (settingsFile);
        if (expected == juce::File())
        {
            expect (! r->launched.opened, what + ": relaunched on a new song");
            expect (r->current() == juce::File(), what + ": untitled");
            expect (r->e->isBlank(), what + ": blank");
        }
        else
        {
            expect (r->launched.opened, what + ": relaunched on " + expected.getFileName());
            expect (r->current() == expected, what + ": named " + expected.getFileName() + " (is " + r->current().getFileName() + ")");
            auto ref = openSaved (expected);
            expect (xmlOf (*r->e) == xmlOf (*ref), what + ": exactly what's in the file");
            expect (audioOf (*r->e) == audioOf (*ref), what + ": its loops");
            expectWithinAbsoluteError (r->e->getTransport().getBpm(), bpmIn (expected), 0.0001, what + ": its tempo");
        }
        return r;
    }

    //==========================================================================
    enum class St { untitledBlank, untitledNonblank, namedClean, namedDirty, namedPending, numStates };
    static juce::String stName (St s)
    {
        static const char* n[] = { "blank untitled", "untitled", "named clean", "named dirty", "named, a bank switch queued (pending)" };
        return n[(int) s];
    }

    void runMatrix()
    {
        int runs = 0, asked = 0;
        for (int s = 0; s < (int) St::numStates; ++s)
        {
            const auto st = (St) s;
            const bool protect = st != St::untitledBlank && st != St::namedClean;
            for (int a = 0; a < (protect ? 3 : 1); ++a)
            {
                const auto ans = (Choice) a;
                const auto what = stName (st) + (protect ? juce::String (a == 0 ? " / Save" : a == 1 ? " / Don't Save" : " / Cancel") : juce::String());
                ++runs;
                const auto named = makeNamed ("Named " + juce::String (runs));
                const auto namedBytes = named.loadFileAsString();
                std::unique_ptr<Run> r;
                if (st == St::untitledBlank || st == St::untitledNonblank)
                {
                    r = startRun (newSettings());
                    expect (! r->launched.opened && r->e->isBlank(), what + ": (a new song at first launch)");
                    if (st == St::untitledNonblank)
                        fill (*r->e, 101.0);
                }
                else
                {
                    r = runOn (named);
                    if (st == St::namedDirty)
                        r->e->getTransport().setBpm (131.0);   // (as the phone sets it: nothing notes it)
                    if (st == St::namedPending)
                    {
                        playing (*r->e);
                        r->e->switchLoopBank (1);
                        expectEquals (r->e->getQueuedLoopBank(), 1, what + ": (a bank switch waits for its bar)");
                    }
                }
                playing (*r->e);
                const auto beforeXml = xmlOf (*r->e), beforeAudio = audioOf (*r->e);
                const auto beforeShow = r->current();
                const auto beforeBpm = r->e->getTransport().getBpm();
                const auto saveAs = newFile ("Saved as");

                expect (r->requestQuit(), what + ": the quit started");
                expectEquals (r->asker.questions(), protect ? 1 : 0, what + ": questions");
                if (! protect)
                {
                    expectEquals (r->quits, 1, what + ": quits, no question");
                    relaunch (*r, beforeShow, what);
                    continue;
                }
                ++asked;
                const auto& q = r->asker.lastQuestion();
                expectEquals (q.title, juce::String ("Quit Kaleidobox?"), what);
                expect (q.saveLabel == "Save" && q.dontSaveLabel == "Don't Save" && q.cancelLabel == "Cancel", what + ": Save / Don't Save / Cancel");
                expect (q.message.contains (beforeShow != juce::File() ? beforeShow.getFileNameWithoutExtension() : juce::String ("this song")), what + ": names the show");
                expectEquals (r->quits, 0, what + ": not before the answer");
                const bool untitled = beforeShow == juce::File();
                if (ans == Choice::save && untitled)
                    r->asker.files.push_back (saveAs);
                expect (r->asker.answer (ans), what + ": answered");
                if (ans == Choice::cancel)
                {
                    expectEquals (r->quits, 0, what + ": still open");
                    expect (r->current() == beforeShow && xmlOf (*r->e) == beforeXml && audioOf (*r->e) == beforeAudio, what + ": the show byte for byte");
                    expect (r->e->getTransport().isPlaying(), what + ": still playing");
                    expect (! r->tracker->record().cleanShutdown, what + ": no quit recorded");
                    expect (named.loadFileAsString() == namedBytes, what + ": its file untouched");
                    // (The app's still running; a kill now relaunches on the last saved show, or a new song.)
                    relaunch (*r, untitled ? juce::File() : named, what + " then killed");
                    continue;
                }
                if (ans == Choice::dontSave)
                {
                    expectEquals (r->quits, 1, what + ": quits");
                    expectEquals (r->flow().getStats().saves, 0, what + ": nothing saved");
                    expect (named.loadFileAsString() == namedBytes, what + ": the file as last saved");
                    auto again = relaunch (*r, untitled ? juce::File() : named, what);
                    if (st == St::namedDirty)
                        expectWithinAbsoluteError (again->e->getTransport().getBpm(), 97.0, 0.0001, what + ": the discarded tempo didn't come back");
                    continue;
                }
                // Save.
                expectEquals (r->flow().getStats().saves, 1, what + ": saved");
                expectEquals (r->asker.count (FakeAsker::Type::saveFile), untitled ? 1 : 0, what + ": Save As only when untitled");
                const auto savedTo = untitled ? saveAs : beforeShow;
                expectWithinAbsoluteError (bpmIn (savedTo), beforeBpm, 0.0001, what + ": saved as it was");
                if (st == St::namedPending)
                {
                    // Still can't be checked: asked again before it quits.
                    expectEquals (r->asker.questions(), 2, what + ": asked again");
                    expect (r->asker.lastQuestion().message.contains ("changed after it was saved"), what + ": " + r->asker.lastQuestion().message);
                    expectEquals (r->quits, 0, what + ": not yet");
                    expect (r->asker.answer (Choice::dontSave));
                }
                expectEquals (r->quits, 1, what + ": quits after the save");
                relaunch (*r, savedTo, what);
            }
        }
        logMessage ("  quit matrix: " + juce::String (runs) + " runs, " + juce::String (asked) + " asked, each relaunched");
    }

    //==========================================================================
    void runTest() override
    {
        Reset reset;
        folder = tempFolder ("RigQuitFlowTests");
        riglastshow::fileForTests() = folder.getChildFile ("Last show.crateshow");
        RigDocFlow::verifyTimeoutMs = 300;
        RigView::standaloneDocuments = true;

        beginTest ("Off by default: no quit question, no record, no dot");
        runModelOff();

        beginTest ("Quit x relaunch: every show x every answer, then the next launch");
        runMatrix();

        beginTest ("Quit: a save that fails, a cancelled Save As");
        runFailedSaves();

        beginTest ("Quit: edits while it asks, while Save As is up, between the save and the quit");
        runEditsDuring();

        beginTest ("Quit: recording, overdubbing, an undo just done");
        runRecording();

        beginTest ("Quit: one at a time, during another request, stale answers");
        runOneAtATime();

        beginTest ("Relaunch: missing, moved, changed elsewhere, unreadable, not a show, folder gone");
        runRelaunchProblems();

        beginTest ("Quit-time Save: its file missing, changed elsewhere, unreadable, folder gone");
        runQuitTimeSaveProblems();

        beginTest ("Interrupted: no quit, the record not written, killed between the save and the record");
        runInterrupted();

        beginTest ("The Last show: never written, read or opened as the show with the model on; off as on main");
        runLastShow();

        beginTest ("Don't Save discards: never back (twice), and a negative control");
        runDiscarded();

        beginTest ("Catch an idea's tempo (#131): the model sees it, the dot, the quit, a take kept");
        runCatchIdea();

        beginTest ("A show's plugin still loading, or missing, at the quit and at the relaunch");
        runPlugins();

        beginTest ("The dot: edits, saves, undo, playback, pending, its cost");
        runDot();

        beginTest ("Logic and the one-tool plugins: no quit, no dot, no record");
        runHosted();

        folder.deleteRecursively();
    }

    //==========================================================================
    void runModelOff()
    {
        expect (! RigEngine().getDirty().isEnabled(), "the model is off by default");
        auto r = startRun (newSettings(), false);
        expect (r->tracker == nullptr, "no launch record");
        expect (! r->v->documentModel(), "nothing of CP4");
        fill (*r->e, 101.0);
        tick (*r->v);
        expect (r->v->getDocDot() == Dot::filled, "the model off: the untitled dot, as the app always had it");
        expectEquals (r->v->getShowTitle(), juce::String ("Untitled song, not saved yet"), "the name's accessible title says the truth");
        const int before = RigDocFlow::requestsForTests.load();
        expect (! r->requestQuit(), "the quit question never starts");
        expectEquals (r->asker.questions(), 0);
        expectEquals (RigDocFlow::requestsForTests.load(), before, "no flow request");
        // A named show: the old title said "Unsaved show" while it was open.
        const auto f = makeNamed ("Off named");
        r->v->askOpenSong (f);
        if (r->asker.questions() > 0)
            r->asker.answer (Choice::dontSave);
        tick (*r->v);
        expect (r->current() == f);
        expectEquals (r->v->getShowTitle(), f.getFileNameWithoutExtension(), "named: its name");
        expect (r->v->getDocDot() == Dot::none);
        r->settings->saveIfNeeded();
        expect (! r->settings->containsKey (riglaunch::kKey), "no launch record in the settings");
    }

    //==========================================================================
    void runFailedSaves()
    {
        // The disk failing at the commit: quit aborted, nothing changed, the show still playing.
        for (const juce::String point : { "commit", "stage" })
        {
            const auto f = makeNamed ("Fail " + point);
            auto r = runOn (f);
            r->e->getTransport().setBpm (120.0);
            playing (*r->e);
            const auto bytes = f.loadFileAsString();
            const auto xml = xmlOf (*r->e);
            rigshow::hooks().at = [point] (const juce::String& p, int) { return p != point; };
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            rigshow::hooks().at = nullptr;
            expectEquals (r->quits, 0, point + ": the quit is off");
            expectEquals (r->flow().getStats().failedSaves, 1);
            expectEquals (r->asker.count (FakeAsker::Type::error), 1, point + ": the problem is shown");
            expect (f.loadFileAsString() == bytes && xmlOf (*r->e) == xml && r->e->getTransport().isPlaying() && ! r->flow().isBusy(), point + ": untouched");
            expect (! r->tracker->record().cleanShutdown, point + ": no quit recorded");
        }
        // A plugin's settings that can't be read (#125): the save fails, no quit.
        {
            hostRef = std::make_unique<juce::SharedResourcePointer<PluginHost>>();
            findFormat();
            const auto f = makeNamed ("Plugin fails");
            auto r = runOn (f);
            r->e->getSynthPlugin().load (FakeFormat::describe ("fake:Q"));
            pumpUntil ([&] { return r->e->getSynthPlugin().isLoaded(); });
            FakeSound::throwOnGetState = true;
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            FakeSound::throwOnGetState = false;
            expectEquals (r->quits, 0, "#125: a plugin's settings that can't be saved: no quit");
            expectEquals (r->flow().getStats().failedSaves, 1);
            expect (r->asker.count (FakeAsker::Type::error) == 1);
            r->e->getSynthPlugin().clear();
            pump (20);
            hostRef.reset();
            format = nullptr;
        }
        // Save As cancelled (untitled): no quit, no file.
        {
            auto r = startRun (newSettings());
            fill (*r->e, 104.0);
            r->asker.choices.push_back (Choice::save);
            r->asker.files.push_back (juce::File());
            expect (r->requestQuit());
            expectEquals (r->quits, 0, "Save As cancelled: no quit");
            expectEquals (r->flow().getStats().saveAsCancelled, 1);
            expect (! r->e->isBlank() && r->current() == juce::File() && ! r->flow().isBusy());
        }
    }

    //==========================================================================
    void runEditsDuring()
    {
        // While the question is up, then Save: the save has it, and it quits.
        {
            const auto f = makeNamed ("Edit asked");
            auto r = runOn (f);
            r->e->getTransport().setBpm (110.0);
            expect (r->requestQuit());
            r->e->getTransport().setBpm (133.0);
            r->asker.answer (Choice::save);
            expectWithinAbsoluteError (bpmIn (f), 133.0, 0.001, "the edit made while it asked is saved");
            expectEquals (r->quits, 1);
            relaunch (*r, f, "edit while asked, Save");
        }
        // ... then Don't Save: the answer came after the edit; it goes too.
        {
            const auto f = makeNamed ("Edit asked dont");
            auto r = runOn (f);
            r->e->getTransport().setBpm (110.0);
            expect (r->requestQuit());
            r->e->getTransport().setBpm (134.0);
            r->asker.answer (Choice::dontSave);
            expectEquals (r->quits, 1);
            auto again = relaunch (*r, f, "edit while asked, Don't Save");
            expectWithinAbsoluteError (again->e->getTransport().getBpm(), 97.0, 0.001, "as last saved");
        }
        // While Save As is up: the save has it.
        {
            auto r = startRun (newSettings());
            fill (*r->e, 101.0);
            const auto as = newFile ("Saved during");
            expect (r->requestQuit());
            r->asker.answer (Choice::save);
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 1);
            r->e->getTransport().setBpm (88.0);
            r->e->getLooper().setLayerAudio (3, noise ((int) kRate, 9), 2.0);
            r->asker.pick (as);
            expectWithinAbsoluteError (bpmIn (as), 88.0, 0.001, "the edit made while Save As was up is saved");
            expectEquals (r->quits, 1);
            relaunch (*r, as, "edit while Save As was up");
        }
        // Between the save and the quit (after the save read the show): asked again, no quit.
        for (int kind = 0; kind < 3; ++kind)
        {
            const juce::String which = kind == 0 ? "tempo" : kind == 1 ? "the phone's tempo" : "a loop recorded";
            const auto f = makeNamed ("After save " + juce::String (kind));
            auto r = runOn (f);
            r->e->getTransport().setBpm (115.0);
            auto* e = r->e.get();
            rigshow::hooks().at = [e, kind] (const juce::String& point, int)
            {
                if (point == "afterCommit")
                {
                    if (kind == 0) { e->getTransport().setBpm (140.0); e->getDirty().noteChange(); }
                    else if (kind == 1) e->getTransport().setBpm (141.0);
                    else e->getLooper().setLayerAudio (5, noise ((int) kRate, 21), 2.0);
                }
                return true;
            };
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            rigshow::hooks().at = nullptr;
            expectEquals (r->flow().getStats().saves, 1, which);
            expectEquals (r->asker.questions(), 2, which + ": asked again after the save");
            expectEquals (r->quits, 0, which + ": no quit without a new answer");
            r->asker.answer (Choice::cancel);
            expectEquals (r->quits, 0, which);
            expect (kind == 2 ? r->e->getLooper().getState (5) != RigLooper::State::empty : r->e->getTransport().getBpm() > 139.0, which + ": kept");
        }
    }

    //==========================================================================
    void runRecording()
    {
        auto record = [this] (const juce::String& what, std::function<void (RigEngine&, Runner&)> act)
        {
            const auto f = makeNamed ("Rec " + what);
            const auto bytes = f.loadFileAsString();
            auto r = runOn (f);
            Runner run { *r->e };
            r->e->getTransport().play();
            r->e->getLooper().maintain();
            run.beats (1);
            act (*r->e, run);
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            expectEquals (r->flow().getStats().saves, 1, what + ": saved");
            expectEquals (r->asker.questions(), 2, what + ": asked again (still recording)");
            expect (r->asker.lastQuestion().message.contains ("recording"), r->asker.lastQuestion().message);
            expectEquals (r->quits, 0, what);
            const auto saved = f.loadFileAsString();
            expect (saved != bytes, what + ": (the save happened)");
            r->asker.answer (Choice::dontSave);
            expectEquals (r->quits, 1, what + ": Don't Save quits");
            relaunch (*r, f, what + ": the save made at the quit");
        };
        record ("recording a new loop", [this] (RigEngine& e, Runner& run)
        {
            auto& layer = e.getLooper().getLayer (6);
            layer.source = RigLooper::input;
            layer.bars = 0;
            layer.countIn = 0;
            run.input = true;
            e.getLooper().pressRecord (6);
            run.beats (4.5);
            expect (e.getLooper().getState (6) == RigLooper::State::recording, "(recording)");
        });
        record ("overdubbing", [this] (RigEngine& e, Runner& run)
        {
            run.input = true;
            e.getLooper().pressRecord (0);
            run.beats (4.5);
            expect (e.getLooper().getState (0) == RigLooper::State::overdubbing, "(overdubbing)");
        });
        // An undo just done: not checked yet, so it asks.
        {
            const auto f = makeNamed ("Undo");
            auto r = runOn (f);
            Runner run { *r->e };
            r->e->getTransport().play();
            r->e->getLooper().maintain();
            run.beats (1);
            run.input = true;
            r->e->getLooper().pressRecord (0);
            run.beats (4.5);
            r->e->getLooper().pressRecord (0);
            run.input = false;
            run.beats (0.5);
            r->e->getLooper().undo (0);
            juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
            in.clear();
            r->e->process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 1, "an undo just done asks");
            r->asker.answer (Choice::cancel);
            expectEquals (r->quits, 0);
        }
    }

    //==========================================================================
    void runOneAtATime()
    {
        const auto f = makeNamed ("One");
        {
            auto r = runOn (f);
            r->e->getTransport().setBpm (120.0);
            expect (r->requestQuit());
            expect (! r->requestQuit(), "a second quit while it asks: nothing");
            expect (! r->requestQuit(), "and a third");
            expectEquals (r->asker.questions(), 1, "one question");
            expectEquals (r->flow().getStats().ignored, 2);
            expect (r->asker.answer (Choice::cancel));
            expectEquals (r->quits, 0);
            // Quit again later: asks again.
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 2);
            r->asker.answer (Choice::dontSave);
            expectEquals (r->quits, 1, "quits once");
        }
        {
            // During another request (Open's window is up): the quit does nothing.
            auto r = runOn (f);
            r->e->getTransport().setBpm (120.0);
            r->v->openShow();
            expectEquals (r->asker.count (FakeAsker::Type::openFile), 1);
            expect (! r->requestQuit(), "the quit is ignored while Open's window is up");
            expectEquals (r->asker.questions(), 0);
            expectEquals (r->quits, 0);
            r->asker.pick (juce::File());
            expect (r->requestQuit(), "then it works");
            r->asker.answer (Choice::cancel);
        }
        {
            // A stale answer: the question was up when another show was opened another way.
            auto r = runOn (f);
            r->e->getTransport().setBpm (120.0);
            expect (r->requestQuit());
            auto held = std::move (r->asker.heldAsk);
            r->asker.heldAsk = nullptr;
            const auto other = makeNamed ("Other");
            r->v->openShowFile (other);   // (the old way: drops what the flow had under way)
            held (Choice::dontSave);
            expectEquals (r->quits, 0, "the old answer does nothing");
            expect (! r->flow().isBusy());
            // The flow gone (the window closed) before the answer.
            auto r2 = runOn (f);
            r2->e->getTransport().setBpm (121.0);
            expect (r2->requestQuit());
            auto orphan = std::move (r2->asker.heldAsk);
            r2->asker.heldAsk = nullptr;
            r2->v.reset();
            orphan (Choice::dontSave);
            expectEquals (r2->quits, 0, "nothing after the window is gone");
        }
    }

    //==========================================================================
    void runRelaunchProblems()
    {
        auto expectNewSong = [this] (Run& r, const juce::String& what)
        {
            expect (! r.launched.opened && r.current() == juce::File() && r.e->isBlank(), what + ": a new song");
            expect (r.tracker->record().intent == riglaunch::Record::Intent::empty, what + ": the record says so now");
        };
        {
            const auto f = makeNamed ("Missing");
            const auto s = settingsOn (f);
            f.deleteFile();
            auto r = startRun (s);
            expectNewSong (*r, "missing");
            expect (r->launched.notice.contains (f.getFullPathName()), "missing: the notice names the path: " + r->launched.notice);
            expect (! f.exists(), "missing: never recreated");
            r->shutdown();
            auto r2 = startRun (s);
            expect (! f.exists() && r2->launched.notice.isEmpty(), "missing: said once, still not recreated");
        }
        {
            const auto f = makeNamed ("Moved");
            const auto s = settingsOn (f);
            const auto to = folder.getChildFile ("Moved here.crateshow");
            expect (f.moveFileTo (to));
            auto r = startRun (s);
            expectNewSong (*r, "moved");
            expect (r->launched.notice.contains (f.getFullPathName()), "moved: names where it was");
            expect (! f.exists() && to.existsAsFile(), "moved: nothing made at the old place, the moved file left");
        }
        {
            // Changed elsewhere, same size: opened as it is now, told, and the next Save asks first.
            const auto f = makeNamed ("Changed", 97.0);
            const auto s = settingsOn (f);
            auto text = f.loadFileAsString();
            expect (text.contains ("bpm=\"97"));
            text = text.replaceFirstOccurrenceOf ("bpm=\"97", "bpm=\"98");
            expect (f.replaceWithText (text));
            expectEquals (f.getSize(), (juce::int64) text.getNumBytesAsUTF8(), "(same size)");
            auto r = startRun (s);
            expect (r->launched.opened && r->current() == f, "changed: still opened, as the named show");
            expect (r->launched.changed, "changed: seen");
            expect (r->launched.notice.contains ("changed outside Kaleidobox"), "changed: told: " + r->launched.notice);
            expectWithinAbsoluteError (r->e->getTransport().getBpm(), 98.0, 0.001, "changed: what's on disk now");
            const auto bytes = f.loadFileAsString();
            r->e->getTransport().setBpm (99.0);
            r->v->saveShow();   // Cmd+S
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 1, "changed: its first Save asks (the Save As window)");
            expect (r->asker.last (FakeAsker::Type::saveFile)->file == f, "changed: on that file (the window asks before replacing it)");
            r->asker.pick (juce::File());
            expect (f.loadFileAsString() == bytes, "changed: cancelled, the file untouched");
            r->v->saveShow();
            r->asker.pick (f);   // (replace it: confirmed in the window)
            expectWithinAbsoluteError (bpmIn (f), 99.0, 0.001, "changed: saved once confirmed");
            r->v->saveShow();
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 2, "changed: after that, Cmd+S saves without asking");
        }
        for (const juce::String kind : { "unreadable", "not a show" })
        {
            const auto f = makeNamed (kind);
            const auto s = settingsOn (f);
            if (kind == "unreadable")
                f.replaceWithText ("this is not XML at all <<<");
            else
                f.replaceWithText ("<Something bpm=\"60\"/>");
            const auto bytes = f.loadFileAsString();
            auto r = startRun (s);
            expectNewSong (*r, kind);
            expect (r->launched.notice.contains (f.getFullPathName()) && r->launched.notice.contains ("left as it is"), kind + ": " + r->launched.notice);
            expect (f.loadFileAsString() == bytes, kind + ": the file left alone");
            // Quit (a blank new song): nothing written to it.
            expect (r->requestQuit());
            expectEquals (r->quits, 1);
            expect (f.loadFileAsString() == bytes, kind + ": still left alone after the quit");
        }
        {
            auto sub = folder.getChildFile ("Gone folder");
            sub.createDirectory();
            RigEngine e;
            e.prepare (kRate, kBlock);
            fill (e, 92.0);
            const auto f = sub.getChildFile ("In gone.crateshow");
            expect (e.saveShowFile (f, rigshow::SaveKind::document).ok);
            const auto s = settingsOn (f);
            sub.deleteRecursively();
            auto r = startRun (s);
            expectNewSong (*r, "folder gone");
            expect (! sub.exists(), "folder gone: not made again");
        }
        {
            // A record naming the Last show (it can't, but if it did): never opened.
            const auto last = riglastshow::file();
            RigEngine e;
            e.prepare (kRate, kBlock);
            fill (e, 70.0);
            expect (e.saveShowFile (last).ok);
            const auto s = newSettings();
            {
                auto p = settingsAt (s);
                auto xml = std::make_unique<juce::XmlElement> ("LaunchDocument");
                xml->setAttribute ("intent", "reopenDocument");
                xml->setAttribute ("path", last.getFullPathName());
                xml->setAttribute ("clean", true);
                p->setValue (riglaunch::kKey, xml.get());
                p->saveIfNeeded();
            }
            auto r = startRun (s);
            expectNewSong (*r, "a record naming the Last show");
            last.deleteFile();
        }
    }

    //==========================================================================
    void runQuitTimeSaveProblems()
    {
        {
            // Its file deleted while it's open, nothing changed: the quit asks (it's the only copy).
            const auto f = makeNamed ("Deleted clean");
            auto r = runOn (f);
            f.deleteFile();
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 1, "the file gone: it asks");
            expect (r->asker.lastQuestion().message.isNotEmpty());
            r->asker.answer (Choice::save);
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 1, "Save: the Save As window (never recreated unasked)");
            expect (r->asker.last (FakeAsker::Type::saveFile)->file == f, "on where it was");
            r->asker.pick (juce::File());
            expectEquals (r->quits, 0, "cancelled: no quit");
            expect (! f.exists(), "not recreated");
            // Confirmed in the window: saved there, then it quits.
            expect (r->requestQuit());
            r->asker.answer (Choice::save);
            r->asker.pick (f);
            expect (f.existsAsFile(), "saved where you said");
            expectEquals (r->quits, 1);
            relaunch (*r, f, "deleted, saved again from the window");
        }
        for (const juce::String kind : { "changed elsewhere", "unreadable now" })
        {
            const auto f = makeNamed ("At quit " + kind);
            auto r = runOn (f);
            r->e->getTransport().setBpm (125.0);
            juce::Thread::sleep (5);
            if (kind == "unreadable now")
                f.replaceWithText ("garbage");
            else
                f.replaceWithText (f.loadFileAsString().replace ("bpm=\"97", "bpm=\"77"));
            f.setLastModificationTime (juce::Time::getCurrentTime() + juce::RelativeTime::seconds (5));
            const auto bytes = f.loadFileAsString();
            expect (r->requestQuit());
            r->asker.answer (Choice::save);
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 1, kind + ": Save asks first (the Save As window)");
            r->asker.pick (juce::File());
            expect (f.loadFileAsString() == bytes && r->quits == 0, kind + ": cancelled, never overwritten, no quit");
        }
        {
            // The folder gone: Save As (CP3), then it quits.
            auto sub = folder.getChildFile ("Quit gone");
            sub.createDirectory();
            RigEngine e;
            e.prepare (kRate, kBlock);
            fill (e, 93.0);
            const auto f = sub.getChildFile ("Away.crateshow");
            expect (e.saveShowFile (f, rigshow::SaveKind::document).ok);
            auto r = runOn (f);
            sub.deleteRecursively();
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 1);
            const auto as = newFile ("Instead");
            r->asker.files.push_back (as);
            r->asker.answer (Choice::save);
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 1, "folder gone: Save As");
            expect (as.existsAsFile() && r->quits == 1 && ! sub.exists());
            relaunch (*r, as, "folder gone, Saved As");
        }
    }

    //==========================================================================
    void runInterrupted()
    {
        {
            // Killed with a named show changed: the next launch has it as last saved, and says why.
            const auto f = makeNamed ("Killed");
            auto r = runOn (f);
            r->e->getTransport().setBpm (130.0);
            const auto s = r->settings->getFile();
            r->v.reset();
            r->e.reset();   // (no quit, no shutdown)
            auto r2 = startRun (s);
            expect (r2->launched.opened && r2->current() == f, "killed: the named show");
            expect (r2->launched.interrupted, "killed: seen");
            expect (r2->launched.notice.contains ("didn't quit properly"), r2->launched.notice);
            expectWithinAbsoluteError (r2->e->getTransport().getBpm(), 97.0, 0.001, "killed: as last saved (no recovery)");
        }
        {
            // Killed on an untitled song: a new song, nothing said.
            auto r = startRun (newSettings());
            fill (*r->e, 101.0);
            const auto s = r->settings->getFile();
            r->v.reset();
            r->e.reset();
            auto r2 = startRun (s);
            expect (! r2->launched.opened && r2->e->isBlank() && r2->launched.notice.isEmpty(), "killed untitled: a new song, no notice");
        }
        {
            // The record can't be written at the quit: it still quits; the next launch is the one
            // the record last had (the named show as last saved, as after a kill).
            const auto f = makeNamed ("No record");
            auto r = runOn (f);
            r->e->getTransport().setBpm (111.0);
            riglaunch::Tracker::failWritesForTests = true;
            expect (r->requestQuit());
            r->asker.answer (Choice::dontSave);
            expectEquals (r->quits, 1, "quits anyway");
            expect (r->tracker->failedWrites > 0);
            const auto s = r->settings->getFile();
            r->v.reset();
            r->e.reset();   // (killed before the shutdown's own save of the settings)
            riglaunch::Tracker::failWritesForTests = false;
            auto r2 = startRun (s);
            expect (r2->launched.opened && r2->current() == f, "the record from before: the named show");
            expectWithinAbsoluteError (r2->e->getTransport().getBpm(), 97.0, 0.001, "as last saved");
            expect (r2->launched.interrupted, "taken for a quit that didn't finish");
        }
        {
            // Killed between the document save and the record: the next launch opens the file as
            // it is (the new save), and says it stopped while saving.
            const auto f = makeNamed ("Between");
            auto r = runOn (f);
            r->e->getTransport().setBpm (118.0);
            rigshow::hooks().at = [] (const juce::String& point, int)
            {
                if (point == "afterCommit")
                    riglaunch::Tracker::failWritesForTests = true;   // (the XML is committed; the process stops here)
                return true;
            };
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            rigshow::hooks().at = nullptr;
            expectWithinAbsoluteError (bpmIn (f), 118.0, 0.001, "(the save was committed)");
            const auto s = r->settings->getFile();
            r->v.reset();
            r->e.reset();
            riglaunch::Tracker::failWritesForTests = false;
            auto r2 = startRun (s);
            expect (r2->launched.opened && r2->current() == f, "between: the named show");
            expectWithinAbsoluteError (r2->e->getTransport().getBpm(), 118.0, 0.001, "between: the save that was committed");
            expect (r2->launched.notice.contains ("stopped while it was saving"), "between: " + r2->launched.notice);
        }
    }

    //==========================================================================
    void runLastShow()
    {
        const auto last = riglastshow::file();
        {
            // Off (main's way, #94): the Last show is saved; a failure is kept and said at the next launch.
            RigEngine e;
            e.prepare (kRate, kBlock);
            fill (e, 66.0);
            const auto s = newSettings();
            auto p = settingsAt (s);
            expect (riglastshow::save (e, last, *p).ok, "off: the Last show saved");
            expect (! p->containsKey (riglastshow::kFailedKey));
            rigshow::hooks().at = [] (const juce::String& point, int) { return point != "commit"; };
            const auto failed = riglastshow::save (e, last, *p);
            rigshow::hooks().at = nullptr;
            expect (! failed.ok && p->containsKey (riglastshow::kFailedKey), "off: a failure is kept");
            expect (riglastshow::quitMessage (failed).contains ("Keep Kaleidobox open"), "off: the quit's alert words");
            auto next = settingsAt (s);
            expect (riglastshow::takeFailureFromLastQuit (*next).contains ("wasn't saved when Kaleidobox last quit"), "off: said at the next launch");
            expect (! next->containsKey (riglaunch::kKey), "off: no launch record");
        }
        {
            // On: the quit never writes the Last show, the launch never reads it.
            const auto bytes = last.loadFileAsString();
            expect (bytes.contains ("bpm=\"66"), "(a Last show from an earlier run, the model off)");
            auto r = startRun (newSettings());
            expect (std::abs (r->e->getTransport().getBpm() - 66.0) > 0.5, "on: the launch didn't load the Last show");
            fill (*r->e, 140.0);
            expect (r->requestQuit());
            r->asker.answer (Choice::dontSave);
            expectEquals (r->quits, 1);
            r->shutdown();
            expect (last.loadFileAsString() == bytes, "on: the Last show not written at the quit");
        }
        {
            // On: opening the Last show (from My ideas) makes it untitled, with the dot; Save As to it is refused.
            auto r = startRun (newSettings());
            r->v->askOpenSong (last);
            expect (r->current() == juce::File(), "the Last show opens untitled");
            expectWithinAbsoluteError (r->e->getTransport().getBpm(), 66.0, 0.001, "(its contents)");
            expect (r->tracker->record().intent == riglaunch::Record::Intent::empty, "never the record's document");
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "untitled, not from a new song: the dot");
            r->v->saveShow();
            expectEquals (r->asker.count (FakeAsker::Type::saveFile), 1, "Cmd+S: Save As");
            const auto bytes = last.loadFileAsString();
            r->asker.pick (last);
            expect (last.loadFileAsString() == bytes && r->current() == juce::File(), "Save As onto the Last show: refused");
            // Quit: it asks (untitled, something in it); Don't Save -> a new song next time.
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 1);
            r->asker.answer (Choice::dontSave);
            relaunch (*r, juce::File(), "the Last show opened, Don't Save");
        }
        last.deleteFile();
    }

    //==========================================================================
    void runDiscarded()
    {
        const auto f = makeNamed ("Discard", 97.0);
        const auto bytes = f.loadFileAsString();
        auto r = runOn (f);
        for (int round = 0; round < 2; ++round)
        {
            const double edit = 150.0 + round;
            r->e->getTransport().setBpm (edit);
            r->e->getLooper().setLayerAudio (7, noise ((int) kRate, 40 + round), 2.0);
            expect (r->requestQuit());
            r->asker.answer (Choice::dontSave);
            expectEquals (r->quits, 1);
            r = relaunch (*r, f, "Don't Save, round " + juce::String (round + 1));
            expectWithinAbsoluteError (r->e->getTransport().getBpm(), 97.0, 0.001, "the discarded tempo never back");
            expect (r->e->getLooper().getState (7) == RigLooper::State::empty, "the discarded loop never back");
            expect (f.loadFileAsString() == bytes, "the file as saved");
            expect (settle (*r->e).state == DS::clean);
        }
        // Negative control: had the quit kept the Last show and the launch read it (main's way),
        // the same check would see the discarded edit come back.
        {
            const auto last = riglastshow::file();
            auto e = openSaved (f);
            e->getTransport().setBpm (152.0);
            auto p = settingsAt (newSettings());
            expect (riglastshow::save (*e, last, *p).ok);
            RigEngine back;
            back.prepare (kRate, kBlock);
            if (auto xml = juce::parseXML (last))
                back.loadShow (juce::ValueTree::fromXml (*xml));
            back.readLoopAudio (last);
            expect (std::abs (back.getTransport().getBpm() - 97.0) > 1.0, "control: restoring from the Last show brings the discarded tempo back");
            last.deleteFile();
        }
    }

    //==========================================================================
    void runCatchIdea()
    {
        {
            // A blank new song: Catch an idea, the take closed, the tempo found: unsaved, the dot,
            // and the quit asks.
            auto r = startRun (newSettings());
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "a blank new song: no dot");
            r->v->catchIdea();
            expectEquals (r->asker.questions(), 0, "(blank: no question)");
            expectEquals (r->v->getTakeLayer(), 0);
            const double was = r->e->getTransport().getBpm();
            r->e->getLooper().setLayerAudio (0, ideatempo::jingle (96.0, 0.8, 16.0), 16.0 * was / 60.0);   // (the take closed)
            expect (r->v->checkCaughtIdea(), "the tempo found");
            expect (settle (*r->e).state == DS::dirty, "the model: unsaved");
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "the dot");
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 1, "the quit asks");
            r->asker.answer (Choice::cancel);
        }
        {
            // Saved (Save As) after the take closed, before its tempo was found: the named show
            // is clean; then the tempo change is unsaved, by the model's own check.
            auto r = startRun (newSettings());
            r->v->catchIdea();
            const double was = r->e->getTransport().getBpm();
            r->e->getLooper().setLayerAudio (0, ideatempo::jingle (96.0, 0.8, 16.0), 16.0 * was / 60.0);
            const auto as = newFile ("Caught");
            r->v->saveShow();
            r->asker.pick (as);
            expect (r->current() == as, "named");
            expect (settle (*r->e).state == DS::clean, "saved: clean");
            expect (r->v->checkCaughtIdea(), "the save left the take alone: its tempo is still found (#136)");
            const auto st = settle (*r->e);
            expect (st.state == DS::dirty, "the caught tempo: unsaved");
            const auto diff = r->e->getDirty().differences (20).joinIntoString (" | ");
            expect (diff.contains ("bpm"), "the model sees the tempo itself: " + diff);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "the dot");
            // The quit: Save keeps it, then quits.
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            expectEquals (r->quits, 1);
            expectWithinAbsoluteError (bpmIn (as), r->e->getTransport().getBpm(), 0.001, "the caught tempo saved");
            relaunch (*r, as, "Catch an idea, saved at the quit");
        }
        {
            // A quit's Save As while the take is waiting for its tempo: the take isn't forgotten.
            auto r = startRun (newSettings());
            r->v->catchIdea();
            const double was = r->e->getTransport().getBpm();
            r->e->getLooper().setLayerAudio (0, ideatempo::jingle (96.0, 0.8, 16.0), 16.0 * was / 60.0);
            expect (r->requestQuit());
            r->asker.answer (Choice::save);
            expect (r->v->checkCaughtIdea(), "the tempo found while Save As is up");
            r->asker.pick (newFile ("Caught at quit"));
            expectEquals (r->quits, 1, "saved with it, quits");
        }
    }

    //==========================================================================
    std::unique_ptr<juce::SharedResourcePointer<PluginHost>> hostRef;
    FakeFormat* format = nullptr;
    void findFormat()
    {
        for (auto* fmt : hostRef->getObject().formats.getFormats())
            if (auto* fake = dynamic_cast<FakeFormat*> (fmt))
                format = fake;
        if (format == nullptr)
        {
            format = new FakeFormat();
            hostRef->getObject().formats.addFormat (std::unique_ptr<juce::AudioPluginFormat> (format));
        }
        format->hold = false;
        format->finishAll();
        format->missing.clear();
        format->failing.clear();
        pump (20);
    }

    void runPlugins()
    {
        hostRef = std::make_unique<juce::SharedResourcePointer<PluginHost>>();
        findFormat();
        RigDocFlow::verifyTimeoutMs = 2000;
        auto makeShow = [this] (const juce::String& label)
        {
            RigEngine e;
            e.prepare (kRate, kBlock);
            e.getSynthPlugin().load (FakeFormat::describe ("fake:A"));
            pumpUntil ([&] { return e.getSynthPlugin().isLoaded(); });
            e.getLooper().setLayerAudio (0, noise ((int) kRate, 3), 2.0);
            const auto f = newFile (label);
            expect (e.saveShowFile (f, rigshow::SaveKind::document).ok);
            return f;
        };
        {
            // Relaunched with the plugin still loading: clean, the quit doesn't ask; the file untouched.
            const auto f = makeShow ("Plugin loading");
            const auto bytes = f.loadFileAsString();
            const auto s = settingsOn (f);
            format->hold = true;
            auto r = startRun (s);
            pump (20);
            expect (r->launched.opened && ! r->e->getSynthPlugin().isLoaded() && format->waiting() == 1, "(relaunched, the plugin still loading)");
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 0, "still loading, checked clean: no question");
            expectEquals (r->quits, 1);
            format->finishAll();
            pump (30);
            expect (f.loadFileAsString() == bytes, "the file untouched");
            format->hold = false;
            r->shutdown();
            auto r2 = startRun (s);
            pumpUntil ([&] { return r2->e->getSynthPlugin().isLoaded(); });
            expect (r2->launched.opened && r2->e->getSynthPlugin().isLoaded(), "relaunched again: the plugin comes in");
        }
        {
            // An edit while it loads: the quit asks; Save keeps the plugin in the file.
            const auto f = makeShow ("Plugin edit");
            format->hold = true;
            auto r = startRun (settingsOn (f));
            pump (20);
            r->e->getTransport().setBpm (99.0);
            r->asker.choices.push_back (Choice::save);
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 1, "an edit while it loads: asks");
            expectEquals (r->quits, 1, "saved, then quits");
            auto xml = juce::parseXML (f);
            bool hasPlugin = false;
            if (xml != nullptr)
                for (auto* inst : xml->getChildWithTagNameIterator ("Instrument"))
                    hasPlugin = hasPlugin || (inst->getStringAttribute ("tool") == "Synth" && inst->getChildByName ("Plugin") != nullptr);
            expect (hasPlugin, "the save while it loaded keeps the plugin");
            format->finishAll();
            format->hold = false;
            pump (30);
        }
        {
            // Not installed here, at the relaunch: opened, clean, no question; the file keeps it.
            const auto f = makeShow ("Plugin missing");
            const auto bytes = f.loadFileAsString();
            format->missing.add ("fake:A");
            auto r = startRun (settingsOn (f));
            pump (30);
            expect (r->launched.opened && ! r->e->getSynthPlugin().isLoaded(), "(relaunched without the plugin)");
            expect (r->requestQuit());
            expectEquals (r->asker.questions(), 0, "a missing plugin: clean, no question");
            expectEquals (r->quits, 1);
            expect (f.loadFileAsString() == bytes, "the file keeps it");
            format->missing.clear();
        }
        RigDocFlow::verifyTimeoutMs = 300;
        hostRef.reset();
        format = nullptr;
    }

    //==========================================================================
    // Plays the run, its window's timer every third block; counts what the dot showed.
    struct DotCount { int ticks = 0, filled = 0, ring = 0, none = 0; };
    DotCount playAndWatch (Run& r, double seconds, std::function<void (int block)> each = {})
    {
        DotCount c;
        Runner run { *r.e, r.v.get() };
        for (int i = 0, n = (int) (seconds * kRate / kBlock); i < n; ++i)
        {
            if (each) each (i);
            run.block();
            if (run.blocks % 3 == 0)
            {
                ++c.ticks;
                const auto d = r.v->getDocDot();
                (d == Dot::filled ? c.filled : d == Dot::ring ? c.ring : c.none) += 1;
            }
        }
        return c;
    }
    juce::String countText (const DotCount& c) { return juce::String (c.ticks) + " ticks: " + juce::String (c.none) + " none, " + juce::String (c.filled) + " dot, " + juce::String (c.ring) + " ring"; }

    void runDot()
    {
        {
            // Untitled: blank, no dot; something in it, the dot.
            auto r = startRun (newSettings());
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "blank untitled: no dot");
            expectEquals (r->v->getShowTitle(), juce::String ("Untitled song"));
            fill (*r->e, 101.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "untitled with something in it: the dot");
            expectEquals (r->v->getShowTitle(), juce::String ("Untitled song, unsaved changes"), "its title says so");
            const auto as = newFile ("Dot save as");
            r->v->saveShow();
            r->asker.pick (as);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "saved: gone");
            expectEquals (r->v->getShowTitle(), as.getFileNameWithoutExtension(), "named: its name (not \"Unsaved show\")");
        }
        {
            // Named: edit, save, edit and undo back, overdub and undo.
            const auto f = makeNamed ("Dot named");
            auto r = runOn (f);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "opened: no dot");
            auto p = r->e->getBass().getPattern (1);
            const auto old = p;
            p.steps[5].on = ! p.steps[5].on;
            r->e->getBass().setPattern (1, p);
            r->e->getDirty().noteChange();
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "an edit: the dot");
            expect (r->v->getShowTitle().endsWith (", unsaved changes"));
            r->e->getBass().setPattern (1, old);   // undone back to the saved state
            r->e->getDirty().noteChange();
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "undone back to as saved: gone");
            r->e->getTransport().setBpm (123.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled);
            r->v->saveShow();
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "Cmd+S: gone");
            // Overdub, then undo it.
            Runner run { *r->e };
            r->e->getTransport().play();
            r->e->getLooper().maintain();
            run.beats (1);
            run.input = true;
            r->e->getLooper().pressRecord (0);
            run.beats (4.5);
            r->e->getLooper().pressRecord (0);
            run.input = false;
            run.beats (1.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "overdubbed: the dot");
            r->e->getLooper().undo (0);
            run.beats (1.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "overdub undone: gone");
            // The same through the window's one Undo / Redo history (Cmd+Z, Cmd+Shift+Z).
            tick (*r->v);
            run.input = true;
            r->e->getLooper().pressRecord (0);
            run.beats (2.0);
            tick (*r->v);   // (the window sees the overdub under way, as its timer does)
            run.beats (2.5);
            r->e->getLooper().pressRecord (0);
            run.input = false;
            run.beats (1.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "overdubbed again: the dot");
            expect (r->v->historyUndo(), "Undo takes the overdub back");
            run.beats (1.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "Undo back to as saved: gone");
            expect (r->v->historyRedo(), "Redo brings it back");
            run.beats (1.0);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "Redo: unsaved again, the dot");
        }
        for (int kind = 0; kind < 3; ++kind)
        {
            // Playback: the song with its automation, macros and sections, scenes, the Drone on;
            // then also a section with its own loop bank; then a plain loop playing.
            const juce::String which = kind == 0 ? "automation, macros, sections, scenes, the Drone on"
                                     : kind == 1 ? "the same, and a section playing its own loop bank" : "a loop and a beat playing";
            RigEngine made;
            made.prepare (kRate, kBlock);
            if (kind == 2)
                fill (made, 100.0);
            else
            {
                buildRich (made, 2);
                if (kind == 0 && made.getTimeline().getMarkers().size() > 1)
                    made.getTimeline().getMarkers()[1].loopBank = -1;
                auto scene = [&made] (const char* name) { RigScene sc; sc.name = name; sc.state = made.captureScene(); return sc; };
                made.getScenes().getScenes().push_back (scene ("One"));
                made.getStrip (RigEngine::bassStrip).mute = true;
                made.getScenes().getScenes().push_back (scene ("Two"));
                made.getStrip (RigEngine::bassStrip).mute = false;
                made.getTimeline().on = true;   // (the song plays its sections: saved that way)
            }
            const auto f = newFile ("Dot playback " + juce::String (kind));
            expect (made.saveShowFile (f, rigshow::SaveKind::document).ok);
            auto r = runOn (f);
            if (kind != 2)
                if (auto* on = r->e->getDrone().params.getParameter ("droneOn"))
                    on->setValueNotifyingHost (1.0f);
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, which + ": (seen saved)");
            r->e->getTransport().play();
            r->e->getDirty().resetStats();
            const auto c = playAndWatch (*r, 12.0, [&] (int block)
            {
                if (kind != 2 && block == 300) r->e->getScenes().launch (1);
                if (kind != 2 && block == 700) r->e->getScenes().launch (0);
            });
            const auto st = r->e->getDirty().getStats();
            logMessage ("  playback (" + which + "): " + countText (c) + "; " + juce::String (st.checks) + " checks, noted by queued bank "
                        + juce::String (st.watchedBy[RigDirty::Stats::byQueuedBank]) + ", banks " + juce::String (st.watchedBy[RigDirty::Stats::byBanks])
                        + ", timeline " + juce::String (st.watchedBy[RigDirty::Stats::byTimeline]) + ", automation " + juce::String (st.watchedBy[RigDirty::Stats::byAutomation])
                        + ", macros " + juce::String (st.watchedBy[RigDirty::Stats::byMacros]) + ", state " + juce::String (st.watchedBy[RigDirty::Stats::byState])
                        + ", notes " + juce::String ((juce::int64) st.notes));
            expectEquals (c.filled, 0, which + ": playback never lights the dot");
            if (kind != 1)
                expectEquals (c.ring, 0, which + ": nor the ring (no blinking)");
            settle (*r->e);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, which + ": after it, still no dot");
        }
        {
            // Pending (a bank switch waiting for its bar): never drawn as saved.
            const auto f = makeNamed ("Dot pending", 30.0);   // (a long bar: the switch waits)
            auto r = runOn (f);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none);
            playing (*r->e);
            settle (*r->e, 300);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::none, "(seen saved)");
            r->e->switchLoopBank (1);
            int pendingTicks = 0, drawnClean = 0;
            Runner run { *r->e, r->v.get() };
            for (int i = 0; i < 60; ++i)
            {
                run.block();
                if (r->e->getDirty().getState() == DS::pending)
                {
                    tick (*r->v);
                    ++pendingTicks;
                    drawnClean += r->v->getDocDot() == Dot::none ? 1 : 0;
                }
            }
            expectGreaterThan (pendingTicks, 0, "(pending)");
            expectEquals (drawnClean, 0, "pending is never drawn as saved");
        }
        {
            // Pending after it was seen clean: a faint ring, not nothing.
            const auto f = makeNamed ("Dot ring");
            auto r = runOn (f);
            tick (*r->v);
            r->e->getDirty().noteChange();
            expect (r->e->getDirty().getState() == DS::pending);
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::ring, "pending after clean: the ring");
            expect (r->v->getShowTitle().endsWith ("checking for unsaved changes"));
            // ... and after it was seen unsaved: the dot stays.
            r->e->getTransport().setBpm (126.0);
            settle (*r->e);
            tick (*r->v);
            r->e->getDirty().noteChange();
            tick (*r->v);
            expect (r->v->getDocDot() == Dot::filled, "pending after unsaved: the dot stays");
        }
        {
            // The cost: the model's checks and hashing while idle and while playing, with the dot
            // drawn (the app's window) and without it (the same show, a hosted window).
            auto cost = [this] (bool withDot, bool play)
            {
                RigView::standaloneDocuments = withDot;
                // (The song whose second section plays its own loop bank: the model checks while it plays.)
                RigEngine made;
                made.prepare (kRate, kBlock);
                buildRich (made, 2);
                made.getTimeline().on = true;
                const auto f = newFile (juce::String ("Cost ") + (withDot ? "dot" : "none") + (play ? " play" : " idle"));
                expect (made.saveShowFile (f, rigshow::SaveKind::document).ok);
                auto r = startRun (settingsOn (f));
                RigView::launchShow = juce::File();   // (a hosted window doesn't take it)
                settle (*r->e);
                r->e->getTransport().setBpm (128.0);   // (an edit: the dot shows)
                settle (*r->e);
                tick (*r->v);
                expect (r->v->getDocDot() == (withDot ? Dot::filled : Dot::none));
                if (play)
                    r->e->getTransport().play();
                r->e->getDirty().resetStats();
                playAndWatch (*r, 12.0);
                const auto s = r->e->getDirty().getStats();
                RigView::standaloneDocuments = true;
                return std::pair<int, juce::int64> { s.checks, s.bytesHashed };
            };
            for (bool play : { false, true })
            {
                const auto with = cost (true, play), without = cost (false, play);
                logMessage (juce::String ("  ") + (play ? "playing" : "idle") + ": checks " + juce::String (with.first) + " with the dot, " + juce::String (without.first)
                            + " without; bytes hashed " + juce::String (with.second) + " / " + juce::String (without.second));
                expectLessOrEqual (with.first, without.first, juce::String (play ? "playing" : "idle") + ": no more checks with the dot");
                expectLessOrEqual (with.second, without.second, juce::String (play ? "playing" : "idle") + ": no more hashing with the dot");
            }
        }
    }

    //==========================================================================
    juce::String stateXml (const juce::MemoryBlock& m)
    {
        auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
        if (xml == nullptr)
            return {};
        xml->removeAttribute (rigshow::kRevisionAttribute);
        if (auto* screens = xml->getChildByName ("Screens"))
            xml->removeChildElement (screens, true);
        return xml->toString (juce::XmlElement::TextFormat().singleLine());
    }

    void runHosted()
    {
        RigView::standaloneDocuments = false;
        const int requests0 = RigDocFlow::requestsForTests.load();
        // A hosted window, even with the model on and a launch record given to it: nothing.
        {
            const auto s = newSettings();
            auto p = settingsAt (s);
            riglaunch::Tracker t (*p);
            const int writes0 = t.writes;
            RigEngine e;
            e.prepare (kRate, kBlock);
            e.getDirty().setEnabled (true);
            RigView v (e, nullptr);
            v.setSize (1440, 960);
            v.setLaunchTracker (&t);
            expect (v.hostedDocuments() && ! v.documentModel(), "hosted: no CP4");
            fill (e, 101.0);
            settle (e);
            tick (v);
            expect (v.getDocDot() == Dot::none, "hosted: no dot (untitled with something in it)");
            expect (! v.requestQuit ([] {}), "hosted: no quit question");
            const auto f = makeNamed ("Hosted");
            v.openShowFile (f);
            e.getTransport().setBpm (120.0);
            v.saveShow();
            settle (e);
            tick (v);
            expect (v.getDocDot() == Dot::none, "hosted: no dot (named, changed)");
            expectEquals (t.writes, writes0, "hosted: no launch record written");
        }
        // A one-tool plugin's window, even with the app's switch on: nothing.
        {
            RigView::standaloneDocuments = true;
            RigEngine e;
            e.prepare (kRate, kBlock);
            e.getDirty().setEnabled (true);
            e.setOnlyTool (RigEngine::bassStrip);
            RigView v (e, nullptr);
            v.setSize (1440, 960);
            tick (v);
            expect (v.hostedDocuments() && ! v.documentModel() && v.getDocDot() == Dot::none, "a one-tool plugin: no CP4");
            expect (! v.requestQuit ([] {}));
            RigView::standaloneDocuments = false;
        }
        // The plugins: editor open/close cycles, no dot, the host's state round trip unchanged.
        for (int tool : { -1, (int) RigEngine::bassStrip })
        {
            const juce::String which = tool < 0 ? "Kaleidobox" : "Kaleidobox Bass";
            RigPlugin pl (tool);
            pl.prepareToPlay (kRate, kBlock);
            expect (! pl.getEngine().getDirty().isEnabled(), which + ": the model off");
            pl.getEngine().getTransport().setBpm (111.0);
            juce::MemoryBlock s0;
            pl.getStateInformation (s0);
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (pl.createEditor());
                editor->setSize (1440, 960);
                auto* view = dynamic_cast<RigView*> (editor->getChildComponent (0));
                expect (view != nullptr && ! view->documentModel(), which + ": no CP4");
                if (view != nullptr)
                {
                    tick (*view);
                    expect (view->getDocDot() == Dot::none, which + ": no dot");
                    expect (! view->requestQuit ([] {}), which + ": no quit question");
                }
            }
            juce::MemoryBlock s1;
            pl.getStateInformation (s1);
            expectEquals (stateXml (s1), stateXml (s0), which + ": the host's state the same");
            RigPlugin p2 (tool);
            p2.prepareToPlay (kRate, kBlock);
            p2.setStateInformation (s1.getData(), (int) s1.getSize());
            juce::MemoryBlock s2;
            p2.getStateInformation (s2);
            expectEquals (stateXml (s2), stateXml (s1), which + ": round trip");
        }
        expectEquals (RigDocFlow::requestsForTests.load(), requests0, "the flow never ran in a hosted window");
        RigView::standaloneDocuments = true;
    }
};

static RigQuitFlowTests rigQuitFlowTests;
