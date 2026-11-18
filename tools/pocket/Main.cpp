#include <juce_gui_basics/juce_gui_basics.h>

#include "PocketApp.h"

// Kaleidobox Pocket: the iPhone companion. On the phone it fills the screen; on a computer
// (the CRATE_POCKET_DESKTOP preview build) it's a fixed 390x844 window. Set POCKET_SNAPSHOT to a
// folder to write a PNG of each screen with demo sounds and quit.
class PocketApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kaleidobox Pocket"; }
    const juce::String getApplicationVersion() override { return "0.2.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        PocketApp::Options options;
        juce::File snapshotDir;
       #if ! JUCE_IOS
        const auto folder = juce::SystemStats::getEnvironmentVariable ("POCKET_SNAPSHOT", {});
        if (folder.isNotEmpty())
        {
            snapshotDir = juce::File (folder);
            options.libraryRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-demo-" + juce::String (juce::Time::currentTimeMillis()));
            options.audio = false;
        }
       #endif

        window = std::make_unique<Window> (getApplicationName(), options);

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

    void shutdown() override { window.reset(); }

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
};

START_JUCE_APPLICATION (PocketApplication)
