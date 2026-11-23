#include "RigPhoneLink.h"
#include "RigSong.h"

#include "core/AudioFiles.h"

using crate::link::Message;

//==============================================================================
class RigPhoneLink::PhoneConnection : public crate::link::Connection
{
public:
    explicit PhoneConnection (RigPhoneLink& l) : link (l), alive (l.alive)
    {
        onMessage = [this] (const Message& m)
        {
            if (m.type == "hello")
                return link.hello (*this, m);
            if (! linked)
            {
                send (Message ("denied").set ("why", "Type the four numbers from the Mac first."));
                return;
            }
            link.handle (m, [this] (const Message& r) { send (r); });
        };
        onLost = [this]
        {
            juce::MessageManager::callAsync ([&l = link, a = alive, self = this]
            {
                if (*a)
                    l.connectionGone (self);
            });
        };
    }

    RigPhoneLink& link;
    std::shared_ptr<std::atomic<bool>> alive;   // the link's
    bool linked = false;
    juce::String id;
};

//==============================================================================
RigPhoneLink::RigPhoneLink (RigEngine& e, juce::File phonesFile, bool advertise)
    : engine (e), phones (phonesFile, [] { juce::PropertiesFile::Options o; o.storageFormat = juce::PropertiesFile::storeAsXML; return o; }())
{
    newCode();
    for (int p = crate::link::kPort; p < crate::link::kPort + 10 && listeningPort == 0; ++p)
        if (beginWaitingForSocket (p))
            listeningPort = p;
    if (listeningPort > 0 && advertise)
        advertiser = std::make_unique<crate::link::Advertiser> (crate::link::deviceName(), listeningPort);
    startTimerHz (10);
}

RigPhoneLink::~RigPhoneLink()
{
    // Stopping the server wakes its accept() by connecting to it, which would hand us one more
    // connection from its thread: `stopping` refuses it, and `alive` stops anything already
    // posted to the message thread from touching this after it's gone.
    stopping = true;
    *alive = false;
    stopTimer();
    stop();
    advertiser.reset();
    phone = nullptr;
    for (auto* c : connections)
    {
        c->onMessage = nullptr;
        c->onLost = nullptr;
        c->disconnect();
    }
    connections.clear();
}

juce::File RigPhoneLink::defaultPhonesFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("Crate Rig").getChildFile ("iPhones.xml");
}

juce::File RigPhoneLink::soundsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Sounds").getChildFile ("From iPhone");
}

const std::array<int, 8>& RigPhoneLink::remoteStrips()
{
    static const std::array<int, 8> strips { RigEngine::mpcStrip, RigEngine::drumsStrip, RigEngine::bassStrip, RigEngine::synthStrip,
                                             RigEngine::vocoderStrip, RigEngine::samplerStrip, RigEngine::loopsStrip, RigEngine::inputStrip };
    return strips;
}

void RigPhoneLink::newCode()
{
    juce::Random rng;
    juce::String c;
    do
        c = juce::String (rng.nextInt (10000)).paddedLeft ('0', 4);
    while (c == code);
    code = c;
}

void RigPhoneLink::setShow (const juce::String& s)
{
    if (show == s)
        return;
    show = s;
    if (advertiser != nullptr)
        advertiser->setShow (s);
}

void RigPhoneLink::unlink()
{
    phones.clear();
    phones.saveIfNeeded();
    if (phone != nullptr)
    {
        phone->send (Message ("unlinked").set ("why", "The Mac unlinked this iPhone."));
        phone->disconnect();
    }
    phone = nullptr;
    phoneName = {};
    newCode();
    changed();
}

juce::InterprocessConnection* RigPhoneLink::createConnectionObject()
{
    // The server's thread: the connection is kept here and only used on the message thread.
    if (stopping)
        return nullptr;
    auto* c = new PhoneConnection (*this);
    juce::MessageManager::callAsync ([this, c, a = alive]
    {
        if (*a)
            connections.add (c);
        else
        {
            c->onMessage = nullptr;   // this went away first
            c->onLost = nullptr;
            delete c;
        }
    });
    return c;
}

void RigPhoneLink::connectionGone (PhoneConnection* c)
{
    if (! connections.contains (c))
        return;
    if (c == phone)
    {
        say (phoneName + " is no longer linked (it left the Wi-Fi or closed Kaleidobox Pocket). It links again by itself when it's back.");
        phone = nullptr;
        phoneName = {};
        changed();
    }
    connections.removeObject (c);
}

void RigPhoneLink::hello (PhoneConnection& c, const Message& m)
{
    const auto id = m.get ("id").toString();
    const auto name = m.get ("phone").toString().isNotEmpty() ? m.get ("phone").toString() : juce::String ("iPhone");
    const auto key = m.get ("key").toString();
    const auto typed = m.get ("code").toString();
    const bool known = id.isNotEmpty() && key.isNotEmpty() && phones.getValue ("key." + id) == key;
    if (! known && (typed.isEmpty() || typed != code))
    {
        c.send (Message ("denied").set ("why", typed.isEmpty() ? "This Mac doesn't know this iPhone any more. Type the four numbers from Settings > iPhone."
                                                               : "Those numbers don't match. Check Settings > iPhone on the Mac."));
        return;
    }

    juce::String newKey = key;
    if (! known)
    {
        newKey = juce::Uuid().toString();
        phones.setValue ("key." + id, newKey);
        phones.setValue ("name." + id, name);
        phones.saveIfNeeded();
        newCode();   // the numbers are used up
    }
    if (phone != nullptr && phone != &c)
    {
        phone->send (Message ("unlinked").set ("why", "Another iPhone linked to this Mac."));
        phone->linked = false;
    }
    c.linked = true;
    c.id = id;
    phone = &c;
    phoneName = name;
    c.send (Message ("welcome").set ("mac", crate::link::deviceName()).set ("key", newKey).set ("show", show));
    lastState = {};
    timerCallback();
    if (! known)
        say (name + " is linked. It can send sounds here and run Kaleidobox from its Remote page.");
    changed();
}

void RigPhoneLink::timerCallback()
{
    if (phone == nullptr)
        return;
    auto state = makeState();
    const auto text = juce::JSON::toString (state.fields, true);
    // Every change, and every couple of seconds anyway (so the phone knows the link is alive).
    if (text != lastState || ++quietTicks >= 20)
    {
        lastState = text;
        quietTicks = 0;
        phone->send (state);
    }
}

//==============================================================================
Message RigPhoneLink::makeState() const
{
    Message m ("state");
    auto& transport = engine.getTransport();
    const int bpb = juce::jmax (1, transport.getBeatsPerBar());
    const double bar = transport.getPositionInBeats() / bpb;
    m.set ("bpm", transport.getBpm()).set ("playing", transport.isPlaying()).set ("bpb", bpb).set ("bar", bar);

    const auto& timeline = engine.getTimeline();
    const auto list = rigsong::sections (timeline);
    juce::Array<juce::var> names;
    for (size_t i = 0; i < list.size() && i < 8; ++i)
        names.add (list[i].name);
    m.set ("sections", names);
    const int at = rigsong::sectionAt (timeline, bar);
    m.set ("section", at).set ("song", engine.songInCharge());
    if (juce::isPositiveAndBelow (at, (int) list.size()))
        m.set ("barIn", bar - list[(size_t) at].start + 1.0).set ("sectionBars", list[(size_t) at].bars)
            .set ("next", at + 1 < (int) list.size() ? at + 1 : -1);

    juce::Array<juce::var> mutes;
    for (auto s : remoteStrips())
        mutes.add (engine.getStrip (s).mute.load());
    m.set ("mutes", mutes);

    juce::Array<juce::var> layers;
    auto& looper = const_cast<RigEngine&> (engine).getLooper();
    for (int i = 0; i < RigLooper::kLayers; ++i)
        layers.add ((int) looper.getState (i));
    m.set ("layers", layers).set ("remoteLayer", remoteLayer);

    auto* rec = new juce::DynamicObject();
    for (auto* target : { "set", "take", "moves" })
        rec->setProperty (target, recState ? recState (target) : 0);
    m.set ("rec", juce::var (rec)).set ("show", show);
    return m;
}

//==============================================================================
void RigPhoneLink::handle (const Message& m, std::function<void (const Message&)> reply)
{
    // "said": what happened, with the phone's request number so it knows which send it answers.
    auto tell = [&reply, &m] (const juce::String& text, bool ok = true)
    {
        if (reply)
            reply (Message ("said").set ("text", text.trimCharactersAtStart ("!")).set ("ok", ok).set ("req", m.get ("req", 0)));
    };
    if (m.type == "sound")
    {
        const auto text = receiveSound (m.get ("dest").toString(), m.get ("name").toString(), (double) m.get ("beats", 0.0), m.data);
        tell (text, ! text.startsWith ("!"));
    }
    else if (m.type == "kit")
    {
        juce::StringArray names;
        if (auto* a = m.get ("names").getArray())
            for (auto& v : *a)
                names.add (v.toString());
        tell (receiveKit (m.get ("name").toString(), names, crate::link::getFiles (m)));
    }
    else if (m.type == "remote")
    {
        const auto action = m.get ("action").toString();
        const auto value = m.get ("value");
        auto& transport = engine.getTransport();
        auto& looper = engine.getLooper();
        const auto list = rigsong::sections (engine.getTimeline());
        if (action == "play")
        {
            if (! transport.isPlaying())
                engine.playOrStop();
        }
        else if (action == "stop")
        {
            transport.stop();
            engine.allNotesOff();
        }
        else if (action == "bpm")
            transport.setBpm (juce::jlimit (RigTransport::kMinBpm, RigTransport::kMaxBpm, (double) value));
        else if (action == "section" || action == "next" || action == "back")
        {
            if (list.empty())
                return tell ("The song on the Mac has no sections yet. Make them on its Song screen.", false);
            int to = (int) value;
            if (action != "section")
            {
                const int bpb = juce::jmax (1, transport.getBeatsPerBar());
                int at = rigsong::sectionAt (engine.getTimeline(), transport.getPositionInBeats() / bpb);
                if (at < 0)
                    at = action == "next" ? -1 : 0;
                to = juce::jlimit (0, (int) list.size() - 1, at + (action == "next" ? 1 : -1));
            }
            if (! juce::isPositiveAndBelow (to, (int) list.size()))
                return tell ("There's no section " + rigsong::letterFor (to) + " in the song.", false);
            engine.launchSection (to);
        }
        else if (action == "mute")
        {
            const int t = (int) value;
            if (juce::isPositiveAndBelow (t, (int) remoteStrips().size()))
            {
                auto& strip = engine.getStrip (remoteStrips()[(size_t) t]);
                strip.mute = ! strip.mute.load();
                engine.getDirty().authoredField (RigDirty::stripMute, remoteStrips()[(size_t) t]);
                engine.controlsMoved = true;
            }
        }
        else if (action == "unmuteAll")
        {
            for (auto s : remoteStrips())
            {
                engine.getStrip (s).mute = false;
                engine.getDirty().authoredField (RigDirty::stripMute, s);
            }
            engine.controlsMoved = true;
        }
        else if (action == "rec")
        {
            const auto target = value.toString();
            if (target != "set" && target != "take" && target != "moves")
                return;
            if (pressRec)
                pressRec (target);
        }
        else if (action == "recLayer")
        {
            // Pressed while the remote's layer is waiting or recording: closes it at the end of the bar.
            if (juce::isPositiveAndBelow (remoteLayer, RigLooper::kLayers))
            {
                const auto st = looper.getState (remoteLayer);
                if (st == RigLooper::State::armed || st == RigLooper::State::recording)
                {
                    looper.recordOrClose (remoteLayer);
                    return;
                }
            }
            int layer = -1;
            for (int i = 0; i < RigLooper::kLayers && layer < 0; ++i)
                if (looper.getState (i) == RigLooper::State::empty)
                    layer = i;
            if (layer < 0)
                return tell ("Every loop layer on the Mac is full. Undo one first.", false);
            looper.recordOrClose (layer);
            remoteLayer = layer;
            say ("The iPhone is recording loop " + juce::String (layer + 1) + " from the next bar.");
        }
        else if (action == "undoLayer")
        {
            if (juce::isPositiveAndBelow (remoteLayer, RigLooper::kLayers) && looper.getState (remoteLayer) != RigLooper::State::empty)
            {
                looper.clear (remoteLayer);
                tell ("Loop " + juce::String (remoteLayer + 1) + " is cleared.");
                remoteLayer = -1;
                return;
            }
            for (int i = 0; i < RigLooper::kLayers; ++i)
                if (looper.canUndo (i))
                {
                    looper.undo (i);
                    return tell ("Took back the last overdub on loop " + juce::String (i + 1) + ".");
                }
            tell ("No loop layer from the Remote to undo.", false);
        }
    }
    else if (m.type == "bye")
    {
        // The phone's Unlink: forget it.
        for (auto& k : phones.getAllProperties().getAllKeys())
            if (k.endsWith ("." + m.get ("id").toString()))
                phones.removeValue (k);
        phones.saveIfNeeded();
    }
}

juce::String RigPhoneLink::receiveSound (const juce::String& dest, const juce::String& rawName, double beats, const juce::MemoryBlock& wav)
{
    const auto name = juce::File::createLegalFileName (rawName.isNotEmpty() ? rawName : juce::String ("iPhone sound")).trim();
    auto audio = crate::AudioFiles::fromWavData (wav.getData(), wav.getSize());
    if (audio == nullptr || audio->getNumSamples() == 0)
        return "!The Mac couldn't read that sound.";

    juce::String text;
    if (dest == "pad")
    {
        const auto pad = engine.putOnMpcPad (juce::AudioBuffer<float> (*audio), name, beats > 0 ? std::round (beats) : 0.0);
        if (pad.isEmpty())
            return "!Every MPC pad in the Mac's kit has a sound. Clear one first.";
        text = name + " is on MPC pad " + pad + " on the Mac.";
    }
    else if (dest == "sampler")
    {
        engine.putInSampler (juce::AudioBuffer<float> (*audio), name);
        text = name + " is in the Sampler on the Mac.";
    }
    else if (dest == "loop")
    {
        const int layer = engine.putOnLoopLayer (audio, name, beats);
        if (layer < 0)
            return "!Every loop layer on the Mac is full. Clear one first.";
        text = name + " is on loop " + juce::String (layer + 1) + " on the Mac, in time at "
               + juce::String (engine.getTransport().getBpm(), 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM.";
    }
    else
    {
        auto folder = soundsTarget;
        folder.createDirectory();
        auto file = folder.getNonexistentChildFile (name, ".wav", false);
        if (! file.replaceWithData (wav.getData(), wav.getSize()))
            return "!The Mac couldn't save that sound in " + folder.getFullPathName() + ".";
        if (onNewSound)
            onNewSound (file);
        text = name + " is in Sounds on the Mac, in From iPhone.";
    }
    say ("From " + (phoneName.isNotEmpty() ? phoneName : juce::String ("the iPhone")) + ": " + text);
    return text;
}

juce::String RigPhoneLink::receiveKit (const juce::String& name, const juce::StringArray& names, const std::vector<juce::MemoryBlock>& wavs)
{
    juce::ignoreUnused (names);
    std::vector<crate::SharedAudio> slices;
    for (auto& w : wavs)
        slices.push_back (crate::AudioFiles::fromWavData (w.getData(), w.getSize()));
    const auto text = engine.sendSlicesToMpc (std::move (slices), name.isNotEmpty() ? name : juce::String ("iPhone kit"));
    say ("From " + (phoneName.isNotEmpty() ? phoneName : juce::String ("the iPhone")) + ": " + text);
    return text;
}
