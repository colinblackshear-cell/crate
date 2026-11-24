// Kaleidobox Visuals: what the picture hears (the tap, from the audio thread without locks), how it
// reads it (the kick, the notes, the clock, Drift), its colours, its picks kept between launches,
// and its controls, the same on the Mac and the phone. The drawing itself runs on the graphics chip
// and isn't run here (no screen); the Linux screenshots use VISUALS_SNAPSHOT for that.
#include "TestHelpers.h"

#include "RigEngine.h"
#include "RigView.h"
#include "core/visuals/VisualAnalysis.h"
#include "core/visuals/VisualSettings.h"
#include "core/visuals/VisualsView.h"

#include <cmath>

using namespace crate::visuals;

namespace
{
// Feeds `seconds` of a sound into the tap, a frame (1/60 s) at a time, stepping the analysis after each.
template <typename Sample>
void feed (VisualTap& tap, VisualAnalysis& a, VisualState& s, double seconds, Sample sample, Clock clock = {}, double* t0 = nullptr)
{
    const double rate = 44100.0;
    const int perFrame = (int) (rate / 60.0);
    std::vector<float> block ((size_t) perFrame);
    double t = t0 != nullptr ? *t0 : 0.0;
    for (int frame = 0; frame < (int) (seconds * 60.0); ++frame)
    {
        for (auto& x : block)
        {
            x = sample (t);
            t += 1.0 / rate;
        }
        tap.push (block.data(), nullptr, perFrame);
        if (clock.playing)
            clock.beats += clock.bpm / 60.0 / 60.0;
        a.step (1.0 / 60.0, clock, s);
    }
    if (t0 != nullptr)
        *t0 = t;
}

int loudestNote (const Frame& f)
{
    int best = 0;
    for (int pc = 1; pc < 12; ++pc)
        if (f.notes[(size_t) pc] > f.notes[(size_t) best])
            best = pc;
    return best;
}
} // namespace

class VisualsTests : public juce::UnitTest
{
public:
    VisualsTests() : juce::UnitTest ("Visuals: the light show", "Visuals") {}

    void runTest() override
    {
        beginTest ("the tap keeps the newest sound in order, and hands notes over once");
        {
            VisualTap tap;
            float out[2048];
            expect (! tap.latest (out, 2048), "nothing yet");
            std::vector<float> ramp (5000);
            for (size_t i = 0; i < ramp.size(); ++i)
                ramp[i] = (float) i;
            tap.push (ramp.data(), nullptr, 3000);
            tap.push (ramp.data() + 3000, nullptr, 2000);
            expect (tap.latest (out, 2048));
            expectEquals (out[0], 5000.0f - 2048.0f, "oldest first");
            expectEquals (out[2047], 4999.0f, "the newest last");

            std::array<float, 12> v;
            tap.noteOn (69, 0.8f);   // A
            tap.noteOn (60 + 12, 0.5f);   // C
            expectEquals (tap.takeNotes (v), 2);
            expectWithinAbsoluteError (v[9], 0.8f, 1.0e-6f);
            expectWithinAbsoluteError (v[0], 0.5f, 1.0e-6f);
            expectEquals (tap.takeNotes (v), 0, "each hit once");

            tap.toolLevel (2, 0.5f);
            tap.toolLevel (2, 0.3f);
            expectWithinAbsoluteError (tap.takeToolLevel (2), 0.5f, 1.0e-6f, "the loudest since the last look");
            expectEquals (tap.takeToolLevel (2), 0.0f);
            expect (! tap.listening());
            tap.addListener();
            expect (tap.listening());
            tap.removeListener();
            expect (! tap.listening());
        }

        beginTest ("a D sounding lights the D, and its colour leads");
        {
            VisualTap tap;
            VisualState state;
            VisualAnalysis a (tap);
            feed (tap, a, state, 1.0, [] (double t) { return 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 293.66 * t); });
            const auto& f = a.frame();
            expect (f.hearing);
            expectEquals (loudestNote (f), 2, "D");
            // Scriabin's D is yellow: the colour with the most weight is it.
            int top = 0;
            for (int j = 1; j < 6; ++j)
                if (f.weights[(size_t) j] > f.weights[(size_t) top])
                    top = j;
            const auto green = scriabinColours()[2];
            expectWithinAbsoluteError (f.palette[(size_t) top * 3 + 1], green.getFloatGreen(), 0.05f);
            expectWithinAbsoluteError (f.palette[(size_t) top * 3], green.getFloatRed(), 0.05f);

            // A C instead: the C takes over.
            feed (tap, a, state, 2.0, [] (double t) { return 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 261.63 * t); });
            expectEquals (loudestNote (a.frame()), 0, "C");
        }

        beginTest ("a kick flashes the picture; silence lets it breathe on its own");
        {
            VisualTap tap;
            VisualState state;
            VisualAnalysis a (tap);
            auto kick = [] (double t)
            {
                const double in = std::fmod (t, 0.5);   // a kick every half second (120 BPM)
                return in < 0.25 ? 0.9f * (float) (std::sin (2.0 * juce::MathConstants<double>::pi * 55.0 * in) * std::exp (-in * 18.0)) : 0.0f;
            };
            float most = 0;
            double t = 0;
            for (int i = 0; i < 4 * 60; ++i)
            {
                feed (tap, a, state, 1.0 / 60.0, kick, {}, &t);
                most = juce::jmax (most, a.frame().beat);
            }
            expect (most > 0.9f, "the kick lands: " + juce::String (most));
            expect (a.frame().bars > 1.0f, "kicks count the bars without a clock: " + juce::String (a.frame().bars));

            // Nothing new written for a second: not hearing.
            for (int i = 0; i < 60; ++i)
                a.step (1.0 / 60.0, {}, state);
            expect (! a.frame().hearing);
        }

        beginTest ("Drift moves to the next look on each 8-bar phrase of the song's clock, and melts into it");
        {
            VisualTap tap;
            VisualState state;
            state.look = 1;
            state.drift = true;
            VisualAnalysis a (tap);
            Clock clock;
            clock.playing = true;
            clock.bpm = 120.0;
            auto quiet = [] (double) { return 0.01f; };
            feed (tap, a, state, 15.0, quiet, clock);   // 7.5 bars at 120
            expectEquals (state.look.load(), 1, "still the first phrase");
            Clock later = clock;
            later.beats = 32.0 + 0.1;   // bar 9
            a.step (1.0 / 60.0, later, state);
            expectEquals (state.look.load(), 2, "the next look on bar 9");
            expect (a.frame().mix < 0.1f && a.frame().lookA == 1 && a.frame().lookB == 2, "melting from the last one");
            for (int i = 0; i < 4 * 60; ++i)
                a.step (1.0 / 60.0, later, state);
            expectEquals (a.frame().mix, 1.0f, "melted in");

            state.drift = false;
            later.beats = 64.1;
            a.step (1.0 / 60.0, later, state);
            expectEquals (state.look.load(), 2, "Drift off: the look stays");
        }

        beginTest ("the Tools colours follow each tool's level; fixed colours stay as they are");
        {
            VisualTap tap;
            VisualState state;
            state.colours = (int) Colours::tools;
            VisualAnalysis a (tap);
            tap.toolLevel (2, 0.9f);   // the bass plays
            feed (tap, a, state, 0.1, [] (double) { return 0.0f; });
            const auto& f = a.frame();
            expect (f.weights[2] > f.weights[3] + 0.3f, "the bass's colour shows most");
            expectWithinAbsoluteError (f.palette[6], fixedColours (Colours::tools)[2].getFloatRed(), 1.0e-4f);
            state.colours = (int) Colours::ice;
            a.step (1.0 / 60.0, {}, state);
            expectWithinAbsoluteError (a.frame().palette[0], fixedColours (Colours::ice)[0].getFloatRed(), 1.0e-4f);
        }

        beginTest ("the note colours: Scriabin, the circle of fifths, and your own, kept between launches");
        {
            expect (scriabinColours()[0] == juce::Colour (0xffff0000), "Scriabin's C is red");
            const auto circle = circleColours();
            expect (circle[7].getHue() > circle[0].getHue() - 1.0f, "G sits next to C round the circle");
            expectWithinAbsoluteError (std::fmod (circle[7].getHue() - circle[0].getHue() + 1.0f, 1.0f), 1.0f / 12.0f, 0.01f, "a fifth up is the next colour");

            VisualState s;
            s.look = 3;
            s.drift = true;
            s.colours = (int) Colours::yours;
            s.wild = 4;
            s.trails = 1;
            s.setYours (9, juce::Colour (0xff123456));
            auto file = juce::File::createTempFile (".json");
            save (s, file);
            VisualState back;
            load (back, file);
            file.deleteFile();
            expectEquals (back.look.load(), 3);
            expect (back.drift.load());
            expectEquals (back.colours.load(), (int) Colours::yours);
            expectEquals (back.wild.load(), 4);
            expectEquals (back.trails.load(), 1);
            expect (back.noteColours()[9] == juce::Colour (0xff123456), back.noteColours()[9].toDisplayString (true));
            VisualState junk;
            fromVar (junk, juce::JSON::parse ("{\"look\": 99, \"colours\": -4, \"wild\": 0}"));
            expect (junk.look.load() == kLooks - 1 && junk.colours.load() == 0 && junk.wild.load() == 1, "out-of-range picks are pulled back in");
        }

        beginTest ("the shaders have every look, and work on the phone's and the Mac's graphics");
        {
            const auto scene = VisualsRenderer::sceneShader();
            for (auto name : { "kaleido", "tunnel", "liquid", "bloom", "trails" })
                expect (scene.contains (juce::String ("vec3 ") + name + " (vec2 p)"), name);
            for (const auto& src : { scene, VisualsRenderer::feedbackShader(), VisualsRenderer::outputShader() })
            {
                expect (src.contains ("#ifdef GL_ES\nprecision highp float;"), "the phone needs a precision");
                expect (src.contains ("gl_FragColor") && ! src.contains ("#version"), "written once, translated for each chip");
            }
        }

        beginTest ("the controls: the five looks, Drift, WILD, TRAILS, COLOURS, and a tap hides them");
        {
            VisualTap tap;
            VisualState state;
            VisualsView::Options o;
            o.phone = true;
            VisualsView view (tap, state, o);
            view.setSize (390, 672);
            for (auto name : { "KALEIDO", "TUNNEL", "LIQUID", "BLOOM", "TRAILS", "DRIFT", "WILD", "COLOURS", "Close" })
            {
                auto* c = view.control (name);
                expect (c != nullptr && c->isVisible() && ! c->getBounds().isEmpty(), name);
                if (c != nullptr && o.phone)
                    expect (c->getHeight() >= 44, juce::String (name) + " is thumb-sized");
            }
            int changes = 0;
            view.onChanged = [&] { ++changes; };
            state.drift = true;
            click (view.control ("LIQUID"));
            expectEquals (state.look.load(), 2);
            expect (! state.drift.load(), "picking a look stops Drift");
            click (view.control ("DRIFT"));
            expect (state.drift.load());
            const int wildWas = state.wild.load();
            click (view.control ("WILD"));
            expectEquals (state.wild.load(), wildWas % 4 + 1);
            expect (changes >= 3, "every pick is saved");
            bool closed = false;
            view.onClose = [&] { closed = true; };
            click (view.control ("Close"));
            expect (closed);

            click (view.control ("COLOURS"));
            expect (view.coloursOpen());
            auto* key = view.control ("Key A");
            expect (key != nullptr && key->isVisible() && key->getHeight() >= 44, "the 12 keys, thumb-sized on the phone");
            click (view.control ("ICE"));
            expectEquals (state.colours.load(), (int) Colours::ice);
            expect (! key->isVisible(), "no keys for a fixed set");
            click (view.control ("DONE"));
            expect (! view.coloursOpen());

            // A tap on the picture hides the controls; another brings them back. A swipe: the next look.
            tapAt (view, { 195, 300 }, { 195, 300 });
            expect (! view.controlsShown() && ! view.control ("KALEIDO")->isVisible());
            tapAt (view, { 195, 300 }, { 195, 300 });
            expect (view.controlsShown());
            const int lookWas = state.look.load();
            tapAt (view, { 300, 300 }, { 120, 310 });
            expectEquals (state.look.load(), (lookWas + 1) % kLooks, "swipe left: the next look");

            // A second screen's picture has no controls at all.
            VisualsView::Options bare;
            bare.controls = false;
            VisualsView show (tap, state, bare);
            show.setSize (1920, 1080);
            expect (! show.control ("KALEIDO")->isVisible() && ! show.control ("Close")->isVisible());
            tapAt (show, { 500, 500 }, { 500, 500 });
            expect (! show.controlsShown(), "a tap doesn't bring buttons onto the projector");
        }

        beginTest ("the Rig: VISUALS in the top bar, over the screen in the window, F8, and nothing heard while it's off");
        {
            RigEngine engine;
            engine.prepare (48000.0, 512);
            RigView view (engine, nullptr);
            view.setSize (1440, 960);
            view.showView ("Play");   // (the Ideas front page has no top bar)
            expect (view.visualsPlace() == RigView::VisualsPlace::off);
            expect (findChild (view, "Visuals") != nullptr, "the top bar's VISUALS button");

            // Off: the engine copies nothing for the picture.
            engine.getRouting().keysTo = (int) RigRouting::Dest::synth;
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 57, 0.9f));
            runBlocks (engine, 8);
            expectEquals ((int) engine.visualTap.samplesWritten(), 0, "nothing copied while Visuals is off");

            view.showVisuals (RigView::VisualsPlace::window);
            auto* pic = view.visualsInWindow();
            expect (pic != nullptr && pic->getParentComponent() == &view);
            expect (pic->getY() >= 60 && pic->getBottom() < view.getHeight() - 60, "between the top bar and the Rec dock: " + pic->getBounds().toString());
            // In a real window it draws and listens; here (no screen) it doesn't, so listen by hand.
            engine.visualTap.addListener();
            engine.addMidiNow (juce::MidiMessage::noteOn (1, 64, 0.9f));
            runBlocks (engine, 8);
            expect (engine.visualTap.samplesWritten() >= 8 * 512, "the mix is copied while it shows");
            std::array<float, 12> hits;
            engine.visualTap.takeNotes (hits);
            expect (hits[4] > 0.0f, "the Synth's E reached the picture as a note");
            float synth = engine.visualTap.takeToolLevel (3);
            expect (synth > 0.0f, "and the Synth's level: " + juce::String (synth));
            engine.visualTap.removeListener();

            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F8Key)), "F8");
            expect (view.visualsPlace() == RigView::VisualsPlace::off && view.visualsInWindow() == nullptr);
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::F8Key)));
            expect (view.visualsPlace() == RigView::VisualsPlace::window, "F8 again: back where it was");
            expect (view.keyPressed (juce::KeyPress ('3', juce::ModifierKeys::altModifier, 0)), "Option+3");
            expect (view.visualsInWindow() != nullptr);
            expect (view.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
            expect (view.visualsPlace() == RigView::VisualsPlace::off, "Esc closes it");
        }
    }

private:
    static void click (juce::Component* c)
    {
        if (c == nullptr)
            return;
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto centre = c->getLocalBounds().getCentre().toFloat();
        juce::MouseEvent e (source, centre, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, c, c, juce::Time::getCurrentTime(), centre, juce::Time::getCurrentTime(), 1, false);
        c->mouseDown (e);
        c->mouseUp (e);
    }
    static void tapAt (juce::Component& c, juce::Point<float> from, juce::Point<float> to)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent down (source, from, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, from, now, 1, false);
        juce::MouseEvent up (source, to, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, from, now, 1, false);
        c.mouseDown (down);
        c.mouseUp (up);
    }
    static juce::Component* findChild (juce::Component& parent, const juce::String& name)
    {
        for (auto* c : parent.getChildren())
        {
            if (c->getName() == name || (dynamic_cast<juce::Button*> (c) != nullptr && dynamic_cast<juce::Button*> (c)->getButtonText() == name))
                return c;
            if (auto* found = findChild (*c, name))
                return found;
        }
        return nullptr;
    }
    static void runBlocks (RigEngine& engine, int blocks)
    {
        juce::AudioBuffer<float> out (2, 512);
        for (int b = 0; b < blocks; ++b)
            engine.process (nullptr, 0, out.getArrayOfWritePointers(), 2, 512);
    }
};

static VisualsTests visualsTests;
