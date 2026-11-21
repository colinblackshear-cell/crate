#include "DroneProcessor.h"

using namespace crate;

namespace
{
float get (const juce::AudioProcessorValueTreeState& params, const char* id)
{
    return params.getRawParameterValue (id)->load();
}

void set (juce::AudioProcessorValueTreeState& params, const char* id, float value)
{
    if (auto* p = params.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

struct Preset
{
    const char* name;
    int first, octave, sync;
    float pace, jawari, decay, width, reeds, strings;
};

const Preset presets[] = {
    { "Classic (Pa)",        0,  0, 0, 1.10f, 0.60f, 0.50f, 0.70f, 0.00f, 1.0f },
    { "Evening (Ma)",        1,  0, 0, 1.30f, 0.55f, 0.60f, 0.70f, 0.00f, 1.0f },
    { "Yearning (Ni)",       2,  0, 0, 1.20f, 0.65f, 0.55f, 0.75f, 0.00f, 1.0f },
    { "Deep and slow",       0, -1, 0, 2.20f, 0.70f, 0.90f, 0.85f, 0.15f, 1.0f },
    { "Bright jawari",       0,  0, 0, 0.90f, 1.00f, 0.45f, 0.60f, 0.00f, 1.0f },
    { "Plain strings",       0,  0, 0, 1.10f, 0.10f, 0.50f, 0.70f, 0.00f, 1.0f },
    { "On the beat",         0,  0, 1, 1.00f, 0.60f, 0.40f, 0.70f, 0.00f, 1.0f },
    { "Shruti box",          0,  0, 0, 1.10f, 0.50f, 0.50f, 0.60f, 1.00f, 0.0f },
    { "Strings and reeds",   0,  0, 0, 1.40f, 0.60f, 0.65f, 0.80f, 0.45f, 1.0f },
    { "High shimmer",        3,  1, 0, 0.70f, 0.85f, 0.50f, 0.90f, 0.00f, 1.0f },
};
}

juce::StringArray DroneProcessor::feedNames()
{
    return { "Mic / line in", "Vocoder", "Bass", "Synth", "Pads", "Sampler", "Drums", "Everything" };
}

juce::StringArray DroneProcessor::rootNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

juce::StringArray DroneProcessor::presetNames()
{
    juce::StringArray names;
    for (const auto& p : presets)
        names.add (p.name);
    return names;
}

void DroneProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) std::size (presets)))
        return;
    const auto& p = presets[index];
    // (The note, On, and what the strings ring to stay as they are: those belong to the song.)
    set (params, "droneFirst", (float) p.first);
    set (params, "droneOctave", (float) (p.octave + 1));
    set (params, "droneSync", (float) p.sync);
    set (params, "dronePace", p.pace);
    set (params, "droneJawari", p.jawari);
    set (params, "droneDecay", p.decay);
    set (params, "droneWidth", p.width);
    set (params, "droneReeds", p.reeds);
    set (params, "droneStrings", p.strings);
}

juce::AudioProcessorValueTreeState::ParameterLayout DroneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto percent = [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; };
    auto number = [&layout] (const char* id, const char* name, NormalisableRange<float> range, float def, std::function<String (float, int)> text)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
    };
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "droneOn", 1 }, "On", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneRoot", 1 }, "Note", rootNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneOctave", 1 }, "Octave", StringArray { "Low", "Middle", "High" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneFirst", 1 }, "First string", StringArray { "Pa (fifth)", "Ma (fourth)", "Ni (seventh)", "Sa (same note)" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneSync", 1 }, "Pluck", StringArray { "Own time", "Every beat", "Every 2 beats", "Every bar" }, 0));
    number ("dronePace", "Pace", { 0.3f, 4.0f, 0.0f, 0.5f }, 1.1f, [] (float v, int) { return String (v, 2) + " s"; });
    number ("droneJawari", "Jawari", { 0.0f, 1.0f }, 0.6f, percent);
    number ("droneDecay", "Ring", { 0.0f, 1.0f }, 0.5f, percent);
    number ("droneFine", "Fine tune", { -50.0f, 50.0f }, 0.0f, [] (float v, int) { return String (roundToInt (v)) + " cents"; });
    number ("droneWidth", "Width", { 0.0f, 1.0f }, 0.7f, percent);
    number ("droneStrings", "Strings", { 0.0f, 1.0f }, 1.0f, percent);
    number ("droneReeds", "Reeds", { 0.0f, 1.0f }, 0.0f, percent);
    number ("droneResonate", "Resonate", { 0.0f, 1.0f }, 0.0f, percent);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneFeed", 1 }, "Rings to", feedNames(), 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "droneFollow", 1 }, "Follow keys", false));
    return layout;
}

DroneProcessor::DroneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "DroneState", createLayout())
{
}

int DroneProcessor::getFeed() const { return juce::jlimit (0, (int) numFeeds - 1, juce::roundToInt (get (params, "droneFeed"))); }
bool DroneProcessor::isOn() const { return get (params, "droneOn") > 0.5f; }
bool DroneProcessor::wantsToPlay() const { return isOn() || get (params, "droneResonate") > 0.0f || engine.isSounding(); }

drone::Settings DroneProcessor::readSettings() const
{
    drone::Settings s;
    s.on = isOn();
    s.root = juce::roundToInt (get (params, "droneRoot"));
    s.octave = juce::roundToInt (get (params, "droneOctave")) - 1;
    s.first = juce::roundToInt (get (params, "droneFirst"));
    s.sync = juce::roundToInt (get (params, "droneSync"));
    s.paceSeconds = get (params, "dronePace");
    s.jawari = get (params, "droneJawari");
    s.decay = get (params, "droneDecay");
    s.fineCents = get (params, "droneFine");
    s.width = get (params, "droneWidth");
    s.strings = get (params, "droneStrings");
    s.reeds = get (params, "droneReeds");
    s.resonate = get (params, "droneResonate");
    return s;
}

juce::StringArray DroneProcessor::stringNotes() const
{
    const auto s = readSettings();
    juce::StringArray notes;
    for (int i = 0; i < drone::kStrings; ++i)
    {
        const double hz = drone::rootHz (s.root, s.octave, 0.0f) * drone::stringRatio (i, s.first);
        const int note = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
        notes.add (juce::MidiMessage::getMidiNoteName (note, true, true, 3));
    }
    return notes;
}

void DroneProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    limiter.prepare (sampleRate);
}

bool DroneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo();
}

void DroneProcessor::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* feed, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || numSamples == 0)
        return;

    // Follow keys: the last note played is the drone's note.
    if (get (params, "droneFollow") > 0.5f)
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
                if (auto* p = params.getParameter ("droneRoot"))
                {
                    const float v = p->convertTo0to1 ((float) (meta.getMessage().getNoteNumber() % 12));
                    if (std::abs (p->getValue() - v) > 1.0e-4f)
                        p->setValueNotifyingHost (v);
                }
    midi.clear();

    drone::Clock clock;
    if (auto* head = getPlayHead())
        if (const auto info = head->getPosition())
        {
            clock.playing = info->getIsPlaying();
            clock.ppq = info->getPpqPosition().orFallback (0.0);
            clock.bpm = info->getBpm().orFallback (120.0);
            if (const auto sig = info->getTimeSignature())
                clock.beatsPerBar = sig->numerator * 4.0 / juce::jmax (1, sig->denominator);
        }

    const bool fed = feed != nullptr && feed->getNumChannels() > 0 && feed->getNumSamples() >= numSamples;
    engine.process (buffer.getWritePointer (0), buffer.getWritePointer (1),
                    fed ? feed->getReadPointer (0) : nullptr,
                    fed ? feed->getReadPointer (juce::jmin (1, feed->getNumChannels() - 1)) : nullptr,
                    numSamples, readSettings(), clock);
    limiter.process (buffer.getWritePointer (0), buffer.getWritePointer (1), numSamples);
}

void DroneProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = params.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void DroneProcessor::setStateInformation (const void* data, int size)
{
    if (const auto xml = getXmlFromBinary (data, size))
    {
        const auto state = juce::ValueTree::fromXml (*xml);
        if (state.hasType (params.state.getType()))
            params.replaceState (state);
    }
}
