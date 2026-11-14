#include "TestHelpers.h"

#include "RigPlugin.h"
#include "RigPanels.h"
#include "RigView.h"

using rigpanels::PanelButton;
using rigpanels::ScreenBar;

// The Rig as Logic shows it (the plugin's own window): every screen opens, and no button on it
// is blank. CRATE_PLUGIN_SNAPSHOT=/path/prefix saves a picture of each screen.
namespace
{
template <typename T>
void collect (juce::Component& c, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&c))
        out.push_back (t);
    for (auto* child : c.getChildren())
        collect (*child, out);
}

juce::String pathOf (juce::Component& c)
{
    juce::String path;
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
    {
        auto name = p->getName().isNotEmpty() ? p->getName() : juce::String (typeid (*p).name());
        path = name + (path.isEmpty() ? "" : " > ") + path;
    }
    return path;
}

// A button you can see, with nothing on it to say what it does.
bool shown (juce::Component& c)
{
    for (auto* p = &c; p != nullptr; p = p->getParentComponent())
        if (! p->isVisible() || p->getWidth() <= 0 || p->getHeight() <= 0)
            return false;
    return true;
}

bool isBlank (juce::Button& b)
{
    if (! shown (b) || b.getWidth() < 8 || b.getHeight() < 8)
        return false;
    if (auto* panel = dynamic_cast<PanelButton*> (&b))
        return panel->text.trim().isEmpty() && panel->icon == PanelButton::Icon::none && ! panel->empty
            && panel->getButtonText().trim().isEmpty();
    if (dynamic_cast<juce::TextButton*> (&b) != nullptr)
        return b.getButtonText().trim().isEmpty();
    return false;
}
}

struct RigPluginScreensTests : juce::UnitTest
{
    RigPluginScreensTests() : juce::UnitTest ("Every screen in Logic's plugin window", "Rig") {}

    void runTest() override
    {
        beginTest ("each screen opens in the plugin window, and none of its buttons is blank");
        {
            RigPlugin plugin;
            plugin.prepareToPlay (48000.0, 512);
            // As in Logic, where every tool's own output is on: the Mixer shows Own out.
            for (int st = 0; st < RigEngine::numStems; ++st)
                plugin.getEngine().setStemOn (st, true);
            // Twice: closing the window in Logic and opening it again gives the same screens.
            for (int opening = 0; opening < 2; ++opening)
                checkScreens (plugin, opening);
        }
    }

    void checkScreens (RigPlugin& plugin, int opening)
    {
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor (plugin.createEditor());
            expect (editor != nullptr);
            editor->setSize (1440, 960);
            editor->setVisible (true);
            std::vector<RigView*> views;
            collect (*editor, views);
            expectEquals ((int) views.size(), 1);
            if (views.empty())
                return;
            auto& view = *views.front();
            const auto snapshot = juce::SystemStats::getEnvironmentVariable ("CRATE_PLUGIN_SNAPSHOT", {});

            const juce::StringArray places { "Guide", "Groove", "Vocoder", "Looper", "Sampler", "MPC", "Sampler editor", "Mixer",
                                             "Effects", "Song", "Drum machine", "Bass", "Synth", "Sounds", "Setup", "More" };
            for (const auto& place : places)
            {
                view.goTo (place);
                if (place == "Sounds")
                    expect (view.isSoundsOpen(), "Sounds opens as a drawer");
                else
                    expect (view.getPlace() == place || view.getPage() == place, place + " opens (on " + view.getPage() + ")");
                if (auto* page = view.getCurrentContent())
                    expect (page->isVisible() && page->getWidth() > 0, place + " shows something");
                else
                    expect (false, place + " has a screen (window opened " + juce::String (opening + 1) + " times)");
                std::vector<juce::Button*> buttons;
                collect (*editor, buttons);
                for (auto* b : buttons)
                    if (isBlank (*b))
                    {
                        logMessage ("BLANK on " + place + ": " + pathOf (*b) + " at " + b->getBounds().toString());
                        expect (false, "a blank button on " + place);
                    }
                if (snapshot.isNotEmpty())
                {
                    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                    juce::File file (snapshot + (opening > 0 ? "-again-" : "-") + place.replaceCharacter (' ', '-') + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
                view.showSounds (false);
            }

            // The view buttons along the top open their views.
            std::vector<ScreenBar*> bars;
            collect (*editor, bars);
            expectEquals ((int) bars.size(), 1);
            if (! bars.empty())
                for (int i = 0; i < bars.front()->tabs.size(); ++i)
                {
                    bars.front()->tabs[i]->onClick();
                    expectEquals (view.getView(), ScreenBar::views()[i]);
                }
        }
    }
};

static RigPluginScreensTests rigPluginScreensTests;
