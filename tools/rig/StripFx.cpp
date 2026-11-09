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
}

void StripFx::setPlayHead (juce::AudioPlayHead* head)
{
    AudioProcessor::setPlayHead (head);
    plugin.setPlayHead (head);
}

bool StripFx::isActive() const
{
    return plugin.isLoaded() || std::abs (filter->load()) >= 0.02f || drive->load() > 0.01f || crush->load() > 0.01f
        || delayMix->load() > 0.01f || reverbMix->load() > 0.01f || repeat->load() > 0.5f;
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

    // Your own plugin last.
    plugin.process (buffer, noPluginMidi);
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
