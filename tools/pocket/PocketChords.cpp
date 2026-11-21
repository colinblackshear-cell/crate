#include "PocketChords.h"
#include "PocketTheme.h"

#include "PluginProcessor.h"   // the vocoder: its chord input (requestChord) and chord builder
#include "dsp/Chords.h"

#include <cmath>

namespace pocket
{
const char* chordColourName (int c)
{
    static const char* const names[kChordColours] = { "PLAIN", "7TH", "9TH", "SUS", "POWER" };
    return names[juce::jlimit (0, kChordColours - 1, c)];
}

namespace chordmath
{
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
        out[n++] = v;
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

    for (int i = 0; i < n; ++i)
    {
        const double t = t0 + i * bps;

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
        if (want != playing || prog != playingProg || version != playingVersion || style != playingStyle || aud != playingAudible)
        {
            // the chord changes here
            synthOff (i);
            padTo (i);
            padPlayer.release();
            offBeat = -1;
            strumLeft.fill (-1);
            const bool newChord = want != playing || version != playingVersion;
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

juce::Array<int> ChordsTool::padNotes (int pad) const
{
    auto n = chordmath::padNotes (key, pad, chordmath::flavourOf (colour));
    if (bass && ! n.isEmpty())
        n.insert (0, n[0] - 12);
    while (n.size() > kMaxChordNotes)
        n.removeLast();
    return n;
}

juce::String ChordsTool::padName (int pad) const { return chordmath::padName (key, pad, chordmath::flavourOf (colour)); }
juce::String ChordsTool::padNumeral (int pad) const { return chordmath::padNumeral (key, pad, chordmath::flavourOf (colour)); }

void ChordsTool::refreshNotes()
{
    for (int p = 0; p < kChordPads; ++p)
        eng.setPadNotes (p, padNotes (p));
}

void ChordsTool::apply()
{
    refreshNotes();
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
    return problems.joinIntoString ("; ");
}
}
