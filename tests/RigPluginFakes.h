#pragma once

// A fake plugin format for the hosted-plugin tests (RigPluginPersistTests, RigDirtyPluginLoadTests):
// it makes "plugins" asynchronously, as every real format does (AudioPluginFormat::
// createPluginInstanceAsync posts a message), and can hold each load until the test lets it
// finish, fail it, or finish loads out of order. No plugin binaries. Named (not unnamed), so the
// PluginHost shared by the test files finds the one format whichever file added it.

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/AudioFiles.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

#if JUCE_MAC
 #include <CoreFoundation/CoreFoundation.h>
#else
namespace juce { namespace detail { bool dispatchNextMessageOnSystemQueue (bool); } }
#endif

namespace pluginfakes
{
// Delivers what's been posted to the message thread, for `ms`.
inline void pump (int ms = 30)
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

inline bool pumpUntil (std::function<bool()> done, int timeoutMs = 20000)
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
    // (`steps` > 1: a stepped control that doesn't say so, as an Arturia synth's LFO rate in sync
    // mode: it takes the nearest of its steps, so what it reads back isn't always what was asked.)
    void setValue (float v) override { value = steps > 1 ? std::round (v * (float) (steps - 1)) / (float) (steps - 1) : v; }
    float getDefaultValue() const override { return def; }
    juce::String getName (int n) const override { return id.substring (0, n); }
    juce::String getLabel() const override { return {}; }
    float getValueForText (const juce::String& t) const override { return t.getFloatValue(); }
    juce::String getParameterID() const override { return id; }
    juce::String id;
    float value, def;
    int steps = 0;
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
        if (steppedDrive)
            drive->steps = 20;
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
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d = desc;
        if (rewriting())
            d.version = "2.0 (as it describes itself once it's open)";
    }
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
                if (analogNoise)
                    v += (noise.nextFloat() * 2.0f - 1.0f) * 0.001f;   // (free running: never the same twice)
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
        if (rewriting())
            o.writeString ("its own form of the same settings");
    }
    void setStateInformation (const void* data, int size) override
    {
        if (throwOnSetState)
            throw std::runtime_error ("fake setStateInformation failure");
        lastReceived.replaceAll (data, (size_t) size);
        if (ignoreSetState)
            return;
        restored = true;
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
    double rate = 48000.0, phase = 0.0, step = 0.0;
    int note = -1;

    static inline int alive = 0, deletedOffMessageThread = 0;
    static inline bool throwOnGetState = false, emptyState = false, throwOnSetState = false;
    static inline juce::MemoryBlock lastReceived;
    static inline bool receivedWhilePrepared = false;
    // As Arturia's do: once given a show's settings, it reports them in its own bytes (not the
    // ones it was given) and describes itself a little differently. Same settings, same sound.
    static inline bool rewritesState = false;
    // ... but only when its Drive was moved from where it starts (as Mini V4 rewrites a patch
    // with its Brightness moved, and hands back the bytes it was given for others).
    static inline bool rewritesOnlyWhenDriveMoved = false;
    // Its Drive a stepped control that doesn't say so (see FakeParam::steps).
    static inline bool steppedDrive = false;
    // A little free-running noise in its sound, as an analog model's (not sample-exact run to run).
    static inline bool analogNoise = false;
    // Takes nothing it's given (a restore that does nothing), for the negative controls.
    static inline bool ignoreSetState = false;
    static void resetOptions()
    {
        rewritesState = rewritesOnlyWhenDriveMoved = steppedDrive = analogNoise = ignoreSetState = false;
    }
    bool restored = false;
    juce::Random noise;

private:
    bool rewriting() const
    {
        return rewritesState && restored && (! rewritesOnlyWhenDriveMoved || std::abs (drive->value - 0.1f) > 0.01f);
    }
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
// A real plugin for the tests that want one (the "real plugin" parts): its formats, where it is,
// and the Sample Editor Instrument's settings with a sound.
namespace real
{
inline void addFormats (juce::AudioPluginFormatManager& fm)
{
   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    fm.addFormat (std::make_unique<juce::AudioUnitPluginFormat>());
   #endif
   #if JUCE_PLUGINHOST_VST3
    fm.addFormat (std::make_unique<juce::VST3PluginFormat>());
   #endif
}

// KALEIDOBOX_TEST_PLUGIN, or this build's Sample Editor Instrument.vst3.
inline bool findPlugin (juce::PluginDescription& desc, bool& isSampleInstrument, juce::String& why)
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
inline juce::MemoryBlock sampleInstrumentState()
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

//==============================================================================
// An edited patch made at run time through a plugin's own parameters (no saved plugin state in
// the repo), for any plugin: automatable, not meta, not discrete or on/off, not a bypass, volume,
// MIDI CC, program or preset; each one moved, and kept only if it moved and holds what it reads
// back (a stepped control holds its nearest step: what it reads back is what's expected, never
// what was asked) and that isn't its default or where it started. KALEIDOBOX_TEST_PARAMS
// ("cutoff,brightness": parts of their names) picks them instead.
struct ChosenParam
{
    int index = -1;
    juce::String name;
    float was = 0, requested = 0, readBack = 0, def = 0;
};

inline juce::String hashOf (const juce::MemoryBlock& m)
{
    juce::uint32 h = 2166136261u;
    for (size_t i = 0; i < m.getSize(); ++i)
        h = (h ^ (juce::uint8) m[i]) * 16777619u;
    return juce::String ((int) m.getSize()) + " B fnv " + juce::String::toHexString ((int) h).paddedLeft ('0', 8);
}

inline std::vector<float> valuesOf (juce::AudioProcessor& p)
{
    std::vector<float> v;
    for (auto* q : p.getParameters())
        v.push_back (q->getValue());
    return v;
}

inline juce::StringArray paramsFromEnv()
{
    auto a = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("KALEIDOBOX_TEST_PARAMS", {}), ",;|", "");
    a.trim();
    a.removeEmptyStrings();
    return a;
}

inline bool usableParam (juce::AudioProcessorParameter& p, bool byName)
{
    if (! p.isAutomatable() || p.isMetaParameter() || p.isDiscrete() || p.isBoolean())
        return false;
    const auto n = p.getName (60);
    if (n.isEmpty())
        return false;
    if (! byName)
        for (auto* skip : { "bypass", "volume", "MIDI CC", "program", "preset" })
            if (n.containsIgnoreCase (skip))
                return false;
    return true;
}

// The patches to try. 0: sound-shaping controls first (cutoff, resonance, brightness, attack,
// decay, release, in the plugin's order), then any; 1: the first ones in order; 2: the next ones, twice as many. With
// KALEIDOBOX_TEST_PARAMS, its controls in each, moved to other values.
inline std::vector<ChosenParam> chooseParams (juce::AudioPluginInstance& inst, int variant, int maxCount,
                                              const std::function<void (const juce::String&)>& log)
{
    auto& ps = inst.getParameters();
    std::vector<int> order;
    auto add = [&order] (int i) { if (std::find (order.begin(), order.end(), i) == order.end()) order.push_back (i); };
    const auto wanted = paramsFromEnv();
    if (! wanted.isEmpty())
    {
        log ("  parameters from KALEIDOBOX_TEST_PARAMS: " + wanted.joinIntoString (", "));
        for (const auto& w : wanted)
        {
            bool any = false;
            for (int i = 0; i < ps.size(); ++i)
                if (ps[i]->getName (60).containsIgnoreCase (w) && usableParam (*ps[i], true))
                    add (i), any = true;
            if (! any)
                log ("  KALEIDOBOX_TEST_PARAMS: no usable parameter's name has '" + w + "'");
        }
        // (None of them is this plugin's: the always-running fake, say. Its own choice then.)
        if (order.empty())
            log ("  KALEIDOBOX_TEST_PARAMS names none of this plugin's parameters: choosing as usual");
    }
    if (order.empty())
    {
        // (In their order: on the Mac this picked the patches Mini V4 and Jup-8 V4 rewrite.)
        if (variant == 0)
            for (int i = 0; i < ps.size(); ++i)
                for (auto* k : { "cutoff", "reson", "brightness", "attack", "decay", "release" })
                    if (ps[i]->getName (60).containsIgnoreCase (k) && usableParam (*ps[i], false))
                        add (i);
        int skip = variant == 2 ? maxCount : 0;
        for (int i = 0; i < ps.size(); ++i)
            if (usableParam (*ps[i], false) && skip-- <= 0)
                add (i);
        if (variant == 2)
            maxCount *= 2;
    }
    // Where each is moved to: away from where it starts, a different way for each patch.
    const float lo[] = { 0.08f, 0.25f, 0.05f }, hi[] = { 0.92f, 0.75f, 0.95f };
    const int v = juce::jlimit (0, 2, variant);
    std::vector<ChosenParam> chosen;
    int tried = 0;
    for (const int i : order)
    {
        if ((int) chosen.size() >= maxCount || tried >= maxCount * 4)
            break;
        ++tried;
        auto* p = ps[i];
        ChosenParam c;
        c.index = i;
        c.name = p->getName (60);
        c.was = p->getValue();
        c.def = p->getDefaultValue();
        c.requested = c.was > 0.5f ? lo[v] : hi[v];
        p->setValueNotifyingHost (c.requested);
        const float now = p->getValue();
        pump (50);
        c.readBack = p->getValue();
        juce::String why;
        if (std::abs (now - c.was) < 0.01f)
            why = "it didn't move";
        else if (std::abs (c.readBack - now) > 1.0e-6f)
            why = "it doesn't hold (read " + juce::String (now, 6) + ", then " + juce::String (c.readBack, 6) + ")";
        else if (std::abs (c.readBack - c.was) < 0.01f || std::abs (c.readBack - c.def) < 0.01f)
            why = "it reads back at its start or its default";
        if (why.isNotEmpty())
        {
            log ("  not used: '" + c.name + "' (" + juce::String (i) + "): " + why);
            p->setValueNotifyingHost (c.was);
            continue;
        }
        chosen.push_back (c);
    }
    // Once they've all settled: what each reads back now is what the patch has.
    pump (300);
    std::vector<ChosenParam> held;
    for (auto c : chosen)
    {
        const float now = ps[c.index]->getValue();
        if (std::abs (now - c.readBack) > 1.0e-6f)
            log ("  '" + c.name + "' moved when others were set (" + juce::String (c.readBack, 6) + " -> " + juce::String (now, 6) + ")");
        c.readBack = now;
        if (std::abs (c.readBack - c.was) < 0.01f || std::abs (c.readBack - c.def) < 0.01f)
        {
            log ("  not used: '" + c.name + "': back at its start or its default");
            continue;
        }
        log ("  set '" + c.name + "' (" + juce::String (c.index) + ") " + juce::String (c.was, 4) + " -> asked " + juce::String (c.requested, 4) + ", reads back "
             + juce::String (c.readBack, 6) + (std::abs (c.readBack - c.requested) > 1.0e-4f ? " (its nearest step)" : "") + "; its default " + juce::String (c.def, 4));
        held.push_back (c);
    }
    return held;
}

// Linux only: a hosted VST3 names the .so inside its bundle, which the VST3 format won't open
// again, so a show saved here couldn't reopen its plugin at all (on a Mac it names the bundle,
// or "AudioUnit:..."). Points the saved description at the bundle, as a Mac's would be.
inline void bundlePath (const juce::File& show)
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

} // namespace real
} // namespace pluginfakes
