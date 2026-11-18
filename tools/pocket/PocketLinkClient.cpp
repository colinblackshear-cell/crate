#include "PocketLinkClient.h"

#include "core/AudioFiles.h"

using crate::link::Message;

namespace pocket
{
LinkClient::LinkClient (juce::File documents, bool browse) : file (documents.getChildFile ("link.json"))
{
    loadKnown();
    if (browse)
    {
        browser = std::make_unique<crate::link::Browser>();
        browser->onChange = [this] { changed(); timerCallback(); };
    }
    startTimer (1500);
}

LinkClient::~LinkClient()
{
    *alive = false;
    stopTimer();
    if (connection != nullptr)
    {
        connection->onConnected = nullptr;
        connection->onLost = nullptr;
        connection->onMessage = nullptr;
        connection->disconnect();
    }
}

void LinkClient::loadKnown()
{
    auto v = juce::JSON::parse (file);
    phoneId = v["id"].toString();
    if (phoneId.isEmpty())
    {
        phoneId = juce::Uuid().toString();
        saveKnown();
    }
    if (auto* macs = v["macs"].getArray())
        for (auto& m : *macs)
            knownMacs.push_back ({ m["name"].toString(), m["key"].toString(), m["host"].toString(), (int) m["port"] });
}

void LinkClient::saveKnown() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", phoneId);
    juce::Array<juce::var> macs;
    for (auto& k : knownMacs)
    {
        auto* m = new juce::DynamicObject();
        m->setProperty ("name", k.name);
        m->setProperty ("key", k.key);
        m->setProperty ("host", k.host);
        m->setProperty ("port", k.port);
        macs.add (juce::var (m));
    }
    o->setProperty ("macs", macs);
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (juce::var (o)));
}

LinkClient::Known* LinkClient::known (const juce::String& name)
{
    for (auto& k : knownMacs)
        if (k.name == name)
            return &k;
    return nullptr;
}

juce::String LinkClient::getShow() const
{
    const auto s = latest.get ("show").toString();
    if (s.isNotEmpty())
        return s;
    for (auto& r : macs())
        if (r.name == macName)
            return r.show;
    return {};
}

std::vector<LinkClient::MacRow> LinkClient::macs() const
{
    if (demo)
        return demoRows;
    std::vector<MacRow> rows;
    if (browser != nullptr)
        for (auto& f : browser->getFound())
        {
            bool isKnown = false;
            for (auto& k : knownMacs)
                isKnown = isKnown || k.name == f.name;
            rows.push_back ({ f.name, f.show, true, isKnown });
        }
    for (auto& k : knownMacs)
    {
        bool shown = false;
        for (auto& r : rows)
            shown = shown || r.name == k.name;
        if (! shown)
            rows.push_back ({ k.name, {}, k.name == macName && status == Status::linked, true });
    }
    return rows;
}

//==============================================================================
void LinkClient::link (const juce::String& mac, const juce::String& code)
{
    if (browser != nullptr)
        for (auto& f : browser->getFound())
            if (f.name == mac)
                return connect (f.name, f.host, f.port, code);
    if (auto* k = known (mac); k != nullptr && k->host.isNotEmpty())
        return connect (k->name, k->host, k->port, code);
    error = mac + " isn't on this Wi-Fi with Kaleidobox open.";
    changed();
}

void LinkClient::linkTo (const juce::String& name, const juce::String& host, int port, const juce::String& code)
{
    connect (name, host, port, code);
}

void LinkClient::connect (const juce::String& name, const juce::String& host, int port, const juce::String& code)
{
    if (connection != nullptr)
    {
        connection->onLost = nullptr;
        connection->disconnect();
    }
    auto* k = known (name);
    usingKey = code.isEmpty() && k != nullptr && k->key.isNotEmpty();
    pendingCode = code;
    macName = name;
    error = {};
    status = Status::connecting;
    lastTry = juce::Time::getMillisecondCounter();
    latest = {};

    auto c = std::make_shared<crate::link::Connection>();
    connection = c;
    const int myAttempt = ++attempt;
    c->onMessage = [this] (const Message& m) { received (m); };
    c->onLost = [this, myAttempt] { if (myAttempt == attempt) lost(); };
    changed();

    // Connecting can take a few seconds when the Mac is gone, so it happens off the message thread.
    juce::Thread::launch ([c, host, port, myAttempt, a = alive, this]
    {
        const bool ok = c->connectToSocket (host, port, 4000);
        juce::MessageManager::callAsync ([c, ok, myAttempt, a, this]
        {
            if (*a)
                connected (myAttempt, ok);
            else if (ok)
                c->disconnect();
        });
    });
}

void LinkClient::connected (int myAttempt, bool ok)
{
    if (myAttempt != attempt || connection == nullptr)
        return;
    if (! ok)
    {
        status = Status::notLinked;
        if (! usingKey)
            error = "Couldn't reach " + macName + ". Is Kaleidobox open on it, on this Wi-Fi?";
        macName = {};
        connection = nullptr;
        return changed();
    }
    Message hello ("hello");
    hello.set ("phone", crate::link::deviceName()).set ("id", phoneId);
    if (auto* k = known (macName); k != nullptr && pendingCode.isEmpty())
        hello.set ("key", k->key);
    else
        hello.set ("code", pendingCode);
    connection->send (hello);
}

void LinkClient::received (const Message& m)
{
    lastHeard = juce::Time::getMillisecondCounter();
    if (m.type == "welcome")
    {
        status = Status::linked;
        error = {};
        auto* k = known (macName);
        if (k == nullptr)
        {
            knownMacs.push_back ({ macName, {}, {}, 0 });
            k = &knownMacs.back();
        }
        k->key = m.get ("key").toString();
        if (browser != nullptr)
            for (auto& f : browser->getFound())
                if (f.name == macName)
                {
                    k->host = f.host;
                    k->port = f.port;
                }
        saveKnown();
        if (! usingKey && onSay)
            onSay ("Linked to " + macName);
        changed();
    }
    else if (m.type == "denied" || m.type == "unlinked")
    {
        const bool wasLinked = status == Status::linked;
        error = m.get ("why").toString();
        if (usingKey || m.type == "unlinked")
        {
            // The Mac forgot this phone: forget its key too, so the next link asks for the numbers.
            if (auto* k = known (macName))
                k->key = {};
            saveKnown();
        }
        status = Status::notLinked;
        if (connection != nullptr)
        {
            connection->onLost = nullptr;
            connection->disconnect();
            connection = nullptr;
        }
        if ((wasLinked || ! usingKey) && onSay)
            onSay (error);
        macName = {};
        latest = {};
        changed();
    }
    else if (m.type == "state")
    {
        latest = m;
        changed();
    }
    else if (m.type == "said")
    {
        const int req = (int) m.get ("req", 0);
        auto it = waiting.find (req);
        if (it != waiting.end())
        {
            auto done = std::move (it->second);
            waiting.erase (it);
            if (done)
                done ((bool) m.get ("ok", true), m.get ("text").toString());
        }
        else if (onSay)
            onSay (m.get ("text").toString());
    }
}

void LinkClient::lost()
{
    const bool was = status == Status::linked;
    status = Status::notLinked;
    connection = nullptr;
    latest = {};
    for (auto& [req, done] : waiting)
        if (done)
            done (false, "The link to the Mac dropped before it answered.");
    waiting.clear();
    if (was && onSay)
        onSay ("Lost the link to " + macName + ". It comes back by itself when the Mac is on the Wi-Fi.");
    macName = {};
    changed();
}

void LinkClient::unlink()
{
    if (connection != nullptr && status == Status::linked)
        connection->send (Message ("bye").set ("id", phoneId));
    if (auto* k = known (macName))
        knownMacs.erase (knownMacs.begin() + (k - knownMacs.data()));
    saveKnown();
    if (connection != nullptr)
    {
        connection->onLost = nullptr;
        connection->disconnect();
        connection = nullptr;
    }
    demo = false;
    status = Status::notLinked;
    macName = {};
    latest = {};
    changed();
}

// Every second and a half: links again by itself to a Mac it knows when it shows up.
void LinkClient::timerCallback()
{
    if (demo || status != Status::notLinked || browser == nullptr)
        return;
    if (juce::Time::getMillisecondCounter() - lastTry < 5000)
        return;
    for (auto& f : browser->getFound())
        if (auto* k = known (f.name); k != nullptr && k->key.isNotEmpty())
            return connect (f.name, f.host, f.port, {});
}

//==============================================================================
bool LinkClient::send (const MacSend& what, const juce::String& dest, std::function<void (bool, juce::String)> done)
{
    if (status != Status::linked || connection == nullptr || what.items.empty())
        return false;

    std::vector<Message> messages;
    if (dest == "kit")
    {
        Message m ("kit");
        juce::Array<juce::var> names;
        std::vector<juce::MemoryBlock> wavs;
        for (auto& it : what.items)
            if (it.audio != nullptr)
            {
                names.add (it.name);
                wavs.push_back (crate::AudioFiles::toWavData (*it.audio));
            }
        m.set ("name", what.kitName).set ("names", names);
        crate::link::addFiles (m, wavs);
        messages.push_back (std::move (m));
    }
    else
    {
        for (auto& it : what.items)
            if (it.audio != nullptr)
            {
                Message m ("sound");
                m.set ("dest", dest).set ("name", it.name).set ("beats", it.beats);
                m.data = crate::AudioFiles::toWavData (*it.audio);
                messages.push_back (std::move (m));
            }
    }
    if (messages.empty())
        return false;

    // One answer per message; `done` gets the last one (or the first failure).
    auto left = std::make_shared<int> ((int) messages.size());
    auto failed = std::make_shared<juce::String>();
    auto lastText = std::make_shared<juce::String>();
    for (auto& m : messages)
    {
        const int req = nextRequest++;
        m.set ("req", req);
        waiting[req] = [left, failed, lastText, done, n = (int) messages.size()] (bool ok, juce::String text)
        {
            if (! ok && failed->isEmpty())
                *failed = text;
            *lastText = text;
            if (--*left == 0 && done)
            {
                if (failed->isNotEmpty())
                    done (false, *failed);
                else
                    done (true, n == 1 ? *lastText : juce::String (n) + " sent to the Mac. " + *lastText);
            }
        };
    }
    // Big files go from a background thread, so the screen keeps moving.
    juce::Thread::launch ([c = connection, messages = std::move (messages)]
    {
        for (auto& m : messages)
            c->send (m);
    });
    return true;
}

void LinkClient::remote (const juce::String& action, const juce::var& value)
{
    if (status != Status::linked || connection == nullptr)
        return;
    connection->send (Message ("remote").set ("action", action).set ("value", value));
}

void LinkClient::setDemo (std::vector<MacRow> rows, const juce::String& linkedTo, const Message& demoState)
{
    demo = true;
    demoRows = std::move (rows);
    macName = linkedTo;
    status = linkedTo.isNotEmpty() ? Status::linked : Status::notLinked;
    latest = demoState;
    changed();
}
}
