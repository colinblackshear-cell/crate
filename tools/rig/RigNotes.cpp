#include "RigNotes.h"

#include <algorithm>
#include <cmath>

namespace rignotes
{
void Clip::sort()
{
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
}

juce::String encode (const std::vector<Note>& notes)
{
    juce::String text;
    for (const auto& n : notes)
        text << juce::String (n.beat, 5) << ' ' << juce::String (n.length, 5) << ' ' << n.note << ' '
             << juce::String (n.velocity, 3) << ' ' << n.channel << ';';
    return text;
}

std::vector<Note> decode (const juce::String& text)
{
    std::vector<Note> notes;
    for (const auto& item : juce::StringArray::fromTokens (text, ";", ""))
    {
        const auto parts = juce::StringArray::fromTokens (item, " ", "");
        if (parts.size() < 4)
            continue;
        Note n;
        n.beat = juce::jmax (0.0, parts[0].getDoubleValue());
        n.length = juce::jmax (1.0e-3, parts[1].getDoubleValue());
        n.note = juce::jlimit (0, 127, parts[2].getIntValue());
        n.velocity = juce::jlimit (0.0f, 1.0f, (float) parts[3].getDoubleValue());
        n.channel = parts.size() > 4 ? juce::jlimit (1, 16, parts[4].getIntValue()) : 1;
        notes.push_back (n);
    }
    return notes;
}

juce::ValueTree toTree (const std::vector<Clip>& clips)
{
    juce::ValueTree t (kTreeType);
    for (const auto& c : clips)
    {
        juce::ValueTree clip ("NoteClip");
        clip.setProperty ("strip", c.strip, nullptr);
        clip.setProperty ("startBar", c.startBar, nullptr);
        clip.setProperty ("bars", c.bars, nullptr);
        clip.setProperty ("notes", encode (c.notes), nullptr);
        t.appendChild (clip, nullptr);
    }
    return t;
}

std::vector<Clip> fromTree (const juce::ValueTree& t)
{
    std::vector<Clip> clips;
    if (! t.hasType (kTreeType))
        return clips;
    for (const auto& child : t)
    {
        if (! child.hasType ("NoteClip"))
            continue;
        Clip c;
        c.strip = child.getProperty ("strip", -1);
        c.startBar = child.getProperty ("startBar", 0.0);
        c.bars = child.getProperty ("bars", 4.0);
        c.notes = decode (child.getProperty ("notes").toString());
        if (c.strip >= 0 && c.bars > 0)
        {
            c.sort();
            clips.push_back (std::move (c));
        }
    }
    return clips;
}

std::vector<Note> notesFrom (const std::vector<Event>& events, int samples, double beats)
{
    std::vector<Note> notes;
    if (samples <= 0 || beats <= 0)
        return notes;
    const double perSample = beats / samples;
    // Each held note: where its note on was (an index into notes), by channel and note.
    std::array<std::array<int, 128>, 16> held;
    for (auto& h : held)
        h.fill (-1);
    for (const auto& e : events)
    {
        const int ch = juce::jlimit (1, 16, e.channel) - 1;
        const int key = juce::jlimit (0, 127, e.note);
        const double at = juce::jlimit (0, samples, e.sample) * perSample;
        auto& h = held[(size_t) ch][(size_t) key];
        if (h >= 0)
        {
            // A note off (or the same note again): the held one ends here.
            notes[(size_t) h].length = juce::jmax (1.0e-3, at - notes[(size_t) h].beat);
            h = -1;
        }
        if (e.on && e.velocity > 0)
        {
            Note n;
            n.beat = at;
            n.length = juce::jmax (1.0e-3, beats - at);   // until it's let go
            n.note = key;
            n.velocity = juce::jlimit (0.0f, 1.0f, e.velocity);
            n.channel = ch + 1;
            h = (int) notes.size();
            notes.push_back (n);
        }
    }
    // (Notes still held run to the end, as they were made.)
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
    return notes;
}

void quantize (std::vector<Note>& notes, double grid, float strength, bool lengths)
{
    if (grid <= 0)
        return;
    strength = juce::jlimit (0.0f, 1.0f, strength);
    for (auto& n : notes)
    {
        const double snapped = std::round (n.beat / grid) * grid;
        n.beat = juce::jmax (0.0, n.beat + (snapped - n.beat) * strength);
        if (lengths)
            n.length = juce::jmax (grid, std::round (n.length / grid) * grid);
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.beat < b.beat; });
}

juce::String noteName (int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    note = juce::jlimit (0, 127, note);
    return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
}
} // namespace rignotes

//==============================================================================
void RigNotePlayer::fill (Live& l, const std::vector<rignotes::Clip>& clips)
{
    l.numClips = 0;
    int used = 0;
    for (const auto& c : clips)
    {
        if (l.numClips >= kMaxClips || ! juce::isPositiveAndBelow (c.strip, kMaxStrips))
            continue;
        const int count = juce::jmin ((int) c.notes.size(), kMaxNotes - used);
        l.clips[(size_t) l.numClips++] = { c.strip, c.startBar, c.bars, used, count };
        for (int i = 0; i < count; ++i)
            l.notes[(size_t) (used + i)] = c.notes[(size_t) i];
        used += count;
    }
}

void RigNotePlayer::publish (const std::vector<rignotes::Clip>& clips)
{
    const juce::SpinLock::ScopedLockType sl (lock);
    fill (*pending, clips);
    ++pendingVersion;
}

void RigNotePlayer::letGo (juce::MidiBuffer* const* dests, int numDests)
{
    for (int s = 0; s < kMaxStrips; ++s)
        for (int ch = 0; ch < 16; ++ch)
        {
            auto& bits = sounding[(size_t) s][(size_t) ch];
            if (bits.none())
                continue;
            if (s < numDests && dests[s] != nullptr)
                for (int k = 0; k < 128; ++k)
                    if (bits.test ((size_t) k))
                        dests[s]->addEvent (juce::MidiMessage::noteOff (ch + 1, k), 0);
            bits.reset();
        }
}

void RigNotePlayer::process (double beat, int n, double bpm, double sampleRate, int bpb, bool playing,
                             juce::MidiBuffer* const* dests, int numDests, int muteStrip, double muteBar)
{
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (sl.isLocked() && pendingVersion != liveVersion)
        {
            std::swap (pending, live);
            liveVersion = pendingVersion;
            // (What was sounding may be gone from the clips: let it go.)
            letGo (dests, numDests);
        }
    }
    const double bps = bpm / (60.0 * juce::jmax (1.0, sampleRate));
    const bool jumped = lastEnd >= 0 && std::abs (beat - lastEnd) > 1.0e-4;
    if (releaseRequested.exchange (false) || ! playing || jumped)
        letGo (dests, numDests);
    if (! playing || n <= 0 || bps <= 0)
    {
        lastEnd = -1;
        return;
    }
    const double end = beat + n * bps;
    lastEnd = end;
    bpb = juce::jmax (1, bpb);
    auto sampleAt = [&] (double b) { return juce::jlimit (0, n - 1, (int) std::floor ((b - beat) / bps)); };

    const auto& l = *live;
    // The note offs first, so a note played again at the same moment starts again.
    for (int pass = 0; pass < 2; ++pass)
        for (int c = 0; c < l.numClips; ++c)
        {
            const auto& clip = l.clips[(size_t) c];
            if (clip.strip >= numDests || dests[clip.strip] == nullptr)
                continue;
            const bool muted = clip.strip == muteStrip && std::abs (clip.startBar - muteBar) < 1.0e-6;
            const double from = clip.startBar * bpb, to = (clip.startBar + clip.bars) * bpb;
            if (end <= from - 1.0e-9 || beat >= to + 1.0e-9)
                continue;
            auto* out = dests[clip.strip];
            for (int i = 0; i < clip.count; ++i)
            {
                const auto& note = l.notes[(size_t) (clip.first + i)];
                const double on = from + note.beat;
                const double off = juce::jmin (to - 1.0e-6, on + note.length);
                auto& bits = sounding[(size_t) clip.strip][(size_t) juce::jlimit (0, 15, note.channel - 1)];
                if (pass == 0)
                {
                    if (off >= beat && off < end && bits.test ((size_t) note.note))
                    {
                        out->addEvent (juce::MidiMessage::noteOff (note.channel, note.note), sampleAt (off));
                        bits.reset ((size_t) note.note);
                    }
                }
                else if (! muted && on >= beat - 1.0e-9 && on < end && on < to)
                {
                    const int at = sampleAt (on);
                    if (bits.test ((size_t) note.note))
                        out->addEvent (juce::MidiMessage::noteOff (note.channel, note.note), at);
                    out->addEvent (juce::MidiMessage::noteOn (note.channel, note.note, juce::jlimit (0.01f, 1.0f, note.velocity)), at);
                    bits.set ((size_t) note.note);
                    // A very short note that ends in this block too.
                    if (off < end && off > on)
                    {
                        out->addEvent (juce::MidiMessage::noteOff (note.channel, note.note), juce::jmax (at, sampleAt (off)));
                        bits.reset ((size_t) note.note);
                    }
                }
            }
        }
}
