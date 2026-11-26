#include "RigBounce.h"

#include "Mp3Writer.h"
#include "RigEngine.h"
#include "RigExport.h"
#include "RigSong.h"
#include "core/TrackAnalysis.h"

namespace
{
juce::String safeName (juce::String s)
{
    return s.removeCharacters ("/\\:*?\"<>|").trim();
}

bool hasLoop (RigEngine& engine, int layer)
{
    const auto state = engine.getLooper().getState (layer);
    return state == RigLooper::State::playing || state == RigLooper::State::overdubbing;
}

bool writeWav24 (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    juce::WavAudioFormat wav;
    auto options = juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (2).withBitsPerSample (24);
    if (auto writer = wav.createWriterFor (stream, options))
        return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    return false;
}

double bpmOf (RigEngine& engine)
{
    return juce::jmax (20.0, engine.getTransport().getBpm());
}

double rateOf (RigEngine& engine)
{
    return engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
}

juce::String whatName (RigEngine& engine, const RigBounce::Options& o)
{
    switch (o.what)
    {
        case RigBounce::What::section:
        {
            const auto secs = rigsong::sections (engine.getTimeline());
            return juce::isPositiveAndBelow (o.section, (int) secs.size()) ? secs[(size_t) o.section].name : juce::String ("Section");
        }
        case RigBounce::What::loops: return "Loops";
        case RigBounce::What::song: break;
    }
    return "Song";
}
}

juce::File RigBounce::toLogicFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig").getChildFile ("To Logic");
}

juce::File RigBounce::newFolder (const juce::File& parent, const Options& o)
{
    const auto name = safeName (o.name.isNotEmpty() ? o.name : juce::String ("Song"));
    const auto what = o.what == What::song ? juce::String() : o.what == What::loops ? juce::String (" loops") : juce::String (" section");
    return parent.getNonexistentChildFile (name + what + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M"), {}, false);
}

juce::Range<double> RigBounce::barsFor (RigEngine& engine, const Options& o)
{
    auto& timeline = engine.getTimeline();
    switch (o.what)
    {
        case What::song:
            // (a friend's track is the song too: to its end)
            return { 0.0, juce::jmax ((double) RigExport::timelineBars (engine), std::ceil (engine.songLengthBarsOfTrack() - 1.0e-3)) };
        case What::section:
        {
            const auto secs = rigsong::sections (timeline);
            if (! juce::isPositiveAndBelow (o.section, (int) secs.size()))
                return {};
            return { secs[(size_t) o.section].start, secs[(size_t) o.section].end() };
        }
        case What::loops:
        {
            // one pass of the longest loop that's playing
            const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
            double beats = 0;
            for (const int l : loopsFor (engine, o))
                beats = juce::jmax (beats, engine.getLooper().getLayer (l).lengthBeats.load());
            return { 0.0, std::ceil (beats / bpb - 1.0e-6) };
        }
    }
    return {};
}

juce::Array<int> RigBounce::loopsFor (RigEngine& engine, const Options& o)
{
    juce::Array<int> out;
    auto& timeline = engine.getTimeline();
    const auto bars = o.what == What::loops ? juce::Range<double>() : barsFor (engine, o);
    for (int l = 0; l < RigLooper::kLayers; ++l)
    {
        if (! hasLoop (engine, l))
            continue;
        if (o.what == What::loops)
        {
            if (! engine.getLooper().getLayer (l).mute.load())
                out.add (l);
            continue;
        }
        const int lane = RigTimeline::laneForLoop (l);
        if (! timeline.laneUsed (lane))
            continue;
        // only loops with a block somewhere in those bars
        for (const auto& b : timeline.getBlocks())
            if (b.lane == lane && b.start < bars.getEnd() - 1.0e-6 && b.end() > bars.getStart() + 1.0e-6)
            {
                out.add (l);
                break;
            }
    }
    return out;
}

bool RigBounce::hasTrack (RigEngine& engine)
{
    return engine.getFriendTrack().hasAudio();
}

juce::String RigBounce::writeNoteForFriend (RigEngine& engine, const Options& o, const juce::File& folder)
{
    if (! hasTrack (engine) || o.what == What::loops)
        return {};
    const auto info = engine.getFriendTrack().getInfo();
    const double bpm = bpmOf (engine);
    juce::String text;
    text << "From Kaleidobox, over " << (info.name.isNotEmpty() ? info.name : juce::String ("your track")) << "\n\n"
         << "Tempo: " << juce::String (bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << " BPM, "
         << engine.getTransport().getBeatsPerBar() << "/4";
    if (juce::isPositiveAndBelow (info.key, 12))
        text << ", " << crate::TrackAnalysis::keyName (info.key, info.minor);
    text << "\n\n";
    if (o.what == What::song)
        text << "Every file here starts at the same moment as your mixdown. Put them at the same place as the mixdown "
                "in your session (its start) and they line up.\n";
    else
        text << "These files start at bar " << (int) barsFor (engine, o).getStart() + 1 << " of the song.\n";
    text << (o.withTrack ? "The mix has your track in it.\n" : "Your track isn't in them: just my parts.\n");
    const auto file = folder.getChildFile ("Read me - tempo and lining up.txt");
    return file.replaceWithText (text) ? file.getFileName() : juce::String();
}

juce::String RigBounce::describe (RigEngine& engine, const Options& o)
{
    const auto bars = barsFor (engine, o);
    const auto loops = loopsFor (engine, o);
    const bool track = hasTrack (engine) && o.what != What::loops;
    if (bars.getLength() <= 0.0 || (loops.isEmpty() && o.split != Split::everything && ! (track && o.withTrack)))
        return o.what == What::loops ? "No loops playing to bounce." : "Nothing in the song to bounce yet.";
    juce::String s = whatName (engine, o) + ", " + rigsong::barsText (bars.getLength()) + ": ";
    if (o.mixOnly)
        s << (o.split == Split::everything ? "one mix of every tool and loop (plays once, in real time)" : "the mix");
    else
        switch (o.split)
        {
            case Split::mix: s << "the mix"; break;
            case Split::eachLoop: s << loops.size() << (loops.size() == 1 ? " loop" : " loops") << " + the mix"; break;
            case Split::everything: s << loops.size() << (loops.size() == 1 ? " loop" : " loops") << ", every tool + the mix (plays once, in real time)"; break;
        }
    if (track)
        s << (o.withTrack ? ", with his track" : ", without his track");
    s << ", " << (o.wav && o.mp3 ? "WAV + MP3" : o.mp3 ? "MP3" : "WAV");
    if (o.dry)
        s << ", dry";
    return s;
}

namespace
{
// `audio` with `seconds` of silence before it.
juce::AudioBuffer<float> withLead (const juce::AudioBuffer<float>& audio, double seconds, double rate)
{
    const int lead = (int) std::round (juce::jmax (0.0, seconds) * rate);
    juce::AudioBuffer<float> out (juce::jmax (1, audio.getNumChannels()), lead + audio.getNumSamples());
    out.clear();
    for (int ch = 0; ch < audio.getNumChannels(); ++ch)
        out.copyFrom (ch, lead, audio, ch, 0, audio.getNumSamples());
    return out;
}
}

bool RigBounce::writeAudio (const juce::File& base, const juce::AudioBuffer<float>& given, double rate, const Options& o, juce::StringArray& files)
{
    juce::AudioBuffer<float> padded;
    if (o.leadSeconds > 0.0005)
        padded = withLead (given, o.leadSeconds, rate);
    const auto& audio = o.leadSeconds > 0.0005 ? padded : given;
    bool ok = true;
    const auto parent = base.getParentDirectory();
    const auto name = base.getFileName();
    if (o.wav || ! mp3::available())
    {
        const auto f = parent.getChildFile (name + ".wav");
        ok = writeWav24 (f, audio, rate) && ok;
        files.add (f.getFileName());
    }
    if (o.mp3 && mp3::available())
    {
        const auto f = parent.getChildFile (name + ".mp3");
        ok = mp3::write (f, audio, rate, 320) && ok;
        files.add (f.getFileName());
    }
    return ok;
}

bool RigBounce::writeMarkers (RigEngine& engine, const juce::File& file, juce::Range<double> bars, double bpm)
{
    const int ppq = 960;
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    juce::MidiMessageSequence track;
    track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
    track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
    for (const auto& sec : rigsong::sections (engine.getTimeline()))
        if (sec.start >= bars.getStart() - 1.0e-6 && sec.start < bars.getEnd() - 1.0e-6)
            track.addEvent (juce::MidiMessage::textMetaEvent (6, sec.name), (sec.start - bars.getStart()) * bpb * ppq);
    track.addEvent (juce::MidiMessage::endOfTrack(), bars.getLength() * bpb * ppq);
    track.sort();
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    midi.addTrack (track);
    file.deleteFile();
    juce::FileOutputStream stream (file);
    return stream.openedOk() && midi.writeTo (stream);
}

RigBounce::Result RigBounce::bounceLoops (RigEngine& engine, const Options& o, const juce::File& folder)
{
    Result result;
    result.folder = folder;
    const auto bars = barsFor (engine, o);
    const auto loops = loopsFor (engine, o);
    const bool track = hasTrack (engine) && o.what != What::loops && o.withTrack;
    if (bars.getLength() <= 0.0 || (loops.isEmpty() && o.split != Split::everything && ! track))
    {
        result.error = describe (engine, o);
        return result;
    }
    if (! folder.createDirectory())
    {
        result.error = "Couldn't make " + folder.getFullPathName() + ".";
        return result;
    }
    const double bpm = bpmOf (engine), rate = rateOf (engine);
    const auto tempo = juce::String (bpm, 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " BPM";
    const auto what = whatName (engine, o);
    bool ok = true;

    juce::AudioBuffer<float> mix;
    for (const int l : loops)
    {
        auto stem = o.what == What::loops
                        ? RigExport::renderLoopPass (engine, l, bpm, rate, ! o.dry, bars.getLength(), o.tailSeconds)
                        : RigExport::renderTimelineRange (engine, l, bpm, rate, ! o.dry, bars.getStart(), bars.getEnd(), o.tailSeconds);
        if (stem.getNumSamples() == 0)
            continue;
        if (o.split != Split::mix && ! o.mixOnly)
            ok = writeAudio (folder.getChildFile (what + " - Loop " + juce::String (l + 1) + " - " + tempo), stem, rate, o, result.files) && ok;
        if (o.split != Split::everything)
        {
            if (mix.getNumSamples() < stem.getNumSamples())
            {
                juce::AudioBuffer<float> bigger (2, stem.getNumSamples());
                bigger.clear();
                for (int ch = 0; ch < 2 && mix.getNumSamples() > 0; ++ch)
                    bigger.copyFrom (ch, 0, mix, ch, 0, mix.getNumSamples());
                mix = std::move (bigger);
            }
            for (int ch = 0; ch < 2; ++ch)
                mix.addFrom (ch, 0, stem, ch, 0, stem.getNumSamples());
        }
    }
    if (o.split != Split::everything && track)
    {
        // His track, under the loops (in the mix only: he has it).
        const auto& ft = engine.getFriendTrack();
        auto under = RigFriendTrack::renderRange (*ft.getAudio(), ft.getInfo(), bpm, rate, engine.getTransport().getBeatsPerBar(),
                                                  bars.getStart(), bars.getEnd(), o.tailSeconds, ft.gain());
        if (mix.getNumSamples() < under.getNumSamples())
        {
            juce::AudioBuffer<float> bigger (2, under.getNumSamples());
            bigger.clear();
            for (int ch = 0; ch < 2 && mix.getNumSamples() > 0; ++ch)
                bigger.copyFrom (ch, 0, mix, ch, 0, mix.getNumSamples());
            mix = std::move (bigger);
        }
        for (int ch = 0; ch < 2; ++ch)
            mix.addFrom (ch, 0, under, ch, 0, under.getNumSamples());
    }
    if (o.split != Split::everything && mix.getNumSamples() > 0)
    {
        // never past full scale: turned down as a whole, never clipped
        const float peak = mix.getMagnitude (0, mix.getNumSamples());
        if (peak > 0.98f)
            mix.applyGain (0.98f / peak);
        ok = writeAudio (folder.getChildFile (what + " - Mix - " + tempo), mix, rate, o, result.files) && ok;
    }
    if (o.what != What::loops)
    {
        const auto markers = folder.getChildFile (what + " markers.mid");
        if (writeMarkers (engine, markers, bars, bpm))
            result.files.add (markers.getFileName());
    }
    if (! ok)
        result.error = "Some files couldn't be written in " + folder.getFullPathName() + ".";
    return result;
}

void RigBounce::convertRecorded (const juce::Array<juce::File>& wavs, const Options& o, juce::StringArray& files)
{
    for (const auto& f : wavs)
    {
        // Only the mix: the stems (and the MIDI, the Logic note) go.
        if (o.mixOnly && ! f.getFileNameWithoutExtension().endsWithIgnoreCase (" Mix"))
        {
            f.deleteFile();
            continue;
        }
        if (! f.hasFileExtension ("wav"))
        {
            files.add (f.getFileName());
            continue;
        }
        if (o.leadSeconds > 0.0005)
        {
            // Starts at the same moment as his mixdown: written again with the time before his bar 1.
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
            if (reader != nullptr)
            {
                juce::AudioBuffer<float> audio ((int) juce::jmax (1u, reader->numChannels), (int) reader->lengthInSamples);
                reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
                const double rate = reader->sampleRate;
                reader.reset();
                auto padded = withLead (audio, o.leadSeconds, rate);
                if (padded.getNumChannels() == 1)
                {
                    juce::AudioBuffer<float> stereo (2, padded.getNumSamples());
                    stereo.copyFrom (0, 0, padded, 0, 0, padded.getNumSamples());
                    stereo.copyFrom (1, 0, padded, 0, 0, padded.getNumSamples());
                    padded = std::move (stereo);
                }
                writeWav24 (f, padded, rate);
            }
        }
        if (o.wav)
            files.add (f.getFileName());
        if (! o.mp3 || ! mp3::available())
            continue;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (f.createInputStream().release(), true));
        if (reader == nullptr)
            continue;
        juce::AudioBuffer<float> audio ((int) juce::jmax (1u, reader->numChannels), (int) reader->lengthInSamples);
        reader->read (&audio, 0, (int) reader->lengthInSamples, 0, true, true);
        const double rate = reader->sampleRate;
        reader.reset();
        if (audio.getNumChannels() == 1)
        {
            juce::AudioBuffer<float> stereo (2, audio.getNumSamples());
            stereo.copyFrom (0, 0, audio, 0, 0, audio.getNumSamples());
            stereo.copyFrom (1, 0, audio, 0, 0, audio.getNumSamples());
            audio = std::move (stereo);
        }
        const auto mp3File = f.withFileExtension ("mp3");
        if (mp3::write (mp3File, audio, rate, 320))
            files.add (mp3File.getFileName());
        if (! o.wav)
            f.deleteFile();
    }
}
