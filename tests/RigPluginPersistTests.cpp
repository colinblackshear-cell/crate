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

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#else
namespace juce { namespace detail { bool dispatchNextMessageOnSystemQueue (bool); } }
#endif

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// Delivers what's been posted to the message thread, for `ms`.
void pump (int ms = 30)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    do
    {
       #if JUCE_MAC
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.001, true);
       #else
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (1);
       #endif
    } while (juce::Time::getMillisecondCounter() < end);
}

bool pumpUntil (std::function<bool()> done, int timeoutMs = 20000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (! done() && juce::Time::getMillisecondCounter() < end)
        pump (5);
    return done();
}

//==============================================================================
struct FakeParam final : juce::AudioPluginInstance::HostedParameter
{
    FakeParam (juce::String i, float d) : id (std::move (i)), value (d), def (d) {}
    float getValue() const override { return value; }
    void setValue (float v) override { value = v; }
    float getDefaultValue() const override { return def; }
    juce::String getName (int n) const override { return id.substring (0, n); }
    juce::String getLabel() const override { return {}; }
    float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }
    juce::String getParameterID() const override { return id; }
    juce::String id;
    float value, def;
};

// A "synth": a sine per note, its loudness and timbre its settings, so its sound is its state.
class FakeSound final : public juce::AudioPluginInstance
{
public:
    explicit FakeSound (const juce::PluginDescription& d)
        : AudioPluginInstance (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo())), desc (d)
    {
        auto c = std::make_unique<FakeParam> ("Cutoff", 0.5f);
        auto r = std::make_unique<FakeParam> ("Drive", 0.1f);
        cutoff = c.get();
        drive = r.get();
        addHostedParameter (std::move (c));
        addHostedParameter (std::move (r));
        ++alive;
    }
    ~FakeSound() override
    {
        --alive;
        if (! juce::MessageManager::getInstance()->isThisTheMessageThread())
            ++deletedOffMessageThread;
    }
    void fillInPluginDescription (juce::PluginDescription& d) const override { d = desc; }
    void prepareToPlay (double r, int) override { rate = r; prepared = true; }
    void releaseResources() override { prepared = false; }
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        b.clear();
        int pos = 0;
        auto play = [&] (int end)
        {
            for (; pos < end; ++pos)
            {
                float v = 0.0f;
                if (note >= 0)
                {
                    v = level * cutoff->value * (float) std::sin (phase) + drive->value * 0.1f * (float) std::sin (2.0 * phase);
                    phase = std::fmod (phase + step, juce::MathConstants<double>::twoPi);
                }
                for (int ch = 0; ch < b.getNumChannels(); ++ch)
                    b.setSample (ch, pos, v);
            }
        };
        for (const auto m : midi)
        {
            play (juce::jlimit (pos, b.getNumSamples(), m.samplePosition));
            const auto msg = m.getMessage();
            if (msg.isNoteOn())
            {
                note = msg.getNoteNumber();
                phase = 0.0;
                step = juce::MathConstants<double>::twoPi * juce::MidiMessage::getMidiNoteInHertz (note) / rate;
            }
            else if (msg.isNoteOff() && msg.getNoteNumber() == note)
                note = -1;
        }
        play (b.getNumSamples());
    }
    using AudioPluginInstance::processBlock;
    const juce::String getName() const override { return desc.name; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& m) override
    {
        if (throwOnGetState)
            throw std::runtime_error ("fake getStateInformation failure");
        if (emptyState)
            return;
        juce::MemoryOutputStream o (m, false);
        o.writeString ("FAKE1");
        o.writeFloat (level);
        o.writeString (patch);
        o.writeFloat (cutoff->value);
        o.writeFloat (drive->value);
        for (int i = 0; i < 3000; ++i)   // (a few KB, as a real plugin's)
            o.writeByte ((char) ((i * 31 + patch.hashCode()) & 0xff));
    }
    void setStateInformation (const void* data, int size) override
    {
        if (throwOnSetState)
            throw std::runtime_error ("fake setStateInformation failure");
        lastReceived.replaceAll (data, (size_t) size);
        receivedWhilePrepared = prepared;
        juce::MemoryInputStream in (data, (size_t) size, false);
        if (in.readString() != "FAKE1")
            return;
        level = in.readFloat();
        patch = in.readString();
        cutoff->value = in.readFloat();
        drive->value = in.readFloat();
    }

    juce::PluginDescription desc;
    float level = 0.1f;
    juce::String patch = "Init";
    FakeParam* cutoff = nullptr;
    FakeParam* drive = nullptr;
    bool prepared = false;
    double rate = kRate, phase = 0.0, step = 0.0;
    int note = -1;

    static inline int alive = 0, deletedOffMessageThread = 0;
    static inline bool throwOnGetState = false, emptyState = false, throwOnSetState = false;
    static inline juce::MemoryBlock lastReceived;
    static inline bool receivedWhilePrepared = false;
};

// Makes fake plugins ("fake:<name>"). `hold`: each load waits until the test finishes it.
class FakeFormat final : public juce::AudioPluginFormat
{
public:
    juce::StringArray missing;   // "not installed here"
    juce::StringArray failing;   // installed, but fails to initialise
    bool hold = false;

    juce::String getName() const override { return "Fake"; }
    void findAllTypesForFile (juce::OwnedArray<juce::PluginDescription>& r, const juce::String& id) override
    {
        if (fileMightContainThisPluginType (id))
            r.add (new juce::PluginDescription (describe (id)));
    }
    static juce::PluginDescription describe (const juce::String& id)
    {
        juce::PluginDescription d;
        d.name = id.fromFirstOccurrenceOf (":", false, false);
        d.manufacturerName = "Test";
        d.pluginFormatName = "Fake";
        d.fileOrIdentifier = id;
        d.isInstrument = true;
        d.uniqueId = d.deprecatedUid = (int) id.hashCode();
        d.numOutputChannels = 2;
        return d;
    }
    bool fileMightContainThisPluginType (const juce::String& id) override { return id.startsWith ("fake:") && ! missing.contains (id); }
    juce::String getNameOfPluginFromIdentifier (const juce::String& id) override { return id; }
    bool pluginNeedsRescanning (const juce::PluginDescription&) override { return false; }
    bool doesPluginStillExist (const juce::PluginDescription& d) override { return fileMightContainThisPluginType (d.fileOrIdentifier); }
    bool canScanForPlugins() const override { return false; }
    bool isTrivialToScan() const override { return true; }
    juce::StringArray searchPathsForPlugins (const juce::FileSearchPath&, bool, bool) override { return {}; }
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
    bool requiresUnblockedMessageThreadDuringCreation (const juce::PluginDescription&) const override { return false; }

    int waiting() const { return (int) pending.size(); }
    // Finishes the oldest held load (of `id`, if given). False if there's none. (Loads asked for
    // are posted first: what's on its way is delivered, and held, before.)
    bool finish (const juce::String& id = {})
    {
        pump (10);
        for (size_t i = 0; i < pending.size(); ++i)
            if (id.isEmpty() || pending[i].d.fileOrIdentifier == id)
            {
                auto p = std::move (pending[i]);
                pending.erase (pending.begin() + (std::ptrdiff_t) i);
                make (p.d, p.cb);
                return true;
            }
        return false;
    }
    void finishAll()
    {
        while (finish()) {}
    }

private:
    struct Pending { juce::PluginDescription d; PluginCreationCallback cb; };
    std::vector<Pending> pending;

    void make (const juce::PluginDescription& d, const PluginCreationCallback& cb)
    {
        if (! fileMightContainThisPluginType (d.fileOrIdentifier))
            cb (nullptr, "not installed");
        else if (failing.contains (d.fileOrIdentifier))
            cb (nullptr, "it failed to initialise");
        else
            cb (std::make_unique<FakeSound> (d), {});
    }
    void createPluginInstance (const juce::PluginDescription& d, double, int, PluginCreationCallback cb) override
    {
        if (hold)
            pending.push_back ({ d, std::move (cb) });
        else
            make (d, cb);
    }
};

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
// The main cases with a real plugin.
struct RigRealPluginPersistTests final : juce::UnitTest
{
    RigRealPluginPersistTests() : juce::UnitTest ("Rig plugin persistence (real plugin)", "Rig") {}

    juce::File folder;
    bool isSampleInstrument = false;
    juce::PluginDescription desc;

    static void addFormats (juce::AudioPluginFormatManager& fm)
    {
       #if JUCE_PLUGINHOST_AU && JUCE_MAC
        fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
       #endif
       #if JUCE_PLUGINHOST_VST3
        fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
       #endif
    }

    // KALEIDOBOX_TEST_PLUGIN, or this build's Sample Editor Instrument.vst3.
    bool findPlugin (juce::String& why)
    {
        auto id = juce::SystemStats::getEnvironmentVariable ("KALEIDOBOX_TEST_PLUGIN", {});
        juce::String formatName = "VST3";
        if (id.startsWith ("AudioUnit:"))
            formatName = "AudioUnit";
        else if (id.startsWith ("VST3:"))
            id = id.fromFirstOccurrenceOf ("VST3:", false, false);
        if (id.isEmpty())
        {
           #ifdef KALEIDOBOX_BUILD_DIR
            const juce::File tools = juce::File (KALEIDOBOX_BUILD_DIR).getChildFile ("tools").getChildFile ("editor");
            for (const auto& f : tools.findChildFiles (juce::File::findDirectories, true, "Sample Editor Instrument.vst3"))
                id = f.getFullPathName();
           #endif
            if (id.isEmpty())
            {
                why = "no KALEIDOBOX_TEST_PLUGIN, and this build didn't make the Sample Editor Instrument VST3";
                return false;
            }
        }
        juce::AudioPluginFormatManager fm;
        addFormats (fm);
        for (auto* f : fm.getFormats())
            if (f->getName() == formatName)
            {
                juce::OwnedArray<juce::PluginDescription> found;
                f->findAllTypesForFile (found, id);
                if (! found.isEmpty())
                {
                    desc = *found[0];
                    isSampleInstrument = desc.name == "Sample Editor Instrument";
                    return true;
                }
            }
        why = "couldn't open " + formatName + " " + id;
        return false;
    }

    // The Sample Editor Instrument's settings with a sound: a decaying saw and fifth at root note
    // 60 (its default has none at all). Its host wrapper's form, as a hosted VST3's state is.
    static juce::MemoryBlock sampleInstrumentState()
    {
        const int n = 26460;
        juce::AudioBuffer<float> a (2, n);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / 44100.0;
            const double saw = 2.0 * std::fmod (t * 220.0, 1.0) - 1.0;
            const double fifth = std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * t);
            const float v = (float) ((0.5 * saw + 0.3 * fifth) * std::exp (-3.0 * t));
            a.setSample (0, i, v);
            a.setSample (1, i, v * 0.8f);
        }
        juce::ValueTree s ("SampleEditor");
        s.setProperty ("name", "persist test saw", nullptr);
        s.setProperty ("source", "", nullptr);
        s.setProperty ("rootNote", 60, nullptr);
        s.setProperty ("keyMode", 0, nullptr);
        s.setProperty ("chops", "", nullptr);
        s.setProperty ("attack", 0.001, nullptr);
        s.setProperty ("release", 0.3, nullptr);
        s.setProperty ("wav", juce::var (crate::AudioFiles::toWavData (a)), nullptr);
        juce::MemoryBlock raw;
        {
            juce::MemoryOutputStream os (raw, false);
            s.writeToStream (os);
        }
        juce::XmlElement state ("VST3PluginState");
        state.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
        juce::MemoryBlock out;
        juce::AudioProcessor::copyXmlToBinary (state, out);
        return out;
    }

    // Linux only: a hosted VST3 names the .so inside its bundle, which the VST3 format won't open
    // again, so a show saved here couldn't reopen its plugin at all (on a Mac it names the bundle,
    // or "AudioUnit:..."). Points the saved description at the bundle, as a Mac's would be.
    static void bundlePath (const juce::File& show)
    {
       #if JUCE_LINUX
        auto xml = juce::parseXML (show);
        if (xml == nullptr)
            return;
        for (auto* inst : xml->getChildWithTagNameIterator ("Instrument"))
            if (auto* p = inst->getChildByName ("Plugin"))
                if (auto d = juce::parseXML (p->getStringAttribute ("description")))
                    if (const auto file = d->getStringAttribute ("file"); file.contains (".vst3/Contents/"))
                    {
                        d->setAttribute ("file", file.upToFirstOccurrenceOf (".vst3", true, false));
                        p->setAttribute ("description", d->toString (juce::XmlElement::TextFormat().singleLine()));
                    }
        xml->writeTo (show);
       #else
        juce::ignoreUnused (show);
       #endif
    }

    static juce::MemoryBlock stateOf (PluginSlot& s)
    {
        juce::MemoryBlock m;
        if (auto* i = s.getInstance())
            i->getStateInformation (m);
        return m;
    }

    bool loadByHand (PluginSlot& s)
    {
        bool called = false;
        juce::String error;
        s.load (desc, [&] (const juce::String& x) { error = x; called = true; });
        pumpUntil ([&] { return called; }, 60000);
        expectEquals (error, juce::String(), "a load that worked reports no error");
        return s.isLoaded();
    }

    void runTest() override
    {
        beginTest ("a real plugin's settings, knobs and sound come back; saves while it loads or is missing keep it");
        juce::String why;
        if (! findPlugin (why))
        {
            logMessage ("skipped: " + why);
            return;
        }
        logMessage ("plugin: " + desc.pluginFormatName + " " + desc.name + " (" + desc.fileOrIdentifier + ")");
        folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("KaleidoboxRealPluginPersistTests");
        folder.deleteRecursively();
        folder.createDirectory();

        const auto file = folder.getChildFile ("real.crateshow");
        std::vector<float> savedAudio;
        juce::MemoryBlock defaultState;
        std::vector<std::pair<int, float>> params;
        {
            auto e = freshEngine();
            auto& s = e->getSynthPlugin();
            if (! loadByHand (s))
                return;
            defaultState = stateOf (s);
            if (isSampleInstrument)
            {
                const auto st = sampleInstrumentState();
                s.getInstance()->setStateInformation (st.getData(), (int) st.getSize());
            }
            else
            {
                // Any other plugin: a few of its settings away from where they start.
                auto& ps = s.getInstance()->getParameters();
                for (int i = 0; i < ps.size() && params.size() < 6; ++i)
                {
                    auto* p = ps[i];
                    const auto pname = p->getName (40);
                    if (! p->isAutomatable() || p->isMetaParameter() || pname.containsIgnoreCase ("bypass") || pname.containsIgnoreCase ("volume")
                        || pname.containsIgnoreCase ("MIDI CC"))
                        continue;
                    p->setValueNotifyingHost (std::abs (p->getValue() - 0.7f) > 0.1f ? 0.7f : 0.3f);
                    params.push_back ({ i, p->getValue() });
                }
            }
            pump (300);
            for (int k = 0; k < PluginSlot::kMacros; ++k)
                s.setMacro (k, -1);
            s.setMacro (5, 0);
            s.level = 0.7f;
            savedAudio = render (s, 200);
            expect (stateOf (s) != defaultState, "(its settings aren't its default)");
            expect (e->saveShowFile (file, rigshow::SaveKind::document).ok);
            bundlePath (file);
        }
        const auto show = readShow (file);
        const auto savedXml = instrumentXml (show);
        const auto savedState = stateBytes (instrumentOf (show));

        // 4: saved straight after opening, while it loads.
        auto e = openShow (file);
        auto& s = e->getSynthPlugin();
        expect (! s.isLoaded(), "(still loading)");
        const auto during = folder.getChildFile ("during.crateshow");
        expect (e->saveShowFile (during, rigshow::SaveKind::document).ok);
        expectEquals (instrumentXml (readShow (during)), savedXml, "saved while loading: byte for byte");
        expect (pumpUntil ([&] { return s.isLoaded(); }, 60000), "then it loads");
        pump (300);

        // 1: what the plugin made of the bytes the show gave it is what it makes of them given directly.
        auto direct = freshEngine();
        if (loadByHand (direct->getSynthPlugin()))
        {
            direct->getSynthPlugin().getInstance()->setStateInformation (savedState.getData(), (int) savedState.getSize());
            pump (300);
            expect (stateOf (s) == stateOf (direct->getSynthPlugin()), "the reopened plugin's settings are the saved bytes' (given directly)");
            direct->getSynthPlugin().level = 0.7f;
        }
        // 2: settings, knobs, level.
        for (auto [i, v] : params)
            expectWithinAbsoluteError (s.getInstance()->getParameters()[i]->getValue(), v, 1.0e-4f, "parameter " + juce::String (i));
        expectEquals (macrosOf (s), juce::String ("-1 -1 -1 -1 -1 0 -1 -1"));
        expectWithinAbsoluteError (s.level.load(), 0.7f, 1.0e-6f);
        // 3: its sound.
        const auto again = render (s, 200);
        if (isSampleInstrument)
        {
            // It re-encodes its 24-bit sample on every save and load, so a reopened sound is within
            // a 24-bit step (1e-6 is below one: 2^-23 = 1.2e-7 per step at full scale) of the saved.
            expect (rms (savedAudio) > 0.01, "it plays");
            expectLessThan (maxDiff (again, savedAudio), 1.0e-6, "the same sound, within 24-bit rounding");
            expectEquals (maxDiff (again, render (direct->getSynthPlugin(), 200)), 0.0, "and exactly what the saved bytes play given directly");
        }
        else
        {
            // Analog models aren't sample-exact run to run: nearer the saved sound than the default's.
            auto plain = freshEngine();
            loadByHand (plain->getSynthPlugin());
            plain->getSynthPlugin().level = 0.7f;
            const auto fresh = render (plain->getSynthPlugin(), 200);
            logMessage ("sound: max diff to the saved " + juce::String (maxDiff (again, savedAudio)) + ", to a fresh one " + juce::String (maxDiff (again, fresh)));
            expect (maxDiff (again, savedAudio) <= maxDiff (again, fresh));
        }

        // 5: missing here (simulated in a copy of the show: never by moving a plugin).
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
            expect (! m->getSynthPlugin().isLoaded());
            const auto resaved = folder.getChildFile ("missing resaved.crateshow");
            expect (m->saveShowFile (resaved, rigshow::SaveKind::document).ok);
            expectEquals (instrumentXml (readShow (resaved)), missingXml, "kept while missing");
        }
        e.reset();
        direct.reset();
        pump (300);
        folder.deleteRecursively();
    }
};

static RigRealPluginPersistTests rigRealPluginPersistTests;
} // namespace
