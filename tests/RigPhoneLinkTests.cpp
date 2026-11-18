#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigPhoneLink.h"
#include "RigView.h"
#include "RigSong.h"
#include "tools/pocket/PocketLinkClient.h"
#include "core/AudioFiles.h"

#include <thread>

// Kaleidobox Pocket linked to the Mac app, for real over a socket on this computer: the four
// numbers, wrong numbers, sending a sound to each place, a kit, the Remote's buttons and the
// state it sees, linking again by itself with its key, and Unlink.
//
// The link talks on the message thread, so the test runs the message loop here and does its
// steps from a second thread, each one on the message thread.
namespace
{
constexpr const char* kLoopbackTest = "Rig: Kaleidobox Pocket link, phone and Mac talking";

template <typename T>
T* find (juce::Component& c, std::function<bool (T&)> wanted = {})
{
    if (auto* t = dynamic_cast<T*> (&c); t != nullptr && (! wanted || wanted (*t)))
        return t;
    for (auto* child : c.getChildren())
        if (auto* t = find<T> (*child, wanted))
            return t;
    return nullptr;
}

juce::TextButton* button (juce::Component& c, const juce::String& text)
{
    return find<juce::TextButton> (c, [&text] (juce::TextButton& b) { return b.getButtonText() == text && b.isVisible(); });
}

void onMain (std::function<void()> f)
{
    juce::MessageManager::getInstance()->callFunctionOnMessageThread ([] (void* p) -> void*
    {
        (*static_cast<std::function<void()>*> (p))();
        return nullptr;
    }, &f);
}

// The engine's audio blocks, played by the test's thread while it waits (the looper's presses
// happen there).
std::function<void()> audioTick;

// Waits (up to `ms`) for something that's checked on the message thread.
bool waitFor (std::function<bool()> done, int ms = 5000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
    while (juce::Time::getMillisecondCounter() < end)
    {
        if (audioTick)
            audioTick();
        bool ok = false;
        onMain ([&] { ok = done(); });
        if (ok)
            return true;
        juce::Thread::sleep (20);
    }
    return false;
}
}

struct RigPhoneLinkTests : juce::UnitTest
{
    RigPhoneLinkTests() : juce::UnitTest ("Rig: Kaleidobox Pocket linked over the Wi-Fi", "Rig") {}

    void runTest() override
    {
        beginTest ("Settings > iPhone shows the four numbers in the app, and says to open the app in Logic");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            {
                RigView plugin (engine, nullptr);
                expect (plugin.getPhoneLink() == nullptr, "no link in Logic");
            }
            juce::AudioDeviceManager devices;
            RigView app (engine, &devices);
            app.setSize (1500, 940);
            app.goTo ("Setup");
            auto* link = app.getPhoneLink();
            expect (link != nullptr && link->isListening(), "the app listens for the phone");
            if (link != nullptr)
            {
                auto* code = find<juce::Label> (app, [&] (juce::Label& l) { return l.getText().removeCharacters (" ") == link->getCode(); });
                expect (code != nullptr && code->isVisible() && ! code->getBounds().isEmpty(), "the numbers are on Settings");
                expect (button (app, "Unlink") != nullptr, "and Unlink");
            }
            // CRATE_PHONE_SNAPSHOT=/path/to.png saves a picture of Settings.
            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_PHONE_SNAPSHOT", {}); path.isNotEmpty())
            {
                auto image = app.createComponentSnapshot (app.getLocalBounds(), true, 1.0f);
                juce::File (path).deleteFile();
                if (auto out = juce::File (path).createOutputStream())
                    juce::PNGImageFormat().writeImageToStream (image, *out);
            }
        }

        beginTest ("Link with the numbers, send sounds and a kit, run the Remote, link again, unlink (in its own process)");
        {
            // It runs the message loop and has to stop it, which would stop later tests' messages,
            // so it runs in a copy of this program on its own.
            setenv ("CRATE_LINK_CHILD", "1", 1);
            juce::ChildProcess child;
            const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
            const bool started = child.start (juce::StringArray { exe, kLoopbackTest });
            unsetenv ("CRATE_LINK_CHILD");
            expect (started, "the copy starts");
            if (started)
            {
                const auto out = child.readAllProcessOutput();
                child.waitForProcessToFinish (120000);
                const bool ok = child.getExitCode() == 0 && out.contains ("All tests passed") && out.contains ("run the Remote");
                expect (ok, out.fromLastOccurrenceOf ("Starting tests in", true, false).substring (0, 3000));
            }
        }
    }
};

struct RigPhoneLinkLoopback : juce::UnitTest
{
    RigPhoneLinkLoopback() : juce::UnitTest (kLoopbackTest, "Rig") {}

    void runTest() override
    {
        if (juce::SystemStats::getEnvironmentVariable ("CRATE_LINK_CHILD", {}).isEmpty())
            return;   // only in the copy the test above starts
        beginTest ("Link with the numbers, send sounds and a kit, run the Remote, link again, unlink");

        auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-phone-link-" + juce::String (juce::Random().nextInt (1000000)));
        home.createDirectory();

        std::unique_ptr<RigEngine> engine;
        std::unique_ptr<RigPhoneLink> mac;
        std::unique_ptr<pocket::LinkClient> phone;
        juce::StringArray phoneHeard, macSaid;
        juce::String recPressed;

        std::thread steps ([&]
        {
            onMain ([&]
            {
                engine = std::make_unique<RigEngine>();
                engine->prepare (48000.0, 480);
                mac = std::make_unique<RigPhoneLink> (*engine, home.getChildFile ("iPhones.xml"), false);
                mac->soundsTarget = home.getChildFile ("From iPhone");
                mac->onSay = [&] (const juce::String& s) { macSaid.add (s); };
                mac->pressRec = [&] (const juce::String& t) { recPressed = t; };
                mac->recState = [&] (const juce::String& t) { return t == recPressed ? 2 : 0; };
                phone = std::make_unique<pocket::LinkClient> (home.getChildFile ("Phone"), false);
                phone->onSay = [&] (const juce::String& s) { phoneHeard.add (s); };
            });
            juce::AudioBuffer<float> in (2, 480), out (2, 480);
            audioTick = [&]
            {
                in.clear();
                engine->process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 480);
            };
            int port = 0;
            juce::String code;
            onMain ([&] { port = mac->getPort(); code = mac->getCode(); });
            expect (port > 0, "the Mac listens");
            expectEquals (code.length(), 4);

            // Wrong numbers: refused, nothing linked.
            const auto wrong = juce::String ((code.getIntValue() + 1) % 10000).paddedLeft ('0', 4);
            onMain ([&] { phone->linkTo ("Test Mac", "127.0.0.1", port, wrong); });
            expect (waitFor ([&] { return phone->getStatus() == pocket::LinkClient::Status::notLinked && phone->getError().isNotEmpty(); }), "wrong numbers are refused");
            onMain ([&] { expect (! mac->isLinked()); expect (phone->getError().contains ("don't match"), phone->getError()); });

            // The right numbers: linked, and the numbers are used up.
            onMain ([&] { phone->linkTo ("Test Mac", "127.0.0.1", port, code); });
            expect (waitFor ([&] { return phone->isLinked() && mac->isLinked(); }), "linked with the four numbers");
            onMain ([&] { expect (mac->getCode() != code, "new numbers after a link"); });
            expect (waitFor ([&] { return phone->hasState(); }), "the phone gets the Mac's state");

            // A sound to each place.
            auto tone = crate::share (sine (44100, 1, 0.5f));
            auto sendTo = [&] (const juce::String& dest, double beats = 0.0)
            {
                pocket::MacSend s;
                s.items.push_back ({ "Porch rain", tone, beats });
                s.dests = { dest };
                bool done = false, ok = false;
                juce::String text;
                onMain ([&]
                {
                    expect (phone->send (s, dest, [&] (bool fine, juce::String t) { done = true; ok = fine; text = t; }));
                });
                expect (waitFor ([&] { return done; }), "the Mac answers a send to " + dest);
                expect (ok, dest + ": " + text);
                return text;
            };
            auto text = sendTo ("sounds");
            expect (text.contains ("Sounds"), text);
            expect (home.getChildFile ("From iPhone").getChildFile ("Porch rain.wav").existsAsFile(), "the WAV is in From iPhone");

            text = sendTo ("pad");
            expect (text.contains ("MPC pad"), text);

            text = sendTo ("sampler");
            onMain ([&] { expect (engine->getSampler().getDocument().getAudio() != nullptr, "the Sampler has it"); });

            onMain ([&] { engine->getTransport().setBpm (120.0); });
            text = sendTo ("loop", 2.0);   // 1 second = 2 beats at 120
            onMain ([&]
            {
                expect (text.contains ("loop 1"), text);
                expect (engine->getLooper().getState (0) != RigLooper::State::empty, "on loop layer 1");
                expectWithinAbsoluteError (engine->getLooper().getLayer (0).lengthBeats.load(), 2.0, 1.0e-6);
                expectEquals (engine->getLoopName (0), juce::String ("Porch rain"));
            });

            // A kit of pads: onto the MPC as a new kit.
            {
                pocket::MacSend kit;
                kit.items.push_back ({ "Kick", crate::share (sine (4410, 1, 0.8f, 60.0)), 0.0 });
                kit.items.push_back ({ "Snare", crate::share (sine (4410, 1, 0.6f, 200.0)), 0.0 });
                kit.kitName = "Road kit";
                bool done = false, ok = false;
                juce::String said;
                onMain ([&] { expect (phone->send (kit, "kit", [&] (bool fine, juce::String t) { done = true; ok = fine; said = t; })); });
                expect (waitFor ([&] { return done; }), "the Mac answers the kit");
                expect (ok && said.isNotEmpty(), said);
            }

            // The Remote: mute, tempo, Rec, a loop layer, and what the phone sees.
            onMain ([&] { phone->remote ("mute", 1); phone->remote ("bpm", 97.0); phone->remote ("rec", "take"); });
            expect (waitFor ([&] { return engine->getStrip (RigEngine::drumsStrip).mute.load(); }), "Drums muted from the phone");
            expect (waitFor ([&] { return std::abs (engine->getTransport().getBpm() - 97.0) < 0.01; }), "tempo from the phone");
            expect (waitFor ([&] { return recPressed == "take"; }), "Rec a take from the phone");
            expect (waitFor ([&]
            {
                const auto mutes = phone->state().get ("mutes");
                return mutes.isArray() && (bool) mutes[1] && std::abs ((double) phone->state().get ("bpm") - 97.0) < 0.01
                       && (int) phone->state().get ("rec")["take"] == 2;
            }), "the phone sees the mute, the tempo and the take recording");
            onMain ([&] { phone->remote ("unmuteAll"); });
            expect (waitFor ([&] { return ! engine->getStrip (RigEngine::drumsStrip).mute.load(); }), "Unmute all");

            onMain ([&] { phone->remote ("recLayer"); });
            expect (waitFor ([&] { const auto st = engine->getLooper().getState (1);
                                   return mac->getRemoteLayer() == 1 && (st == RigLooper::State::armed || st == RigLooper::State::recording); }),
                    "Rec next loop layer arms the next empty layer (2)");
            onMain ([&] { phone->remote ("undoLayer"); });
            expect (waitFor ([&] { return mac->getRemoteLayer() == -1; }), "Undo layer");

            // Sections: none yet says so; with sections, a tap plays one.
            onMain ([&] { phoneHeard.clear(); phone->remote ("section", 0); });
            expect (waitFor ([&] { return phoneHeard.joinIntoString (" ").contains ("no sections"); }), "no sections yet: the phone is told");
            onMain ([&]
            {
                rigsong::addSection (engine->getTimeline(), "Intro", 4);
                rigsong::addSection (engine->getTimeline(), "Verse", 8);
                engine->getTimeline().changed();
                phone->remote ("section", 1);
            });
            expect (waitFor ([&]
            {
                const auto names = phone->state().get ("sections");
                return names.isArray() && names.size() == 2 && names[1].toString() == "Verse";
            }), "the phone sees the sections");

            // Gone and back: links again by itself with its key (no numbers).
            onMain ([&] { phone.reset(); });
            expect (waitFor ([&] { return ! mac->isLinked(); }), "the Mac sees the phone go");
            onMain ([&]
            {
                phone = std::make_unique<pocket::LinkClient> (home.getChildFile ("Phone"), false);
                phone->linkTo ("Test Mac", "127.0.0.1", port, {});
            });
            expect (waitFor ([&] { return phone->isLinked() && mac->isLinked(); }), "links again with its key");

            // Unlink on the Mac: the phone forgets its key and can't come back without numbers.
            onMain ([&] { mac->unlink(); });
            expect (waitFor ([&] { return ! phone->isLinked(); }), "the phone hears it was unlinked");
            onMain ([&] { phone->linkTo ("Test Mac", "127.0.0.1", port, {}); });
            expect (waitFor ([&] { return phone->getStatus() == pocket::LinkClient::Status::notLinked && phone->getError().isNotEmpty(); }),
                    "no key, no numbers: refused");
            onMain ([&] { expect (! mac->isLinked()); });

            audioTick = nullptr;
            onMain ([&] { phone.reset(); mac.reset(); engine.reset(); });
            juce::MessageManager::getInstance()->stopDispatchLoop();
        });

        juce::MessageManager::getInstance()->runDispatchLoop();
        steps.join();
        home.deleteRecursively();
    }
};

static RigPhoneLinkTests rigPhoneLinkTests;
static RigPhoneLinkLoopback rigPhoneLinkLoopback;
