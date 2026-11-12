#include "TestHelpers.h"

#include "MpcProcessor.h"
#include "MpcView.h"
#include "RigEngine.h"
#include "RigRouting.h"
#include "RigView.h"
#include "SamplerScreen.h"
#include "SixteenLevels.h"
#include "core/AudioFiles.h"
#include "core/control/Launchpad.h"

using namespace crate;

namespace
{
constexpr int kBlock = 512;

// Plays the processor for `blocks` blocks, the first with `midi`, and returns the audio.
juce::AudioBuffer<float> run (MpcProcessor& p, int blocks, juce::MidiBuffer midi = {})
{
    juce::AudioBuffer<float> all (2, blocks * kBlock), out (2, kBlock);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer m;
        if (b == 0)
            m = midi;
        p.processBlock (out, m);
        for (int c = 0; c < 2; ++c)
            all.copyFrom (c, b * kBlock, out, c, 0, kBlock);
    }
    return all;
}

juce::MidiBuffer hit (int pad, float velocity = 1.0f)
{
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, noteForPad (pad), velocity), 0);
    return m;
}

// Rising zero crossings a second, from the left channel.
double frequency (const juce::AudioBuffer<float>& b, int from, int length)
{
    int crossings = 0;
    for (int i = from + 1; i < from + length; ++i)
        if (b.getSample (0, i - 1) <= 0.0f && b.getSample (0, i) > 0.0f)
            ++crossings;
    return crossings * 44100.0 / length;
}

float peak (const juce::AudioBuffer<float>& b, int from, int length) { return b.getMagnitude (0, from, length); }

struct Fixture
{
    MpcProcessor processor;
    Fixture()
    {
        processor.setPlayConfigDetails (0, 2, 44100.0, kBlock);
        processor.prepareToPlay (44100.0, kBlock);
    }
    Pad& pad (int i) { return processor.getProject().currentBank().pads[(size_t) i]; }
    void changed()
    {
        processor.getProject().changed();
        processor.getProject().dispatchPendingMessages();
    }
    void stop()
    {
        processor.stopAll();
        run (processor, 8);
    }
};

struct SixteenLevelsTests : juce::UnitTest
{
    SixteenLevelsTests() : juce::UnitTest ("16 Levels", "MPC") {}

    void runTest() override
    {
        beginTest ("the steps");
        expect (levels::semitones (levels::tune, 0) == 0.0 && levels::semitones (levels::tune, 15) == 15.0);
        expect (levels::cutoff (levels::filter, 0) < 200.0f && levels::cutoff (levels::filter, 15) > 15000.0f);
        expect (levels::cutoff (levels::tune, 3) == 0.0f, "tune doesn't filter");
        expect (levels::velocityFor (0) < 10 && levels::velocityFor (15) == 127);
        expect (levels::decaySeconds (levels::decay, 0) < 0.05 && levels::decaySeconds (levels::decay, 15) == 0.0);
        for (int t = levels::tune; t < levels::numTypes; ++t)
            for (int s = 0; s < 16; ++s)
                if (const int v = levels::pack (t, s); v != 0)
                    expect (levels::typeOf (v) == t && levels::stepOf (v) == s && v <= 127);
        expectEquals (levels::pack (levels::velocity, 4), 0, "velocity is the hit's own velocity");
        expectEquals (levels::stepText (levels::tune, 7), juce::String ("+7 st"));

        Fixture f;
        f.pad (0).audio = share (sine (44100, 1, 0.5f, 441.0));
        f.pad (0).name = "tone";
        f.changed();
        auto& p = f.processor;
        p.levelsPad = 0;
        p.padBankOnScreen = 0;

        beginTest ("Tune: the pads play the sound a semitone apart");
        p.levelsType = levels::tune;
        auto low = run (p, 20, hit (0));
        f.stop();
        auto high = run (p, 20, hit (12));   // pad 13: an octave up
        f.stop();
        expect (std::abs (frequency (low, 1000, 8000) - 441.0) < 10.0, juce::String (frequency (low, 1000, 8000)));
        expect (std::abs (frequency (high, 1000, 8000) - 882.0) < 15.0, juce::String (frequency (high, 1000, 8000)));
        expect ((p.takePlayedPads() & ((juce::uint64) 1 << 12)) != 0, "the pad you hit lights");

        beginTest ("pads in other banks play their own sounds");
        f.pad (20).audio = share (sine (44100, 1, 0.5f, 441.0));
        f.changed();
        auto other = run (p, 20, hit (20));
        f.stop();
        expect (std::abs (frequency (other, 1000, 8000) - 441.0) < 10.0);

        beginTest ("Velocity: soft on pad 1, full on pad 16");
        p.levelsType = levels::velocity;
        auto soft = run (p, 8, hit (0));
        f.stop();
        auto loud = run (p, 8, hit (15));
        f.stop();
        expect (peak (soft, 0, 4000) < 0.1f && peak (loud, 0, 4000) > 0.4f);

        beginTest ("Filter: dark on pad 1, open on pad 16");
        f.pad (0).audio = share (sine (44100, 1, 0.5f, 5000.0));
        f.changed();
        p.levelsType = levels::filter;
        auto dark = run (p, 20, hit (0));
        f.stop();
        auto open = run (p, 20, hit (15));
        f.stop();
        expect (peak (dark, 2000, 6000) < 0.05f && peak (open, 2000, 6000) > 0.35f,
                juce::String (peak (dark, 2000, 6000)) + " " + juce::String (peak (open, 2000, 6000)));

        beginTest ("Decay: short on pad 1, the whole sound on pad 16");
        p.levelsType = levels::decay;
        f.pad (0).audio = share (sine (44100, 1, 0.5f, 441.0));
        f.changed();
        auto shortHit = run (p, 20, hit (0));
        f.stop();
        auto whole = run (p, 20, hit (15));
        f.stop();
        expect (peak (shortHit, 4410, 4000) < 1.0e-4f && peak (whole, 4410, 4000) > 0.4f);

        beginTest ("letting go stops the hit even after 16 Levels is turned off");
        p.levelsType = levels::tune;
        f.pad (0).mode = PadMode::noteOff;
        f.changed();
        run (p, 4, hit (5));
        p.levelsType = levels::off;
        juce::MidiBuffer up;
        up.addEvent (juce::MidiMessage::noteOff (1, noteForPad (5)), 0);
        auto after = run (p, 6, up);
        expect (peak (after, 3 * kBlock, 2 * kBlock) < 1.0e-6f);
        f.pad (0).mode = PadMode::oneShot;
        f.changed();

        beginTest ("hits recorded with 16 Levels keep their level, and play back with it");
        {
            SequencerEngine engine;
            engine.prepare (44100.0);
            seq::Sequence s;
            s.bpm = 120.0;
            s.bars = 1;
            engine.countIn = false;
            engine.clickMode = (int) SequencerEngine::ClickMode::off;
            engine.setSnapshot (SequencerEngine::makeSnapshot (s));
            engine.record();
            juce::MidiBuffer out;
            std::array<SequencerEngine::Click, 16> clicks;
            int n = 0;
            engine.process (kBlock, nullptr, {}, out, clicks, n);
            juce::MidiBuffer played;
            played.addEvent (levels::message (noteForPad (3), levels::pack (levels::tune, 7)), 10);
            played.addEvent (juce::MidiMessage::noteOn (1, noteForPad (3), (juce::uint8) 100), 10);
            engine.process (kBlock, nullptr, played, out, clicks, n);
            juce::MidiBuffer released;
            released.addEvent (juce::MidiMessage::noteOff (1, noteForPad (3)), 0);
            engine.process (kBlock, nullptr, released, out, clicks, n);
            std::array<SequencerEngine::Recorded, 8> got;
            const int count = engine.takeRecorded (got.data(), 8);
            expectEquals (count, 1);
            expectEquals (got[0].pad, 3);
            expectEquals (got[0].variation, levels::pack (levels::tune, 7));

            s.tracks[0].add ({ 0, 3, 100, 24, levels::pack (levels::filter, 2) });
            SequencerEngine player;
            player.prepare (44100.0);
            player.clickMode = (int) SequencerEngine::ClickMode::off;
            player.setSnapshot (SequencerEngine::makeSnapshot (s));
            player.play (true);
            juce::MidiBuffer playedBack;
            player.process (kBlock, nullptr, {}, playedBack, clicks, n);
            int variationAt = -1, noteAt = -1, value = -1;
            for (const auto m : playedBack)
            {
                if (levels::isVariation (m.getMessage()))
                {
                    variationAt = m.samplePosition;
                    value = m.getMessage().getAfterTouchValue();
                }
                else if (m.getMessage().isNoteOn() && noteAt < 0)
                    noteAt = m.samplePosition;
            }
            expect (variationAt == 0 && noteAt == 0, "the level comes just before its note");
            expectEquals (value, levels::pack (levels::filter, 2));
        }

        beginTest ("sequences save the levels");
        {
            seq::Sequence s;
            s.tracks[0].add ({ 0, 3, 100, 24, levels::pack (levels::tune, 9) });
            s.tracks[0].add ({ 96, 4, 90, 24 });
            const auto back = seq::Sequence::fromValueTree (s.toValueTree());
            expect (back.tracks[0].notes == s.tracks[0].notes);
            seq::Sequence plain;
            plain.tracks[0].add ({ 0, 3, 100, 24 });
            expect (! plain.toValueTree().getChild (0).hasProperty ("variations"), "plain tracks save as before");
        }

        beginTest ("the processor plays a sequenced level");
        {
            auto& sq = p.getProject().currentSequence();
            sq.tracks[0].add ({ 0, 0, 127, 24, levels::pack (levels::tune, 12) });
            sq.bpm = 60.0;
            p.levelsType = levels::off;
            p.getProject().sequenceChanged();
            p.getProject().dispatchPendingMessages();
            p.getProject().sequenceEdits.dispatchPendingMessages();
            p.getSequencer().clickMode = (int) SequencerEngine::ClickMode::off;
            p.getSequencer().play (true);
            auto played = run (p, 20);
            p.stopTransport();
            f.stop();
            expect (std::abs (frequency (played, 1000, 8000) - 882.0) < 15.0, juce::String (frequency (played, 1000, 8000)));
            sq.tracks[0].notes.clear();
        }
    }
};

struct NoteRepeatTests : juce::UnitTest
{
    NoteRepeatTests() : juce::UnitTest ("Note repeat, stopped", "MPC") {}

    void runTest() override
    {
        beginTest ("held pads repeat at their own rate while stopped");
        SequencerEngine engine;
        engine.prepare (48000.0);
        seq::Sequence s;
        s.bpm = 120.0;
        engine.setSnapshot (SequencerEngine::makeSnapshot (s));
        engine.noteRepeat = true;
        engine.grid = (int) seq::Grid::eighth;
        engine.repeatGrid = (int) seq::Grid::sixteenth;   // 1/16 at 120 = 6000 samples
        expect (engine.getRepeatGrid() == seq::Grid::sixteenth);

        std::vector<long long> hits;
        long long now = 0;
        auto block = [&] (const juce::MidiBuffer& played)
        {
            juce::MidiBuffer out;
            std::array<SequencerEngine::Click, 16> clicks;
            int n = 0;
            engine.process (kBlock, nullptr, played, out, clicks, n);
            for (const auto m : out)
                if (m.getMessage().isNoteOn())
                    hits.push_back (now + m.samplePosition);
            now += kBlock;
        };
        juce::MidiBuffer down;
        down.addEvent (juce::MidiMessage::noteOn (1, noteForPad (2), (juce::uint8) 100), 100);
        block (down);
        while (now < 100 + 6000 * 3 + 3000)
            block ({});
        expectEquals ((int) hits.size(), 3, "a repeat every 1/16 after the press");
        if (hits.size() == 3)
            for (int i = 0; i < 3; ++i)
                expect (std::abs (hits[(size_t) i] - (100 + 6000 * (i + 1))) <= 1, juce::String (hits[(size_t) i]));
        juce::MidiBuffer up;
        up.addEvent (juce::MidiMessage::noteOff (1, noteForPad (2)), 0);
        block (up);
        for (int i = 0; i < 40; ++i)
            block ({});
        expectEquals ((int) hits.size(), 3, "stops when you let go");

        beginTest ("repeat off: nothing while stopped");
        engine.noteRepeat = false;
        block (down);
        for (int i = 0; i < 40; ++i)
            block ({});
        expectEquals ((int) hits.size(), 3);
    }
};

struct PadSoundTests : juce::UnitTest
{
    PadSoundTests() : juce::UnitTest ("Pad live sound", "MPC") {}

    void runTest() override
    {
        Fixture f;
        auto& p = f.processor;
        f.pad (0).audio = share (sine (44100, 1, 0.5f, 5000.0));
        f.changed();

        beginTest ("the filter darkens the pad as it plays");
        auto plain = run (p, 10, hit (0));
        f.stop();
        f.pad (0).cutoff = 300.0f;
        f.changed();
        auto dark = run (p, 10, hit (0));
        f.stop();
        expect (peak (plain, 1000, 3000) > 0.4f && peak (dark, 1000, 3000) < 0.05f);

        beginTest ("attack fades it in, decay makes it short");
        f.pad (0).cutoff = 20000.0f;
        f.pad (0).attack = 100;
        f.changed();
        auto fadeIn = run (p, 20, hit (0));
        f.stop();
        expect (peak (fadeIn, 0, 400) < 0.05f && peak (fadeIn, 5000, 2000) > 0.4f);
        f.pad (0).attack = 0;
        f.pad (0).decay = 50;
        f.changed();
        auto shortHit = run (p, 20, hit (0));
        f.stop();
        expect (peak (shortHit, 0, 300) > 0.3f && peak (shortHit, 2400, 4000) < 1.0e-4f);

        beginTest ("delay: the hit comes back after the echo time");
        f.pad (0).decay = 20;
        f.pad (0).delaySend = 100;
        p.getProject().currentBank().delayBeats = 0.5;   // an 1/8 at 90 BPM = 1/3 s
        p.getProject().currentBank().delayFeedback = 0;
        f.changed();
        auto echo = run (p, 60, hit (0));
        const int echoAt = (int) (0.5 * 60.0 / 90.0 * 44100.0);
        expect (peak (echo, 3000, echoAt - 4000) < 1.0e-3f, "quiet between the hit and its echo");
        expect (peak (echo, echoAt, 800) > 0.2f, "then the echo");
        f.pad (0).delaySend = 0;
        f.changed();
        run (p, 400);

        beginTest ("reverb: a tail after a short hit");
        f.pad (0).reverbSend = 100;
        f.changed();
        auto room = run (p, 60, hit (0));
        expect (peak (room, 8000, 4000) > 1.0e-3f);
        f.pad (0).reverbSend = 0;
        f.changed();

        beginTest ("the live sound is saved with the kit");
        auto& pad = f.pad (0);
        pad.cutoff = 1200.0f;
        pad.resonance = 0.4f;
        pad.attack = 15;
        pad.decay = 300;
        pad.delaySend = 25;
        pad.reverbSend = 60;
        p.getProject().currentBank().delayBeats = 0.75;
        p.getProject().currentBank().delayFeedback = 55;
        p.getProject().currentBank().reverbSize = 80;
        juce::MemoryBlock state;
        p.getStateInformation (state);
        MpcProcessor other;
        other.setStateInformation (state.getData(), (int) state.getSize());
        const auto& b = other.getProject().currentBank();
        const auto& back = b.pads[0];
        expect (std::abs (back.cutoff - 1200.0f) < 0.5f && std::abs (back.resonance - 0.4f) < 0.001f);
        expect (back.attack == 15 && back.decay == 300 && back.delaySend == 25 && back.reverbSend == 60);
        expect (std::abs (b.delayBeats - 0.75) < 1.0e-9 && b.delayFeedback == 55 && b.reverbSize == 80);
    }
};

struct PadControllerTests : juce::UnitTest
{
    PadControllerTests() : juce::UnitTest ("Launchpad and Launchkey pads", "MPC") {}

    void runTest() override
    {
        beginTest ("a Launchpad's four blocks are the bank on screen and the three after it");
        RigRouting routing;
        routing.padBank = 0;
        expectEquals (routing.mpcNoteForPad (36), noteForPad (0));
        expectEquals (routing.mpcNoteForPad (51), noteForPad (15));
        expectEquals (routing.mpcNoteForPad (52), noteForPad (16));
        expectEquals (routing.mpcNoteForPad (99), noteForPad (63));
        routing.padBank = 2;
        expectEquals (routing.mpcNoteForPad (36), noteForPad (32), "the Launchkey's pads play the bank picked");
        expectEquals (routing.mpcNoteForPad (68), noteForPad (0), "the top-left block wraps round to A");
        expectEquals (routing.mpcNoteForPad (35), -1);
        expectEquals (routing.mpcNoteForPad (100), -1);
        expect (crate::launchpad::isLaunchpad ("Launchpad X LPX MIDI") && ! crate::launchpad::isLaunchpad ("Launchkey 61 MK3"));
        expect (crate::launchpad::isPadMessage (juce::MidiMessage::noteOn (1, 99, 1.0f)));
        expect (! crate::launchpad::isPadMessage (juce::MidiMessage::noteOn (1, 30, 1.0f)));
    }
};

struct MpcPadToolsViewTests : juce::UnitTest
{
    MpcPadToolsViewTests() : juce::UnitTest ("MPC pad tools and sound bank", "MPC") {}

    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("crate-mpc-bankbrowser");
        dir.deleteRecursively();
        auto sounds = dir.getChildFile ("Sounds");
        sounds.createDirectory();
        expect (AudioFiles::write (sounds.getChildFile ("Dusty_Kick.wav"), sine (8000, 1, 0.5f)));
        expect (AudioFiles::write (sounds.getChildFile ("Soul_Keys_Loop_90.wav"), sine (44100 * 8 / 3, 2, 0.3f)));

        MpcProcessor processor;
        processor.setPlayConfigDetails (0, 2, 44100.0, kBlock);
        processor.prepareToPlay (44100.0, kBlock);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
        auto* view = dynamic_cast<MpcView*> (editor.get());
        expect (view != nullptr);
        if (view == nullptr)
            return;

        beginTest ("16 Levels needs a pad with a sound");
        view->setSixteenLevels (levels::tune);
        expectEquals (processor.levelsType.load(), (int) levels::off);

        beginTest ("the sound bank under the pads puts sounds on the picked pad");
        auto bank = std::make_shared<SoundBank> (dir.getChildFile ("Home"));
        for (auto& folder : bank->getFolders())
            bank->removeFolder (folder);
        bank->addFolder (sounds);
        for (int i = 0; i < 200 && (bank->isScanning() || bank->size() < 2); ++i)
            juce::Thread::sleep (20);
        expectEquals (bank->size(), 2);
        view->setSoundBank (bank);
        auto& browser = view->getBrowser();
        expectEquals (browser.getNumShown(), 2);
        browser.setSearch ("kick");
        expectEquals (browser.getNumShown(), 1);
        browser.selectRow (0);
        expect (browser.putSelectedOnPad());
        auto& pads = processor.getProject().currentBank().pads;
        expect (! pads[0].isEmpty() && pads[0].name == "Dusty_Kick");

        browser.setSearch ("keys");
        browser.selectRow (0);
        browser.targetPad = [] { return 5; };
        expect (browser.putSelectedOnPad());
        expect (! pads[5].isEmpty());
        expect (std::abs (pads[5].beats - 4.0) < 1.0e-9, "a loop with its tempo in the name gets its length in beats");
        browser.targetPad = [view] { return view->getSelectedPad(); };

        beginTest ("16 Levels and note repeat from the pad tools");
        view->setSixteenLevels (levels::filter);
        expectEquals (processor.levelsType.load(), (int) levels::filter);
        expectEquals (processor.levelsPad.load(), view->getSelectedPad(), "the picked pad's sound");
        expectEquals (view->getSelectedPad(), 5, "a sound put on a pad picks it");
        view->setSixteenLevels (levels::off);
        expectEquals (processor.levelsType.load(), (int) levels::off);
        view->setNoteRepeat (true, (int) seq::Grid::thirtySecond);
        expect (processor.getSequencer().noteRepeat.load());
        expect (processor.getSequencer().getRepeatGrid() == seq::Grid::thirtySecond);
        view->setNoteRepeat (false);

        if (auto* path = std::getenv ("CRATE_MPC_SNAPSHOT"))
        {
            view->setSixteenLevels (levels::tune);
            browser.setSearch ({});
            view->setSize (1280, 860);
            auto image = view->createComponentSnapshot (view->getLocalBounds());
            juce::FileOutputStream stream { juce::File { juce::String { path } }.getSiblingFile ("mpc-pad-tools.png") };
            stream.setPosition (0);
            stream.truncate();
            juce::PNGImageFormat().writeImageToStream (image, stream);
            view->setSixteenLevels (levels::off);
        }
        editor.reset();
        dir.deleteRecursively();
    }
};

struct RigPadsFollowTests : juce::UnitTest
{
    RigPadsFollowTests() : juce::UnitTest ("Rig: keyboard pads follow the MPC", "MPC") {}

    template <typename T>
    static T* find (juce::Component& c, std::function<bool (T&)> wanted = {})
    {
        if (auto* t = dynamic_cast<T*> (&c); t != nullptr && (! wanted || wanted (*t)))
            return t;
        for (auto* child : c.getChildren())
            if (auto* t = find<T> (*child, wanted))
                return t;
        return nullptr;
    }

    void runTest() override
    {
        beginTest ("the drum pads play the tool on screen, in the bank it shows");
        RigEngine engine;
        engine.prepare (48000.0, 480);
        RigView view (engine, nullptr);
        view.setSize (1440, 960);
        auto& routing = engine.getRouting();

        view.goTo ("Drum machine");
        expectEquals (routing.padsTo.load(), (int) RigRouting::Dest::drums);
        view.goTo ("MPC");
        expectEquals (routing.padsTo.load(), (int) RigRouting::Dest::mpc);

        auto* mpcView = find<MpcView> (view);
        expect (mpcView != nullptr);
        if (mpcView == nullptr)
            return;
        expect (mpcView->getBrowser().getSoundBank() != nullptr, "the MPC shares the Sounds page's bank");
        auto* bankC = find<juce::TextButton> (*mpcView, [] (juce::TextButton& b) { return b.getButtonText() == "C"; });
        expect (bankC != nullptr);
        if (bankC != nullptr)
            bankC->onClick();
        expectEquals (engine.getMpc().padBankOnScreen.load(), 2);
        expectEquals (routing.padBank.load(), 2, "the Launchkey's pads play bank C now");

        view.goTo ("Drum machine");
        view.goTo ("Sampler");
        expectEquals (routing.padsTo.load(), (int) RigRouting::Dest::mpc, "the Sampler · MPC screen's pads too");

        if (auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_MPC_RIG_SNAPSHOT", {}); path.isNotEmpty())
        {
            auto& kit = engine.getMpc().getProject().currentBank();
            for (int i = 0; i < 16; ++i)
            {
                kit.pads[(size_t) i].audio = share (sine (20000, 1, 0.4f, 110.0 * (1 + i % 5)));
                kit.pads[(size_t) i].name = juce::StringArray { "Kick", "Snare", "Hat", "Clap", "Bass" }[i % 5] + " " + juce::String (i + 1);
            }
            engine.getMpc().padBankOnScreen = 0;
            engine.getMpc().getProject().changed();
            if (auto* screen = find<SamplerScreen> (view))
            {
                screen->selectSlice (0);
                screen->showFxKnobs (true);
                screen->showSounds (true);
            }
            engine.getMpc().levelsPad = 0;
            engine.getMpc().levelsType = levels::tune;
            for (auto [page, suffix] : { std::pair<const char*, const char*> { "Sampler", "-f4" }, { "MPC", "-mpc" } })
            {
                view.goTo (page);
                if (auto* editor = find<PadEditor> (view))
                    editor->showSoundPage (true);
                if (auto* b = find<juce::TextButton> (view, [] (juce::TextButton& t) { return t.getButtonText() == "A" && t.isShowing(); }))
                    b->onClick();
                engine.getMpc().getProject().dispatchPendingMessages();
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                juce::File file (path + suffix + ".png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
        }
    }
};

SixteenLevelsTests sixteenLevelsTests;
RigPadsFollowTests rigPadsFollowTests;
NoteRepeatTests noteRepeatTests;
PadSoundTests padSoundTests;
PadControllerTests padControllerTests;
MpcPadToolsViewTests mpcPadToolsViewTests;
}
