#include "PocketChords.h"
#include "PocketTheme.h"

#include "PluginProcessor.h"   // the vocoder: its chord input (requestChord) and chord builder
#include "dsp/Chords.h"
#include "rig/RigChords.h"   // the Mac chord builder's namer (RigChords::nameOf)

#include <cmath>

namespace pocket
{
const char* chordColourName (int c)
{
    static const char* const names[kChordColours] = { "PLAIN", "7TH", "9TH", "SUS", "POWER" };
    return names[juce::jlimit (0, kChordColours - 1, c)];
}

const std::vector<OwnChordType>& ownChordTypes()
{
    static const std::vector<OwnChordType> types {
        { "MAJ", "", { 0, 4, 7 } }, { "MIN", "m", { 0, 3, 7 } }, { "7", "7", { 0, 4, 7, 10 } }, { "MAJ7", "maj7", { 0, 4, 7, 11 } },
        { "MIN7", "m7", { 0, 3, 7, 10 } }, { "9", "9", { 0, 4, 7, 10, 14 } }, { "MAJ9", "maj9", { 0, 4, 7, 11, 14 } }, { "MIN9", "m9", { 0, 3, 7, 10, 14 } },
        { "6", "6", { 0, 4, 7, 9 } }, { "ADD9", "add9", { 0, 4, 7, 14 } }, { "SUS2", "sus2", { 0, 2, 7 } }, { "SUS4", "sus4", { 0, 5, 7 } },
        { "DIM", "dim", { 0, 3, 6 } }, { "AUG", "aug", { 0, 4, 8 } }, { "POWER", "5", { 0, 7, 12 } } };
    return types;
}

juce::var OwnChord::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("on", on);
    o->setProperty ("root", root);
    o->setProperty ("type", type);
    o->setProperty ("inversion", inversion);
    o->setProperty ("octave", octave);
    juce::Array<juce::var> n;
    for (int v : notes)
        n.add (v);
    o->setProperty ("notes", n);
    return juce::var (o);
}

OwnChord OwnChord::fromVar (const juce::var& v)
{
    OwnChord c;
    if (! v.isObject())
        return c;
    c.root = juce::jlimit (0, 11, (int) v.getProperty ("root", 0));
    c.type = juce::jlimit (-1, (int) ownChordTypes().size() - 1, (int) v.getProperty ("type", 0));
    c.inversion = juce::jlimit (-8, 8, (int) v.getProperty ("inversion", 0));
    c.octave = juce::jlimit (-4, 4, (int) v.getProperty ("octave", 0));
    if (auto* a = v.getProperty ("notes", {}).getArray())
        for (auto& x : *a)
            if (c.notes.size() < kMaxChordNotes)
                c.notes.addIfNotAlreadyThere (juce::jlimit (12, 115, (int) x));
    c.notes.sort();
    c.on = (bool) v.getProperty ("on", false) && ! c.notes.isEmpty();
    return c;
}

namespace chordmath
{
juce::Array<int> buildOwn (int root, int type, int inversion, int octave)
{
    const auto& types = ownChordTypes();
    const auto& t = types[(size_t) juce::jlimit (0, (int) types.size() - 1, type)];
    juce::Array<int> n;
    for (int i : t.intervals)
        n.add (48 + juce::jlimit (0, 11, root) + i);
    for (int k = 0; k < inversion && ! n.isEmpty(); ++k)
    {
        n.set (0, n[0] + 12);
        n.sort();
    }
    for (int k = 0; k > inversion && ! n.isEmpty(); --k)
    {
        n.set (n.size() - 1, n.getLast() - 12);
        n.sort();
    }
    for (auto& v : n)
        v = juce::jlimit (12, 115, v + 12 * octave);
    return n;
}

static juce::String prettyName (const juce::String& s)
{
    // "Eb" and "F#" as the app writes them (the flat and sharp signs), roots and slash basses alike
    juce::String out;
    for (int i = 0; i < s.length(); ++i)
    {
        const auto c = s[i];
        const bool afterNote = i > 0 && juce::String ("ABCDEFG").containsChar (s[i - 1]);
        if (c == 'b' && afterNote)
            out << U8("\xe2\x99\xad");
        else if (c == '#' && afterNote)
            out << U8("\xe2\x99\xaf");
        else
            out << juce::String::charToString (c);
    }
    return out.replace ("dim", U8("\xc2\xb0"));
}

juce::String nameNotes (const juce::Array<int>& notes)
{
    const auto name = RigChords::nameOf (notes);
    if (name.isEmpty() || name.containsChar (' '))
        return {};   // notes it can't name ("C C# D"): CUSTOM
    return prettyName (name);
}

bool detectOwn (const juce::Array<int>& notes, int& root, int& type)
{
    if (notes.isEmpty())
        return false;
    int pcs = 0;
    for (int n : notes)
        pcs |= 1 << (((n % 12) + 12) % 12);
    juce::Array<int> roots;
    auto sorted = notes;
    sorted.sort();
    for (int n : sorted)
        roots.addIfNotAlreadyThere (((n % 12) + 12) % 12);
    const auto& types = ownChordTypes();
    for (int r : roots)
        for (int t = 0; t < (int) types.size(); ++t)
        {
            int want = 0;
            for (int i : types[(size_t) t].intervals)
                want |= 1 << ((r + i) % 12);
            if (want == pcs)
            {
                root = r;
                type = t;
                return true;
            }
        }
    return false;
}

int flavourOf (int colour)
{
    static const int flavours[kChordColours] = { vox::Chords::triad, vox::Chords::seventh, vox::Chords::ninth, vox::Chords::sus4, vox::Chords::power };
    return flavours[juce::jlimit (0, kChordColours - 1, colour)];
}

static int dominantRoot (SongKey k)
{
    const auto five = vox::Chords::build (k.root, k.minor ? 1 : 0, 4, vox::Chords::triad);
    return five.empty() ? 55 : five[0];
}

juce::Array<int> padNotes (SongKey k, int pad, int flavour)
{
    juce::Array<int> out;
    if (pad < 7)
    {
        for (int n : vox::Chords::build (k.root, k.minor ? 1 : 0, juce::jlimit (0, 6, pad), flavour))
            out.add (n);
        return out;
    }
    // the dominant: a major chord with a minor seventh on the fifth, whatever the key
    const int r = dominantRoot (k);
    switch (flavour)
    {
        case vox::Chords::ninth: return { r, r + 4, r + 7, r + 10, r + 14 };
        case vox::Chords::sus4: return { r, r + 5, r + 7, r + 10 };
        case vox::Chords::power: return { r, r + 7, r + 12 };
        default: return { r, r + 4, r + 7, r + 10 };
    }
}

juce::String padName (SongKey k, int pad, int flavour)
{
    if (pad >= 7)
    {
        const juce::String root = SongKey::pitchName (dominantRoot (k));
        switch (flavour)
        {
            case vox::Chords::ninth: return root + "9";
            case vox::Chords::sus4: return root + "7sus4";
            case vox::Chords::power: return root + "5";
            default: return root + "7";
        }
    }
    const juce::String s (vox::Chords::name (k.root, k.minor ? 1 : 0, juce::jlimit (0, 6, pad), flavour));
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
    rest = rest.replace ("b5", U8("\xe2\x99\xad" "5"));
    if (rest.startsWith ("dim"))
        rest = U8("\xc2\xb0") + rest.substring (3);
    return out + rest;
}

juce::String padNumeral (SongKey k, int pad, int flavour)
{
    static const char* const majorN[7] = { "I", "ii", "iii", "IV", "V", "vi", "vii\xc2\xb0" };
    static const char* const minorN[7] = { "i", "ii\xc2\xb0", "III", "iv", "v", "VI", "VII" };
    if (pad >= 7)
        return flavour == vox::Chords::ninth ? "V9" : flavour == vox::Chords::sus4 ? "V7sus" : flavour == vox::Chords::power ? "V5" : "V7";
    return juce::String (juce::CharPointer_UTF8 ((k.minor ? minorN : majorN)[juce::jlimit (0, 6, pad)]));
}
}

//==============================================================================
ChordsEngine::ChordsEngine (BeatsEngine& b) : beats (b)
{
    for (auto& pad : padNotesLive)
        for (auto& n : pad)
            n = -1;
    for (auto& s : progSteps)
        s = -1;
    for (auto& s : sentNotes)
        s = -1;
    midi.ensureSize (4096);
    forVocoder.ensureStorageAllocated (16);
    prepare (44100.0);
}

bool ChordsEngine::clockRunning() const
{
    return beats.clockRunning();
}

void ChordsEngine::setPadNotes (int pad, const juce::Array<int>& n)
{
    if (! juce::isPositiveAndBelow (pad, kChordPads))
        return;
    for (int i = 0; i < kMaxChordNotes; ++i)
        padNotesLive[(size_t) pad][(size_t) i] = i < n.size() ? n[i] : -1;
    ++notesVersion;
}

void ChordsEngine::setProgression (const std::array<int, kProgressionMax>& steps, int length, bool isOn)
{
    for (int i = 0; i < kProgressionMax; ++i)
        progSteps[(size_t) i] = steps[(size_t) i];
    progLength = juce::jlimit (1, kProgressionMax, length);
    progOn = isOn;
}

int ChordsEngine::changes (double* b, int* p, int max) const
{
    const int n = juce::jmin (max, logCount.load(), (int) logBeat.size());
    for (int i = 0; i < n; ++i)
    {
        b[i] = logBeat[(size_t) i].load();
        p[i] = logPad[(size_t) i].load();
    }
    return n;
}

juce::Array<int> ChordsEngine::lastVocoderNotes() const
{
    juce::Array<int> out;
    for (auto& n : sentNotes)
        if (n.load() >= 0)
            out.add (n.load());
    return out;
}

void ChordsEngine::logChange (double beat, int pad)
{
    const int i = logCount.load();
    if (i < (int) logBeat.size())
    {
        logBeat[(size_t) i] = beat;
        logPad[(size_t) i] = pad;
        logCount = i + 1;
    }
}

int ChordsEngine::notesOf (int pad, int* out) const
{
    if (! juce::isPositiveAndBelow (pad, kChordPads))
        return 0;
    int n = 0;
    for (int i = 0; i < kMaxChordNotes; ++i)
    {
        const int v = padNotesLive[(size_t) pad][(size_t) i].load();
        if (v < 0)
            break;
        if (v + shiftNow >= 0 && v + shiftNow <= 127)   // LIVE KEY
            out[n++] = v + shiftNow;
    }
    return n;
}

void ChordsEngine::prepare (double rate)
{
    sampleRate = rate;
    voice.prepare (rate);
    padPlayer.prepare (rate);
    numOn = 0;
    playing = -1;
}

void ChordsEngine::render (float* left, float* right, int n)
{
    renderAt (left, right, n, beats.clockRunning(), beats.clockBeat(), beats.getBpm());
}

void ChordsEngine::sendToVocoder (int pad)
{
    int buf[kMaxChordNotes];
    const int count = pad >= 0 ? notesOf (pad, buf) : 0;
    forVocoder.clearQuick();
    for (int i = 0; i < count && i < 6; ++i)
        forVocoder.add (buf[i]);
    if (vocoder != nullptr)
        vocoder->requestChord (forVocoder, true);   // one atomic store: safe here
    for (int i = 0; i < 6; ++i)
        sentNotes[(size_t) i] = i < forVocoder.size() ? forVocoder[i] : -1;
    ++sends;
}

void ChordsEngine::renderAt (float* left, float* right, int n, bool running, double beat, double bpm)
{
    const double bps = bpm / (60.0 * sampleRate);
    double t0 = running ? beat : freeBeat;
    if (running && ! wasRunning)
        lastTick = -1;
    wasRunning = running;
    midi.clear();

    const bool synthOn = playSynth.load(), padOn = playPad.load(), vocOn = playVocoder.load();
    if (! synthOn && wasSynth)
    {
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
        numOn = 0;
    }
    if (! padOn && wasPad)
        padPlayer.release();
    wasSynth = synthOn;
    wasPad = padOn;

    const int style = styleNow.load();
    const int version = notesVersion.load();
    const int strumGap = (int) (0.028 * sampleRate);
    int padDone = 0;
    auto padTo = [&] (int i)
    {
        if (i > padDone)
        {
            if (padOn)
                padPlayer.render (left + padDone, right != nullptr ? right + padDone : nullptr, i - padDone);
            padDone = i;
        }
    };
    const bool muted = progMuted.load();
    auto audible = [&] { return playingAudible; };
    auto synthOff = [&] (int at)
    {
        for (int k = 0; k < numOn; ++k)
            midi.addEvent (juce::MidiMessage::noteOff (1, on[(size_t) k]), at);
        numOn = 0;
    };
    auto synthOnNote = [&] (int note, int at)
    {
        if (! synthOn || numOn >= (int) on.size())
            return;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.75f), at);
        on[(size_t) numOn++] = note;
    };

    const int liveWant = liveTarget.load(), liveMode = liveWhen.load();
    for (int i = 0; i < n; ++i)
    {
        const double t = t0 + i * bps;

        // LIVE KEY: a change lands now, or on the next beat or bar of the clock
        if (liveWant != shiftNow)
        {
            bool land = ! running || liveMode == 0;
            if (! land)
            {
                const double unit = liveMode == 1 ? 1.0 : 4.0;
                if (liveWaitUntil < 0)
                    liveWaitUntil = std::ceil (t / unit - 1.0e-9) * unit;
                land = t >= liveWaitUntil - 1.0e-9;
            }
            if (land)
            {
                shiftNow = liveWant;
                liveApplied = liveWant;
                liveWaitUntil = -1;
            }
        }
        else
            liveWaitUntil = -1;

        // what should sound: the pad held, else the progression's chord for this bar
        int want = livePad.load(), step = -1;
        bool prog = false;
        if (want < 0 && running && progOn.load())
        {
            const int len = juce::jlimit (1, kProgressionMax, progLength.load());
            const auto bar = (long long) std::floor ((t - origin.load()) / 4.0 + 1.0e-9);
            step = (int) (((bar % len) + len) % len);
            want = progSteps[(size_t) step].load();
            prog = true;
        }
        if (i == 0 || prog)
            progStep = step;
        const bool aud = want >= 0 && ! (prog && muted);
        if (want != playing || prog != playingProg || version != playingVersion || style != playingStyle || aud != playingAudible
            || shiftNow != playingShiftNow)
        {
            // the chord changes here
            synthOff (i);
            padTo (i);
            padPlayer.release();
            offBeat = -1;
            strumLeft.fill (-1);
            const bool newChord = want != playing || version != playingVersion || shiftNow != playingShiftNow;
            playingShiftNow = shiftNow;
            playing = want;
            playingProg = prog;
            playingVersion = version;
            playingStyle = style;
            playingAudible = aud;
            current = want;
            currentFromProg = prog;
            numNotes = want >= 0 ? notesOf (want, notes.data()) : 0;
            if (vocOn && newChord && want >= 0)   // let go: the vocoder keeps the last chord
                sendToVocoder (want);
            if (newChord)
                logChange (t, want);
            arpAt = 0;
            if (audible() && numNotes > 0)
            {
                if (style == styleChord || style == styleStrum)
                {
                    padPlayer.setStrum (style == styleStrum ? strumGap : 0);
                    padPlayer.playNotes (notes.data(), numNotes);
                    for (int k = 0; k < numNotes; ++k)
                    {
                        if (style == styleChord || k == 0)
                            synthOnNote (notes[(size_t) k], i);
                        else
                            strumLeft[(size_t) k] = k * strumGap;
                    }
                }
                else if (style == styleArp && ! prog)
                    lastTick = -1;   // a pad pressed: the arp starts at once
            }
        }

        // STRUM: the notes after the first, one after another
        for (int k = 1; k < numNotes; ++k)
            if (strumLeft[(size_t) k] >= 0 && strumLeft[(size_t) k]-- == 0 && audible())
                synthOnNote (notes[(size_t) k], i);

        // ARP and STABS: on the 1/16 grid of the clock (or a free one when the song is stopped)
        if (style == styleArp || style == styleStabs)
        {
            const auto k = (long long) std::floor (t / 0.25 + 1.0e-9);
            if (offBeat >= 0 && t >= offBeat)
            {
                synthOff (i);
                padTo (i);
                padPlayer.release();
                offBeat = -1;
            }
            if (k != lastTick)
            {
                lastTick = k;
                if (audible() && numNotes > 0)
                {
                    if (style == styleArp)
                    {
                        synthOff (i);
                        const int note = notes[(size_t) (arpAt++ % numNotes)];
                        synthOnNote (note, i);
                        padTo (i);
                        padPlayer.setStrum (0);
                        padPlayer.playNotes (&note, 1);
                        offBeat = t + 0.25 * 0.6;
                    }
                    else
                    {
                        const int s = (int) (((k % 16) + 16) % 16);
                        if (s == 0 || s == 3 || s == 6 || s == 10 || s == 12)
                        {
                            synthOff (i);
                            for (int m = 0; m < numNotes; ++m)
                                synthOnNote (notes[(size_t) m], i);
                            padTo (i);
                            padPlayer.setStrum (0);
                            padPlayer.playNotes (notes.data(), numNotes);
                            offBeat = t + 0.16;
                        }
                    }
                }
            }
        }
    }
    padTo (n);
    if (! running)
        freeBeat = t0 + n * bps;

    crate::synthtool::Clock clock;
    clock.playing = running;
    clock.ppq = beat;
    clock.bpm = bpm;
    voice.render (left, right, n, midi, clock);

    float p = peak.load();
    for (int i = 0; i < n; ++i)
        p = juce::jmax (p, std::abs (left[i]), right != nullptr ? std::abs (right[i]) : 0.0f);
    peak = p;
}

//==============================================================================
ChordsTool::ChordsTool (BeatsEngine& b) : eng (b)
{
    reset();
}

void ChordsTool::reset()
{
    colour = (int) ChordColour::seventh;
    style = styleChord;
    hold = true;
    bass = false;
    progression.fill (-1);
    progLength = 4;
    progressionOn = false;
    playsSynth = true;
    playsPad = false;
    playsVocoder = true;
    synthPreset = juce::jmax (0, synthPresetIndex ("Electric Piano"));
    padSoundPad = -1;
    padSoundId = {};
    editingBox = -1;
    live = -1;
    ++auditionId;
    own.fill ({});
    liveKeyShift = 0;
    liveOct = 0;
    liveOnBar = true;
    liveOpen = false;
    eng.setLivePad (-1);
    eng.setProgressionMuted (false);
    eng.setOrigin (0.0);
    apply();
}

void ChordsTool::setKey (SongKey k)
{
    key = k;
    refreshNotes();
}

juce::Array<int> ChordsTool::keyNotes (int pad) const
{
    return chordmath::padNotes (key, juce::jlimit (0, kChordPads - 1, pad), chordmath::flavourOf (colour));
}

juce::Array<int> ChordsTool::padNotes (int pad) const
{
    auto n = isOwn (pad) ? ownChord (pad).notes : keyNotes (pad);
    if (bass && ! n.isEmpty())
        n.insert (0, n[0] - 12);
    while (n.size() > kMaxChordNotes)
        n.removeLast();
    return n;
}

juce::String ChordsTool::padName (int pad) const
{
    if (isOwn (pad))
    {
        const auto n = chordmath::nameNotes (ownChord (pad).notes);
        return n.isNotEmpty() ? n : juce::String ("CUSTOM");
    }
    return chordmath::padName (key, pad, chordmath::flavourOf (colour));
}

juce::String ChordsTool::padNumeral (int pad) const
{
    return isOwn (pad) ? juce::String ("OWN") : chordmath::padNumeral (key, pad, chordmath::flavourOf (colour));
}

void ChordsTool::setOwnChord (int pad, const OwnChord& c)
{
    if (! juce::isPositiveAndBelow (pad, kChordPads))
        return;
    auto& o = own[(size_t) pad];
    o = c;
    o.notes.sort();
    while (o.notes.size() > kMaxChordNotes)
        o.notes.removeLast();
    o.on = c.on && ! o.notes.isEmpty();
    eng.setPadNotes (pad, padNotes (pad));
}

void ChordsTool::resetToKey (int pad)
{
    if (! juce::isPositiveAndBelow (pad, kChordPads))
        return;
    own[(size_t) pad] = {};
    eng.setPadNotes (pad, padNotes (pad));
}

void ChordsTool::copyChord (int from, int to)
{
    if (! juce::isPositiveAndBelow (from, kChordPads) || ! juce::isPositiveAndBelow (to, kChordPads) || from == to)
        return;
    OwnChord c = own[(size_t) from];
    if (! c.on)
    {
        c.notes = keyNotes (from);
        c.on = true;
        c.inversion = c.octave = 0;
        if (! chordmath::detectOwn (c.notes, c.root, c.type))
            c.type = -1;
    }
    setOwnChord (to, c);
}

void ChordsTool::audition (int pad, int ms)
{
    if (! juce::isPositiveAndBelow (pad, kChordPads))
        return;
    const int id = ++auditionId;
    live = pad;
    momentary = true;
    eng.setLivePad (pad);
    if (onPlay)
        onPlay();
    juce::Timer::callAfterDelay (ms, [this, id, pad, token = std::weak_ptr<bool> (alive)]
    {
        const auto still = token.lock();
        if (still != nullptr && *still && id == auditionId && live == pad && momentary)
        {
            live = -1;
            momentary = false;
            eng.setLivePad (-1);
        }
    });
}

void ChordsTool::refreshNotes()
{
    for (int p = 0; p < kChordPads; ++p)
        eng.setPadNotes (p, padNotes (p));
}

void ChordsTool::apply()
{
    refreshNotes();
    applyLive();
    eng.setStyle (style);
    eng.setProgression (progression, juce::jmax (1, usedBoxes()), progressionOn && usedBoxes() > 0);
    eng.setPlays (playsSynth, playsPad && eng.hasPadSound(), playsVocoder);
    auto s = makeSynthSettings (synthPreset, SynthKnobs { 0.5f, presetShape (synthPreset), 0.5f, 0.0f }, 0);
    s.arpOn = false;
    s.latch = false;
    s.voiceMode = crate::synthtool::VoiceMode::poly;   // a chord needs every voice, whatever the sound was made as
    s.volume *= 0.6f;
    eng.setSynthSettings (s);
}

void ChordsTool::padDown (int pad, bool setBox)
{
    pad = juce::jlimit (0, kChordPads - 1, pad);
    if (setBox && editingBox >= 0)
    {
        // a box is picked: the pad goes in it (and is heard while it's held); the next box is picked
        progression[(size_t) editingBox] = pad;
        editingBox = editingBox + 1 < progLength ? editingBox + 1 : -1;
        progressionEdited();
        momentary = true;
        live = pad;
        eng.setLivePad (live);
        return;
    }
    momentary = ! hold;
    live = hold && live == pad ? -1 : pad;   // HOLD: the lit pad again lets go
    eng.setLivePad (live);
    if (live >= 0 && onPlay)
        onPlay();
}

void ChordsTool::padUp (int pad)
{
    if (momentary && live == pad)
    {
        live = -1;
        eng.setLivePad (-1);
    }
    momentary = false;
}

void ChordsTool::releaseAll()
{
    live = -1;
    momentary = false;
    eng.setLivePad (-1);
}

int ChordsTool::usedBoxes() const
{
    int last = 0;
    for (int i = 0; i < progLength; ++i)
        if (progression[(size_t) i] >= 0)
            last = i + 1;
    return last;
}

void ChordsTool::progressionEdited()
{
    eng.setProgressionMuted (false);
    eng.setProgression (progression, juce::jmax (1, usedBoxes()), progressionOn && usedBoxes() > 0);
    if (progressionOn && usedBoxes() > 0 && onPlay)
        onPlay();
}

void ChordsTool::setLiveKey (int pitchClass)
{
    int d = ((juce::jlimit (0, 11, pitchClass) - key.root) % 12 + 12) % 12;
    if (d > 6)
        d -= 12;   // the nearest way
    liveKeyShift = d;
    applyLive();
}

void ChordsTool::nudgeLiveOctave (int delta)
{
    liveOct = juce::jlimit (-kLiveOctaves, kLiveOctaves, liveOct + delta);
    applyLive();
}

void ChordsTool::liveHome()
{
    liveKeyShift = 0;
    liveOct = 0;
    applyLive();
}

juce::String ChordsTool::livePadName (int pad) const
{
    const int shift = eng.playingShift();
    if (shift % 12 == 0)
        return padName (pad);
    auto notes = padNotes (pad);
    for (auto& n : notes)
        n += shift;
    const auto name = chordmath::nameNotes (notes);
    return name.isNotEmpty() ? name : padName (pad);
}

juce::var ChordsTool::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("colour", colour);
    o->setProperty ("style", style);
    o->setProperty ("hold", hold);
    o->setProperty ("bass", bass);
    juce::Array<juce::var> steps;
    for (int s : progression)
        steps.add (s);
    o->setProperty ("progression", steps);
    o->setProperty ("progLength", progLength);
    o->setProperty ("progressionOn", progressionOn);
    o->setProperty ("playsSynth", playsSynth);
    o->setProperty ("playsPad", playsPad);
    o->setProperty ("playsVocoder", playsVocoder);
    o->setProperty ("sound", synthPresets()[(size_t) synthPreset].name);
    o->setProperty ("padSoundPad", padSoundPad);
    o->setProperty ("padSoundId", padSoundId);
    juce::Array<juce::var> ownList;
    bool anyOwn = false;
    for (auto& c : own)
    {
        ownList.add (c.toVar());
        anyOwn = anyOwn || c.on;
    }
    if (anyOwn)
        o->setProperty ("own", ownList);
    o->setProperty ("liveKeyShift", liveKeyShift);
    o->setProperty ("liveOctave", liveOct);
    o->setProperty ("liveOnBar", liveOnBar);
    o->setProperty ("liveOpen", liveOpen);
    return juce::var (o);
}

void ChordsTool::fromVar (const juce::var& v)
{
    reset();
    if (! v.isObject())
        return;
    colour = juce::jlimit (0, kChordColours - 1, (int) v.getProperty ("colour", colour));
    style = juce::jlimit (0, 3, (int) v.getProperty ("style", style));
    hold = (bool) v.getProperty ("hold", hold);
    bass = (bool) v.getProperty ("bass", bass);
    if (auto* a = v.getProperty ("progression", {}).getArray())
        for (int i = 0; i < kProgressionMax && i < a->size(); ++i)
            progression[(size_t) i] = juce::jlimit (-1, kChordPads - 1, (int) (*a)[i]);
    progLength = (int) v.getProperty ("progLength", 4) > 4 ? 8 : 4;
    progressionOn = (bool) v.getProperty ("progressionOn", false);
    playsSynth = (bool) v.getProperty ("playsSynth", true);
    playsPad = (bool) v.getProperty ("playsPad", false);
    playsVocoder = (bool) v.getProperty ("playsVocoder", true);
    if (const int p = synthPresetIndex (v.getProperty ("sound", {}).toString()); p >= 0)
        synthPreset = p;
    padSoundPad = (int) v.getProperty ("padSoundPad", -1);
    padSoundId = v.getProperty ("padSoundId", {}).toString();
    if (auto* a = v.getProperty ("own", {}).getArray())
        for (int i = 0; i < kChordPads && i < a->size(); ++i)
            own[(size_t) i] = OwnChord::fromVar ((*a)[i]);
    liveKeyShift = juce::jlimit (-5, 6, (int) v.getProperty ("liveKeyShift", 0));
    liveOct = juce::jlimit (-kLiveOctaves, kLiveOctaves, (int) v.getProperty ("liveOctave", 0));
    liveOnBar = (bool) v.getProperty ("liveOnBar", true);
    liveOpen = (bool) v.getProperty ("liveOpen", false);
    apply();
}

//==============================================================================
juce::String chordsSelfCheck (juce::String& report)
{
    juce::StringArray problems;
    auto pcs = [] (const juce::Array<int>& notes)
    {
        juce::StringArray names;
        for (int n : notes)
        {
            const auto name = SongKey::pitchName (n);
            if (! names.contains (name))
                names.add (name);
        }
        names.sort (false);
        return names.joinIntoString (" ");
    };
    auto want = [] (std::initializer_list<const char*> names)
    {
        juce::StringArray s;
        for (auto* n : names)
            s.add (juce::String (juce::CharPointer_UTF8 (n)));
        s.sort (false);
        return s.joinIntoString (" ");
    };

    // each COLOUR's notes, in A minor (the first pad, Am, and the V7 pad, E) and C major (G)
    {
        const SongKey am { 9, true }, c { 0, false };
        struct Case { SongKey k; int pad, colour; juce::String expect; };
        const Case cases[] = {
            { am, 0, 0, want ({ "A", "C", "E" }) }, { am, 0, 1, want ({ "A", "C", "E", "G" }) }, { am, 0, 2, want ({ "A", "C", "E", "G", "B" }) },
            { am, 0, 3, want ({ "A", "D", "E" }) }, { am, 0, 4, want ({ "A", "E" }) }, { am, 7, 0, want ({ "E", "A\xe2\x99\xad", "B", "D" }) },
            { am, 7, 2, want ({ "E", "A\xe2\x99\xad", "B", "D", "F\xe2\x99\xaf" }) }, { c, 4, 1, want ({ "G", "B", "D", "F" }) }, { c, 1, 0, want ({ "D", "F", "A" }) } };
        juce::String line;
        for (auto& cs : cases)
        {
            const auto got = pcs (chordmath::padNotes (cs.k, cs.pad, chordmath::flavourOf (cs.colour)));
            const auto name = chordmath::padName (cs.k, cs.pad, chordmath::flavourOf (cs.colour));
            line << name << " (" << chordColourName (cs.colour) << ") " << got << "; ";
            if (got != cs.expect)
                problems.add (cs.k.label() + " pad " + juce::String (cs.pad + 1) + " " + chordColourName (cs.colour) + " gave " + got + ", not " + cs.expect);
        }
        report << "colours: " << line << "\n";
    }

    // the progression changes chord on every bar line, from its first box, and each change reaches the vocoder
    {
        BeatsEngine clock;
        ChordsTool tool (clock);
        tool.setKey ({ 9, true });
        tool.progression = { 0, 3, 4, 7, -1, -1, -1, -1 };
        tool.progressionOn = true;
        tool.apply();
        auto& e = tool.engine();
        const double sr = 44100.0, bpm = 120.0;
        const int block = 256;
        std::vector<float> l (block), r (block);
        double beat = 0;
        const double bps = bpm / (60.0 * sr);
        juce::String vocNotes;
        int lastSends = 0;
        float peak = 0;
        while (beat < 18.0)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            e.renderAt (l.data(), r.data(), block, true, beat, bpm);
            beat += block * bps;
            for (auto v : l)
                peak = juce::jmax (peak, std::abs (v));
            if (e.vocoderSends() != lastSends)
            {
                lastSends = e.vocoderSends();
                juce::StringArray n;
                for (int v : e.lastVocoderNotes())
                    n.add (SongKey::noteName (v));
                vocNotes << "[" << n.joinIntoString (" ") << "] ";
            }
        }
        double b[16];
        int p[16];
        const int count = e.changes (b, p, 16);
        juce::String got;
        bool ok = count == 5;
        const int wantPads[5] = { 0, 3, 4, 7, 0 };
        for (int i = 0; i < count; ++i)
        {
            got << tool.padName (p[i]) << "@" << juce::String (b[i], 3) << " ";
            ok = ok && i < 5 && p[i] == wantPads[i] && std::abs (b[i] - 4.0 * i) < 1.5 * bps;
        }
        report << "progression (Am Dm Em E7, 120 BPM): " << got << "; vocoder sent " << vocNotes << "; peak " << juce::String (peak, 2) << "\n";
        if (! ok)
            problems.add ("the progression didn't change chord on each bar line (" + got + ")");
        if (lastSends < 5)
            problems.add ("the chords didn't all reach the vocoder (" + juce::String (lastSends) + " sent)");
        if (peak < 0.02f)
            problems.add ("the chords' synth is silent");
    }

    // own chords: what the pad plays (and sends the vocoder), its name (or CUSTOM), saved, and left alone by the key
    {
        BeatsEngine clock;
        ChordsTool tool (clock);
        tool.setKey ({ 9, true });
        OwnChord own;
        own.on = true;
        own.root = 2;
        own.type = 7;   // MIN9
        own.notes = chordmath::buildOwn (own.root, own.type, 1, 0);
        tool.setOwnChord (4, own);
        auto& e = tool.engine();
        tool.padDown (4, false);
        std::vector<float> l (256), r (256);
        for (int i = 0; i < 8; ++i)
            e.renderAt (l.data(), r.data(), 256, false, 0.0, 120.0);
        const auto sent = pcs (e.lastVocoderNotes()), wanted = pcs (own.notes);
        tool.releaseAll();
        report << "own chord on pad 5: " << tool.padName (4) << " " << pcs (tool.padNotes (4)) << ", sent to the vocoder " << sent << "\n";
        if (tool.padNotes (4) != own.notes)
            problems.add ("an own chord isn't the pad's notes");
        if (sent != wanted)
            problems.add ("an own chord sent " + sent + " to the vocoder, not " + wanted);
        if (! tool.padName (4).startsWith ("D"))
            problems.add ("D MIN9 is called " + tool.padName (4));
        ChordsTool back (clock);
        back.fromVar (tool.toVar());
        if (! back.isOwn (4) || back.padNotes (4) != own.notes || back.isOwn (3))
            problems.add ("an own chord didn't survive the song's save and load");
        tool.setKey ({ 0, false });
        if (tool.padNotes (4) != own.notes)
            problems.add ("a key change overwrote an own chord");
        if (chordmath::nameNotes ({ 48, 49, 50 }).isNotEmpty())
            problems.add ("a C C# D cluster got a name, not CUSTOM");
    }

    // LIVE KEY: A minor to B (the nearest way, +2) waits for the bar line with ON BAR, moves a held chord there,
    // goes down to G (-2), adds an octave, comes back, and survives the song's save
    {
        BeatsEngine clock;
        ChordsTool tool (clock);
        tool.setKey ({ 9, true });
        auto& e = tool.engine();
        std::vector<float> l (256), r (256);
        const double bps = 120.0 / (60.0 * 44100.0);
        double beat = 0.5;
        auto run = [&] (double until, bool running)
        {
            while (beat < until)
            {
                e.renderAt (l.data(), r.data(), 256, running, beat, 120.0);
                beat += 256 * bps;
            }
        };
        tool.padDown (0, false);   // Am, held
        run (1.0, true);
        const auto before = pcs (e.lastVocoderNotes());
        tool.setLiveKey (11);      // B
        run (3.5, true);
        const bool waited = e.playingShift() == 0 && pcs (e.lastVocoderNotes()) == before;
        run (4.3, true);           // past the bar line at beat 4
        const auto moved = pcs (e.lastVocoderNotes());
        report << "live key A -> B: before " << before << ", waited for the bar " << (waited ? "yes" : "no") << ", then " << moved
               << " (" << tool.livePadName (0) << ")\n";
        if (tool.liveShift() != 2)
            problems.add ("A to B isn't up 2 (" + juce::String (tool.liveShift()) + ")");
        if (! waited)
            problems.add ("a live key change with ON BAR didn't wait for the bar line");
        if (moved != want ({ "B", "D", "F\xe2\x99\xaf", "A" }) || e.playingShift() != 2)
            problems.add ("the held Am7 didn't move to Bm7 on the bar (" + moved + ")");
        if (! tool.livePadName (0).startsWith ("Bm"))
            problems.add ("pad 1 shows " + tool.livePadName (0) + " in B, not Bm7");
        tool.setLiveKey (7);       // G: down 2
        tool.nudgeLiveOctave (1);
        if (tool.liveShift() != 10)
            problems.add ("G and OCT + aren't -2 + 12 (" + juce::String (tool.liveShift()) + ")");
        tool.liveOnBar = false;
        tool.liveHome();
        run (4.6, true);
        if (e.playingShift() != 0)
            problems.add ("BACK with ON BAR off didn't land at once");
        tool.setLiveKey (2);
        ChordsTool back (clock);
        back.fromVar (tool.toVar());
        if (back.liveShift() != tool.liveShift() || back.liveOnBar)
            problems.add ("the live key didn't survive the song's save and load");
        tool.releaseAll();
    }
    return problems.joinIntoString ("; ");
}
}
