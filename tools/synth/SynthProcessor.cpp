#include "SynthProcessor.h"
#include "SynthView.h"

using namespace crate;

namespace
{
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (std::sqrt (lo * hi));
    return r;
}

// How each kind of setting reads under its knob: 400 Hz, 1.2 kHz, 300 ms, 60%.
juce::String formatValue (float v, const juce::String& unit)
{
    if (unit == "Hz")
        return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
    if (unit == "s")
        return v >= 1.0f ? juce::String (v, 2) + " s" : juce::String (juce::roundToInt (v * 1000.0f)) + " ms";
    return juce::String (juce::roundToInt (v * 100.0f)) + "%";
}

float get (const juce::AudioProcessorValueTreeState& p, const char* id)
{
    return p.getRawParameterValue (id)->load();
}
}

juce::AudioProcessorValueTreeState::ParameterLayout SynthProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };
    auto number = [&] (const char* id, const char* name, NormalisableRange<float> range, float def, const char* unit = "")
    {
        const String u (unit);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (
                                                               [u] (float v, int) { return formatValue (v, u); })));
    };
    auto integer = [&] (const char* id, const char* name, int lo, int hi, int def)
    {
        layout.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def));
    };

    choice ("wave", "Wave", { "Saw", "Square", "Triangle", "Supersaw" }, 3);
    number ("detune", "Detune", { 0.0f, 1.0f }, 0.4f);
    integer ("octave", "Octave", -2, 2, 0);
    number ("cutoff", "Cutoff", logRange (60.0f, 12000.0f), 2000.0f, "Hz");
    number ("resonance", "Resonance", { 0.0f, 1.0f }, 0.3f);
    number ("envMod", "Env mod", { 0.0f, 1.0f }, 0.4f);
    number ("filterDecay", "Filter decay", logRange (0.02f, 3.0f), 0.4f, "s");
    number ("attack", "Attack", logRange (0.001f, 3.0f), 0.005f, "s");
    number ("decay", "Decay", logRange (0.01f, 3.0f), 0.3f, "s");
    number ("sustain", "Sustain", { 0.0f, 1.0f }, 0.7f);
    number ("release", "Release", logRange (0.01f, 5.0f), 0.3f, "s");
    number ("lfoDepth", "LFO depth", { 0.0f, 1.0f }, 0.0f);
    choice ("lfoRate", "LFO rate", synth::SyncRates::names(), 5);
    choice ("lfoShape", "LFO shape", { "Sine", "Triangle", "Saw", "Square", "Random" }, 0);
    number ("volume", "Volume", { 0.0f, 1.0f }, 0.7f);

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "arpOn", 1 }, "Arp", false));
    choice ("arpMode", "Arp mode", { "Up", "Down", "Up and down", "Random", "As played" }, 0);
    choice ("arpRate", "Arp rate", synth::SyncRates::names(), 6);
    integer ("arpOctaves", "Arp octaves", 1, 3, 1);
    number ("arpGate", "Arp gate", { 0.05f, 1.0f }, 0.5f);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "latch", 1 }, "Latch", false));
    return layout;
}

SynthProcessor::SynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "SynthState", createLayout())
{
}

synthtool::Rhythm SynthProcessor::getRhythm() const
{
    const juce::SpinLock::ScopedLockType sl (rhythmLock);
    return rhythm;
}

void SynthProcessor::setRhythm (const synthtool::Rhythm& r)
{
    const juce::SpinLock::ScopedLockType sl (rhythmLock);
    rhythm = r;
}

synthtool::Settings SynthProcessor::readSettings() const
{
    synthtool::Settings s;
    s.wave = (synth::Wave) (int) get (params, "wave");
    s.detune = get (params, "detune");
    s.octave = (int) get (params, "octave");
    s.cutoff = get (params, "cutoff");
    s.resonance = get (params, "resonance");
    s.envMod = get (params, "envMod");
    s.filterDecay = get (params, "filterDecay");
    s.attack = get (params, "attack");
    s.decay = get (params, "decay");
    s.sustain = get (params, "sustain");
    s.release = get (params, "release");
    s.lfoDepth = get (params, "lfoDepth");
    s.lfoRate = (int) get (params, "lfoRate");
    s.lfoShape = (synth::LfoShape) (int) get (params, "lfoShape");
    s.volume = get (params, "volume");
    s.arpOn = get (params, "arpOn") > 0.5f;
    s.arpMode = (synthtool::ArpMode) (int) get (params, "arpMode");
    s.arpRate = (int) get (params, "arpRate");
    s.arpOctaves = (int) get (params, "arpOctaves");
    s.arpGate = get (params, "arpGate");
    s.latch = get (params, "latch") > 0.5f;
    return s;
}

void SynthProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keyboardState.reset();
}

bool SynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void SynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    synthtool::Clock clock;
    if (auto* head = getPlayHead())
        if (const auto info = head->getPosition())
        {
            clock.playing = info->getIsPlaying();
            clock.ppq = info->getPpqPosition().orFallback (0.0);
            clock.bpm = info->getBpm().orFallback (120.0);
        }

    {
        const juce::SpinLock::ScopedTryLockType sl (rhythmLock);
        if (sl.isLocked())
            engine.setRhythm (rhythm);
    }
    engine.setSettings (readSettings());
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.render (buffer.getWritePointer (0), right, numSamples, midi, clock);
    arpStep = engine.getArpStep();
    midi.clear();
}

juce::AudioProcessorEditor* SynthProcessor::createEditor() { return new SynthView (*this); }

void SynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.removeChild (state.getChildWithName ("Rhythm"), nullptr);
    const auto r = getRhythm();
    juce::ValueTree t ("Rhythm");
    juce::String on, accent;
    for (int i = 0; i < synthtool::kRhythmSteps; ++i)
    {
        on << (r.on[(size_t) i] ? "1" : "0");
        accent << (r.accent[(size_t) i] ? "1" : "0");
    }
    t.setProperty ("on", on, nullptr);
    t.setProperty ("accent", accent, nullptr);
    t.setProperty ("length", r.length, nullptr);
    state.appendChild (t, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void SynthProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType()))
        return;

    const auto t = state.getChildWithName ("Rhythm");
    if (t.isValid())
    {
        synthtool::Rhythm r;
        const auto on = t.getProperty ("on").toString();
        const auto accent = t.getProperty ("accent").toString();
        for (int i = 0; i < synthtool::kRhythmSteps; ++i)
        {
            r.on[(size_t) i] = i < on.length() ? on[i] == '1' : true;
            r.accent[(size_t) i] = i < accent.length() && accent[i] == '1';
        }
        r.length = juce::jlimit (1, synthtool::kRhythmSteps, (int) t.getProperty ("length", synthtool::kRhythmSteps));
        setRhythm (r);
        state.removeChild (t, nullptr);
    }
    params.replaceState (state);
}
