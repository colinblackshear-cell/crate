// The dirty model with Loop Lab: a warp is an edit; taking every edit back (Undo, Reset, or
// ORIGINAL) puts the recording itself back, so the show is what was saved again, whether or not
// the loop is exactly its length in beats at the song's tempo. (Loop Lab has no Redo.)

#include "RigDirtyTestHelpers.h"

using namespace dirtytest;

namespace
{
using DS = RigDirty::State;

bool sameAudio (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
        return false;
    for (int c = 0; c < a.getNumChannels(); ++c)
        if (std::memcmp (a.getReadPointer (c), b.getReadPointer (c), sizeof (float) * (size_t) a.getNumSamples()) != 0)
            return false;
    return true;
}
} // namespace

class RigDirtyLoopLabTests final : public juce::UnitTest
{
public:
    RigDirtyLoopLabTests() : juce::UnitTest ("Rig dirty with Loop Lab", "Rig") {}

    // The verdict once the loops' hashes are in. A playing loop is copied a slice per audio block
    // (the looper's way), so the engine keeps playing meanwhile.
    Runner* runner = nullptr;
    DS verdict (RigEngine& e)
    {
        for (int i = 0; i < 4000; ++i)
        {
            const auto s = e.getDirty().checkNow();
            if (s.state != DS::pending)
                return s.state;
            runner->block();
            if (i % 8 == 0)
                juce::Thread::sleep (1);
        }
        return e.getDirty().checkNow().state;
    }

    void expectState (RigEngine& e, DS want, const juce::String& what)
    {
        const auto got = verdict (e);
        expect (got == want, what + ": want " + stateName (want) + ", got " + stateName (got));
        if (got != want)
            for (auto& d : e.getDirty().differences (4))
                logMessage ("    differs: " + d);
    }

    void runTest() override
    {
        const auto folder = tempFolder ("RigDirtyLoopLabTests");
        constexpr int exact = (int) kRate * 2;   // 4 beats at 120 BPM
        for (const int extra : { 0, 37, -23 })
        {
            const auto name = extra == 0 ? juce::String ("exactly 4 beats") : juce::String (extra > 0 ? "+" : "") + juce::String (extra) + " samples";
            beginTest ("Loop Lab, a loop " + name + ": warp is dirty; Undo, Reset and ORIGINAL put the recording back");

            const auto show = folder.getChildFile ("Lab " + juce::String (extra) + ".crateshow");
            const auto recording = noise (exact + extra, 21 + extra);
            {
                auto e = std::make_unique<RigEngine>();
                e->prepare (kRate, kBlock);
                e->getTransport().setBpm (120.0);
                e->getLooper().setLayerAudio (0, recording, 4.0);
                Runner r { *e };
                r.seconds (0.05);
                expect (e->saveShowFile (show, rigshow::SaveKind::document).ok);
            }
            auto e = openSaved (show);
            Runner r { *e };
            runner = &r;
            auto& lab = e->getLoopLab();
            lab.renderInline = true;
            e->getTransport().play();
            r.beats (1);
            const auto saved = e->getLooper().copyLayerAudio (0);
            expect (saved.getNumSamples() == recording.getNumSamples(), "the saved loop is the recording's length");
            expectState (*e, DS::clean, "opened");
            expect (lab.open (0));
            expectState (*e, DS::clean, "the lab opened, no edit");

            // Why Undo used to stay dirty: a straight plan rendered at the loop's length in beats
            // is the recording only when that's exactly its length in samples.
            {
                crate::Warp::Plan straight;
                straight.sourceBeats = straight.targetBeats = 4.0;
                const auto rendered = crate::Warp::render (recording, straight, exact, kRate);
                float most = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < juce::jmin (rendered.getNumSamples(), recording.getNumSamples()); ++i)
                        most = juce::jmax (most, std::abs (rendered.getSample (c, i) - recording.getSample (c, i)));
                logMessage ("  a straight plan rendered: " + juce::String (rendered.getNumSamples()) + " samples from "
                            + juce::String (recording.getNumSamples()) + ", largest sample difference over the common part " + juce::String (most, 4));
                expect (sameAudio (rendered, recording) == (extra == 0), "rendering gives the recording back only at its exact length");
            }

            auto undoBack = [&] (const juce::String& what)
            {
                expectState (*e, DS::clean, what);
                const auto now = e->getLooper().copyLayerAudio (0);
                expect (sameAudio (now, saved), what + ": the loop is the saved one, sample for sample (" + juce::String (now.getNumSamples())
                                                    + " samples, saved " + juce::String (saved.getNumSamples()) + ")");
                expectEquals (e->getLooper().getLayer (0).lengthBeats.load(), 4.0);
            };

            crate::Warp::Settings up;
            up.transpose = 2.0f;
            lab.setSettings (0, up);
            r.beats (0.25);
            expectState (*e, DS::dirty, "warped (transpose 2)");
            lab.undo (0);
            r.beats (0.25);
            undoBack ("Undo");
            expect (! lab.canUndo (0), "nothing left to undo");

            // Several edits, then back one at a time: dirty until the last one is undone.
            lab.setSettings (0, up);
            const int marker = lab.addMarker (0, 1.5);
            expect (marker >= 0);
            lab.moveMarker (0, marker, 1.7);
            lab.fitTo (0, 8.0);
            r.beats (0.25);
            expectState (*e, DS::dirty, "four edits");
            for (int step = 3; step >= 1; --step)
            {
                lab.undo (0);
                r.beats (0.25);
                expectState (*e, DS::dirty, "undone back to edit " + juce::String (step));
            }
            lab.undo (0);
            r.beats (0.25);
            undoBack ("every edit undone");

            // A straight plan that isn't the recording as it was still counts.
            lab.fitTo (0, 8.0);
            r.beats (0.25);
            expectState (*e, DS::dirty, "fit to 8 beats (straight, but stretched)");
            lab.undo (0);
            r.beats (0.25);
            undoBack ("fit undone");
            lab.setSettings (0, { crate::Warp::Style::tape, 0.0f, 0.0f });
            r.beats (0.25);
            if (extra == 0)
                expectState (*e, DS::clean, "TAPE on an exact-length loop (renders it unchanged, as before)");
            else
                expectState (*e, DS::dirty, "TAPE: the style changes how it's made to fit");
            lab.undo (0);
            r.beats (0.25);
            undoBack ("style undone");

            lab.setSettings (0, up);
            lab.addMarker (0, 1.0);
            r.beats (0.25);
            expectState (*e, DS::dirty, "warped again");
            lab.resetWarp (0);
            r.beats (0.25);
            undoBack ("Reset");
            lab.undo (0);   // Reset itself can be undone: the warp again
            r.beats (0.25);
            expectState (*e, DS::dirty, "Reset undone");
            lab.resetWarp (0);
            r.beats (0.25);
            undoBack ("Reset again");

            lab.setSettings (0, up);
            r.beats (0.25);
            lab.setOriginal (0, true);
            r.beats (0.25);
            undoBack ("ORIGINAL (A/B)");
            lab.setOriginal (0, false);
            r.beats (0.25);
            expectState (*e, DS::dirty, "back to the warped one");
            lab.undo (0);
            r.beats (0.25);
            undoBack ("Undo after A/B");

            // FOLLOW SONG TEMPO: at another tempo a straight plan is the recording rendered at
            // that tempo (it keeps its pitch), not the recording; back at the tempo it opened at,
            // it's the recording again.
            e->getTransport().setBpm (126.0);
            lab.tick();
            r.beats (0.25);
            expect (e->getLooper().copyLayerAudio (0).getNumSamples() == (int) std::round (4.0 * 60.0 / 126.0 * kRate), "followed the tempo");
            expectState (*e, DS::dirty, "another tempo");
            e->getTransport().setBpm (120.0);
            lab.tick();
            r.beats (0.25);
            undoBack ("the tempo back");
            e->getTransport().stop();
            runner = nullptr;
        }
        folder.deleteRecursively();
    }
};

static RigDirtyLoopLabTests rigDirtyLoopLabTests;
