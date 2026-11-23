#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "RigView.h"
#include "RigLastShow.h"

// The Rig: a standalone app that plays the Crate tools live, together, on one clock.
class RigApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kaleidobox"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        // The settings keep their old Crate Rig file name, so your audio and MIDI setup survive the
        // rename to Kaleidobox.
        options.applicationName = "Crate Rig";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "Crate Rig";
        settings = std::make_unique<juce::PropertiesFile> (options);

        RigEngine::startWithFactoryKit = true;
        RigView::titleAtLaunch = true;   // the opening title (not in Logic)
        engine = std::make_unique<RigEngine>();
        // The show's changed-since-saved model, for measuring it on a real Mac (nothing shows it
        // yet): KALEIDOBOX_DIRTY=1 switches it on and logs to ~/Library/Logs/Kaleidobox/dirty.log.
        if (juce::SystemStats::getEnvironmentVariable ("KALEIDOBOX_DIRTY", {}) == "1")
        {
           #if JUCE_MAC
            const auto logs = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Logs/Kaleidobox");
           #else
            const auto logs = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Kaleidobox");
           #endif
            logs.createDirectory();
            const auto file = logs.getChildFile ("dirty.log");
            engine->getDirty().log = [file] (const juce::String& line)
            {
                file.appendText (juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H:%M:%S ") + line + "\n");
            };
            engine->getDirty().setEnabled (true);
        }

        // Pick up where the last show left off.
        if (auto xml = juce::parseXML (lastShowFile()))
        {
            engine->loadShow (juce::ValueTree::fromXml (*xml));
            engine->readLoopAudio (lastShowFile());
            // A Last show from before the factory kits, with nothing on the pads: put the Street kit
            // on them, as a new Rig has (a show you open yourself stays as it was saved).
            bool padsEmpty = true;
            for (auto& bank : engine->getMpc().getProject().getBanks())
                padsEmpty = padsEmpty && bank.usedPads() == 0;
            if (padsEmpty)
                engine->loadFactoryKit (0, 0);
        }

        // Ask for the mic first (macOS shows its prompt), then open the audio device.
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [this] (bool granted)
        {
            const auto saved = settings->getXmlValue ("audio");
            devices.initialise (granted ? 2 : 0, 2, saved.get(), true);
            if (granted)
                makeSureTheMicIsOpen();
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "The mic is blocked",
                    "macOS isn't letting Kaleidobox hear your mic. Turn Kaleidobox on in System Settings > "
                    "Privacy & Security > Microphone, then quit and reopen Kaleidobox.");
            devices.addAudioCallback (engine.get());
            devices.addMidiInputDeviceCallback ({}, engine.get());
        });

        window = std::make_unique<Window> (*engine, devices);

        // A Last show that couldn't be saved at the last quit: say so (this is the one from before).
        const auto lastQuit = riglastshow::takeFailureFromLastQuit (*settings);
        if (lastQuit.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Your show wasn't saved last time", lastQuit);
    }

    void shutdown() override
    {
        if (engine != nullptr)
        {
            if (! lastShowSaved)
                saveLastShow();   // (if it fails here, the next launch says so)
            devices.removeMidiInputDeviceCallback ({}, engine.get());
            devices.removeAudioCallback (engine.get());
        }
        if (auto xml = devices.createStateXml())
            settings->setValue ("audio", xml.get());
        settings->saveIfNeeded();

        window = nullptr;
        devices.closeAudioDevice();
        engine = nullptr;
    }

    // Saves the Last show first, so a failure can be shown while there's still a window: quit
    // anyway (the Last show from before stays) or keep the app open to save elsewhere.
    void systemRequestedQuit() override
    {
        if (engine == nullptr || lastShowSaved || askingToQuit)
        {
            quit();
            return;
        }
        const auto result = saveLastShow();
        if (result.ok)
        {
            quit();
            return;
        }
        askingToQuit = true;
        juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Couldn't save your show",
                                            riglastshow::quitMessage (result), "Quit anyway", "Keep Kaleidobox open", nullptr,
                                            juce::ModalCallbackFunction::create ([this] (int quitAnyway)
                                            {
                                                askingToQuit = false;
                                                if (quitAnyway != 0)
                                                    quit();   // (shutdown doesn't try again; the next launch says it wasn't saved)
                                                else
                                                    lastShowSaved = false;
                                            }));
    }

private:
    // The saved audio set-up can have no input in it (from a time the mic was blocked, or a
    // mic that was unplugged), and then nothing would ever come in. Open the Mac's own input.
    void makeSureTheMicIsOpen()
    {
        auto setup = devices.getAudioDeviceSetup();
        const auto* device = devices.getCurrentAudioDevice();
        if (device != nullptr && ! device->getActiveInputChannels().isZero())
            return;
        auto* type = devices.getCurrentDeviceTypeObject();
        if (type == nullptr)
            return;
        type->scanForDevices();
        const auto inputs = type->getDeviceNames (true);
        if (inputs.isEmpty())
            return;
        if (setup.inputDeviceName.isEmpty() || ! inputs.contains (setup.inputDeviceName))
            setup.inputDeviceName = inputs[juce::jmax (0, type->getDefaultDeviceIndex (true))];
        setup.useDefaultInputChannels = true;
        setup.inputChannels.clear();
        devices.setAudioDeviceSetup (setup, true);
    }

    static juce::File lastShowFile()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile ("Crate").getChildFile ("Rig").getChildFile ("Last show.crateshow");
    }

    // A failed save leaves the Last show from before as it was, and is kept in the settings so
    // the next launch says so (RigLastShow.h).
    rigshow::Result saveLastShow()
    {
        lastShowSaved = true;
        return riglastshow::save (*engine, lastShowFile(), *settings);
    }
    bool lastShowSaved = false, askingToQuit = false;

    class Window : public juce::DocumentWindow
    {
    public:
        Window (RigEngine& engine, juce::AudioDeviceManager& devices)
            : DocumentWindow ("Kaleidobox", crate::Theme::background, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new RigView (engine, &devices), false);
            setResizable (true, false);
            setResizeLimits (1000, 700, 4000, 3000);

            // As big as the screen allows, up to the size the tools are designed for.
            auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
            const auto area = screen != nullptr ? screen->userBounds.toNearestInt() : juce::Rectangle<int> (0, 0, 1440, 900);
            centreWithSize (juce::jmin (1440, area.getWidth()), juce::jmin (1040, area.getHeight()));
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<juce::PropertiesFile> settings;
    juce::AudioDeviceManager devices;
    std::unique_ptr<RigEngine> engine;
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (RigApplication)
