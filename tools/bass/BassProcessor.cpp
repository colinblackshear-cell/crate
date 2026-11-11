#include "BassProcessor.h"
#include "BassView.h"

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

juce::AudioProcessorValueTreeState::ParameterLayout BassProcessor::createLayout()
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
    auto toggle = [&] (const char* id, const char* name, bool def)
    {
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    choice ("pattern", "Pattern", { "1", "2", "3", "4", "5", "6", "7", "8" }, 0);
    toggle ("sequencer", "Sequencer", true);
    toggle ("keysTranspose", "Keys transpose", true);
    choice ("wave", "Wave", { "Saw", "Square", "Triangle" }, 0);
    number ("cutoff", "Cutoff", logRange (40.0f, 8000.0f), 400.0f, "Hz");
    number ("resonance", "Resonance", { 0.0f, 1.0f }, 0.6f);
    number ("envMod", "Env mod", { 0.0f, 1.0f }, 0.6f);
    number ("decay", "Decay", logRange (0.03f, 2.0f), 0.3f, "s");
    number ("accent", "Accent", { 0.0f, 1.0f }, 0.6f);
    number ("glide", "Glide", logRange (0.005f, 0.4f), 0.06f, "s");
    number ("drive", "Drive", { 0.0f, 1.0f }, 0.3f);
    number ("sub", "Sub", { 0.0f, 1.0f }, 0.4f);
    number ("lfoDepth", "LFO depth", { 0.0f, 1.0f }, 0.0f);
    choice ("lfoRate", "LFO rate", synth::SyncRates::names(), 4);
    choice ("lfoShape", "LFO shape", { "Sine", "Triangle", "Saw", "Square", "Random" }, 0);
    choice ("stepRate", "Step rate", synth::SyncRates::names(), 6);
    number ("gate", "Gate", { 0.1f, 1.0f }, 0.55f);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "root", 1 }, "Root note", 24, 60, 36));
    number ("volume", "Volume", { 0.0f, 1.0f }, 0.8f);
    // The deeper settings (added later; their defaults leave the sound as it was).
    number ("pulseWidth", "Pulse width", { 0.05f, 0.5f }, 0.5f);
    number ("fat", "Fat", { 0.0f, 1.0f }, 0.0f);
    choice ("filterType", "Filter type", { "Low 24", "Low 12", "Band", "High" }, 0);
    number ("keyTrack", "Key track", { 0.0f, 1.0f }, 0.0f);
    number ("attack", "Attack", logRange (0.001f, 1.0f), 0.003f, "s");
    number ("release", "Release", logRange (0.005f, 2.0f), 0.012f, "s");
    choice ("lfoDest", "LFO to", { "Filter", "Pitch", "Volume" }, 0);
    return layout;
}

BassProcessor::BassProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "BassState", createLayout())
{
    // Pattern 1 starts as a simple line so there's something to hear straight away.
    auto& p = patterns[0];
    const int notes[] = { 0, 0, 12, 0, 0, 10, 0, 7, 0, 0, 12, 0, 3, 0, 5, 7 };
    for (int i = 0; i < bass::kSteps; ++i)
    {
        p.steps[(size_t) i].on = i % 4 != 1;
        p.steps[(size_t) i].note = notes[i];
        p.steps[(size_t) i].accent = i % 4 == 0;
        p.steps[(size_t) i].slide = i == 6 || i == 14;
    }
}

bass::Pattern BassProcessor::getPattern (int index) const
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    return patterns[(size_t) juce::jlimit (0, bass::kPatterns - 1, index)];
}

void BassProcessor::setPattern (int index, const bass::Pattern& p)
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    patterns[(size_t) juce::jlimit (0, bass::kPatterns - 1, index)] = p;
}

int BassProcessor::getSelectedPattern() const { return (int) get (params, "pattern"); }

void BassProcessor::selectPattern (int index)
{
    if (auto* p = params.getParameter ("pattern"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, bass::kPatterns - 1, index)));
}

bass::Pattern BassProcessor::randomPattern (juce::Random& random, int scale)
{
    // Minor pentatonic plus octaves: it's hard to make a bad bass line from these.
    static const int pentatonic[] = { 0, 3, 5, 7, 10, 12 };
    static const int minor[] = { 0, 2, 3, 5, 7, 8, 10, 12 };
    bass::Pattern p;
    for (auto& s : p.steps)
    {
        s.on = random.nextFloat() < 0.7f;
        s.note = scale == 0 ? pentatonic[random.nextInt (6)] : minor[random.nextInt (8)];
        if (random.nextFloat() < 0.15f)
            s.note += 12;
        s.accent = random.nextFloat() < 0.3f;
        s.slide = random.nextFloat() < 0.2f;
    }
    p.steps[0].on = true;
    p.steps[0].note = 0;
    p.steps[0].accent = true;
    return p;
}

bass::Settings BassProcessor::readSettings() const
{
    bass::Settings s;
    s.wave = (synth::Wave) (int) get (params, "wave");
    s.cutoff = get (params, "cutoff");
    s.resonance = get (params, "resonance");
    s.envMod = get (params, "envMod");
    s.decay = get (params, "decay");
    s.accent = get (params, "accent");
    s.glide = get (params, "glide");
    s.drive = get (params, "drive");
    s.sub = get (params, "sub");
    s.lfoDepth = get (params, "lfoDepth");
    s.lfoRate = (int) get (params, "lfoRate");
    s.lfoShape = (synth::LfoShape) (int) get (params, "lfoShape");
    s.stepRate = (int) get (params, "stepRate");
    s.gate = get (params, "gate");
    s.root = (int) get (params, "root");
    s.volume = get (params, "volume");
    s.pulseWidth = get (params, "pulseWidth");
    s.fat = get (params, "fat");
    s.filterType = (synth::FilterType) (int) get (params, "filterType");
    s.keyTrack = get (params, "keyTrack");
    s.attack = get (params, "attack");
    s.release = get (params, "release");
    s.lfoDest = (int) get (params, "lfoDest");
    s.keysTranspose = get (params, "keysTranspose") > 0.5f;
    s.sequencerOn = get (params, "sequencer") > 0.5f;
    return s;
}

void BassProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keyboardState.reset();
    playing = getPattern (getSelectedPattern());
    playingPattern = getSelectedPattern();
    engine.setPattern (&playing);
}

bool BassProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void BassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    bass::Clock clock;
    if (auto* head = getPlayHead())
        if (const auto info = head->getPosition())
        {
            clock.playing = info->getIsPlaying();
            clock.ppq = info->getPpqPosition().orFallback (0.0);
            clock.bpm = info->getBpm().orFallback (120.0);
        }

    // A new pattern starts at the next bar (or straight away when stopped), so switching
    // live stays in time.
    const int wanted = getSelectedPattern();
    bool switchNow = ! clock.playing || lastPpq < 0;
    if (clock.playing && ! switchNow)
    {
        const double blockBeats = numSamples * clock.bpm / (60.0 * getSampleRate());
        switchNow = std::floor (clock.ppq / 4.0) != std::floor ((clock.ppq + blockBeats) / 4.0)
                    || std::fmod (clock.ppq, 4.0) < 1.0e-6;
    }
    {
        const juce::SpinLock::ScopedTryLockType sl (patternLock);
        if (sl.isLocked())
        {
            if (switchNow && wanted != playingPattern.load())
                playingPattern = wanted;
            playing = patterns[(size_t) playingPattern.load()];
        }
    }
    lastPpq = clock.playing ? clock.ppq : -1;

    engine.setSettings (readSettings());
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.render (buffer.getWritePointer (0), right, numSamples, midi, clock);
    currentStep = engine.getCurrentStep();
    transpose = engine.getTranspose();
    midi.clear();
}

juce::AudioProcessorEditor* BassProcessor::createEditor() { return new BassView (*this); }

//==============================================================================
juce::ValueTree BassProcessor::patternToTree (const bass::Pattern& p)
{
    juce::ValueTree t ("Pattern");
    t.setProperty ("length", p.length, nullptr);
    juce::String steps;
    for (const auto& s : p.steps)
        steps << (s.on ? 1 : 0) << ":" << s.note << ":" << (s.accent ? 1 : 0) << ":" << (s.slide ? 1 : 0) << " ";
    t.setProperty ("steps", steps.trim(), nullptr);
    return t;
}

bass::Pattern BassProcessor::patternFromTree (const juce::ValueTree& t)
{
    bass::Pattern p;
    p.length = juce::jlimit (1, bass::kSteps, (int) t.getProperty ("length", bass::kSteps));
    const auto steps = juce::StringArray::fromTokens (t.getProperty ("steps").toString(), " ", {});
    for (int i = 0; i < juce::jmin (bass::kSteps, steps.size()); ++i)
    {
        const auto f = juce::StringArray::fromTokens (steps[i], ":", {});
        auto& s = p.steps[(size_t) i];
        s.on = f[0].getIntValue() != 0;
        s.note = juce::jlimit (-12, 24, f[1].getIntValue());
        s.accent = f[2].getIntValue() != 0;
        s.slide = f[3].getIntValue() != 0;
    }
    return p;
}

void BassProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.removeChild (state.getChildWithName ("Patterns"), nullptr);
    juce::ValueTree list ("Patterns");
    for (int i = 0; i < bass::kPatterns; ++i)
        list.appendChild (patternToTree (getPattern (i)), nullptr);
    state.appendChild (list, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void BassProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType()))
        return;

    const auto list = state.getChildWithName ("Patterns");
    for (int i = 0; i < juce::jmin (bass::kPatterns, list.getNumChildren()); ++i)
        setPattern (i, patternFromTree (list.getChild (i)));
    state.removeChild (list, nullptr);
    params.replaceState (state);
}
