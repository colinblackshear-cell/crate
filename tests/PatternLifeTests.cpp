#include "TestHelpers.h"

#include "core/synth/PatternLife.h"
#include "DrumEngine.h"
#include "DrumProcessor.h"
#include "BassProcessor.h"
#include "GrooveView.h"
#include "RigEngine.h"

// Pattern life: chance, conditions, nudge, rows of their own length, Euclid and Mutate on the step
// grids (the drum machine, the bass, the MPC's drum grid).
class PatternLifeTests : public juce::UnitTest
{
public:
    PatternLifeTests() : juce::UnitTest ("Pattern life", "Rig") {}

    // Plays the drum machine for `steps` 16th steps and returns each hit as (sample, note).
    static std::vector<std::pair<int, int>> play (crate::drums::Pattern& p, int steps, double rate = 48000.0)
    {
        using namespace crate::drums;
        Engine e;
        e.prepare (rate);
        Settings s;
        s.sequencerOn = true;
        e.setSettings (s);
        e.setPatterns (&p, nullptr);
        juce::MidiBuffer tap, none;
        e.noteTap = &tap;
        std::vector<std::pair<int, int>> hits;
        const double bpm = 120.0;
        const int block = 480;
        const int total = (int) std::round (steps * 0.25 * 60.0 / bpm * rate);
        std::vector<float> l ((size_t) block), r ((size_t) block);
        for (int at = 0; at < total; at += block)
        {
            tap.clear();
            Clock c;
            c.playing = true;
            c.bpm = bpm;
            c.ppq = at * bpm / (60.0 * rate);
            e.render (l.data(), r.data(), block, none, c);
            for (const auto m : tap)
                if (m.getMessage().isNoteOn())
                    hits.push_back ({ at + m.samplePosition, m.getMessage().getNoteNumber() });
        }
        return hits;
    }

    static int count (const std::vector<std::pair<int, int>>& hits, int voice)
    {
        int n = 0;
        for (auto& h : hits)
            n += h.second == crate::drums::noteForVoice (voice) ? 1 : 0;
        return n;
    }

    void runTest() override
    {
        namespace life = crate::life;
        using namespace crate::drums;

        beginTest ("Euclid and conditions");
        {
            const auto t = life::euclid (3, 8);
            juce::String text;
            for (bool b : t)
                text << (b ? "x" : ".");
            expect (text == "x..x..x.", text);
            const auto turned = life::euclid (3, 8, 1);
            expect (turned[1] && ! turned[0]);
            expect (life::passes (life::oneOfTwo, 0, false) && ! life::passes (life::oneOfTwo, 1, false));
            expect (life::passes (life::threeOfFour, 2, false) && ! life::passes (life::threeOfFour, 3, false));
            expect (life::passes (life::fill, 5, true) && ! life::passes (life::fill, 5, false));
            expect (life::passes (life::first, 0, false) && ! life::passes (life::first, 1, false));
            expectEquals (life::nextIn (life::chanceSteps(), 100), 75);
            expectEquals (life::nextIn (life::chanceSteps(), 100, true), 10);
        }

        beginTest ("the drum machine: a condition plays some times round; a chance some of the time");
        {
            Pattern p;
            p.hits[kick][0] = 1;
            p.cond[kick][0] = life::oneOfTwo;
            expectEquals (count (play (p, 16 * 4), kick), 2, "1:2 over four times round");

            Pattern q;
            for (int st = 0; st < 16; ++st)
            {
                q.hits[closedHat][(size_t) st] = 1;
                q.chance[closedHat][(size_t) st] = 50;
            }
            const int n = count (play (q, 16 * 8), closedHat);
            expect (n > 30 && n < 98, "about half of 128: " + juce::String (n));
        }

        beginTest ("nudge: a hit early or late; a row of its own length comes round against the beat");
        {
            Pattern p;
            p.hits[snare][4] = 1;
            const auto straight = play (p, 16);
            p.nudge[snare][4] = 40;
            const auto late = play (p, 16);
            p.nudge[snare][4] = -40;
            const auto early = play (p, 16);
            expectEquals ((int) straight.size(), 1);
            expectEquals ((int) late.size(), 1);
            expectEquals ((int) early.size(), 1, "an early hit plays once (not again on its step)");
            if (straight.size() == 1 && late.size() == 1 && early.size() == 1)
            {
                const int step = 6000;   // a 16th at 120 BPM, 48 kHz
                expect (std::abs (late[0].first - straight[0].first - step * 40 / 100) < 64, juce::String (late[0].first - straight[0].first));
                expect (std::abs (straight[0].first - early[0].first - step * 40 / 100) < 64, juce::String (straight[0].first - early[0].first));
            }

            Pattern poly;
            poly.hits[rim][0] = 1;
            poly.rowLength[rim] = 3;
            expectEquals (count (play (poly, 16), rim), 6, "every 3 steps: 0, 3, 6, 9, 12, 15");
            expectEquals (count (play (poly, 32), rim), 11, "and on round the next bar (18, 21...)");
        }

        beginTest ("pattern life is saved with the drum machine and the bass");
        {
            Pattern p;
            p.hits[kick][0] = 1;
            p.chance[kick][0] = 25;
            p.cond[kick][0] = life::threeOfFour;
            p.nudge[kick][0] = -10;
            p.rowLength[clave] = 5;
            const auto back = DrumProcessor::patternFromTree (DrumProcessor::patternToTree (p));
            expect (back == p);
            Pattern plain;
            plain.hits[kick][0] = 1;
            expect (! DrumProcessor::patternToTree (plain).getChildWithName ("Life").isValid(), "only where it's used");
            expect (DrumProcessor::patternFromTree (DrumProcessor::patternToTree (plain)) == plain);

            crate::bass::Pattern b;
            b.steps[3].on = true;
            b.steps[3].chance = 50;
            b.steps[3].cond = life::fill;
            const auto bb = BassProcessor::patternFromTree (BassProcessor::patternToTree (b));
            expectEquals ((int) bb.steps[3].chance, 50);
            expectEquals ((int) bb.steps[3].cond, (int) life::fill);
            expectEquals ((int) bb.steps[0].chance, 100);
        }

        beginTest ("the Groove grid: Chance mode, Euclid, its own length, Mutate and its undo");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            GrooveView view (engine);
            view.setSize (1280, 820);
            view.selectTrack (GrooveView::machine);
            view.resized();
            auto& d = engine.getDrums();
            auto pattern = [&] { return d.getPattern (d.getSelectedPattern(), d.editVariation); };
            const int kickRow = 1 + kick;
            d.setPattern (d.getSelectedPattern(), Pattern {}, d.editVariation);   // (a blank beat)
            view.pressCell (kickRow, 0);
            expectEquals ((int) pattern().hits[kick][0], 1);
            view.setLifeMode (GrooveView::lifeChance);
            view.pressCell (kickRow, 0);
            expectEquals ((int) pattern().chance[kick][0], 75, "a click on a lit step: its chance");
            expectEquals ((int) pattern().hits[kick][0], 1, "still lit");
            view.setLifeMode (GrooveView::lifeCond);
            view.pressCell (kickRow, 0);
            expectEquals ((int) pattern().cond[kick][0], 1);
            view.setLifeMode (GrooveView::lifeHits);

            const int rimRow = 1 + rim;
            view.euclidRow (rimRow, 3, 8);
            const auto e = pattern();
            expectEquals ((int) e.rowLength[rim], 8, "its own length");
            expect (e.hits[rim][0] && e.hits[rim][3] && e.hits[rim][6] && ! e.hits[rim][1]);
            view.setRowLength (rimRow, 0);
            expectEquals ((int) pattern().rowLength[rim], 0);
            view.turnRow (rimRow, 1);
            expect (pattern().hits[rim][1] && ! pattern().hits[rim][0]);

            const auto before = pattern();
            view.mutate (1.0f);
            expect (! (pattern() == before), "mutated");
            expect (view.undoMutate());
            expect (pattern() == before, "and put back");

            if (const auto path = juce::SystemStats::getEnvironmentVariable ("CRATE_TAKES_SNAPSHOT", {}); path.isNotEmpty())
            {
                view.euclidRow (rimRow, 5, 12);
                view.euclidRow (1 + kick, 4, 16);
                view.setLifeMode (GrooveView::lifeChance);
                view.pressCell (1 + kick, 4);
                view.pressCell (1 + kick, 4);
                view.resized();
                const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 1.0f);
                juce::File file (path + "-life.png");
                file.deleteFile();
                juce::FileOutputStream stream (file);
                juce::PNGImageFormat().writeImageToStream (image, stream);
            }
        }
    }
};

static PatternLifeTests patternLifeTests;
