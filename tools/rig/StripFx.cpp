#include "StripFx.h"

namespace
{
constexpr float kMaxDelaySeconds = 4.0f;
const double delayBeats[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
const double repeatBeats[] = { 0.0, 1.0, 0.5, 0.25, 0.125 };

// The filter knob: left of the middle sweeps a low-pass down from the top, right of it sweeps
// a high-pass up from the bottom.
float lowPassHz (float amount) { return 20000.0f * std::pow (60.0f / 20000.0f, amount); }
float highPassHz (float amount) { return 20.0f * std::pow (400.0f, amount); }

juce::String hz (float f)
{
    return f >= 1000.0f ? juce::String (f / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz";
}

juce::String filterText (float v, int)
{
    if (std::abs (v) < 0.02f)
        return "Off";
    return v < 0 ? "Low " + hz (lowPassHz (-v)) : "High " + hz (highPassHz (v));
}

juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

juce::AudioParameterFloatAttributes percentAttributes()
{
    return juce::AudioParameterFloatAttributes().withStringFromValueFunction (percent);
}
}

const juce::StringArray& StripFx::delayNames()
{
    static const juce::StringArray names { "1/16", "1/8", "1/8 dotted", "1/4", "1/4 dotted", "1/2" };
    return names;
}

const juce::StringArray& StripFx::repeatNames()
{
    static const juce::StringArray names { "Off", "1/4", "1/8", "1/16", "1/32" };
    return names;
}

const juce::StringArray& StripFx::ampNames()
{
    static const juce::StringArray names { "Clean", "Crunch", "Lead", "Fuzz", "Bass amp" };
    return names;
}

const juce::StringArray& StripFx::tremoloNames()
{
    static const juce::StringArray names { "1/4", "1/8", "1/8 triplet", "1/16" };
    return names;
}

juce::AudioProcessorValueTreeState::ParameterLayout StripFx::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "filter", 1 }, "Filter", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (filterText)));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "resonance", 1 }, "Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "drive", 1 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "crush", 1 }, "Crush", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "delayTime", 1 }, "Delay time", delayNames(), 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "feedback", 1 }, "Feedback", NormalisableRange<float> (0.0f, 0.95f), 0.45f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "delayMix", 1 }, "Delay", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbSize", 1 }, "Room size", NormalisableRange<float> (0.0f, 1.0f), 0.6f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverbMix", 1 }, "Reverb", NormalisableRange<float> (0.0f, 1.0f), 0.0f, percentAttributes()));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "repeat", 1 }, "Repeat", repeatNames(), 0));

    // The pedalboard's pedals (after the first ones, so learned knobs keep their numbers).
    const auto dbAttributes = AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
                                                                                           { return (v > 0.05f ? "+" : "") + juce::String (v, std::abs (v) >= 9.95f ? 0 : 1) + " dB"; });
    const auto hzAttributes = AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " Hz"; });
    auto onOff = [&layout] (const char* id, const char* name)
    { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, false)); };
    auto amount = [&layout] (const char* id, const char* name, float lo, float hi, float def, AudioParameterFloatAttributes a)
    { layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def, a)); };
    amount ("inputGain", "Gain in", -12.0f, 24.0f, 0.0f, dbAttributes);
    onOff ("bypass", "Bypass");
    onOff ("gateOn", "Gate");
    amount ("gateThreshold", "Gate threshold", -80.0f, -20.0f, -55.0f, dbAttributes);
    onOff ("compOn", "Comp");
    amount ("compAmount", "Squeeze", 0.0f, 1.0f, 0.5f, percentAttributes());
    amount ("compLevel", "Comp level", -12.0f, 12.0f, 0.0f, dbAttributes);
    onOff ("wahOn", "Wah");
    amount ("wahSense", "Sensitivity", 0.0f, 1.0f, 0.6f, percentAttributes());
    amount ("wahRange", "Range", 0.0f, 1.0f, 0.6f, percentAttributes());
    amount ("wahPeak", "Peak", 0.0f, 1.0f, 0.6f, percentAttributes());
    onOff ("ampOn", "Amp");
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ampType", 1 }, "Amp type", ampNames(), 1));
    amount ("ampGain", "Amp gain", 0.0f, 1.0f, 0.5f, percentAttributes());
    amount ("ampTone", "Tone", 0.0f, 1.0f, 0.5f, percentAttributes());
    amount ("ampLevel", "Amp level", 0.0f, 1.0f, 0.6f, percentAttributes());
    onOff ("eqOn", "EQ");
    amount ("eqLow", "Bass", -12.0f, 12.0f, 0.0f, dbAttributes);
    amount ("eqMid", "Middle", -12.0f, 12.0f, 0.0f, dbAttributes);
    amount ("eqHigh", "Treble", -12.0f, 12.0f, 0.0f, dbAttributes);
    onOff ("chorusOn", "Chorus");
    amount ("chorusMix", "Chorus mix", 0.0f, 1.0f, 0.5f, percentAttributes());
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "chorusRate", 1 }, "Chorus rate", NormalisableRange<float> (0.05f, 5.0f, 0.0f, 0.4f), 0.8f, hzAttributes));
    amount ("chorusDepth", "Chorus depth", 0.0f, 1.0f, 0.5f, percentAttributes());
    onOff ("phaserOn", "Phaser");
    amount ("phaserMix", "Phaser mix", 0.0f, 1.0f, 0.5f, percentAttributes());
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "phaserRate", 1 }, "Phaser rate", NormalisableRange<float> (0.05f, 5.0f, 0.0f, 0.4f), 0.4f, hzAttributes));
    amount ("phaserDepth", "Phaser depth", 0.0f, 1.0f, 0.7f, percentAttributes());
    amount ("phaserFeedback", "Phaser feedback", 0.0f, 0.9f, 0.4f, percentAttributes());
    onOff ("tremOn", "Tremolo");
    amount ("tremDepth", "Tremolo depth", 0.0f, 1.0f, 0.6f, percentAttributes());
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tremRate", 1 }, "Tremolo speed", tremoloNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tremShape", 1 }, "Tremolo shape", juce::StringArray { "Smooth", "Choppy" }, 0));
    return layout;
}

StripFx::StripFx()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "StripFx", createLayout())
{
    filter = params.getRawParameterValue ("filter");
    resonance = params.getRawParameterValue ("resonance");
    drive = params.getRawParameterValue ("drive");
    crush = params.getRawParameterValue ("crush");
    delayTime = params.getRawParameterValue ("delayTime");
    feedback = params.getRawParameterValue ("feedback");
    delayMix = params.getRawParameterValue ("delayMix");
    reverbSize = params.getRawParameterValue ("reverbSize");
    reverbMix = params.getRawParameterValue ("reverbMix");
    repeat = params.getRawParameterValue ("repeat");

    auto raw = [this] (const char* id) { return params.getRawParameterValue (id); };
    inputGain = raw ("inputGain");
    bypass = raw ("bypass");
    gateOn = raw ("gateOn");
    gateThreshold = raw ("gateThreshold");
    compOn = raw ("compOn");
    compAmount = raw ("compAmount");
    compLevel = raw ("compLevel");
    wahOn = raw ("wahOn");
    wahSense = raw ("wahSense");
    wahRange = raw ("wahRange");
    wahPeak = raw ("wahPeak");
    ampOn = raw ("ampOn");
    ampType = raw ("ampType");
    ampGain = raw ("ampGain");
    ampTone = raw ("ampTone");
    ampLevel = raw ("ampLevel");
    eqOn = raw ("eqOn");
    eqLow = raw ("eqLow");
    eqMid = raw ("eqMid");
    eqHigh = raw ("eqHigh");
    chorusOn = raw ("chorusOn");
    chorusMix = raw ("chorusMix");
    chorusRate = raw ("chorusRate");
    chorusDepth = raw ("chorusDepth");
    phaserOn = raw ("phaserOn");
    phaserMix = raw ("phaserMix");
    phaserRate = raw ("phaserRate");
    phaserDepth = raw ("phaserDepth");
    phaserFeedback = raw ("phaserFeedback");
    tremOn = raw ("tremOn");
    tremDepth = raw ("tremDepth");
    tremRate = raw ("tremRate");
    tremShape = raw ("tremShape");
}

void StripFx::readTuner (float* dest, int count) const
{
    count = juce::jlimit (0, kTunerSize, count);
    const int end = tunerWrite.load();
    for (int i = 0; i < count; ++i)
        dest[i] = tunerRing[(size_t) ((end - count + i + kTunerSize) % kTunerSize)];
}

void StripFx::setPlayHead (juce::AudioPlayHead* head)
{
    AudioProcessor::setPlayHead (head);
    plugin.setPlayHead (head);
}

bool StripFx::isActive() const
{
    if (bypass->load() > 0.5f)
        return false;
    return plugin.isLoaded() || std::abs (filter->load()) >= 0.02f || drive->load() > 0.01f || crush->load() > 0.01f
        || delayMix->load() > 0.01f || reverbMix->load() > 0.01f || repeat->load() > 0.5f
        || std::abs (inputGain->load()) > 0.05f || gateOn->load() > 0.5f || compOn->load() > 0.5f || wahOn->load() > 0.5f
        || ampOn->load() > 0.5f || eqOn->load() > 0.5f || chorusOn->load() > 0.5f || phaserOn->load() > 0.5f || tremOn->load() > 0.5f;
}

void StripFx::reset()
{
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
}

double StripFx::currentBpm() const
{
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto bpm = pos->getBpm())
                if (*bpm > 20.0)
                    return *bpm;
    return 120.0;
}

void StripFx::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    rate = sampleRate > 0 ? sampleRate : 44100.0;
    plugin.prepare (rate, samplesPerBlock);
    for (auto& s : svf)
        s.clear();
    filterSmooth.reset (rate, 0.03);
    filterSmooth.setCurrentAndTargetValue (filter->load());
    resonanceSmooth.reset (rate, 0.03);
    resonanceSmooth.setCurrentAndTargetValue (resonance->load());
    held = {};
    holdCount = 0;

    delayLine.setSize (2, (int) (rate * kMaxDelaySeconds) + 2);
    delayLine.clear();
    delayWrite = 0;
    delaySamples.reset (rate, 0.08);
    delaySamples.setCurrentAndTargetValue ((float) (delayBeats[(int) delayTime->load()] * 60.0 / currentBpm() * rate));
    feedbackLow = {};

    repeatBuffer.setSize (2, (int) (rate * kMaxDelaySeconds) + 2);
    repeatBuffer.clear();
    repeatChoice = 0;
    repeatFilled = false;
    repeatMix.reset (rate, 0.005);
    repeatMix.setCurrentAndTargetValue (0.0f);

    reverb.setSampleRate (rate);
    reverb.reset();
    wet.setSize (2, juce::jmax (1, samplesPerBlock));

    trimSmooth.reset (rate, 0.03);
    trimSmooth.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputGain->load()));
    gateEnv = compEnv = 0;
    gateGain = compGain = 1;
    wahEnv = 0;
    for (auto& w : wahSvf)
        w.clear();
    ampPre = ampTone1 = ampCab1 = ampCab2 = ampDc = {};
    for (auto* f : { &eqLowF, &eqMidF, &eqHighF })
        for (auto& side : *f)
            side.reset();
    eqWasOn = false;
    chorusLine.setSize (2, (int) (rate * 0.05) + 4);
    chorusLine.clear();
    chorusWrite = 0;
    phaserState = {};
    phaserLast = {};
    for (auto* s : { &chorusAmount, &phaserAmount, &tremAmount })
    {
        s->reset (rate, 0.02);
        s->setCurrentAndTargetValue (0.0f);
    }
}

double StripFx::currentPpq (bool& known) const
{
    known = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
            if (auto ppq = pos->getPpqPosition(); ppq && pos->getIsPlaying())
            {
                known = true;
                return *ppq;
            }
    return 0.0;
}

//==============================================================================
// Gate and Comp: the gate shuts out hum and hiss between notes (a guitar's buzz); the comp
// evens the notes out and makes them sustain.
void StripFx::processDynamics (float* left, float* right, int n)
{
    const bool gate = gateOn->load() > 0.5f, comp = compOn->load() > 0.5f;
    if (gate)
    {
        const float open = juce::Decibels::decibelsToGain (gateThreshold->load());
        const float close = open * 0.5f;   // a little hysteresis, so it doesn't chatter
        const float envRelease = std::exp (-1.0f / (0.05f * (float) rate));
        const float opening = 1.0f - std::exp (-1.0f / (0.002f * (float) rate));
        const float closing = 1.0f - std::exp (-1.0f / (0.08f * (float) rate));
        bool isOpen = gateGain > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
            gateEnv = level > gateEnv ? level : gateEnv * envRelease;
            if (gateEnv > open)
                isOpen = true;
            else if (gateEnv < close)
                isOpen = false;
            gateGain += ((isOpen ? 1.0f : 0.0f) - gateGain) * (isOpen ? opening : closing);
            left[i] *= gateGain;
            right[i] *= gateGain;
        }
        gateOpenNow = gateGain;
    }
    else
    {
        gateGain = 1.0f;
        gateOpenNow = 1.0f;
    }

    if (comp)
    {
        const float a = compAmount->load();
        const float thresholdDb = -8.0f - 30.0f * a;
        const float ratio = 2.0f + 8.0f * a;
        // Make up about half of what it takes off at the threshold, then the Level knob.
        const float makeup = juce::Decibels::decibelsToGain (-thresholdDb * (1.0f - 1.0f / ratio) * 0.5f + compLevel->load());
        const float attack = 1.0f - std::exp (-1.0f / (0.004f * (float) rate));
        const float release = 1.0f - std::exp (-1.0f / (0.15f * (float) rate));
        float most = 0;
        for (int i = 0; i < n; ++i)
        {
            const float level = juce::jmax (std::abs (left[i]), std::abs (right[i]));
            compEnv += (level - compEnv) * (level > compEnv ? attack : release);
            const float levelDb = juce::Decibels::gainToDecibels (compEnv, -100.0f);
            const float overDb = levelDb - thresholdDb;
            const float reductionDb = overDb > 0 ? overDb * (1.0f - 1.0f / ratio) : 0.0f;
            compGain = juce::Decibels::decibelsToGain (-reductionDb);
            most = juce::jmax (most, reductionDb);
            left[i] *= compGain * makeup;
            right[i] *= compGain * makeup;
        }
        compReductionNow = juce::jmin (1.0f, most / 18.0f);
    }
    else
    {
        compReductionNow = 0.0f;
    }
}

// Wah: an envelope filter. Play harder and the band-pass opens up ("quack"); Range is how
// far it sweeps, Peak how vocal it sounds.
void StripFx::processWah (float* left, float* right, int n)
{
    const float sense = wahSense->load(), range = wahRange->load(), peak = wahPeak->load();
    const float attack = 1.0f - std::exp (-1.0f / (0.003f * (float) rate));
    const float release = 1.0f - std::exp (-1.0f / (0.12f * (float) rate));
    const float low = 220.0f, high = juce::jmin ((float) rate * 0.4f, 600.0f + range * 3400.0f);
    const float k = 1.2f - 1.05f * peak;   // lower k: a sharper, more vocal peak
    const float drive = 2.0f + sense * 30.0f;
    for (int i = 0; i < n; ++i)
    {
        const float level = 0.5f * (std::abs (left[i]) + std::abs (right[i]));
        wahEnv += (level - wahEnv) * (level > wahEnv ? attack : release);
        const float sweep = juce::jlimit (0.0f, 1.0f, wahEnv * drive);
        const float fc = low * std::pow (high / low, sweep);
        const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) rate);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto* d = ch == 0 ? left : right;
            auto& s = wahSvf[(size_t) ch];
            const float v0 = d[i];
            const float v3 = v0 - s.ic2;
            const float v1 = a1 * s.ic1 + a2 * v3;
            const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;
            // Mostly the band-pass (brought up to the level it came in at), a little low end.
            d[i] = v1 * k * 1.6f + v2 * 0.35f;
        }
    }
}

// Amp: a guitar or bass amp and its speaker cabinet. Clean stays clear until you dig in,
// Crunch breaks up, Lead sings, Fuzz is a square-wave buzz, and the Bass amp keeps the lows.
void StripFx::processAmp (float* left, float* right, int n)
{
    const int type = juce::jlimit (0, 4, (int) ampType->load());
    const float gainKnob = ampGain->load();
    static constexpr float maxGain[] = { 6.0f, 30.0f, 120.0f, 300.0f, 16.0f };
    const float g = 1.0f + gainKnob * gainKnob * maxGain[type];
    const float level = 0.15f + ampLevel->load() * 1.4f;
    const bool bassAmp = type == 4;
    // Before the drive: take off the lowest lows (guitar amps tighten up; the bass amp keeps them).
    const float preHz = bassAmp ? 30.0f : 110.0f;
    const float preCoef = std::exp (-juce::MathConstants<float>::twoPi * preHz / (float) rate);
    // Tone: dark to bright.
    const float toneHz = 1200.0f * std::pow (10.0f, ampTone->load());
    const float toneCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (toneHz, (float) rate * 0.45f) / (float) rate);
    // The cabinet: a speaker rolls off the top (lower for a bass cab).
    const float cabHz = bassAmp ? 3500.0f : 5200.0f;
    const float cabCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * cabHz / (float) rate);
    const float dcCoef = std::exp (-juce::MathConstants<float>::twoPi * 12.0f / (float) rate);
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = ch == 0 ? left : right;
        auto& pre = ampPre[(size_t) ch];
        auto& tone = ampTone1[(size_t) ch];
        auto& cab1 = ampCab1[(size_t) ch];
        auto& cab2 = ampCab2[(size_t) ch];
        auto& dc = ampDc[(size_t) ch];
        for (int i = 0; i < n; ++i)
        {
            // One-pole high-pass.
            pre = pre * preCoef + d[i] * (1.0f - preCoef);
            float x = (d[i] - pre) * g;
            if (bassAmp)
                x += pre * g * 0.8f;   // the bass amp puts the lows back after the split
            float y;
            switch (type)
            {
                case 0: y = std::tanh (x * 0.7f) / 0.7f * 0.7f; break;
                case 1: y = std::tanh (x + 0.25f * x * x * (x > 0 ? 1.0f : 0.4f)); break;    // asymmetric: valve-like
                case 2: y = std::tanh (1.5f * std::tanh (x)); break;
                case 3: y = juce::jlimit (-1.0f, 1.0f, x * 2.0f) * 0.9f + 0.1f * std::tanh (x); break;
                default: y = std::tanh (x * 0.8f); break;
            }
            // Keep it centred (the asymmetric shapes add a little DC).
            dc = dc * dcCoef + y * (1.0f - dcCoef);
            y -= dc;
            tone += (y - tone) * toneCoef;
            cab1 += (tone - cab1) * cabCoef;
            cab2 += (cab1 - cab2) * cabCoef;
            d[i] = cab2 * level;
        }
    }
}

// EQ: bass, middle and treble, up or down 12 dB.
void StripFx::processEq (juce::AudioBuffer<float>& buffer, int n)
{
    const float lowDb = eqLow->load(), midDb = eqMid->load(), highDb = eqHigh->load();
    if (! eqWasOn || lowDb != eqShown[0] || midDb != eqShown[1] || highDb != eqShown[2])
    {
        const auto lowC = juce::IIRCoefficients::makeLowShelf (rate, 120.0, 0.7, juce::Decibels::decibelsToGain (lowDb));
        const auto midC = juce::IIRCoefficients::makePeakFilter (rate, 900.0, 0.8, juce::Decibels::decibelsToGain (midDb));
        const auto highC = juce::IIRCoefficients::makeHighShelf (rate, juce::jmin (5000.0, rate * 0.4), 0.7, juce::Decibels::decibelsToGain (highDb));
        for (int ch = 0; ch < 2; ++ch)
        {
            eqLowF[(size_t) ch].setCoefficients (lowC);
            eqMidF[(size_t) ch].setCoefficients (midC);
            eqHighF[(size_t) ch].setCoefficients (highC);
        }
        eqShown[0] = lowDb;
        eqShown[1] = midDb;
        eqShown[2] = highDb;
    }
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        eqLowF[(size_t) ch].processSamples (d, n);
        eqMidF[(size_t) ch].processSamples (d, n);
        eqHighF[(size_t) ch].processSamples (d, n);
    }
}

// Chorus, Phaser and Tremolo. Each fades in and out when it's switched, so it never clicks.
void StripFx::processModulation (float* left, float* right, int n, double bpm)
{
    chorusAmount.setTargetValue (chorusOn->load() > 0.5f ? chorusMix->load() : 0.0f);
    if (chorusAmount.isSmoothing() || chorusAmount.getTargetValue() > 0.0f)
    {
        const int size = chorusLine.getNumSamples();
        auto* cl = chorusLine.getWritePointer (0);
        auto* cr = chorusLine.getWritePointer (1);
        const double inc = chorusRate->load() / rate;
        const float centre = 0.012f * (float) rate, swing = (0.001f + 0.006f * chorusDepth->load()) * (float) rate;
        for (int i = 0; i < n; ++i)
        {
            cl[chorusWrite] = left[i];
            cr[chorusWrite] = right[i];
            const float m = chorusAmount.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                // The two sides a quarter of a turn apart, for width.
                const double phase = chorusPhase + (ch == 0 ? 0.0 : 0.25);
                const float lfo = (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                float pos = (float) chorusWrite - (centre + swing * lfo);
                while (pos < 0)
                    pos += (float) size;
                const int i0 = (int) pos % size;
                const int i1 = (i0 + 1) % size;
                const float frac = pos - std::floor (pos);
                const auto* line = ch == 0 ? cl : cr;
                const float delayed = line[i0] + (line[i1] - line[i0]) * frac;
                auto* d = ch == 0 ? left : right;
                d[i] = d[i] * (1.0f - 0.5f * m) + delayed * 0.8f * m;
            }
            chorusWrite = (chorusWrite + 1) % size;
            chorusPhase += inc;
            if (chorusPhase >= 1.0)
                chorusPhase -= 1.0;
        }
    }

    phaserAmount.setTargetValue (phaserOn->load() > 0.5f ? phaserMix->load() : 0.0f);
    if (phaserAmount.isSmoothing() || phaserAmount.getTargetValue() > 0.0f)
    {
        const double inc = phaserRate->load() / rate;
        const float depth = phaserDepth->load(), fb = phaserFeedback->load();
        for (int i = 0; i < n; ++i)
        {
            const float m = phaserAmount.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                const double phase = phaserPhase + (ch == 0 ? 0.0 : 0.2);
                const float lfo = 0.5f + 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * phase);
                const float fc = 250.0f * std::pow (1.0f + 14.0f * depth, lfo);
                const float t = std::tan (juce::MathConstants<float>::pi * juce::jmin (fc, (float) rate * 0.45f) / (float) rate);
                const float a = (t - 1.0f) / (t + 1.0f);
                auto* d = ch == 0 ? left : right;
                float x = d[i] + phaserLast[(size_t) ch] * fb;
                for (auto& z : phaserState[(size_t) ch])
                {
                    // First-order all-pass.
                    const float y = a * x + z;
                    z = x - a * y;
                    x = y;
                }
                phaserLast[(size_t) ch] = x;
                d[i] = d[i] * (1.0f - 0.5f * m) + x * m * 0.7f;
            }
            phaserPhase += inc;
            if (phaserPhase >= 1.0)
                phaserPhase -= 1.0;
        }
    }

    tremAmount.setTargetValue (tremOn->load() > 0.5f ? tremDepth->load() : 0.0f);
    if (tremAmount.isSmoothing() || tremAmount.getTargetValue() > 0.0f)
    {
        // In time with the Rig: locked to the beat while it plays, free-running when stopped.
        static constexpr double perBeat[] = { 1.0, 2.0, 3.0, 4.0 };
        const double cycles = perBeat[juce::jlimit (0, 3, (int) tremRate->load())];
        bool known = false;
        const double ppq = currentPpq (known);
        const double beatsPerSample = bpm / 60.0 / rate;
        const bool choppy = tremShape->load() > 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const double beat = known ? ppq + i * beatsPerSample : tremFreePhase + i * beatsPerSample;
            const double phase = beat * cycles - std::floor (beat * cycles);
            float shape;
            if (choppy)
                shape = juce::jlimit (0.0f, 1.0f, (float) (0.5 + 6.0 * std::sin (juce::MathConstants<double>::twoPi * phase)));
            else
                shape = 0.5f + 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * phase);
            const float depth = tremAmount.getNextValue();
            const float g = 1.0f - depth * (1.0f - shape);
            left[i] *= g;
            right[i] *= g;
        }
        tremFreePhase += n * beatsPerSample;
        if (tremFreePhase > 1.0e6)
            tremFreePhase = 0;
    }
}

void StripFx::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2 || n == 0)
        return;
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    const double bpm = currentBpm();
    const int lineSize = delayLine.getNumSamples();

    // Gain in (an instrument's level up to a line's), and the tuner hears it.
    trimSmooth.setTargetValue (juce::Decibels::decibelsToGain (inputGain->load()));
    if (trimSmooth.isSmoothing() || std::abs (trimSmooth.getTargetValue() - 1.0f) > 0.0001f)
        for (int i = 0; i < n; ++i)
        {
            const float g = trimSmooth.getNextValue();
            left[i] *= g;
            right[i] *= g;
        }
    if (tunerListening.load())
    {
        int w = tunerWrite.load();
        for (int i = 0; i < n; ++i)
        {
            tunerRing[(size_t) w] = 0.5f * (left[i] + right[i]);
            w = (w + 1) % kTunerSize;
        }
        tunerWrite = w;
    }

    // Bypass: the whole rack out (Gain in stays, so the level doesn't jump).
    if (bypass->load() > 0.5f)
        return;

    processDynamics (left, right, n);
    if (wahOn->load() > 0.5f)
        processWah (left, right, n);
    else
        for (auto& w : wahSvf)
            w.clear();

    // Filter.
    filterSmooth.setTargetValue (filter->load());
    resonanceSmooth.setTargetValue (resonance->load());
    if (filterSmooth.isSmoothing() || std::abs (filterSmooth.getTargetValue()) >= 0.001f)
    {
        const float nyquistLimit = (float) rate * 0.45f;
        for (int i = 0; i < n; ++i)
        {
            const float f = filterSmooth.getNextValue();
            const float res = resonanceSmooth.getNextValue();
            const float amount = std::abs (f);
            const bool high = f > 0.0f;
            const float fc = juce::jmin (nyquistLimit, high ? highPassHz (amount) : lowPassHz (amount));
            // Resonance comes in as the knob leaves the middle, so the middle is truly off.
            const float k = 1.414f - 1.32f * res * juce::jmin (1.0f, amount * 8.0f);
            const float g = std::tan (juce::MathConstants<float>::pi * fc / (float) rate);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1;
            const float a3 = g * a2;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* d = ch == 0 ? left : right;
                auto& s = svf[(size_t) ch];
                const float v0 = d[i];
                const float v3 = v0 - s.ic2;
                const float v1 = a1 * s.ic1 + a2 * v3;
                const float v2 = s.ic2 + a2 * s.ic1 + a3 * v3;
                s.ic1 = 2.0f * v1 - s.ic1;
                s.ic2 = 2.0f * v2 - s.ic2;
                d[i] = high ? v0 - k * v1 - v2 : v2;
            }
        }
    }
    else
    {
        for (auto& s : svf)
            s.clear();
        resonanceSmooth.skip (n);
    }

    // Drive: soft clipping that gets harder as you turn it up, at about the same loudness.
    const float d = drive->load();
    if (d > 0.001f)
    {
        const float g = 1.0f + d * 24.0f;
        const float makeup = 1.0f / (1.0f + d * 3.0f);
        for (int i = 0; i < n; ++i)
        {
            left[i] = std::tanh (left[i] * g) * makeup;
            right[i] = std::tanh (right[i] * g) * makeup;
        }
    }

    // Crush: fewer bits and a lower sample rate.
    const float c = crush->load();
    if (c > 0.001f)
    {
        const float steps = std::pow (2.0f, 16.0f - c * 12.0f);
        const float hold = 1.0f + c * c * 20.0f;
        for (int i = 0; i < n; ++i)
        {
            holdCount += 1.0f;
            if (holdCount >= hold)
            {
                holdCount -= hold;
                held = { left[i], right[i] };
            }
            left[i] = std::round (held[0] * steps) / steps;
            right[i] = std::round (held[1] * steps) / steps;
        }
    }

    // Amp and EQ.
    if (ampOn->load() > 0.5f)
        processAmp (left, right, n);
    if (eqOn->load() > 0.5f)
    {
        processEq (buffer, n);
        eqWasOn = true;
    }
    else if (eqWasOn)
    {
        for (auto* f : { &eqLowF, &eqMidF, &eqHighF })
            for (auto& side : *f)
                side.reset();
        eqWasOn = false;
    }

    processModulation (left, right, n, bpm);

    // Delay, ping-pong between the sides. The Delay knob is how much goes in, so pulling it
    // down lets the echoes ring out.
    {
        const int timeIndex = juce::jlimit (0, 5, (int) delayTime->load());
        delaySamples.setTargetValue ((float) juce::jlimit (1.0, (double) lineSize - 2, delayBeats[timeIndex] * 60.0 / bpm * rate));
        const float send = delayMix->load();
        const float fb = feedback->load();
        const float lowCoef = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 5000.0f / (float) rate);
        auto* dl = delayLine.getWritePointer (0);
        auto* dr = delayLine.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float time = delaySamples.getNextValue();
            float readPos = (float) delayWrite - time;
            if (readPos < 0)
                readPos += (float) lineSize;
            const int i0 = (int) readPos;
            const int i1 = (i0 + 1) % lineSize;
            const float frac = readPos - (float) i0;
            const float outL = dl[i0] + (dl[i1] - dl[i0]) * frac;
            const float outR = dr[i0] + (dr[i1] - dr[i0]) * frac;

            feedbackLow[0] += (outR - feedbackLow[0]) * lowCoef;
            feedbackLow[1] += (outL - feedbackLow[1]) * lowCoef;
            const float in = (left[i] + right[i]) * 0.5f * send;
            dl[delayWrite] = in + feedbackLow[0] * fb;
            dr[delayWrite] = feedbackLow[1] * fb;
            delayWrite = (delayWrite + 1) % lineSize;

            left[i] += outL;
            right[i] += outR;
        }
    }

    // Reverb, also as a send.
    const float rm = reverbMix->load();
    if (wet.getNumSamples() < n)
        wet.setSize (2, n, false, false, true);
    juce::Reverb::Parameters p;
    p.roomSize = reverbSize->load();
    p.damping = 0.45f;
    p.wetLevel = 1.0f;
    p.dryLevel = 0.0f;
    p.width = 1.0f;
    reverb.setParameters (p);
    for (int ch = 0; ch < 2; ++ch)
    {
        wet.copyFrom (ch, 0, buffer, ch, 0, n);
        wet.applyGain (ch, 0, n, rm * 0.5f);
    }
    reverb.processStereo (wet.getWritePointer (0), wet.getWritePointer (1), n);
    buffer.addFrom (0, 0, wet, 0, 0, n);
    buffer.addFrom (1, 0, wet, 1, 0, n);

    // Repeat, on everything before it (echoes and all).
    processRepeat (left, right, n, bpm);

    // Your own plugin last.
    plugin.process (buffer, noPluginMidi);
}

void StripFx::processRepeat (float* left, float* right, int n, double bpm)
{
    // Repeat: a new length (or turning it on) catches a fresh slice from now.
    const int choice = juce::jlimit (0, 4, (int) repeat->load());
    if (choice != repeatChoice)
    {
        repeatChoice = choice;
        if (choice > 0)
        {
            repeatLength = juce::jlimit (64, repeatBuffer.getNumSamples() - 1, (int) (repeatBeats[choice] * 60.0 / bpm * rate));
            repeatPos = 0;
            repeatFilled = false;
        }
    }
    repeatMix.setTargetValue (choice > 0 ? 1.0f : 0.0f);
    if (choice > 0 || repeatMix.isSmoothing() || repeatMix.getCurrentValue() > 0.0f)
    {
        auto* rl = repeatBuffer.getWritePointer (0);
        auto* rr = repeatBuffer.getWritePointer (1);
        constexpr int fade = 64;
        for (int i = 0; i < n; ++i)
        {
            const float m = repeatMix.getNextValue();
            float l = left[i], r = right[i];
            if (repeatChoice > 0)
            {
                if (! repeatFilled)
                {
                    rl[repeatPos] = l;
                    rr[repeatPos] = r;
                    if (++repeatPos >= repeatLength)
                    {
                        repeatFilled = true;
                        repeatPos = 0;
                    }
                }
                else
                {
                    // Short fades at both ends of the slice so the loop doesn't click.
                    const float env = juce::jmin (1.0f, (float) repeatPos / fade, (float) (repeatLength - repeatPos) / fade);
                    l = rl[repeatPos] * env;
                    r = rr[repeatPos] * env;
                    repeatPos = (repeatPos + 1) % repeatLength;
                }
            }
            left[i] += (l - left[i]) * m;
            right[i] += (r - right[i]) * m;
        }
    }

}

void StripFx::getStateInformation (juce::MemoryBlock& data)
{
    auto state = params.copyState();
    state.removeChild (state.getChildWithName ("Plugin"), nullptr);
    if (plugin.isLoaded())
        state.appendChild (plugin.toTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, data);
}

void StripFx::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (params.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            const auto pluginTree = state.getChildWithName ("Plugin").createCopy();
            state.removeChild (state.getChildWithName ("Plugin"), nullptr);
            params.replaceState (state);
            plugin.fromTree (pluginTree);
        }
}
