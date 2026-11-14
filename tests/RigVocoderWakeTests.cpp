// The vocoder going quiet in Logic: Colin played it, went to the Synth and the Effects, came
// back, and no pedal or sound did anything until he reloaded the plugin. The sound itself
// survives the trip (checked first, with the plugin's own window open). What does leave every
// pedal silent is the vocoder switched off, muted, left out of a solo or with its fader down.
// The Vocoder screen must say which, bring it back with one click, and come back on by itself
// when you play it (a pedal, a sound, a chord).
#include "TestHelpers.h"

#include "GrooveView.h"
#include "EffectsView.h"
#include "Parameters.h"
#include "Presets.h"
#include "RigPlugin.h"
#include "RigView.h"
#include "VocoderScreen.h"

namespace
{
template <typename T>
void collectAll (juce::Component& c, std::vector<T*>& out)
{
    if (auto* t = dynamic_cast<T*> (&c))
        out.push_back (t);
    for (auto* child : c.getChildren())
        collectAll (*child, out);
}

template <typename T>
T* find (juce::Component& c)
{
    std::vector<T*> all;
    collectAll (c, all);
    return all.empty() ? nullptr : all.front();
}

int soundCalled (const juce::String& name)
{
    for (int i = 0; i < (int) Presets::all().size(); ++i)
        if (name == Presets::all()[(size_t) i].name)
            return i;
    return 1;
}

// Kaleidobox as Logic runs it, with its window open and a voice on the side chain.
struct LogicBench
{
    RigPlugin plugin;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    RigView* view = nullptr;
    juce::AudioBuffer<float> in { 2, 256 }, out { 2, 256 };
    double phase = 0;

    LogicBench()
    {
        plugin.prepareToPlay (48000.0, 256);
        editor.reset (plugin.createEditor());
        editor->setSize (1440, 960);
        view = find<RigView> (*editor);
    }

    RigEngine& engine() { return plugin.getEngine(); }

    template <typename Page>
    Page* page (const juce::String& name)
    {
        view->goTo (name);
        return find<Page> (*editor);
    }

    // How loud the vocoder's strip is, singing into it for a while.
    float vocoder (double seconds)
    {
        auto& e = engine();
        e.getStrip (RigEngine::vocoderStrip).peak = 0.0f;
        for (int b = 0; b < (int) (seconds * 48000.0 / 256); ++b)
        {
            for (int i = 0; i < 256; ++i)
            {
                phase += 140.0 / 48000.0;
                const float v = (float) (0.3 * (2.0 * (phase - std::floor (phase)) - 1.0));
                in.setSample (0, i, v);
                in.setSample (1, i, v);
            }
            e.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, 256);
        }
        return e.getStrip (RigEngine::vocoderStrip).peak.load();
    }
};
}

struct RigVocoderWakeTests : juce::UnitTest
{
    RigVocoderWakeTests() : juce::UnitTest ("Rig vocoder never silent by surprise", "Rig") {}

    void runTest() override
    {
        beginTest ("French Touch, the Synth and its effects, back to the Vocoder: it still sings");
        {
            LogicBench b;
            auto* vocoder = b.page<VocoderScreen> ("Vocoder");
            expect (vocoder != nullptr);
            vocoder->pickSound (soundCalled ("French Touch"));
            const float before = b.vocoder (1.0);
            expectGreaterThan (before, 0.02f, "it sings to start with");

            expect (b.page<juce::Component> ("Synth") != nullptr);
            b.engine().addMidiNow (juce::MidiMessage::noteOn (1, 60, 1.0f));
            b.vocoder (0.3);
            b.engine().addMidiNow (juce::MidiMessage::noteOff (1, 60));
            auto* effects = b.page<EffectsView> ("Effects");
            expect (effects != nullptr);
            effects->showStrip (RigEngine::synthStrip);
            for (int pedal = 0; pedal < 13; ++pedal)
            {
                effects->stomp (pedal);
                b.vocoder (0.05);
            }

            vocoder = b.page<VocoderScreen> ("Vocoder");
            expect (vocoder->whyQuiet().isEmpty());
            expect (! vocoder->isWakeShown());
            vocoder->stomp (VocoderScreen::echo);
            vocoder->stomp (VocoderScreen::space);
            expectGreaterThan (b.vocoder (1.0), before * 0.3f, "it still sings, with Echo and Space");
            vocoder->pickSound (soundCalled ("French Touch") + 1);
            expectGreaterThan (b.vocoder (1.0), before * 0.3f, "and with another sound");
        }

        beginTest ("switched off by a stray click: the screen says so, and a sound brings it back");
        {
            LogicBench b;
            auto* vocoder = b.page<VocoderScreen> ("Vocoder");
            vocoder->pickSound (soundCalled ("French Touch"));
            const float before = b.vocoder (1.0);

            // The vocoder's On button, next to the Synth's play button.
            auto* groove = b.page<GrooveView> ("Groove");
            expect (groove != nullptr);
            groove->pressTrackButton (GrooveView::vocoder);
            expect (! b.engine().vocoderOn.load());

            vocoder = b.page<VocoderScreen> ("Vocoder");
            vocoder->refresh();
            expectEquals (vocoder->whyQuiet(), juce::String ("Vocoder is off"));
            expect (vocoder->isWakeShown(), "the red Turn on shows");
            b.vocoder (0.2);   // (it fades out)
            expectLessThan (b.vocoder (0.5), 1.0e-4f, "and it is quiet");

            vocoder->pickSound (soundCalled ("French Touch") + 1);
            expect (b.engine().vocoderOn.load(), "picking a sound turns it back on");
            vocoder->refresh();
            expect (! vocoder->isWakeShown());
            expectGreaterThan (b.vocoder (1.0), before * 0.3f, "and you hear it");
        }

        beginTest ("muted or off: stomping a pedal on, a chord or the FX switch bring it back");
        {
            const std::function<void (VocoderScreen&)> plays[] = {
                [] (VocoderScreen& s) { s.stomp (VocoderScreen::echo); },
                [] (VocoderScreen& s) { s.stomp (VocoderScreen::space); },
                [] (VocoderScreen& s) { s.pressChord (2); },
                [] (VocoderScreen& s) { if (s.isEffectsOn()) s.pressEffects(); s.pressEffects(); },
                [] (VocoderScreen& s) { s.stepSound (1); },
            };
            for (const auto& play : plays)
                for (int muted = 0; muted < 2; ++muted)
                {
                    LogicBench b;
                    auto* vocoder = b.page<VocoderScreen> ("Vocoder");
                    if (muted == 1)
                        b.engine().getStrip (RigEngine::vocoderStrip).mute = true;
                    else
                        b.engine().vocoderOn = false;
                    vocoder->refresh();
                    expect (vocoder->isWakeShown());
                    expectEquals (vocoder->whyQuiet(), juce::String (muted == 1 ? "Vocoder is muted" : "Vocoder is off"));
                    play (*vocoder);
                    expect (b.engine().vocoderOn.load());
                    expect (! b.engine().getStrip (RigEngine::vocoderStrip).mute.load());
                    vocoder->refresh();
                    expect (! vocoder->isWakeShown());
                }
        }

        beginTest ("stomping a pedal off leaves the vocoder switched off");
        {
            LogicBench b;
            auto* vocoder = b.page<VocoderScreen> ("Vocoder");
            vocoder->stomp (VocoderScreen::space);
            b.engine().vocoderOn = false;
            vocoder->stomp (VocoderScreen::space);
            expect (! b.engine().vocoderOn.load());
        }

        beginTest ("another track soloed, or the fader down: Turn on fixes it and leaves the solo as it was");
        {
            LogicBench b;
            auto* vocoder = b.page<VocoderScreen> ("Vocoder");
            vocoder->pickSound (soundCalled ("French Touch"));
            const float before = b.vocoder (1.0);
            auto& e = b.engine();
            e.getStrip (RigEngine::synthStrip).solo = true;
            vocoder->refresh();
            expectEquals (vocoder->whyQuiet(), juce::String ("Another track is soloed"));
            vocoder->wake();
            expect (e.getStrip (RigEngine::synthStrip).solo.load(), "the synth stays soloed");
            expect (! e.isSilenced (RigEngine::vocoderStrip));
            expectGreaterThan (b.vocoder (1.0), before * 0.3f);

            e.getStrip (RigEngine::vocoderStrip).level = 0.0f;
            vocoder->refresh();
            expectEquals (vocoder->whyQuiet(), juce::String ("Its fader is down"));
            vocoder->wake();
            expectWithinAbsoluteError (e.getStrip (RigEngine::vocoderStrip).level.load(), 0.8f, 1.0e-6f);
            expect (vocoder->whyQuiet().isEmpty());
            expectGreaterThan (b.vocoder (1.0), before * 0.3f, "and it's heard");

            e.vocoderOn = false;
            e.getStrip (RigEngine::vocoderStrip).mute = true;
            vocoder->wake();
            expect (e.vocoderOn.load() && ! e.getStrip (RigEngine::vocoderStrip).mute.load());
        }
    }
};

static RigVocoderWakeTests rigVocoderWakeTests;
