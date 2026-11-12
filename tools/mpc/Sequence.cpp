#include "Sequence.h"

#include "BankModel.h"

namespace seq
{
void Track::add (Note n)
{
    // One hit per pad per tick: a new one replaces the old.
    remove (n.pad, n.tick, n.tick + 1);
    notes.insert (std::upper_bound (notes.begin(), notes.end(), n), n);
}

int Track::remove (int pad, int from, int to)
{
    const auto before = notes.size();
    notes.erase (std::remove_if (notes.begin(), notes.end(),
                                 [=] (const Note& n) { return n.pad == pad && n.tick >= from && n.tick < to; }),
                 notes.end());
    return (int) (before - notes.size());
}

Sequence::Sequence()
{
    tracks.resize (8);
    for (int i = 0; i < (int) tracks.size(); ++i)
        tracks[(size_t) i].name = "Track " + juce::String (i + 1);
}

int Sequence::usedTracks() const
{
    return (int) std::count_if (tracks.begin(), tracks.end(), [] (const Track& t) { return ! t.notes.empty(); });
}

void Sequence::setBars (int newBars)
{
    bars = juce::jlimit (1, kMaxBars, newBars);
    const int end = lengthTicks();
    for (auto& t : tracks)
        t.notes.erase (std::remove_if (t.notes.begin(), t.notes.end(), [end] (const Note& n) { return n.tick >= end; }),
                       t.notes.end());
}

juce::ValueTree Sequence::toValueTree() const
{
    juce::ValueTree v ("Sequence");
    v.setProperty ("name", name, nullptr);
    v.setProperty ("bpm", bpm, nullptr);
    v.setProperty ("bars", bars, nullptr);
    v.setProperty ("beatsPerBar", beatsPerBar, nullptr);
    v.setProperty ("loop", loop, nullptr);

    for (const auto& t : tracks)
    {
        juce::ValueTree tv ("Track");
        tv.setProperty ("name", t.name, nullptr);
        tv.setProperty ("mute", t.mute, nullptr);
        tv.setProperty ("solo", t.solo, nullptr);

        // Four 32-bit numbers a note keeps the saved state small for long sequences.
        juce::MemoryOutputStream notes;
        for (const auto& n : t.notes)
        {
            notes.writeInt (n.tick);
            notes.writeInt (n.pad);
            notes.writeInt (n.velocity);
            notes.writeInt (n.length);
        }
        tv.setProperty ("notes", notes.getMemoryBlock(), nullptr);

        // 16 Levels hits keep their variation, one number a note, in the notes' order. Saved
        // only when a track has some, so older versions read the notes as they always did.
        if (std::any_of (t.notes.begin(), t.notes.end(), [] (const Note& n) { return n.variation != 0; }))
        {
            juce::MemoryOutputStream vars;
            for (const auto& n : t.notes)
                vars.writeInt (n.variation);
            tv.setProperty ("variations", vars.getMemoryBlock(), nullptr);
        }
        v.appendChild (tv, nullptr);
    }
    return v;
}

Sequence Sequence::fromValueTree (const juce::ValueTree& v)
{
    Sequence s;
    s.name = v["name"].toString();
    s.bpm = juce::jlimit (30.0, 300.0, (double) v.getProperty ("bpm", 90.0));
    s.beatsPerBar = juce::jlimit (1, 16, (int) v.getProperty ("beatsPerBar", 4));
    s.bars = juce::jlimit (1, kMaxBars, (int) v.getProperty ("bars", 2));
    s.loop = v.getProperty ("loop", true);
    s.tracks.clear();

    for (const auto& tv : v)
    {
        if (! tv.hasType ("Track") || (int) s.tracks.size() >= kMaxTracks)
            continue;

        Track t;
        t.name = tv["name"].toString();
        t.mute = tv["mute"];
        t.solo = tv["solo"];
        if (const auto* data = tv["notes"].getBinaryData())
        {
            juce::MemoryInputStream in (*data, false);
            const auto* varData = tv["variations"].getBinaryData();
            std::unique_ptr<juce::MemoryInputStream> vars;
            if (varData != nullptr && varData->getSize() * 4 == data->getSize())
                vars = std::make_unique<juce::MemoryInputStream> (*varData, false);
            while (in.getNumBytesRemaining() >= 16)
            {
                Note n;
                n.tick = in.readInt();
                n.pad = in.readInt();
                n.velocity = juce::jlimit (1, 127, in.readInt());
                n.length = juce::jmax (1, in.readInt());
                if (vars != nullptr && vars->getNumBytesRemaining() >= 4)
                    n.variation = juce::jlimit (0, 127, vars->readInt());
                if (juce::isPositiveAndBelow (n.pad, mpc::kPadCount) && juce::isPositiveAndBelow (n.tick, s.lengthTicks()))
                    t.notes.push_back (n);
            }
            std::sort (t.notes.begin(), t.notes.end());
        }
        s.tracks.push_back (std::move (t));
    }

    if (s.tracks.empty())
        s.tracks = Sequence().tracks;
    return s;
}

//==============================================================================
int Song::totalBars (const std::vector<Sequence>& seqs) const
{
    int bars = 0;
    for (const auto& s : steps)
        if (juce::isPositiveAndBelow (s.sequence, (int) seqs.size()))
            bars += seqs[(size_t) s.sequence].bars * s.repeats;
    return bars;
}

void Song::sequenceRemoved (int index)
{
    steps.erase (std::remove_if (steps.begin(), steps.end(), [index] (const SongStep& s) { return s.sequence == index; }),
                 steps.end());
    for (auto& s : steps)
        if (s.sequence > index)
            --s.sequence;
}

juce::ValueTree Song::toValueTree() const
{
    juce::ValueTree v ("Song");
    v.setProperty ("name", name, nullptr);
    v.setProperty ("loop", loop, nullptr);
    for (const auto& s : steps)
    {
        juce::ValueTree step ("Step");
        step.setProperty ("sequence", s.sequence, nullptr);
        step.setProperty ("repeats", s.repeats, nullptr);
        v.appendChild (step, nullptr);
    }
    return v;
}

Song Song::fromValueTree (const juce::ValueTree& v, int numSequences)
{
    Song song;
    song.name = v["name"].toString();
    song.loop = v["loop"];
    for (const auto& step : v)
    {
        const int index = step["sequence"];
        if (step.hasType ("Step") && juce::isPositiveAndBelow (index, numSequences))
            song.steps.push_back ({ index, juce::jlimit (1, 99, (int) step.getProperty ("repeats", 1)) });
    }
    return song;
}

//==============================================================================
int gridTicks (Grid g)
{
    switch (g)
    {
        case Grid::eighth:              return kTicksPerBeat / 2;
        case Grid::eighthTriplet:       return kTicksPerBeat / 3;
        case Grid::sixteenth:           return kTicksPerBeat / 4;
        case Grid::sixteenthTriplet:    return kTicksPerBeat / 6;
        case Grid::thirtySecond:        return kTicksPerBeat / 8;
        case Grid::thirtySecondTriplet: return kTicksPerBeat / 12;
        case Grid::off:                 break;
    }
    return 0;
}

juce::String gridName (Grid g)
{
    switch (g)
    {
        case Grid::eighth:              return "1/8";
        case Grid::eighthTriplet:       return "1/8 triplets";
        case Grid::sixteenth:           return "1/16";
        case Grid::sixteenthTriplet:    return "1/16 triplets";
        case Grid::thirtySecond:        return "1/32";
        case Grid::thirtySecondTriplet: return "1/32 triplets";
        case Grid::off:                 break;
    }
    return "Off";
}

bool gridSwings (Grid g)
{
    return g == Grid::eighth || g == Grid::sixteenth;
}

int stepTick (Grid g, int swing, int n)
{
    const int step = gridTicks (g);
    int tick = n * step;
    if (gridSwings (g) && (n & 1) != 0)
        tick += juce::roundToInt (step * (juce::jlimit (50, 75, swing) - 50) / 50.0);
    return tick;
}

int correct (int tick, Grid g, int swing, int lengthTicks)
{
    const int step = gridTicks (g);
    int result = tick;
    if (step > 0)
    {
        const int n0 = (int) std::floor ((double) tick / step);
        int best = stepTick (g, swing, n0);
        for (int n = n0 - 1; n <= n0 + 2; ++n)
        {
            const int t = stepTick (g, swing, n);
            if (std::abs (t - tick) < std::abs (best - tick))
                best = t;
        }
        result = best;
    }
    if (lengthTicks > 0)
        result = ((result % lengthTicks) + lengthTicks) % lengthTicks;
    return result;
}

void correct (Track& track, Grid g, int swing, int lengthTicks)
{
    if (gridTicks (g) == 0)
        return;
    auto notes = std::move (track.notes);
    track.notes.clear();
    for (auto n : notes)
    {
        n.tick = correct (n.tick, g, swing, lengthTicks);
        track.add (n);
    }
}

juce::String formatPosition (double tick, int beatsPerBar)
{
    const int t = juce::jmax (0, (int) std::floor (tick));
    const int perBar = beatsPerBar * kTicksPerBeat;
    return juce::String (t / perBar + 1).paddedLeft ('0', 3) + "." + juce::String ((t % perBar) / kTicksPerBeat + 1) + "."
           + juce::String (t % kTicksPerBeat).paddedLeft ('0', 2);
}

//==============================================================================
juce::MemoryBlock toMidiFile (const Sequence& s)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote (kTicksPerBeat);

    juce::MidiMessageSequence tempo;
    auto name = juce::MidiMessage::textMetaEvent (3, s.name.isNotEmpty() ? s.name : "Sequence");
    tempo.addEvent (name, 0);
    tempo.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / s.bpm)), 0);
    tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (s.beatsPerBar, 4), 0);
    tempo.addEvent (juce::MidiMessage::endOfTrack(), s.lengthTicks());
    file.addTrack (tempo);

    for (const auto& t : s.tracks)
    {
        if (t.notes.empty())
            continue;
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, t.name), 0);
        for (const auto& n : t.notes)
        {
            const int note = noteForPad (n.pad);
            track.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) n.velocity), n.tick);
            track.addEvent (juce::MidiMessage::noteOff (10, note), juce::jmin (s.lengthTicks(), n.tick + n.length));
        }
        track.addEvent (juce::MidiMessage::endOfTrack(), s.lengthTicks());
        track.updateMatchedPairs();
        file.addTrack (track);
    }

    juce::MemoryOutputStream out;
    file.writeTo (out, 1);
    return out.getMemoryBlock();
}

bool fromMidiFile (const void* data, size_t size, Sequence& s, juce::String& error)
{
    juce::MemoryInputStream in (data, size, false);
    juce::MidiFile file;
    if (! file.readFrom (in))
    {
        error = "That isn't a MIDI file.";
        return false;
    }

    const short format = file.getTimeFormat();
    if (format <= 0)
    {
        error = "That MIDI file counts time in frames, not beats.";
        return false;
    }
    const double scale = (double) kTicksPerBeat / format;

    juce::MidiMessageSequence tempos;
    file.findAllTempoEvents (tempos);
    if (tempos.getNumEvents() > 0)
        s.bpm = juce::jlimit (30.0, 300.0, 60.0 / tempos.getEventPointer (0)->message.getTempoSecondsPerQuarterNote());

    juce::MidiMessageSequence signatures;
    file.findAllTimeSigEvents (signatures);
    if (signatures.getNumEvents() > 0)
    {
        int num = 4, den = 4;
        signatures.getEventPointer (0)->message.getTimeSignatureInfo (num, den);
        s.beatsPerBar = juce::jlimit (1, 16, juce::roundToInt (num * 4.0 / den));
    }

    std::vector<Track> tracks;
    int lastTick = 0, skipped = 0;
    for (int i = 0; i < file.getNumTracks(); ++i)
    {
        juce::MidiMessageSequence events (*file.getTrack (i));
        events.updateMatchedPairs();
        Track t;
        t.name = "Track " + juce::String ((int) tracks.size() + 1);
        for (const auto* e : events)
        {
            if (e->message.isTrackNameEvent())
                t.name = e->message.getTextFromTextMetaEvent();
            if (! e->message.isNoteOn())
                continue;
            const int pad = padForNote (e->message.getNoteNumber());
            if (pad < 0)
            {
                ++skipped;
                continue;
            }
            Note n;
            n.tick = juce::roundToInt (e->message.getTimeStamp() * scale);
            n.pad = pad;
            n.velocity = juce::jlimit (1, 127, (int) e->message.getVelocity());
            n.length = e->noteOffObject != nullptr ? juce::jmax (1, juce::roundToInt ((e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp()) * scale))
                                                   : kTicksPerBeat / 4;
            t.add (n);
            lastTick = juce::jmax (lastTick, n.tick);
        }
        if (! t.notes.empty() && (int) tracks.size() < kMaxTracks)
            tracks.push_back (std::move (t));
    }

    if (tracks.empty())
    {
        error = skipped > 0 ? "None of that file's notes are on the MPC's pad notes (35 to 98)." : "That MIDI file has no notes.";
        return false;
    }

    s.tracks = std::move (tracks);
    while ((int) s.tracks.size() < 8)
        s.tracks.push_back ({ "Track " + juce::String ((int) s.tracks.size() + 1), false, false, {} });
    s.bars = juce::jlimit (1, kMaxBars, lastTick / s.ticksPerBar() + 1);
    error = skipped > 0 ? juce::String (skipped) + " notes weren't on pad notes and were left out." : juce::String();
    return true;
}
}
