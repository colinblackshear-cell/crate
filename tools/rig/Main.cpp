#include <juce_audio_utils/juce_audio_utils.h>

#include "RigEngine.h"
#include "RigView.h"

// The Rig: a standalone app that plays the Crate tools live, together, on one clock.
class RigApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Crate Rig"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options options;
        options.applicationName = "Crate Rig";
        options.filenameSuffix = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName = "Crate Rig";
        settings = std::make_unique<juce::PropertiesFile> (options);

        engine = std::make_unique<RigEngine>();

        // Pick up where the last show left off.
        if (auto xml = juce::parseXML (lastShowFile()))
        {
            engine->loadShow (juce::ValueTree::fromXml (*xml));
            engine->readLoopAudio (lastShowFile());
        }

        // Ask for the mic first (macOS shows its prompt), then open the audio device.
        juce::RuntimePermissions::request (juce::RuntimePermissions::recordAudio, [this] (bool granted)
        {
            const auto saved = settings->getXmlValue ("audio");
            devices.initialise (granted ? 2 : 0, 2, saved.get(), true);
            devices.addAudioCallback (engine.get());
            devices.addMidiInputDeviceCallback ({}, engine.get());
        });

        window = std::make_unique<Window> (*engine, devices);
    }

    void shutdown() override
    {
        if (engine != nullptr)
        {
            saveLastShow();
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

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File lastShowFile()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
            .getChildFile ("Crate").getChildFile ("Rig").getChildFile ("Last show.crateshow");
    }

    void saveLastShow()
    {
        const auto file = lastShowFile();
        file.getParentDirectory().createDirectory();
        if (auto xml = engine->saveShow().createXml())
            if (xml->writeTo (file))
                engine->writeLoopAudio (file);
    }

    class Window : public juce::DocumentWindow
    {
    public:
        Window (RigEngine& engine, juce::AudioDeviceManager& devices)
            : DocumentWindow ("Crate Rig", crate::Theme::background, DocumentWindow::allButtons)
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
