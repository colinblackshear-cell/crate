#pragma once

#include "RigEngine.h"
#include "core/link/PocketLink.h"
#include <atomic>

// Kaleidobox Pocket, linked (Settings > iPhone): the Mac's end of the link to the phone app.
//
// The app listens on the Wi-Fi and says so with Bonjour. A phone links by typing the four
// numbers Settings shows; it's given a key then, and comes back by itself after that (until
// Unlink). One phone at a time is linked.
//
// What the phone can do:
//  - Send a sound: into Sounds (a WAV in ~/Music/Crate/Sounds/From iPhone, so the Sounds drawer
//    has it), onto the next empty MPC pad, into the Sampler, or onto the next empty loop layer
//    (stretched to the tempo). A kit from its pads goes onto the MPC as a new kit.
//  - Remote: Play / Stop, the tempo, the song's sections (A-H, back, next), track mutes, Rec (the
//    set, a take or knob moves), and Rec next loop layer / Undo layer. It sees what's going on
//    from the state the Mac sends it a few times a second.
//
// Message thread.
class RigPhoneLink : private juce::InterprocessConnectionServer,
                     private juce::Timer
{
public:
    // `phonesFile`: where linked phones' keys are kept (one per Mac, not in the show).
    RigPhoneLink (RigEngine&, juce::File phonesFile = defaultPhonesFile(), bool advertise = true);
    ~RigPhoneLink() override;

    static juce::File defaultPhonesFile();
    // Where sounds sent "into Sounds" go: ~/Music/Crate/Sounds/From iPhone.
    static juce::File soundsFolder();
    // Where this one puts them (tests point it somewhere else).
    juce::File soundsTarget = soundsFolder();

    bool isListening() const { return listeningPort > 0; }
    int getPort() const { return listeningPort; }
    // The four numbers a phone types to link (a new set after each link and after Unlink).
    juce::String getCode() const { return code; }
    bool isLinked() const { return phone != nullptr && phoneName.isNotEmpty(); }
    juce::String getPhoneName() const { return phoneName; }
    // Forgets every phone and drops the one linked, with new numbers.
    void unlink();
    // The show's name, for phones looking for this Mac.
    void setShow (const juce::String&);

    // The window's part. onSay: tell the user. onChange: the Settings card should repaint.
    std::function<void (const juce::String&)> onSay;
    std::function<void()> onChange;
    // A new WAV in soundsFolder(), for the Sounds drawer to look at again.
    std::function<void (const juce::File&)> onNewSound;
    // Rec on the remote (the window owns those buttons): `target` is "set", "take" or "moves".
    // pressRec presses it; recState says 0 idle, 1 armed, 2 recording.
    std::function<void (const juce::String& target)> pressRec;
    std::function<int (const juce::String& target)> recState;

    // The tracks on the remote, in its order (MPC, Drums, Bass, Synth, Vocoder, Sampler, Loops,
    // Line in), as mixer strips.
    static const std::array<int, 8>& remoteStrips();

    // Public for tests: handles a message as if it came from the linked phone; replies go to
    // `reply` (empty when there's nothing to say back).
    void handle (const crate::link::Message&, std::function<void (const crate::link::Message&)> reply);
    // A sound from the phone: "sounds", "pad", "sampler" or "loop". Says what happened.
    juce::String receiveSound (const juce::String& dest, const juce::String& name, double beats, const juce::MemoryBlock& wav);
    // A kit from the phone's pads: each sound onto the MPC's pads, as a new kit.
    juce::String receiveKit (const juce::String& name, const juce::StringArray& names, const std::vector<juce::MemoryBlock>& wavs);
    // The state the remote shows.
    crate::link::Message makeState() const;
    // The layer the remote is recording (or last recorded), -1 for none.
    int getRemoteLayer() const { return remoteLayer; }

private:
    class PhoneConnection;
    juce::InterprocessConnection* createConnectionObject() override;
    void timerCallback() override;
    void connectionGone (PhoneConnection*);
    void hello (PhoneConnection&, const crate::link::Message&);
    void remote (const crate::link::Message&);
    void newCode();
    void say (const juce::String& s) { if (onSay) onSay (s); }
    void changed() { if (onChange) onChange(); }

    RigEngine& engine;
    juce::PropertiesFile phones;
    std::unique_ptr<crate::link::Advertiser> advertiser;
    juce::OwnedArray<PhoneConnection> connections;
    PhoneConnection* phone = nullptr;   // the linked one, among connections
    juce::String phoneName, code, show;
    int listeningPort = 0;
    int remoteLayer = -1;
    juce::String lastState;
    int quietTicks = 0;
    std::atomic<bool> stopping { false };
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
