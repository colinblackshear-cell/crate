#pragma once

// A fake plugin format for the hosted-plugin tests (RigPluginPersistTests, RigDirtyPluginLoadTests):
// it makes "plugins" asynchronously, as every real format does (AudioPluginFormat::
// createPluginInstanceAsync posts a message), and can hold each load until the test lets it
// finish, fail it, or finish loads out of order. No plugin binaries. Named (not unnamed), so the
// PluginHost shared by the test files finds the one format whichever file added it.

#include <juce_audio_processors/juce_audio_processors.h>

#include "core/AudioFiles.h"

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
    void fillInPluginDescription (juce::PluginDescription& d) const override
    {
        d = desc;
        if (rewritesState && restored)
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
        if (rewritesState && restored)
            o.writeString ("its own form of the same settings");
    }
    void setStateInformation (const void* data, int size) override
    {
        if (throwOnSetState)
            throw std::runtime_error ("fake setStateInformation failure");
        lastReceived.replaceAll (data, (size_t) size);
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
    bool restored = false;
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
