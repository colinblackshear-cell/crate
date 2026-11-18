#include <juce_gui_basics/juce_gui_basics.h>

#include "PocketApp.h"

// Kaleidobox Pocket: the iPhone companion. This first build is a placeholder screen with one
// Play button, to prove the sound engines build and run on the phone.
class PocketApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Kaleidobox Pocket"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        window = std::make_unique<Window> (getApplicationName());
    }

    void shutdown() override { window.reset(); }

private:
    class Window : public juce::DocumentWindow
    {
    public:
        explicit Window (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xffefe7d6), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new PocketApp(), true);
            setFullScreen (true);
            setVisible (true);
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (PocketApplication)
