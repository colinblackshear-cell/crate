#include "PocketLink.h"

#include <atomic>
#include <map>
#include <mutex>

#if JUCE_MAC || JUCE_IOS
 #include <dns_sd.h>
 #include <arpa/inet.h>
 #include <sys/select.h>
#endif

namespace crate::link
{
//==============================================================================
Message& Message::set (const juce::Identifier& name, const juce::var& value)
{
    if (fields.getDynamicObject() == nullptr)
        fields = new juce::DynamicObject();
    fields.getDynamicObject()->setProperty (name, value);
    return *this;
}

juce::var Message::get (const juce::Identifier& name, const juce::var& fallback) const
{
    if (auto* o = fields.getDynamicObject())
        if (o->hasProperty (name))
            return o->getProperty (name);
    return fallback;
}

// [json length, 4 bytes little endian][json: {"type":..., "fields":{...}}][data]
juce::MemoryBlock Message::toBlock() const
{
    auto* head = new juce::DynamicObject();
    head->setProperty ("type", type);
    head->setProperty ("v", kProtocol);
    head->setProperty ("fields", fields);
    const auto json = juce::JSON::toString (juce::var (head), true);
    const auto* utf8 = json.toRawUTF8();
    const auto jsonBytes = (juce::uint32) std::strlen (utf8);

    juce::MemoryBlock out;
    const auto le = juce::ByteOrder::swapIfBigEndian (jsonBytes);
    out.append (&le, 4);
    out.append (utf8, jsonBytes);
    if (data.getSize() > 0)
        out.append (data.getData(), data.getSize());
    return out;
}

bool Message::fromBlock (const juce::MemoryBlock& block, Message& out)
{
    if (block.getSize() < 4)
        return false;
    juce::uint32 jsonBytes = 0;
    std::memcpy (&jsonBytes, block.getData(), 4);
    jsonBytes = juce::ByteOrder::swapIfBigEndian (jsonBytes);
    if ((size_t) jsonBytes + 4 > block.getSize())
        return false;
    const auto json = juce::String::fromUTF8 (static_cast<const char*> (block.getData()) + 4, (int) jsonBytes);
    const auto head = juce::JSON::parse (json);
    if (head.getDynamicObject() == nullptr)
        return false;
    out.type = head["type"].toString();
    out.fields = head["fields"];
    if (out.fields.getDynamicObject() == nullptr)
        out.fields = new juce::DynamicObject();
    const auto rest = block.getSize() - 4 - jsonBytes;
    out.data.reset();
    if (rest > 0)
        out.data.append (static_cast<const char*> (block.getData()) + 4 + jsonBytes, rest);
    return out.type.isNotEmpty();
}

void Connection::messageReceived (const juce::MemoryBlock& block)
{
    Message m;

    if (Message::fromBlock (block, m) && onMessage)
        onMessage (m);
}

void addFiles (Message& m, const std::vector<juce::MemoryBlock>& files)
{
    juce::Array<juce::var> sizes;
    for (auto& f : files)
    {
        sizes.add ((juce::int64) f.getSize());
        m.data.append (f.getData(), f.getSize());
    }
    m.set ("sizes", sizes);
}

std::vector<juce::MemoryBlock> getFiles (const Message& m)
{
    std::vector<juce::MemoryBlock> out;
    const auto sizes = m.get ("sizes");
    if (! sizes.isArray())
    {
        if (m.data.getSize() > 0)
            out.push_back (m.data);
        return out;
    }
    size_t at = 0;
    for (auto& s : *sizes.getArray())
    {
        const auto n = (size_t) (juce::int64) s;
        if (at + n > m.data.getSize())
            break;
        out.emplace_back (static_cast<const char*> (m.data.getData()) + at, n);
        at += n;
    }
    return out;
}

juce::String deviceName()
{
   #if JUCE_IOS
    return "iPhone";
   #else
    auto name = juce::SystemStats::getComputerName().upToFirstOccurrenceOf (".", false, false).replaceCharacter ('-', ' ').trim();
    return name.isNotEmpty() ? name : juce::String ("This computer");
   #endif
}

#if JUCE_MAC || JUCE_IOS
//==============================================================================
// Bonjour, through the dns_sd C API (part of the system on macOS and iOS). Each object runs one
// thread that waits on its service references' sockets and handles their replies; the references
// are only touched on that thread (after they're made).
namespace
{
juce::String txtValue (const void* txt, uint16_t len, const char* key)
{
    uint8_t valueLen = 0;
    if (auto* v = TXTRecordGetValuePtr (len, txt, key, &valueLen))
        return juce::String::fromUTF8 (static_cast<const char*> (v), valueLen);
    return {};
}

// Waits on some references' sockets and processes whichever have something.
bool pump (const std::vector<DNSServiceRef>& refs, int timeoutMs)
{
    fd_set fds;
    FD_ZERO (&fds);
    int maxFd = -1;
    for (auto r : refs)
    {
        const int fd = DNSServiceRefSockFD (r);
        if (fd >= 0)
        {
            FD_SET (fd, &fds);
            maxFd = juce::jmax (maxFd, fd);
        }
    }
    if (maxFd < 0)
    {
        juce::Thread::sleep (timeoutMs);
        return false;
    }
    timeval tv { timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    if (select (maxFd + 1, &fds, nullptr, nullptr, &tv) <= 0)
        return false;
    for (auto r : refs)
        if (FD_ISSET (DNSServiceRefSockFD (r), &fds))
            DNSServiceProcessResult (r);
    return true;
}
}

static void registerReply (DNSServiceRef, DNSServiceFlags, DNSServiceErrorType err, const char* name, const char*, const char*, void*)
{
    if (err != kDNSServiceErr_NoError)
        DBG ("Bonjour register failed: " << (int) err);
    else
        DBG ("Kaleidobox is on the network as " << name);
}

struct Advertiser::Impl : juce::Thread
{
    Impl (int p) : juce::Thread ("Kaleidobox link advertiser"), port (p) { startThread(); }
    ~Impl() override
    {
        stopThread (2000);
        if (ref != nullptr)
            DNSServiceRefDeallocate (ref);
    }

    void run() override
    {
        // An empty name: the Mac's own name (Sharing > Computer name), as Finder shows it.
        TXTRecordRef txt;
        TXTRecordCreate (&txt, 0, nullptr);
        TXTRecordSetValue (&txt, "v", 1, "1");
        const auto err = DNSServiceRegister (&ref, 0, 0, nullptr, kServiceType, nullptr, nullptr, htons ((uint16_t) port),
                                             TXTRecordGetLength (&txt), TXTRecordGetBytesPtr (&txt), &registerReply, nullptr);
        TXTRecordDeallocate (&txt);
        if (err != kDNSServiceErr_NoError)
        {
            ref = nullptr;
            DBG ("Bonjour register failed: " << (int) err);
            return;
        }
        while (! threadShouldExit())
        {
            juce::String newShow;
            bool update = false;
            {
                std::lock_guard<std::mutex> l (lock);
                if (showChanged)
                {
                    newShow = show;
                    showChanged = false;
                    update = true;
                }
            }
            if (update)
            {
                TXTRecordRef t;
                TXTRecordCreate (&t, 0, nullptr);
                TXTRecordSetValue (&t, "v", 1, "1");
                const auto* utf8 = newShow.toRawUTF8();
                TXTRecordSetValue (&t, "show", (uint8_t) juce::jmin<size_t> (200, std::strlen (utf8)), utf8);
                DNSServiceUpdateRecord (ref, nullptr, 0, TXTRecordGetLength (&t), TXTRecordGetBytesPtr (&t), 0);
                TXTRecordDeallocate (&t);
            }
            pump ({ ref }, 250);
        }
    }

    const int port;
    DNSServiceRef ref = nullptr;
    std::mutex lock;
    juce::String show;
    bool showChanged = false;
};

Advertiser::Advertiser (const juce::String&, int port) : impl (std::make_unique<Impl> (port)) {}
Advertiser::~Advertiser() = default;
void Advertiser::setShow (const juce::String& s)
{
    std::lock_guard<std::mutex> l (impl->lock);
    if (impl->show != s)
    {
        impl->show = s;
        impl->showChanged = true;
    }
}

struct Browser::Impl : juce::Thread
{
    explicit Impl (Browser& b) : juce::Thread ("Kaleidobox link browser"), owner (b), alive (std::make_shared<std::atomic<bool>> (true))
    {
        startThread();
    }
    ~Impl() override
    {
        *alive = false;
        stopThread (2000);
        for (auto& [key, s] : services)
            if (s.resolver != nullptr)
                DNSServiceRefDeallocate (s.resolver);
        if (browser != nullptr)
            DNSServiceRefDeallocate (browser);
    }

    struct Service
    {
        DNSServiceRef resolver = nullptr;
        Found found;
        bool resolved = false;
        int interfaces = 0;   // it's seen once per network interface (Wi-Fi, Ethernet...)
    };

    static void browseReply (DNSServiceRef, DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType err,
                             const char* name, const char* type, const char* domain, void* context)
    {
        auto& self = *static_cast<Impl*> (context);
        if (err != kDNSServiceErr_NoError)
            return;
        const auto key = juce::String::fromUTF8 (name);
        if ((flags & kDNSServiceFlagsAdd) != 0)
        {
            if (auto it = self.services.find (key); it != self.services.end())
            {
                ++it->second.interfaces;
                return;
            }
            auto& s = self.services[key];
            s.interfaces = 1;
            s.found.name = key;
            auto* resolveContext = new std::pair<Impl*, juce::String> (&self, key);
            self.contexts.emplace_back (resolveContext);
            if (DNSServiceResolve (&s.resolver, 0, ifIndex, name, type, domain, &Impl::resolveReply, resolveContext) != kDNSServiceErr_NoError)
                s.resolver = nullptr;
        }
        else
        {
            auto it = self.services.find (key);
            if (it != self.services.end() && --it->second.interfaces <= 0)
            {
                // Freed after this round of replies (the loop may still have it in its list).
                if (it->second.resolver != nullptr)
                    self.toFree.push_back (it->second.resolver);
                self.services.erase (it);
                self.publish();
            }
        }
    }

    static void resolveReply (DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType err, const char*,
                              const char* host, uint16_t port, uint16_t txtLen, const unsigned char* txt, void* context)
    {
        auto& [self, key] = *static_cast<std::pair<Impl*, juce::String>*> (context);
        if (err != kDNSServiceErr_NoError)
            return;
        auto it = self->services.find (key);
        if (it == self->services.end())
            return;
        auto& f = it->second.found;
        f.host = juce::String::fromUTF8 (host);
        f.port = ntohs (port);
        f.show = txtValue (txt, txtLen, "show");
        it->second.resolved = true;
        self->publish();
    }

    void publish()
    {
        {
            std::lock_guard<std::mutex> l (lock);
            found.clear();
            for (auto& [key, s] : services)
                if (s.resolved)
                    found.push_back (s.found);
        }
        juce::MessageManager::callAsync ([a = alive, &o = owner]
        {
            if (*a && o.onChange)
                o.onChange();
        });
    }

    void run() override
    {
        if (DNSServiceBrowse (&browser, 0, 0, kServiceType, nullptr, &Impl::browseReply, this) != kDNSServiceErr_NoError)
        {
            browser = nullptr;
            return;
        }
        while (! threadShouldExit())
        {
            std::vector<DNSServiceRef> refs { browser };
            for (auto& [key, s] : services)
                if (s.resolver != nullptr)
                    refs.push_back (s.resolver);
            pump (refs, 250);
            for (auto r : toFree)
                DNSServiceRefDeallocate (r);
            toFree.clear();
        }
    }

    Browser& owner;
    std::vector<DNSServiceRef> toFree;
    std::shared_ptr<std::atomic<bool>> alive;
    DNSServiceRef browser = nullptr;
    std::map<juce::String, Service> services;   // the thread's
    std::vector<std::unique_ptr<std::pair<Impl*, juce::String>>> contexts;
    mutable std::mutex lock;
    std::vector<Found> found;   // under lock
};

#else
//==============================================================================
// Elsewhere (the desktop preview, tests): JUCE's UDP broadcast discovery. The description
// carries "name|show".
struct Advertiser::Impl
{
    Impl (const juce::String& n, int p) : name (n), port (p) { restart(); }
    void restart()
    {
        advertiser.reset();
        advertiser = std::make_unique<juce::NetworkServiceDiscovery::Advertiser> (kServiceType, name + "|" + show, kBroadcastPort, port,
                                                                                 juce::RelativeTime::seconds (0.5));
    }
    juce::String name, show;
    int port;
    std::unique_ptr<juce::NetworkServiceDiscovery::Advertiser> advertiser;
};

Advertiser::Advertiser (const juce::String& macName, int port) : impl (std::make_unique<Impl> (macName, port)) {}
Advertiser::~Advertiser() = default;
void Advertiser::setShow (const juce::String& s)
{
    if (impl->show == s)
        return;
    impl->show = s;
    impl->restart();
}

struct Browser::Impl
{
    explicit Impl (Browser& b) : owner (b)
    {
        list.onChange = [this] { if (owner.onChange) owner.onChange(); };
    }
    Browser& owner;
    juce::NetworkServiceDiscovery::AvailableServiceList list { kServiceType, kBroadcastPort };
};

std::vector<Found> Browser::getFound() const
{
    std::vector<Found> out;
    for (auto& s : impl->list.getServices())
    {
        Found f;
        f.name = s.description.upToFirstOccurrenceOf ("|", false, false);
        f.show = s.description.fromFirstOccurrenceOf ("|", false, false);
        f.host = s.address.toString();
        f.port = s.port;
        out.push_back (f);
    }
    return out;
}
#endif

Browser::Browser() : impl (std::make_unique<Impl> (*this)) {}
Browser::~Browser() = default;

#if JUCE_MAC || JUCE_IOS
std::vector<Found> Browser::getFound() const
{
    std::lock_guard<std::mutex> l (impl->lock);
    return impl->found;
}
#endif
}
