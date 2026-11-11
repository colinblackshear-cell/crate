#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "GuideView.h"

// The Guide: the Rig's front page, with an entry for each tool that clicks through to it.
class RigGuideTests : public juce::UnitTest
{
public:
    RigGuideTests() : juce::UnitTest ("Rig guide", "Rig") {}

    void runTest() override
    {
        beginTest ("the Rig opens on the Guide, with an entry for every tool it has");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            expectEquals (view.getPage(), juce::String ("Guide"));
            auto* guide = view.getGuide();
            expect (guide != nullptr);
            if (guide == nullptr)
                return;

            // The five screens first, then the rest; nothing for a page the Rig lacks.
            const auto& shown = guide->getShown();
            expect (shown.size() >= 10, "an entry per tool");
            const char* five[] = { "Groove", "Vocoder", "Looper", "Sampler", "Stage" };
            for (int i = 0; i < 5; ++i)
            {
                expectEquals (shown[(size_t) i].page, juce::String (five[i]));
                expect (shown[(size_t) i].main);
            }
            for (const auto& e : shown)
            {
                expect (e.blurb.length() > 40 && e.tagline.isNotEmpty() && e.title.isNotEmpty(), e.page);
                expect (e.page != "Guide");
            }

            // Every entry goes to its page (the sample editor is the Sampler screen's Edit side).
            for (int i = 0; i < (int) shown.size(); ++i)
            {
                view.goTo ("Guide");
                auto* card = guide->getCard (i);
                expect (card != nullptr && card->isVisible() && card->getWidth() > 100 && card->getHeight() > 100, shown[(size_t) i].page);
                // As a click does (a Button's triggerClick only posts it).
                if (auto* b = dynamic_cast<juce::Button*> (card); b != nullptr && b->onClick)
                    b->onClick();
                const auto want = shown[(size_t) i].page == "Sampler editor" ? juce::String ("Sampler") : shown[(size_t) i].page;
                expectEquals (view.getPage(), want);
            }

            if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_GUIDE_SNAPSHOT", {}); path.isNotEmpty())
            {
                for (auto [w, h, suffix] : { std::tuple<int, int, const char*> { 1440, 960, "" }, { 1440, 1600, "-full" }, { 820, 1180, "-narrow" } })
                {
                    view.setSize (w, h);
                    view.goTo ("Guide");
                    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                    juce::File file (path + suffix + ".png");
                    file.deleteFile();
                    juce::FileOutputStream stream (file);
                    juce::PNGImageFormat().writeImageToStream (image, stream);
                }
            }
        }

        beginTest ("switched off, the Rig opens on the Groove; the Guide is still a click away");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            {
                RigView view (engine, nullptr);
                view.getGuide()->setStartsHere (false);
                expect (! view.getGuide()->startsHere());
            }
            RigView view (engine, nullptr);
            expectEquals (view.getPage(), juce::String ("Groove"));
            view.goTo ("Guide");
            expectEquals (view.getPage(), juce::String ("Guide"));
            view.getGuide()->setStartsHere (true);
            expect (! engine.screens.hasProperty ("skipGuide"));
        }

        beginTest ("entries for tools the Rig doesn't have yet are left out");
        {
            GuideView guide ([] (const juce::String& page) { return page == "Groove" || page == "MPC"; });
            expectEquals ((int) guide.getShown().size(), 2);
            bool any = false;
            for (const auto& e : GuideView::entries())
                any = any || e.page == "Drum machine";
            expect (any, "the drum machine's entry waits for its page");
        }
    }
};

static RigGuideTests rigGuideTests;
