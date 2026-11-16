#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigPanels.h"
#include "RigSong.h"
#include "RigView.h"
#include "GrooveView.h"

// A first-time user's dead ends, closed: the song never takes over by a stray key, it says when
// it's in charge (and Jam takes over), Clear can be taken back, and the song bar stays thin until
// there's a song.
class RigFirstRunTests : public juce::UnitTest
{
public:
    RigFirstRunTests() : juce::UnitTest ("Rig: a first-time user's dead ends", "Rig") {}

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

    static int sections (RigEngine& e) { return (int) rigsong::sections (e.getTimeline()).size(); }

    void runTest() override
    {
        beginTest ("a number key never makes a song section, and only plays one that exists");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Groove");
            view.pickTrack (GrooveView::bass);
            expect (view.keyPressed (juce::KeyPress ('1', {}, '1')));
            expectEquals (sections (engine), 0, "no section by accident");
            expect (! engine.songInCharge());

            view.keepAsSongPart();   // on purpose
            expectEquals (sections (engine), 1);
            view.keyPressed (juce::KeyPress ('1', {}, '1'));
            expect (engine.songInCharge(), "A plays, with the song in charge");
            view.keyPressed (juce::KeyPress ('2', {}, '2'));
            expectEquals (sections (engine), 1, "no B made by a key");

            engine.jam();
            expect (! engine.songInCharge(), "Jam hands control back");
            expectEquals (sections (engine), 1, "the song waits, as it was");
        }

        beginTest ("with Loops picked on Play, 1-8 work the loop layers, as on the Looper");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Groove");
            view.pickTrack (GrooveView::loops);
            view.keyPressed (juce::KeyPress ('3', {}, '3'));
            juce::AudioBuffer<float> out (2, 480);
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 480);
            const auto st = engine.getLooper().getState (2);
            expect (st == RigLooper::State::armed || st == RigLooper::State::recording, "layer 3 records");
            expectEquals (sections (engine), 0);
        }

        beginTest ("Clear can be taken back (the button, or Cmd+Z), until you change the pattern again");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Groove");
            view.pickTrack (GrooveView::bass);
            auto* groove = find<GrooveView> (view);
            expect (groove != nullptr);
            if (groove == nullptr)
                return;
            auto& bass = engine.getBass();
            auto p = bass.getPattern (bass.getSelectedPattern());
            p.steps[5].on = true;
            p.steps[5].note = 7;
            bass.setPattern (bass.getSelectedPattern(), p);

            groove->clearPatternNow();
            expect (! bass.getPattern (bass.getSelectedPattern()).steps[5].on, "cleared");
            expect (groove->canUndoClear());
            expect (view.undoLast(), "Cmd+Z takes it back");
            expect (bass.getPattern (bass.getSelectedPattern()).steps[5].on);
            expectEquals (bass.getPattern (bass.getSelectedPattern()).steps[5].note, 7);
            expect (! groove->canUndoClear());

            groove->clearPatternNow();
            groove->pressCell (0, 2);   // a new step: that's the pattern now
            expect (! groove->canUndoClear());
            expect (! view.undoLast(), "nothing to undo");
        }

        beginTest ("the song bar is one thin row until there's a song");
        {
            RigEngine engine;
            engine.prepare (48000.0, 480);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.goTo ("Groove");
            auto* footer = find<rigpanels::Footer> (view);
            expect (footer != nullptr);
            if (footer == nullptr)
                return;
            footer->tick();
            expect (footer->isSlim());
            const int thin = footer->getHeight();
            expect (! footer->parts[1]->isVisible());
            view.keepAsSongPart();
            footer->tick();
            expect (! footer->isSlim());
            expect (footer->getHeight() > thin, "the full bar, A to H");
            expect (footer->parts[7]->isVisible());
        }
    }
};

static RigFirstRunTests rigFirstRunTests;
