// The dirty model with a hosted plugin in the show (an Arturia synth in the Synth's slot, an
// effect in a strip's effects). JUCE stamps a plugin's description with the time it was asked
// for ("infoUpdateTime"), so it differs at every check: the comparison leaves that one field out,
// and everything else about the plugin (who it is, its state) still counts.
//
// Three layers, so the logic runs everywhere:
//  - the comparison itself, on two descriptions that differ only in that time (no plugin);
//  - a stand-in plugin that stamps the time the way JUCE's VST3 and AU instances do (every OS);
//  - the repo's own "Sample Editor Instrument" hosted as a real VST3, and on the Mac as an Audio
//    Unit, when it has been built (the Mac's CI builds it; Linux CI builds only the tests, so
//    there the real-plugin part logs "skipped").

#include "RigDirtyTestHelpers.h"
#include "PluginSlot.h"

#include <thread>

#ifndef CRATE_TEST_INSTRUMENT_VST3
 #define CRATE_TEST_INSTRUMENT_VST3 ""
#endif
#ifndef CRATE_TEST_EFFECT_VST3
 #define CRATE_TEST_EFFECT_VST3 ""
#endif

namespace juce { namespace detail { bool dispatchNextMessageOnSystemQueue (bool); } }

using namespace dirtytest;

namespace
{
using DS = RigDirty::State;

struct Knob final : juce::AudioPluginInstance::HostedParameter
{
    float value = 0.5f;
    float get() const { return value; }
    float getValue() const override { return value; }
    void setValue (float v) override { value = v; }
    float getDefaultValue() const override { return 0.5f; }
    juce::String getName (int) const override { return "Cutoff"; }
    juce::String getLabel() const override { return {}; }
    float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }
    juce::String getParameterID() const override { return "cutoff"; }
};

// A stand-in for someone else's plugin. Its description is filled in like JUCE's own hosted
// instances fill theirs: infoUpdateTime is "now" at every call.
class StampingPlugin : public juce::AudioPluginInstance
{
public:
    StampingPlugin (juce::String n, int id, bool instrument)
        : AudioPluginInstance (instrument ? BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())
                                          : BusesProperties().withInput ("In", juce::AudioChannelSet::stereo()).withOutput ("Out", juce::AudioChannelSet::stereo())),
          name (std::move (n)), uid (id), isInstrument (instrument)
    {
        auto knob = std::make_unique<Knob>();
        cutoff = knob.get();
        addHostedParameter (std::move (knob));
    }

    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d.name = d.descriptiveName = name;
        d.manufacturerName = "Test";
        d.pluginFormatName = "Stamping";
        d.category = isInstrument ? "Instrument" : "Fx";
        d.version = "1.0";
        d.isInstrument = isInstrument;
        d.fileOrIdentifier = "/Plug-Ins/" + name;
        d.uniqueId = d.deprecatedUid = uid;
        d.lastFileModTime = juce::Time (1700000000000);
        d.lastInfoUpdateTime = juce::Time::getCurrentTime();   // what JUCE does
        d.numInputChannels = isInstrument ? 0 : 2;
        d.numOutputChannels = 2;
    }

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        if (isInstrument)
            b.clear();
    }
    using AudioPluginInstance::processBlock;
    const juce::String getName() const override { return name; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return isInstrument; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 4; }
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int i) override { program = i; }
    const juce::String getProgramName (int i) override { return "Patch " + juce::String (i + 1); }
    void changeProgramName (int, const juce::String&) override {}
    // Binary, not XML (as most plugins' state is): the model compares it as bytes.
    void getStateInformation (juce::MemoryBlock& m) override
    {
        juce::MemoryOutputStream out (m, false);
        out.writeString ("STAMP");
        out.writeFloat (cutoff->get());
        out.writeInt (program);
        out.write (extra.getData(), extra.getSize());
    }
    void setStateInformation (const void* data, int size) override
    {
        juce::MemoryInputStream in (data, (size_t) size, false);
        if (in.readString() != "STAMP")
            return;
        cutoff->value = in.readFloat();
        program = in.readInt();
        extra.reset();
        in.readIntoMemoryBlock (extra);
    }

    const juce::String name;
    const int uid;
    const bool isInstrument;
    Knob* cutoff = nullptr;
    int program = 0;
    juce::MemoryBlock extra;
};

void pump (int ms)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (2);
}

juce::String descriptionXml (const juce::PluginDescription& d)
{
    return d.createXml()->toString (juce::XmlElement::TextFormat().singleLine());
}

juce::ValueTree showWithDescription (const juce::String& description)
{
    juce::ValueTree show ("Show"), instrument ("Instrument"), plugin ("Plugin");
    instrument.setProperty ("tool", "Synth", nullptr);
    plugin.setProperty ("name", "Some Synth", nullptr);
    plugin.setProperty ("description", description, nullptr);
    plugin.setProperty ("state", juce::MemoryBlock ("0123456789abcdefghij", 20).toBase64Encoding(), nullptr);
    instrument.appendChild (plugin, nullptr);
    show.appendChild (instrument, nullptr);
    return show;
}

// The description in the show the engine would save now.
juce::String savedDescription (RigEngine& e, const char* tool = "Synth")
{
    return e.saveShow().getChildWithProperty ("tool", tool).getChildWithName ("Plugin").getProperty ("description").toString();
}
juce::String infoTimeOf (const juce::String& description)
{
    if (auto x = juce::XmlDocument::parse (description))
        return x->getStringAttribute ("infoUpdateTime");
    return {};
}
} // namespace

class RigDirtyPluginTests final : public juce::UnitTest
{
public:
    RigDirtyPluginTests() : juce::UnitTest ("Rig dirty with a hosted plugin", "Rig") {}

    DS verdict (RigEngine& e) { return settle (e).state; }

    void expectState (RigEngine& e, DS want, const juce::String& what)
    {
        const auto got = verdict (e);
        expect (got == want, what + ": want " + stateName (want) + ", got " + stateName (got));
        if (got != want)
            for (auto& d : e.getDirty().differences (6))
                logMessage ("    differs: " + d.substring (0, 300));
    }

    // 20 checks over more than a second with nothing changed (the description's time moves on).
    void stillClean (RigEngine& e, const juce::String& what)
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        int clean = 0;
        for (int i = 0; i < 20; ++i)
        {
            juce::Thread::sleep (60);
            e.getDirty().service();
            clean += verdict (e) == DS::clean ? 1 : 0;
        }
        const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
        expect (ms > 1000.0, "more than a second passed");
        expectEquals (clean, 20, what + ": 20 checks over " + juce::String (ms, 0) + " ms, all clean");
    }

    std::unique_ptr<RigEngine> newEngine()
    {
        auto e = std::make_unique<RigEngine>();
        e->prepare (kRate, kBlock);
        e->getDirty().setEnabled (true);
        e->getLooper().setLayerAudio (0, noise ((int) kRate * 2, 5), 4.0);
        return e;
    }

    void runTest() override
    {
        folder = tempFolder ("RigDirtyPluginTests");

        beginTest ("The comparison leaves out only the time JUCE stamps on a plugin's description");
        {
            juce::PluginDescription d;
            d.name = d.descriptiveName = "Some Synth";
            d.pluginFormatName = "AudioUnit";
            d.manufacturerName = "Arturia";
            d.version = "2.1";
            d.category = "Synth";
            d.fileOrIdentifier = "AudioUnit:Synths/aumu,Pig3,Artu";
            d.uniqueId = d.deprecatedUid = 0x1234;
            d.isInstrument = true;
            d.numOutputChannels = 2;
            d.lastFileModTime = juce::Time (1700000000000);
            d.lastInfoUpdateTime = juce::Time (1800000000000);
            auto later = d;
            later.lastInfoUpdateTime = juce::Time (1800000003456);
            const auto a = RigDirty::flattenForTests (showWithDescription (descriptionXml (d)));
            const auto b = RigDirty::flattenForTests (showWithDescription (descriptionXml (later)));
            expect (descriptionXml (d) != descriptionXml (later), "the written descriptions differ (in the time)");
            expect (a == b, "the same plugin a moment later compares equal");
            const auto key = std::string ("Show/Instrument[tool=Synth]/Plugin[name=Some Synth]@description");
            expect (a.count (key) == 1 && juce::String (a.at (key)).contains ("Arturia"), "the description is still compared");
            expect (a.count (key) == 1 && ! juce::String (a.at (key)).contains ("infoUpdateTime"), "without its time");

            // Who the plugin is still counts, field by field.
            auto differs = [&] (std::function<void (juce::PluginDescription&)> change, const char* what)
            {
                auto other = later;
                change (other);
                expect (RigDirty::flattenForTests (showWithDescription (descriptionXml (other))) != a, juce::String (what) + " counts");
            };
            differs ([] (auto& x) { x.name = x.descriptiveName = "Other Synth"; }, "the name");
            differs ([] (auto& x) { x.pluginFormatName = "VST3"; }, "the format");
            differs ([] (auto& x) { x.fileOrIdentifier = "AudioUnit:Synths/aumu,Jup8,Artu"; }, "the file or identifier");
            differs ([] (auto& x) { x.uniqueId = 0x9999; }, "the unique id");
            differs ([] (auto& x) { x.deprecatedUid = 0x9999; }, "the old uid");
            differs ([] (auto& x) { x.manufacturerName = "Someone"; }, "the manufacturer");
            differs ([] (auto& x) { x.version = "2.2"; }, "the version");
            differs ([] (auto& x) { x.numOutputChannels = 4; }, "the outputs");
            differs ([] (auto& x) { x.lastFileModTime = juce::Time (1700000099000); }, "the plugin file's date (only changes when the plugin is updated)");
            {
                // Its state too, byte for byte.
                auto s = showWithDescription (descriptionXml (later));
                s.getChild (0).getChild (0).setProperty ("state", juce::MemoryBlock ("0123456789abcdefghiJ", 20).toBase64Encoding(), nullptr);
                expect (RigDirty::flattenForTests (s) != a, "the state counts");
            }

            {
                // Binary state: a byte after a zero byte, and bytes that aren't text, count.
                auto withState = [&] (const juce::MemoryBlock& m)
                {
                    auto s = showWithDescription (descriptionXml (later));
                    s.getChild (0).getChild (0).setProperty ("state", m.toBase64Encoding(), nullptr);
                    return RigDirty::flattenForTests (s);
                };
                const char x1[] = { 'A', 'B', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };
                char x2[sizeof (x1)];
                std::memcpy (x2, x1, sizeof (x1));
                x2[sizeof (x1) - 1] = 18;
                expect (withState ({ x1, sizeof (x1) }) != withState ({ x2, sizeof (x2) }), "state bytes after a zero byte count");
                const unsigned char y1[] = { 0xff, 0xfe, 0x80, 0x81, 0xc0, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0 };
                unsigned char y2[sizeof (y1)];
                std::memcpy (y2, y1, sizeof (y1));
                y2[2] = 0x82;
                expect (withState ({ y1, sizeof (y1) }) != withState ({ y2, sizeof (y2) }), "state bytes that aren't text count");
                expect (withState ({ y1, sizeof (y1) }) == withState ({ y1, sizeof (y1) }), "the same bytes compare equal");

                // A hosted VST3's state as JUCE writes it: XML with the plugin's own state as text.
                auto vst3State = [] (const juce::String& component)
                {
                    juce::XmlElement x ("VST3PluginState");
                    x.createNewChildElement ("IComponent")->addTextElement (component);
                    x.createNewChildElement ("IEditController")->addTextElement ("12.AAAAAAAAAAAAAAAA");
                    juce::MemoryBlock m;
                    juce::AudioProcessor::copyXmlToBinary (x, m);
                    return m;
                };
                expect (withState (vst3State ("40.abcdefghijklmnop")) != withState (vst3State ("40.abcdefghijklmnoq")), "a VST3's state text counts");
                expect (withState (vst3State ("40.abcdefghijklmnop")) == withState (vst3State ("40.abcdefghijklmnop")), "the same text compares equal");
            }

            // The old comparison (the negative control): the time made them differ.
            RigDirty::comparePluginInfoTimeForTests = true;
            const auto oldA = RigDirty::flattenForTests (showWithDescription (descriptionXml (d)));
            const auto oldB = RigDirty::flattenForTests (showWithDescription (descriptionXml (later)));
            RigDirty::comparePluginInfoTimeForTests = false;
            expect (oldA != oldB, "with the time compared, they differ");

            // Not a plugin description: compared as written.
            juce::ValueTree s ("Show");
            s.setProperty ("description", "<NOTPLUGIN infoUpdateTime=\"1\"/>", nullptr);
            expectEquals (juce::String (RigDirty::flattenForTests (s).at ("Show@description")), juce::String ("<NOTPLUGIN infoUpdateTime=\"1\"/>"));
        }

        beginTest ("A stand-in plugin that stamps the time: clean after a save, its edits count");
        {
            auto e = newEngine();
            auto inst = std::make_unique<StampingPlugin> ("Stamp Synth", 11, true);
            auto* synth = inst.get();
            e->getSynthPlugin().setInstance (std::move (inst));
            auto fxInst = std::make_unique<StampingPlugin> ("Stamp FX", 12, false);
            auto* fx = fxInst.get();
            e->getFx (RigEngine::synthStrip).plugin.setInstance (std::move (fxInst));
            Runner r { *e };
            r.seconds (0.05);

            const auto file = folder.getChildFile ("Stamp.crateshow");
            expect (e->saveShowFile (file, rigshow::SaveKind::document).ok, "saved");
            expectState (*e, DS::clean, "right after the save");
            {
                const auto d1 = savedDescription (*e);
                juce::Thread::sleep (20);
                const auto d2 = savedDescription (*e);
                expect (d1 != d2 && infoTimeOf (d1) != infoTimeOf (d2), "the show's description has a new time at each call (as JUCE writes it)");
            }
            stillClean (*e, "nothing changed");

            const float was = synth->cutoff->get();
            synth->cutoff->setValueNotifyingHost (0.9f);
            expectState (*e, DS::dirty, "the synth plugin's knob turned");
            synth->cutoff->setValueNotifyingHost (was);
            expectState (*e, DS::clean, "turned back (its state bytes are the same again)");

            fx->cutoff->setValueNotifyingHost (0.1f);
            expectState (*e, DS::dirty, "the effect plugin's knob turned");
            fx->cutoff->setValueNotifyingHost (0.5f);
            expectState (*e, DS::clean, "turned back");

            synth->setCurrentProgram (2);
            expectState (*e, DS::dirty, "another program");
            synth->setCurrentProgram (0);
            expectState (*e, DS::clean, "the program back");

            {
                juce::MemoryBlock before, other;
                synth->getStateInformation (before);
                synth->extra = juce::MemoryBlock ("preset", 6);
                synth->getStateInformation (other);
                synth->setStateInformation (before.getData(), (int) before.getSize());
                expectState (*e, DS::clean, "state put back as it was");
                synth->setStateInformation (other.getData(), (int) other.getSize());
                expectState (*e, DS::dirty, "a preset loaded (different state bytes)");
                synth->setStateInformation (before.getData(), (int) before.getSize());
                expectState (*e, DS::clean, "the old state back");
            }

            e->getSynthPlugin().setInstance (std::make_unique<StampingPlugin> ("Other Synth", 13, true));
            expectState (*e, DS::dirty, "replaced with a different plugin");
            e->getSynthPlugin().setInstance (std::make_unique<StampingPlugin> ("Stamp Synth", 11, true));
            expectState (*e, DS::clean, "the same plugin again, as saved");
            e->getSynthPlugin().clear();
            expectState (*e, DS::dirty, "the plugin removed");
            e->getSynthPlugin().setInstance (std::make_unique<StampingPlugin> ("Stamp Synth", 11, true));
            e->getFx (RigEngine::synthStrip).plugin.clear();
            expectState (*e, DS::dirty, "the effect plugin removed");
        }

        beginTest ("Negative control: comparing the time again makes a plugin show dirty right after its save");
        {
            auto e = newEngine();
            e->getSynthPlugin().setInstance (std::make_unique<StampingPlugin> ("Stamp Synth", 11, true));
            Runner r { *e };
            r.seconds (0.05);
            RigDirty::comparePluginInfoTimeForTests = true;
            expect (e->saveShowFile (folder.getChildFile ("Control.crateshow"), rigshow::SaveKind::document).ok);
            juce::Thread::sleep (20);
            const auto got = verdict (*e);
            RigDirty::comparePluginInfoTimeForTests = false;
            expect (got == DS::dirty, "the old comparison: dirty right after saving (got " + stateName (got) + ")");
            expect (e->saveShowFile (folder.getChildFile ("Control.crateshow"), rigshow::SaveKind::document).ok);
            juce::Thread::sleep (20);
            expectState (*e, DS::clean, "the comparison as it is now, after another save");
        }

        realPlugin ("VST3", juce::String (CRATE_TEST_INSTRUMENT_VST3), juce::String (CRATE_TEST_EFFECT_VST3));
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        realPlugin ("AudioUnit", "AudioUnit:Synths/aumu,CSei,Cblk", "AudioUnit:Effects/aumf,CSed,Cblk");
       #endif

        folder.deleteRecursively();
    }

    //==========================================================================
    // The repo's own instrument, hosted for real (the description is JUCE's own).
    std::unique_ptr<juce::AudioPluginFormat> makeFormat (const juce::String& kind)
    {
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        if (kind == "AudioUnit")
            return std::make_unique<juce::AudioUnitPluginFormat>();
       #endif
       #if JUCE_PLUGINHOST_VST3
        if (kind == "VST3")
            return std::make_unique<juce::VST3PluginFormat>();
       #endif
        return nullptr;
    }

    std::unique_ptr<juce::AudioPluginInstance> make (juce::AudioPluginFormat& format, const juce::String& where, juce::PluginDescription& found)
    {
        if (where.isEmpty() || (format.getName() == "VST3" && ! juce::File (where).exists()))
            return nullptr;
        juce::OwnedArray<juce::PluginDescription> types;
        format.findAllTypesForFile (types, where);
        if (types.isEmpty())
            return nullptr;
        found = *types[0];
        juce::String error;
        auto inst = format.createInstanceFromDescription (found, kRate, kBlock, error);
        if (inst == nullptr)
            logMessage ("  couldn't open " + found.name + ": " + error);
        return inst;
    }

    void realPlugin (const juce::String& kind, const juce::String& instrumentAt, const juce::String& effectAt)
    {
        beginTest ("The repo's own instrument as a real " + kind + ": clean after a save, its edits count, saved and reopened");
        auto format = makeFormat (kind);
        juce::PluginDescription found;
        auto inst = format != nullptr ? make (*format, instrumentAt, found) : nullptr;
        if (inst == nullptr)
        {
            logMessage ("  no " + kind + " test instrument at '" + instrumentAt + "' (build SampleInstrument_" + (kind == "VST3" ? "VST3" : "AU")
                        + " first): skipped" + (kind == "AudioUnit" ? " (it must be installed for the system to find it)" : ""));
            return;
        }
        logMessage ("  hosting '" + found.name + "' (" + found.pluginFormatName + ", " + found.fileOrIdentifier + ")");
        auto* plugin = inst.get();
        auto e = newEngine();
        e->getSynthPlugin().setInstance (std::move (inst));
        Runner r { *e };
        r.seconds (0.05);

        const auto file = folder.getChildFile ("Real " + kind + ".crateshow");
        expect (e->saveShowFile (file, rigshow::SaveKind::document).ok, "saved");
        expectState (*e, DS::clean, kind + ": right after the save");
        {
            const auto d1 = savedDescription (*e);
            juce::Thread::sleep (1100);
            const auto d2 = savedDescription (*e);
            logMessage ("  " + kind + " description's infoUpdateTime 1.1 s apart: " + infoTimeOf (d1) + " -> " + infoTimeOf (d2)
                        + (d1 == d2 ? " (same)" : " (differs)"));
            auto withoutTime = [] (const juce::String& s) { auto x = juce::XmlDocument::parse (s); x->removeAttribute ("infoUpdateTime"); return x->toString(); };
            expect (withoutTime (d1) == withoutTime (d2), kind + ": apart from that time, the description is the same at every call");
        }
        stillClean (*e, kind + ": nothing changed");

        // Play it a while (notes in): its state doesn't drift.
        e->getTransport().play();
        for (int b = 0; b < 8; ++b)
        {
            e->addMidiNow (juce::MidiMessage::noteOn (1, 60 + b, (juce::uint8) 100));
            r.beats (0.5);
            e->addMidiNow (juce::MidiMessage::noteOff (1, 60 + b));
            r.beats (0.5);
        }
        e->getTransport().stop();
        r.seconds (0.1);
        expectState (*e, DS::clean, kind + ": played for 8 beats");

        // A parameter the plugin keeps in its state (not the VST3 wrapper's Bypass or MIDI CC ones).
        auto& params = plugin->getParameters();
        logMessage ("  " + juce::String (params.size()) + " parameters, " + juce::String (plugin->getNumPrograms()) + " programs");
        juce::AudioProcessorParameter* p = nullptr;
        juce::MemoryBlock before;
        plugin->getStateInformation (before);
        for (auto* q : params)
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
        expect (p != nullptr, kind + ": a parameter that's in the plugin's state");
        if (p != nullptr)
        {
            const float was = p->getValue();
            logMessage ("  turning '" + p->getName (40) + "' (was " + juce::String (was, 3) + ")");
            p->setValueNotifyingHost (was > 0.5f ? 0.1f : 0.9f);
            expectState (*e, DS::dirty, kind + ": its parameter '" + p->getName (30) + "' changed");
            juce::MemoryBlock other;
            plugin->getStateInformation (other);
            p->setValueNotifyingHost (was);
            juce::MemoryBlock after;
            plugin->getStateInformation (after);
            const bool sameBytes = before == after;
            logMessage ("  state bytes after the parameter went back: " + juce::String (sameBytes ? "identical" : "different"));
            if (sameBytes)
                expectState (*e, DS::clean, kind + ": the parameter back");
            else
                logMessage ("  (the plugin's state doesn't come back byte for byte: dirty is right; verdict " + stateName (verdict (*e)) + ")");

            // A preset: other state bytes, loaded with setStateInformation.
            plugin->setStateInformation (other.getData(), (int) other.getSize());
            expectState (*e, DS::dirty, kind + ": a preset loaded (setStateInformation, different bytes)");
            plugin->setStateInformation (before.getData(), (int) before.getSize());
            juce::MemoryBlock back;
            plugin->getStateInformation (back);
            if (back == before)
                expectState (*e, DS::clean, kind + ": the old state back");
            else
                logMessage ("  (the plugin doesn't give back the state it was given byte for byte; verdict " + stateName (verdict (*e)) + ")");
        }
        if (plugin->getNumPrograms() > 1)
        {
            const int was = plugin->getCurrentProgram();
            plugin->setCurrentProgram (was == 0 ? 1 : 0);
            logMessage ("  another program: " + stateName (verdict (*e)) + " (only dirty if the plugin's state has the program)");
            plugin->setCurrentProgram (was);
        }

        // Replaced by a different plugin, then removed.
        {
            juce::PluginDescription otherFound;
            auto other = make (*format, effectAt, otherFound);
            if (other != nullptr)
            {
                e->getSynthPlugin().setInstance (std::move (other));
                expectState (*e, DS::dirty, kind + ": replaced with '" + otherFound.name + "'");
            }
            else
            {
                e->getSynthPlugin().setInstance (std::make_unique<StampingPlugin> ("Stamp Synth", 11, true));
                expectState (*e, DS::dirty, kind + ": replaced with a stand-in");
            }
            e->getSynthPlugin().clear();
            expectState (*e, DS::dirty, kind + ": removed");
        }

        // Saved and reopened, the way the app opens a show (the plugin opens asynchronously).
        {
            auto again = openSaved (file);
            const bool atOpen = again->getSynthPlugin().isLoaded();
            logMessage ("  reopened: left closed (froze last time): '" + juce::SharedResourcePointer<PluginHost>()->takeLeftClosed().joinIntoString (", ") + "'");
            for (int i = 0; i < 100 && ! again->getSynthPlugin().isLoaded(); ++i)
                pump (50);
            Runner r2 { *again };
            r2.seconds (0.2);
            const auto got = verdict (*again);
            logMessage ("  reopened: plugin loaded at the baseline " + juce::String (atOpen ? "yes" : "no") + ", after 5 s "
                        + juce::String (again->getSynthPlugin().isLoaded() ? "yes" : "no") + "; verdict " + stateName (got));
            for (auto& d : again->getDirty().differences (6))
                logMessage ("    differs: " + d.substring (0, 300));
            if (again->getSynthPlugin().isLoaded())
            {
                // Whatever else the reopen does (see the report: the plugin opens after the
                // baseline), its description compares equal to the file's.
                auto xml = juce::parseXML (file);
                const auto saved = juce::ValueTree::fromXml (*xml).getChildWithProperty ("tool", "Synth").getChildWithName ("Plugin").getProperty ("description").toString();
                const auto now = savedDescription (*again);
                expect (RigDirty::flattenForTests (showWithDescription (saved)) == RigDirty::flattenForTests (showWithDescription (now)),
                        kind + ": the reopened plugin's description compares equal to the saved one");
                // Saved again now that it's open: clean, and stays clean.
                expect (again->saveShowFile (folder.getChildFile ("Real " + kind + " again.crateshow"), rigshow::SaveKind::document).ok);
                expectState (*again, DS::clean, kind + ": reopened, plugin open, saved");
                stillClean (*again, kind + ": reopened and saved");
            }
        }
    }

    juce::File folder;
};

static RigDirtyPluginTests rigDirtyPluginTests;
