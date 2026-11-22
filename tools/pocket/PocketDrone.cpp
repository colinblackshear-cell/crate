#include "PocketDrone.h"

using namespace pocket;

namespace
{
constexpr int kMaxBlock = 1024;
}

const char* pocket::droneFeedName (DroneFeed f)
{
    switch (f)
    {
        case DroneFeed::voice: return "MY VOICE";
        case DroneFeed::vocoder: return "VOCODER";
        case DroneFeed::drums: return "DRUMS";
        case DroneFeed::synth: return "SYNTH";
        case DroneFeed::chords: return "CHORDS";
        default: return "";
    }
}

DroneTool::DroneTool (BeatsEngine& b) : beats (b)
{
    loadPreset (0);
}

float DroneTool::faderGain (float lvl)
{
    // As the Mac's faders: 0.8 is 0 dB, the top +6 dB, and down to silence at 0 along a dB curve.
    lvl = juce::jlimit (0.0f, 1.0f, lvl);
    if (lvl <= 0.0f)
        return 0.0f;
    const float gain = juce::Decibels::decibelsToGain ((lvl - 0.8f) * 30.0f);
    return lvl < 0.1f ? gain * (lvl / 0.1f) : gain;
}

juce::String DroneTool::volumeText (float lvl)
{
    const float gain = faderGain (lvl);
    if (gain <= 0.0001f)
        return "OFF";
    const float db = juce::Decibels::gainToDecibels (gain);
    return (db > 0.05f ? "+" : "") + juce::String (db, 1) + " dB";
}

juce::String DroneTool::noteName (int r)
{
    static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[juce::jlimit (0, 11, r)];
}

void DroneTool::loadPreset (int index)
{
    const auto& list = crate::drone::presets();
    if (! juce::isPositiveAndBelow (index, (int) list.size()))
        return;
    const auto& p = list[(size_t) index];
    first = p.first;
    octave = p.octave;
    sync = p.sync;
    pace = p.pace;
    jawari = p.jawari;
    ring = p.decay;
    width = p.width;
    reeds = p.reeds;
    strings = p.strings;
    voice = p.voice;
    order = p.order;
    human = p.human;
    bright = p.bright;
    body = p.body;
    detune = p.detune;
    space = p.space;
    // (a ready-made drone plays its own order: your steps stay drawn, for when you pick them again)
    stepCount = 0;
    swing = 0.0f;
}

void DroneTool::setStepCount (int choice)
{
    bool empty = true;
    for (auto& v : steps)
        empty = empty && v.load() == 0;
    if (choice > 0 && empty)
    {
        const auto first = crate::drone::starterSteps();
        for (size_t i = 0; i < steps.size(); ++i)
            steps[i] = first[i];
    }
    stepCount = juce::jlimit (0, 2, choice);
}

void DroneTool::toggleStep (int step, int string)
{
    if (! juce::isPositiveAndBelow (step, 16) || ! juce::isPositiveAndBelow (string, crate::drone::kStrings))
        return;
    if (stepCount.load() == 0)
        stepCount = step < 8 ? 1 : 2;   // (drawing turns them on, as you draw them: no first pattern)
    else if (step >= 8 && stepCount.load() == 1)
        stepCount = 2;
    steps[(size_t) step] = (std::uint8_t) (steps[(size_t) step].load() ^ (1 << string));
}

void DroneTool::clearSteps()
{
    for (auto& v : steps)
        v = 0;
}

int DroneTool::presetIndex() const
{
    const auto& list = crate::drone::presets();
    auto near = [] (float a, float b) { return std::abs (a - b) < 0.005f; };
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& p = list[i];
        if (p.first == first.load() && p.octave == octave.load() && p.sync == sync.load() && near (p.pace, pace.load()) && near (p.jawari, jawari.load())
            && near (p.decay, ring.load()) && near (p.width, width.load()) && near (p.reeds, reeds.load()) && near (p.strings, strings.load())
            && p.voice == voice.load() && p.order == order.load() && stepCount.load() == 0 && near (p.human, human.load()) && near (p.bright, bright.load())
            && near (p.body, body.load()) && near (p.detune, detune.load()) && near (p.space, space.load()))
            return (int) i;
    }
    return -1;
}

juce::String DroneTool::presetName() const
{
    const int i = presetIndex();
    return i >= 0 ? juce::String (crate::drone::presets()[(size_t) i].name) : juce::String ("Your own");
}

juce::String DroneTool::stringNote (int string) const
{
    const double hz = crate::drone::rootHz (root.load(), octave.load(), 0.0f) * crate::drone::stringRatio (string, first.load());
    const int note = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
    return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

void DroneTool::reset()
{
    on = false;
    loadPreset (0);
    fine = 0.0f;
    resonate = 0.0f;
    feed = (int) DroneFeed::voice;
    level = 0.8f;
    followKey = true;
    stepRate = 1;
    clearSteps();
}

juce::var DroneTool::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("on", on.load());
    o->setProperty ("root", root.load());
    o->setProperty ("followKey", followKey);
    o->setProperty ("octave", octave.load());
    o->setProperty ("first", first.load());
    o->setProperty ("sync", sync.load());
    o->setProperty ("pace", pace.load());
    o->setProperty ("jawari", jawari.load());
    o->setProperty ("ring", ring.load());
    o->setProperty ("fine", fine.load());
    o->setProperty ("width", width.load());
    o->setProperty ("strings", strings.load());
    o->setProperty ("reeds", reeds.load());
    o->setProperty ("resonate", resonate.load());
    o->setProperty ("feed", feed.load());
    o->setProperty ("level", level.load());
    o->setProperty ("order", order.load());
    o->setProperty ("stepCount", stepCount.load());
    o->setProperty ("stepRate", stepRate.load());
    juce::String text;
    for (auto& v : steps)
        text << juce::String::toHexString ((int) v.load());
    o->setProperty ("steps", text);
    o->setProperty ("swing", swing.load());
    o->setProperty ("human", human.load());
    o->setProperty ("voice", voice.load());
    o->setProperty ("bright", bright.load());
    o->setProperty ("body", body.load());
    o->setProperty ("detune", detune.load());
    o->setProperty ("space", space.load());
    return juce::var (o);
}

void DroneTool::fromVar (const juce::var& v, int songRoot)
{
    reset();
    root = juce::jlimit (0, 11, songRoot);
    if (! v.isObject())
        return;
    auto num = [&v] (const char* id, float fallback, float lo, float hi) { return juce::jlimit (lo, hi, (float) (double) v.getProperty (id, fallback)); };
    auto whole = [&v] (const char* id, int fallback, int lo, int hi) { return juce::jlimit (lo, hi, (int) v.getProperty (id, fallback)); };
    on = (bool) v.getProperty ("on", false);
    followKey = (bool) v.getProperty ("followKey", true);
    root = followKey ? juce::jlimit (0, 11, songRoot) : whole ("root", songRoot, 0, 11);
    octave = whole ("octave", 0, -1, 1);
    first = whole ("first", 0, 0, 3);
    sync = whole ("sync", 0, 0, 3);
    pace = num ("pace", 1.1f, 0.3f, 4.0f);
    jawari = num ("jawari", 0.6f, 0.0f, 1.0f);
    ring = num ("ring", 0.5f, 0.0f, 1.0f);
    fine = num ("fine", 0.0f, -50.0f, 50.0f);
    width = num ("width", 0.7f, 0.0f, 1.0f);
    strings = num ("strings", 1.0f, 0.0f, 1.0f);
    reeds = num ("reeds", 0.0f, 0.0f, 1.0f);
    resonate = num ("resonate", 0.0f, 0.0f, 1.0f);
    feed = whole ("feed", 0, 0, (int) DroneFeed::count - 1);
    level = num ("level", 0.8f, 0.0f, 1.0f);
    // (songs from before the pattern and tone: their defaults, which are how it sounded)
    order = whole ("order", 0, 0, crate::drone::numOrders - 1);
    stepCount = whole ("stepCount", 0, 0, 2);
    stepRate = whole ("stepRate", 1, 0, 2);
    const auto text = v.getProperty ("steps", {}).toString();
    for (int i = 0; i < 16; ++i)
        steps[(size_t) i] = (std::uint8_t) (i < text.length() ? juce::CharacterFunctions::getHexDigitValue (text[i]) & 0x0f : 0);
    swing = num ("swing", 0.0f, 0.0f, 1.0f);
    human = num ("human", 0.0f, 0.0f, 1.0f);
    voice = whole ("voice", 0, 0, crate::drone::numVoices - 1);
    bright = num ("bright", 0.5f, 0.0f, 1.0f);
    body = num ("body", 0.5f, 0.0f, 1.0f);
    detune = num ("detune", 0.0f, 0.0f, 1.0f);
    space = num ("space", 0.0f, 0.0f, 1.0f);
}

crate::drone::Settings DroneTool::read() const
{
    crate::drone::Settings s;
    s.on = on.load();
    s.root = root.load();
    s.octave = octave.load();
    s.first = first.load();
    s.sync = sync.load();
    s.paceSeconds = pace.load();
    s.jawari = jawari.load();
    s.decay = ring.load();
    s.fineCents = fine.load();
    s.width = width.load();
    s.strings = strings.load();
    s.reeds = reeds.load();
    s.resonate = resonate.load();
    s.order = order.load();
    const int count = stepCount.load(), rateChoice = stepRate.load();
    s.stepCount = count == 1 ? 8 : count == 2 ? 16 : 0;
    s.stepBeats = rateChoice == 0 ? 1.0f : rateChoice == 2 ? 0.25f : 0.5f;
    for (size_t i = 0; i < steps.size(); ++i)
        s.steps[i] = steps[i].load();
    s.swing = swing.load();
    s.human = human.load();
    s.voice = voice.load();
    s.bright = bright.load();
    s.body = body.load();
    s.detune = detune.load();
    s.space = space.load();
    return s;
}

void DroneTool::prepare (double sampleRate)
{
    rate = juce::jmax (8000.0, sampleRate);
    engine.prepare (sampleRate);
    limiter.prepare (sampleRate);
    outL.assign ((size_t) kMaxBlock, 0.0f);
    outR.assign ((size_t) kMaxBlock, 0.0f);
    gainNow = faderGain (level.load());
}

void DroneTool::render (float* left, float* right, int numSamples)
{
    renderAt (left, right, numSamples, beats.clockRunning(), beats.clockBeat(), beats.getBpm());
}

void DroneTool::renderAt (float* left, float* right, int numSamples, bool running, double beat, double bpm)
{
    const auto s = read();
    const bool wake = plucked.exchange (false);
    // Asleep while there's nothing to play (the phone's battery): off, rung out, and not listening.
    if (! wake && ! s.on && s.resonate <= 0.0f && ! engine.isSounding())
        return;
    crate::drone::Clock clock;
    clock.playing = running;
    clock.ppq = beat;
    clock.bpm = bpm;
    const float target = faderGain (level.load()) * crate::drone::kBedGain;
    for (int done = 0; done < numSamples;)
    {
        const int n = juce::jmin (kMaxBlock, numSamples - done, (int) outL.size());
        if (n <= 0)
            return;
        std::fill (outL.begin(), outL.begin() + n, 0.0f);
        std::fill (outR.begin(), outR.begin() + n, 0.0f);
        const float* in = feedIn != nullptr ? feedIn + done : nullptr;
        engine.process (outL.data(), outR.data(), in, in, n, s, clock);
        // (the volume ramps over the block, so a turn of the knob never clicks)
        const float from = gainNow, step = (target - from) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float g = from + step * (float) (i + 1);
            outL[(size_t) i] *= g;
            outR[(size_t) i] *= g;
        }
        gainNow = target;
        limiter.process (outL.data(), outR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            left[done + i] += outL[(size_t) i];
            if (right != nullptr)
                right[done + i] += outR[(size_t) i];
        }
        done += n;
        clock.ppq += n * bpm / 60.0 / rate;   // (only for a second chunk: the looper's blocks are short)
    }
}
