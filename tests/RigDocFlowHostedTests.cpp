// The Logic / hosted boundaries of the standalone document flow (RigDocFlow): in the Logic
// plugins and the one-tool plugins the old confirmation runs exactly as it did (its words, its
// buttons, what each answer does), the new flow never runs (RigDocFlow::requestsForTests stays put),
// opening and closing the editor asks nothing, the host's state round trip is untouched, Cmd+S is
// the old Save, and the dirty model stays off.
//
// The old questions are seen headless through RigView::legacyQuestionForTests (it gets the
// AlertWindow's options and its callback instead of the AlertWindow; nothing else changes).

#include "RigDirtyTestHelpers.h"
#include "RigDocFlow.h"
#include "RigPlugin.h"

using namespace dirtytest;

namespace
{
struct Asked
{
    juce::String title, message;
    juce::StringArray buttons;
};

// Holds the old way's question until the test answers it (1, 2, or 0 for Cancel).
struct LegacyQuestions
{
    std::vector<Asked> asked;
    std::function<void (int)> held;
    LegacyQuestions()
    {
        RigView::legacyQuestionForTests = [this] (const juce::MessageBoxOptions& o, std::function<void (int)> done)
        {
            Asked a { o.getTitle(), o.getMessage(), {} };
            for (int i = 0; i < o.getNumButtons(); ++i)
                a.buttons.add (o.getButtonText (i));
            asked.push_back (a);
            held = std::move (done);
        };
    }
    ~LegacyQuestions() { RigView::legacyQuestionForTests = nullptr; }
    void answer (int result)
    {
        auto cb = std::move (held);
        held = nullptr;
        if (cb) cb (result);
    }
};

juce::String xmlOf (RigEngine& e)
{
    auto t = e.saveShow();
    t.removeChild (t.getChildWithName ("Screens"), nullptr);
    auto xml = t.createXml();
    return xml != nullptr ? xml->toString (juce::XmlElement::TextFormat().singleLine()) : juce::String();
}

double bpmIn (const juce::File& show)
{
    auto xml = juce::parseXML (show);
    return xml != nullptr ? xml->getDoubleAttribute ("bpm", -1) : -1;
}

// The host's state as XML, without the loops' revision (each save makes a new one).
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
} // namespace

struct RigDocFlowHostedTests final : juce::UnitTest
{
    RigDocFlowHostedTests() : juce::UnitTest ("Rig document flow: Logic and the one-tool plugins", "Rig") {}

    juce::File folder;
    int fileCount = 0;
    juce::File newFile (const juce::String& label) { return folder.getChildFile (juce::String (++fileCount) + " " + label + ".crateshow"); }

    juce::File makeShow (const juce::String& label, double bpm)
    {
        RigEngine e;
        e.prepare (kRate, kBlock);
        e.getTransport().setBpm (bpm);
        e.getLooper().setLayerAudio (0, noise ((int) kRate, 4), 2.0);
        const auto f = newFile (label);
        expect (e.saveShowFile (f, rigshow::SaveKind::document).ok, "made " + label);
        return f;
    }

    // A hosted window on a show you've opened and changed.
    struct Hosted
    {
        std::unique_ptr<RigEngine> e;
        std::unique_ptr<RigView> v;
        juce::File file;
    };
    Hosted onChangedShow (const juce::String& label)
    {
        Hosted h;
        h.file = makeShow (label, 97.0);
        h.e = std::make_unique<RigEngine>();
        h.e->prepare (kRate, kBlock);
        h.v = std::make_unique<RigView> (*h.e, nullptr);
        h.v->setSize (1440, 960);
        h.v->openShowFile (h.file);
        h.e->getTransport().setBpm (123.0);
        expect (h.v->hostedDocuments() && ! h.e->isBlank() && h.v->getCurrentShow() == h.file);
        return h;
    }

    void expectAsked (const Asked& a, const juce::String& title, const juce::String& message, const juce::StringArray& buttons)
    {
        expectEquals (a.title, title);
        expectEquals (a.message, message);
        expectEquals (a.buttons.joinIntoString ("|"), buttons.joinIntoString ("|"));
    }

    void runTest() override
    {
        beginTest ("(shows)");
        RigView::standaloneDocuments = false;
        folder = tempFolder ("RigDocFlowHostedTests");
        const auto target = makeShow ("Target", 77.0);
        const int requests0 = RigDocFlow::requestsForTests.load();

        beginTest ("The old confirmation, word for word, and what each answer does");
        {
            LegacyQuestions q;
            // New song: Cancel keeps everything. (Its other answers open the New pattern menu,
            // which can't open headless: the same newSong is answered through the ways in below.)
            for (int answer : { 0 })
            {
                auto h = onChangedShow ("New");
                const auto xml = xmlOf (*h.e);
                h.v->askNewSong();
                expectEquals ((int) q.asked.size(), 1, "one question");
                expectAsked (q.asked.back(), "Start a new song?", "Kaleidobox clears every tool, loop and song part for a new song. Save " + h.file.getFileNameWithoutExtension() + " first?",
                             { "Save, then new song", "New song, don't save", "Cancel" });
                q.answer (answer);
                if (answer == 0)
                    expect (xmlOf (*h.e) == xml && h.v->getCurrentShow() == h.file, "Cancel: unchanged");
                else
                    expect (h.e->isBlank() && h.v->getCurrentShow() == juce::File(), "a new song");
                expectWithinAbsoluteError (bpmIn (h.file), answer == 1 ? 123.0 : 97.0, 0.001, answer == 1 ? "saved to its file" : "not saved");
                q.asked.clear();
            }
            // The lesson (newSong without the menu): Cancel, Don't save, Save.
            for (int answer : { 0, 2, 1 })
            {
                auto h = onChangedShow ("Lesson");
                const auto xml = xmlOf (*h.e);
                h.v->startLesson();
                expectAsked (q.asked.back(), "Make a beat", "The lesson starts a new song, so " + h.file.getFileNameWithoutExtension() + " stays as it is. Save it first?",
                             { "Save, then the lesson", "Lesson, don't save", "Cancel" });
                q.answer (answer);
                if (answer == 0)
                    expect (xmlOf (*h.e) == xml && h.v->getLesson() == nullptr, "Cancel: unchanged, no lesson");
                else
                {
                    expect (h.v->getLesson() != nullptr && h.v->getCurrentShow() == juce::File(), "a new song and the lesson");
                    h.v->endLesson();
                }
                expectWithinAbsoluteError (bpmIn (h.file), answer == 1 ? 123.0 : 97.0, 0.001, answer == 1 ? "saved to its file" : "not saved");
                q.asked.clear();
            }
            // An idea.
            for (int answer : { 0, 2 })
            {
                auto h = onChangedShow ("Idea");
                h.v->askOpenSong (target);
                expectAsked (q.asked.back(), "Open " + target.getFileNameWithoutExtension() + "?", "Save " + h.file.getFileNameWithoutExtension() + " first?",
                             { "Save, then open", "Open, don't save", "Cancel" });
                q.answer (answer);
                expect (h.v->getCurrentShow() == (answer == 2 ? target : h.file));
                expectWithinAbsoluteError (h.e->getTransport().getBpm(), answer == 2 ? 77.0 : 123.0, 0.001);
                q.asked.clear();
            }
            // Catch an idea (the ways to start).
            for (int answer : { 0, 2, 1 })
            {
                auto h = onChangedShow ("Catch");
                h.v->catchIdea();
                expectAsked (q.asked.back(), "Catch an idea", "This starts a new song. Save " + h.file.getFileNameWithoutExtension() + " first?",
                             { "Save, then start", "Start, don't save", "Cancel" });
                q.answer (answer);
                expectEquals (h.v->getTakeLayer(), answer != 0 ? 0 : -1, "recording only after an answer");
                expectWithinAbsoluteError (bpmIn (h.file), answer == 1 ? 123.0 : 97.0, 0.001);
                q.asked.clear();
            }
            // Import from Logic (its folder window comes after the question; Cancel only, headless).
            {
                auto h = onChangedShow ("Import");
                const auto xml = xmlOf (*h.e);
                h.v->askImportFromLogic();
                expectAsked (q.asked.back(), "Import a song from Logic",
                             "The Logic song comes in as a new song (its stems as loops, its markers as sections). Save " + h.file.getFileNameWithoutExtension() + " first?",
                             { "Save, then import", "Import, don't save", "Cancel" });
                q.answer (0);
                expect (xmlOf (*h.e) == xml, "Cancel: unchanged");
            }
            expectEquals (RigDocFlow::requestsForTests.load(), requests0, "the new flow never ran");
        }

        beginTest ("Cmd+S in a hosted window: the old Save, the key handled as before");
        {
            LegacyQuestions q;
            auto h = onChangedShow ("Cmd S");
            expect (h.v->keyPressed (juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)), "the key is taken, as on main");
            expectWithinAbsoluteError (bpmIn (h.file), 123.0, 0.001, "saved to its file");
            expect (q.asked.empty());
            expectEquals (RigDocFlow::requestsForTests.load(), requests0, "not the flow's Save");
        }

        beginTest ("The plugins: editor open, close, reopen; the host's state round trip; the model off");
        for (int tool : { -1, (int) RigEngine::bassStrip })
        {
            LegacyQuestions q;
            const juce::String which = tool < 0 ? "Kaleidobox" : "Kaleidobox Bass";
            RigPlugin p (tool);
            p.prepareToPlay (kRate, kBlock);
            expect (! p.getEngine().getDirty().isEnabled(), which + ": the dirty model is off");
            p.getEngine().getTransport().setBpm (111.0);   // a change nothing has saved
            const auto before = xmlOf (p.getEngine());
            juce::MemoryBlock s0;
            p.getStateInformation (s0);
            for (int cycle = 0; cycle < 3; ++cycle)
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
                editor->setSize (1440, 960);
                auto* view = dynamic_cast<RigView*> (editor->getChildComponent (0));
                expect (view != nullptr && view->hostedDocuments(), which + ": hosted");
                editor.reset();
            }
            expect (xmlOf (p.getEngine()) == before, which + ": the show unchanged by the editor");
            juce::MemoryBlock s1;
            p.getStateInformation (s1);
            expect (stateXml (s0).contains ("bpm=\"111"), which + ": (the state has the show)");
            expectEquals (stateXml (s1), stateXml (s0), which + ": the host's state the same");
            RigPlugin p2 (tool);
            p2.prepareToPlay (kRate, kBlock);
            p2.setStateInformation (s1.getData(), (int) s1.getSize());
            expectWithinAbsoluteError (p2.getEngine().getTransport().getBpm(), 111.0, 0.001, which + ": restored");
            expect (! p2.getEngine().getDirty().isEnabled());
            {
                std::unique_ptr<juce::AudioProcessorEditor> editor (p2.createEditor());
                editor->setSize (1440, 960);
            }
            juce::MemoryBlock s2;
            p2.getStateInformation (s2);
            expectEquals (stateXml (s2), stateXml (s1), which + ": round trip");
            // An untitled blank one too.
            RigPlugin blank (tool);
            juce::MemoryBlock b0, b1;
            blank.getStateInformation (b0);
            { std::unique_ptr<juce::AudioProcessorEditor> editor (blank.createEditor()); }
            blank.getStateInformation (b1);
            expectEquals (stateXml (b1), stateXml (b0), which + ": blank, the same");
            expect (q.asked.empty(), which + ": no question");
            expectEquals (RigDocFlow::requestsForTests.load(), requests0, which + ": the flow never ran");
        }
        folder.deleteRecursively();
    }
};

static RigDocFlowHostedTests rigDocFlowHostedTests;
