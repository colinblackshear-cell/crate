#include "TestHelpers.h"

#include <tuple>

#include "../tools/pocket/BeatsEngine.h"
#include "MpcProcessor.h"
#include "NoteRepeatControls.h"
#include "RepeatHold.h"
#include "RigEngine.h"
#include "RigRouting.h"

using namespace crate;

// NOTE REPEAT as a button you hold: the hold / double-tap latch state machine, the new rates, the Mac engine's
// hold, a learned keyboard button, and the phone's pad engine repeating held pads.
namespace
{
constexpr int kBlock = 512;

float runRig (RigEngine& engine, int blocks)
{
    juce::AudioBuffer<float> out (2, kBlock);
    float peak = 0;
    for (int b = 0; b < blocks; ++b)
    {
        engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, kBlock);
        peak = juce::jmax (peak, out.getMagnitude (0, 0, kBlock));
    }
    return peak;
}

// Plays a SequencerEngine stopped and collects the note ons (their sample positions).
struct StoppedRun
{
    explicit StoppedRun (SequencerEngine& e) : engine (e) {}

    void block (const juce::MidiBuffer& played = {})
    {
        juce::MidiBuffer out;
        std::array<SequencerEngine::Click, 16> clicks;
        int n = 0;
        engine.process (kBlock, nullptr, played, out, clicks, n);
        for (const auto m : out)
            if (m.getMessage().isNoteOn())
                hits.push_back (now + m.samplePosition);
        now += kBlock;
    }

    SequencerEngine& engine;
    std::vector<long long> hits;
    long long now = 0;
};
}

struct NoteRepeatHoldTests : juce::UnitTest
{
    NoteRepeatHoldTests() : juce::UnitTest ("Note repeat: hold and latch", "NoteRepeat") {}

    void runTest() override
    {
        beginTest ("hold: on while it's down, off when you let go");
        {
            noterepeat::Hold h;
            expect (! h.isOn());
            expect (h.press (1000.0));
            expect (h.isOn() && h.isHeld() && ! h.isLatched());
            expect (! h.release());
            expect (! h.isOn());
        }

        beginTest ("a double tap latches it on, one more press switches it off");
        {
            noterepeat::Hold h;
            h.press (1000.0);
            h.release();
            expect (h.press (1200.0), "the second press, 200 ms later");
            expect (h.isLatched());
            expect (h.release(), "letting go of a latched button keeps it on");
            expect (h.isOn() && h.isLatched());
            expect (! h.press (5000.0), "a single press while latched turns it off");
            expect (! h.isOn() && ! h.isLatched());
            h.release();
            expect (! h.isOn());
            h.press (5100.0);
            expect (! h.isLatched(), "right after switching off, a press is a first tap, not a double tap");
            h.release();
            expect (! h.isOn());
        }

        beginTest ("two slow presses don't latch");
        {
            noterepeat::Hold h;
            h.press (0.0);
            h.release();
            h.press (noterepeat::kDoubleTapMs + 50.0);
            expect (! h.isLatched() && h.isOn());
            h.release();
            expect (! h.isOn());
        }

        beginTest ("rates: the eight buttons and what they mean");
        {
            expectEquals (seq::gridTicks (seq::Grid::quarter), 96);
            expectEquals (seq::gridTicks (seq::Grid::sixtyFourth), 6);
            expectEquals (seq::gridName (seq::Grid::quarter), juce::String ("1/4"));
            expectEquals (seq::gridName (seq::Grid::sixtyFourth), juce::String ("1/64"));
            expectEquals (seq::gridShortName (seq::Grid::eighthTriplet), juce::String ("1/8T"));
            expectEquals (seq::gridShortName (seq::Grid::thirtySecond), juce::String ("1/32"));
            expect (! seq::isTimingGrid (seq::Grid::quarter) && ! seq::isTimingGrid (seq::Grid::sixtyFourth));
            expect (seq::isTimingGrid (seq::Grid::thirtySecondTriplet) && seq::isTimingGrid (seq::Grid::off));
            expect (! seq::gridSwings (seq::Grid::quarter) && ! seq::gridSwings (seq::Grid::sixtyFourth));
            expectEquals ((int) seq::Grid::thirtySecondTriplet, 6, "stored numbers didn't move");
            for (int i = 0; i < noterepeat::kNumRates; ++i)
            {
                const auto g = SequencerEngine::rateGrid (i);
                expectEquals (SequencerEngine::rateIndex (g), i);
                expectEquals (seq::gridShortName (g), juce::String (noterepeat::kRates[i].name));
                expectWithinAbsoluteError (seq::gridTicks (g) / (double) seq::kTicksPerBeat, noterepeat::kRates[i].beats, 1.0e-9);
            }
            expectEquals (SequencerEngine::rateIndex (seq::Grid::off), -1);
        }
    }
};

struct NoteRepeatEngineTests : juce::UnitTest
{
    NoteRepeatEngineTests() : juce::UnitTest ("Note repeat: Mac engine", "NoteRepeat") {}

    void runTest() override
    {
        beginTest ("the engine's hold button state, and the screen's button reads it");
        {
            SequencerEngine engine;
            expect (! engine.isRepeatOn());
            engine.repeatPress (0.0);
            expect (engine.noteRepeat.load() && engine.isRepeatOn());
            engine.repeatRelease();
            expect (! engine.noteRepeat.load());
            engine.repeatPress (1000.0);
            engine.repeatRelease();
            engine.repeatPress (1100.0);
            engine.repeatRelease();
            expect (engine.isRepeatLatched() && engine.noteRepeat.load(), "double tap latches");
            engine.repeatPress (9000.0);
            expect (! engine.noteRepeat.load() && ! engine.isRepeatLatched(), "a press while latched switches it off");
            engine.repeatRelease();

            noterepeatui::HoldButton button (engine);
            noterepeatui::RateButtons rates (engine);
            button.press (20000.0);
            expect (engine.isRepeatOn());
            button.release();
            expect (! engine.isRepeatOn());
            rates.button (7).onClick();
            expectEquals (engine.getRepeatRateIndex(), 7);
            expectEquals (engine.getRepeatGrid() == seq::Grid::sixtyFourth ? 1 : 0, 1);
            expect (rates.button (7).getToggleState() && ! rates.button (3).getToggleState());
            engine.stepRepeatRate();
            expectEquals (engine.getRepeatRateIndex(), 0, "the rate steps round from 1/64 to 1/4");
            engine.midiRepeat = true;
            expect (engine.isRepeatOn(), "a keyboard button holds it too, beside the screen's");
        }

        beginTest ("held pads repeat at 1/4 and at 1/64 while stopped");
        for (auto [grid, every, count] : { std::tuple<seq::Grid, int, int> { seq::Grid::quarter, 24000, 2 },
                                           { seq::Grid::sixtyFourth, 1500, 10 } })
        {
            SequencerEngine engine;
            engine.prepare (48000.0);
            seq::Sequence s;
            s.bpm = 120.0;
            engine.setSnapshot (SequencerEngine::makeSnapshot (s));
            engine.repeatGrid = (int) grid;
            engine.repeatPress (0.0);
            StoppedRun run { engine };
            juce::MidiBuffer down;
            down.addEvent (juce::MidiMessage::noteOn (1, noteForPad (2), (juce::uint8) 100), 100);
            run.block (down);
            while (run.now < 100 + (long long) every * count + every / 2)
                run.block();
            expectEquals ((int) run.hits.size(), count, seq::gridName (grid));
            for (size_t i = 0; i < run.hits.size(); ++i)
                expect (std::abs (run.hits[i] - (100 + (long long) every * (long long) (i + 1))) <= 1, juce::String (run.hits[i]));
            engine.repeatRelease();
            juce::MidiBuffer up;
            up.addEvent (juce::MidiMessage::noteOff (1, noteForPad (2)), 0);
            run.block (up);
            for (int i = 0; i < 100; ++i)
                run.block();
            expectEquals ((int) run.hits.size(), count, "stops when you let go");
        }
    }
};

struct NoteRepeatKeyboardTests : juce::UnitTest
{
    NoteRepeatKeyboardTests() : juce::UnitTest ("Note repeat: learned keyboard button", "NoteRepeat") {}

    void runTest() override
    {
        beginTest ("the controls are new at the end, so saved shows keep their numbers");
        expect (RigRouting::noteRepeatHold > RigRouting::liveKeyHome && RigRouting::noteRepeatRate == RigRouting::noteRepeatHold + 1);
        // Later controls go after them (Chords on/off), so these keep their numbers too.
        expectEquals ((int) RigRouting::chordsOnOff, (int) RigRouting::noteRepeatRate + 1);
        expectEquals ((int) RigRouting::numControls, (int) RigRouting::chordsOnOff + 1);
        expect (RigRouting::controlName (RigRouting::noteRepeatHold).isNotEmpty() && RigRouting::controlName (RigRouting::noteRepeatRate).isNotEmpty());
        expect (RigRouting::isHold (RigRouting::noteRepeatHold) && ! RigRouting::isHold (RigRouting::noteRepeatRate));

        beginTest ("a held button: down on, up off (the sustain pedal, CC 64, learns like any CC)");
        {
            RigRouting r;
            std::array<juce::MidiBuffer, RigRouting::kNumDests> buffers;
            std::array<juce::MidiBuffer*, RigRouting::kNumDests> to {};
            for (int d = 1; d < RigRouting::kNumDests; ++d)
                to[(size_t) d] = &buffers[(size_t) d];
            RigRouting::Result result;
            auto send = [&] (int cc, int value)
            {
                juce::MidiBuffer in;
                in.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), 0);
                r.route (in, to, result);
            };
            r.learn (RigRouting::noteRepeatHold);
            send (64, 127);
            expect (r.getAssignment (RigRouting::noteRepeatHold) == std::pair<int, int> { 1, 64 });
            send (64, 0);
            expectEquals ((int) result.holds[RigRouting::noteRepeatHold], 0);
            send (64, 127);
            expectEquals ((int) result.holds[RigRouting::noteRepeatHold], 1);
            send (64, 127);
            expectEquals ((int) result.holds[RigRouting::noteRepeatHold], -1, "still down: nothing new");
            send (64, 63);
            expectEquals ((int) result.holds[RigRouting::noteRepeatHold], 0, "below the halfway mark counts as up");
            send (64, 100);
            expectEquals ((int) result.holds[RigRouting::noteRepeatHold], 1);
        }

        beginTest ("in the engine: the learned button turns repeat on while it's down, off when it lets go");
        {
            RigEngine engine;
            engine.prepare (48000.0, kBlock);
            auto& sequencer = engine.getMpc().getSequencer();
            engine.getRouting().learn (RigRouting::noteRepeatHold);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 64, 127));
            runRig (engine, 2);
            expect (engine.getRouting().getAssignment (RigRouting::noteRepeatHold).second == 64);
            expect (! sequencer.isRepeatOn(), "learning isn't a press");
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 64, 0));
            runRig (engine, 2);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 64, 127));
            runRig (engine, 2);
            expect (sequencer.isRepeatOn(), "down: on");
            expect (! sequencer.noteRepeat.load(), "(the screen's own button isn't what turned it on)");
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 64, 0));
            runRig (engine, 2);
            expect (! sequencer.isRepeatOn(), "up: off");

            beginTest ("the rate button steps through the rates");
            engine.getRouting().learn (RigRouting::noteRepeatRate);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 20, 127));
            runRig (engine, 2);
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 20, 0));
            runRig (engine, 2);
            const int before = sequencer.getRepeatRateIndex();
            engine.addMidiNow (juce::MidiMessage::controllerEvent (1, 20, 127));
            runRig (engine, 2);
            expectEquals (sequencer.getRepeatRateIndex(), (before + 1) % noterepeat::kNumRates);
        }
    }
};

struct NoteRepeatPhoneTests : juce::UnitTest
{
    NoteRepeatPhoneTests() : juce::UnitTest ("Note repeat: phone pads", "NoteRepeat") {}

    static constexpr int kStep = 480;

    static void render (pocket::BeatsEngine& e, int samples)
    {
        std::vector<float> l ((size_t) kStep), r ((size_t) kStep);
        for (int done = 0; done < samples; done += kStep)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.render (l.data(), r.data(), juce::jmin (kStep, samples - done));
        }
    }

    static void setup (pocket::BeatsEngine& e)
    {
        e.prepare (48000.0);
        e.setBpm (120.0);   // a beat is 24000 samples
        pocket::PadSetup pad;
        pad.audio = share (sine (48000, 1, 0.5f));
        e.setPad (0, pad);
        e.setPad (1, pad);
    }

    void runTest() override
    {
        beginTest ("stopped: a held pad repeats at 1/16 from the press, and stops on release");
        {
            pocket::BeatsEngine e;
            setup (e);
            e.setRepeatBeats (noterepeat::kRates[3].beats);   // 1/16 = 6000 samples
            e.setNoteRepeat (true);
            e.padOn (0);
            render (e, 6000 * 3 + 3000);
            expectEquals (e.repeatsPlayed(), 3);
            e.padOff (0);
            render (e, 30000);
            expectEquals (e.repeatsPlayed(), 3, "nothing after the finger lifts");
        }

        beginTest ("repeat off: a held pad just plays once");
        {
            pocket::BeatsEngine e;
            setup (e);
            e.padOn (0);
            render (e, 30000);
            expectEquals (e.repeatsPlayed(), 0);
            e.setNoteRepeat (true);
            render (e, 12000);
            expect (e.repeatsPlayed() > 0, "switching it on while a pad is held starts the repeats");
        }

        beginTest ("clock running: repeats fall on the clock's 1/16 grid, the press is the first hit");
        {
            pocket::BeatsEngine e;
            setup (e);
            e.setRepeatBeats (0.25);
            e.setNoteRepeat (true);
            e.holdClock (true);
            render (e, kStep);
            expect (e.clockRunning());
            e.padOn (0);
            render (e, 48000);   // two beats
            pocket::BeatsEngine::RepeatReport reports[32];
            const int n = e.takeRepeats (reports, 32);
            expectEquals (e.repeatsPlayed(), 8);
            expectEquals (n, 8);
            for (int i = 0; i < n; ++i)
            {
                const double grid = reports[i].beat / 0.25;
                expect (std::abs (grid - std::round (grid)) < 0.02, "on the grid: " + juce::String (reports[i].beat));
                expectEquals (reports[i].pad, 0);
            }
            e.padOff (0);
            render (e, 24000);
            expectEquals (e.repeatsPlayed(), 8, "stops on release");
        }

        beginTest ("clock running: a press just before a grid line waits for the next one");
        {
            pocket::BeatsEngine e;
            setup (e);
            e.setRepeatBeats (0.25);
            e.setNoteRepeat (true);
            e.holdClock (true);
            render (e, 5760);   // 0.24 beat: a hair before the 0.25 line
            e.padOn (1);
            render (e, 2 * kStep);
            expectEquals (e.repeatsPlayed(), 0, "no double hit on the press");
            render (e, 12000);
            pocket::BeatsEngine::RepeatReport reports[8];
            const int n = e.takeRepeats (reports, 8);
            expect (n >= 1 && std::abs (reports[0].beat - 0.5) < 0.02, "the first repeat is on 0.5");
        }

        beginTest ("1/64 and a second pad held at once");
        {
            pocket::BeatsEngine e;
            setup (e);
            e.setRepeatBeats (noterepeat::kRates[7].beats);   // 1/64 = 1500 samples
            e.setNoteRepeat (true);
            e.padOn (0);
            e.padOn (1);
            render (e, 1500 * 4 + 700);
            expectEquals (e.repeatsPlayed(), 8, "both pads, four repeats each");
            e.padOff (0);
            render (e, 1500 * 4);
            expectEquals (e.repeatsPlayed(), 12, "only the pad still held");
            e.stopPads();
            render (e, 12000);
            expectEquals (e.repeatsPlayed(), 12);
        }
    }
};

static NoteRepeatHoldTests noteRepeatHoldTests;
static NoteRepeatEngineTests noteRepeatEngineTests;
static NoteRepeatKeyboardTests noteRepeatKeyboardTests;
static NoteRepeatPhoneTests noteRepeatPhoneTests;
