#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <vector>

// The link between Kaleidobox on the Mac and Kaleidobox Pocket on the iPhone, over the same Wi-Fi.
//
// The Mac listens on a TCP port and says so on the network (Bonjour on Apple systems, a UDP
// broadcast elsewhere, for the desktop preview and tests). The phone finds it, connects and says
// hello with the four numbers the Mac's Settings > iPhone shows (or the key it was given the
// first time, so it links again by itself). After that both sides send Messages: a type, a few
// named fields, and an optional block of bytes (a WAV file).
//
// Everything here runs on the message thread unless it says otherwise.
namespace crate::link
{
constexpr const char* kServiceType = "_kaleidobox._tcp";   // Bonjour
constexpr int kPort = 47291;                                // the Mac tries this and the next nine
constexpr int kBroadcastPort = 47290;                       // the UDP fallback (not Apple)
constexpr juce::uint32 kMagic = 0x4b424f58;                 // "KBOX"
constexpr int kProtocol = 1;

struct Message
{
    juce::String type;
    juce::var fields;          // a DynamicObject (may be void)
    juce::MemoryBlock data;    // a file, or several end to end (see the "sizes" field)

    Message() = default;
    explicit Message (juce::String t) : type (std::move (t)), fields (new juce::DynamicObject()) {}
    Message& set (const juce::Identifier& name, const juce::var& value);
    juce::var get (const juce::Identifier& name, const juce::var& fallback = {}) const;

    juce::MemoryBlock toBlock() const;
    static bool fromBlock (const juce::MemoryBlock&, Message&);
};

// One end of a connection. Callbacks arrive on the message thread. send() can be called from any
// thread (a big file goes from a background thread so the screen doesn't stop).
class Connection : public juce::InterprocessConnection
{
public:
    Connection() : juce::InterprocessConnection (true, kMagic) {}
    ~Connection() override { disconnect(); }

    std::function<void()> onConnected, onLost;
    std::function<void (const Message&)> onMessage;

    bool send (const Message& m) { return sendMessage (m.toBlock()); }

private:
    void connectionMade() override { if (onConnected) onConnected(); }
    void connectionLost() override { if (onLost) onLost(); }
    void messageReceived (const juce::MemoryBlock&) override;
};

//==============================================================================
// A Mac that has Kaleidobox open, seen on the network.
struct Found
{
    juce::String name;      // the Mac's name ("Colin's MacBook")
    juce::String host;      // host name or IP address to connect to
    int port = 0;
    juce::String show;      // the show it has open ("Friday Set"), may be empty
};

// The Mac's side: says on the network that Kaleidobox is open, on `port`.
class Advertiser
{
public:
    Advertiser (const juce::String& macName, int port);
    ~Advertiser();
    // The open show, so phones can say "Kaleidobox open · Friday Set" before linking.
    void setShow (const juce::String&);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// The phone's side: the Macs with Kaleidobox open on this Wi-Fi, kept up to date.
class Browser
{
public:
    Browser();
    ~Browser();
    std::vector<Found> getFound() const;
    std::function<void()> onChange;   // message thread

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// This computer's or phone's name, as people know it.
juce::String deviceName();

// Several files in one message: their bytes end to end, with their sizes in "sizes".
void addFiles (Message&, const std::vector<juce::MemoryBlock>&);
std::vector<juce::MemoryBlock> getFiles (const Message&);
}
