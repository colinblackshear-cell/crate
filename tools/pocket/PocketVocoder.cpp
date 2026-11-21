#include "PocketVocoder.h"

#include "PocketLooper.h"
#include "PocketTheme.h"

#include "Parameters.h"   // the vocoder's
#include "Presets.h"
#include "dsp/Chords.h"

using namespace pocket;

namespace
{
using namespace ParamIDs;

const PedalDef kDefs[kPedals] = {
    { "VOICE", "your own voice under the robot", 0xffd9b98a, 4,
      { { voiceLevel, "VOICE", KnobText::percent }, { robotLevel, "ROBOT", KnobText::percent }, { inputGain, "MIC", KnobText::semitones }, { outputGain, "OUT", KnobText::semitones } } },
    { "TUNE", "pulls your voice onto the notes", 0xff4fb39a, 4,
      { { tuneKey, "KEY", KnobText::text }, { tuneScale, "SCALE", KnobText::text }, { tuneSpeed, "SPEED", KnobText::percent }, { tuneAmount, "AMOUNT", KnobText::percent } } },
    { "HARMONY", "sing one note, hear a chord", 0xff8fc06a, 4,
      { { singToPlay, "PLAYS", KnobText::text }, { tuneKey, "KEY", KnobText::text }, { tuneScale, "SCALE", KnobText::text }, { tuneSpeed, "SPEED", KnobText::percent } } },
    { "ROBOT", "your voice shapes the synth", 0xff5fb3c4, 4,
      { { bands, "BANDS", KnobText::text }, { sharpness, "BEEP", KnobText::percent }, { gateThreshold, "GATE", KnobText::percent }, { robotLevel, "LEVEL", KnobText::percent } } },
    { "CHARACTER", "giants to chipmunks", 0xff8a6fb3, 4,
      { { formant, "SHAPE", KnobText::semitones }, { sibilance, "AIR", KnobText::percent }, { noise, "BREATH", KnobText::percent }, { vibratoDepth, "WOBBLE", KnobText::percent } } },
    { "SYNTH", "the sound the robot sings with", 0xffe0785a, 4,
      { { waveform, "WAVE", KnobText::text }, { octave, "OCTAVE", KnobText::text }, { subLevel, "SUB", KnobText::percent }, { glide, "GLIDE", KnobText::percent } } },
    { "CHORDS", "the notes the robot sings", 0xffefcb45, 4,
      { { chordKey, "KEY", KnobText::text }, { chordScale, "SCALE", KnobText::text }, { chordFlavour, "TYPE", KnobText::text }, { chordTiming, "CHANGES", KnobText::text } } },
    { "ARP", "notes in rhythm", 0xffd9534a, 4,
      { { arpMode, "MODE", KnobText::text }, { arpRate, "SPEED", KnobText::text }, { arpPattern, "PATTERN", KnobText::text }, { arpGate, "LENGTH", KnobText::percent } } },
    { "FILTER", "darker or thinner", 0xff4f8fd0, 4,
      { { filterMode, "TYPE", KnobText::text }, { cutoff, "CUTOFF", KnobText::percent }, { resonance, "RESO", KnobText::percent }, { lfoDepth, "WOBBLE", KnobText::percent } } },
    { "VINTAGE", "sing through an old mic", 0xffc9a472, 3,
      { { vintageEra, "ERA", KnobText::text }, { vintageWear, "WEAR", KnobText::percent }, { vintageAmount, "AMOUNT", KnobText::percent }, { nullptr, nullptr, KnobText::percent } } },
    { "DIRT", "drive and crunch", 0xffb0563c, 2,
      { { drive, "DRIVE", KnobText::percent }, { crush, "CRUSH", KnobText::percent }, { nullptr, nullptr, KnobText::percent }, { nullptr, nullptr, KnobText::percent } } },
    { "CHORUS", "a shimmering group of voices", 0xff63c2b7, 3,
      { { chorusMix, "MIX", KnobText::percent }, { chorusRate, "RATE", KnobText::percent }, { chorusDepth, "DEPTH", KnobText::percent }, { nullptr, nullptr, KnobText::percent } } },
    { "ECHO", "repeats in time", 0xff7d9be0, 4,
      { { delayMix, "MIX", KnobText::percent }, { delayTime, "TIME", KnobText::text }, { delayFeedback, "REPEATS", KnobText::percent }, { delayPingPong, "PING-PONG", KnobText::text } } },
    { "SPACE", "a room, a hall, a cathedral", 0xff9b8ccf, 3,
      { { reverbMix, "MIX", KnobText::percent }, { reverbSize, "SIZE", KnobText::percent }, { reverbDamping, "DARKNESS", KnobText::percent }, { nullptr, nullptr, KnobText::percent } } },
};

const char* const kKeyNames[12] = { "C", "D\xe2\x99\xad", "D", "E\xe2\x99\xad", "E", "F", "F\xe2\x99\xaf", "G", "A\xe2\x99\xad", "A", "B\xe2\x99\xad", "B" };
}

namespace pocket
{
const PedalDef& pedalDef (int pedal) { return kDefs[juce::jlimit (0, kPedals - 1, pedal)]; }
}

//==============================================================================
VocoderEngine::VocoderEngine (BeatsEngine& b) : beats (b)
{
    proc.setPlayHead (this);
    proc.setRateAndBufferSizeDetails (44100.0, kBlock);
    for (auto& v : block)
        v.assign ((size_t) kBlock, 0.0f);
    channels[0] = block[0].data();
    channels[1] = block[1].data();
    midi.ensureSize (4096);
    writerThread.startThread();
}

VocoderEngine::~VocoderEngine()
{
    stopTake();
    proc.setPlayHead (nullptr);
    writerThread.stopThread (1000);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> VocoderEngine::getPosition() const
{
    PositionInfo info;
    info.setBpm (beats.getBpm());
    info.setPpqPosition (beats.clockBeat());
    info.setIsPlaying (beats.clockRunning());
    info.setTimeSignature (TimeSignature { 4, 4 });
    return info;
}

float VocoderEngine::param (const char* id) const
{
    if (auto* v = proc.params.getRawParameterValue (id))
        return v->load();
    return 0.0f;
}

void VocoderEngine::setParam (const char* id, float value)
{
    if (auto* p = proc.params.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    changed = true;
}

void VocoderEngine::setParamNormalised (const char* id, float value01)
{
    if (auto* p = proc.params.getParameter (id))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value01));
    changed = true;
}

//==============================================================================
// Sounds
int VocoderEngine::presetCount() const { return (int) Presets::all().size(); }

juce::String VocoderEngine::presetName (int i) const
{
    return juce::isPositiveAndBelow (i, presetCount()) ? juce::String (Presets::all()[(size_t) i].name) : juce::String();
}

juce::String VocoderEngine::presetCategory (int i) const
{
    return juce::isPositiveAndBelow (i, presetCount()) ? juce::String (Presets::all()[(size_t) i].category) : juce::String();
}

int VocoderEngine::preset() const { return proc.getCurrentProgram(); }

void VocoderEngine::setPreset (int i)
{
    if (! juce::isPositiveAndBelow (i, presetCount()))
        return;
    proc.setCurrentProgram (i);
    ensureChord();
    changed = true;
}

void VocoderEngine::stepPreset (int delta)
{
    const int n = presetCount();
    setPreset (((preset() + delta) % n + n) % n);
}

// A robot with nothing to sing is silent, so a sound that brings the robot in with no chord, no drone and no
// Sing to Play gets the key's own chord (the Mac's Vocoder screen does the same).
void VocoderEngine::ensureChord()
{
    if (param (robotOn) > 0.5f && param (singToPlay) < 0.5f && param (ParamIDs::drone) < 0.5f && chord < 0 && ! fed)
        pressChord (0);
}

void VocoderEngine::feedTakesOver()
{
    fed = true;
    chord = -1;   // (no empty chord is sent: CHORDS' own is on its way)
    if (param (robotOn) < 0.5f)
        setParam (robotOn, 1.0f);
    if (param (ParamIDs::drone) > 0.5f)
        setParam (ParamIDs::drone, 0.0f);
    changed = true;
}

//==============================================================================
// Pedals
bool VocoderEngine::pedalOn (int pedal) const
{
    switch ((Pedal) pedal)
    {
        case Pedal::voice: return param (voiceOn) > 0.5f && param (voiceLevel) > 0.005f;
        case Pedal::tune: return param (tuneOn) > 0.5f;
        case Pedal::harmony: return param (tuneOn) > 0.5f && juce::roundToInt (param (singToPlay)) != 0;
        case Pedal::robot: return param (robotOn) > 0.5f;
        case Pedal::character: return std::abs (param (formant)) >= 0.5f;
        case Pedal::synth: return param (ParamIDs::drone) > 0.5f;
        case Pedal::chords: return chord >= 0;
        case Pedal::arp: return param (arpOn) > 0.5f && juce::roundToInt (param (arpMode)) != 0;
        case Pedal::filter:
        {
            const int mode = juce::roundToInt (param (filterMode));
            const bool wideOpen = mode == 1 && param (cutoff) >= 19900.0f && param (lfoDepth) < 0.01f && param (filterDrive) < 0.01f
                               && std::abs (param (voiceFollow)) < 0.01f;
            return mode != 0 && ! wideOpen;
        }
        case Pedal::vintage: return param (vintageOn) > 0.5f;
        case Pedal::dirt: return param (drive) > 0.005f || param (crush) > 0.005f;
        case Pedal::chorus: return param (chorusMix) > 0.005f;
        case Pedal::echo: return param (delayMix) > 0.005f;
        case Pedal::space: return param (reverbMix) > 0.005f;
    }
    return false;
}

int VocoderEngine::pedalsOn() const
{
    int n = 0;
    for (int i = 0; i < kPedals; ++i)
        n += pedalOn (i) ? 1 : 0;
    return n;
}

void VocoderEngine::stomp (int pedal)
{
    const bool wasOn = pedalOn (pedal);
    auto mix = [&] (const char* id, float& kept)
    {
        if (wasOn)
        {
            kept = param (id);
            setParam (id, 0.0f);
        }
        else
        {
            setParam (fxOn, 1.0f);
            setParam (id, kept);
        }
    };

    switch ((Pedal) pedal)
    {
        case Pedal::voice:
            // Your dry voice in what you hear (and record): off leaves only the robot and the effects.
            setParam (voiceOn, wasOn ? 0.0f : 1.0f);
            if (! wasOn && param (voiceLevel) < 0.05f)
                setParam (voiceLevel, 0.5f);
            break;
        case Pedal::tune: setParam (tuneOn, wasOn ? 0.0f : 1.0f); break;
        case Pedal::harmony:
            if (wasOn)
            {
                keptHarmony = (float) juce::roundToInt (param (singToPlay));
                setParam (singToPlay, 0.0f);
            }
            else
            {
                setParam (tuneOn, 1.0f);
                setParam (singToPlay, juce::jmax (1.0f, keptHarmony));
                setParam (robotOn, 1.0f);   // Harmony is the robot singing along
            }
            break;
        case Pedal::robot:
            setParam (robotOn, wasOn ? 0.0f : 1.0f);
            if (! wasOn)
                ensureChord();
            break;
        case Pedal::character:
            if (wasOn)
            {
                keptFormant = param (formant);
                setParam (formant, 0.0f);
            }
            else
                setParam (formant, keptFormant);
            break;
        case Pedal::synth: toggleDrone(); break;
        case Pedal::chords: pressChord (wasOn ? chord : 0); break;
        case Pedal::arp:
            if (wasOn)
                setParam (arpOn, 0.0f);
            else
            {
                // A note arp needs the robot's chord; without the robot, it chops your voice in time.
                if (juce::roundToInt (param (arpMode)) == 0)
                    setParam (arpMode, param (robotOn) > 0.5f ? 1.0f : 2.0f);
                setParam (arpOn, 1.0f);
            }
            break;
        case Pedal::filter:
            if (wasOn)
            {
                keptFilterMode = (float) juce::roundToInt (param (filterMode));
                keptCutoff = param (cutoff);
                setParam (filterMode, 0.0f);
            }
            else
            {
                setParam (filterMode, juce::jmax (1.0f, keptFilterMode));
                if (param (cutoff) >= 19900.0f || keptCutoff < 19900.0f)
                    setParam (cutoff, keptCutoff < 19900.0f ? keptCutoff : 1500.0f);
            }
            break;
        case Pedal::vintage: setParam (vintageOn, wasOn ? 0.0f : 1.0f); break;
        case Pedal::dirt:
            if (wasOn)
            {
                keptDrive = param (drive);
                keptCrush = param (crush);
                setParam (drive, 0.0f);
                setParam (crush, 0.0f);
            }
            else
            {
                setParam (fxOn, 1.0f);
                setParam (drive, keptDrive > 0.005f || keptCrush > 0.005f ? keptDrive : 0.35f);
                setParam (crush, keptCrush);
            }
            break;
        case Pedal::chorus: mix (chorusMix, keptChorus); break;
        case Pedal::echo: mix (delayMix, keptEcho); break;
        case Pedal::space: mix (reverbMix, keptSpace); break;
    }
    changed = true;
}

float VocoderEngine::knob01 (int pedal, int k) const
{
    const auto& d = pedalDef (pedal);
    if (k < 0 || k >= d.knobs || d.knob[k].id == nullptr)
        return 0.0f;
    if (auto* p = proc.params.getParameter (d.knob[k].id))
        return p->getValue();
    return 0.0f;
}

void VocoderEngine::setKnob01 (int pedal, int k, float v)
{
    const auto& d = pedalDef (pedal);
    if (k < 0 || k >= d.knobs || d.knob[k].id == nullptr)
        return;
    setParamNormalised (d.knob[k].id, v);
    const juce::String id (d.knob[k].id);
    // The effects only sound while the Effects switch is on, so turning one up switches it on.
    if (v > 0.0f && (id == chorusMix || id == delayMix || id == reverbMix || id == drive || id == crush))
        setParam (fxOn, 1.0f);
    // Turning your voice up brings it in; all the way down takes it out.
    if (id == voiceLevel)
        setParam (voiceOn, v > 0.005f ? 1.0f : 0.0f);
    // The key's knobs are the same settings as the chord buttons', so they move the chord too.
    if ((id == chordKey || id == chordScale || id == chordFlavour) && chord >= 0)
        requestChord (padNotes (chord));
}

void VocoderEngine::resetKnob (int pedal, int k)
{
    const auto& d = pedalDef (pedal);
    if (k < 0 || k >= d.knobs || d.knob[k].id == nullptr)
        return;
    if (auto* p = proc.params.getParameter (d.knob[k].id))
        setKnob01 (pedal, k, p->getDefaultValue());
}

juce::String VocoderEngine::knobText (int pedal, int k) const
{
    const auto& d = pedalDef (pedal);
    if (k < 0 || k >= d.knobs || d.knob[k].id == nullptr)
        return {};
    auto* p = proc.params.getParameter (d.knob[k].id);
    if (p == nullptr)
        return {};
    switch (d.knob[k].text)
    {
        case KnobText::percent: return juce::String (juce::roundToInt (p->getValue() * 100.0f));
        case KnobText::semitones:
        {
            const int st = juce::roundToInt (param (d.knob[k].id));
            return (st > 0 ? "+" : "") + juce::String (st);
        }
        case KnobText::text: break;
    }
    auto t = p->getCurrentValueAsText();
    if (juce::String (d.knob[k].id) == vintageEra)
        t = t.upToFirstOccurrenceOf (" ", false, false);
    return t.toUpperCase();
}

//==============================================================================
// Chords
int VocoderEngine::key() const { return juce::jlimit (0, 11, juce::roundToInt (param (chordKey))); }
bool VocoderEngine::minor() const { return juce::roundToInt (param (chordScale)) == 1; }

juce::String VocoderEngine::keyName (int k) { return juce::String (juce::CharPointer_UTF8 (kKeyNames[juce::jlimit (0, 11, k)])); }
juce::String VocoderEngine::keyLabel() const { return keyName (key()).toUpperCase() + (minor() ? " MINOR" : " MAJOR"); }

juce::Array<int> VocoderEngine::notesOf (int degree) const
{
    juce::Array<int> notes;
    for (int n : vox::Chords::build (juce::roundToInt (param (chordKey)), juce::roundToInt (param (chordScale)), degree,
                                     juce::roundToInt (param (chordFlavour))))
        notes.add (n);
    return notes;
}

juce::String VocoderEngine::chordName (int degree) const
{
    const std::string raw = vox::Chords::name (juce::roundToInt (param (chordKey)), juce::roundToInt (param (chordScale)), degree,
                                               juce::roundToInt (param (chordFlavour)));
    juce::String s (raw);
    juce::String out = s.substring (0, 1);
    auto rest = s.substring (1);
    if (rest.startsWith ("b"))
    {
        out += U8("\xe2\x99\xad");
        rest = rest.substring (1);
    }
    else if (rest.startsWith ("#"))
    {
        out += U8("\xe2\x99\xaf");
        rest = rest.substring (1);
    }
    if (rest.startsWith ("dim"))
        rest = "m" + U8("\xc2\xb0") + rest.substring (3);
    return out + rest;
}

void VocoderEngine::requestChord (const juce::Array<int>& notes)
{
    // On the clock the chord changes on the next beat, in time; with the clock stopped it sounds at once.
    proc.requestChord (notes, ! beats.clockRunning());
}

void VocoderEngine::pressChord (int degree)
{
    fed = false;   // the vocoder's own chord buttons take the chords back
    if (degree == chord || degree < 0)
    {
        chord = -1;
        proc.requestChord ({}, true);
    }
    else
    {
        chord = juce::jlimit (0, 6, degree);
        if (param (robotOn) < 0.5f)
            setParam (robotOn, 1.0f);   // chords are sung by the robot
        if (param (ParamIDs::drone) > 0.5f)
            setParam (ParamIDs::drone, 0.0f);
        requestChord (padNotes (chord));
    }
    changed = true;
}

juce::Array<int> VocoderEngine::padNotes (int pad) const
{
    if (pad < 7)
        return notesOf (juce::jlimit (0, 6, pad));
    auto five = notesOf (4);
    const int root = five.isEmpty() ? 55 : five[0];
    return { root, root + 4, root + 7, root + 10 };
}

juce::String VocoderEngine::padName (int pad) const
{
    return pad < 7 ? chordName (pad) : keyName ((key() + 7) % 12) + "7";
}

juce::String VocoderEngine::padNumeral (int pad) const
{
    static const char* const majorN[7] = { "I", "ii", "iii", "IV", "V", "vi", "vii\xc2\xb0" };
    static const char* const minorN[7] = { "i", "ii\xc2\xb0", "III", "iv", "v", "VI", "VII" };
    if (pad >= 7)
        return "V7";
    return juce::String (juce::CharPointer_UTF8 ((minor() ? minorN : majorN)[juce::jlimit (0, 6, pad)]));
}

void VocoderEngine::pressPad (int pad)
{
    fed = false;
    if (pad < 7)
        return pressChord (pad);
    if (chord == pad)
    {
        chord = -1;
        proc.requestChord ({}, true);
    }
    else
    {
        chord = pad;
        if (param (robotOn) < 0.5f)
            setParam (robotOn, 1.0f);
        if (param (ParamIDs::drone) > 0.5f)
            setParam (ParamIDs::drone, 0.0f);
        requestChord (padNotes (pad));
    }
    changed = true;
}

bool VocoderEngine::drone() const { return param (ParamIDs::drone) > 0.5f; }

void VocoderEngine::toggleDrone()
{
    if (drone())
        setParam (ParamIDs::drone, 0.0f);
    else
    {
        setParam (ParamIDs::drone, 1.0f);
        setParam (robotOn, 1.0f);   // a drone is the robot singing one note
        setParam (droneNote, (float) (48 + key()));
        if (chord >= 0)
        {
            chord = -1;
            proc.requestChord ({}, true);
        }
    }
    changed = true;
}

void VocoderEngine::setKey (int k, bool isMinor)
{
    k = juce::jlimit (0, 11, k);
    const int scale = isMinor ? 1 : 0;
    setParam (chordKey, (float) k);
    setParam (chordScale, (float) scale);
    // Auto-tune follows the chord buttons' key, so your voice and the chords agree.
    setParam (tuneKey, (float) k);
    setParam (tuneScale, (float) ChordNames::tuneScales[scale]);
    if (drone())
        setParam (droneNote, (float) (48 + k));
    if (chord >= 0)
        requestChord (padNotes (chord));
    changed = true;
}

//==============================================================================
// REC A TAKE
bool VocoderEngine::startTake (const juce::File& file)
{
    if (recording.load())
        return false;
    file.deleteFile();
    file.getParentDirectory().createDirectory();
    auto stream = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
    if (w == nullptr)
        return false;
    stream.release();   // the writer owns it now
    takenSamples = 0;
    auto threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w.release(), writerThread, 32768);
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        writer = std::move (threaded);
    }
    recording = true;
    return true;
}

void VocoderEngine::stopTake()
{
    recording = false;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> old;
    {
        const juce::SpinLock::ScopedLockType lock (writerLock);
        old = std::move (writer);
    }
    old.reset();   // flushes and closes the file
}

//==============================================================================
// Saving
void VocoderEngine::save (const juce::File& file) const
{
    auto state = proc.params.copyState();
    state.setProperty ("pocketChord", chord, nullptr);
    if (auto xml = state.createXml())
    {
        file.getParentDirectory().createDirectory();
        xml->writeTo (file);
    }
}

void VocoderEngine::load (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (proc.params.state.getType()))
        return;
    auto state = juce::ValueTree::fromXml (*xml);
    const int saved = juce::jlimit (-1, kChordPads - 1, (int) state.getProperty ("pocketChord", -1));
    proc.params.replaceState (state);
    chord = -1;
    if (saved >= 0)
        pressPad (saved);
    else
        proc.requestChord ({}, true);
    changed = false;
}

//==============================================================================
// Audio thread
void VocoderEngine::prepare (double rate)
{
    sampleRate = rate;
    proc.setRateAndBufferSizeDetails (rate, kBlock);
    proc.prepareToPlay (rate, kBlock);
}

void VocoderEngine::render (float* left, float* right, int numSamples)
{
    for (int done = 0; done < numSamples; done += kBlock)
    {
        const int n = juce::jmin (kBlock, numSamples - done);

        // the voice: the mic, on both channels
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            int used = 0;
            for (int c = 0; c < inChannels && c < 2; ++c)
                if (inPtr != nullptr && inPtr[c] != nullptr)
                {
                    s += inPtr[c][done + i];
                    ++used;
                }
            if (used > 1)
                s /= (float) used;
            channels[0][i] = channels[1][i] = s;
            p = juce::jmax (p, std::abs (s));
        }
        if (p > micPeak.load())
            micPeak = p;

        juce::AudioBuffer<float> buffer (channels, 2, n);
        midi.clear();
        proc.processBlock (buffer, midi);

        if (recording.load())
        {
            const juce::SpinLock::ScopedTryLockType lock (writerLock);
            if (lock.isLocked() && writer != nullptr)
            {
                writer->write (buffer.getArrayOfReadPointers(), n);
                takenSamples += n;
            }
        }

        for (int i = 0; i < n; ++i)
        {
            left[done + i] += channels[0][i];
            if (right != nullptr)
                right[done + i] += channels[1][i];
        }
    }
}

//==============================================================================
// The self-check: the real engine, fed made-up voices.
namespace
{
// A voice-like sound: a 120 Hz sawtooth (the glottal buzz) through three resonances (an "ah").
struct FakeVoice
{
    explicit FakeVoice (double rate) : sr (rate)
    {
        const double centre[3] = { 700.0, 1200.0, 2600.0 }, width[3] = { 90.0, 110.0, 160.0 };
        for (int i = 0; i < 3; ++i)
        {
            r[i] = std::exp (-juce::MathConstants<double>::pi * width[i] / sr);
            a1[i] = 2.0 * r[i] * std::cos (juce::MathConstants<double>::twoPi * centre[i] / sr);
            a2[i] = -r[i] * r[i];
        }
    }

    float next (float level)
    {
        phase += 120.0 / sr;
        if (phase >= 1.0)
            phase -= 1.0;
        const double buzz = 2.0 * phase - 1.0;
        double out = 0.0;
        for (int i = 0; i < 3; ++i)
        {
            const double y = (1.0 - r[i]) * buzz + a1[i] * y1[i] + a2[i] * y2[i];
            y2[i] = y1[i];
            y1[i] = y;
            out += y;
        }
        return (float) (out * 1.2) * level;
    }

    double sr, phase = 0.0, r[3] {}, a1[3] {}, a2[3] {}, y1[3] {}, y2[3] {};
};

struct Rms
{
    double sum = 0.0;
    int count = 0;
    float peak = 0.0f;
    void add (float s) { sum += (double) s * s; ++count; peak = juce::jmax (peak, std::abs (s)); }
    double value() const { return count > 0 ? std::sqrt (sum / count) : 0.0; }
};
}

juce::String VocoderEngine::selfCheck (juce::String& report)
{
    juce::StringArray problems;
    const double sr = 44100.0;
    constexpr int n = 256;

    {
        auto beatsEngine = std::make_unique<BeatsEngine>();
        auto voc = std::make_unique<VocoderEngine> (*beatsEngine);
        voc->prepare (sr);

        // the sounds and the chords
        report << "sounds: " << voc->presetCount() << " (the vocoder's own)\n";
        if (voc->presetCount() != (int) Presets::all().size() || voc->presetCount() < 2)
            problems.add ("preset count does not match the vocoder's");
        voc->setPreset (1);
        voc->setKey (0, true);
        juce::StringArray names;
        for (int d = 0; d < 7; ++d)
            names.add (voc->chordName (d));
        report << "C minor chords: " << names.joinIntoString (" ") << "\n";
        if (names.joinIntoString (" ") != U8("Cm Dm\xc2\xb0 E\xe2\x99\xad Fm Gm A\xe2\x99\xad B\xe2\x99\xad"))
            problems.add ("C minor chord names are " + names.joinIntoString (" "));
        if (voc->keyLabel() != "C MINOR")
            problems.add ("key label is " + voc->keyLabel());
        if (voc->activeChord() != 0)
            problems.add ("a robot sound did not get the key's chord to sing");

        // a held chord with a voice through it, in bursts: loud, soft, silent, loud again
        voc->pressChord (0);   // let go
        voc->pressChord (5);   // A flat
        FakeVoice voice (sr);
        std::vector<float> in ((size_t) n), l ((size_t) n), r ((size_t) n);
        auto run = [&] (double seconds, float level, double measureFrom)
        {
            Rms rms;
            const int total = (int) (seconds * sr / n);
            for (int b = 0; b < total; ++b)
            {
                for (int i = 0; i < n; ++i)
                    in[(size_t) i] = voice.next (level);
                const float* ptr = in.data();
                voc->setInput (&ptr, 1);
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                voc->render (l.data(), r.data(), n);
                if ((double) b * n / sr >= measureFrom)
                    for (int i = 0; i < n; ++i)
                        rms.add (0.5f * (l[(size_t) i] + r[(size_t) i]));
            }
            return rms;
        };
        run (1.2, 0.3f, 99.0);                      // the chord takes a beat to come in on the vocoder's own clock
        const auto loud = run (0.6, 0.3f, 0.2);
        const auto soft = run (0.6, 0.05f, 0.3);
        const auto quiet = run (0.8, 0.0f, 0.4);
        const auto loud2 = run (0.6, 0.3f, 0.2);
        report << "output RMS: voice loud " << juce::String (loud.value(), 4) << ", soft " << juce::String (soft.value(), 4)
               << ", stopped " << juce::String (quiet.value(), 5) << ", loud again " << juce::String (loud2.value(), 4) << "\n";
        if (loud.value() < 0.003)
            problems.add ("the vocoder is silent with a voice and a held chord (RMS " + juce::String (loud.value(), 5) + ")");
        if (quiet.value() > loud.value() * 0.1)
            problems.add ("the output does not drop when the voice stops (" + juce::String (quiet.value(), 5) + " against " + juce::String (loud.value(), 5) + ")");
        if (soft.value() > loud.value() * 0.8 || soft.value() < quiet.value())
            problems.add ("the output does not follow the voice's loudness (soft " + juce::String (soft.value(), 5) + ")");
        if (loud2.value() < loud.value() * 0.5)
            problems.add ("the output does not come back with the voice");

        // each pedal stomps on and off
        juce::StringArray stuck;
        for (int p = 0; p < kPedals; ++p)
        {
            const bool before = voc->pedalOn (p);
            voc->stomp (p);
            const bool flipped = voc->pedalOn (p) != before;
            voc->stomp (p);
            if (! flipped || voc->pedalOn (p) != before)
                stuck.add (pedalDef (p).name);
        }
        report << "pedals: " << kPedals << ", " << (stuck.isEmpty() ? juce::String ("all stomp on and off") : "stuck: " + stuck.joinIntoString (", ")) << "\n";
        if (stuck.size() > 0)
            problems.add ("these pedals do not stomp on and off: " + stuck.joinIntoString (", "));

        // a knob moves the setting it says it does
        voc->setKnob01 ((int) Pedal::robot, 0, 1.0f);
        if (voc->knobText ((int) Pedal::robot, 0) != "40")
            problems.add ("the Robot BANDS knob at the top reads " + voc->knobText ((int) Pedal::robot, 0) + ", not 40");
        // the saved state comes back
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pocket-vocoder-check.xml");
        voc->setPreset (3);
        voc->setKey (8, false);
        voc->pressChord (2);
        voc->save (file);
        auto again = std::make_unique<VocoderEngine> (*beatsEngine);
        again->load (file);
        file.deleteFile();
        report << "saved and loaded: preset " << again->preset() << ", " << again->keyLabel() << ", chord " << again->activeChord() << "\n";
        if (again->preset() != 3 || again->keyLabel() != U8("A\xe2\x99\xad MAJOR") || again->activeChord() != 2)
            problems.add ("the saved sound did not come back (preset, key and chord)");
    }

    // Loops > FROM > VOCODER: it records the vocoder with HEAR ME off, and the speaker stays silent
    {
        const double bpm = 120.0;
        auto beatsEngine = std::make_unique<BeatsEngine>();
        auto loops = std::make_unique<LoopsEngine> (*beatsEngine);
        auto voc = std::make_unique<VocoderEngine> (*beatsEngine);
        loops->setVocoderSource (voc.get(), &voc->monitorFlag());
        loops->prepare (sr);
        loops->setLatency (0);
        loops->setCountInBars (0);
        loops->setMic (true, true);   // heard: only HEAR ME (the monitor) decides below
        beatsEngine->setBpm (bpm);
        voc->setPreset (1);

        FakeVoice voice (sr);
        std::vector<float> in (512), l (512), r (512);
        float speaker = 0.0f;
        auto run = [&] (int samples)
        {
            for (int done = 0; done < samples; done += 512)
            {
                for (auto& s : in)
                    s = voice.next (0.3f);
                const float* ptr = in.data();
                loops->setInput (&ptr, 1);
                std::fill (l.begin(), l.end(), 0.0f);
                std::fill (r.begin(), r.end(), 0.0f);
                loops->render (l.data(), r.data(), 512);
                if (loops->state (0) == LoopState::recording || loops->state (0) == LoopState::waiting)
                    for (int i = 0; i < 512; ++i)
                        speaker = juce::jmax (speaker, std::abs (l[(size_t) i]), std::abs (r[(size_t) i]));
            }
        };
        run (4096);
        loops->arm (0, LoopSource::vocoder, 1);
        for (int guard = 0; guard < 400000 && loops->state (0) != LoopState::playing; guard += 512)
            run (512);
        float took = 0.0f;
        int len = 0;
        if (auto take = loops->takeOf (0))
        {
            len = take->length.load();
            for (int i = 0; i < len; ++i)
                took = juce::jmax (took, std::abs (take->data[(size_t) i]));
        }
        const double expected = 4.0 * 60.0 / bpm * sr;
        report << "loop from the vocoder: 1 bar = " << len << " samples (expected " << juce::String (expected, 1) << "), peak " << juce::String (took, 3)
               << ", speaker peak while HEAR ME is off " << juce::String (speaker, 6) << "\n";
        if (loops->state (0) != LoopState::playing)
            problems.add ("the vocoder layer never finished recording");
        else
        {
            if (std::abs ((double) len - expected) > 2.0)
                problems.add ("the vocoder layer is " + juce::String (len) + " samples, expected about " + juce::String (juce::roundToInt (expected)));
            if (took < 0.01f)
                problems.add ("the vocoder layer recorded silence");
        }
        if (speaker > 1.0e-5f)
            problems.add ("the vocoder was heard in the speaker with HEAR ME off (peak " + juce::String (speaker, 5) + ")");

        // and HEAR ME puts it in the speaker
        loops->clear (0);
        loops->setTransport (false);
        voc->setMonitor (true);
        float heard = 0.0f;
        for (int k = 0; k < 40; ++k)
        {
            for (auto& s : in)
                s = voice.next (0.3f);
            const float* ptr = in.data();
            loops->setInput (&ptr, 1);
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            loops->render (l.data(), r.data(), 512);
            if (k > 20)
                for (int i = 0; i < 512; ++i)
                    heard = juce::jmax (heard, std::abs (l[(size_t) i]));
        }
        report << "speaker peak with HEAR ME on " << juce::String (heard, 3) << "\n";
        if (heard < 0.01f)
            problems.add ("HEAR ME did not put the vocoder in the speaker");
    }
    return problems.joinIntoString ("\n");
}
