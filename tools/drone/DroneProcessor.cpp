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
    for (const auto& p : drone::presets())
        names.add (p.name);
    return names;
}

void DroneProcessor::loadPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) drone::presets().size()))
        return;
    const auto& p = drone::presets()[(size_t) index];
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
    set (params, "droneVoice", (float) p.voice);
    set (params, "droneOrder", (float) p.order);
    set (params, "droneHuman", p.human);
    set (params, "droneBright", p.bright);
    set (params, "droneBody", p.body);
    set (params, "droneDetune", p.detune);
    set (params, "droneSpace", p.space);
    // (A ready-made drone plays its own order: your steps stay drawn, for when you pick them again.)
    set (params, "droneStepCount", 0.0f);
    set (params, "droneSwing", 0.0f);
}

void DroneProcessor::setStepCount (int choice)
{
    bool empty = true;
    for (auto& v : steps)
        empty = empty && v.load() == 0;
    if (choice > 0 && empty)
    {
        const auto first = drone::starterSteps();
        for (int i = 0; i < 16; ++i)
            setStep (i, first[(size_t) i]);
    }
    if (auto* p = params.getParameter ("droneStepCount"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, 2, choice)));
        p->endChangeGesture();
    }
}

int DroneProcessor::getStep (int step) const
{
    return juce::isPositiveAndBelow (step, 16) ? (int) steps[(size_t) step].load() : 0;
}

void DroneProcessor::setStep (int step, int mask)
{
    if (! juce::isPositiveAndBelow (step, 16))
        return;
    steps[(size_t) step].store ((std::uint8_t) (mask & 0x0f));
    juce::String text;
    for (auto& v : steps)
        text << juce::String::toHexString ((int) v.load());
    params.state.setProperty ("steps", text, nullptr);
}

void DroneProcessor::clearSteps()
{
    for (int i = 0; i < 16; ++i)
        setStep (i, 0);
}

void DroneProcessor::stepsFromState()
{
    const auto text = params.state.getProperty ("steps", {}).toString();
    for (int i = 0; i < 16; ++i)
        steps[(size_t) i].store ((std::uint8_t) (i < text.length() ? juce::CharacterFunctions::getHexDigitValue (text[i]) & 0x0f : 0));
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
    // The pattern and the tone (added with the more-patterns update: shows from before get these defaults).
    StringArray orders, voices;
    for (int i = 0; i < drone::numOrders; ++i)
        orders.add (drone::orderName (i));
    for (int i = 0; i < drone::numVoices; ++i)
        voices.add (drone::voiceName (i));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneOrder", 1 }, "Order", orders, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneStepCount", 1 }, "Steps", StringArray { "Off", "8 steps", "16 steps" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneStepRate", 1 }, "Step length", StringArray { "1/4", "1/8", "1/16" }, 1));
    number ("droneSwing", "Swing", { 0.0f, 1.0f }, 0.0f, percent);
    number ("droneHuman", "Human", { 0.0f, 1.0f }, 0.0f, percent);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneVoice", 1 }, "Instrument", voices, 0));
    number ("droneBright", "Bright", { 0.0f, 1.0f }, 0.5f, percent);
    number ("droneBody", "Body", { 0.0f, 1.0f }, 0.5f, percent);
    number ("droneDetune", "Detune", { 0.0f, 1.0f }, 0.0f, percent);
    number ("droneSpace", "Space", { 0.0f, 1.0f }, 0.0f, percent);
    // Miracle tones (added later: shows from before get these defaults, the tone off).
    StringArray toneNames;
    for (const auto& tone : drone::tones())
        toneNames.add (drone::toneHzText (tone.hz) + " Hz " + tone.name);
    toneNames.add ("Your own");
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "droneToneOn", 1 }, "Tone on", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneTone", 1 }, "Miracle tone", toneNames, 5));
    number ("droneToneHz", "Your own Hz", { (float) drone::kOwnToneMin, (float) drone::kOwnToneMax, 0.0f, 0.3f }, 111.0f,
            [] (float v, int) { return drone::toneHzText (v) + " Hz"; });
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneToneFit", 1 }, "Tone fit", StringArray { "Over the drone", "Tune drone to it" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneToneSound", 1 }, "Tone sound", StringArray { "Pure", "Warm", "Bowl" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "droneToneBeat", 1 }, "Binaural", StringArray { "Off", "2 Hz", "6 Hz", "10 Hz" }, 0));
    number ("droneToneLevel", "Tone level", { 0.0f, 1.0f }, 0.6f, percent);
    number ("droneToneSwell", "Tone swell", { 0.0f, 1.0f }, 0.3f, percent);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "droneToneOnly", 1 }, "Tone only", false));
    return layout;
}

DroneProcessor::DroneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "DroneState", createLayout())
{
}

int DroneProcessor::getTone() const { return juce::jlimit (0, drone::kNumTones - 1, juce::roundToInt (get (params, "droneTone"))); }
double DroneProcessor::getToneHz() const { return drone::toneHzFor (getTone(), get (params, "droneToneHz")); }
bool DroneProcessor::isToneOn() const { return get (params, "droneToneOn") > 0.5f; }

void DroneProcessor::pickTone (int index)
{
    index = juce::jlimit (0, drone::kNumTones - 1, index);
    // The tone that's playing again: off. Another: it plays (and the drone comes on, so you hear it).
    const bool same = isToneOn() && index == getTone();
    set (params, "droneTone", (float) index);
    set (params, "droneToneOn", same ? 0.0f : 1.0f);
    if (! same && ! isOn())
        set (params, "droneOn", 1.0f);
}

void DroneProcessor::setOwnToneHz (double hz)
{
    set (params, "droneToneHz", (float) juce::jlimit (drone::kOwnToneMin, drone::kOwnToneMax, hz));
    set (params, "droneTone", (float) drone::kOwnTone);
    set (params, "droneToneOn", 1.0f);
    if (! isOn())
        set (params, "droneOn", 1.0f);
}

int DroneProcessor::getFeed() const { return juce::jlimit (0, (int) numFeeds - 1, juce::roundToInt (get (params, "droneFeed"))); }
bool DroneProcessor::isOn() const { return get (params, "droneOn") > 0.5f; }
// (Follow keys counts: it has to hear the keys to take its note from them, even while off.)
bool DroneProcessor::wantsToPlay() const { return isOn() || get (params, "droneResonate") > 0.0f || get (params, "droneFollow") > 0.5f || engine.isSounding(); }

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
    s.order = juce::roundToInt (get (params, "droneOrder"));
    const int count = juce::roundToInt (get (params, "droneStepCount"));
    s.stepCount = count == 1 ? 8 : count == 2 ? 16 : 0;
    const int rate = juce::roundToInt (get (params, "droneStepRate"));
    s.stepBeats = rate == 0 ? 1.0f : rate == 2 ? 0.25f : 0.5f;
    for (int i = 0; i < 16; ++i)
        s.steps[(size_t) i] = steps[(size_t) i].load();
    s.swing = get (params, "droneSwing");
    s.human = get (params, "droneHuman");
    s.voice = juce::roundToInt (get (params, "droneVoice"));
    s.bright = get (params, "droneBright");
    s.body = get (params, "droneBody");
    s.detune = get (params, "droneDetune");
    s.space = get (params, "droneSpace");
    s.toneOn = get (params, "droneToneOn") > 0.5f;
    s.toneHz = getToneHz();
    s.toneFit = juce::roundToInt (get (params, "droneToneFit"));
    s.toneSound = juce::roundToInt (get (params, "droneToneSound"));
    s.toneBeat = drone::toneBeatHz (juce::roundToInt (get (params, "droneToneBeat")));
    s.toneLevel = get (params, "droneToneLevel");
    s.toneSwell = get (params, "droneToneSwell");
    s.toneOnly = get (params, "droneToneOnly") > 0.5f;
    return s;
}

juce::StringArray DroneProcessor::stringNotes() const
{
    const auto s = readSettings();
    juce::StringArray notes;
    for (int i = 0; i < drone::kStrings; ++i)
    {
        const double sa = s.toneOn && s.toneFit == 1 ? drone::droneSa (s) : drone::rootHz (s.root, s.octave, 0.0f);   // (tuned to the tone, or the note)
        const double hz = sa * drone::stringRatio (i, s.first);
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
    // A bed under the song: 6 dB under the Bass's and the Synth's sounds (at their level it sat on
    // top of everything).
    buffer.applyGain (kBedGain);
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
        {
            params.replaceState (state);
            stepsFromState();
        }
    }
}
