#include "RigLogicImport.h"
#include "RigEngine.h"
#include "RigSong.h"

namespace rigimport
{
MidiInfo readMidi (const juce::MidiFile& file)
{
    MidiInfo info;
    info.found = true;
    const double ticks = file.getTimeFormat() > 0 ? (double) file.getTimeFormat() : 480.0;

    juce::MidiMessageSequence tempos, signatures;
    file.findAllTempoEvents (tempos);
    file.findAllTimeSigEvents (signatures);
    if (tempos.getNumEvents() > 0)
    {
        const auto first = tempos.getEventPointer (0)->message;
        const double secondsPerQuarter = first.getTempoSecondsPerQuarterNote();
        // (MIDI keeps tempo in whole microseconds: 90 BPM comes back as 89.99996.)
        if (secondsPerQuarter > 0)
            info.bpm = std::round (60.0 / secondsPerQuarter * 1000.0) / 1000.0;
        for (int i = 1; i < tempos.getNumEvents(); ++i)
            if (std::abs (60.0 / tempos.getEventPointer (i)->message.getTempoSecondsPerQuarterNote() - info.bpm) > 0.01)
                ++info.tempoChanges;
    }
    if (signatures.getNumEvents() > 0)
        signatures.getEventPointer (0)->message.getTimeSignatureInfo (info.numerator, info.denominator);

    // Logic writes its markers as marker (type 6) meta events; cue points (7) count too.
    for (int t = 0; t < file.getNumTracks(); ++t)
        if (const auto* track = file.getTrack (t))
            for (const auto* e : *track)
            {
                const auto& m = e->message;
                if (! m.isMetaEvent() || (m.getMetaEventType() != 6 && m.getMetaEventType() != 7))
                    continue;
                const auto name = m.getTextFromTextMetaEvent().trim();
                const double beat = juce::jmax (0.0, m.getTimeStamp() / ticks);
                info.markers.emplace_back (beat, name.isNotEmpty() ? name : juce::String ("Section"));
            }
    std::stable_sort (info.markers.begin(), info.markers.end(), [] (const auto& a, const auto& b) { return a.first < b.first; });
    return info;
}

MidiInfo readMidi (const juce::File& f)
{
    juce::FileInputStream in (f);
    juce::MidiFile file;
    if (! in.openedOk() || ! file.readFrom (in))
        return {};
    return readMidi (file);
}

juce::Array<juce::File> stemFiles (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.WAV;*.AIF;*.AIFF");
    // findChildFiles can list a file twice on case-insensitive disks.
    juce::Array<juce::File> unique;
    for (const auto& f : files)
        if (! f.getFileName().startsWith (".") && ! unique.contains (f))
            unique.add (f);
    std::sort (unique.begin(), unique.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return unique;
}

juce::File midiFile (const juce::File& folder)
{
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.mid;*.midi;*.MID;*.MIDI");
    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });
    return files.isEmpty() ? juce::File() : files.getFirst();
}

juce::StringArray stemNames (const juce::Array<juce::File>& files)
{
    juce::StringArray names;
    for (const auto& f : files)
        names.add (f.getFileNameWithoutExtension());
    // The part every name starts with (the project's name), cut back to a separator.
    if (names.size() > 1)
    {
        juce::String common = names[0];
        for (const auto& n : names)
            while (common.isNotEmpty() && ! n.startsWith (common))
                common = common.dropLastCharacters (1);
        const int cut = juce::jmax (common.lastIndexOfAnyOf ("_- "), -1);
        if (cut > 0)
            for (auto& n : names)
                n = n.substring (cut + 1);
    }
    for (auto& n : names)
    {
        n = n.trim().trimCharactersAtStart ("#0123456789_-. ").trim();
        if (n.isEmpty())
            n = "Stem";
    }
    // Logic's "Track Name_bip" style endings.
    for (auto& n : names)
        for (auto ending : { "_bip", " bip", "_mx", "_st" })
            if (n.endsWithIgnoreCase (ending))
                n = n.dropLastCharacters ((int) strlen (ending)).trim();
    return names;
}

Song make (std::vector<Stem> stems, const MidiInfo& midi, double fallbackBpm, const juce::String& name)
{
    Song song;
    song.name = name;
    song.bpm = midi.bpm > 0 ? midi.bpm : (fallbackBpm > 0 ? fallbackBpm : 120.0);
    song.bpm = juce::jlimit (30.0, 300.0, song.bpm);
    const int den = midi.denominator > 0 ? midi.denominator : 4;
    song.beatsPerBar = juce::jlimit (1, 16, juce::roundToInt (midi.numerator * 4.0 / den));
    if (! midi.found)
        song.notes.add ("No MIDI file, so it plays at " + juce::String (song.bpm, 1) + " BPM as one section");
    else if (midi.bpm <= 0)
        song.notes.add ("The MIDI file has no tempo, so it plays at " + juce::String (song.bpm, 1) + " BPM");
    if (midi.tempoChanges > 0)
        song.notes.add ("Logic's song changes tempo; Kaleidobox plays it all at " + juce::String (song.bpm, 1) + " BPM");
    if (den != 4 && den != 0)
        song.notes.add (juce::String (midi.numerator) + "/" + juce::String (den) + " counts as " + juce::String (song.beatsPerBar) + " beats a bar");

    // Every stem the same way round: stereo.
    stems.erase (std::remove_if (stems.begin(), stems.end(), [] (const Stem& s) { return s.audio.getNumSamples() == 0; }), stems.end());
    for (auto& s : stems)
        if (s.audio.getNumChannels() == 1)
        {
            s.audio.setSize (2, s.audio.getNumSamples(), true);
            s.audio.copyFrom (1, 0, s.audio, 0, 0, s.audio.getNumSamples());
        }

    // More than eight: the rest go together on the eighth layer.
    constexpr int kLayers = 8;
    if ((int) stems.size() > kLayers)
    {
        Stem other;
        other.name = "Other";
        other.sampleRate = stems[kLayers - 1].sampleRate;
        juce::StringArray mixed;
        int length = 0;
        for (size_t i = kLayers - 1; i < stems.size(); ++i)
            if (std::abs (stems[i].sampleRate - other.sampleRate) < 1.0)
                length = juce::jmax (length, stems[i].audio.getNumSamples());
        other.audio.setSize (2, length);
        other.audio.clear();
        for (size_t i = kLayers - 1; i < stems.size(); ++i)
        {
            if (std::abs (stems[i].sampleRate - other.sampleRate) >= 1.0)
            {
                song.notes.add (stems[i].name + " was left out (a different sample rate)");
                continue;
            }
            for (int ch = 0; ch < 2; ++ch)
                other.audio.addFrom (ch, 0, stems[i].audio, ch, 0, stems[i].audio.getNumSamples());
            mixed.add (stems[i].name);
        }
        song.notes.add ("Loop 8 (Other) is " + mixed.joinIntoString (", ") + " mixed together");
        stems.resize (kLayers - 1);
        stems.push_back (std::move (other));
    }

    double beats = 0;
    for (const auto& s : stems)
        beats = juce::jmax (beats, s.seconds() * song.bpm / 60.0);
    song.bars = beats / song.beatsPerBar;
    song.layers = std::move (stems);

    // The sections: Logic's markers, from bar 1 (a song whose first marker comes later starts
    // with "Start"), each running to the next.
    std::vector<RigTimeline::Marker> list;
    for (const auto& [beat, label] : midi.markers)
    {
        const double bar = beat / song.beatsPerBar;
        if (bar >= song.bars - 1.0e-6 && song.bars > 0)
            continue;
        if (! list.empty() && bar - list.back().bar < 1.0e-6)
        {
            list.back().name = label;   // two at one place: the later name
            continue;
        }
        RigTimeline::Marker m;
        m.bar = bar;
        m.name = label;
        list.push_back (m);
    }
    if (list.empty() || list.front().bar > 1.0e-6)
    {
        RigTimeline::Marker start;
        start.name = list.empty() ? (name.isNotEmpty() ? name : juce::String ("Song")) : juce::String ("Start");
        list.insert (list.begin(), start);
    }
    if ((int) list.size() > rigsong::kMaxSections)
    {
        song.notes.add ("Only the first " + juce::String (rigsong::kMaxSections) + " of Logic's " + juce::String ((int) list.size()) + " markers became sections");
        list.resize ((size_t) rigsong::kMaxSections);
    }
    for (size_t i = 0; i < list.size(); ++i)
        list[i].bars = (i + 1 < list.size() ? list[i + 1].bar : juce::jmax (song.bars, list[i].bar + 1.0)) - list[i].bar;
    song.sections = std::move (list);
    return song;
}

Song read (const juce::File& folder, double fallbackBpm)
{
    const auto files = stemFiles (folder);
    const auto names = stemNames (files);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::vector<Stem> stems;
    juce::StringArray unread;
    for (int i = 0; i < files.size(); ++i)
    {
        std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (files[i]) };
        if (reader == nullptr || reader->lengthInSamples <= 0 || reader->lengthInSamples > std::numeric_limits<int>::max())
        {
            unread.add (files[i].getFileName());
            continue;
        }
        Stem s;
        s.name = names[i];
        s.sampleRate = reader->sampleRate;
        s.audio.setSize (2, (int) reader->lengthInSamples);
        reader->read (&s.audio, 0, s.audio.getNumSamples(), 0, true, true);
        stems.push_back (std::move (s));
    }
    const auto mid = midiFile (folder);
    auto song = make (std::move (stems), mid.existsAsFile() ? readMidi (mid) : MidiInfo(), fallbackBpm, folder.getFileName());
    if (! unread.isEmpty())
        song.notes.add ("Couldn't read " + unread.joinIntoString (", "));
    if (song.layers.empty())
        song.notes.insert (0, "No WAV or AIFF stems in " + folder.getFileName()
                                  + ". In Logic: File > Export > All Tracks as Audio Files, into this folder");
    return song;
}

juce::String apply (RigEngine& engine, Song&& song)
{
    if (song.layers.empty())
        return song.notes.joinIntoString (". ") + ".";

    auto& transport = engine.getTransport();
    transport.setBpm (song.bpm);
    transport.setBeatsPerBar (song.beatsPerBar);

    auto& looper = engine.getLooper();
    juce::StringArray loops;
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        auto& layer = looper.getLayer (i);
        if (i < (int) song.layers.size())
        {
            auto& stem = song.layers[(size_t) i];
            const double beats = stem.seconds() * song.bpm / 60.0;
            looper.setLayerAudio (i, std::move (stem.audio), beats);
            engine.setLoopName (i, stem.name);
            layer.level = 0.8f;
            layer.mute = false;
            loops.add ("Loop " + juce::String (i + 1) + " " + stem.name);
        }
        else if (layer.state.load() != (int) RigLooper::State::empty)
        {
            looper.clear (i);
        }
    }
    if (song.name.isNotEmpty())
        engine.saveLoopBank (song.name);

    // The Song screen: the sections, and every loop from bar 1 to the end. Jumping to a section
    // or looping it plays the stems from that point, as each block starts its loop at bar 1.
    auto& timeline = engine.getTimeline();
    auto& blocks = timeline.getBlocks();
    blocks.erase (std::remove_if (blocks.begin(), blocks.end(), [] (const RigTimeline::Block& b)
                                  { return RigTimeline::lanes()[(size_t) juce::jlimit (0, RigTimeline::kLanes - 1, b.lane)].isLoop; }),
                  blocks.end());
    for (int i = 0; i < (int) song.layers.size(); ++i)
    {
        RigTimeline::Block b;
        b.lane = RigTimeline::laneForLoop (i);
        b.start = 0;
        b.length = song.bars;
        blocks.push_back (b);
    }
    timeline.getMarkers() = song.sections;
    timeline.changed();
    timeline.on = true;
    engine.getScenes().songMode = false;
    engine.songStartBar = 0;
    engine.controlsMoved = true;

    juce::String said = "Imported " + (song.name.isNotEmpty() ? song.name : juce::String ("the song")) + ": "
                      + juce::String ((int) song.sections.size()) + (song.sections.size() == 1 ? " section, " : " sections, ")
                      + juce::String (song.bpm, 1) + " BPM, " + loops.joinIntoString (", ") + ".";
    if (! song.notes.isEmpty())
        said << " " << song.notes.joinIntoString (". ") << ".";
    return said;
}
}
