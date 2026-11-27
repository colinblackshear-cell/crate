// A show's hosted plugins (an Arturia synth on the Synth, an effect at the end of a strip's or a
// loop's effects, the plugin in a saved tool sound, the Kaleidobox plugin's state in Logic): their
// settings come back when the show opens, and a save made while a plugin isn't in (still loading,
// not installed here, didn't open, left closed after a freeze) keeps it exactly as the show had it.
//
// A fake plugin format in PluginHost::formats makes the "plugins" asynchronously, as every real
// format does (AudioPluginFormat::createPluginInstanceAsync posts a message), and can hold each
// load until the test lets it finish, fail it, or finish loads out of order. No plugin binaries.
//
// "Rig plugin persistence (real plugin)" does the main cases with a real plugin: the one named by
// KALEIDOBOX_TEST_PLUGIN ("AudioUnit:Synths/aumu,<sub>,<maker>", "VST3:/path/X.vst3" or a .vst3
// path), or this repo's own Sample Editor Instrument VST3 when the build made it (CI builds it on
// the Mac; the Linux job builds only the tests, so it says "skipped" there).
#include <juce_audio_processors/juce_audio_processors.h>

#include "RigEngine.h"
#include "RigLastShow.h"
#include "RigPlugin.h"
#include "PluginSlot.h"
#include "RigShowFiles.h"
#include "core/AudioFiles.h"

#include <cmath>

#include "RigPluginFakes.h"

using namespace pluginfakes;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;


//==============================================================================
juce::ValueTree readShow (const juce::File& f)
{
    auto xml = juce::parseXML (f);
    return xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
}

juce::String xmlOf (const juce::ValueTree& t)
{
    if (! t.isValid())
        return "(none)";
    auto xml = t.createXml();
    return xml != nullptr ? xml->toString (juce::XmlElement::TextFormat().singleLine()) : juce::String ("(unwritable)");
}

juce::ValueTree instrumentOf (const juce::ValueTree& show, const char* tool = "Synth")
{
    return show.getChildWithProperty ("tool", tool).getChildWithName ("Plugin");
}

juce::String instrumentXml (const juce::ValueTree& show, const char* tool = "Synth") { return xmlOf (instrumentOf (show, tool)); }

// The plugin inside a binary StripFx state (a strip's "FX ..." tool, or a loop's "fx").
juce::String rackPluginXml (const juce::String& base64)
{
    juce::MemoryBlock m;
    if (! m.fromBase64Encoding (base64) || m.isEmpty())
        return "(none)";
    auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
    if (xml == nullptr)
        return "(none)";
    auto* p = xml->getChildByName ("Plugin");
    return p != nullptr ? p->toString (juce::XmlElement::TextFormat().singleLine()) : juce::String ("(none)");
}

juce::String stripPluginXml (const juce::ValueTree& show, int strip = RigEngine::synthStrip)
{
    const auto t = show.getChildWithProperty ("name", RigEngine::toolName (RigEngine::fxTool + strip));
    return rackPluginXml (t.getProperty ("state").toString());
}

juce::String loopPluginXml (const juce::ValueTree& show, int layer = 0)
{
    for (const auto c : show)
        if (c.hasType ("Loop") && (int) c.getProperty ("index", -1) == layer)
            return rackPluginXml (c.getProperty ("fx").toString());
    return "(none)";
}

juce::MemoryBlock stateBytes (const juce::ValueTree& plugin)
{
    juce::MemoryBlock m;
    m.fromBase64Encoding (plugin.getProperty ("state").toString());
    return m;
}

juce::String macrosOf (const PluginSlot& s)
{
    juce::StringArray k;
    for (int i = 0; i < PluginSlot::kMacros; ++i)
        k.add (juce::String (s.getMacro (i)));
    return k.joinIntoString (" ");
}

// Note 60 at the start, off half way, through the slot (what the Synth track hears).
std::vector<float> render (PluginSlot& s, int blocks = 40)
{
    std::vector<float> out;
    juce::AudioBuffer<float> b (2, kBlock);
    for (int i = 0; i < blocks; ++i)
    {
        b.clear();
        juce::MidiBuffer m;
        if (i == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 17);
        if (i == blocks / 2) m.addEvent (juce::MidiMessage::noteOff (1, 60), 3);
        s.process (b, m);
        for (int c = 0; c < 2; ++c)
            for (int j = 0; j < kBlock; ++j)
                out.push_back (b.getSample (c, j));
    }
    return out;
}

double maxDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return 1.0e9;
    double d = 0;
    for (size_t i = 0; i < a.size(); ++i)
        d = juce::jmax (d, (double) std::abs (a[i] - b[i]));
    return d;
}

double rms (const std::vector<float>& v)
{
    double s = 0;
    for (auto x : v)
        s += (double) x * x;
    return v.empty() ? 0.0 : std::sqrt (s / (double) v.size());
}

std::unique_ptr<RigEngine> freshEngine()
{
    auto e = std::make_unique<RigEngine>();   // (big: on the heap)
    e->prepare (kRate, kBlock);
    return e;
}

// Opens a saved show as RigView::openShowFile does (the show, then its loops).
std::unique_ptr<RigEngine> openShow (const juce::File& f)
{
    auto e = freshEngine();
    e->loadShow (readShow (f));
    e->readLoopAudio (f);
    return e;
}

juce::File openingNote()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Crate").getChildFile ("Rig Plugin Opening.txt");
}

//==============================================================================
struct RigPluginPersistTests final : juce::UnitTest
{
    RigPluginPersistTests() : juce::UnitTest ("Rig plugin persistence", "Rig") {}

    std::unique_ptr<juce::SharedResourcePointer<PluginHost>> hostRef;
    FakeFormat* format = nullptr;
    juce::File folder;
    int fileCount = 0;

    PluginHost& host() { return hostRef->getObject(); }
    void attachHost()
    {
        hostRef = std::make_unique<juce::SharedResourcePointer<PluginHost>>();
        format = nullptr;
        for (auto* f : host().formats.getFormats())
            if (auto* fake = dynamic_cast<FakeFormat*> (f))
                format = fake;
        if (format == nullptr)
        {
            format = new FakeFormat();
            host().formats.addFormat (std::unique_ptr<juce::AudioPluginFormat> (format));
        }
    }
    void detachHost()
    {
        format = nullptr;
        hostRef.reset();
    }
    void resetFakes()
    {
        format->hold = false;
        format->finishAll();
        format->missing.clear();
        format->failing.clear();
        FakeSound::throwOnGetState = FakeSound::emptyState = FakeSound::throwOnSetState = false;
        FakeSound::resetOptions();
        rigshow::hooks().at = nullptr;
        pump (20);
    }

    juce::File newFile (const juce::String& label)
    {
        return folder.getChildFile (juce::String (++fileCount) + " " + label + ".crateshow");
    }
    rigshow::Result saveTo (RigEngine& e, const juce::File& f, rigshow::SaveKind kind = rigshow::SaveKind::document)
    {
        return e.saveShowFile (f, kind);
    }

    static FakeSound* fake (PluginSlot& s) { return dynamic_cast<FakeSound*> (s.getInstance()); }

    // A saved show with fake:<name> on the Synth: patch "Lead 7", its own level 0.42, Cutoff 0.83,
    // Drive 0.27, knob 3 on Drive (the rest off), the slot at level 0.6, as a document on disk.
    struct Made
    {
        juce::File file;
        juce::ValueTree show;
        juce::String pluginXml, loadError;
        juce::MemoryBlock state;
        std::vector<float> audio;
    };
    Made makeShow (const juce::String& id = "fake:A", const juce::String& patch = "Lead 7", bool bypass = false)
    {
        Made m;
        auto e = freshEngine();
        auto& s = e->getSynthPlugin();
        bool called = false;
        m.loadError = "(not called)";
        s.load (FakeFormat::describe (id), [&] (const juce::String& x) { m.loadError = x; called = true; });
        pumpUntil ([&] { return called; });
        if (auto* f = fake (s))
        {
            f->level = 0.42f;
            f->patch = patch;
            f->cutoff->value = 0.83f;
            f->drive->value = 0.27f;
        }
        for (int k = 0; k < PluginSlot::kMacros; ++k)
            s.setMacro (k, -1);
        s.setMacro (3, 1);
        s.level = 0.6f;
        m.audio = render (s);
        s.bypassed = bypass;
        m.file = newFile (id.fromFirstOccurrenceOf (":", false, false));
        expect (saveTo (*e, m.file).ok);
        m.show = readShow (m.file);
        m.pluginXml = instrumentXml (m.show);
        m.state = stateBytes (instrumentOf (m.show));
        return m;
    }

    //==========================================================================
    // The checks the negative controls turn off one by one (each true when the fix works).

    // A show's plugin comes back with its settings and knobs (and a load that worked says so).
    bool restoreWorks()
    {
        const auto m = makeShow();
        auto e = openShow (m.file);
        pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }, 3000);
        pump (20);
        auto* f = fake (e->getSynthPlugin());
        return m.loadError.isEmpty() && f != nullptr && f->patch == "Lead 7" && macrosOf (e->getSynthPlugin()) == instrumentOf (m.show).getProperty ("macros").toString();
    }
    // A save while the show's plugin is still loading keeps it byte for byte.
    bool saveWhileLoadingKeeps()
    {
        const auto m = makeShow();
        format->hold = true;
        auto e = openShow (m.file);
        const auto f = newFile ("while loading");
        const bool ok = saveTo (*e, f).ok && instrumentXml (readShow (f)) == m.pluginXml;
        resetFakes();
        return ok;
    }
    // Remove while it loads: it stays removed when the load finishes.
    bool removeWhileLoadingStays()
    {
        const auto m = makeShow();
        format->hold = true;
        auto e = openShow (m.file);
        e->getSynthPlugin().clear();
        format->finishAll();
        pump();
        const bool ok = ! e->getSynthPlugin().isLoaded() && ! instrumentOf (e->saveShow()).isValid();
        resetFakes();
        return ok;
    }
    // Another show opened while the first one's plugin loads gets nothing of the first one's.
    bool otherShowClean()
    {
        const auto m = makeShow();
        const auto b = makeShow ("fake:B", "Pad 2");
        format->hold = true;
        auto e = openShow (m.file);
        e->loadShow (readShow (b.file));
        format->finish ("fake:B");   // out of order: the newer one first
        format->finish ("fake:A");
        pump();
        auto* f = fake (e->getSynthPlugin());
        const bool ok = f != nullptr && f->getName() == "B" && f->patch == "Pad 2" && instrumentOf (e->saveShow()).getProperty ("name").toString() == "B";
        resetFakes();
        return ok;
    }

    //==========================================================================
    void runTest() override
    {
        attachHost();
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KaleidoboxPluginPersistTests");
        folder.deleteRecursively();
        folder.createDirectory();
        const bool hadNote = openingNote().existsAsFile();

        beginTest ("1. a show's plugin gets back exactly the bytes the show saved, before it plays");
        {
            const auto m = makeShow();
            expectEquals (m.loadError, juce::String(), "a load that worked reports no error");
            expect (m.state.getSize() > 3000, "the show has the plugin's settings");
            FakeSound::lastReceived.reset();
            auto e = openShow (m.file);
            expect (! e->getSynthPlugin().isLoaded(), "(it opens asynchronously)");
            expect (pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }));
            expect (FakeSound::lastReceived == m.state, "the plugin was handed the saved bytes");
            expect (! FakeSound::receivedWhilePrepared, "before it was live (prepared, on the audio thread)");
            juce::MemoryBlock now;
            e->getSynthPlugin().getInstance()->getStateInformation (now);
            expect (now == m.state);
        }

        beginTest ("2. edited settings, macro knobs, level and bypass come back");
        {
            for (bool bypass : { false, true })
            {
                const auto m = makeShow ("fake:A", "Lead 7", bypass);
                auto e = openShow (m.file);
                pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
                auto& s = e->getSynthPlugin();
                auto* f = fake (s);
                expect (f != nullptr);
                if (f != nullptr)
                {
                    expectEquals (f->patch, juce::String ("Lead 7"));
                    expectWithinAbsoluteError (f->level, 0.42f, 1.0e-6f);
                    expectWithinAbsoluteError (f->cutoff->value, 0.83f, 1.0e-6f);
                    expectWithinAbsoluteError (f->drive->value, 0.27f, 1.0e-6f);
                }
                expectEquals (macrosOf (s), juce::String ("-1 -1 -1 1 -1 -1 -1 -1"), "the show's knobs, not autoMap's");
                expectWithinAbsoluteError (s.level.load(), 0.6f, 1.0e-6f);
                expect (s.bypassed.load() == bypass, "bypass");
                expectEquals (instrumentXml (e->saveShow()), m.pluginXml, "saved again, unchanged");
            }
        }

        beginTest ("3. the same notes sound the same after the show is reopened");
        {
            const auto m = makeShow();
            auto e = openShow (m.file);
            pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
            const auto again = render (e->getSynthPlugin());
            expect (rms (m.audio) > 0.01, "it plays");
            expectEquals (maxDiff (again, m.audio), 0.0, "sample for sample");
            auto plain = freshEngine();
            plain->getSynthPlugin().load (FakeFormat::describe ("fake:A"));
            pumpUntil ([&] { return plain->getSynthPlugin().isLoaded(); });
            expect (maxDiff (render (plain->getSynthPlugin()), m.audio) > 0.01, "(a fresh one sounds different)");
        }

        beginTest ("4. a save while the show's plugin is still loading keeps it byte for byte; then it comes in");
        {
            const auto m = makeShow();
            format->hold = true;
            auto e = openShow (m.file);
            pump();
            expect (! e->getSynthPlugin().isLoaded() && format->waiting() == 1, "(held)");
            expect (e->getSynthPlugin().hasPluginToSave());
            const auto f = newFile ("saved while loading");
            const auto r = saveTo (*e, f);
            expect (r.ok, r.error);
            expectEquals (r.warning, juce::String(), "a show's own plugin on its way isn't a warning");
            expectEquals (instrumentXml (readShow (f)), m.pluginXml);
            expectEquals (instrumentXml (e->saveShow()), m.pluginXml, "the tree too");
            format->finish();
            pump();
            auto* fs = fake (e->getSynthPlugin());
            expect (fs != nullptr && fs->patch == "Lead 7", "then it loads with the show's settings");
            expectEquals (instrumentXml (e->saveShow()), m.pluginXml);
            resetFakes();
        }

        beginTest ("5. a save with the plugin not installed here keeps it");
        {
            const auto m = makeShow ("fake:M");
            format->missing.add ("fake:M");
            auto e = openShow (m.file);
            pump (100);
            expect (! e->getSynthPlugin().isLoaded());
            for (int i = 0; i < 2; ++i)
            {
                const auto f = newFile ("missing");
                expect (saveTo (*e, f).ok);
                expectEquals (instrumentXml (readShow (f)), m.pluginXml);
            }
            resetFakes();
        }

        beginTest ("6. a save after the plugin failed to open keeps it");
        {
            const auto m = makeShow ("fake:F");
            format->failing.add ("fake:F");
            {
                auto e = openShow (m.file);
                pump (100);
                expect (! e->getSynthPlugin().isLoaded());
                const auto f = newFile ("failed");
                expect (saveTo (*e, f).ok);
                expectEquals (instrumentXml (readShow (f)), m.pluginXml);
            }
            format->failing.clear();
            {
                // One that throws the show's settings back stays out, and the show keeps them.
                FakeSound::throwOnSetState = true;
                const int before = FakeSound::alive;
                auto e = openShow (m.file);
                pump (100);
                expect (! e->getSynthPlugin().isLoaded(), "not put in without its settings");
                expectEquals (FakeSound::alive, before, "(and deleted)");
                expectEquals (instrumentXml (e->saveShow()), m.pluginXml);
            }
            resetFakes();
        }

        beginTest ("7. a show saved while its plugin was missing gets its sound back once it's installed");
        {
            const auto m = makeShow ("fake:R");
            format->missing.add ("fake:R");
            const auto f = newFile ("resaved without it");
            {
                auto e = openShow (m.file);
                pump (100);
                expect (saveTo (*e, f).ok);
            }
            format->missing.clear();
            auto e = openShow (f);
            expect (pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }));
            auto* fs = fake (e->getSynthPlugin());
            expect (fs != nullptr && fs->patch == "Lead 7");
            expectEquals (maxDiff (render (e->getSynthPlugin()), m.audio), 0.0, "its sound, sample for sample");
            resetFakes();
        }

        beginTest ("8. Remove really removes it: loaded, loading, missing or failed, and a late load doesn't bring it back");
        {
            const auto m = makeShow ("fake:A");
            for (auto how : { "loaded", "loading", "missing", "failed" })
            {
                const juce::String which (how);
                if (which == "loading") format->hold = true;
                if (which == "missing") format->missing.add ("fake:A");
                if (which == "failed") format->failing.add ("fake:A");
                auto e = openShow (m.file);
                if (which != "loading")
                    pump (100);
                expect (e->getSynthPlugin().isLoaded() == (which == "loaded"), which);
                expect (e->getSynthPlugin().hasPluginToSave(), which + ": there's one to remove");
                e->getSynthPlugin().clear();   // the shell's Remove
                expect (! e->getSynthPlugin().hasPluginToSave(), which);
                format->hold = false;
                format->finishAll();
                pump();
                expect (! e->getSynthPlugin().isLoaded(), which + ": stays removed");
                const auto f = newFile ("removed " + which);
                expect (saveTo (*e, f).ok);
                expect (! instrumentOf (readShow (f)).isValid(), which + ": gone from the save");
                resetFakes();
            }
            expect (removeWhileLoadingStays());
        }

        beginTest ("9. a plugin picked while the show's loads replaces it; the old load is dropped (either order)");
        {
            const auto m = makeShow ("fake:A");
            for (bool newerFirst : { false, true })
            {
                format->hold = true;
                auto e = openShow (m.file);
                auto& s = e->getSynthPlugin();
                juce::String said = "(not called)";
                s.load (FakeFormat::describe ("fake:D"), [&] (const juce::String& x) { said = x; });
                // Until it's in, the slot is what it was (the show's A, whole) and the save says so.
                const auto f = newFile ("picked, loading");
                const auto r = saveTo (*e, f);
                expect (r.ok);
                expectEquals (instrumentXml (readShow (f)), m.pluginXml);
                expect (r.warning.contains ("D was still loading"), r.warning);
                const int before = FakeSound::alive;
                format->finish (newerFirst ? "fake:D" : "fake:A");
                format->finish (newerFirst ? "fake:A" : "fake:D");
                pump();
                expectEquals (FakeSound::alive, before + 1, "only the one picked is kept");
                expectEquals (FakeSound::deletedOffMessageThread, 0);
                auto* fs = fake (s);
                expect (fs != nullptr && fs->getName() == "D" && fs->patch == "Init", "the one picked, without the show's settings");
                expectEquals (said, juce::String());
                const auto saved = instrumentOf (e->saveShow());
                expectEquals (saved.getProperty ("name").toString(), juce::String ("D"));
                resetFakes();
            }
            {
                // Over a plugin that's in: it stays (and is saved) until the new one comes.
                auto e = openShow (m.file);
                pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
                format->hold = true;
                e->getSynthPlugin().load (FakeFormat::describe ("fake:D"));
                expectEquals (instrumentOf (e->saveShow()).getProperty ("name").toString(), juce::String ("A"));
                format->finishAll();
                pump();
                expectEquals (instrumentOf (e->saveShow()).getProperty ("name").toString(), juce::String ("D"));
                resetFakes();
            }
            {
                // A pick that fails changes nothing.
                auto e = openShow (m.file);
                pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
                juce::String said;
                e->getSynthPlugin().load (FakeFormat::describe ("fake:Nope"), [&] (const juce::String& x) { said = x; });
                format->missing.add ("fake:Nope");
                pump (100);
                expect (said.isNotEmpty());
                expectEquals (instrumentXml (e->saveShow()), m.pluginXml);
                resetFakes();
            }
        }

        beginTest ("10. a show opened while another's plugin loads gets nothing of it, and saves right");
        {
            const auto a = makeShow ("fake:A");
            const auto b = makeShow ("fake:B", "Pad 2");
            for (bool newerFirst : { false, true })
            {
                format->hold = true;
                auto e = openShow (a.file);
                e->loadShow (readShow (b.file));
                const auto f = newFile ("B while both load");
                expect (saveTo (*e, f).ok);
                expectEquals (instrumentXml (readShow (f)), b.pluginXml, "B's plugin, as B had it");
                format->finish (newerFirst ? "fake:B" : "fake:A");
                format->finish (newerFirst ? "fake:A" : "fake:B");
                pump();
                auto* fs = fake (e->getSynthPlugin());
                expect (fs != nullptr && fs->getName() == "B" && fs->patch == "Pad 2");
                expectEquals (instrumentXml (e->saveShow()), b.pluginXml);
                resetFakes();
            }
            {
                // A new song, and a show with no plugin, while A's loads.
                format->hold = true;
                auto e = openShow (a.file);
                e->newShow();
                auto other = freshEngine();
                const auto none = newFile ("no plugin");
                expect (saveTo (*other, none).ok);
                auto e2 = openShow (a.file);
                e2->loadShow (readShow (none));
                format->finishAll();
                pump();
                expect (! e->getSynthPlugin().isLoaded() && ! e2->getSynthPlugin().isLoaded());
                expect (! instrumentOf (e->saveShow()).isValid() && ! instrumentOf (e2->saveShow()).isValid());
                resetFakes();
            }
            {
                // The previous show's plugin (in) isn't saved under the next show while its own loads.
                auto e = openShow (b.file);
                pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
                format->hold = true;
                e->loadShow (readShow (a.file));
                expectEquals (instrumentXml (e->saveShow()), a.pluginXml);
                resetFakes();
            }
            expect (otherShowClean());
        }

        beginTest ("11. every save while it loads writes the same plugin tree");
        {
            const auto m = makeShow();
            format->hold = true;
            auto e = openShow (m.file);
            juce::StringArray trees;
            for (int i = 0; i < 4; ++i)
            {
                const auto f = newFile ("again " + juce::String (i));
                expect (saveTo (*e, f, i % 2 == 0 ? rigshow::SaveKind::document : rigshow::SaveKind::copy).ok);
                trees.add (instrumentXml (readShow (f)));
            }
            expect (saveTo (*e, m.file).ok, "over the show itself");   // Cmd+S
            trees.add (instrumentXml (readShow (m.file)));
            for (const auto& t : trees)
                expectEquals (t, m.pluginXml);
            resetFakes();
        }

        beginTest ("12. failed saves leave the kept plugin as it was; a save that can't be whole fails");
        {
            const auto m = makeShow();
            format->hold = true;
            auto e = openShow (m.file);
            const auto before = m.file.loadFileAsString();
            for (auto point : { "stage", "commit" })
            {
                rigshow::hooks().at = [point] (const juce::String& p, int) { return p != point; };
                const auto r = saveTo (*e, m.file);
                rigshow::hooks().at = nullptr;
                expect (! r.ok, juce::String ("failing at ") + point);
                expectEquals (m.file.loadFileAsString(), before, "the show on disk is unchanged");
                expect (e->getSynthPlugin().hasPluginToSave() && ! e->getSynthPlugin().isLoaded());
                expectEquals (instrumentXml (e->saveShow()), m.pluginXml);
            }
            const auto f = newFile ("after failures");
            expect (saveTo (*e, f).ok);
            expectEquals (instrumentXml (readShow (f)), m.pluginXml);
            format->finishAll();
            pump();

            // A plugin that's in but can't give its settings: the save fails and says why.
            expect (e->getSynthPlugin().isLoaded());
            FakeSound::throwOnGetState = true;
            auto r = saveTo (*e, m.file);
            expect (! r.ok && r.error.contains ("A couldn't give") && r.error.contains ("unchanged"), r.error);
            expectEquals (m.file.loadFileAsString(), before);
            FakeSound::throwOnGetState = false;
            // One that gives none, though it had some from the show: the same.
            FakeSound::emptyState = true;
            r = saveTo (*e, m.file);
            expect (! r.ok && r.error.contains ("no settings"), r.error);
            juce::String why;
            expect (e->saveToolSound (RigEngine::synthStrip, why).isValid() && why.isNotEmpty(), "a tool sound can't be kept either");
            expect (e->saveToolSound (RigEngine::bassStrip, why).isValid() && why.isEmpty(), "(another tool's sound can: it hasn't that plugin)");
            FakeSound::emptyState = false;
            r = saveTo (*e, m.file);
            expect (r.ok, r.error);
            expectEquals (instrumentXml (readShow (m.file)), m.pluginXml, "then it saves whole");
            // A plugin that has never had settings may give none.
            auto plain = freshEngine();
            FakeSound::emptyState = true;
            plain->getSynthPlugin().load (FakeFormat::describe ("fake:E"));
            pumpUntil ([&] { return plain->getSynthPlugin().isLoaded(); });
            expect (saveTo (*plain, newFile ("never had settings")).ok);
            resetFakes();
        }

        beginTest ("13. the Last show saved at quit keeps the plugin while it loads, or when it's missing");
        {
            const auto m = makeShow ("fake:L");
            juce::PropertiesFile::Options o;
            o.applicationName = "KaleidoboxPersistTests";
            juce::PropertiesFile settings (folder.getChildFile ("settings.xml"), o);
            for (bool missing : { false, true })
            {
                if (missing) format->missing.add ("fake:L");
                else format->hold = true;
                auto e = openShow (m.file);
                if (missing)
                    pump (100);
                const auto last = folder.getChildFile ("Last show.crateshow");
                const auto r = riglastshow::save (*e, last, settings);
                expect (r.ok && ! settings.containsKey (riglastshow::kFailedKey));
                expectEquals (instrumentXml (readShow (last)), m.pluginXml, missing ? "missing" : "loading");
                resetFakes();
            }
            {
                // When it can't be whole, the quit save fails (the app asks before quitting).
                auto e = openShow (m.file);
                pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
                FakeSound::throwOnGetState = true;
                const auto r = riglastshow::save (*e, folder.getChildFile ("Last show.crateshow"), settings);
                expect (! r.ok && settings.containsKey (riglastshow::kFailedKey));
                resetFakes();
            }
        }

        beginTest ("14. Kaleidobox inside Logic: its hosted plugin's settings round-trip, and a state asked for while it loads keeps it");
        {
            juce::MemoryBlock state;
            {
                RigPlugin plugin;
                plugin.prepareToPlay (kRate, kBlock);
                auto& s = plugin.getEngine().getSynthPlugin();
                s.load (FakeFormat::describe ("fake:G"));
                pumpUntil ([&] { return s.isLoaded(); });
                fake (s)->patch = "Logic lead";
                s.level = 0.3f;
                plugin.getStateInformation (state);
            }
            auto pluginTree = [] (const juce::MemoryBlock& m)
            {
                auto xml = juce::AudioProcessor::getXmlFromBinary (m.getData(), (int) m.getSize());
                return xml != nullptr ? instrumentXml (juce::ValueTree::fromXml (*xml)) : juce::String ("(none)");
            };
            const auto saved = pluginTree (state);
            expect (saved.contains ("Fake-G"), saved.substring (0, 200));
            {
                RigPlugin again;
                again.prepareToPlay (kRate, kBlock);
                again.setStateInformation (state.getData(), (int) state.getSize());
                expect (pumpUntil ([&] { return again.getEngine().getSynthPlugin().isLoaded(); }));
                auto* fs = fake (again.getEngine().getSynthPlugin());
                expect (fs != nullptr && fs->patch == "Logic lead", "restored in Logic");
                expectWithinAbsoluteError (again.getEngine().getSynthPlugin().level.load(), 0.3f, 1.0e-6f);
            }
            {
                format->hold = true;
                RigPlugin again;
                again.prepareToPlay (kRate, kBlock);
                again.setStateInformation (state.getData(), (int) state.getSize());
                juce::MemoryBlock whileLoading;
                again.getStateInformation (whileLoading);
                expectEquals (pluginTree (whileLoading), saved, "Logic's save while it loads keeps it");
                // Another song's state comes in (Logic's undo, a project switch) before it's in.
                RigPlugin blank;
                juce::MemoryBlock none;
                blank.getStateInformation (none);
                again.setStateInformation (none.getData(), (int) none.getSize());
                format->finishAll();
                pump();
                expect (! again.getEngine().getSynthPlugin().isLoaded(), "the older state's plugin doesn't land in the newer one");
                resetFakes();
            }
            {
                // A plugin whose settings can't be read: Logic gets the last whole state, and is told why.
                RigPlugin again;
                again.prepareToPlay (kRate, kBlock);
                again.setStateInformation (state.getData(), (int) state.getSize());
                pumpUntil ([&] { return again.getEngine().getSynthPlugin().isLoaded(); });
                FakeSound::throwOnGetState = true;
                juce::MemoryBlock failed;
                again.getStateInformation (failed);
                expect (failed == state, "the last whole state");
                bool error = false;
                const auto said = again.getEngine().takeShowFilesMessage (error);
                expect (error && said.contains ("plugin's settings"), said);
                resetFakes();
            }
        }

        beginTest ("15. a slot that goes while its plugin loads: no crash, no 'froze' note; a real freeze still leaves it closed");
        {
            const auto m = makeShow ();
            if (hadNote)
            {
                logMessage ("skipped: a 'Rig Plugin Opening.txt' was already there (the app's own): not touched");
            }
            else
            {
                {
                    format->hold = true;
                    const int before = FakeSound::alive;
                    {
                        auto e = openShow (m.file);
                        expect (openingNote().existsAsFile(), "(written while it opens)");
                    }
                    expect (! openingNote().existsAsFile(), "no note left when the engine goes mid-load");
                    format->finishAll();   // the load comes after its slot went
                    pump();
                    expectEquals (FakeSound::alive, before, "its plugin is deleted");
                    expectEquals (FakeSound::deletedOffMessageThread, 0);
                    resetFakes();
                }
                {
                    // The PluginHost goes too (the last slot), with the load's message still posted.
                    auto e = openShow (m.file);
                    e.reset();
                    detachHost();
                    pump (50);
                    expect (! openingNote().existsAsFile());
                    attachHost();
                }
                {
                    // A real freeze: the note is there at launch. The show's plugin is left closed,
                    // and the show keeps it, save after save.
                    openingNote().getParentDirectory().createDirectory();
                    openingNote().replaceWithText (FakeFormat::describe ("fake:A").createIdentifierString());
                    detachHost();
                    attachHost();
                    auto e = openShow (m.file);
                    pump (100);
                    expect (! e->getSynthPlugin().isLoaded(), "left closed");
                    expect (host().takeLeftClosed().contains ("A"));
                    for (int i = 0; i < 2; ++i)
                    {
                        const auto f = newFile ("left closed");
                        expect (saveTo (*e, f).ok);
                        expectEquals (instrumentXml (readShow (f)), m.pluginXml);
                    }
                    e->getSynthPlugin().load (FakeFormat::describe ("fake:A"));   // picked again by hand
                    expect (pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }));
                    e.reset();
                    detachHost();
                    attachHost();
                }
                expect (! openingNote().existsAsFile());
            }
        }

        beginTest ("16. a strip's and a loop's effect plugin, and a saved tool sound's plugin, the same way");
        {
            // The strip's effect plugin.
            auto e = freshEngine();
            auto& fx = e->getFx (RigEngine::synthStrip).plugin;
            fx.load (FakeFormat::describe ("fake:C"));
            auto& loop = e->getLoopFx (0).plugin;
            loop.load (FakeFormat::describe ("fake:K"));
            pumpUntil ([&] { return fx.isLoaded() && loop.isLoaded(); });
            fake (fx)->patch = "Wide";
            fake (loop)->patch = "Dub";
            e->getSynthPlugin().load (FakeFormat::describe ("fake:T"));
            pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
            fake (e->getSynthPlugin())->patch = "Tool lead";
            const auto file = newFile ("effects");
            expect (saveTo (*e, file).ok);
            const auto show = readShow (file);
            const auto fxXml = stripPluginXml (show), loopXml = loopPluginXml (show);
            expect (fxXml.contains ("Fake-C") && loopXml.contains ("Fake-K"), fxXml.substring (0, 120) + " / " + loopXml.substring (0, 120));
            const auto sound = e->saveToolSound (RigEngine::synthStrip);
            const auto soundXml = instrumentXml (sound);
            expect (soundXml.contains ("Fake-T"));

            // 1: restored with their bytes.
            {
                auto again = openShow (file);
                auto& afx = again->getFx (RigEngine::synthStrip).plugin;
                auto& aloop = again->getLoopFx (0).plugin;
                expect (pumpUntil ([&] { return afx.isLoaded() && aloop.isLoaded(); }));
                expect (fake (afx) != nullptr && fake (afx)->patch == "Wide");
                expect (fake (aloop) != nullptr && fake (aloop)->patch == "Dub");
                auto box = freshEngine();
                box->loadToolSound (sound);
                expect (pumpUntil ([&] { return box->getSynthPlugin().isLoaded(); }));
                expect (fake (box->getSynthPlugin()) != nullptr && fake (box->getSynthPlugin())->patch == "Tool lead", "the tool sound's plugin");
            }
            // 4: saved while they load.
            {
                format->hold = true;
                auto again = openShow (file);
                const auto f = newFile ("effects while loading");
                expect (saveTo (*again, f).ok);
                expectEquals (stripPluginXml (readShow (f)), fxXml, "strip effect");
                expectEquals (loopPluginXml (readShow (f)), loopXml, "loop effect");
                auto box = freshEngine();
                box->loadToolSound (sound);
                expectEquals (instrumentXml (box->saveToolSound (RigEngine::synthStrip)), soundXml, "tool sound");
                resetFakes();
            }
            // 5: missing.
            {
                format->missing.addArray (juce::StringArray { "fake:C", "fake:K", "fake:T" });
                auto again = openShow (file);
                auto box = freshEngine();
                box->loadToolSound (sound);
                pump (100);
                expect (! again->getFx (RigEngine::synthStrip).plugin.isLoaded() && ! box->getSynthPlugin().isLoaded());
                const auto f = newFile ("effects missing");
                expect (saveTo (*again, f).ok);
                expectEquals (stripPluginXml (readShow (f)), fxXml, "strip effect");
                expectEquals (loopPluginXml (readShow (f)), loopXml, "loop effect");
                expectEquals (instrumentXml (box->saveToolSound (RigEngine::synthStrip)), soundXml, "tool sound");
                resetFakes();
            }
            // Removed (None in its menu) really goes; a loop's effect plugin from the last show doesn't stay.
            {
                format->hold = true;
                auto again = openShow (file);
                again->getFx (RigEngine::synthStrip).plugin.clear();
                format->finishAll();
                pump();
                expectEquals (stripPluginXml (again->saveShow()), juce::String ("(none)"));
                auto other = freshEngine();
                const auto none = newFile ("no effects");
                expect (saveTo (*other, none).ok);
                again->loadShow (readShow (none));
                expect (! again->getLoopFx (0).plugin.hasPluginToSave(), "the next show's loop has no plugin");
                resetFakes();
            }
        }

        beginTest ("17. negative controls: each old fault, switched back on, is caught");
        {
            expect (restoreWorks() && saveWhileLoadingKeeps() && removeWhileLoadingStays() && otherShowClean(), "all fixed");

            PluginSlot::oldLoadResultForTests = true;
            const bool a = restoreWorks();
            PluginSlot::oldLoadResultForTests = false;
            expect (! a, "\"It didn't load\" (a) is caught: the show's settings and knobs don't come back");

            PluginSlot::oldSavingForTests = true;
            const bool b = saveWhileLoadingKeeps();
            PluginSlot::oldSavingForTests = false;
            expect (! b, "saving only a loaded plugin (b) is caught: the plugin is dropped");

            PluginSlot::noGenerationForTests = true;
            const bool c1 = removeWhileLoadingStays();
            const bool c2 = otherShowClean();
            PluginSlot::noGenerationForTests = false;
            expect (! c1, "no generation check (c) is caught: a removed plugin comes back");
            expect (! c2, "no generation check (c) is caught: the older show's plugin lands in the newer one");
        }

        beginTest ("18. a level or bypass changed while the show's plugin isn't in is saved; nothing else of its tree changes");
        {
            // The plugin's tree without its level and bypass (what must stay byte for byte).
            auto withoutHost = [] (const juce::String& x)
            {
                auto xml = juce::parseXML (x);
                if (xml == nullptr)
                    return x;
                xml->removeAttribute ("level");
                xml->removeAttribute ("bypassed");
                return xml->toString (juce::XmlElement::TextFormat().singleLine());
            };
            auto attr = [] (const juce::String& x, const char* attrName)
            {
                auto xml = juce::parseXML (x);
                return xml != nullptr ? xml->getStringAttribute (attrName) : juce::String ("(none)");
            };
            const auto m = makeShow();
            for (auto how : { "loading", "missing", "failed", "left closed" })
            {
                const juce::String which (how);
                if (which == "loading") format->hold = true;
                if (which == "missing") format->missing.add ("fake:A");
                if (which == "failed") format->failing.add ("fake:A");
                if (which == "left closed")
                {
                    if (hadNote) { resetFakes(); continue; }
                    openingNote().getParentDirectory().createDirectory();
                    openingNote().replaceWithText (FakeFormat::describe ("fake:A").createIdentifierString());
                    detachHost();
                    attachHost();
                }
                auto e = openShow (m.file);
                if (which != "loading")
                    pump (100);
                auto& s = e->getSynthPlugin();
                expect (! s.isLoaded(), which);
                // Unchanged: byte for byte.
                expectEquals (instrumentXml (e->saveShow()), m.pluginXml, which + ": unchanged, the show's tree exactly");
                s.level = 0.25f;
                const auto f = newFile ("level " + which);
                expect (saveTo (*e, f).ok);
                const auto saved = instrumentXml (readShow (f));
                expectWithinAbsoluteError ((float) attr (saved, "level").getFloatValue(), 0.25f, 1.0e-6f, which + ": the level you set is saved");
                expectEquals (withoutHost (saved), withoutHost (m.pluginXml), which + ": the rest of its tree byte for byte");
                s.bypassed = true;
                expect (attr (instrumentXml (e->saveShow()), "bypassed") == "1", which + ": bypass too");
                s.bypassed = false;
                s.level = 0.6f;
                expectEquals (instrumentXml (e->saveShow()), m.pluginXml, which + ": put back: the show's tree exactly (it was never changed)");
                // Reopened, the level comes back.
                if (which == "loading")
                {
                    format->hold = false;
                    auto again = openShow (f);
                    pumpUntil ([&] { return again->getSynthPlugin().isLoaded(); });
                    expectWithinAbsoluteError (again->getSynthPlugin().level.load(), 0.25f, 1.0e-6f, "reopened: the level saved while it loaded");
                    expect (fake (again->getSynthPlugin()) != nullptr && fake (again->getSynthPlugin())->patch == "Lead 7", "and its settings");
                }
                if (which == "left closed")
                {
                    e.reset();
                    openingNote().deleteFile();
                    detachHost();
                    attachHost();
                }
                resetFakes();
            }

            // Every saver: the Last show, Logic's state, a tool sound, a strip's and a loop's racks.
            {
                format->hold = true;
                auto e = openShow (m.file);
                e->getSynthPlugin().level = 0.25f;
                juce::PropertiesFile::Options o;
                o.applicationName = "KaleidoboxPersistTests";
                juce::PropertiesFile settings (folder.getChildFile ("settings.xml"), o);
                const auto last = folder.getChildFile ("Last show.crateshow");
                expect (riglastshow::save (*e, last, settings).ok);
                expectWithinAbsoluteError ((float) attr (instrumentXml (readShow (last)), "level").getFloatValue(), 0.25f, 1.0e-6f, "the Last show");
                const auto sound = e->saveToolSound (RigEngine::synthStrip);
                expectWithinAbsoluteError ((float) attr (instrumentXml (sound), "level").getFloatValue(), 0.25f, 1.0e-6f, "a tool sound");
                expectEquals (withoutHost (instrumentXml (sound)), withoutHost (m.pluginXml), "a tool sound: the rest byte for byte");
                resetFakes();
            }
            {
                juce::MemoryBlock state;
                {
                    RigPlugin plugin;
                    plugin.prepareToPlay (kRate, kBlock);
                    auto& s = plugin.getEngine().getSynthPlugin();
                    s.load (FakeFormat::describe ("fake:G"));
                    pumpUntil ([&] { return s.isLoaded(); });
                    s.level = 0.3f;
                    plugin.getStateInformation (state);
                }
                format->hold = true;
                RigPlugin again;
                again.prepareToPlay (kRate, kBlock);
                again.setStateInformation (state.getData(), (int) state.getSize());
                again.getEngine().getSynthPlugin().level = 0.9f;
                juce::MemoryBlock whileLoading;
                again.getStateInformation (whileLoading);
                auto xml = juce::AudioProcessor::getXmlFromBinary (whileLoading.getData(), (int) whileLoading.getSize());
                const auto tree = xml != nullptr ? instrumentXml (juce::ValueTree::fromXml (*xml)) : juce::String();
                expectWithinAbsoluteError ((float) attr (tree, "level").getFloatValue(), 0.9f, 1.0e-6f, "Logic's state while it loads");
                resetFakes();
            }
            {
                auto e = freshEngine();
                e->getFx (RigEngine::synthStrip).plugin.load (FakeFormat::describe ("fake:C"));
                e->getLoopFx (0).plugin.load (FakeFormat::describe ("fake:K"));
                pumpUntil ([&] { return e->getFx (RigEngine::synthStrip).plugin.isLoaded() && e->getLoopFx (0).plugin.isLoaded(); });
                const auto file = newFile ("racks");
                expect (saveTo (*e, file).ok);
                const auto fxXml = stripPluginXml (readShow (file)), loopXml = loopPluginXml (readShow (file));
                format->hold = true;
                auto again = openShow (file);
                again->getFx (RigEngine::synthStrip).plugin.level = 0.4f;
                again->getLoopFx (0).plugin.bypassed = true;
                const auto f = newFile ("racks while loading");
                expect (saveTo (*again, f).ok);
                const auto show = readShow (f);
                expectWithinAbsoluteError ((float) attr (stripPluginXml (show), "level").getFloatValue(), 0.4f, 1.0e-6f, "a strip's rack");
                expectEquals (withoutHost (stripPluginXml (show)), withoutHost (fxXml), "a strip's rack: the rest byte for byte");
                expect (attr (loopPluginXml (show), "bypassed") == "1", "a loop's rack");
                expectEquals (withoutHost (loopPluginXml (show)), withoutHost (loopXml), "a loop's rack: the rest byte for byte");
                resetFakes();
            }

            // Negative control: the old saving (the show's tree exactly) loses the level.
            {
                format->hold = true;
                auto e = openShow (m.file);
                e->getSynthPlugin().level = 0.25f;
                PluginSlot::keptLevelOverlayOffForTests = true;
                const auto lost = attr (instrumentXml (e->saveShow()), "level").getFloatValue();
                PluginSlot::keptLevelOverlayOffForTests = false;
                expect (std::abs (lost - 0.25f) > 0.1f, "with the old saving, the level changed while it loaded is lost (caught)");
                resetFakes();
            }
        }

        resetFakes();
        expectEquals (FakeSound::deletedOffMessageThread, 0, "every fake plugin was deleted on the message thread");
        if (! hadNote)
            expect (! openingNote().existsAsFile(), "no 'froze' note left behind");
        detachHost();
        folder.deleteRecursively();
    }
};

static RigPluginPersistTests rigPluginPersistTests;

//==============================================================================
// The main cases with a real plugin, then the same checks with a fake one made like an analog
// model (a stepped control, a little free-running noise, its own bytes once restored), which
// always runs, and a negative control: a plugin that takes nothing it's given is caught.
//
// What "restored" means for a real plugin: its state is the bytes a fresh instance makes of the
// saved state given directly (the reference), every parameter is the reference's, and each
// parameter the test edited reads what the plugin itself read back when it was set (before the
// save), which isn't its default. Not what was asked for (a stepped control holds its nearest
// step), and never by the sound alone: an analog model's oscillators and noise run free, so
// sample-level sound can't tell restored from default. Its sound is compared only by a robust
// feature (each block's loudness), and only when the saved sound and the default's differ by a
// wide margin over how much either differs from itself run to run.
namespace realpersist
{
// Each block's loudness in dB (stereo, interleaved per block as render makes it), from -80 dB.
inline std::vector<double> blockDb (const std::vector<float>& v)
{
    std::vector<double> out;
    const size_t n = (size_t) (2 * kBlock);
    for (size_t k = 0; (k + 1) * n <= v.size(); ++k)
    {
        double q = 0;
        for (size_t i = k * n; i < (k + 1) * n; ++i)
            q += (double) v[i] * v[i];
        out.push_back (10.0 * std::log10 (q / (double) n + 1.0e-8));
    }
    return out;
}
// The mean loudness difference (dB) over the blocks where either sounds (above -60 dB).
inline double loudnessDistance (const std::vector<float>& a, const std::vector<float>& b)
{
    const auto x = blockDb (a), y = blockDb (b);
    double sum = 0;
    int n = 0;
    for (size_t k = 0; k < juce::jmin (x.size(), y.size()); ++k)
        if (juce::jmax (x[k], y[k]) > -60.0)
            sum += std::abs (x[k] - y[k]), ++n;
    return n > 0 ? sum / n : 0.0;
}
} // namespace realpersist

struct RigRealPluginPersistTests final : juce::UnitTest
{
    RigRealPluginPersistTests() : juce::UnitTest ("Rig plugin persistence (real plugin)", "Rig") {}

    juce::File folder;
    std::unique_ptr<juce::SharedResourcePointer<PluginHost>> hostRef;

    static juce::MemoryBlock sampleInstrumentState() { return real::sampleInstrumentState(); }
    static void bundlePath (const juce::File& show) { real::bundlePath (show); }

    static juce::MemoryBlock stateOf (PluginSlot& s)
    {
        juce::MemoryBlock m;
        if (auto* i = s.getInstance())
            i->getStateInformation (m);
        return m;
    }

    bool loadByHand (PluginSlot& s, const juce::PluginDescription& desc)
    {
        bool called = false;
        juce::String error;
        s.load (desc, [&] (const juce::String& x) { error = x; called = true; });
        pumpUntil ([&] { return called; }, 60000);
        expectEquals (error, juce::String(), "a load that worked reports no error");
        return s.isLoaded();
    }

    // Each check by name, so a negative control can say which ones catch the fault.
    struct Outcome
    {
        struct Check { juce::String what, detail; bool ok; };
        std::vector<Check> checks;
        int strongParams = 0, steppedParams = 0;
        bool audioDiscriminating = false;
        void add (const juce::String& what, bool ok, const juce::String& detail = {}) { checks.push_back ({ what, detail, ok }); }
        // False if any check whose name starts with `prefix` failed (true if there are none).
        bool passed (const juce::String& prefix) const
        {
            for (const auto& c : checks)
                if (c.what.startsWith (prefix) && ! c.ok)
                    return false;
            return true;
        }
        bool has (const juce::String& prefix) const
        {
            for (const auto& c : checks)
                if (c.what.startsWith (prefix))
                    return true;
            return false;
        }
    };
    void expectAll (const Outcome& o, const juce::String& tag)
    {
        for (const auto& c : o.checks)
            expect (c.ok, "[" + tag + "] " + c.what + (c.detail.isNotEmpty() ? " -- " + c.detail : juce::String()));
    }

    Outcome persist (const juce::PluginDescription& desc, bool isSampleInstrument, const juce::String& tag)
    {
        Outcome o;
        auto say = [this, &tag] (const juce::String& t) { logMessage ("[" + tag + "] " + t.trimStart()); };
        say ("plugin: " + desc.pluginFormatName + " " + desc.name + " " + desc.version + " (" + desc.fileOrIdentifier + ")");
        const auto file = folder.getChildFile ("real " + juce::File::createLegalFileName (tag) + ".crateshow");
        file.deleteFile();

        // A fresh instance at its default (what a restore that did nothing would leave).
        auto plain = freshEngine();
        if (! loadByHand (plain->getSynthPlugin(), desc))
        {
            o.add ("loads", false, "it didn't load by hand");
            return o;
        }
        pump (300);
        const auto defaultState = stateOf (plain->getSynthPlugin());
        const auto defaultValues = real::valuesOf (*plain->getSynthPlugin().getInstance());

        std::vector<float> savedAudio;
        std::vector<real::ChosenParam> params;
        {
            auto e = freshEngine();
            auto& s = e->getSynthPlugin();
            if (! loadByHand (s, desc))
            {
                o.add ("loads", false, "it didn't load by hand");
                return o;
            }
            if (isSampleInstrument)
            {
                const auto st = sampleInstrumentState();
                s.getInstance()->setStateInformation (st.getData(), (int) st.getSize());
            }
            else
            {
                params = real::chooseParams (*s.getInstance(), 0, 6, say);
            }
            pump (300);
            // What it holds right before the save is what has to come back.
            auto& ps = s.getInstance()->getParameters();
            for (auto& c : params)
            {
                const float now = ps[c.index]->getValue();
                if (std::abs (now - c.readBack) > 1.0e-6f)
                    say ("'" + c.name + "' reads " + juce::String (now, 6) + " at the save (was " + juce::String (c.readBack, 6) + ")");
                c.readBack = now;
                if (std::abs (c.readBack - c.requested) > 1.0e-4f)
                    ++o.steppedParams;
            }
            for (int k = 0; k < PluginSlot::kMacros; ++k)
                s.setMacro (k, -1);
            s.setMacro (5, 0);
            s.level = 0.7f;
            savedAudio = render (s, 200);   // (the edited sound, as it played before the save)
            o.add ("edited: its state isn't its default", stateOf (s) != defaultState);
            if (! isSampleInstrument)
                o.add ("edited: some of its parameters moved and hold", ! params.empty(),
                       "no automatable continuous parameter moved and held a value other than its default (try KALEIDOBOX_TEST_PARAMS)");
            o.add ("saved", e->saveShowFile (file, rigshow::SaveKind::document).ok);
            bundlePath (file);
        }
        const auto show = readShow (file);
        const auto savedXml = instrumentXml (show);
        const auto savedState = stateBytes (instrumentOf (show));
        say ("saved state " + real::hashOf (savedState) + "; default " + real::hashOf (defaultState));

        // Saved straight after opening, while it loads: byte for byte.
        auto e = openShow (file);
        auto& s = e->getSynthPlugin();
        o.add ("(still loading at open)", ! s.isLoaded());
        const auto during = folder.getChildFile ("during.crateshow");
        o.add ("saved while loading", e->saveShowFile (during, rigshow::SaveKind::document).ok);
        o.add ("saved while loading: byte for byte", instrumentXml (readShow (during)) == savedXml);
        if (! pumpUntil ([&] { return s.isLoaded(); }, 60000))
        {
            o.add ("then it loads", false);
            return o;
        }
        pump (300);

        // The reference: a fresh instance given the saved bytes directly.
        auto direct = freshEngine();
        if (! loadByHand (direct->getSynthPlugin(), desc))
        {
            o.add ("reference loads", false);
            return o;
        }
        direct->getSynthPlugin().getInstance()->setStateInformation (savedState.getData(), (int) savedState.getSize());
        pump (300);
        const auto reopenedState = stateOf (s), directState = stateOf (direct->getSynthPlugin());
        say ("reopened state " + real::hashOf (reopenedState) + "; reference " + real::hashOf (directState)
             + (reopenedState == savedState ? " (the saved bytes)" : " (its own bytes, not the saved ones)"));
        o.add ("state==reference: the reopened plugin's state is the saved bytes' given directly", reopenedState == directState);
        o.add ("state!=default: the reopened plugin's state isn't a fresh one's", reopenedState != defaultState);

        // Every parameter is the reference's.
        {
            const auto now = real::valuesOf (*s.getInstance()), ref = real::valuesOf (*direct->getSynthPlugin().getInstance());
            juce::StringArray off;
            for (size_t i = 0; i < juce::jmin (now.size(), ref.size()); ++i)
                if (std::abs (now[i] - ref[i]) > 1.0e-6f)
                    off.add ("'" + s.getInstance()->getParameters()[(int) i]->getName (40) + "' " + juce::String (now[i], 6) + " vs " + juce::String (ref[i], 6));
            say (juce::String ((int) now.size()) + " parameters; " + juce::String (off.size()) + " differ from the reference's");
            o.add ("params==reference: every parameter is the reference's", now.size() == ref.size() && off.isEmpty(), off.joinIntoString ("; ").substring (0, 400));
        }
        // Each edited one: what it read back before the save (within float rounding: 1e-6 is a
        // few steps of a float near 1), and not its default.
        {
            auto& ps = s.getInstance()->getParameters();
            auto& rs = direct->getSynthPlugin().getInstance()->getParameters();
            for (const auto& c : params)
            {
                const float got = ps[c.index]->getValue(), ref = rs[c.index]->getValue(), def = defaultValues[(size_t) c.index];
                const juce::String what = "'" + c.name + "' read back " + juce::String (c.readBack, 6) + " before the save; reopened " + juce::String (got, 6)
                                          + ", reference " + juce::String (ref, 6) + ", fresh " + juce::String (def, 6);
                if (std::abs (ref - c.readBack) > 1.0e-6f)
                {
                    // The plugin's own restore puts it elsewhere: only the reference can say what it holds.
                    say ("its own restore doesn't bring back " + what + " (the reference checks it)");
                    continue;
                }
                o.add ("param==readback: " + what, std::abs (got - c.readBack) <= 1.0e-6f);
                o.add ("param!=default: " + what, std::abs (got - def) >= 0.01f);
                if (std::abs (got - c.readBack) <= 1.0e-6f && std::abs (got - def) >= 0.01f)
                    ++o.strongParams;
            }
            if (! isSampleInstrument)
                o.add ("strong: at least one edited parameter came back as read back, away from its default", o.strongParams > 0,
                       juce::String (o.strongParams) + " of " + juce::String ((int) params.size()));
        }
        o.add ("macros", macrosOf (s) == "-1 -1 -1 -1 -1 0 -1 -1", macrosOf (s));
        o.add ("level", std::abs (s.level.load() - 0.7f) <= 1.0e-6f, juce::String (s.level.load()));

        // Its sound.
        direct->getSynthPlugin().level = 0.7f;
        if (isSampleInstrument)
        {
            // It re-encodes its 24-bit sample on every save and load, so a reopened sound is within
            // a 24-bit step (1e-6 is below one: 2^-23 = 1.2e-7 per step at full scale) of the saved.
            const auto again = render (s, 200);
            o.add ("audio: it plays", rms (savedAudio) > 0.01);
            o.add ("audio: the same sound, within 24-bit rounding", maxDiff (again, savedAudio) < 1.0e-6, juce::String (maxDiff (again, savedAudio)));
            o.add ("audio: exactly what the saved bytes play given directly", maxDiff (again, render (direct->getSynthPlugin(), 200)) <= 0.0);
        }
        else
        {
            auto direct2 = freshEngine();
            auto plain2 = freshEngine();
            if (loadByHand (direct2->getSynthPlugin(), desc) && loadByHand (plain2->getSynthPlugin(), desc))
            {
                direct2->getSynthPlugin().getInstance()->setStateInformation (savedState.getData(), (int) savedState.getSize());
                pump (300);
                for (auto* x : { &plain, &plain2, &direct2 })
                    (*x)->getSynthPlugin().level = 0.7f;
                const auto ref = render (direct->getSynthPlugin(), 200), ref2 = render (direct2->getSynthPlugin(), 200);
                const auto def = render (plain->getSynthPlugin(), 200), def2 = render (plain2->getSynthPlugin(), 200);
                const auto again = render (s, 200);
                using realpersist::loudnessDistance;
                const double apart = loudnessDistance (ref, def), editedApart = loudnessDistance (savedAudio, def);
                const double jitter = juce::jmax (loudnessDistance (ref, ref2), loudnessDistance (def, def2));
                const double toRef = loudnessDistance (again, ref), toDefault = loudnessDistance (again, def);
                say ("sound (mean block loudness difference): saved (the reference) vs default " + juce::String (apart, 2) + " dB (as edited, before the save: "
                     + juce::String (editedApart, 2) + " dB), run to run " + juce::String (jitter, 2) + " dB; reopened to the reference " + juce::String (toRef, 2)
                     + " dB, to a fresh one " + juce::String (toDefault, 2) + " dB");
                o.audioDiscriminating = apart >= 3.0 && apart >= 4.0 * jitter;
                if (! o.audioDiscriminating && editedApart >= 3.0 && editedApart >= 4.0 * jitter)
                    say ("NOTE: the edited sound was " + juce::String (editedApart, 2) + " dB from the default's, but the saved bytes given directly to a fresh instance "
                         + "sound like the default: the plugin's own restore doesn't bring its sound back (the state and parameter checks decide)");
                if (o.audioDiscriminating)
                {
                    o.add ("audio: the reopened sound is the saved one's, not the default's", toRef < apart / 2 && toDefault > apart / 2,
                           juce::String (toRef, 2) + " / " + juce::String (toDefault, 2) + " dB, apart " + juce::String (apart, 2));
                }
                else
                {
                    say ("the audio check is not discriminating for this plugin/patch (saved vs default " + juce::String (apart, 2) + " dB, needs 3 dB and 4x the run-to-run "
                         + juce::String (jitter, 2) + " dB): it relies on the state and parameter checks above");
                }
            }
            else
                o.add ("audio: two more instances load", false);
        }

        // Missing here (simulated in a copy of the show: never by moving a plugin).
        {
            auto xml = juce::parseXML (file);
            for (auto* inst : xml->getChildWithTagNameIterator ("Instrument"))
                if (auto* p = inst->getChildByName ("Plugin"))
                    if (auto d = juce::parseXML (p->getStringAttribute ("description")))
                    {
                        const auto f = d->getStringAttribute ("file");
                        d->setAttribute ("file", f.startsWith ("AudioUnit:") ? juce::String ("AudioUnit:Synths/aumu,Zzzz,Zzzz")
                                                                             : folder.getChildFile ("Not installed.vst3").getFullPathName());
                        p->setAttribute ("description", d->toString (juce::XmlElement::TextFormat().singleLine()));
                    }
            const auto missing = folder.getChildFile ("missing.crateshow");
            xml->writeTo (missing);
            const auto missingXml = instrumentXml (readShow (missing));
            auto m = openShow (missing);
            pump (1000);
            o.add ("missing: not in", ! m->getSynthPlugin().isLoaded());
            const auto resaved = folder.getChildFile ("missing resaved.crateshow");
            o.add ("missing: saved", m->saveShowFile (resaved, rigshow::SaveKind::document).ok);
            o.add ("missing: kept byte for byte", instrumentXml (readShow (resaved)) == missingXml);
        }
        e.reset();
        direct.reset();
        pump (300);
        return o;
    }

    void runTest() override
    {
        hostRef = std::make_unique<juce::SharedResourcePointer<PluginHost>>();
        bool hasFake = false;
        for (auto* f : hostRef->getObject().formats.getFormats())
            hasFake = hasFake || dynamic_cast<FakeFormat*> (f) != nullptr;
        if (! hasFake)
            hostRef->getObject().formats.addFormat (std::make_unique<FakeFormat>());
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KaleidoboxRealPluginPersistTests");
        folder.deleteRecursively();
        folder.createDirectory();

        beginTest ("a real plugin's settings, knobs and sound come back; saves while it loads or is missing keep it");
        {
            juce::PluginDescription desc;
            bool isSampleInstrument = false;
            juce::String why;
            if (real::findPlugin (desc, isSampleInstrument, why))
                expectAll (persist (desc, isSampleInstrument, desc.name), desc.name);
            else
                logMessage ("skipped: " + why);
        }

        beginTest ("the same with a fake analog-style plugin (a stepped control, free-running noise, its own bytes): always runs");
        {
            FakeSound::resetOptions();
            FakeSound::rewritesState = FakeSound::steppedDrive = FakeSound::analogNoise = true;
            const auto o = persist (FakeFormat::describe ("fake:Analog"), false, "fake analog");
            FakeSound::resetOptions();
            expectAll (o, "fake analog");
            expect (o.steppedParams > 0, "a stepped control read back a value other than the one asked for (and that's what's expected back)");
            if (real::paramsFromEnv().isEmpty())
                expectEquals (o.strongParams, 2, "both its parameters came back exactly as read back");
            expect (o.audioDiscriminating, "its sound check discriminates (the patch is loud enough apart)");
            expect (o.has ("audio: the reopened sound"), "(so it was asserted)");
        }

        beginTest ("negative control: a plugin that takes nothing it's given is caught, though it matches a reference given the same bytes");
        {
            FakeSound::resetOptions();
            FakeSound::rewritesState = FakeSound::steppedDrive = FakeSound::analogNoise = true;
            FakeSound::ignoreSetState = true;
            const auto o = persist (FakeFormat::describe ("fake:Analog"), false, "fake that ignores its settings");
            FakeSound::resetOptions();
            expect (o.passed ("state==reference"), "(the reference alone can't see it: it ignores them too)");
            expect (o.passed ("params==reference"), "(nor every parameter against the reference)");
            expect (! o.passed ("state!=default"), "caught: its state is a fresh one's");
            expect (! o.passed ("strong"), "caught: no edited parameter came back");
            expect (o.has ("audio: the reopened sound") ? ! o.passed ("audio: the reopened sound") : true, "and its sound, where asserted, is the default's");
            logMessage ("  negative control: " + juce::String (o.passed ("state!=default") ? "missed" : "caught") + " by state, "
                        + (o.passed ("strong") ? "missed" : "caught") + " by parameters, audio check "
                        + (o.has ("audio: the reopened sound") ? (o.passed ("audio: the reopened sound") ? "missed" : "caught") : "not asserted (not discriminating)"));
        }

        hostRef.reset();
        folder.deleteRecursively();
    }
};

static RigRealPluginPersistTests rigRealPluginPersistTests;
} // namespace
