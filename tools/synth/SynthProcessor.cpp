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

    // The deeper sound settings (added later; their defaults leave the sound as it was).
    number ("pulseWidth", "Pulse width", { 0.05f, 0.5f }, 0.5f);
    choice ("osc2Wave", "Osc 2", { "Off", "Saw", "Square", "Triangle", "Sine" }, 0);
    integer ("osc2Semi", "Osc 2 tune", -24, 24, 0);
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "osc2Fine", 1 }, "Osc 2 fine", NormalisableRange<float> (-50.0f, 50.0f), 7.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; })));
    number ("osc2Level", "Osc 2 level", { 0.0f, 1.0f }, 0.5f);
    number ("subLevel", "Sub", { 0.0f, 1.0f }, 0.0f);
    number ("noiseLevel", "Noise", { 0.0f, 1.0f }, 0.0f);
    choice ("filterType", "Filter type", { "Low 24", "Low 12", "Band", "High" }, 0);
    number ("filterAttack", "Filter attack", logRange (0.001f, 3.0f), 0.002f, "s");
    number ("filterSustain", "Filter sustain", { 0.0f, 1.0f }, 0.0f);
    number ("keyTrack", "Key track", { 0.0f, 1.0f }, 0.0f);
    number ("velFilter", "Velocity", { 0.0f, 1.0f }, 0.0f);
    choice ("voiceMode", "Voices", { "Poly", "Mono", "Legato" }, 0);
    {
        NormalisableRange<float> glide (0.0f, 1.5f);
        glide.setSkewForCentre (0.15f);
        number ("glide", "Glide", glide, 0.0f, "s");
    }
    choice ("lfoDest", "LFO to", { "Filter", "Pitch", "Volume", "Pulse width" }, 0);
    number ("drive", "Drive", { 0.0f, 1.0f }, 0.0f);
    number ("width", "Width", { 0.0f, 1.0f }, 0.0f);

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "arpOn", 1 }, "Arp", false));
    choice ("arpMode", "Arp mode", { "Up", "Down", "Up and down", "Random", "As played", "Chord", "Converge", "Pinky" }, 0);
    // Slowest first, so the knob turns down to slow. (Replaces the old "arpRate"; old shows
    // are moved over as they load.)
    choice ("arpSpeed", "Arp speed", synth::ArpRates::names(), synth::ArpRates::sixteenth);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "arpSync", 1 }, "Arp sync", true));
    {
        NormalisableRange<float> hz (0.1f, 30.0f);
        hz.setSkewForCentre (4.0f);
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "arpHz", 1 }, "Arp free rate", hz, 8.0f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (
                                                               [] (float v, int) { return juce::String (v, v < 10.0f ? 1 : 0) + " Hz"; })));
    }
    integer ("arpOctaves", "Arp octaves", 1, 4, 1);
    number ("arpGate", "Arp gate", { 0.05f, 1.0f }, 0.5f);
    number ("arpSwing", "Arp swing", { 0.0f, 1.0f }, 0.0f);
    integer ("arpRepeats", "Arp repeats", 1, 4, 1);
    number ("arpChance", "Arp chance", { 0.0f, 1.0f }, 1.0f);
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
    s.pulseWidth = get (params, "pulseWidth");
    s.osc2Wave = (synthtool::Osc2Wave) (int) get (params, "osc2Wave");
    s.osc2Semi = (int) get (params, "osc2Semi");
    s.osc2Fine = get (params, "osc2Fine");
    s.osc2Level = get (params, "osc2Level");
    s.subLevel = get (params, "subLevel");
    s.noiseLevel = get (params, "noiseLevel");
    s.filterType = (synth::FilterType) (int) get (params, "filterType");
    s.filterAttack = get (params, "filterAttack");
    s.filterSustain = get (params, "filterSustain");
    s.keyTrack = get (params, "keyTrack");
    s.velFilter = get (params, "velFilter");
    s.voiceMode = (synthtool::VoiceMode) (int) get (params, "voiceMode");
    s.glide = get (params, "glide");
    s.lfoDest = (synthtool::LfoDest) (int) get (params, "lfoDest");
    s.drive = get (params, "drive");
    s.width = get (params, "width");
    s.arpOn = get (params, "arpOn") > 0.5f;
    s.arpMode = (synthtool::ArpMode) (int) get (params, "arpMode");
    s.arpSpeed = (int) get (params, "arpSpeed");
    s.arpSync = get (params, "arpSync") > 0.5f;
    s.arpHz = get (params, "arpHz");
    s.arpOctaves = (int) get (params, "arpOctaves");
    s.arpGate = get (params, "arpGate");
    s.arpSwing = get (params, "arpSwing");
    s.arpRepeats = (int) get (params, "arpRepeats");
    s.arpChance = get (params, "arpChance");
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
    // Shows from before the arp's speed knob kept "arpRate" (an older list of speeds): move it over.
    auto paramNamed = [&state] (const juce::String& id)
    {
        for (auto child : state)
            if (child.hasType ("PARAM") && child.getProperty ("id").toString() == id)
                return child;
        return juce::ValueTree();
    };
    if (auto old = paramNamed ("arpRate"); old.isValid())
    {
        if (! paramNamed ("arpSpeed").isValid())
        {
            juce::ValueTree speed ("PARAM");
            speed.setProperty ("id", "arpSpeed", nullptr);
            speed.setProperty ("value", synth::ArpRates::fromSyncRate (juce::roundToInt ((float) old.getProperty ("value"))), nullptr);
            state.appendChild (speed, nullptr);
        }
        state.removeChild (old, nullptr);
    }
    params.replaceState (state);
}
