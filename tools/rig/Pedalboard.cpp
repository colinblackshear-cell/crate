#include "Pedalboard.h"

namespace pedalboard
{
const std::vector<Pedal>& pedals()
{
    static const std::vector<Pedal> list {
        { "Gate", "noise gate",
          "Shuts out the hum and hiss between notes (a guitar's buzz, a noisy amp). Threshold is how loud a note has to be to get through.",
          0xff7f8c8d, { "gateThreshold", "Threshold" }, "gateOn",
          { { "gateThreshold", "Threshold" } } },
        { "Comp", "compressor",
          "Evens out loud and quiet notes and makes them sustain: a must for bass and clean guitar. Squeeze is how hard; Level brings it back up.",
          0xff4fb39a, { "compAmount", "Squeeze" }, "compOn",
          { { "compAmount", "Squeeze" }, { "compLevel", "Level" } } },
        { "Wah", "envelope filter",
          "An auto-wah: play harder and it opens up, the funky quack. Sensitivity is how much your playing moves it, Range how far, Peak how vocal.",
          0xff8fc06a, { "wahSense", "Sensitivity" }, "wahOn",
          { { "wahSense", "Sensitivity" }, { "wahRange", "Range" }, { "wahPeak", "Peak" } } },
        { "Filter", "dark / thin",
          "The DJ filter: turn left to make it darker (low-pass), right to make it thinner (high-pass). The middle is off. Resonance makes it ring.",
          0xff4f8fd0, { "filter", "Filter" }, nullptr,
          { { "filter", "Filter" }, { "resonance", "Resonance" } } },
        { "Dirt", "drive + crush",
          "Drive warms it up and then distorts it. Crush makes it crunchy and digital, like an old games console.",
          0xffb0563c, { "drive", "Drive" }, nullptr,
          { { "drive", "Drive" }, { "crush", "Crush" } } },
        { "Amp", "amp + cabinet",
          "A guitar or bass amp and its speaker. Clean stays clear, Crunch breaks up when you dig in, Lead sings, Fuzz buzzes, Bass amp keeps the lows.",
          0xffe0785a, { "ampGain", "Gain" }, "ampOn",
          { { "ampType", "Amp" }, { "ampGain", "Gain" }, { "ampTone", "Tone" }, { "ampLevel", "Level" } } },
        { "EQ", "bass mid treble",
          "Bass, middle and treble, up or down. Scoop the middle for heavy guitar, push it to cut through the band.",
          0xffefcb45, { "eqMid", "Middle" }, "eqOn",
          { { "eqLow", "Bass" }, { "eqMid", "Middle" }, { "eqHigh", "Treble" } } },
        { "Chorus", "shimmer",
          "Thickens the sound into a shimmering, wide group. Rate is how fast it moves, Depth how far.",
          0xff63c2b7, { "chorusMix", "Mix" }, "chorusOn",
          { { "chorusMix", "Mix" }, { "chorusRate", "Rate" }, { "chorusDepth", "Depth" } } },
        { "Phaser", "swoosh",
          "The swirling, jet-plane swoosh. Slow for pads and keys, faster for funk guitar. Feedback makes it sharper.",
          0xff8a6fb3, { "phaserRate", "Speed" }, "phaserOn",
          { { "phaserMix", "Mix" }, { "phaserRate", "Speed" }, { "phaserDepth", "Depth" }, { "phaserFeedback", "Feedback" } } },
        { "Tremolo", "in time",
          "The volume pulses in time with Kaleidobox: smooth like an old amp, or choppy like a gate.",
          0xffd9534a, { "tremDepth", "Depth" }, "tremOn",
          { { "tremDepth", "Depth" }, { "tremRate", "Speed" }, { "tremShape", "Shape" } } },
        { "Delay", "echo in time",
          "Echoes in time with Kaleidobox, bouncing left and right. Pull the mix down and the echoes ring out.",
          0xff7d9be0, { "delayMix", "Mix" }, nullptr,
          { { "delayMix", "Mix" }, { "delayTime", "Time" }, { "feedback", "Repeats" } } },
        { "Reverb", "space",
          "Puts it in a room, a hall or a cathedral. Size is how big.",
          0xff9b8ccf, { "reverbMix", "Mix" }, nullptr,
          { { "reverbMix", "Mix" }, { "reverbSize", "Size" } } },
        { "Repeat", "beat stutter",
          "Catches a slice of the beat and loops it while it's on: stomp it for a fill, stomp again to let go.",
          0xffc9a472, { "repeat", "Slice" }, nullptr,
          { { "repeat", "Slice" } } },
    };
    return list;
}

namespace
{
juce::RangedAudioParameter* find (const StripFx& fx, const juce::String& id)
{
    return const_cast<StripFx&> (fx).params.getParameter (id);
}

// The amount pedals: their settings and the amount each comes back at.
struct Amount
{
    const char* id;
    float start;
};

std::vector<Amount> amountsOf (int pedal)
{
    switch (pedal)
    {
        case filter: return { { "filter", -0.35f } };
        case dirt:   return { { "drive", 0.35f }, { "crush", 0.0f } };
        case delay:  return { { "delayMix", 0.3f } };
        case reverb: return { { "reverbMix", 0.3f } };
        case repeat: return { { "repeat", 2.0f } };
        default:     return {};
    }
}

bool amountIsOn (const juce::String& id, float v)
{
    if (id == "filter")
        return std::abs (v) >= 0.02f;
    if (id == "repeat")
        return v > 0.5f;
    return v > 0.01f;
}
}

float get (const StripFx& fx, const juce::String& id)
{
    if (auto* p = find (fx, id))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

void set (StripFx& fx, const juce::String& id, float value)
{
    if (auto* p = fx.params.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

bool isOn (const StripFx& fx, int pedal)
{
    if (pedal < 0 || pedal >= numPedals)
        return false;
    if (const char* on = pedals()[(size_t) pedal].onId)
        return get (fx, on) > 0.5f;
    for (const auto& a : amountsOf (pedal))
        if (amountIsOn (a.id, get (fx, a.id)))
            return true;
    return false;
}

void setOn (StripFx& fx, int pedal, bool on, float* kept)
{
    if (pedal < 0 || pedal >= numPedals || on == isOn (fx, pedal))
        return;
    if (const char* onId = pedals()[(size_t) pedal].onId)
    {
        set (fx, onId, on ? 1.0f : 0.0f);
        // A flat EQ would be a switch you can't hear: it comes on with a little lift.
        if (on && pedal == eq && get (fx, "eqLow") == 0.0f && get (fx, "eqMid") == 0.0f && get (fx, "eqHigh") == 0.0f)
        {
            set (fx, "eqLow", 3.0f);
            set (fx, "eqHigh", 2.0f);
        }
        return;
    }
    const auto amounts = amountsOf (pedal);
    for (size_t i = 0; i < amounts.size(); ++i)
    {
        const auto& a = amounts[i];
        if (on)
        {
            const float back = kept != nullptr && amountIsOn (a.id, kept[i]) ? kept[i] : a.start;
            set (fx, a.id, back);
        }
        else
        {
            if (kept != nullptr)
                kept[i] = get (fx, a.id);
            set (fx, a.id, 0.0f);
        }
    }
}

void resetPedal (StripFx& fx, int pedal)
{
    if (pedal < 0 || pedal >= numPedals)
        return;
    const auto& info = pedals()[(size_t) pedal];
    auto toDefault = [&fx] (const char* id)
    {
        if (auto* p = fx.params.getParameter (id))
            p->setValueNotifyingHost (p->getDefaultValue());
    };
    for (const auto& s : info.rack)
        toDefault (s.id);
    if (info.onId != nullptr)
        toDefault (info.onId);
}

int countOn (const StripFx& fx)
{
    int n = 0;
    for (int i = 0; i < numPedals; ++i)
        n += isOn (fx, i) ? 1 : 0;
    return n;
}

//==============================================================================
const std::vector<Preset>& presets()
{
    // Choice settings by their number: ampType 0 Clean, 1 Crunch, 2 Lead, 3 Fuzz, 4 Bass amp;
    // delayTime 0 1/16, 1 1/8, 2 1/8 dotted, 3 1/4, 4 1/4 dotted, 5 1/2; tremRate 0 1/4 ... 3 1/16.
    static const std::vector<Preset> list {
        { "Clean guitar", "Guitar", { { "compOn", 1 }, { "compAmount", 0.35f }, { "ampOn", 1 }, { "ampType", 0 }, { "ampGain", 0.3f },
                                      { "ampTone", 0.6f }, { "chorusOn", 1 }, { "chorusMix", 0.3f }, { "reverbMix", 0.2f } } },
        { "Crunch", "Guitar", { { "gateOn", 1 }, { "gateThreshold", -60 }, { "ampOn", 1 }, { "ampType", 1 }, { "ampGain", 0.55f },
                                { "ampTone", 0.55f }, { "eqOn", 1 }, { "eqMid", 3 }, { "reverbMix", 0.15f } } },
        { "Lead solo", "Guitar", { { "gateOn", 1 }, { "compOn", 1 }, { "compAmount", 0.4f }, { "ampOn", 1 }, { "ampType", 2 }, { "ampGain", 0.7f },
                                   { "delayMix", 0.3f }, { "delayTime", 2 }, { "feedback", 0.4f }, { "reverbMix", 0.25f } } },
        { "Fuzz", "Guitar", { { "gateOn", 1 }, { "ampOn", 1 }, { "ampType", 3 }, { "ampGain", 0.8f }, { "ampTone", 0.45f }, { "reverbMix", 0.1f } } },
        { "Funk wah", "Guitar", { { "compOn", 1 }, { "compAmount", 0.5f }, { "wahOn", 1 }, { "wahSense", 0.65f }, { "wahRange", 0.7f },
                                  { "wahPeak", 0.7f }, { "ampOn", 1 }, { "ampType", 0 }, { "ampGain", 0.3f } } },
        { "Surf tremolo", "Guitar", { { "ampOn", 1 }, { "ampType", 0 }, { "ampGain", 0.35f }, { "ampTone", 0.7f }, { "tremOn", 1 },
                                      { "tremDepth", 0.7f }, { "tremRate", 1 }, { "reverbMix", 0.45f }, { "reverbSize", 0.7f } } },
        { "Bass DI", "Bass", { { "compOn", 1 }, { "compAmount", 0.55f }, { "eqOn", 1 }, { "eqLow", 3 }, { "eqMid", -2 }, { "eqHigh", 1 } } },
        { "Fat bass amp", "Bass", { { "compOn", 1 }, { "compAmount", 0.5f }, { "ampOn", 1 }, { "ampType", 4 }, { "ampGain", 0.45f },
                                    { "ampTone", 0.35f }, { "eqOn", 1 }, { "eqLow", 4 } } },
        { "Funk bass", "Bass", { { "compOn", 1 }, { "compAmount", 0.6f }, { "wahOn", 1 }, { "wahSense", 0.5f }, { "wahRange", 0.45f },
                                 { "wahPeak", 0.55f } } },
        { "Gritty synth bass", "Bass", { { "drive", 0.3f }, { "filter", -0.15f }, { "compOn", 1 }, { "compAmount", 0.4f } } },
        { "Shimmer pad", "Keys and synths", { { "chorusOn", 1 }, { "chorusMix", 0.6f }, { "chorusDepth", 0.7f }, { "delayMix", 0.3f },
                                              { "delayTime", 3 }, { "reverbMix", 0.6f }, { "reverbSize", 0.9f } } },
        { "Phase out", "Keys and synths", { { "phaserOn", 1 }, { "phaserMix", 0.6f }, { "phaserRate", 0.3f }, { "phaserDepth", 0.8f },
                                            { "phaserFeedback", 0.6f }, { "reverbMix", 0.2f } } },
        { "Old organ", "Keys and synths", { { "ampOn", 1 }, { "ampType", 1 }, { "ampGain", 0.35f }, { "chorusOn", 1 }, { "chorusMix", 0.5f },
                                            { "chorusRate", 3.5f }, { "chorusDepth", 0.3f }, { "reverbMix", 0.2f } } },
        { "Lo-fi drums", "Drums", { { "crush", 0.35f }, { "filter", -0.25f }, { "compOn", 1 }, { "compAmount", 0.6f } } },
        { "Punchy drums", "Drums", { { "compOn", 1 }, { "compAmount", 0.7f }, { "compLevel", 2 }, { "eqOn", 1 }, { "eqLow", 3 }, { "eqHigh", 2 } } },
        { "Dub delay", "Anything", { { "filter", 0.15f }, { "delayMix", 0.5f }, { "delayTime", 4 }, { "feedback", 0.7f }, { "reverbMix", 0.3f } } },
        { "Big space", "Anything", { { "delayMix", 0.2f }, { "delayTime", 3 }, { "reverbMix", 0.55f }, { "reverbSize", 0.85f } } },
        { "Telephone", "Anything", { { "filter", 0.45f }, { "drive", 0.25f }, { "eqOn", 1 }, { "eqMid", 8 }, { "eqLow", -12 }, { "eqHigh", -6 } } },
    };
    return list;
}

void applyPreset (StripFx& fx, const Preset& preset)
{
    // Gain in is the line's level, not the sound: keep it.
    const float gainIn = get (fx, "inputGain");
    for (auto* p : fx.getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
    set (fx, "inputGain", gainIn);
    for (const auto& [id, value] : preset.values)
        set (fx, id, value);
}

//==============================================================================
juce::File userPresetsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Effects");
}

juce::ValueTree copySettings (const StripFx& fx)
{
    auto state = const_cast<StripFx&> (fx).params.copyState();
    state.removeChild (state.getChildWithName ("Plugin"), nullptr);
    return state;
}

void pasteSettings (StripFx& fx, const juce::ValueTree& settings)
{
    if (! settings.isValid())
        return;
    for (auto* p : fx.getParameters())
        if (auto* withId = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float value = withId->convertFrom0to1 (withId->getDefaultValue());
            for (const auto& child : settings)
                if (child.getProperty ("id").toString() == withId->paramID)
                    value = (float) child.getProperty ("value", value);
            withId->setValueNotifyingHost (withId->convertTo0to1 (value));
        }
}

bool savePreset (const StripFx& fx, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    if (auto xml = copySettings (fx).createXml())
        return xml->writeTo (file);
    return false;
}

bool loadPreset (StripFx& fx, const juce::File& file)
{
    if (auto xml = juce::parseXML (file))
    {
        const auto tree = juce::ValueTree::fromXml (*xml);
        if (tree.hasType (fx.params.state.getType()))
        {
            pasteSettings (fx, tree);
            return true;
        }
    }
    return false;
}

//==============================================================================
float detectPitch (const float* x, int count, double rate)
{
    if (x == nullptr || count < 256 || rate <= 0)
        return 0.0f;
    double energy = 0;
    for (int i = 0; i < count; ++i)
        energy += (double) x[i] * x[i];
    if (std::sqrt (energy / count) < 0.003)
        return 0.0f;   // too quiet to tell

    // YIN: the difference of the sound with itself, shifted, normalised by its running mean.
    // Low B on a five-string bass is about 31 Hz; the top of a guitar about 1.3 kHz.
    const int minLag = juce::jmax (2, (int) (rate / 1500.0));
    const int maxLag = juce::jmin (count / 2, (int) (rate / 30.0));
    const int window = count - maxLag;
    if (maxLag <= minLag || window < 64)
        return 0.0f;
    std::vector<float> d ((size_t) maxLag + 1, 0.0f);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        double sum = 0;
        for (int i = 0; i < window; ++i)
        {
            const double diff = (double) x[i] - x[i + lag];
            sum += diff * diff;
        }
        d[(size_t) lag] = (float) sum;
    }
    double running = 0;
    std::vector<float> nd ((size_t) maxLag + 1, 1.0f);
    for (int lag = 1; lag <= maxLag; ++lag)
    {
        running += d[(size_t) lag];
        nd[(size_t) lag] = running > 0 ? (float) (d[(size_t) lag] * lag / running) : 1.0f;
    }
    int best = -1;
    for (int lag = minLag; lag < maxLag; ++lag)
        if (nd[(size_t) lag] < 0.15f)
        {
            while (lag + 1 < maxLag && nd[(size_t) lag + 1] < nd[(size_t) lag])
                ++lag;
            best = lag;
            break;
        }
    if (best < 0)
        return 0.0f;
    // Between samples: a parabola through the dip.
    const float a = nd[(size_t) best - 1], b = nd[(size_t) best], c = nd[(size_t) best + 1];
    const float denom = a - 2 * b + c;
    const float shift = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
    return (float) (rate / (best + juce::jlimit (-1.0f, 1.0f, shift)));
}

Note nearestNote (float hz)
{
    Note n;
    if (hz <= 0)
        return n;
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const float midi = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    const int nearest = juce::roundToInt (midi);
    n.name = names[((nearest % 12) + 12) % 12];
    n.octave = nearest / 12 - 1;
    n.cents = (midi - (float) nearest) * 100.0f;
    return n;
}
}
