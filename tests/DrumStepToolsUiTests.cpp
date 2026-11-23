#include "TestHelpers.h"

#include "DrumProcessor.h"
#include "DrumView.h"
#include "GrooveView.h"
#include "RigEngine.h"
#include "StepTools.h"

using namespace crate;
namespace st = crate::drums::steptools;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

juce::TextButton* findButton (juce::Component& c, const juce::String& startsWith)
{
    if (auto* b = dynamic_cast<juce::TextButton*> (&c); b != nullptr && b->isVisible() && b->getButtonText().startsWith (startsWith))
        return b;
    for (auto* child : c.getChildren())
        if (auto* b = findButton (*child, startsWith))
            return b;
    return nullptr;
}

juce::TextButton* findByTip (juce::Component& c, const juce::String& startsWith)
{
    if (auto* b = dynamic_cast<juce::TextButton*> (&c); b != nullptr && b->getTooltip().startsWith (startsWith))
        return b;
    for (auto* child : c.getChildren())
        if (auto* b = findByTip (*child, startsWith))
            return b;
    return nullptr;
}

// A click on a button, as the mouse would do it.
void click (juce::Button* b)
{
    if (b->getClickingTogglesState())
        b->setToggleState (! b->getToggleState(), juce::dontSendNotification);
    if (b->onClick)
        b->onClick();
}

struct Head : juce::AudioPlayHead
{
    double ppq = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (true);
        info.setBpm (120.0);
        info.setPpqPosition (ppq);
        return info;
    }
};
}

struct DrumStepToolsUiTests : juce::UnitTest
{
    DrumStepToolsUiTests() : juce::UnitTest ("Drum machine: step tools on the panels", "Drums") {}

    void runTest() override
    {
        beginTest ("the shared step tools: hit, roll, builds, flam, accent, soft/loud and erase");
        {
            drums::Pattern p;
            const int v = drums::snare;
            expect (st::apply (p, st::Tool::hit, v, 3), "a hit sounds");
            expectEquals ((int) p.hits[v][3], 1);
            expectEquals ((int) p.shape[v][3], (int) drums::plain);
            st::apply (p, st::Tool::hit, v, 3);
            expectEquals ((int) p.hits[v][3], 0, "Hit again takes it away");

            st::apply (p, st::Tool::roll, v, 5, 3);
            expectEquals ((int) p.hits[v][5], 3, "a roll of three");
            expectEquals ((int) p.shape[v][5], (int) drums::plain);
            st::apply (p, st::Tool::roll, v, 5, 3);
            expectEquals ((int) p.hits[v][5], 1, "the same roll again goes back to one hit");
            st::apply (p, st::Tool::roll, v, 5, 3);
            st::apply (p, st::Tool::roll, v, 5, 6);
            expectEquals ((int) p.hits[v][5], 6, "a different size replaces it");
            st::apply (p, st::Tool::roll, v, 5, 20);
            expectEquals ((int) p.hits[v][5], drums::kMaxRoll, "no more than 8");

            st::apply (p, st::Tool::builds, v, 6, 4);
            expectEquals ((int) p.hits[v][6], 4);
            expectEquals ((int) p.shape[v][6], (int) drums::builds);
            st::apply (p, st::Tool::builds, v, 6, 4);
            expectEquals ((int) p.hits[v][6], 1, "builds again goes back to one plain hit");
            expectEquals ((int) p.shape[v][6], (int) drums::plain);

            st::apply (p, st::Tool::flam, v, 7);
            expectEquals ((int) p.hits[v][7], 1);
            expectEquals ((int) p.shape[v][7], (int) drums::flam);
            st::apply (p, st::Tool::flam, v, 7);
            expectEquals ((int) p.shape[v][7], (int) drums::plain, "flam again makes it plain");
            expectEquals ((int) p.hits[v][7], 1);

            st::apply (p, st::Tool::accent, v, 9);
            expect (p.accent[9]);
            st::apply (p, st::Tool::accent, v, 9);
            expect (! p.accent[9]);

            expect (! st::apply (p, st::Tool::level, v, 11), "soft/loud on an empty step does nothing");
            expectEquals ((int) p.hits[v][11], 0);
            st::apply (p, st::Tool::hit, v, 11);
            st::apply (p, st::Tool::level, v, 11);
            expectEquals ((int) p.velocity[v][11], 60);
            st::apply (p, st::Tool::level, v, 11);
            expectEquals ((int) p.velocity[v][11], 30);
            st::apply (p, st::Tool::level, v, 11);
            expectEquals ((int) p.velocity[v][11], 100, "100, 60, 30, then 100 again");

            st::apply (p, st::Tool::builds, v, 12, 5);
            p.velocity[v][12] = 30;
            st::apply (p, st::Tool::erase, v, 12);
            expectEquals ((int) p.hits[v][12], 0);
            expectEquals ((int) p.shape[v][12], (int) drums::plain);
            expectEquals ((int) p.velocity[v][12], 100, "erase puts the level back too");

            // The right-click cycle.
            int h = 0;
            std::vector<int> seen;
            for (int i = 0; i < 7; ++i)
            {
                h = st::cycleRoll (p, v, 20);
                seen.push_back (h);
            }
            expect (seen == std::vector<int> { 2, 3, 4, 6, 8, 1, 2 }, "none, 2, 3, 4, 6, 8, one, 2...");
            expectEquals (st::nextRollChoice (2), 3);
            expectEquals (st::nextRollChoice (6), 8);
            expectEquals (st::nextRollChoice (8), 2);
        }

        beginTest ("row tools move the shape with the hit; bars copy and double");
        {
            drums::Pattern p;
            const int v = drums::clave;
            st::apply (p, st::Tool::flam, v, 2);
            st::apply (p, st::Tool::builds, v, 15, 5);
            p.velocity[v][2] = 60;
            st::shiftRow (p, v, 1);
            expectEquals ((int) p.hits[v][3], 1);
            expectEquals ((int) p.shape[v][3], (int) drums::flam, "the flam moved");
            expectEquals ((int) p.velocity[v][3], 60, "and its level");
            expectEquals ((int) p.hits[v][0], 5, "step 16 came round to step 1");
            expectEquals ((int) p.shape[v][0], (int) drums::builds);
            st::shiftRow (p, v, -1);
            expectEquals ((int) p.hits[v][2], 1);
            st::reverseRow (p, v);
            expectEquals ((int) p.hits[v][13], 1, "reversed: step 3 is now step 14");
            expectEquals ((int) p.shape[v][13], (int) drums::flam);
            st::spreadRow (p, v, 4);
            int total = 0;
            for (int i = 0; i < 16; ++i)
                total += p.hits[v][i] > 0 ? 1 : 0;
            expectEquals (total, 4, "four hits spread over the row");
            st::clearRow (p, v);
            expectEquals ((int) p.hits[v][13], 0);

            drums::Pattern q;
            st::apply (q, st::Tool::roll, drums::kick, 1, 3);
            q.accent[4] = true;
            expect (st::copyBar (q, 0));
            expectEquals (q.length, 32, "copying bar 1 makes the pattern two bars");
            expectEquals ((int) q.hits[drums::kick][17], 3);
            expect (q.accent[20]);
            expect (st::doubleLength (q));
            expectEquals (q.length, 64);
            expectEquals ((int) q.hits[drums::kick][49], 3);
            expect (! st::doubleLength (q), "64 is the most");
            expect (! st::copyBar (q, 3), "there is no bar after bar 4");
        }

        beginTest ("the Mac panel: the SELECT key picks the sound; a step key with a tool writes it");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            expect (view != nullptr);
            view->setSize (1128, 604);
            p.setPattern (0, {});

            auto* snareKey = findButton (*view, drums::voiceShort (drums::snare));
            expect (snareKey != nullptr, "a SELECT key per sound");
            click (snareKey);
            expectEquals (view->getSelectedVoice(), (int) drums::snare);

            view->pressKey (0);
            expectEquals ((int) p.getPattern (0).hits[drums::snare][0], 1, "the Hit tool is picked at first");

            click (findButton (*view, "Roll"));
            expect (view->getTool() == DrumView::Tool::roll);
            view->setRollCount (3);
            view->pressKey (4);
            expectEquals ((int) p.getPattern (0).hits[drums::snare][4], 3);
            expectEquals ((int) p.getPattern (0).shape[drums::snare][4], (int) drums::plain);
            view->pressKey (4);
            expectEquals ((int) p.getPattern (0).hits[drums::snare][4], 1, "the same roll again: back to one hit");
            expectEquals (view->getRollCount(), 3);

            view->setTool (DrumView::Tool::builds);
            view->setRollCount (5);
            view->pressKey (6);
            expectEquals ((int) p.getPattern (0).hits[drums::snare][6], 5);
            expectEquals ((int) p.getPattern (0).shape[drums::snare][6], (int) drums::builds);
            view->setTool (DrumView::Tool::flam);
            view->pressKey (8);
            expectEquals ((int) p.getPattern (0).shape[drums::snare][8], (int) drums::flam);
            view->setTool (DrumView::Tool::level);
            view->pressKey (0);
            expectEquals ((int) p.getPattern (0).velocity[drums::snare][0], 60);
            view->setTool (DrumView::Tool::accent);
            view->pressKey (12);
            expect (p.getPattern (0).accent[12]);
            view->setTool (DrumView::Tool::erase);
            view->pressKey (8);
            expectEquals ((int) p.getPattern (0).hits[drums::snare][8], 0);

            // The grid uses the same tool, and its right-click still cycles a roll.
            view->setTool (DrumView::Tool::hit);
            view->getGrid().click (3, 1 + drums::cowbell, false);
            expectEquals ((int) p.getPattern (0).hits[drums::cowbell][3], 1);
            expectEquals (view->getSelectedVoice(), (int) drums::cowbell, "a click on a row picks that sound for the keys");
            view->getGrid().click (3, 1 + drums::cowbell, true);
            expectEquals ((int) p.getPattern (0).hits[drums::cowbell][3], 2);
            view->getGrid().click (3, 1 + drums::cowbell, true);
            expectEquals ((int) p.getPattern (0).hits[drums::cowbell][3], 3);
            view->setTool (DrumView::Tool::roll);
            view->setRollCount (4);
            view->getGrid().click (9, 1 + drums::kick, false);
            expectEquals ((int) p.getPattern (0).hits[drums::kick][9], 4, "the grid writes the picked tool too");
        }

        beginTest ("the Mac panel: bars, Copy bar, Double, the length");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            view->setSize (1128, 604);
            p.setPattern (0, {});
            view->selectVoice (drums::kick, false);
            expectEquals (p.getPattern (0).length, 16);

            view->setBar (2);   // Bar 3
            expectEquals (view->getShownBar(), 2);
            expectEquals (p.getPattern (0).length, 48, "a bar past the end makes the pattern longer");
            view->pressKey (1);
            expectEquals ((int) p.getPattern (0).hits[drums::kick][33], 1, "Bar 3, key 2 is step 34");
            expectEquals ((int) p.getPattern (0).hits[drums::kick][1], 0);
            view->getGrid().click (5, 1 + drums::snare, false);
            expectEquals ((int) p.getPattern (0).hits[drums::snare][37], 1, "the grid shows the same bar");

            view->setBar (0);
            view->selectVoice (drums::kick, false);
            view->setTool (DrumView::Tool::roll);
            view->setRollCount (3);
            view->pressKey (4);
            view->copyBarToNext();
            expectEquals ((int) p.getPattern (0).hits[drums::kick][20], 3, "Copy bar: bar 1 into bar 2");

            p.setPattern (0, {});
            view->setBar (0);
            view->setTool (DrumView::Tool::hit);
            view->pressKey (2);
            expectEquals ((int) p.getPattern (0).hits[drums::kick][2], 1);
            view->doubleLength();
            expectEquals (p.getPattern (0).length, 32);
            expectEquals ((int) p.getPattern (0).hits[drums::kick][18], 1, "Double repeats the pattern after itself");
            view->doubleLength();
            expectEquals (p.getPattern (0).length, 64);
            expectEquals ((int) p.getPattern (0).hits[drums::kick][50], 1);
            view->doubleLength();
            expectEquals (p.getPattern (0).length, 64, "64 is the most");

            click (findByTip (*view, "One step longer"));
            expectEquals (p.getPattern (0).length, 64);
            click (findByTip (*view, "One step shorter"));
            expectEquals (p.getPattern (0).length, 63);

            // Follow: the keys turn to the bar that's playing.
            view->setFollow (true);
            expect (view->isFollowing());
            view->setBar (1);
            expect (! view->isFollowing(), "picking a bar stops Follow");
        }

        beginTest ("the Mac panel: Undo and Redo cover every edit, and row tools move the shape");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            view->setSize (1128, 604);
            p.setPattern (0, {});
            const auto start = p.getPattern (0);
            expect (! view->canUndo());
            view->selectVoice (drums::clave, false);
            view->setTool (DrumView::Tool::flam);
            view->pressKey (2);
            const auto one = p.getPattern (0);
            view->setTool (DrumView::Tool::hit);
            view->pressKey (7);
            expect (view->canUndo());
            view->undo();
            expect (p.getPattern (0) == one, "Undo takes the last key back");
            view->undo();
            expect (p.getPattern (0) == start, "and the one before");
            expect (! view->canUndo());
            view->redo();
            expect (p.getPattern (0) == one);
            expect (view->canRedo());
            view->redo();
            expectEquals ((int) p.getPattern (0).hits[drums::clave][7], 1);
            expect (! view->canRedo());

            view->shiftRow (drums::clave, 1);
            expectEquals ((int) p.getPattern (0).hits[drums::clave][3], 1);
            expectEquals ((int) p.getPattern (0).shape[drums::clave][3], (int) drums::flam, "the shift moved the flam");
            view->undo();
            expectEquals ((int) p.getPattern (0).shape[drums::clave][2], (int) drums::flam, "and Undo put it back");

            // Rows: copy, paste, clear.
            view->copyRow (drums::clave);
            view->clearRow (drums::clave);
            expectEquals ((int) p.getPattern (0).hits[drums::clave][2], 0);
            view->pasteRow (drums::cowbell);
            expectEquals ((int) p.getPattern (0).hits[drums::cowbell][2], 1);
            expectEquals ((int) p.getPattern (0).shape[drums::cowbell][2], (int) drums::flam);
            view->spreadRow (drums::cowbell, 2);
            int n = 0;
            for (int i = 0; i < 16; ++i)
                n += p.getPattern (0).hits[drums::cowbell][i] > 0 ? 1 : 0;
            expectEquals (n, 2);
            // Many edits: only the last 50 can be undone.
            for (int i = 0; i < 60; ++i)
                view->pressKey (i % 16);
            int undone = 0;
            while (view->canUndo())
            {
                view->undo();
                ++undone;
            }
            expectEquals (undone, 50);
        }

        beginTest ("Rec live: a note played while the pattern plays is written at the nearest step");
        {
            DrumProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
            auto* view = dynamic_cast<DrumView*> (editor.get());
            view->setSize (1128, 604);
            p.setPattern (0, {});
            p.params.getParameter ("sequencer")->setValueNotifyingHost (1.0f);
            p.prepareToPlay (kRate, kBlock);
            Head head;
            p.setPlayHead (&head);
            juce::AudioBuffer<float> out (2, kBlock);
            juce::MidiBuffer midi;

            click (findButton (*view, "Rec"));
            expect (p.recLive.load());

            // Not yet: the first note comes while it plays, a little before step 5 (index 4).
            for (int i = 0; i < 45; ++i)   // up to 0.9 beats (0.02 a block): step 4 is at 1.0 beat, 0.25 a step
            {
                p.processBlock (out, midi);
                head.ppq += kBlock * 2.0 / kRate;
            }
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, drums::noteForVoice (drums::snare), 0.8f), 0);
            p.processBlock (out, midi);
            head.ppq += kBlock * 2.0 / kRate;
            view->flushRecorded();
            expectEquals ((int) p.getPattern (0).hits[drums::snare][4], 1, "0.9 beats is nearest step 5 (index 4)");
            int total = 0;
            for (int i = 0; i < drums::kMaxSteps; ++i)
                total += p.getPattern (0).hits[drums::snare][i];
            expectEquals (total, 1, "and nowhere else");
            expect (view->canUndo(), "it can be undone");

            // A note that isn't a drum sound isn't written; with Rec live off nothing is.
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 100, 0.8f), 0);
            p.processBlock (out, midi);
            click (findButton (*view, "Rec"));
            expect (! p.recLive.load());
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, drums::noteForVoice (drums::kick), 0.8f), 0);
            p.processBlock (out, midi);
            view->flushRecorded();
            int kicks = 0;
            for (int i = 0; i < drums::kMaxSteps; ++i)
                kicks += p.getPattern (0).hits[drums::kick][i];
            expectEquals (kicks, 0);
            p.setPlayHead (nullptr);
        }

        beginTest ("the Rig's Groove screen: the ROLL chip writes a roll; Bar 4 of 64 steps");
        {
            RigEngine engine;
            engine.prepare (kRate, kBlock);
            auto& d = engine.getDrums();
            d.setPattern (0, {});
            GrooveView view (engine);
            view.setSize (1400, 740);
            view.selectTrack (GrooveView::machine);

            view.pressCell (1 + drums::kick, 0);
            expectEquals ((int) d.getPattern (0).hits[drums::kick][0], 1, "no chip: a click is a plain hit, as before");

            view.setMachineRoll (3);
            view.setMachineTool (st::Tool::roll, true);
            view.pressCell (1 + drums::snare, 4);
            expectEquals ((int) d.getPattern (0).hits[drums::snare][4], 3, "the ROLL chip, then a step: a roll of three");
            view.pressCell (1 + drums::snare, 4);
            expectEquals ((int) d.getPattern (0).hits[drums::snare][4], 1, "again: back to one hit");

            view.setMachineTool (st::Tool::builds, true);
            view.setMachineRoll (4);
            view.pressCell (1 + drums::clap, 6);
            expectEquals ((int) d.getPattern (0).hits[drums::clap][6], 4);
            expectEquals ((int) d.getPattern (0).shape[drums::clap][6], (int) drums::builds);
            view.setMachineTool (st::Tool::flam, true);
            view.pressCell (1 + drums::clap, 8);
            expectEquals ((int) d.getPattern (0).shape[drums::clap][8], (int) drums::flam);
            view.setMachineTool (st::Tool::level, true);
            view.pressCell (1 + drums::clap, 8);
            expectEquals ((int) d.getPattern (0).velocity[drums::clap][8], 60);
            view.setMachineTool (st::Tool::level, false);
            view.pressCell (1 + drums::clap, 8);
            expectEquals ((int) d.getPattern (0).hits[drums::clap][8], 0, "no chip again: a plain click clears it");

            // Right-click still cycles.
            view.pressRoll (1 + drums::snare, 4);
            expectEquals ((int) d.getPattern (0).hits[drums::snare][4], 2);
            view.pressRoll (1 + drums::snare, 4);
            expectEquals ((int) d.getPattern (0).hits[drums::snare][4], 3);

            // Bar 4: the pattern gets as long as it needs, and the steps are 49 to 64.
            expectEquals (d.getPattern (0).length, 16);
            view.setMachinePage (3);
            expectEquals (view.getMachinePage(), 3);
            expectEquals (d.getPattern (0).length, 64);
            view.setMachineTool (st::Tool::level, false);
            view.pressCell (1 + drums::kick, 2);
            expectEquals ((int) d.getPattern (0).hits[drums::kick][50], 1, "Bar 4, step 3 is step 51");
            view.setMachinePage (0);
            expectEquals ((int) d.getPattern (0).hits[drums::kick][2], 0);
        }
    }
};

static DrumStepToolsUiTests drumStepToolsUiTests;
