#include "TestHelpers.h"

#include "../tools/pocket/BeatsEngine.h"
#include "DrumProcessor.h"
#include "DrumView.h"
#include "core/AudioFiles.h"

using namespace crate;

namespace
{
juce::AudioBuffer<float> tone (int samples, float level, float step = 0.05f)
{
    juce::AudioBuffer<float> b (1, samples);
    for (int i = 0; i < samples; ++i)
        b.setSample (0, i, level * std::sin (step * (float) i));
    return b;
}

juce::File tempFolder (const juce::String& name)
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    dir.deleteRecursively();
    dir.createDirectory();
    return dir;
}

juce::TextButton* findButton (juce::Component& c, const juce::String& startsWith)
{
    if (auto* b = dynamic_cast<juce::TextButton*> (&c); b != nullptr && b->isVisible() && b->getButtonText().startsWith (startsWith))
        return b;
    for (auto* child : c.getChildren())
        if (auto* b = findButton (*child, startsWith))
            return b;
    return nullptr;
}

void collectChips (juce::Component& c, std::vector<DrumView::SourceChip*>& out)
{
    if (auto* chip = dynamic_cast<DrumView::SourceChip*> (&c))
        out.push_back (chip);
    for (auto* child : c.getChildren())
        collectChips (*child, out);
}

// Plays a hit through the phone's engine and returns how long it sounded (in samples) and its peak.
std::pair<int, float> hearPhone (pocket::BeatsEngine& e, int voice, double seconds = 1.0)
{
    e.hitVoice (voice);
    constexpr int block = 441;
    std::vector<float> l (block), r (block);
    int last = 0;
    float peak = 0;
    for (int done = 0; done < (int) (seconds * 44100.0); done += block)
    {
        std::fill (l.begin(), l.end(), 0.0f);
        std::fill (r.begin(), r.end(), 0.0f);
        e.render (l.data(), r.data(), block);
        for (int i = 0; i < block; ++i)
        {
            const float x = std::abs (l[(size_t) i]);
            peak = std::max (peak, x);
            if (x > 1.0e-3f)
                last = done + i;
        }
    }
    return { last, peak };
}
}

struct DrumKitUiTests : juce::UnitTest
{
    DrumKitUiTests() : juce::UnitTest ("Drum machine: your own kits on the panels", "Drums") {}

    void runTest() override
    {
        auto dir = tempFolder ("crate-drumkit-ui-test");
        auto kick = dir.getChildFile ("Kick_Deep.wav"), snare = dir.getChildFile ("Snare_Vinyl.wav"), hat = dir.getChildFile ("Hat_Closed.wav");
        beginTest ("a file dropped on a sound's column puts it on that sound, and the label and choice show it");
        {
            expect (AudioFiles::write (kick, tone (8000, 0.6f, 0.02f)));
            expect (AudioFiles::write (snare, tone (6000, 0.6f, 0.2f)));
            expect (AudioFiles::write (hat, tone (3000, 0.5f, 0.8f)));
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            expect (view != nullptr);
            view->setSize (1128, 604);
            std::vector<DrumView::SourceChip*> chips;
            collectChips (*view, chips);
            expectEquals ((int) chips.size(), (int) drums::kVoices, "a source label per sound");
            expectEquals (view->getSourceLabel (drums::snare), juce::String ("Analog"));
            expect (! view->isSoundChoiceDimmed (drums::snare));
            expect (view->getKitLabel() == "Kit: Analog");

            expect (view->isInterestedInFileDrag ({ snare.getFullPathName() }), "an audio file");
            expect (view->isInterestedInFileDrag ({ dir.getFullPathName() }), "a folder");
            auto note = dir.getChildFile ("notes.txt");
            note.replaceWithText ("hello");
            expect (! view->isInterestedInFileDrag ({ note.getFullPathName() }), "a text file is not a sound");

            const auto at = view->columnBounds (drums::snare).getCentre();
            view->filesDropped ({ snare.getFullPathName() }, at.x, at.y);
            expect (p.getVoiceSample (drums::snare) != nullptr, "the snare has the sample");
            expect (p.getVoiceSample (drums::kick) == nullptr, "no other sound changed");
            expectEquals (view->getSourceLabel (drums::snare), juce::String ("Snare_Vinyl.wav"));
            expect (view->isSoundChoiceDimmed (drums::snare), "the sound choice doesn't apply now");
            expect (! view->isSoundChoiceDimmed (drums::kick));
            expect (view->getStatus().contains ("Snare_Vinyl.wav"), "a message says what happened");
            expectEquals (chips[(size_t) drums::snare]->text, juce::String ("Snare_Vinyl.wav"));
            expect (chips[(size_t) drums::snare]->sample && ! chips[(size_t) drums::kick]->sample);

            // The same drop on a different column.
            const auto atKick = view->columnBounds (drums::kick).getCentre();
            view->filesDropped ({ kick.getFullPathName() }, atKick.x, atKick.y);
            expectEquals (view->getSourceLabel (drums::kick), juce::String ("Kick_Deep.wav"));

            // A file dropped off the columns goes on the picked sound.
            view->selectVoice (drums::clap, false);
            view->filesDropped ({ hat.getFullPathName() }, 400, 590);
            expectEquals (view->getSourceLabel (drums::clap), juce::String ("Hat_Closed.wav"));

            // A file that can't be read leaves the sound alone and says so.
            view->filesDropped ({ note.getFullPathName() }, at.x, at.y);
            expectEquals (view->getSourceLabel (drums::snare), juce::String ("Snare_Vinyl.wav"));

            view->timerCallbackForTest();
            expect (findButton (*view, "Kit: ") != nullptr, "the KIT chip");
            expect (findButton (*view, "Load kit") != nullptr, "the Load kit button");
        }

        beginTest ("Back to analog puts the sound's own voice back, and the choice applies again");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            view->setSize (1128, 604);
            expect (view->putSampleOnVoice (drums::snare, snare));
            expect (p.getVoiceSample (drums::snare) != nullptr);
            view->backToAnalog (drums::snare);
            expect (p.getVoiceSample (drums::snare) == nullptr);
            expectEquals (view->getSourceLabel (drums::snare), juce::String ("Analog"));
            expect (! view->isSoundChoiceDimmed (drums::snare));
            expect (! view->putSampleOnVoice (drums::snare, dir.getChildFile ("missing.wav")), "a missing file");
            expect (view->getStatus().startsWith ("Couldn't read"));
        }

        beginTest ("a folder dropped anywhere loads a kit, and says how many sounds");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            view->setSize (1128, 604);
            view->filesDropped ({ dir.getFullPathName() }, 600, 500);
            expect (p.getVoiceSample (drums::kick) != nullptr && p.getVoiceSample (drums::snare) != nullptr, "kick and snare from the folder");
            expect (p.getVoiceSample (drums::closedHat) != nullptr, "the hat");
            expectEquals (view->getStatus(), "Loaded 3 sounds from " + dir.getFileName());
            expectEquals (p.getKitName(), dir.getFileName());
            expectEquals (view->getKitLabel(), "Kit: " + dir.getFileName());
            view->timerCallbackForTest();
            auto* chip = findButton (*view, "Kit: ");
            expect (chip != nullptr && chip->getButtonText().contains (dir.getFileName()), "the KIT chip names the kit");

            view->backToAnalog (drums::kick);
            expect (p.hasSamples());
            // A folder with nothing in it says so and changes nothing.
            auto empty = tempFolder ("crate-drumkit-ui-empty");
            view->filesDropped ({ empty.getFullPathName() }, 600, 500);
            expectEquals (view->getStatus(), "No drum sounds found in " + empty.getFileName());
            expect (p.getVoiceSample (drums::snare) != nullptr, "the kit is still there");
            empty.deleteRecursively();
        }

        beginTest ("several files dropped together load as a kit; a new kit replaces the old one");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            view->setSize (1128, 604);
            view->filesDropped ({ dir.getFullPathName() }, 600, 500);
            expect (p.getVoiceSample (drums::closedHat) != nullptr);
            view->filesDropped ({ kick.getFullPathName(), snare.getFullPathName() }, 600, 500);
            expect (p.getVoiceSample (drums::kick) != nullptr && p.getVoiceSample (drums::snare) != nullptr);
            expect (p.getVoiceSample (drums::closedHat) == nullptr, "the hat of the old kit is gone");
            expectEquals (view->getStatus(), "Loaded 2 sounds from " + dir.getFileName());
            p.clearSamples();
            view->timerCallbackForTest();
            expectEquals (view->getSourceLabel (drums::kick), juce::String ("Analog"));
        }

        beginTest ("the kit chip and Load kit sit in the nameplate, clear of the display and of each other");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            for (auto size : { juce::Point<int> (1128, 604), juce::Point<int> (1180, 840), juce::Point<int> (1000, 640) })
            {
                view->setSize (size.x, size.y);
                auto* chip = findButton (*view, "Kit: ");
                auto* load = findButton (*view, "Load kit");
                expect (chip != nullptr && load != nullptr);
                if (chip == nullptr || load == nullptr)
                    continue;
                expect (chip->getBottom() <= 58 && load->getBottom() <= 58, "in the nameplate");
                expect (chip->getRight() <= load->getX(), "side by side");
                expect (chip->getWidth() >= 70 && load->getWidth() >= 60, "wide enough to read");
                expect (load->getRight() < size.x - 300, "left of the display");
            }
        }

        dir.deleteRecursively();

        beginTest ("the phone's engine plays a sound on a drum sound, and the idea remembers which");
        {
            pocket::BeatsEngine e;
            e.prepare (44100.0);
            auto analog = hearPhone (e, drums::clave);
            auto s = std::make_shared<drums::Sample>();
            s->audio = crate::share (tone (2205, 0.7f, 0.1f));   // a tenth of a second
            s->name = "Mine";
            e.setDrumSample (drums::clave, s);
            expect (e.getDrumSample (drums::clave) == s);
            auto mine = hearPhone (e, drums::clave);
            expect (mine.second > 0.1f, "it sounds");
            expect (mine.first > 1500 && mine.first < 12000, "about as long as the sound (" + juce::String (mine.first) + " samples)");
            expect (mine.first != analog.first || std::abs (mine.second - analog.second) > 0.01f, "not the analog clave");
            e.setDrumSample (drums::clave, nullptr);
            auto back = hearPhone (e, drums::clave);
            expect (std::abs (back.first - analog.first) < 300 && std::abs (back.second - analog.second) < 0.15f, "back to analog (" + juce::String (back.first) + " / " + juce::String (analog.first) + ", " + juce::String (back.second) + " / " + juce::String (analog.second) + ")");

            pocket::DrumData d;
            expect (d.toVar().getProperty ("sampleIds", {}).isVoid(), "no kit: nothing saved");
            d.sampleIds[(size_t) drums::kick] = "id-kick";
            d.sampleIds[(size_t) drums::closedHat] = "id-hat";
            const auto text = juce::JSON::toString (d.toVar());
            const auto again = pocket::DrumData::fromVar (juce::JSON::parse (text));
            expectEquals (again.sampleIds[(size_t) drums::kick], juce::String ("id-kick"));
            expectEquals (again.sampleIds[(size_t) drums::closedHat], juce::String ("id-hat"));
            expect (again.sampleIds[(size_t) drums::snare].isEmpty());
            expect (pocket::DrumData::fromVar ({}).sampleIds[(size_t) drums::kick].isEmpty(), "an old idea has no kit");
        }
    }
};

static DrumKitUiTests drumKitUiTests;
