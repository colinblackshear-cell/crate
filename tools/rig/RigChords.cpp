#include "RigChords.h"

#include "dsp/Chords.h"   // the vocoder's chords of a key

namespace
{
const char* const noteNames[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };

// Chord shapes by their pitch classes above the root, most common first (so a set that two
// names fit gets the plainer one).
struct Shape { const char* suffix; std::initializer_list<int> intervals; };
const Shape shapes[] = {
    { "", { 0, 4, 7 } }, { "m", { 0, 3, 7 } }, { "5", { 0, 7 } }, { "sus4", { 0, 5, 7 } }, { "sus2", { 0, 2, 7 } },
    { "dim", { 0, 3, 6 } }, { "aug", { 0, 4, 8 } },
    { "7", { 0, 4, 7, 10 } }, { "maj7", { 0, 4, 7, 11 } }, { "m7", { 0, 3, 7, 10 } }, { "m7b5", { 0, 3, 6, 10 } },
    { "dim7", { 0, 3, 6, 9 } }, { "mMaj7", { 0, 3, 7, 11 } }, { "6", { 0, 4, 7, 9 } }, { "m6", { 0, 3, 7, 9 } },
    { "7sus4", { 0, 5, 7, 10 } }, { "add9", { 0, 2, 4, 7 } }, { "madd9", { 0, 2, 3, 7 } }, { "aug7", { 0, 4, 8, 10 } },
    { "9", { 0, 2, 4, 7, 10 } }, { "maj9", { 0, 2, 4, 7, 11 } }, { "m9", { 0, 2, 3, 7, 10 } }, { "6/9", { 0, 2, 4, 7, 9 } },
    { "7b9", { 0, 1, 4, 7, 10 } }, { "7#9", { 0, 3, 4, 7, 10 } }, { "add11", { 0, 4, 5, 7 } },
    { "11", { 0, 2, 4, 5, 7, 10 } }, { "m11", { 0, 2, 3, 5, 7, 10 } }, { "13", { 0, 2, 4, 7, 9, 10 } },
    { "maj13", { 0, 2, 4, 7, 9, 11 } }, { "m13", { 0, 2, 3, 7, 9, 10 } },
    // Without their fifths, as players often voice them.
    { "7", { 0, 4, 10 } }, { "maj7", { 0, 4, 11 } }, { "m7", { 0, 3, 10 } }, { "9", { 0, 2, 4, 10 } },
    { "maj9", { 0, 2, 4, 11 } }, { "m9", { 0, 2, 3, 10 } }, { "", { 0, 4 } }, { "m", { 0, 3 } },
};

int pitchSet (std::initializer_list<int> intervals)
{
    int mask = 0;
    for (int i : intervals)
        mask |= 1 << i;
    return mask;
}

juce::Array<int> sortedUnique (const juce::Array<int>& in)
{
    juce::Array<int> out;
    for (int n : in)
        if (n >= 0 && n < 128)
            out.addIfNotAlreadyThere (n);
    out.sort();
    return out;
}

juce::Array<int> toArray (const std::vector<int>& v, int add = 0)
{
    juce::Array<int> out;
    for (int n : v)
        out.add (n + add);
    return out;
}

constexpr int kProgressionSource = 300;   // the progression's voice
}

RigChords::RigChords()
{
    for (auto& pad : live)
        for (auto& n : pad)
            n = -1;
    for (auto& s : steps)
        s = -1;
    reset();
}

juce::String RigChords::triggerName (int t)
{
    switch ((Trigger) t)
    {
        case Trigger::pads:     return "Launchkey pads";
        case Trigger::lowKeys:  return "Lowest 16 keys";
        case Trigger::everyKey: return "Every key";
        case Trigger::screen:
        default:                return "Clicks only";
    }
}

const std::array<double, 5>& RigChords::stepLengths()
{
    static const std::array<double, 5> lengths { 1.0, 2.0, 4.0, 8.0, 16.0 };
    return lengths;
}

juce::String RigChords::stepLengthName (int i)
{
    static const char* names[] = { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars" };
    return names[juce::jlimit (0, 4, i)];
}

const std::array<int, 4>& RigChords::strumTimes()
{
    static const std::array<int, 4> times { 0, 15, 40, 90 };
    return times;
}

juce::String RigChords::strumName (int i)
{
    static const char* names[] = { "No strum", "Fast strum", "Strum", "Slow strum" };
    return names[juce::jlimit (0, 3, i)];
}

//==============================================================================
void RigChords::setPad (int pad, const juce::Array<int>& notes, const juce::String& name)
{
    if (pad < 0 || pad >= kPads)
        return;
    auto clean = sortedUnique (notes);
    while (clean.size() > kMaxNotes)
        clean.remove (clean.size() - 1);
    pads[(size_t) pad].notes = clean;
    pads[(size_t) pad].name = name;
    syncPad (pad);
}

void RigChords::syncPad (int pad)
{
    const auto& notes = pads[(size_t) pad].notes;
    for (int i = 0; i < kMaxNotes; ++i)
        live[(size_t) pad][(size_t) i] = i < notes.size() ? notes[i] : -1;
}

juce::String RigChords::padName (int pad) const
{
    const auto& p = getPad (pad);
    return p.name.isNotEmpty() ? p.name : nameOf (p.notes);
}

void RigChords::toggleNote (int pad, int note)
{
    if (pad < 0 || pad >= kPads || note < 0 || note > 127)
        return;
    auto notes = getPad (pad).notes;
    if (notes.contains (note))
        notes.removeFirstMatchingValue (note);
    else if (notes.size() < kMaxNotes)
        notes.add (note);
    setPad (pad, notes, {});   // a changed chord is named by its notes again
}

void RigChords::invert (int pad, int direction)
{
    auto notes = getPad (pad).notes;
    if (notes.size() < 2)
        return;
    if (direction >= 0)
    {
        // The lowest note up an octave at a time until it's above the top note.
        int moved = notes.getFirst();
        notes.remove (0);
        while (moved <= notes.getLast() && moved + 12 <= 127)
            moved += 12;
        notes.add (moved);
    }
    else
    {
        int high = notes.getLast();
        notes.remove (notes.size() - 1);
        while (high >= notes.getFirst() && high - 12 >= 0)
            high -= 12;
        notes.insert (0, high);
    }
    setPad (pad, notes, getPad (pad).name);
}

void RigChords::octave (int pad, int delta)
{
    auto notes = getPad (pad).notes;
    if (notes.isEmpty())
        return;
    const int shift = 12 * delta;
    if (notes.getFirst() + shift < 0 || notes.getLast() + shift > 127)
        return;
    for (auto& n : notes)
        n += shift;
    setPad (pad, notes, getPad (pad).name);
}

void RigChords::spread (int pad)
{
    auto notes = getPad (pad).notes;
    if (notes.size() < 3)
        return;
    // Every other note above the root up an octave: a close C E G B becomes C G E B, wide and open.
    for (int i = 1; i < notes.size(); i += 2)
        if (notes[i] + 12 <= 127)
            notes.set (i, notes[i] + 12);
    setPad (pad, notes, getPad (pad).name);
}

void RigChords::close (int pad)
{
    auto notes = getPad (pad).notes;
    if (notes.size() < 2)
        return;
    const int root = notes.getFirst();
    juce::Array<int> packed;
    for (int n : notes)
    {
        int m = root + ((n - root) % 12);
        while (packed.contains (m))
            m += 12;
        packed.add (m);
    }
    setPad (pad, packed, getPad (pad).name);
}

void RigChords::copyPad (int from, int to)
{
    if (from < 0 || from >= kPads || to < 0 || to >= kPads || from == to)
        return;
    const auto p = getPad (from);
    setPad (to, p.notes, p.name);
}

void RigChords::fillFromKey (int key, int scale, int type)
{
    fillKey = juce::jlimit (0, 11, key);
    fillScale = juce::jlimit (0, vox::Chords::numScales - 1, scale);
    fillType = juce::jlimit (0, vox::Chords::numFlavours - 1, type);
    for (int degree = 0; degree < 7; ++degree)
        setPad (degree, toArray (vox::Chords::build (fillKey, fillScale, degree, fillType)));
    setPad (7, toArray (vox::Chords::build (fillKey, fillScale, 0, fillType), 12));
}

juce::StringArray RigChords::starterNames()
{
    return { "Pop: C  G  Am  F", "Neo soul 7ths: Dm7  Gm7  C7  Fmaj7", "Dreamy 9ths: Fmaj9  Dm9  Gm9  C9",
             "Dark minor: Am  F  C  G", "Power chords: E5  C5  D5", "Blank: every pad empty" };
}

void RigChords::loadStarter (int index)
{
    struct Starter { int key, scale, type; std::array<int, 4> progression; };
    // vox::Chords: scale 0 major, 1 minor; type 0 simple, 1 seventh, 2 ninth, 7 power.
    static const Starter starters[] = { { 0, 0, 0, { 0, 4, 5, 3 } }, { 2, 1, 1, { 0, 3, 6, 2 } }, { 5, 0, 2, { 0, 5, 1, 4 } },
                                        { 9, 1, 0, { 0, 5, 2, 6 } }, { 4, 1, 7, { 0, 5, 6, 0 } } };
    for (auto& s : steps)
        s = -1;
    for (int i = 0; i < kPads; ++i)
        setPad (i, {});
    if (index < 0 || index >= (int) std::size (starters))
        return;
    const auto& s = starters[index];
    fillFromKey (s.key, s.scale, s.type);
    for (int i = 0; i < 4; ++i)
        steps[(size_t) i] = s.progression[(size_t) i];
}

juce::String RigChords::noteName (int note)
{
    return juce::String (noteNames[((note % 12) + 12) % 12]) + juce::String (note / 12 - 2);
}

juce::String RigChords::nameOf (const juce::Array<int>& input)
{
    const auto notes = sortedUnique (input);
    if (notes.isEmpty())
        return {};
    int pcs = 0;
    for (int n : notes)
        pcs |= 1 << (n % 12);
    const int bass = notes.getFirst() % 12;
    if (pcs == (1 << bass))
        return noteNames[bass];

    auto rotated = [pcs] (int root)
    {
        int m = 0;
        for (int i = 0; i < 12; ++i)
            if (pcs & (1 << ((root + i) % 12)))
                m |= 1 << i;
        return m;
    };
    // The bass note as the root first (C E G A is C6, not Am7/C), then every other note in the chord.
    juce::Array<int> roots { bass };
    for (int n : notes)
        roots.addIfNotAlreadyThere (n % 12);
    for (int root : roots)
    {
        const int have = rotated (root);
        for (const auto& shape : shapes)
            if (have == pitchSet (shape.intervals))
            {
                juce::String name = juce::String (noteNames[root]) + shape.suffix;
                if (root != bass)
                    name << "/" << noteNames[bass];
                return name;
            }
    }
    juce::StringArray list;
    for (int n : notes)
        list.addIfNotAlreadyThere (noteNames[n % 12]);
    return list.joinIntoString (" ");
}

//==============================================================================
void RigChords::queue (int pad, float velocity)
{
    const auto scope = pressFifo.write (1);
    if (scope.blockSize1 > 0)
        pressQueue[(size_t) scope.startIndex1] = { pad, velocity };
}

void RigChords::press (int pad, float velocity)
{
    if (pad < 0 || pad >= kPads)
        return;
    if (hold.load())
    {
        // Latched: the same pad again lets it go.
        const bool wasOn = down[(size_t) pad].load();
        for (auto& d : down)
            d = false;
        down[(size_t) pad] = ! wasOn;
    }
    else
    {
        down[(size_t) pad] = true;
    }
    queue (pad, juce::jlimit (0.05f, 1.0f, velocity));
}

void RigChords::release (int pad)
{
    if (pad < 0 || pad >= kPads)
        return;
    if (! hold.load())
        down[(size_t) pad] = false;
    queue (-(pad + 1), 0.0f);
}

void RigChords::audition (int pad, int ms)
{
    if (isEmpty (pad) || hold.load())
        return;
    press (pad, 0.75f);
    juce::Timer::callAfterDelay (ms, [this, pad] { release (pad); });
}

void RigChords::stopAll()
{
    for (auto& d : down)
        d = false;
    stopRequested = true;
}

bool RigChords::isPadDown (int pad) const
{
    return pad >= 0 && pad < kPads && down[(size_t) pad].load();
}

bool RigChords::isNoteSounding (int note) const
{
    if (note < 0 || note > 127)
        return false;
    return (soundingMask[(size_t) (note / 64)].load() >> (note % 64)) & 1;
}

bool RigChords::anySounding() const
{
    for (auto& d : down)
        if (d.load())
            return true;
    return soundingMask[0].load() != 0 || soundingMask[1].load() != 0;
}

int RigChords::resolveTarget (int keysTo) const
{
    const int t = target.load();
    return t == kFollowKeys ? keysTo : t;
}

void RigChords::catchFromKeys (int pad)
{
    for (int i = 0; i < 2; ++i)
    {
        catchMask[(size_t) i] = 0;
        catchHeld[(size_t) i] = 0;
    }
    catchDone = false;
    catchPad = juce::jlimit (-1, kPads - 1, pad);
}

bool RigChords::serviceCatch()
{
    if (! catchDone.exchange (false))
        return false;
    const int pad = catchPad.exchange (-1);
    if (pad < 0)
        return false;
    juce::Array<int> notes;
    for (int n = 0; n < 128; ++n)
        if ((catchMask[(size_t) (n / 64)].load() >> (n % 64)) & 1)
            notes.add (n);
    setPad (pad, notes);
    return true;
}

int RigChords::progressionSteps() const
{
    for (int i = kSteps; i > 0; --i)
        if (steps[(size_t) i - 1].load() >= 0)
            return i;
    return 0;
}

double RigChords::progressionBeats() const
{
    return progressionSteps() * stepLengths()[(size_t) juce::jlimit (0, 4, stepLength.load())];
}

//==============================================================================
void RigChords::prepare (double rate)
{
    sampleRate = rate > 0 ? rate : 44100.0;
}

void RigChords::takeTriggers (juce::MidiBuffer& midi, int padChannel, int splitNote)
{
    triggers.clear();
    if (midi.isEmpty())
        return;
    const auto mode = (Trigger) trigger.load();
    const bool catching = catchPad.load() >= 0;
    bool anyTaken = false;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        anyTaken = anyTaken || (msg.isNoteOff() && takenBy[(size_t) msg.getChannel()][(size_t) msg.getNoteNumber()] > 0);
    }
    if (mode == Trigger::screen && ! catching && ! anyTaken)
        return;

    // Note-offs of trigger notes are always taken (even if the mode changed while they were
    // down), as what their note-on triggered.
    juce::MidiBuffer kept;
    bool changed = false;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (! msg.isNoteOnOrOff())
        {
            kept.addEvent (msg, m.samplePosition);
            continue;
        }
        const int ch = msg.getChannel(), note = msg.getNoteNumber();
        const bool isPad = padChannel > 0 && ch == padChannel;
        auto& wasTaken = takenBy[(size_t) ch][(size_t) note];
        if (msg.isNoteOn())
        {
            int code = -1;   // what it triggers: channel 1 = a pad (0-15), channel 2 = a key's shape
            int channel = 1;
            if (mode == Trigger::pads && isPad && note >= RigRouting::kFirstPadNote && note < RigRouting::kFirstPadNote + kPads)
                code = note - RigRouting::kFirstPadNote;
            else if (mode == Trigger::lowKeys && ! isPad && note >= lowKey.load() && note < lowKey.load() + kPads)
                code = note - lowKey.load();
            else if (mode == Trigger::everyKey && ! isPad && (splitNote <= 0 || note >= splitNote))
            {
                code = note;
                channel = 2;
            }
            if (code >= 0)
            {
                wasTaken = (juce::int16) (channel * 256 + code + 1);
                triggers.addEvent (juce::MidiMessage::noteOn (channel, code, msg.getFloatVelocity()), m.samplePosition);
                changed = true;
                continue;
            }
            if (catching && ! isPad)
            {
                catchMask[(size_t) (note / 64)].fetch_or (juce::uint64 (1) << (note % 64));
                catchHeld[(size_t) (note / 64)].fetch_or (juce::uint64 (1) << (note % 64));
            }
        }
        else
        {
            if (wasTaken > 0)
            {
                const int channel = wasTaken / 256, code = wasTaken % 256 - 1;
                wasTaken = 0;
                triggers.addEvent (juce::MidiMessage::noteOff (channel, code), m.samplePosition);
                changed = true;
                continue;
            }
            if (catching && ! isPad)
            {
                catchHeld[(size_t) (note / 64)].fetch_and (~(juce::uint64 (1) << (note % 64)));
                if (catchHeld[0].load() == 0 && catchHeld[1].load() == 0 && (catchMask[0].load() != 0 || catchMask[1].load() != 0))
                    catchDone = true;
            }
        }
        kept.addEvent (msg, m.samplePosition);
    }
    if (changed)
        midi.swapWith (kept);
}

void RigChords::noteOn (int dest, int note, float velocity, int at, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to)
{
    if (dest <= 0 || dest >= RigRouting::kNumDests || note < 0 || note > 127 || to[(size_t) dest] == nullptr)
        return;
    auto& count = held[(size_t) dest][(size_t) note];
    if (count++ == 0)
    {
        to[(size_t) dest]->addEvent (juce::MidiMessage::noteOn (1, note, juce::jlimit (0.05f, 1.0f, velocity)), at);
        addToTake (note, velocity, at);
    }
}

void RigChords::noteOff (int dest, int note, int at, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to)
{
    if (dest <= 0 || dest >= RigRouting::kNumDests || note < 0 || note > 127)
        return;
    auto& count = held[(size_t) dest][(size_t) note];
    if (count == 0)
        return;
    if (--count == 0)
    {
        if (to[(size_t) dest] != nullptr)
            to[(size_t) dest]->addEvent (juce::MidiMessage::noteOff (1, note), at);
        addToTake (note, 0.0f, at);
    }
}

void RigChords::startVoice (int source, int pad, int transpose, float velocity, int dest, int at,
                            std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to, int strumSamples)
{
    stopVoice (source, at, to);
    if (pad < 0 || pad >= kPads || dest <= 0)
        return;
    int v = -1;
    for (int i = 0; i < kVoices && v < 0; ++i)
        if (voices[(size_t) i].source < 0)
            v = i;
    if (v < 0)
    {
        stopVoice (voices[0].source, at, to);
        v = 0;
    }
    auto& voice = voices[(size_t) v];
    voice.source = source;
    voice.dest = dest;
    voice.count = 0;
    int index = 0;
    for (const auto& n : live[(size_t) pad])
    {
        const int note = n.load();
        if (note < 0)
            break;
        const int played = note + transpose;
        if (played < 0 || played > 127)
            continue;
        const int when = at + index * strumSamples;
        ++index;
        if (strumSamples <= 0 || when < blockSize)
        {
            noteOn (dest, played, velocity, when, to);
            voice.notes[(size_t) voice.count++] = played;
        }
        else if (numPending < (int) pending.size())
        {
            // From the start of the next block.
            pending[(size_t) numPending++] = { when - blockSize, dest, played, v, velocity };
        }
    }
    sounding = pad;
    lastSoundingVoice = v;
}

void RigChords::stopVoice (int source, int at, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to)
{
    if (source < 0)
        return;
    for (int v = 0; v < kVoices; ++v)
    {
        auto& voice = voices[(size_t) v];
        if (voice.source != source)
            continue;
        for (int i = 0; i < voice.count; ++i)
            noteOff (voice.dest, voice.notes[(size_t) i], at, to);
        voice.count = 0;
        voice.source = -1;
        // Its strummed notes still to come never play.
        for (int p = numPending; --p >= 0;)
            if (pending[(size_t) p].voice == v)
                pending[(size_t) p] = pending[(size_t) --numPending];
    }
}

void RigChords::updateMask()
{
    juce::uint64 m[2] {};
    for (const auto& voice : voices)
        if (voice.source >= 0)
            for (int i = 0; i < voice.count; ++i)
                m[voice.notes[(size_t) i] / 64] |= juce::uint64 (1) << (voice.notes[(size_t) i] % 64);
    soundingMask[0] = m[0];
    soundingMask[1] = m[1];
    bool any = false;
    for (const auto& voice : voices)
        any = any || voice.source >= 0;
    if (! any)
        sounding = -1;
}

void RigChords::addToTake (int note, float velocity, int at)
{
    if (! taking.load())
        return;
    const auto scope = takeFifo.write (1);
    if (scope.blockSize1 > 0)
        takeQueue[(size_t) scope.startIndex1] = { takeBeats + at * takeBeatsPerSample, note, velocity };
}

void RigChords::process (const Clock& clock, int numSamples, int keysTo, std::array<juce::MidiBuffer*, RigRouting::kNumDests>& to)
{
    blockSize = numSamples;
    if (takeReset.exchange (false))
        takeBeats = 0;
    takeBeatsPerSample = clock.bpm / 60.0 / juce::jmax (1.0, clock.sampleRate);

    if (stopRequested.exchange (false))
    {
        for (int v = 0; v < kVoices; ++v)
            stopVoice (voices[(size_t) v].source, 0, to);
        numPending = 0;
        heldSource = -1;
        progressionStepNow = -1;
        playingStep = -1;
    }

    // Strummed notes whose time has come.
    for (int p = 0; p < numPending;)
    {
        auto& note = pending[(size_t) p];
        if (note.samples < numSamples)
        {
            auto& voice = voices[(size_t) note.voice];
            if (voice.source >= 0 && voice.count < kMaxNotes)
            {
                noteOn (note.dest, note.note, note.velocity, juce::jmax (0, note.samples), to);
                voice.notes[(size_t) voice.count++] = note.note;
            }
            pending[(size_t) p] = pending[(size_t) --numPending];
        }
        else
        {
            note.samples -= numSamples;
            ++p;
        }
    }

    const int strumSamples = juce::roundToInt (strumTimes()[(size_t) juce::jlimit (0, 3, strum.load())] * 0.001 * clock.sampleRate);
    const int dest = resolveTarget (keysTo);
    const bool latch = hold.load();

    auto pressSource = [&] (int source, int pad, int transpose, float velocity, int at)
    {
        if (latch)
        {
            if (heldSource == source)
            {
                stopVoice (source, at, to);
                heldSource = -1;
                return;
            }
            stopVoice (heldSource, at, to);
            heldSource = source;
        }
        startVoice (source, pad, transpose, velocity, dest, at, to, strumSamples);
    };
    auto releaseSource = [&] (int source, int at)
    {
        if (heldSource != source)
            stopVoice (source, at, to);
    };

    // Clicks on the screen (at the start of the block).
    {
        const auto scope = pressFifo.read (pressFifo.getNumReady());
        auto handle = [&] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto p = pressQueue[(size_t) i];
                if (p.pad >= 0)
                    pressSource (p.pad, p.pad, 0, p.velocity, 0);
                else
                    releaseSource (-p.pad - 1, 0);
            }
        };
        handle (scope.startIndex1, scope.blockSize1);
        handle (scope.startIndex2, scope.blockSize2);
    }

    // Your keyboard's pads and keys.
    for (const auto m : triggers)
    {
        const auto msg = m.getMessage();
        const int code = msg.getNoteNumber(), at = juce::jlimit (0, juce::jmax (0, numSamples - 1), m.samplePosition);
        const bool shape = msg.getChannel() == 2;
        const int source = 100 + (shape ? 16 + code : code);
        if (msg.isNoteOn())
        {
            if (shape)
            {
                const int pad = juce::jlimit (0, kPads - 1, shapePad.load());
                const int lowest = live[(size_t) pad][0].load();
                if (lowest >= 0)
                    pressSource (source, pad, code - lowest, msg.getFloatVelocity(), at);
            }
            else
                pressSource (source, code, 0, msg.getFloatVelocity(), at);
        }
        else
            releaseSource (source, at);
    }
    triggers.clear();

    // The progression, on the clock.
    const int numSteps = progressionSteps();
    if (progressionOn.load() && clock.playing && numSteps > 0)
    {
        const double len = stepLengths()[(size_t) juce::jlimit (0, 4, stepLength.load())];
        const double beatsPerSample = clock.bpm / 60.0 / juce::jmax (1.0, clock.sampleRate);
        const double start = juce::jmax (0.0, clock.beat);
        auto stepAt = [&] (double beat) { return (int) std::floor (beat / len + 1.0e-9); };
        auto change = [&] (int absolute, int at)
        {
            progressionStepNow = absolute;
            const int step = absolute % numSteps;
            playingStep = step;
            const int pad = steps[(size_t) step].load();
            stopVoice (kProgressionSource, at, to);
            if (pad >= 0 && pad < kPads && live[(size_t) pad][0].load() >= 0)
                startVoice (kProgressionSource, pad, 0, 0.8f, dest, at, to, strumSamples);
        };
        if (stepAt (start) != progressionStepNow)
            change (stepAt (start), 0);
        for (int guard = 0; guard < 64; ++guard)
        {
            const double boundary = (progressionStepNow + 1) * len;
            const int at = (int) std::ceil ((boundary - start) / beatsPerSample - 1.0e-6);
            if (at >= numSamples || at < 0)
                break;
            change (progressionStepNow + 1, at);
        }
    }
    else if (progressionStepNow >= 0 || playingStep.load() >= 0)
    {
        stopVoice (kProgressionSource, 0, to);
        progressionStepNow = -1;
        playingStep = -1;
    }

    updateMask();
    if (taking.load())
        takeBeats += numSamples * takeBeatsPerSample;
}

//==============================================================================
void RigChords::startTake()
{
    take.clear();
    takeFifo.reset();
    takeReset = true;
    taking = true;
}

void RigChords::stopTake()
{
    taking = false;
    serviceTake();
}

void RigChords::serviceTake()
{
    const auto scope = takeFifo.read (takeFifo.getNumReady());
    auto handle = [this] (int start, int size)
    {
        for (int i = start; i < start + size; ++i)
        {
            const auto& e = takeQueue[(size_t) i];
            take.addEvent (e.velocity > 0 ? juce::MidiMessage::noteOn (1, e.note, e.velocity) : juce::MidiMessage::noteOff (1, e.note),
                           e.beat);
        }
    };
    handle (scope.startIndex1, scope.blockSize1);
    handle (scope.startIndex2, scope.blockSize2);
}

namespace
{
bool writeMidiFile (const juce::File& file, juce::MidiMessageSequence notes, double bpm, int beatsPerBar, double endBeat)
{
    constexpr int ppq = 960;
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::textMetaEvent (3, "Chords"), 0);
    track.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / juce::jmax (20.0, bpm))), 0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (juce::jlimit (1, 16, beatsPerBar), 4), 0);
    for (int i = 0; i < notes.getNumEvents(); ++i)
    {
        auto m = notes.getEventPointer (i)->message;
        m.setTimeStamp (m.getTimeStamp() * ppq);
        track.addEvent (m);
    }
    auto end = juce::MidiMessage::endOfTrack();
    end.setTimeStamp (juce::jmax (endBeat, notes.getEndTime()) * ppq);
    track.addEvent (end);
    track.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (track);
    file.getParentDirectory().createDirectory();
    juce::FileOutputStream out (file);
    if (! out.openedOk())
        return false;
    out.setPosition (0);
    out.truncate();
    return mf.writeTo (out);
}
}

bool RigChords::writeTake (const juce::File& file, double bpm) const
{
    if (take.getNumEvents() == 0)
        return false;
    juce::MidiMessageSequence notes (take);
    notes.sort();
    notes.updateMatchedPairs();
    // Starts at the first note; notes still down at the end let go there.
    const double first = notes.getStartTime(), end = notes.getEndTime();
    for (int i = 0; i < notes.getNumEvents(); ++i)
        if (auto* e = notes.getEventPointer (i); e->message.isNoteOn() && e->noteOffObject == nullptr)
            notes.addEvent (juce::MidiMessage::noteOff (1, e->message.getNoteNumber()), end + 0.25);
    notes.addTimeToMessages (-first);
    notes.sort();
    return writeMidiFile (file, notes, bpm, 4, 0);
}

bool RigChords::writeProgression (const juce::File& file, double bpm, int beatsPerBar) const
{
    const int n = progressionSteps();
    if (n == 0)
        return false;
    const double len = stepLengths()[(size_t) juce::jlimit (0, 4, stepLength.load())];
    juce::MidiMessageSequence notes;
    for (int i = 0; i < n; ++i)
    {
        const int pad = steps[(size_t) i].load();
        if (pad < 0)
            continue;
        for (int note : getPad (pad).notes)
        {
            notes.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), i * len);
            notes.addEvent (juce::MidiMessage::noteOff (1, note), (i + 1) * len - 0.01);
        }
    }
    if (notes.getNumEvents() == 0)
        return false;
    notes.sort();
    return writeMidiFile (file, notes, bpm, beatsPerBar, n * len);
}

juce::File RigChords::toLogicFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate/Rig/To Logic");
}

//==============================================================================
void RigChords::reset()
{
    target = kFollowKeys;
    trigger = (int) Trigger::screen;
    hold = false;
    strum = 0;
    lowKey = 36;
    shapePad = 0;
    stepLength = 2;
    progressionOn = false;
    loadStarter (0);
}

juce::ValueTree RigChords::toTree() const
{
    juce::ValueTree t (kTreeType);
    t.setProperty ("target", target.load(), nullptr);
    t.setProperty ("trigger", trigger.load(), nullptr);
    t.setProperty ("hold", hold.load(), nullptr);
    t.setProperty ("strum", strum.load(), nullptr);
    t.setProperty ("lowKey", lowKey.load(), nullptr);
    t.setProperty ("shapePad", shapePad.load(), nullptr);
    t.setProperty ("stepLength", stepLength.load(), nullptr);
    t.setProperty ("progressionOn", progressionOn.load(), nullptr);
    t.setProperty ("fillKey", fillKey, nullptr);
    t.setProperty ("fillScale", fillScale, nullptr);
    t.setProperty ("fillType", fillType, nullptr);
    juce::StringArray stepText;
    for (const auto& s : steps)
        stepText.add (juce::String (s.load()));
    t.setProperty ("steps", stepText.joinIntoString (" "), nullptr);
    for (int i = 0; i < kPads; ++i)
    {
        const auto& p = pads[(size_t) i];
        if (p.notes.isEmpty() && p.name.isEmpty())
            continue;
        juce::ValueTree pad ("Pad");
        pad.setProperty ("index", i, nullptr);
        juce::StringArray noteText;
        for (int n : p.notes)
            noteText.add (juce::String (n));
        pad.setProperty ("notes", noteText.joinIntoString (" "), nullptr);
        if (p.name.isNotEmpty())
            pad.setProperty ("name", p.name, nullptr);
        t.appendChild (pad, nullptr);
    }
    return t;
}

void RigChords::fromTree (const juce::ValueTree& t)
{
    stopAll();
    if (! t.isValid() || ! t.hasType (kTreeType))
    {
        reset();
        return;
    }
    target = juce::jlimit (kFollowKeys, RigRouting::kNumDests - 1, (int) t.getProperty ("target", kFollowKeys));
    trigger = juce::jlimit (0, (int) Trigger::everyKey, (int) t.getProperty ("trigger", 0));
    hold = (bool) t.getProperty ("hold", false);
    strum = juce::jlimit (0, 3, (int) t.getProperty ("strum", 0));
    lowKey = juce::jlimit (0, 127 - kPads, (int) t.getProperty ("lowKey", 36));
    shapePad = juce::jlimit (0, kPads - 1, (int) t.getProperty ("shapePad", 0));
    stepLength = juce::jlimit (0, 4, (int) t.getProperty ("stepLength", 2));
    progressionOn = (bool) t.getProperty ("progressionOn", false);
    fillKey = juce::jlimit (0, 11, (int) t.getProperty ("fillKey", 0));
    fillScale = juce::jlimit (0, vox::Chords::numScales - 1, (int) t.getProperty ("fillScale", 0));
    fillType = juce::jlimit (0, vox::Chords::numFlavours - 1, (int) t.getProperty ("fillType", 0));
    juce::StringArray stepText;
    stepText.addTokens (t.getProperty ("steps").toString(), " ", "");
    for (int i = 0; i < kSteps; ++i)
        steps[(size_t) i] = i < stepText.size() ? juce::jlimit (-1, kPads - 1, stepText[i].getIntValue()) : -1;
    for (int i = 0; i < kPads; ++i)
        setPad (i, {});
    for (const auto pad : t)
    {
        if (! pad.hasType ("Pad"))
            continue;
        juce::StringArray noteText;
        noteText.addTokens (pad.getProperty ("notes").toString(), " ", "");
        juce::Array<int> notes;
        for (const auto& n : noteText)
            notes.add (n.getIntValue());
        setPad ((int) pad.getProperty ("index", -1), notes, pad.getProperty ("name").toString());
    }
}

juce::File RigChords::setsFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate/Chords");
}

bool RigChords::saveSet (const juce::File& file) const
{
    file.getParentDirectory().createDirectory();
    if (auto xml = toTree().createXml())
        return xml->writeTo (file);
    return false;
}

bool RigChords::loadSet (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
    {
        const auto t = juce::ValueTree::fromXml (*xml);
        if (t.hasType (kTreeType))
        {
            // A set is your chords and progression; where they play stays as it is.
            const int keepTarget = target.load(), keepTrigger = trigger.load();
            fromTree (t);
            target = keepTarget;
            trigger = keepTrigger;
            return true;
        }
    }
    return false;
}
