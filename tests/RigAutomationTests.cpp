#include "TestHelpers.h"

#include "RigAutomation.h"
#include "RigEngine.h"
#include "RigLooper.h"
#include "RigView.h"
#include "RigPanels.h"
#include "AutomationPanel.h"
#include "RigExport.h"

// Long takes (a whole long set on one loop, kept when the clock stops or the song switches
// loops) and automation (knob moves recorded over the beat, played back, drawn, cleared, saved).
namespace
{
// A few settings to automate, like a tool's.
struct FakeTool
{
    juce::AudioParameterFloat cutoff { juce::ParameterID { "cutoff", 1 }, "Cutoff", 0.0f, 1.0f, 0.5f };
    juce::AudioParameterFloat mix { juce::ParameterID { "mix", 1 }, "Mix", 0.0f, 1.0f, 0.2f };
    juce::AudioParameterChoice wave { juce::ParameterID { "wave", 1 }, "Wave", juce::StringArray { "Saw", "Square", "Sine" }, 0 };
    std::vector<juce::AudioParameterFloat*> many;   // more settings, for a "new sound" landing
    std::vector<std::unique_ptr<juce::AudioParameterFloat>> owned;
    juce::AudioProcessorParameter* at (int i)
    {
        if (i == 0) return &cutoff;
        if (i == 1) return &mix;
        if (i == 2) return &wave;
        return juce::isPositiveAndBelow (i - 3, (int) many.size()) ? many[(size_t) (i - 3)] : nullptr;
    }
    int size() const { return 3 + (int) many.size(); }
    FakeTool()
    {
        for (int i = 0; i < 12; ++i)
        {
            owned.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "p" + juce::String (i), 1 }, "P" + juce::String (i), 0.0f, 1.0f, 0.5f));
            many.push_back (owned.back().get());
        }
    }
};

struct AutoRig
{
    FakeTool tool;
    RigAutomation automation;
    RigAutomation::Clock clock;
    static constexpr int kBlock = 480;
    std::function<void (double beat)> hands;   // what you do before each block

    AutoRig()
    {
        automation.numTools = 1;
        automation.getParameter = [this] (int t, int p) { return t == 0 ? tool.at (p) : nullptr; };
        automation.numParameters = [this] (int t) { return t == 0 ? tool.size() : 0; };
        automation.toolName = [] (int) { return juce::String ("Bass"); };
        clock.sampleRate = 48000.0;
        clock.bpm = 120.0;
        clock.beatsPerBar = 4;
        clock.samples = kBlock;
    }

    void run (double beats)
    {
        const int blocks = (int) std::round (beats * 60.0 / clock.bpm * clock.sampleRate / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            if (hands)
                hands (clock.beat);
            automation.process (clock);
            if (clock.playing)
                clock.beat += kBlock * clock.bpm / (60.0 * clock.sampleRate);
            if (b % 20 == 0)
                automation.service();
        }
        automation.service();
    }
};

struct LooperRig
{
    RigLooper looper;
    RigLooper::Clock clock;
    juce::AudioBuffer<float> source { 2, 480 }, out { 2, 480 };
    LooperRig()
    {
        looper.prepare (48000.0);
        clock.bpm = 120.0;
        clock.beatsPerBar = 4;
    }
    void run (double beats)
    {
        const int blocks = (int) std::round (beats * 60.0 / clock.bpm * 48000.0 / 480);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < 480; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    source.setSample (ch, i, 0.25f);
            std::array<const juce::AudioBuffer<float>*, RigLooper::numSources> sources {};
            for (auto& s : sources)
                s = &source;
            if (looper.process (clock, sources, out))
                clock.playing = true;
            if (clock.playing)
                clock.beat += 480 * clock.bpm / (60.0 * 48000.0);
            if (b % 10 == 0)
                looper.maintain();
        }
    }
};
}

class RigAutomationTests : public juce::UnitTest
{
public:
    RigAutomationTests() : juce::UnitTest ("Rig: long takes and automation", "Rig") {}

    template <typename T>
    static T* find (juce::Component& c)
    {
        if (auto* t = dynamic_cast<T*> (&c))
            return t;
        for (auto* child : c.getChildren())
            if (auto* t = find<T> (*child))
                return t;
        return nullptr;
    }

    static void snapshot (juce::Component& view, const juce::String& name)
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_CAPTURE_SNAPSHOT", {});
        if (path.isEmpty())
            return;
        const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
        juce::File file (path + name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    void runTest() override
    {
        beginTest ("a take can be a whole long set: an hour per loop, the loops' total bounded");
        {
            expectGreaterOrEqual (RigLooper::kMaxRecordSeconds, 3600.0, "an hour on one loop");
            expectGreaterThan (RigLooper::kTotalRecordSeconds, RigLooper::kMaxRecordSeconds, "and more than one long take in all");
            LooperRig rig;
            expectWithinAbsoluteError (rig.looper.roomLeftSeconds (0), RigLooper::kMaxRecordSeconds, 1.0);
        }

        beginTest ("a take keeps going past the old 10-minute limit");
        {
            LooperRig rig;
            rig.clock.bpm = 240.0;   // (faster, so the test is quicker: the limit is in seconds)
            rig.looper.getLayer (0).bars = 0;
            rig.looper.pressRecord (0);
            rig.run (0.01);
            rig.run (240.0 * 11.0);   // 11 minutes at 240 BPM
            expect (rig.looper.getState (0) == RigLooper::State::recording, "still recording after 11 minutes");
            expectGreaterThan (rig.looper.recordedSeconds (0), 650.0);
            rig.looper.pressRecord (0);
            rig.run (8.0);
            expect (rig.looper.getState (0) == RigLooper::State::playing);
            expect (rig.looper.takeEnding (0) == RigLooper::Ending::asked, "closed because you asked");
            expectGreaterThan (rig.looper.getLayer (0).lengthBeats.load(), 240.0 * 11.0 - 1.0);
        }

        beginTest ("stopping the clock mid-take keeps it, to the end of that bar");
        {
            LooperRig rig;
            rig.looper.getLayer (2).bars = 0;
            rig.looper.getLayer (2).level = 1.0f;
            rig.looper.pressRecord (2);
            rig.run (0.01);
            rig.run (4.0 * 5 + 2.0);   // five bars and a half
            rig.clock.playing = false;
            rig.run (0.5);
            expect (rig.looper.getState (2) == RigLooper::State::playing, "the take was kept");
            expectWithinAbsoluteError (rig.looper.getLayer (2).lengthBeats.load(), 24.0, 1.0e-6, "six whole bars");
            expect (rig.looper.takeEnding (2) == RigLooper::Ending::clockStopped);
            const auto audio = rig.looper.copyLayerAudio (2);
            expectWithinAbsoluteError (audio.getSample (0, 48000 * 5), 0.25f, 0.01f, "what was played is there");
            expectWithinAbsoluteError (audio.getSample (0, audio.getNumSamples() - 100), 0.0f, 1.0e-6f, "silence after it, to the bar");
        }

        beginTest ("a loop of set bars that didn't get there is still called off when the clock stops");
        {
            LooperRig rig;
            rig.looper.getLayer (1).bars = 8;
            rig.looper.pressRecord (1);
            rig.run (0.01);
            rig.run (6.0);
            rig.clock.playing = false;
            rig.run (0.5);
            expect (rig.looper.getState (1) == RigLooper::State::empty);
        }

        beginTest ("a loop bank or song part switching never replaces a take that's recording");
        {
            LooperRig rig;
            rig.looper.getLayer (0).bars = 0;
            rig.looper.pressRecord (0);
            rig.run (0.01);
            rig.run (8.0);
            juce::AudioBuffer<float> other (2, 48000);
            other.clear();
            rig.looper.queueLayerAudio (0, std::move (other), 4.0, 12.0, 0.8f, false);
            rig.run (8.0);
            expect (rig.looper.getState (0) == RigLooper::State::recording, "still recording");
            expect (rig.looper.peekEnding (0) == RigLooper::Ending::kept);
            expect (! rig.looper.isSwapPending (0));
        }

        beginTest ("Rec moves records a knob you turn, from the next bar, and plays it back on the beat");
        {
            AutoRig rig;
            rig.clock.playing = true;
            rig.clock.beat = 1.5;
            rig.automation.pressRecord (false);
            expect (rig.automation.getRecState() == RigAutomation::RecState::armed);
            // A sweep: the cutoff goes from 0 to 1 over bars 2 and 3 (beats 4 to 12).
            rig.hands = [&rig] (double beat)
            {
                if (beat >= 4.0 && beat < 12.0)
                    rig.tool.cutoff.setValueNotifyingHost ((float) ((beat - 4.0) / 8.0));
            };
            rig.run (3.0);
            expect (rig.automation.getRecState() == RigAutomation::RecState::recording, "started on bar 2");
            rig.run (6.0);
            expectEquals (rig.automation.getNumRecording(), 1, "one knob moving");
            rig.automation.pressRecord (false);   // closes at the end of this bar (beat 12)
            rig.run (4.0);
            rig.hands = nullptr;
            expect (rig.automation.getRecState() == RigAutomation::RecState::off);
            expectEquals (rig.automation.getNumLanes(), 1);
            const auto lane = rig.automation.getLanes()[0];
            expectEquals (lane.parameter, 0, "the cutoff");
            expectEquals (lane.paramId, juce::String ("cutoff"));
            expectWithinAbsoluteError (lane.startBeat, 4.0, 1.0e-9);
            expectWithinAbsoluteError (lane.lengthBeats, 8.0, 1.0e-9, "two bars");
            expect (lane.loop);
            expectWithinAbsoluteError (lane.valueAt (4.0), 0.5f, 0.02f, "halfway through, halfway up");

            // Played back: round again, it sweeps by itself.
            rig.tool.cutoff.setValueNotifyingHost (0.9f);   // (your hand: the lane waits half a second)
            rig.run (1.5);
            rig.clock.beat = 18.0;   // a lane-relative beat of 6 (14..22 is its second time round... 4 + 8 + 6)
            rig.run (0.05);
            expectWithinAbsoluteError (rig.tool.cutoff.get(), 0.75f, 0.03f, "the cutoff follows the lane");
            expect (rig.automation.getLanes()[0].points.size() >= 3);
        }

        beginTest ("switches aren't recorded, and a new sound landing isn't a move");
        {
            AutoRig rig;
            rig.clock.playing = true;
            rig.automation.pressRecord (false);
            int block = 0;
            rig.hands = [&] (double)
            {
                ++block;
                if (block == 10)
                    rig.tool.wave.setValueNotifyingHost (1.0f);
                if (block == 20)
                    for (auto* p : rig.tool.many)
                        p->setValueNotifyingHost (0.9f);   // a preset: 12 settings at once
            };
            rig.run (4.0);
            rig.automation.pressRecord (false);
            rig.run (4.0);
            expectEquals (rig.automation.getNumLanes(), 0, "no lanes from a switch or a sound loading");
        }

        beginTest ("moving a knob that has a lane takes over while you move it, then the lane carries on");
        {
            AutoRig rig;
            RigAutomation::Lane lane;
            lane.tool = 0;
            lane.parameter = 1;   // mix
            lane.startBeat = 0;
            lane.lengthBeats = 4;
            lane.points = { { 0.0, 0.2f }, { 4.0, 0.2f } };
            rig.automation.addLane (lane);
            rig.clock.playing = true;
            rig.run (1.0);
            expectWithinAbsoluteError (rig.tool.mix.get(), 0.2f, 1.0e-4f);
            rig.tool.mix.setValueNotifyingHost (0.7f);   // your hand
            rig.run (0.25);
            expectWithinAbsoluteError (rig.tool.mix.get(), 0.7f, 1.0e-4f, "you have it");
            expect (rig.automation.isTouched (0));
            rig.run (2.0);   // a second later
            expectWithinAbsoluteError (rig.tool.mix.get(), 0.2f, 1.0e-4f, "the lane carries on");
        }

        beginTest ("Once lanes play once from where they were recorded; Read off moves nothing");
        {
            AutoRig rig;
            RigAutomation::Lane lane;
            lane.tool = 0;
            lane.parameter = 0;
            lane.startBeat = 16;
            lane.lengthBeats = 8;
            lane.loop = false;
            lane.points = { { 0.0, 0.0f }, { 8.0, 1.0f } };
            rig.automation.addLane (lane);
            rig.tool.cutoff.setValueNotifyingHost (0.5f);
            rig.clock.playing = true;
            rig.run (4.0);
            expectWithinAbsoluteError (rig.tool.cutoff.get(), 0.5f, 1.0e-4f, "before its start, nothing moves");
            rig.clock.beat = 20.0;
            rig.run (0.02);
            expectWithinAbsoluteError (rig.tool.cutoff.get(), 0.5f, 0.01f, "halfway");
            rig.clock.beat = 40.0;
            rig.tool.cutoff.setValueNotifyingHost (0.3f);
            rig.run (0.02);
            rig.run (2.0);
            expectWithinAbsoluteError (rig.tool.cutoff.get(), 0.3f, 1.0e-4f, "after its end it stays where you put it");
            rig.automation.reading = false;
            rig.clock.beat = 20.0;
            rig.run (0.1);
            expectWithinAbsoluteError (rig.tool.cutoff.get(), 0.3f, 1.0e-4f, "Read off");
        }

        beginTest ("drawing, length, clear and undo; saved with the show");
        {
            AutoRig rig;
            RigAutomation::Lane lane;
            lane.tool = 0;
            lane.parameter = 0;
            lane.lengthBeats = 16;
            lane.points = { { 0.0, 0.5f }, { 16.0, 0.5f } };
            rig.automation.addLane (lane);
            rig.automation.drawLine (0, 4.0, 0.0f, 8.0, 1.0f, true);
            auto l = rig.automation.getLanes()[0];
            expectWithinAbsoluteError (l.valueAt (6.0), 0.5f, 0.01f, "the line drawn");
            expectWithinAbsoluteError (l.valueAt (2.0), 0.5f, 0.01f, "before it, as it was");
            expectWithinAbsoluteError (l.valueAt (12.0), 0.5f, 0.01f, "after it, as it was");
            rig.automation.setLaneLength (0, 8.0);
            expectWithinAbsoluteError (rig.automation.getLanes()[0].lengthBeats, 8.0, 1.0e-9);
            rig.automation.setLaneLoop (0, false);

            RigAutomation copy;
            copy.numTools = 1;
            copy.getParameter = rig.automation.getParameter;
            copy.numParameters = rig.automation.numParameters;
            copy.fromTree (rig.automation.toTree());
            expectEquals (copy.getNumLanes(), 1, "saved and loaded");
            expect (! copy.getLanes()[0].loop);
            expectWithinAbsoluteError (copy.getLanes()[0].valueAt (6.0), 0.5f, 0.01f);
            expectEquals (copy.laneName (0), juce::String ("0 Cutoff"));

            rig.automation.clearAll();
            expectEquals (rig.automation.getNumLanes(), 0);
            rig.automation.undo();
            expectEquals (rig.automation.getNumLanes(), 1, "Undo brings them back");
            rig.automation.clearLane (0);
            expectEquals (rig.automation.getNumLanes(), 0);
        }

        beginTest ("in Kaleidobox: Rec moves records the bass's cutoff, the show keeps it, New song clears it");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            juce::AudioBuffer<float> out (2, 480);
            const auto runBlocks = [&] (int blocks, std::function<void (int)> each = nullptr)
            {
                for (int i = 0; i < blocks; ++i)
                {
                    if (each)
                        each (i);
                    engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
                    if (i % 10 == 0)
                        engine.getAutomation().service();
                }
                engine.getAutomation().service();
            };
            auto* cutoff = engine.getBass().params.getParameter ("cutoff");
            expect (cutoff != nullptr);
            engine.pressRecMoves();   // the clock was stopped: it starts
            runBlocks (5);
            expect (engine.getTransport().isPlaying());
            expect (engine.getAutomation().getRecState() == RigAutomation::RecState::recording);
            runBlocks (400, [&] (int i) { cutoff->setValueNotifyingHost ((float) i / 400.0f); });   // 4 seconds: 2 bars
            engine.pressRecMoves();
            runBlocks (300);
            expectEquals (engine.getAutomation().getNumLanes(), 1, "a lane for the cutoff");
            expectEquals (engine.getAutomation().laneName (0), juce::String ("Bass ") + cutoff->getName (40));

            // Export for Logic writes the moves as MIDI CCs (even with no loops).
            {
                const auto folder = juce::File::createTempFile ("automation-export");
                folder.createDirectory();
                const auto result = RigExport::exportForLogic (engine, folder);
                expect (result.error.isEmpty(), result.error);
                const auto file = result.folder.getChildFile ("Automation.mid");
                expect (file.existsAsFile(), "Automation.mid");
                juce::MidiFile midi;
                juce::FileInputStream in (file);
                expect (midi.readFrom (in));
                expectEquals (midi.getNumTracks(), 2, "the tempo and one lane");
                int ccs = 0, lastValue = -1;
                if (midi.getNumTracks() == 2)
                    for (const auto* e : *midi.getTrack (1))
                        if (e->message.isController() && e->message.getControllerNumber() == RigExport::ccForLane (0))
                        {
                            ++ccs;
                            lastValue = e->message.getControllerValue();
                        }
                expectGreaterThan (ccs, 50, "the sweep as CCs");
                expectGreaterThan (lastValue, 120, "up to the top, where the sweep ended");
                expect (result.folder.getChildFile ("Read me.txt").loadFileAsString().contains ("CC 102"));
                folder.deleteRecursively();
            }

            const auto show = engine.saveShow();
            RigEngine other;
            other.prepare (48000.0, 480);
            other.loadShow (show);
            expectEquals (other.getAutomation().getNumLanes(), 1, "the show keeps it");
            expect (! other.isBlank());
            other.newShow();
            expectEquals (other.getAutomation().getNumLanes(), 0, "New song clears it");
        }

        beginTest ("the bottom bar's Rec moves and Automation buttons do what they say");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");
            auto* rail = find<rigpanels::Dock> (view);
            expect (rail != nullptr);
            if (rail == nullptr)
                return;
            expect (rail->recMoves.isVisible() && rail->moves.isVisible());
            expect (! rail->recMoves.getBounds().isEmpty() && ! rail->moves.getBounds().isEmpty());
            rail->recMoves.onClick();
            expect (engine.getAutomation().getRecState() != RigAutomation::RecState::off, "Rec moves arms");
            rail->recMoves.onClick();
            expect (engine.getAutomation().getRecState() == RigAutomation::RecState::off, "pressed again before the bar: called off");
            expect (rail->moves.onClick != nullptr, "Automation opens the lanes");

            // The panel, with a lane in it.
            RigAutomation::Lane lane;
            lane.tool = RigEngine::bassTool;
            lane.parameter = engine.getBass().params.getParameter ("cutoff")->getParameterIndex();
            lane.lengthBeats = 8;
            lane.points = { { 0.0, 0.1f }, { 6.0, 0.9f }, { 8.0, 0.4f } };
            engine.getAutomation().addLane (lane);
            vintage::LookAndFeel look;
            AutomationPanel panel (engine);
            panel.setLookAndFeel (&look);
            expectEquals (panel.getNumRows(), 1);
            panel.pressLaneLoop (0);
            expect (! engine.getAutomation().getLanes()[0].loop, "Once");
            panel.pressLaneLength (0, true);
            expectWithinAbsoluteError (engine.getAutomation().getLanes()[0].lengthBeats, 16.0, 1.0e-9, "x2");
            panel.drawOnLane (0, 0.6f, 0.2f, 0.9f, 0.8f, true);
            panel.pressLaneOn (0);
            expect (! engine.getAutomation().getLanes()[0].on);
            panel.pressLaneOn (0);
            panel.pressRead();
            expect (! engine.getAutomation().reading.load());
            panel.pressRead();
            snapshot (panel, "automation-panel");
            panel.pressClearAll();
            expectEquals (engine.getAutomation().getNumLanes(), 0);
            panel.pressUndo();
            expectEquals (engine.getAutomation().getNumLanes(), 1);
            panel.pressLaneClear (0);
            expectEquals (engine.getAutomation().getNumLanes(), 0);
            panel.setLookAndFeel (nullptr);
            snapshot (view, "rail-rec-moves");
        }
    }
};

static RigAutomationTests rigAutomationTests;
