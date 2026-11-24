#include "RigExport.h"

#include "RigEngine.h"

namespace
{
constexpr int kHoldBars = 4;   // a part with no length ("hold") is exported this long

// What a fresh copy of the effects needs to know the tempo (for the delay and the repeat).
struct FixedTempo : juce::AudioPlayHead
{
    double bpm = 120.0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (bpm);
        info.setIsPlaying (true);
        return info;
    }
};

// The loop's sound at a beat of it (0 .. lengthBeats), read the way the looper plays it.
float sampleAt (const juce::AudioBuffer<float>& audio, double lengthBeats, int ch, double beat)
{
    const int n = audio.getNumSamples();
    if (n < 2 || lengthBeats <= 0)
        return 0.0f;
    double p = std::fmod (beat, lengthBeats);
    if (p < 0)
        p += lengthBeats;
    const double pos = p / lengthBeats * n;
    const int i0 = juce::jlimit (0, n - 1, (int) pos);
    const int i1 = (i0 + 1) % n;
    const float frac = (float) (pos - i0);
    const float* d = audio.getReadPointer (juce::jmin (ch, audio.getNumChannels() - 1));
    return d[i0] + (d[i1] - d[i0]) * frac;
}

// Runs `buffer` through a fresh copy of the layer's built-in effects (not its plugin).
// `warmUp` is played through first and thrown away, so a loop's echoes carry round.
void applyEffects (RigEngine& engine, int layer, double bpm, double rate, juce::AudioBuffer<float>& buffer,
                   const juce::AudioBuffer<float>* warmUp)
{
    auto& source = engine.getLoopFx (layer);
    if (! source.isActive())
        return;
    StripFx fx;
    for (auto* p : source.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (auto* target = fx.params.getParameter (withId->paramID))
                target->setValueNotifyingHost (p->getValue());
    FixedTempo tempo;
    tempo.bpm = bpm;
    fx.setPlayHead (&tempo);
    const int block = 512;
    fx.setRateAndBufferSizeDetails (rate, block);
    fx.prepareToPlay (rate, block);
    juce::MidiBuffer none;
    const auto run = [&] (juce::AudioBuffer<float>& b)
    {
        for (int start = 0; start < b.getNumSamples(); start += block)
        {
            const int n = juce::jmin (block, b.getNumSamples() - start);
            juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, start, n);
            fx.processBlock (view, none);
        }
    };
    if (warmUp != nullptr)
    {
        juce::AudioBuffer<float> copy (*warmUp);
        run (copy);
    }
    run (buffer);
    fx.setPlayHead (nullptr);
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double rate)
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

juce::String safeName (juce::String s)
{
    return s.removeCharacters ("/\\:*?\"<>|").trim();
}

bool hasLoop (RigEngine& engine, int layer)
{
    const auto state = engine.getLooper().getState (layer);
    return state == RigLooper::State::playing || state == RigLooper::State::overdubbing;
}
}

juce::File RigExport::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Crate").getChildFile ("Rig").getChildFile ("Exports");
}

juce::AudioBuffer<float> RigExport::renderLoop (RigEngine& engine, int layer, double bpm, double rate, bool withEffects)
{
    const auto audio = engine.getLooper().copyLayerAudio (layer);
    const double beats = engine.getLooper().getLayer (layer).lengthBeats.load();
    if (audio.getNumSamples() == 0 || beats <= 0)
        return {};
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int n = juce::jmax (1, (int) std::round (beats * samplesPerBeat));
    juce::AudioBuffer<float> out (2, n);
    const float level = engine.getLooper().getLayer (layer).level.load();
    for (int i = 0; i < n; ++i)
        for (int ch = 0; ch < 2; ++ch)
            out.setSample (ch, i, sampleAt (audio, beats, ch, i / samplesPerBeat) * (withEffects ? level : 1.0f));
    if (withEffects)
    {
        const juce::AudioBuffer<float> warm (out);
        applyEffects (engine, layer, bpm, rate, out, &warm);
    }
    return out;
}

int RigExport::songBars (RigEngine& engine)
{
    int bars = 0;
    for (const auto& part : engine.getScenes().getScenes())
        bars += part.bars > 0 ? part.bars : kHoldBars;
    return bars;
}

juce::AudioBuffer<float> RigExport::renderSongStem (RigEngine& engine, int layer, double bpm, double rate, bool withEffects)
{
    const auto& parts = engine.getScenes().getScenes();
    const auto audio = engine.getLooper().copyLayerAudio (layer);
    const double beats = engine.getLooper().getLayer (layer).lengthBeats.load();
    if (parts.empty() || audio.getNumSamples() == 0 || beats <= 0)
        return {};
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int total = (int) std::round (songBars (engine) * bpb * samplesPerBeat);
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    const float level = withEffects ? engine.getLooper().getLayer (layer).level.load() : 1.0f;

    // Like the Rig: a part leaves a loop as it was (-1), mutes it, or plays it; a loop that
    // comes in with a part starts from its top on the part's first beat.
    bool on = ! engine.getLooper().getLayer (layer).mute.load();
    double startBeat = 0, partBeat = 0;
    for (const auto& part : parts)
    {
        const int value = part.state.loopMutes[(size_t) layer];
        if (value == 0 && ! on)
            startBeat = partBeat;
        if (value >= 0)
            on = value == 0;
        const double partBeats = (part.bars > 0 ? part.bars : kHoldBars) * bpb;
        if (on)
        {
            const int from = (int) std::round (partBeat * samplesPerBeat);
            const int to = juce::jmin (total, (int) std::round ((partBeat + partBeats) * samplesPerBeat));
            for (int i = from; i < to; ++i)
                for (int ch = 0; ch < 2; ++ch)
                    out.setSample (ch, i, sampleAt (audio, beats, ch, i / samplesPerBeat - startBeat) * level);
            // A short fade where it stops, so it doesn't click.
            const int fade = juce::jmin (96, (to - from) / 4);
            if (fade > 0 && to < total)
                for (int ch = 0; ch < 2; ++ch)
                    out.applyGainRamp (ch, to - fade, fade, 1.0f, 0.0f);
        }
        partBeat += partBeats;
    }
    if (withEffects)
        applyEffects (engine, layer, bpm, rate, out, nullptr);
    return out;
}

int RigExport::timelineBars (RigEngine& engine)
{
    return (int) std::ceil (engine.getTimeline().lengthBars() - 1.0e-6);
}

juce::AudioBuffer<float> RigExport::renderTimelineStem (RigEngine& engine, int layer, double bpm, double rate, bool withEffects)
{
    return renderTimelineRange (engine, layer, bpm, rate, withEffects, 0.0, (double) timelineBars (engine), 0.0);
}

juce::AudioBuffer<float> RigExport::renderTimelineRange (RigEngine& engine, int layer, double bpm, double rate, bool withEffects,
                                                         double fromBar, double toBar, double tailSeconds)
{
    const auto& timeline = engine.getTimeline();
    const auto audio = engine.getLooper().copyLayerAudio (layer);
    const double beats = engine.getLooper().getLayer (layer).lengthBeats.load();
    const int lane = RigTimeline::laneForLoop (layer);
    if (timeline.isEmpty() || audio.getNumSamples() == 0 || beats <= 0 || toBar <= fromBar)
        return {};
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int body = (int) std::round ((toBar - fromBar) * bpb * samplesPerBeat);
    const int total = body + juce::jmax (0, (int) std::round (tailSeconds * rate));
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    const float level = withEffects ? engine.getLooper().getLayer (layer).level.load() : 1.0f;
    const auto& blocks = timeline.getBlocks();
    const bool used = timeline.laneUsed (lane);
    // Like the Rig: inside a block the loop's top is where the block puts it (its slip), and it
    // moves smoothly to a new level (a few milliseconds where a block starts or stops without a
    // fade, so it can't click).
    const float slew = 1.0f / 96.0f;
    float gain = 0;
    for (int i = 0; i < body; ++i)
    {
        const double beat = fromBar * bpb + i / samplesPerBeat;
        const double bar = beat / bpb;
        const float target = RigTimeline::gainAt (blocks.data(), (int) blocks.size(), lane, bar, used);
        gain = target > gain ? juce::jmin (target, gain + slew) : juce::jmax (target, gain - slew);
        if (gain <= 0.0f)
            continue;
        const double origin = RigTimeline::blockOriginAt (blocks.data(), (int) blocks.size(), lane, bar);
        const double from = origin > -1.0e8 ? origin * bpb : 0.0;
        for (int ch = 0; ch < 2; ++ch)
            out.setSample (ch, i, sampleAt (audio, beats, ch, beat - from) * level * gain);
    }
    if (withEffects)
        applyEffects (engine, layer, bpm, rate, out, nullptr);
    return out;
}

juce::AudioBuffer<float> RigExport::renderLoopPass (RigEngine& engine, int layer, double bpm, double rate, bool withEffects, double bars,
                                                    double tailSeconds)
{
    const auto one = renderLoop (engine, layer, bpm, rate, false);
    if (one.getNumSamples() == 0)
        return {};
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    const double samplesPerBeat = rate * 60.0 / bpm;
    const int body = (int) std::round (bars * bpb * samplesPerBeat);
    const int total = body + juce::jmax (0, (int) std::round (tailSeconds * rate));
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    const float level = withEffects ? engine.getLooper().getLayer (layer).level.load() : 1.0f;
    for (int i = 0; i < body; ++i)
        for (int ch = 0; ch < 2; ++ch)
            out.setSample (ch, i, one.getSample (juce::jmin (ch, one.getNumChannels() - 1), i % one.getNumSamples()) * level);
    if (withEffects)
    {
        // warmed up with a pass, so its echoes carry round into bar 1 as they do when it plays
        juce::AudioBuffer<float> warm (2, one.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            warm.copyFrom (ch, 0, one, juce::jmin (ch, one.getNumChannels() - 1), 0, one.getNumSamples());
        warm.applyGain (level);
        applyEffects (engine, layer, bpm, rate, out, &warm);
    }
    return out;
}

int RigExport::ccForLane (int lane)
{
    static const std::vector<int> free = []
    {
        std::vector<int> list;
        for (int cc = 102; cc <= 119; ++cc) list.push_back (cc);
        for (int cc = 20; cc <= 31; ++cc) list.push_back (cc);
        for (int cc = 85; cc <= 90; ++cc) list.push_back (cc);
        for (int cc : { 3, 9, 14, 15 }) list.push_back (cc);
        return list;
    }();
    return juce::isPositiveAndBelow (lane, (int) free.size()) ? free[(size_t) lane] : -1;
}

juce::MidiFile RigExport::automationMidi (RigEngine& engine, int bars, double bpm)
{
    const int ppq = 960;
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    juce::MidiFile midi;
    midi.setTicksPerQuarterNote (ppq);
    const double endBeat = juce::jmax (1, bars) * (double) bpb;
    {
        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
        tempo.addEvent (juce::MidiMessage::endOfTrack(), endBeat * ppq);
        midi.addTrack (tempo);
    }
    auto& automation = engine.getAutomation();
    const auto lanes = automation.getLanes();
    for (int i = 0; i < (int) lanes.size(); ++i)
    {
        const int cc = ccForLane (i);
        if (cc < 0)
            break;
        const auto& lane = lanes[(size_t) i];
        juce::MidiMessageSequence track;
        track.addEvent (juce::MidiMessage::textMetaEvent (3, automation.laneName (lane) + " (CC " + juce::String (cc) + ")"), 0);
        int last = -1;
        // A value every 1/32 beat where it changes.
        for (double beat = 0; beat < endBeat; beat += 1.0 / 32.0)
        {
            const double at = lane.positionAt (beat);
            if (at < 0 || ! lane.on)
                continue;
            const int value = juce::jlimit (0, 127, juce::roundToInt (lane.valueAt (at) * 127.0f));
            if (value == last)
                continue;
            track.addEvent (juce::MidiMessage::controllerEvent (1, cc, value), beat * ppq);
            last = value;
        }
        track.addEvent (juce::MidiMessage::endOfTrack(), endBeat * ppq);
        track.updateMatchedPairs();
        midi.addTrack (track);
    }
    return midi;
}

RigExport::Result RigExport::exportForLogic (RigEngine& engine, const juce::File& parent)
{
    Result result;
    const double rate = engine.getSampleRate() > 0 ? engine.getSampleRate() : 48000.0;
    const double bpm = engine.getTransport().getBpm();
    const int bpb = juce::jmax (1, engine.getTransport().getBeatsPerBar());
    bool anyLoop = false;
    for (int i = 0; i < RigLooper::kLayers; ++i)
        anyLoop = anyLoop || hasLoop (engine, i);
    const int lanes = engine.getAutomation().getNumLanes();
    if (! anyLoop && lanes == 0)
    {
        result.error = "There are no loops (or knob moves) to export yet.";
        return result;
    }

    result.folder = parent.getNonexistentChildFile ("Rig export " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M"), {}, false);
    if (! result.folder.createDirectory())
    {
        result.error = "Couldn't make the folder " + result.folder.getFullPathName();
        return result;
    }
    const juce::String tempoText = juce::String (bpm, std::abs (bpm - std::round (bpm)) < 0.05 ? 0 : 1) + " BPM";
    bool ok = true;

    const auto loops = result.folder.getChildFile ("Loops");
    loops.createDirectory();
    const auto dryLoops = loops.getChildFile ("Dry");
    for (int i = 0; i < RigLooper::kLayers; ++i)
    {
        if (! hasLoop (engine, i))
            continue;
        const double beats = engine.getLooper().getLayer (i).lengthBeats.load();
        const double bars = beats / bpb;
        const juce::String name = "Loop " + juce::String (i + 1) + " - " + safeName (RigLooper::sourceName (engine.getLooper().getLayer (i).source.load()))
                                + " - " + juce::String (bars, std::abs (bars - std::round (bars)) < 0.01 ? 0 : 2) + " bars - " + tempoText;
        ok = writeWav (loops.getChildFile (name + ".wav"), renderLoop (engine, i, bpm, rate, true), rate) && ok;
        dryLoops.createDirectory();
        ok = writeWav (dryLoops.getChildFile (name + " (dry).wav"), renderLoop (engine, i, bpm, rate, false), rate) && ok;
        result.files += 2;
    }

    const auto& parts = engine.getScenes().getScenes();
    const auto& timeline = engine.getTimeline();
    const bool useTimeline = ! timeline.isEmpty();
    if (useTimeline)
    {
        const auto song = result.folder.getChildFile ("Song");
        song.createDirectory();
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            if (! hasLoop (engine, i) || ! timeline.laneUsed (RigTimeline::laneForLoop (i)))
                continue;
            ok = writeWav (song.getChildFile ("Song - Loop " + juce::String (i + 1) + " - " + tempoText + ".wav"),
                           renderTimelineStem (engine, i, bpm, rate, true), rate) && ok;
            ++result.files;
        }
        juce::MidiMessageSequence track;
        const int ppq = 960;
        track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
        for (const auto& m : timeline.getMarkers())
            track.addEvent (juce::MidiMessage::textMetaEvent (6, m.name), m.bar * bpb * ppq);
        track.addEvent (juce::MidiMessage::endOfTrack(), timelineBars (engine) * bpb * ppq);
        track.sort();
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ppq);
        midi.addTrack (track);
        if (juce::FileOutputStream stream (song.getChildFile ("Song markers.mid")); stream.openedOk())
            ok = midi.writeTo (stream) && ok;
        ++result.files;
    }
    else if (! parts.empty())
    {
        const auto song = result.folder.getChildFile ("Song");
        song.createDirectory();
        for (int i = 0; i < RigLooper::kLayers; ++i)
        {
            if (! hasLoop (engine, i))
                continue;
            ok = writeWav (song.getChildFile ("Song - Loop " + juce::String (i + 1) + " - " + tempoText + ".wav"),
                           renderSongStem (engine, i, bpm, rate, true), rate) && ok;
            ++result.files;
        }

        // Tempo and part markers, for Logic.
        juce::MidiMessageSequence track;
        const int ppq = 960;
        track.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::round (60000000.0 / bpm)), 0);
        track.addEvent (juce::MidiMessage::timeSignatureMetaEvent (bpb, 4), 0);
        double beat = 0;
        for (const auto& part : parts)
        {
            auto marker = juce::MidiMessage::textMetaEvent (6, part.name);
            track.addEvent (marker, beat * ppq);
            beat += (part.bars > 0 ? part.bars : kHoldBars) * bpb;
        }
        track.addEvent (juce::MidiMessage::endOfTrack(), beat * ppq);
        juce::MidiFile midi;
        midi.setTicksPerQuarterNote (ppq);
        midi.addTrack (track);
        const auto midiFile = song.getChildFile ("Song markers.mid");
        if (juce::FileOutputStream stream (midiFile); stream.openedOk())
            ok = midi.writeTo (stream) && ok;
        ++result.files;
    }

    juce::String notes;
    notes << "Exported from Kaleidobox, " << juce::Time::getCurrentTime().toString (true, true) << "\n\n"
          << "Tempo: " << tempoText << ", " << bpb << "/4\n\n"
          << "Set Logic's project to this tempo, then drag the files in at bar 1. Every file starts on bar 1.\n"
          << "Loops: each loop once (as heard, and dry). Loop them in Logic.\n";
    if (useTimeline)
    {
        notes << "Song: each loop on the timeline, the whole song long, playing in its blocks (with their fades).\n"
              << "Song markers.mid: drag it in too for the tempo and the timeline's markers:\n";
        for (const auto& m : timeline.getMarkers())
            notes << "  bar " << juce::String (m.bar + 1.0, std::abs (m.bar - std::round (m.bar)) < 0.01 ? 0 : 2) << ": " << m.name << "\n";
        notes << "The tools (MPC, Bass, Synth, vocals, Input) play live, so they aren't in these files: in the Timeline, "
                 "Record song plays the song once and records every tool as its own WAV.\n";
    }
    else if (! parts.empty())
    {
        notes << "Song: each loop laid out over the whole song, playing in the parts where it's on.\n"
              << "Song markers.mid: drag it in too for the tempo and a marker for each part:\n";
        double bar = 1;
        for (const auto& part : parts)
        {
            notes << "  bar " << (int) bar << ": " << part.name << "\n";
            bar += part.bars > 0 ? part.bars : kHoldBars;
        }
        notes << "(A part set to hold is exported 4 bars long.)\n";
    }
    if (lanes > 0)
    {
        // The song's length, or the lanes' own if there's no song.
        int bars = useTimeline ? timelineBars (engine) : (! parts.empty() ? songBars (engine) : 0);
        if (bars <= 0)
            for (const auto& lane : engine.getAutomation().getLanes())
                bars = juce::jmax (bars, (int) std::ceil ((lane.loop ? lane.lengthBeats : lane.startBeat + lane.lengthBeats) / bpb - 1.0e-6));
        bars = juce::jmax (1, bars);
        const auto midi = automationMidi (engine, bars, bpm);
        if (juce::FileOutputStream stream (result.folder.getChildFile ("Automation.mid")); stream.openedOk())
            ok = midi.writeTo (stream) && ok;
        ++result.files;
        notes << "\nAutomation.mid: your knob moves (Rec moves), a track per setting, as MIDI CCs from bar 1, " << bars << " bars:\n";
        const auto list = engine.getAutomation().getLanes();
        for (int i = 0; i < (int) list.size(); ++i)
            if (const int cc = ccForLane (i); cc >= 0)
                notes << "  CC " << cc << ": " << engine.getAutomation().laneName (list[(size_t) i]) << (list[(size_t) i].on ? "" : " (off)") << "\n";
        notes << "In Logic, drag it onto the tracks you want and point each CC at a plugin's setting (or use it as a guide for Logic's own "
                 "automation). Anything recorded live from Kaleidobox (Record set, To Logic, a plugin's Rec) already has the moves in it.\n";
    }
    notes << "\nYour own plugins on a loop aren't in these files; use Record set for those.\n";
    ok = result.folder.getChildFile ("Read me.txt").replaceWithText (notes) && ok;
    if (! ok)
        result.error = "Some files couldn't be written.";
    return result;
}
