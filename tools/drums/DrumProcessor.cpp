#include "DrumProcessor.h"
#include "DrumView.h"

using namespace crate;

namespace
{
const char* const voiceKeys[drums::kVoices] = { "kick", "snare", "clap", "rim", "lowTom", "midTom", "highTom",
                                                "clave", "cowbell", "closedHat", "openHat", "cymbal" };

// Each sound's starting settings: decay and tone, so the kit sounds right out of the box.
constexpr float defaultDecay[drums::kVoices] = { 0.55f, 0.4f, 0.4f, 0.5f, 0.45f, 0.45f, 0.45f, 0.5f, 0.35f, 0.35f, 0.4f, 0.5f };
constexpr float defaultTone[drums::kVoices] = { 0.35f, 0.55f, 0.5f, 0.5f, 0.3f, 0.3f, 0.3f, 0.3f, 0.5f, 0.5f, 0.5f, 0.5f };
constexpr float defaultLevel[drums::kVoices] = { 0.9f, 0.75f, 0.7f, 0.6f, 0.7f, 0.7f, 0.7f, 0.6f, 0.55f, 0.55f, 0.5f, 0.45f };
// A little stereo, like a mixed kit: toms across, hats a touch right, cowbell and clave left.
constexpr float defaultPan[drums::kVoices] = { 0.0f, 0.0f, 0.0f, -0.1f, -0.35f, 0.0f, 0.35f, -0.25f, -0.2f, 0.2f, 0.2f, 0.15f };

constexpr int fillEveryValues[] = { 0, 2, 4, 8, 16 };

float get (const juce::AudioProcessorValueTreeState& p, const juce::String& id)
{
    auto* v = p.getRawParameterValue (id);
    return v != nullptr ? v->load() : 0.0f;
}

juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

// A beat written out: one string per sound ('x' a hit, '2'-'4' a roll, '.' rest) and the accents ('>').
struct Written
{
    const char* name;
    std::array<const char*, drums::kVoices> rows;
    const char* accent;
};

const Written written[] = {
    { "Electro", { "x.....x...x.....", "....x.......x...", "............x...", "", "", "", "", "", "",
                   "x.x.x.x.x.x.x...", "..............x.", "" }, ">...>...>...>..." },
    { "Boom bap", { "x......x..x.....", "....x.......x...", "", "...........x....", "", "", "", "", "",
                    "x.x.x.x.x.x.x.xx", "", "" }, "....>.......>..." },
    { "Trap", { "x.......x.x.....", "", "........x.......", "", "", "", "", "", "",
                "x.x.x.x.x.x.3.x4", "", "x..............." }, "x.......>......." },
    { "House", { "x...x...x...x...", "", "....x.......x...", "", "", "", "", "", "",
                 "xx.xxx.xxx.xxx.x", "..x...x...x...x.", "" }, "..>...>...>...>." },
    { "Funk", { "x..x..x...x..x..", "....x..x.x..x...", "", "", "", "", "", "", "",
                "xxxxxxxxxxxxxxxx", "", "" }, ">...>...>...>..." },
    { "Latin", { "x...x...x...x...", "", "", "", "......x......x..", "", "", "x..x..x...x.x...", "x.xx.x.xx.x.x.xx",
                 ".x.x.x.x.x.x.x.x", "", "" }, "x...x...x...x..." },
    { "Tom fill", { "x...............", "...............4", "", "", "............x.x.", "........x.x.....", "x.x.x.x.........",
                    "", "", "", "", "" }, ">...>...>...>.>>" },
    { "Snare fill", { "x.......x.......", "x.x.x.x.xxxx2222", "", "", "", "", "", "", "", "", "", "" }, ">...>...>.>.>>>>" },
};
}

juce::String DrumProcessor::voiceParam (int voice, const juce::String& what)
{
    return juce::String (voiceKeys[juce::jlimit (0, drums::kVoices - 1, voice)]) + what;
}

juce::AudioProcessorValueTreeState::ParameterLayout DrumProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto number = [&] (const String& id, const String& name, NormalisableRange<float> range, float def,
                       std::function<String (float, int)> text = percent)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (text)));
    };

    StringArray patternNames, fillNames { "Off" };
    for (int i = 1; i <= drums::kPatterns; ++i)
    {
        patternNames.add (String (i));
        fillNames.add (String (i));
    }
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "pattern", 1 }, "Pattern", patternNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill", 1 }, "Fill pattern", fillNames, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "fillEvery", 1 }, "Fill every",
                                                        StringArray { "Only when pressed", "2nd time", "4th time", "8th time", "16th time" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "sequencer", 1 }, "Sequencer", true));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "stepRate", 1 }, "Step rate", drums::stepRateNames(), 1));
    number ("accent", "Accent", { 0.0f, 1.0f }, 0.5f);
    number ("swing", "Swing", { 0.0f, 1.0f }, 0.0f);
    number ("drive", "Drive", { 0.0f, 1.0f }, 0.0f);
    number ("humanize", "Humanize", { 0.0f, 1.0f }, 0.0f);
    number ("volume", "Volume", { 0.0f, 1.0f }, 0.8f);
    number ("tempo", "Tempo", { 60.0f, 200.0f, 1.0f }, 120.0f, [] (float v, int) { return String (roundToInt (v)) + " BPM"; });

    for (int v = 0; v < drums::kVoices; ++v)
    {
        const String name (drums::voiceName (v));
        number (voiceParam (v, "Level"), name + " level", { 0.0f, 1.0f }, defaultLevel[v]);
        number (voiceParam (v, "Tune"), name + " tune", { -12.0f, 12.0f }, 0.0f, [] (float t, int)
                { return (t > 0.05f ? "+" : "") + String (t, 1) + " st"; });
        number (voiceParam (v, "Decay"), name + " decay", { 0.0f, 1.0f }, defaultDecay[v]);
        number (voiceParam (v, "Tone"), name + " tone", { 0.0f, 1.0f }, defaultTone[v]);
        number (voiceParam (v, "Pan"), name + " pan", { -1.0f, 1.0f }, defaultPan[v], [] (float p, int)
                {
                    if (std::abs (p) < 0.03f)
                        return String ("Centre");
                    return (p < 0 ? "L " : "R ") + String (roundToInt (std::abs (p) * 100.0f));
                });
    }
    return layout;
}

DrumProcessor::DrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "DrumState", createLayout())
{
    // The first patterns are ready-made beats, so there's something to hear straight away.
    for (int i = 0; i < (int) std::size (written) && i < drums::kPatterns; ++i)
        patterns[(size_t) i] = groove (i);
}

juce::StringArray DrumProcessor::grooveNames()
{
    juce::StringArray names;
    for (const auto& w : written)
        names.add (w.name);
    return names;
}

drums::Pattern DrumProcessor::groove (int index)
{
    drums::Pattern p;
    if (! juce::isPositiveAndBelow (index, (int) std::size (written)))
        return p;
    const auto& w = written[index];
    for (int v = 0; v < drums::kVoices; ++v)
    {
        const juce::String row (w.rows[(size_t) v]);
        for (int s = 0; s < juce::jmin (drums::kMaxSteps, row.length()); ++s)
        {
            const auto c = row[s];
            p.hits[(size_t) v][(size_t) s] = (std::uint8_t) (c == 'x' ? 1 : (c >= '2' && c <= '4') ? c - '0' : 0);
        }
    }
    const juce::String accent (w.accent);
    for (int s = 0; s < juce::jmin (drums::kMaxSteps, accent.length()); ++s)
        p.accent[(size_t) s] = accent[s] == '>' || accent[s] == 'x';
    p.length = 16;
    return p;
}

drums::Pattern DrumProcessor::randomPattern (juce::Random& random)
{
    drums::Pattern p;
    p.length = 16;
    auto& kick = p.hits[drums::kick];
    auto& snare = p.hits[drums::snare];
    kick[0] = 1;
    for (int s : { 3, 6, 7, 8, 10, 11, 14 })
        if (random.nextFloat() < 0.3f)
            kick[(size_t) s] = 1;
    const bool clap = random.nextBool();
    for (int s : { 4, 12 })
        p.hits[clap ? drums::clap : drums::snare][(size_t) s] = 1;
    if (random.nextFloat() < 0.4f)
        snare[(size_t) (random.nextBool() ? 15 : 13)] = 1;
    // Hats: straight 8ths, 16ths or offbeats, with the odd roll.
    const int hatStyle = random.nextInt (3);
    for (int s = 0; s < 16; ++s)
    {
        const bool on = hatStyle == 0 ? s % 2 == 0 : hatStyle == 1 ? true : s % 4 == 2;
        if (on)
            p.hits[hatStyle == 2 ? drums::openHat : drums::closedHat][(size_t) s] = 1;
    }
    if (random.nextFloat() < 0.5f)
        p.hits[drums::closedHat][(size_t) (11 + random.nextInt (5))] = (std::uint8_t) (2 + random.nextInt (3));
    for (int extra : { drums::rim, drums::cowbell, drums::clave, drums::lowTom })
        if (random.nextFloat() < 0.25f)
            for (int s = 0; s < 16; ++s)
                if (random.nextFloat() < 0.18f)
                    p.hits[(size_t) extra][(size_t) s] = 1;
    for (int s = 0; s < 16; s += 4)
        p.accent[(size_t) s] = random.nextFloat() < 0.6f;
    return p;
}

drums::Pattern DrumProcessor::getPattern (int index) const
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    return patterns[(size_t) juce::jlimit (0, drums::kPatterns - 1, index)];
}

void DrumProcessor::setPattern (int index, const drums::Pattern& p)
{
    const juce::SpinLock::ScopedLockType sl (patternLock);
    patterns[(size_t) juce::jlimit (0, drums::kPatterns - 1, index)] = p;
}

int DrumProcessor::getSelectedPattern() const { return (int) get (params, "pattern"); }

int DrumProcessor::getFillPattern() const { return (int) get (params, "fill") - 1; }

void DrumProcessor::selectPattern (int index)
{
    if (auto* p = params.getParameter ("pattern"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, drums::kPatterns - 1, index)));
}

void DrumProcessor::fillNow()
{
    if (getFillPattern() < 0)
    {
        const int next = getSelectedPattern() + 1;
        const int fill = next < drums::kPatterns && ! getPattern (next).isEmpty() ? next : 6;
        if (auto* param = params.getParameter ("fill"))
            param->setValueNotifyingHost (param->convertTo0to1 ((float) (fill + 1)));
    }
    fillRequested = true;
}

void DrumProcessor::playVoice (int voice, float velocity)
{
    const int note = drums::noteForVoice (voice);
    if (note >= 0)
    {
        keyboardState.noteOn (10, note, velocity);
        keyboardState.noteOff (10, note, 0.0f);
    }
}

drums::Settings DrumProcessor::readSettings() const
{
    drums::Settings s;
    for (int v = 0; v < drums::kVoices; ++v)
    {
        auto& vs = s.voices[(size_t) v];
        vs.level = get (params, voiceParam (v, "Level"));
        vs.tune = get (params, voiceParam (v, "Tune"));
        vs.decay = get (params, voiceParam (v, "Decay"));
        vs.tone = get (params, voiceParam (v, "Tone"));
        vs.pan = get (params, voiceParam (v, "Pan"));
    }
    s.accent = get (params, "accent");
    s.swing = get (params, "swing");
    s.drive = get (params, "drive");
    s.humanize = get (params, "humanize");
    s.volume = get (params, "volume");
    s.stepRate = (int) get (params, "stepRate");
    s.sequencerOn = get (params, "sequencer") > 0.5f;
    s.fillEvery = fillEveryValues[juce::jlimit (0, 4, (int) get (params, "fillEvery"))];
    return s;
}

void DrumProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate);
    keyboardState.reset();
    playing = getPattern (getSelectedPattern());
    playingPattern = getSelectedPattern();
    engine.setPatterns (&playing, nullptr);
}

bool DrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void DrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    const int numSamples = buffer.getNumSamples();
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    drums::Clock clock;
    if (hasOwnClock())
    {
        // Its own app: Start runs the clock from the top.
        const bool running = ownClockRunning.load();
        if (running && ! ownWasRunning)
            ownPpq = 0;
        ownWasRunning = running;
        clock.playing = running;
        clock.ppq = ownPpq;
        clock.bpm = get (params, "tempo");
        if (running)
            ownPpq += numSamples * clock.bpm / (60.0 * getSampleRate());
    }
    else if (auto* head = getPlayHead())
    {
        if (const auto info = head->getPosition())
        {
            clock.playing = info->getIsPlaying();
            clock.ppq = info->getPpqPosition().orFallback (0.0);
            clock.bpm = info->getBpm().orFallback (120.0);
        }
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
    const int fill = getFillPattern();
    {
        const juce::SpinLock::ScopedTryLockType sl (patternLock);
        if (sl.isLocked())
        {
            if (switchNow && wanted != playingPattern.load())
                playingPattern = wanted;
            playing = patterns[(size_t) playingPattern.load()];
            if (fill >= 0)
                playingFillPattern = patterns[(size_t) fill];
        }
    }
    lastPpq = clock.playing ? clock.ppq : -1;
    engine.setPatterns (&playing, fill >= 0 ? &playingFillPattern : nullptr);
    if (fillRequested.exchange (false))
        engine.requestFill();

    engine.setSettings (readSettings());
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.render (buffer.getWritePointer (0), right, numSamples, midi, clock);
    currentStep = engine.getCurrentStep();
    playingFill = engine.isPlayingFill();
    midi.clear();
}

juce::AudioProcessorEditor* DrumProcessor::createEditor() { return new DrumView (*this); }

//==============================================================================
juce::ValueTree DrumProcessor::patternToTree (const drums::Pattern& p)
{
    juce::ValueTree t ("Pattern");
    t.setProperty ("length", p.length, nullptr);
    for (int v = 0; v < drums::kVoices; ++v)
    {
        juce::String row;
        for (auto h : p.hits[(size_t) v])
            row << (int) h;
        t.setProperty (voiceKeys[v], row, nullptr);
    }
    juce::String accent;
    for (auto a : p.accent)
        accent << (a ? "1" : "0");
    t.setProperty ("accent", accent, nullptr);
    return t;
}

drums::Pattern DrumProcessor::patternFromTree (const juce::ValueTree& t)
{
    drums::Pattern p;
    p.length = juce::jlimit (1, drums::kMaxSteps, (int) t.getProperty ("length", 16));
    for (int v = 0; v < drums::kVoices; ++v)
    {
        const auto row = t.getProperty (voiceKeys[v]).toString();
        for (int s = 0; s < juce::jmin (drums::kMaxSteps, row.length()); ++s)
            p.hits[(size_t) v][(size_t) s] = (std::uint8_t) juce::jlimit (0, 4, (int) (row[s] - '0'));
    }
    const auto accent = t.getProperty ("accent").toString();
    for (int s = 0; s < juce::jmin (drums::kMaxSteps, accent.length()); ++s)
        p.accent[(size_t) s] = accent[s] == '1';
    return p;
}

void DrumProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = params.copyState();
    state.removeChild (state.getChildWithName ("Patterns"), nullptr);
    juce::ValueTree list ("Patterns");
    for (int i = 0; i < drums::kPatterns; ++i)
        list.appendChild (patternToTree (getPattern (i)), nullptr);
    state.appendChild (list, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void DrumProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType()))
        return;

    const auto list = state.getChildWithName ("Patterns");
    for (int i = 0; i < juce::jmin (drums::kPatterns, list.getNumChildren()); ++i)
        setPattern (i, patternFromTree (list.getChild (i)));
    state.removeChild (list, nullptr);
    params.replaceState (state);
}
