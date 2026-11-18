#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Audio.h"
#include "core/link/PocketLink.h"

#include <map>

// The phone's end of the link to Kaleidobox on the Mac (core/link/PocketLink.h has the wire part).
//
// It looks for Macs with Kaleidobox open on the Wi-Fi. Link: connect to one and type the four numbers
// from its Settings > iPhone; the Mac hands back a key, kept in Documents/link.json, so the phone
// links again by itself whenever that Mac shows up (until Unlink, on either side).
//
// Linked, it sends sounds and kits (from a background thread, so the screen keeps going), the
// Remote's buttons, and keeps the latest state the Mac sends (tempo, sections, mutes, loops, Rec).
//
// Message thread.
namespace pocket
{
// What the Send to Mac sheet sends: one sound, some loop layers, or a kit of pads.
struct MacSend
{
    struct Item
    {
        juce::String name;
        crate::SharedAudio audio;   // 44.1 kHz, effects already in
        double beats = 0;           // > 0 for loops: its length, so the Mac stretches it to its tempo
    };
    juce::String title, detail;     // "Porch rain", "0:12 · WAV"
    std::vector<Item> items;
    juce::StringArray dests;        // what it can go to: "sounds", "pad", "sampler", "loop", "kit"
    juce::String dest;              // picked to start with
    juce::String kitName;           // for "kit"
};

class LinkClient : private juce::Timer
{
public:
    explicit LinkClient (juce::File documents, bool browse = true);
    ~LinkClient() override;

    enum class Status { notLinked, connecting, linked };
    Status getStatus() const { return status; }
    bool isLinked() const { return status == Status::linked; }
    // The Mac linked to (or being linked to); empty when not linked.
    juce::String getMacName() const { return macName; }
    juce::String getShow() const;
    juce::String getError() const { return error; }

    // The Macs to show on Link to your Mac: those with Kaleidobox open now, and ones linked before
    // that aren't (Kaleidobox not open).
    struct MacRow { juce::String name, show; bool open = false, known = false; };
    std::vector<MacRow> macs() const;

    // Connects to `mac` (a name from macs()) and links with the four numbers typed.
    void link (const juce::String& mac, const juce::String& code);
    // Connects straight to a host and port (tests, or a Mac Bonjour can't see).
    void linkTo (const juce::String& name, const juce::String& host, int port, const juce::String& code);
    // Unlinks and forgets the Mac's key (it forgets this phone too).
    void unlink();

    // Sends. False (and nothing sent) when not linked. `done` comes back on the message thread
    // with what the Mac said.
    bool send (const MacSend&, const juce::String& dest, std::function<void (bool ok, juce::String text)> done);
    void remote (const juce::String& action, const juce::var& value = {});

    // The Mac's latest state (empty fields until the first one arrives).
    const crate::link::Message& state() const { return latest; }
    bool hasState() const { return latest.type == "state"; }

    std::function<void()> onChange;                       // status, Macs or state changed
    std::function<void (const juce::String&)> onSay;      // the Mac has something to tell you

    // Screenshot mode: pretend to see these Macs, or to be linked with this state.
    void setDemo (std::vector<MacRow> rows, const juce::String& linkedTo, const crate::link::Message& demoState);

private:
    struct Known { juce::String name, key, host; int port = 0; };
    void timerCallback() override;
    void connect (const juce::String& name, const juce::String& host, int port, const juce::String& code);
    void connected (int attempt, bool ok);
    void received (const crate::link::Message&);
    void lost();
    void loadKnown();
    void saveKnown() const;
    void changed() { if (onChange) onChange(); }
    Known* known (const juce::String& name);

    juce::File file;
    juce::String phoneId;
    std::vector<Known> knownMacs;
    std::unique_ptr<crate::link::Browser> browser;
    std::shared_ptr<crate::link::Connection> connection;
    int attempt = 0;
    Status status = Status::notLinked;
    juce::String macName, pendingCode, error;
    bool usingKey = false;
    crate::link::Message latest;
    std::map<int, std::function<void (bool, juce::String)>> waiting;   // sends waiting for the Mac's word, by request id
    int nextRequest = 1;
    juce::uint32 lastHeard = 0, lastTry = 0;

    bool demo = false;
    std::vector<MacRow> demoRows;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
}
