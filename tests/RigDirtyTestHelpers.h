#pragma once

// What the dirty model's tests share: a rich show built the way a person would, a runner that
// plays the engine block by block (with the window's and the engine's timers), and the hands.

#include <juce_audio_processors/juce_audio_processors.h>

#include "RigEngine.h"
#include "RigShowFiles.h"
#include "RigSong.h"
#include "RigView.h"

#include <functional>
#include <iostream>

namespace dirtytest
{
// Unnamed inside, so each test file gets its own copy (the timer trick instantiates a template).
namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// The window's timer, for tests that run it by hand.
template <typename Tag, typename Tag::type M> struct Rob { friend typename Tag::type get (Tag) { return M; } };
struct TimerTag { using type = void (RigView::*)(); friend type get (TimerTag); };
template struct Rob<TimerTag, &RigView::timerCallback>;

inline juce::AudioBuffer<float> noise (int frames, int seed)
{
    juce::AudioBuffer<float> b (2, frames);
    juce::Random r (seed);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < frames; ++i)
            b.setSample (c, i, (r.nextFloat() - 0.5f) * 0.4f);
    return b;
}

// Plays the engine on this thread, and every third block does what its timers do.
struct Runner
{
    RigEngine& engine;
    RigView* view = nullptr;
    bool input = false;
    int blocks = 0;
    juce::Random random { 7 };
    void block()
    {
        juce::AudioBuffer<float> in (2, kBlock), out (2, kBlock);
        in.clear();
        if (input)
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < kBlock; ++i)
                    in.setSample (c, i, (random.nextFloat() - 0.5f) * 0.3f);
        engine.process (in.getArrayOfReadPointers(), 2, out.getArrayOfWritePointers(), 2, kBlock);
        if (++blocks % 3 == 0)
        {
            engine.tickScenes();
            engine.serviceTakes();
            engine.serviceLoopBanks();
            engine.getAutomation().service();
            engine.getDirty().service();
            if (view != nullptr)
                (view->*get (TimerTag {})) ();
        }
    }
    void seconds (double s) { for (int i = 0, n = (int) (s * kRate / kBlock); i < n; ++i) block(); }
    void beats (double b) { seconds (b * 60.0 / engine.getTransport().getBpm()); }
};

inline juce::AudioProcessorParameter* paramOf (juce::AudioProcessor& tool, const char* id)
{
    for (auto* p : tool.getParameters())
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); w != nullptr && w->paramID == id)
            return p;
    return nullptr;
}
inline int indexOf (juce::AudioProcessor& tool, const char* id)
{
    const auto& ps = tool.getParameters();
    for (int i = 0; i < ps.size(); ++i)
        if (auto* w = dynamic_cast<juce::AudioProcessorParameterWithID*> (ps[i]); w != nullptr && w->paramID == id)
            return i;
    return -1;
}
// Your hand on a setting (the window, a learned knob).
inline void hand (juce::AudioProcessor& tool, const char* id, float plain)
{
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (paramOf (tool, id)))
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
}
// Your hand on a mute button: what the Mixer's buttons do (the mute, then they say it's yours).
inline void handStripMute (RigEngine& e, int strip, bool mute)
{
    e.getStrip (strip).mute = mute;
    e.getDirty().authoredField (RigDirty::stripMute, strip);
}
inline void handLoopMute (RigEngine& e, int layer, bool mute)
{
    e.getLooper().getLayer (layer).mute = mute;
    e.getDirty().authoredField (RigDirty::loopMute, layer);
}

// The CP1 show: two loops, a drum pattern, bass patterns, an automation lane on the bass cutoff,
// a macro on the synth cutoff, a two-section song whose second section plays bank B, the drums muted.
inline void buildRich (RigEngine& e, int loopSeconds)
{
    e.getTransport().setBpm (120.0);
    const int frames = (int) (kRate * loopSeconds);
    const double beats = loopSeconds * 2.0;
    e.getLooper().setLayerAudio (0, noise (frames, 1), beats);
    e.getLooper().setLayerAudio (1, noise (frames, 2), beats);
    {
        auto p = e.getDrums().getEditedPattern();
        for (int s = 0; s < 16; s += 4)
            p.hits[0][(size_t) s] = 1;
        e.getDrums().setEditedPattern (p);
        hand (e.getDrums(), "sequencer", 1.0f);
    }
    for (int pat : { 0, 2 })
    {
        auto p = e.getBass().getPattern (pat);
        for (int s = 0; s < 16; s += 2)
            p.steps[(size_t) s].on = true, p.steps[(size_t) s].note = s % 5;
        e.getBass().setPattern (pat, p);
    }
    {
        RigAutomation::Lane lane;
        lane.tool = RigEngine::bassTool;
        lane.parameter = indexOf (e.getBass(), "cutoff");
        lane.paramId = "cutoff";
        lane.lengthBeats = 4;
        lane.points = { { 0.0, 0.0f }, { 4.0, 1.0f } };
        e.getAutomation().addLane (lane);
    }
    {
        e.getMacros().setName (0, "Build");
        RigMacros::Target t;
        t.tool = RigEngine::synthTool_;
        t.parameter = indexOf (e.getSynth(), "cutoff");
        t.paramId = "cutoff";
        t.from = 0.1f;
        t.to = 0.9f;
        e.getMacros().setTarget (0, t);
        e.getMacros().value[0] = 0.6f;
    }
    {
        auto& t = e.getTimeline();
        rigsong::addSection (t, "Intro", 2);
        rigsong::addSection (t, "Verse", 2);
        rigsong::setBox (t, 0, RigTimeline::laneForStrip (RigEngine::bassStrip), 0);
        rigsong::setBox (t, 1, RigTimeline::laneForStrip (RigEngine::bassStrip), 2);
        rigsong::setBox (t, 1, RigTimeline::laneForStrip (RigEngine::drumsStrip), 0);
        rigsong::setBox (t, 1, RigTimeline::laneForStrip (RigEngine::synthStrip), 1);
        t.changed();
    }
    e.saveLoopBank ("A");
    e.newLoopBank ("B");
    e.getLooper().setLayerAudio (0, noise (frames, 3), beats);
    e.switchLoopBank (0);
    e.serviceLoopBanks();
    if (e.getTimeline().getMarkers().size() > 1)
        e.getTimeline().getMarkers()[1].loopBank = 1;
    e.getStrip (RigEngine::drumsStrip).mute = true;
    e.getStrip (RigEngine::bassStrip).level = 0.7f;
}

// Two takes of section 1 on loop 5, from the input.
inline void addTakes (RigEngine& e)
{
    Runner r { e };
    for (int k = 0; k < 2; ++k)
    {
        r.input = true;
        juce::String why;
        if (! e.recordInto (0, 4, RigLooper::input, false, &why))
            std::cout << "  recordInto failed: " << why << std::endl;
        r.beats (12);
        r.input = false;
        r.seconds (0.2);
        if (e.getTransport().isPlaying())
            e.getTransport().stop();
        r.seconds (0.2);
    }
}

inline void addNoteClip (RigEngine& e, int notes = 8, int strip = RigEngine::synthStrip, double startBar = 0)
{
    rignotes::Clip c;
    c.strip = strip;
    c.startBar = startBar;
    for (int n = 0; n < notes; ++n)
    {
        rignotes::Note note;
        note.beat = n * 0.5;
        note.note = 60 + n % 12;
        note.length = 0.25;
        c.notes.push_back (note);
    }
    e.getNoteClips().push_back (c);
    e.notesChanged();
}

// Opens a saved show the way the app does, with the dirty model on.
inline std::unique_ptr<RigEngine> openSaved (const juce::File& show, int block = kBlock)
{
    auto e = std::make_unique<RigEngine>();
    e->prepare (kRate, block);
    e->getDirty().setEnabled (true);
    if (auto xml = juce::parseXML (show))
        e->loadShow (juce::ValueTree::fromXml (*xml));
    e->readLoopAudio (show);
    return e;
}

inline juce::String stateName (RigDirty::State s)
{
    return s == RigDirty::State::clean ? "clean" : s == RigDirty::State::dirty ? "dirty" : "pending";
}

// Waits for the verdict (hashes may be on their way) and returns it.
inline RigDirty::Status settle (RigEngine& e, int timeoutMs = 20000)
{
    e.getDirty().waitUntilResolved (timeoutMs);
    return e.getDirty().checkNow();
}

inline juce::File tempFolder (const juce::String& name)
{
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (name);
    f.deleteRecursively();
    f.createDirectory();
    return f;
}
} // namespace
} // namespace dirtytest
