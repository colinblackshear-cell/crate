// The dirty model while a show's hosted plugins open. A show's plugin opens asynchronously: the
// baseline is taken while it's still loading (with the show's tree for it, which every save
// writes until it's in), and once it's in it reports its state in its own bytes (an Arturia synth
// doesn't hand back the bytes it was given). RigDirty::pluginLanding moves only that slot's plugin
// entries in the baseline, and only when the baseline had what the show had there; everything
// you changed meanwhile stays a change.
//
// The fake plugin format (RigPluginFakes.h) holds each load until the test lets it finish, fails
// it, or finishes loads out of order; FakeSound::rewritesState makes the fakes do what Arturia's
// do. "Rig dirty while a show's plugin loads (real plugin)" does the main cases with a real
// plugin (KALEIDOBOX_TEST_PLUGIN, or this build's Sample Editor Instrument VST3), or says skipped.
//
// The gate is always waitUntilResolved, then getState().

#include "RigDirtyTestHelpers.h"
#include "PluginSlot.h"
#include "RigPluginFakes.h"

using namespace dirtytest;

namespace
{
using DS = RigDirty::State;
using pluginfakes::FakeFormat;
using pluginfakes::FakeSound;
using pluginfakes::pump;
using pluginfakes::pumpUntil;

enum class Where { synth, bass, stripFx, loopFx };

juce::String whereName (Where w)
{
    switch (w)
    {
        case Where::synth: return "Synth instrument";
        case Where::bass: return "Bass instrument";
        case Where::stripFx: return "strip effect";
        case Where::loopFx: return "loop effect";
    }
    return {};
}

PluginSlot& slotOf (RigEngine& e, Where w)
{
    switch (w)
    {
        case Where::bass: return e.getBassPlugin();
        case Where::stripFx: return e.getFx (RigEngine::synthStrip).plugin;
        case Where::loopFx: return e.getLoopFx (0).plugin;
        case Where::synth:
        default: return e.getSynthPlugin();
    }
}

// Where the slot's plugin is in the baseline (the current bank is the first: no banks here).
std::string prefixOf (Where w)
{
    switch (w)
    {
        case Where::bass: return "Show/Instrument[tool=Bass]/Plugin";
        case Where::stripFx: return "Show/Tool[name=" + RigEngine::toolName (RigEngine::fxTool + RigEngine::synthStrip).toStdString() + "]@state{xml}/Plugin";
        case Where::loopFx: return "Show/LoopBanks/Bank[0]/Loop[index=0]@fx{xml}/Plugin";
        case Where::synth:
        default: return "Show/Instrument[tool=Synth]/Plugin";
    }
}

FakeSound* fake (PluginSlot& s) { return dynamic_cast<FakeSound*> (s.getInstance()); }

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

// The slot's plugin as the show has it (any kind of slot).
juce::String pluginXml (const juce::ValueTree& show, Where w)
{
    switch (w)
    {
        case Where::bass: return xmlOf (instrumentOf (show, "Bass"));
        case Where::stripFx: return rackPluginXml (show.getChildWithProperty ("name", RigEngine::toolName (RigEngine::fxTool + RigEngine::synthStrip)).getProperty ("state").toString());
        case Where::loopFx:
            for (const auto c : show)
                if (c.hasType ("Loop") && (int) c.getProperty ("index", -1) == 0)
                    return rackPluginXml (c.getProperty ("fx").toString());
            return "(none)";
        case Where::synth:
        default: return xmlOf (instrumentOf (show));
    }
}

int landings (const RigDirty::Stats& s) { return s.pluginLandingsRebased + s.pluginLandingsLeft + s.pluginLandingsSame + s.pluginLandingsNoBaseline; }
} // namespace

//==============================================================================
struct RigDirtyPluginLoadTests final : juce::UnitTest
{
    RigDirtyPluginLoadTests() : juce::UnitTest ("Rig dirty while a show's plugin loads", "Rig") {}

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
            if (auto* fakeFormat = dynamic_cast<FakeFormat*> (f))
                format = fakeFormat;
        if (format == nullptr)
        {
            format = new FakeFormat();
            host().formats.addFormat (std::unique_ptr<juce::AudioPluginFormat> (format));
        }
    }
    void resetFakes()
    {
        format->hold = false;
        format->finishAll();
        format->missing.clear();
        format->failing.clear();
        FakeSound::throwOnGetState = FakeSound::emptyState = FakeSound::throwOnSetState = false;
        RigDirty::noLandingRebaseForTests = false;
        PluginSlot::keptLevelOverlayOffForTests = false;
        rigshow::hooks().at = nullptr;
        pump (20);
    }
    juce::File newFile (const juce::String& label) { return folder.getChildFile (juce::String (++fileCount) + " " + label + ".crateshow"); }

    //==========================================================================
    DS verdict (RigEngine& e, int timeoutMs = 20000)
    {
        e.getDirty().waitUntilResolved (timeoutMs);
        return e.getDirty().getState();
    }
    void expectState (RigEngine& e, DS want, const juce::String& what)
    {
        const auto got = verdict (e);
        expect (got == want, what + ": want " + stateName (want) + ", got " + stateName (got));
        if (got != want)
            for (auto& d : e.getDirty().differences (6))
                logMessage ("    differs: " + d.substring (0, 300));
    }
    // Several service cycles with nothing changed: each one clean (and resolved in time).
    void staysClean (RigEngine& e, const juce::String& what, int cycles = 6)
    {
        int clean = 0, resolved = 0;
        for (int i = 0; i < cycles; ++i)
        {
            juce::Thread::sleep (30);
            pump (5);
            e.getDirty().service();
            resolved += e.getDirty().waitUntilResolved (5000) ? 1 : 0;
            clean += e.getDirty().getState() == DS::clean ? 1 : 0;
        }
        expectEquals (resolved, cycles, what + ": every check resolved");
        expectEquals (clean, cycles, what + ": every check clean");
    }

    // A show with fake plugins in the given slots (each patch "Lead 7", its own settings moved, the
    // slot's level 0.6), saved as a document. Loaded by hand here (so not as Arturia rewrites them).
    juce::File makeShow (std::vector<std::pair<Where, juce::String>> plugins, const juce::String& label)
    {
        auto e = std::make_unique<RigEngine>();
        e->prepare (kRate, kBlock);
        for (const auto& [w, id] : plugins)
            slotOf (*e, w).load (FakeFormat::describe (id));
        pumpUntil ([&] { for (const auto& p : plugins) if (! slotOf (*e, p.first).isLoaded()) return false; return true; });
        for (const auto& [w, id] : plugins)
        {
            auto& s = slotOf (*e, w);
            if (auto* f = fake (s))
            {
                f->level = 0.42f;
                f->patch = "Lead 7";
                f->cutoff->value = 0.83f;
                f->drive->value = 0.27f;
            }
            for (int k = 0; k < PluginSlot::kMacros; ++k)
                s.setMacro (k, -1);
            s.setMacro (3, 1);
            s.level = 0.6f;
        }
        const auto f = newFile (label);
        expect (e->saveShowFile (f, rigshow::SaveKind::document).ok, "made " + label);
        return f;
    }
    juce::File makeShow (Where w, const juce::String& id = "fake:A") { return makeShow ({ { w, id } }, whereName (w) + " " + id); }

    // Opens it the way the app does (dirty on), with the plugin's load held.
    std::unique_ptr<RigEngine> openHeld (const juce::File& f)
    {
        format->hold = true;
        auto e = openSaved (f);
        pump (20);
        return e;
    }
    void land (const juce::String& id = {})
    {
        expect (format->finish (id), "a load to finish: " + id);
        pump (20);
    }

    //==========================================================================
    // Case 1, for any slot: clean while it loads, clean once it's in (its bytes its own). True if
    // it ended clean.
    bool openUnchanged (Where w, const juce::String& id = "fake:A", bool quiet = false)
    {
        const auto file = makeShow (w, id);
        const auto saved = pluginXml (readShow (file), w);
        auto e = openHeld (file);
        e->getDirty().resetStats();
        expect (! slotOf (*e, w).isLoaded() && format->waiting() == 1, "(held)");
        if (! quiet)
            expectState (*e, DS::clean, whereName (w) + ": while it loads");
        land();
        expect (slotOf (*e, w).isLoaded(), whereName (w) + ": in");
        const auto now = pluginXml (e->saveShow(), w);
        expect (now != saved, whereName (w) + ": (it reports its settings in its own bytes, not the show's)");
        const auto got = verdict (*e);
        if (! quiet)
        {
            expect (got == DS::clean, whereName (w) + ": clean once it's in, got " + stateName (got));
            for (auto& d : e->getDirty().differences (6))
                logMessage ("    differs: " + d.substring (0, 300));
            const auto st = e->getDirty().getStats();
            expectEquals (st.pluginLandingsRebased, 1, whereName (w) + ": its entries moved");
            expectEquals (st.pluginLandingsLeft, 0);
            logMessage ("  " + whereName (w) + ": landing " + juce::String (st.lastPluginLandingMs, 2) + " ms (two looks at the show), "
                        + juce::String (st.pluginLandingKeysMoved) + " keys moved; a check " + juce::String (st.lastCheckMs, 2) + " ms");
            staysClean (*e, whereName (w) + ": afterwards");
        }
        resetFakes();
        return got == DS::clean;
    }

    // Case 3, for any slot: another plugin picked while the show's loads (either order).
    void replaceWhileLoading (Where w)
    {
        const auto file = makeShow (w, "fake:A");
        for (bool newerFirst : { false, true })
        {
            auto e = openHeld (file);
            expectState (*e, DS::clean, whereName (w) + ": while it loads");
            e->getDirty().resetStats();
            const auto before = e->getDirty().baselineForTests();
            slotOf (*e, w).load (FakeFormat::describe ("fake:D"));
            pump (20);
            land (newerFirst ? "fake:D" : "fake:A");
            land (newerFirst ? "fake:A" : "fake:D");
            auto* f = fake (slotOf (*e, w));
            expect (f != nullptr && f->getName() == "D", whereName (w) + ": the one picked is in");
            expect (e->getDirty().baselineForTests() == before, whereName (w) + ": the old load never touched the baseline");
            expectEquals (landings (e->getDirty().getStats()), 0, whereName (w) + ": no show plugin landed");
            expectState (*e, DS::dirty, whereName (w) + ": replaced while it loaded (" + (newerFirst ? "picked first" : "show's first") + ")");
            resetFakes();
        }
    }

    // A level or bypass change while it loads (two slots, landing in either order).
    void twoSlots()
    {
        const auto file = makeShow ({ { Where::synth, "fake:A" }, { Where::stripFx, "fake:C" } }, "two slots");
        for (bool synthFirst : { true, false })
        {
            const juce::String order = synthFirst ? "synth first" : "effect first";
            {
                auto e = openHeld (file);
                expect (format->waiting() == 2);
                expectState (*e, DS::clean, "two loading");
                land (synthFirst ? "fake:A" : "fake:C");
                expectState (*e, DS::clean, "one in (" + order + ")");
                land();
                expectState (*e, DS::clean, "both in (" + order + ")");
                expectEquals (e->getDirty().getStats().pluginLandingsRebased, 2);
                resetFakes();
            }
            for (bool bypass : { false, true })
            {
                const juce::String what = juce::String (bypass ? "bypass" : "level") + " changed while it loaded (" + order + ")";
                auto e = openHeld (file);
                auto& s = slotOf (*e, Where::synth);
                if (bypass)
                    s.bypassed = true;
                else
                    s.level = 0.3f;
                // (Saved with the show's tree for it, with your level or bypass: a change already.)
                expectState (*e, DS::dirty, what + ": while it loads");
                land (synthFirst ? "fake:A" : "fake:C");
                land();
                expectState (*e, DS::dirty, what);
                if (bypass)
                    s.bypassed = false;
                else
                    s.level = 0.6f;
                expectState (*e, DS::clean, what + ", then put back");
                resetFakes();
            }
        }
    }

    // Baseline keys outside the slot are the same before and after its landing; its own moved.
    void othersUntouched (Where w)
    {
        const auto file = makeShow (w, "fake:A");
        auto e = openHeld (file);
        expectState (*e, DS::clean, whereName (w) + ": while it loads");
        const auto before = e->getDirty().baselineForTests();
        land();
        expectState (*e, DS::clean, whereName (w) + ": in");
        const auto after = e->getDirty().baselineForTests();
        const auto prefix = prefixOf (w);
        auto outside = [&prefix] (const RigDirty::Flat& f)
        {
            RigDirty::Flat o;
            for (const auto& [k, v] : f)
                if (k.compare (0, prefix.size(), prefix) != 0)
                    o[k] = v;
            return o;
        };
        int inside = 0, changed = 0;
        for (const auto& [k, v] : after)
            if (k.compare (0, prefix.size(), prefix) == 0)
            {
                ++inside;
                const auto it = before.find (k);
                changed += it == before.end() || it->second != v ? 1 : 0;
            }
        expect (inside > 0 && changed > 0, whereName (w) + ": its own entries moved (" + juce::String (changed) + " of " + juce::String (inside) + ")");
        expect (outside (before) == outside (after), whereName (w) + ": every other key exactly as it was");
        expectEquals ((int) outside (before).size(), (int) outside (after).size());
        resetFakes();
    }

    //==========================================================================
    void runTest() override
    {
        attachHost();
        folder = tempFolder ("RigDirtyPluginLoadTests");
        FakeSound::rewritesState = true;   // as Arturia's do

        beginTest ("1. a reopened, unchanged show: clean while its plugin loads, clean once it's in");
        openUnchanged (Where::synth);

        beginTest ("2. changes made while it loads stay changes: tempo, a drum edit, a loop recorded");
        {
            const auto file = makeShow (Where::synth);
            {
                auto e = openHeld (file);
                const double bpm = e->getTransport().getBpm();
                e->getTransport().setBpm (bpm + 7.0);
                expectState (*e, DS::dirty, "tempo changed while it loads");
                land();
                expectState (*e, DS::dirty, "tempo: still dirty once it's in");
                expectEquals (e->getDirty().getStats().pluginLandingsRebased, 1, "(the plugin's own entries still moved)");
                e->getTransport().setBpm (bpm);
                expectState (*e, DS::clean, "tempo put back: clean (only the plugin's entries had moved)");
                resetFakes();
            }
            {
                auto e = openHeld (file);
                auto p = e->getDrums().getEditedPattern();
                p.hits[1][3] = p.hits[1][3] != 0 ? 0 : 1;
                e->getDrums().setEditedPattern (p);
                expectState (*e, DS::dirty, "a drum step while it loads");
                land();
                expectState (*e, DS::dirty, "drum step: still dirty once it's in");
                resetFakes();
            }
            {
                auto e = openHeld (file);
                Runner r { *e };
                e->getTransport().play();
                r.input = true;
                e->getLooper().pressRecord (0);
                r.beats (1.0);
                auto s = e->getDirty().checkNow();
                expect (e->getLooper().getState (0) == RigLooper::State::recording, "(recording)");
                expect (s.state == DS::dirty && s.provisional, "recording while it loads: dirty, for now (" + stateName (s.state) + ")");
                land();
                r.beats (0.5);
                expect (verdict (*e) == DS::dirty && e->getDirty().getLastResolved().provisional, "recording, plugin in: dirty, for now");
                e->getLooper().pressRecord (0);
                for (int i = 0; i < 40 && e->getLooper().getState (0) != RigLooper::State::playing; ++i)
                    r.beats (0.5);
                r.input = false;
                r.beats (1.0);
                e->getTransport().stop();
                r.seconds (0.1);
                expect (e->getLooper().getState (0) == RigLooper::State::playing, "(the loop plays)");
                expectState (*e, DS::dirty, "a loop recorded while it loaded: dirty once both are done");
                expect (! e->getDirty().getLastResolved().provisional, "(for real)");
                resetFakes();
            }
        }

        beginTest ("3. another plugin picked while the show's loads (either order): dirty; the old load never touches the baseline");
        replaceWhileLoading (Where::synth);

        beginTest ("4. removed while it loads: dirty");
        {
            const auto file = makeShow (Where::synth);
            auto e = openHeld (file);
            e->getDirty().resetStats();
            e->getSynthPlugin().clear();
            format->finishAll();
            pump (20);
            expect (! e->getSynthPlugin().isLoaded());
            expectEquals (landings (e->getDirty().getStats()), 0, "no show plugin landed");
            expectState (*e, DS::dirty, "removed while it loaded");
            resetFakes();
        }

        beginTest ("5. a document save while it loads: clean, still clean once it's in, then a knob counts");
        {
            const auto file = makeShow (Where::synth);
            const auto saved = pluginXml (readShow (file), Where::synth);
            auto e = openHeld (file);
            const auto during = newFile ("saved while loading");
            const auto r = e->saveShowFile (during, rigshow::SaveKind::document);
            expect (r.ok, r.error);
            expectEquals (pluginXml (readShow (during), Where::synth), saved, "(the show's tree, byte for byte)");
            expectState (*e, DS::clean, "right after the save");
            land();
            expectState (*e, DS::clean, "once it's in");
            expectEquals (e->getDirty().getStats().pluginLandingsRebased, 1);
            staysClean (*e, "after the save and the landing");
            fake (e->getSynthPlugin())->cutoff->setValueNotifyingHost (0.1f);
            expectState (*e, DS::dirty, "its parameter moved");
            resetFakes();
        }

        beginTest ("6. a missing plugin, and one that fails to open: clean at once, settles, kept byte for byte");
        {
            for (auto how : { "missing", "failed", "wouldn't take the settings" })
            {
                const juce::String which (how);
                const auto file = makeShow (Where::synth, "fake:M");
                const auto saved = pluginXml (readShow (file), Where::synth);
                if (which == "missing") format->missing.add ("fake:M");
                if (which == "failed") format->failing.add ("fake:M");
                if (which != "missing" && which != "failed") FakeSound::throwOnSetState = true;
                auto e = openSaved (file);
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                expect (e->getDirty().waitUntilResolved (3000), which + ": resolves");
                expect (e->getDirty().getState() == DS::clean, which + ": clean at once (" + juce::String (juce::Time::getMillisecondCounterHiRes() - t0, 0) + " ms)");
                pump (100);
                expect (! e->getSynthPlugin().isLoaded(), which + ": not in");
                staysClean (*e, which);
                const auto again = newFile (which);
                expect (e->saveShowFile (again, rigshow::SaveKind::document).ok);
                expectEquals (pluginXml (readShow (again), Where::synth), saved, which + ": saved byte for byte");
                expectState (*e, DS::clean, which + ": after saving it");
                expectEquals (landings (e->getDirty().getStats()), 0, which + ": nothing landed");
                resetFakes();
            }
        }

        beginTest ("7. another show opened while the first one's plugin loads: the first's load never reaches the second's baseline");
        {
            const auto a = makeShow (Where::synth, "fake:A");
            const auto b = makeShow ({ { Where::synth, "fake:B" } }, "B");
            for (bool newerFirst : { false, true })
            {
                auto e = openHeld (a);
                expectState (*e, DS::clean, "A while it loads");
                // B opened the way the app opens a show.
                e->loadShow (readShow (b));
                e->readLoopAudio (b);
                pump (20);
                expectState (*e, DS::clean, "B, both loading");
                e->getDirty().resetStats();
                const auto baseB = e->getDirty().baselineForTests();
                if (! newerFirst)
                {
                    land ("fake:A");
                    expect (e->getDirty().baselineForTests() == baseB, "A's late load: B's baseline untouched");
                    expectEquals (landings (e->getDirty().getStats()), 0, "A's load didn't land");
                    expectState (*e, DS::clean, "B after A's late load");
                }
                land ("fake:B");
                if (newerFirst)
                    land ("fake:A");
                auto* f = fake (e->getSynthPlugin());
                expect (f != nullptr && f->getName() == "B", "B's plugin is in");
                expectEquals (landings (e->getDirty().getStats()), 1, "only B's landed");
                expectState (*e, DS::clean, juce::String ("B unchanged, everything in (") + (newerFirst ? "B first" : "A first") + ")");
                e->getTransport().setBpm (e->getTransport().getBpm() + 3.0);
                expectState (*e, DS::dirty, "then an edit");
                resetFakes();
            }
        }

        beginTest ("8. after a clean landing: a parameter, a preset, a replacement, a removal all count");
        {
            const auto file = makeShow (Where::synth);
            auto e = openHeld (file);
            land();
            expectState (*e, DS::clean, "in");
            auto* f = fake (e->getSynthPlugin());
            const float was = f->cutoff->getValue();
            f->cutoff->setValueNotifyingHost (0.2f);
            expectState (*e, DS::dirty, "a parameter");
            f->cutoff->setValueNotifyingHost (was);
            expectState (*e, DS::clean, "the parameter back");
            f->patch = "Other preset";
            expectState (*e, DS::dirty, "a preset (other state bytes)");
            f->patch = "Lead 7";
            expectState (*e, DS::clean, "the preset back");
            format->hold = false;
            e->getSynthPlugin().load (FakeFormat::describe ("fake:Z"));
            pumpUntil ([&] { return fake (e->getSynthPlugin()) != nullptr && fake (e->getSynthPlugin())->getName() == "Z"; });
            expectState (*e, DS::dirty, "replaced");
            e->getSynthPlugin().clear();
            expectState (*e, DS::dirty, "removed");
            resetFakes();
        }

        beginTest ("9. two plugins landing in either order: clean; a level or bypass changed while one loads: dirty");
        twoSlots();

        beginTest ("10. a plugin picked by hand on a clean show: dirty");
        {
            auto blank = std::make_unique<RigEngine>();
            blank->prepare (kRate, kBlock);
            const auto none = newFile ("no plugin");
            expect (blank->saveShowFile (none, rigshow::SaveKind::document).ok);
            auto e = openSaved (none);
            expectState (*e, DS::clean, "opened");
            e->getDirty().resetStats();
            e->getSynthPlugin().load (FakeFormat::describe ("fake:H"));
            pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); });
            expectEquals (landings (e->getDirty().getStats()), 0, "a plugin picked by hand isn't a show's");
            expectState (*e, DS::dirty, "picked by hand");
            // And over a show's own that's in: the same plugin, picked again by hand.
            const auto file = makeShow (Where::synth);
            auto e2 = openHeld (file);
            land();
            expectState (*e2, DS::clean, "the show's own in");
            format->hold = false;
            e2->getSynthPlugin().load (FakeFormat::describe ("fake:A"));
            pumpUntil ([&] { return fake (e2->getSynthPlugin()) != nullptr && ! fake (e2->getSynthPlugin())->restored; });
            expectState (*e2, DS::dirty, "the same plugin picked again by hand (its own defaults)");
            resetFakes();
        }

        beginTest ("11. a strip's effect, a loop's effect and a tool sound's plugin: the same");
        {
            openUnchanged (Where::stripFx, "fake:C");
            openUnchanged (Where::loopFx, "fake:K");
            openUnchanged (Where::bass, "fake:G");
            replaceWhileLoading (Where::stripFx);
            replaceWhileLoading (Where::loopFx);

            // A tool sound: its plugin is restored from the sound's tree the same way.
            const auto file = makeShow (Where::synth, "fake:T");
            juce::ValueTree sameSound, otherSound;
            {
                auto src = openHeld (file);   // (while it loads, the sound has the show's tree, byte for byte)
                sameSound = src->saveToolSound (RigEngine::synthStrip);
                resetFakes();
            }
            {
                const auto other = makeShow (Where::synth, "fake:U");
                auto src = openHeld (other);
                otherSound = src->saveToolSound (RigEngine::synthStrip);
                resetFakes();
            }
            expect (xmlOf (instrumentOf (sameSound)) == pluginXml (readShow (file), Where::synth), "(the sound has the show's own plugin tree)");
            {
                auto e = openHeld (file);
                expectState (*e, DS::clean, "tool sound: the show loading");
                e->getDirty().resetStats();
                expect (e->loadToolSound (sameSound), "the sound loaded");
                pump (20);
                expectState (*e, DS::clean, "tool sound: the show's own sound again, loading");
                format->finishAll();
                pump (20);
                expectEquals (landings (e->getDirty().getStats()), 1, "only the sound's plugin landed");
                expectEquals (e->getDirty().getStats().pluginLandingsRebased, 1);
                expectState (*e, DS::clean, "tool sound: the show's own sound again, in");
                resetFakes();
            }
            for (bool newerFirst : { false, true })
            {
                auto e = openHeld (file);
                expectState (*e, DS::clean, "tool sound: the show loading");
                e->getDirty().resetStats();
                expect (e->loadToolSound (otherSound));
                pump (20);
                land (newerFirst ? "fake:U" : "fake:T");
                if (format->waiting() > 0)
                    land();
                auto* f = fake (e->getSynthPlugin());
                expect (f != nullptr && f->getName() == "U", "the sound's plugin is in");
                expectEquals (e->getDirty().getStats().pluginLandingsRebased, 0, "nothing of the baseline moved");
                expectState (*e, DS::dirty, juce::String ("tool sound: another sound loaded while the show's plugin loaded (") + (newerFirst ? "sound first" : "show's first") + ")");
                resetFakes();
            }
        }

        beginTest ("12. negative control: with the correction off, an unchanged reopened show reads dirty once its plugin is in");
        {
            expect (openUnchanged (Where::synth, "fake:A", true), "on: clean");
            RigDirty::noLandingRebaseForTests = true;
            const bool clean = openUnchanged (Where::synth, "fake:A", true);
            RigDirty::noLandingRebaseForTests = false;
            expect (! clean, "off: dirty (the old fault is caught)");
        }

        beginTest ("13. a landing moves only its slot's entries in the baseline");
        {
            othersUntouched (Where::synth);
            othersUntouched (Where::stripFx);
            othersUntouched (Where::loopFx);
        }

        beginTest ("14. a show opened before its baseline (shown, then opened): the landing does nothing, the baseline has it");
        {
            const auto file = makeShow (Where::synth);
            format->hold = true;
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, kBlock);
            e->getDirty().setEnabled (true);
            e->loadShow (readShow (file));   // no baseline yet
            pump (20);
            e->getDirty().resetStats();
            land();
            expectEquals (e->getDirty().getStats().pluginLandingsNoBaseline, 1, "nothing to move yet");
            expect (e->getDirty().baselineForTests().empty(), "(still none)");
            e->getDirty().markOpened();
            expectState (*e, DS::clean, "the baseline taken after: clean");
            const auto base = e->getDirty().baselineForTests();
            const auto live = RigDirty::flattenForTests (e->saveShow());
            bool has = false;
            for (const auto& [k, v] : base)
                if (k.compare (0, prefixOf (Where::synth).size(), prefixOf (Where::synth)) == 0 && juce::String (k).contains ("@state"))
                    has = has || (live.count (k) > 0 && live.at (k) == v);
            expect (has, "the baseline has the plugin's own (landed) state");
            resetFakes();
        }

        beginTest ("16. a level changed while the show's plugin loads: dirty while it loads and once it's in; saved with it, clean after");
        {
            const auto file = makeShow (Where::synth);
            {
                auto e = openHeld (file);
                expectState (*e, DS::clean, "loading");
                e->getSynthPlugin().level = 0.25f;
                expectState (*e, DS::dirty, "level changed while it loads");
                land();
                expectState (*e, DS::dirty, "level: still dirty once it's in");
                expectEquals (e->getDirty().getStats().pluginLandingsRebased, 1, "(its state's entries still moved)");
                e->getSynthPlugin().level = 0.6f;
                expectState (*e, DS::clean, "level put back: clean");
                resetFakes();
            }
            for (bool bypass : { false, true })
            {
                // Saved while it loads, with the edit: clean after the save, and once it's in.
                const juce::String what = bypass ? "bypass" : "level";
                auto e = openHeld (file);
                auto& s = e->getSynthPlugin();
                if (bypass) s.bypassed = true; else s.level = 0.25f;
                expectState (*e, DS::dirty, what + " changed while it loads");
                const auto saved = newFile (what + " saved while loading");
                expect (e->saveShowFile (saved, rigshow::SaveKind::document).ok);
                expectState (*e, DS::clean, what + ": right after the save");
                land();
                expectState (*e, DS::clean, what + ": saved while loading, then in");
                expectEquals (e->getDirty().getStats().pluginLandingsRebased, 1, what + ": its state's entries moved");
                staysClean (*e, what + ": saved, then in");
                if (bypass) s.bypassed = false; else s.level = 0.6f;
                expectState (*e, DS::dirty, what + ": back to what the old file had: dirty (the saved file has yours)");
                resetFakes();
                auto again = openSaved (saved);
                pumpUntil ([&] { return again->getSynthPlugin().isLoaded(); });
                if (bypass)
                    expect (again->getSynthPlugin().bypassed.load(), "reopened: the bypass saved while it loaded");
                else
                    expectWithinAbsoluteError (again->getSynthPlugin().level.load(), 0.25f, 1.0e-6f, "reopened: the level saved while it loaded");
                expectState (*again, DS::clean, what + ": reopened, in");
                resetFakes();
            }
            {
                // The plugin landing during the save itself (after the save took the show, before
                // its baseline is set): the save's candidate moves too.
                auto e = openHeld (file);
                e->getSynthPlugin().level = 0.25f;
                juce::StringArray said;
                e->getDirty().log = [&said] (const juce::String& t) { said.add (t); };
                bool landedInSave = false;
                rigshow::hooks().at = [&] (const juce::String& point, int)
                {
                    if (point == "beforeCommit" && ! landedInSave)
                    {
                        landedInSave = format->finish();
                        pump (20);
                    }
                    return true;
                };
                const auto saved = newFile ("landed during the save");
                expect (e->saveShowFile (saved, rigshow::SaveKind::document).ok);
                rigshow::hooks().at = nullptr;
                expect (landedInSave && e->getSynthPlugin().isLoaded(), "(it landed during the save)");
                bool candidateMoved = false;
                for (const auto& t : said)
                    candidateMoved = candidateMoved || t.contains ("save(s) in flight");
                expect (candidateMoved, "the save's candidate moved too: " + said.joinIntoString (" | ").substring (0, 300));
                expectState (*e, DS::clean, "landed during the save: clean after it");
                e->getDirty().log = nullptr;
                resetFakes();
            }
            {
                // A missing plugin: a level change is yours, saved, and clean after the save.
                const auto missingFile = makeShow (Where::synth, "fake:M");
                format->missing.add ("fake:M");
                auto e = openSaved (missingFile);
                pump (100);
                expectState (*e, DS::clean, "missing");
                e->getSynthPlugin().level = 0.25f;
                expectState (*e, DS::dirty, "missing: level changed");
                const auto saved = newFile ("missing, level");
                expect (e->saveShowFile (saved, rigshow::SaveKind::document).ok);
                expectState (*e, DS::clean, "missing: saved");
                const auto tree = juce::parseXML (pluginXml (readShow (saved), Where::synth));
                expect (tree != nullptr && std::abs ((float) tree->getDoubleAttribute ("level") - 0.25f) < 1.0e-6f, "missing: the level is in the file");
                resetFakes();
            }
            {
                // Negative control: the old saving (the show's tree exactly) hides the edit.
                PluginSlot::keptLevelOverlayOffForTests = true;
                auto e = openHeld (file);
                e->getSynthPlugin().level = 0.25f;
                const auto got = verdict (*e);
                PluginSlot::keptLevelOverlayOffForTests = false;
                expect (got == DS::clean, "with the old saving, a level changed while it loads reads clean (the fault is caught): " + stateName (got));
                resetFakes();
            }
        }

        beginTest ("15. what a landing costs on the message thread, in a rich show (two looks at the show, against one check)");
        {
            juce::File file;
            {
                auto e = std::make_unique<RigEngine>();
                e->prepare (kRate, kBlock);
                buildRich (*e, 4);
                for (auto w : { Where::synth, Where::bass, Where::stripFx, Where::loopFx })
                    slotOf (*e, w).load (FakeFormat::describe ("fake:R" + juce::String ((int) w)));
                pumpUntil ([&] { return e->getSynthPlugin().isLoaded() && e->getBassPlugin().isLoaded() && e->getFx (RigEngine::synthStrip).plugin.isLoaded()
                                        && e->getLoopFx (0).plugin.isLoaded(); });
                file = newFile ("rich");
                expect (e->saveShowFile (file, rigshow::SaveKind::document).ok);
            }
            auto e = openHeld (file);
            expectState (*e, DS::clean, "rich, four loading");
            double maxLanding = 0, sumLanding = 0;
            int n = 0;
            while (format->waiting() > 0)
            {
                land();
                const auto st = e->getDirty().getStats();
                maxLanding = juce::jmax (maxLanding, st.lastPluginLandingMs);
                sumLanding += st.lastPluginLandingMs;
                ++n;
            }
            expectState (*e, DS::clean, "rich, all four in");
            const auto st = e->getDirty().getStats();
            expectEquals (st.pluginLandingsRebased, 4, "each one's entries moved");
            e->getDirty().resetStats();
            double checks = 0;
            for (int i = 0; i < 5; ++i)
            {
                e->getDirty().checkNow();
                checks += e->getDirty().getStats().lastCheckMs;
            }
            logMessage ("  rich show, 4 plugins: landing mean " + juce::String (sumLanding / juce::jmax (1, n), 2) + " ms, max " + juce::String (maxLanding, 2)
                        + " ms; one check (for comparison) " + juce::String (checks / 5.0, 2) + " ms");
            resetFakes();
        }

        FakeSound::rewritesState = false;
        resetFakes();
        expectEquals (FakeSound::deletedOffMessageThread, 0, "every fake plugin was deleted on the message thread");
        format = nullptr;
        hostRef.reset();
        folder.deleteRecursively();
    }
};

static RigDirtyPluginLoadTests rigDirtyPluginLoadTests;

//==============================================================================
// The main cases with a real plugin (KALEIDOBOX_TEST_PLUGIN, or this build's Sample Editor
// Instrument VST3): reopened unchanged, a tempo change while it loads, a parameter once it's in.
struct RigDirtyRealPluginLoadTests final : juce::UnitTest
{
    RigDirtyRealPluginLoadTests() : juce::UnitTest ("Rig dirty while a show's plugin loads (real plugin)", "Rig") {}

    juce::PluginDescription desc;
    bool isSampleInstrument = false;

    void expectState (RigEngine& e, DS want, const juce::String& what)
    {
        e.getDirty().waitUntilResolved (20000);
        const auto got = e.getDirty().getState();
        expect (got == want, what + ": want " + stateName (want) + ", got " + stateName (got));
        if (got != want)
            for (auto& d : e.getDirty().differences (6))
                logMessage ("    differs: " + d.substring (0, 300));
    }

    void runTest() override
    {
        beginTest ("a real plugin: clean once a reopened show's plugin is in; a tempo change while it loads and a parameter after count");
        juce::String why;
        if (! pluginfakes::real::findPlugin (desc, isSampleInstrument, why))
        {
            logMessage ("skipped: " + why);
            return;
        }
        logMessage ("plugin: " + desc.pluginFormatName + " " + desc.name + " (" + desc.fileOrIdentifier + ")");
        auto folder = tempFolder ("RigDirtyRealPluginLoadTests");
        const auto file = folder.getChildFile ("real.crateshow");
        {
            auto e = std::make_unique<RigEngine>();
            e->prepare (kRate, kBlock);
            auto& s = e->getSynthPlugin();
            bool called = false;
            juce::String error;
            s.load (desc, [&] (const juce::String& x) { error = x; called = true; });
            pumpUntil ([&] { return called; }, 60000);
            expectEquals (error, juce::String(), "loaded by hand");
            if (! s.isLoaded())
                return;
            if (isSampleInstrument)
            {
                const auto st = pluginfakes::real::sampleInstrumentState();
                s.getInstance()->setStateInformation (st.getData(), (int) st.getSize());
            }
            pump (300);
            s.level = 0.7f;
            expect (e->saveShowFile (file, rigshow::SaveKind::document).ok);
            pluginfakes::real::bundlePath (file);
        }
        const auto savedState = [&]
        {
            juce::MemoryBlock m;
            m.fromBase64Encoding (instrumentOf (readShow (file)).getProperty ("state").toString());
            return m;
        }();

        // 1: reopened unchanged.
        {
            auto e = openSaved (file);
            e->getDirty().resetStats();
            const bool atOpen = e->getSynthPlugin().isLoaded();
            if (! atOpen)
                expectState (*e, DS::clean, "while it loads");
            expect (pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }, 60000), "it loads");
            pump (300);
            juce::MemoryBlock now;
            e->getSynthPlugin().getInstance()->getStateInformation (now);
            logMessage ("  its state once it's in: " + juce::String (now == savedState ? "the bytes the show gave it" : "its own bytes (not the show's)"));
            expectState (*e, DS::clean, "reopened, unchanged, plugin in");
            const auto st = e->getDirty().getStats();
            logMessage ("  landings: moved " + juce::String (st.pluginLandingsRebased) + ", same " + juce::String (st.pluginLandingsSame) + ", left "
                        + juce::String (st.pluginLandingsLeft) + "; " + juce::String (st.lastPluginLandingMs, 2) + " ms");

            // 8: a parameter that's in its state.
            auto* plugin = e->getSynthPlugin().getInstance();
            juce::MemoryBlock before;
            plugin->getStateInformation (before);
            juce::AudioProcessorParameter* p = nullptr;
            for (auto* q : plugin->getParameters())
            {
                const float was = q->getValue();
                q->setValue (was > 0.5f ? 0.1f : 0.9f);
                juce::MemoryBlock moved;
                plugin->getStateInformation (moved);
                q->setValue (was);
                if (moved != before)
                {
                    p = q;
                    break;
                }
            }
            if (p != nullptr)
            {
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.1f : 0.9f);
                expectState (*e, DS::dirty, "its parameter '" + p->getName (30) + "' changed");
            }
            else
                logMessage ("  (no parameter in its state: the parameter case is skipped)");
        }
        // 2: tempo while it loads.
        {
            auto e = openSaved (file);
            const bool atOpen = e->getSynthPlugin().isLoaded();
            e->getTransport().setBpm (e->getTransport().getBpm() + 5.0);
            expectState (*e, DS::dirty, juce::String ("tempo changed ") + (atOpen ? "(it was in at once)" : "while it loads"));
            expect (pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }, 60000));
            pump (300);
            expectState (*e, DS::dirty, "tempo: still dirty once it's in");
        }
        // Negative control: with the correction off, what this plugin reports reads as a change.
        {
            RigDirty::noLandingRebaseForTests = true;
            auto e = openSaved (file);
            pumpUntil ([&] { return e->getSynthPlugin().isLoaded(); }, 60000);
            pump (300);
            e->getDirty().waitUntilResolved (20000);
            const auto got = e->getDirty().getState();
            RigDirty::noLandingRebaseForTests = false;
            logMessage ("  with the correction off: " + stateName (got));
            if (e->getDirty().getStats().pluginLandingsSame == 0)
                expect (got == DS::dirty, "off: dirty (this plugin reports its own bytes)");
        }
        pump (300);
        folder.deleteRecursively();
    }
};

static RigDirtyRealPluginLoadTests rigDirtyRealPluginLoadTests;
