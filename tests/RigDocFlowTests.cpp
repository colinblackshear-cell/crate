// The standalone app's Save / Don't save / Cancel flow (RigDocFlow), headless: a real engine, a
// fake Asker (answers now, later or never, and can change the show first) and a small host.
// Every entry point against every kind of show you can be on, each answer; then saves that fail,
// edits that land while a question or a window is open, recordings, a show's plugin still
// loading, repeated and stale answers, Cmd+S, the window's wiring, the plugins' old way, a
// negative control (the cached state loses an edit), and the Drone's On switch.

#include "RigDirtyTestHelpers.h"
#include "RigAsker.h"
#include "RigDocFlow.h"
#include "PluginSlot.h"
#include "RigPluginFakes.h"

#include <deque>

using namespace dirtytest;

namespace
{
using DS = RigDirty::State;
using rigdirty::Choice;
using pluginfakes::FakeFormat;
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
    std::deque<Choice> choices;      // answered at once, in order
    std::deque<juce::File> files;    // picked at once, in order (juce::File(): cancelled)
    std::function<void()> before;    // runs just before an answer given at once
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
    int any() const { return (int) calls.size(); }
    const rigdirty::Question& lastQuestion() const
    {
        static rigdirty::Question none;
        for (auto it = calls.rbegin(); it != calls.rend(); ++it)
            if (it->type == Type::ask)
                return it->question;
        return none;
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

struct TestHost final : RigDocFlow::Host
{
    juce::File current, shows;
    juce::StringArray said;
    std::vector<Replaced> replacedKinds;
    juce::File getCurrentShow() const override { return current; }
    void setCurrentShow (const juce::File& f) override { current = f; }
    void say (const juce::String& s) override { said.add (s); }
    juce::File showsFolder() const override { return shows; }
    void replaced (Replaced k) override { replacedKinds.push_back (k); }
};

// The show you're on: its engine, the flow, its host and the fake. (The flow goes first.)
struct Doc
{
    std::unique_ptr<RigEngine> e;
    TestHost host;
    FakeAsker asker;
    std::unique_ptr<RigDocFlow> flow;
    void start() { flow = std::make_unique<RigDocFlow> (*e, asker, host); }
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

struct Reset
{
    ~Reset()
    {
        RigDocFlow::verifyTimeoutMs = 2000;
        RigDocFlow::maxRounds = 4;
        RigDocFlow::cachedStateForTests = false;
        RigView::standaloneDocuments = false;
        rigshow::hooks().at = nullptr;
    }
};
} // namespace

//==============================================================================
struct RigDocFlowTests final : juce::UnitTest
{
    RigDocFlowTests() : juce::UnitTest ("Rig document flow", "Rig") {}

    juce::File folder, target, stems, emptyFolder;
    int fileCount = 0;
    juce::File newFile (const juce::String& label) { return folder.getChildFile (juce::String (++fileCount) + " " + label + ".crateshow"); }

    enum class Entry { newSong, lesson, startFresh, openChooser, openFile, import, numEntries };
    enum class St { blankUntitled, untitled, untitledLastShow, namedClean, namedDirty, namedPending, namedNoBaseline, namedModelOff, numStates };
    static juce::String entryName (Entry e)
    {
        static const char* n[] = { "New song", "Lesson", "Start fresh (Catch an idea)", "Open show chooser", "Open an idea", "Import from Logic" };
        return n[(int) e];
    }
    static juce::String stName (St s)
    {
        static const char* n[] = { "blank untitled", "untitled", "untitled, the Last show", "named clean", "named dirty", "named, a bank switch queued",
                                   "named, no baseline", "named, model off" };
        return n[(int) s];
    }

    // Plays for a moment (the transport starts in the audio's first block).
    void playing (RigEngine& e)
    {
        if (! e.getTransport().isPlaying())
            e.getTransport().play();
        Runner r { e };
        r.seconds (0.05);
        expect (e.getTransport().isPlaying(), "(playing)");
    }

    static std::unique_ptr<RigEngine> fresh (bool model)
    {
        auto e = std::make_unique<RigEngine>();
        e->prepare (kRate, kBlock);
        if (model)
            e->getDirty().setEnabled (true);
        return e;
    }
    // A show with something in it: tempo, a bass step, a loop in each of two banks.
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
    juce::File makeNamed (const juce::String& label, double bpm = 97.0, rigshow::SaveKind kind = rigshow::SaveKind::document)
    {
        auto e = fresh (false);
        fill (*e, bpm);
        const auto f = newFile (label);
        expect (e->saveShowFile (f, kind).ok, "made " + label);
        return f;
    }

    std::unique_ptr<Doc> makeDoc (St st)
    {
        auto d = std::make_unique<Doc>();
        d->host.shows = folder;
        switch (st)
        {
            case St::blankUntitled:
                d->e = fresh (true);
                break;
            case St::untitled:
                d->e = fresh (true);
                fill (*d->e, 101.0);
                break;
            case St::untitledLastShow:
            {
                // Relaunched into the Last show (a copy): the model calls it clean; it's still untitled.
                const auto last = makeNamed ("Last show", 103.0, rigshow::SaveKind::copy);
                d->e = openSaved (last);
                expect (settle (*d->e).state == DS::clean, "the Last show reads clean to the model");
                break;
            }
            case St::namedClean:
            case St::namedDirty:
            case St::namedPending:
            {
                d->host.current = makeNamed ("Named");
                d->e = openSaved (d->host.current);
                if (st == St::namedDirty)
                    d->e->getTransport().setBpm (131.0);   // (as the phone sets it: nothing notes it)
                if (st == St::namedPending)
                {
                    playing (*d->e);
                    d->e->switchLoopBank (1);
                    expectEquals (d->e->getQueuedLoopBank(), 1, "a bank switch waits for its bar");
                }
                break;
            }
            case St::namedNoBaseline:
            {
                d->host.current = makeNamed ("Named");
                d->e = fresh (true);
                if (auto xml = juce::parseXML (d->host.current))
                    d->e->loadShow (juce::ValueTree::fromXml (*xml));   // (no audio step: no baseline)
                break;
            }
            case St::namedModelOff:
                d->host.current = makeNamed ("Named");
                d->e = fresh (false);
                if (auto xml = juce::parseXML (d->host.current))
                    d->e->loadShow (juce::ValueTree::fromXml (*xml));
                d->e->readLoopAudio (d->host.current);
                break;
            case St::numStates:
                break;
        }
        d->start();
        return d;
    }

    bool startEntry (Doc& d, Entry entry, bool& thenRan, RigDocFlow::Outcome& out)
    {
        auto then = [&thenRan, &out] (const RigDocFlow::Outcome& o) { thenRan = true; out = o; };
        switch (entry)
        {
            case Entry::newSong:     return d.flow->newSong (RigDocFlow::newSongWords(), then, true);
            case Entry::lesson:      return d.flow->newSong (RigDocFlow::lessonWords(), then, false);
            case Entry::startFresh:  return d.flow->newSong (RigDocFlow::startWords ("Catch an idea"), then, false);
            case Entry::openChooser: d.asker.files.push_back (target); return d.flow->openChooser (then);
            case Entry::openFile:    return d.flow->openFile (target, then);
            case Entry::import:      d.asker.files.push_back (stems); return d.flow->importFromLogic (then);
            case Entry::numEntries:  break;
        }
        return false;
    }

    // The show was replaced as the entry point does it.
    void expectReplaced (Doc& d, Entry entry, St st, bool thenRan, const RigDocFlow::Outcome& out, const juce::String& what)
    {
        expect (thenRan, what + ": what comes next ran");
        switch (entry)
        {
            case Entry::newSong:
            case Entry::lesson:
            case Entry::startFresh:
                expect (d.e->isBlank(), what + ": a new song");
                expect (d.host.current == juce::File(), what + ": untitled");
                expect (out.cleared == (entry == Entry::newSong || st != St::blankUntitled), what + ": cleared when it had to be");
                break;
            case Entry::openChooser:
            case Entry::openFile:
                expect (d.host.current == target, what + ": the show opened");
                expectWithinAbsoluteError (d.e->getTransport().getBpm(), 77.0, 0.001, what + ": its tempo");
                break;
            case Entry::import:
                expect (d.host.current == juce::File(), what + ": a new song");
                expect (d.e->getLooper().getState (0) != RigLooper::State::empty && d.e->getLooper().getState (1) != RigLooper::State::empty, what + ": the stems are loops");
                expect (out.said.startsWith ("Imported"), what + ": " + out.said);
                break;
            case Entry::numEntries: break;
        }
    }

    //==========================================================================
    void runMatrix()
    {
        int runs = 0, asked = 0;
        for (int en = 0; en < (int) Entry::numEntries; ++en)
            for (int s = 0; s < (int) St::numStates; ++s)
            {
                const auto entry = (Entry) en;
                const auto st = (St) s;
                const bool protect = st != St::blankUntitled && st != St::namedClean;
                for (int a = 0; a < (protect ? 3 : 1); ++a)
                {
                    const auto ans = (Choice) a;
                    const auto what = entryName (entry) + " / " + stName (st) + (protect ? juce::String (a == 0 ? " / Save" : a == 1 ? " / Don't save" : " / Cancel") : juce::String());
                    auto d = makeDoc (st);
                    playing (*d->e);
                    const auto beforeXml = xmlOf (*d->e), beforeAudio = audioOf (*d->e);
                    const auto beforeShow = d->host.current;
                    const auto beforeBpm = d->e->getTransport().getBpm();
                    bool thenRan = false;
                    RigDocFlow::Outcome out;
                    ++runs;
                    expect (startEntry (*d, entry, thenRan, out), what + ": started");
                    if (entry == Entry::openChooser || entry == Entry::import)
                        expect (d->asker.calls.size() >= 1 && d->asker.calls[0].type != FakeAsker::Type::ask, what + ": its window first, before any question");
                    expectEquals (d->asker.questions(), protect ? 1 : 0, what + ": questions");
                    if (! protect)
                    {
                        expectReplaced (*d, entry, st, thenRan, out, what);
                        expect (! d->flow->isBusy());
                        continue;
                    }
                    ++asked;
                    expect (d->flow->isBusy(), what + ": waiting for the answer");
                    expect (d->asker.lastQuestion().message.contains (beforeShow != juce::File() ? beforeShow.getFileNameWithoutExtension() : juce::String ("this song")),
                            what + ": the question names the show");
                    const bool untitled = beforeShow == juce::File();
                    const auto saveAs = newFile ("Saved as");
                    if (ans == Choice::save && untitled)
                        d->asker.files.push_back (saveAs);
                    expect (d->asker.answer (ans), what + ": answered");
                    if (ans == Choice::cancel)
                    {
                        expect (! thenRan, what + ": nothing replaced");
                        expect (d->host.current == beforeShow, what + ": the same show");
                        expect (xmlOf (*d->e) == beforeXml, what + ": the show byte for byte");
                        expect (audioOf (*d->e) == beforeAudio, what + ": its loops' sound");
                        expectWithinAbsoluteError (d->e->getTransport().getBpm(), beforeBpm, 0.0001);
                        expect (d->e->getTransport().isPlaying(), what + ": still playing");
                        expectEquals (d->flow->getStats().saves, 0);
                    }
                    else if (ans == Choice::dontSave)
                    {
                        expectEquals (d->flow->getStats().saves, 0, what + ": nothing saved");
                        expectReplaced (*d, entry, st, thenRan, out, what);
                    }
                    else
                    {
                        expectEquals (d->flow->getStats().saves, 1, what + ": saved");
                        expectEquals (d->asker.count (FakeAsker::Type::saveFile), untitled ? 1 : 0, what + ": Save As only for an untitled show");
                        const auto savedTo = untitled ? saveAs : beforeShow;
                        expect (savedTo.existsAsFile(), what + ": the file");
                        expectWithinAbsoluteError (bpmIn (savedTo), beforeBpm, 0.0001, what + ": the show as it was");
                        if (st == St::namedPending)
                        {
                            // Still can't be checked: it asks again, saying why.
                            expectEquals (d->asker.questions(), 2, what + ": asked again");
                            expect (d->asker.lastQuestion().message.contains ("changed after it was saved"), what + ": " + d->asker.lastQuestion().message);
                            expect (d->asker.answer (Choice::cancel));
                            expect (! thenRan, what + ": nothing replaced");
                            expect (d->host.current == savedTo, what + ": the saved show is open");
                        }
                        else
                        {
                            expectEquals (d->asker.questions(), 1, what + ": asked once");
                            expectReplaced (*d, entry, st, thenRan, out, what);
                            expect (out.saved == savedTo.getFileNameWithoutExtension(), what + ": says it saved");
                        }
                    }
                    expect (! d->flow->isBusy(), what + ": over");
                }
            }
        logMessage ("  matrix: " + juce::String (runs) + " runs, " + juce::String (asked) + " asked");
    }

    //==========================================================================
    void runTest() override
    {
        Reset reset;
        folder = tempFolder ("RigDocFlowTests");
        RigDocFlow::verifyTimeoutMs = 300;   // (a check that can't finish gives up sooner here)
        beginTest ("(the shows and stems the tests open)");
        {
            auto e = fresh (false);
            fill (*e, 77.0);
            target = folder.getChildFile ("Target.crateshow");
            expect (e->saveShowFile (target, rigshow::SaveKind::document).ok);
        }
        stems = folder.getChildFile ("Stems");
        stems.createDirectory();
        writeWav (stems.getChildFile ("Tune_Drums.wav"), 2.0, 0.25f);
        writeWav (stems.getChildFile ("Tune_Keys.wav"), 2.0, 0.2f);
        emptyFolder = folder.getChildFile ("No stems");
        emptyFolder.createDirectory();

        beginTest ("The dirty model is off by default");
        {
            RigEngine e;
            expect (! e.getDirty().isEnabled());
            expect (! RigView::standaloneDocuments, "the window's flow is off unless the app switches it on");
        }

        beginTest ("Every entry point x every show x every answer");
        runMatrix();

        beginTest ("A blank untitled show never asks, with the model on or off");
        for (bool model : { true, false })
        {
            Doc d;
            d.e = fresh (model);
            d.host.shows = folder;
            d.start();
            bool ran = false;
            expect (d.flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            expect (ran && d.asker.any() == 0);
        }

        beginTest ("Named and clean, the model off: it asks (nothing can say it's unchanged)");
        {
            auto d = makeDoc (St::namedModelOff);
            expect (d->flow->newSong (RigDocFlow::newSongWords(), {}));
            expectEquals (d->asker.questions(), 1);
            d->asker.answer (Choice::cancel);
        }

        beginTest ("A named blank show still asks unless it's checked clean");
        {
            // Saved blank, reopened: the model checks it clean, so no question.
            auto blank = fresh (false);
            const auto f = newFile ("Blank");
            expect (blank->saveShowFile (f, rigshow::SaveKind::document).ok);
            Doc d;
            d.e = openSaved (f);
            d.host.current = f;
            d.host.shows = folder;
            d.start();
            expect (d.flow->newSong (RigDocFlow::newSongWords(), {}));
            expectEquals (d.asker.questions(), 0, "checked clean");
            // The model off: it asks.
            Doc d2;
            d2.e = fresh (false);
            d2.host.current = f;
            d2.host.shows = folder;
            d2.start();
            expect (d2.e->isBlank());
            expect (d2.flow->newSong (RigDocFlow::newSongWords(), {}));
            expectEquals (d2.asker.questions(), 1, "named, not checked");
            d2.asker.answer (Choice::cancel);
        }

        beginTest ("Save fails: nothing replaced, it says why");
        {
            // The disk failing at the commit.
            auto d = makeDoc (St::namedDirty);
            const auto file = d->host.current;
            const auto before = file.loadFileAsString();
            const auto xml = xmlOf (*d->e);
            rigshow::hooks().at = [] (const juce::String& point, int) { return point != "commit"; };
            d->asker.choices.push_back (Choice::save);
            bool ran = false;
            expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            rigshow::hooks().at = nullptr;
            expect (! ran, "nothing replaced");
            expectEquals (d->flow->getStats().failedSaves, 1);
            expectEquals (d->asker.count (FakeAsker::Type::error), 1, "the problem is shown");
            expect (file.loadFileAsString() == before, "the file as it was");
            expect (xmlOf (*d->e) == xml, "the show as it was");
            expect (d->host.current == file && ! d->flow->isBusy());
            expect (d->host.said.joinIntoString ("\n").contains ("Nothing was replaced"), d->host.said.joinIntoString (" | "));

            // Staging the loops failing.
            auto d2 = makeDoc (St::namedDirty);
            rigshow::hooks().at = [] (const juce::String& point, int) { return point != "stage"; };
            d2->asker.choices.push_back (Choice::save);
            bool ran2 = false;
            expect (d2->flow->openFile (target, [&] (const RigDocFlow::Outcome&) { ran2 = true; }));
            rigshow::hooks().at = nullptr;
            expect (! ran2 && d2->host.current != target, "nothing opened");
            expectEquals (d2->flow->getStats().failedSaves, 1);
        }

        beginTest ("Save As cancelled: nothing replaced; untitled Save goes to Save As; a missing folder too");
        {
            auto d = makeDoc (St::untitled);
            const auto xml = xmlOf (*d->e);
            d->asker.choices.push_back (Choice::save);
            d->asker.files.push_back (juce::File());   // cancelled
            bool ran = false;
            expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            expect (! ran && xmlOf (*d->e) == xml && d->host.current == juce::File() && ! d->flow->isBusy());
            expectEquals (d->flow->getStats().saveAsCancelled, 1);
            expectEquals (d->asker.count (FakeAsker::Type::saveFile), 1, "Save As, never a name made up");
            expectEquals (folder.getNumberOfChildFiles (juce::File::findFiles, "Song *"), 0, "no auto-named file");

            // A named show whose folder is gone: Save As.
            auto gone = folder.getChildFile ("Gone");
            gone.createDirectory();
            auto d2 = makeDoc (St::namedDirty);
            d2->host.current = gone.getChildFile ("Away.crateshow");
            gone.deleteRecursively();
            const auto saveAs = newFile ("Instead");
            d2->asker.choices.push_back (Choice::save);
            d2->asker.files.push_back (saveAs);
            bool ran2 = false;
            expect (d2->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran2 = true; }));
            expectEquals (d2->asker.count (FakeAsker::Type::saveFile), 1, "Save As for a show whose folder is gone");
            expect (saveAs.existsAsFile() && ran2);

            // Save As to a name without .crateshow, where that .crateshow is there: never replaced unasked.
            auto d3 = makeDoc (St::untitled);
            const auto there = newFile ("There");
            there.replaceWithText ("keep me");
            d3->asker.choices.push_back (Choice::save);
            d3->asker.files.push_back (there.withFileExtension (""));
            bool ran3 = false;
            expect (d3->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran3 = true; }));
            expect (! ran3 && there.loadFileAsString() == "keep me", "the file it didn't ask about is left alone");
        }

        beginTest ("Resolving first: a cancelled window, an unreadable file, a folder with no stems leave the show alone");
        {
            auto check = [this] (const juce::String& what, std::function<bool (Doc&)> go)
            {
                auto d = makeDoc (St::namedDirty);
                playing (*d->e);
                const auto xml = xmlOf (*d->e), audio = audioOf (*d->e);
                const auto show = d->host.current;
                expect (go (*d), what + ": started");
                expectEquals (d->asker.questions(), 0, what + ": no question (nothing to replace with)");
                expect (xmlOf (*d->e) == xml && audioOf (*d->e) == audio && d->host.current == show && d->e->getTransport().isPlaying(), what + ": intact");
                expect (! d->flow->isBusy(), what);
            };
            check ("Open chooser cancelled", [] (Doc& d) { d.asker.files.push_back (juce::File()); return d.flow->openChooser(); });
            const auto junk = folder.getChildFile ("Junk.crateshow");
            junk.replaceWithText ("not a show");
            check ("An unreadable file", [junk] (Doc& d) { return d.flow->openFile (junk); });
            const auto other = folder.getChildFile ("Other.crateshow");
            other.replaceWithText ("<Something bpm=\"60\"/>");
            check ("XML that isn't a show", [other] (Doc& d) { return d.flow->openFile (other); });
            check ("Open chooser picks an unreadable file", [junk] (Doc& d) { d.asker.files.push_back (junk); return d.flow->openChooser(); });
            check ("Import folder cancelled", [] (Doc& d) { d.asker.files.push_back (juce::File()); return d.flow->importFromLogic(); });
            check ("Import folder with no stems", [this] (Doc& d) { d.asker.files.push_back (emptyFolder); return d.flow->importFromLogic(); });
        }

        beginTest ("Edits while the question is up, while Save As is up, and after the save");
        {
            // While the question is up, then Save: the save has the edit.
            auto d = makeDoc (St::namedDirty);
            const auto file = d->host.current;
            expect (d->flow->newSong (RigDocFlow::newSongWords(), {}));
            d->e->getTransport().setBpm (133.0);
            d->asker.answer (Choice::save);
            expectWithinAbsoluteError (bpmIn (file), 133.0, 0.001, "the edit made while it asked is saved");

            // While Save As is up: the save has it.
            auto d2 = makeDoc (St::untitled);
            const auto saveAs = newFile ("Saved during");
            expect (d2->flow->newSong (RigDocFlow::newSongWords(), {}));
            d2->asker.answer (Choice::save);
            expectEquals (d2->asker.count (FakeAsker::Type::saveFile), 1);
            d2->e->getTransport().setBpm (88.0);
            d2->e->getLooper().setLayerAudio (3, noise ((int) kRate, 9), 2.0);   // a loop recorded meanwhile
            d2->asker.pick (saveAs);
            expectWithinAbsoluteError (bpmIn (saveAs), 88.0, 0.001, "the edit made while Save As was up is saved");
            expect (d2->host.current == juce::File(), "then replaced");

            // Between the save and the replacement (after the save read the show): asked again.
            for (int kind = 0; kind < 3; ++kind)
            {
                const juce::String which = kind == 0 ? "tempo" : kind == 1 ? "the phone's tempo" : "a loop recorded";
                auto d3 = makeDoc (St::namedDirty);
                auto* e = d3->e.get();
                const auto f = d3->host.current;
                rigshow::hooks().at = [e, kind] (const juce::String& point, int)
                {
                    if (point == "afterCommit")
                    {
                        if (kind == 0) { e->getTransport().setBpm (140.0); e->getDirty().noteChange(); }
                        else if (kind == 1) e->getTransport().setBpm (141.0);   // nothing notes it
                        else e->getLooper().setLayerAudio (5, noise ((int) kRate, 21), 2.0);
                    }
                    return true;
                };
                d3->asker.choices.push_back (Choice::save);
                bool ran = false;
                expect (d3->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
                rigshow::hooks().at = nullptr;
                expectEquals (d3->flow->getStats().saves, 1, which);
                expectEquals (d3->asker.questions(), 2, which + ": asked again after the save");
                expect (d3->asker.lastQuestion().message.contains ("changed after it was saved"), which);
                expect (! ran, which + ": not replaced without a new answer");
                d3->asker.answer (Choice::cancel);
                expect (! ran && d3->host.current == f, which + ": kept");
                expect (kind == 2 ? d3->e->getLooper().getState (5) != RigLooper::State::empty
                                  : d3->e->getTransport().getBpm() > 139.0, which + ": the edit is still there");
            }
        }

        beginTest ("Recording, overdubbing, an undo just done: it asks, and asks again after a save");
        {
            auto record = [this] (const juce::String& what, std::function<void (RigEngine&, Runner&)> act, bool stillBusy)
            {
                auto d = makeDoc (St::namedClean);
                Runner r { *d->e };
                d->e->getTransport().play();
                d->e->getLooper().maintain();
                r.beats (1);
                act (*d->e, r);
                d->asker.choices.push_back (Choice::save);
                bool ran = false;
                expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
                expectGreaterOrEqual (d->asker.questions(), 1, what + ": asks");
                expectEquals (d->flow->getStats().saves, 1, what + ": saved");
                if (stillBusy)
                {
                    expectEquals (d->asker.questions(), 2, what + ": asks again (still recording)");
                    expect (d->asker.lastQuestion().message.contains ("recording"), d->asker.lastQuestion().message);
                    expect (! ran);
                    d->asker.answer (Choice::cancel);
                }
                expect (! ran || ! stillBusy, what);
            };
            record ("Recording a new loop", [this] (RigEngine& e, Runner& r)
                    {
                        auto& layer = e.getLooper().getLayer (6);
                        layer.source = RigLooper::input;
                        layer.bars = 0;
                        layer.countIn = 0;
                        r.input = true;
                        e.getLooper().pressRecord (6);
                        r.beats (4.5);
                        expect (e.getLooper().getState (6) == RigLooper::State::recording, "(recording)");
                    }, true);
            record ("Overdubbing", [this] (RigEngine& e, Runner& r)
                    {
                        r.input = true;
                        e.getLooper().pressRecord (0);
                        r.beats (4.5);
                        expect (e.getLooper().getState (0) == RigLooper::State::overdubbing, "(overdubbing)");
                    }, true);
            // An undo just done: its sound isn't checked until the audio has moved on, so it asks.
            {
                auto d = makeDoc (St::namedClean);
                Runner r { *d->e };
                d->e->getTransport().play();
                d->e->getLooper().maintain();
                r.beats (1);
                r.input = true;
                d->e->getLooper().pressRecord (0);
                r.beats (4.5);
                d->e->getLooper().pressRecord (0);
                r.input = false;
                r.beats (0.5);
                d->e->getLooper().undo (0);
                juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
                in.clear();
                d->e->process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
                expect (d->flow->newSong (RigDocFlow::newSongWords(), {}));
                expectEquals (d->asker.questions(), 1, "an undo just done asks (not checked yet)");
                d->asker.answer (Choice::cancel);
            }
        }

        beginTest ("A show's plugin still loading");
        runPluginTests();

        beginTest ("One at a time: a second request, a double press, stale answers");
        {
            auto d = makeDoc (St::namedDirty);
            expect (d->flow->newSong (RigDocFlow::newSongWords(), {}));
            expect (! d->flow->newSong (RigDocFlow::newSongWords(), {}), "a double press");
            expect (! d->flow->openFile (target), "another while the question is up");
            expect (! d->flow->importFromLogic(), "and Import");
            expect (! d->flow->save(), "Cmd+S while it asks");
            expectEquals (d->asker.questions(), 1, "one question");
            expectEquals (d->flow->getStats().ignored, 4);
            d->asker.answer (Choice::cancel);

            // While a window is up.
            auto d2 = makeDoc (St::namedDirty);
            expect (d2->flow->openChooser());
            expect (! d2->flow->openChooser() && ! d2->flow->newSong (RigDocFlow::newSongWords(), {}));
            expectEquals (d2->asker.count (FakeAsker::Type::openFile), 1);
            d2->asker.pick (juce::File());

            // The answer arrives after another show was opened (the window said so: abandon).
            auto d3 = makeDoc (St::namedDirty);
            bool ran = false;
            expect (d3->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            auto held = std::move (d3->asker.heldAsk);
            d3->flow->abandon();
            d3->host.current = target;
            held (Choice::dontSave);
            expect (! ran && d3->host.current == target, "stale after abandon");
            expectEquals (d3->flow->getStats().stale, 1);

            // ... another show opened without telling the flow: the show it asked about is gone.
            auto d4 = makeDoc (St::namedDirty);
            bool ran4 = false;
            expect (d4->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran4 = true; }));
            d4->host.current = target;
            d4->asker.answer (Choice::dontSave);
            expect (! ran4 && ! d4->flow->isBusy(), "stale: not the show it asked about");

            // Superseded: the old answer arrives after a new request.
            auto d5 = makeDoc (St::namedDirty);
            int first = 0, second = 0;
            expect (d5->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ++first; }));
            auto old = std::move (d5->asker.heldAsk);
            d5->flow->abandon();
            expect (d5->flow->newSong (RigDocFlow::lessonWords(), [&] (const RigDocFlow::Outcome&) { ++second; }, false));
            old (Choice::dontSave);
            expect (first == 0 && second == 0 && d5->flow->isBusy(), "the old answer does nothing; the new question waits");
            d5->asker.answer (Choice::dontSave);
            expect (first == 0 && second == 1);

            // After the flow (the window) is gone.
            auto d6 = makeDoc (St::namedDirty);
            expect (d6->flow->newSong (RigDocFlow::newSongWords(), {}));
            auto orphan = std::move (d6->asker.heldAsk);
            const auto xml = xmlOf (*d6->e);
            d6->flow.reset();
            orphan (Choice::dontSave);
            expect (xmlOf (*d6->e) == xml, "nothing happened after the flow was gone");
            auto d7 = makeDoc (St::untitled);
            expect (d7->flow->newSong (RigDocFlow::newSongWords(), {}));
            d7->asker.answer (Choice::save);
            auto orphanFile = std::move (d7->asker.heldFile);
            d7->flow.reset();
            orphanFile (newFile ("Never"));
            expect (! folder.getChildFile (juce::String (fileCount) + " Never.crateshow").existsAsFile(), "no save after the flow was gone");
        }

        beginTest ("Cmd+S: named saves as the document; untitled is Save As; cancelled changes nothing");
        {
            auto d = makeDoc (St::namedDirty);
            expect (d->flow->save());
            expectEquals (d->flow->getStats().saves, 1);
            expectEquals (d->asker.any(), 0, "no window for a named show");
            expectWithinAbsoluteError (bpmIn (d->host.current), 131.0, 0.001);
            expect (settle (*d->e).state == DS::clean, "the model reads clean after it");
            expect (! d->flow->isBusy());

            auto d2 = makeDoc (St::untitled);
            const auto xml = xmlOf (*d2->e);
            expect (d2->flow->save());
            expectEquals (d2->asker.count (FakeAsker::Type::saveFile), 1, "Save As");
            d2->asker.pick (juce::File());
            expect (d2->host.current == juce::File() && xmlOf (*d2->e) == xml && ! d2->flow->isBusy(), "cancelled: nothing changes");
            const auto as = newFile ("Cmd S");
            expect (d2->flow->save());
            d2->asker.pick (as);
            expect (as.existsAsFile() && d2->host.current == as, "saved and named");
            expect (settle (*d2->e).state == DS::clean);

            // Save show as... on a named show: always the window.
            auto d3 = makeDoc (St::namedClean);
            expect (d3->flow->saveAs());
            expectEquals (d3->asker.count (FakeAsker::Type::saveFile), 1);
            d3->asker.pick (juce::File());

            // While a replacement is under way.
            auto d4 = makeDoc (St::namedDirty);
            expect (d4->flow->openChooser());
            expect (! d4->flow->save() && ! d4->flow->saveAs(), "Cmd+S does nothing while Open's window is up");
            d4->asker.pick (juce::File());
        }

        beginTest ("Negative control: the cached state loses an edit nothing noted");
        {
            for (bool cached : { true, false })
            {
                auto d = makeDoc (St::namedClean);
                expect (settle (*d->e).state == DS::clean);
                d->e->getTransport().setBpm (127.0);   // the phone's tempo: nothing notes it
                RigDocFlow::cachedStateForTests = cached;
                bool ran = false;
                expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
                RigDocFlow::cachedStateForTests = false;
                if (cached)
                    expect (ran && d->asker.questions() == 0, "the cached state reads clean: the edit is lost (the old fault)");
                else
                {
                    expect (! ran && d->asker.questions() == 1, "the check finds the edit and asks");
                    d->asker.answer (Choice::cancel);
                }
            }
        }

        beginTest ("The window: the standalone entry points go through the flow");
        runViewTests();

        beginTest ("Logic and the one-tool plugins: the old way, never the flow");
        runHostedTests();

        beginTest ("The Drone's On switch never makes a show unsaved");
        runDroneTests();

        folder.deleteRecursively();
    }

    //==========================================================================
    std::unique_ptr<juce::SharedResourcePointer<PluginHost>> hostRef;
    FakeFormat* format = nullptr;

    void runPluginTests()
    {
        hostRef = std::make_unique<juce::SharedResourcePointer<PluginHost>>();
        for (auto* f : hostRef->getObject().formats.getFormats())
            if (auto* fake = dynamic_cast<FakeFormat*> (f))
                format = fake;
        if (format == nullptr)
        {
            format = new FakeFormat();
            hostRef->getObject().formats.addFormat (std::unique_ptr<juce::AudioPluginFormat> (format));
        }
        auto reset = [this]
        {
            format->hold = false;
            format->finishAll();
            format->missing.clear();
            format->failing.clear();
            pump (20);
        };
        reset();

        auto makeShow = [this] (const juce::String& label)
        {
            auto e = fresh (false);
            e->getSynthPlugin().load (FakeFormat::describe ("fake:A"));
            pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
            e->getLooper().setLayerAudio (0, noise ((int) kRate, 3), 2.0);
            const auto f = newFile (label);
            expect (e->saveShowFile (f, rigshow::SaveKind::document).ok);
            return f;
        };
        auto openHeld = [this] (const juce::File& f)
        {
            format->hold = true;
            auto d = std::make_unique<Doc>();
            d->host.shows = folder;
            d->host.current = f;
            d->e = openSaved (f);
            pump (20);
            d->start();
            return d;
        };
        auto hasPlugin = [] (const juce::File& f)
        {
            auto xml = juce::parseXML (f);
            if (xml == nullptr) return false;
            for (auto* inst : xml->getChildWithTagNameIterator ("Instrument"))
                if (inst->getStringAttribute ("tool") == "Synth" && inst->getChildByName ("Plugin") != nullptr)
                    return true;
            return false;
        };

        RigDocFlow::verifyTimeoutMs = 2000;
        {
            const auto f = makeShow ("Plugin");
            auto d = openHeld (f);
            expect (! d->e->getSynthPlugin().isLoaded() && format->waiting() == 1, "(still loading)");
            bool ran = false;
            expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            expect (ran && d->asker.questions() == 0, "still loading, checked clean: no question");
            reset();
        }
        {
            const auto f = makeShow ("Plugin edit");
            auto d = openHeld (f);
            d->e->getTransport().setBpm (99.0);
            d->asker.choices.push_back (Choice::save);
            bool ran = false;
            expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            expectEquals (d->asker.questions(), 1, "an edit while it loads: asks");
            expect (ran, "saved, then replaced");
            expect (hasPlugin (f), "the save while it loaded keeps the plugin");
            expectWithinAbsoluteError (bpmIn (f), 99.0, 0.001);
            reset();
        }
        {
            const auto f = makeShow ("Plugin missing");
            format->missing.add ("fake:A");
            auto d = openHeld (f);
            format->finishAll();
            pump (30);
            expect (! d->e->getSynthPlugin().isLoaded(), "(not installed here)");
            bool ran = false;
            expect (d->flow->newSong (RigDocFlow::newSongWords(), [&] (const RigDocFlow::Outcome&) { ran = true; }));
            expect (ran && d->asker.questions() == 0, "a missing plugin: clean, no question");
            reset();
        }
        RigDocFlow::verifyTimeoutMs = 300;
        hostRef.reset();
        format = nullptr;
    }

    //==========================================================================
    void runViewTests()
    {
        RigView::standaloneDocuments = true;
        auto viewOn = [] (RigEngine& e)
        {
            auto v = std::make_unique<RigView> (e, nullptr);
            v->setSize (1440, 960);
            return v;
        };
        {
            auto e = fresh (false);
            fill (*e, 101.0);
            FakeAsker asker;
            auto v = viewOn (*e);
            v->setAskerForTests (&asker);
            expect (! v->hostedDocuments());

            v->askNewSong();
            expectEquals (asker.lastQuestion().title, juce::String ("Start a new song?"));
            expect (asker.lastQuestion().message.contains ("this song"));
            asker.answer (Choice::cancel);
            expect (! e->isBlank(), "cancelled");

            v->startLesson();
            expectEquals (asker.lastQuestion().title, juce::String ("Make a beat"));
            asker.answer (Choice::cancel);

            v->catchIdea();
            expectEquals (asker.lastQuestion().title, juce::String ("Catch an idea"));
            asker.answer (Choice::cancel);

            v->startWithTile (0);
            expectEquals (asker.lastQuestion().title, juce::String ("Start with A beat"));
            asker.answer (Choice::cancel);

            v->askOpenSong (target);
            expectEquals (asker.lastQuestion().title, juce::String ("Open Target?"));
            asker.answer (Choice::cancel);

            v->openShow();
            expectEquals (asker.count (FakeAsker::Type::openFile), 1, "Open show: the window first");
            asker.pick (juce::File());

            v->askImportFromLogic();
            expectEquals (asker.count (FakeAsker::Type::folder), 1, "Import: the folder first");
            asker.pick (juce::File());

            expectEquals (asker.questions(), 5);
            expect (! e->isBlank() && v->getCurrentShow() == juce::File(), "all cancelled: the show is as it was");

            // Cmd+S, untitled: Save As.
            expect (v->keyPressed (juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)));
            expectEquals (asker.count (FakeAsker::Type::saveFile), 1, "Cmd+S on an untitled show: Save As");
            const auto as = newFile ("View save");
            asker.pick (as);
            expect (as.existsAsFile() && v->getCurrentShow() == as);

            // Open an idea, Don't save: opened, "Opened ...".
            fill (*e, 105.0);
            v->askOpenSong (target);
            asker.answer (Choice::dontSave);
            expect (v->getCurrentShow() == target);
            expectWithinAbsoluteError (e->getTransport().getBpm(), 77.0, 0.001);

            // The lesson, Don't save: a new song, then the lesson.
            v->startLesson();
            asker.answer (Choice::dontSave);
            expect (v->getCurrentShow() == juce::File(), "a new song");
            expect (v->getLesson() != nullptr, "the lesson started (on a new song)");
            v->endLesson();
            v->setAskerForTests (nullptr);
        }
        {
            // A one-tool plugin's window, even with the app's switch on: the old way.
            auto e = fresh (false);
            e->setOnlyTool (RigEngine::bassStrip);
            auto v = viewOn (*e);
            expect (v->hostedDocuments(), "a one-tool plugin is hosted");
        }
        RigView::standaloneDocuments = false;
    }

    void runHostedTests()
    {
        expect (! RigView::standaloneDocuments);
        auto e = fresh (false);
        FakeAsker asker;
        auto v = std::make_unique<RigView> (*e, nullptr);
        v->setSize (1440, 960);
        v->setAskerForTests (&asker);
        expect (v->hostedDocuments());
        // A blank Rig, as on main: no question, straight on.
        v->startLesson();
        expect (v->getLesson() != nullptr, "the lesson");
        v->endLesson();
        {
            auto e3 = fresh (false);
            RigView v3 (*e3, nullptr);
            v3.setSize (1440, 960);
            v3.setAskerForTests (&asker);
            v3.catchIdea();
            expectEquals (v3.getTakeLayer(), 0, "Catch an idea records straight away");
            v3.setAskerForTests (nullptr);
        }
        RigEngine e2;
        e2.prepare (kRate, kBlock);
        RigView v2 (e2, nullptr);
        v2.setSize (1440, 960);
        v2.setAskerForTests (&asker);
        v2.openShowFile (target);
        expect (v2.getCurrentShow() == target, "opened (the old way)");
        e2.getTransport().setBpm (66.0);
        v2.saveShow();
        expectWithinAbsoluteError (bpmIn (target), 66.0, 0.001, "Save show to its file, the old way");
        v2.askOpenSong (target, [] {});
        expectEquals (asker.any(), 0, "the flow never asked anything in the hosted window");
        v2.setAskerForTests (nullptr);
        v->setAskerForTests (nullptr);
        // (Put Target back as it was for the other tests.)
        e2.getTransport().setBpm (77.0);
        expect (e2.saveShowFile (target, rigshow::SaveKind::document).ok);
    }

    //==========================================================================
    void runDroneTests()
    {
        auto setParam = [] (RigEngine& e, const char* id, float v)
        {
            if (auto* p = e.getDrone().params.getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        const auto named = makeNamed ("Drone");
        {
            auto e = openSaved (named);
            expect (settle (*e).state == DS::clean);
            setParam (*e, "droneOn", 1.0f);
            expect (e->getDrone().isOn());
            expect (settle (*e).state == DS::clean, "switched on after opening: clean");
            setParam (*e, "droneOn", 0.0f);
            expect (settle (*e).state == DS::clean, "and off again");
            setParam (*e, "droneRoot", 5.0f);
            expect (settle (*e).state == DS::dirty, "a Drone setting: unsaved");
        }
        {
            // Saved with it on: the file keeps it; reopened (it opens off): clean.
            auto e = fresh (false);
            fill (*e, 90.0);
            setParam (*e, "droneOn", 1.0f);
            const auto f = newFile ("Drone on");
            expect (e->saveShowFile (f, rigshow::SaveKind::document).ok);
            auto again = openSaved (f);
            expect (! again->getDrone().isOn(), "(a show opens with the Drone off)");
            expect (settle (*again).state == DS::clean, "saved with the Drone on, reopened: clean");
            // The saved file is unchanged by this: its On is still in it.
            const auto flat = RigDirty::flattenForTests (juce::ValueTree::fromXml (*juce::parseXML (f)));
            const auto it = flat.find ("Show/Tool[name=Drone]@state{xml}/PARAM[id=droneOn]@value");
            expect (it != flat.end() && juce::String (it->second).getFloatValue() > 0.5f, "the file still has the Drone on");
            // Saved with it on, as a document, while on: clean.
            auto e3 = openSaved (f);
            setParam (*e3, "droneOn", 1.0f);
            expect (e3->saveShowFile (f, rigshow::SaveKind::document).ok);
            expect (settle (*e3).state == DS::clean);
            setParam (*e3, "droneOn", 0.0f);
            expect (settle (*e3).state == DS::clean, "switched off after that save: clean");
        }
    }
};

static RigDocFlowTests rigDocFlowTests;
