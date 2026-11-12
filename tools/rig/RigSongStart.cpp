#include "RigSongStart.h"
#include "GrooveView.h"

namespace songstart
{
namespace
{
void setParam (juce::AudioProcessorValueTreeState& state, const char* id, float plainValue)
{
    if (auto* p = state.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
}

bool bassPatternEmpty (const crate::bass::Pattern& p)
{
    for (const auto& step : p.steps)
        if (step.on)
            return false;
    return true;
}

// "Bass pattern 3 is empty and on." or, with all eight in use, which one stays on.
juce::String startedText (const juce::String& what, int index, bool fresh)
{
    return fresh ? what + " " + juce::String (index + 1) + " is new and empty, and on."
                 : "All the " + what.toLowerCase() + "s have something in them, so " + what.toLowerCase() + " "
                       + juce::String (index + 1) + " stays on: clear it, or keep building on it.";
}

const juce::String kKeep = " When it sounds right, New pattern > Keep as the next song part.";
}

juce::String name (int start)
{
    switch (start)
    {
        case mpcBeat:     return "A beat on the MPC pads";
        case machineBeat: return "A beat on the drum machine";
        case bassLine:    return "A bass line";
        case keys:        return "A melody or chords on the keys (Synth)";
        case vocoder:     return "Sing through the vocoder";
        case loop:        return "A loop: record anything and layer it";
        case sample:      return "A sample: record it, play it on the keys";
        default:          return {};
    }
}

int firstEmptyMpcBeat (RigEngine& engine)
{
    const auto& sequences = engine.getMpc().getProject().getSequences();
    for (int i = 0; i < kPatternsShown; ++i)
        if (i >= (int) sequences.size() || sequences[(size_t) i].isEmpty())
            return i;
    return -1;
}

int firstEmptyMachinePattern (RigEngine& engine)
{
    // (All 16: the drum machine's first ones come with beats in them.)
    for (int i = 0; i < crate::drums::kPatterns; ++i)
        if (engine.getDrums().getPattern (i).isEmpty())
            return i;
    return -1;
}

int firstEmptyBassPattern (RigEngine& engine)
{
    for (int i = 0; i < kPatternsShown && i < crate::bass::kPatterns; ++i)
        if (bassPatternEmpty (engine.getBass().getPattern (i)))
            return i;
    return -1;
}

Result begin (RigEngine& engine, int start)
{
    Result r;
    const bool playing = engine.getTransport().isPlaying();
    const juce::String play = playing ? juce::String() : juce::String (" Press Play (space) to hear it go round.");
    switch (start)
    {
        case mpcBeat:
        {
            auto& project = engine.getMpc().getProject();
            const int empty = firstEmptyMpcBeat (engine);
            const int beat = empty >= 0 ? empty : juce::jmax (0, project.getCurrentSequenceIndex());
            while ((int) project.getSequences().size() <= beat)
                project.addSequence();
            engine.playMpcSequence (beat);
            engine.getStrip (RigEngine::mpcStrip).mute = false;
            r.track = GrooveView::drums;
            r.pattern = beat;
            r.message = startedText ("Beat", beat, empty >= 0)
                        + " Tap the steps, or play your keyboard's pads while it goes round." + play + kKeep;
            break;
        }
        case machineBeat:
        {
            auto& d = engine.getDrums();
            const int empty = firstEmptyMachinePattern (engine);
            const int pattern = empty >= 0 ? empty : d.getSelectedPattern();
            d.selectPattern (pattern);
            setParam (d.params, "sequencer", 1.0f);
            engine.getStrip (RigEngine::drumsStrip).mute = false;
            r.track = GrooveView::machine;
            r.pattern = pattern;
            r.message = startedText ("Pattern", pattern, empty >= 0) + " Tap the steps to put the drum machine's hits in." + play + kKeep;
            break;
        }
        case bassLine:
        {
            auto& b = engine.getBass();
            const int empty = firstEmptyBassPattern (engine);
            const int pattern = empty >= 0 ? empty : b.getSelectedPattern();
            b.selectPattern (pattern);
            setParam (b.params, "sequencer", 1.0f);
            engine.getStrip (RigEngine::bassStrip).mute = false;
            r.track = GrooveView::bass;
            r.pattern = pattern;
            r.message = startedText ("Pattern", pattern, empty >= 0)
                        + " Tap a step for a note (drag up or down on it to change the note), or play the bass on the keys." + play + kKeep;
            break;
        }
        case keys:
            // The keys play the synth as you play them (no arp).
            setParam (engine.getSynth().params, "arpOn", 0.0f);
            engine.getStrip (RigEngine::synthStrip).mute = false;
            r.track = GrooveView::synth;
            r.message = "The keys play the synth. Pick a sound, then play. To keep a melody, record it on a loop layer: "
                        "on the Looper, press an empty layer (or key 1)." + kKeep;
            break;
        case vocoder:
            engine.vocoderOn = true;
            engine.getStrip (RigEngine::vocoderStrip).mute = false;
            r.track = GrooveView::vocoder;
            r.message = "The keys play the vocoder: switch the mic on, hold a chord and sing." + kKeep;
            break;
        case loop:
            engine.getStrip (RigEngine::loopsStrip).mute = false;
            r.page = "Looper";
            r.message = "Press an empty layer (or keys 1-8) to record: it starts at the next bar, and loops when you press it again."
                        + kKeep;
            break;
        case sample:
            engine.getStrip (RigEngine::samplerStrip).mute = false;
            r.page = "Sampler";
            r.message = "Press Sample to record from the next bar, then play it across the keys." + kKeep;
            break;
        default:
            r.page = {};
            break;
    }
    engine.controlsMoved = true;
    return r;
}
}
