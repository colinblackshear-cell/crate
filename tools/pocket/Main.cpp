#include <juce_gui_basics/juce_gui_basics.h>

#include "PocketApp.h"
#if ! JUCE_IOS
 #include "PocketStress.h"
#endif

// Kaleidobox Pocket: the iPhone companion. On the phone it fills the screen; on a computer
// (the CRATE_POCKET_DESKTOP preview build) it's a fixed 390x844 window. Set POCKET_SNAPSHOT to a
// folder to write a PNG of each screen with demo sounds and quit. Set POCKET_STRESS to a folder to run the
// stress mode (PocketStress.cpp: the renderer on its own real-time thread while a script taps the UI) and quit.
class PocketApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kaleidobox Pocket"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        PocketApp::Options options;
        juce::File snapshotDir, stressDir;
       #if ! JUCE_IOS
        if (const auto stressEnv = juce::SystemStats::getEnvironmentVariable ("POCKET_STRESS", {}); stressEnv.isNotEmpty())
        {
            stressDir = juce::File (stressEnv);
            options.libraryRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-stress-" + juce::String (juce::Time::currentTimeMillis()));
            options.audio = false;     // no sound card: the stress mode is the sound card
            options.phoneUi = true;
        }
        const auto folder = juce::SystemStats::getEnvironmentVariable ("POCKET_SNAPSHOT", {});
        if (folder.isNotEmpty())
        {
            snapshotDir = juce::File (folder);
            options.libraryRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-demo-" + juce::String (juce::Time::currentTimeMillis()));
            options.audio = false;
        }
       #endif

        window = std::make_unique<Window> (getApplicationName(), options);
       #if ! JUCE_IOS
        // POCKET_SIZE=375x667: another phone's screen (in points), for POCKET_STRESS
        if (const auto size = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("POCKET_SIZE", {}), "x", {}); size.size() == 2)
            window->app().setSize (juce::jlimit (200, 1400, size[0].getIntValue()), juce::jlimit (300, 1400, size[1].getIntValue()));
       #endif

       #if ! JUCE_IOS
        if (stressDir != juce::File())
        {
            const int seconds = juce::jlimit (0, 3600, juce::SystemStats::getEnvironmentVariable ("POCKET_STRESS_SECONDS", "60").getIntValue());
            juce::MessageManager::callAsync ([this, stressDir, seconds]
            {
                stress = startPocketStress (window->app(), stressDir, seconds, [this] (bool ok)
                {
                    setApplicationReturnValue (ok ? 0 : 1);
                    juce::MessageManager::callAsync ([this] { stress.reset(); quit(); });
                });
            });
        }
       #endif

        if (snapshotDir != juce::File())
        {
            auto& app = window->app();
            PocketApp::fillDemoLibrary (app.library());
            juce::MessageManager::callAsync ([this, snapshotDir]
            {
                window->app().writeSnapshots (snapshotDir);
                quit();
            });
        }
    }

    void shutdown() override
    {
        stress.reset();
        window.reset();
    }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        Window (const juce::String& name, PocketApp::Options options)
            : DocumentWindow (name, juce::Colour (0xffefe7d6), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content = new PocketApp (options), true);
           #if JUCE_IOS
            setFullScreen (true);
           #else
            setResizable (false, false);
            centreWithSize (getWidth(), getHeight());
           #endif
            setVisible (true);
        }

        PocketApp& app() { return *content; }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

    private:
        PocketApp* content = nullptr;
    };

    std::unique_ptr<Window> window;
    std::shared_ptr<void> stress;
};

START_JUCE_APPLICATION (PocketApplication)
